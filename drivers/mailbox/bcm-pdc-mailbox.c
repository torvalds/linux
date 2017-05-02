/*
 * Copyright 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

/*
 * Broadcom PDC Mailbox Driver
 * The PDC provides a ring based programming interface to one or more hardware
 * offload engines. For example, the PDC driver works with both SPU-M and SPU2
 * cryptographic offload hardware. In some chips the PDC is referred to as MDE,
 * and in others the FA2/FA+ hardware is used with this PDC driver.
 *
 * The PDC driver registers with the Linux mailbox framework as a mailbox
 * controller, once for each PDC instance. Ring 0 for each PDC is registered as
 * a mailbox channel. The PDC driver uses interrupts to determine when data
 * transfers to and from an offload engine are complete. The PDC driver uses
 * threaded IRQs so that response messages are handled outside of interrupt
 * context.
 *
 * The PDC driver allows multiple messages to be pending in the descriptor
 * rings. The tx_msg_start descriptor index indicates where the last message
 * starts. The txin_numd value at this index indicates how many descriptor
 * indexes make up the message. Similar state is kept on the receive side. When
 * an rx interrupt indicates a response is ready, the PDC driver processes numd
 * descriptors from the tx and rx ring, thus processing one response at a time.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/brcm-message.h>
#include <linux/scatterlist.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>

#define PDC_SUCCESS  0

#define RING_ENTRY_SIZE   sizeof(struct dma64dd)

/* # entries in PDC dma ring */
#define PDC_RING_ENTRIES  512
/*
 * Minimum number of ring descriptor entries that must be free to tell mailbox
 * framework that it can submit another request
 */
#define PDC_RING_SPACE_MIN  15

#define PDC_RING_SIZE    (PDC_RING_ENTRIES * RING_ENTRY_SIZE)
/* Rings are 8k aligned */
#define RING_ALIGN_ORDER  13
#define RING_ALIGN        BIT(RING_ALIGN_ORDER)

#define RX_BUF_ALIGN_ORDER  5
#define RX_BUF_ALIGN	    BIT(RX_BUF_ALIGN_ORDER)

/* descriptor bumping macros */
#define XXD(x, max_mask)              ((x) & (max_mask))
#define TXD(x, max_mask)              XXD((x), (max_mask))
#define RXD(x, max_mask)              XXD((x), (max_mask))
#define NEXTTXD(i, max_mask)          TXD((i) + 1, (max_mask))
#define PREVTXD(i, max_mask)          TXD((i) - 1, (max_mask))
#define NEXTRXD(i, max_mask)          RXD((i) + 1, (max_mask))
#define PREVRXD(i, max_mask)          RXD((i) - 1, (max_mask))
#define NTXDACTIVE(h, t, max_mask)    TXD((t) - (h), (max_mask))
#define NRXDACTIVE(h, t, max_mask)    RXD((t) - (h), (max_mask))

/* Length of BCM header at start of SPU msg, in bytes */
#define BCM_HDR_LEN  8

/*
 * PDC driver reserves ringset 0 on each SPU for its own use. The driver does
 * not currently support use of multiple ringsets on a single PDC engine.
 */
#define PDC_RINGSET  0

/*
 * Interrupt mask and status definitions. Enable interrupts for tx and rx on
 * ring 0
 */
#define PDC_RCVINT_0         (16 + PDC_RINGSET)
#define PDC_RCVINTEN_0       BIT(PDC_RCVINT_0)
#define PDC_INTMASK	     (PDC_RCVINTEN_0)
#define PDC_LAZY_FRAMECOUNT  1
#define PDC_LAZY_TIMEOUT     10000
#define PDC_LAZY_INT  (PDC_LAZY_TIMEOUT | (PDC_LAZY_FRAMECOUNT << 24))
#define PDC_INTMASK_OFFSET   0x24
#define PDC_INTSTATUS_OFFSET 0x20
#define PDC_RCVLAZY0_OFFSET  (0x30 + 4 * PDC_RINGSET)
#define FA_RCVLAZY0_OFFSET   0x100

/*
 * For SPU2, configure MDE_CKSUM_CONTROL to write 17 bytes of metadata
 * before frame
 */
#define PDC_SPU2_RESP_HDR_LEN  17
#define PDC_CKSUM_CTRL         BIT(27)
#define PDC_CKSUM_CTRL_OFFSET  0x400

#define PDC_SPUM_RESP_HDR_LEN  32

/*
 * Sets the following bits for write to transmit control reg:
 * 11    - PtyChkDisable - parity check is disabled
 * 20:18 - BurstLen = 3 -> 2^7 = 128 byte data reads from memory
 */
#define PDC_TX_CTL		0x000C0800

/* Bit in tx control reg to enable tx channel */
#define PDC_TX_ENABLE		0x1

/*
 * Sets the following bits for write to receive control reg:
 * 7:1   - RcvOffset - size in bytes of status region at start of rx frame buf
 * 9     - SepRxHdrDescEn - place start of new frames only in descriptors
 *                          that have StartOfFrame set
 * 10    - OflowContinue - on rx FIFO overflow, clear rx fifo, discard all
 *                         remaining bytes in current frame, report error
 *                         in rx frame status for current frame
 * 11    - PtyChkDisable - parity check is disabled
 * 20:18 - BurstLen = 3 -> 2^7 = 128 byte data reads from memory
 */
#define PDC_RX_CTL		0x000C0E00

/* Bit in rx control reg to enable rx channel */
#define PDC_RX_ENABLE		0x1

#define CRYPTO_D64_RS0_CD_MASK   ((PDC_RING_ENTRIES * RING_ENTRY_SIZE) - 1)

/* descriptor flags */
#define D64_CTRL1_EOT   BIT(28)	/* end of descriptor table */
#define D64_CTRL1_IOC   BIT(29)	/* interrupt on complete */
#define D64_CTRL1_EOF   BIT(30)	/* end of frame */
#define D64_CTRL1_SOF   BIT(31)	/* start of frame */

#define RX_STATUS_OVERFLOW       0x00800000
#define RX_STATUS_LEN            0x0000FFFF

#define PDC_TXREGS_OFFSET  0x200
#define PDC_RXREGS_OFFSET  0x220

/* Maximum size buffer the DMA engine can handle */
#define PDC_DMA_BUF_MAX 16384

enum pdc_hw {
	FA_HW,		/* FA2/FA+ hardware (i.e. Northstar Plus) */
	PDC_HW		/* PDC/MDE hardware (i.e. Northstar 2, Pegasus) */
};

struct pdc_dma_map {
	void *ctx;          /* opaque context associated with frame */
};

/* dma descriptor */
struct dma64dd {
	u32 ctrl1;      /* misc control bits */
	u32 ctrl2;      /* buffer count and address extension */
	u32 addrlow;    /* memory address of the date buffer, bits 31:0 */
	u32 addrhigh;   /* memory address of the date buffer, bits 63:32 */
};

/* dma registers per channel(xmt or rcv) */
struct dma64_regs {
	u32  control;   /* enable, et al */
	u32  ptr;       /* last descriptor posted to chip */
	u32  addrlow;   /* descriptor ring base address low 32-bits */
	u32  addrhigh;  /* descriptor ring base address bits 63:32 */
	u32  status0;   /* last rx descriptor written by hw */
	u32  status1;   /* driver does not use */
};

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define _PADLINE(line)  pad ## line
#define _XSTR(line)     _PADLINE(line)
#define PAD             _XSTR(__LINE__)
#endif  /* PAD */

/* dma registers. matches hw layout. */
struct dma64 {
	struct dma64_regs dmaxmt;  /* dma tx */
	u32          PAD[2];
	struct dma64_regs dmarcv;  /* dma rx */
	u32          PAD[2];
};

