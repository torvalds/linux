/*
 *  Amiga Gayle IDE Driver
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

#define GAYLE_CONTROL	0x101a

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

static void __init gayle_setup_ports(hw_regs_t *hw, unsigned long base,
				     unsigned long ctl, unsigned long irq_port,
				     ide_ack_intr_t *ack_intr);
{
	int i;

	memset(hw, 0, sizeof(*hw));

	hw->io_ports[IDE_DATA_OFFSET] = base;

	for (i = 1; i < 8; i++)
		hw->io_ports[i] = base + 2 + i * 4;

	hw->io_ports[IDE_CONTROL_OFFSET] = ctl;
	hw->io_ports[IDE_IRQ_OFFSET] = irq_port;

	hw->irq = IRQ_AMIGA_PORTS;
	hw->ack_intr = ack_intr;
}

    /*
     *  Probe for a Gayle IDE interface (and optionally for an IDE doubler)
     */

static int __init gayle_init(void)
{
    int a4000, i;
    u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

    if (!MACH_IS_AMIGA)
	return -ENODEV;

    if ((a4000 = AMIGAHW_PRESENT(A4000_IDE)) || AMIGAHW_PRESENT(A1200_IDE))
	goto found;

#ifdef CONFIG_ZORRO
    if (zorro_find_device(ZORRO_PROD_MTEC_VIPER_MK_V_E_MATRIX_530_SCSI_IDE,
			  NULL))
	goto found;
#endif
    return -ENODEV;

found:
	printk(KERN_INFO "ide: Gayle IDE controller (A%d style%s)\n",
			 a4000 ? 4000 : 1200,
#ifdef CONFIG_BLK_DEV_IDEDOUBLER
			 ide_doubler ? ", IDE doubler" :
#endif
			 "");

    for (i = 0; i < GAYLE_NUM_PROBE_HWIFS; i++) {
	unsigned long base, ctrlport, irqport;
	ide_ack_intr_t *ack_intr;
	hw_regs_t hw;
	ide_hwif_t *hwif;
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

	gayle_setup_ports(&hw, base, ctrlport, irqport, ack_intr);

	hwif = ide_find_port(base);
	if (hwif) {
	    u8 index = hwif->index;

	    ide_init_port_data(hwif, index);
	    ide_init_port_hw(hwif, &hw);

	    hwif->mmio = 1;

	    idx[i] = index;
	} else
	    release_mem_region(res_start, res_n);
    }

    ide_device_add(idx, NULL);

    return 0;
}

module_init(gayle_init);
