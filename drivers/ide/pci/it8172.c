/*
 *
 * BRIEF MODULE DESCRIPTION
 *      IT8172 IDE controller support
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/it8172/it8172_int.h>

/*
 * Prototypes
 */
static u8 it8172_ratemask (ide_drive_t *drive)
{
	return 1;
}

static void it8172_tune_drive (ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= (&hwif->drives[1] == drive);
	unsigned long flags;
	u16 drive_enables;
	u32 drive_timing;

	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	spin_lock_irqsave(&ide_lock, flags);
	pci_read_config_word(dev, 0x40, &drive_enables);
	pci_read_config_dword(dev, 0x44, &drive_timing);

	/*
	 * FIX! The DIOR/DIOW pulse width and recovery times in port 0x44
	 * are being left at the default values of 8 PCI clocks (242 nsec
	 * for a 33 MHz clock). These can be safely shortened at higher
	 * PIO modes. The DIOR/DIOW pulse width and recovery times only
	 * apply to PIO modes, not to the DMA modes.
	 */

	/*
	 * Enable port 0x44. The IT8172G spec is confused; it calls
	 * this register the "Slave IDE Timing Register", but in fact,
	 * it controls timing for both master and slave drives.
	 */
	drive_enables |= 0x4000;

	if (is_slave) {
		drive_enables &= 0xc006;
		if (pio > 1)
			/* enable prefetch and IORDY sample-point */
			drive_enables |= 0x0060;
	} else {
		drive_enables &= 0xc060;
		if (pio > 1)
			/* enable prefetch and IORDY sample-point */
			drive_enables |= 0x0006;
	}

	pci_write_config_word(dev, 0x40, drive_enables);
	spin_unlock_irqrestore(&ide_lock, flags);
}

static u8 it8172_dma_2_pio (u8 xfer_rate)
{
	switch(xfer_rate) {
		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
		case XFER_MW_DMA_2:
		case XFER_PIO_4:
			return 4;
		case XFER_MW_DMA_1:
		case XFER_PIO_3:
			return 3;
		case XFER_SW_DMA_2:
		case XFER_PIO_2:
			return 2;
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
		case XFER_PIO_1:
		case XFER_PIO_0:
		case XFER_PIO_SLOW:
		default:
			return 0;
	}
}

static int it8172_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 speed	= ide_rate_filter(it8172_ratemask(drive), xferspeed);
	int a_speed		= 3 << (drive->dn * 4);
	int u_flag		= 1 << drive->dn;
	int u_speed		= 0;
	u8 reg48, reg4a;

	pci_read_config_byte(dev, 0x48, &reg48);
	pci_read_config_byte(dev, 0x4a, &reg4a);

    /*
     * Setting the DMA cycle time to 2 or 3 PCI clocks (60 and 91 nsec
     * at 33 MHz PCI clock) seems to cause BadCRC errors during DMA
     * transfers on some drives, even though both numbers meet the minimum
     * ATAPI-4 spec of 73 and 54 nsec for UDMA 1 and 2 respectively.
     * So the faster times are just commented out here. The good news is
     * that the slower cycle time has very little affect on transfer
     * performance.
     */
    
	switch(speed) {
		case XFER_UDMA_4:
		case XFER_UDMA_2:	//u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_5:
		case XFER_UDMA_3:
		case XFER_UDMA_1:	//u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive->dn * 4); break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:	break;
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_0:	break;
		default:		return -1;
	}

	if (speed >= XFER_UDMA_0) {
		pci_write_config_byte(dev, 0x48, reg48 | u_flag);
		reg4a &= ~a_speed;
		pci_write_config_byte(dev, 0x4a, reg4a | u_speed);
	} else {
		pci_write_config_byte(dev, 0x48, reg48 & ~u_flag);
		pci_write_config_byte(dev, 0x4a, reg4a & ~a_speed);
	}

	it8172_tune_drive(drive, it8172_dma_2_pio(speed));
	return (ide_config_drive_speed(drive, speed));
}

static int it8172_config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, it8172_ratemask(drive));

	if (!(speed)) {
		u8 tspeed = ide_get_best_pio_mode(drive, 255, 4, NULL);
		speed = it8172_dma_2_pio(XFER_PIO_0 + tspeed);
	}

	(void) it8172_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

static int it8172_config_drive_xfer_rate (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	drive->init_speed = 0;

	if (id && (id->capability & 1) && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (it8172_config_chipset_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		it8172_tune_drive(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
}

static unsigned int __init init_chipset_it8172 (struct pci_dev *dev, const char *name)
{
	unsigned char progif;
    
	/*
	 * Place both IDE interfaces into PCI "native" mode
	 */
	pci_read_config_byte(dev, PCI_CLASS_PROG, &progif);
	pci_write_config_byte(dev, PCI_CLASS_PROG, progif | 0x05);    

	return IT8172_IDE_IRQ;
}


static void __init init_hwif_it8172 (ide_hwif_t *hwif)
{
	struct pci_dev* dev = hwif->pci_dev;
	unsigned long cmdBase, ctrlBase;
    
	hwif->autodma = 0;
	hwif->tuneproc = &it8172_tune_drive;
	hwif->speedproc = &it8172_tune_chipset;

	cmdBase = dev->resource[0].start;
	ctrlBase = dev->resource[1].start;
    
	ide_init_hwif_ports(&hwif->hw, cmdBase, ctrlBase | 2, NULL);
	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
	hwif->noprobe = 0;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x07;
	hwif->mwdma_mask = 0x06;
	hwif->swdma_mask = 0x04;

	hwif->ide_dma_check = &it8172_config_drive_xfer_rate;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static ide_pci_device_t it8172_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "IT8172G",
		.init_chipset	= init_chipset_it8172,
		.init_hwif	= init_hwif_it8172,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x00,0x00,0x00}, {0x40,0x00,0x01}},
		.bootable	= ON_BOARD,
	}
};

static int __devinit it8172_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
        if ((!(PCI_FUNC(dev->devfn) & 1) ||
            (!((dev->class >> 8) == PCI_CLASS_STORAGE_IDE))))
		return -ENODEV; /* IT8172 is more than an IDE controller */
	return ide_setup_pci_device(dev, &it8172_chipsets[id->driver_data]);
}

static struct pci_device_id it8172_pci_tbl[] = {
	{ PCI_VENDOR_ID_ITE, PCI_DEVICE_ID_ITE_IT8172G, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, it8172_pci_tbl);

static struct pci_driver driver = {
	.name		= "IT8172_IDE",
	.id_table	= it8172_pci_tbl,
	.probe		= it8172_init_one,
};

static int it8172_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(it8172_ide_init);

MODULE_AUTHOR("SteveL@mvista.com");
MODULE_DESCRIPTION("PCI driver module for ITE 8172 IDE");
MODULE_LICENSE("GPL");
