/*
 *  Amiga Buddha, Catweasel and X-Surf IDE Driver
 *
 *	Copyright (C) 1997, 2001 by Geert Uytterhoeven and others
 *
 *  This driver was written based on the specifications in README.buddha and
 *  the X-Surf info from Inside_XSurf.txt available at
 *  http://www.jschoenfeld.com
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *  TODO:
 *    - test it :-)
 *    - tune the timings using the speed-register
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/zorro.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/amigahw.h>
#include <asm/amigaints.h>


    /*
     *  The Buddha has 2 IDE interfaces, the Catweasel has 3, X-Surf has 2
     */

#define BUDDHA_NUM_HWIFS	2
#define CATWEASEL_NUM_HWIFS	3
#define XSURF_NUM_HWIFS         2

#define MAX_NUM_HWIFS		3

    /*
     *  Bases of the IDE interfaces (relative to the board address)
     */

#define BUDDHA_BASE1	0x800
#define BUDDHA_BASE2	0xa00
#define BUDDHA_BASE3	0xc00

#define XSURF_BASE1     0xb000 /* 2.5" Interface */
#define XSURF_BASE2     0xd000 /* 3.5" Interface */

static u_int buddha_bases[CATWEASEL_NUM_HWIFS] __initdata = {
    BUDDHA_BASE1, BUDDHA_BASE2, BUDDHA_BASE3
};

static u_int xsurf_bases[XSURF_NUM_HWIFS] __initdata = {
     XSURF_BASE1, XSURF_BASE2
};

    /*
     *  Offsets from one of the above bases
     */

#define BUDDHA_CONTROL	0x11a

    /*
     *  Other registers
     */

#define BUDDHA_IRQ1	0xf00		/* MSB = 1, Harddisk is source of */
#define BUDDHA_IRQ2	0xf40		/* interrupt */
#define BUDDHA_IRQ3	0xf80

#define XSURF_IRQ1      0x7e
#define XSURF_IRQ2      0x7e

static int buddha_irqports[CATWEASEL_NUM_HWIFS] __initdata = {
    BUDDHA_IRQ1, BUDDHA_IRQ2, BUDDHA_IRQ3
};

static int xsurf_irqports[XSURF_NUM_HWIFS] __initdata = {
    XSURF_IRQ1, XSURF_IRQ2
};

#define BUDDHA_IRQ_MR	0xfc0		/* master interrupt enable */


    /*
     *  Board information
     */

typedef enum BuddhaType_Enum {
    BOARD_BUDDHA, BOARD_CATWEASEL, BOARD_XSURF
} BuddhaType;

static const char *buddha_board_name[] = { "Buddha", "Catweasel", "X-Surf" };

    /*
     *  Check and acknowledge the interrupt status
     */

static int buddha_test_irq(ide_hwif_t *hwif)
{
    unsigned char ch;

    ch = z_readb(hwif->io_ports.irq_addr);
    if (!(ch & 0x80))
	    return 0;
    return 1;
}

static void xsurf_clear_irq(ide_drive_t *drive)
{
    /*
     * X-Surf needs 0 written to IRQ register to ensure ISA bit A11 stays at 0
     */
    z_writeb(0, drive->hwif->io_ports.irq_addr);
}

static void __init buddha_setup_ports(struct ide_hw *hw, unsigned long base,
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

static const struct ide_port_ops buddha_port_ops = {
	.test_irq		= buddha_test_irq,
};

static const struct ide_port_ops xsurf_port_ops = {
	.clear_irq		= xsurf_clear_irq,
	.test_irq		= buddha_test_irq,
};

static const struct ide_port_info buddha_port_info = {
	.port_ops		= &buddha_port_ops,
	.host_flags		= IDE_HFLAG_MMIO | IDE_HFLAG_NO_DMA,
	.irq_flags		= IRQF_SHARED,
	.chipset		= ide_generic,
};

    /*
     *  Probe for a Buddha or Catweasel IDE interface
     */

static int __init buddha_init(void)
{
	struct zorro_dev *z = NULL;
	u_long buddha_board = 0;
	BuddhaType type;
	int buddha_num_hwifs, i;

	while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
		unsigned long board;
		struct ide_hw hw[MAX_NUM_HWIFS], *hws[MAX_NUM_HWIFS];
		struct ide_port_info d = buddha_port_info;

		if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_BUDDHA) {
			buddha_num_hwifs = BUDDHA_NUM_HWIFS;
			type=BOARD_BUDDHA;
		} else if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_CATWEASEL) {
			buddha_num_hwifs = CATWEASEL_NUM_HWIFS;
			type=BOARD_CATWEASEL;
		} else if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_X_SURF) {
			buddha_num_hwifs = XSURF_NUM_HWIFS;
			type=BOARD_XSURF;
			d.port_ops = &xsurf_port_ops;
		} else 
			continue;
		
		board = z->resource.start;

		if(type != BOARD_XSURF) {
			if (!request_mem_region(board+BUDDHA_BASE1, 0x800, "IDE"))
				continue;
		} else {
			if (!request_mem_region(board+XSURF_BASE1, 0x1000, "IDE"))
				continue;
			if (!request_mem_region(board+XSURF_BASE2, 0x1000, "IDE"))
				goto fail_base2;
			if (!request_mem_region(board+XSURF_IRQ1, 0x8, "IDE")) {
				release_mem_region(board+XSURF_BASE2, 0x1000);
fail_base2:
				release_mem_region(board+XSURF_BASE1, 0x1000);
				continue;
			}
		}	  
		buddha_board = (unsigned long)ZTWO_VADDR(board);
		
		/* write to BUDDHA_IRQ_MR to enable the board IRQ */
		/* X-Surf doesn't have this.  IRQs are always on */
		if (type != BOARD_XSURF)
			z_writeb(0, buddha_board+BUDDHA_IRQ_MR);

		printk(KERN_INFO "ide: %s IDE controller\n",
				 buddha_board_name[type]);

		for (i = 0; i < buddha_num_hwifs; i++) {
			unsigned long base, ctl, irq_port;

			if (type != BOARD_XSURF) {
				base = buddha_board + buddha_bases[i];
				ctl = base + BUDDHA_CONTROL;
				irq_port = buddha_board + buddha_irqports[i];
			} else {
				base = buddha_board + xsurf_bases[i];
				/* X-Surf has no CS1* (Control/AltStat) */
				ctl = 0;
				irq_port = buddha_board + xsurf_irqports[i];
			}

			buddha_setup_ports(&hw[i], base, ctl, irq_port);

			hws[i] = &hw[i];
		}

		ide_host_add(&d, hws, i, NULL);
	}

	return 0;
}

module_init(buddha_init);

MODULE_LICENSE("GPL");
