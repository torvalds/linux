/*
 * $Id: cstm_mips_ixx.c,v 1.12 2004/11/04 13:24:14 gleixner Exp $
 *
 * Mapping of a custom board with both AMD CFI and JEDEC flash in partitions.
 * Config with both CFI and JEDEC device support.
 *
 * Basically physmap.c with the addition of partitions and 
 * an array of mapping info to accomodate more than one flash type per board.
 *
 * Copyright 2000 MontaVista Software Inc.
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
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/config.h>
#include <linux/delay.h>

#if defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR)
#define CC_GCR             0xB4013818
#define CC_GPBCR           0xB401380A
#define CC_GPBDR           0xB4013808
#define CC_M68K_DEVICE     1
#define CC_M68K_FUNCTION   6
#define CC_CONFADDR        0xB8004000
#define CC_CONFDATA        0xB8004004
#define CC_FC_FCR          0xB8002004
#define CC_FC_DCR          0xB8002008
#define CC_GPACR           0xB4013802
#define CC_GPAICR          0xB4013804
#endif /* defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR) */

#if defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR)
void cstm_mips_ixx_set_vpp(struct map_info *map,int vpp)
{
	static DEFINE_SPINLOCK(vpp_lock);
	static int vpp_count = 0;
	unsigned long flags;

	spin_lock_irqsave(&vpp_lock, flags);

	if (vpp) {
		if (!vpp_count++) {
			__u16	data;
			__u8	data1;
			static u8 first = 1;
		
			// Set GPIO port B pin3 to high
			data = *(__u16 *)(CC_GPBCR);
			data = (data & 0xff0f) | 0x0040;
			*(__u16 *)CC_GPBCR = data;
			*(__u8 *)CC_GPBDR = (*(__u8*)CC_GPBDR) | 0x08;
			if (first) {
				first = 0;
				/* need to have this delay for first
				   enabling vpp after powerup */
				udelay(40);
			}
		}
	} else {
		if (!--vpp_count) {
			__u16	data;
		
			// Set GPIO port B pin3 to high
			data = *(__u16 *)(CC_GPBCR);
			data = (data & 0xff3f) | 0x0040;
			*(__u16 *)CC_GPBCR = data;
			*(__u8 *)CC_GPBDR = (*(__u8*)CC_GPBDR) & 0xf7;
		}
	}
	spin_unlock_irqrestore(&vpp_lock, flags);
}
#endif

/* board and partition description */

#define MAX_PHYSMAP_PARTITIONS    8
struct cstm_mips_ixx_info {
	char *name;
	unsigned long window_addr;
	unsigned long window_size;
	int bankwidth;
	int num_partitions;
};

#if defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR)
#define PHYSMAP_NUMBER  1  // number of board desc structs needed, one per contiguous flash type 
const struct cstm_mips_ixx_info cstm_mips_ixx_board_desc[PHYSMAP_NUMBER] = 
{
    {   // 28F128J3A in 2x16 configuration
        "big flash",     // name
	0x08000000,      // window_addr
	0x02000000,      // window_size
        4,               // bankwidth
	1,               // num_partitions
    }

};
static struct mtd_partition cstm_mips_ixx_partitions[PHYSMAP_NUMBER][MAX_PHYSMAP_PARTITIONS] = {
{   // 28F128J3A in 2x16 configuration
	{
		.name = "main partition ",
		.size = 0x02000000, // 128 x 2 x 128k byte sectors
		.offset = 0,
	},
},
};
#else /* defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR) */
#define PHYSMAP_NUMBER  1  // number of board desc structs needed, one per contiguous flash type 
const struct cstm_mips_ixx_info cstm_mips_ixx_board_desc[PHYSMAP_NUMBER] = 
{
    {  
        "MTD flash",                   // name
	CONFIG_MTD_CSTM_MIPS_IXX_START,      // window_addr
	CONFIG_MTD_CSTM_MIPS_IXX_LEN,        // window_size
        CONFIG_MTD_CSTM_MIPS_IXX_BUSWIDTH,   // bankwidth
	1,                             // num_partitions
    },

};
static struct mtd_partition cstm_mips_ixx_partitions[PHYSMAP_NUMBER][MAX_PHYSMAP_PARTITIONS] = {
{ 
	{
		.name = "main partition",
		.size =  CONFIG_MTD_CSTM_MIPS_IXX_LEN,
		.offset = 0,
	},
},
};
#endif /* defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR) */

struct map_info cstm_mips_ixx_map[PHYSMAP_NUMBER];

