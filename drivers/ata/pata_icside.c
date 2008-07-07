#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <scsi/scsi_host.h>
#include <linux/ata.h>
#include <linux/libata.h>

#include <asm/dma.h>
#include <asm/ecard.h>

#define DRV_NAME	"pata_icside"

#define ICS_IDENT_OFFSET		0x2280

#define ICS_ARCIN_V5_INTRSTAT		0x0000
#define ICS_ARCIN_V5_INTROFFSET		0x0004

#define ICS_ARCIN_V6_INTROFFSET_1	0x2200
#define ICS_ARCIN_V6_INTRSTAT_1		0x2290
#define ICS_ARCIN_V6_INTROFFSET_2	0x3200
#define ICS_ARCIN_V6_INTRSTAT_2		0x3290

struct portinfo {
	unsigned int dataoffset;
	unsigned int ctrloffset;
	unsigned int stepping;
};

static const struct portinfo pata_icside_portinfo_v5 = {
	.dataoffset	= 0x2800,
	.ctrloffset	= 0x2b80,
	.stepping	= 6,
};

static const struct portinfo pata_icside_portinfo_v6_1 = {
	.dataoffset	= 0x2000,
	.ctrloffset	= 0x2380,
	.stepping	= 6,
};

static const struct portinfo pata_icside_portinfo_v6_2 = {
	.dataoffset	= 0x3000,
	.ctrloffset	= 0x3380,
	.stepping	= 6,
};

#define PATA_ICSIDE_MAX_SG	128

struct pata_icside_state {
	void __iomem *irq_port;
	void __iomem *ioc_base;
	unsigned int type;
	unsigned int dma;
	struct {
		u8 port_sel;
		u8 disabled;
		unsigned int speed[ATA_MAX_DEVICES];
	} port[2];
	struct scatterlist sg[PATA_ICSIDE_MAX_SG];
};

struct pata_icside_info {
	struct pata_icside_state *state;
	struct expansion_card	*ec;
	void __iomem		*base;
	void __iomem		*irqaddr;
	unsigned int		irqmask;
	const expansioncard_ops_t *irqops;
	unsigned int		mwdma_mask;
	unsigned int		nr_ports;
	const struct portinfo	*port[2];
	unsigned long		raw_base;
	unsigned long		raw_ioc_base;
};

#define ICS_TYPE_A3IN	0
#define ICS_TYPE_A3USER	1
#define ICS_TYPE_V6	3
#define ICS_TYPE_V5	15
#define ICS_TYPE_NOTYPE	((unsigned int)-1)

/* ---------------- Version 5 PCB Support Functions --------------------- */
/* Prototype: pata_icside_irqenable_arcin_v5 (struct expansion_card *ec, int irqnr)
 * Purpose  : enable interrupts from card
 */
static void pata_icside_irqenable_arcin_v5 (struct expansion_card *ec, int irqnr)
{
	struct pata_icside_state *state = ec->irq_data;

	writeb(0, state->irq_port + ICS_ARCIN_V5_INTROFFSET);
}

/* Prototype: pata_icside_irqdisable_arcin_v5 (struct expansion_card *ec, int irqnr)
 * Purpose  : disable interrupts from card
 */
static void pata_icside_irqdisable_arcin_v5 (struct expansion_card *ec, int irqnr)
{
	struct pata_icside_state *state = ec->irq_data;

	readb(state->irq_port + ICS_ARCIN_V5_INTROFFSET);
}

static const expansioncard_ops_t pata_icside_ops_arcin_v5 = {
	.irqenable	= pata_icside_irqenable_arcin_v5,
	.irqdisable	= pata_icside_irqdisable_arcin_v5,
};


/* ---------------- Version 6 PCB Support Functions --------------------- */
/* Prototype: pata_icside_irqenable_arcin_v6 (struct expansion_card *ec, int irqnr)
 * Purpose  : enable interrupts from card
 */
static void pata_icside_irqenable_arcin_v6 (struct expansion_card *ec, int irqnr)
{
	struct pata_icside_state *state = ec->irq_data;
	void __iomem *base = state->irq_port;

	if (!state->port[0].disabled)
		writeb(0, base + ICS_ARCIN_V6_INTROFFSET_1);
	if (!state->port[1].disabled)
		writeb(0, base + ICS_ARCIN_V6_INTROFFSET_2);
}

