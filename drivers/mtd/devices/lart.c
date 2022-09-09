// SPDX-License-Identifier: GPL-2.0-only

/*
 * MTD driver for the 28F160F3 Flash Memory (non-CFI) on LART.
 *
 * Author: Abraham vd Merwe <abraham@2d3d.co.za>
 *
 * Copyright (c) 2001, 2d3D, Inc.
 *
 * References:
 *
 *    [1] 3 Volt Fast Boot Block Flash Memory" Intel Datasheet
 *           - Order Number: 290644-005
 *           - January 2000
 *
 *    [2] MTD internal API documentation
 *           - http://www.linux-mtd.infradead.org/ 
 *
 * Limitations:
 *
 *    Even though this driver is written for 3 Volt Fast Boot
 *    Block Flash Memory, it is rather specific to LART. With
 *    Minor modifications, notably the without data/address line
 *    mangling and different bus settings, etc. it should be
 *    trivial to adapt to other platforms.
 *
 *    If somebody would sponsor me a different board, I'll
 *    adapt the driver (:
 */

/* debugging */
//#define LART_DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#ifndef CONFIG_SA1100_LART
#error This is for LART architecture only
#endif

static char module_name[] = "lart";

/*
 * These values is specific to 28Fxxxx3 flash memory.
 * See section 2.3.1 in "3 Volt Fast Boot Block Flash Memory" Intel Datasheet
 */
#define FLASH_BLOCKSIZE_PARAM		(4096 * BUSWIDTH)
#define FLASH_NUMBLOCKS_16m_PARAM	8
#define FLASH_NUMBLOCKS_8m_PARAM	8

/*
 * These values is specific to 28Fxxxx3 flash memory.
 * See section 2.3.2 in "3 Volt Fast Boot Block Flash Memory" Intel Datasheet
 */
#define FLASH_BLOCKSIZE_MAIN		(32768 * BUSWIDTH)
#define FLASH_NUMBLOCKS_16m_MAIN	31
#define FLASH_NUMBLOCKS_8m_MAIN		15

/*
 * These values are specific to LART
 */

/* general */
#define BUSWIDTH			4				/* don't change this - a lot of the code _will_ break if you change this */
#define FLASH_OFFSET		0xe8000000		/* see linux/arch/arm/mach-sa1100/lart.c */

/* blob */
#define NUM_BLOB_BLOCKS		FLASH_NUMBLOCKS_16m_PARAM
#define PART_BLOB_START		0x00000000
#define PART_BLOB_LEN		(NUM_BLOB_BLOCKS * FLASH_BLOCKSIZE_PARAM)

/* kernel */
#define NUM_KERNEL_BLOCKS	7
#define PART_KERNEL_START	(PART_BLOB_START + PART_BLOB_LEN)
#define PART_KERNEL_LEN		(NUM_KERNEL_BLOCKS * FLASH_BLOCKSIZE_MAIN)

/* initial ramdisk */
#define NUM_INITRD_BLOCKS	24
#define PART_INITRD_START	(PART_KERNEL_START + PART_KERNEL_LEN)
#define PART_INITRD_LEN		(NUM_INITRD_BLOCKS * FLASH_BLOCKSIZE_MAIN)

/*
 * See section 4.0 in "3 Volt Fast Boot Block Flash Memory" Intel Datasheet
 */
#define READ_ARRAY			0x00FF00FF		/* Read Array/Reset */
#define READ_ID_CODES		0x00900090		/* Read Identifier Codes */
#define ERASE_SETUP			0x00200020		/* Block Erase */
#define ERASE_CONFIRM		0x00D000D0		/* Block Erase and Program Resume */
#define PGM_SETUP			0x00400040		/* Program */
#define STATUS_READ			0x00700070		/* Read Status Register */
#define STATUS_CLEAR		0x00500050		/* Clear Status Register */
#define STATUS_BUSY			0x00800080		/* Write State Machine Status (WSMS) */
#define STATUS_ERASE_ERR	0x00200020		/* Erase Status (ES) */
#define STATUS_PGM_ERR		0x00100010		/* Program Status (PS) */

