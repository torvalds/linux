// SPDX-License-Identifier: GPL-2.0-only
/*
 * memconsole-x86-legacy.c
 *
 * EBDA specific parts of the memory based BIOS console.
 *
 * Copyright 2017 Google Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/mm.h>
#include <asm/bios_ebda.h>
#include <linux/acpi.h>

#include "memconsole.h"

#define BIOS_MEMCONSOLE_V1_MAGIC	0xDEADBABE
#define BIOS_MEMCONSOLE_V2_MAGIC	(('M')|('C'<<8)|('O'<<16)|('N'<<24))

struct biosmemcon_ebda {
	u32 signature;
	union {
		struct {
			u8  enabled;
			u32 buffer_addr;
			u16 start;
			u16 end;
			u16 num_chars;
			u8  wrapped;
		} __packed v1;
		struct {
			u32 buffer_addr;
			/* Misdocumented as number of pages! */
			u16 num_bytes;
			u16 start;
			u16 end;
		} __packed v2;
	};
} __packed;

static char *memconsole_baseaddr;
static size_t memconsole_length;

static ssize_t memconsole_read(char *buf, loff_t pos, size_t count)
{
	return memory_read_from_buffer(buf, count, &pos, memconsole_baseaddr,
				       memconsole_length);
}

static void found_v1_header(struct biosmemcon_ebda *hdr)
{
	pr_info("memconsole: BIOS console v1 EBDA structure found at %p\n",
		hdr);
	pr_info("memconsole: BIOS console buffer at 0x%.8x, start = %d, end = %d, num = %d\n",
		hdr->v1.buffer_addr, hdr->v1.start,
		hdr->v1.end, hdr->v1.num_chars);

	memconsole_baseaddr = phys_to_virt(hdr->v1.buffer_addr);
	memconsole_length = hdr->v1.num_chars;
	memconsole_setup(memconsole_read);
}

static void found_v2_header(struct biosmemcon_ebda *hdr)
{
	pr_info("memconsole: BIOS console v2 EBDA structure found at %p\n",
		hdr);
	pr_info("memconsole: BIOS console buffer at 0x%.8x, start = %d, end = %d, num_bytes = %d\n",
		hdr->v2.buffer_addr, hdr->v2.start,
		hdr->v2.end, hdr->v2.num_bytes);

	memconsole_baseaddr = phys_to_virt(hdr->v2.buffer_addr + hdr->v2.start);
	memconsole_length = hdr->v2.end - hdr->v2.start;
	memconsole_setup(memconsole_read);
}

/*
 * Search through the EBDA for the BIOS Memory Console, and
 * set the global variables to point to it.  Return true if found.
 */
static bool memconsole_ebda_init(void)
{
	unsigned int address;
	size_t length, cur;

	address = get_bios_ebda();
	if (!address) {
		pr_info("memconsole: BIOS EBDA non-existent.\n");
		return false;
	}

	/* EBDA length is byte 0 of EBDA (in KB) */
	length = *(u8 *)phys_to_virt(address);
	length <<= 10; /* convert to bytes */

	/*
	 * Search through EBDA for BIOS memory console structure
	 * note: signature is not necessarily dword-aligned
	 */
	for (cur = 0; cur < length; cur++) {
		struct biosmemcon_ebda *hdr = phys_to_virt(address + cur);

		/* memconsole v1 */
		if (hdr->signature == BIOS_MEMCONSOLE_V1_MAGIC) {
			found_v1_header(hdr);
			return true;
		}

		/* memconsole v2 */
		if (hdr->signature == BIOS_MEMCONSOLE_V2_MAGIC) {
			found_v2_header(hdr);
			return true;
		}
	}

	pr_info("memconsole: BIOS console EBDA structure not found!\n");
	return false;
}

static const struct dmi_system_id memconsole_dmi_table[] __initconst = {
	{
		.ident = "Google Board",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Google, Inc."),
		},
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, memconsole_dmi_table);

static bool __init memconsole_find(void)
{
	if (!dmi_check_system(memconsole_dmi_table))
		return false;

	return memconsole_ebda_init();
}

static int __init memconsole_x86_init(void)
{
	if (!memconsole_find())
		return -ENODEV;

	return memconsole_sysfs_init();
}

static void __exit memconsole_x86_exit(void)
{
	memconsole_exit();
}

module_init(memconsole_x86_init);
module_exit(memconsole_x86_exit);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
