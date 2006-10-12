/*
 * $Id: cstm_mips_ixx.c,v 1.14 2005/11/07 11:14:26 gleixner Exp $
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
#include <linux/delay.h>

/* board and partition description */

#define MAX_PHYSMAP_PARTITIONS    8
struct cstm_mips_ixx_info {
	char *name;
	unsigned long window_addr;
	unsigned long window_size;
	int bankwidth;
	int num_partitions;
};

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
			int j = 0;
			printk(KERN_WARNING "Failed to ioremap\n");
			for (j = 0; j < i; j++) {
				if (cstm_mips_ixx_map[j].virt) {
					iounmap(cstm_mips_ixx_map[j].virt);
					cstm_mips_ixx_map[j].virt = NULL;
				}
			}
			return -EIO;
	        }
		cstm_mips_ixx_map[i].name = cstm_mips_ixx_board_desc[i].name;
		cstm_mips_ixx_map[i].size = cstm_mips_ixx_board_desc[i].window_size;
		cstm_mips_ixx_map[i].bankwidth = cstm_mips_ixx_board_desc[i].bankwidth;
		simple_map_init(&cstm_mips_ixx_map[i]);
		//printk(KERN_NOTICE "cstm_mips_ixx: ioremap is %x\n",(unsigned int)(cstm_mips_ixx_map[i].virt));
	}

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
		else {
			for (i = 0; i < PHYSMAP_NUMBER; i++) {
				if (cstm_mips_ixx_map[i].virt) {
					iounmap(cstm_mips_ixx_map[i].virt);
					cstm_mips_ixx_map[i].virt = NULL;
				}
			}
			return -ENXIO;
		}
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

module_init(init_cstm_mips_ixx);
module_exit(cleanup_cstm_mips_ixx);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alice Hennessy <ahennessy@mvista.com>");
MODULE_DESCRIPTION("MTD map driver for MIPS boards");
