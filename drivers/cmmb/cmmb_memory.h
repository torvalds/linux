#ifndef _CMMB_AV_MEMORY_H_
#define _CMMB_AV_MEMORY_H_

#include <linux/module.h>
#include <linux/interrupt.h>
//#include <asm/semaphore.h>
#include <linux/mutex.h>
#include "cmmb_ringbuffer.h"


#define   CMMB_MEMO_WRITE          (0x80000001)
#define   CMMB_MEMO_READ           (0x80000002)
#define   CMMB_MEMO_FLUSH_ONE      (0x80000003)
#define   CMMB_MEMO_FLUSH_ALL      (0x80000004)
#define   CMMB_MEMO_INIT           (0x80000005)
#define   CMMB_SET_VIDEO_TYPE      (0x80000006)
#define   CMMB_SET_AUDIO_TYPE      (0x80000007)
#define   CMMB_SET_AUDIO_SAMPLE    (0x80000008)
#define   CMMB_GET_VIDEO_TYPE      (0x80000009)
#define   CMMB_GET_AUDIO_TYPE      (0x8000000a)
#define   CMMB_GET_AUDIO_SAMPLE    (0x8000000b)
#define   CMMB_GET_BUFF_FREE       (0x8000000c)
#define   CMMB_GET_BUFF_AVAIL      (0x8000000d)


struct cmmb_memory
{
    int  w_datatype;
    int  r_datatype;
    unsigned long  videotype;
    unsigned long  audiotype;
    unsigned long  audiosample;
    int usr;

    struct device *device;
    struct file_operations* fops;
    struct dvb_ringbuffer  buffer_Video;
    struct dvb_ringbuffer  buffer_Audio;
    struct dvb_ringbuffer  buffer_Data;
    u8 *video_buf; 
    u8 *audio_buf; 
    u8 *data_buf; 
    
    #define  CMMB_VIDEO_TYPE     0
    #define  CMMB_AUDIO_TYPE     1
    #define  CMMB_DATA_TYPE      2
    #define  CMMB_NULL_TYPE      3
    
    #define CMMB_VIDEO_BUFFER_SIZE (512*1024)
    #define CMMB_AUDIO_BUFFER_SIZE (64*1024)
    #define CMMB_DATA_BUFFER_SIZE  (1*1024)

	struct mutex mutex;
    //struct semaphore sem;
	spinlock_t lock;
    wait_queue_head_t rqueue;
    void* priv;
};



#endif/*_CMMBMEMORY_H_*/


