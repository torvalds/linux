// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tag parsing.
 *
 * Copyright (C) 1995-2001 Russell King
 */

/*
 * This is the traditional way of passing data to the kernel at boot time.  Rather
 * than passing a fixed inflexible structure to the kernel, we pass a list
 * of variable-sized tags to the kernel.  The first tag must be a ATAG_CORE
 * tag for the list to be recognised (to distinguish the tagged list from
 * a param_struct).  The list is terminated with a zero-length tag (this tag
 * is not parsed in any way).
 */

#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/root_dev.h>
#include <linux/screen_info.h>
#include <linux/memblock.h>
#include <uapi/linux/mount.h>

#include <asm/setup.h>
#include <asm/system_info.h>
#include <asm/page.h>
#include <asm/mach/arch.h>

#include "atags.h"

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;

#ifndef MEM_SIZE
#define MEM_SIZE	(16*1024*1024)
#endif

static struct {
	struct tag_header hdr1;
	struct tag_core   core;
	struct tag_header hdr2;
	struct tag_mem32  mem;
	struct tag_header hdr3;
} default_tags __initdata = {
	{ tag_size(tag_core), ATAG_CORE },
	{ 1, PAGE_SIZE, 0xff },
	{ tag_size(tag_mem32), ATAG_MEM },
	{ MEM_SIZE },
	{ 0, ATAG_NONE }
};

static int __init parse_tag_core(const struct tag *tag)
{
	if (tag->hdr.size > 2) {
		if ((tag->u.core.flags & 1) == 0)
			root_mountflags &= ~MS_RDONLY;
		ROOT_DEV = old_decode_dev(tag->u.core.rootdev);
	}
	return 0;
}

__tagtable(ATAG_CORE, parse_tag_core);

static int __init parse_tag_mem32(const struct tag *tag)
{
	return arm_add_memory(tag->u.mem.start, tag->u.mem.size);
}

__tagtable(ATAG_MEM, parse_tag_mem32);

#if defined(CONFIG_VGA_CONSOLE) || defined(CONFIG_DUMMY_CONSOLE)
static int __init parse_tag_videotext(const struct tag *tag)
{
	screen_info.orig_x            = tag->u.videotext.x;
	screen_info.orig_y            = tag->u.videotext.y;
	screen_info.orig_video_page   = tag->u.videotext.video_page;
	screen_info.orig_video_mode   = tag->u.videotext.video_mode;
	screen_info.orig_video_cols   = tag->u.videotext.video_cols;
	screen_info.orig_video_ega_bx = tag->u.videotext.video_ega_bx;
	screen_info.orig_video_lines  = tag->u.videotext.video_lines;
	screen_info.orig_video_isVGA  = tag->u.videotext.video_isvga;
	screen_info.orig_video_points = tag->u.videotext.video_points;
	return 0;
}

__tagtable(ATAG_VIDEOTEXT, parse_tag_videotext);
#endif

#ifdef CONFIG_BLK_DEV_RAM
static int __init parse_tag_ramdisk(const struct tag *tag)
{
	rd_image_start = tag->u.ramdisk.start;

	if (tag->u.ramdisk.size)
		rd_size = tag->u.ramdisk.size;

	return 0;
}

__tagtable(ATAG_RAMDISK, parse_tag_ramdisk);
#endif

static int __init parse_tag_serialnr(const struct tag *tag)
{
	system_serial_low = tag->u.serialnr.low;
	system_serial_high = tag->u.serialnr.high;
	return 0;
}

__tagtable(ATAG_SERIAL, parse_tag_serialnr);

static int __init parse_tag_revision(const struct tag *tag)
{
	system_rev = tag->u.revision.rev;
	return 0;
}

__tagtable(ATAG_REVISION, parse_tag_revision);

static int __init parse_tag_cmdline(const struct tag *tag)
{
#if defined(CONFIG_CMDLINE_EXTEND)
	strlcat(default_command_line, " ", COMMAND_LINE_SIZE);
	strlcat(default_command_line, tag->u.cmdline.cmdline,
		COMMAND_LINE_SIZE);
#elif defined(CONFIG_CMDLINE_FORCE)
	pr_warn("Ignoring tag cmdline (using the default kernel command line)\n");
#else
	strlcpy(default_command_line, tag->u.cmdline.cmdline,
		COMMAND_LINE_SIZE);
#endif
	return 0;
}

__tagtable(ATAG_CMDLINE, parse_tag_cmdline);

/*
 * Scan the tag table for this tag, and call its parse function.
 * The tag table is built by the linker from all the __tagtable
 * declarations.
 */
static int __init parse_tag(const struct tag *tag)
{
	extern struct tagtable __tagtable_begin, __tagtable_end;
	struct tagtable *t;

	for (t = &__tagtable_begin; t < &__tagtable_end; t++)
		if (tag->hdr.tag == t->tag) {
			t->parse(tag);
			break;
		}

	return t < &__tagtable_end;
}

/*
 * Parse all tags in the list, checking both the global and architecture
 * specific tag tables.
 */
static void __init parse_tags(const struct tag *t)
{
	for (; t->hdr.size; t = tag_next(t))
		if (!parse_tag(t))
			pr_warn("Ignoring unrecognised tag 0x%08x\n",
				t->hdr.tag);
}

static void __init squash_mem_tags(struct tag *tag)
{
	for (; tag->hdr.size; tag = tag_next(tag))
		if (tag->hdr.tag == ATAG_MEM)
			tag->hdr.tag = ATAG_NONE;
}

const struct machine_desc * __init
setup_machine_tags(void *atags_vaddr, unsigned int machine_nr)
{
	struct tag *tags = (struct tag *)&default_tags;
	const struct machine_desc *mdesc = NULL, *p;
	char *from = default_command_line;

	default_tags.mem.start = PHYS_OFFSET;

	/*
	 * locate machine in the list of supported machines.
	 */
	for_each_machine_desc(p)
		if (machine_nr == p->nr) {
			pr_info("Machine: %s\n", p->name);
			mdesc = p;
			break;
		}

	if (!mdesc)
		return NULL;

	if (atags_vaddr)
		tags = atags_vaddr;
	else if (mdesc->atag_offset)
		tags = (void *)(PAGE_OFFSET + mdesc->atag_offset);

#if defined(CONFIG_DEPRECATED_PARAM_STRUCT)
	/*
	 * If we have the old style parameters, convert them to
	 * a tag list.
	 */
	if (tags->hdr.tag != ATAG_CORE)
		convert_to_tag_list(tags);
#endif
	if (tags->hdr.tag != ATAG_CORE) {
		early_print("Warning: Neither atags nor dtb found\n");
		tags = (struct tag *)&default_tags;
	}

	if (mdesc->fixup)
		mdesc->fixup(tags, &from);

	if (tags->hdr.tag == ATAG_CORE) {
		if (memblock_phys_mem_size())
			squash_mem_tags(tags);
		save_atags(tags);
		parse_tags(tags);
	}

	/* parse_early_param needs a boot_command_line */
	strlcpy(boot_command_line, from, COMMAND_LINE_SIZE);

	return mdesc;
}
