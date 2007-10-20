/*
 *  linux/drivers/ide/legacy/gayle.c -- Amiga Gayle IDE Driver
 *
 *     Created 9 Jul 1997 by Geert Uytterhoeven
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
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/zorro.h>

#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amigayle.h>


    /*
     *  Bases of the IDE interfaces
     */

#define GAYLE_BASE_4000	0xdd2020	/* A4000/A4000T */
#define GAYLE_BASE_1200	0xda0000	/* A1200/A600 and E-Matrix 530 */

    /*
     *  Offsets from one of the above bases
     */

#define GAYLE_DATA	0x00
#define GAYLE_ERROR	0x06		/* see err-bits */
#define GAYLE_NSECTOR	0x0a		/* nr of sectors to read/write */
#define GAYLE_SECTOR	0x0e		/* starting sector */
#define GAYLE_LCYL	0x12		/* starting cylinder */
#define GAYLE_HCYL	0x16		/* high byte of starting cyl */
#define GAYLE_SELECT	0x1a		/* 101dhhhh , d=drive, hhhh=head */
#define GAYLE_STATUS	0x1e		/* see status-bits */
#define GAYLE_CONTROL	0x101a

static int gayle_offsets[IDE_NR_PORTS] __initdata = {
    GAYLE_DATA, GAYLE_ERROR, GAYLE_NSECTOR, GAYLE_SECTOR, GAYLE_LCYL,
    GAYLE_HCYL, GAYLE_SELECT, GAYLE_STATUS, -1, -1
};


    /*
     *  These are at different offsets from the base
     */

#define GAYLE_IRQ_4000	0xdd3020	/* MSB = 1, Harddisk is source of */
#define GAYLE_IRQ_1200	0xda9000	/* interrupt */


    /*
     *  Offset of the secondary port for IDE doublers
     *  Note that GAYLE_CONTROL is NOT available then!
     */

#define GAYLE_NEXT_PORT	0x1000

#ifndef CONFIG_BLK_DEV_IDEDOUBLER
#define GAYLE_NUM_HWIFS		1
#define GAYLE_NUM_PROBE_HWIFS	GAYLE_NUM_HWIFS
#define GAYLE_HAS_CONTROL_REG	1
#define GAYLE_IDEREG_SIZE	0x2000
#else /* CONFIG_BLK_DEV_IDEDOUBLER */
#define GAYLE_NUM_HWIFS		2
#define GAYLE_NUM_PROBE_HWIFS	(ide_doubler ? GAYLE_NUM_HWIFS : \
					       GAYLE_NUM_HWIFS-1)
#define GAYLE_HAS_CONTROL_REG	(!ide_doubler)
#define GAYLE_IDEREG_SIZE	(ide_doubler ? 0x1000 : 0x2000)
int ide_doubler = 0;	/* support IDE doublers? */
#endif /* CONFIG_BLK_DEV_IDEDOUBLER */


    /*
     *  Check and acknowledge the interrupt status
     */

static int gayle_ack_intr_a4000(ide_hwif_t *hwif)
{
    unsigned char ch;

    ch = z_readb(hwif->io_ports[IDE_IRQ_OFFSET]);
    if (!(ch & GAYLE_IRQ_IDE))
	return 0;
    return 1;
}

static int gayle_ack_intr_a1200(ide_hwif_t *hwif)
{
    unsigned char ch;

    ch = z_readb(hwif->io_ports[IDE_IRQ_OFFSET]);
    if (!(ch & GAYLE_IRQ_IDE))
	return 0;
    (void)z_readb(hwif->io_ports[IDE_STATUS_OFFSET]);
    z_writeb(0x7c, hwif->io_ports[IDE_IRQ_OFFSET]);
    return 1;
}

    /*
     *  Probe for a Gayle IDE interface (and optionally for an IDE doubler)
     */

void __init gayle_init(void)
{
    int a4000, i;

    if (!MACH_IS_AMIGA)
	return;

    if ((a4000 = AMIGAHW_PRESENT(A4000_IDE)) || AMIGAHW_PRESENT(A1200_IDE))
	goto found;

#ifdef CONFIG_ZORRO
    if (zorro_find_device(ZORRO_PROD_MTEC_VIPER_MK_V_E_MATRIX_530_SCSI_IDE,
			  NULL))
	goto found;
#endif
    return;

found:
    for (i = 0; i < GAYLE_NUM_PROBE_HWIFS; i++) {
	unsigned long base, ctrlport, irqport;
	ide_ack_intr_t *ack_intr;
	hw_regs_t hw;
	ide_hwif_t *hwif;
	int index;
	unsigned long phys_base, res_start, res_n;

	if (a4000) {
	    phys_base = GAYLE_BASE_4000;
	    irqport = (unsigned long)ZTWO_VADDR(GAYLE_IRQ_4000);
	    ack_intr = gayle_ack_intr_a4000;
	} else {
	    phys_base = GAYLE_BASE_1200;
	    irqport = (unsigned long)ZTWO_VADDR(GAYLE_IRQ_1200);
	    ack_intr = gayle_ack_intr_a1200;
	}
/*
 * FIXME: we now have selectable modes between mmio v/s iomio
 */

	phys_base += i*GAYLE_NEXT_PORT;

	res_start = ((unsigned long)phys_base) & ~(GAYLE_NEXT_PORT-1);
	res_n = GAYLE_IDEREG_SIZE;

	if (!request_mem_region(res_start, res_n, "IDE"))
	    continue;

	base = (unsigned long)ZTWO_VADDR(phys_base);
	ctrlport = GAYLE_HAS_CONTROL_REG ? (base + GAYLE_CONTROL) : 0;

	ide_setup_ports(&hw, base, gayle_offsets,
			ctrlport, irqport, ack_intr,
//			&gayle_iops,
			IRQ_AMIGA_PORTS);

	index = ide_register_hw(&hw, NULL, 1, &hwif);
	if (index != -1) {
	    hwif->mmio = 1;
	    switch (i) {
		case 0:
		    printk("ide%d: Gayle IDE interface (A%d style)\n", index,
			   a4000 ? 4000 : 1200);
		    break;
#ifdef CONFIG_BLK_DEV_IDEDOUBLER
		case 1:
		    printk("ide%d: IDE doubler\n", index);
		    break;
#endif /* CONFIG_BLK_DEV_IDEDOUBLER */
	    }
	} else
	    release_mem_region(res_start, res_n);
    }
}
