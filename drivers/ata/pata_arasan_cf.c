/*
 * drivers/ata/pata_arasan_cf.c
 *
 * Arasan Compact Flash host controller source file
 *
 * Copyright (C) 2011 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/*
 * The Arasan CompactFlash Device Controller IP core has three basic modes of
 * operation: PC card ATA using I/O mode, PC card ATA using memory mode, PC card
 * ATA using true IDE modes. This driver supports only True IDE mode currently.
 *
 * Arasan CF Controller shares global irq register with Arasan XD Controller.
 *
 * Tested on arch/arm/mach-spear13xx
 */

#include <linux/ata.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/libata.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pata_arasan_cf_data.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define DRIVER_NAME	"arasan_cf"
#define TIMEOUT		msecs_to_jiffies(3000)

/* Registers */
/* CompactFlash Interface Status */
#define CFI_STS			0x000
	#define STS_CHG				(1)
	#define BIN_AUDIO_OUT			(1 << 1)
	#define CARD_DETECT1			(1 << 2)
	#define CARD_DETECT2			(1 << 3)
	#define INP_ACK				(1 << 4)
	#define CARD_READY			(1 << 5)
	#define IO_READY			(1 << 6)
	#define B16_IO_PORT_SEL			(1 << 7)
/* IRQ */
#define IRQ_STS			0x004
/* Interrupt Enable */
#define IRQ_EN			0x008
	#define CARD_DETECT_IRQ			(1)
	#define STATUS_CHNG_IRQ			(1 << 1)
	#define MEM_MODE_IRQ			(1 << 2)
	#define IO_MODE_IRQ			(1 << 3)
	#define TRUE_IDE_MODE_IRQ		(1 << 8)
	#define PIO_XFER_ERR_IRQ		(1 << 9)
	#define BUF_AVAIL_IRQ			(1 << 10)
	#define XFER_DONE_IRQ			(1 << 11)
	#define IGNORED_IRQS	(STATUS_CHNG_IRQ | MEM_MODE_IRQ | IO_MODE_IRQ |\
					TRUE_IDE_MODE_IRQ)
	#define TRUE_IDE_IRQS	(CARD_DETECT_IRQ | PIO_XFER_ERR_IRQ |\
					BUF_AVAIL_IRQ | XFER_DONE_IRQ)
/* Operation Mode */
#define OP_MODE			0x00C
	#define CARD_MODE_MASK			(0x3)
	#define MEM_MODE			(0x0)
	#define IO_MODE				(0x1)
	#define TRUE_IDE_MODE			(0x2)

	#define CARD_TYPE_MASK			(1 << 2)
	#define CF_CARD				(0)
	#define CF_PLUS_CARD			(1 << 2)

	#define CARD_RESET			(1 << 3)
	#define CFHOST_ENB			(1 << 4)
	#define OUTPUTS_TRISTATE		(1 << 5)
	#define ULTRA_DMA_ENB			(1 << 8)
	#define MULTI_WORD_DMA_ENB		(1 << 9)
	#define DRQ_BLOCK_SIZE_MASK		(0x3 << 11)
	#define DRQ_BLOCK_SIZE_512		(0)
	#define DRQ_BLOCK_SIZE_1024		(1 << 11)
	#define DRQ_BLOCK_SIZE_2048		(2 << 11)
	#define DRQ_BLOCK_SIZE_4096		(3 << 11)
/* CF Interface Clock Configuration */
#define CLK_CFG			0x010
	#define CF_IF_CLK_MASK			(0XF)
/* CF Timing Mode Configuration */
#define TM_CFG			0x014
	#define MEM_MODE_TIMING_MASK		(0x3)
	#define MEM_MODE_TIMING_250NS		(0x0)
	#define MEM_MODE_TIMING_120NS		(0x1)
	#define MEM_MODE_TIMING_100NS		(0x2)
	#define MEM_MODE_TIMING_80NS		(0x3)

	#define IO_MODE_TIMING_MASK		(0x3 << 2)
	#define IO_MODE_TIMING_250NS		(0x0 << 2)
	#define IO_MODE_TIMING_120NS		(0x1 << 2)
	#define IO_MODE_TIMING_100NS		(0x2 << 2)
	#define IO_MODE_TIMING_80NS		(0x3 << 2)

	#define TRUEIDE_PIO_TIMING_MASK		(0x7 << 4)
	#define TRUEIDE_PIO_TIMING_SHIFT	4

	#define TRUEIDE_MWORD_DMA_TIMING_MASK	(0x7 << 7)
	#define TRUEIDE_MWORD_DMA_TIMING_SHIFT	7

	#define ULTRA_DMA_TIMING_MASK		(0x7 << 10)
	#define ULTRA_DMA_TIMING_SHIFT		10
