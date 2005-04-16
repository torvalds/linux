/* dilnetpc.c -- MTD map driver for SSV DIL/Net PC Boards "DNP" and "ADNP"
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * $Id: dilnetpc.c,v 1.17 2004/11/28 09:40:39 dwmw2 Exp $
 *
 * The DIL/Net PC is a tiny embedded PC board made by SSV Embedded Systems
 * featuring the AMD Elan SC410 processor. There are two variants of this
 * board: DNP/1486 and ADNP/1486. The DNP version has 2 megs of flash
 * ROM (Intel 28F016S3) and 8 megs of DRAM, the ADNP version has 4 megs
 * flash and 16 megs of RAM.
 * For details, see http://www.ssv-embedded.de/ssv/pc104/p169.htm
 * and http://www.ssv-embedded.de/ssv/pc104/p170.htm
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/concat.h>

/*
** The DIL/NetPC keeps its BIOS in two distinct flash blocks.
** Destroying any of these blocks transforms the DNPC into
** a paperweight (albeit not a very useful one, considering
** it only weighs a few grams).
**
** Therefore, the BIOS blocks must never be erased or written to
** except by people who know exactly what they are doing (e.g.
** to install a BIOS update). These partitions are marked read-only
** by default, but can be made read/write by undefining
** DNPC_BIOS_BLOCKS_WRITEPROTECTED:
*/
#define DNPC_BIOS_BLOCKS_WRITEPROTECTED

/*
** The ID string (in ROM) is checked to determine whether we
** are running on a DNP/1486 or ADNP/1486
*/
#define BIOSID_BASE	0x000fe100

#define ID_DNPC	"DNP1486"
#define ID_ADNP	"ADNP1486"

/*
** Address where the flash should appear in CPU space
*/
#define FLASH_BASE	0x2000000

/*
** Chip Setup and Control (CSC) indexed register space
*/
#define CSC_INDEX	0x22
#define CSC_DATA	0x23

#define CSC_MMSWAR	0x30	/* MMS window C-F attributes register */
#define CSC_MMSWDSR	0x31	/* MMS window C-F device select register */

#define CSC_RBWR	0xa7	/* GPIO Read-Back/Write Register B */

#define CSC_CR		0xd0	/* internal I/O device disable/Echo */
				/* Z-bus/configuration register */

#define CSC_PCCMDCR	0xf1	/* PC card mode and DMA control register */


/*
** PC Card indexed register space:
*/

#define PCC_INDEX	0x3e0
#define PCC_DATA	0x3e1

#define PCC_AWER_B		0x46	/* Socket B Address Window enable register */
#define PCC_MWSAR_1_Lo	0x58	/* memory window 1 start address low register */
#define PCC_MWSAR_1_Hi	0x59	/* memory window 1 start address high register */
#define PCC_MWEAR_1_Lo	0x5A	/* memory window 1 stop address low register */
#define PCC_MWEAR_1_Hi	0x5B	/* memory window 1 stop address high register */
#define PCC_MWAOR_1_Lo	0x5C	/* memory window 1 address offset low register */
#define PCC_MWAOR_1_Hi	0x5D	/* memory window 1 address offset high register */


/*
** Access to SC4x0's Chip Setup and Control (CSC)
** and PC Card (PCC) indexed registers:
*/
static inline void setcsc(int reg, unsigned char data)
{
	outb(reg, CSC_INDEX);
	outb(data, CSC_DATA);
}

static inline unsigned char getcsc(int reg)
{
	outb(reg, CSC_INDEX);
	return(inb(CSC_DATA));
}

static inline void setpcc(int reg, unsigned char data)
{
	outb(reg, PCC_INDEX);
	outb(data, PCC_DATA);
}

static inline unsigned char getpcc(int reg)
{
	outb(reg, PCC_INDEX);
	return(inb(PCC_DATA));
}


