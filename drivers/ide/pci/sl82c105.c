/*
 * linux/drivers/ide/pci/sl82c105.c
 *
 * SL82C105/Winbond 553 IDE driver
 *
 * Maintainer unknown.
 *
 * Drive tuning added from Rebel.com's kernel sources
 *  -- Russell King (15/11/98) linux@arm.linux.org.uk
 * 
 * Merge in Russell's HW workarounds, fix various problems
 * with the timing registers setup.
 *  -- Benjamin Herrenschmidt (01/11/03) benh@kernel.crashing.org
 *
 * Copyright (C) 2006-2007 MontaVista Software, Inc. <source@mvista.com>
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/dma.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(arg) printk arg
#else
#define DBG(fmt,...)
#endif
/*
 * SL82C105 PCI config register 0x40 bits.
 */
#define CTRL_IDE_IRQB   (1 << 30)
#define CTRL_IDE_IRQA   (1 << 28)
#define CTRL_LEGIRQ     (1 << 11)
#define CTRL_P1F16      (1 << 5)
#define CTRL_P1EN       (1 << 4)
#define CTRL_P0F16      (1 << 1)
#define CTRL_P0EN       (1 << 0)

/*
 * Convert a PIO mode and cycle time to the required on/off times
 * for the interface.  This has protection against runaway timings.
 */
static unsigned int get_pio_timings(ide_drive_t *drive, u8 pio)
{
	unsigned int cmd_on, cmd_off;
	u8 iordy = 0;

	cmd_on  = (ide_pio_timings[pio].active_time + 29) / 30;
	cmd_off = (ide_pio_cycle_time(drive, pio) - 30 * cmd_on + 29) / 30;

	if (cmd_on == 0)
		cmd_on = 1;

	if (cmd_off == 0)
		cmd_off = 1;

	if (pio > 2 || ide_dev_has_iordy(drive->id))
		iordy = 0x40;

	return (cmd_on - 1) << 8 | (cmd_off - 1) | iordy;
}

/*
 * Configure the chipset for PIO mode.
 */
static void sl82c105_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	int reg			= 0x44 + drive->dn * 4;
	u16 drv_ctrl;

	drv_ctrl = get_pio_timings(drive, pio);

	/*
	 * Store the PIO timings so that we can restore them
	 * in case DMA will be turned off...
	 */
	drive->drive_data &= 0xffff0000;
	drive->drive_data |= drv_ctrl;

	if (!drive->using_dma) {
		/*
		 * If we are actually using MW DMA, then we can not
		 * reprogram the interface drive control register.
		 */
		pci_write_config_word(dev, reg,  drv_ctrl);
		pci_read_config_word (dev, reg, &drv_ctrl);
	}

	printk(KERN_DEBUG "%s: selected %s (%dns) (%04X)\n", drive->name,
			  ide_xfer_verbose(pio + XFER_PIO_0),
			  ide_pio_cycle_time(drive, pio), drv_ctrl);
}

/*
 * Configure the chipset for DMA mode.
 */
static void sl82c105_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	static u16 mwdma_timings[] = {0x0707, 0x0201, 0x0200};
	u16 drv_ctrl;

 	DBG(("sl82c105_tune_chipset(drive:%s, speed:%s)\n",
	     drive->name, ide_xfer_verbose(speed)));

	switch (speed) {
	case XFER_MW_DMA_2:
	case XFER_MW_DMA_1:
	case XFER_MW_DMA_0:
		drv_ctrl = mwdma_timings[speed - XFER_MW_DMA_0];

		/*
		 * Store the DMA timings so that we can actually program
		 * them when DMA will be turned on...
		 */
		drive->drive_data &= 0x0000ffff;
		drive->drive_data |= (unsigned long)drv_ctrl << 16;

		/*
		 * If we are already using DMA, we just reprogram
		 * the drive control register.
		 */
		if (drive->using_dma) {
			struct pci_dev *dev	= HWIF(drive)->pci_dev;
			int reg 		= 0x44 + drive->dn * 4;

			pci_write_config_word(dev, reg, drv_ctrl);
		}
		break;
	default:
		return;
	}
}

