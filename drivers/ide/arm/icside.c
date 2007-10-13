/*
 * linux/drivers/ide/arm/icside.c
 *
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
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/scatterlist.h>
#include <linux/io.h>

#include <asm/dma.h>
#include <asm/ecard.h>

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
	unsigned int type;
	/* parent device... until the IDE core gets one of its own */
	struct device *dev;
	ide_hwif_t *hwif[2];
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
	ide_hwif_t *hwif = HWIF(drive);
	struct icside_state *state = hwif->hwif_data;
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

static void icside_build_sglist(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = drive->hwif;
	struct icside_state *state = hwif->hwif_data;
	struct scatterlist *sg = hwif->sg_table;

	ide_map_sg(drive, rq);

	if (rq_data_dir(rq) == READ)
		hwif->sg_dma_direction = DMA_FROM_DEVICE;
	else
		hwif->sg_dma_direction = DMA_TO_DEVICE;

	hwif->sg_nents = dma_map_sg(state->dev, sg, hwif->sg_nents,
				    hwif->sg_dma_direction);
}

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
static void icside_set_dma_mode(ide_drive_t *drive, const u8 xfer_mode)
{
	int cycle_time, use_dma_info = 0;

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
	default:
		return;
	}

	/*
	 * If we're going to be doing MW_DMA_1 or MW_DMA_2, we should
	 * take care to note the values in the ID...
	 */
	if (use_dma_info && drive->id->eide_dma_time > cycle_time)
		cycle_time = drive->id->eide_dma_time;

	drive->drive_data = cycle_time;

	printk("%s: %s selected (peak %dMB/s)\n", drive->name,
		ide_xfer_verbose(xfer_mode), 2000 / drive->drive_data);
}

static void icside_dma_host_off(ide_drive_t *drive)
{
}

static void icside_dma_off_quietly(ide_drive_t *drive)
{
	drive->using_dma = 0;
}

static void icside_dma_host_on(ide_drive_t *drive)
{
}

static int icside_dma_on(ide_drive_t *drive)
{
	drive->using_dma = 1;

	return 0;
}

static int icside_dma_check(ide_drive_t *drive)
{
	if (ide_tune_dma(drive))
		return 0;

	return -1;
}

static int icside_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct icside_state *state = hwif->hwif_data;

	drive->waiting_for_dma = 0;

	disable_dma(hwif->hw.dma);

	/* Teardown mappings after DMA has completed. */
	dma_unmap_sg(state->dev, hwif->sg_table, hwif->sg_nents,
		     hwif->sg_dma_direction);

	return get_dma_residue(hwif->hw.dma) != 0;
}

static void icside_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	/* We can not enable DMA on both channels simultaneously. */
	BUG_ON(dma_channel_active(hwif->hw.dma));
	enable_dma(hwif->hw.dma);
}

static int icside_dma_setup(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct request *rq = hwif->hwgroup->rq;
	unsigned int dma_mode;

	if (rq_data_dir(rq))
		dma_mode = DMA_MODE_WRITE;
	else
		dma_mode = DMA_MODE_READ;

	/*
	 * We can not enable DMA on both channels.
	 */
	BUG_ON(dma_channel_active(hwif->hw.dma));

	icside_build_sglist(drive, rq);

	/*
	 * Ensure that we have the right interrupt routed.
	 */
	icside_maskproc(drive, 0);

	/*
	 * Route the DMA signals to the correct interface.
	 */
	writeb(hwif->select_data, hwif->config_data);

	/*
	 * Select the correct timing for this drive.
	 */
	set_dma_speed(hwif->hw.dma, drive->drive_data);

	/*
	 * Tell the DMA engine about the SG table and
	 * data direction.
	 */
	set_dma_sg(hwif->hw.dma, hwif->sg_table, hwif->sg_nents);
	set_dma_mode(hwif->hw.dma, dma_mode);

	drive->waiting_for_dma = 1;

	return 0;
}

static void icside_dma_exec_cmd(ide_drive_t *drive, u8 cmd)
{
	/* issue cmd to drive */
	ide_execute_command(drive, cmd, ide_dma_intr, 2 * WAIT_CMD, NULL);
}

static int icside_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct icside_state *state = hwif->hwif_data;

	return readb(state->irq_port +
		     (hwif->channel ?
			ICS_ARCIN_V6_INTRSTAT_2 :
			ICS_ARCIN_V6_INTRSTAT_1)) & 1;
}

static void icside_dma_timeout(ide_drive_t *drive)
{
	printk(KERN_ERR "%s: DMA timeout occurred: ", drive->name);

	if (icside_dma_test_irq(drive))
		return;

	ide_dump_status(drive, "DMA timeout", HWIF(drive)->INB(IDE_STATUS_REG));

	icside_dma_end(drive);
}

static void icside_dma_lost_irq(ide_drive_t *drive)
{
	printk(KERN_ERR "%s: IRQ lost\n", drive->name);
}