/*
************************************************************
** Enable access to DIL/NetPC's flash by mapping it into
** the SC4x0's MMS Window C.
************************************************************
*/
static void dnpc_map_flash(unsigned long flash_base, unsigned long flash_size)
{
	unsigned long flash_end = flash_base + flash_size - 1;

	/*
	** enable setup of MMS windows C-F:
	*/
	/* - enable PC Card indexed register space */
	setcsc(CSC_CR, getcsc(CSC_CR) | 0x2);
	/* - set PC Card controller to operate in standard mode */
	setcsc(CSC_PCCMDCR, getcsc(CSC_PCCMDCR) & ~1);

	/*
	** Program base address and end address of window
	** where the flash ROM should appear in CPU address space
	*/
	setpcc(PCC_MWSAR_1_Lo, (flash_base >> 12) & 0xff);
	setpcc(PCC_MWSAR_1_Hi, (flash_base >> 20) & 0x3f);
	setpcc(PCC_MWEAR_1_Lo, (flash_end >> 12) & 0xff);
	setpcc(PCC_MWEAR_1_Hi, (flash_end >> 20) & 0x3f);

	/* program offset of first flash location to appear in this window (0) */
	setpcc(PCC_MWAOR_1_Lo, ((0 - flash_base) >> 12) & 0xff);
	setpcc(PCC_MWAOR_1_Hi, ((0 - flash_base)>> 20) & 0x3f);

	/* set attributes for MMS window C: non-cacheable, write-enabled */
	setcsc(CSC_MMSWAR, getcsc(CSC_MMSWAR) & ~0x11);

	/* select physical device ROMCS0 (i.e. flash) for MMS Window C */
	setcsc(CSC_MMSWDSR, getcsc(CSC_MMSWDSR) & ~0x03);

	/* enable memory window 1 */
	setpcc(PCC_AWER_B, getpcc(PCC_AWER_B) | 0x02);

	/* now disable PC Card indexed register space again */
	setcsc(CSC_CR, getcsc(CSC_CR) & ~0x2);
}


/*
************************************************************
** Disable access to DIL/NetPC's flash by mapping it into
** the SC4x0's MMS Window C.
************************************************************
*/
static void dnpc_unmap_flash(void)
{
	/* - enable PC Card indexed register space */
	setcsc(CSC_CR, getcsc(CSC_CR) | 0x2);

	/* disable memory window 1 */
	setpcc(PCC_AWER_B, getpcc(PCC_AWER_B) & ~0x02);

	/* now disable PC Card indexed register space again */
	setcsc(CSC_CR, getcsc(CSC_CR) & ~0x2);
}



/*
************************************************************
** Enable/Disable VPP to write to flash
************************************************************
*/

static DEFINE_SPINLOCK(dnpc_spin);
static int        vpp_counter = 0;
/*
** This is what has to be done for the DNP board ..
*/
static void dnp_set_vpp(struct map_info *not_used, int on)
{
	spin_lock_irq(&dnpc_spin);

	if (on)
	{
		if(++vpp_counter == 1)
			setcsc(CSC_RBWR, getcsc(CSC_RBWR) & ~0x4);
	}
	else
	{
		if(--vpp_counter == 0)
			setcsc(CSC_RBWR, getcsc(CSC_RBWR) | 0x4);
		else if(vpp_counter < 0)
			BUG();
	}
	spin_unlock_irq(&dnpc_spin);
}

/*
** .. and this the ADNP version:
*/
static void adnp_set_vpp(struct map_info *not_used, int on)
{
	spin_lock_irq(&dnpc_spin);

	if (on)
	{
		if(++vpp_counter == 1)
			setcsc(CSC_RBWR, getcsc(CSC_RBWR) & ~0x8);
	}
	else
	{
		if(--vpp_counter == 0)
			setcsc(CSC_RBWR, getcsc(CSC_RBWR) | 0x8);
		else if(vpp_counter < 0)
			BUG();
	}
	spin_unlock_irq(&dnpc_spin);
}



#define DNP_WINDOW_SIZE		0x00200000	/*  DNP flash size is 2MiB  */
#define ADNP_WINDOW_SIZE	0x00400000	/* ADNP flash size is 4MiB */
#define WINDOW_ADDR		FLASH_BASE

static struct map_info dnpc_map = {
	.name = "ADNP Flash Bank",
	.size = ADNP_WINDOW_SIZE,
	.bankwidth = 1,
	.set_vpp = adnp_set_vpp,
	.phys = WINDOW_ADDR
};