/* Prototype: pata_icside_irqdisable_arcin_v6 (struct expansion_card *ec, int irqnr)
 * Purpose  : disable interrupts from card
 */
static void pata_icside_irqdisable_arcin_v6 (struct expansion_card *ec, int irqnr)
{
	struct pata_icside_state *state = ec->irq_data;

	readb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
	readb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
}

/* Prototype: pata_icside_irqprobe(struct expansion_card *ec)
 * Purpose  : detect an active interrupt from card
 */
static int pata_icside_irqpending_arcin_v6(struct expansion_card *ec)
{
	struct pata_icside_state *state = ec->irq_data;

	return readb(state->irq_port + ICS_ARCIN_V6_INTRSTAT_1) & 1 ||
	       readb(state->irq_port + ICS_ARCIN_V6_INTRSTAT_2) & 1;
}

static const expansioncard_ops_t pata_icside_ops_arcin_v6 = {
	.irqenable	= pata_icside_irqenable_arcin_v6,
	.irqdisable	= pata_icside_irqdisable_arcin_v6,
	.irqpending	= pata_icside_irqpending_arcin_v6,
};


/*
 * SG-DMA support.
 *
 * Similar to the BM-DMA, but we use the RiscPCs IOMD DMA controllers.
 * There is only one DMA controller per card, which means that only
 * one drive can be accessed at one time.  NOTE! We do not enforce that
 * here, but we rely on the main IDE driver spotting that both
 * interfaces use the same IRQ, which should guarantee this.
 */

/*
 * Configure the IOMD to give the appropriate timings for the transfer
 * mode being requested.  We take the advice of the ATA standards, and
 * calculate the cycle time based on the transfer mode, and the EIDE
 * MW DMA specs that the drive provides in the IDENTIFY command.
 *
 * We have the following IOMD DMA modes to choose from:
 *
 *	Type	Active		Recovery	Cycle
 *	A	250 (250)	312 (550)	562 (800)
 *	B	187 (200)	250 (550)	437 (750)
 *	C	125 (125)	125 (375)	250 (500)
 *	D	62  (50)	125 (375)	187 (425)
 *
 * (figures in brackets are actual measured timings on DIOR/DIOW)
 *
 * However, we also need to take care of the read/write active and
 * recovery timings:
 *
 *			Read	Write
 *  	Mode	Active	-- Recovery --	Cycle	IOMD type
 *	MW0	215	50	215	480	A
 *	MW1	80	50	50	150	C
 *	MW2	70	25	25	120	C
 */
static void pata_icside_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pata_icside_state *state = ap->host->private_data;
	struct ata_timing t;
	unsigned int cycle;
	char iomd_type;

	/*
	 * DMA is based on a 16MHz clock
	 */
	if (ata_timing_compute(adev, adev->dma_mode, &t, 1000, 1))
		return;

	/*
	 * Choose the IOMD cycle timing which ensure that the interface
	 * satisfies the measured active, recovery and cycle times.
	 */
	if (t.active <= 50 && t.recover <= 375 && t.cycle <= 425)
		iomd_type = 'D', cycle = 187;
	else if (t.active <= 125 && t.recover <= 375 && t.cycle <= 500)
		iomd_type = 'C', cycle = 250;
	else if (t.active <= 200 && t.recover <= 550 && t.cycle <= 750)
		iomd_type = 'B', cycle = 437;
	else
		iomd_type = 'A', cycle = 562;

	ata_dev_printk(adev, KERN_INFO, "timings: act %dns rec %dns cyc %dns (%c)\n",
		t.active, t.recover, t.cycle, iomd_type);

	state->port[ap->port_no].speed[adev->devno] = cycle;
}

