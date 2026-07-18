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
#include <asm/kvm_guest.h>

static void *htm_buf;
static void *htm_status_buf;
static void *htm_info_buf;
static void *htm_caps_buf;
static void *htm_mem_buf;
static u32 nodeindex;
static u32 nodalchipindex;
static u32 coreindexonchip;
static u32 htmtype;
static u32 htmconfigure;
static u32 htmstart;
static u32 htmsetup;
static u64 htmflags;

static struct dentry *htmdump_debugfs_dir;
#define	HTM_ENABLE	1
#define	HTM_DISABLE	0
#define	HTM_NOWRAP	1
#define	HTM_WRAP	0

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
	void *htm_buf_data = filp->private_data;
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
	rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
				   htmtype, H_HTM_OP_DUMP_DATA, virt_to_phys(htm_buf_data),
				   PAGE_SIZE, page);

	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall failed for op: H_HTM_OP_DUMP_DATA, returning %ld\n", ret);
		return ret;
	}

	available = PAGE_SIZE;
	read_size = min(count, available);
	*ppos += read_size;
	return simple_read_from_buffer(ubuf, count, &offset, htm_buf_data, available);
}

static ssize_t htmsystem_mem_read(struct file *filp, char __user *ubuf,
		size_t count, loff_t *ppos)
{
	void *htm_mem_data = filp->private_data;
	long rc, ret;
	u64 *num_entries;
	u64 to_copy = 0;
	loff_t offset = 0;
	u64 mem_offset = 0;

	/*
	 * Invoke H_HTM call with:
	 * - operation as htm status (H_HTM_OP_STATUS)
	 * - last three values as addr, size and offset. "offset"
	 *   is value from output buffer header that points to next
	 *   entry to dump. 0 is the first entry to dump. next entry
	 *   is read from the output bufferbyte offset 0x8.
	 *
	 *   When first time hcall is invoked, mem_offset should be
	 *   zero because zero is the first entry.
	 *   In the next hcall, offset of next entry to read from is
	 *   picked from output buffer header itself. So don't fill
	 *   mem_offset for first read.
	 *
	 *  If there is no further data to read in next iteration,
	 *  offset value from output buffer header will point to -1.
	 */
	if (*ppos) {
		mem_offset = *(u64 *)(htm_mem_data  + 0x8);
		if (mem_offset == -1)
			return 0;
	}
	rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
			htmtype, H_HTM_OP_DUMP_SYSMEM_CONF, virt_to_phys(htm_mem_data),
			PAGE_SIZE, be64_to_cpu(mem_offset));
	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall returned for op: H_HTM_OP_DUMP_SYSMEM_CONF with hcall returning  %ld\n", ret);
		return ret;
	}

	/*
	 * HTM system mem buffer, start of buffer + 0x10 gives the
	 * number of HTM entries in the buffer.
	 * So total count to copy is:
	 * 32 bytes (for first 5 fields) + (number of HTM entries * entry size)
	 */
	num_entries = htm_mem_data + 0x10;
	to_copy = 32 + (be64_to_cpu(*num_entries) * 32);

	*ppos += to_copy;
	return simple_read_from_buffer(ubuf, count, &offset, htm_mem_data, to_copy);
}

static const struct file_operations htmdump_fops = {
	.llseek = NULL,
	.read	= htmdump_read,
	.open	= simple_open,
};

static const struct file_operations htmsystem_mem_fops = {
	.llseek = NULL,
	.read   = htmsystem_mem_read,
	.open   = simple_open,
};

