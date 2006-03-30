/*
 *  sata_sx4.c - Promise SATA
 *
 *  Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *  		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2003-2004 Red Hat, Inc.
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *  Hardware documentation available under NDA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>
#include <asm/io.h>
#include "sata_promise.h"

#define DRV_NAME	"sata_sx4"
#define DRV_VERSION	"0.8"


enum {
	PDC_PRD_TBL		= 0x44,	/* Direct command DMA table addr */

	PDC_PKT_SUBMIT		= 0x40, /* Command packet pointer addr */
	PDC_HDMA_PKT_SUBMIT	= 0x100, /* Host DMA packet pointer addr */
	PDC_INT_SEQMASK		= 0x40,	/* Mask of asserted SEQ INTs */
	PDC_HDMA_CTLSTAT	= 0x12C, /* Host DMA control / status */

	PDC_20621_SEQCTL	= 0x400,
	PDC_20621_SEQMASK	= 0x480,
	PDC_20621_GENERAL_CTL	= 0x484,
	PDC_20621_PAGE_SIZE	= (32 * 1024),

	/* chosen, not constant, values; we design our own DIMM mem map */
	PDC_20621_DIMM_WINDOW	= 0x0C,	/* page# for 32K DIMM window */
	PDC_20621_DIMM_BASE	= 0x00200000,
	PDC_20621_DIMM_DATA	= (64 * 1024),
	PDC_DIMM_DATA_STEP	= (256 * 1024),
	PDC_DIMM_WINDOW_STEP	= (8 * 1024),
	PDC_DIMM_HOST_PRD	= (6 * 1024),
	PDC_DIMM_HOST_PKT	= (128 * 0),
	PDC_DIMM_HPKT_PRD	= (128 * 1),
	PDC_DIMM_ATA_PKT	= (128 * 2),
	PDC_DIMM_APKT_PRD	= (128 * 3),
	PDC_DIMM_HEADER_SZ	= PDC_DIMM_APKT_PRD + 128,
	PDC_PAGE_WINDOW		= 0x40,
	PDC_PAGE_DATA		= PDC_PAGE_WINDOW +
				  (PDC_20621_DIMM_DATA / PDC_20621_PAGE_SIZE),
	PDC_PAGE_SET		= PDC_DIMM_DATA_STEP / PDC_20621_PAGE_SIZE,

	PDC_CHIP0_OFS		= 0xC0000, /* offset of chip #0 */

	PDC_20621_ERR_MASK	= (1<<19) | (1<<20) | (1<<21) | (1<<22) |
				  (1<<23),

	board_20621		= 0,	/* FastTrak S150 SX4 */

	PDC_RESET		= (1 << 11), /* HDMA reset */

	PDC_MAX_HDMA		= 32,
	PDC_HDMA_Q_MASK		= (PDC_MAX_HDMA - 1),

	PDC_DIMM0_SPD_DEV_ADDRESS     = 0x50,
	PDC_DIMM1_SPD_DEV_ADDRESS     = 0x51,
	PDC_MAX_DIMM_MODULE           = 0x02,
	PDC_I2C_CONTROL_OFFSET        = 0x48,
	PDC_I2C_ADDR_DATA_OFFSET      = 0x4C,
	PDC_DIMM0_CONTROL_OFFSET      = 0x80,
	PDC_DIMM1_CONTROL_OFFSET      = 0x84,
	PDC_SDRAM_CONTROL_OFFSET      = 0x88,
	PDC_I2C_WRITE                 = 0x00000000,
	PDC_I2C_READ                  = 0x00000040,
	PDC_I2C_START                 = 0x00000080,
	PDC_I2C_MASK_INT              = 0x00000020,
	PDC_I2C_COMPLETE              = 0x00010000,
	PDC_I2C_NO_ACK                = 0x00100000,
	PDC_DIMM_SPD_SUBADDRESS_START = 0x00,
	PDC_DIMM_SPD_SUBADDRESS_END   = 0x7F,
	PDC_DIMM_SPD_ROW_NUM          = 3,
	PDC_DIMM_SPD_COLUMN_NUM       = 4,
	PDC_DIMM_SPD_MODULE_ROW       = 5,
	PDC_DIMM_SPD_TYPE             = 11,
	PDC_DIMM_SPD_FRESH_RATE       = 12,
	PDC_DIMM_SPD_BANK_NUM         = 17,
	PDC_DIMM_SPD_CAS_LATENCY      = 18,
	PDC_DIMM_SPD_ATTRIBUTE        = 21,
	PDC_DIMM_SPD_ROW_PRE_CHARGE   = 27,
	PDC_DIMM_SPD_ROW_ACTIVE_DELAY = 28,
	PDC_DIMM_SPD_RAS_CAS_DELAY    = 29,
	PDC_DIMM_SPD_ACTIVE_PRECHARGE = 30,
	PDC_DIMM_SPD_SYSTEM_FREQ      = 126,
	PDC_CTL_STATUS		      = 0x08,
	PDC_DIMM_WINDOW_CTLR	      = 0x0C,
	PDC_TIME_CONTROL              = 0x3C,
	PDC_TIME_PERIOD               = 0x40,
	PDC_TIME_COUNTER              = 0x44,
	PDC_GENERAL_CTLR	      = 0x484,
	PCI_PLL_INIT                  = 0x8A531824,
	PCI_X_TCOUNT                  = 0xEE1E5CFF
};


struct pdc_port_priv {
	u8			dimm_buf[(ATA_PRD_SZ * ATA_MAX_PRD) + 512];
	u8			*pkt;
	dma_addr_t		pkt_dma;
};

struct pdc_host_priv {
	void			__iomem *dimm_mmio;

	unsigned int		doing_hdma;
	unsigned int		hdma_prod;
	unsigned int		hdma_cons;
	struct {
		struct ata_queued_cmd *qc;
		unsigned int	seq;
		unsigned long	pkt_ofs;
	} hdma[32];
};


static int pdc_sata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static irqreturn_t pdc20621_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static void pdc_eng_timeout(struct ata_port *ap);
static void pdc_20621_phy_reset (struct ata_port *ap);
static int pdc_port_start(struct ata_port *ap);
static void pdc_port_stop(struct ata_port *ap);
static void pdc20621_qc_prep(struct ata_queued_cmd *qc);
static void pdc_tf_load_mmio(struct ata_port *ap, const struct ata_taskfile *tf);
static void pdc_exec_command_mmio(struct ata_port *ap, const struct ata_taskfile *tf);
static void pdc20621_host_stop(struct ata_host_set *host_set);
static unsigned int pdc20621_dimm_init(struct ata_probe_ent *pe);
static int pdc20621_detect_dimm(struct ata_probe_ent *pe);
static unsigned int pdc20621_i2c_read(struct ata_probe_ent *pe,
				      u32 device, u32 subaddr, u32 *pdata);
