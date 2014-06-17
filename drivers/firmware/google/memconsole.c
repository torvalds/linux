/*
 * memconsole.c
 *
 * Infrastructure for importing the BIOS memory based console
 * into the kernel log ringbuffer.
 *
 * Copyright 2010 Google Inc. All rights reserved.
 */

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <asm/bios_ebda.h>

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

static u32 memconsole_baseaddr;
static size_t memconsole_length;

static ssize_t memconsole_read(struct file *filp, struct kobject *kobp,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t pos, size_t count)
{
	char *memconsole;
	ssize_t ret;

	memconsole = ioremap_cache(memconsole_baseaddr, memconsole_length);
	if (!memconsole) {
		pr_err("memconsole: ioremap_cache failed\n");
		return -ENOMEM;
	}
	ret = memory_read_from_buffer(buf, count, &pos, memconsole,
				      memconsole_length);
	iounmap(memconsole);
	return ret;
}

static struct bin_attribute memconsole_bin_attr = {
	.attr = {.name = "log", .mode = 0444},
	.read = memconsole_read,
};


static void __init found_v1_header(struct biosmemcon_ebda *hdr)
{
	pr_info("BIOS console v1 EBDA structure found at %p\n", hdr);
	pr_info("BIOS console buffer at 0x%.8x, "
	       "start = %d, end = %d, num = %d\n",
	       hdr->v1.buffer_addr, hdr->v1.start,
	       hdr->v1.end, hdr->v1.num_chars);

	memconsole_length = hdr->v1.num_chars;
	memconsole_baseaddr = hdr->v1.buffer_addr;
}

static void __init found_v2_header(struct biosmemcon_ebda *hdr)
{
	pr_info("BIOS console v2 EBDA structure found at %p\n", hdr);
	pr_info("BIOS console buffer at 0x%.8x, "
	       "start = %d, end = %d, num_bytes = %d\n",
	       hdr->v2.buffer_addr, hdr->v2.start,
	       hdr->v2.end, hdr->v2.num_bytes);

	memconsole_length = hdr->v2.end - hdr->v2.start;
	memconsole_baseaddr = hdr->v2.buffer_addr + hdr->v2.start;
}

/*
 * Search through the EBDA for the BIOS Memory Console, and
 * set the global variables to point to it.  Return true if found.
 */
static bool __init found_memconsole(void)
{
	unsigned int address;
	size_t length, cur;

	address = get_bios_ebda();
	if (!address) {
		pr_info("BIOS EBDA non-existent.\n");
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

	pr_info("BIOS console EBDA structure not found!\n");
	return false;
}

static struct dmi_system_id memconsole_dmi_table[] __initdata = {
	{
		.ident = "Google Board",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Google, Inc."),
		},
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, memconsole_dmi_table);

static int __init memconsole_init(void)
{
	if (!dmi_check_system(memconsole_dmi_table))
		return -ENODEV;

	if (!found_memconsole())
		return -ENODEV;

	memconsole_bin_attr.size = memconsole_length;
	return sysfs_create_bin_file(firmware_kobj, &memconsole_bin_attr);
}

static void __exit memconsole_exit(void)
{
	sysfs_remove_bin_file(firmware_kobj, &memconsole_bin_attr);
}

module_init(memconsole_init);
module_exit(memconsole_exit);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