static int  htmconfigure_set(void *data, u64 val)
{
	long rc, ret;
	unsigned long param1 = -1, param2 = -1;

	/*
	 * value as 1 : configure HTM.
	 * value as 0 : deconfigure HTM. Return -EINVAL for
	 * other values.
	 */
	if (val == HTM_ENABLE) {
		/*
		 * Invoke H_HTM call with:
		 * - operation as htm configure (H_HTM_OP_CONFIGURE)
		 * - If htmflags is set, param1 and param2 will be -1
		 *   which is an indicator to use default htm mode reg mask
		 *   and htm mode reg value.
		 * - last three values are unused, hence set to zero
		 */
		if (!htmflags) {
			param1 = 0;
			param2 = 0;
		}

		rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
			   htmtype, H_HTM_OP_CONFIGURE, param1, param2, 0);
	} else if (val == HTM_DISABLE) {
		/*
		 * Invoke H_HTM call with:
		 * - operation as htm deconfigure (H_HTM_OP_DECONFIGURE)
		 * - last three values are unused, hence set to zero
		 */
		rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
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
		rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
			   htmtype, H_HTM_OP_START, 0, 0, 0);

	} else if (val == HTM_DISABLE) {
		/*
		 * Invoke H_HTM call with:
		 * - operation as htm stop (H_HTM_OP_STOP)
		 * - last three values are unused, hence set to zero
		 */
		rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
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

static ssize_t htmstatus_read(struct file *filp, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	void *htm_status_data = filp->private_data;
	long rc, ret;
	u64 *num_entries;
	u64 to_copy;
	int htmstatus_flag;
	loff_t offset = 0;
	u64 status_offset = 0;

	/*
	 * Invoke H_HTM call with:
	 * - operation as htm status (H_HTM_OP_STATUS)
	 * - last three values as addr, size and offset.
	 *   "offset" is value from output buffer header
	 *   that points to next entry to dump. 0 is the first
	 *   entry to dump. next entry is read from the output
	 *   bufferbyte offset 0x8.
	 */
	if (*ppos) {
		status_offset = *(u64 *)(htm_status_data + 0x8);
		if (status_offset == -1)
			return 0;
	}
	rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
				   htmtype, H_HTM_OP_STATUS, virt_to_phys(htm_status_data),
				   PAGE_SIZE, be64_to_cpu(status_offset));

	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall failed for op: H_HTM_OP_STATUS, returning %ld\n", ret);
		return ret;
	}

	/*
	 * HTM status buffer, start of buffer + 0x10 gives the
	 * number of HTM entries in the buffer. Each nest htm status
	 * entry is 0x6 bytes where each core htm status entry is
	 * 0x8 bytes.
	 * So total count to copy is:
	 * 32 bytes (for first 7 fields) + (number of HTM entries * entry size)
	 */
	num_entries = htm_status_data + 0x10;
	if (htmtype == 0x2)
		htmstatus_flag = 0x8;
	else
		htmstatus_flag = 0x6;
	to_copy = 32 + (be64_to_cpu(*num_entries) * htmstatus_flag);
	*ppos += to_copy;

	return simple_read_from_buffer(ubuf, count, &offset, htm_status_data, to_copy);
}

static const struct file_operations htmstatus_fops = {
	.llseek = NULL,
	.read	= htmstatus_read,
	.open	= simple_open,
};

static ssize_t htminfo_read(struct file *filp, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	void *htm_info_data = filp->private_data;
	long rc, ret;
	u64 *num_entries;
	u64 to_copy;
	loff_t offset = 0;
	u64 info_offset = 0;

	/*
	 * Invoke H_HTM call with:
	 * - operation as htm status (H_HTM_OP_STATUS)
	 * - last three values as addr, size and offset
	 *   "offset" is value from output buffer header
	 *   that points to next entry to dump. 0 is the first
	 *   entry to dump. next entry is read from the output
	 *   bufferbyte offset 0x8.
	 */
	if (*ppos) {
		info_offset = *(u64 *)(htm_info_data + 0x8);
		if (info_offset == -1)
			return 0;
	}
	rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
				   htmtype, H_HTM_OP_DUMP_SYSPROC_CONF, virt_to_phys(htm_info_data),
				   PAGE_SIZE, be64_to_cpu(info_offset));

	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall failed for op: H_HTM_OP_DUMP_SYSPROC_CONF, returning %ld\n", ret);
		return ret;
	}

	/*
	 * HTM status buffer, start of buffer + 0x10 gives the
	 * number of HTM entries in the buffer. Each entry of processor
	 * is 16 bytes.
	 *
	 * So total count to copy is:
	 * 32 bytes (for first 5 fields) + (number of HTM entries * entry size)
	 */
	num_entries = htm_info_data + 0x10;
	to_copy = 32 + (be64_to_cpu(*num_entries) * 16);

	*ppos += to_copy;
	return simple_read_from_buffer(ubuf, count, &offset, htm_info_data, to_copy);
}

static ssize_t htmcaps_read(struct file *filp, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	void *htm_caps_data = filp->private_data;
	long rc, ret;

	/*
	 * Invoke H_HTM call with:
	 * - operation as htm capabilities (H_HTM_OP_CAPABILITIES)
	 * - last three values as addr, size (0x80 for Capabilities Output Buffer
	 *   and zero
	 */
	rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
				   htmtype, H_HTM_OP_CAPABILITIES, virt_to_phys(htm_caps_data),
				   0x80, 0);

	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall failed for op: H_HTM_OP_CAPABILITIES, returning %ld\n", ret);
		return ret;
	}

	return simple_read_from_buffer(ubuf, count, ppos, htm_caps_data, 0x80);
}