/* CF Transfer Address */
#define XFER_ADDR		0x014
	#define XFER_ADDR_MASK			(0x7FF)
	#define MAX_XFER_COUNT			0x20000u
/* Transfer Control */
#define XFER_CTR		0x01C
	#define XFER_COUNT_MASK			(0x3FFFF)
	#define ADDR_INC_DISABLE		(1 << 24)
	#define XFER_WIDTH_MASK			(1 << 25)
	#define XFER_WIDTH_8B			(0)
	#define XFER_WIDTH_16B			(1 << 25)

	#define MEM_TYPE_MASK			(1 << 26)
	#define MEM_TYPE_COMMON			(0)
	#define MEM_TYPE_ATTRIBUTE		(1 << 26)

	#define MEM_IO_XFER_MASK		(1 << 27)
	#define MEM_XFER			(0)
	#define IO_XFER				(1 << 27)

	#define DMA_XFER_MODE			(1 << 28)

	#define AHB_BUS_NORMAL_PIO_OPRTN	(~(1 << 29))
	#define XFER_DIR_MASK			(1 << 30)
	#define XFER_READ			(0)
	#define XFER_WRITE			(1 << 30)

	#define XFER_START			(1 << 31)
/* Write Data Port */
#define WRITE_PORT		0x024
/* Read Data Port */
#define READ_PORT		0x028
/* ATA Data Port */
#define ATA_DATA_PORT		0x030
	#define ATA_DATA_PORT_MASK		(0xFFFF)
/* ATA Error/Features */
#define ATA_ERR_FTR		0x034
/* ATA Sector Count */
#define ATA_SC			0x038
/* ATA Sector Number */
#define ATA_SN			0x03C
/* ATA Cylinder Low */
#define ATA_CL			0x040
/* ATA Cylinder High */
#define ATA_CH			0x044
/* ATA Select Card/Head */
#define ATA_SH			0x048
/* ATA Status-Command */
#define ATA_STS_CMD		0x04C
/* ATA Alternate Status/Device Control */
#define ATA_ASTS_DCTR		0x050
/* Extended Write Data Port 0x200-0x3FC */
#define EXT_WRITE_PORT		0x200
/* Extended Read Data Port 0x400-0x5FC */
#define EXT_READ_PORT		0x400
	#define FIFO_SIZE	0x200u
/* Global Interrupt Status */
#define GIRQ_STS		0x800
/* Global Interrupt Status enable */
#define GIRQ_STS_EN		0x804
/* Global Interrupt Signal enable */
#define GIRQ_SGN_EN		0x808
	#define GIRQ_CF		(1)
	#define GIRQ_XD		(1 << 1)

/* Compact Flash Controller Dev Structure */
struct arasan_cf_dev {
	/* pointer to ata_host structure */
	struct ata_host *host;
	/* clk structure */
	struct clk *clk;

	/* physical base address of controller */
	dma_addr_t pbase;
	/* virtual base address of controller */
	void __iomem *vbase;
	/* irq number*/
	int irq;

	/* status to be updated to framework regarding DMA transfer */
	u8 dma_status;
	/* Card is present or Not */
	u8 card_present;

	/* dma specific */
	/* Completion for transfer complete interrupt from controller */
	struct completion cf_completion;
	/* Completion for DMA transfer complete. */
	struct completion dma_completion;
	/* Dma channel allocated */
	struct dma_chan *dma_chan;
	/* Mask for DMA transfers */
	dma_cap_mask_t mask;
	/* DMA transfer work */
	struct work_struct work;
	/* DMA delayed finish work */
	struct delayed_work dwork;
	/* qc to be transferred using DMA */
	struct ata_queued_cmd *qc;
};

static struct scsi_host_template arasan_cf_sht = {
	ATA_BASE_SHT(DRIVER_NAME),
	.sg_tablesize = SG_NONE,
	.dma_boundary = 0xFFFFFFFFUL,
};

