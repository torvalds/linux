/*
 * davinci_mmc.c - TI DaVinci MMC/SD/SDIO driver
 *
 * Copyright (C) 2006 Texas Instruments.
 *       Original author: Purushotam Kumar
 * Copyright (C) 2009 David Brownell
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

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/cpufreq.h>
#include <linux/mmc/host.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/mmc.h>

#include <linux/platform_data/mmc-davinci.h>
#include <mach/edma.h>

/*
 * Register Definitions
 */
#define DAVINCI_MMCCTL       0x00 /* Control Register                  */
#define DAVINCI_MMCCLK       0x04 /* Memory Clock Control Register     */
#define DAVINCI_MMCST0       0x08 /* Status Register 0                 */
#define DAVINCI_MMCST1       0x0C /* Status Register 1                 */
#define DAVINCI_MMCIM        0x10 /* Interrupt Mask Register           */
#define DAVINCI_MMCTOR       0x14 /* Response Time-Out Register        */
#define DAVINCI_MMCTOD       0x18 /* Data Read Time-Out Register       */
#define DAVINCI_MMCBLEN      0x1C /* Block Length Register             */
#define DAVINCI_MMCNBLK      0x20 /* Number of Blocks Register         */
#define DAVINCI_MMCNBLC      0x24 /* Number of Blocks Counter Register */
#define DAVINCI_MMCDRR       0x28 /* Data Receive Register             */
#define DAVINCI_MMCDXR       0x2C /* Data Transmit Register            */
#define DAVINCI_MMCCMD       0x30 /* Command Register                  */
#define DAVINCI_MMCARGHL     0x34 /* Argument Register                 */
#define DAVINCI_MMCRSP01     0x38 /* Response Register 0 and 1         */
#define DAVINCI_MMCRSP23     0x3C /* Response Register 0 and 1         */
#define DAVINCI_MMCRSP45     0x40 /* Response Register 0 and 1         */
#define DAVINCI_MMCRSP67     0x44 /* Response Register 0 and 1         */
#define DAVINCI_MMCDRSP      0x48 /* Data Response Register            */
#define DAVINCI_MMCETOK      0x4C
#define DAVINCI_MMCCIDX      0x50 /* Command Index Register            */
#define DAVINCI_MMCCKC       0x54
#define DAVINCI_MMCTORC      0x58
#define DAVINCI_MMCTODC      0x5C
#define DAVINCI_MMCBLNC      0x60
#define DAVINCI_SDIOCTL      0x64
#define DAVINCI_SDIOST0      0x68
#define DAVINCI_SDIOIEN      0x6C
#define DAVINCI_SDIOIST      0x70
#define DAVINCI_MMCFIFOCTL   0x74 /* FIFO Control Register             */

/* DAVINCI_MMCCTL definitions */
#define MMCCTL_DATRST         (1 << 0)
#define MMCCTL_CMDRST         (1 << 1)
#define MMCCTL_WIDTH_8_BIT    (1 << 8)
#define MMCCTL_WIDTH_4_BIT    (1 << 2)
#define MMCCTL_DATEG_DISABLED (0 << 6)
#define MMCCTL_DATEG_RISING   (1 << 6)
#define MMCCTL_DATEG_FALLING  (2 << 6)
#define MMCCTL_DATEG_BOTH     (3 << 6)
#define MMCCTL_PERMDR_LE      (0 << 9)
#define MMCCTL_PERMDR_BE      (1 << 9)
#define MMCCTL_PERMDX_LE      (0 << 10)
#define MMCCTL_PERMDX_BE      (1 << 10)

/* DAVINCI_MMCCLK definitions */
#define MMCCLK_CLKEN          (1 << 8)
#define MMCCLK_CLKRT_MASK     (0xFF << 0)

/* IRQ bit definitions, for DAVINCI_MMCST0 and DAVINCI_MMCIM */
#define MMCST0_DATDNE         BIT(0)	/* data done */
#define MMCST0_BSYDNE         BIT(1)	/* busy done */
#define MMCST0_RSPDNE         BIT(2)	/* command done */
#define MMCST0_TOUTRD         BIT(3)	/* data read timeout */
#define MMCST0_TOUTRS         BIT(4)	/* command response timeout */
#define MMCST0_CRCWR          BIT(5)	/* data write CRC error */
#define MMCST0_CRCRD          BIT(6)	/* data read CRC error */
#define MMCST0_CRCRS          BIT(7)	/* command response CRC error */
#define MMCST0_DXRDY          BIT(9)	/* data transmit ready (fifo empty) */
#define MMCST0_DRRDY          BIT(10)	/* data receive ready (data in fifo)*/
#define MMCST0_DATED          BIT(11)	/* DAT3 edge detect */
#define MMCST0_TRNDNE         BIT(12)	/* transfer done */

/* DAVINCI_MMCST1 definitions */
#define MMCST1_BUSY           (1 << 0)

/* DAVINCI_MMCCMD definitions */
#define MMCCMD_CMD_MASK       (0x3F << 0)
#define MMCCMD_PPLEN          (1 << 7)
#define MMCCMD_BSYEXP         (1 << 8)
#define MMCCMD_RSPFMT_MASK    (3 << 9)
#define MMCCMD_RSPFMT_NONE    (0 << 9)
#define MMCCMD_RSPFMT_R1456   (1 << 9)
#define MMCCMD_RSPFMT_R2      (2 << 9)
#define MMCCMD_RSPFMT_R3      (3 << 9)
#define MMCCMD_DTRW           (1 << 11)
#define MMCCMD_STRMTP         (1 << 12)
#define MMCCMD_WDATX          (1 << 13)
#define MMCCMD_INITCK         (1 << 14)
#define MMCCMD_DCLR           (1 << 15)
#define MMCCMD_DMATRIG        (1 << 16)

/* DAVINCI_MMCFIFOCTL definitions */
#define MMCFIFOCTL_FIFORST    (1 << 0)
#define MMCFIFOCTL_FIFODIR_WR (1 << 1)
#define MMCFIFOCTL_FIFODIR_RD (0 << 1)
#define MMCFIFOCTL_FIFOLEV    (1 << 2) /* 0 = 128 bits, 1 = 256 bits */
#define MMCFIFOCTL_ACCWD_4    (0 << 3) /* access width of 4 bytes    */
#define MMCFIFOCTL_ACCWD_3    (1 << 3) /* access width of 3 bytes    */
#define MMCFIFOCTL_ACCWD_2    (2 << 3) /* access width of 2 bytes    */
#define MMCFIFOCTL_ACCWD_1    (3 << 3) /* access width of 1 byte     */

/* DAVINCI_SDIOST0 definitions */
#define SDIOST0_DAT1_HI       BIT(0)

/* DAVINCI_SDIOIEN definitions */
#define SDIOIEN_IOINTEN       BIT(0)