/*
 * See section 4.2 in "3 Volt Fast Boot Block Flash Memory" Intel Datasheet
 */
#define FLASH_MANUFACTURER			0x00890089
#define FLASH_DEVICE_8mbit_TOP		0x88f188f1
#define FLASH_DEVICE_8mbit_BOTTOM	0x88f288f2
#define FLASH_DEVICE_16mbit_TOP		0x88f388f3
#define FLASH_DEVICE_16mbit_BOTTOM	0x88f488f4

/***************************************************************************************************/

/*
 * The data line mapping on LART is as follows:
 *
 *   	 U2  CPU |   U3  CPU
 *   	 -------------------
 *   	  0  20  |   0   12
 *   	  1  22  |   1   14
 *   	  2  19  |   2   11
 *   	  3  17  |   3   9
 *   	  4  24  |   4   0
 *   	  5  26  |   5   2
 *   	  6  31  |   6   7
 *   	  7  29  |   7   5
 *   	  8  21  |   8   13
 *   	  9  23  |   9   15
 *   	  10 18  |   10  10
 *   	  11 16  |   11  8
 *   	  12 25  |   12  1
 *   	  13 27  |   13  3
 *   	  14 30  |   14  6
 *   	  15 28  |   15  4
 */

/* Mangle data (x) */
#define DATA_TO_FLASH(x)				\
	(									\
		(((x) & 0x08009000) >> 11)	+	\
		(((x) & 0x00002000) >> 10)	+	\
		(((x) & 0x04004000) >> 8)	+	\
		(((x) & 0x00000010) >> 4)	+	\
		(((x) & 0x91000820) >> 3)	+	\
		(((x) & 0x22080080) >> 2)	+	\
		((x) & 0x40000400)			+	\
		(((x) & 0x00040040) << 1)	+	\
		(((x) & 0x00110000) << 4)	+	\
		(((x) & 0x00220100) << 5)	+	\
		(((x) & 0x00800208) << 6)	+	\
		(((x) & 0x00400004) << 9)	+	\
		(((x) & 0x00000001) << 12)	+	\
		(((x) & 0x00000002) << 13)		\
	)

/* Unmangle data (x) */
#define FLASH_TO_DATA(x)				\
	(									\
		(((x) & 0x00010012) << 11)	+	\
		(((x) & 0x00000008) << 10)	+	\
		(((x) & 0x00040040) << 8)	+	\
		(((x) & 0x00000001) << 4)	+	\
		(((x) & 0x12200104) << 3)	+	\
		(((x) & 0x08820020) << 2)	+	\
		((x) & 0x40000400)			+	\
		(((x) & 0x00080080) >> 1)	+	\
		(((x) & 0x01100000) >> 4)	+	\
		(((x) & 0x04402000) >> 5)	+	\
		(((x) & 0x20008200) >> 6)	+	\
		(((x) & 0x80000800) >> 9)	+	\
		(((x) & 0x00001000) >> 12)	+	\
		(((x) & 0x00004000) >> 13)		\
	)

/*
 * The address line mapping on LART is as follows:
 *
 *   	 U3  CPU |   U2  CPU
 *   	 -------------------
 *   	  0  2   |   0   2
 *   	  1  3   |   1   3
 *   	  2  9   |   2   9
 *   	  3  13  |   3   8
 *   	  4  8   |   4   7
 *   	  5  12  |   5   6
 *   	  6  11  |   6   5
 *   	  7  10  |   7   4
 *   	  8  4   |   8   10
 *   	  9  5   |   9   11
 *   	 10  6   |   10  12
 *   	 11  7   |   11  13
 *
 *   	 BOOT BLOCK BOUNDARY
 *
 *   	 12  15  |   12  15
 *   	 13  14  |   13  14
 *   	 14  16  |   14  16
 *
 *   	 MAIN BLOCK BOUNDARY
 *
 *   	 15  17  |   15  18
 *   	 16  18  |   16  17
 *   	 17  20  |   17  20
 *   	 18  19  |   18  19
 *   	 19  21  |   19  21
 *
 * As we can see from above, the addresses aren't mangled across
 * block boundaries, so we don't need to worry about address
 * translations except for sending/reading commands during
 * initialization
 */

