// SPDX-License-Identifier: GPL-2.0
/*
 * TDX guest user interface driver
 *
 * Copyright (C) 2022 Intel Corporation
 */

#define pr_fmt(fmt)			KBUILD_MODNAME ": " fmt

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
#include <linux/sockptr.h>
#include <linux/tsm.h>
#include <linux/tsm-mr.h>

#include <uapi/linux/tdx-guest.h>

#include <asm/cpu_device_id.h>
#include <asm/tdx.h>

/* TDREPORT buffer */
static u8 *tdx_report_buf;

/* Lock to serialize TDG.MR.REPORT and TDG.MR.RTMR.EXTEND TDCALLs */
static DEFINE_MUTEX(mr_lock);

/* TDREPORT fields */
enum {
	TDREPORT_reportdata = 128,
	TDREPORT_tee_tcb_info = 256,
	TDREPORT_tdinfo = TDREPORT_tee_tcb_info + 256,
	TDREPORT_attributes = TDREPORT_tdinfo,
	TDREPORT_xfam = TDREPORT_attributes + sizeof(u64),
	TDREPORT_mrtd = TDREPORT_xfam + sizeof(u64),
	TDREPORT_mrconfigid = TDREPORT_mrtd + SHA384_DIGEST_SIZE,
	TDREPORT_mrowner = TDREPORT_mrconfigid + SHA384_DIGEST_SIZE,
	TDREPORT_mrownerconfig = TDREPORT_mrowner + SHA384_DIGEST_SIZE,
	TDREPORT_rtmr0 = TDREPORT_mrownerconfig + SHA384_DIGEST_SIZE,
	TDREPORT_rtmr1 = TDREPORT_rtmr0 + SHA384_DIGEST_SIZE,
	TDREPORT_rtmr2 = TDREPORT_rtmr1 + SHA384_DIGEST_SIZE,
	TDREPORT_rtmr3 = TDREPORT_rtmr2 + SHA384_DIGEST_SIZE,
	TDREPORT_servtd_hash = TDREPORT_rtmr3 + SHA384_DIGEST_SIZE,
};

static int tdx_do_report(sockptr_t data, sockptr_t tdreport)
{
	scoped_cond_guard(mutex_intr, return -EINTR, &mr_lock) {
		u8 *reportdata = tdx_report_buf + TDREPORT_reportdata;
		int ret;

		if (!sockptr_is_null(data) &&
		    copy_from_sockptr(reportdata, data, TDX_REPORTDATA_LEN))
			return -EFAULT;

		ret = tdx_mcall_get_report0(reportdata, tdx_report_buf);
		if (WARN_ONCE(ret, "tdx_mcall_get_report0() failed: %d", ret))
			return ret;

		if (!sockptr_is_null(tdreport) &&
		    copy_to_sockptr(tdreport, tdx_report_buf, TDX_REPORT_LEN))
			return -EFAULT;
	}
	return 0;
}

static int tdx_do_extend(u8 mr_ind, const u8 *data)
{
	scoped_cond_guard(mutex_intr, return -EINTR, &mr_lock) {
		/*
		 * TDX requires @extend_buf to be 64-byte aligned.
		 * It's safe to use REPORTDATA buffer for that purpose because
		 * tdx_mr_report/extend_lock() are mutually exclusive.
		 */
		u8 *extend_buf = tdx_report_buf + TDREPORT_reportdata;
		int ret;

		memcpy(extend_buf, data, SHA384_DIGEST_SIZE);

		ret = tdx_mcall_extend_rtmr(mr_ind, extend_buf);
		if (WARN_ONCE(ret, "tdx_mcall_extend_rtmr(%u) failed: %d", mr_ind, ret))
			return ret;
	}
	return 0;
}

#define TDX_MR_(r) .mr_value = (void *)TDREPORT_##r, TSM_MR_(r, SHA384)
static struct tsm_measurement_register tdx_mrs[] = {
	{ TDX_MR_(rtmr0) | TSM_MR_F_RTMR },
	{ TDX_MR_(rtmr1) | TSM_MR_F_RTMR },
	{ TDX_MR_(rtmr2) | TSM_MR_F_RTMR },
	{ TDX_MR_(rtmr3) | TSM_MR_F_RTMR },
	{ TDX_MR_(mrtd) },
	{ TDX_MR_(mrconfigid) | TSM_MR_F_NOHASH },
	{ TDX_MR_(mrowner) | TSM_MR_F_NOHASH },
	{ TDX_MR_(mrownerconfig) | TSM_MR_F_NOHASH },
};
#undef TDX_MR_

static int tdx_mr_refresh(const struct tsm_measurements *tm)
{
	return tdx_do_report(KERNEL_SOCKPTR(NULL), KERNEL_SOCKPTR(NULL));
}

static int tdx_mr_extend(const struct tsm_measurements *tm,
			 const struct tsm_measurement_register *mr,
			 const u8 *data)
{
	return tdx_do_extend(mr - tm->mrs, data);
}