/* DAVINCI_SDIOIST definitions */
#define SDIOIST_IOINT         BIT(0)

/* MMCSD Init clock in Hz in opendrain mode */
#define MMCSD_INIT_CLOCK		200000

/*
 * One scatterlist dma "segment" is at most MAX_CCNT rw_threshold units,
 * and we handle up to MAX_NR_SG segments.  MMC_BLOCK_BOUNCE kicks in only
 * for drivers with max_segs == 1, making the segments bigger (64KB)
 * than the page or two that's otherwise typical. nr_sg (passed from
 * platform data) == 16 gives at least the same throughput boost, using
 * EDMA transfer linkage instead of spending CPU time copying pages.
 */
#define MAX_CCNT	((1 << 16) - 1)

#define MAX_NR_SG	16

static unsigned rw_threshold = 32;
module_param(rw_threshold, uint, S_IRUGO);
MODULE_PARM_DESC(rw_threshold,
		"Read/Write threshold. Default = 32");

static unsigned poll_threshold = 128;
module_param(poll_threshold, uint, S_IRUGO);
MODULE_PARM_DESC(poll_threshold,
		 "Polling transaction size threshold. Default = 128");

static unsigned poll_loopcount = 32;
module_param(poll_loopcount, uint, S_IRUGO);
MODULE_PARM_DESC(poll_loopcount,
		 "Maximum polling loop count. Default = 32");

static unsigned __initdata use_dma = 1;
module_param(use_dma, uint, 0);
MODULE_PARM_DESC(use_dma, "Whether to use DMA or not. Default = 1");

struct mmc_davinci_host {
	struct mmc_command *cmd;
	struct mmc_data *data;
	struct mmc_host *mmc;
	struct clk *clk;
	unsigned int mmc_input_clk;
	void __iomem *base;
	struct resource *mem_res;
	int mmc_irq, sdio_irq;
	unsigned char bus_mode;

#define DAVINCI_MMC_DATADIR_NONE	0
#define DAVINCI_MMC_DATADIR_READ	1
#define DAVINCI_MMC_DATADIR_WRITE	2
	unsigned char data_dir;
	unsigned char suspended;

	/* buffer is used during PIO of one scatterlist segment, and
	 * is updated along with buffer_bytes_left.  bytes_left applies
	 * to all N blocks of the PIO transfer.
	 */
	u8 *buffer;
	u32 buffer_bytes_left;
	u32 bytes_left;

	u32 rxdma, txdma;
	bool use_dma;
	bool do_dma;
	bool sdio_int;
	bool active_request;

	/* Scatterlist DMA uses one or more parameter RAM entries:
	 * the main one (associated with rxdma or txdma) plus zero or
	 * more links.  The entries for a given transfer differ only
	 * by memory buffer (address, length) and link field.
	 */
	struct edmacc_param	tx_template;
	struct edmacc_param	rx_template;
	unsigned		n_link;
	u32			links[MAX_NR_SG - 1];

	/* For PIO we walk scatterlists one segment at a time. */
	unsigned int		sg_len;
	struct scatterlist *sg;

	/* Version of the MMC/SD controller */
	u8 version;
	/* for ns in one cycle calculation */
	unsigned ns_in_one_cycle;
	/* Number of sg segments */
	u8 nr_sg;
#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif
};

static irqreturn_t mmc_davinci_irq(int irq, void *dev_id);

/* PIO only */
static void mmc_davinci_sg_to_buf(struct mmc_davinci_host *host)
{
	host->buffer_bytes_left = sg_dma_len(host->sg);
	host->buffer = sg_virt(host->sg);
	if (host->buffer_bytes_left > host->bytes_left)
		host->buffer_bytes_left = host->bytes_left;
}

static void davinci_fifo_data_trans(struct mmc_davinci_host *host,
					unsigned int n)
{
	u8 *p;
	unsigned int i;

	if (host->buffer_bytes_left == 0) {
		host->sg = sg_next(host->data->sg);
		mmc_davinci_sg_to_buf(host);
	}

	p = host->buffer;
	if (n > host->buffer_bytes_left)
		n = host->buffer_bytes_left;
	host->buffer_bytes_left -= n;
	host->bytes_left -= n;

	/* NOTE:  we never transfer more than rw_threshold bytes
	 * to/from the fifo here; there's no I/O overlap.
	 * This also assumes that access width( i.e. ACCWD) is 4 bytes
	 */
	if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE) {
		for (i = 0; i < (n >> 2); i++) {
			writel(*((u32 *)p), host->base + DAVINCI_MMCDXR);
			p = p + 4;
		}
		if (n & 3) {
			iowrite8_rep(host->base + DAVINCI_MMCDXR, p, (n & 3));
			p = p + (n & 3);
		}
	} else {
		for (i = 0; i < (n >> 2); i++) {
			*((u32 *)p) = readl(host->base + DAVINCI_MMCDRR);
			p  = p + 4;
		}
		if (n & 3) {
			ioread8_rep(host->base + DAVINCI_MMCDRR, p, (n & 3));
			p = p + (n & 3);
		}
	}
	host->buffer = p;
}

static void mmc_davinci_start_command(struct mmc_davinci_host *host,
		struct mmc_command *cmd)
{
	u32 cmd_reg = 0;
	u32 im_val;

