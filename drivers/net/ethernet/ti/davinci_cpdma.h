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

#define CPDMA_RX_SOURCE_PORT(__status__)	((__status__ >> 16) & 0x7)

#define CPDMA_RX_VLAN_ENCAP BIT(19)

#define CPDMA_EOI_RX_THRESH	0x0
#define CPDMA_EOI_RX		0x1
#define CPDMA_EOI_TX		0x2
#define CPDMA_EOI_MISC		0x3

struct cpdma_params {
	struct device		*dev;
	void __iomem		*dmaregs;
	void __iomem		*txhdp, *rxhdp, *txcp, *rxcp;
	void __iomem		*rxthresh, *rxfree;
	int			num_chan;
	bool			has_soft_reset;
	int			min_packet_size;
	u32			desc_mem_phys;
	u32			desc_hw_addr;
	int			desc_mem_size;
	int			desc_align;
	u32			bus_freq_mhz;
	u32			descs_pool_size;

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

struct cpdma_chan *cpdma_chan_create(struct cpdma_ctlr *ctlr, int chan_num,
				     cpdma_handler_fn handler, int rx_type);
int cpdma_chan_get_rx_buf_num(struct cpdma_chan *chan);
int cpdma_chan_destroy(struct cpdma_chan *chan);
int cpdma_chan_start(struct cpdma_chan *chan);
int cpdma_chan_stop(struct cpdma_chan *chan);

int cpdma_chan_get_stats(struct cpdma_chan *chan,
			 struct cpdma_chan_stats *stats);
int cpdma_chan_submit(struct cpdma_chan *chan, void *token, void *data,
		      int len, int directed);
int cpdma_chan_process(struct cpdma_chan *chan, int quota);

int cpdma_ctlr_int_ctrl(struct cpdma_ctlr *ctlr, bool enable);
void cpdma_ctlr_eoi(struct cpdma_ctlr *ctlr, u32 value);
int cpdma_chan_int_ctrl(struct cpdma_chan *chan, bool enable);
u32 cpdma_ctrl_rxchs_state(struct cpdma_ctlr *ctlr);
u32 cpdma_ctrl_txchs_state(struct cpdma_ctlr *ctlr);
bool cpdma_check_free_tx_desc(struct cpdma_chan *chan);
int cpdma_chan_set_weight(struct cpdma_chan *ch, int weight);
int cpdma_chan_set_rate(struct cpdma_chan *ch, u32 rate);
u32 cpdma_chan_get_rate(struct cpdma_chan *ch);
u32 cpdma_chan_get_min_rate(struct cpdma_ctlr *ctlr);

enum cpdma_control {
	CPDMA_TX_RLIM,			/* read-write */
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
int cpdma_get_num_rx_descs(struct cpdma_ctlr *ctlr);
void cpdma_set_num_rx_descs(struct cpdma_ctlr *ctlr, int num_rx_desc);
int cpdma_get_num_tx_descs(struct cpdma_ctlr *ctlr);
int cpdma_chan_split_pool(struct cpdma_ctlr *ctlr);

#endif
