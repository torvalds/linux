/*
 * sata_inic162x.c - Driver for Initio 162x SATA controllers
 *
 * Copyright 2006  SUSE Linux Products GmbH
 * Copyright 2006  Tejun Heo <teheo@novell.com>
 *
 * This file is released under GPL v2.
 *
 * This controller is eccentric and easily locks up if something isn't
 * right.  Documentation is available at initio's website but it only
 * documents registers (not programming model).
 *
 * - ATA disks work.
 * - Hotplug works.
 * - ATAPI read works but burning doesn't.  This thing is really
 *   peculiar about ATAPI and I couldn't figure out how ATAPI PIO and
 *   ATAPI DMA WRITE should be programmed.  If you've got a clue, be
 *   my guest.
 * - Both STR and STD work.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/blkdev.h>
#include <scsi/scsi_device.h>

#define DRV_NAME	"sata_inic162x"
#define DRV_VERSION	"0.2"

enum {
	MMIO_BAR		= 5,

	NR_PORTS		= 2,

	HOST_CTL		= 0x7c,
	HOST_STAT		= 0x7e,
	HOST_IRQ_STAT		= 0xbc,
	HOST_IRQ_MASK		= 0xbe,

	PORT_SIZE		= 0x40,

	/* registers for ATA TF operation */
	PORT_TF			= 0x00,
	PORT_ALT_STAT		= 0x08,
	PORT_IRQ_STAT		= 0x09,
	PORT_IRQ_MASK		= 0x0a,
	PORT_PRD_CTL		= 0x0b,
	PORT_PRD_ADDR		= 0x0c,
	PORT_PRD_XFERLEN	= 0x10,

	/* IDMA register */
	PORT_IDMA_CTL		= 0x14,

	PORT_SCR		= 0x20,

	/* HOST_CTL bits */
	HCTL_IRQOFF		= (1 << 8),  /* global IRQ off */
	HCTL_PWRDWN		= (1 << 13), /* power down PHYs */
	HCTL_SOFTRST		= (1 << 13), /* global reset (no phy reset) */
	HCTL_RPGSEL		= (1 << 15), /* register page select */

	HCTL_KNOWN_BITS		= HCTL_IRQOFF | HCTL_PWRDWN | HCTL_SOFTRST |
				  HCTL_RPGSEL,

	/* HOST_IRQ_(STAT|MASK) bits */
	HIRQ_PORT0		= (1 << 0),
	HIRQ_PORT1		= (1 << 1),
	HIRQ_SOFT		= (1 << 14),
	HIRQ_GLOBAL		= (1 << 15), /* STAT only */

	/* PORT_IRQ_(STAT|MASK) bits */
	PIRQ_OFFLINE		= (1 << 0),  /* device unplugged */
	PIRQ_ONLINE		= (1 << 1),  /* device plugged */
	PIRQ_COMPLETE		= (1 << 2),  /* completion interrupt */
	PIRQ_FATAL		= (1 << 3),  /* fatal error */
	PIRQ_ATA		= (1 << 4),  /* ATA interrupt */
	PIRQ_REPLY		= (1 << 5),  /* reply FIFO not empty */
	PIRQ_PENDING		= (1 << 7),  /* port IRQ pending (STAT only) */

	PIRQ_ERR		= PIRQ_OFFLINE | PIRQ_ONLINE | PIRQ_FATAL,

	PIRQ_MASK_DMA_READ	= PIRQ_REPLY | PIRQ_ATA,
	PIRQ_MASK_OTHER		= PIRQ_REPLY | PIRQ_COMPLETE,
	PIRQ_MASK_FREEZE	= 0xff,

	/* PORT_PRD_CTL bits */
	PRD_CTL_START		= (1 << 0),
	PRD_CTL_WR		= (1 << 3),
	PRD_CTL_DMAEN		= (1 << 7),  /* DMA enable */

	/* PORT_IDMA_CTL bits */
	IDMA_CTL_RST_ATA	= (1 << 2),  /* hardreset ATA bus */
	IDMA_CTL_RST_IDMA	= (1 << 5),  /* reset IDMA machinary */
	IDMA_CTL_GO		= (1 << 7),  /* IDMA mode go */
	IDMA_CTL_ATA_NIEN	= (1 << 8),  /* ATA IRQ disable */
};

