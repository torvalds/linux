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
static unsigned int get_pio_timings(ide_pio_data_t *p)
{
	unsigned int cmd_on, cmd_off;

	cmd_on  = (ide_pio_timings[p->pio_mode].active_time + 29) / 30;
	cmd_off = (p->cycle_time - 30 * cmd_on + 29) / 30;

	if (cmd_on == 0)
		cmd_on = 1;

	if (cmd_off == 0)
		cmd_off = 1;

	return (cmd_on - 1) << 8 | (cmd_off - 1) | (p->use_iordy ? 0x40 : 0x00);
}

/*
 * Configure the chipset for PIO mode.
 */
static u8 sl82c105_tune_pio(ide_drive_t *drive, u8 pio)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	int reg			= 0x44 + drive->dn * 4;
	ide_pio_data_t p;
	u16 drv_ctrl;

	DBG(("sl82c105_tune_pio(drive:%s, pio:%u)\n", drive->name, pio));

	pio = ide_get_best_pio_mode(drive, pio, 5, &p);

	drv_ctrl = get_pio_timings(&p);

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
	       ide_xfer_verbose(pio + XFER_PIO_0), p.cycle_time, drv_ctrl);

	return pio;
}

/*
 * Configure the drive and chipset for a new transfer speed.
 */
static int sl82c105_tune_chipset(ide_drive_t *drive, u8 speed)
{
	static u16 mwdma_timings[] = {0x0707, 0x0201, 0x0200};
	u16 drv_ctrl;

 	DBG(("sl82c105_tune_chipset(drive:%s, speed:%s)\n",
	     drive->name, ide_xfer_verbose(speed)));

	speed = ide_rate_filter(drive, speed);

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
	case XFER_PIO_5:
	case XFER_PIO_4:
	case XFER_PIO_3:
	case XFER_PIO_2:
	case XFER_PIO_1:
	case XFER_PIO_0:
		(void) sl82c105_tune_pio(drive, speed - XFER_PIO_0);
		break;
	default:
		return -1;
	}

	return ide_config_drive_speed(drive, speed);
}

/*
 * Check to see if the drive and chipset are capable of DMA mode.
 */
static int sl82c105_ide_dma_check(ide_drive_t *drive)
{
	DBG(("sl82c105_ide_dma_check(drive:%s)\n", drive->name));

	if (ide_tune_dma(drive))
		return 0;

	return -1;
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
 * We only deal with PIO mode here - DMA mode 'using_dma' is not
 * initialised at the point that this function is called.
 */
static void sl82c105_tune_drive(ide_drive_t *drive, u8 pio)
{
	DBG(("sl82c105_tune_drive(drive:%s, pio:%u)\n", drive->name, pio));

	pio = sl82c105_tune_pio(drive, pio);
	(void) ide_config_drive_speed(drive, XFER_PIO_0 + pio);
}

/*
 * Return the revision of the Winbond bridge
 * which this function is part of.
 */
static unsigned int sl82c105_bridge_revision(struct pci_dev *dev)
{
	struct pci_dev *bridge;
	u8 rev;

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
	pci_read_config_byte(bridge, PCI_REVISION_ID, &rev);
	pci_dev_put(bridge);

	return rev;
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

	hwif->tuneproc		= &sl82c105_tune_drive;
	hwif->speedproc 	= &sl82c105_tune_chipset;
	hwif->selectproc	= &sl82c105_selectproc;
	hwif->resetproc 	= &sl82c105_resetproc;

	/*
	 * We support 32-bit I/O on this interface, and
	 * it doesn't have problems with interrupts.
	 */
	hwif->drives[0].io_32bit = hwif->drives[1].io_32bit = 1;
	hwif->drives[0].unmask   = hwif->drives[1].unmask   = 1;

	/*
	 * We always autotune PIO,  this is done before DMA is checked,
	 * so there's no risk of accidentally disabling DMA
	 */
	hwif->drives[0].autotune = hwif->drives[1].autotune = 1;

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

	hwif->atapi_dma  = 1;
	hwif->mwdma_mask = 0x07;

	hwif->ide_dma_check		= &sl82c105_ide_dma_check;
	hwif->ide_dma_on		= &sl82c105_ide_dma_on;
	hwif->dma_off_quietly		= &sl82c105_dma_off_quietly;
	hwif->dma_lost_irq		= &sl82c105_dma_lost_irq;
	hwif->dma_start			= &sl82c105_dma_start;
	hwif->dma_timeout		= &sl82c105_dma_timeout;

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->drives[1].autodma = hwif->autodma;

	if (hwif->mate)
		hwif->serialized = hwif->mate->serialized = 1;
}

static ide_pci_device_t sl82c105_chipset __devinitdata = {
	.name		= "W82C105",
	.init_chipset	= init_chipset_sl82c105,
	.init_hwif	= init_hwif_sl82c105,
	.channels	= 2,
	.autodma	= NOAUTODMA,
	.enablebits	= {{0x40,0x01,0x01}, {0x40,0x10,0x10}},
	.bootable	= ON_BOARD,
};

static int __devinit sl82c105_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &sl82c105_chipset);
}

static struct pci_device_id sl82c105_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105), 0},
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