static void cf_dumpregs(struct arasan_cf_dev *acdev)
{
	struct device *dev = acdev->host->dev;

	dev_dbg(dev, ": =========== REGISTER DUMP ===========");
	dev_dbg(dev, ": CFI_STS: %x", readl(acdev->vbase + CFI_STS));
	dev_dbg(dev, ": IRQ_STS: %x", readl(acdev->vbase + IRQ_STS));
	dev_dbg(dev, ": IRQ_EN: %x", readl(acdev->vbase + IRQ_EN));
	dev_dbg(dev, ": OP_MODE: %x", readl(acdev->vbase + OP_MODE));
	dev_dbg(dev, ": CLK_CFG: %x", readl(acdev->vbase + CLK_CFG));
	dev_dbg(dev, ": TM_CFG: %x", readl(acdev->vbase + TM_CFG));
	dev_dbg(dev, ": XFER_CTR: %x", readl(acdev->vbase + XFER_CTR));
	dev_dbg(dev, ": GIRQ_STS: %x", readl(acdev->vbase + GIRQ_STS));
	dev_dbg(dev, ": GIRQ_STS_EN: %x", readl(acdev->vbase + GIRQ_STS_EN));
	dev_dbg(dev, ": GIRQ_SGN_EN: %x", readl(acdev->vbase + GIRQ_SGN_EN));
	dev_dbg(dev, ": =====================================");
}

/* Enable/Disable global interrupts shared between CF and XD ctrlr. */
static void cf_ginterrupt_enable(struct arasan_cf_dev *acdev, bool enable)
{
	/* enable should be 0 or 1 */
	writel(enable, acdev->vbase + GIRQ_STS_EN);
	writel(enable, acdev->vbase + GIRQ_SGN_EN);
}

/* Enable/Disable CF interrupts */
static inline void
cf_interrupt_enable(struct arasan_cf_dev *acdev, u32 mask, bool enable)
{
	u32 val = readl(acdev->vbase + IRQ_EN);
	/* clear & enable/disable irqs */
	if (enable) {
		writel(mask, acdev->vbase + IRQ_STS);
		writel(val | mask, acdev->vbase + IRQ_EN);
	} else
		writel(val & ~mask, acdev->vbase + IRQ_EN);
}

static inline void cf_card_reset(struct arasan_cf_dev *acdev)
{
	u32 val = readl(acdev->vbase + OP_MODE);

	writel(val | CARD_RESET, acdev->vbase + OP_MODE);
	udelay(200);
	writel(val & ~CARD_RESET, acdev->vbase + OP_MODE);
}

static inline void cf_ctrl_reset(struct arasan_cf_dev *acdev)
{
	writel(readl(acdev->vbase + OP_MODE) & ~CFHOST_ENB,
			acdev->vbase + OP_MODE);
	writel(readl(acdev->vbase + OP_MODE) | CFHOST_ENB,
			acdev->vbase + OP_MODE);
}

static void cf_card_detect(struct arasan_cf_dev *acdev, bool hotplugged)
{
	struct ata_port *ap = acdev->host->ports[0];
	struct ata_eh_info *ehi = &ap->link.eh_info;
	u32 val = readl(acdev->vbase + CFI_STS);

	/* Both CD1 & CD2 should be low if card inserted completely */
	if (!(val & (CARD_DETECT1 | CARD_DETECT2))) {
		if (acdev->card_present)
			return;
		acdev->card_present = 1;
		cf_card_reset(acdev);
	} else {
		if (!acdev->card_present)
			return;
		acdev->card_present = 0;
	}

	if (hotplugged) {
		ata_ehi_hotplugged(ehi);
		ata_port_freeze(ap);
	}
}

static int cf_init(struct arasan_cf_dev *acdev)
{
	struct arasan_cf_pdata *pdata = dev_get_platdata(acdev->host->dev);
	unsigned int if_clk;
	unsigned long flags;
	int ret = 0;

	ret = clk_prepare_enable(acdev->clk);
	if (ret) {
		dev_dbg(acdev->host->dev, "clock enable failed");
		return ret;
	}

	ret = clk_set_rate(acdev->clk, 166000000);
	if (ret) {
		dev_warn(acdev->host->dev, "clock set rate failed");
		clk_disable_unprepare(acdev->clk);
		return ret;
	}

	spin_lock_irqsave(&acdev->host->lock, flags);
	/* configure CF interface clock */
	/* TODO: read from device tree */
	if_clk = CF_IF_CLK_166M;
	if (pdata && pdata->cf_if_clk <= CF_IF_CLK_200M)
		if_clk = pdata->cf_if_clk;

	writel(if_clk, acdev->vbase + CLK_CFG);

	writel(TRUE_IDE_MODE | CFHOST_ENB, acdev->vbase + OP_MODE);
	cf_interrupt_enable(acdev, CARD_DETECT_IRQ, 1);
	cf_ginterrupt_enable(acdev, 1);
	spin_unlock_irqrestore(&acdev->host->lock, flags);

	return ret;
}

