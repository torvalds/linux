/* elan-104nc.c -- MTD map driver for Arcom Control Systems ELAN-104NC
 
   Copyright (C) 2000 Arcom Control System Ltd
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA

   $Id: elan-104nc.c,v 1.25 2004/11/28 09:40:39 dwmw2 Exp $

The ELAN-104NC has up to 8 Mibyte of Intel StrataFlash (28F320/28F640) in x16
mode.  This drivers uses the CFI probe and Intel Extended Command Set drivers.

The flash is accessed as follows:

   32 kbyte memory window at 0xb0000-0xb7fff
   
   16 bit I/O port (0x22) for some sort of paging.

The single flash device is divided into 3 partition which appear as separate
MTD devices.

Linux thinks that the I/O port is used by the PIC and hence check_region() will
always fail.  So we don't do it.  I just hope it doesn't break anything.
*/
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <asm/io.h>

#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define WINDOW_START 0xb0000
/* Number of bits in offset. */
#define WINDOW_SHIFT 15
#define WINDOW_LENGTH (1 << WINDOW_SHIFT)
/* The bits for the offset into the window. */
#define WINDOW_MASK (WINDOW_LENGTH-1)
#define PAGE_IO 0x22
#define PAGE_IO_SIZE 2

static volatile int page_in_window = -1; // Current page in window.
static void __iomem *iomapadr;
static DEFINE_SPINLOCK(elan_104nc_spin);

/* partition_info gives details on the logical partitions that the split the 
 * single flash device into. If the size if zero we use up to the end of the
 * device. */
static struct mtd_partition partition_info[]={
    { .name = "ELAN-104NC flash boot partition", 
      .offset = 0, 
      .size = 640*1024 },
    { .name = "ELAN-104NC flash partition 1", 
      .offset = 640*1024, 
      .size = 896*1024 },
    { .name = "ELAN-104NC flash partition 2", 
      .offset = (640+896)*1024 }
};
#define NUM_PARTITIONS (sizeof(partition_info)/sizeof(partition_info[0]))

/*
 * If no idea what is going on here.  This is taken from the FlashFX stuff.
 */
#define ROMCS 1

static inline void elan_104nc_setup(void)
{
    u16 t;

    outw( 0x0023 + ROMCS*2, PAGE_IO );
    t=inb( PAGE_IO+1 );

    t=(t & 0xf9) | 0x04;

    outw( ((0x0023 + ROMCS*2) | (t << 8)), PAGE_IO );
}

static inline void elan_104nc_page(struct map_info *map, unsigned long ofs)
{
	unsigned long page = ofs >> WINDOW_SHIFT;
       
	if( page!=page_in_window ) {
		int cmd1;
		int cmd2;

		cmd1=(page & 0x700) + 0x0833 + ROMCS*0x4000;
		cmd2=((page & 0xff) << 8) + 0x0032;

		outw( cmd1, PAGE_IO );
		outw( cmd2, PAGE_IO );

		page_in_window = page;
	}
}


static map_word elan_104nc_read16(struct map_info *map, unsigned long ofs)
{
	map_word ret;
	spin_lock(&elan_104nc_spin);
	elan_104nc_page(map, ofs);
	ret.x[0] = readw(iomapadr + (ofs & WINDOW_MASK));
	spin_unlock(&elan_104nc_spin);
	return ret;
}

static void elan_104nc_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	while (len) {
		unsigned long thislen = len;
		if (len > (WINDOW_LENGTH - (from & WINDOW_MASK)))
			thislen = WINDOW_LENGTH-(from & WINDOW_MASK);
		
		spin_lock(&elan_104nc_spin);
		elan_104nc_page(map, from);
		memcpy_fromio(to, iomapadr + (from & WINDOW_MASK), thislen);
		spin_unlock(&elan_104nc_spin);
		to += thislen;
		from += thislen;
		len -= thislen;
	}
}

static void elan_104nc_write16(struct map_info *map, map_word d, unsigned long adr)
{
	spin_lock(&elan_104nc_spin);
	elan_104nc_page(map, adr);
	writew(d.x[0], iomapadr + (adr & WINDOW_MASK));
	spin_unlock(&elan_104nc_spin);
}

static void elan_104nc_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{	
	while(len) {
		unsigned long thislen = len;
		if (len > (WINDOW_LENGTH - (to & WINDOW_MASK)))
			thislen = WINDOW_LENGTH-(to & WINDOW_MASK);
		
		spin_lock(&elan_104nc_spin);
		elan_104nc_page(map, to);
		memcpy_toio(iomapadr + (to & WINDOW_MASK), from, thislen);
		spin_unlock(&elan_104nc_spin);
		to += thislen;
		from += thislen;
		len -= thislen;
	}
}

static struct map_info elan_104nc_map = {
	.name = "ELAN-104NC flash",
	.phys = NO_XIP,
	.size = 8*1024*1024, /* this must be set to a maximum possible amount
			of flash so the cfi probe routines find all
			the chips */
	.bankwidth = 2,
	.read = elan_104nc_read16,
	.copy_from = elan_104nc_copy_from,
	.write = elan_104nc_write16,
	.copy_to = elan_104nc_copy_to
};

/* MTD device for all of the flash. */
static struct mtd_info *all_mtd;

static void cleanup_elan_104nc(void)
{
	if( all_mtd ) {
		del_mtd_partitions( all_mtd );
		map_destroy( all_mtd );
	}

	iounmap(iomapadr);
}

static int __init init_elan_104nc(void)
{
	/* Urg! We use I/O port 0x22 without request_region()ing it,
	   because it's already allocated to the PIC. */

  	iomapadr = ioremap(WINDOW_START, WINDOW_LENGTH);
	if (!iomapadr) {
		printk( KERN_ERR"%s: failed to ioremap memory region\n",
			elan_104nc_map.name );
		return -EIO;
	}

	printk( KERN_INFO"%s: IO:0x%x-0x%x MEM:0x%x-0x%x\n",
		elan_104nc_map.name,
		PAGE_IO, PAGE_IO+PAGE_IO_SIZE-1,
		WINDOW_START, WINDOW_START+WINDOW_LENGTH-1 );

	elan_104nc_setup();

	/* Probe for chip. */
	all_mtd = do_map_probe("cfi_probe",  &elan_104nc_map );
	if( !all_mtd ) {
		cleanup_elan_104nc();
		return -ENXIO;
	}
	
	all_mtd->owner = THIS_MODULE;

	/* Create MTD devices for each partition. */
	add_mtd_partitions( all_mtd, partition_info, NUM_PARTITIONS );

	return 0;
}

module_init(init_elan_104nc);
module_exit(cleanup_elan_104nc);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arcom Control Systems Ltd.");
MODULE_DESCRIPTION("MTD map driver for Arcom Control Systems ELAN-104NC");
