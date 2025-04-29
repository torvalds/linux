// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Encrypted Virtualization (SEV) guest driver interface
 *
 * Copyright (C) 2021-2024 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/set_memory.h>
#include <linux/fs.h>
#include <linux/tsm.h>
#include <crypto/gcm.h>
#include <linux/psp-sev.h>
#include <linux/sockptr.h>
#include <linux/cleanup.h>
#include <linux/uuid.h>
#include <linux/configfs.h>
#include <linux/mm.h>
#include <uapi/linux/sev-guest.h>
#include <uapi/linux/psp-sev.h>

#include <asm/svm.h>
#include <asm/sev.h>

#define DEVICE_NAME	"sev-guest"

#define SVSM_MAX_RETRIES		3

struct snp_guest_dev {
	struct device *dev;
	struct miscdevice misc;

	struct snp_msg_desc *msg_desc;
};

/*
 * The VMPCK ID represents the key used by the SNP guest to communicate with the
 * SEV firmware in the AMD Secure Processor (ASP, aka PSP). By default, the key
 * used will be the key associated with the VMPL at which the guest is running.
 * Should the default key be wiped (see snp_disable_vmpck()), this parameter
 * allows for using one of the remaining VMPCKs.
 */
static int vmpck_id = -1;
module_param(vmpck_id, int, 0444);
MODULE_PARM_DESC(vmpck_id, "The VMPCK ID to use when communicating with the PSP.");

static inline struct snp_guest_dev *to_snp_dev(struct file *file)
{
	struct miscdevice *dev = file->private_data;

	return container_of(dev, struct snp_guest_dev, misc);
}

struct snp_req_resp {
	sockptr_t req_data;
	sockptr_t resp_data;
};

static int get_report(struct snp_guest_dev *snp_dev, struct snp_guest_request_ioctl *arg)
{
	struct snp_report_req *report_req __free(kfree) = NULL;
	struct snp_msg_desc *mdesc = snp_dev->msg_desc;
	struct snp_report_resp *report_resp;
	struct snp_guest_req req = {};
	int rc, resp_len;

	if (!arg->req_data || !arg->resp_data)
		return -EINVAL;

	report_req = kzalloc(sizeof(*report_req), GFP_KERNEL_ACCOUNT);
	if (!report_req)
		return -ENOMEM;

	if (copy_from_user(report_req, (void __user *)arg->req_data, sizeof(*report_req)))
		return -EFAULT;

	/*
	 * The intermediate response buffer is used while decrypting the
	 * response payload. Make sure that it has enough space to cover the
	 * authtag.
	 */
	resp_len = sizeof(report_resp->data) + mdesc->ctx->authsize;
	report_resp = kzalloc(resp_len, GFP_KERNEL_ACCOUNT);
	if (!report_resp)
		return -ENOMEM;

	req.msg_version = arg->msg_version;
	req.msg_type = SNP_MSG_REPORT_REQ;
	req.vmpck_id = mdesc->vmpck_id;
	req.req_buf = report_req;
	req.req_sz = sizeof(*report_req);
	req.resp_buf = report_resp->data;
	req.resp_sz = resp_len;
	req.exit_code = SVM_VMGEXIT_GUEST_REQUEST;

	rc = snp_send_guest_request(mdesc, &req, arg);
	if (rc)
		goto e_free;

	if (copy_to_user((void __user *)arg->resp_data, report_resp, sizeof(*report_resp)))
		rc = -EFAULT;

e_free:
	kfree(report_resp);
	return rc;
}