static void pata_icside_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pata_icside_state *state = ap->host->private_data;
	struct scatterlist *sg, *rsg = state->sg;
	unsigned int write = qc->tf.flags & ATA_TFLAG_WRITE;
	unsigned int si;

	/*
	 * We are simplex; BUG if we try to fiddle with DMA
	 * while it's active.
	 */
	BUG_ON(dma_channel_active(state->dma));

	/*
	 * Copy ATAs scattered sg list into a contiguous array of sg
	 */
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		memcpy(rsg, sg, sizeof(*sg));
		rsg++;
	}

	/*
	 * Route the DMA signals to the correct interface
	 */
	writeb(state->port[ap->port_no].port_sel, state->ioc_base);

	set_dma_speed(state->dma, state->port[ap->port_no].speed[qc->dev->devno]);
	set_dma_sg(state->dma, state->sg, rsg - state->sg);
	set_dma_mode(state->dma, write ? DMA_MODE_WRITE : DMA_MODE_READ);

	/* issue r/w command */
	ap->ops->sff_exec_command(ap, &qc->tf);
}

static void pata_icside_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pata_icside_state *state = ap->host->private_data;

	BUG_ON(dma_channel_active(state->dma));
	enable_dma(state->dma);
}

static void pata_icside_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pata_icside_state *state = ap->host->private_data;

	disable_dma(state->dma);

	/* see ata_bmdma_stop */
	ata_sff_dma_pause(ap);
}

static u8 pata_icside_bmdma_status(struct ata_port *ap)
{
	struct pata_icside_state *state = ap->host->private_data;
	void __iomem *irq_port;

	irq_port = state->irq_port + (ap->port_no ? ICS_ARCIN_V6_INTRSTAT_2 :
						    ICS_ARCIN_V6_INTRSTAT_1);

	return readb(irq_port) & 1 ? ATA_DMA_INTR : 0;
}

static int icside_dma_init(struct pata_icside_info *info)
{
	struct pata_icside_state *state = info->state;
	struct expansion_card *ec = info->ec;
	int i;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		state->port[0].speed[i] = 480;
		state->port[1].speed[i] = 480;
	}

	if (ec->dma != NO_DMA && !request_dma(ec->dma, DRV_NAME)) {
		state->dma = ec->dma;
		info->mwdma_mask = 0x07;	/* MW0..2 */
	}

	return 0;
}


static struct scsi_host_template pata_icside_sht = {
	ATA_BASE_SHT(DRV_NAME),
	.sg_tablesize		= PATA_ICSIDE_MAX_SG,
	.dma_boundary		= ~0, /* no dma boundaries */
};

static void pata_icside_postreset(struct ata_link *link, unsigned int *classes)
{
	struct ata_port *ap = link->ap;
	struct pata_icside_state *state = ap->host->private_data;

	if (classes[0] != ATA_DEV_NONE || classes[1] != ATA_DEV_NONE)
		return ata_sff_postreset(link, classes);

	state->port[ap->port_no].disabled = 1;

	if (state->type == ICS_TYPE_V6) {
		/*
		 * Disable interrupts from this port, otherwise we
		 * receive spurious interrupts from the floating
		 * interrupt line.
		 */
		void __iomem *irq_port = state->irq_port +
				(ap->port_no ? ICS_ARCIN_V6_INTROFFSET_2 : ICS_ARCIN_V6_INTROFFSET_1);
		readb(irq_port);
	}
}

static struct ata_port_operations pata_icside_port_ops = {
	.inherits		= &ata_sff_port_ops,
	/* no need to build any PRD tables for DMA */
	.qc_prep		= ata_noop_qc_prep,
	.sff_data_xfer		= ata_sff_data_xfer_noirq,
	.bmdma_setup		= pata_icside_bmdma_setup,
	.bmdma_start		= pata_icside_bmdma_start,
	.bmdma_stop		= pata_icside_bmdma_stop,
	.bmdma_status		= pata_icside_bmdma_status,

	.cable_detect		= ata_cable_40wire,
	.set_dmamode		= pata_icside_set_dmamode,
	.postreset		= pata_icside_postreset,
	.post_internal_cmd	= pata_icside_bmdma_stop,
};