	dev_dbg(mmc_dev(host->mmc), "CMD%d, arg 0x%08x%s\n",
		cmd->opcode, cmd->arg,
		({ char *s;
		switch (mmc_resp_type(cmd)) {
		case MMC_RSP_R1:
			s = ", R1/R5/R6/R7 response";
			break;
		case MMC_RSP_R1B:
			s = ", R1b response";
			break;
		case MMC_RSP_R2:
			s = ", R2 response";
			break;
		case MMC_RSP_R3:
			s = ", R3/R4 response";
			break;
		default:
			s = ", (R? response)";
			break;
		}; s; }));
	host->cmd = cmd;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1B:
		/* There's some spec confusion about when R1B is
		 * allowed, but if the card doesn't issue a BUSY
		 * then it's harmless for us to allow it.
		 */
		cmd_reg |= MMCCMD_BSYEXP;
		/* FALLTHROUGH */
	case MMC_RSP_R1:		/* 48 bits, CRC */
		cmd_reg |= MMCCMD_RSPFMT_R1456;
		break;
	case MMC_RSP_R2:		/* 136 bits, CRC */
		cmd_reg |= MMCCMD_RSPFMT_R2;
		break;
	case MMC_RSP_R3:		/* 48 bits, no CRC */
		cmd_reg |= MMCCMD_RSPFMT_R3;
		break;
	default:
		cmd_reg |= MMCCMD_RSPFMT_NONE;
		dev_dbg(mmc_dev(host->mmc), "unknown resp_type %04x\n",
			mmc_resp_type(cmd));
		break;
	}

	/* Set command index */
	cmd_reg |= cmd->opcode;

	/* Enable EDMA transfer triggers */
	if (host->do_dma)
		cmd_reg |= MMCCMD_DMATRIG;

	if (host->version == MMC_CTLR_VERSION_2 && host->data != NULL &&
			host->data_dir == DAVINCI_MMC_DATADIR_READ)
		cmd_reg |= MMCCMD_DMATRIG;

	/* Setting whether command involves data transfer or not */
	if (cmd->data)
		cmd_reg |= MMCCMD_WDATX;

	/* Setting whether stream or block transfer */
	if (cmd->flags & MMC_DATA_STREAM)
		cmd_reg |= MMCCMD_STRMTP;

	/* Setting whether data read or write */
	if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE)
		cmd_reg |= MMCCMD_DTRW;

	if (host->bus_mode == MMC_BUSMODE_PUSHPULL)
		cmd_reg |= MMCCMD_PPLEN;

	/* set Command timeout */
	writel(0x1FFF, host->base + DAVINCI_MMCTOR);

	/* Enable interrupt (calculate here, defer until FIFO is stuffed). */
	im_val =  MMCST0_RSPDNE | MMCST0_CRCRS | MMCST0_TOUTRS;
	if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE) {
		im_val |= MMCST0_DATDNE | MMCST0_CRCWR;

		if (!host->do_dma)
			im_val |= MMCST0_DXRDY;
	} else if (host->data_dir == DAVINCI_MMC_DATADIR_READ) {
		im_val |= MMCST0_DATDNE | MMCST0_CRCRD | MMCST0_TOUTRD;

		if (!host->do_dma)
			im_val |= MMCST0_DRRDY;
	}

	/*
	 * Before non-DMA WRITE commands the controller needs priming:
	 * FIFO should be populated with 32 bytes i.e. whatever is the FIFO size
	 */
	if (!host->do_dma && (host->data_dir == DAVINCI_MMC_DATADIR_WRITE))
		davinci_fifo_data_trans(host, rw_threshold);

	writel(cmd->arg, host->base + DAVINCI_MMCARGHL);
	writel(cmd_reg,  host->base + DAVINCI_MMCCMD);

	host->active_request = true;

	if (!host->do_dma && host->bytes_left <= poll_threshold) {
		u32 count = poll_loopcount;

		while (host->active_request && count--) {
			mmc_davinci_irq(0, host);
			cpu_relax();
		}
	}

	if (host->active_request)
		writel(im_val, host->base + DAVINCI_MMCIM);
}

/*----------------------------------------------------------------------*/

/* DMA infrastructure */

static void davinci_abort_dma(struct mmc_davinci_host *host)
{
	int sync_dev;

	if (host->data_dir == DAVINCI_MMC_DATADIR_READ)
		sync_dev = host->rxdma;
	else
		sync_dev = host->txdma;

	edma_stop(sync_dev);
	edma_clean_channel(sync_dev);
}

static void
mmc_davinci_xfer_done(struct mmc_davinci_host *host, struct mmc_data *data);

static void mmc_davinci_dma_cb(unsigned channel, u16 ch_status, void *data)
{
	if (DMA_COMPLETE != ch_status) {
		struct mmc_davinci_host *host = data;

		/* Currently means:  DMA Event Missed, or "null" transfer
		 * request was seen.  In the future, TC errors (like bad
		 * addresses) might be presented too.
		 */
		dev_warn(mmc_dev(host->mmc), "DMA %s error\n",
			(host->data->flags & MMC_DATA_WRITE)
				? "write" : "read");
		host->data->error = -EIO;
		mmc_davinci_xfer_done(host, host->data);
	}
}

/* Set up tx or rx template, to be modified and updated later */
static void __init mmc_davinci_dma_setup(struct mmc_davinci_host *host,
		bool tx, struct edmacc_param *template)
{
	unsigned	sync_dev;
	const u16	acnt = 4;
	const u16	bcnt = rw_threshold >> 2;
	const u16	ccnt = 0;
	u32		src_port = 0;
	u32		dst_port = 0;
	s16		src_bidx, dst_bidx;
	s16		src_cidx, dst_cidx;

	/*
	 * A-B Sync transfer:  each DMA request is for one "frame" of
	 * rw_threshold bytes, broken into "acnt"-size chunks repeated
	 * "bcnt" times.  Each segment needs "ccnt" such frames; since
	 * we tell the block layer our mmc->max_seg_size limit, we can
	 * trust (later) that it's within bounds.
	 *
	 * The FIFOs are read/written in 4-byte chunks (acnt == 4) and
	 * EDMA will optimize memory operations to use larger bursts.
	 */
	if (tx) {
		sync_dev = host->txdma;

		/* src_prt, ccnt, and link to be set up later */
		src_bidx = acnt;
		src_cidx = acnt * bcnt;

		dst_port = host->mem_res->start + DAVINCI_MMCDXR;
		dst_bidx = 0;
		dst_cidx = 0;
	} else {
		sync_dev = host->rxdma;

		src_port = host->mem_res->start + DAVINCI_MMCDRR;
		src_bidx = 0;
		src_cidx = 0;

		/* dst_prt, ccnt, and link to be set up later */
		dst_bidx = acnt;
		dst_cidx = acnt * bcnt;
	}

	/*
	 * We can't use FIFO mode for the FIFOs because MMC FIFO addresses
	 * are not 256-bit (32-byte) aligned.  So we use INCR, and the W8BIT
	 * parameter is ignored.
	 */
	edma_set_src(sync_dev, src_port, INCR, W8BIT);
	edma_set_dest(sync_dev, dst_port, INCR, W8BIT);

	edma_set_src_index(sync_dev, src_bidx, src_cidx);
	edma_set_dest_index(sync_dev, dst_bidx, dst_cidx);

	edma_set_transfer_params(sync_dev, acnt, bcnt, ccnt, 8, ABSYNC);

	edma_read_slot(sync_dev, template);

	/* don't bother with irqs or chaining */
	template->opt |= EDMA_CHAN_SLOT(sync_dev) << 12;
}

static void mmc_davinci_send_dma_request(struct mmc_davinci_host *host,
		struct mmc_data *data)
{
	struct edmacc_param	*template;
	int			channel, slot;
	unsigned		link;
	struct scatterlist	*sg;
	unsigned		sg_len;
	unsigned		bytes_left = host->bytes_left;
	const unsigned		shift = ffs(rw_threshold) - 1;

