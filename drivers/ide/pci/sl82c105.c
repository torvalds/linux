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
 * Convert a PIO mode and cycle time to the required on/off
 * times for the interface.  This has protection against run-away
 * timings.
 */
static unsigned int get_timing_sl82c105(ide_pio_data_t *p)
{
	unsigned int cmd_on;
	unsigned int cmd_off;

	cmd_on = (ide_pio_timings[p->pio_mode].active_time + 29) / 30;
	cmd_off = (p->cycle_time - 30 * cmd_on + 29) / 30;

	if (cmd_on > 32)
		cmd_on = 32;
	if (cmd_on == 0)
		cmd_on = 1;

	if (cmd_off > 32)
		cmd_off = 32;
	if (cmd_off == 0)
		cmd_off = 1;

	return (cmd_on - 1) << 8 | (cmd_off - 1) | (p->use_iordy ? 0x40 : 0x00);
}

/*
 * Configure the drive and chipset for PIO
 */
static void config_for_pio(ide_drive_t *drive, int pio, int report, int chipset_only)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	ide_pio_data_t p;
	u16 drv_ctrl = 0x909;
	unsigned int xfer_mode, reg;

	DBG(("config_for_pio(drive:%s, pio:%d, report:%d, chipset_only:%d)\n",
		drive->name, pio, report, chipset_only));
		
	reg = (hwif->channel ? 0x4c : 0x44) + (drive->select.b.unit ? 4 : 0);

	pio = ide_get_best_pio_mode(drive, pio, 5, &p);

	xfer_mode = XFER_PIO_0 + pio;

	if (chipset_only || ide_config_drive_speed(drive, xfer_mode) == 0) {
		drv_ctrl = get_timing_sl82c105(&p);
		drive->pio_speed = xfer_mode;
	} else
		drive->pio_speed = XFER_PIO_0;

	if (drive->using_dma == 0) {
		/*
		 * If we are actually using MW DMA, then we can not
		 * reprogram the interface drive control register.
		 */
		pci_write_config_word(dev, reg, drv_ctrl);
		pci_read_config_word(dev, reg, &drv_ctrl);

		if (report) {
			printk("%s: selected %s (%dns) (%04X)\n", drive->name,
			       ide_xfer_verbose(xfer_mode), p.cycle_time, drv_ctrl);
		}
	}
}

/*
 * Configure the drive and the chipset for DMA
 */
static int config_for_dma (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	unsigned int reg;

	DBG(("config_for_dma(drive:%s)\n", drive->name));

	reg = (hwif->channel ? 0x4c : 0x44) + (drive->select.b.unit ? 4 : 0);

	if (ide_config_drive_speed(drive, XFER_MW_DMA_2) != 0)
		return 1;

	pci_write_config_word(dev, reg, 0x0240);

	return 0;
}

/*
 * Check to see if the drive and
 * chipset is capable of DMA mode
 */

static int sl82c105_check_drive (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);

	DBG(("sl82c105_check_drive(drive:%s)\n", drive->name));

	do {
		struct hd_driveid *id = drive->id;

		if (!drive->autodma)
			break;

		if (!id || !(id->capability & 1))
			break;

		/* Consult the list of known "bad" drives */
		if (__ide_dma_bad_drive(drive))
			break;

		if (id->field_valid & 2) {
			if ((id->dma_mword & hwif->mwdma_mask) ||
			    (id->dma_1word & hwif->swdma_mask))
				return hwif->ide_dma_on(drive);
		}

		if (__ide_dma_good_drive(drive))
			return hwif->ide_dma_on(drive);
	} while (0);

	return hwif->ide_dma_off_quietly(drive);
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
static int sl82c105_ide_dma_lost_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	u32 val, mask = hwif->channel ? CTRL_IDE_IRQB : CTRL_IDE_IRQA;
	unsigned long dma_base = hwif->dma_base;

	printk("sl82c105: lost IRQ: resetting host\n");

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
	val = hwif->INB(dma_base);
	if (val & 1) {
		outb(val & ~1, dma_base);
		printk("sl82c105: DMA was enabled\n");
	}

	sl82c105_reset_host(dev);

	/* ide_dmaproc would return 1, so we do as well */
	return 1;
}

/*
 * ATAPI devices can cause the SL82C105 DMA state machine to go gaga.
 * Winbond recommend that the DMA state machine is reset prior to
 * setting the bus master DMA enable bit.
 *
 * The generic IDE core will have disabled the BMEN bit before this
 * function is called.
 */
static void sl82c105_ide_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;

	sl82c105_reset_host(dev);
	ide_dma_start(drive);
}