static int pdc20621_prog_dimm0(struct ata_probe_ent *pe);
static unsigned int pdc20621_prog_dimm_global(struct ata_probe_ent *pe);
#ifdef ATA_VERBOSE_DEBUG
static void pdc20621_get_from_dimm(struct ata_probe_ent *pe,
				   void *psource, u32 offset, u32 size);
#endif
static void pdc20621_put_to_dimm(struct ata_probe_ent *pe,
				 void *psource, u32 offset, u32 size);
static void pdc20621_irq_clear(struct ata_port *ap);
static unsigned int pdc20621_qc_issue_prot(struct ata_queued_cmd *qc);


static struct scsi_host_template pdc_sata_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations pdc_20621_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= pdc_tf_load_mmio,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= pdc_exec_command_mmio,
	.dev_select		= ata_std_dev_select,
	.phy_reset		= pdc_20621_phy_reset,
	.qc_prep		= pdc20621_qc_prep,
	.qc_issue		= pdc20621_qc_issue_prot,
	.eng_timeout		= pdc_eng_timeout,
	.irq_handler		= pdc20621_interrupt,
	.irq_clear		= pdc20621_irq_clear,
	.port_start		= pdc_port_start,
	.port_stop		= pdc_port_stop,
	.host_stop		= pdc20621_host_stop,
};

static const struct ata_port_info pdc_port_info[] = {
	/* board_20621 */
	{
		.sht		= &pdc_sata_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_SRST | ATA_FLAG_MMIO |
				  ATA_FLAG_NO_ATAPI,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_20621_ops,
	},

};

static const struct pci_device_id pdc_sata_pci_tbl[] = {
	{ PCI_VENDOR_ID_PROMISE, 0x6622, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20621 },
	{ }	/* terminate list */
};


static struct pci_driver pdc_sata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= pdc_sata_pci_tbl,
	.probe			= pdc_sata_init_one,
	.remove			= ata_pci_remove_one,
};


static void pdc20621_host_stop(struct ata_host_set *host_set)
{
	struct pci_dev *pdev = to_pci_dev(host_set->dev);
	struct pdc_host_priv *hpriv = host_set->private_data;
	void __iomem *dimm_mmio = hpriv->dimm_mmio;

	pci_iounmap(pdev, dimm_mmio);
	kfree(hpriv);

	pci_iounmap(pdev, host_set->mmio_base);
}

static int pdc_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct pdc_port_priv *pp;
	int rc;

	rc = ata_port_start(ap);
	if (rc)
		return rc;

	pp = kmalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		rc = -ENOMEM;
		goto err_out;
	}
	memset(pp, 0, sizeof(*pp));

	pp->pkt = dma_alloc_coherent(dev, 128, &pp->pkt_dma, GFP_KERNEL);
	if (!pp->pkt) {
		rc = -ENOMEM;
		goto err_out_kfree;
	}

	ap->private_data = pp;

	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}


static void pdc_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct pdc_port_priv *pp = ap->private_data;

	ap->private_data = NULL;
	dma_free_coherent(dev, 128, pp->pkt, pp->pkt_dma);
	kfree(pp);
	ata_port_stop(ap);
}


static void pdc_20621_phy_reset (struct ata_port *ap)
{
	VPRINTK("ENTER\n");
        ap->cbl = ATA_CBL_SATA;
        ata_port_probe(ap);
        ata_bus_reset(ap);
}

static inline void pdc20621_ata_sg(struct ata_taskfile *tf, u8 *buf,
				    	   unsigned int portno,
					   unsigned int total_len)
{
	u32 addr;
	unsigned int dw = PDC_DIMM_APKT_PRD >> 2;
	u32 *buf32 = (u32 *) buf;

	/* output ATA packet S/G table */
	addr = PDC_20621_DIMM_BASE + PDC_20621_DIMM_DATA +
	       (PDC_DIMM_DATA_STEP * portno);
	VPRINTK("ATA sg addr 0x%x, %d\n", addr, addr);
	buf32[dw] = cpu_to_le32(addr);
	buf32[dw + 1] = cpu_to_le32(total_len | ATA_PRD_EOT);

	VPRINTK("ATA PSG @ %x == (0x%x, 0x%x)\n",
		PDC_20621_DIMM_BASE +
		       (PDC_DIMM_WINDOW_STEP * portno) +
		       PDC_DIMM_APKT_PRD,
		buf32[dw], buf32[dw + 1]);
}

static inline void pdc20621_host_sg(struct ata_taskfile *tf, u8 *buf,
				    	    unsigned int portno,
					    unsigned int total_len)
{
	u32 addr;
	unsigned int dw = PDC_DIMM_HPKT_PRD >> 2;
	u32 *buf32 = (u32 *) buf;

	/* output Host DMA packet S/G table */
	addr = PDC_20621_DIMM_BASE + PDC_20621_DIMM_DATA +
	       (PDC_DIMM_DATA_STEP * portno);

	buf32[dw] = cpu_to_le32(addr);
	buf32[dw + 1] = cpu_to_le32(total_len | ATA_PRD_EOT);

	VPRINTK("HOST PSG @ %x == (0x%x, 0x%x)\n",
		PDC_20621_DIMM_BASE +
		       (PDC_DIMM_WINDOW_STEP * portno) +
		       PDC_DIMM_HPKT_PRD,
		buf32[dw], buf32[dw + 1]);
}

