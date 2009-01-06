/*
 *  Copyright (C) 1998-1999 Andrzej Krzysztofowicz, Author and Maintainer
 *  Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003 Red Hat
 *  Copyright (C) 2006-2007 MontaVista Software, Inc. <source@mvista.com>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 * Documentation:
 *
 *	Publically available from Intel web site. Errata documentation
 * is also publically available. As an aide to anyone hacking on this
 * driver the list of errata that are relevant is below.going back to
 * PIIX4. Older device documentation is now a bit tricky to find.
 *
 * Errata of note:
 *
 * Unfixable
 *	PIIX4    errata #9	- Only on ultra obscure hw
 *	ICH3	 errata #13     - Not observed to affect real hw
 *				  by Intel
 *
 * Things we must deal with
 *	PIIX4	errata #10	- BM IDE hang with non UDMA
 *				  (must stop/start dma to recover)
 *	440MX   errata #15	- As PIIX4 errata #10
 *	PIIX4	errata #15	- Must not read control registers
 * 				  during a PIO transfer
 *	440MX   errata #13	- As PIIX4 errata #15
 *	ICH2	errata #21	- DMA mode 0 doesn't work right
 *	ICH0/1  errata #55	- As ICH2 errata #21
 *	ICH2	spec c #9	- Extra operations needed to handle
 *				  drive hotswap [NOT YET SUPPORTED]
 *	ICH2    spec c #20	- IDE PRD must not cross a 64K boundary
 *				  and must be dword aligned
 *	ICH2    spec c #24	- UDMA mode 4,5 t85/86 should be 6ns not 3.3
 *
 * Should have been BIOS fixed:
 *	450NX:	errata #19	- DMA hangs on old 450NX
 *	450NX:  errata #20	- DMA hangs on old 450NX
 *	450NX:  errata #25	- Corruption with DMA on old 450NX
 *	ICH3    errata #15      - IDE deadlock under high load
 *				  (BIOS must set dev 31 fn 0 bit 23)
 *	ICH3	errata #18	- Don't use native mode
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#define DRV_NAME "piix"

static int no_piix_dma;

/**
 *	piix_set_pio_mode	-	set host controller for PIO mode
 *	@drive: drive
 *	@pio: PIO mode number
 *
 *	Set the interface PIO mode based upon the settings done by AMI BIOS.
 */