/*
** The layout of the flash is somewhat "strange":
**
** 1.  960 KiB (15 blocks) : Space for ROM Bootloader and user data
** 2.   64 KiB (1 block)   : System BIOS
** 3.  960 KiB (15 blocks) : User Data (DNP model) or
** 3. 3008 KiB (47 blocks) : User Data (ADNP model)
** 4.   64 KiB (1 block)   : System BIOS Entry
*/

static struct mtd_partition partition_info[]=
{
	{ 
		.name =		"ADNP boot", 
		.offset =	0, 
		.size =		0xf0000,
	},
	{ 
		.name =		"ADNP system BIOS", 
		.offset =	MTDPART_OFS_NXTBLK,
		.size =		0x10000,
#ifdef DNPC_BIOS_BLOCKS_WRITEPROTECTED
		.mask_flags =	MTD_WRITEABLE,
#endif
	},
	{
		.name =		"ADNP file system",
		.offset =	MTDPART_OFS_NXTBLK,
		.size =		0x2f0000,
	},
	{
		.name =		"ADNP system BIOS entry", 
		.offset =	MTDPART_OFS_NXTBLK,
		.size =		MTDPART_SIZ_FULL,
#ifdef DNPC_BIOS_BLOCKS_WRITEPROTECTED
		.mask_flags =	MTD_WRITEABLE,
#endif
	},
};

#define NUM_PARTITIONS (sizeof(partition_info)/sizeof(partition_info[0]))

static struct mtd_info *mymtd;
static struct mtd_info *lowlvl_parts[NUM_PARTITIONS];
static struct mtd_info *merged_mtd;

/*
** "Highlevel" partition info:
**
** Using the MTD concat layer, we can re-arrange partitions to our
** liking: we construct a virtual MTD device by concatenating the
** partitions, specifying the sequence such that the boot block
** is immediately followed by the filesystem block (i.e. the stupid
** system BIOS block is mapped to a different place). When re-partitioning
** this concatenated MTD device, we can set the boot block size to
** an arbitrary (though erase block aligned) value i.e. not one that
** is dictated by the flash's physical layout. We can thus set the
** boot block to be e.g. 64 KB (which is fully sufficient if we want
** to boot an etherboot image) or to -say- 1.5 MB if we want to boot
** a large kernel image. In all cases, the remainder of the flash
** is available as file system space.
*/

static struct mtd_partition higlvl_partition_info[]=
{
	{ 
		.name =		"ADNP boot block", 
		.offset =	0, 
		.size =		CONFIG_MTD_DILNETPC_BOOTSIZE,
	},
	{
		.name =		"ADNP file system space",
		.offset =	MTDPART_OFS_NXTBLK,
		.size =		ADNP_WINDOW_SIZE-CONFIG_MTD_DILNETPC_BOOTSIZE-0x20000,
	},
	{ 
		.name =		"ADNP system BIOS + BIOS Entry", 
		.offset =	MTDPART_OFS_NXTBLK,
		.size =		MTDPART_SIZ_FULL,
#ifdef DNPC_BIOS_BLOCKS_WRITEPROTECTED
		.mask_flags =	MTD_WRITEABLE,
#endif
	},
};

#define NUM_HIGHLVL_PARTITIONS (sizeof(higlvl_partition_info)/sizeof(partition_info[0]))


static int dnp_adnp_probe(void)
{
	char *biosid, rc = -1;

	biosid = (char*)ioremap(BIOSID_BASE, 16);
	if(biosid)
	{
		if(!strcmp(biosid, ID_DNPC))
			rc = 1;		/* this is a DNPC  */
		else if(!strcmp(biosid, ID_ADNP))
			rc = 0;		/* this is a ADNPC */
	}
	iounmap((void *)biosid);
	return(rc);
}


