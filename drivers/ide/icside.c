/*
 * Copyright (c) 1996-2004 Russell King.
 *
 * Please note that this platform does not support 32-bit IDE IO.
 */

#include <linux/string.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/ide.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/scatterlist.h>
#include <linux/io.h>

#include <asm/dma.h>
#include <asm/ecard.h>

#define DRV_NAME "icside"

#define ICS_IDENT_OFFSET		0x2280

#define ICS_ARCIN_V5_INTRSTAT		0x0000
#define ICS_ARCIN_V5_INTROFFSET		0x0004
#define ICS_ARCIN_V5_IDEOFFSET		0x2800
#define ICS_ARCIN_V5_IDEALTOFFSET	0x2b80
#define ICS_ARCIN_V5_IDESTEPPING	6

#define ICS_ARCIN_V6_IDEOFFSET_1	0x2000
#define ICS_ARCIN_V6_INTROFFSET_1	0x2200
#define ICS_ARCIN_V6_INTRSTAT_1		0x2290
#define ICS_ARCIN_V6_IDEALTOFFSET_1	0x2380
#define ICS_ARCIN_V6_IDEOFFSET_2	0x3000
#define ICS_ARCIN_V6_INTROFFSET_2	0x3200
#define ICS_ARCIN_V6_INTRSTAT_2		0x3290
#define ICS_ARCIN_V6_IDEALTOFFSET_2	0x3380
#define ICS_ARCIN_V6_IDESTEPPING	6

struct cardinfo {
	unsigned int dataoffset;
	unsigned int ctrloffset;
	unsigned int stepping;
};

static struct cardinfo icside_cardinfo_v5 = {
	.dataoffset	= ICS_ARCIN_V5_IDEOFFSET,
	.ctrloffset	= ICS_ARCIN_V5_IDEALTOFFSET,
	.stepping	= ICS_ARCIN_V5_IDESTEPPING,
};

static struct cardinfo icside_cardinfo_v6_1 = {
	.dataoffset	= ICS_ARCIN_V6_IDEOFFSET_1,
	.ctrloffset	= ICS_ARCIN_V6_IDEALTOFFSET_1,
	.stepping	= ICS_ARCIN_V6_IDESTEPPING,
};

static struct cardinfo icside_cardinfo_v6_2 = {
	.dataoffset	= ICS_ARCIN_V6_IDEOFFSET_2,
	.ctrloffset	= ICS_ARCIN_V6_IDEALTOFFSET_2,
	.stepping	= ICS_ARCIN_V6_IDESTEPPING,
};

struct icside_state {
	unsigned int channel;
	unsigned int enabled;
	void __iomem *irq_port;
	void __iomem *ioc_base;
	unsigned int sel;
	unsigned int type;
	struct ide_host *host;
};

#define ICS_TYPE_A3IN	0
#define ICS_TYPE_A3USER	1
#define ICS_TYPE_V6	3
#define ICS_TYPE_V5	15
#define ICS_TYPE_NOTYPE	((unsigned int)-1)

/* ---------------- Version 5 PCB Support Functions --------------------- */
/* Prototype: icside_irqenable_arcin_v5 (struct expansion_card *ec, int irqnr)
 * Purpose  : enable interrupts from card
 */
static void icside_irqenable_arcin_v5 (struct expansion_card *ec, int irqnr)
{
	struct icside_state *state = ec->irq_data;

	writeb(0, state->irq_port + ICS_ARCIN_V5_INTROFFSET);
}

/* Prototype: icside_irqdisable_arcin_v5 (struct expansion_card *ec, int irqnr)
 * Purpose  : disable interrupts from card
 */
static void icside_irqdisable_arcin_v5 (struct expansion_card *ec, int irqnr)
{
	struct icside_state *state = ec->irq_data;

	readb(state->irq_port + ICS_ARCIN_V5_INTROFFSET);
}

static const expansioncard_ops_t icside_ops_arcin_v5 = {
	.irqenable	= icside_irqenable_arcin_v5,
	.irqdisable	= icside_irqdisable_arcin_v5,
};


/* ---------------- Version 6 PCB Support Functions --------------------- */
/* Prototype: icside_irqenable_arcin_v6 (struct expansion_card *ec, int irqnr)
 * Purpose  : enable interrupts from card
 */
static void icside_irqenable_arcin_v6 (struct expansion_card *ec, int irqnr)
{
	struct icside_state *state = ec->irq_data;
	void __iomem *base = state->irq_port;

	state->enabled = 1;

	switch (state->channel) {
	case 0:
		writeb(0, base + ICS_ARCIN_V6_INTROFFSET_1);
		readb(base + ICS_ARCIN_V6_INTROFFSET_2);
		break;
	case 1:
		writeb(0, base + ICS_ARCIN_V6_INTROFFSET_2);
		readb(base + ICS_ARCIN_V6_INTROFFSET_1);
		break;
	}
}