static void __devinit
pata_icside_setup_ioaddr(struct ata_port *ap, void __iomem *base,
			 struct pata_icside_info *info,
			 const struct portinfo *port)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	void __iomem *cmd = base + port->dataoffset;

	ioaddr->cmd_addr	= cmd;
	ioaddr->data_addr	= cmd + (ATA_REG_DATA    << port->stepping);
	ioaddr->error_addr	= cmd + (ATA_REG_ERR     << port->stepping);
	ioaddr->feature_addr	= cmd + (ATA_REG_FEATURE << port->stepping);
	ioaddr->nsect_addr	= cmd + (ATA_REG_NSECT   << port->stepping);
	ioaddr->lbal_addr	= cmd + (ATA_REG_LBAL    << port->stepping);
	ioaddr->lbam_addr	= cmd + (ATA_REG_LBAM    << port->stepping);
	ioaddr->lbah_addr	= cmd + (ATA_REG_LBAH    << port->stepping);
	ioaddr->device_addr	= cmd + (ATA_REG_DEVICE  << port->stepping);
	ioaddr->status_addr	= cmd + (ATA_REG_STATUS  << port->stepping);
	ioaddr->command_addr	= cmd + (ATA_REG_CMD     << port->stepping);

	ioaddr->ctl_addr	= base + port->ctrloffset;
	ioaddr->altstatus_addr	= ioaddr->ctl_addr;

	ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx",
		      info->raw_base + port->dataoffset,
		      info->raw_base + port->ctrloffset);

	if (info->raw_ioc_base)
		ata_port_desc(ap, "iocbase 0x%lx", info->raw_ioc_base);
}

static int __devinit pata_icside_register_v5(struct pata_icside_info *info)
{
	struct pata_icside_state *state = info->state;
	void __iomem *base;

	base = ecardm_iomap(info->ec, ECARD_RES_MEMC, 0, 0);
	if (!base)
		return -ENOMEM;

	state->irq_port = base;

	info->base = base;
	info->irqaddr = base + ICS_ARCIN_V5_INTRSTAT;
	info->irqmask = 1;
	info->irqops = &pata_icside_ops_arcin_v5;
	info->nr_ports = 1;
	info->port[0] = &pata_icside_portinfo_v5;

	info->raw_base = ecard_resource_start(info->ec, ECARD_RES_MEMC);

	return 0;
}

static int __devinit pata_icside_register_v6(struct pata_icside_info *info)
{
	struct pata_icside_state *state = info->state;
	struct expansion_card *ec = info->ec;
	void __iomem *ioc_base, *easi_base;
	unsigned int sel = 0;

	ioc_base = ecardm_iomap(ec, ECARD_RES_IOCFAST, 0, 0);
	if (!ioc_base)
		return -ENOMEM;

	easi_base = ioc_base;

	if (ecard_resource_flags(ec, ECARD_RES_EASI)) {
		easi_base = ecardm_iomap(ec, ECARD_RES_EASI, 0, 0);
		if (!easi_base)
			return -ENOMEM;

		/*
		 * Enable access to the EASI region.
		 */
		sel = 1 << 5;
	}

	writeb(sel, ioc_base);

	state->irq_port = easi_base;
	state->ioc_base = ioc_base;
	state->port[0].port_sel = sel;
	state->port[1].port_sel = sel | 1;

	info->base = easi_base;
	info->irqops = &pata_icside_ops_arcin_v6;
	info->nr_ports = 2;
	info->port[0] = &pata_icside_portinfo_v6_1;
	info->port[1] = &pata_icside_portinfo_v6_2;

	info->raw_base = ecard_resource_start(ec, ECARD_RES_EASI);
	info->raw_ioc_base = ecard_resource_start(ec, ECARD_RES_IOCFAST);

	return icside_dma_init(info);
}

static int __devinit pata_icside_add_ports(struct pata_icside_info *info)
{
	struct expansion_card *ec = info->ec;
	struct ata_host *host;
	int i;

	if (info->irqaddr) {
		ec->irqaddr = info->irqaddr;
		ec->irqmask = info->irqmask;
	}
	if (info->irqops)
		ecard_setirq(ec, info->irqops, info->state);

	/*
	 * Be on the safe side - disable interrupts
	 */
	ec->ops->irqdisable(ec, ec->irq);

	host = ata_host_alloc(&ec->dev, info->nr_ports);
	if (!host)
		return -ENOMEM;

	host->private_data = info->state;
	host->flags = ATA_HOST_SIMPLEX;

	for (i = 0; i < info->nr_ports; i++) {
		struct ata_port *ap = host->ports[i];

		ap->pio_mask = 0x1f;
		ap->mwdma_mask = info->mwdma_mask;
		ap->flags |= ATA_FLAG_SLAVE_POSS;
		ap->ops = &pata_icside_port_ops;

		pata_icside_setup_ioaddr(ap, info->base, info, info->port[i]);
	}

	return ata_host_activate(host, ec->irq, ata_sff_interrupt, 0,
				 &pata_icside_sht);
}

