/*
 *  linux/arch/arm/kernel/compat.c
 *
 *  Copyright (C) 2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * We keep the old params compatibility cruft in one place (here)
 * so we don't end up with lots of mess around other places.
 *
 * NOTE:
 *  The old struct param_struct is deprecated, but it will be kept in
 *  the kernel for 5 years from now (2001). This will allow boot loaders
 *  to convert to the new struct tag way.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/page.h>

#include <asm/mach/arch.h>

#include "compat.h"

/*
 * Usage:
 *  - do not go blindly adding fields, add them at the end
 *  - when adding fields, don't rely on the address until
 *    a patch from me has been released
 *  - unused fields should be zero (for future expansion)
 *  - this structure is relatively short-lived - only
 *    guaranteed to contain useful data in setup_arch()
 *
 * This is the old deprecated way to pass parameters to the kernel
 */
struct param_struct {
    union {
	struct {
	    unsigned long page_size;		/*  0 */
	    unsigned long nr_pages;		/*  4 */
	    unsigned long ramdisk_size;		/*  8 */
	    unsigned long flags;		/* 12 */
#define FLAG_READONLY	1
#define FLAG_RDLOAD	4
#define FLAG_RDPROMPT	8
	    unsigned long rootdev;		/* 16 */
	    unsigned long video_num_cols;	/* 20 */
	    unsigned long video_num_rows;	/* 24 */
	    unsigned long video_x;		/* 28 */
	    unsigned long video_y;		/* 32 */
	    unsigned long memc_control_reg;	/* 36 */
	    unsigned char sounddefault;		/* 40 */
	    unsigned char adfsdrives;		/* 41 */
	    unsigned char bytes_per_char_h;	/* 42 */
	    unsigned char bytes_per_char_v;	/* 43 */
	    unsigned long pages_in_bank[4];	/* 44 */
	    unsigned long pages_in_vram;	/* 60 */
	    unsigned long initrd_start;		/* 64 */
	    unsigned long initrd_size;		/* 68 */
	    unsigned long rd_start;		/* 72 */
	    unsigned long system_rev;		/* 76 */
	    unsigned long system_serial_low;	/* 80 */
	    unsigned long system_serial_high;	/* 84 */
	    unsigned long mem_fclk_21285;       /* 88 */
	} s;
	char unused[256];
    } u1;
    union {
	char paths[8][128];
	struct {
	    unsigned long magic;
	    char n[1024 - sizeof(unsigned long)];
	} s;
    } u2;
    char commandline[COMMAND_LINE_SIZE];
};

static struct tag * __init memtag(struct tag *tag, unsigned long start, unsigned long size)
{
	tag = tag_next(tag);
	tag->hdr.tag = ATAG_MEM;
	tag->hdr.size = tag_size(tag_mem32);
	tag->u.mem.size = size;
	tag->u.mem.start = start;

	return tag;
}