/* PDC registers */
struct pdc_regs {
	u32  devcontrol;             /* 0x000 */
	u32  devstatus;              /* 0x004 */
	u32  PAD;
	u32  biststatus;             /* 0x00c */
	u32  PAD[4];
	u32  intstatus;              /* 0x020 */
	u32  intmask;                /* 0x024 */
	u32  gptimer;                /* 0x028 */

	u32  PAD;
	u32  intrcvlazy_0;           /* 0x030 (Only in PDC, not FA2) */
	u32  intrcvlazy_1;           /* 0x034 (Only in PDC, not FA2) */
	u32  intrcvlazy_2;           /* 0x038 (Only in PDC, not FA2) */
	u32  intrcvlazy_3;           /* 0x03c (Only in PDC, not FA2) */

	u32  PAD[48];
	u32  fa_intrecvlazy;         /* 0x100 (Only in FA2, not PDC) */
	u32  flowctlthresh;          /* 0x104 */
	u32  wrrthresh;              /* 0x108 */
	u32  gmac_idle_cnt_thresh;   /* 0x10c */

	u32  PAD[4];
	u32  ifioaccessaddr;         /* 0x120 */
	u32  ifioaccessbyte;         /* 0x124 */
	u32  ifioaccessdata;         /* 0x128 */

	u32  PAD[21];
	u32  phyaccess;              /* 0x180 */
	u32  PAD;
	u32  phycontrol;             /* 0x188 */
	u32  txqctl;                 /* 0x18c */
	u32  rxqctl;                 /* 0x190 */
	u32  gpioselect;             /* 0x194 */
	u32  gpio_output_en;         /* 0x198 */
	u32  PAD;                    /* 0x19c */
	u32  txq_rxq_mem_ctl;        /* 0x1a0 */
	u32  memory_ecc_status;      /* 0x1a4 */
	u32  serdes_ctl;             /* 0x1a8 */
	u32  serdes_status0;         /* 0x1ac */
	u32  serdes_status1;         /* 0x1b0 */
	u32  PAD[11];                /* 0x1b4-1dc */
	u32  clk_ctl_st;             /* 0x1e0 */
	u32  hw_war;                 /* 0x1e4 (Only in PDC, not FA2) */
	u32  pwrctl;                 /* 0x1e8 */
	u32  PAD[5];

#define PDC_NUM_DMA_RINGS   4
	struct dma64 dmaregs[PDC_NUM_DMA_RINGS];  /* 0x0200 - 0x2fc */

	/* more registers follow, but we don't use them */
};

/* structure for allocating/freeing DMA rings */
struct pdc_ring_alloc {
	dma_addr_t  dmabase; /* DMA address of start of ring */
	void	   *vbase;   /* base kernel virtual address of ring */
	u32	    size;    /* ring allocation size in bytes */
};

/*
 * context associated with a receive descriptor.
 * @rxp_ctx: opaque context associated with frame that starts at each
 *           rx ring index.
 * @dst_sg:  Scatterlist used to form reply frames beginning at a given ring
 *           index. Retained in order to unmap each sg after reply is processed.
 * @rxin_numd: Number of rx descriptors associated with the message that starts
 *             at a descriptor index. Not set for every index. For example,
 *             if descriptor index i points to a scatterlist with 4 entries,
 *             then the next three descriptor indexes don't have a value set.
 * @resp_hdr: Virtual address of buffer used to catch DMA rx status
 * @resp_hdr_daddr: physical address of DMA rx status buffer
 */
struct pdc_rx_ctx {
	void *rxp_ctx;
	struct scatterlist *dst_sg;
	u32  rxin_numd;
	void *resp_hdr;
	dma_addr_t resp_hdr_daddr;
};

/* PDC state structure */
struct pdc_state {
	/* Index of the PDC whose state is in this structure instance */
	u8 pdc_idx;

	/* Platform device for this PDC instance */
	struct platform_device *pdev;

	/*
	 * Each PDC instance has a mailbox controller. PDC receives request
	 * messages through mailboxes, and sends response messages through the
	 * mailbox framework.
	 */
	struct mbox_controller mbc;

	unsigned int pdc_irq;

	/* tasklet for deferred processing after DMA rx interrupt */
	struct tasklet_struct rx_tasklet;

	/* Number of bytes of receive status prior to each rx frame */
	u32 rx_status_len;
	/* Whether a BCM header is prepended to each frame */
	bool use_bcm_hdr;
	/* Sum of length of BCM header and rx status header */
	u32 pdc_resp_hdr_len;

	/* The base virtual address of DMA hw registers */
	void __iomem *pdc_reg_vbase;

	/* Pool for allocation of DMA rings */
	struct dma_pool *ring_pool;

	/* Pool for allocation of metadata buffers for response messages */
	struct dma_pool *rx_buf_pool;

	/*
	 * The base virtual address of DMA tx/rx descriptor rings. Corresponding
	 * DMA address and size of ring allocation.
	 */
	struct pdc_ring_alloc tx_ring_alloc;
	struct pdc_ring_alloc rx_ring_alloc;

	struct pdc_regs *regs;    /* start of PDC registers */

	struct dma64_regs *txregs_64; /* dma tx engine registers */
	struct dma64_regs *rxregs_64; /* dma rx engine registers */

	/*
	 * Arrays of PDC_RING_ENTRIES descriptors
	 * To use multiple ringsets, this needs to be extended
	 */
	struct dma64dd   *txd_64;  /* tx descriptor ring */
	struct dma64dd   *rxd_64;  /* rx descriptor ring */

	/* descriptor ring sizes */
	u32      ntxd;       /* # tx descriptors */
	u32      nrxd;       /* # rx descriptors */
	u32      nrxpost;    /* # rx buffers to keep posted */
	u32      ntxpost;    /* max number of tx buffers that can be posted */

	/*
	 * Index of next tx descriptor to reclaim. That is, the descriptor
	 * index of the oldest tx buffer for which the host has yet to process
	 * the corresponding response.
	 */
	u32  txin;

	/*
	 * Index of the first receive descriptor for the sequence of
	 * message fragments currently under construction. Used to build up
	 * the rxin_numd count for a message. Updated to rxout when the host
	 * starts a new sequence of rx buffers for a new message.
	 */
	u32  tx_msg_start;

	/* Index of next tx descriptor to post. */
	u32  txout;

	/*
	 * Number of tx descriptors associated with the message that starts
	 * at this tx descriptor index.
	 */
	u32      txin_numd[PDC_RING_ENTRIES];

	/*
	 * Index of next rx descriptor to reclaim. This is the index of
	 * the next descriptor whose data has yet to be processed by the host.
	 */
	u32  rxin;

	/*
	 * Index of the first receive descriptor for the sequence of
	 * message fragments currently under construction. Used to build up
	 * the rxin_numd count for a message. Updated to rxout when the host
	 * starts a new sequence of rx buffers for a new message.
	 */
	u32  rx_msg_start;

	/*
	 * Saved value of current hardware rx descriptor index.
	 * The last rx buffer written by the hw is the index previous to
	 * this one.
	 */
	u32  last_rx_curr;

	/* Index of next rx descriptor to post. */
	u32  rxout;

	struct pdc_rx_ctx rx_ctx[PDC_RING_ENTRIES];

	/*
	 * Scatterlists used to form request and reply frames beginning at a
	 * given ring index. Retained in order to unmap each sg after reply
	 * is processed
	 */
	struct scatterlist *src_sg[PDC_RING_ENTRIES];

	struct dentry *debugfs_stats;  /* debug FS stats file for this PDC */

