/*
 *  Q40 I/O port IDE Driver
 *
 *     (c) Richard Zidlicky
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/ide.h>

    /*
     *  Bases of the IDE interfaces
     */

#define Q40IDE_NUM_HWIFS	2

#define PCIDE_BASE1	0x1f0
#define PCIDE_BASE2	0x170
#define PCIDE_BASE3	0x1e8
#define PCIDE_BASE4	0x168
#define PCIDE_BASE5	0x1e0
#define PCIDE_BASE6	0x160

static const unsigned long pcide_bases[Q40IDE_NUM_HWIFS] = {
    PCIDE_BASE1, PCIDE_BASE2, /* PCIDE_BASE3, PCIDE_BASE4  , PCIDE_BASE5,
    PCIDE_BASE6 */
};


    /*
     *  Offsets from one of the above bases
     */

/* used to do addr translation here but it is easier to do in setup ports */
/*#define IDE_OFF_B(x)	((unsigned long)Q40_ISA_IO_B((IDE_##x##_OFFSET)))*/

#define IDE_OFF_B(x)	((unsigned long)((IDE_##x##_OFFSET)))
#define IDE_OFF_W(x)	((unsigned long)((IDE_##x##_OFFSET)))

static const int pcide_offsets[IDE_NR_PORTS] = {
    IDE_OFF_W(DATA), IDE_OFF_B(ERROR), IDE_OFF_B(NSECTOR), IDE_OFF_B(SECTOR),
    IDE_OFF_B(LCYL), IDE_OFF_B(HCYL), 6 /*IDE_OFF_B(CURRENT)*/, IDE_OFF_B(STATUS),
    518/*IDE_OFF(CMD)*/
};

static int q40ide_default_irq(unsigned long base)
{
           switch (base) {
	            case 0x1f0: return 14;
		    case 0x170: return 15;
		    case 0x1e8: return 11;
		    default:
			return 0;
	   }
}


/*
 * Addresses are pretranslated for Q40 ISA access.
 */
void q40_ide_setup_ports ( hw_regs_t *hw,
			unsigned long base, int *offsets,
			unsigned long ctrl, unsigned long intr,
			ide_ack_intr_t *ack_intr,
			int irq)
{
	int i;

	memset(hw, 0, sizeof(hw_regs_t));
	for (i = 0; i < IDE_NR_PORTS; i++) {
		/* BIG FAT WARNING: 
		   assumption: only DATA port is ever used in 16 bit mode */
		if ( i==0 )
			hw->io_ports[i] = Q40_ISA_IO_W(base + offsets[i]);
		else
			hw->io_ports[i] = Q40_ISA_IO_B(base + offsets[i]);
	}

	hw->irq = irq;
	hw->ack_intr = ack_intr;
}



/* 
 * the static array is needed to have the name reported in /proc/ioports,
 * hwif->name unfortunately isn't available yet
 */
static const char *q40_ide_names[Q40IDE_NUM_HWIFS]={
	"ide0", "ide1"
};

/*
 *  Probe for Q40 IDE interfaces
 */

static int __init q40ide_init(void)
{
    int i;
    ide_hwif_t *hwif;
    const char *name;
    u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

    if (!MACH_IS_Q40)
      return -ENODEV;

    printk(KERN_INFO "ide: Q40 IDE controller\n");

    for (i = 0; i < Q40IDE_NUM_HWIFS; i++) {
	hw_regs_t hw;

	name = q40_ide_names[i];
	if (!request_region(pcide_bases[i], 8, name)) {
		printk("could not reserve ports %lx-%lx for %s\n",
		       pcide_bases[i],pcide_bases[i]+8,name);
		continue;
	}
	if (!request_region(pcide_bases[i]+0x206, 1, name)) {
		printk("could not reserve port %lx for %s\n",
		       pcide_bases[i]+0x206,name);
		release_region(pcide_bases[i], 8);
		continue;
	}
	q40_ide_setup_ports(&hw,(unsigned long) pcide_bases[i], (int *)pcide_offsets, 
			pcide_bases[i]+0x206, 
			0, NULL,
//			m68kide_iops,
			q40ide_default_irq(pcide_bases[i]));

	hwif = ide_find_port();
	if (hwif) {
		ide_init_port_data(hwif, hwif->index);
		ide_init_port_hw(hwif, &hw);

		idx[i] = hwif->index;
	}
    }

    ide_device_add(idx, NULL);

    return 0;
}

module_init(q40ide_init);

MODULE_LICENSE("GPL");