static void __init build_tag_list(struct param_struct *params, void *taglist)
{
	struct tag *tag = taglist;

	if (params->u1.s.page_size != PAGE_SIZE) {
		printk(KERN_WARNING "Warning: bad configuration page, "
		       "trying to continue\n");
		return;
	}

	printk(KERN_DEBUG "Converting old-style param struct to taglist\n");

#ifdef CONFIG_ARCH_NETWINDER
	if (params->u1.s.nr_pages != 0x02000 &&
	    params->u1.s.nr_pages != 0x04000 &&
	    params->u1.s.nr_pages != 0x08000 &&
	    params->u1.s.nr_pages != 0x10000) {
		printk(KERN_WARNING "Warning: bad NeTTrom parameters "
		       "detected, using defaults\n");

		params->u1.s.nr_pages = 0x1000;	/* 16MB */
		params->u1.s.ramdisk_size = 0;
		params->u1.s.flags = FLAG_READONLY;
		params->u1.s.initrd_start = 0;
		params->u1.s.initrd_size = 0;
		params->u1.s.rd_start = 0;
	}
#endif

	tag->hdr.tag  = ATAG_CORE;
	tag->hdr.size = tag_size(tag_core);
	tag->u.core.flags = params->u1.s.flags & FLAG_READONLY;
	tag->u.core.pagesize = params->u1.s.page_size;
	tag->u.core.rootdev = params->u1.s.rootdev;

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_RAMDISK;
	tag->hdr.size = tag_size(tag_ramdisk);
	tag->u.ramdisk.flags = (params->u1.s.flags & FLAG_RDLOAD ? 1 : 0) |
			       (params->u1.s.flags & FLAG_RDPROMPT ? 2 : 0);
	tag->u.ramdisk.size  = params->u1.s.ramdisk_size;
	tag->u.ramdisk.start = params->u1.s.rd_start;

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_INITRD;
	tag->hdr.size = tag_size(tag_initrd);
	tag->u.initrd.start = params->u1.s.initrd_start;
	tag->u.initrd.size  = params->u1.s.initrd_size;

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_SERIAL;
	tag->hdr.size = tag_size(tag_serialnr);
	tag->u.serialnr.low = params->u1.s.system_serial_low;
	tag->u.serialnr.high = params->u1.s.system_serial_high;

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_REVISION;
	tag->hdr.size = tag_size(tag_revision);
	tag->u.revision.rev = params->u1.s.system_rev;

#ifdef CONFIG_ARCH_ACORN
	if (machine_is_riscpc()) {
		int i;
		for (i = 0; i < 4; i++)
			tag = memtag(tag, PHYS_OFFSET + (i << 26),
				 params->u1.s.pages_in_bank[i] * PAGE_SIZE);
	} else
#endif
	tag = memtag(tag, PHYS_OFFSET, params->u1.s.nr_pages * PAGE_SIZE);

#ifdef CONFIG_FOOTBRIDGE
	if (params->u1.s.mem_fclk_21285) {
		tag = tag_next(tag);
		tag->hdr.tag = ATAG_MEMCLK;
		tag->hdr.size = tag_size(tag_memclk);
		tag->u.memclk.fmemclk = params->u1.s.mem_fclk_21285;
	}
#endif

#ifdef CONFIG_ARCH_EBSA285
	if (machine_is_ebsa285()) {
		tag = tag_next(tag);
		tag->hdr.tag = ATAG_VIDEOTEXT;
		tag->hdr.size = tag_size(tag_videotext);
		tag->u.videotext.x            = params->u1.s.video_x;
		tag->u.videotext.y            = params->u1.s.video_y;
		tag->u.videotext.video_page   = 0;
		tag->u.videotext.video_mode   = 0;
		tag->u.videotext.video_cols   = params->u1.s.video_num_cols;
		tag->u.videotext.video_ega_bx = 0;
		tag->u.videotext.video_lines  = params->u1.s.video_num_rows;
		tag->u.videotext.video_isvga  = 1;
		tag->u.videotext.video_points = 8;
	}
#endif

#ifdef CONFIG_ARCH_ACORN
	tag = tag_next(tag);
	tag->hdr.tag = ATAG_ACORN;
	tag->hdr.size = tag_size(tag_acorn);
	tag->u.acorn.memc_control_reg = params->u1.s.memc_control_reg;
	tag->u.acorn.vram_pages       = params->u1.s.pages_in_vram;
	tag->u.acorn.sounddefault     = params->u1.s.sounddefault;
	tag->u.acorn.adfsdrives       = params->u1.s.adfsdrives;
#endif

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_CMDLINE;
	tag->hdr.size = (strlen(params->commandline) + 3 +
			 sizeof(struct tag_header)) >> 2;
	strcpy(tag->u.cmdline.cmdline, params->commandline);

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_NONE;
	tag->hdr.size = 0;

	memmove(params, taglist, ((int)tag) - ((int)taglist) +
				 sizeof(struct tag_header));
}

void __init convert_to_tag_list(struct tag *tags)
{
	struct param_struct *params = (struct param_struct *)tags;
	build_tag_list(params, &params->u2);
}

void __init squash_mem_tags(struct tag *tag)
{
	for (; tag->hdr.size; tag = tag_next(tag))
		if (tag->hdr.tag == ATAG_MEM)
			tag->hdr.tag = ATAG_NONE;
}