static int get_derived_key(struct snp_guest_dev *snp_dev, struct snp_guest_request_ioctl *arg)
{
	struct snp_derived_key_req *derived_key_req __free(kfree) = NULL;
	struct snp_derived_key_resp derived_key_resp = {0};
	struct snp_msg_desc *mdesc = snp_dev->msg_desc;
	struct snp_guest_req req = {};
	int rc, resp_len;
	/* Response data is 64 bytes and max authsize for GCM is 16 bytes. */
	u8 buf[64 + 16];

	if (!arg->req_data || !arg->resp_data)
		return -EINVAL;

	/*
	 * The intermediate response buffer is used while decrypting the
	 * response payload. Make sure that it has enough space to cover the
	 * authtag.
	 */
	resp_len = sizeof(derived_key_resp.data) + mdesc->ctx->authsize;
	if (sizeof(buf) < resp_len)
		return -ENOMEM;

	derived_key_req = kzalloc(sizeof(*derived_key_req), GFP_KERNEL_ACCOUNT);
	if (!derived_key_req)
		return -ENOMEM;

	if (copy_from_user(derived_key_req, (void __user *)arg->req_data,
			   sizeof(*derived_key_req)))
		return -EFAULT;

	req.msg_version = arg->msg_version;
	req.msg_type = SNP_MSG_KEY_REQ;
	req.vmpck_id = mdesc->vmpck_id;
	req.req_buf = derived_key_req;
	req.req_sz = sizeof(*derived_key_req);
	req.resp_buf = buf;
	req.resp_sz = resp_len;
	req.exit_code = SVM_VMGEXIT_GUEST_REQUEST;

	rc = snp_send_guest_request(mdesc, &req, arg);
	if (rc)
		return rc;

	memcpy(derived_key_resp.data, buf, sizeof(derived_key_resp.data));
	if (copy_to_user((void __user *)arg->resp_data, &derived_key_resp,
			 sizeof(derived_key_resp)))
		rc = -EFAULT;

	/* The response buffer contains the sensitive data, explicitly clear it. */
	memzero_explicit(buf, sizeof(buf));
	memzero_explicit(&derived_key_resp, sizeof(derived_key_resp));
	return rc;
}

static int get_ext_report(struct snp_guest_dev *snp_dev, struct snp_guest_request_ioctl *arg,
			  struct snp_req_resp *io)

{
	struct snp_ext_report_req *report_req __free(kfree) = NULL;
	struct snp_msg_desc *mdesc = snp_dev->msg_desc;
	struct snp_report_resp *report_resp;
	struct snp_guest_req req = {};
	int ret, npages = 0, resp_len;
	sockptr_t certs_address;
	struct page *page;

	if (sockptr_is_null(io->req_data) || sockptr_is_null(io->resp_data))
		return -EINVAL;

	report_req = kzalloc(sizeof(*report_req), GFP_KERNEL_ACCOUNT);
	if (!report_req)
		return -ENOMEM;

	if (copy_from_sockptr(report_req, io->req_data, sizeof(*report_req)))
		return -EFAULT;

	/* caller does not want certificate data */
	if (!report_req->certs_len || !report_req->certs_address)
		goto cmd;

	if (report_req->certs_len > SEV_FW_BLOB_MAX_SIZE ||
	    !IS_ALIGNED(report_req->certs_len, PAGE_SIZE))
		return -EINVAL;

	if (sockptr_is_kernel(io->resp_data)) {
		certs_address = KERNEL_SOCKPTR((void *)report_req->certs_address);
	} else {
		certs_address = USER_SOCKPTR((void __user *)report_req->certs_address);
		if (!access_ok(certs_address.user, report_req->certs_len))
			return -EFAULT;
	}

	/*
	 * Initialize the intermediate buffer with all zeros. This buffer
	 * is used in the guest request message to get the certs blob from
	 * the host. If host does not supply any certs in it, then copy
	 * zeros to indicate that certificate data was not provided.
	 */
	npages = report_req->certs_len >> PAGE_SHIFT;
	page = alloc_pages(GFP_KERNEL_ACCOUNT | __GFP_ZERO,
			   get_order(report_req->certs_len));
	if (!page)
		return -ENOMEM;