static inline unsigned int pdc20621_ata_pkt(struct ata_taskfile *tf,
					    unsigned int devno, u8 *buf,
					    unsigned int portno)
{
	unsigned int i, dw;
	u32 *buf32 = (u32 *) buf;
	u8 dev_reg;

	unsigned int dimm_sg = PDC_20621_DIMM_BASE +
			       (PDC_DIMM_WINDOW_STEP * portno) +
			       PDC_DIMM_APKT_PRD;
	VPRINTK("ENTER, dimm_sg == 0x%x, %d\n", dimm_sg, dimm_sg);

	i = PDC_DIMM_ATA_PKT;

	/*
	 * Set up ATA packet
	 */
	if ((tf->protocol == ATA_PROT_DMA) && (!(tf->flags & ATA_TFLAG_WRITE)))
		buf[i++] = PDC_PKT_READ;
	else if (tf->protocol == ATA_PROT_NODATA)
		buf[i++] = PDC_PKT_NODATA;
	else
		buf[i++] = 0;
	buf[i++] = 0;			/* reserved */
	buf[i++] = portno + 1;		/* seq. id */
	buf[i++] = 0xff;		/* delay seq. id */

	/* dimm dma S/G, and next-pkt */
	dw = i >> 2;
	if (tf->protocol == ATA_PROT_NODATA)
		buf32[dw] = 0;
	else
		buf32[dw] = cpu_to_le32(dimm_sg);
	buf32[dw + 1] = 0;
	i += 8;

	if (devno == 0)
		dev_reg = ATA_DEVICE_OBS;
	else
		dev_reg = ATA_DEVICE_OBS | ATA_DEV1;

	/* select device */
	buf[i++] = (1 << 5) | PDC_PKT_CLEAR_BSY | ATA_REG_DEVICE;
	buf[i++] = dev_reg;

	/* device control register */
	buf[i++] = (1 << 5) | PDC_REG_DEVCTL;
	buf[i++] = tf->ctl;

	return i;
}

static inline void pdc20621_host_pkt(struct ata_taskfile *tf, u8 *buf,
				     unsigned int portno)
{
	unsigned int dw;
	u32 tmp, *buf32 = (u32 *) buf;

	unsigned int host_sg = PDC_20621_DIMM_BASE +
			       (PDC_DIMM_WINDOW_STEP * portno) +
			       PDC_DIMM_HOST_PRD;
	unsigned int dimm_sg = PDC_20621_DIMM_BASE +
			       (PDC_DIMM_WINDOW_STEP * portno) +
			       PDC_DIMM_HPKT_PRD;
	VPRINTK("ENTER, dimm_sg == 0x%x, %d\n", dimm_sg, dimm_sg);
	VPRINTK("host_sg == 0x%x, %d\n", host_sg, host_sg);

	dw = PDC_DIMM_HOST_PKT >> 2;

	/*
	 * Set up Host DMA packet
	 */
	if ((tf->protocol == ATA_PROT_DMA) && (!(tf->flags & ATA_TFLAG_WRITE)))
		tmp = PDC_PKT_READ;
	else
		tmp = 0;
	tmp |= ((portno + 1 + 4) << 16);	/* seq. id */
	tmp |= (0xff << 24);			/* delay seq. id */
	buf32[dw + 0] = cpu_to_le32(tmp);
	buf32[dw + 1] = cpu_to_le32(host_sg);
	buf32[dw + 2] = cpu_to_le32(dimm_sg);
	buf32[dw + 3] = 0;

	VPRINTK("HOST PKT @ %x == (0x%x 0x%x 0x%x 0x%x)\n",
		PDC_20621_DIMM_BASE + (PDC_DIMM_WINDOW_STEP * portno) +
			PDC_DIMM_HOST_PKT,
		buf32[dw + 0],
		buf32[dw + 1],
		buf32[dw + 2],
		buf32[dw + 3]);
}

static void pdc20621_dma_prep(struct ata_queued_cmd *qc)
{
	struct scatterlist *sg;
	struct ata_port *ap = qc->ap;
	struct pdc_port_priv *pp = ap->private_data;
	void __iomem *mmio = ap->host_set->mmio_base;
	struct pdc_host_priv *hpriv = ap->host_set->private_data;
	void __iomem *dimm_mmio = hpriv->dimm_mmio;
	unsigned int portno = ap->port_no;
	unsigned int i, idx, total_len = 0, sgt_len;
	u32 *buf = (u32 *) &pp->dimm_buf[PDC_DIMM_HEADER_SZ];

	WARN_ON(!(qc->flags & ATA_QCFLAG_DMAMAP));

	VPRINTK("ata%u: ENTER\n", ap->id);

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	/*
	 * Build S/G table
	 */
	idx = 0;
	ata_for_each_sg(sg, qc) {
		buf[idx++] = cpu_to_le32(sg_dma_address(sg));
		buf[idx++] = cpu_to_le32(sg_dma_len(sg));
		total_len += sg_dma_len(sg);
	}
	buf[idx - 1] |= cpu_to_le32(ATA_PRD_EOT);
	sgt_len = idx * 4;

	/*
	 * Build ATA, host DMA packets
	 */
	pdc20621_host_sg(&qc->tf, &pp->dimm_buf[0], portno, total_len);
	pdc20621_host_pkt(&qc->tf, &pp->dimm_buf[0], portno);

	pdc20621_ata_sg(&qc->tf, &pp->dimm_buf[0], portno, total_len);
	i = pdc20621_ata_pkt(&qc->tf, qc->dev->devno, &pp->dimm_buf[0], portno);

	if (qc->tf.flags & ATA_TFLAG_LBA48)
		i = pdc_prep_lba48(&qc->tf, &pp->dimm_buf[0], i);
	else
		i = pdc_prep_lba28(&qc->tf, &pp->dimm_buf[0], i);

	pdc_pkt_footer(&qc->tf, &pp->dimm_buf[0], i);

	/* copy three S/G tables and two packets to DIMM MMIO window */
	memcpy_toio(dimm_mmio + (portno * PDC_DIMM_WINDOW_STEP),
		    &pp->dimm_buf, PDC_DIMM_HEADER_SZ);
	memcpy_toio(dimm_mmio + (portno * PDC_DIMM_WINDOW_STEP) +
		    PDC_DIMM_HOST_PRD,
		    &pp->dimm_buf[PDC_DIMM_HEADER_SZ], sgt_len);

	/* force host FIFO dump */
	writel(0x00000001, mmio + PDC_20621_GENERAL_CTL);

	readl(dimm_mmio);	/* MMIO PCI posting flush */

	VPRINTK("ata pkt buf ofs %u, prd size %u, mmio copied\n", i, sgt_len);
}

static void pdc20621_nodata_prep(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pdc_port_priv *pp = ap->private_data;
	void __iomem *mmio = ap->host_set->mmio_base;
	struct pdc_host_priv *hpriv = ap->host_set->private_data;
	void __iomem *dimm_mmio = hpriv->dimm_mmio;
	unsigned int portno = ap->port_no;
	unsigned int i;

	VPRINTK("ata%u: ENTER\n", ap->id);

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	i = pdc20621_ata_pkt(&qc->tf, qc->dev->devno, &pp->dimm_buf[0], portno);

	if (qc->tf.flags & ATA_TFLAG_LBA48)
		i = pdc_prep_lba48(&qc->tf, &pp->dimm_buf[0], i);
	else
		i = pdc_prep_lba28(&qc->tf, &pp->dimm_buf[0], i);

	pdc_pkt_footer(&qc->tf, &pp->dimm_buf[0], i);

	/* copy three S/G tables and two packets to DIMM MMIO window */
	memcpy_toio(dimm_mmio + (portno * PDC_DIMM_WINDOW_STEP),
		    &pp->dimm_buf, PDC_DIMM_HEADER_SZ);

	/* force host FIFO dump */
	writel(0x00000001, mmio + PDC_20621_GENERAL_CTL);

	readl(dimm_mmio);	/* MMIO PCI posting flush */

	VPRINTK("ata pkt buf ofs %u, mmio copied\n", i);
}