static void piix_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	int is_slave		= drive->dn & 1;
	int master_port		= hwif->channel ? 0x42 : 0x40;
	int slave_port		= 0x44;
	unsigned long flags;
	u16 master_data;
	u8 slave_data;
	static DEFINE_SPINLOCK(tune_lock);
	int control = 0;

				     /* ISP  RTC */
	static const u8 timings[][2]= {
					{ 0, 0 },
					{ 0, 0 },
					{ 1, 0 },
					{ 2, 1 },
					{ 2, 3 }, };

	/*
	 * Master vs slave is synchronized above us but the slave register is
	 * shared by the two hwifs so the corner case of two slave timeouts in
	 * parallel must be locked.
	 */
	spin_lock_irqsave(&tune_lock, flags);
	pci_read_config_word(dev, master_port, &master_data);

	if (pio > 1)
		control |= 1;	/* Programmable timing on */
	if (drive->media == ide_disk)
		control |= 4;	/* Prefetch, post write */
	if (pio > 2)
		control |= 2;	/* IORDY */
	if (is_slave) {
		master_data |=  0x4000;
		master_data &= ~0x0070;
		if (pio > 1) {
			/* Set PPE, IE and TIME */
			master_data |= control << 4;
		}
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= hwif->channel ? 0x0f : 0xf0;
		slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) <<
			       (hwif->channel ? 4 : 0);
	} else {
		master_data &= ~0x3307;
		if (pio > 1) {
			/* enable PPE, IE and TIME */
			master_data |= control;
		}
		master_data |= (timings[pio][0] << 12) | (timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
	spin_unlock_irqrestore(&tune_lock, flags);
}

/**
 *	piix_set_dma_mode	-	set host controller for DMA mode
 *	@drive: drive
 *	@speed: DMA mode
 *
 *	Set a PIIX host controller to the desired DMA mode.  This involves
 *	programming the right timing data into the PCI configuration space.
 */

static void piix_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u8 maslave		= hwif->channel ? 0x42 : 0x40;
	int a_speed		= 3 << (drive->dn * 4);
	int u_flag		= 1 << drive->dn;
	int v_flag		= 0x01 << drive->dn;
	int w_flag		= 0x10 << drive->dn;
	int u_speed		= 0;
	int			sitre;
	u16			reg4042, reg4a;
	u8			reg48, reg54, reg55;

	pci_read_config_word(dev, maslave, &reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_byte(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);
	pci_read_config_byte(dev, 0x54, &reg54);
	pci_read_config_byte(dev, 0x55, &reg55);

	if (speed >= XFER_UDMA_0) {
		u8 udma = speed - XFER_UDMA_0;

		u_speed = min_t(u8, 2 - (udma & 1), udma) << (drive->dn * 4);

		if (!(reg48 & u_flag))
			pci_write_config_byte(dev, 0x48, reg48 | u_flag);
		if (speed == XFER_UDMA_5) {
			pci_write_config_byte(dev, 0x55, (u8) reg55|w_flag);
		} else {
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
		}
		if ((reg4a & a_speed) != u_speed)
			pci_write_config_word(dev, 0x4a, (reg4a & ~a_speed) | u_speed);
		if (speed > XFER_UDMA_2) {
			if (!(reg54 & v_flag))
				pci_write_config_byte(dev, 0x54, reg54 | v_flag);
		} else
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
	} else {
		const u8 mwdma_to_pio[] = { 0, 3, 4 };
		u8 pio;

		if (reg48 & u_flag)
			pci_write_config_byte(dev, 0x48, reg48 & ~u_flag);
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		if (reg54 & v_flag)
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
		if (reg55 & w_flag)
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);

		if (speed >= XFER_MW_DMA_0)
			pio = mwdma_to_pio[speed - XFER_MW_DMA_0];
		else
			pio = 2; /* only SWDMA2 is allowed */

		piix_set_pio_mode(drive, pio);
	}
}

/**
 *	init_chipset_ich	-	set up the ICH chipset
 *	@dev: PCI device to set up
 *
 *	Initialize the PCI device as required.  For the ICH this turns
 *	out to be nice and simple.
 */

static unsigned int init_chipset_ich(struct pci_dev *dev)
{
	u32 extra = 0;

	pci_read_config_dword(dev, 0x54, &extra);
	pci_write_config_dword(dev, 0x54, extra | 0x400);

	return 0;
}

/**
 *	ich_clear_irq	-	clear BMDMA status
 *	@drive: IDE drive
 *
 *	ICHx contollers set DMA INTR no matter DMA or PIO.
 *	BMDMA status might need to be cleared even for
 *	PIO interrupts to prevent spurious/lost IRQ.
 */
static void ich_clear_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 dma_stat;

	/*
	 * ide_dma_end() needs BMDMA status for error checking.
	 * So, skip clearing BMDMA status here and leave it
	 * to ide_dma_end() if this is DMA interrupt.
	 */
	if (drive->waiting_for_dma || hwif->dma_base == 0)
		return;

	/* clear the INTR & ERROR bits */
	dma_stat = inb(hwif->dma_base + ATA_DMA_STATUS);
	/* Should we force the bit as well ? */
	outb(dma_stat, hwif->dma_base + ATA_DMA_STATUS);
}

struct ich_laptop {
	u16 device;
	u16 subvendor;
	u16 subdevice;
};

/*
 *	List of laptops that use short cables rather than 80 wire
 */

