/*******************************************************************
 * 
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description: 
 *
 *  Author: Herbert.hu 
 *  Created: 07/26 2011
 *
 *******************************************************************/

#ifndef __PCMENC_STREAM_H__
#define __PCMENC_STREAM_H__
#include <asm/uaccess.h>


typedef struct pcm51_encoded_info_s
{ 
    unsigned int InfoValidFlag;
	unsigned int SampFs;
	unsigned int NumCh;
	unsigned int AcMode;
	unsigned int LFEFlag;
	unsigned int BitsPerSamp;
}pcm51_encoded_info_t;

#define AUDIODSP_PCMENC_GET_RING_BUF_SIZE      _IOR('l', 0x01, unsigned long)
#define AUDIODSP_PCMENC_GET_RING_BUF_CONTENT   _IOR('l', 0x02, unsigned long)
#define AUDIODSP_PCMENC_GET_RING_BUF_SPACE     _IOR('l', 0x03, unsigned long)
#define AUDIODSP_PCMENC_SET_RING_BUF_RPTR	   _IOW('l', 0x04, unsigned long)	
#define AUDIODSP_PCMENC_GET_PCMINFO	   	   _IOR('l', 0x05, unsigned long)	

/* initialize  stream FIFO 
 * return value: on success, zero is returned, on error, -1 is returned
 * */
extern int pcmenc_stream_init(void);

/* return space of  stream FIFO, unit:byte
 * */
extern int pcmenc_stream_space(void);

/* return content of  stream FIFO, unit:byte
 * */
extern int pcmenc_stream_content(void);

/* deinit  stream  FIFO 
 * return value: on success, zero is returned, on error, -1 is returned
 * */
extern int pcmenc_stream_deinit(void);

/* read  data out of FIFO, the minimum of the FIFO's content and size will be read, if the FIFO is empty, read will be failed 
 * return value: on success, the number of bytes read are returned, othewise, 0 is returned
 * */
extern int  pcmenc_stream_read(char __user *buffer, int size);


#endif