static void pdc20621_qc_prep(struct ata_queued_cmd *qc)
{
	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
		pdc20621_dma_prep(qc);
		break;
	case ATA_PROT_NODATA:
		pdc20621_nodata_prep(qc);
		break;
	default:
		break;
	}
}

static void __pdc20621_push_hdma(struct ata_queued_cmd *qc,
				 unsigned int seq,
				 u32 pkt_ofs)
{
	struct ata_port *ap = qc->ap;
	struct ata_host_set *host_set = ap->host_set;
	void __iomem *mmio = host_set->mmio_base;

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	writel(0x00000001, mmio + PDC_20621_SEQCTL + (seq * 4));
	readl(mmio + PDC_20621_SEQCTL + (seq * 4));	/* flush */

	writel(pkt_ofs, mmio + PDC_HDMA_PKT_SUBMIT);
	readl(mmio + PDC_HDMA_PKT_SUBMIT);	/* flush */
}

static void pdc20621_push_hdma(struct ata_queued_cmd *qc,
				unsigned int seq,
				u32 pkt_ofs)
{
	struct ata_port *ap = qc->ap;
	struct pdc_host_priv *pp = ap->host_set->private_data;
	unsigned int idx = pp->hdma_prod & PDC_HDMA_Q_MASK;

	if (!pp->doing_hdma) {
		__pdc20621_push_hdma(qc, seq, pkt_ofs);
		pp->doing_hdma = 1;
		return;
	}

	pp->hdma[idx].qc = qc;
	pp->hdma[idx].seq = seq;
	pp->hdma[idx].pkt_ofs = pkt_ofs;
	pp->hdma_prod++;
}

static void pdc20621_pop_hdma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pdc_host_priv *pp = ap->host_set->private_data;
	unsigned int idx = pp->hdma_cons & PDC_HDMA_Q_MASK;

	/* if nothing on queue, we're done */
	if (pp->hdma_prod == pp->hdma_cons) {
		pp->doing_hdma = 0;
		return;
	}

	__pdc20621_push_hdma(pp->hdma[idx].qc, pp->hdma[idx].seq,
			     pp->hdma[idx].pkt_ofs);
	pp->hdma_cons++;
}

#ifdef ATA_VERBOSE_DEBUG
static void pdc20621_dump_hdma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int port_no = ap->port_no;
	struct pdc_host_priv *hpriv = ap->host_set->private_data;
	void *dimm_mmio = hpriv->dimm_mmio;

	dimm_mmio += (port_no * PDC_DIMM_WINDOW_STEP);
	dimm_mmio += PDC_DIMM_HOST_PKT;

	printk(KERN_ERR "HDMA[0] == 0x%08X\n", readl(dimm_mmio));
	printk(KERN_ERR "HDMA[1] == 0x%08X\n", readl(dimm_mmio + 4));
	printk(KERN_ERR "HDMA[2] == 0x%08X\n", readl(dimm_mmio + 8));
	printk(KERN_ERR "HDMA[3] == 0x%08X\n", readl(dimm_mmio + 12));
}
#else
static inline void pdc20621_dump_hdma(struct ata_queued_cmd *qc) { }
#endif /* ATA_VERBOSE_DEBUG */

