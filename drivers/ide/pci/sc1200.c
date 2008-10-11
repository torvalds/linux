/*
 * Copyright (C) 2000-2002		Mark Lord <mlord@pobox.com>
 * Copyright (C)      2007		Bartlomiej Zolnierkiewicz
 *
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Development of this chipset driver was funded
 * by the nice folks at National Semiconductor.
 *
 * Documentation:
 *	Available from National Semiconductor
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/pm.h>

#include <asm/io.h>

#define DRV_NAME "sc1200"

#define SC1200_REV_A	0x00
#define SC1200_REV_B1	0x01
#define SC1200_REV_B3	0x02
#define SC1200_REV_C1	0x03
#define SC1200_REV_D1	0x04

#define PCI_CLK_33	0x00
#define PCI_CLK_48	0x01
#define PCI_CLK_66	0x02
#define PCI_CLK_33A	0x03

static unsigned short sc1200_get_pci_clock (void)
{
	unsigned char chip_id, silicon_revision;
	unsigned int pci_clock;
	/*
	 * Check the silicon revision, as not all versions of the chip
	 * have the register with the fast PCI bus timings.
	 */
	chip_id = inb (0x903c);
	silicon_revision = inb (0x903d);

	// Read the fast pci clock frequency
	if (chip_id == 0x04 && silicon_revision < SC1200_REV_B1) {
		pci_clock = PCI_CLK_33;
	} else {
		// check clock generator configuration (cfcc)
		// the clock is in bits 8 and 9 of this word

		pci_clock = inw (0x901e);
		pci_clock >>= 8;
		pci_clock &= 0x03;
		if (pci_clock == PCI_CLK_33A)
			pci_clock = PCI_CLK_33;
	}
	return pci_clock;
}

/*
 * Here are the standard PIO mode 0-4 timings for each "format".
 * Format-0 uses fast data reg timings, with slower command reg timings.
 * Format-1 uses fast timings for all registers, but won't work with all drives.
 */
static const unsigned int sc1200_pio_timings[4][5] =
	{{0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010},	// format0  33Mhz
	 {0xd1329172, 0x71212171, 0x30200080, 0x20102010, 0x00100010},	// format1, 33Mhz
	 {0xfaa3f4f3, 0xc23232b2, 0x513101c1, 0x31213121, 0x10211021},	// format1, 48Mhz
	 {0xfff4fff4, 0xf35353d3, 0x814102f1, 0x42314231, 0x11311131}};	// format1, 66Mhz

/*
 * After chip reset, the PIO timings are set to 0x00009172, which is not valid.
 */
//#define SC1200_BAD_PIO(timings) (((timings)&~0x80000000)==0x00009172)

static void sc1200_tunepio(ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *pdev = to_pci_dev(hwif->dev);
	unsigned int basereg = hwif->channel ? 0x50 : 0x40, format = 0;

	pci_read_config_dword(pdev, basereg + 4, &format);
	format = (format >> 31) & 1;
	if (format)
		format += sc1200_get_pci_clock();
	pci_write_config_dword(pdev, basereg + ((drive->dn & 1) << 3),
			       sc1200_pio_timings[format][pio]);
}

/*
 *	The SC1200 specifies that two drives sharing a cable cannot mix
 *	UDMA/MDMA.  It has to be one or the other, for the pair, though
 *	different timings can still be chosen for each drive.  We could
 *	set the appropriate timing bits on the fly, but that might be
 *	a bit confusing.  So, for now we statically handle this requirement
 *	by looking at our mate drive to see what it is capable of, before
 *	choosing a mode for our own drive.
 */
