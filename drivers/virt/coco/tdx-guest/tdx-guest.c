// SPDX-License-Identifier: GPL-2.0
/*
 * TDX guest user interface driver
 *
 * Copyright (C) 2022 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/set_memory.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/tsm.h>
#include <linux/sizes.h>

#include <uapi/linux/tdx-guest.h>

#include <asm/cpu_device_id.h>
#include <asm/tdx.h>

/*
 * Intel's SGX QE implementation generally uses Quote size less
 * than 8K (2K Quote data + ~5K of certificate blob).
 */
#define GET_QUOTE_BUF_SIZE		SZ_8K

#define GET_QUOTE_CMD_VER		1

/* TDX GetQuote status codes */
#define GET_QUOTE_SUCCESS		0
#define GET_QUOTE_IN_FLIGHT		0xffffffffffffffff

/* struct tdx_quote_buf: Format of Quote request buffer.
 * @version: Quote format version, filled by TD.
 * @status: Status code of Quote request, filled by VMM.
 * @in_len: Length of TDREPORT, filled by TD.
 * @out_len: Length of Quote data, filled by VMM.
 * @data: Quote data on output or TDREPORT on input.
 *
 * More details of Quote request buffer can be found in TDX
 * Guest-Host Communication Interface (GHCI) for Intel TDX 1.0,
 * section titled "TDG.VP.VMCALL<GetQuote>"
 */
struct tdx_quote_buf {
	u64 version;
	u64 status;
	u32 in_len;
	u32 out_len;
	u8 data[];
};

/* Quote data buffer */
static void *quote_data;

/* Lock to streamline quote requests */
static DEFINE_MUTEX(quote_lock);

/*
 * GetQuote request timeout in seconds. Expect that 30 seconds
 * is enough time for QE to respond to any Quote requests.
 */
static u32 getquote_timeout = 30;

static long tdx_get_report0(struct tdx_report_req __user *req)
{
	u8 *reportdata, *tdreport;
	long ret;

	reportdata = kmalloc(TDX_REPORTDATA_LEN, GFP_KERNEL);
	if (!reportdata)
		return -ENOMEM;

	tdreport = kzalloc(TDX_REPORT_LEN, GFP_KERNEL);
	if (!tdreport) {
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(reportdata, req->reportdata, TDX_REPORTDATA_LEN)) {
		ret = -EFAULT;
		goto out;
	}

	/* Generate TDREPORT0 using "TDG.MR.REPORT" TDCALL */
	ret = tdx_mcall_get_report0(reportdata, tdreport);
	if (ret)
		goto out;

	if (copy_to_user(req->tdreport, tdreport, TDX_REPORT_LEN))
		ret = -EFAULT;

out:
	kfree(reportdata);
	kfree(tdreport);

	return ret;
}

static void free_quote_buf(void *buf)
{
	size_t len = PAGE_ALIGN(GET_QUOTE_BUF_SIZE);
	unsigned int count = len >> PAGE_SHIFT;

	if (set_memory_encrypted((unsigned long)buf, count)) {
		pr_err("Failed to restore encryption mask for Quote buffer, leak it\n");
		return;
	}

	free_pages_exact(buf, len);
}

static void *alloc_quote_buf(void)
{
	size_t len = PAGE_ALIGN(GET_QUOTE_BUF_SIZE);
	unsigned int count = len >> PAGE_SHIFT;
	void *addr;

	addr = alloc_pages_exact(len, GFP_KERNEL | __GFP_ZERO);
	if (!addr)
		return NULL;

	if (set_memory_decrypted((unsigned long)addr, count))
		return NULL;

	return addr;
}

/*
 * wait_for_quote_completion() - Wait for Quote request completion
 * @quote_buf: Address of Quote buffer.
 * @timeout: Timeout in seconds to wait for the Quote generation.
 *
 * As per TDX GHCI v1.0 specification, sec titled "TDG.VP.VMCALL<GetQuote>",
 * the status field in the Quote buffer will be set to GET_QUOTE_IN_FLIGHT
 * while VMM processes the GetQuote request, and will change it to success
 * or error code after processing is complete. So wait till the status
 * changes from GET_QUOTE_IN_FLIGHT or the request being timed out.
 */
static int wait_for_quote_completion(struct tdx_quote_buf *quote_buf, u32 timeout)
{
	int i = 0;

	/*
	 * Quote requests usually take a few seconds to complete, so waking up
	 * once per second to recheck the status is fine for this use case.
	 */
	while (quote_buf->status == GET_QUOTE_IN_FLIGHT && i++ < timeout) {
		if (msleep_interruptible(MSEC_PER_SEC))
			return -EINTR;
	}

	return (i == timeout) ? -ETIMEDOUT : 0;
}