static void pdc20621_packet_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_host_set *host_set = ap->host_set;
	unsigned int port_no = ap->port_no;
	void __iomem *mmio = host_set->mmio_base;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 seq = (u8) (port_no + 1);
	unsigned int port_ofs;

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	VPRINTK("ata%u: ENTER\n", ap->id);

	wmb();			/* flush PRD, pkt writes */

	port_ofs = PDC_20621_DIMM_BASE + (PDC_DIMM_WINDOW_STEP * port_no);

	/* if writing, we (1) DMA to DIMM, then (2) do ATA command */
	if (rw && qc->tf.protocol == ATA_PROT_DMA) {
		seq += 4;

		pdc20621_dump_hdma(qc);
		pdc20621_push_hdma(qc, seq, port_ofs + PDC_DIMM_HOST_PKT);
		VPRINTK("queued ofs 0x%x (%u), seq %u\n",
			port_ofs + PDC_DIMM_HOST_PKT,
			port_ofs + PDC_DIMM_HOST_PKT,
			seq);
	} else {
		writel(0x00000001, mmio + PDC_20621_SEQCTL + (seq * 4));
		readl(mmio + PDC_20621_SEQCTL + (seq * 4));	/* flush */

		writel(port_ofs + PDC_DIMM_ATA_PKT,
		       (void __iomem *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
		readl((void __iomem *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
		VPRINTK("submitted ofs 0x%x (%u), seq %u\n",
			port_ofs + PDC_DIMM_ATA_PKT,
			port_ofs + PDC_DIMM_ATA_PKT,
			seq);
	}
}

static unsigned int pdc20621_qc_issue_prot(struct ata_queued_cmd *qc)
{
	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
	case ATA_PROT_NODATA:
		pdc20621_packet_start(qc);
		return 0;

	case ATA_PROT_ATAPI_DMA:
		BUG();
		break;

	default:
		break;
	}

	return ata_qc_issue_prot(qc);
}

static inline unsigned int pdc20621_host_intr( struct ata_port *ap,
                                          struct ata_queued_cmd *qc,
					  unsigned int doing_hdma,
					  void __iomem *mmio)
{
	unsigned int port_no = ap->port_no;
	unsigned int port_ofs =
		PDC_20621_DIMM_BASE + (PDC_DIMM_WINDOW_STEP * port_no);
	u8 status;
	unsigned int handled = 0;

	VPRINTK("ENTER\n");

	if ((qc->tf.protocol == ATA_PROT_DMA) &&	/* read */
	    (!(qc->tf.flags & ATA_TFLAG_WRITE))) {

		/* step two - DMA from DIMM to host */
		if (doing_hdma) {
			VPRINTK("ata%u: read hdma, 0x%x 0x%x\n", ap->id,
				readl(mmio + 0x104), readl(mmio + PDC_HDMA_CTLSTAT));
			/* get drive status; clear intr; complete txn */
			qc->err_mask |= ac_err_mask(ata_wait_idle(ap));
			ata_qc_complete(qc);
			pdc20621_pop_hdma(qc);
		}

		/* step one - exec ATA command */
		else {
			u8 seq = (u8) (port_no + 1 + 4);
			VPRINTK("ata%u: read ata, 0x%x 0x%x\n", ap->id,
				readl(mmio + 0x104), readl(mmio + PDC_HDMA_CTLSTAT));

			/* submit hdma pkt */
			pdc20621_dump_hdma(qc);
			pdc20621_push_hdma(qc, seq,
					   port_ofs + PDC_DIMM_HOST_PKT);
		}
		handled = 1;

	} else if (qc->tf.protocol == ATA_PROT_DMA) {	/* write */

		/* step one - DMA from host to DIMM */
		if (doing_hdma) {
			u8 seq = (u8) (port_no + 1);
			VPRINTK("ata%u: write hdma, 0x%x 0x%x\n", ap->id,
				readl(mmio + 0x104), readl(mmio + PDC_HDMA_CTLSTAT));

			/* submit ata pkt */
			writel(0x00000001, mmio + PDC_20621_SEQCTL + (seq * 4));
			readl(mmio + PDC_20621_SEQCTL + (seq * 4));
			writel(port_ofs + PDC_DIMM_ATA_PKT,
			       (void __iomem *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
			readl((void __iomem *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
		}

		/* step two - execute ATA command */
		else {
			VPRINTK("ata%u: write ata, 0x%x 0x%x\n", ap->id,
				readl(mmio + 0x104), readl(mmio + PDC_HDMA_CTLSTAT));
			/* get drive status; clear intr; complete txn */
			qc->err_mask |= ac_err_mask(ata_wait_idle(ap));
			ata_qc_complete(qc);
			pdc20621_pop_hdma(qc);
		}
		handled = 1;

	/* command completion, but no data xfer */
	} else if (qc->tf.protocol == ATA_PROT_NODATA) {

		status = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);
		DPRINTK("BUS_NODATA (drv_stat 0x%X)\n", status);
		qc->err_mask |= ac_err_mask(status);
		ata_qc_complete(qc);
		handled = 1;

	} else {
		ap->stats.idle_irq++;
	}

	return handled;
}

static void pdc20621_irq_clear(struct ata_port *ap)
{
	struct ata_host_set *host_set = ap->host_set;
	void __iomem *mmio = host_set->mmio_base;

	mmio += PDC_CHIP0_OFS;

	readl(mmio + PDC_20621_SEQMASK);
}

static irqreturn_t pdc20621_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct ata_port *ap;
	u32 mask = 0;
	unsigned int i, tmp, port_no;
	unsigned int handled = 0;
	void __iomem *mmio_base;

	VPRINTK("ENTER\n");

	if (!host_set || !host_set->mmio_base) {
		VPRINTK("QUICK EXIT\n");
		return IRQ_NONE;
	}

	mmio_base = host_set->mmio_base;

	/* reading should also clear interrupts */
	mmio_base += PDC_CHIP0_OFS;
	mask = readl(mmio_base + PDC_20621_SEQMASK);
	VPRINTK("mask == 0x%x\n", mask);

	if (mask == 0xffffffff) {
		VPRINTK("QUICK EXIT 2\n");
		return IRQ_NONE;
	}
	mask &= 0xffff;		/* only 16 tags possible */
	if (!mask) {
		VPRINTK("QUICK EXIT 3\n");
		return IRQ_NONE;
	}

        spin_lock(&host_set->lock);

        for (i = 1; i < 9; i++) {
		port_no = i - 1;
		if (port_no > 3)
			port_no -= 4;
		if (port_no >= host_set->n_ports)
			ap = NULL;
		else
			ap = host_set->ports[port_no];
		tmp = mask & (1 << i);
		VPRINTK("seq %u, port_no %u, ap %p, tmp %x\n", i, port_no, ap, tmp);
		if (tmp && ap &&
		    !(ap->flags & (ATA_FLAG_PORT_DISABLED | ATA_FLAG_NOINTR))) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.ctl & ATA_NIEN)))
				handled += pdc20621_host_intr(ap, qc, (i > 4),
							      mmio_base);
		}
	}

        spin_unlock(&host_set->lock);

	VPRINTK("mask == 0x%x\n", mask);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static void pdc_eng_timeout(struct ata_port *ap)
{
	u8 drv_stat;
	struct ata_host_set *host_set = ap->host_set;
	struct ata_queued_cmd *qc;
	unsigned long flags;

	DPRINTK("ENTER\n");

	spin_lock_irqsave(&host_set->lock, flags);

	qc = ata_qc_from_tag(ap, ap->active_tag);

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
	case ATA_PROT_NODATA:
		printk(KERN_ERR "ata%u: command timeout\n", ap->id);
		qc->err_mask |= __ac_err_mask(ata_wait_idle(ap));
		break;

	default:
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

		printk(KERN_ERR "ata%u: unknown timeout, cmd 0x%x stat 0x%x\n",
		       ap->id, qc->tf.command, drv_stat);

		qc->err_mask |= ac_err_mask(drv_stat);
		break;
	}

	spin_unlock_irqrestore(&host_set->lock, flags);
	ata_eh_qc_complete(qc);
	DPRINTK("EXIT\n");
}

static void pdc_tf_load_mmio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	WARN_ON (tf->protocol == ATA_PROT_DMA ||
		 tf->protocol == ATA_PROT_NODATA);
	ata_tf_load(ap, tf);
}


static void pdc_exec_command_mmio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	WARN_ON (tf->protocol == ATA_PROT_DMA ||
		 tf->protocol == ATA_PROT_NODATA);
	ata_exec_command(ap, tf);
}


static void pdc_sata_setup_port(struct ata_ioports *port, unsigned long base)
{
	port->cmd_addr		= base;
	port->data_addr		= base;
	port->feature_addr	=
	port->error_addr	= base + 0x4;
	port->nsect_addr	= base + 0x8;
	port->lbal_addr		= base + 0xc;
	port->lbam_addr		= base + 0x10;
	port->lbah_addr		= base + 0x14;
	port->device_addr	= base + 0x18;
	port->command_addr	=
	port->status_addr	= base + 0x1c;
	port->altstatus_addr	=
	port->ctl_addr		= base + 0x38;
}