static u8 sc1200_udma_filter(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	ide_drive_t *mate = ide_get_pair_dev(drive);
	u16 *mateid = mate->id;
	u8 mask = hwif->ultra_mask;

	if (mate == NULL)
		goto out;

	if (ata_id_has_dma(mateid) && __ide_dma_bad_drive(mate) == 0) {
		if ((mateid[ATA_ID_FIELD_VALID] & 4) &&
		    (mateid[ATA_ID_UDMA_MODES] & 7))
			goto out;
		if ((mateid[ATA_ID_FIELD_VALID] & 2) &&
		    (mateid[ATA_ID_MWDMA_MODES] & 7))
			mask = 0;
	}
out:
	return mask;
}

static void sc1200_set_dma_mode(ide_drive_t *drive, const u8 mode)
{
	ide_hwif_t		*hwif = HWIF(drive);
	struct pci_dev		*dev = to_pci_dev(hwif->dev);
	int			unit = drive->select.b.unit;
	unsigned int		reg, timings;
	unsigned short		pci_clock;
	unsigned int		basereg = hwif->channel ? 0x50 : 0x40;

	static const u32 udma_timing[3][3] = {
		{ 0x00921250, 0x00911140, 0x00911030 },
		{ 0x00932470, 0x00922260, 0x00922140 },
		{ 0x009436a1, 0x00933481, 0x00923261 },
	};

	static const u32 mwdma_timing[3][3] = {
		{ 0x00077771, 0x00012121, 0x00002020 },
		{ 0x000bbbb2, 0x00024241, 0x00013131 },
		{ 0x000ffff3, 0x00035352, 0x00015151 },
	};

	pci_clock = sc1200_get_pci_clock();

	/*
	 * Note that each DMA mode has several timings associated with it.
	 * The correct timing depends on the fast PCI clock freq.
	 */

	if (mode >= XFER_UDMA_0)
		timings =  udma_timing[pci_clock][mode - XFER_UDMA_0];
	else
		timings = mwdma_timing[pci_clock][mode - XFER_MW_DMA_0];

	if (unit == 0) {			/* are we configuring drive0? */
		pci_read_config_dword(dev, basereg + 4, &reg);
		timings |= reg & 0x80000000;	/* preserve PIO format bit */
		pci_write_config_dword(dev, basereg + 4, timings);
	} else
		pci_write_config_dword(dev, basereg + 12, timings);
}

/*  Replacement for the standard ide_dma_end action in
 *  dma_proc.
 *
 *  returns 1 on error, 0 otherwise
 */
static int sc1200_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long dma_base = hwif->dma_base;
	byte dma_stat;

	dma_stat = inb(dma_base+2);		/* get DMA status */

	if (!(dma_stat & 4))
		printk(" ide_dma_end dma_stat=%0x err=%x newerr=%x\n",
		  dma_stat, ((dma_stat&7)!=4), ((dma_stat&2)==2));

	outb(dma_stat|0x1b, dma_base+2);	/* clear the INTR & ERROR bits */
	outb(inb(dma_base)&~1, dma_base);	/* !! DO THIS HERE !! stop DMA */

	drive->waiting_for_dma = 0;
	ide_destroy_dmatable(drive);		/* purge DMA mappings */

	return (dma_stat & 7) != 4;		/* verify good DMA status */
}

/*
 * sc1200_set_pio_mode() handles setting of PIO modes
 * for both the chipset and drive.
 *
 * All existing BIOSs for this chipset guarantee that all drives
 * will have valid default PIO timings set up before we get here.
 */

static void sc1200_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t	*hwif = HWIF(drive);
	int		mode = -1;

	/*
	 * bad abuse of ->set_pio_mode interface
	 */
	switch (pio) {
		case 200: mode = XFER_UDMA_0;	break;
		case 201: mode = XFER_UDMA_1;	break;
		case 202: mode = XFER_UDMA_2;	break;
		case 100: mode = XFER_MW_DMA_0;	break;
		case 101: mode = XFER_MW_DMA_1;	break;
		case 102: mode = XFER_MW_DMA_2;	break;
	}
	if (mode != -1) {
		printk("SC1200: %s: changing (U)DMA mode\n", drive->name);
		ide_dma_off_quietly(drive);
		if (ide_set_dma_mode(drive, mode) == 0 && drive->using_dma)
			hwif->dma_ops->dma_host_set(drive, 1);
		return;
	}

	sc1200_tunepio(drive, pio);
}