static void cf_exit(struct arasan_cf_dev *acdev)
{
	unsigned long flags;

	spin_lock_irqsave(&acdev->host->lock, flags);
	cf_ginterrupt_enable(acdev, 0);
	cf_interrupt_enable(acdev, TRUE_IDE_IRQS, 0);
	cf_card_reset(acdev);
	writel(readl(acdev->vbase + OP_MODE) & ~CFHOST_ENB,
			acdev->vbase + OP_MODE);
	spin_unlock_irqrestore(&acdev->host->lock, flags);
	clk_disable_unprepare(acdev->clk);
}

static void dma_callback(void *dev)
{
	struct arasan_cf_dev *acdev = (struct arasan_cf_dev *) dev;

	complete(&acdev->dma_completion);
}

static inline void dma_complete(struct arasan_cf_dev *acdev)
{
	struct ata_queued_cmd *qc = acdev->qc;
	unsigned long flags;

	acdev->qc = NULL;
	ata_sff_interrupt(acdev->irq, acdev->host);

	spin_lock_irqsave(&acdev->host->lock, flags);
	if (unlikely(qc->err_mask) && ata_is_dma(qc->tf.protocol))
		ata_ehi_push_desc(&qc->ap->link.eh_info, "DMA Failed: Timeout");
	spin_unlock_irqrestore(&acdev->host->lock, flags);
}

static inline int wait4buf(struct arasan_cf_dev *acdev)
{
	if (!wait_for_completion_timeout(&acdev->cf_completion, TIMEOUT)) {
		u32 rw = acdev->qc->tf.flags & ATA_TFLAG_WRITE;

		dev_err(acdev->host->dev, "%s TimeOut", rw ? "write" : "read");
		return -ETIMEDOUT;
	}

	/* Check if PIO Error interrupt has occurred */
	if (acdev->dma_status & ATA_DMA_ERR)
		return -EAGAIN;

	return 0;
}

static int
dma_xfer(struct arasan_cf_dev *acdev, dma_addr_t src, dma_addr_t dest, u32 len)
{
	struct dma_async_tx_descriptor *tx;
	struct dma_chan *chan = acdev->dma_chan;
	dma_cookie_t cookie;
	unsigned long flags = DMA_PREP_INTERRUPT;
	int ret = 0;

	tx = chan->device->device_prep_dma_memcpy(chan, dest, src, len, flags);
	if (!tx) {
		dev_err(acdev->host->dev, "device_prep_dma_memcpy failed\n");
		return -EAGAIN;
	}

	tx->callback = dma_callback;
	tx->callback_param = acdev;
	cookie = tx->tx_submit(tx);

	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(acdev->host->dev, "dma_submit_error\n");
		return ret;
	}

	chan->device->device_issue_pending(chan);

	/* Wait for DMA to complete */
	if (!wait_for_completion_timeout(&acdev->dma_completion, TIMEOUT)) {
		chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
		dev_err(acdev->host->dev, "wait_for_completion_timeout\n");
		return -ETIMEDOUT;
	}

	return ret;
}

static int sg_xfer(struct arasan_cf_dev *acdev, struct scatterlist *sg)
{
	dma_addr_t dest = 0, src = 0;
	u32 xfer_cnt, sglen, dma_len, xfer_ctr;
	u32 write = acdev->qc->tf.flags & ATA_TFLAG_WRITE;
	unsigned long flags;
	int ret = 0;

	sglen = sg_dma_len(sg);
	if (write) {
		src = sg_dma_address(sg);
		dest = acdev->pbase + EXT_WRITE_PORT;
	} else {
		dest = sg_dma_address(sg);
		src = acdev->pbase + EXT_READ_PORT;
	}

	/*
	 * For each sg:
	 * MAX_XFER_COUNT data will be transferred before we get transfer
	 * complete interrupt. Between after FIFO_SIZE data
	 * buffer available interrupt will be generated. At this time we will
	 * fill FIFO again: max FIFO_SIZE data.
	 */
	while (sglen) {
		xfer_cnt = min(sglen, MAX_XFER_COUNT);
		spin_lock_irqsave(&acdev->host->lock, flags);
		xfer_ctr = readl(acdev->vbase + XFER_CTR) &
			~XFER_COUNT_MASK;
		writel(xfer_ctr | xfer_cnt | XFER_START,
				acdev->vbase + XFER_CTR);
		spin_unlock_irqrestore(&acdev->host->lock, flags);

		/* continue dma xfers until current sg is completed */
		while (xfer_cnt) {
			/* wait for read to complete */
			if (!write) {
				ret = wait4buf(acdev);
				if (ret)
					goto fail;
			}

			/* read/write FIFO in chunk of FIFO_SIZE */
			dma_len = min(xfer_cnt, FIFO_SIZE);
			ret = dma_xfer(acdev, src, dest, dma_len);
			if (ret) {
				dev_err(acdev->host->dev, "dma failed");
				goto fail;
			}

			if (write)
				src += dma_len;
			else
				dest += dma_len;

			sglen -= dma_len;
			xfer_cnt -= dma_len;

			/* wait for write to complete */
			if (write) {
				ret = wait4buf(acdev);
				if (ret)
					goto fail;
			}
		}
	}

fail:
	spin_lock_irqsave(&acdev->host->lock, flags);
	writel(readl(acdev->vbase + XFER_CTR) & ~XFER_START,
			acdev->vbase + XFER_CTR);
	spin_unlock_irqrestore(&acdev->host->lock, flags);

	return ret;
}

