/*
 *  linux/drivers/ide/legacy/macide.c -- Macintosh IDE Driver
 *
 *     Copyright (C) 1998 by Michael Schmitz
 *
 *  This driver was written based on information obtained from the MacOS IDE
 *  driver binary by Mikael Forselius
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/machw.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_baboon.h>

#define IDE_BASE 0x50F1A000	/* Base address of IDE controller */

/*
 * Generic IDE registers as offsets from the base
 * These match MkLinux so they should be correct.
 */

#define IDE_DATA	0x00
#define IDE_ERROR	0x04	/* see err-bits */
#define IDE_NSECTOR	0x08	/* nr of sectors to read/write */
#define IDE_SECTOR	0x0c	/* starting sector */
#define IDE_LCYL	0x10	/* starting cylinder */
#define IDE_HCYL	0x14	/* high byte of starting cyl */
#define IDE_SELECT	0x18	/* 101dhhhh , d=drive, hhhh=head */
#define IDE_STATUS	0x1c	/* see status-bits */
#define IDE_CONTROL	0x38	/* control/altstatus */

/*
 * Mac-specific registers
 */

/*
 * this register is odd; it doesn't seem to do much and it's
 * not word-aligned like virtually every other hardware register
 * on the Mac...
 */

#define IDE_IFR		0x101	/* (0x101) IDE interrupt flags on Quadra:
				 *
				 * Bit 0+1: some interrupt flags
				 * Bit 2+3: some interrupt enable
				 * Bit 4:   ??
				 * Bit 5:   IDE interrupt flag (any hwif)
				 * Bit 6:   maybe IDE interrupt enable (any hwif) ??
				 * Bit 7:   Any interrupt condition
				 */

volatile unsigned char *ide_ifr = (unsigned char *) (IDE_BASE + IDE_IFR);

static int macide_offsets[IDE_NR_PORTS] = {
    IDE_DATA, IDE_ERROR,  IDE_NSECTOR, IDE_SECTOR, IDE_LCYL,
    IDE_HCYL, IDE_SELECT, IDE_STATUS,  IDE_CONTROL
};

int macide_ack_intr(ide_hwif_t* hwif)
{
	if (*ide_ifr & 0x20) {
		*ide_ifr &= ~0x20;
		return 1;
	}
	return 0;
}

#ifdef CONFIG_BLK_DEV_MAC_MEDIABAY
static void macide_mediabay_interrupt(int irq, void *dev_id)
{
	int state = baboon->mb_status & 0x04;

	printk(KERN_INFO "macide: media bay %s detected\n", state? "removal":"insertion");
}
#endif

/*
 * Probe for a Macintosh IDE interface
 */

void macide_init(void)
{
	hw_regs_t hw;
	ide_hwif_t *hwif;
	int index = -1;

	switch (macintosh_config->ide_type) {
	case MAC_IDE_QUADRA:
		ide_setup_ports(&hw, IDE_BASE, macide_offsets,
				0, 0, macide_ack_intr,
//				quadra_ide_iops,
				IRQ_NUBUS_F);
		index = ide_register_hw(&hw, 1, &hwif);
		break;
	case MAC_IDE_PB:
		ide_setup_ports(&hw, IDE_BASE, macide_offsets,
				0, 0, macide_ack_intr,
//				macide_pb_iops,
				IRQ_NUBUS_C);
		index = ide_register_hw(&hw, 1, &hwif);
		break;
	case MAC_IDE_BABOON:
		ide_setup_ports(&hw, BABOON_BASE, macide_offsets,
				0, 0, NULL,
//				macide_baboon_iops,
				IRQ_BABOON_1);
		index = ide_register_hw(&hw, 1, &hwif);
		if (index == -1) break;
		if (macintosh_config->ident == MAC_MODEL_PB190) {

			/* Fix breakage in ide-disk.c: drive capacity	*/
			/* is not initialized for drives without a 	*/
			/* hardware ID, and we can't get that without	*/
			/* probing the drive which freezes a 190.	*/

			ide_drive_t *drive = &ide_hwifs[index].drives[0];
			drive->capacity64 = drive->cyl*drive->head*drive->sect;

#ifdef CONFIG_BLK_DEV_MAC_MEDIABAY
			request_irq(IRQ_BABOON_2, macide_mediabay_interrupt,
					IRQ_FLG_FAST, "mediabay",
					macide_mediabay_interrupt);
#endif
		}
		break;

	default:
	    return;
	}

        if (index != -1) {
		hwif->mmio = 1;
		if (macintosh_config->ide_type == MAC_IDE_QUADRA)
			printk(KERN_INFO "ide%d: Macintosh Quadra IDE interface\n", index);
		else if (macintosh_config->ide_type == MAC_IDE_PB)
			printk(KERN_INFO "ide%d: Macintosh Powerbook IDE interface\n", index);
		else if (macintosh_config->ide_type == MAC_IDE_BABOON)
			printk(KERN_INFO "ide%d: Macintosh Powerbook Baboon IDE interface\n", index);
		else
			printk(KERN_INFO "ide%d: Unknown Macintosh IDE interface\n", index);
	}
}
