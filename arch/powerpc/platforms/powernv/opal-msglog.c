/*
 * PowerNV OPAL in-memory console interface
 *
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/io.h>
#include <asm/opal.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/types.h>
#include <asm/barrier.h>

/* OPAL in-memory console. Defined in OPAL source at core/console.c */
struct memcons {
	__be64 magic;
#define MEMCONS_MAGIC	0x6630696567726173L
	__be64 obuf_phys;
	__be64 ibuf_phys;
	__be32 obuf_size;
	__be32 ibuf_size;
	__be32 out_pos;
#define MEMCONS_OUT_POS_WRAP	0x80000000u
#define MEMCONS_OUT_POS_MASK	0x00ffffffu
	__be32 in_prod;
	__be32 in_cons;
};

static struct memcons *opal_memcons = NULL;

ssize_t opal_msglog_copy(char *to, loff_t pos, size_t count)
{
	const char *conbuf;
	ssize_t ret;
	size_t first_read = 0;
	uint32_t out_pos, avail;

	if (!opal_memcons)
		return -ENODEV;

	out_pos = be32_to_cpu(READ_ONCE(opal_memcons->out_pos));

	/* Now we've read out_pos, put a barrier in before reading the new
	 * data it points to in conbuf. */
	smp_rmb();

	conbuf = phys_to_virt(be64_to_cpu(opal_memcons->obuf_phys));

	/* When the buffer has wrapped, read from the out_pos marker to the end
	 * of the buffer, and then read the remaining data as in the un-wrapped
	 * case. */
	if (out_pos & MEMCONS_OUT_POS_WRAP) {

		out_pos &= MEMCONS_OUT_POS_MASK;
		avail = be32_to_cpu(opal_memcons->obuf_size) - out_pos;

		ret = memory_read_from_buffer(to, count, &pos,
				conbuf + out_pos, avail);

		if (ret < 0)
			goto out;

		first_read = ret;
		to += first_read;
		count -= first_read;
		pos -= avail;

		if (count <= 0)
			goto out;
	}

	/* Sanity check. The firmware should not do this to us. */
	if (out_pos > be32_to_cpu(opal_memcons->obuf_size)) {
		pr_err("OPAL: memory console corruption. Aborting read.\n");
		return -EINVAL;
	}

	ret = memory_read_from_buffer(to, count, &pos, conbuf, out_pos);

	if (ret < 0)
		goto out;

	ret += first_read;
out:
	return ret;
}

static ssize_t opal_msglog_read(struct file *file, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *to,
				loff_t pos, size_t count)
{
	return opal_msglog_copy(to, pos, count);
}

static struct bin_attribute opal_msglog_attr = {
	.attr = {.name = "msglog", .mode = 0400},
	.read = opal_msglog_read
};

void __init opal_msglog_init(void)
{
	u64 mcaddr;
	struct memcons *mc;

	if (of_property_read_u64(opal_node, "ibm,opal-memcons", &mcaddr)) {
		pr_warn("OPAL: Property ibm,opal-memcons not found, no message log\n");
		return;
	}

	mc = phys_to_virt(mcaddr);
	if (!mc) {
		pr_warn("OPAL: memory console address is invalid\n");
		return;
	}

	if (be64_to_cpu(mc->magic) != MEMCONS_MAGIC) {
		pr_warn("OPAL: memory console version is invalid\n");
		return;
	}

	/* Report maximum size */
	opal_msglog_attr.size =  be32_to_cpu(mc->ibuf_size) +
		be32_to_cpu(mc->obuf_size);

	opal_memcons = mc;
}

void __init opal_msglog_sysfs_init(void)
{
	if (!opal_memcons) {
		pr_warn("OPAL: message log initialisation failed, not creating sysfs entry\n");
		return;
	}

	if (sysfs_create_bin_file(opal_kobj, &opal_msglog_attr) != 0)
		pr_warn("OPAL: sysfs file creation failed\n");
}