	req.certs_data = page_address(page);
	ret = set_memory_decrypted((unsigned long)req.certs_data, npages);
	if (ret) {
		pr_err("failed to mark page shared, ret=%d\n", ret);
		__free_pages(page, get_order(report_req->certs_len));
		return -EFAULT;
	}

cmd:
	/*
	 * The intermediate response buffer is used while decrypting the
	 * response payload. Make sure that it has enough space to cover the
	 * authtag.
	 */
	resp_len = sizeof(report_resp->data) + mdesc->ctx->authsize;
	report_resp = kzalloc(resp_len, GFP_KERNEL_ACCOUNT);
	if (!report_resp) {
		ret = -ENOMEM;
		goto e_free_data;
	}

	req.input.data_npages = npages;

	req.msg_version = arg->msg_version;
	req.msg_type = SNP_MSG_REPORT_REQ;
	req.vmpck_id = mdesc->vmpck_id;
	req.req_buf = &report_req->data;
	req.req_sz = sizeof(report_req->data);
	req.resp_buf = report_resp->data;
	req.resp_sz = resp_len;
	req.exit_code = SVM_VMGEXIT_EXT_GUEST_REQUEST;

	ret = snp_send_guest_request(mdesc, &req, arg);

	/* If certs length is invalid then copy the returned length */
	if (arg->vmm_error == SNP_GUEST_VMM_ERR_INVALID_LEN) {
		report_req->certs_len = req.input.data_npages << PAGE_SHIFT;

		if (copy_to_sockptr(io->req_data, report_req, sizeof(*report_req)))
			ret = -EFAULT;
	}

	if (ret)
		goto e_free;

	if (npages && copy_to_sockptr(certs_address, req.certs_data, report_req->certs_len)) {
		ret = -EFAULT;
		goto e_free;
	}

	if (copy_to_sockptr(io->resp_data, report_resp, sizeof(*report_resp)))
		ret = -EFAULT;

e_free:
	kfree(report_resp);
e_free_data:
	if (npages) {
		if (set_memory_encrypted((unsigned long)req.certs_data, npages))
			WARN_ONCE(ret, "failed to restore encryption mask (leak it)\n");
		else
			__free_pages(page, get_order(report_req->certs_len));
	}
	return ret;
}

static long snp_guest_ioctl(struct file *file, unsigned int ioctl, unsigned long arg)
{
	struct snp_guest_dev *snp_dev = to_snp_dev(file);
	void __user *argp = (void __user *)arg;
	struct snp_guest_request_ioctl input;
	struct snp_req_resp io;
	int ret = -ENOTTY;

	if (copy_from_user(&input, argp, sizeof(input)))
		return -EFAULT;

	input.exitinfo2 = 0xff;

	/* Message version must be non-zero */
	if (!input.msg_version)
		return -EINVAL;

	switch (ioctl) {
	case SNP_GET_REPORT:
		ret = get_report(snp_dev, &input);
		break;
	case SNP_GET_DERIVED_KEY:
		ret = get_derived_key(snp_dev, &input);
		break;
	case SNP_GET_EXT_REPORT:
		/*
		 * As get_ext_report() may be called from the ioctl() path and a
		 * kernel internal path (configfs-tsm), decorate the passed
		 * buffers as user pointers.
		 */
		io.req_data = USER_SOCKPTR((void __user *)input.req_data);
		io.resp_data = USER_SOCKPTR((void __user *)input.resp_data);
		ret = get_ext_report(snp_dev, &input, &io);
		break;
	default:
		break;
	}

	if (input.exitinfo2 && copy_to_user(argp, &input, sizeof(input)))
		return -EFAULT;

	return ret;
}

static const struct file_operations snp_guest_fops = {
	.owner	= THIS_MODULE,
	.unlocked_ioctl = snp_guest_ioctl,
};

struct snp_msg_report_resp_hdr {
	u32 status;
	u32 report_size;
	u8 rsvd[24];
};

struct snp_msg_cert_entry {
	guid_t guid;
	u32 offset;
	u32 length;
};