struct inic_host_priv {
	u16	cached_hctl;
};

struct inic_port_priv {
	u8	dfl_prdctl;
	u8	cached_prdctl;
	u8	cached_pirq_mask;
};

static int inic_slave_config(struct scsi_device *sdev)
{
	/* This controller is braindamaged.  dma_boundary is 0xffff
	 * like others but it will lock up the whole machine HARD if
	 * 65536 byte PRD entry is fed.  Reduce maximum segment size.
	 */
	blk_queue_max_segment_size(sdev->request_queue, 65536 - 512);

	return ata_scsi_slave_config(sdev);
}

static struct scsi_host_template inic_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= inic_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static const int scr_map[] = {
	[SCR_STATUS]	= 0,
	[SCR_ERROR]	= 1,
	[SCR_CONTROL]	= 2,
};

static void __iomem * inic_port_base(struct ata_port *ap)
{
	return ap->host->iomap[MMIO_BAR] + ap->port_no * PORT_SIZE;
}

static void __inic_set_pirq_mask(struct ata_port *ap, u8 mask)
{
	void __iomem *port_base = inic_port_base(ap);
	struct inic_port_priv *pp = ap->private_data;

	writeb(mask, port_base + PORT_IRQ_MASK);
	pp->cached_pirq_mask = mask;
}

static void inic_set_pirq_mask(struct ata_port *ap, u8 mask)
{
	struct inic_port_priv *pp = ap->private_data;

	if (pp->cached_pirq_mask != mask)
		__inic_set_pirq_mask(ap, mask);
}

static void inic_reset_port(void __iomem *port_base)
{
	void __iomem *idma_ctl = port_base + PORT_IDMA_CTL;
	u16 ctl;

	ctl = readw(idma_ctl);
	ctl &= ~(IDMA_CTL_RST_IDMA | IDMA_CTL_ATA_NIEN | IDMA_CTL_GO);

	/* mask IRQ and assert reset */
	writew(ctl | IDMA_CTL_RST_IDMA | IDMA_CTL_ATA_NIEN, idma_ctl);
	readw(idma_ctl); /* flush */

	/* give it some time */
	msleep(1);

	/* release reset */
	writew(ctl | IDMA_CTL_ATA_NIEN, idma_ctl);

	/* clear irq */
	writeb(0xff, port_base + PORT_IRQ_STAT);

	/* reenable ATA IRQ, turn off IDMA mode */
	writew(ctl, idma_ctl);
}

static u32 inic_scr_read(struct ata_port *ap, unsigned sc_reg)
{
	void __iomem *scr_addr = ap->ioaddr.scr_addr;
	void __iomem *addr;
	u32 val;

	if (unlikely(sc_reg >= ARRAY_SIZE(scr_map)))
		return 0xffffffffU;

	addr = scr_addr + scr_map[sc_reg] * 4;
	val = readl(scr_addr + scr_map[sc_reg] * 4);

	/* this controller has stuck DIAG.N, ignore it */
	if (sc_reg == SCR_ERROR)
		val &= ~SERR_PHYRDY_CHG;
	return val;
}

static void inic_scr_write(struct ata_port *ap, unsigned sc_reg, u32 val)
{
	void __iomem *scr_addr = ap->ioaddr.scr_addr;
	void __iomem *addr;

	if (unlikely(sc_reg >= ARRAY_SIZE(scr_map)))
		return;

	addr = scr_addr + scr_map[sc_reg] * 4;
	writel(val, scr_addr + scr_map[sc_reg] * 4);
}