	if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE) {
		template = &host->tx_template;
		channel = host->txdma;
	} else {
		template = &host->rx_template;
		channel = host->rxdma;
	}

	/* We know sg_len and ccnt will never be out of range because
	 * we told the mmc layer which in turn tells the block layer
	 * to ensure that it only hands us one scatterlist segment
	 * per EDMA PARAM entry.  Update the PARAM
	 * entries needed for each segment of this scatterlist.
	 */
	for (slot = channel, link = 0, sg = data->sg, sg_len = host->sg_len;
			sg_len-- != 0 && bytes_left;
			sg = sg_next(sg), slot = host->links[link++]) {
		u32		buf = sg_dma_address(sg);
		unsigned	count = sg_dma_len(sg);

		template->link_bcntrld = sg_len
				? (EDMA_CHAN_SLOT(host->links[link]) << 5)
				: 0xffff;

		if (count > bytes_left)
			count = bytes_left;
		bytes_left -= count;

		if (host->data_dir == DAVINCI_MMC_DATADIR_WRITE)
			template->src = buf;
		else
			template->dst = buf;
		template->ccnt = count >> shift;

		edma_write_slot(slot, template);
	}

	if (host->version == MMC_CTLR_VERSION_2)
		edma_clear_event(channel);

	edma_start(channel);
}

static int mmc_davinci_start_dma_transfer(struct mmc_davinci_host *host,
		struct mmc_data *data)
{
	int i;
	int mask = rw_threshold - 1;

	host->sg_len = dma_map_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
				((data->flags & MMC_DATA_WRITE)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE));

	/* no individual DMA segment should need a partial FIFO */
	for (i = 0; i < host->sg_len; i++) {
		if (sg_dma_len(data->sg + i) & mask) {
			dma_unmap_sg(mmc_dev(host->mmc),
					data->sg, data->sg_len,
					(data->flags & MMC_DATA_WRITE)
					? DMA_TO_DEVICE
					: DMA_FROM_DEVICE);
			return -1;
		}
	}

	host->do_dma = 1;
	mmc_davinci_send_dma_request(host, data);

	return 0;
}

static void __init_or_module
davinci_release_dma_channels(struct mmc_davinci_host *host)
{
	unsigned	i;

	if (!host->use_dma)
		return;

	for (i = 0; i < host->n_link; i++)
		edma_free_slot(host->links[i]);

	edma_free_channel(host->txdma);
	edma_free_channel(host->rxdma);
}

static int __init davinci_acquire_dma_channels(struct mmc_davinci_host *host)
{
	u32 link_size;
	int r, i;

	/* Acquire master DMA write channel */
	r = edma_alloc_channel(host->txdma, mmc_davinci_dma_cb, host,
			EVENTQ_DEFAULT);
	if (r < 0) {
		dev_warn(mmc_dev(host->mmc), "alloc %s channel err %d\n",
				"tx", r);
		return r;
	}
	mmc_davinci_dma_setup(host, true, &host->tx_template);

	/* Acquire master DMA read channel */
	r = edma_alloc_channel(host->rxdma, mmc_davinci_dma_cb, host,
			EVENTQ_DEFAULT);
	if (r < 0) {
		dev_warn(mmc_dev(host->mmc), "alloc %s channel err %d\n",
				"rx", r);
		goto free_master_write;
	}
	mmc_davinci_dma_setup(host, false, &host->rx_template);

	/* Allocate parameter RAM slots, which will later be bound to a
	 * channel as needed to handle a scatterlist.
	 */
	link_size = min_t(unsigned, host->nr_sg, ARRAY_SIZE(host->links));
	for (i = 0; i < link_size; i++) {
		r = edma_alloc_slot(EDMA_CTLR(host->txdma), EDMA_SLOT_ANY);
		if (r < 0) {
			dev_dbg(mmc_dev(host->mmc), "dma PaRAM alloc --> %d\n",
				r);
			break;
		}
		host->links[i] = r;
	}
	host->n_link = i;

	return 0;

free_master_write:
	edma_free_channel(host->txdma);

	return r;
}

/*----------------------------------------------------------------------*/

static void
mmc_davinci_prepare_data(struct mmc_davinci_host *host, struct mmc_request *req)
{
	int fifo_lev = (rw_threshold == 32) ? MMCFIFOCTL_FIFOLEV : 0;
	int timeout;
	struct mmc_data *data = req->data;

	if (host->version == MMC_CTLR_VERSION_2)
		fifo_lev = (rw_threshold == 64) ? MMCFIFOCTL_FIFOLEV : 0;

	host->data = data;
	if (data == NULL) {
		host->data_dir = DAVINCI_MMC_DATADIR_NONE;
		writel(0, host->base + DAVINCI_MMCBLEN);
		writel(0, host->base + DAVINCI_MMCNBLK);
		return;
	}

	dev_dbg(mmc_dev(host->mmc), "%s %s, %d blocks of %d bytes\n",
		(data->flags & MMC_DATA_STREAM) ? "stream" : "block",
		(data->flags & MMC_DATA_WRITE) ? "write" : "read",
		data->blocks, data->blksz);
	dev_dbg(mmc_dev(host->mmc), "  DTO %d cycles + %d ns\n",
		data->timeout_clks, data->timeout_ns);
	timeout = data->timeout_clks +
		(data->timeout_ns / host->ns_in_one_cycle);
	if (timeout > 0xffff)
		timeout = 0xffff;

	writel(timeout, host->base + DAVINCI_MMCTOD);
	writel(data->blocks, host->base + DAVINCI_MMCNBLK);
	writel(data->blksz, host->base + DAVINCI_MMCBLEN);

	/* Configure the FIFO */
	switch (data->flags & MMC_DATA_WRITE) {
	case MMC_DATA_WRITE:
		host->data_dir = DAVINCI_MMC_DATADIR_WRITE;
		writel(fifo_lev | MMCFIFOCTL_FIFODIR_WR | MMCFIFOCTL_FIFORST,
			host->base + DAVINCI_MMCFIFOCTL);
		writel(fifo_lev | MMCFIFOCTL_FIFODIR_WR,
			host->base + DAVINCI_MMCFIFOCTL);
		break;

	default:
		host->data_dir = DAVINCI_MMC_DATADIR_READ;
		writel(fifo_lev | MMCFIFOCTL_FIFODIR_RD | MMCFIFOCTL_FIFORST,
			host->base + DAVINCI_MMCFIFOCTL);
		writel(fifo_lev | MMCFIFOCTL_FIFODIR_RD,
			host->base + DAVINCI_MMCFIFOCTL);
		break;
	}

	host->buffer = NULL;
	host->bytes_left = data->blocks * data->blksz;

	/* For now we try to use DMA whenever we won't need partial FIFO
	 * reads or writes, either for the whole transfer (as tested here)
	 * or for any individual scatterlist segment (tested when we call
	 * start_dma_transfer).
	 *
	 * While we *could* change that, unusual block sizes are rarely
	 * used.  The occasional fallback to PIO should't hurt.
	 */
	if (host->use_dma && (host->bytes_left & (rw_threshold - 1)) == 0
			&& mmc_davinci_start_dma_transfer(host, data) == 0) {
		/* zero this to ensure we take no PIO paths */
		host->bytes_left = 0;
	} else {
		/* Revert to CPU Copy */
		host->sg_len = data->sg_len;
		host->sg = host->data->sg;
		mmc_davinci_sg_to_buf(host);
	}
}

