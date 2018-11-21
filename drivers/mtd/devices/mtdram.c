/*
 * mtdram - a test mtd device
 * Author: Alexander Larsson <alex@cendio.se>
 *
 * Copyright (c) 1999 Alexander Larsson <alex@cendio.se>
 * Copyright (c) 2005 Joern Engel <joern@wh.fh-wedel.de>
 *
 * This code is GPL
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/mtdram.h>

static unsigned long total_size = CONFIG_MTDRAM_TOTAL_SIZE;
static unsigned long erase_size = CONFIG_MTDRAM_ERASE_SIZE;
static unsigned long writebuf_size = 64;
#define MTDRAM_TOTAL_SIZE (total_size * 1024)
#define MTDRAM_ERASE_SIZE (erase_size * 1024)

#ifdef MODULE
module_param(total_size, ulong, 0);
MODULE_PARM_DESC(total_size, "Total device size in KiB");
module_param(erase_size, ulong, 0);
MODULE_PARM_DESC(erase_size, "Device erase block size in KiB");
module_param(writebuf_size, ulong, 0);
MODULE_PARM_DESC(writebuf_size, "Device write buf size in Bytes (Default: 64)");
#endif

// We could store these in the mtd structure, but we only support 1 device..
static struct mtd_info *mtd_info;

static int check_offs_len(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	int ret = 0;

	/* Start address must align on block boundary */
	if (mtd_mod_by_eb(ofs, mtd)) {
		pr_debug("%s: unaligned address\n", __func__);
		ret = -EINVAL;
	}

	/* Length must align on block boundary */
	if (mtd_mod_by_eb(len, mtd)) {
		pr_debug("%s: length not block aligned\n", __func__);
		ret = -EINVAL;
	}

	return ret;
}

static int ram_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	if (check_offs_len(mtd, instr->addr, instr->len))
		return -EINVAL;
	memset((char *)mtd->priv + instr->addr, 0xff, instr->len);

	return 0;
}

static int ram_point(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, void **virt, resource_size_t *phys)
{
	*virt = mtd->priv + from;
	*retlen = len;

	if (phys) {
		/* limit retlen to the number of contiguous physical pages */
		unsigned long page_ofs = offset_in_page(*virt);
		void *addr = *virt - page_ofs;
		unsigned long pfn1, pfn0 = vmalloc_to_pfn(addr);

		*phys = __pfn_to_phys(pfn0) + page_ofs;
		len += page_ofs;
		while (len > PAGE_SIZE) {
			len -= PAGE_SIZE;
			addr += PAGE_SIZE;
			pfn0++;
			pfn1 = vmalloc_to_pfn(addr);
			if (pfn1 != pfn0) {
				*retlen = addr - *virt;
				break;
			}
		}
	}

	return 0;
}

static int ram_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return 0;
}

static int ram_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	memcpy(buf, mtd->priv + from, len);
	*retlen = len;
	return 0;
}

static int ram_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	memcpy((char *)mtd->priv + to, buf, len);
	*retlen = len;
	return 0;
}

static void __exit cleanup_mtdram(void)
{
	if (mtd_info) {
		mtd_device_unregister(mtd_info);
		vfree(mtd_info->priv);
		kfree(mtd_info);
	}
}

int mtdram_init_device(struct mtd_info *mtd, void *mapped_address,
		unsigned long size, const char *name)
{
	memset(mtd, 0, sizeof(*mtd));

	/* Setup the MTD structure */
	mtd->name = name;
	mtd->type = MTD_RAM;
	mtd->flags = MTD_CAP_RAM;
	mtd->size = size;
	mtd->writesize = 1;
	mtd->writebufsize = writebuf_size;
	mtd->erasesize = MTDRAM_ERASE_SIZE;
	mtd->priv = mapped_address;

	mtd->owner = THIS_MODULE;
	mtd->_erase = ram_erase;
	mtd->_point = ram_point;
	mtd->_unpoint = ram_unpoint;
	mtd->_read = ram_read;
	mtd->_write = ram_write;

	if (mtd_device_register(mtd, NULL, 0))
		return -EIO;

	return 0;
}

static int __init init_mtdram(void)
{
	void *addr;
	int err;

	if (!total_size)
		return -EINVAL;

	/* Allocate some memory */
	mtd_info = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!mtd_info)
		return -ENOMEM;

	addr = vmalloc(MTDRAM_TOTAL_SIZE);
	if (!addr) {
		kfree(mtd_info);
		mtd_info = NULL;
		return -ENOMEM;
	}
	err = mtdram_init_device(mtd_info, addr, MTDRAM_TOTAL_SIZE, "mtdram test device");
	if (err) {
		vfree(addr);
		kfree(mtd_info);
		mtd_info = NULL;
		return err;
	}
	memset(mtd_info->priv, 0xff, MTDRAM_TOTAL_SIZE);
	return err;
}

module_init(init_mtdram);
module_exit(cleanup_mtdram);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Larsson <alexl@redhat.com>");
MODULE_DESCRIPTION("Simulated MTD driver for testing");