	/* counters */
	u32  pdc_requests;     /* number of request messages submitted */
	u32  pdc_replies;      /* number of reply messages received */
	u32  last_tx_not_done; /* too few tx descriptors to indicate done */
	u32  tx_ring_full;     /* unable to accept msg because tx ring full */
	u32  rx_ring_full;     /* unable to accept msg because rx ring full */
	u32  txnobuf;          /* unable to create tx descriptor */
	u32  rxnobuf;          /* unable to create rx descriptor */
	u32  rx_oflow;         /* count of rx overflows */

	/* hardware type - FA2 or PDC/MDE */
	enum pdc_hw hw_type;
};

/* Global variables */

struct pdc_globals {
	/* Actual number of SPUs in hardware, as reported by device tree */
	u32 num_spu;
};

static struct pdc_globals pdcg;

/* top level debug FS directory for PDC driver */
static struct dentry *debugfs_dir;

static ssize_t pdc_debugfs_read(struct file *filp, char __user *ubuf,
				size_t count, loff_t *offp)
{
	struct pdc_state *pdcs;
	char *buf;
	ssize_t ret, out_offset, out_count;

	out_count = 512;

	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pdcs = filp->private_data;
	out_offset = 0;
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "SPU %u stats:\n", pdcs->pdc_idx);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "PDC requests....................%u\n",
			       pdcs->pdc_requests);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "PDC responses...................%u\n",
			       pdcs->pdc_replies);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Tx not done.....................%u\n",
			       pdcs->last_tx_not_done);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Tx ring full....................%u\n",
			       pdcs->tx_ring_full);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Rx ring full....................%u\n",
			       pdcs->rx_ring_full);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Tx desc write fail. Ring full...%u\n",
			       pdcs->txnobuf);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Rx desc write fail. Ring full...%u\n",
			       pdcs->rxnobuf);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Receive overflow................%u\n",
			       pdcs->rx_oflow);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Num frags in rx ring............%u\n",
			       NRXDACTIVE(pdcs->rxin, pdcs->last_rx_curr,
					  pdcs->nrxpost));

	if (out_offset > out_count)
		out_offset = out_count;

	ret = simple_read_from_buffer(ubuf, count, offp, buf, out_offset);
	kfree(buf);
	return ret;
}

static const struct file_operations pdc_debugfs_stats = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = pdc_debugfs_read,
};

/**
 * pdc_setup_debugfs() - Create the debug FS directories. If the top-level
 * directory has not yet been created, create it now. Create a stats file in
 * this directory for a SPU.
 * @pdcs: PDC state structure
 */
static void pdc_setup_debugfs(struct pdc_state *pdcs)
{
	char spu_stats_name[16];

	if (!debugfs_initialized())
		return;

	snprintf(spu_stats_name, 16, "pdc%d_stats", pdcs->pdc_idx);
	if (!debugfs_dir)
		debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	/* S_IRUSR == 0400 */
	pdcs->debugfs_stats = debugfs_create_file(spu_stats_name, 0400,
						  debugfs_dir, pdcs,
						  &pdc_debugfs_stats);
}

static void pdc_free_debugfs(void)
{
	debugfs_remove_recursive(debugfs_dir);
	debugfs_dir = NULL;
}

/**
 * pdc_build_rxd() - Build DMA descriptor to receive SPU result.
 * @pdcs:      PDC state for SPU that will generate result
 * @dma_addr:  DMA address of buffer that descriptor is being built for
 * @buf_len:   Length of the receive buffer, in bytes
 * @flags:     Flags to be stored in descriptor
 */
static inline void
pdc_build_rxd(struct pdc_state *pdcs, dma_addr_t dma_addr,
	      u32 buf_len, u32 flags)
{
	struct device *dev = &pdcs->pdev->dev;
	struct dma64dd *rxd = &pdcs->rxd_64[pdcs->rxout];

	dev_dbg(dev,
		"Writing rx descriptor for PDC %u at index %u with length %u. flags %#x\n",
		pdcs->pdc_idx, pdcs->rxout, buf_len, flags);

	rxd->addrlow = cpu_to_le32(lower_32_bits(dma_addr));
	rxd->addrhigh = cpu_to_le32(upper_32_bits(dma_addr));
	rxd->ctrl1 = cpu_to_le32(flags);
	rxd->ctrl2 = cpu_to_le32(buf_len);

	/* bump ring index and return */
	pdcs->rxout = NEXTRXD(pdcs->rxout, pdcs->nrxpost);
}

/**
 * pdc_build_txd() - Build a DMA descriptor to transmit a SPU request to
 * hardware.
 * @pdcs:        PDC state for the SPU that will process this request
 * @dma_addr:    DMA address of packet to be transmitted
 * @buf_len:     Length of tx buffer, in bytes
 * @flags:       Flags to be stored in descriptor
 */
static inline void
pdc_build_txd(struct pdc_state *pdcs, dma_addr_t dma_addr, u32 buf_len,
	      u32 flags)
{
	struct device *dev = &pdcs->pdev->dev;
	struct dma64dd *txd = &pdcs->txd_64[pdcs->txout];

	dev_dbg(dev,
		"Writing tx descriptor for PDC %u at index %u with length %u, flags %#x\n",
		pdcs->pdc_idx, pdcs->txout, buf_len, flags);

	txd->addrlow = cpu_to_le32(lower_32_bits(dma_addr));
	txd->addrhigh = cpu_to_le32(upper_32_bits(dma_addr));
	txd->ctrl1 = cpu_to_le32(flags);
	txd->ctrl2 = cpu_to_le32(buf_len);

	/* bump ring index and return */
	pdcs->txout = NEXTTXD(pdcs->txout, pdcs->ntxpost);
}

/**
 * pdc_receive_one() - Receive a response message from a given SPU.
 * @pdcs:    PDC state for the SPU to receive from
 *
 * When the return code indicates success, the response message is available in
 * the receive buffers provided prior to submission of the request.
 *
 * Return:  PDC_SUCCESS if one or more receive descriptors was processed
 *          -EAGAIN indicates that no response message is available
 *          -EIO an error occurred
 */
