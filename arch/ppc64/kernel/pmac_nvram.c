/*
 *  arch/ppc/platforms/pmac_nvram.c
 *
 *  Copyright (C) 2002 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Todo: - add support for the OF persistent properties
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/bootmem.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/nvram.h>

#define DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#define NVRAM_SIZE		0x2000	/* 8kB of non-volatile RAM */

#define CORE99_SIGNATURE	0x5a
#define CORE99_ADLER_START	0x14

/* On Core99, nvram is either a sharp, a micron or an AMD flash */
#define SM_FLASH_STATUS_DONE	0x80
#define SM_FLASH_STATUS_ERR	0x38

#define SM_FLASH_CMD_ERASE_CONFIRM	0xd0
#define SM_FLASH_CMD_ERASE_SETUP	0x20
#define SM_FLASH_CMD_RESET		0xff
#define SM_FLASH_CMD_WRITE_SETUP	0x40
#define SM_FLASH_CMD_CLEAR_STATUS	0x50
#define SM_FLASH_CMD_READ_STATUS	0x70

/* CHRP NVRAM header */
struct chrp_header {
  u8		signature;
  u8		cksum;
  u16		len;
  char          name[12];
  u8		data[0];
};

struct core99_header {
  struct chrp_header	hdr;
  u32			adler;
  u32			generation;
  u32			reserved[2];
};

/*
 * Read and write the non-volatile RAM on PowerMacs and CHRP machines.
 */
static volatile unsigned char *nvram_data;
static int core99_bank = 0;
// XXX Turn that into a sem
static DEFINE_SPINLOCK(nv_lock);

extern int system_running;

static int (*core99_write_bank)(int bank, u8* datas);
static int (*core99_erase_bank)(int bank);

static char *nvram_image __pmacdata;


static ssize_t __pmac core99_nvram_read(char *buf, size_t count, loff_t *index)
{
	int i;

	if (nvram_image == NULL)
		return -ENODEV;
	if (*index > NVRAM_SIZE)
		return 0;

	i = *index;
	if (i + count > NVRAM_SIZE)
		count = NVRAM_SIZE - i;

	memcpy(buf, &nvram_image[i], count);
	*index = i + count;
	return count;
}

static ssize_t __pmac core99_nvram_write(char *buf, size_t count, loff_t *index)
{
	int i;

	if (nvram_image == NULL)
		return -ENODEV;
	if (*index > NVRAM_SIZE)
		return 0;

	i = *index;
	if (i + count > NVRAM_SIZE)
		count = NVRAM_SIZE - i;

	memcpy(&nvram_image[i], buf, count);
	*index = i + count;
	return count;
}

static ssize_t __pmac core99_nvram_size(void)
{
	if (nvram_image == NULL)
		return -ENODEV;
	return NVRAM_SIZE;
}

static u8 __pmac chrp_checksum(struct chrp_header* hdr)
{
	u8 *ptr;
	u16 sum = hdr->signature;
	for (ptr = (u8 *)&hdr->len; ptr < hdr->data; ptr++)
		sum += *ptr;
	while (sum > 0xFF)
		sum = (sum & 0xFF) + (sum>>8);
	return sum;
}

static u32 __pmac core99_calc_adler(u8 *buffer)
{
	int cnt;
	u32 low, high;

   	buffer += CORE99_ADLER_START;
	low = 1;
	high = 0;
	for (cnt=0; cnt<(NVRAM_SIZE-CORE99_ADLER_START); cnt++) {
		if ((cnt % 5000) == 0) {
			high  %= 65521UL;
			high %= 65521UL;
		}
		low += buffer[cnt];
		high += low;
	}
	low  %= 65521UL;
	high %= 65521UL;

	return (high << 16) | low;
}

static u32 __pmac core99_check(u8* datas)
{
	struct core99_header* hdr99 = (struct core99_header*)datas;

	if (hdr99->hdr.signature != CORE99_SIGNATURE) {
		DBG("Invalid signature\n");
		return 0;
	}
	if (hdr99->hdr.cksum != chrp_checksum(&hdr99->hdr)) {
		DBG("Invalid checksum\n");
		return 0;
	}
	if (hdr99->adler != core99_calc_adler(datas)) {
		DBG("Invalid adler\n");
		return 0;
	}
	return hdr99->generation;
}