static int __init init_dnpc(void)
{
	int is_dnp;

	/*
	** determine hardware (DNP/ADNP/invalid)
	*/	
	if((is_dnp = dnp_adnp_probe()) < 0)
		return -ENXIO;

	/*
	** Things are set up for ADNP by default
	** -> modify all that needs to be different for DNP
	*/
	if(is_dnp)
	{	/*
		** Adjust window size, select correct set_vpp function.
		** The partitioning scheme is identical on both DNP
		** and ADNP except for the size of the third partition.
		*/
		int i;
		dnpc_map.size          = DNP_WINDOW_SIZE;
		dnpc_map.set_vpp       = dnp_set_vpp;
		partition_info[2].size = 0xf0000;

		/*
		** increment all string pointers so the leading 'A' gets skipped,
		** thus turning all occurrences of "ADNP ..." into "DNP ..."
		*/
		++dnpc_map.name;
		for(i = 0; i < NUM_PARTITIONS; i++)
			++partition_info[i].name;
		higlvl_partition_info[1].size = DNP_WINDOW_SIZE - 
			CONFIG_MTD_DILNETPC_BOOTSIZE - 0x20000;
		for(i = 0; i < NUM_HIGHLVL_PARTITIONS; i++)
			++higlvl_partition_info[i].name;
	}

	printk(KERN_NOTICE "DIL/Net %s flash: 0x%lx at 0x%lx\n", 
		is_dnp ? "DNPC" : "ADNP", dnpc_map.size, dnpc_map.phys);

	dnpc_map.virt = ioremap_nocache(dnpc_map.phys, dnpc_map.size);

	dnpc_map_flash(dnpc_map.phys, dnpc_map.size);

	if (!dnpc_map.virt) {
		printk("Failed to ioremap_nocache\n");
		return -EIO;
	}
	simple_map_init(&dnpc_map);

	printk("FLASH virtual address: 0x%p\n", dnpc_map.virt);

	mymtd = do_map_probe("jedec_probe", &dnpc_map);

	if (!mymtd)
		mymtd = do_map_probe("cfi_probe", &dnpc_map);

	/*
	** If flash probes fail, try to make flashes accessible
	** at least as ROM. Ajust erasesize in this case since
	** the default one (128M) will break our partitioning
	*/
	if (!mymtd)
		if((mymtd = do_map_probe("map_rom", &dnpc_map)))
			mymtd->erasesize = 0x10000;

	if (!mymtd) {
		iounmap(dnpc_map.virt);
		return -ENXIO;
	}
		
	mymtd->owner = THIS_MODULE;

	/*
	** Supply pointers to lowlvl_parts[] array to add_mtd_partitions()
	** -> add_mtd_partitions() will _not_ register MTD devices for
	** the partitions, but will instead store pointers to the MTD
	** objects it creates into our lowlvl_parts[] array.
	** NOTE: we arrange the pointers such that the sequence of the
	**       partitions gets re-arranged: partition #2 follows
	**       partition #0.
	*/
	partition_info[0].mtdp = &lowlvl_parts[0];
	partition_info[1].mtdp = &lowlvl_parts[2];
	partition_info[2].mtdp = &lowlvl_parts[1];
	partition_info[3].mtdp = &lowlvl_parts[3];

	add_mtd_partitions(mymtd, partition_info, NUM_PARTITIONS);

	/*
	** now create a virtual MTD device by concatenating the for partitions
	** (in the sequence given by the lowlvl_parts[] array.
	*/
	merged_mtd = mtd_concat_create(lowlvl_parts, NUM_PARTITIONS, "(A)DNP Flash Concatenated");
	if(merged_mtd)
	{	/*
		** now partition the new device the way we want it. This time,
		** we do not supply mtd pointers in higlvl_partition_info, so
		** add_mtd_partitions() will register the devices.
		*/
		add_mtd_partitions(merged_mtd, higlvl_partition_info, NUM_HIGHLVL_PARTITIONS);
	}

	return 0;
}

static void __exit cleanup_dnpc(void)
{
	if(merged_mtd) {
		del_mtd_partitions(merged_mtd);
		mtd_concat_destroy(merged_mtd);
	}

	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
	}
	if (dnpc_map.virt) {
		iounmap(dnpc_map.virt);
		dnpc_unmap_flash();
		dnpc_map.virt = NULL;
	}
}

module_init(init_dnpc);
module_exit(cleanup_dnpc);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sysgo Real-Time Solutions GmbH");
MODULE_DESCRIPTION("MTD map driver for SSV DIL/NetPC DNP & ADNP");
