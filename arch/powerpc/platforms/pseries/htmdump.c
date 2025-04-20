// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) IBM Corporation, 2024
 */

#define pr_fmt(fmt) "htmdump: " fmt

#include <linux/debugfs.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/plpar_wrappers.h>

static void *htm_buf;
static u32 nodeindex;
static u32 nodalchipindex;
static u32 coreindexonchip;
static u32 htmtype;
static u32 htmconfigure;
static u32 htmstart;
static struct dentry *htmdump_debugfs_dir;
#define	HTM_ENABLE	1
#define	HTM_DISABLE	0

/*
 * Check the return code for H_HTM hcall.
 * Return non-zero value (1) if either H_PARTIAL or H_SUCCESS
 * is returned. For other return codes:
 * Return zero if H_NOT_AVAILABLE.
 * Return -EBUSY if hcall return busy.
 * Return -EINVAL if any parameter or operation is not valid.
 * Return -EPERM if HTM Virtualization Engine Technology code
 * is not applied.
 * Return -EIO if the HTM state is not valid.
 */
static ssize_t htm_return_check(long rc)
{
	switch (rc) {
	case H_SUCCESS:
	/* H_PARTIAL for the case where all available data can't be
	 * returned due to buffer size constraint.
	 */
	case H_PARTIAL:
		break;
	/* H_NOT_AVAILABLE indicates reading from an offset outside the range,
	 * i.e. past end of file.
	 */
	case H_NOT_AVAILABLE:
		return 0;
	case H_BUSY:
	case H_LONG_BUSY_ORDER_1_MSEC:
	case H_LONG_BUSY_ORDER_10_MSEC:
	case H_LONG_BUSY_ORDER_100_MSEC:
	case H_LONG_BUSY_ORDER_1_SEC:
	case H_LONG_BUSY_ORDER_10_SEC:
	case H_LONG_BUSY_ORDER_100_SEC:
		return -EBUSY;
	case H_PARAMETER:
	case H_P2:
	case H_P3:
	case H_P4:
	case H_P5:
	case H_P6:
		return -EINVAL;
	case H_STATE:
		return -EIO;
	case H_AUTHORITY:
		return -EPERM;
	}

	/*
	 * Return 1 for H_SUCCESS/H_PARTIAL
	 */
	return 1;
}

static ssize_t htmdump_read(struct file *filp, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	void *htm_buf = filp->private_data;
	unsigned long page, read_size, available;
	loff_t offset;
	long rc, ret;

	page = ALIGN_DOWN(*ppos, PAGE_SIZE);
	offset = (*ppos) % PAGE_SIZE;

	/*
	 * Invoke H_HTM call with:
	 * - operation as htm dump (H_HTM_OP_DUMP_DATA)
	 * - last three values are address, size and offset
	 */
	rc = htm_hcall_wrapper(nodeindex, nodalchipindex, coreindexonchip,
				   htmtype, H_HTM_OP_DUMP_DATA, virt_to_phys(htm_buf),
				   PAGE_SIZE, page);

	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall failed for op: H_HTM_OP_DUMP_DATA, returning %ld\n", ret);
		return ret;
	}

	available = PAGE_SIZE;
	read_size = min(count, available);
	*ppos += read_size;
	return simple_read_from_buffer(ubuf, count, &offset, htm_buf, available);
}

static const struct file_operations htmdump_fops = {
	.llseek = NULL,
	.read	= htmdump_read,
	.open	= simple_open,
};