static const struct file_operations htminfo_fops = {
	.llseek = NULL,
	.read   = htminfo_read,
	.open   = simple_open,
};

static const struct file_operations htmcaps_fops = {
	.llseek = NULL,
	.read   = htmcaps_read,
	.open   = simple_open,
};

static int  htmsetup_set(void *data, u64 val)
{
	long rc, ret;

	/*
	 * Input value: HTM buffer size in the power of 2
	 * example: hex value 0x21 ( decimal: 33 ) is for
	 * 8GB
	 * Invoke H_HTM call with:
	 * - operation as htm start (H_HTM_OP_SETUP)
	 * - parameter 1 set to input value.
	 * - last two values are unused, hence set to zero
	 */
	rc = htm_hcall_wrapper(htmflags, nodeindex, nodalchipindex, coreindexonchip,
			htmtype, H_HTM_OP_SETUP, val, 0, 0);

	ret = htm_return_check(rc);
	if (ret <= 0) {
		pr_debug("H_HTM hcall failed for op: H_HTM_OP_SETUP, returning %ld\n", ret);
		return ret;
	}

	/* Set htmsetup if H_HTM_OP_SETUP operation succeeds */
	htmsetup = val;

	return 0;
}

static int htmsetup_get(void *data, u64 *val)
{
	*val = htmsetup;
	return 0;
}

static int  htmflags_set(void *data, u64 val)
{
	/*
	 * Input value:
	 * Currently supported flag value is to enable/disable
	 * HTM buffer wrap. wrap is used along with "configure"
	 * to prevent HTM buffer from wrapping.
	 * Writing 1 will set noWrap while configuring HTM
	 */
	if (val == HTM_NOWRAP)
		htmflags = H_HTM_FLAGS_NOWRAP;
	else if (val == HTM_WRAP)
		htmflags = 0;
	else
		return -EINVAL;

	return 0;
}

static int htmflags_get(void *data, u64 *val)
{
	*val = htmflags;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(htmconfigure_fops, htmconfigure_get, htmconfigure_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(htmstart_fops, htmstart_get, htmstart_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(htmsetup_fops, htmsetup_get, htmsetup_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(htmflags_fops, htmflags_get, htmflags_set, "%llu\n");

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
	debugfs_create_file("htmsetup", 0600, htmdump_debugfs_dir, NULL, &htmsetup_fops);
	debugfs_create_file("htmflags", 0600, htmdump_debugfs_dir, NULL, &htmflags_fops);

	/* Debugfs interface file to present status of HTM */
	htm_status_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!htm_status_buf) {
		pr_err("Failed to allocate htmstatus buf\n");
		return -ENOMEM;
	}

	/* Debugfs interface file to present System Processor Configuration */
	htm_info_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!htm_info_buf) {
		pr_err("Failed to allocate htm info buf\n");
		return -ENOMEM;
	}

	/* Debugfs interface file to present HTM capabilities */
	htm_caps_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!htm_caps_buf) {
		pr_err("Failed to allocate htm caps buf\n");
		return -ENOMEM;
	}

	/* Memory to present HTM system memory configuration */
	htm_mem_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!htm_mem_buf) {
		pr_err("Failed to allocate htm mem buf\n");
		return -ENOMEM;
	}

	debugfs_create_file("htmstatus", 0400, htmdump_debugfs_dir, htm_status_buf, &htmstatus_fops);
	debugfs_create_file("htminfo", 0400, htmdump_debugfs_dir, htm_info_buf, &htminfo_fops);
	debugfs_create_file("htmcaps", 0400, htmdump_debugfs_dir, htm_caps_buf, &htmcaps_fops);
	debugfs_create_file("htmsystem_mem", 0400, htmdump_debugfs_dir, htm_mem_buf, &htmsystem_mem_fops);

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
	kfree(htm_status_buf);
	kfree(htm_info_buf);
	kfree(htm_caps_buf);
	kfree(htm_mem_buf);
}

module_init(htmdump_init);
module_exit(htmdump_exit);
MODULE_DESCRIPTION("PHYP Hardware Trace Macro (HTM) data dumper");
MODULE_LICENSE("GPL");