static int tdx_report_new(struct tsm_report *report, void *data)
{
	u8 *buf, *reportdata = NULL, *tdreport = NULL;
	struct tdx_quote_buf *quote_buf = quote_data;
	struct tsm_desc *desc = &report->desc;
	int ret;
	u64 err;

	/* TODO: switch to guard(mutex_intr) */
	if (mutex_lock_interruptible(&quote_lock))
		return -EINTR;

	/*
	 * If the previous request is timedout or interrupted, and the
	 * Quote buf status is still in GET_QUOTE_IN_FLIGHT (owned by
	 * VMM), don't permit any new request.
	 */
	if (quote_buf->status == GET_QUOTE_IN_FLIGHT) {
		ret = -EBUSY;
		goto done;
	}

	if (desc->inblob_len != TDX_REPORTDATA_LEN) {
		ret = -EINVAL;
		goto done;
	}

	reportdata = kmalloc(TDX_REPORTDATA_LEN, GFP_KERNEL);
	if (!reportdata) {
		ret = -ENOMEM;
		goto done;
	}

	tdreport = kzalloc(TDX_REPORT_LEN, GFP_KERNEL);
	if (!tdreport) {
		ret = -ENOMEM;
		goto done;
	}

	memcpy(reportdata, desc->inblob, desc->inblob_len);

	/* Generate TDREPORT0 using "TDG.MR.REPORT" TDCALL */
	ret = tdx_mcall_get_report0(reportdata, tdreport);
	if (ret) {
		pr_err("GetReport call failed\n");
		goto done;
	}

	memset(quote_data, 0, GET_QUOTE_BUF_SIZE);

	/* Update Quote buffer header */
	quote_buf->version = GET_QUOTE_CMD_VER;
	quote_buf->in_len = TDX_REPORT_LEN;

	memcpy(quote_buf->data, tdreport, TDX_REPORT_LEN);

	err = tdx_hcall_get_quote(quote_data, GET_QUOTE_BUF_SIZE);
	if (err) {
		pr_err("GetQuote hypercall failed, status:%llx\n", err);
		ret = -EIO;
		goto done;
	}

	ret = wait_for_quote_completion(quote_buf, getquote_timeout);
	if (ret) {
		pr_err("GetQuote request timedout\n");
		goto done;
	}

	buf = kvmemdup(quote_buf->data, quote_buf->out_len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto done;
	}

	report->outblob = buf;
	report->outblob_len = quote_buf->out_len;

	/*
	 * TODO: parse the PEM-formatted cert chain out of the quote buffer when
	 * provided
	 */
done:
	mutex_unlock(&quote_lock);
	kfree(reportdata);
	kfree(tdreport);

	return ret;
}

static bool tdx_report_attr_visible(int n)
{
	switch (n) {
	case TSM_REPORT_GENERATION:
	case TSM_REPORT_PROVIDER:
		return true;
	}

	return false;
}

static bool tdx_report_bin_attr_visible(int n)
{
	switch (n) {
	case TSM_REPORT_INBLOB:
	case TSM_REPORT_OUTBLOB:
		return true;
	}

	return false;
}

static long tdx_guest_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	switch (cmd) {
	case TDX_CMD_GET_REPORT0:
		return tdx_get_report0((struct tdx_report_req __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations tdx_guest_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tdx_guest_ioctl,
};

static struct miscdevice tdx_misc_dev = {
	.name = KBUILD_MODNAME,
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &tdx_guest_fops,
};

static const struct x86_cpu_id tdx_guest_ids[] = {
	X86_MATCH_FEATURE(X86_FEATURE_TDX_GUEST, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, tdx_guest_ids);

static const struct tsm_ops tdx_tsm_ops = {
	.name = KBUILD_MODNAME,
	.report_new = tdx_report_new,
	.report_attr_visible = tdx_report_attr_visible,
	.report_bin_attr_visible = tdx_report_bin_attr_visible,
};

static int __init tdx_guest_init(void)
{
	int ret;

	if (!x86_match_cpu(tdx_guest_ids))
		return -ENODEV;

	ret = misc_register(&tdx_misc_dev);
	if (ret)
		return ret;

	quote_data = alloc_quote_buf();
	if (!quote_data) {
		pr_err("Failed to allocate Quote buffer\n");
		ret = -ENOMEM;
		goto free_misc;
	}

	ret = tsm_register(&tdx_tsm_ops, NULL);
	if (ret)
		goto free_quote;

	return 0;

free_quote:
	free_quote_buf(quote_data);
free_misc:
	misc_deregister(&tdx_misc_dev);

	return ret;
}
module_init(tdx_guest_init);

static void __exit tdx_guest_exit(void)
{
	tsm_unregister(&tdx_tsm_ops);
	free_quote_buf(quote_data);
	misc_deregister(&tdx_misc_dev);
}
module_exit(tdx_guest_exit);

MODULE_AUTHOR("Kuppuswamy Sathyanarayanan <sathyanarayanan.kuppuswamy@linux.intel.com>");
MODULE_DESCRIPTION("TDX Guest Driver");
MODULE_LICENSE("GPL");