static void mmc_davinci_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct mmc_davinci_host *host = mmc_priv(mmc);
	unsigned long timeout = jiffies + msecs_to_jiffies(900);
	u32 mmcst1 = 0;

	/* Card may still be sending BUSY after a previous operation,
	 * typically some kind of write.  If so, we can't proceed yet.
	 */
	while (time_before(jiffies, timeout)) {
		mmcst1  = readl(host->base + DAVINCI_MMCST1);
		if (!(mmcst1 & MMCST1_BUSY))
			break;
		cpu_relax();
	}
	if (mmcst1 & MMCST1_BUSY) {
		dev_err(mmc_dev(host->mmc), "still BUSY? bad ... \n");
		req->cmd->error = -ETIMEDOUT;
		mmc_request_done(mmc, req);
		return;
	}

	host->do_dma = 0;
	mmc_davinci_prepare_data(host, req);
	mmc_davinci_start_command(host, req->cmd);
}

static unsigned int calculate_freq_for_card(struct mmc_davinci_host *host,
	unsigned int mmc_req_freq)
{
	unsigned int mmc_freq = 0, mmc_pclk = 0, mmc_push_pull_divisor = 0;

	mmc_pclk = host->mmc_input_clk;
	if (mmc_req_freq && mmc_pclk > (2 * mmc_req_freq))
		mmc_push_pull_divisor = ((unsigned int)mmc_pclk
				/ (2 * mmc_req_freq)) - 1;
	else
		mmc_push_pull_divisor = 0;

	mmc_freq = (unsigned int)mmc_pclk
		/ (2 * (mmc_push_pull_divisor + 1));

	if (mmc_freq > mmc_req_freq)
		mmc_push_pull_divisor = mmc_push_pull_divisor + 1;
	/* Convert ns to clock cycles */
	if (mmc_req_freq <= 400000)
		host->ns_in_one_cycle = (1000000) / (((mmc_pclk
				/ (2 * (mmc_push_pull_divisor + 1)))/1000));
	else
		host->ns_in_one_cycle = (1000000) / (((mmc_pclk
				/ (2 * (mmc_push_pull_divisor + 1)))/1000000));

	return mmc_push_pull_divisor;
}

static void calculate_clk_divider(struct mmc_host *mmc, struct mmc_ios *ios)
{
	unsigned int open_drain_freq = 0, mmc_pclk = 0;
	unsigned int mmc_push_pull_freq = 0;
	struct mmc_davinci_host *host = mmc_priv(mmc);

	if (ios->bus_mode == MMC_BUSMODE_OPENDRAIN) {
		u32 temp;

		/* Ignoring the init clock value passed for fixing the inter
		 * operability with different cards.
		 */
		open_drain_freq = ((unsigned int)mmc_pclk
				/ (2 * MMCSD_INIT_CLOCK)) - 1;

		if (open_drain_freq > 0xFF)
			open_drain_freq = 0xFF;

		temp = readl(host->base + DAVINCI_MMCCLK) & ~MMCCLK_CLKRT_MASK;
		temp |= open_drain_freq;
		writel(temp, host->base + DAVINCI_MMCCLK);

		/* Convert ns to clock cycles */
		host->ns_in_one_cycle = (1000000) / (MMCSD_INIT_CLOCK/1000);
	} else {
		u32 temp;
		mmc_push_pull_freq = calculate_freq_for_card(host, ios->clock);

		if (mmc_push_pull_freq > 0xFF)
			mmc_push_pull_freq = 0xFF;

		temp = readl(host->base + DAVINCI_MMCCLK) & ~MMCCLK_CLKEN;
		writel(temp, host->base + DAVINCI_MMCCLK);

		udelay(10);

		temp = readl(host->base + DAVINCI_MMCCLK) & ~MMCCLK_CLKRT_MASK;
		temp |= mmc_push_pull_freq;
		writel(temp, host->base + DAVINCI_MMCCLK);

		writel(temp | MMCCLK_CLKEN, host->base + DAVINCI_MMCCLK);

		udelay(10);
	}
}

static void mmc_davinci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmc_davinci_host *host = mmc_priv(mmc);
	struct platform_device *pdev = to_platform_device(mmc->parent);
	struct davinci_mmc_config *config = pdev->dev.platform_data;

	dev_dbg(mmc_dev(host->mmc),
		"clock %dHz busmode %d powermode %d Vdd %04x\n",
		ios->clock, ios->bus_mode, ios->power_mode,
		ios->vdd);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		if (config && config->set_power)
			config->set_power(pdev->id, false);
		break;
	case MMC_POWER_UP:
		if (config && config->set_power)
			config->set_power(pdev->id, true);
		break;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_8:
		dev_dbg(mmc_dev(host->mmc), "Enabling 8 bit mode\n");
		writel((readl(host->base + DAVINCI_MMCCTL) &
			~MMCCTL_WIDTH_4_BIT) | MMCCTL_WIDTH_8_BIT,
			host->base + DAVINCI_MMCCTL);
		break;
	case MMC_BUS_WIDTH_4:
		dev_dbg(mmc_dev(host->mmc), "Enabling 4 bit mode\n");
		if (host->version == MMC_CTLR_VERSION_2)
			writel((readl(host->base + DAVINCI_MMCCTL) &
				~MMCCTL_WIDTH_8_BIT) | MMCCTL_WIDTH_4_BIT,
				host->base + DAVINCI_MMCCTL);
		else
			writel(readl(host->base + DAVINCI_MMCCTL) |
				MMCCTL_WIDTH_4_BIT,
				host->base + DAVINCI_MMCCTL);
		break;
	case MMC_BUS_WIDTH_1:
		dev_dbg(mmc_dev(host->mmc), "Enabling 1 bit mode\n");
		if (host->version == MMC_CTLR_VERSION_2)
			writel(readl(host->base + DAVINCI_MMCCTL) &
				~(MMCCTL_WIDTH_8_BIT | MMCCTL_WIDTH_4_BIT),
				host->base + DAVINCI_MMCCTL);
		else
			writel(readl(host->base + DAVINCI_MMCCTL) &
				~MMCCTL_WIDTH_4_BIT,
				host->base + DAVINCI_MMCCTL);
		break;
	}

	calculate_clk_divider(mmc, ios);

	host->bus_mode = ios->bus_mode;
	if (ios->power_mode == MMC_POWER_UP) {
		unsigned long timeout = jiffies + msecs_to_jiffies(50);
		bool lose = true;

		/* Send clock cycles, poll completion */
		writel(0, host->base + DAVINCI_MMCARGHL);
		writel(MMCCMD_INITCK, host->base + DAVINCI_MMCCMD);
		while (time_before(jiffies, timeout)) {
			u32 tmp = readl(host->base + DAVINCI_MMCST0);

			if (tmp & MMCST0_RSPDNE) {
				lose = false;
				break;
			}
			cpu_relax();
		}
		if (lose)
			dev_warn(mmc_dev(host->mmc), "powerup timeout\n");
	}

	/* FIXME on power OFF, reset things ... */
}