static void icside_dma_init(ide_hwif_t *hwif)
{
	printk("    %s: SG-DMA", hwif->name);

	hwif->atapi_dma		= 1;
	hwif->mwdma_mask	= 7; /* MW0..2 */
	hwif->swdma_mask	= 7; /* SW0..2 */

	hwif->dmatable_cpu	= NULL;
	hwif->dmatable_dma	= 0;
	hwif->set_dma_mode	= icside_set_dma_mode;
	hwif->autodma		= 1;

	hwif->ide_dma_check	= icside_dma_check;
	hwif->dma_host_off	= icside_dma_host_off;
	hwif->dma_off_quietly	= icside_dma_off_quietly;
	hwif->dma_host_on	= icside_dma_host_on;
	hwif->ide_dma_on	= icside_dma_on;
	hwif->dma_setup		= icside_dma_setup;
	hwif->dma_exec_cmd	= icside_dma_exec_cmd;
	hwif->dma_start		= icside_dma_start;
	hwif->ide_dma_end	= icside_dma_end;
	hwif->ide_dma_test_irq	= icside_dma_test_irq;
	hwif->dma_timeout	= icside_dma_timeout;
	hwif->dma_lost_irq	= icside_dma_lost_irq;

	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;

	printk(" capable%s\n", hwif->autodma ? ", auto-enable" : "");
}
#else
#define icside_dma_init(hwif)	(0)
#endif

static ide_hwif_t *icside_find_hwif(unsigned long dataport)
{
	ide_hwif_t *hwif;
	int index;

	for (index = 0; index < MAX_HWIFS; ++index) {
		hwif = &ide_hwifs[index];
		if (hwif->io_ports[IDE_DATA_OFFSET] == dataport)
			goto found;
	}

	for (index = 0; index < MAX_HWIFS; ++index) {
		hwif = &ide_hwifs[index];
		if (!hwif->io_ports[IDE_DATA_OFFSET])
			goto found;
	}

	hwif = NULL;
found:
	return hwif;
}

static ide_hwif_t *
icside_setup(void __iomem *base, struct cardinfo *info, struct expansion_card *ec)
{
	unsigned long port = (unsigned long)base + info->dataoffset;
	ide_hwif_t *hwif;

	hwif = icside_find_hwif(port);
	if (hwif) {
		int i;

		memset(&hwif->hw, 0, sizeof(hw_regs_t));

		/*
		 * Ensure we're using MMIO
		 */
		default_hwif_mmiops(hwif);
		hwif->mmio = 1;

		for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
			hwif->hw.io_ports[i] = port;
			hwif->io_ports[i] = port;
			port += 1 << info->stepping;
		}
		hwif->hw.io_ports[IDE_CONTROL_OFFSET] = (unsigned long)base + info->ctrloffset;
		hwif->io_ports[IDE_CONTROL_OFFSET] = (unsigned long)base + info->ctrloffset;
		hwif->hw.irq  = ec->irq;
		hwif->irq     = ec->irq;
		hwif->noprobe = 0;
		hwif->chipset = ide_acorn;
		hwif->gendev.parent = &ec->dev;
	}

	return hwif;
}

static int __init
icside_register_v5(struct icside_state *state, struct expansion_card *ec)
{
	ide_hwif_t *hwif;
	void __iomem *base;

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

	hwif = icside_setup(base, &icside_cardinfo_v5, ec);
	if (!hwif)
		return -ENODEV;

	state->hwif[0] = hwif;

	probe_hwif_init(hwif);

	ide_proc_register_port(hwif);

	return 0;
}

static int __init
icside_register_v6(struct icside_state *state, struct expansion_card *ec)
{
	ide_hwif_t *hwif, *mate;
	void __iomem *ioc_base, *easi_base;
	unsigned int sel = 0;
	int ret;

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

	/*
	 * Be on the safe side - disable interrupts
	 */
	icside_irqdisable_arcin_v6(ec, 0);

	/*
	 * Find and register the interfaces.
	 */
	hwif = icside_setup(easi_base, &icside_cardinfo_v6_1, ec);
	mate = icside_setup(easi_base, &icside_cardinfo_v6_2, ec);

	if (!hwif || !mate) {
		ret = -ENODEV;
		goto out;
	}

	state->hwif[0]    = hwif;
	state->hwif[1]    = mate;

	hwif->maskproc    = icside_maskproc;
	hwif->channel     = 0;
	hwif->hwif_data   = state;
	hwif->mate        = mate;
	hwif->serialized  = 1;
	hwif->config_data = (unsigned long)ioc_base;
	hwif->select_data = sel;
	hwif->hw.dma      = ec->dma;

	mate->maskproc    = icside_maskproc;
	mate->channel     = 1;
	mate->hwif_data   = state;
	mate->mate        = hwif;
	mate->serialized  = 1;
	mate->config_data = (unsigned long)ioc_base;
	mate->select_data = sel | 1;
	mate->hw.dma      = ec->dma;

	if (ec->dma != NO_DMA && !request_dma(ec->dma, hwif->name)) {
		icside_dma_init(hwif);
		icside_dma_init(mate);
	}

	probe_hwif_init(hwif);
	probe_hwif_init(mate);

	ide_proc_register_port(hwif);
	ide_proc_register_port(mate);

	return 0;

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
	state->dev	= &ec->dev;

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

	if (ret == 0) {
		ecard_set_drvdata(ec, state);
		goto out;
	}

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

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ICS IDE driver");

module_init(icside_init);
