/*
 *  linux/drivers/ide/legacy/buddha.c -- Amiga Buddha, Catweasel and X-Surf IDE Driver
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
#include <linux/hdreg.h>
#include <linux/zorro.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/amigahw.h>
#include <asm/amigaints.h>


    /*
     *  The Buddha has 2 IDE interfaces, the Catweasel has 3, X-Surf has 2
     */

#define BUDDHA_NUM_HWIFS	2
#define CATWEASEL_NUM_HWIFS	3
#define XSURF_NUM_HWIFS         2

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

#define BUDDHA_DATA	0x00
#define BUDDHA_ERROR	0x06		/* see err-bits */
#define BUDDHA_NSECTOR	0x0a		/* nr of sectors to read/write */
#define BUDDHA_SECTOR	0x0e		/* starting sector */
#define BUDDHA_LCYL	0x12		/* starting cylinder */
#define BUDDHA_HCYL	0x16		/* high byte of starting cyl */
#define BUDDHA_SELECT	0x1a		/* 101dhhhh , d=drive, hhhh=head */
#define BUDDHA_STATUS	0x1e		/* see status-bits */
#define BUDDHA_CONTROL	0x11a
#define XSURF_CONTROL   -1              /* X-Surf has no CS1* (Control/AltStat) */

static int buddha_offsets[IDE_NR_PORTS] __initdata = {
    BUDDHA_DATA, BUDDHA_ERROR, BUDDHA_NSECTOR, BUDDHA_SECTOR, BUDDHA_LCYL,
    BUDDHA_HCYL, BUDDHA_SELECT, BUDDHA_STATUS, BUDDHA_CONTROL, -1
};

static int xsurf_offsets[IDE_NR_PORTS] __initdata = {
    BUDDHA_DATA, BUDDHA_ERROR, BUDDHA_NSECTOR, BUDDHA_SECTOR, BUDDHA_LCYL,
    BUDDHA_HCYL, BUDDHA_SELECT, BUDDHA_STATUS, XSURF_CONTROL, -1
};

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


    /*
     *  Check and acknowledge the interrupt status
     */

static int buddha_ack_intr(ide_hwif_t *hwif)
{
    unsigned char ch;

    ch = z_readb(hwif->io_ports[IDE_IRQ_OFFSET]);
    if (!(ch & 0x80))
	    return 0;
    return 1;
}

static int xsurf_ack_intr(ide_hwif_t *hwif)
{
    unsigned char ch;

    ch = z_readb(hwif->io_ports[IDE_IRQ_OFFSET]);
    /* X-Surf needs a 0 written to IRQ register to ensure ISA bit A11 stays at 0 */
    z_writeb(0, hwif->io_ports[IDE_IRQ_OFFSET]); 
    if (!(ch & 0x80))
	    return 0;
    return 1;
}

    /*
     *  Probe for a Buddha or Catweasel IDE interface
     */

void __init buddha_init(void)
{
	hw_regs_t hw;
	ide_hwif_t *hwif;
	int i, index;

	struct zorro_dev *z = NULL;
	u_long buddha_board = 0;
	BuddhaType type;
	int buddha_num_hwifs;

	while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
		unsigned long board;
		if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_BUDDHA) {
			buddha_num_hwifs = BUDDHA_NUM_HWIFS;
			type=BOARD_BUDDHA;
		} else if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_CATWEASEL) {
			buddha_num_hwifs = CATWEASEL_NUM_HWIFS;
			type=BOARD_CATWEASEL;
		} else if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_X_SURF) {
			buddha_num_hwifs = XSURF_NUM_HWIFS;
			type=BOARD_XSURF;
		} else 
			continue;
		
		board = z->resource.start;

/*
 * FIXME: we now have selectable mmio v/s iomio transports.
 */

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
		buddha_board = ZTWO_VADDR(board);
		
		/* write to BUDDHA_IRQ_MR to enable the board IRQ */
		/* X-Surf doesn't have this.  IRQs are always on */
		if (type != BOARD_XSURF)
			z_writeb(0, buddha_board+BUDDHA_IRQ_MR);
		
		for(i=0;i<buddha_num_hwifs;i++) {
			if(type != BOARD_XSURF) {
				ide_setup_ports(&hw, (buddha_board+buddha_bases[i]),
						buddha_offsets, 0,
						(buddha_board+buddha_irqports[i]),
						buddha_ack_intr,
//						budda_iops,
						IRQ_AMIGA_PORTS);
			} else {
				ide_setup_ports(&hw, (buddha_board+xsurf_bases[i]),
						xsurf_offsets, 0,
						(buddha_board+xsurf_irqports[i]),
						xsurf_ack_intr,
//						xsurf_iops,
						IRQ_AMIGA_PORTS);
			}	

			index = ide_register_hw(&hw, NULL, 1, &hwif);
			if (index != -1) {
				hwif->mmio = 1;
				printk("ide%d: ", index);
				switch(type) {
				case BOARD_BUDDHA:
					printk("Buddha");
					break;
				case BOARD_CATWEASEL:
					printk("Catweasel");
					break;
				case BOARD_XSURF:
					printk("X-Surf");
					break;
				}
				printk(" IDE interface\n");	    
			}		      
		}
	}
}