/*
 * In TF mode, inic162x is very similar to SFF device.  TF registers
 * function the same.  DMA engine behaves similary using the same PRD
 * format as BMDMA but different command register, interrupt and event
 * notification methods are used.  The following inic_bmdma_*()
 * functions do the impedance matching.
 */
static void inic_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct inic_port_priv *pp = ap->private_data;
	void __iomem *port_base = inic_port_base(ap);
	int rw = qc->tf.flags & ATA_TFLAG_WRITE;

	/* make sure device sees PRD table writes */
	wmb();

	/* load transfer length */
	writel(qc->nbytes, port_base + PORT_PRD_XFERLEN);

	/* turn on DMA and specify data direction */
	pp->cached_prdctl = pp->dfl_prdctl | PRD_CTL_DMAEN;
	if (!rw)
		pp->cached_prdctl |= PRD_CTL_WR;
	writeb(pp->cached_prdctl, port_base + PORT_PRD_CTL);

	/* issue r/w command */
	ap->ops->exec_command(ap, &qc->tf);
}

static void inic_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct inic_port_priv *pp = ap->private_data;
	void __iomem *port_base = inic_port_base(ap);

	/* start host DMA transaction */
	pp->cached_prdctl |= PRD_CTL_START;
	writeb(pp->cached_prdctl, port_base + PORT_PRD_CTL);
}

static void inic_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct inic_port_priv *pp = ap->private_data;
	void __iomem *port_base = inic_port_base(ap);

	/* stop DMA engine */
	writeb(pp->dfl_prdctl, port_base + PORT_PRD_CTL);
}

static u8 inic_bmdma_status(struct ata_port *ap)
{
	/* event is already verified by the interrupt handler */
	return ATA_DMA_INTR;
}

static void inic_irq_clear(struct ata_port *ap)
{
	/* noop */
}

static void inic_host_intr(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);
	struct ata_eh_info *ehi = &ap->eh_info;
	u8 irq_stat;

	/* fetch and clear irq */
	irq_stat = readb(port_base + PORT_IRQ_STAT);
	writeb(irq_stat, port_base + PORT_IRQ_STAT);

	if (likely(!(irq_stat & PIRQ_ERR))) {
		struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->active_tag);

		if (unlikely(!qc || (qc->tf.flags & ATA_TFLAG_POLLING))) {
			ata_chk_status(ap);	/* clear ATA interrupt */
			return;
		}

		if (likely(ata_host_intr(ap, qc)))
			return;

		ata_chk_status(ap);	/* clear ATA interrupt */
		ata_port_printk(ap, KERN_WARNING, "unhandled "
				"interrupt, irq_stat=%x\n", irq_stat);
		return;
	}

	/* error */
	ata_ehi_push_desc(ehi, "irq_stat=0x%x", irq_stat);

	if (irq_stat & (PIRQ_OFFLINE | PIRQ_ONLINE)) {
		ata_ehi_hotplugged(ehi);
		ata_port_freeze(ap);
	} else
		ata_port_abort(ap);
}

static irqreturn_t inic_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	void __iomem *mmio_base = host->iomap[MMIO_BAR];
	u16 host_irq_stat;
	int i, handled = 0;;

	host_irq_stat = readw(mmio_base + HOST_IRQ_STAT);

	if (unlikely(!(host_irq_stat & HIRQ_GLOBAL)))
		goto out;

	spin_lock(&host->lock);

	for (i = 0; i < NR_PORTS; i++) {
		struct ata_port *ap = host->ports[i];

		if (!(host_irq_stat & (HIRQ_PORT0 << i)))
			continue;

		if (likely(ap && !(ap->flags & ATA_FLAG_DISABLED))) {
			inic_host_intr(ap);
			handled++;
		} else {
			if (ata_ratelimit())
				dev_printk(KERN_ERR, host->dev, "interrupt "
					   "from disabled port %d (0x%x)\n",
					   i, host_irq_stat);
		}
	}

	spin_unlock(&host->lock);

 out:
	return IRQ_RETVAL(handled);
}