/* Mangle address (x) on chip U2 */
#define ADDR_TO_FLASH_U2(x)				\
	(									\
		(((x) & 0x00000f00) >> 4)	+	\
		(((x) & 0x00042000) << 1)	+	\
		(((x) & 0x0009c003) << 2)	+	\
		(((x) & 0x00021080) << 3)	+	\
		(((x) & 0x00000010) << 4)	+	\
		(((x) & 0x00000040) << 5)	+	\
		(((x) & 0x00000024) << 7)	+	\
		(((x) & 0x00000008) << 10)		\
	)

/* Unmangle address (x) on chip U2 */
#define FLASH_U2_TO_ADDR(x)				\
	(									\
		(((x) << 4) & 0x00000f00)	+	\
		(((x) >> 1) & 0x00042000)	+	\
		(((x) >> 2) & 0x0009c003)	+	\
		(((x) >> 3) & 0x00021080)	+	\
		(((x) >> 4) & 0x00000010)	+	\
		(((x) >> 5) & 0x00000040)	+	\
		(((x) >> 7) & 0x00000024)	+	\
		(((x) >> 10) & 0x00000008)		\
	)

/* Mangle address (x) on chip U3 */
#define ADDR_TO_FLASH_U3(x)				\
	(									\
		(((x) & 0x00000080) >> 3)	+	\
		(((x) & 0x00000040) >> 1)	+	\
		(((x) & 0x00052020) << 1)	+	\
		(((x) & 0x00084f03) << 2)	+	\
		(((x) & 0x00029010) << 3)	+	\
		(((x) & 0x00000008) << 5)	+	\
		(((x) & 0x00000004) << 7)		\
	)

/* Unmangle address (x) on chip U3 */
#define FLASH_U3_TO_ADDR(x)				\
	(									\
		(((x) << 3) & 0x00000080)	+	\
		(((x) << 1) & 0x00000040)	+	\
		(((x) >> 1) & 0x00052020)	+	\
		(((x) >> 2) & 0x00084f03)	+	\
		(((x) >> 3) & 0x00029010)	+	\
		(((x) >> 5) & 0x00000008)	+	\
		(((x) >> 7) & 0x00000004)		\
	)

/***************************************************************************************************/

static __u8 read8 (__u32 offset)
{
   volatile __u8 *data = (__u8 *) (FLASH_OFFSET + offset);
#ifdef LART_DEBUG
   printk (KERN_DEBUG "%s(): 0x%.8x -> 0x%.2x\n", __func__, offset, *data);
#endif
   return (*data);
}

static __u32 read32 (__u32 offset)
{
   volatile __u32 *data = (__u32 *) (FLASH_OFFSET + offset);
#ifdef LART_DEBUG
   printk (KERN_DEBUG "%s(): 0x%.8x -> 0x%.8x\n", __func__, offset, *data);
#endif
   return (*data);
}

static void write32 (__u32 x,__u32 offset)
{
   volatile __u32 *data = (__u32 *) (FLASH_OFFSET + offset);
   *data = x;
#ifdef LART_DEBUG
   printk (KERN_DEBUG "%s(): 0x%.8x <- 0x%.8x\n", __func__, offset, *data);
#endif
}

/***************************************************************************************************/

/*
 * Probe for 16mbit flash memory on a LART board without doing
 * too much damage. Since we need to write 1 dword to memory,
 * we're f**cked if this happens to be DRAM since we can't
 * restore the memory (otherwise we might exit Read Array mode).
 *
 * Returns 1 if we found 16mbit flash memory on LART, 0 otherwise.
 */
static int flash_probe (void)
{
   __u32 manufacturer,devtype;

   /* setup "Read Identifier Codes" mode */
   write32 (DATA_TO_FLASH (READ_ID_CODES),0x00000000);

   /* probe U2. U2/U3 returns the same data since the first 3
	* address lines is mangled in the same way */
   manufacturer = FLASH_TO_DATA (read32 (ADDR_TO_FLASH_U2 (0x00000000)));
   devtype = FLASH_TO_DATA (read32 (ADDR_TO_FLASH_U2 (0x00000001)));

   /* put the flash back into command mode */
   write32 (DATA_TO_FLASH (READ_ARRAY),0x00000000);

   return (manufacturer == FLASH_MANUFACTURER && (devtype == FLASH_DEVICE_16mbit_TOP || devtype == FLASH_DEVICE_16mbit_BOTTOM));
}