/*
 * This routine uses External DMA controller to read/write data to FIFO of CF
 * controller. There are two xfer related interrupt supported by CF controller:
 * - buf_avail: This interrupt is generated as soon as we have buffer of 512
 *	bytes available for reading or empty buffer available for writing.
 * - xfer_done: This interrupt is generated on transfer of "xfer_size" amount of
 *	data to/from FIFO. xfer_size is programmed in XFER_CTR register.
 *
 * Max buffer size = FIFO_SIZE = 512 Bytes.
 * Max xfer_size = MAX_XFER_COUNT = 256 KB.
 */
static void data_xfer(struct work_struct *work)
{
	struct arasan_cf_dev *acdev = container_of(work, struct arasan_cf_dev,
			work);
	struct ata_queued_cmd *qc = acdev->qc;
	struct scatterlist *sg;
	unsigned long flags;
	u32 temp;
	int ret = 0;

	/* request dma channels */
	/* dma_request_channel may sleep, so calling from process context */
	acdev->dma_chan = dma_request_slave_channel(acdev->host->dev, "data");
	if (!acdev->dma_chan) {
		dev_err(acdev->host->dev, "Unable to get dma_chan\n");
		goto chan_request_fail;
	}

	for_each_sg(qc->sg, sg, qc->n_elem, temp) {
		ret = sg_xfer(acdev, sg);
		if (ret)
			break;
	}

	dma_release_channel(acdev->dma_chan);

	/* data xferred successfully */
	if (!ret) {
		u32 status;

		spin_lock_irqsave(&acdev->host->lock, flags);
		status = ioread8(qc->ap->ioaddr.altstatus_addr);
		spin_unlock_irqrestore(&acdev->host->lock, flags);
		if (status & (ATA_BUSY | ATA_DRQ)) {
			ata_sff_queue_delayed_work(&acdev->dwork, 1);
			return;
		}

		goto sff_intr;
	}

	cf_dumpregs(acdev);

chan_request_fail:
	spin_lock_irqsave(&acdev->host->lock, flags);
	/* error when transferring data to/from memory */
	qc->err_mask |= AC_ERR_HOST_BUS;
	qc->ap->hsm_task_state = HSM_ST_ERR;

	cf_ctrl_reset(acdev);
	spin_unlock_irqrestore(qc->ap->lock, flags);
sff_intr:
	dma_complete(acdev);
}

static void delayed_finish(struct work_struct *work)
{
	struct arasan_cf_dev *acdev = container_of(work, struct arasan_cf_dev,
			dwork.work);
	struct ata_queued_cmd *qc = acdev->qc;
	unsigned long flags;
	u8 status;

	spin_lock_irqsave(&acdev->host->lock, flags);
	status = ioread8(qc->ap->ioaddr.altstatus_addr);
	spin_unlock_irqrestore(&acdev->host->lock, flags);

	if (status & (ATA_BUSY | ATA_DRQ))
		ata_sff_queue_delayed_work(&acdev->dwork, 1);
	else
		dma_complete(acdev);
}