/*
 * The SL82C105 holds off all IDE interrupts while in DMA mode until
 * all DMA activity is completed.  Sometimes this causes problems (eg,
 * when the drive wants to report an error condition).
 *
 * 0x7e is a "chip testing" register.  Bit 2 resets the DMA controller
 * state machine.  We need to kick this to work around various bugs.
 */
static inline void sl82c105_reset_host(struct pci_dev *dev)
{
	u16 val;

	pci_read_config_word(dev, 0x7e, &val);
	pci_write_config_word(dev, 0x7e, val | (1 << 2));
	pci_write_config_word(dev, 0x7e, val & ~(1 << 2));
}

/*
 * If we get an IRQ timeout, it might be that the DMA state machine
 * got confused.  Fix from Todd Inglett.  Details from Winbond.
 *
 * This function is called when the IDE timer expires, the drive
 * indicates that it is READY, and we were waiting for DMA to complete.
 */
static void sl82c105_dma_lost_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u32 val, mask		= hwif->channel ? CTRL_IDE_IRQB : CTRL_IDE_IRQA;
	u8 dma_cmd;

	printk("sl82c105: lost IRQ, resetting host\n");

	/*
	 * Check the raw interrupt from the drive.
	 */
	pci_read_config_dword(dev, 0x40, &val);
	if (val & mask)
		printk("sl82c105: drive was requesting IRQ, but host lost it\n");

	/*
	 * Was DMA enabled?  If so, disable it - we're resetting the
	 * host.  The IDE layer will be handling the drive for us.
	 */
	dma_cmd = inb(hwif->dma_command);
	if (dma_cmd & 1) {
		outb(dma_cmd & ~1, hwif->dma_command);
		printk("sl82c105: DMA was enabled\n");
	}

	sl82c105_reset_host(dev);
}

/*
 * ATAPI devices can cause the SL82C105 DMA state machine to go gaga.
 * Winbond recommend that the DMA state machine is reset prior to
 * setting the bus master DMA enable bit.
 *
 * The generic IDE core will have disabled the BMEN bit before this
 * function is called.
 */
static void sl82c105_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	sl82c105_reset_host(dev);
	ide_dma_start(drive);
}

static void sl82c105_dma_timeout(ide_drive_t *drive)
{
	DBG(("sl82c105_dma_timeout(drive:%s)\n", drive->name));

	sl82c105_reset_host(HWIF(drive)->pci_dev);
	ide_dma_timeout(drive);
}

static int sl82c105_ide_dma_on(ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	int rc, reg 		= 0x44 + drive->dn * 4;

	DBG(("sl82c105_ide_dma_on(drive:%s)\n", drive->name));

	rc = __ide_dma_on(drive);
	if (rc == 0) {
		pci_write_config_word(dev, reg, drive->drive_data >> 16);

		printk(KERN_INFO "%s: DMA enabled\n", drive->name);
	}
	return rc;
}

static void sl82c105_dma_off_quietly(ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	int reg 		= 0x44 + drive->dn * 4;

	DBG(("sl82c105_dma_off_quietly(drive:%s)\n", drive->name));

	pci_write_config_word(dev, reg, drive->drive_data);

	ide_dma_off_quietly(drive);
}

/*
 * Ok, that is nasty, but we must make sure the DMA timings
 * won't be used for a PIO access. The solution here is
 * to make sure the 16 bits mode is diabled on the channel
 * when DMA is enabled, thus causing the chip to use PIO0
 * timings for those operations.
 */
static void sl82c105_selectproc(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u32 val, old, mask;

	//DBG(("sl82c105_selectproc(drive:%s)\n", drive->name));

	mask = hwif->channel ? CTRL_P1F16 : CTRL_P0F16;
	old = val = (u32)pci_get_drvdata(dev);
	if (drive->using_dma)
		val &= ~mask;
	else
		val |= mask;
	if (old != val) {
		pci_write_config_dword(dev, 0x40, val);	
		pci_set_drvdata(dev, (void *)val);
	}
}

/*
 * ATA reset will clear the 16 bits mode in the control
 * register, we need to update our cache
 */
static void sl82c105_resetproc(ide_drive_t *drive)
{
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	u32 val;

	DBG(("sl82c105_resetproc(drive:%s)\n", drive->name));

	pci_read_config_dword(dev, 0x40, &val);
	pci_set_drvdata(dev, (void *)val);
}

/*
 * Return the revision of the Winbond bridge
 * which this function is part of.
 */