/*
 * Erase one block of flash memory at offset ``offset'' which is any
 * address within the block which should be erased.
 *
 * Returns 1 if successful, 0 otherwise.
 */
static inline int erase_block (__u32 offset)
{
   __u32 status;

#ifdef LART_DEBUG
   printk (KERN_DEBUG "%s(): 0x%.8x\n", __func__, offset);
#endif

   /* erase and confirm */
   write32 (DATA_TO_FLASH (ERASE_SETUP),offset);
   write32 (DATA_TO_FLASH (ERASE_CONFIRM),offset);

   /* wait for block erase to finish */
   do
	 {
		write32 (DATA_TO_FLASH (STATUS_READ),offset);
		status = FLASH_TO_DATA (read32 (offset));
	 }
   while ((~status & STATUS_BUSY) != 0);

   /* put the flash back into command mode */
   write32 (DATA_TO_FLASH (READ_ARRAY),offset);

   /* was the erase successful? */
   if ((status & STATUS_ERASE_ERR))
	 {
		printk (KERN_WARNING "%s: erase error at address 0x%.8x.\n",module_name,offset);
		return (0);
	 }

   return (1);
}

static int flash_erase (struct mtd_info *mtd,struct erase_info *instr)
{
   __u32 addr,len;
   int i,first;

#ifdef LART_DEBUG
   printk (KERN_DEBUG "%s(addr = 0x%.8x, len = %d)\n", __func__, instr->addr, instr->len);
#endif

   /*
	* check that both start and end of the requested erase are
	* aligned with the erasesize at the appropriate addresses.
	*
	* skip all erase regions which are ended before the start of
	* the requested erase. Actually, to save on the calculations,
	* we skip to the first erase region which starts after the
	* start of the requested erase, and then go back one.
	*/
   for (i = 0; i < mtd->numeraseregions && instr->addr >= mtd->eraseregions[i].offset; i++) ;
   i--;

   /*
	* ok, now i is pointing at the erase region in which this
	* erase request starts. Check the start of the requested
	* erase range is aligned with the erase size which is in
	* effect here.
	*/
   if (i < 0 || (instr->addr & (mtd->eraseregions[i].erasesize - 1)))
      return -EINVAL;

   /* Remember the erase region we start on */
   first = i;

   /*
	* next, check that the end of the requested erase is aligned
	* with the erase region at that address.
	*
	* as before, drop back one to point at the region in which
	* the address actually falls
	*/
   for (; i < mtd->numeraseregions && instr->addr + instr->len >= mtd->eraseregions[i].offset; i++) ;
   i--;

   /* is the end aligned on a block boundary? */
   if (i < 0 || ((instr->addr + instr->len) & (mtd->eraseregions[i].erasesize - 1)))
      return -EINVAL;

   addr = instr->addr;
   len = instr->len;

   i = first;

   /* now erase those blocks */
   while (len)
	 {
		if (!erase_block (addr))
			 return (-EIO);

		addr += mtd->eraseregions[i].erasesize;
		len -= mtd->eraseregions[i].erasesize;

		if (addr == mtd->eraseregions[i].offset + (mtd->eraseregions[i].erasesize * mtd->eraseregions[i].numblocks)) i++;
	 }

   return (0);
}

static int flash_read (struct mtd_info *mtd,loff_t from,size_t len,size_t *retlen,u_char *buf)
{
#ifdef LART_DEBUG
   printk (KERN_DEBUG "%s(from = 0x%.8x, len = %d)\n", __func__, (__u32)from, len);
#endif

   /* we always read len bytes */
   *retlen = len;

   /* first, we read bytes until we reach a dword boundary */
   if (from & (BUSWIDTH - 1))
	 {
		int gap = BUSWIDTH - (from & (BUSWIDTH - 1));

		while (len && gap--) {
			*buf++ = read8 (from++);
			len--;
		}
	 }

   /* now we read dwords until we reach a non-dword boundary */
   while (len >= BUSWIDTH)
	 {
		*((__u32 *) buf) = read32 (from);

		buf += BUSWIDTH;
		from += BUSWIDTH;
		len -= BUSWIDTH;
	 }

   /* top up the last unaligned bytes */
   if (len & (BUSWIDTH - 1))
	 while (len--) *buf++ = read8 (from++);

   return (0);
}