static irqreturn_t arasan_cf_interrupt(int irq, void *dev)
{
	struct arasan_cf_dev *acdev = ((struct ata_host *)dev)->private_data;
	unsigned long flags;
	u32 irqsts;

	irqsts = readl(acdev->vbase + GIRQ_STS);
	if (!(irqsts & GIRQ_CF))
		return IRQ_NONE;

	spin_lock_irqsave(&acdev->host->lock, flags);
	irqsts = readl(acdev->vbase + IRQ_STS);
	writel(irqsts, acdev->vbase + IRQ_STS);		/* clear irqs */
	writel(GIRQ_CF, acdev->vbase + GIRQ_STS);	/* clear girqs */

	/* handle only relevant interrupts */
	irqsts &= ~IGNORED_IRQS;

	if (irqsts & CARD_DETECT_IRQ) {
		cf_card_detect(acdev, 1);
		spin_unlock_irqrestore(&acdev->host->lock, flags);
		return IRQ_HANDLED;
	}

	if (irqsts & PIO_XFER_ERR_IRQ) {
		acdev->dma_status = ATA_DMA_ERR;
		writel(readl(acdev->vbase + XFER_CTR) & ~XFER_START,
				acdev->vbase + XFER_CTR);
		spin_unlock_irqrestore(&acdev->host->lock, flags);
		complete(&acdev->cf_completion);
		dev_err(acdev->host->dev, "pio xfer err irq\n");
		return IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&acdev->host->lock, flags);

	if (irqsts & BUF_AVAIL_IRQ) {
		complete(&acdev->cf_completion);
		return IRQ_HANDLED;
	}

	if (irqsts & XFER_DONE_IRQ) {
		struct ata_queued_cmd *qc = acdev->qc;

		/* Send Complete only for write */
		if (qc->tf.flags & ATA_TFLAG_WRITE)
			complete(&acdev->cf_completion);
	}

	return IRQ_HANDLED;
}

static void arasan_cf_freeze(struct ata_port *ap)
{
	struct arasan_cf_dev *acdev = ap->host->private_data;

	/* stop transfer and reset controller */
	writel(readl(acdev->vbase + XFER_CTR) & ~XFER_START,
			acdev->vbase + XFER_CTR);
	cf_ctrl_reset(acdev);
	acdev->dma_status = ATA_DMA_ERR;

	ata_sff_dma_pause(ap);
	ata_sff_freeze(ap);
}

static void arasan_cf_error_handler(struct ata_port *ap)
{
	struct arasan_cf_dev *acdev = ap->host->private_data;

	/*
	 * DMA transfers using an external DMA controller may be scheduled.
	 * Abort them before handling error. Refer data_xfer() for further
	 * details.
	 */
	cancel_work_sync(&acdev->work);
	cancel_delayed_work_sync(&acdev->dwork);
	return ata_sff_error_handler(ap);
}

static void arasan_cf_dma_start(struct arasan_cf_dev *acdev)
{
	struct ata_queued_cmd *qc = acdev->qc;
	struct ata_port *ap = qc->ap;
	struct ata_taskfile *tf = &qc->tf;
	u32 xfer_ctr = readl(acdev->vbase + XFER_CTR) & ~XFER_DIR_MASK;
	u32 write = tf->flags & ATA_TFLAG_WRITE;

	xfer_ctr |= write ? XFER_WRITE : XFER_READ;
	writel(xfer_ctr, acdev->vbase + XFER_CTR);

	ap->ops->sff_exec_command(ap, tf);
	ata_sff_queue_work(&acdev->work);
}

static unsigned int arasan_cf_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct arasan_cf_dev *acdev = ap->host->private_data;

	/* defer PIO handling to sff_qc_issue */
	if (!ata_is_dma(qc->tf.protocol))
		return ata_sff_qc_issue(qc);

	/* select the device */
	ata_wait_idle(ap);
	ata_sff_dev_select(ap, qc->dev->devno);
	ata_wait_idle(ap);

	/* start the command */
	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
		WARN_ON_ONCE(qc->tf.flags & ATA_TFLAG_POLLING);

		ap->ops->sff_tf_load(ap, &qc->tf);
		acdev->dma_status = 0;
		acdev->qc = qc;
		arasan_cf_dma_start(acdev);
		ap->hsm_task_state = HSM_ST_LAST;
		break;

	default:
		WARN_ON(1);
		return AC_ERR_SYSTEM;
	}

	return 0;
}