static int sev_svsm_report_new(struct tsm_report *report, void *data)
{
	unsigned int rep_len, man_len, certs_len;
	struct tsm_desc *desc = &report->desc;
	struct svsm_attest_call ac = {};
	unsigned int retry_count;
	void *rep, *man, *certs;
	struct svsm_call call;
	unsigned int size;
	bool try_again;
	void *buffer;
	u64 call_id;
	int ret;

	/*
	 * Allocate pages for the request:
	 * - Report blob (4K)
	 * - Manifest blob (4K)
	 * - Certificate blob (16K)
	 *
	 * Above addresses must be 4K aligned
	 */
	rep_len = SZ_4K;
	man_len = SZ_4K;
	certs_len = SEV_FW_BLOB_MAX_SIZE;

	if (guid_is_null(&desc->service_guid)) {
		call_id = SVSM_ATTEST_CALL(SVSM_ATTEST_SERVICES);
	} else {
		export_guid(ac.service_guid, &desc->service_guid);
		ac.service_manifest_ver = desc->service_manifest_version;

		call_id = SVSM_ATTEST_CALL(SVSM_ATTEST_SINGLE_SERVICE);
	}

	retry_count = 0;

retry:
	memset(&call, 0, sizeof(call));

	size = rep_len + man_len + certs_len;
	buffer = alloc_pages_exact(size, __GFP_ZERO);
	if (!buffer)
		return -ENOMEM;

	rep = buffer;
	ac.report_buf.pa = __pa(rep);
	ac.report_buf.len = rep_len;

	man = rep + rep_len;
	ac.manifest_buf.pa = __pa(man);
	ac.manifest_buf.len = man_len;

	certs = man + man_len;
	ac.certificates_buf.pa = __pa(certs);
	ac.certificates_buf.len = certs_len;

	ac.nonce.pa = __pa(desc->inblob);
	ac.nonce.len = desc->inblob_len;

	ret = snp_issue_svsm_attest_req(call_id, &call, &ac);
	if (ret) {
		free_pages_exact(buffer, size);

		switch (call.rax_out) {
		case SVSM_ERR_INVALID_PARAMETER:
			try_again = false;

			if (ac.report_buf.len > rep_len) {
				rep_len = PAGE_ALIGN(ac.report_buf.len);
				try_again = true;
			}

			if (ac.manifest_buf.len > man_len) {
				man_len = PAGE_ALIGN(ac.manifest_buf.len);
				try_again = true;
			}

			if (ac.certificates_buf.len > certs_len) {
				certs_len = PAGE_ALIGN(ac.certificates_buf.len);
				try_again = true;
			}

			/* If one of the buffers wasn't large enough, retry the request */
			if (try_again && retry_count < SVSM_MAX_RETRIES) {
				retry_count++;
				goto retry;
			}

			return -EINVAL;
		default:
			pr_err_ratelimited("SVSM attestation request failed (%d / 0x%llx)\n",
					   ret, call.rax_out);
			return -EINVAL;
		}
	}

	/*
	 * Allocate all the blob memory buffers at once so that the cleanup is
	 * done for errors that occur after the first allocation (i.e. before
	 * using no_free_ptr()).
	 */
	rep_len = ac.report_buf.len;
	void *rbuf __free(kvfree) = kvzalloc(rep_len, GFP_KERNEL);

	man_len = ac.manifest_buf.len;
	void *mbuf __free(kvfree) = kvzalloc(man_len, GFP_KERNEL);

	certs_len = ac.certificates_buf.len;
	void *cbuf __free(kvfree) = certs_len ? kvzalloc(certs_len, GFP_KERNEL) : NULL;

	if (!rbuf || !mbuf || (certs_len && !cbuf)) {
		free_pages_exact(buffer, size);
		return -ENOMEM;
	}

	memcpy(rbuf, rep, rep_len);
	report->outblob = no_free_ptr(rbuf);
	report->outblob_len = rep_len;

	memcpy(mbuf, man, man_len);
	report->manifestblob = no_free_ptr(mbuf);
	report->manifestblob_len = man_len;

	if (certs_len) {
		memcpy(cbuf, certs, certs_len);
		report->auxblob = no_free_ptr(cbuf);
		report->auxblob_len = certs_len;
	}

	free_pages_exact(buffer, size);

	return 0;
}

