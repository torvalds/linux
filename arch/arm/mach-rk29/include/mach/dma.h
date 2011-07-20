/* arch/arm/plat-samsung/include/plat/dma.h
 *
 * Copyright (C) 2003-2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung rk29 DMA support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

enum rk29_dma_buffresult {
	RK29_RES_OK,
	RK29_RES_ERR,
	RK29_RES_ABORT
};

enum rk29_dmasrc {
	RK29_DMASRC_HW,		/* source is memory */
	RK29_DMASRC_MEM,		/* source is hardware */
	RK29_DMASRC_MEMTOMEM
};

/* enum rk29_chan_op
 *
 * operation codes passed to the DMA code by the user, and also used
 * to inform the current channel owner of any changes to the system state
*/

enum rk29_chan_op {
	RK29_DMAOP_START,
	RK29_DMAOP_STOP,
	RK29_DMAOP_PAUSE,
	RK29_DMAOP_RESUME,
	RK29_DMAOP_FLUSH,
	RK29_DMAOP_TIMEOUT,		/* internal signal to handler */
	RK29_DMAOP_STARTED,		/* indicate channel started */
};

struct rk29_dma_client {
	char                *name;
};

/* rk29_dma_cbfn_t
 *
 * buffer callback routine type
*/

typedef void (*rk29_dma_cbfn_t)(void *buf, int size,
				   enum rk29_dma_buffresult result);

typedef int  (*rk29_dma_opfn_t)(enum rk29_chan_op );



/* rk29_dma_request
 *
 * request a dma channel exclusivley
*/

extern int rk29_dma_request(unsigned int channel,
			       struct rk29_dma_client *, void *dev);


/* rk29_dma_ctrl
 *
 * change the state of the dma channel
*/

extern int rk29_dma_ctrl(unsigned int channel, enum rk29_chan_op op);

/* rk29_dma_setflags
 *
 * set the channel's flags to a given state
*/

extern int rk29_dma_setflags(unsigned int channel,
				unsigned int flags);

/* rk29_dma_free
 *
 * free the dma channel (will also abort any outstanding operations)
*/

extern int rk29_dma_free(unsigned int channel, struct rk29_dma_client *);

/* rk29_dma_enqueue
 *
 * place the given buffer onto the queue of operations for the channel.
 * The buffer must be allocated from dma coherent memory, or the Dcache/WB
 * drained before the buffer is given to the DMA system.
*/

extern int rk29_dma_enqueue(unsigned int channel, void *id,
			       dma_addr_t data, int size);

/* rk29_dma_config
 *
 * configure the dma channel
*/

extern int rk29_dma_config(unsigned int channel, int xferunit, int brst_len);

/* rk29_dma_devconfig
 *
 * configure the device we're talking to
*/

extern int rk29_dma_devconfig(unsigned int channel,
		enum rk29_dmasrc source, unsigned long devaddr);

/* rk29_dma_getposition
 *
 * get the position that the dma transfer is currently at
*/

extern int rk29_dma_getposition(unsigned int channel,
				   dma_addr_t *src, dma_addr_t *dest);

extern int rk29_dma_set_opfn(unsigned int, rk29_dma_opfn_t rtn);
extern int rk29_dma_set_buffdone_fn(unsigned int, rk29_dma_cbfn_t rtn);


