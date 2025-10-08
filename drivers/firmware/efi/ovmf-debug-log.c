// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#define OVMF_DEBUG_LOG_MAGIC1  0x3167646d666d766f  // "ovmfmdg1"
#define OVMF_DEBUG_LOG_MAGIC2  0x3267646d666d766f  // "ovmfmdg2"

struct ovmf_debug_log_header {
	u64    magic1;
	u64    magic2;
	u64    hdr_size;
	u64    log_size;
	u64    lock; // edk2 spinlock
	u64    head_off;
	u64    tail_off;
	u64    truncated;
	u8     fw_version[128];
};

static struct ovmf_debug_log_header *hdr;
static u8 *logbuf;
static u64 logbufsize;

static ssize_t ovmf_log_read(struct file *filp, struct kobject *kobj,
			     const struct bin_attribute *attr, char *buf,
			     loff_t offset, size_t count)
{
	u64 start, end;

	start = hdr->head_off + offset;
	if (hdr->head_off > hdr->tail_off && start >= hdr->log_size)
		start -= hdr->log_size;

	end = start + count;
	if (start > hdr->tail_off) {
		if (end > hdr->log_size)
			end = hdr->log_size;
	} else {
		if (end > hdr->tail_off)
			end = hdr->tail_off;
	}

	if (start > logbufsize || end > logbufsize)
		return 0;
	if (start >= end)
		return 0;

	memcpy(buf, logbuf + start, end - start);
	return end - start;
}

static struct bin_attribute ovmf_log_bin_attr = {
	.attr = {
		.name = "ovmf_debug_log",
		.mode = 0444,
	},
	.read = ovmf_log_read,
};

int __init ovmf_log_probe(unsigned long ovmf_debug_log_table)
{
	int ret = -EINVAL;
	u64 size;

	/* map + verify header */
	hdr = memremap(ovmf_debug_log_table, sizeof(*hdr), MEMREMAP_WB);
	if (!hdr) {
		pr_err("OVMF debug log: header map failed\n");
		return -EINVAL;
	}

	if (hdr->magic1 != OVMF_DEBUG_LOG_MAGIC1 ||
	    hdr->magic2 != OVMF_DEBUG_LOG_MAGIC2) {
		printk(KERN_ERR "OVMF debug log: magic mismatch\n");
		goto err_unmap;
	}

	size = hdr->hdr_size + hdr->log_size;
	pr_info("OVMF debug log: firmware version: \"%s\"\n", hdr->fw_version);
	pr_info("OVMF debug log: buffer size: %lluk\n", size / 1024);

	/* map complete log buffer */
	memunmap(hdr);
	hdr = memremap(ovmf_debug_log_table, size, MEMREMAP_WB);
	if (!hdr) {
		pr_err("OVMF debug log: buffer map failed\n");
		return -EINVAL;
	}
	logbuf = (void *)hdr + hdr->hdr_size;
	logbufsize = hdr->log_size;

	ovmf_log_bin_attr.size = size;
	ret = sysfs_create_bin_file(efi_kobj, &ovmf_log_bin_attr);
	if (ret != 0) {
		pr_err("OVMF debug log: sysfs register failed\n");
		goto err_unmap;
	}

	return 0;

err_unmap:
	memunmap(hdr);
	return ret;
}