static void
mmc_davinci_xfer_done(struct mmc_davinci_host *host, struct mmc_data *data)
{
	host->data = NULL;

	if (host->mmc->caps & MMC_CAP_SDIO_IRQ) {
		/*
		 * SDIO Interrupt Detection work-around as suggested by
		 * Davinci Errata (TMS320DM355 Silicon Revision 1.1 Errata
		 * 2.1.6): Signal SDIO interrupt only if it is enabled by core
		 */
		if (host->sdio_int && !(readl(host->base + DAVINCI_SDIOST0) &
					SDIOST0_DAT1_HI)) {
			writel(SDIOIST_IOINT, host->base + DAVINCI_SDIOIST);
			mmc_signal_sdio_irq(host->mmc);
		}
	}

	if (host->do_dma) {
		davinci_abort_dma(host);

		dma_unmap_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
			     (data->flags & MMC_DATA_WRITE)
			     ? DMA_TO_DEVICE
			     : DMA_FROM_DEVICE);
		host->do_dma = false;
	}
	host->data_dir = DAVINCI_MMC_DATADIR_NONE;

	if (!data->stop || (host->cmd && host->cmd->error)) {
		mmc_request_done(host->mmc, data->mrq);
		writel(0, host->base + DAVINCI_MMCIM);
		host->active_request = false;
	} else
		mmc_davinci_start_command(host, data->stop);
}

static void mmc_davinci_cmd_done(struct mmc_davinci_host *host,
				 struct mmc_command *cmd)
{
	host->cmd = NULL;

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			/* response type 2 */
			cmd->resp[3] = readl(host->base + DAVINCI_MMCRSP01);
			cmd->resp[2] = readl(host->base + DAVINCI_MMCRSP23);
			cmd->resp[1] = readl(host->base + DAVINCI_MMCRSP45);
			cmd->resp[0] = readl(host->base + DAVINCI_MMCRSP67);
		} else {
			/* response types 1, 1b, 3, 4, 5, 6 */
			cmd->resp[0] = readl(host->base + DAVINCI_MMCRSP67);
		}
	}

	if (host->data == NULL || cmd->error) {
		if (cmd->error == -ETIMEDOUT)
			cmd->mrq->cmd->retries = 0;
		mmc_request_done(host->mmc, cmd->mrq);
		writel(0, host->base + DAVINCI_MMCIM);
		host->active_request = false;
	}
}

static inline void mmc_davinci_reset_ctrl(struct mmc_davinci_host *host,
								int val)
{
	u32 temp;

	temp = readl(host->base + DAVINCI_MMCCTL);
	if (val)	/* reset */
		temp |= MMCCTL_CMDRST | MMCCTL_DATRST;
	else		/* enable */
		temp &= ~(MMCCTL_CMDRST | MMCCTL_DATRST);

	writel(temp, host->base + DAVINCI_MMCCTL);
	udelay(10);
}

static void
davinci_abort_data(struct mmc_davinci_host *host, struct mmc_data *data)
{
	mmc_davinci_reset_ctrl(host, 1);
	mmc_davinci_reset_ctrl(host, 0);
}

static irqreturn_t mmc_davinci_sdio_irq(int irq, void *dev_id)
{
	struct mmc_davinci_host *host = dev_id;
	unsigned int status;

	status = readl(host->base + DAVINCI_SDIOIST);
	if (status & SDIOIST_IOINT) {
		dev_dbg(mmc_dev(host->mmc),
			"SDIO interrupt status %x\n", status);
		writel(status | SDIOIST_IOINT, host->base + DAVINCI_SDIOIST);
		mmc_signal_sdio_irq(host->mmc);
	}
	return IRQ_HANDLED;
}

static irqreturn_t mmc_davinci_irq(int irq, void *dev_id)
{
	struct mmc_davinci_host *host = (struct mmc_davinci_host *)dev_id;
	unsigned int status, qstatus;
	int end_command = 0;
	int end_transfer = 0;
	struct mmc_data *data = host->data;

	if (host->cmd == NULL && host->data == NULL) {
		status = readl(host->base + DAVINCI_MMCST0);
		dev_dbg(mmc_dev(host->mmc),
			"Spurious interrupt 0x%04x\n", status);
		/* Disable the interrupt from mmcsd */
		writel(0, host->base + DAVINCI_MMCIM);
		return IRQ_NONE;
	}

	status = readl(host->base + DAVINCI_MMCST0);
	qstatus = status;

	/* handle FIFO first when using PIO for data.
	 * bytes_left will decrease to zero as I/O progress and status will
	 * read zero over iteration because this controller status
	 * register(MMCST0) reports any status only once and it is cleared
	 * by read. So, it is not unbouned loop even in the case of
	 * non-dma.
	 */
	if (host->bytes_left && (status & (MMCST0_DXRDY | MMCST0_DRRDY))) {
		unsigned long im_val;

		/*
		 * If interrupts fire during the following loop, they will be
		 * handled by the handler, but the PIC will still buffer these.
		 * As a result, the handler will be called again to serve these
		 * needlessly. In order to avoid these spurious interrupts,
		 * keep interrupts masked during the loop.
		 */
		im_val = readl(host->base + DAVINCI_MMCIM);
		writel(0, host->base + DAVINCI_MMCIM);

		do {
			davinci_fifo_data_trans(host, rw_threshold);
			status = readl(host->base + DAVINCI_MMCST0);
			qstatus |= status;
		} while (host->bytes_left &&
			 (status & (MMCST0_DXRDY | MMCST0_DRRDY)));

		/*
		 * If an interrupt is pending, it is assumed it will fire when
		 * it is unmasked. This assumption is also taken when the MMCIM
		 * is first set. Otherwise, writing to MMCIM after reading the
		 * status is race-prone.
		 */
		writel(im_val, host->base + DAVINCI_MMCIM);
	}

	if (qstatus & MMCST0_DATDNE) {
		/* All blocks sent/received, and CRC checks passed */
		if (data != NULL) {
			if ((host->do_dma == 0) && (host->bytes_left > 0)) {
				/* if datasize < rw_threshold
				 * no RX ints are generated
				 */
				davinci_fifo_data_trans(host, host->bytes_left);
			}
			end_transfer = 1;
			data->bytes_xfered = data->blocks * data->blksz;
		} else {
			dev_err(mmc_dev(host->mmc),
					"DATDNE with no host->data\n");
		}
	}

	if (qstatus & MMCST0_TOUTRD) {
		/* Read data timeout */
		data->error = -ETIMEDOUT;
		end_transfer = 1;

		dev_dbg(mmc_dev(host->mmc),
			"read data timeout, status %x\n",
			qstatus);

		davinci_abort_data(host, data);
	}

	if (qstatus & (MMCST0_CRCWR | MMCST0_CRCRD)) {
		/* Data CRC error */
		data->error = -EILSEQ;
		end_transfer = 1;

		/* NOTE:  this controller uses CRCWR to report both CRC
		 * errors and timeouts (on writes).  MMCDRSP values are
		 * only weakly documented, but 0x9f was clearly a timeout
		 * case and the two three-bit patterns in various SD specs
		 * (101, 010) aren't part of it ...
		 */
		if (qstatus & MMCST0_CRCWR) {
			u32 temp = readb(host->base + DAVINCI_MMCDRSP);

			if (temp == 0x9f)
				data->error = -ETIMEDOUT;
		}
		dev_dbg(mmc_dev(host->mmc), "data %s %s error\n",
			(qstatus & MMCST0_CRCWR) ? "write" : "read",
			(data->error == -ETIMEDOUT) ? "timeout" : "CRC");

		davinci_abort_data(host, data);
	}

	if (qstatus & MMCST0_TOUTRS) {
		/* Command timeout */
		if (host->cmd) {
			dev_dbg(mmc_dev(host->mmc),
				"CMD%d timeout, status %x\n",
				host->cmd->opcode, qstatus);
			host->cmd->error = -ETIMEDOUT;
			if (data) {
				end_transfer = 1;
				davinci_abort_data(host, data);
			} else
				end_command = 1;
		}
	}

	if (qstatus & MMCST0_CRCRS) {
		/* Command CRC error */
		dev_dbg(mmc_dev(host->mmc), "Command CRC error\n");
		if (host->cmd) {
			host->cmd->error = -EILSEQ;
			end_command = 1;
		}
	}

	if (qstatus & MMCST0_RSPDNE) {
		/* End of command phase */
		end_command = (int) host->cmd;
	}

	if (end_command)
		mmc_davinci_cmd_done(host, host->cmd);
	if (end_transfer)
		mmc_davinci_xfer_done(host, data);
	return IRQ_HANDLED;
}