static int
pdc_receive_one(struct pdc_state *pdcs)
{
	struct device *dev = &pdcs->pdev->dev;
	struct mbox_controller *mbc;
	struct mbox_chan *chan;
	struct brcm_message mssg;
	u32 len, rx_status;
	u32 num_frags;
	u8 *resp_hdr;    /* virtual addr of start of resp message DMA header */
	u32 frags_rdy;   /* number of fragments ready to read */
	u32 rx_idx;      /* ring index of start of receive frame */
	dma_addr_t resp_hdr_daddr;
	struct pdc_rx_ctx *rx_ctx;

	mbc = &pdcs->mbc;
	chan = &mbc->chans[0];
	mssg.type = BRCM_MESSAGE_SPU;

	/*
	 * return if a complete response message is not yet ready.
	 * rxin_numd[rxin] is the number of fragments in the next msg
	 * to read.
	 */
	frags_rdy = NRXDACTIVE(pdcs->rxin, pdcs->last_rx_curr, pdcs->nrxpost);
	if ((frags_rdy == 0) ||
	    (frags_rdy < pdcs->rx_ctx[pdcs->rxin].rxin_numd))
		/* No response ready */
		return -EAGAIN;

	num_frags = pdcs->txin_numd[pdcs->txin];
	WARN_ON(num_frags == 0);

	dma_unmap_sg(dev, pdcs->src_sg[pdcs->txin],
		     sg_nents(pdcs->src_sg[pdcs->txin]), DMA_TO_DEVICE);

	pdcs->txin = (pdcs->txin + num_frags) & pdcs->ntxpost;

	dev_dbg(dev, "PDC %u reclaimed %d tx descriptors",
		pdcs->pdc_idx, num_frags);

	rx_idx = pdcs->rxin;
	rx_ctx = &pdcs->rx_ctx[rx_idx];
	num_frags = rx_ctx->rxin_numd;
	/* Return opaque context with result */
	mssg.ctx = rx_ctx->rxp_ctx;
	rx_ctx->rxp_ctx = NULL;
	resp_hdr = rx_ctx->resp_hdr;
	resp_hdr_daddr = rx_ctx->resp_hdr_daddr;
	dma_unmap_sg(dev, rx_ctx->dst_sg, sg_nents(rx_ctx->dst_sg),
		     DMA_FROM_DEVICE);

	pdcs->rxin = (pdcs->rxin + num_frags) & pdcs->nrxpost;

	dev_dbg(dev, "PDC %u reclaimed %d rx descriptors",
		pdcs->pdc_idx, num_frags);

	dev_dbg(dev,
		"PDC %u txin %u, txout %u, rxin %u, rxout %u, last_rx_curr %u\n",
		pdcs->pdc_idx, pdcs->txin, pdcs->txout, pdcs->rxin,
		pdcs->rxout, pdcs->last_rx_curr);

	if (pdcs->pdc_resp_hdr_len == PDC_SPUM_RESP_HDR_LEN) {
		/*
		 * For SPU-M, get length of response msg and rx overflow status.
		 */
		rx_status = *((u32 *)resp_hdr);
		len = rx_status & RX_STATUS_LEN;
		dev_dbg(dev,
			"SPU response length %u bytes", len);
		if (unlikely(((rx_status & RX_STATUS_OVERFLOW) || (!len)))) {
			if (rx_status & RX_STATUS_OVERFLOW) {
				dev_err_ratelimited(dev,
						    "crypto receive overflow");
				pdcs->rx_oflow++;
			} else {
				dev_info_ratelimited(dev, "crypto rx len = 0");
			}
			return -EIO;
		}
	}

	dma_pool_free(pdcs->rx_buf_pool, resp_hdr, resp_hdr_daddr);

	mbox_chan_received_data(chan, &mssg);

	pdcs->pdc_replies++;
	return PDC_SUCCESS;
}

/**
 * pdc_receive() - Process as many responses as are available in the rx ring.
 * @pdcs:  PDC state
 *
 * Called within the hard IRQ.
 * Return:
 */
static int
pdc_receive(struct pdc_state *pdcs)
{
	int rx_status;

	/* read last_rx_curr from register once */
	pdcs->last_rx_curr =
	    (ioread32(&pdcs->rxregs_64->status0) &
	     CRYPTO_D64_RS0_CD_MASK) / RING_ENTRY_SIZE;

	do {
		/* Could be many frames ready */
		rx_status = pdc_receive_one(pdcs);
	} while (rx_status == PDC_SUCCESS);

	return 0;
}

/**
 * pdc_tx_list_sg_add() - Add the buffers in a scatterlist to the transmit
 * descriptors for a given SPU. The scatterlist buffers contain the data for a
 * SPU request message.
 * @spu_idx:   The index of the SPU to submit the request to, [0, max_spu)
 * @sg:        Scatterlist whose buffers contain part of the SPU request
 *
 * If a scatterlist buffer is larger than PDC_DMA_BUF_MAX, multiple descriptors
 * are written for that buffer, each <= PDC_DMA_BUF_MAX byte in length.
 *
 * Return: PDC_SUCCESS if successful
 *         < 0 otherwise
 */
static int pdc_tx_list_sg_add(struct pdc_state *pdcs, struct scatterlist *sg)
{
	u32 flags = 0;
	u32 eot;
	u32 tx_avail;

	/*
	 * Num descriptors needed. Conservatively assume we need a descriptor
	 * for every entry in sg.
	 */
	u32 num_desc;
	u32 desc_w = 0;	/* Number of tx descriptors written */
	u32 bufcnt;	/* Number of bytes of buffer pointed to by descriptor */
	dma_addr_t databufptr;	/* DMA address to put in descriptor */

	num_desc = (u32)sg_nents(sg);

	/* check whether enough tx descriptors are available */
	tx_avail = pdcs->ntxpost - NTXDACTIVE(pdcs->txin, pdcs->txout,
					      pdcs->ntxpost);
	if (unlikely(num_desc > tx_avail)) {
		pdcs->txnobuf++;
		return -ENOSPC;
	}

	/* build tx descriptors */
	if (pdcs->tx_msg_start == pdcs->txout) {
		/* Start of frame */
		pdcs->txin_numd[pdcs->tx_msg_start] = 0;
		pdcs->src_sg[pdcs->txout] = sg;
		flags = D64_CTRL1_SOF;
	}

	while (sg) {
		if (unlikely(pdcs->txout == (pdcs->ntxd - 1)))
			eot = D64_CTRL1_EOT;
		else
			eot = 0;

		/*
		 * If sg buffer larger than PDC limit, split across
		 * multiple descriptors
		 */
		bufcnt = sg_dma_len(sg);
		databufptr = sg_dma_address(sg);
		while (bufcnt > PDC_DMA_BUF_MAX) {
			pdc_build_txd(pdcs, databufptr, PDC_DMA_BUF_MAX,
				      flags | eot);
			desc_w++;
			bufcnt -= PDC_DMA_BUF_MAX;
			databufptr += PDC_DMA_BUF_MAX;
			if (unlikely(pdcs->txout == (pdcs->ntxd - 1)))
				eot = D64_CTRL1_EOT;
			else
				eot = 0;
		}
		sg = sg_next(sg);
		if (!sg)
			/* Writing last descriptor for frame */
			flags |= (D64_CTRL1_EOF | D64_CTRL1_IOC);
		pdc_build_txd(pdcs, databufptr, bufcnt, flags | eot);
		desc_w++;
		/* Clear start of frame after first descriptor */
		flags &= ~D64_CTRL1_SOF;
	}
	pdcs->txin_numd[pdcs->tx_msg_start] += desc_w;

	return PDC_SUCCESS;
}

/**
 * pdc_tx_list_final() - Initiate DMA transfer of last frame written to tx
 * ring.
 * @pdcs:  PDC state for SPU to process the request
 *
 * Sets the index of the last descriptor written in both the rx and tx ring.
 *
 * Return: PDC_SUCCESS
 */
static int pdc_tx_list_final(struct pdc_state *pdcs)
{
	/*
	 * write barrier to ensure all register writes are complete
	 * before chip starts to process new request
	 */
	wmb();
	iowrite32(pdcs->rxout << 4, &pdcs->rxregs_64->ptr);
	iowrite32(pdcs->txout << 4, &pdcs->txregs_64->ptr);
	pdcs->pdc_requests++;

	return PDC_SUCCESS;
}

/**
 * pdc_rx_list_init() - Start a new receive descriptor list for a given PDC.
 * @pdcs:   PDC state for SPU handling request
 * @dst_sg: scatterlist providing rx buffers for response to be returned to
 *	    mailbox client
 * @ctx:    Opaque context for this request
 *
 * Posts a single receive descriptor to hold the metadata that precedes a
 * response. For example, with SPU-M, the metadata is a 32-byte DMA header and
 * an 8-byte BCM header. Moves the msg_start descriptor indexes for both tx and
 * rx to indicate the start of a new message.
 *
 * Return:  PDC_SUCCESS if successful
 *          < 0 if an error (e.g., rx ring is full)
 */