/*
 * Write one dword ``x'' to flash memory at offset ``offset''. ``offset''
 * must be 32 bits, i.e. it must be on a dword boundary.
 *
 * Returns 1 if successful, 0 otherwise.
 */
static inline int write_dword (__u32 offset,__u32 x)
{
   __u32 status;

#ifdef LART_DEBUG
   printk (KERN_DEBUG "%s(): 0x%.8x <- 0x%.8x\n", __func__, offset, x);
#endif

   /* setup writing */
   write32 (DATA_TO_FLASH (PGM_SETUP),offset);

   /* write the data */
   write32 (x,offset);

   /* wait for the write to finish */
   do
	 {
		write32 (DATA_TO_FLASH (STATUS_READ),offset);
		status = FLASH_TO_DATA (read32 (offset));
	 }
   while ((~status & STATUS_BUSY) != 0);

   /* put the flash back into command mode */
   write32 (DATA_TO_FLASH (READ_ARRAY),offset);

   /* was the write successful? */
   if ((status & STATUS_PGM_ERR) || read32 (offset) != x)
	 {
		printk (KERN_WARNING "%s: write error at address 0x%.8x.\n",module_name,offset);
		return (0);
	 }

   return (1);
}

static int flash_write (struct mtd_info *mtd,loff_t to,size_t len,size_t *retlen,const u_char *buf)
{
   __u8 tmp[4];
   int i,n;

#ifdef LART_DEBUG
   printk (KERN_DEBUG "%s(to = 0x%.8x, len = %d)\n", __func__, (__u32)to, len);
#endif

   /* sanity checks */
   if (!len) return (0);

   /* first, we write a 0xFF.... padded byte until we reach a dword boundary */
   if (to & (BUSWIDTH - 1))
	 {
		__u32 aligned = to & ~(BUSWIDTH - 1);
		int gap = to - aligned;

		i = n = 0;

		while (gap--) tmp[i++] = 0xFF;
		while (len && i < BUSWIDTH) {
			tmp[i++] = buf[n++];
			len--;
		}
		while (i < BUSWIDTH) tmp[i++] = 0xFF;

		if (!write_dword (aligned,*((__u32 *) tmp))) return (-EIO);

		to += n;
		buf += n;
		*retlen += n;
	 }

   /* now we write dwords until we reach a non-dword boundary */
   while (len >= BUSWIDTH)
	 {
		if (!write_dword (to,*((__u32 *) buf))) return (-EIO);

		to += BUSWIDTH;
		buf += BUSWIDTH;
		*retlen += BUSWIDTH;
		len -= BUSWIDTH;
	 }

   /* top up the last unaligned bytes, padded with 0xFF.... */
   if (len & (BUSWIDTH - 1))
	 {
		i = n = 0;

		while (len--) tmp[i++] = buf[n++];
		while (i < BUSWIDTH) tmp[i++] = 0xFF;

		if (!write_dword (to,*((__u32 *) tmp))) return (-EIO);

		*retlen += n;
	 }

   return (0);
}

/***************************************************************************************************/

static struct mtd_info mtd;

static struct mtd_erase_region_info erase_regions[] = {
	/* parameter blocks */
	{
		.offset		= 0x00000000,
		.erasesize	= FLASH_BLOCKSIZE_PARAM,
		.numblocks	= FLASH_NUMBLOCKS_16m_PARAM,
	},
	/* main blocks */
	{
		.offset	 = FLASH_BLOCKSIZE_PARAM * FLASH_NUMBLOCKS_16m_PARAM,
		.erasesize	= FLASH_BLOCKSIZE_MAIN,
		.numblocks	= FLASH_NUMBLOCKS_16m_MAIN,
	}
};

