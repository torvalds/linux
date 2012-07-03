/* linux/include/asm/hardware/pl330.h
 *
 * Copyright (C) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __PL330_CORE_H
#define __PL330_CORE_H

#define PL330_MAX_CHAN		8
#define PL330_MAX_IRQS		32
#define PL330_MAX_PERI		32

enum pl330_srccachectrl {
	SCCTRL0 = 0, /* Noncacheable and nonbufferable */
	SCCTRL1, /* Bufferable only */
	SCCTRL2, /* Cacheable, but do not allocate */
	SCCTRL3, /* Cacheable and bufferable, but do not allocate */
	SINVALID1,
	SINVALID2,
	SCCTRL6, /* Cacheable write-through, allocate on reads only */
	SCCTRL7, /* Cacheable write-back, allocate on reads only */
};

enum pl330_dstcachectrl {
	DCCTRL0 = 0, /* Noncacheable and nonbufferable */
	DCCTRL1, /* Bufferable only */
	DCCTRL2, /* Cacheable, but do not allocate */
	DCCTRL3, /* Cacheable and bufferable, but do not allocate */
	DINVALID1 = 8,
	DINVALID2,
	DCCTRL6, /* Cacheable write-through, allocate on writes only */
	DCCTRL7, /* Cacheable write-back, allocate on writes only */
};

/* Populated by the PL330 core driver for DMA API driver's info */
struct pl330_config {
	u32	periph_id;
	u32	pcell_id;
#define DMAC_MODE_NS	(1 << 0)
	unsigned int	mode;
	unsigned int	data_bus_width:10; /* In number of bits */
	unsigned int	data_buf_dep:10;
	unsigned int	num_chan:4;
	unsigned int	num_peri:6;
	u32		peri_ns;
	unsigned int	num_events:6;
	u32		irq_ns;
};

/* Handle to the DMAC provided to the PL330 core */
struct pl330_info {
	/* Owning device */
	struct device *dev;
	/* Size of MicroCode buffers for each channel. */
	unsigned mcbufsz;
	/* ioremap'ed address of PL330 registers. */
	void __iomem	*base;
	/* Client can freely use it. */
	void	*client_data;
	/* PL330 core data, Client must not touch it. */
	void	*pl330_data;
	/* Populated by the PL330 core driver during pl330_add */
	struct pl330_config	pcfg;
	/*
	 * If the DMAC has some reset mechanism, then the
	 * client may want to provide pointer to the method.
	 */
	void (*dmac_reset)(struct pl330_info *pi);
};

enum pl330_byteswap {
	SWAP_NO = 0,
	SWAP_2,
	SWAP_4,
	SWAP_8,
	SWAP_16,
};

/**
 * Request Configuration.
 * The PL330 core does not modify this and uses the last
 * working configuration if the request doesn't provide any.
 *
 * The Client may want to provide this info only for the
 * first request and a request with new settings.
 */
struct pl330_reqcfg {
	/* Address Incrementing */
	unsigned dst_inc:1;
	unsigned src_inc:1;

	/*
	 * For now, the SRC & DST protection levels
	 * and burst size/length are assumed same.
	 */
	bool nonsecure;
	bool privileged;
	bool insnaccess;
	unsigned brst_len:5;
	unsigned brst_size:3; /* in power of 2 */

	enum pl330_dstcachectrl dcctl;
	enum pl330_srccachectrl scctl;
	enum pl330_byteswap swap;
};

/*
 * One cycle of DMAC operation.
 * There may be more than one xfer in a request.
 */
struct pl330_xfer {
	u32 src_addr;
	u32 dst_addr;
	/* Size to xfer */
	u32 bytes;
	/*
	 * Pointer to next xfer in the list.
	 * The last xfer in the req must point to NULL.
	 */
	struct pl330_xfer *next;
};

/* The xfer callbacks are made with one of these arguments. */
enum pl330_op_err {
	/* The all xfers in the request were success. */
	PL330_ERR_NONE,
	/* If req aborted due to global error. */
	PL330_ERR_ABORT,
	/* If req failed due to problem with Channel. */
	PL330_ERR_FAIL,
};

enum pl330_reqtype {
	MEMTOMEM,
	MEMTODEV,
	DEVTOMEM,
	DEVTODEV,
};

/* A request defining Scatter-Gather List ending with NULL xfer. */
struct pl330_req {
	enum pl330_reqtype rqtype;
	/* Index of peripheral for the xfer. */
	unsigned peri:5;
	/* Unique token for this xfer, set by the client. */
	void *token;
	/* Callback to be called after xfer. */
	void (*xfer_cb)(void *token, enum pl330_op_err err);
	/* If NULL, req will be done at last set parameters. */
	struct pl330_reqcfg *cfg;
	/* Pointer to first xfer in the request. */
	struct pl330_xfer *x;
};

/*
 * To know the status of the channel and DMAC, the client
 * provides a pointer to this structure. The PL330 core
 * fills it with current information.
 */
struct pl330_chanstatus {
	/*
	 * If the DMAC engine halted due to some error,
	 * the client should remove-add DMAC.
	 */
	bool dmac_halted;
	/*
	 * If channel is halted due to some error,
	 * the client should ABORT/FLUSH and START the channel.
	 */
	bool faulting;
	/* Location of last load */
	u32 src_addr;
	/* Location of last store */
	u32 dst_addr;
	/*
	 * Pointer to the currently active req, NULL if channel is
	 * inactive, even though the requests may be present.
	 */
	struct pl330_req *top_req;
	/* Pointer to req waiting second in the queue if any. */
	struct pl330_req *wait_req;
};

enum pl330_chan_op {
	/* Start the channel */
	PL330_OP_START,
	/* Abort the active xfer */
	PL330_OP_ABORT,
	/* Stop xfer and flush queue */
	PL330_OP_FLUSH,
};

extern int pl330_add(struct pl330_info *);
extern void pl330_del(struct pl330_info *pi);
extern int pl330_update(const struct pl330_info *pi);
extern void pl330_release_channel(void *ch_id);
extern void *pl330_request_channel(int id, const struct pl330_info *pi);
extern int pl330_chan_status(void *ch_id, struct pl330_chanstatus *pstatus);
extern int pl330_chan_ctrl(void *ch_id, enum pl330_chan_op op);
extern int pl330_submit_req(void *ch_id, struct pl330_req *r);

#endif	/* __PL330_CORE_H */