static void arasan_cf_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct arasan_cf_dev *acdev = ap->host->private_data;
	u8 pio = adev->pio_mode - XFER_PIO_0;
	unsigned long flags;
	u32 val;

	/* Arasan ctrl supports Mode0 -> Mode6 */
	if (pio > 6) {
		dev_err(ap->dev, "Unknown PIO mode\n");
		return;
	}

	spin_lock_irqsave(&acdev->host->lock, flags);
	val = readl(acdev->vbase + OP_MODE) &
		~(ULTRA_DMA_ENB | MULTI_WORD_DMA_ENB | DRQ_BLOCK_SIZE_MASK);
	writel(val, acdev->vbase + OP_MODE);
	val = readl(acdev->vbase + TM_CFG) & ~TRUEIDE_PIO_TIMING_MASK;
	val |= pio << TRUEIDE_PIO_TIMING_SHIFT;
	writel(val, acdev->vbase + TM_CFG);

	cf_interrupt_enable(acdev, BUF_AVAIL_IRQ | XFER_DONE_IRQ, 0);
	cf_interrupt_enable(acdev, PIO_XFER_ERR_IRQ, 1);
	spin_unlock_irqrestore(&acdev->host->lock, flags);
}

static void arasan_cf_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct arasan_cf_dev *acdev = ap->host->private_data;
	u32 opmode, tmcfg, dma_mode = adev->dma_mode;
	unsigned long flags;

	spin_lock_irqsave(&acdev->host->lock, flags);
	opmode = readl(acdev->vbase + OP_MODE) &
		~(MULTI_WORD_DMA_ENB | ULTRA_DMA_ENB);
	tmcfg = readl(acdev->vbase + TM_CFG);

	if ((dma_mode >= XFER_UDMA_0) && (dma_mode <= XFER_UDMA_6)) {
		opmode |= ULTRA_DMA_ENB;
		tmcfg &= ~ULTRA_DMA_TIMING_MASK;
		tmcfg |= (dma_mode - XFER_UDMA_0) << ULTRA_DMA_TIMING_SHIFT;
	} else if ((dma_mode >= XFER_MW_DMA_0) && (dma_mode <= XFER_MW_DMA_4)) {
		opmode |= MULTI_WORD_DMA_ENB;
		tmcfg &= ~TRUEIDE_MWORD_DMA_TIMING_MASK;
		tmcfg |= (dma_mode - XFER_MW_DMA_0) <<
			TRUEIDE_MWORD_DMA_TIMING_SHIFT;
	} else {
		dev_err(ap->dev, "Unknown DMA mode\n");
		spin_unlock_irqrestore(&acdev->host->lock, flags);
		return;
	}

	writel(opmode, acdev->vbase + OP_MODE);
	writel(tmcfg, acdev->vbase + TM_CFG);
	writel(DMA_XFER_MODE, acdev->vbase + XFER_CTR);

	cf_interrupt_enable(acdev, PIO_XFER_ERR_IRQ, 0);
	cf_interrupt_enable(acdev, BUF_AVAIL_IRQ | XFER_DONE_IRQ, 1);
	spin_unlock_irqrestore(&acdev->host->lock, flags);
}

static struct ata_port_operations arasan_cf_ops = {
	.inherits = &ata_sff_port_ops,
	.freeze = arasan_cf_freeze,
	.error_handler = arasan_cf_error_handler,
	.qc_issue = arasan_cf_qc_issue,
	.set_piomode = arasan_cf_set_piomode,
	.set_dmamode = arasan_cf_set_dmamode,
};