static unsigned int sl82c105_bridge_revision(struct pci_dev *dev)
{
	struct pci_dev *bridge;

	/*
	 * The bridge should be part of the same device, but function 0.
	 */
	bridge = pci_get_bus_and_slot(dev->bus->number,
			       PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	if (!bridge)
		return -1;

	/*
	 * Make sure it is a Winbond 553 and is an ISA bridge.
	 */
	if (bridge->vendor != PCI_VENDOR_ID_WINBOND ||
	    bridge->device != PCI_DEVICE_ID_WINBOND_83C553 ||
	    bridge->class >> 8 != PCI_CLASS_BRIDGE_ISA) {
	    	pci_dev_put(bridge);
		return -1;
	}
	/*
	 * We need to find function 0's revision, not function 1
	 */
	pci_dev_put(bridge);

	return bridge->revision;
}

/*
 * Enable the PCI device
 * 
 * --BenH: It's arch fixup code that should enable channels that
 * have not been enabled by firmware. I decided we can still enable
 * channel 0 here at least, but channel 1 has to be enabled by
 * firmware or arch code. We still set both to 16 bits mode.
 */
static unsigned int __devinit init_chipset_sl82c105(struct pci_dev *dev, const char *msg)
{
	u32 val;

	DBG(("init_chipset_sl82c105()\n"));

	pci_read_config_dword(dev, 0x40, &val);
	val |= CTRL_P0EN | CTRL_P0F16 | CTRL_P1F16;
	pci_write_config_dword(dev, 0x40, val);
	pci_set_drvdata(dev, (void *)val);

	return dev->irq;
}

/*
 * Initialise IDE channel
 */
static void __devinit init_hwif_sl82c105(ide_hwif_t *hwif)
{
	unsigned int rev;

	DBG(("init_hwif_sl82c105(hwif: ide%d)\n", hwif->index));

	hwif->set_pio_mode	= &sl82c105_set_pio_mode;
	hwif->set_dma_mode	= &sl82c105_set_dma_mode;
	hwif->selectproc	= &sl82c105_selectproc;
	hwif->resetproc 	= &sl82c105_resetproc;

	if (!hwif->dma_base)
		return;

	rev = sl82c105_bridge_revision(hwif->pci_dev);
	if (rev <= 5) {
		/*
		 * Never ever EVER under any circumstances enable
		 * DMA when the bridge is this old.
		 */
		printk("    %s: Winbond W83C553 bridge revision %d, "
		       "BM-DMA disabled\n", hwif->name, rev);
		return;
	}

	hwif->mwdma_mask = ATA_MWDMA2;

	hwif->ide_dma_on		= &sl82c105_ide_dma_on;
	hwif->dma_off_quietly		= &sl82c105_dma_off_quietly;
	hwif->dma_lost_irq		= &sl82c105_dma_lost_irq;
	hwif->dma_start			= &sl82c105_dma_start;
	hwif->dma_timeout		= &sl82c105_dma_timeout;

	if (hwif->mate)
		hwif->serialized = hwif->mate->serialized = 1;
}

static const struct ide_port_info sl82c105_chipset __devinitdata = {
	.name		= "W82C105",
	.init_chipset	= init_chipset_sl82c105,
	.init_hwif	= init_hwif_sl82c105,
	.enablebits	= {{0x40,0x01,0x01}, {0x40,0x10,0x10}},
	.host_flags	= IDE_HFLAG_IO_32BIT |
			  IDE_HFLAG_UNMASK_IRQS |
			  IDE_HFLAG_NO_AUTODMA |
			  IDE_HFLAG_BOOTABLE,
	.pio_mask	= ATA_PIO5,
};

static int __devinit sl82c105_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &sl82c105_chipset);
}

static const struct pci_device_id sl82c105_pci_tbl[] = {
	{ PCI_VDEVICE(WINBOND, PCI_DEVICE_ID_WINBOND_82C105), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, sl82c105_pci_tbl);

static struct pci_driver driver = {
	.name		= "W82C105_IDE",
	.id_table	= sl82c105_pci_tbl,
	.probe		= sl82c105_init_one,
};

static int __init sl82c105_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(sl82c105_ide_init);

MODULE_DESCRIPTION("PCI driver module for W82C105 IDE");
MODULE_LICENSE("GPL");
