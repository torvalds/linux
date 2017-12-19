/*
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
 * Copyright (C) 2006-2007,2009 MontaVista Software, Inc. <source@mvista.com>
 * Copyright (C)      2007 Bartlomiej Zolnierkiewicz
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>

#define DRV_NAME "sl82c105"

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
	struct ide_timing *t = ide_timing_find_mode(XFER_PIO_0 + pio);
	unsigned int cmd_on, cmd_off;
	u8 iordy = 0;

	cmd_on  = (t->active + 29) / 30;
	cmd_off = (ide_pio_cycle_time(drive, pio) - 30 * cmd_on + 29) / 30;

	if (cmd_on == 0)
		cmd_on = 1;

	if (cmd_off == 0)
		cmd_off = 1;

	if (ide_pio_need_iordy(drive, pio))
		iordy = 0x40;

	return (cmd_on - 1) << 8 | (cmd_off - 1) | iordy;
}

/*
 * Configure the chipset for PIO mode.
 */
static void sl82c105_set_pio_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	unsigned long timings	= (unsigned long)ide_get_drivedata(drive);
	int reg			= 0x44 + drive->dn * 4;
	u16 drv_ctrl;
	const u8 pio		= drive->pio_mode - XFER_PIO_0;

	drv_ctrl = get_pio_timings(drive, pio);

	/*
	 * Store the PIO timings so that we can restore them
	 * in case DMA will be turned off...
	 */
	timings &= 0xffff0000;
	timings |= drv_ctrl;
	ide_set_drivedata(drive, (void *)timings);

	pci_write_config_word(dev, reg,  drv_ctrl);
	pci_read_config_word (dev, reg, &drv_ctrl);

	printk(KERN_DEBUG "%s: selected %s (%dns) (%04X)\n", drive->name,
			  ide_xfer_verbose(pio + XFER_PIO_0),
			  ide_pio_cycle_time(drive, pio), drv_ctrl);
}

/*
 * Configure the chipset for DMA mode.
 */
static void sl82c105_set_dma_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	static u16 mwdma_timings[] = {0x0707, 0x0201, 0x0200};
	unsigned long timings = (unsigned long)ide_get_drivedata(drive);
	u16 drv_ctrl;
	const u8 speed = drive->dma_mode;

	drv_ctrl = mwdma_timings[speed - XFER_MW_DMA_0];

	/*
	 * Store the DMA timings so that we can actually program
	 * them when DMA will be turned on...
	 */
	timings &= 0x0000ffff;
	timings |= (unsigned long)drv_ctrl << 16;
	ide_set_drivedata(drive, (void *)timings);
}

static int sl82c105_test_irq(ide_hwif_t *hwif)
{
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u32 val, mask		= hwif->channel ? CTRL_IDE_IRQB : CTRL_IDE_IRQA;

	pci_read_config_dword(dev, 0x40, &val);

	return (val & mask) ? 1 : 0;
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
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u32 val, mask		= hwif->channel ? CTRL_IDE_IRQB : CTRL_IDE_IRQA;
	u8 dma_cmd;

	printk(KERN_WARNING "sl82c105: lost IRQ, resetting host\n");

	/*
	 * Check the raw interrupt from the drive.
	 */
	pci_read_config_dword(dev, 0x40, &val);
	if (val & mask)
		printk(KERN_INFO "sl82c105: drive was requesting IRQ, "
		       "but host lost it\n");

	/*
	 * Was DMA enabled?  If so, disable it - we're resetting the
	 * host.  The IDE layer will be handling the drive for us.
	 */
	dma_cmd = inb(hwif->dma_base + ATA_DMA_CMD);
	if (dma_cmd & 1) {
		outb(dma_cmd & ~1, hwif->dma_base + ATA_DMA_CMD);
		printk(KERN_INFO "sl82c105: DMA was enabled\n");
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
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	int reg 		= 0x44 + drive->dn * 4;

	pci_write_config_word(dev, reg,
			      (unsigned long)ide_get_drivedata(drive) >> 16);

	sl82c105_reset_host(dev);
	ide_dma_start(drive);
}

static void sl82c105_dma_clear(ide_drive_t *drive)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);

	sl82c105_reset_host(dev);
}