/* Prototype: icside_irqdisable_arcin_v6 (struct expansion_card *ec, int irqnr)
 * Purpose  : disable interrupts from card
 */
static void icside_irqdisable_arcin_v6 (struct expansion_card *ec, int irqnr)
{
	struct icside_state *state = ec->irq_data;

	state->enabled = 0;

	readb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
	readb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
}

/* Prototype: icside_irqprobe(struct expansion_card *ec)
 * Purpose  : detect an active interrupt from card
 */
static int icside_irqpending_arcin_v6(struct expansion_card *ec)
{
	struct icside_state *state = ec->irq_data;

	return readb(state->irq_port + ICS_ARCIN_V6_INTRSTAT_1) & 1 ||
	       readb(state->irq_port + ICS_ARCIN_V6_INTRSTAT_2) & 1;
}

static const expansioncard_ops_t icside_ops_arcin_v6 = {
	.irqenable	= icside_irqenable_arcin_v6,
	.irqdisable	= icside_irqdisable_arcin_v6,
	.irqpending	= icside_irqpending_arcin_v6,
};

/*
 * Handle routing of interrupts.  This is called before
 * we write the command to the drive.
 */
static void icside_maskproc(ide_drive_t *drive, int mask)
{
	ide_hwif_t *hwif = drive->hwif;
	struct expansion_card *ec = ECARD_DEV(hwif->dev);
	struct icside_state *state = ecard_get_drvdata(ec);
	unsigned long flags;

	local_irq_save(flags);

	state->channel = hwif->channel;

	if (state->enabled && !mask) {
		switch (hwif->channel) {
		case 0:
			writeb(0, state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
			readb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
			break;
		case 1:
			writeb(0, state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
			readb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
			break;
		}
	} else {
		readb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_2);
		readb(state->irq_port + ICS_ARCIN_V6_INTROFFSET_1);
	}

	local_irq_restore(flags);
}

static const struct ide_port_ops icside_v6_no_dma_port_ops = {
	.maskproc		= icside_maskproc,
};

#ifdef CONFIG_BLK_DEV_IDEDMA_ICS
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
 *	B	187		250		437
 *	C	125 (125)	125 (375)	250 (500)
 *	D	62		125		187
 *
 * (figures in brackets are actual measured timings)
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
static void icside_set_dma_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	unsigned long cycle_time = 0;
	int use_dma_info = 0;
	const u8 xfer_mode = drive->dma_mode;

	switch (xfer_mode) {
	case XFER_MW_DMA_2:
		cycle_time = 250;
		use_dma_info = 1;
		break;

	case XFER_MW_DMA_1:
		cycle_time = 250;
		use_dma_info = 1;
		break;

	case XFER_MW_DMA_0:
		cycle_time = 480;
		break;

	case XFER_SW_DMA_2:
	case XFER_SW_DMA_1:
	case XFER_SW_DMA_0:
		cycle_time = 480;
		break;
	}

	/*
	 * If we're going to be doing MW_DMA_1 or MW_DMA_2, we should
	 * take care to note the values in the ID...
	 */
	if (use_dma_info && drive->id[ATA_ID_EIDE_DMA_TIME] > cycle_time)
		cycle_time = drive->id[ATA_ID_EIDE_DMA_TIME];

	ide_set_drivedata(drive, (void *)cycle_time);

	printk(KERN_INFO "%s: %s selected (peak %luMB/s)\n",
	       drive->name, ide_xfer_verbose(xfer_mode),
	       2000 / (cycle_time ? cycle_time : (unsigned long) -1));
}

static const struct ide_port_ops icside_v6_port_ops = {
	.set_dma_mode		= icside_set_dma_mode,
	.maskproc		= icside_maskproc,
};

static void icside_dma_host_set(ide_drive_t *drive, int on)
{
}

static int icside_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct expansion_card *ec = ECARD_DEV(hwif->dev);

	disable_dma(ec->dma);

	return get_dma_residue(ec->dma) != 0;
}

static void icside_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct expansion_card *ec = ECARD_DEV(hwif->dev);

	/* We can not enable DMA on both channels simultaneously. */
	BUG_ON(dma_channel_active(ec->dma));
	enable_dma(ec->dma);
}