#ifdef ATA_VERBOSE_DEBUG
static void pdc20621_get_from_dimm(struct ata_probe_ent *pe, void *psource,
				   u32 offset, u32 size)
{
	u32 window_size;
	u16 idx;
	u8 page_mask;
	long dist;
	void __iomem *mmio = pe->mmio_base;
	struct pdc_host_priv *hpriv = pe->private_data;
	void __iomem *dimm_mmio = hpriv->dimm_mmio;

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	page_mask = 0x00;
   	window_size = 0x2000 * 4; /* 32K byte uchar size */
	idx = (u16) (offset / window_size);

	writel(0x01, mmio + PDC_GENERAL_CTLR);
	readl(mmio + PDC_GENERAL_CTLR);
	writel(((idx) << page_mask), mmio + PDC_DIMM_WINDOW_CTLR);
	readl(mmio + PDC_DIMM_WINDOW_CTLR);

	offset -= (idx * window_size);
	idx++;
	dist = ((long) (window_size - (offset + size))) >= 0 ? size :
		(long) (window_size - offset);
	memcpy_fromio((char *) psource, (char *) (dimm_mmio + offset / 4),
		      dist);

	psource += dist;
	size -= dist;
	for (; (long) size >= (long) window_size ;) {
		writel(0x01, mmio + PDC_GENERAL_CTLR);
		readl(mmio + PDC_GENERAL_CTLR);
		writel(((idx) << page_mask), mmio + PDC_DIMM_WINDOW_CTLR);
		readl(mmio + PDC_DIMM_WINDOW_CTLR);
		memcpy_fromio((char *) psource, (char *) (dimm_mmio),
			      window_size / 4);
		psource += window_size;
		size -= window_size;
		idx ++;
	}

	if (size) {
		writel(0x01, mmio + PDC_GENERAL_CTLR);
		readl(mmio + PDC_GENERAL_CTLR);
		writel(((idx) << page_mask), mmio + PDC_DIMM_WINDOW_CTLR);
		readl(mmio + PDC_DIMM_WINDOW_CTLR);
		memcpy_fromio((char *) psource, (char *) (dimm_mmio),
			      size / 4);
	}
}
#endif


static void pdc20621_put_to_dimm(struct ata_probe_ent *pe, void *psource,
				 u32 offset, u32 size)
{
	u32 window_size;
	u16 idx;
	u8 page_mask;
	long dist;
	void __iomem *mmio = pe->mmio_base;
	struct pdc_host_priv *hpriv = pe->private_data;
	void __iomem *dimm_mmio = hpriv->dimm_mmio;

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	page_mask = 0x00;
   	window_size = 0x2000 * 4;       /* 32K byte uchar size */
	idx = (u16) (offset / window_size);

	writel(((idx) << page_mask), mmio + PDC_DIMM_WINDOW_CTLR);
	readl(mmio + PDC_DIMM_WINDOW_CTLR);
	offset -= (idx * window_size);
	idx++;
	dist = ((long)(s32)(window_size - (offset + size))) >= 0 ? size :
		(long) (window_size - offset);
	memcpy_toio(dimm_mmio + offset / 4, psource, dist);
	writel(0x01, mmio + PDC_GENERAL_CTLR);
	readl(mmio + PDC_GENERAL_CTLR);

	psource += dist;
	size -= dist;
	for (; (long) size >= (long) window_size ;) {
		writel(((idx) << page_mask), mmio + PDC_DIMM_WINDOW_CTLR);
		readl(mmio + PDC_DIMM_WINDOW_CTLR);
		memcpy_toio(dimm_mmio, psource, window_size / 4);
		writel(0x01, mmio + PDC_GENERAL_CTLR);
		readl(mmio + PDC_GENERAL_CTLR);
		psource += window_size;
		size -= window_size;
		idx ++;
	}

	if (size) {
		writel(((idx) << page_mask), mmio + PDC_DIMM_WINDOW_CTLR);
		readl(mmio + PDC_DIMM_WINDOW_CTLR);
		memcpy_toio(dimm_mmio, psource, size / 4);
		writel(0x01, mmio + PDC_GENERAL_CTLR);
		readl(mmio + PDC_GENERAL_CTLR);
	}
}


static unsigned int pdc20621_i2c_read(struct ata_probe_ent *pe, u32 device,
				      u32 subaddr, u32 *pdata)
{
	void __iomem *mmio = pe->mmio_base;
	u32 i2creg  = 0;
	u32 status;
	u32 count =0;

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	i2creg |= device << 24;
	i2creg |= subaddr << 16;

	/* Set the device and subaddress */
	writel(i2creg, mmio + PDC_I2C_ADDR_DATA_OFFSET);
	readl(mmio + PDC_I2C_ADDR_DATA_OFFSET);

	/* Write Control to perform read operation, mask int */
	writel(PDC_I2C_READ | PDC_I2C_START | PDC_I2C_MASK_INT,
	       mmio + PDC_I2C_CONTROL_OFFSET);

	for (count = 0; count <= 1000; count ++) {
		status = readl(mmio + PDC_I2C_CONTROL_OFFSET);
		if (status & PDC_I2C_COMPLETE) {
			status = readl(mmio + PDC_I2C_ADDR_DATA_OFFSET);
			break;
		} else if (count == 1000)
			return 0;
	}

	*pdata = (status >> 8) & 0x000000ff;
	return 1;
}


static int pdc20621_detect_dimm(struct ata_probe_ent *pe)
{
	u32 data=0 ;
  	if (pdc20621_i2c_read(pe, PDC_DIMM0_SPD_DEV_ADDRESS,
			     PDC_DIMM_SPD_SYSTEM_FREQ, &data)) {
   		if (data == 100)
			return 100;
  	} else
		return 0;

   	if (pdc20621_i2c_read(pe, PDC_DIMM0_SPD_DEV_ADDRESS, 9, &data)) {
		if(data <= 0x75)
			return 133;
   	} else
		return 0;

   	return 0;
}