static int  htmconfigure_set(void *data, u64 val)
{
	long rc, ret;

	/*
	 * value as 1 : configure HTM.
	 * value as 0 : deconfigure HTM. Return -EINVAL for
	 * other values.
	 */
	if (val == HTM_ENABLE) {
		/*
		 * Invoke H_HTM call with:
		 * - operation as htm configure (H_HTM_OP_CONFIGURE)
		 * - last three values are unused, hence set to zero
		 */
		rc = htm_hcall_wrapper(nodeindex, nodalchipindex, coreindexonchip,
			   htmtype, H_HTM_OP_CONFIGURE, 0, 0, 0);
	} else if (val == HTM_DISABLE) {
		/*
		 * Invoke H_HTM call with:
		 * - operation as htm deconfigure (H_HTM_OP_DECONFIGURE)
		 * - last three values are unused, hence set to zero
		 */
		rc = htm_hcall_wrapper(nodeindex, nodalchipindex, coreindexonchip,
				htmtype, H_HTM_OP_DECONFIGURE, 0, 0, 0);
	} else
		return -EINVAL;

	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall failed, returning %ld\n", ret);
		return ret;
	}

	/* Set htmconfigure if operation succeeds */
	htmconfigure = val;

	return 0;
}

static int htmconfigure_get(void *data, u64 *val)
{
	*val = htmconfigure;
	return 0;
}

static int  htmstart_set(void *data, u64 val)
{
	long rc, ret;

	/*
	 * value as 1: start HTM
	 * value as 0: stop HTM
	 * Return -EINVAL for other values.
	 */
	if (val == HTM_ENABLE) {
		/*
		 * Invoke H_HTM call with:
		 * - operation as htm start (H_HTM_OP_START)
		 * - last three values are unused, hence set to zero
		 */
		rc = htm_hcall_wrapper(nodeindex, nodalchipindex, coreindexonchip,
			   htmtype, H_HTM_OP_START, 0, 0, 0);

	} else if (val == HTM_DISABLE) {
		/*
		 * Invoke H_HTM call with:
		 * - operation as htm stop (H_HTM_OP_STOP)
		 * - last three values are unused, hence set to zero
		 */
		rc = htm_hcall_wrapper(nodeindex, nodalchipindex, coreindexonchip,
				htmtype, H_HTM_OP_STOP, 0, 0, 0);
	} else
		return -EINVAL;

	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall failed, returning %ld\n", ret);
		return ret;
	}

	/* Set htmstart if H_HTM_OP_START/H_HTM_OP_STOP operation succeeds */
	htmstart = val;

	return 0;
}

static int htmstart_get(void *data, u64 *val)
{
	*val = htmstart;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(htmconfigure_fops, htmconfigure_get, htmconfigure_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(htmstart_fops, htmstart_get, htmstart_set, "%llu\n");

static int htmdump_init_debugfs(void)
{
	htm_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!htm_buf) {
		pr_err("Failed to allocate htmdump buf\n");
		return -ENOMEM;
	}

	htmdump_debugfs_dir = debugfs_create_dir("htmdump",
						  arch_debugfs_dir);

	debugfs_create_u32("nodeindex", 0600,
			htmdump_debugfs_dir, &nodeindex);
	debugfs_create_u32("nodalchipindex", 0600,
			htmdump_debugfs_dir, &nodalchipindex);
	debugfs_create_u32("coreindexonchip", 0600,
			htmdump_debugfs_dir, &coreindexonchip);
	debugfs_create_u32("htmtype", 0600,
			htmdump_debugfs_dir, &htmtype);
	debugfs_create_file("trace", 0400, htmdump_debugfs_dir, htm_buf, &htmdump_fops);

	/*
	 * Debugfs interface files to control HTM operations:
	 */
	debugfs_create_file("htmconfigure", 0600, htmdump_debugfs_dir, NULL, &htmconfigure_fops);
	debugfs_create_file("htmstart", 0600, htmdump_debugfs_dir, NULL, &htmstart_fops);

	return 0;
}

static int __init htmdump_init(void)
{
	/* Disable on kvm guest */
	if (is_kvm_guest()) {
		pr_info("htmdump not supported inside KVM guest\n");
		return -EOPNOTSUPP;
	}

	if (htmdump_init_debugfs())
		return -ENOMEM;

	return 0;
}

static void __exit htmdump_exit(void)
{
	debugfs_remove_recursive(htmdump_debugfs_dir);
	kfree(htm_buf);
}

module_init(htmdump_init);
module_exit(htmdump_exit);
MODULE_DESCRIPTION("PHYP Hardware Trace Macro (HTM) data dumper");
MODULE_LICENSE("GPL");