static int mmc_davinci_get_cd(struct mmc_host *mmc)
{
	struct platform_device *pdev = to_platform_device(mmc->parent);
	struct davinci_mmc_config *config = pdev->dev.platform_data;

	if (!config || !config->get_cd)
		return -ENOSYS;
	return config->get_cd(pdev->id);
}

static int mmc_davinci_get_ro(struct mmc_host *mmc)
{
	struct platform_device *pdev = to_platform_device(mmc->parent);
	struct davinci_mmc_config *config = pdev->dev.platform_data;

	if (!config || !config->get_ro)
		return -ENOSYS;
	return config->get_ro(pdev->id);
}

static void mmc_davinci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct mmc_davinci_host *host = mmc_priv(mmc);

	if (enable) {
		if (!(readl(host->base + DAVINCI_SDIOST0) & SDIOST0_DAT1_HI)) {
			writel(SDIOIST_IOINT, host->base + DAVINCI_SDIOIST);
			mmc_signal_sdio_irq(host->mmc);
		} else {
			host->sdio_int = true;
			writel(readl(host->base + DAVINCI_SDIOIEN) |
			       SDIOIEN_IOINTEN, host->base + DAVINCI_SDIOIEN);
		}
	} else {
		host->sdio_int = false;
		writel(readl(host->base + DAVINCI_SDIOIEN) & ~SDIOIEN_IOINTEN,
		       host->base + DAVINCI_SDIOIEN);
	}
}

static struct mmc_host_ops mmc_davinci_ops = {
	.request	= mmc_davinci_request,
	.set_ios	= mmc_davinci_set_ios,
	.get_cd		= mmc_davinci_get_cd,
	.get_ro		= mmc_davinci_get_ro,
	.enable_sdio_irq = mmc_davinci_enable_sdio_irq,
};

/*----------------------------------------------------------------------*/

#ifdef CONFIG_CPU_FREQ
static int mmc_davinci_cpufreq_transition(struct notifier_block *nb,
				     unsigned long val, void *data)
{
	struct mmc_davinci_host *host;
	unsigned int mmc_pclk;
	struct mmc_host *mmc;
	unsigned long flags;

	host = container_of(nb, struct mmc_davinci_host, freq_transition);
	mmc = host->mmc;
	mmc_pclk = clk_get_rate(host->clk);

	if (val == CPUFREQ_POSTCHANGE) {
		spin_lock_irqsave(&mmc->lock, flags);
		host->mmc_input_clk = mmc_pclk;
		calculate_clk_divider(mmc, &mmc->ios);
		spin_unlock_irqrestore(&mmc->lock, flags);
	}

	return 0;
}

