/*
 *  Atari Falcon IDE Driver
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

#define ATA_HD_CONTROL	0x39

    /*
     *  falconide_intr_lock is used to obtain access to the IDE interrupt,
     *  which is shared between several drivers.
     */

int falconide_intr_lock;
EXPORT_SYMBOL(falconide_intr_lock);

static void __init falconide_setup_ports(hw_regs_t *hw)
{
	int i;

	memset(hw, 0, sizeof(*hw));

	hw->io_ports[IDE_DATA_OFFSET] = ATA_HD_BASE;

	for (i = 1; i < 8; i++)
		hw->io_ports[i] = ATA_HD_BASE + 1 + i * 4;

	hw->io_ports[IDE_CONTROL_OFFSET] = ATA_HD_BASE + ATA_HD_CONTROL;

	hw->irq = IRQ_MFP_IDE;
	hw->ack_intr = NULL;
}

    /*
     *  Probe for a Falcon IDE interface
     */

static int __init falconide_init(void)
{
	hw_regs_t hw;
	ide_hwif_t *hwif;

	if (!MACH_IS_ATARI || !ATARIHW_PRESENT(IDE))
		return 0;

	printk(KERN_INFO "ide: Falcon IDE controller\n");

	falconide_setup_ports(&hw);

	hwif = ide_find_port(hw.io_ports[IDE_DATA_OFFSET]);
	if (hwif) {
		u8 index = hwif->index;
		u8 idx[4] = { index, 0xff, 0xff, 0xff };

		ide_init_port_data(hwif, index);
		ide_init_port_hw(hwif, &hw);

		ide_get_lock(NULL, NULL);
		ide_device_add(idx, NULL);
		ide_release_lock();
	}

	return 0;
}

module_init(falconide_init);