static unsigned int inic_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* ATA IRQ doesn't wait for DMA transfer completion and vice
	 * versa.  Mask IRQ selectively to detect command completion.
	 * Without it, ATA DMA read command can cause data corruption.
	 *
	 * Something similar might be needed for ATAPI writes.  I
	 * tried a lot of combinations but couldn't find the solution.
	 */
	if (qc->tf.protocol == ATA_PROT_DMA &&
	    !(qc->tf.flags & ATA_TFLAG_WRITE))
		inic_set_pirq_mask(ap, PIRQ_MASK_DMA_READ);
	else
		inic_set_pirq_mask(ap, PIRQ_MASK_OTHER);

	/* Issuing a command to yet uninitialized port locks up the
	 * controller.  Most of the time, this happens for the first
	 * command after reset which are ATA and ATAPI IDENTIFYs.
	 * Fast fail if stat is 0x7f or 0xff for those commands.
	 */
	if (unlikely(qc->tf.command == ATA_CMD_ID_ATA ||
		     qc->tf.command == ATA_CMD_ID_ATAPI)) {
		u8 stat = ata_chk_status(ap);
		if (stat == 0x7f || stat == 0xff)
			return AC_ERR_HSM;
	}

	return ata_qc_issue_prot(qc);
}

static void inic_freeze(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);

	__inic_set_pirq_mask(ap, PIRQ_MASK_FREEZE);

	ata_chk_status(ap);
	writeb(0xff, port_base + PORT_IRQ_STAT);

	readb(port_base + PORT_IRQ_STAT); /* flush */
}

static void inic_thaw(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);

	ata_chk_status(ap);
	writeb(0xff, port_base + PORT_IRQ_STAT);

	__inic_set_pirq_mask(ap, PIRQ_MASK_OTHER);

	readb(port_base + PORT_IRQ_STAT); /* flush */
}

/*
 * SRST and SControl hardreset don't give valid signature on this
 * controller.  Only controller specific hardreset mechanism works.
 */
static int inic_hardreset(struct ata_port *ap, unsigned int *class,
			  unsigned long deadline)
{
	void __iomem *port_base = inic_port_base(ap);
	void __iomem *idma_ctl = port_base + PORT_IDMA_CTL;
	const unsigned long *timing = sata_ehc_deb_timing(&ap->eh_context);
	u16 val;
	int rc;

	/* hammer it into sane state */
	inic_reset_port(port_base);

	val = readw(idma_ctl);
	writew(val | IDMA_CTL_RST_ATA, idma_ctl);
	readw(idma_ctl);	/* flush */
	msleep(1);
	writew(val & ~IDMA_CTL_RST_ATA, idma_ctl);

	rc = sata_phy_resume(ap, timing, deadline);
	if (rc) {
		ata_port_printk(ap, KERN_WARNING, "failed to resume "
				"link after reset (errno=%d)\n", rc);
		return rc;
	}

	*class = ATA_DEV_NONE;
	if (ata_port_online(ap)) {
		struct ata_taskfile tf;

		/* wait a while before checking status */
		msleep(150);

		rc = ata_wait_ready(ap, deadline);
		/* link occupied, -ENODEV too is an error */
		if (rc) {
			ata_port_printk(ap, KERN_WARNING, "device not ready "
					"after hardreset (errno=%d)\n", rc);
			return rc;
		}

		ata_tf_read(ap, &tf);
		*class = ata_dev_classify(&tf);
		if (*class == ATA_DEV_UNKNOWN)
			*class = ATA_DEV_NONE;
	}

	return 0;
}