static int sev_report_new(struct tsm_report *report, void *data)
{
	struct snp_msg_cert_entry *cert_table;
	struct tsm_desc *desc = &report->desc;
	struct snp_guest_dev *snp_dev = data;
	struct snp_msg_report_resp_hdr hdr;
	const u32 report_size = SZ_4K;
	const u32 ext_size = SEV_FW_BLOB_MAX_SIZE;
	u32 certs_size, i, size = report_size + ext_size;
	int ret;

	if (desc->inblob_len != SNP_REPORT_USER_DATA_SIZE)
		return -EINVAL;

	if (desc->service_provider) {
		if (strcmp(desc->service_provider, "svsm"))
			return -EINVAL;

		return sev_svsm_report_new(report, data);
	}

	void *buf __free(kvfree) = kvzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	cert_table = buf + report_size;
	struct snp_ext_report_req ext_req = {
		.data = { .vmpl = desc->privlevel },
		.certs_address = (__u64)cert_table,
		.certs_len = ext_size,
	};
	memcpy(&ext_req.data.user_data, desc->inblob, desc->inblob_len);

	struct snp_guest_request_ioctl input = {
		.msg_version = 1,
		.req_data = (__u64)&ext_req,
		.resp_data = (__u64)buf,
		.exitinfo2 = 0xff,
	};
	struct snp_req_resp io = {
		.req_data = KERNEL_SOCKPTR(&ext_req),
		.resp_data = KERNEL_SOCKPTR(buf),
	};

	ret = get_ext_report(snp_dev, &input, &io);
	if (ret)
		return ret;

	memcpy(&hdr, buf, sizeof(hdr));
	if (hdr.status == SEV_RET_INVALID_PARAM)
		return -EINVAL;
	if (hdr.status == SEV_RET_INVALID_KEY)
		return -EINVAL;
	if (hdr.status)
		return -ENXIO;
	if ((hdr.report_size + sizeof(hdr)) > report_size)
		return -ENOMEM;

	void *rbuf __free(kvfree) = kvzalloc(hdr.report_size, GFP_KERNEL);
	if (!rbuf)
		return -ENOMEM;

	memcpy(rbuf, buf + sizeof(hdr), hdr.report_size);
	report->outblob = no_free_ptr(rbuf);
	report->outblob_len = hdr.report_size;

	certs_size = 0;
	for (i = 0; i < ext_size / sizeof(struct snp_msg_cert_entry); i++) {
		struct snp_msg_cert_entry *ent = &cert_table[i];

		if (guid_is_null(&ent->guid) && !ent->offset && !ent->length)
			break;
		certs_size = max(certs_size, ent->offset + ent->length);
	}

	/* Suspicious that the response populated entries without populating size */
	if (!certs_size && i)
		dev_warn_ratelimited(snp_dev->dev, "certificate slots conveyed without size\n");

	/* No certs to report */
	if (!certs_size)
		return 0;

	/* Suspicious that the certificate blob size contract was violated
	 */
	if (certs_size > ext_size) {
		dev_warn_ratelimited(snp_dev->dev, "certificate data truncated\n");
		certs_size = ext_size;
	}

	void *cbuf __free(kvfree) = kvzalloc(certs_size, GFP_KERNEL);
	if (!cbuf)
		return -ENOMEM;

	memcpy(cbuf, cert_table, certs_size);
	report->auxblob = no_free_ptr(cbuf);
	report->auxblob_len = certs_size;

	return 0;
}