static int pdc_rx_list_init(struct pdc_state *pdcs, struct scatterlist *dst_sg,
			    void *ctx)
{
	u32 flags = 0;
	u32 rx_avail;
	u32 rx_pkt_cnt = 1;	/* Adding a single rx buffer */
	dma_addr_t daddr;
	void *vaddr;
	struct pdc_rx_ctx *rx_ctx;

	rx_avail = pdcs->nrxpost - NRXDACTIVE(pdcs->rxin, pdcs->rxout,
					      pdcs->nrxpost);
	if (unlikely(rx_pkt_cnt > rx_avail)) {
		pdcs->rxnobuf++;
		return -ENOSPC;
	}

	/* allocate a buffer for the dma rx status */
	vaddr = dma_pool_zalloc(pdcs->rx_buf_pool, GFP_ATOMIC, &daddr);
	if (unlikely(!vaddr))
		return -ENOMEM;

	/*
	 * Update msg_start indexes for both tx and rx to indicate the start
	 * of a new sequence of descriptor indexes that contain the fragments
	 * of the same message.
	 */
	pdcs->rx_msg_start = pdcs->rxout;
	pdcs->tx_msg_start = pdcs->txout;

	/* This is always the first descriptor in the receive sequence */
	flags = D64_CTRL1_SOF;
	pdcs->rx_ctx[pdcs->rx_msg_start].rxin_numd = 1;

	if (unlikely(pdcs->rxout == (pdcs->nrxd - 1)))
		flags |= D64_CTRL1_EOT;

	rx_ctx = &pdcs->rx_ctx[pdcs->rxout];
	rx_ctx->rxp_ctx = ctx;
	rx_ctx->dst_sg = dst_sg;
	rx_ctx->resp_hdr = vaddr;
	rx_ctx->resp_hdr_daddr = daddr;
	pdc_build_rxd(pdcs, daddr, pdcs->pdc_resp_hdr_len, flags);
	return PDC_SUCCESS;
}

/**
 * pdc_rx_list_sg_add() - Add the buffers in a scatterlist to the receive
 * descriptors for a given SPU. The caller must have already DMA mapped the
 * scatterlist.
 * @spu_idx:    Indicates which SPU the buffers are for
 * @sg:         Scatterlist whose buffers are added to the receive ring
 *
 * If a receive buffer in the scatterlist is larger than PDC_DMA_BUF_MAX,
 * multiple receive descriptors are written, each with a buffer <=
 * PDC_DMA_BUF_MAX.
 *
 * Return: PDC_SUCCESS if successful
 *         < 0 otherwise (e.g., receive ring is full)
 */
static int pdc_rx_list_sg_add(struct pdc_state *pdcs, struct scatterlist *sg)
{
	u32 flags = 0;
	u32 rx_avail;

	/*
	 * Num descriptors needed. Conservatively assume we need a descriptor
	 * for every entry from our starting point in the scatterlist.
	 */
	u32 num_desc;
	u32 desc_w = 0;	/* Number of tx descriptors written */
	u32 bufcnt;	/* Number of bytes of buffer pointed to by descriptor */
	dma_addr_t databufptr;	/* DMA address to put in descriptor */

	num_desc = (u32)sg_nents(sg);

	rx_avail = pdcs->nrxpost - NRXDACTIVE(pdcs->rxin, pdcs->rxout,
					      pdcs->nrxpost);
	if (unlikely(num_desc > rx_avail)) {
		pdcs->rxnobuf++;
		return -ENOSPC;
	}

	while (sg) {
		if (unlikely(pdcs->rxout == (pdcs->nrxd - 1)))
			flags = D64_CTRL1_EOT;
		else
			flags = 0;

		/*
		 * If sg buffer larger than PDC limit, split across
		 * multiple descriptors
		 */
		bufcnt = sg_dma_len(sg);
		databufptr = sg_dma_address(sg);
		while (bufcnt > PDC_DMA_BUF_MAX) {
			pdc_build_rxd(pdcs, databufptr, PDC_DMA_BUF_MAX, flags);
			desc_w++;
			bufcnt -= PDC_DMA_BUF_MAX;
			databufptr += PDC_DMA_BUF_MAX;
			if (unlikely(pdcs->rxout == (pdcs->nrxd - 1)))
				flags = D64_CTRL1_EOT;
			else
				flags = 0;
		}
		pdc_build_rxd(pdcs, databufptr, bufcnt, flags);
		desc_w++;
		sg = sg_next(sg);
	}
	pdcs->rx_ctx[pdcs->rx_msg_start].rxin_numd += desc_w;

	return PDC_SUCCESS;
}

/**
 * pdc_irq_handler() - Interrupt handler called in interrupt context.
 * @irq:      Interrupt number that has fired
 * @data:     device struct for DMA engine that generated the interrupt
 *
 * We have to clear the device interrupt status flags here. So cache the
 * status for later use in the thread function. Other than that, just return
 * WAKE_THREAD to invoke the thread function.
 *
 * Return: IRQ_WAKE_THREAD if interrupt is ours
 *         IRQ_NONE otherwise
 */
static irqreturn_t pdc_irq_handler(int irq, void *data)
{
	struct device *dev = (struct device *)data;
	struct pdc_state *pdcs = dev_get_drvdata(dev);
	u32 intstatus = ioread32(pdcs->pdc_reg_vbase + PDC_INTSTATUS_OFFSET);

	if (unlikely(intstatus == 0))
		return IRQ_NONE;

	/* Disable interrupts until soft handler runs */
	iowrite32(0, pdcs->pdc_reg_vbase + PDC_INTMASK_OFFSET);

	/* Clear interrupt flags in device */
	iowrite32(intstatus, pdcs->pdc_reg_vbase + PDC_INTSTATUS_OFFSET);

	/* Wakeup IRQ thread */
	tasklet_schedule(&pdcs->rx_tasklet);
	return IRQ_HANDLED;
}

/**
 * pdc_tasklet_cb() - Tasklet callback that runs the deferred processing after
 * a DMA receive interrupt. Reenables the receive interrupt.
 * @data: PDC state structure
 */
static void pdc_tasklet_cb(unsigned long data)
{
	struct pdc_state *pdcs = (struct pdc_state *)data;

	pdc_receive(pdcs);

	/* reenable interrupts */
	iowrite32(PDC_INTMASK, pdcs->pdc_reg_vbase + PDC_INTMASK_OFFSET);
}

/**
 * pdc_ring_init() - Allocate DMA rings and initialize constant fields of
 * descriptors in one ringset.
 * @pdcs:    PDC instance state
 * @ringset: index of ringset being used
 *
 * Return: PDC_SUCCESS if ring initialized
 *         < 0 otherwise
 */
