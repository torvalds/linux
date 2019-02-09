/*
 * memconsole-coreboot.c
 *
 * Memory based BIOS console accessed through coreboot table.
 *
 * Copyright 2017 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "memconsole.h"
#include "coreboot_table.h"

#define CB_TAG_CBMEM_CONSOLE	0x17

/* CBMEM firmware console log descriptor. */
struct cbmem_cons {
	u32 size_dont_access_after_boot;
	u32 cursor;
	u8  body[0];
} __packed;

#define CURSOR_MASK ((1 << 28) - 1)
#define OVERFLOW (1 << 31)

static struct cbmem_cons __iomem *cbmem_console;
static u32 cbmem_console_size;

/*
 * The cbmem_console structure is read again on every access because it may
 * change at any time if runtime firmware logs new messages. This may rarely
 * lead to race conditions where the firmware overwrites the beginning of the
 * ring buffer with more lines after we have already read |cursor|. It should be
 * rare and harmless enough that we don't spend extra effort working around it.
 */
static ssize_t memconsole_coreboot_read(char *buf, loff_t pos, size_t count)
{
	u32 cursor = cbmem_console->cursor & CURSOR_MASK;
	u32 flags = cbmem_console->cursor & ~CURSOR_MASK;
	u32 size = cbmem_console_size;
	struct seg {	/* describes ring buffer segments in logical order */
		u32 phys;	/* physical offset from start of mem buffer */
		u32 len;	/* length of segment */
	} seg[2] = { {0}, {0} };
	size_t done = 0;
	int i;

	if (flags & OVERFLOW) {
		if (cursor > size)	/* Shouldn't really happen, but... */
			cursor = 0;
		seg[0] = (struct seg){.phys = cursor, .len = size - cursor};
		seg[1] = (struct seg){.phys = 0, .len = cursor};
	} else {
		seg[0] = (struct seg){.phys = 0, .len = min(cursor, size)};
	}

	for (i = 0; i < ARRAY_SIZE(seg) && count > done; i++) {
		done += memory_read_from_buffer(buf + done, count - done, &pos,
			cbmem_console->body + seg[i].phys, seg[i].len);
		pos -= seg[i].len;
	}
	return done;
}

static int memconsole_probe(struct coreboot_device *dev)
{
	struct cbmem_cons __iomem *tmp_cbmc;

	tmp_cbmc = memremap(dev->cbmem_ref.cbmem_addr,
			    sizeof(*tmp_cbmc), MEMREMAP_WB);

	if (!tmp_cbmc)
		return -ENOMEM;

	/* Read size only once to prevent overrun attack through /dev/mem. */
	cbmem_console_size = tmp_cbmc->size_dont_access_after_boot;
	cbmem_console = memremap(dev->cbmem_ref.cbmem_addr,
				 cbmem_console_size + sizeof(*cbmem_console),
				 MEMREMAP_WB);
	memunmap(tmp_cbmc);

	if (!cbmem_console)
		return -ENOMEM;

	memconsole_setup(memconsole_coreboot_read);

	return memconsole_sysfs_init();
}

static int memconsole_remove(struct coreboot_device *dev)
{
	memconsole_exit();

	if (cbmem_console)
		memunmap(cbmem_console);

	return 0;
}

static struct coreboot_driver memconsole_driver = {
	.probe = memconsole_probe,
	.remove = memconsole_remove,
	.drv = {
		.name = "memconsole",
	},
	.tag = CB_TAG_CBMEM_CONSOLE,
};

static void coreboot_memconsole_exit(void)
{
	coreboot_driver_unregister(&memconsole_driver);
}

static int __init coreboot_memconsole_init(void)
{
	return coreboot_driver_register(&memconsole_driver);
}

module_exit(coreboot_memconsole_exit);
module_init(coreboot_memconsole_init);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
