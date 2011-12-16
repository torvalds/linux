/* arch/arm/plat-samsung/include/plat/dma.h
 *
 * Copyright (C) 2003-2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C DMA support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/dma-mapping.h>

enum s3c2410_dma_buffresult {
	S3C2410_RES_OK,
	S3C2410_RES_ERR,
	S3C2410_RES_ABORT
};

/* enum s3c2410_chan_op
 *
 * operation codes passed to the DMA code by the user, and also used
 * to inform the current channel owner of any changes to the system state
*/

enum s3c2410_chan_op {
	S3C2410_DMAOP_START,
	S3C2410_DMAOP_STOP,
	S3C2410_DMAOP_PAUSE,
	S3C2410_DMAOP_RESUME,
	S3C2410_DMAOP_FLUSH,
	S3C2410_DMAOP_TIMEOUT,		/* internal signal to handler */
	S3C2410_DMAOP_STARTED,		/* indicate channel started */
};

struct s3c2410_dma_client {
	char                *name;
};

struct s3c2410_dma_chan;
enum dma_ch;

/* s3c2410_dma_cbfn_t
 *
 * buffer callback routine type
*/

typedef void (*s3c2410_dma_cbfn_t)(struct s3c2410_dma_chan *,
				   void *buf, int size,
				   enum s3c2410_dma_buffresult result);

typedef int  (*s3c2410_dma_opfn_t)(struct s3c2410_dma_chan *,
				   enum s3c2410_chan_op );



/* s3c2410_dma_request
 *
 * request a dma channel exclusivley
*/

extern int s3c2410_dma_request(enum dma_ch channel,
			       struct s3c2410_dma_client *, void *dev);


/* s3c2410_dma_ctrl
 *
 * change the state of the dma channel
*/

extern int s3c2410_dma_ctrl(enum dma_ch channel, enum s3c2410_chan_op op);

/* s3c2410_dma_setflags
 *
 * set the channel's flags to a given state
*/

extern int s3c2410_dma_setflags(enum dma_ch channel,
				unsigned int flags);

/* s3c2410_dma_free
 *
 * free the dma channel (will also abort any outstanding operations)
*/

extern int s3c2410_dma_free(enum dma_ch channel, struct s3c2410_dma_client *);

/* s3c2410_dma_enqueue
 *
 * place the given buffer onto the queue of operations for the channel.
 * The buffer must be allocated from dma coherent memory, or the Dcache/WB
 * drained before the buffer is given to the DMA system.
*/

extern int s3c2410_dma_enqueue(enum dma_ch channel, void *id,
			       dma_addr_t data, int size);

/* s3c2410_dma_config
 *
 * configure the dma channel
*/

extern int s3c2410_dma_config(enum dma_ch channel, int xferunit);

/* s3c2410_dma_devconfig
 *
 * configure the device we're talking to
*/

extern int s3c2410_dma_devconfig(enum dma_ch channel,
		enum dma_data_direction source, unsigned long devaddr);

/* s3c2410_dma_getposition
 *
 * get the position that the dma transfer is currently at
*/

extern int s3c2410_dma_getposition(enum dma_ch channel,
				   dma_addr_t *src, dma_addr_t *dest);

extern int s3c2410_dma_set_opfn(enum dma_ch, s3c2410_dma_opfn_t rtn);
extern int s3c2410_dma_set_buffdone_fn(enum dma_ch, s3c2410_dma_cbfn_t rtn);


#include <plat/dma-ops.h>