static int pdc_ring_init(struct pdc_state *pdcs, int ringset)
{
	int i;
	int err = PDC_SUCCESS;
	struct dma64 *dma_reg;
	struct device *dev = &pdcs->pdev->dev;
	struct pdc_ring_alloc tx;
	struct pdc_ring_alloc rx;

	/* Allocate tx ring */
	tx.vbase = dma_pool_zalloc(pdcs->ring_pool, GFP_KERNEL, &tx.dmabase);
	if (unlikely(!tx.vbase)) {
		err = -ENOMEM;
		goto done;
	}

	/* Allocate rx ring */
	rx.vbase = dma_pool_zalloc(pdcs->ring_pool, GFP_KERNEL, &rx.dmabase);
	if (unlikely(!rx.vbase)) {
		err = -ENOMEM;
		goto fail_dealloc;
	}

	dev_dbg(dev, " - base DMA addr of tx ring      %pad", &tx.dmabase);
	dev_dbg(dev, " - base virtual addr of tx ring  %p", tx.vbase);
	dev_dbg(dev, " - base DMA addr of rx ring      %pad", &rx.dmabase);
	dev_dbg(dev, " - base virtual addr of rx ring  %p", rx.vbase);

	memcpy(&pdcs->tx_ring_alloc, &tx, sizeof(tx));
	memcpy(&pdcs->rx_ring_alloc, &rx, sizeof(rx));

	pdcs->rxin = 0;
	pdcs->rx_msg_start = 0;
	pdcs->last_rx_curr = 0;
	pdcs->rxout = 0;
	pdcs->txin = 0;
	pdcs->tx_msg_start = 0;
	pdcs->txout = 0;

	/* Set descriptor array base addresses */
	pdcs->txd_64 = (struct dma64dd *)pdcs->tx_ring_alloc.vbase;
	pdcs->rxd_64 = (struct dma64dd *)pdcs->rx_ring_alloc.vbase;

	/* Tell device the base DMA address of each ring */
	dma_reg = &pdcs->regs->dmaregs[ringset];

	/* But first disable DMA and set curptr to 0 for both TX & RX */
	iowrite32(PDC_TX_CTL, &dma_reg->dmaxmt.control);
	iowrite32((PDC_RX_CTL + (pdcs->rx_status_len << 1)),
		  &dma_reg->dmarcv.control);
	iowrite32(0, &dma_reg->dmaxmt.ptr);
	iowrite32(0, &dma_reg->dmarcv.ptr);

	/* Set base DMA addresses */
	iowrite32(lower_32_bits(pdcs->tx_ring_alloc.dmabase),
		  &dma_reg->dmaxmt.addrlow);
	iowrite32(upper_32_bits(pdcs->tx_ring_alloc.dmabase),
		  &dma_reg->dmaxmt.addrhigh);

	iowrite32(lower_32_bits(pdcs->rx_ring_alloc.dmabase),
		  &dma_reg->dmarcv.addrlow);
	iowrite32(upper_32_bits(pdcs->rx_ring_alloc.dmabase),
		  &dma_reg->dmarcv.addrhigh);

	/* Re-enable DMA */
	iowrite32(PDC_TX_CTL | PDC_TX_ENABLE, &dma_reg->dmaxmt.control);
	iowrite32((PDC_RX_CTL | PDC_RX_ENABLE | (pdcs->rx_status_len << 1)),
		  &dma_reg->dmarcv.control);

	/* Initialize descriptors */
	for (i = 0; i < PDC_RING_ENTRIES; i++) {
		/* Every tx descriptor can be used for start of frame. */
		if (i != pdcs->ntxpost) {
			iowrite32(D64_CTRL1_SOF | D64_CTRL1_EOF,
				  &pdcs->txd_64[i].ctrl1);
		} else {
			/* Last descriptor in ringset. Set End of Table. */
			iowrite32(D64_CTRL1_SOF | D64_CTRL1_EOF |
				  D64_CTRL1_EOT, &pdcs->txd_64[i].ctrl1);
		}

		/* Every rx descriptor can be used for start of frame */
		if (i != pdcs->nrxpost) {
			iowrite32(D64_CTRL1_SOF,
				  &pdcs->rxd_64[i].ctrl1);
		} else {
			/* Last descriptor in ringset. Set End of Table. */
			iowrite32(D64_CTRL1_SOF | D64_CTRL1_EOT,
				  &pdcs->rxd_64[i].ctrl1);
		}
	}
	return PDC_SUCCESS;

fail_dealloc:
	dma_pool_free(pdcs->ring_pool, tx.vbase, tx.dmabase);
done:
	return err;
}

static void pdc_ring_free(struct pdc_state *pdcs)
{
	if (pdcs->tx_ring_alloc.vbase) {
		dma_pool_free(pdcs->ring_pool, pdcs->tx_ring_alloc.vbase,
			      pdcs->tx_ring_alloc.dmabase);
		pdcs->tx_ring_alloc.vbase = NULL;
	}

	if (pdcs->rx_ring_alloc.vbase) {
		dma_pool_free(pdcs->ring_pool, pdcs->rx_ring_alloc.vbase,
			      pdcs->rx_ring_alloc.dmabase);
		pdcs->rx_ring_alloc.vbase = NULL;
	}
}

/**
 * pdc_desc_count() - Count the number of DMA descriptors that will be required
 * for a given scatterlist. Account for the max length of a DMA buffer.
 * @sg:    Scatterlist to be DMA'd
 * Return: Number of descriptors required
 */
static u32 pdc_desc_count(struct scatterlist *sg)
{
	u32 cnt = 0;

	while (sg) {
		cnt += ((sg->length / PDC_DMA_BUF_MAX) + 1);
		sg = sg_next(sg);
	}
	return cnt;
}

/**
 * pdc_rings_full() - Check whether the tx ring has room for tx_cnt descriptors
 * and the rx ring has room for rx_cnt descriptors.
 * @pdcs:  PDC state
 * @tx_cnt: The number of descriptors required in the tx ring
 * @rx_cnt: The number of descriptors required i the rx ring
 *
 * Return: true if one of the rings does not have enough space
 *         false if sufficient space is available in both rings
 */
static bool pdc_rings_full(struct pdc_state *pdcs, int tx_cnt, int rx_cnt)
{
	u32 rx_avail;
	u32 tx_avail;
	bool full = false;

	/* Check if the tx and rx rings are likely to have enough space */
	rx_avail = pdcs->nrxpost - NRXDACTIVE(pdcs->rxin, pdcs->rxout,
					      pdcs->nrxpost);
	if (unlikely(rx_cnt > rx_avail)) {
		pdcs->rx_ring_full++;
		full = true;
	}

	if (likely(!full)) {
		tx_avail = pdcs->ntxpost - NTXDACTIVE(pdcs->txin, pdcs->txout,
						      pdcs->ntxpost);
		if (unlikely(tx_cnt > tx_avail)) {
			pdcs->tx_ring_full++;
			full = true;
		}
	}
	return full;
}

/**
 * pdc_last_tx_done() - If both the tx and rx rings have at least
 * PDC_RING_SPACE_MIN descriptors available, then indicate that the mailbox
 * framework can submit another message.
 * @chan:  mailbox channel to check
 * Return: true if PDC can accept another message on this channel
 */
static bool pdc_last_tx_done(struct mbox_chan *chan)
{
	struct pdc_state *pdcs = chan->con_priv;
	bool ret;

	if (unlikely(pdc_rings_full(pdcs, PDC_RING_SPACE_MIN,
				    PDC_RING_SPACE_MIN))) {
		pdcs->last_tx_not_done++;
		ret = false;
	} else {
		ret = true;
	}
	return ret;
}

/**
 * pdc_send_data() - mailbox send_data function
 * @chan:	The mailbox channel on which the data is sent. The channel
 *              corresponds to a DMA ringset.
 * @data:	The mailbox message to be sent. The message must be a
 *              brcm_message structure.
 *
 * This function is registered as the send_data function for the mailbox
 * controller. From the destination scatterlist in the mailbox message, it
 * creates a sequence of receive descriptors in the rx ring. From the source
 * scatterlist, it creates a sequence of transmit descriptors in the tx ring.
 * After creating the descriptors, it writes the rx ptr and tx ptr registers to
 * initiate the DMA transfer.
 *
 * This function does the DMA map and unmap of the src and dst scatterlists in
 * the mailbox message.
 *
 * Return: 0 if successful
 *	   -ENOTSUPP if the mailbox message is a type this driver does not
 *			support
 *         < 0 if an error
 */