static const struct mtd_partition lart_partitions[] = {
	/* blob */
	{
		.name	= "blob",
		.offset	= PART_BLOB_START,
		.size	= PART_BLOB_LEN,
	},
	/* kernel */
	{
		.name	= "kernel",
		.offset	= PART_KERNEL_START,	/* MTDPART_OFS_APPEND */
		.size	= PART_KERNEL_LEN,
	},
	/* initial ramdisk / file system */
	{
		.name	= "file system",
		.offset	= PART_INITRD_START,	/* MTDPART_OFS_APPEND */
		.size	= PART_INITRD_LEN,	/* MTDPART_SIZ_FULL */
	}
};
#define NUM_PARTITIONS ARRAY_SIZE(lart_partitions)

static int __init lart_flash_init (void)
{
   int result;
   memset (&mtd,0,sizeof (mtd));
   printk ("MTD driver for LART. Written by Abraham vd Merwe <abraham@2d3d.co.za>\n");
   printk ("%s: Probing for 28F160x3 flash on LART...\n",module_name);
   if (!flash_probe ())
	 {
		printk (KERN_WARNING "%s: Found no LART compatible flash device\n",module_name);
		return (-ENXIO);
	 }
   printk ("%s: This looks like a LART board to me.\n",module_name);
   mtd.name = module_name;
   mtd.type = MTD_NORFLASH;
   mtd.writesize = 1;
   mtd.writebufsize = 4;
   mtd.flags = MTD_CAP_NORFLASH;
   mtd.size = FLASH_BLOCKSIZE_PARAM * FLASH_NUMBLOCKS_16m_PARAM + FLASH_BLOCKSIZE_MAIN * FLASH_NUMBLOCKS_16m_MAIN;
   mtd.erasesize = FLASH_BLOCKSIZE_MAIN;
   mtd.numeraseregions = ARRAY_SIZE(erase_regions);
   mtd.eraseregions = erase_regions;
   mtd._erase = flash_erase;
   mtd._read = flash_read;
   mtd._write = flash_write;
   mtd.owner = THIS_MODULE;

#ifdef LART_DEBUG
   printk (KERN_DEBUG
		   "mtd.name = %s\n"
		   "mtd.size = 0x%.8x (%uM)\n"
		   "mtd.erasesize = 0x%.8x (%uK)\n"
		   "mtd.numeraseregions = %d\n",
		   mtd.name,
		   mtd.size,mtd.size / (1024*1024),
		   mtd.erasesize,mtd.erasesize / 1024,
		   mtd.numeraseregions);

   if (mtd.numeraseregions)
	 for (result = 0; result < mtd.numeraseregions; result++)
	   printk (KERN_DEBUG
			   "\n\n"
			   "mtd.eraseregions[%d].offset = 0x%.8x\n"
			   "mtd.eraseregions[%d].erasesize = 0x%.8x (%uK)\n"
			   "mtd.eraseregions[%d].numblocks = %d\n",
			   result,mtd.eraseregions[result].offset,
			   result,mtd.eraseregions[result].erasesize,mtd.eraseregions[result].erasesize / 1024,
			   result,mtd.eraseregions[result].numblocks);

   printk ("\npartitions = %d\n", ARRAY_SIZE(lart_partitions));

   for (result = 0; result < ARRAY_SIZE(lart_partitions); result++)
	 printk (KERN_DEBUG
			 "\n\n"
			 "lart_partitions[%d].name = %s\n"
			 "lart_partitions[%d].offset = 0x%.8x\n"
			 "lart_partitions[%d].size = 0x%.8x (%uK)\n",
			 result,lart_partitions[result].name,
			 result,lart_partitions[result].offset,
			 result,lart_partitions[result].size,lart_partitions[result].size / 1024);
#endif

   result = mtd_device_register(&mtd, lart_partitions,
                                ARRAY_SIZE(lart_partitions));

   return (result);
}

static void __exit lart_flash_exit (void)
{
   mtd_device_unregister(&mtd);
}

module_init (lart_flash_init);
module_exit (lart_flash_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abraham vd Merwe <abraham@2d3d.co.za>");
MODULE_DESCRIPTION("MTD driver for Intel 28F160F3 on LART board");