static inline int mmc_davinci_cpufreq_register(struct mmc_davinci_host *host)
{
	host->freq_transition.notifier_call = mmc_davinci_cpufreq_transition;

	return cpufreq_register_notifier(&host->freq_transition,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void mmc_davinci_cpufreq_deregister(struct mmc_davinci_host *host)
{
	cpufreq_unregister_notifier(&host->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
}
#else
static inline int mmc_davinci_cpufreq_register(struct mmc_davinci_host *host)
{
	return 0;
}

static inline void mmc_davinci_cpufreq_deregister(struct mmc_davinci_host *host)
{
}
#endif
static void __init init_mmcsd_host(struct mmc_davinci_host *host)
{

	mmc_davinci_reset_ctrl(host, 1);

	writel(0, host->base + DAVINCI_MMCCLK);
	writel(MMCCLK_CLKEN, host->base + DAVINCI_MMCCLK);

	writel(0x1FFF, host->base + DAVINCI_MMCTOR);
	writel(0xFFFF, host->base + DAVINCI_MMCTOD);

	mmc_davinci_reset_ctrl(host, 0);
}

static int __init davinci_mmcsd_probe(struct platform_device *pdev)
{
	struct davinci_mmc_config *pdata = pdev->dev.platform_data;
	struct mmc_davinci_host *host = NULL;
	struct mmc_host *mmc = NULL;
	struct resource *r, *mem = NULL;
	int ret = 0, irq = 0;
	size_t mem_size;

	/* REVISIT:  when we're fully converted, fail if pdata is NULL */

	ret = -ENODEV;
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!r || irq == NO_IRQ)
		goto out;

	ret = -EBUSY;
	mem_size = resource_size(r);
	mem = request_mem_region(r->start, mem_size, pdev->name);
	if (!mem)
		goto out;

	ret = -ENOMEM;
	mmc = mmc_alloc_host(sizeof(struct mmc_davinci_host), &pdev->dev);
	if (!mmc)
		goto out;

	host = mmc_priv(mmc);
	host->mmc = mmc;	/* Important */

	r = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!r)
		goto out;
	host->rxdma = r->start;

	r = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!r)
		goto out;
	host->txdma = r->start;

	host->mem_res = mem;
	host->base = ioremap(mem->start, mem_size);
	if (!host->base)
		goto out;

	ret = -ENXIO;
	host->clk = clk_get(&pdev->dev, "MMCSDCLK");
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		goto out;
	}
	clk_enable(host->clk);
	host->mmc_input_clk = clk_get_rate(host->clk);

	init_mmcsd_host(host);

	if (pdata->nr_sg)
		host->nr_sg = pdata->nr_sg - 1;

	if (host->nr_sg > MAX_NR_SG || !host->nr_sg)
		host->nr_sg = MAX_NR_SG;

	host->use_dma = use_dma;
	host->mmc_irq = irq;
	host->sdio_irq = platform_get_irq(pdev, 1);

	if (host->use_dma && davinci_acquire_dma_channels(host) != 0)
		host->use_dma = 0;

	/* REVISIT:  someday, support IRQ-driven card detection.  */
	mmc->caps |= MMC_CAP_NEEDS_POLL;
	mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY;

	if (pdata && (pdata->wires == 4 || pdata->wires == 0))
		mmc->caps |= MMC_CAP_4_BIT_DATA;

	if (pdata && (pdata->wires == 8))
		mmc->caps |= (MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA);

	host->version = pdata->version;

	mmc->ops = &mmc_davinci_ops;
	mmc->f_min = 312500;
	mmc->f_max = 25000000;
	if (pdata && pdata->max_freq)
		mmc->f_max = pdata->max_freq;
	if (pdata && pdata->caps)
		mmc->caps |= pdata->caps;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	/* With no iommu coalescing pages, each phys_seg is a hw_seg.
	 * Each hw_seg uses one EDMA parameter RAM slot, always one
	 * channel and then usually some linked slots.
	 */
	mmc->max_segs		= 1 + host->n_link;

	/* EDMA limit per hw segment (one or two MBytes) */
	mmc->max_seg_size	= MAX_CCNT * rw_threshold;

	/* MMC/SD controller limits for multiblock requests */
	mmc->max_blk_size	= 4095;  /* BLEN is 12 bits */
	mmc->max_blk_count	= 65535; /* NBLK is 16 bits */
	mmc->max_req_size	= mmc->max_blk_size * mmc->max_blk_count;

	dev_dbg(mmc_dev(host->mmc), "max_segs=%d\n", mmc->max_segs);
	dev_dbg(mmc_dev(host->mmc), "max_blk_size=%d\n", mmc->max_blk_size);
	dev_dbg(mmc_dev(host->mmc), "max_req_size=%d\n", mmc->max_req_size);
	dev_dbg(mmc_dev(host->mmc), "max_seg_size=%d\n", mmc->max_seg_size);

	platform_set_drvdata(pdev, host);

	ret = mmc_davinci_cpufreq_register(host);
	if (ret) {
		dev_err(&pdev->dev, "failed to register cpufreq\n");
		goto cpu_freq_fail;
	}

	ret = mmc_add_host(mmc);
	if (ret < 0)
		goto out;

	ret = request_irq(irq, mmc_davinci_irq, 0, mmc_hostname(mmc), host);
	if (ret)
		goto out;

	if (host->sdio_irq >= 0) {
		ret = request_irq(host->sdio_irq, mmc_davinci_sdio_irq, 0,
				  mmc_hostname(mmc), host);
		if (!ret)
			mmc->caps |= MMC_CAP_SDIO_IRQ;
	}

	rename_region(mem, mmc_hostname(mmc));

	dev_info(mmc_dev(host->mmc), "Using %s, %d-bit mode\n",
		host->use_dma ? "DMA" : "PIO",
		(mmc->caps & MMC_CAP_4_BIT_DATA) ? 4 : 1);

	return 0;

out:
	mmc_davinci_cpufreq_deregister(host);
cpu_freq_fail:
	if (host) {
		davinci_release_dma_channels(host);

		if (host->clk) {
			clk_disable(host->clk);
			clk_put(host->clk);
		}

		if (host->base)
			iounmap(host->base);
	}

	if (mmc)
		mmc_free_host(mmc);

	if (mem)
		release_resource(mem);

	dev_dbg(&pdev->dev, "probe err %d\n", ret);

	return ret;
}

static int __exit davinci_mmcsd_remove(struct platform_device *pdev)
{
	struct mmc_davinci_host *host = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	if (host) {
		mmc_davinci_cpufreq_deregister(host);

		mmc_remove_host(host->mmc);
		free_irq(host->mmc_irq, host);
		if (host->mmc->caps & MMC_CAP_SDIO_IRQ)
			free_irq(host->sdio_irq, host);

		davinci_release_dma_channels(host);

		clk_disable(host->clk);
		clk_put(host->clk);

		iounmap(host->base);

		release_resource(host->mem_res);

		mmc_free_host(host->mmc);
	}

	return 0;
}

#ifdef CONFIG_PM
static int davinci_mmcsd_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mmc_davinci_host *host = platform_get_drvdata(pdev);
	int ret;

	ret = mmc_suspend_host(host->mmc);
	if (!ret) {
		writel(0, host->base + DAVINCI_MMCIM);
		mmc_davinci_reset_ctrl(host, 1);
		clk_disable(host->clk);
		host->suspended = 1;
	} else {
		host->suspended = 0;
	}

	return ret;
}

static int davinci_mmcsd_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mmc_davinci_host *host = platform_get_drvdata(pdev);
	int ret;

	if (!host->suspended)
		return 0;

	clk_enable(host->clk);

	mmc_davinci_reset_ctrl(host, 0);
	ret = mmc_resume_host(host->mmc);
	if (!ret)
		host->suspended = 0;

	return ret;
}

static const struct dev_pm_ops davinci_mmcsd_pm = {
	.suspend        = davinci_mmcsd_suspend,
	.resume         = davinci_mmcsd_resume,
};

#define davinci_mmcsd_pm_ops (&davinci_mmcsd_pm)
#else
#define davinci_mmcsd_pm_ops NULL
#endif

static struct platform_driver davinci_mmcsd_driver = {
	.driver		= {
		.name	= "davinci_mmc",
		.owner	= THIS_MODULE,
		.pm	= davinci_mmcsd_pm_ops,
	},
	.remove		= __exit_p(davinci_mmcsd_remove),
};

static int __init davinci_mmcsd_init(void)
{
	return platform_driver_probe(&davinci_mmcsd_driver,
				     davinci_mmcsd_probe);
}
module_init(davinci_mmcsd_init);

static void __exit davinci_mmcsd_exit(void)
{
	platform_driver_unregister(&davinci_mmcsd_driver);
}
module_exit(davinci_mmcsd_exit);

MODULE_AUTHOR("Texas Instruments India");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MMC/SD driver for Davinci MMC controller");
MODULE_ALIAS("platform:davinci_mmc");

