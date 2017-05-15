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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "memconsole.h"
#include "coreboot_table.h"

#define CB_TAG_CBMEM_CONSOLE	0x17

/* CBMEM firmware console log descriptor. */
struct cbmem_cons {
	u32 buffer_size;
	u32 buffer_cursor;
	u8  buffer_body[0];
} __packed;

static struct cbmem_cons __iomem *cbmem_console;

static int memconsole_coreboot_init(phys_addr_t physaddr)
{
	struct cbmem_cons __iomem *tmp_cbmc;

	tmp_cbmc = memremap(physaddr, sizeof(*tmp_cbmc), MEMREMAP_WB);

	if (!tmp_cbmc)
		return -ENOMEM;

	cbmem_console = memremap(physaddr,
				 tmp_cbmc->buffer_size + sizeof(*cbmem_console),
				 MEMREMAP_WB);
	memunmap(tmp_cbmc);

	if (!cbmem_console)
		return -ENOMEM;

	memconsole_setup(cbmem_console->buffer_body,
		min(cbmem_console->buffer_cursor, cbmem_console->buffer_size));

	return 0;
}

static int memconsole_probe(struct platform_device *pdev)
{
	int ret;
	struct lb_cbmem_ref entry;

	ret = coreboot_table_find(CB_TAG_CBMEM_CONSOLE, &entry, sizeof(entry));
	if (ret)
		return ret;

	ret = memconsole_coreboot_init(entry.cbmem_addr);
	if (ret)
		return ret;

	return memconsole_sysfs_init();
}

static int memconsole_remove(struct platform_device *pdev)
{
	memconsole_exit();

	if (cbmem_console)
		memunmap(cbmem_console);

	return 0;
}

static struct platform_driver memconsole_driver = {
	.probe = memconsole_probe,
	.remove = memconsole_remove,
	.driver = {
		.name = "memconsole",
	},
};

static int __init platform_memconsole_init(void)
{
	struct platform_device *pdev;

	pdev = platform_device_register_simple("memconsole", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	platform_driver_register(&memconsole_driver);

	return 0;
}

module_init(platform_memconsole_init);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