int __init init_cstm_mips_ixx(void)
{
	int i;
	int jedec;
        struct mtd_info *mymtd;
        struct mtd_partition *parts;

	/* Initialize mapping */
	for (i=0;i<PHYSMAP_NUMBER;i++) {
		printk(KERN_NOTICE "cstm_mips_ixx flash device: 0x%lx at 0x%lx\n", 
		       cstm_mips_ixx_board_desc[i].window_size, cstm_mips_ixx_board_desc[i].window_addr);


		cstm_mips_ixx_map[i].phys = cstm_mips_ixx_board_desc[i].window_addr;
		cstm_mips_ixx_map[i].virt = ioremap(cstm_mips_ixx_board_desc[i].window_addr, cstm_mips_ixx_board_desc[i].window_size);
		if (!cstm_mips_ixx_map[i].virt) {
			printk(KERN_WARNING "Failed to ioremap\n");
			return -EIO;
	        }
		cstm_mips_ixx_map[i].name = cstm_mips_ixx_board_desc[i].name;
		cstm_mips_ixx_map[i].size = cstm_mips_ixx_board_desc[i].window_size;
		cstm_mips_ixx_map[i].bankwidth = cstm_mips_ixx_board_desc[i].bankwidth;
#if defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR)
                cstm_mips_ixx_map[i].set_vpp = cstm_mips_ixx_set_vpp;
#endif
		simple_map_init(&cstm_mips_ixx_map[i]);
		//printk(KERN_NOTICE "cstm_mips_ixx: ioremap is %x\n",(unsigned int)(cstm_mips_ixx_map[i].virt));
	}

#if defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR)
        setup_ITE_IVR_flash();
#endif /* defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR) */

	for (i=0;i<PHYSMAP_NUMBER;i++) {
                parts = &cstm_mips_ixx_partitions[i][0];
		jedec = 0;
		mymtd = (struct mtd_info *)do_map_probe("cfi_probe", &cstm_mips_ixx_map[i]);
		//printk(KERN_NOTICE "phymap %d cfi_probe: mymtd is %x\n",i,(unsigned int)mymtd);
		if (!mymtd) {
			jedec = 1;
			mymtd = (struct mtd_info *)do_map_probe("jedec", &cstm_mips_ixx_map[i]);
		        printk(KERN_NOTICE "cstm_mips_ixx %d jedec: mymtd is %x\n",i,(unsigned int)mymtd);
		}
		if (mymtd) {
			mymtd->owner = THIS_MODULE;

	                cstm_mips_ixx_map[i].map_priv_2 = (unsigned long)mymtd;
		        add_mtd_partitions(mymtd, parts, cstm_mips_ixx_board_desc[i].num_partitions);
		}
		else
	           return -ENXIO;
	}
	return 0;
}

static void __exit cleanup_cstm_mips_ixx(void)
{
	int i;
        struct mtd_info *mymtd;

	for (i=0;i<PHYSMAP_NUMBER;i++) {
	        mymtd = (struct mtd_info *)cstm_mips_ixx_map[i].map_priv_2;
		if (mymtd) {
			del_mtd_partitions(mymtd);
			map_destroy(mymtd);
		}
		if (cstm_mips_ixx_map[i].virt) {
			iounmap((void *)cstm_mips_ixx_map[i].virt);
			cstm_mips_ixx_map[i].virt = 0;
		}
	}
}
#if defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR)
void PCISetULongByOffset(__u32 DevNumber, __u32 FuncNumber, __u32 Offset, __u32 data)
{
	__u32	offset;

	offset = ( unsigned long )( 0x80000000 | ( DevNumber << 11 ) + ( FuncNumber << 8 ) + Offset) ;

	*(__u32 *)CC_CONFADDR = offset;	
	*(__u32 *)CC_CONFDATA = data;
}
void setup_ITE_IVR_flash()
{
		__u32	size, base;

		size = 0x0e000000;		// 32MiB
		base = (0x08000000) >> 8 >>1; // Bug: we must shift one more bit

		/* need to set ITE flash to 32 bits instead of default 8 */
#ifdef CONFIG_MIPS_IVR
		*(__u32 *)CC_FC_FCR = 0x55;
		*(__u32 *)CC_GPACR = 0xfffc;
#else
		*(__u32 *)CC_FC_FCR = 0x77;
#endif
		/* turn bursting off */
		*(__u32 *)CC_FC_DCR = 0x0;

		/* setup for one chip 4 byte PCI access */
		PCISetULongByOffset(CC_M68K_DEVICE, CC_M68K_FUNCTION, 0x60, size | base);
		PCISetULongByOffset(CC_M68K_DEVICE, CC_M68K_FUNCTION, 0x64, 0x02);
}
#endif /* defined(CONFIG_MIPS_ITE8172) || defined(CONFIG_MIPS_IVR) */

module_init(init_cstm_mips_ixx);
module_exit(cleanup_cstm_mips_ixx);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alice Hennessy <ahennessy@mvista.com>");
MODULE_DESCRIPTION("MTD map driver for ITE 8172G and Globespan IVR boards");
