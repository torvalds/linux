// SPDX-License-Identifier: GPL-2.0
/*
 * Export Runtime Configuration Interface Table Version 2 (RCI2)
 * to sysfs
 *
 * Copyright (C) 2019 Dell Inc
 * by Narendra K <Narendra.K@dell.com>
 *
 * System firmware advertises the address of the RCI2 Table via
 * an EFI Configuration Table entry. This code retrieves the RCI2
 * table from the address and exports it to sysfs as a binary
 * attribute 'rci2' under /sys/firmware/efi/tables directory.
 */

#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/efi.h>
#include <linux/types.h>
#include <linux/io.h>

#define RCI_SIGNATURE	"_RC_"

struct rci2_table_global_hdr {
	u16 type;
	u16 resvd0;
	u16 hdr_len;
	u8 rci2_sig[4];
	u16 resvd1;
	u32 resvd2;
	u32 resvd3;
	u8 major_rev;
	u8 minor_rev;
	u16 num_of_structs;
	u32 rci2_len;
	u16 rci2_chksum;
} __packed;

static u8 *rci2_base;
static u32 rci2_table_len;
unsigned long rci2_table_phys __ro_after_init = EFI_INVALID_TABLE_ADDR;

static ssize_t raw_table_read(struct file *file, struct kobject *kobj,
			      struct bin_attribute *attr, char *buf,
			      loff_t pos, size_t count)
{
	memcpy(buf, attr->private + pos, count);
	return count;
}

static BIN_ATTR(rci2, S_IRUSR, raw_table_read, NULL, 0);

static u16 checksum(void)
{
	u8 len_is_odd = rci2_table_len % 2;
	u32 chksum_len = rci2_table_len;
	u16 *base = (u16 *)rci2_base;
	u8 buf[2] = {0};
	u32 offset = 0;
	u16 chksum = 0;

	if (len_is_odd)
		chksum_len -= 1;

	while (offset < chksum_len) {
		chksum += *base;
		offset += 2;
		base++;
	}

	if (len_is_odd) {
		buf[0] = *(u8 *)base;
		chksum += *(u16 *)(buf);
	}

	return chksum;
}

static int __init efi_rci2_sysfs_init(void)
{
	struct kobject *tables_kobj;
	int ret = -ENOMEM;

	rci2_base = memremap(rci2_table_phys,
			     sizeof(struct rci2_table_global_hdr),
			     MEMREMAP_WB);
	if (!rci2_base) {
		pr_debug("RCI2 table init failed - could not map RCI2 table\n");
		goto err;
	}

	if (strncmp(rci2_base +
		    offsetof(struct rci2_table_global_hdr, rci2_sig),
		    RCI_SIGNATURE, 4)) {
		pr_debug("RCI2 table init failed - incorrect signature\n");
		ret = -ENODEV;
		goto err_unmap;
	}

	rci2_table_len = *(u32 *)(rci2_base +
				  offsetof(struct rci2_table_global_hdr,
				  rci2_len));

	memunmap(rci2_base);

	if (!rci2_table_len) {
		pr_debug("RCI2 table init failed - incorrect table length\n");
		goto err;
	}

	rci2_base = memremap(rci2_table_phys, rci2_table_len, MEMREMAP_WB);
	if (!rci2_base) {
		pr_debug("RCI2 table - could not map RCI2 table\n");
		goto err;
	}

	if (checksum() != 0) {
		pr_debug("RCI2 table - incorrect checksum\n");
		ret = -ENODEV;
		goto err_unmap;
	}

	tables_kobj = kobject_create_and_add("tables", efi_kobj);
	if (!tables_kobj) {
		pr_debug("RCI2 table - tables_kobj creation failed\n");
		goto err_unmap;
	}

	bin_attr_rci2.size = rci2_table_len;
	bin_attr_rci2.private = rci2_base;
	ret = sysfs_create_bin_file(tables_kobj, &bin_attr_rci2);
	if (ret != 0) {
		pr_debug("RCI2 table - rci2 sysfs bin file creation failed\n");
		kobject_del(tables_kobj);
		kobject_put(tables_kobj);
		goto err_unmap;
	}

	return 0;

 err_unmap:
	memunmap(rci2_base);
 err:
	pr_debug("RCI2 table - sysfs initialization failed\n");
	return ret;
}
late_initcall(efi_rci2_sysfs_init);