static int pdc_send_data(struct mbox_chan *chan, void *data)
{
	struct pdc_state *pdcs = chan->con_priv;
	struct device *dev = &pdcs->pdev->dev;
	struct brcm_message *mssg = data;
	int err = PDC_SUCCESS;
	int src_nent;
	int dst_nent;
	int nent;
	u32 tx_desc_req;
	u32 rx_desc_req;

	if (unlikely(mssg->type != BRCM_MESSAGE_SPU))
		return -ENOTSUPP;

	src_nent = sg_nents(mssg->spu.src);
	if (likely(src_nent)) {
		nent = dma_map_sg(dev, mssg->spu.src, src_nent, DMA_TO_DEVICE);
		if (unlikely(nent == 0))
			return -EIO;
	}

	dst_nent = sg_nents(mssg->spu.dst);
	if (likely(dst_nent)) {
		nent = dma_map_sg(dev, mssg->spu.dst, dst_nent,
				  DMA_FROM_DEVICE);
		if (unlikely(nent == 0)) {
			dma_unmap_sg(dev, mssg->spu.src, src_nent,
				     DMA_TO_DEVICE);
			return -EIO;
		}
	}

	/*
	 * Check if the tx and rx rings have enough space. Do this prior to
	 * writing any tx or rx descriptors. Need to ensure that we do not write
	 * a partial set of descriptors, or write just rx descriptors but
	 * corresponding tx descriptors don't fit. Note that we want this check
	 * and the entire sequence of descriptor to happen without another
	 * thread getting in. The channel spin lock in the mailbox framework
	 * ensures this.
	 */
	tx_desc_req = pdc_desc_count(mssg->spu.src);
	rx_desc_req = pdc_desc_count(mssg->spu.dst);
	if (unlikely(pdc_rings_full(pdcs, tx_desc_req, rx_desc_req + 1)))
		return -ENOSPC;

	/* Create rx descriptors to SPU catch response */
	err = pdc_rx_list_init(pdcs, mssg->spu.dst, mssg->ctx);
	err |= pdc_rx_list_sg_add(pdcs, mssg->spu.dst);

	/* Create tx descriptors to submit SPU request */
	err |= pdc_tx_list_sg_add(pdcs, mssg->spu.src);
	err |= pdc_tx_list_final(pdcs);	/* initiate transfer */

	if (unlikely(err))
		dev_err(&pdcs->pdev->dev,
			"%s failed with error %d", __func__, err);

	return err;
}

static int pdc_startup(struct mbox_chan *chan)
{
	return pdc_ring_init(chan->con_priv, PDC_RINGSET);
}

static void pdc_shutdown(struct mbox_chan *chan)
{
	struct pdc_state *pdcs = chan->con_priv;

	if (!pdcs)
		return;

	dev_dbg(&pdcs->pdev->dev,
		"Shutdown mailbox channel for PDC %u", pdcs->pdc_idx);
	pdc_ring_free(pdcs);
}

/**
 * pdc_hw_init() - Use the given initialization parameters to initialize the
 * state for one of the PDCs.
 * @pdcs:  state of the PDC
 */
static
void pdc_hw_init(struct pdc_state *pdcs)
{
	struct platform_device *pdev;
	struct device *dev;
	struct dma64 *dma_reg;
	int ringset = PDC_RINGSET;

	pdev = pdcs->pdev;
	dev = &pdev->dev;

	dev_dbg(dev, "PDC %u initial values:", pdcs->pdc_idx);
	dev_dbg(dev, "state structure:                   %p",
		pdcs);
	dev_dbg(dev, " - base virtual addr of hw regs    %p",
		pdcs->pdc_reg_vbase);

	/* initialize data structures */
	pdcs->regs = (struct pdc_regs *)pdcs->pdc_reg_vbase;
	pdcs->txregs_64 = (struct dma64_regs *)
	    (((u8 *)pdcs->pdc_reg_vbase) +
		     PDC_TXREGS_OFFSET + (sizeof(struct dma64) * ringset));
	pdcs->rxregs_64 = (struct dma64_regs *)
	    (((u8 *)pdcs->pdc_reg_vbase) +
		     PDC_RXREGS_OFFSET + (sizeof(struct dma64) * ringset));

	pdcs->ntxd = PDC_RING_ENTRIES;
	pdcs->nrxd = PDC_RING_ENTRIES;
	pdcs->ntxpost = PDC_RING_ENTRIES - 1;
	pdcs->nrxpost = PDC_RING_ENTRIES - 1;
	iowrite32(0, &pdcs->regs->intmask);

	dma_reg = &pdcs->regs->dmaregs[ringset];

	/* Configure DMA but will enable later in pdc_ring_init() */
	iowrite32(PDC_TX_CTL, &dma_reg->dmaxmt.control);

	iowrite32(PDC_RX_CTL + (pdcs->rx_status_len << 1),
		  &dma_reg->dmarcv.control);

	/* Reset current index pointers after making sure DMA is disabled */
	iowrite32(0, &dma_reg->dmaxmt.ptr);
	iowrite32(0, &dma_reg->dmarcv.ptr);

	if (pdcs->pdc_resp_hdr_len == PDC_SPU2_RESP_HDR_LEN)
		iowrite32(PDC_CKSUM_CTRL,
			  pdcs->pdc_reg_vbase + PDC_CKSUM_CTRL_OFFSET);
}

/**
 * pdc_hw_disable() - Disable the tx and rx control in the hw.
 * @pdcs: PDC state structure
 *
 */
static void pdc_hw_disable(struct pdc_state *pdcs)
{
	struct dma64 *dma_reg;

	dma_reg = &pdcs->regs->dmaregs[PDC_RINGSET];
	iowrite32(PDC_TX_CTL, &dma_reg->dmaxmt.control);
	iowrite32(PDC_RX_CTL + (pdcs->rx_status_len << 1),
		  &dma_reg->dmarcv.control);
}

/**
 * pdc_rx_buf_pool_create() - Pool of receive buffers used to catch the metadata
 * header returned with each response message.
 * @pdcs: PDC state structure
 *
 * The metadata is not returned to the mailbox client. So the PDC driver
 * manages these buffers.
 *
 * Return: PDC_SUCCESS
 *         -ENOMEM if pool creation fails
 */
static int pdc_rx_buf_pool_create(struct pdc_state *pdcs)
{
	struct platform_device *pdev;
	struct device *dev;

	pdev = pdcs->pdev;
	dev = &pdev->dev;

	pdcs->pdc_resp_hdr_len = pdcs->rx_status_len;
	if (pdcs->use_bcm_hdr)
		pdcs->pdc_resp_hdr_len += BCM_HDR_LEN;

	pdcs->rx_buf_pool = dma_pool_create("pdc rx bufs", dev,
					    pdcs->pdc_resp_hdr_len,
					    RX_BUF_ALIGN, 0);
	if (!pdcs->rx_buf_pool)
		return -ENOMEM;

	return PDC_SUCCESS;
}

/**
 * pdc_interrupts_init() - Initialize the interrupt configuration for a PDC and
 * specify a threaded IRQ handler for deferred handling of interrupts outside of
 * interrupt context.
 * @pdcs:   PDC state
 *
 * Set the interrupt mask for transmit and receive done.
 * Set the lazy interrupt frame count to generate an interrupt for just one pkt.
 *
 * Return:  PDC_SUCCESS
 *          <0 if threaded irq request fails
 */
static int pdc_interrupts_init(struct pdc_state *pdcs)
{
	struct platform_device *pdev = pdcs->pdev;
	struct device *dev = &pdev->dev;
	struct device_node *dn = pdev->dev.of_node;
	int err;

	/* interrupt configuration */
	iowrite32(PDC_INTMASK, pdcs->pdc_reg_vbase + PDC_INTMASK_OFFSET);

	if (pdcs->hw_type == FA_HW)
		iowrite32(PDC_LAZY_INT, pdcs->pdc_reg_vbase +
			  FA_RCVLAZY0_OFFSET);
	else
		iowrite32(PDC_LAZY_INT, pdcs->pdc_reg_vbase +
			  PDC_RCVLAZY0_OFFSET);

	/* read irq from device tree */
	pdcs->pdc_irq = irq_of_parse_and_map(dn, 0);
	dev_dbg(dev, "pdc device %s irq %u for pdcs %p",
		dev_name(dev), pdcs->pdc_irq, pdcs);

	err = devm_request_irq(dev, pdcs->pdc_irq, pdc_irq_handler, 0,
			       dev_name(dev), dev);
	if (err) {
		dev_err(dev, "IRQ %u request failed with err %d\n",
			pdcs->pdc_irq, err);
		return err;
	}
	return PDC_SUCCESS;
}