static int sl82c105_ide_dma_timeout(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;

	DBG(("sl82c105_ide_dma_timeout(drive:%s)\n", drive->name));

	sl82c105_reset_host(dev);
	return __ide_dma_timeout(drive);
}

static int sl82c105_ide_dma_on (ide_drive_t *drive)
{
	DBG(("sl82c105_ide_dma_on(drive:%s)\n", drive->name));

	if (config_for_dma(drive)) {
		config_for_pio(drive, 4, 0, 0);
		return HWIF(drive)->ide_dma_off_quietly(drive);
	}
	printk(KERN_INFO "%s: DMA enabled\n", drive->name);
	return __ide_dma_on(drive);
}

static int sl82c105_ide_dma_off_quietly (ide_drive_t *drive)
{
	u8 speed = XFER_PIO_0;
	int rc;
	
	DBG(("sl82c105_ide_dma_off_quietly(drive:%s)\n", drive->name));

	rc = __ide_dma_off_quietly(drive);
	if (drive->pio_speed)
		speed = drive->pio_speed - XFER_PIO_0;
	config_for_pio(drive, speed, 0, 1);
	drive->current_speed = drive->pio_speed;

	return rc;
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
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
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
static void tune_sl82c105(ide_drive_t *drive, u8 pio)
{
	DBG(("tune_sl82c105(drive:%s)\n", drive->name));

	config_for_pio(drive, pio, 1, 0);

	/*
	 * We support 32-bit I/O on this interface, and it
	 * doesn't have problems with interrupts.
	 */
	drive->io_32bit = 1;
	drive->unmask = 1;
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
	bridge = pci_find_slot(dev->bus->number,
			       PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	if (!bridge)
		return -1;

	/*
	 * Make sure it is a Winbond 553 and is an ISA bridge.
	 */
	if (bridge->vendor != PCI_VENDOR_ID_WINBOND ||
	    bridge->device != PCI_DEVICE_ID_WINBOND_83C553 ||
	    bridge->class >> 8 != PCI_CLASS_BRIDGE_ISA)
		return -1;

	/*
	 * We need to find function 0's revision, not function 1
	 */
	pci_read_config_byte(bridge, PCI_REVISION_ID, &rev);

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
 * Initialise the chip
 */

static void __devinit init_hwif_sl82c105(ide_hwif_t *hwif)
{
	unsigned int rev;
	u8 dma_state;

	DBG(("init_hwif_sl82c105(hwif: ide%d)\n", hwif->index));

	hwif->tuneproc = tune_sl82c105;
	hwif->selectproc = sl82c105_selectproc;
	hwif->resetproc = sl82c105_resetproc;

	/*
	 * Default to PIO 0 for fallback unless tuned otherwise.
	 * We always autotune PIO,  this is done before DMA is checked,
	 * so there's no risk of accidentally disabling DMA
	 */
	hwif->drives[0].pio_speed = XFER_PIO_0;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].pio_speed = XFER_PIO_0;
	hwif->drives[1].autotune = 1;

	hwif->atapi_dma = 0;
	hwif->mwdma_mask = 0;
	hwif->swdma_mask = 0;
	hwif->autodma = 0;

	if (!hwif->dma_base)
		return;

	dma_state = hwif->INB(hwif->dma_base + 2) & ~0x60;
	rev = sl82c105_bridge_revision(hwif->pci_dev);
	if (rev <= 5) {
		/*
		 * Never ever EVER under any circumstances enable
		 * DMA when the bridge is this old.
		 */
		printk("    %s: Winbond 553 bridge revision %d, BM-DMA disabled\n",
		       hwif->name, rev);
	} else {
		dma_state |= 0x60;

		hwif->atapi_dma = 1;
		hwif->mwdma_mask = 0x07;
		hwif->swdma_mask = 0x07;

		hwif->ide_dma_check = &sl82c105_check_drive;
		hwif->ide_dma_on = &sl82c105_ide_dma_on;
		hwif->ide_dma_off_quietly = &sl82c105_ide_dma_off_quietly;
		hwif->ide_dma_lostirq = &sl82c105_ide_dma_lost_irq;
		hwif->dma_start = &sl82c105_ide_dma_start;
		hwif->ide_dma_timeout = &sl82c105_ide_dma_timeout;

		if (!noautodma)
			hwif->autodma = 1;
		hwif->drives[0].autodma = hwif->autodma;
		hwif->drives[1].autodma = hwif->autodma;

		if (hwif->mate)
			hwif->serialized = hwif->mate->serialized = 1;
	}
	hwif->OUTB(dma_state, hwif->dma_base + 2);
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

static int sl82c105_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(sl82c105_ide_init);

MODULE_DESCRIPTION("PCI driver module for W82C105 IDE");
MODULE_LICENSE("GPL");