static int __devinit
pata_icside_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct pata_icside_state *state;
	struct pata_icside_info info;
	void __iomem *idmem;
	int ret;

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	state = devm_kzalloc(&ec->dev, sizeof(*state), GFP_KERNEL);
	if (!state) {
		ret = -ENOMEM;
		goto release;
	}

	state->type = ICS_TYPE_NOTYPE;
	state->dma = NO_DMA;

	idmem = ecardm_iomap(ec, ECARD_RES_IOCFAST, 0, 0);
	if (idmem) {
		unsigned int type;

		type = readb(idmem + ICS_IDENT_OFFSET) & 1;
		type |= (readb(idmem + ICS_IDENT_OFFSET + 4) & 1) << 1;
		type |= (readb(idmem + ICS_IDENT_OFFSET + 8) & 1) << 2;
		type |= (readb(idmem + ICS_IDENT_OFFSET + 12) & 1) << 3;
		ecardm_iounmap(ec, idmem);

		state->type = type;
	}

	memset(&info, 0, sizeof(info));
	info.state = state;
	info.ec = ec;

	switch (state->type) {
	case ICS_TYPE_A3IN:
		dev_warn(&ec->dev, "A3IN unsupported\n");
		ret = -ENODEV;
		break;

	case ICS_TYPE_A3USER:
		dev_warn(&ec->dev, "A3USER unsupported\n");
		ret = -ENODEV;
		break;

	case ICS_TYPE_V5:
		ret = pata_icside_register_v5(&info);
		break;

	case ICS_TYPE_V6:
		ret = pata_icside_register_v6(&info);
		break;

	default:
		dev_warn(&ec->dev, "unknown interface type\n");
		ret = -ENODEV;
		break;
	}

	if (ret == 0)
		ret = pata_icside_add_ports(&info);

	if (ret == 0)
		goto out;

 release:
	ecard_release_resources(ec);
 out:
	return ret;
}

static void pata_icside_shutdown(struct expansion_card *ec)
{
	struct ata_host *host = ecard_get_drvdata(ec);
	unsigned long flags;

	/*
	 * Disable interrupts from this card.  We need to do
	 * this before disabling EASI since we may be accessing
	 * this register via that region.
	 */
	local_irq_save(flags);
	ec->ops->irqdisable(ec, ec->irq);
	local_irq_restore(flags);

	/*
	 * Reset the ROM pointer so that we can read the ROM
	 * after a soft reboot.  This also disables access to
	 * the IDE taskfile via the EASI region.
	 */
	if (host) {
		struct pata_icside_state *state = host->private_data;
		if (state->ioc_base)
			writeb(0, state->ioc_base);
	}
}

static void __devexit pata_icside_remove(struct expansion_card *ec)
{
	struct ata_host *host = ecard_get_drvdata(ec);
	struct pata_icside_state *state = host->private_data;

	ata_host_detach(host);

	pata_icside_shutdown(ec);

	/*
	 * don't NULL out the drvdata - devres/libata wants it
	 * to free the ata_host structure.
	 */
	if (state->dma != NO_DMA)
		free_dma(state->dma);

	ecard_release_resources(ec);
}

static const struct ecard_id pata_icside_ids[] = {
	{ MANU_ICS,  PROD_ICS_IDE  },
	{ MANU_ICS2, PROD_ICS2_IDE },
	{ 0xffff, 0xffff }
};

static struct ecard_driver pata_icside_driver = {
	.probe		= pata_icside_probe,
	.remove 	= __devexit_p(pata_icside_remove),
	.shutdown	= pata_icside_shutdown,
	.id_table	= pata_icside_ids,
	.drv = {
		.name	= DRV_NAME,
	},
};

static int __init pata_icside_init(void)
{
	return ecard_register_driver(&pata_icside_driver);
}

static void __exit pata_icside_exit(void)
{
	ecard_remove_driver(&pata_icside_driver);
}

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ICS PATA driver");

module_init(pata_icside_init);
module_exit(pata_icside_exit);