static int pdc20621_prog_dimm0(struct ata_probe_ent *pe)
{
	u32 spd0[50];
	u32 data = 0;
   	int size, i;
   	u8 bdimmsize;
   	void __iomem *mmio = pe->mmio_base;
	static const struct {
		unsigned int reg;
		unsigned int ofs;
	} pdc_i2c_read_data [] = {
		{ PDC_DIMM_SPD_TYPE, 11 },
		{ PDC_DIMM_SPD_FRESH_RATE, 12 },
		{ PDC_DIMM_SPD_COLUMN_NUM, 4 },
		{ PDC_DIMM_SPD_ATTRIBUTE, 21 },
		{ PDC_DIMM_SPD_ROW_NUM, 3 },
		{ PDC_DIMM_SPD_BANK_NUM, 17 },
		{ PDC_DIMM_SPD_MODULE_ROW, 5 },
		{ PDC_DIMM_SPD_ROW_PRE_CHARGE, 27 },
		{ PDC_DIMM_SPD_ROW_ACTIVE_DELAY, 28 },
		{ PDC_DIMM_SPD_RAS_CAS_DELAY, 29 },
		{ PDC_DIMM_SPD_ACTIVE_PRECHARGE, 30 },
		{ PDC_DIMM_SPD_CAS_LATENCY, 18 },
	};

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	for(i=0; i<ARRAY_SIZE(pdc_i2c_read_data); i++)
		pdc20621_i2c_read(pe, PDC_DIMM0_SPD_DEV_ADDRESS,
				  pdc_i2c_read_data[i].reg,
				  &spd0[pdc_i2c_read_data[i].ofs]);

   	data |= (spd0[4] - 8) | ((spd0[21] != 0) << 3) | ((spd0[3]-11) << 4);
   	data |= ((spd0[17] / 4) << 6) | ((spd0[5] / 2) << 7) |
		((((spd0[27] + 9) / 10) - 1) << 8) ;
   	data |= (((((spd0[29] > spd0[28])
		    ? spd0[29] : spd0[28]) + 9) / 10) - 1) << 10;
   	data |= ((spd0[30] - spd0[29] + 9) / 10 - 2) << 12;

   	if (spd0[18] & 0x08)
		data |= ((0x03) << 14);
   	else if (spd0[18] & 0x04)
		data |= ((0x02) << 14);
   	else if (spd0[18] & 0x01)
		data |= ((0x01) << 14);
   	else
		data |= (0 << 14);

  	/*
	   Calculate the size of bDIMMSize (power of 2) and
	   merge the DIMM size by program start/end address.
	*/

   	bdimmsize = spd0[4] + (spd0[5] / 2) + spd0[3] + (spd0[17] / 2) + 3;
   	size = (1 << bdimmsize) >> 20;	/* size = xxx(MB) */
   	data |= (((size / 16) - 1) << 16);
   	data |= (0 << 23);
	data |= 8;
   	writel(data, mmio + PDC_DIMM0_CONTROL_OFFSET);
	readl(mmio + PDC_DIMM0_CONTROL_OFFSET);
   	return size;
}


static unsigned int pdc20621_prog_dimm_global(struct ata_probe_ent *pe)
{
	u32 data, spd0;
   	int error, i;
   	void __iomem *mmio = pe->mmio_base;

	/* hard-code chip #0 */
   	mmio += PDC_CHIP0_OFS;

   	/*
	  Set To Default : DIMM Module Global Control Register (0x022259F1)
	  DIMM Arbitration Disable (bit 20)
	  DIMM Data/Control Output Driving Selection (bit12 - bit15)
	  Refresh Enable (bit 17)
	*/

	data = 0x022259F1;
	writel(data, mmio + PDC_SDRAM_CONTROL_OFFSET);
	readl(mmio + PDC_SDRAM_CONTROL_OFFSET);

	/* Turn on for ECC */
	pdc20621_i2c_read(pe, PDC_DIMM0_SPD_DEV_ADDRESS,
			  PDC_DIMM_SPD_TYPE, &spd0);
	if (spd0 == 0x02) {
		data |= (0x01 << 16);
		writel(data, mmio + PDC_SDRAM_CONTROL_OFFSET);
		readl(mmio + PDC_SDRAM_CONTROL_OFFSET);
		printk(KERN_ERR "Local DIMM ECC Enabled\n");
   	}

   	/* DIMM Initialization Select/Enable (bit 18/19) */
   	data &= (~(1<<18));
   	data |= (1<<19);
   	writel(data, mmio + PDC_SDRAM_CONTROL_OFFSET);

   	error = 1;
   	for (i = 1; i <= 10; i++) {   /* polling ~5 secs */
		data = readl(mmio + PDC_SDRAM_CONTROL_OFFSET);
		if (!(data & (1<<19))) {
	   		error = 0;
	   		break;
		}
		msleep(i*100);
   	}
   	return error;
}