static int icside_dma_setup(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct expansion_card *ec = ECARD_DEV(hwif->dev);
	struct icside_state *state = ecard_get_drvdata(ec);
	unsigned int dma_mode;

	if (cmd->tf_flags & IDE_TFLAG_WRITE)
		dma_mode = DMA_MODE_WRITE;
	else
		dma_mode = DMA_MODE_READ;

	/*
	 * We can not enable DMA on both channels.
	 */
	BUG_ON(dma_channel_active(ec->dma));

	/*
	 * Ensure that we have the right interrupt routed.
	 */
	icside_maskproc(drive, 0);

	/*
	 * Route the DMA signals to the correct interface.
	 */
	writeb(state->sel | hwif->channel, state->ioc_base);

	/*
	 * Select the correct timing for this drive.
	 */
	set_dma_speed(ec->dma, (unsigned long)ide_get_drivedata(drive));

	/*
	 * Tell the DMA engine about the SG table and
	 * data direction.
	 */
	set_dma_sg(ec->dma, hwif->sg_table, cmd->sg_nents);
	set_dma_mode(ec->dma, dma_mode);

	return 0;
}

static int icside_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct expansion_card *ec = ECARD_DEV(hwif->dev);
	struct icside_state *state = ecard_get_drvdata(ec);

	return readb(state->irq_port +
		     (hwif->channel ?
			ICS_ARCIN_V6_INTRSTAT_2 :
			ICS_ARCIN_V6_INTRSTAT_1)) & 1;
}

static int icside_dma_init(ide_hwif_t *hwif, const struct ide_port_info *d)
{
	hwif->dmatable_cpu	= NULL;
	hwif->dmatable_dma	= 0;

	return 0;
}

static const struct ide_dma_ops icside_v6_dma_ops = {
	.dma_host_set		= icside_dma_host_set,
	.dma_setup		= icside_dma_setup,
	.dma_start		= icside_dma_start,
	.dma_end		= icside_dma_end,
	.dma_test_irq		= icside_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
};
#endif

static int icside_dma_off_init(ide_hwif_t *hwif, const struct ide_port_info *d)
{
	return -EOPNOTSUPP;
}

static void icside_setup_ports(struct ide_hw *hw, void __iomem *base,
			       struct cardinfo *info, struct expansion_card *ec)
{
	unsigned long port = (unsigned long)base + info->dataoffset;

	hw->io_ports.data_addr	 = port;
	hw->io_ports.error_addr	 = port + (1 << info->stepping);
	hw->io_ports.nsect_addr	 = port + (2 << info->stepping);
	hw->io_ports.lbal_addr	 = port + (3 << info->stepping);
	hw->io_ports.lbam_addr	 = port + (4 << info->stepping);
	hw->io_ports.lbah_addr	 = port + (5 << info->stepping);
	hw->io_ports.device_addr = port + (6 << info->stepping);
	hw->io_ports.status_addr = port + (7 << info->stepping);
	hw->io_ports.ctl_addr	 = (unsigned long)base + info->ctrloffset;

	hw->irq = ec->irq;
	hw->dev = &ec->dev;
}

static const struct ide_port_info icside_v5_port_info = {
	.host_flags		= IDE_HFLAG_NO_DMA,
	.chipset		= ide_acorn,
};

static int __devinit
icside_register_v5(struct icside_state *state, struct expansion_card *ec)
{
	void __iomem *base;
	struct ide_host *host;
	struct ide_hw hw, *hws[] = { &hw };
	int ret;

	base = ecardm_iomap(ec, ECARD_RES_MEMC, 0, 0);
	if (!base)
		return -ENOMEM;

	state->irq_port = base;

	ec->irqaddr  = base + ICS_ARCIN_V5_INTRSTAT;
	ec->irqmask  = 1;

	ecard_setirq(ec, &icside_ops_arcin_v5, state);

	/*
	 * Be on the safe side - disable interrupts
	 */
	icside_irqdisable_arcin_v5(ec, 0);

	icside_setup_ports(&hw, base, &icside_cardinfo_v5, ec);

	host = ide_host_alloc(&icside_v5_port_info, hws, 1);
	if (host == NULL)
		return -ENODEV;

	state->host = host;

	ecard_set_drvdata(ec, state);

	ret = ide_host_register(host, &icside_v5_port_info, hws);
	if (ret)
		goto err_free;

	return 0;
err_free:
	ide_host_free(host);
	ecard_set_drvdata(ec, NULL);
	return ret;
}

static const struct ide_port_info icside_v6_port_info __initdata = {
	.init_dma		= icside_dma_off_init,
	.port_ops		= &icside_v6_no_dma_port_ops,
	.host_flags		= IDE_HFLAG_SERIALIZE | IDE_HFLAG_MMIO,
	.mwdma_mask		= ATA_MWDMA2,
	.swdma_mask		= ATA_SWDMA2,
	.chipset		= ide_acorn,
};