static void inic_error_handler(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);
	struct inic_port_priv *pp = ap->private_data;
	unsigned long flags;

	/* reset PIO HSM and stop DMA engine */
	inic_reset_port(port_base);

	spin_lock_irqsave(ap->lock, flags);
	ap->hsm_task_state = HSM_ST_IDLE;
	writeb(pp->dfl_prdctl, port_base + PORT_PRD_CTL);
	spin_unlock_irqrestore(ap->lock, flags);

	/* PIO and DMA engines have been stopped, perform recovery */
	ata_do_eh(ap, ata_std_prereset, NULL, inic_hardreset,
		  ata_std_postreset);
}

static void inic_post_internal_cmd(struct ata_queued_cmd *qc)
{
	/* make DMA engine forget about the failed command */
	if (qc->flags & ATA_QCFLAG_FAILED)
		inic_reset_port(inic_port_base(qc->ap));
}

static void inic_dev_config(struct ata_device *dev)
{
	/* inic can only handle upto LBA28 max sectors */
	if (dev->max_sectors > ATA_MAX_SECTORS)
		dev->max_sectors = ATA_MAX_SECTORS;

	if (dev->n_sectors >= 1 << 28) {
		ata_dev_printk(dev, KERN_ERR,
	"ERROR: This driver doesn't support LBA48 yet and may cause\n"
	"                data corruption on such devices.  Disabling.\n");
		ata_dev_disable(dev);
	}
}

static void init_port(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);

	/* Setup PRD address */
	writel(ap->prd_dma, port_base + PORT_PRD_ADDR);
}

static int inic_port_resume(struct ata_port *ap)
{
	init_port(ap);
	return 0;
}

static int inic_port_start(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);
	struct inic_port_priv *pp;
	u8 tmp;
	int rc;

	/* alloc and initialize private data */
	pp = devm_kzalloc(ap->host->dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;
	ap->private_data = pp;

	/* default PRD_CTL value, DMAEN, WR and START off */
	tmp = readb(port_base + PORT_PRD_CTL);
	tmp &= ~(PRD_CTL_DMAEN | PRD_CTL_WR | PRD_CTL_START);
	pp->dfl_prdctl = tmp;

	/* Alloc resources */
	rc = ata_port_start(ap);
	if (rc) {
		kfree(pp);
		return rc;
	}

	init_port(ap);

	return 0;
}

static struct ata_port_operations inic_port_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.scr_read		= inic_scr_read,
	.scr_write		= inic_scr_write,

	.bmdma_setup		= inic_bmdma_setup,
	.bmdma_start		= inic_bmdma_start,
	.bmdma_stop		= inic_bmdma_stop,
	.bmdma_status		= inic_bmdma_status,

	.irq_clear		= inic_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.qc_prep	 	= ata_qc_prep,
	.qc_issue		= inic_qc_issue,
	.data_xfer		= ata_data_xfer,

	.freeze			= inic_freeze,
	.thaw			= inic_thaw,
	.error_handler		= inic_error_handler,
	.post_internal_cmd	= inic_post_internal_cmd,
	.dev_config		= inic_dev_config,

	.port_resume		= inic_port_resume,

	.port_start		= inic_port_start,
};

static struct ata_port_info inic_port_info = {
	/* For some reason, ATA_PROT_ATAPI is broken on this
	 * controller, and no, PIO_POLLING does't fix it.  It somehow
	 * manages to report the wrong ireason and ignoring ireason
	 * results in machine lock up.  Tell libata to always prefer
	 * DMA.
	 */
	.flags			= ATA_FLAG_SATA | ATA_FLAG_PIO_DMA,
	.pio_mask		= 0x1f,	/* pio0-4 */
	.mwdma_mask		= 0x07, /* mwdma0-2 */
	.udma_mask		= ATA_UDMA6,
	.port_ops		= &inic_port_ops
};