static const struct ich_laptop ich_laptop[] = {
	/* devid, subvendor, subdev */
	{ 0x27DF, 0x1025, 0x0102 },	/* ICH7 on Acer 5602aWLMi */
	{ 0x27DF, 0x0005, 0x0280 },	/* ICH7 on Acer 5602WLMi */
	{ 0x27DF, 0x1025, 0x0110 },	/* ICH7 on Acer 3682WLMi */
	{ 0x27DF, 0x1043, 0x1267 },	/* ICH7 on Asus W5F */
	{ 0x27DF, 0x103C, 0x30A1 },	/* ICH7 on HP Compaq nc2400 */
	{ 0x27DF, 0x1071, 0xD221 },	/* ICH7 on Hercules EC-900 */
	{ 0x24CA, 0x1025, 0x0061 },	/* ICH4 on Acer Aspire 2023WLMi */
	{ 0x24CA, 0x1025, 0x003d },	/* ICH4 on ACER TM290 */
	{ 0x266F, 0x1025, 0x0066 },	/* ICH6 on ACER Aspire 1694WLMi */
	{ 0x2653, 0x1043, 0x82D8 },	/* ICH6M on Asus Eee 701 */
	/* end marker */
	{ 0, }
};

static u8 piix_cable_detect(ide_hwif_t *hwif)
{
	struct pci_dev *pdev = to_pci_dev(hwif->dev);
	const struct ich_laptop *lap = &ich_laptop[0];
	u8 reg54h = 0, mask = hwif->channel ? 0xc0 : 0x30;

	/* check for specials */
	while (lap->device) {
		if (lap->device == pdev->device &&
		    lap->subvendor == pdev->subsystem_vendor &&
		    lap->subdevice == pdev->subsystem_device) {
			return ATA_CBL_PATA40_SHORT;
		}
		lap++;
	}

	pci_read_config_byte(pdev, 0x54, &reg54h);

	return (reg54h & mask) ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
}

/**
 *	init_hwif_piix		-	fill in the hwif for the PIIX
 *	@hwif: IDE interface
 *
 *	Set up the ide_hwif_t for the PIIX interface according to the
 *	capabilities of the hardware.
 */

static void __devinit init_hwif_piix(ide_hwif_t *hwif)
{
	if (!hwif->dma_base)
		return;

	if (no_piix_dma)
		hwif->ultra_mask = hwif->mwdma_mask = hwif->swdma_mask = 0;
}

static const struct ide_port_ops piix_port_ops = {
	.set_pio_mode		= piix_set_pio_mode,
	.set_dma_mode		= piix_set_dma_mode,
	.cable_detect		= piix_cable_detect,
};

static const struct ide_port_ops ich_port_ops = {
	.set_pio_mode		= piix_set_pio_mode,
	.set_dma_mode		= piix_set_dma_mode,
	.clear_irq		= ich_clear_irq,
	.cable_detect		= piix_cable_detect,
};

#ifndef CONFIG_IA64
 #define IDE_HFLAGS_PIIX IDE_HFLAG_LEGACY_IRQS
#else
 #define IDE_HFLAGS_PIIX 0
#endif