static int sl82c105_dma_end(ide_drive_t *drive)
{
	struct pci_dev *dev	= to_pci_dev(drive->hwif->dev);
	int reg 		= 0x44 + drive->dn * 4;
	int ret			= ide_dma_end(drive);

	pci_write_config_word(dev, reg,
			      (unsigned long)ide_get_drivedata(drive));

	return ret;
}

/*
 * ATA reset will clear the 16 bits mode in the control
 * register, we need to reprogram it
 */
static void sl82c105_resetproc(ide_drive_t *drive)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u32 val;

	pci_read_config_dword(dev, 0x40, &val);
	val |= (CTRL_P1F16 | CTRL_P0F16);
	pci_write_config_dword(dev, 0x40, val);
}

/*
 * Return the revision of the Winbond bridge
 * which this function is part of.
 */
static u8 sl82c105_bridge_revision(struct pci_dev *dev)
{
	struct pci_dev *bridge;

	/*
	 * The bridge should be part of the same device, but function 0.
	 */
	bridge = pci_get_domain_bus_and_slot(pci_domain_nr(dev->bus),
					dev->bus->number,
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
static int init_chipset_sl82c105(struct pci_dev *dev)
{
	u32 val;

	pci_read_config_dword(dev, 0x40, &val);
	val |= CTRL_P0EN | CTRL_P0F16 | CTRL_P1F16;
	pci_write_config_dword(dev, 0x40, val);

	return 0;
}

static const struct ide_port_ops sl82c105_port_ops = {
	.set_pio_mode		= sl82c105_set_pio_mode,
	.set_dma_mode		= sl82c105_set_dma_mode,
	.resetproc		= sl82c105_resetproc,
	.test_irq		= sl82c105_test_irq,
};

static const struct ide_dma_ops sl82c105_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_start		= sl82c105_dma_start,
	.dma_end		= sl82c105_dma_end,
	.dma_test_irq		= ide_dma_test_irq,
	.dma_lost_irq		= sl82c105_dma_lost_irq,
	.dma_timer_expiry	= ide_dma_sff_timer_expiry,
	.dma_clear		= sl82c105_dma_clear,
	.dma_sff_read_status	= ide_dma_sff_read_status,
};

static const struct ide_port_info sl82c105_chipset = {
	.name		= DRV_NAME,
	.init_chipset	= init_chipset_sl82c105,
	.enablebits	= {{0x40,0x01,0x01}, {0x40,0x10,0x10}},
	.port_ops	= &sl82c105_port_ops,
	.dma_ops	= &sl82c105_dma_ops,
	.host_flags	= IDE_HFLAG_IO_32BIT |
			  IDE_HFLAG_UNMASK_IRQS |
			  IDE_HFLAG_SERIALIZE_DMA |
			  IDE_HFLAG_NO_AUTODMA,
	.pio_mask	= ATA_PIO5,
	.mwdma_mask	= ATA_MWDMA2,
};

static int sl82c105_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct ide_port_info d = sl82c105_chipset;
	u8 rev = sl82c105_bridge_revision(dev);

	if (rev <= 5) {
		/*
		 * Never ever EVER under any circumstances enable
		 * DMA when the bridge is this old.
		 */
		printk(KERN_INFO DRV_NAME ": Winbond W83C553 bridge "
				 "revision %d, BM-DMA disabled\n", rev);
		d.dma_ops = NULL;
		d.mwdma_mask = 0;
		d.host_flags &= ~IDE_HFLAG_SERIALIZE_DMA;
	}

	return ide_pci_init_one(dev, &d, NULL);
}

static const struct pci_device_id sl82c105_pci_tbl[] = {
	{ PCI_VDEVICE(WINBOND, PCI_DEVICE_ID_WINBOND_82C105), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, sl82c105_pci_tbl);

static struct pci_driver sl82c105_pci_driver = {
	.name		= "W82C105_IDE",
	.id_table	= sl82c105_pci_tbl,
	.probe		= sl82c105_init_one,
	.remove		= ide_pci_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init sl82c105_ide_init(void)
{
	return ide_pci_register_driver(&sl82c105_pci_driver);
}

static void __exit sl82c105_ide_exit(void)
{
	pci_unregister_driver(&sl82c105_pci_driver);
}

module_init(sl82c105_ide_init);
module_exit(sl82c105_ide_exit);

MODULE_DESCRIPTION("PCI driver module for W82C105 IDE");
MODULE_LICENSE("GPL");
