/*
 * Texas Instruments CPDMA Driver
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __DAVINCI_CPDMA_H__
#define __DAVINCI_CPDMA_H__

#define CPDMA_MAX_CHANNELS	BITS_PER_LONG

#define tx_chan_num(chan)	(chan)
#define rx_chan_num(chan)	((chan) + CPDMA_MAX_CHANNELS)
#define is_rx_chan(chan)	((chan)->chan_num >= CPDMA_MAX_CHANNELS)
#define is_tx_chan(chan)	(!is_rx_chan(chan))
#define __chan_linear(chan_num)	((chan_num) & (CPDMA_MAX_CHANNELS - 1))
#define chan_linear(chan)	__chan_linear((chan)->chan_num)

struct cpdma_params {
	struct device		*dev;
	void __iomem		*dmaregs;
	void __iomem		*txhdp, *rxhdp, *txcp, *rxcp;
	void __iomem		*rxthresh, *rxfree;
	int			num_chan;
	bool			has_soft_reset;
	int			min_packet_size;
	u32			desc_mem_phys;
	int			desc_mem_size;
	int			desc_align;

	/*
	 * Some instances of embedded cpdma controllers have extra control and
	 * status registers.  The following flag enables access to these
	 * "extended" registers.
	 */
	bool			has_ext_regs;
};

struct cpdma_chan_stats {
	u32			head_enqueue;
	u32			tail_enqueue;
	u32			pad_enqueue;
	u32			misqueued;
	u32			desc_alloc_fail;
	u32			pad_alloc_fail;
	u32			runt_receive_buff;
	u32			runt_transmit_buff;
	u32			empty_dequeue;
	u32			busy_dequeue;
	u32			good_dequeue;
	u32			requeue;
	u32			teardown_dequeue;
};

struct cpdma_ctlr;
struct cpdma_chan;

typedef void (*cpdma_handler_fn)(void *token, int len, int status);

struct cpdma_ctlr *cpdma_ctlr_create(struct cpdma_params *params);
int cpdma_ctlr_destroy(struct cpdma_ctlr *ctlr);
int cpdma_ctlr_start(struct cpdma_ctlr *ctlr);
int cpdma_ctlr_stop(struct cpdma_ctlr *ctlr);
int cpdma_ctlr_dump(struct cpdma_ctlr *ctlr);

struct cpdma_chan *cpdma_chan_create(struct cpdma_ctlr *ctlr, int chan_num,
				     cpdma_handler_fn handler);
int cpdma_chan_destroy(struct cpdma_chan *chan);
int cpdma_chan_start(struct cpdma_chan *chan);
int cpdma_chan_stop(struct cpdma_chan *chan);
int cpdma_chan_dump(struct cpdma_chan *chan);

int cpdma_chan_get_stats(struct cpdma_chan *chan,
			 struct cpdma_chan_stats *stats);
int cpdma_chan_submit(struct cpdma_chan *chan, void *token, void *data,
		      int len, gfp_t gfp_mask);
int cpdma_chan_process(struct cpdma_chan *chan, int quota);

int cpdma_ctlr_int_ctrl(struct cpdma_ctlr *ctlr, bool enable);
void cpdma_ctlr_eoi(struct cpdma_ctlr *ctlr);
int cpdma_chan_int_ctrl(struct cpdma_chan *chan, bool enable);

enum cpdma_control {
	CPDMA_CMD_IDLE,			/* write-only */
	CPDMA_COPY_ERROR_FRAMES,	/* read-write */
	CPDMA_RX_OFF_LEN_UPDATE,	/* read-write */
	CPDMA_RX_OWNERSHIP_FLIP,	/* read-write */
	CPDMA_TX_PRIO_FIXED,		/* read-write */
	CPDMA_STAT_IDLE,		/* read-only */
	CPDMA_STAT_TX_ERR_CHAN,		/* read-only */
	CPDMA_STAT_TX_ERR_CODE,		/* read-only */
	CPDMA_STAT_RX_ERR_CHAN,		/* read-only */
	CPDMA_STAT_RX_ERR_CODE,		/* read-only */
	CPDMA_RX_BUFFER_OFFSET,		/* read-write */
};

int cpdma_control_get(struct cpdma_ctlr *ctlr, int control);
int cpdma_control_set(struct cpdma_ctlr *ctlr, int control, int value);

#endif