static int arasan_cf_probe(struct platform_device *pdev)
{
	struct arasan_cf_dev *acdev;
	struct arasan_cf_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct ata_host *host;
	struct ata_port *ap;
	struct resource *res;
	u32 quirk;
	irq_handler_t irq_handler = NULL;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	if (!devm_request_mem_region(&pdev->dev, res->start, resource_size(res),
				DRIVER_NAME)) {
		dev_warn(&pdev->dev, "Failed to get memory region resource\n");
		return -ENOENT;
	}

	acdev = devm_kzalloc(&pdev->dev, sizeof(*acdev), GFP_KERNEL);
	if (!acdev) {
		dev_warn(&pdev->dev, "kzalloc fail\n");
		return -ENOMEM;
	}

	if (pdata)
		quirk = pdata->quirk;
	else
		quirk = CF_BROKEN_UDMA; /* as it is on spear1340 */

	/* if irq is 0, support only PIO */
	acdev->irq = platform_get_irq(pdev, 0);
	if (acdev->irq)
		irq_handler = arasan_cf_interrupt;
	else
		quirk |= CF_BROKEN_MWDMA | CF_BROKEN_UDMA;

	acdev->pbase = res->start;
	acdev->vbase = devm_ioremap_nocache(&pdev->dev, res->start,
			resource_size(res));
	if (!acdev->vbase) {
		dev_warn(&pdev->dev, "ioremap fail\n");
		return -ENOMEM;
	}

	acdev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(acdev->clk)) {
		dev_warn(&pdev->dev, "Clock not found\n");
		return PTR_ERR(acdev->clk);
	}

	/* allocate host */
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host) {
		ret = -ENOMEM;
		dev_warn(&pdev->dev, "alloc host fail\n");
		goto free_clk;
	}

	ap = host->ports[0];
	host->private_data = acdev;
	acdev->host = host;
	ap->ops = &arasan_cf_ops;
	ap->pio_mask = ATA_PIO6;
	ap->mwdma_mask = ATA_MWDMA4;
	ap->udma_mask = ATA_UDMA6;

	init_completion(&acdev->cf_completion);
	init_completion(&acdev->dma_completion);
	INIT_WORK(&acdev->work, data_xfer);
	INIT_DELAYED_WORK(&acdev->dwork, delayed_finish);
	dma_cap_set(DMA_MEMCPY, acdev->mask);

	/* Handle platform specific quirks */
	if (quirk) {
		if (quirk & CF_BROKEN_PIO) {
			ap->ops->set_piomode = NULL;
			ap->pio_mask = 0;
		}
		if (quirk & CF_BROKEN_MWDMA)
			ap->mwdma_mask = 0;
		if (quirk & CF_BROKEN_UDMA)
			ap->udma_mask = 0;
	}
	ap->flags |= ATA_FLAG_PIO_POLLING | ATA_FLAG_NO_ATAPI;

	ap->ioaddr.cmd_addr = acdev->vbase + ATA_DATA_PORT;
	ap->ioaddr.data_addr = acdev->vbase + ATA_DATA_PORT;
	ap->ioaddr.error_addr = acdev->vbase + ATA_ERR_FTR;
	ap->ioaddr.feature_addr = acdev->vbase + ATA_ERR_FTR;
	ap->ioaddr.nsect_addr = acdev->vbase + ATA_SC;
	ap->ioaddr.lbal_addr = acdev->vbase + ATA_SN;
	ap->ioaddr.lbam_addr = acdev->vbase + ATA_CL;
	ap->ioaddr.lbah_addr = acdev->vbase + ATA_CH;
	ap->ioaddr.device_addr = acdev->vbase + ATA_SH;
	ap->ioaddr.status_addr = acdev->vbase + ATA_STS_CMD;
	ap->ioaddr.command_addr = acdev->vbase + ATA_STS_CMD;
	ap->ioaddr.altstatus_addr = acdev->vbase + ATA_ASTS_DCTR;
	ap->ioaddr.ctl_addr = acdev->vbase + ATA_ASTS_DCTR;

	ata_port_desc(ap, "phy_addr %llx virt_addr %p",
		      (unsigned long long) res->start, acdev->vbase);

	ret = cf_init(acdev);
	if (ret)
		goto free_clk;

	cf_card_detect(acdev, 0);

	return ata_host_activate(host, acdev->irq, irq_handler, 0,
			&arasan_cf_sht);

free_clk:
	clk_put(acdev->clk);
	return ret;
}

static int arasan_cf_remove(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct arasan_cf_dev *acdev = host->ports[0]->private_data;

	ata_host_detach(host);
	cf_exit(acdev);
	clk_put(acdev->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int arasan_cf_suspend(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct arasan_cf_dev *acdev = host->ports[0]->private_data;

	if (acdev->dma_chan)
		acdev->dma_chan->device->device_control(acdev->dma_chan,
				DMA_TERMINATE_ALL, 0);

	cf_exit(acdev);
	return ata_host_suspend(host, PMSG_SUSPEND);
}

static int arasan_cf_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct arasan_cf_dev *acdev = host->ports[0]->private_data;

	cf_init(acdev);
	ata_host_resume(host);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(arasan_cf_pm_ops, arasan_cf_suspend, arasan_cf_resume);

#ifdef CONFIG_OF
static const struct of_device_id arasan_cf_id_table[] = {
	{ .compatible = "arasan,cf-spear1340" },
	{}
};
MODULE_DEVICE_TABLE(of, arasan_cf_id_table);
#endif

static struct platform_driver arasan_cf_driver = {
	.probe		= arasan_cf_probe,
	.remove		= arasan_cf_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.pm	= &arasan_cf_pm_ops,
		.of_match_table = of_match_ptr(arasan_cf_id_table),
	},
};

module_platform_driver(arasan_cf_driver);

MODULE_AUTHOR("Viresh Kumar <viresh.linux@gmail.com>");
MODULE_DESCRIPTION("Arasan ATA Compact Flash driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
