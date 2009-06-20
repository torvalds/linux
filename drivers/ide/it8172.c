/*
 *
 * BRIEF MODULE DESCRIPTION
 *      IT8172 IDE controller support
 *
 * Copyright (C) 2000 MontaVista Software Inc.
 * Copyright (C) 2008 Shane McDonald
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

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#define DRV_NAME "IT8172"

static void it8172_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u16 drive_enables;
	u32 drive_timing;

	/*
	 * The highest value of DIOR/DIOW pulse width and recovery time
	 * that can be set in the IT8172 is 8 PCI clock cycles.  As a result,
	 * it cannot be configured for PIO mode 0.  This table sets these
	 * parameters to the maximum supported by the IT8172.
	 */
	static const u8 timings[] = { 0x3f, 0x3c, 0x1b, 0x12, 0x0a };

	pci_read_config_word(dev, 0x40, &drive_enables);
	pci_read_config_dword(dev, 0x44, &drive_timing);

	/*
	 * Enable port 0x44. The IT8172 spec is confused; it calls
	 * this register the "Slave IDE Timing Register", but in fact,
	 * it controls timing for both master and slave drives.
	 */
	drive_enables |= 0x4000;

	drive_enables &= drive->dn ? 0xc006 : 0xc060;
	if (drive->media == ide_disk)
		/* enable prefetch */
		drive_enables |= 0x0004 << (drive->dn * 4);
	if (ide_pio_need_iordy(drive, pio))
		/* enable IORDY sample-point */
		drive_enables |= 0x0002 << (drive->dn * 4);

	drive_timing &= drive->dn ? 0x00003f00 : 0x000fc000;
	drive_timing |= timings[pio] << (drive->dn * 6 + 8);

	pci_write_config_word(dev, 0x40, drive_enables);
	pci_write_config_dword(dev, 0x44, drive_timing);
}

static void it8172_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	int a_speed		= 3 << (drive->dn * 4);
	int u_flag		= 1 << drive->dn;
	int u_speed		= 0;
	u8 reg48, reg4a;

	pci_read_config_byte(dev, 0x48, &reg48);
	pci_read_config_byte(dev, 0x4a, &reg4a);

	if (speed >= XFER_UDMA_0) {
		u8 udma = speed - XFER_UDMA_0;
		u_speed = udma << (drive->dn * 4);

		pci_write_config_byte(dev, 0x48, reg48 | u_flag);
		reg4a &= ~a_speed;
		pci_write_config_byte(dev, 0x4a, reg4a | u_speed);
	} else {
		const u8 mwdma_to_pio[] = { 0, 3, 4 };
		u8 pio;

		pci_write_config_byte(dev, 0x48, reg48 & ~u_flag);
		pci_write_config_byte(dev, 0x4a, reg4a & ~a_speed);

		pio = mwdma_to_pio[speed - XFER_MW_DMA_0];

		it8172_set_pio_mode(drive, pio);
	}
}


static const struct ide_port_ops it8172_port_ops = {
	.set_pio_mode	= it8172_set_pio_mode,
	.set_dma_mode	= it8172_set_dma_mode,
};

static const struct ide_port_info it8172_port_info __devinitdata = {
	.name		= DRV_NAME,
	.port_ops	= &it8172_port_ops,
	.enablebits	= { {0x41, 0x80, 0x80}, {0x00, 0x00, 0x00} },
	.host_flags	= IDE_HFLAG_SINGLE,
	.pio_mask	= ATA_PIO4 & ~ATA_PIO0,
	.mwdma_mask	= ATA_MWDMA2,
	.udma_mask	= ATA_UDMA2,
};

static int __devinit it8172_init_one(struct pci_dev *dev,
					const struct pci_device_id *id)
{
	if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return -ENODEV; /* IT8172 is more than an IDE controller */
	return ide_pci_init_one(dev, &it8172_port_info, NULL);
}

static struct pci_device_id it8172_pci_tbl[] = {
	{ PCI_VDEVICE(ITE, PCI_DEVICE_ID_ITE_8172), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, it8172_pci_tbl);

static struct pci_driver it8172_pci_driver = {
	.name		= "IT8172_IDE",
	.id_table	= it8172_pci_tbl,
	.probe		= it8172_init_one,
	.remove		= ide_pci_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init it8172_ide_init(void)
{
	return ide_pci_register_driver(&it8172_pci_driver);
}

static void __exit it8172_ide_exit(void)
{
	pci_unregister_driver(&it8172_pci_driver);
}

module_init(it8172_ide_init);
module_exit(it8172_ide_exit);

MODULE_AUTHOR("Steve Longerbeam");
MODULE_DESCRIPTION("PCI driver module for ITE 8172 IDE");
MODULE_LICENSE("GPL");
