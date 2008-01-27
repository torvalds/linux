/*
 *  linux/drivers/ide/legacy/falconide.c -- Atari Falcon IDE Driver
 *
 *     Created 12 Jul 1997 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>


    /*
     *  Base of the IDE interface
     */

#define ATA_HD_BASE	0xfff00000

    /*
     *  Offsets from the above base
     */

#define ATA_HD_DATA	0x00
#define ATA_HD_ERROR	0x05		/* see err-bits */
#define ATA_HD_NSECTOR	0x09		/* nr of sectors to read/write */
#define ATA_HD_SECTOR	0x0d		/* starting sector */
#define ATA_HD_LCYL	0x11		/* starting cylinder */
#define ATA_HD_HCYL	0x15		/* high byte of starting cyl */
#define ATA_HD_SELECT	0x19		/* 101dhhhh , d=drive, hhhh=head */
#define ATA_HD_STATUS	0x1d		/* see status-bits */
#define ATA_HD_CONTROL	0x39

static int falconide_offsets[IDE_NR_PORTS] __initdata = {
    ATA_HD_DATA, ATA_HD_ERROR, ATA_HD_NSECTOR, ATA_HD_SECTOR, ATA_HD_LCYL,
    ATA_HD_HCYL, ATA_HD_SELECT, ATA_HD_STATUS, ATA_HD_CONTROL, -1
};


    /*
     *  falconide_intr_lock is used to obtain access to the IDE interrupt,
     *  which is shared between several drivers.
     */

int falconide_intr_lock;
EXPORT_SYMBOL(falconide_intr_lock);


    /*
     *  Probe for a Falcon IDE interface
     */

static int __init falconide_init(void)
{
    if (MACH_IS_ATARI && ATARIHW_PRESENT(IDE)) {
	hw_regs_t hw;

	printk(KERN_INFO "ide: Falcon IDE controller\n");

	ide_setup_ports(&hw, ATA_HD_BASE, falconide_offsets,
			0, 0, NULL,
//			falconide_iops,
			IRQ_MFP_IDE);

	hwif = ide_find_port(hw.io_ports[IDE_DATA_OFFSET]);
	if (hwif) {
		u8 index = hwif->index;
		u8 idx[4] = { index, 0xff, 0xff, 0xff };

		ide_init_port_data(hwif, index);
		ide_init_port_hw(hwif, &hw);

		ide_device_add(idx);
	}
    }

    return 0;
}

module_init(falconide_init);