static int __pmac sm_erase_bank(int bank)
{
	int stat, i;
	unsigned long timeout;

	u8* base = (u8 *)nvram_data + core99_bank*NVRAM_SIZE;

       	DBG("nvram: Sharp/Micron Erasing bank %d...\n", bank);

	out_8(base, SM_FLASH_CMD_ERASE_SETUP);
	out_8(base, SM_FLASH_CMD_ERASE_CONFIRM);
	timeout = 0;
	do {
		if (++timeout > 1000000) {
			printk(KERN_ERR "nvram: Sharp/Miron flash erase timeout !\n");
			break;
		}
		out_8(base, SM_FLASH_CMD_READ_STATUS);
		stat = in_8(base);
	} while (!(stat & SM_FLASH_STATUS_DONE));

	out_8(base, SM_FLASH_CMD_CLEAR_STATUS);
	out_8(base, SM_FLASH_CMD_RESET);

	for (i=0; i<NVRAM_SIZE; i++)
		if (base[i] != 0xff) {
			printk(KERN_ERR "nvram: Sharp/Micron flash erase failed !\n");
			return -ENXIO;
		}
	return 0;
}

static int __pmac sm_write_bank(int bank, u8* datas)
{
	int i, stat = 0;
	unsigned long timeout;

	u8* base = (u8 *)nvram_data + core99_bank*NVRAM_SIZE;

       	DBG("nvram: Sharp/Micron Writing bank %d...\n", bank);

	for (i=0; i<NVRAM_SIZE; i++) {
		out_8(base+i, SM_FLASH_CMD_WRITE_SETUP);
		udelay(1);
		out_8(base+i, datas[i]);
		timeout = 0;
		do {
			if (++timeout > 1000000) {
				printk(KERN_ERR "nvram: Sharp/Micron flash write timeout !\n");
				break;
			}
			out_8(base, SM_FLASH_CMD_READ_STATUS);
			stat = in_8(base);
		} while (!(stat & SM_FLASH_STATUS_DONE));
		if (!(stat & SM_FLASH_STATUS_DONE))
			break;
	}
	out_8(base, SM_FLASH_CMD_CLEAR_STATUS);
	out_8(base, SM_FLASH_CMD_RESET);
	for (i=0; i<NVRAM_SIZE; i++)
		if (base[i] != datas[i]) {
			printk(KERN_ERR "nvram: Sharp/Micron flash write failed !\n");
			return -ENXIO;
		}
	return 0;
}

static int __pmac amd_erase_bank(int bank)
{
	int i, stat = 0;
	unsigned long timeout;

	u8* base = (u8 *)nvram_data + core99_bank*NVRAM_SIZE;

       	DBG("nvram: AMD Erasing bank %d...\n", bank);

	/* Unlock 1 */
	out_8(base+0x555, 0xaa);
	udelay(1);
	/* Unlock 2 */
	out_8(base+0x2aa, 0x55);
	udelay(1);

	/* Sector-Erase */
	out_8(base+0x555, 0x80);
	udelay(1);
	out_8(base+0x555, 0xaa);
	udelay(1);
	out_8(base+0x2aa, 0x55);
	udelay(1);
	out_8(base, 0x30);
	udelay(1);

	timeout = 0;
	do {
		if (++timeout > 1000000) {
			printk(KERN_ERR "nvram: AMD flash erase timeout !\n");
			break;
		}
		stat = in_8(base) ^ in_8(base);
	} while (stat != 0);
	
	/* Reset */
	out_8(base, 0xf0);
	udelay(1);
	
	for (i=0; i<NVRAM_SIZE; i++)
		if (base[i] != 0xff) {
			printk(KERN_ERR "nvram: AMD flash erase failed !\n");
			return -ENXIO;
		}
	return 0;
}

static int __pmac amd_write_bank(int bank, u8* datas)
{
	int i, stat = 0;
	unsigned long timeout;

	u8* base = (u8 *)nvram_data + core99_bank*NVRAM_SIZE;

       	DBG("nvram: AMD Writing bank %d...\n", bank);

	for (i=0; i<NVRAM_SIZE; i++) {
		/* Unlock 1 */
		out_8(base+0x555, 0xaa);
		udelay(1);
		/* Unlock 2 */
		out_8(base+0x2aa, 0x55);
		udelay(1);

		/* Write single word */
		out_8(base+0x555, 0xa0);
		udelay(1);
		out_8(base+i, datas[i]);
		
		timeout = 0;
		do {
			if (++timeout > 1000000) {
				printk(KERN_ERR "nvram: AMD flash write timeout !\n");
				break;
			}
			stat = in_8(base) ^ in_8(base);
		} while (stat != 0);
		if (stat != 0)
			break;
	}

	/* Reset */
	out_8(base, 0xf0);
	udelay(1);

	for (i=0; i<NVRAM_SIZE; i++)
		if (base[i] != datas[i]) {
			printk(KERN_ERR "nvram: AMD flash write failed !\n");
			return -ENXIO;
		}
	return 0;
}