static unsigned int pdc20621_dimm_init(struct ata_probe_ent *pe)
{
	int speed, size, length;
	u32 addr,spd0,pci_status;
	u32 tmp=0;
	u32 time_period=0;
	u32 tcount=0;
	u32 ticks=0;
	u32 clock=0;
	u32 fparam=0;
   	void __iomem *mmio = pe->mmio_base;

	/* hard-code chip #0 */
   	mmio += PDC_CHIP0_OFS;

	/* Initialize PLL based upon PCI Bus Frequency */

	/* Initialize Time Period Register */
	writel(0xffffffff, mmio + PDC_TIME_PERIOD);
	time_period = readl(mmio + PDC_TIME_PERIOD);
	VPRINTK("Time Period Register (0x40): 0x%x\n", time_period);

	/* Enable timer */
	writel(0x00001a0, mmio + PDC_TIME_CONTROL);
	readl(mmio + PDC_TIME_CONTROL);

	/* Wait 3 seconds */
	msleep(3000);

	/*
	   When timer is enabled, counter is decreased every internal
	   clock cycle.
	*/

	tcount = readl(mmio + PDC_TIME_COUNTER);
	VPRINTK("Time Counter Register (0x44): 0x%x\n", tcount);

	/*
	   If SX4 is on PCI-X bus, after 3 seconds, the timer counter
	   register should be >= (0xffffffff - 3x10^8).
	*/
	if(tcount >= PCI_X_TCOUNT) {
		ticks = (time_period - tcount);
		VPRINTK("Num counters 0x%x (%d)\n", ticks, ticks);

		clock = (ticks / 300000);
		VPRINTK("10 * Internal clk = 0x%x (%d)\n", clock, clock);

		clock = (clock * 33);
		VPRINTK("10 * Internal clk * 33 = 0x%x (%d)\n", clock, clock);

		/* PLL F Param (bit 22:16) */
		fparam = (1400000 / clock) - 2;
		VPRINTK("PLL F Param: 0x%x (%d)\n", fparam, fparam);

		/* OD param = 0x2 (bit 31:30), R param = 0x5 (bit 29:25) */
		pci_status = (0x8a001824 | (fparam << 16));
	} else
		pci_status = PCI_PLL_INIT;

	/* Initialize PLL. */
	VPRINTK("pci_status: 0x%x\n", pci_status);
	writel(pci_status, mmio + PDC_CTL_STATUS);
	readl(mmio + PDC_CTL_STATUS);

	/*
	   Read SPD of DIMM by I2C interface,
	   and program the DIMM Module Controller.
	*/
 	if (!(speed = pdc20621_detect_dimm(pe))) {
		printk(KERN_ERR "Detect Local DIMM Fail\n");
		return 1;	/* DIMM error */
   	}
   	VPRINTK("Local DIMM Speed = %d\n", speed);

   	/* Programming DIMM0 Module Control Register (index_CID0:80h) */
   	size = pdc20621_prog_dimm0(pe);
   	VPRINTK("Local DIMM Size = %dMB\n",size);

   	/* Programming DIMM Module Global Control Register (index_CID0:88h) */
   	if (pdc20621_prog_dimm_global(pe)) {
		printk(KERN_ERR "Programming DIMM Module Global Control Register Fail\n");
		return 1;
   	}

#ifdef ATA_VERBOSE_DEBUG
	{
		u8 test_parttern1[40] = {0x55,0xAA,'P','r','o','m','i','s','e',' ',
  				'N','o','t',' ','Y','e','t',' ','D','e','f','i','n','e','d',' ',
 				 '1','.','1','0',
  				'9','8','0','3','1','6','1','2',0,0};
		u8 test_parttern2[40] = {0};

		pdc20621_put_to_dimm(pe, (void *) test_parttern2, 0x10040, 40);
		pdc20621_put_to_dimm(pe, (void *) test_parttern2, 0x40, 40);

		pdc20621_put_to_dimm(pe, (void *) test_parttern1, 0x10040, 40);
		pdc20621_get_from_dimm(pe, (void *) test_parttern2, 0x40, 40);
		printk(KERN_ERR "%x, %x, %s\n", test_parttern2[0],
		       test_parttern2[1], &(test_parttern2[2]));
		pdc20621_get_from_dimm(pe, (void *) test_parttern2, 0x10040,
				       40);
		printk(KERN_ERR "%x, %x, %s\n", test_parttern2[0],
		       test_parttern2[1], &(test_parttern2[2]));

		pdc20621_put_to_dimm(pe, (void *) test_parttern1, 0x40, 40);
		pdc20621_get_from_dimm(pe, (void *) test_parttern2, 0x40, 40);
		printk(KERN_ERR "%x, %x, %s\n", test_parttern2[0],
		       test_parttern2[1], &(test_parttern2[2]));
	}
#endif

	/* ECC initiliazation. */

	pdc20621_i2c_read(pe, PDC_DIMM0_SPD_DEV_ADDRESS,
			  PDC_DIMM_SPD_TYPE, &spd0);
	if (spd0 == 0x02) {
		VPRINTK("Start ECC initialization\n");
		addr = 0;
		length = size * 1024 * 1024;
		while (addr < length) {
			pdc20621_put_to_dimm(pe, (void *) &tmp, addr,
					     sizeof(u32));
			addr += sizeof(u32);
		}
		VPRINTK("Finish ECC initialization\n");
	}
	return 0;
}


static void pdc_20621_init(struct ata_probe_ent *pe)
{
	u32 tmp;
	void __iomem *mmio = pe->mmio_base;

	/* hard-code chip #0 */
	mmio += PDC_CHIP0_OFS;

	/*
	 * Select page 0x40 for our 32k DIMM window
	 */
	tmp = readl(mmio + PDC_20621_DIMM_WINDOW) & 0xffff0000;
	tmp |= PDC_PAGE_WINDOW;	/* page 40h; arbitrarily selected */
	writel(tmp, mmio + PDC_20621_DIMM_WINDOW);

	/*
	 * Reset Host DMA
	 */
	tmp = readl(mmio + PDC_HDMA_CTLSTAT);
	tmp |= PDC_RESET;
	writel(tmp, mmio + PDC_HDMA_CTLSTAT);
	readl(mmio + PDC_HDMA_CTLSTAT);		/* flush */

	udelay(10);

	tmp = readl(mmio + PDC_HDMA_CTLSTAT);
	tmp &= ~PDC_RESET;
	writel(tmp, mmio + PDC_HDMA_CTLSTAT);
	readl(mmio + PDC_HDMA_CTLSTAT);		/* flush */
}

static int pdc_sata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	unsigned long base;
	void __iomem *mmio_base;
	void __iomem *dimm_mmio = NULL;
	struct pdc_host_priv *hpriv = NULL;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	int pci_dev_busy = 0;
	int rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/*
	 * If this driver happens to only be useful on Apple's K2, then
	 * we should check that here as it has a normal Serverworks ID
	 */
	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	mmio_base = pci_iomap(pdev, 3, 0);
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}
	base = (unsigned long) mmio_base;

	hpriv = kmalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		rc = -ENOMEM;
		goto err_out_iounmap;
	}
	memset(hpriv, 0, sizeof(*hpriv));

	dimm_mmio = pci_iomap(pdev, 4, 0);
	if (!dimm_mmio) {
		kfree(hpriv);
		rc = -ENOMEM;
		goto err_out_iounmap;
	}

	hpriv->dimm_mmio = dimm_mmio;

	probe_ent->sht		= pdc_port_info[board_idx].sht;
	probe_ent->host_flags	= pdc_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= pdc_port_info[board_idx].pio_mask;
	probe_ent->mwdma_mask	= pdc_port_info[board_idx].mwdma_mask;
	probe_ent->udma_mask	= pdc_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= pdc_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;

	probe_ent->private_data = hpriv;
	base += PDC_CHIP0_OFS;

	probe_ent->n_ports = 4;
	pdc_sata_setup_port(&probe_ent->port[0], base + 0x200);
	pdc_sata_setup_port(&probe_ent->port[1], base + 0x280);
	pdc_sata_setup_port(&probe_ent->port[2], base + 0x300);
	pdc_sata_setup_port(&probe_ent->port[3], base + 0x380);

	pci_set_master(pdev);

	/* initialize adapter */
	/* initialize local dimm */
	if (pdc20621_dimm_init(probe_ent)) {
		rc = -ENOMEM;
		goto err_out_iounmap_dimm;
	}
	pdc_20621_init(probe_ent);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_iounmap_dimm:		/* only get to this label if 20621 */
	kfree(hpriv);
	pci_iounmap(pdev, dimm_mmio);
err_out_iounmap:
	pci_iounmap(pdev, mmio_base);
err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
	return rc;
}


static int __init pdc_sata_init(void)
{
	return pci_module_init(&pdc_sata_pci_driver);
}


static void __exit pdc_sata_exit(void)
{
	pci_unregister_driver(&pdc_sata_pci_driver);
}


MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Promise SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pdc_sata_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(pdc_sata_init);
module_exit(pdc_sata_exit);