static const struct mbox_chan_ops pdc_mbox_chan_ops = {
	.send_data = pdc_send_data,
	.last_tx_done = pdc_last_tx_done,
	.startup = pdc_startup,
	.shutdown = pdc_shutdown
};

/**
 * pdc_mb_init() - Initialize the mailbox controller.
 * @pdcs:  PDC state
 *
 * Each PDC is a mailbox controller. Each ringset is a mailbox channel. Kernel
 * driver only uses one ringset and thus one mb channel. PDC uses the transmit
 * complete interrupt to determine when a mailbox message has successfully been
 * transmitted.
 *
 * Return: 0 on success
 *         < 0 if there is an allocation or registration failure
 */
static int pdc_mb_init(struct pdc_state *pdcs)
{
	struct device *dev = &pdcs->pdev->dev;
	struct mbox_controller *mbc;
	int chan_index;
	int err;

	mbc = &pdcs->mbc;
	mbc->dev = dev;
	mbc->ops = &pdc_mbox_chan_ops;
	mbc->num_chans = 1;
	mbc->chans = devm_kcalloc(dev, mbc->num_chans, sizeof(*mbc->chans),
				  GFP_KERNEL);
	if (!mbc->chans)
		return -ENOMEM;

	mbc->txdone_irq = false;
	mbc->txdone_poll = true;
	mbc->txpoll_period = 1;
	for (chan_index = 0; chan_index < mbc->num_chans; chan_index++)
		mbc->chans[chan_index].con_priv = pdcs;

	/* Register mailbox controller */
	err = mbox_controller_register(mbc);
	if (err) {
		dev_crit(dev,
			 "Failed to register PDC mailbox controller. Error %d.",
			 err);
		return err;
	}
	return 0;
}

/* Device tree API */
static const int pdc_hw = PDC_HW;
static const int fa_hw = FA_HW;

static const struct of_device_id pdc_mbox_of_match[] = {
	{.compatible = "brcm,iproc-pdc-mbox", .data = &pdc_hw},
	{.compatible = "brcm,iproc-fa2-mbox", .data = &fa_hw},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pdc_mbox_of_match);

/**
 * pdc_dt_read() - Read application-specific data from device tree.
 * @pdev:  Platform device
 * @pdcs:  PDC state
 *
 * Reads the number of bytes of receive status that precede each received frame.
 * Reads whether transmit and received frames should be preceded by an 8-byte
 * BCM header.
 *
 * Return: 0 if successful
 *         -ENODEV if device not available
 */
static int pdc_dt_read(struct platform_device *pdev, struct pdc_state *pdcs)
{
	struct device *dev = &pdev->dev;
	struct device_node *dn = pdev->dev.of_node;
	const struct of_device_id *match;
	const int *hw_type;
	int err;

	err = of_property_read_u32(dn, "brcm,rx-status-len",
				   &pdcs->rx_status_len);
	if (err < 0)
		dev_err(dev,
			"%s failed to get DMA receive status length from device tree",
			__func__);

	pdcs->use_bcm_hdr = of_property_read_bool(dn, "brcm,use-bcm-hdr");

	pdcs->hw_type = PDC_HW;

	match = of_match_device(of_match_ptr(pdc_mbox_of_match), dev);
	if (match != NULL) {
		hw_type = match->data;
		pdcs->hw_type = *hw_type;
	}

	return 0;
}

/**
 * pdc_probe() - Probe function for PDC driver.
 * @pdev:   PDC platform device
 *
 * Reserve and map register regions defined in device tree.
 * Allocate and initialize tx and rx DMA rings.
 * Initialize a mailbox controller for each PDC.
 *
 * Return: 0 if successful
 *         < 0 if an error
 */
static int pdc_probe(struct platform_device *pdev)
{
	int err = 0;
	struct device *dev = &pdev->dev;
	struct resource *pdc_regs;
	struct pdc_state *pdcs;

	/* PDC state for one SPU */
	pdcs = devm_kzalloc(dev, sizeof(*pdcs), GFP_KERNEL);
	if (!pdcs) {
		err = -ENOMEM;
		goto cleanup;
	}

	pdcs->pdev = pdev;
	platform_set_drvdata(pdev, pdcs);
	pdcs->pdc_idx = pdcg.num_spu;
	pdcg.num_spu++;

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(39));
	if (err) {
		dev_warn(dev, "PDC device cannot perform DMA. Error %d.", err);
		goto cleanup;
	}

	/* Create DMA pool for tx ring */
	pdcs->ring_pool = dma_pool_create("pdc rings", dev, PDC_RING_SIZE,
					  RING_ALIGN, 0);
	if (!pdcs->ring_pool) {
		err = -ENOMEM;
		goto cleanup;
	}

	err = pdc_dt_read(pdev, pdcs);
	if (err)
		goto cleanup_ring_pool;

	pdc_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!pdc_regs) {
		err = -ENODEV;
		goto cleanup_ring_pool;
	}
	dev_dbg(dev, "PDC register region res.start = %pa, res.end = %pa",
		&pdc_regs->start, &pdc_regs->end);

	pdcs->pdc_reg_vbase = devm_ioremap_resource(&pdev->dev, pdc_regs);
	if (IS_ERR(pdcs->pdc_reg_vbase)) {
		err = PTR_ERR(pdcs->pdc_reg_vbase);
		dev_err(&pdev->dev, "Failed to map registers: %d\n", err);
		goto cleanup_ring_pool;
	}

	/* create rx buffer pool after dt read to know how big buffers are */
	err = pdc_rx_buf_pool_create(pdcs);
	if (err)
		goto cleanup_ring_pool;

	pdc_hw_init(pdcs);

	/* Init tasklet for deferred DMA rx processing */
	tasklet_init(&pdcs->rx_tasklet, pdc_tasklet_cb, (unsigned long)pdcs);

	err = pdc_interrupts_init(pdcs);
	if (err)
		goto cleanup_buf_pool;

	/* Initialize mailbox controller */
	err = pdc_mb_init(pdcs);
	if (err)
		goto cleanup_buf_pool;

	pdcs->debugfs_stats = NULL;
	pdc_setup_debugfs(pdcs);

	dev_dbg(dev, "pdc_probe() successful");
	return PDC_SUCCESS;

cleanup_buf_pool:
	tasklet_kill(&pdcs->rx_tasklet);
	dma_pool_destroy(pdcs->rx_buf_pool);

cleanup_ring_pool:
	dma_pool_destroy(pdcs->ring_pool);

cleanup:
	return err;
}

static int pdc_remove(struct platform_device *pdev)
{
	struct pdc_state *pdcs = platform_get_drvdata(pdev);

	pdc_free_debugfs();

	tasklet_kill(&pdcs->rx_tasklet);

	pdc_hw_disable(pdcs);

	mbox_controller_unregister(&pdcs->mbc);

	dma_pool_destroy(pdcs->rx_buf_pool);
	dma_pool_destroy(pdcs->ring_pool);
	return 0;
}

static struct platform_driver pdc_mbox_driver = {
	.probe = pdc_probe,
	.remove = pdc_remove,
	.driver = {
		   .name = "brcm-iproc-pdc-mbox",
		   .of_match_table = of_match_ptr(pdc_mbox_of_match),
		   },
};
module_platform_driver(pdc_mbox_driver);

MODULE_AUTHOR("Rob Rice <rob.rice@broadcom.com>");
MODULE_DESCRIPTION("Broadcom PDC mailbox driver");
MODULE_LICENSE("GPL v2");