static int __pmac core99_nvram_sync(void)
{
	struct core99_header* hdr99;
	unsigned long flags;

	spin_lock_irqsave(&nv_lock, flags);
	if (!memcmp(nvram_image, (u8*)nvram_data + core99_bank*NVRAM_SIZE,
		NVRAM_SIZE))
		goto bail;

	DBG("Updating nvram...\n");

	hdr99 = (struct core99_header*)nvram_image;
	hdr99->generation++;
	hdr99->hdr.signature = CORE99_SIGNATURE;
	hdr99->hdr.cksum = chrp_checksum(&hdr99->hdr);
	hdr99->adler = core99_calc_adler(nvram_image);
	core99_bank = core99_bank ? 0 : 1;
	if (core99_erase_bank)
		if (core99_erase_bank(core99_bank)) {
			printk("nvram: Error erasing bank %d\n", core99_bank);
			goto bail;
		}
	if (core99_write_bank)
		if (core99_write_bank(core99_bank, nvram_image))
			printk("nvram: Error writing bank %d\n", core99_bank);
 bail:
	spin_unlock_irqrestore(&nv_lock, flags);

	return 0;
}

int __init pmac_nvram_init(void)
{
	struct device_node *dp;
	u32 gen_bank0, gen_bank1;
	int i;

	dp = find_devices("nvram");
	if (dp == NULL) {
		printk(KERN_ERR "Can't find NVRAM device\n");
		return -ENODEV;
	}
	if (!device_is_compatible(dp, "nvram,flash")) {
		printk(KERN_ERR "Incompatible type of NVRAM\n");
		return -ENXIO;
	}

	nvram_image = alloc_bootmem(NVRAM_SIZE);
	if (nvram_image == NULL) {
		printk(KERN_ERR "nvram: can't allocate ram image\n");
		return -ENOMEM;
	}
	nvram_data = ioremap(dp->addrs[0].address, NVRAM_SIZE*2);
	
	DBG("nvram: Checking bank 0...\n");

	gen_bank0 = core99_check((u8 *)nvram_data);
	gen_bank1 = core99_check((u8 *)nvram_data + NVRAM_SIZE);
	core99_bank = (gen_bank0 < gen_bank1) ? 1 : 0;

	DBG("nvram: gen0=%d, gen1=%d\n", gen_bank0, gen_bank1);
	DBG("nvram: Active bank is: %d\n", core99_bank);

	for (i=0; i<NVRAM_SIZE; i++)
		nvram_image[i] = nvram_data[i + core99_bank*NVRAM_SIZE];

	ppc_md.nvram_read	= core99_nvram_read;
	ppc_md.nvram_write	= core99_nvram_write;
	ppc_md.nvram_size	= core99_nvram_size;
	ppc_md.nvram_sync	= core99_nvram_sync;
	
	/* 
	 * Maybe we could be smarter here though making an exclusive list
	 * of known flash chips is a bit nasty as older OF didn't provide us
	 * with a useful "compatible" entry. A solution would be to really
	 * identify the chip using flash id commands and base ourselves on
	 * a list of known chips IDs
	 */
	if (device_is_compatible(dp, "amd-0137")) {
		core99_erase_bank = amd_erase_bank;
		core99_write_bank = amd_write_bank;
	} else {
		core99_erase_bank = sm_erase_bank;
		core99_write_bank = sm_write_bank;
	}

	return 0;
}

int __pmac pmac_get_partition(int partition)
{
	struct nvram_partition *part;
	const char *name;
	int sig;

	switch(partition) {
	case pmac_nvram_OF:
		name = "common";
		sig = NVRAM_SIG_SYS;
		break;
	case pmac_nvram_XPRAM:
		name = "APL,MacOS75";
		sig = NVRAM_SIG_OS;
		break;
	case pmac_nvram_NR:
	default:
		/* Oldworld stuff */
		return -ENODEV;
	}

	part = nvram_find_partition(sig, name);
	if (part == NULL)
		return 0;

	return part->index;
}

u8 __pmac pmac_xpram_read(int xpaddr)
{
	int offset = pmac_get_partition(pmac_nvram_XPRAM);
	loff_t index;
	u8 buf;
	ssize_t count;

	if (offset < 0 || xpaddr < 0 || xpaddr > 0x100)
		return 0xff;
	index = offset + xpaddr;

	count = ppc_md.nvram_read(&buf, 1, &index);
	if (count != 1)
		return 0xff;
	return buf;
}

void __pmac pmac_xpram_write(int xpaddr, u8 data)
{
	int offset = pmac_get_partition(pmac_nvram_XPRAM);
	loff_t index;
	u8 buf;

	if (offset < 0 || xpaddr < 0 || xpaddr > 0x100)
		return;
	index = offset + xpaddr;
	buf = data;

	ppc_md.nvram_write(&buf, 1, &index);
}

EXPORT_SYMBOL(pmac_get_partition);
EXPORT_SYMBOL(pmac_xpram_read);
EXPORT_SYMBOL(pmac_xpram_write);