static int __devinit
icside_register_v6(struct icside_state *state, struct expansion_card *ec)
{
	void __iomem *ioc_base, *easi_base;
	struct ide_host *host;
	unsigned int sel = 0;
	int ret;
	struct ide_hw hw[2], *hws[] = { &hw[0], &hw[1] };
	struct ide_port_info d = icside_v6_port_info;

	ioc_base = ecardm_iomap(ec, ECARD_RES_IOCFAST, 0, 0);
	if (!ioc_base) {
		ret = -ENOMEM;
		goto out;
	}

	easi_base = ioc_base;

	if (ecard_resource_flags(ec, ECARD_RES_EASI)) {
		easi_base = ecardm_iomap(ec, ECARD_RES_EASI, 0, 0);
		if (!easi_base) {
			ret = -ENOMEM;
			goto out;
		}

		/*
		 * Enable access to the EASI region.
		 */
		sel = 1 << 5;
	}

	writeb(sel, ioc_base);

	ecard_setirq(ec, &icside_ops_arcin_v6, state);

	state->irq_port   = easi_base;
	state->ioc_base   = ioc_base;
	state->sel	  = sel;

	/*
	 * Be on the safe side - disable interrupts
	 */
	icside_irqdisable_arcin_v6(ec, 0);

	icside_setup_ports(&hw[0], easi_base, &icside_cardinfo_v6_1, ec);
	icside_setup_ports(&hw[1], easi_base, &icside_cardinfo_v6_2, ec);

	host = ide_host_alloc(&d, hws, 2);
	if (host == NULL)
		return -ENODEV;

	state->host = host;

	ecard_set_drvdata(ec, state);

#ifdef CONFIG_BLK_DEV_IDEDMA_ICS
	if (ec->dma != NO_DMA && !request_dma(ec->dma, DRV_NAME)) {
		d.init_dma = icside_dma_init;
		d.port_ops = &icside_v6_port_ops;
		d.dma_ops  = &icside_v6_dma_ops;
	}
#endif

	ret = ide_host_register(host, &d, hws);
	if (ret)
		goto err_free;

	return 0;
err_free:
	ide_host_free(host);
	if (d.dma_ops)
		free_dma(ec->dma);
	ecard_set_drvdata(ec, NULL);
out:
	return ret;
}

static int __devinit
icside_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct icside_state *state;
	void __iomem *idmem;
	int ret;

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	state = kzalloc(sizeof(struct icside_state), GFP_KERNEL);
	if (!state) {
		ret = -ENOMEM;
		goto release;
	}

	state->type	= ICS_TYPE_NOTYPE;

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
		ret = icside_register_v5(state, ec);
		break;

	case ICS_TYPE_V6:
		ret = icside_register_v6(state, ec);
		break;

	default:
		dev_warn(&ec->dev, "unknown interface type\n");
		ret = -ENODEV;
		break;
	}

	if (ret == 0)
		goto out;

	kfree(state);
 release:
	ecard_release_resources(ec);
 out:
	return ret;
}

static void __devexit icside_remove(struct expansion_card *ec)
{
	struct icside_state *state = ecard_get_drvdata(ec);

	switch (state->type) {
	case ICS_TYPE_V5:
		/* FIXME: tell IDE to stop using the interface */

		/* Disable interrupts */
		icside_irqdisable_arcin_v5(ec, 0);
		break;

	case ICS_TYPE_V6:
		/* FIXME: tell IDE to stop using the interface */
		if (ec->dma != NO_DMA)
			free_dma(ec->dma);

		/* Disable interrupts */
		icside_irqdisable_arcin_v6(ec, 0);

		/* Reset the ROM pointer/EASI selection */
		writeb(0, state->ioc_base);
		break;
	}

	ecard_set_drvdata(ec, NULL);

	kfree(state);
	ecard_release_resources(ec);
}

static void icside_shutdown(struct expansion_card *ec)
{
	struct icside_state *state = ecard_get_drvdata(ec);
	unsigned long flags;

	/*
	 * Disable interrupts from this card.  We need to do
	 * this before disabling EASI since we may be accessing
	 * this register via that region.
	 */
	local_irq_save(flags);
	ec->ops->irqdisable(ec, 0);
	local_irq_restore(flags);

	/*
	 * Reset the ROM pointer so that we can read the ROM
	 * after a soft reboot.  This also disables access to
	 * the IDE taskfile via the EASI region.
	 */
	if (state->ioc_base)
		writeb(0, state->ioc_base);
}

static const struct ecard_id icside_ids[] = {
	{ MANU_ICS,  PROD_ICS_IDE  },
	{ MANU_ICS2, PROD_ICS2_IDE },
	{ 0xffff, 0xffff }
};

static struct ecard_driver icside_driver = {
	.probe		= icside_probe,
	.remove		= __devexit_p(icside_remove),
	.shutdown	= icside_shutdown,
	.id_table	= icside_ids,
	.drv = {
		.name	= "icside",
	},
};

static int __init icside_init(void)
{
	return ecard_register_driver(&icside_driver);
}

static void __exit icside_exit(void)
{
	ecard_remove_driver(&icside_driver);
}

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ICS IDE driver");

module_init(icside_init);
module_exit(icside_exit);