static int init_controller(void __iomem *mmio_base, u16 hctl)
{
	int i;
	u16 val;

	hctl &= ~HCTL_KNOWN_BITS;

	/* Soft reset whole controller.  Spec says reset duration is 3
	 * PCI clocks, be generous and give it 10ms.
	 */
	writew(hctl | HCTL_SOFTRST, mmio_base + HOST_CTL);
	readw(mmio_base + HOST_CTL); /* flush */

	for (i = 0; i < 10; i++) {
		msleep(1);
		val = readw(mmio_base + HOST_CTL);
		if (!(val & HCTL_SOFTRST))
			break;
	}

	if (val & HCTL_SOFTRST)
		return -EIO;

	/* mask all interrupts and reset ports */
	for (i = 0; i < NR_PORTS; i++) {
		void __iomem *port_base = mmio_base + i * PORT_SIZE;

		writeb(0xff, port_base + PORT_IRQ_MASK);
		inic_reset_port(port_base);
	}

	/* port IRQ is masked now, unmask global IRQ */
	writew(hctl & ~HCTL_IRQOFF, mmio_base + HOST_CTL);
	val = readw(mmio_base + HOST_IRQ_MASK);
	val &= ~(HIRQ_PORT0 | HIRQ_PORT1);
	writew(val, mmio_base + HOST_IRQ_MASK);

	return 0;
}

#ifdef CONFIG_PM
static int inic_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	struct inic_host_priv *hpriv = host->private_data;
	void __iomem *mmio_base = host->iomap[MMIO_BAR];
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	if (pdev->dev.power.power_state.event == PM_EVENT_SUSPEND) {
		rc = init_controller(mmio_base, hpriv->cached_hctl);
		if (rc)
			return rc;
	}

	ata_host_resume(host);

	return 0;
}
#endif

static int inic_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	const struct ata_port_info *ppi[] = { &inic_port_info, NULL };
	struct ata_host *host;
	struct inic_host_priv *hpriv;
	void __iomem * const *iomap;
	int i, rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* alloc host */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, NR_PORTS);
	hpriv = devm_kzalloc(&pdev->dev, sizeof(*hpriv), GFP_KERNEL);
	if (!host || !hpriv)
		return -ENOMEM;

	host->private_data = hpriv;

	/* acquire resources and fill host */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, 0x3f, DRV_NAME);
	if (rc)
		return rc;
	host->iomap = iomap = pcim_iomap_table(pdev);

	for (i = 0; i < NR_PORTS; i++) {
		struct ata_ioports *port = &host->ports[i]->ioaddr;
		void __iomem *port_base = iomap[MMIO_BAR] + i * PORT_SIZE;

		port->cmd_addr = iomap[2 * i];
		port->altstatus_addr =
		port->ctl_addr = (void __iomem *)
			((unsigned long)iomap[2 * i + 1] | ATA_PCI_CTL_OFS);
		port->scr_addr = port_base + PORT_SCR;

		ata_std_ports(port);
	}

	hpriv->cached_hctl = readw(iomap[MMIO_BAR] + HOST_CTL);

	/* Set dma_mask.  This devices doesn't support 64bit addressing. */
	rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "32-bit DMA enable failed\n");
		return rc;
	}

	rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "32-bit consistent DMA enable failed\n");
		return rc;
	}

	rc = init_controller(iomap[MMIO_BAR], hpriv->cached_hctl);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to initialize controller\n");
		return rc;
	}

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, inic_interrupt, IRQF_SHARED,
				 &inic_sht);
}

static const struct pci_device_id inic_pci_tbl[] = {
	{ PCI_VDEVICE(INIT, 0x1622), },
	{ },
};

static struct pci_driver inic_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= inic_pci_tbl,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= inic_pci_device_resume,
#endif
	.probe 		= inic_init_one,
	.remove		= ata_pci_remove_one,
};

static int __init inic_init(void)
{
	return pci_register_driver(&inic_pci_driver);
}

static void __exit inic_exit(void)
{
	pci_unregister_driver(&inic_pci_driver);
}

MODULE_AUTHOR("Tejun Heo");
MODULE_DESCRIPTION("low-level driver for Initio 162x SATA");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, inic_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(inic_init);
module_exit(inic_exit);