static struct tsm_measurements tdx_measurements = {
	.mrs = tdx_mrs,
	.nr_mrs = ARRAY_SIZE(tdx_mrs),
	.refresh = tdx_mr_refresh,
	.write = tdx_mr_extend,
};

static const struct attribute_group *tdx_mr_init(void)
{
	const struct attribute_group *g;
	int rc;

	u8 *buf __free(kfree) = kzalloc(TDX_REPORT_LEN, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	tdx_report_buf = buf;
	rc = tdx_mr_refresh(&tdx_measurements);
	if (rc)
		return ERR_PTR(rc);

	/*
	 * @mr_value was initialized with the offset only, while the base
	 * address is being added here.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(tdx_mrs); ++i)
		*(long *)&tdx_mrs[i].mr_value += (long)buf;

	g = tsm_mr_create_attribute_group(&tdx_measurements);
	if (!IS_ERR(g))
		tdx_report_buf = no_free_ptr(buf);

	return g;
}

static void tdx_mr_deinit(const struct attribute_group *mr_grp)
{
	tsm_mr_free_attribute_group(mr_grp);
	kfree(tdx_report_buf);
}

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
	return tdx_do_report(USER_SOCKPTR(req->reportdata),
			     USER_SOCKPTR(req->tdreport));
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

static int tdx_report_new_locked(struct tsm_report *report, void *data)
{
	u8 *buf;
	struct tdx_quote_buf *quote_buf = quote_data;
	struct tsm_report_desc *desc = &report->desc;
	int ret;
	u64 err;

	/*
	 * If the previous request is timedout or interrupted, and the
	 * Quote buf status is still in GET_QUOTE_IN_FLIGHT (owned by
	 * VMM), don't permit any new request.
	 */
	if (quote_buf->status == GET_QUOTE_IN_FLIGHT)
		return -EBUSY;

	if (desc->inblob_len != TDX_REPORTDATA_LEN)
		return -EINVAL;

	memset(quote_data, 0, GET_QUOTE_BUF_SIZE);

	/* Update Quote buffer header */
	quote_buf->version = GET_QUOTE_CMD_VER;
	quote_buf->in_len = TDX_REPORT_LEN;

	ret = tdx_do_report(KERNEL_SOCKPTR(desc->inblob),
			    KERNEL_SOCKPTR(quote_buf->data));
	if (ret)
		return ret;

	err = tdx_hcall_get_quote(quote_data, GET_QUOTE_BUF_SIZE);
	if (err) {
		pr_err("GetQuote hypercall failed, status:%llx\n", err);
		return -EIO;
	}

	ret = wait_for_quote_completion(quote_buf, getquote_timeout);
	if (ret) {
		pr_err("GetQuote request timedout\n");
		return ret;
	}

	buf = kvmemdup(quote_buf->data, quote_buf->out_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	report->outblob = buf;
	report->outblob_len = quote_buf->out_len;

	/*
	 * TODO: parse the PEM-formatted cert chain out of the quote buffer when
	 * provided
	 */

	return ret;
}

static int tdx_report_new(struct tsm_report *report, void *data)
{
	scoped_cond_guard(mutex_intr, return -EINTR, &quote_lock)
		return tdx_report_new_locked(report, data);
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

static const struct attribute_group *tdx_attr_groups[] = {
	NULL, /* measurements */
	NULL
};

static struct miscdevice tdx_misc_dev = {
	.name = KBUILD_MODNAME,
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &tdx_guest_fops,
	.groups = tdx_attr_groups,
};

static const struct x86_cpu_id tdx_guest_ids[] = {
	X86_MATCH_FEATURE(X86_FEATURE_TDX_GUEST, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, tdx_guest_ids);

static const struct tsm_report_ops tdx_tsm_ops = {
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

	tdx_attr_groups[0] = tdx_mr_init();
	if (IS_ERR(tdx_attr_groups[0]))
		return PTR_ERR(tdx_attr_groups[0]);

	ret = misc_register(&tdx_misc_dev);
	if (ret)
		goto deinit_mr;

	quote_data = alloc_quote_buf();
	if (!quote_data) {
		pr_err("Failed to allocate Quote buffer\n");
		ret = -ENOMEM;
		goto free_misc;
	}

	ret = tsm_report_register(&tdx_tsm_ops, NULL);
	if (ret)
		goto free_quote;

	return 0;

free_quote:
	free_quote_buf(quote_data);
free_misc:
	misc_deregister(&tdx_misc_dev);
deinit_mr:
	tdx_mr_deinit(tdx_attr_groups[0]);

	return ret;
}
module_init(tdx_guest_init);

static void __exit tdx_guest_exit(void)
{
	tsm_report_unregister(&tdx_tsm_ops);
	free_quote_buf(quote_data);
	misc_deregister(&tdx_misc_dev);
	tdx_mr_deinit(tdx_attr_groups[0]);
}
module_exit(tdx_guest_exit);

MODULE_AUTHOR("Kuppuswamy Sathyanarayanan <sathyanarayanan.kuppuswamy@linux.intel.com>");
MODULE_DESCRIPTION("TDX Guest Driver");
MODULE_LICENSE("GPL");
