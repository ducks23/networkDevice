#include "packetdescriptor.h"
#include "destination.h"
#include "pid.h"
#include "BoundedBuffer.h"
#include "freepacketdescriptorstore.h"
#include "freepacketdescriptorstore__full.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "networkdevice.h"
#include "networkdevice__full.h"
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 5

FreePacketDescriptorStore *my_fpds;
NetworkDevice *mydevice;
BoundedBuffer *pool_buf;
BoundedBuffer *send_buf;
BoundedBuffer *rec_buf[MAX_PID+1];

static pthread_t send_p;
static pthread_t recieve_p;
typedef struct args{
    NetworkDevice *nd;
    BoundedBuffer *pool;
    BoundedBuffer *send;
    BoundedBuffer **rec;
    FreePacketDescriptorStore *fpds;    
}_Args;

static _Args Args;

int getPD(BoundedBuffer *pool, FreePacketDescriptorStore *fpds, PacketDescriptor **pd)
{
    int i;
    if((i = pool->nonblockingRead(pool, (void**)pd)) == 1){
        return 1;
    }
    if((i = fpds->nonblockingGet(fpds, pd)) == 1){
	return 1;
    }
    return 0;
}
void *send_thread(void *args){
    PacketDescriptor *packet = NULL;
    int i,result;  
    _Args *r_args = args;
    FreePacketDescriptorStore *fpds = r_args->fpds;
    NetworkDevice *nd = r_args->nd;
    BoundedBuffer *pool_buf = r_args->pool;
    BoundedBuffer *send_buf = r_args->send;
    while(1){
        send_buf->blockingRead(send_buf, (void **)&packet);
        for(i = 0; i < 5; i++){
	    result = nd->sendPacket(nd, packet);
	    if(result ==1){
		printf("SUCCESFULLY SENT PD TO DEVICE\n");
		i = 5;
	    }
	    else{printf("FAILED TO SEND PD TO DEVICE\n");
	    }

	}
	result = pool_buf->nonblockingWrite(pool_buf, packet);
	if(result == 0){
	    printf("FAILED TO WRITE TO POOL_BUF, sending back to fpds\n");
	    result = fpds->nonblockingPut(fpds, packet);
	}


    }
    return NULL;
}
void *recieve_thread(void *args){
    int get, write;
    PID pid;
    _Args *r_args = args;
    FreePacketDescriptorStore *fpds = r_args->fpds;
    NetworkDevice *nd = r_args->nd;
    BoundedBuffer *pool_buf = r_args->pool;
    BoundedBuffer **rec_buf =  r_args->rec;
    PacketDescriptor *PD = NULL;
    PacketDescriptor *temp = NULL;
    r_args->fpds->blockingGet(r_args->fpds, &PD);
    initPD(PD);
    mydevice->registerPD(mydevice, PD);
    while(1){
    printf("inside recieve thread");
	nd->awaitIncomingPacket(nd);
	get = getPD(pool_buf, fpds, &temp);
	if(get == 1){
	   pid = getPID(temp);
	   write = rec_buf[pid]->nonblockingWrite(rec_buf[pid], temp);
	   if(write == 0){
		fpds->nonblockingPut(fpds, PD);
		
		}
	   PD = temp;
	}
    initPD(PD);
    nd->registerPD(nd, PD);
    }
    return NULL;
}

void blocking_send_packet(PacketDescriptor *pd){
	send_buf->blockingWrite(send_buf, pd);
}
int  nonblocking_send_packet(PacketDescriptor *pd){
	if(send_buf->nonblockingWrite(send_buf, pd)){
	    return 1;
	}
	return 0;

}

void blocking_get_packet(PacketDescriptor **pd, PID pid){	 
	rec_buf[pid]->blockingRead(rec_buf[pid], (void**)pd);

}
int  nonblocking_get_packet(PacketDescriptor **pd, PID pid){
	if(rec_buf[pid]->nonblockingRead(rec_buf[pid], (void**)pd)){
	    return 1;
	}
	return 0;
}
/* These represent requests for packets by the application threads */
/* The nonblocking call must return promptly, with the result 1 if */
/* a packet was found (and the first argument set accordingly) or  */
/* 0 if no packet was waiting.                                     */
/* The blocking call only returns when a packet has been received  */
/* for the indicated process, and the first arg points at it.      */
/* Both calls indicate their process number and should only be     */
/* given appropriate packets. You may use a small bounded buffer   */
/* to hold packets that haven't yet been collected by a process,   */
/* but are also allowed to discard extra packets if at least one   */
/* is waiting uncollected for the same PID. i.e. applications must */
/* collect their packets reasonably promptly, or risk packet loss. */

void init_network_driver(NetworkDevice               *nd, 
                         void                        *mem_start, 
                         unsigned long               mem_length,
                         FreePacketDescriptorStore **fpds_ptr){
    
    int i;
    mydevice = nd;
    printf("mem length %ld\n", mem_length/sizeof(int));
    *fpds_ptr = FreePacketDescriptorStore_create(mem_start, mem_length);
    my_fpds = *fpds_ptr;
    send_buf = BoundedBuffer_create(BUFFER_SIZE);
    pool_buf = BoundedBuffer_create(BUFFER_SIZE);
    
    for (i = 0; i < MAX_PID + 1; i++){
        rec_buf[i] = BoundedBuffer_create(BUFFER_SIZE);
    }
    Args.nd = mydevice;
    Args.pool = pool_buf;
    Args.send = send_buf;
    Args.rec = rec_buf;
    Args.fpds = *fpds_ptr;    
    pthread_create(&send_p, NULL, send_thread, (void *)&Args);
    pthread_create(&recieve_p, NULL, recieve_thread, (void *)&Args);

    
}
/* Called before any other methods, to allow you to initialise */
/* data structures and start any internal threads.             */ 
/* Arguments:                                                  */
/*   nd: the NetworkDevice that you must drive,                */
/*   mem_start, mem_length: some memory for PacketDescriptors  */
/*   fpds_ptr: You hand back a FreePacketDescriptorStore into  */
/*             which you have put the divided up memory        */