static bool sev_report_attr_visible(int n)
{
	switch (n) {
	case TSM_REPORT_GENERATION:
	case TSM_REPORT_PROVIDER:
	case TSM_REPORT_PRIVLEVEL:
	case TSM_REPORT_PRIVLEVEL_FLOOR:
		return true;
	case TSM_REPORT_SERVICE_PROVIDER:
	case TSM_REPORT_SERVICE_GUID:
	case TSM_REPORT_SERVICE_MANIFEST_VER:
		return snp_vmpl;
	}

	return false;
}

static bool sev_report_bin_attr_visible(int n)
{
	switch (n) {
	case TSM_REPORT_INBLOB:
	case TSM_REPORT_OUTBLOB:
	case TSM_REPORT_AUXBLOB:
		return true;
	case TSM_REPORT_MANIFESTBLOB:
		return snp_vmpl;
	}

	return false;
}

static struct tsm_ops sev_tsm_ops = {
	.name = KBUILD_MODNAME,
	.report_new = sev_report_new,
	.report_attr_visible = sev_report_attr_visible,
	.report_bin_attr_visible = sev_report_bin_attr_visible,
};

static void unregister_sev_tsm(void *data)
{
	tsm_unregister(&sev_tsm_ops);
}

static int __init sev_guest_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snp_guest_dev *snp_dev;
	struct snp_msg_desc *mdesc;
	struct miscdevice *misc;
	int ret;

	BUILD_BUG_ON(sizeof(struct snp_guest_msg) > PAGE_SIZE);

	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return -ENODEV;

	snp_dev = devm_kzalloc(&pdev->dev, sizeof(struct snp_guest_dev), GFP_KERNEL);
	if (!snp_dev)
		return -ENOMEM;

	mdesc = snp_msg_alloc();
	if (IS_ERR_OR_NULL(mdesc))
		return -ENOMEM;

	ret = snp_msg_init(mdesc, vmpck_id);
	if (ret)
		goto e_msg_init;

	platform_set_drvdata(pdev, snp_dev);
	snp_dev->dev = dev;

	misc = &snp_dev->misc;
	misc->minor = MISC_DYNAMIC_MINOR;
	misc->name = DEVICE_NAME;
	misc->fops = &snp_guest_fops;

	/* Set the privlevel_floor attribute based on the vmpck_id */
	sev_tsm_ops.privlevel_floor = mdesc->vmpck_id;

	ret = tsm_register(&sev_tsm_ops, snp_dev);
	if (ret)
		goto e_msg_init;

	ret = devm_add_action_or_reset(&pdev->dev, unregister_sev_tsm, NULL);
	if (ret)
		goto e_msg_init;

	ret =  misc_register(misc);
	if (ret)
		goto e_msg_init;

	snp_dev->msg_desc = mdesc;
	dev_info(dev, "Initialized SEV guest driver (using VMPCK%d communication key)\n",
		 mdesc->vmpck_id);
	return 0;

e_msg_init:
	snp_msg_free(mdesc);

	return ret;
}

static void __exit sev_guest_remove(struct platform_device *pdev)
{
	struct snp_guest_dev *snp_dev = platform_get_drvdata(pdev);

	snp_msg_free(snp_dev->msg_desc);
	misc_deregister(&snp_dev->misc);
}

/*
 * This driver is meant to be a common SEV guest interface driver and to
 * support any SEV guest API. As such, even though it has been introduced
 * with the SEV-SNP support, it is named "sev-guest".
 *
 * sev_guest_remove() lives in .exit.text. For drivers registered via
 * module_platform_driver_probe() this is ok because they cannot get unbound
 * at runtime. So mark the driver struct with __refdata to prevent modpost
 * triggering a section mismatch warning.
 */
static struct platform_driver sev_guest_driver __refdata = {
	.remove		= __exit_p(sev_guest_remove),
	.driver		= {
		.name = "sev-guest",
	},
};

module_platform_driver_probe(sev_guest_driver, sev_guest_probe);

MODULE_AUTHOR("Brijesh Singh <brijesh.singh@amd.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
MODULE_DESCRIPTION("AMD SEV Guest Driver");
MODULE_ALIAS("platform:sev-guest");
