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
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/zorro.h>
#include <linux/module.h>

#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amigayle.h>


    /*
     *  Bases of the IDE interfaces
     */

#define GAYLE_BASE_4000	0xdd2020	/* A4000/A4000T */
#define GAYLE_BASE_1200	0xda0000	/* A1200/A600 and E-Matrix 530 */

#define GAYLE_IDEREG_SIZE	0x2000

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

#define GAYLE_NUM_HWIFS		2
#define GAYLE_NUM_PROBE_HWIFS	(ide_doubler ? GAYLE_NUM_HWIFS : \
					       GAYLE_NUM_HWIFS-1)
#define GAYLE_HAS_CONTROL_REG	(!ide_doubler)

static int ide_doubler;
module_param_named(doubler, ide_doubler, bool, 0);
MODULE_PARM_DESC(doubler, "enable support for IDE doublers");

    /*
     *  Check and acknowledge the interrupt status
     */

static int gayle_test_irq(ide_hwif_t *hwif)
{
    unsigned char ch;

    ch = z_readb(hwif->io_ports.irq_addr);
    if (!(ch & GAYLE_IRQ_IDE))
	return 0;
    return 1;
}

static void gayle_a1200_clear_irq(ide_drive_t *drive)
{
    ide_hwif_t *hwif = drive->hwif;

    (void)z_readb(hwif->io_ports.status_addr);
    z_writeb(0x7c, hwif->io_ports.irq_addr);
}

static void __init gayle_setup_ports(struct ide_hw *hw, unsigned long base,
				     unsigned long ctl, unsigned long irq_port)
{
	int i;

	memset(hw, 0, sizeof(*hw));

	hw->io_ports.data_addr = base;

	for (i = 1; i < 8; i++)
		hw->io_ports_array[i] = base + 2 + i * 4;

	hw->io_ports.ctl_addr = ctl;
	hw->io_ports.irq_addr = irq_port;

	hw->irq = IRQ_AMIGA_PORTS;
}

static const struct ide_port_ops gayle_a4000_port_ops = {
	.test_irq		= gayle_test_irq,
};

static const struct ide_port_ops gayle_a1200_port_ops = {
	.clear_irq		= gayle_a1200_clear_irq,
	.test_irq		= gayle_test_irq,
};

static const struct ide_port_info gayle_port_info = {
	.host_flags		= IDE_HFLAG_MMIO | IDE_HFLAG_SERIALIZE |
				  IDE_HFLAG_NO_DMA,
	.irq_flags		= IRQF_SHARED,
	.chipset		= ide_generic,
};

    /*
     *  Probe for a Gayle IDE interface (and optionally for an IDE doubler)
     */

static int __init gayle_init(void)
{
    unsigned long phys_base, res_start, res_n;
    unsigned long base, ctrlport, irqport;
    int a4000, i, rc;
    struct ide_hw hw[GAYLE_NUM_HWIFS], *hws[GAYLE_NUM_HWIFS];
    struct ide_port_info d = gayle_port_info;

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
			 ide_doubler ? ", IDE doubler" : "");

	if (a4000) {
	    phys_base = GAYLE_BASE_4000;
	    irqport = (unsigned long)ZTWO_VADDR(GAYLE_IRQ_4000);
	    d.port_ops = &gayle_a4000_port_ops;
	} else {
	    phys_base = GAYLE_BASE_1200;
	    irqport = (unsigned long)ZTWO_VADDR(GAYLE_IRQ_1200);
	    d.port_ops = &gayle_a1200_port_ops;
	}

	res_start = ((unsigned long)phys_base) & ~(GAYLE_NEXT_PORT-1);
	res_n = GAYLE_IDEREG_SIZE;

	if (!request_mem_region(res_start, res_n, "IDE"))
		return -EBUSY;

    for (i = 0; i < GAYLE_NUM_PROBE_HWIFS; i++) {
	base = (unsigned long)ZTWO_VADDR(phys_base + i * GAYLE_NEXT_PORT);
	ctrlport = GAYLE_HAS_CONTROL_REG ? (base + GAYLE_CONTROL) : 0;

	gayle_setup_ports(&hw[i], base, ctrlport, irqport);

	hws[i] = &hw[i];
    }

    rc = ide_host_add(&d, hws, i, NULL);
    if (rc)
	release_mem_region(res_start, res_n);

    return rc;
}

module_init(gayle_init);

MODULE_LICENSE("GPL");