#define DECLARE_PIIX_DEV(udma) \
	{						\
		.name		= DRV_NAME,		\
		.init_hwif	= init_hwif_piix,	\
		.enablebits	= {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, \
		.port_ops	= &piix_port_ops,	\
		.host_flags	= IDE_HFLAGS_PIIX,	\
		.pio_mask	= ATA_PIO4,		\
		.swdma_mask	= ATA_SWDMA2_ONLY,	\
		.mwdma_mask	= ATA_MWDMA12_ONLY,	\
		.udma_mask	= udma,			\
	}

#define DECLARE_ICH_DEV(udma) \
	{ \
		.name		= DRV_NAME, \
		.init_chipset	= init_chipset_ich, \
		.init_hwif	= init_hwif_piix, \
		.enablebits	= {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, \
		.port_ops	= &ich_port_ops, \
		.host_flags	= IDE_HFLAGS_PIIX, \
		.pio_mask	= ATA_PIO4, \
		.swdma_mask	= ATA_SWDMA2_ONLY, \
		.mwdma_mask	= ATA_MWDMA12_ONLY, \
		.udma_mask	= udma, \
	}

static const struct ide_port_info piix_pci_info[] __devinitdata = {
	/* 0: MPIIX */
	{	/*
		 * MPIIX actually has only a single IDE channel mapped to
		 * the primary or secondary ports depending on the value
		 * of the bit 14 of the IDETIM register at offset 0x6c
		 */
		.name		= DRV_NAME,
		.enablebits	= {{0x6d,0xc0,0x80}, {0x6d,0xc0,0xc0}},
		.host_flags	= IDE_HFLAG_ISA_PORTS | IDE_HFLAG_NO_DMA |
				  IDE_HFLAGS_PIIX,
		.pio_mask	= ATA_PIO4,
		/* This is a painful system best to let it self tune for now */
	},
	/* 1: PIIXa/PIIXb/PIIX3 */
	DECLARE_PIIX_DEV(0x00), /* no udma */
	/* 2: PIIX4 */
	DECLARE_PIIX_DEV(ATA_UDMA2),
	/* 3: ICH0 */
	DECLARE_ICH_DEV(ATA_UDMA2),
	/* 4: ICH */
	DECLARE_ICH_DEV(ATA_UDMA4),
	/* 5: PIIX4 */
	DECLARE_PIIX_DEV(ATA_UDMA4),
	/* 6: ICH[2-7]/ICH[2-3]M/C-ICH/ICH5-SATA/ESB2/ICH8M */
	DECLARE_ICH_DEV(ATA_UDMA5),
};

/**
 *	piix_init_one	-	called when a PIIX is found
 *	@dev: the piix device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit piix_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_pci_init_one(dev, &piix_pci_info[id->driver_data], NULL);
}

/**
 *	piix_check_450nx	-	Check for problem 450NX setup
 *	
 *	Check for the present of 450NX errata #19 and errata #25. If
 *	they are found, disable use of DMA IDE
 */

static void __devinit piix_check_450nx(void)
{
	struct pci_dev *pdev = NULL;
	u16 cfg;
	while((pdev=pci_get_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82454NX, pdev))!=NULL)
	{
		/* Look for 450NX PXB. Check for problem configurations
		   A PCI quirk checks bit 6 already */
		pci_read_config_word(pdev, 0x41, &cfg);
		/* Only on the original revision: IDE DMA can hang */
		if (pdev->revision == 0x00)
			no_piix_dma = 1;
		/* On all revisions below 5 PXB bus lock must be disabled for IDE */
		else if (cfg & (1<<14) && pdev->revision < 5)
			no_piix_dma = 2;
	}
	if(no_piix_dma)
		printk(KERN_WARNING DRV_NAME ": 450NX errata present, disabling IDE DMA.\n");
	if(no_piix_dma == 2)
		printk(KERN_WARNING DRV_NAME ": A BIOS update may resolve this.\n");
}		

static const struct pci_device_id piix_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82371FB_0),  1 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82371FB_1),  1 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82371MX),    0 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82371SB_1),  1 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82371AB),    2 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801AB_1),  3 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82443MX_1),  2 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801AA_1),  4 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82372FB_1),  5 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82451NX),    2 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801BA_9),  6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801BA_8),  6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801CA_10), 6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801CA_11), 6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801DB_11), 6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801EB_11), 6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801E_11),  6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801DB_10), 6 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801EB_1),  6 },
#endif
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ESB_2),      6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ICH6_19),    6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ICH7_21),    6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82801DB_1),  6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ESB2_18),    6 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ICH8_6),     6 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, piix_pci_tbl);

static struct pci_driver piix_pci_driver = {
	.name		= "PIIX_IDE",
	.id_table	= piix_pci_tbl,
	.probe		= piix_init_one,
	.remove		= ide_pci_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init piix_ide_init(void)
{
	piix_check_450nx();
	return ide_pci_register_driver(&piix_pci_driver);
}

static void __exit piix_ide_exit(void)
{
	pci_unregister_driver(&piix_pci_driver);
}

module_init(piix_ide_init);
module_exit(piix_ide_exit);

MODULE_AUTHOR("Andre Hedrick, Andrzej Krzysztofowicz");
MODULE_DESCRIPTION("PCI driver module for Intel PIIX IDE");
MODULE_LICENSE("GPL");
