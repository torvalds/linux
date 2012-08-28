/*
 * Copyright (C) 2010 RockChip Electronics Co. Ltd.
 *	ZhenFu Fang <fzf@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef	__RK29_DMA_PL330_H_
#define	__RK29_DMA_PL330_H_

#define RK29_DMAF_AUTOSTART		(1 << 0)
#define RK29_DMAF_CIRCULAR		(1 << 1)

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

/*
 * PL330 can assign any channel to communicate with
 * any of the peripherals attched to the DMAC.
 * For the sake of consistency across client drivers,
 * We keep the channel names unchanged and only add
 * missing peripherals are added.
 * Order is not important since rk PL330 API driver
 * use these just as IDs.
 */
enum dma_ch {

	DMACH_UART0_TX,
	DMACH_UART0_RX,
	DMACH_UART1_TX,
	DMACH_UART1_RX,
	DMACH_I2S0_8CH_TX,
	DMACH_I2S0_8CH_RX,
	DMACH_I2S1_2CH_TX,
	DMACH_I2S1_2CH_RX,
	DMACH_SPDIF_TX,
	DMACH_I2S2_2CH_TX,
	DMACH_I2S2_2CH_RX,

	DMACH_HSADC,
	DMACH_SDMMC,
	DMACH_SDIO,
	DMACH_EMMC,
	DMACH_PID_FILTER,
	DMACH_UART2_TX,
	DMACH_UART2_RX,
	DMACH_UART3_TX,
	DMACH_UART3_RX,
	DMACH_SPI0_TX,
	DMACH_SPI0_RX,
	DMACH_SPI1_TX,
	DMACH_SPI1_RX,
	DMACH_DMAC1_MEMTOMEM,
	DMACH_DMAC2_MEMTOMEM,
	/* END Marker, also used to denote a reserved channel */
	DMACH_MAX,
};

static inline bool rk29_dma_has_circular(void)
{
	return true;
}
static inline bool rk29_dma_has_infiniteloop(void)
{
	return true;
}
/*
 * Every PL330 DMAC has max 32 peripheral interfaces,
 * of which some may be not be really used in your
 * DMAC's configuration.
 * Populate this array of 32 peri i/fs with relevant
 * channel IDs for used peri i/f and DMACH_MAX for
 * those unused.
 *
 * The platforms just need to provide this info
 * to the rk DMA API driver for PL330.
 */
struct rk29_pl330_platdata {
	enum dma_ch peri[32];
};



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

/* rk29_dma_enqueue_ring
 *
 * place the given buffer onto the queue of operations for the channel.
 * The buffer must be allocated from dma coherent memory, or the Dcache/WB
 * drained before the buffer is given to the DMA system.
*/

extern int rk29_dma_enqueue_ring(enum dma_ch channel, void *id,
			       dma_addr_t data, int size, int numofblock, bool sev);

/* rk29_dma_enqueue
 *
 * place the given buffer onto the queue of operations for the channel.
 * The buffer must be allocated from dma coherent memory, or the Dcache/WB
 * drained before the buffer is given to the DMA system.
*/

extern int rk29_dma_enqueue(enum dma_ch channel, void *id,
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

#endif	/* __RK29_DMA_PL330_H_ */