#ifdef CONFIG_PM
struct sc1200_saved_state {
	u32 regs[8];
};

static int sc1200_suspend (struct pci_dev *dev, pm_message_t state)
{
	printk("SC1200: suspend(%u)\n", state.event);

	/*
	 * we only save state when going from full power to less
	 */
	if (state.event == PM_EVENT_ON) {
		struct ide_host *host = pci_get_drvdata(dev);
		struct sc1200_saved_state *ss = host->host_priv;
		unsigned int r;

		/*
		 * save timing registers
		 * (this may be unnecessary if BIOS also does it)
		 */
		for (r = 0; r < 8; r++)
			pci_read_config_dword(dev, 0x40 + r * 4, &ss->regs[r]);
	}

	pci_disable_device(dev);
	pci_set_power_state(dev, pci_choose_state(dev, state));
	return 0;
}

static int sc1200_resume (struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	struct sc1200_saved_state *ss = host->host_priv;
	unsigned int r;
	int i;

	i = pci_enable_device(dev);
	if (i)
		return i;

	/*
	 * restore timing registers
	 * (this may be unnecessary if BIOS also does it)
	 */
	for (r = 0; r < 8; r++)
		pci_write_config_dword(dev, 0x40 + r * 4, ss->regs[r]);

	return 0;
}
#endif

static const struct ide_port_ops sc1200_port_ops = {
	.set_pio_mode		= sc1200_set_pio_mode,
	.set_dma_mode		= sc1200_set_dma_mode,
	.udma_filter		= sc1200_udma_filter,
};

static const struct ide_dma_ops sc1200_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= ide_dma_start,
	.dma_end		= sc1200_dma_end,
	.dma_test_irq		= ide_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timeout		= ide_dma_timeout,
};

static const struct ide_port_info sc1200_chipset __devinitdata = {
	.name		= DRV_NAME,
	.port_ops	= &sc1200_port_ops,
	.dma_ops	= &sc1200_dma_ops,
	.host_flags	= IDE_HFLAG_SERIALIZE |
			  IDE_HFLAG_POST_SET_MODE |
			  IDE_HFLAG_ABUSE_DMA_MODES,
	.pio_mask	= ATA_PIO4,
	.mwdma_mask	= ATA_MWDMA2,
	.udma_mask	= ATA_UDMA2,
};

static int __devinit sc1200_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct sc1200_saved_state *ss = NULL;
	int rc;

#ifdef CONFIG_PM
	ss = kmalloc(sizeof(*ss), GFP_KERNEL);
	if (ss == NULL)
		return -ENOMEM;
#endif
	rc = ide_pci_init_one(dev, &sc1200_chipset, ss);
	if (rc)
		kfree(ss);

	return rc;
}

static const struct pci_device_id sc1200_pci_tbl[] = {
	{ PCI_VDEVICE(NS, PCI_DEVICE_ID_NS_SCx200_IDE), 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, sc1200_pci_tbl);

static struct pci_driver driver = {
	.name		= "SC1200_IDE",
	.id_table	= sc1200_pci_tbl,
	.probe		= sc1200_init_one,
	.remove		= ide_pci_remove,
#ifdef CONFIG_PM
	.suspend	= sc1200_suspend,
	.resume		= sc1200_resume,
#endif
};

static int __init sc1200_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void __exit sc1200_ide_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(sc1200_ide_init);
module_exit(sc1200_ide_exit);

MODULE_AUTHOR("Mark Lord");
MODULE_DESCRIPTION("PCI driver module for NS SC1200 IDE");
MODULE_LICENSE("GPL");
