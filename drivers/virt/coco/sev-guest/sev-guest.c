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
#include <uapi/linux/sev-guest.h>
#include <uapi/linux/psp-sev.h>

#include <asm/svm.h>
#include <asm/sev.h>

#define DEVICE_NAME	"sev-guest"

#define SNP_REQ_MAX_RETRY_DURATION	(60*HZ)
#define SNP_REQ_RETRY_DELAY		(2*HZ)

#define SVSM_MAX_RETRIES		3

struct snp_guest_dev {
	struct device *dev;
	struct miscdevice misc;

	struct snp_msg_desc *msg_desc;

	union {
		struct snp_report_req report;
		struct snp_derived_key_req derived_key;
		struct snp_ext_report_req ext_report;
	} req;
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

/* Mutex to serialize the shared buffer access and command handling. */
static DEFINE_MUTEX(snp_cmd_mutex);

static bool is_vmpck_empty(struct snp_msg_desc *mdesc)
{
	char zero_key[VMPCK_KEY_LEN] = {0};

	if (mdesc->vmpck)
		return !memcmp(mdesc->vmpck, zero_key, VMPCK_KEY_LEN);

	return true;
}

/*
 * If an error is received from the host or AMD Secure Processor (ASP) there
 * are two options. Either retry the exact same encrypted request or discontinue
 * using the VMPCK.
 *
 * This is because in the current encryption scheme GHCB v2 uses AES-GCM to
 * encrypt the requests. The IV for this scheme is the sequence number. GCM
 * cannot tolerate IV reuse.
 *
 * The ASP FW v1.51 only increments the sequence numbers on a successful
 * guest<->ASP back and forth and only accepts messages at its exact sequence
 * number.
 *
 * So if the sequence number were to be reused the encryption scheme is
 * vulnerable. If the sequence number were incremented for a fresh IV the ASP
 * will reject the request.
 */
static void snp_disable_vmpck(struct snp_msg_desc *mdesc)
{
	pr_alert("Disabling VMPCK%d communication key to prevent IV reuse.\n",
		  vmpck_id);
	memzero_explicit(mdesc->vmpck, VMPCK_KEY_LEN);
	mdesc->vmpck = NULL;
}

static inline u64 __snp_get_msg_seqno(struct snp_msg_desc *mdesc)
{
	u64 count;

	lockdep_assert_held(&snp_cmd_mutex);

	/* Read the current message sequence counter from secrets pages */
	count = *mdesc->os_area_msg_seqno;

	return count + 1;
}

/* Return a non-zero on success */
static u64 snp_get_msg_seqno(struct snp_msg_desc *mdesc)
{
	u64 count = __snp_get_msg_seqno(mdesc);

	/*
	 * The message sequence counter for the SNP guest request is a  64-bit
	 * value but the version 2 of GHCB specification defines a 32-bit storage
	 * for it. If the counter exceeds the 32-bit value then return zero.
	 * The caller should check the return value, but if the caller happens to
	 * not check the value and use it, then the firmware treats zero as an
	 * invalid number and will fail the  message request.
	 */
	if (count >= UINT_MAX) {
		pr_err("request message sequence counter overflow\n");
		return 0;
	}

	return count;
}

static void snp_inc_msg_seqno(struct snp_msg_desc *mdesc)
{
	/*
	 * The counter is also incremented by the PSP, so increment it by 2
	 * and save in secrets page.
	 */
	*mdesc->os_area_msg_seqno += 2;
}

static inline struct snp_guest_dev *to_snp_dev(struct file *file)
{
	struct miscdevice *dev = file->private_data;

	return container_of(dev, struct snp_guest_dev, misc);
}

static struct aesgcm_ctx *snp_init_crypto(u8 *key, size_t keylen)
{
	struct aesgcm_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL_ACCOUNT);
	if (!ctx)
		return NULL;

	if (aesgcm_expandkey(ctx, key, keylen, AUTHTAG_LEN)) {
		pr_err("Crypto context initialization failed\n");
		kfree(ctx);
		return NULL;
	}

	return ctx;
}

static int verify_and_dec_payload(struct snp_msg_desc *mdesc, struct snp_guest_req *req)
{
	struct snp_guest_msg *resp_msg = &mdesc->secret_response;
	struct snp_guest_msg *req_msg = &mdesc->secret_request;
	struct snp_guest_msg_hdr *req_msg_hdr = &req_msg->hdr;
	struct snp_guest_msg_hdr *resp_msg_hdr = &resp_msg->hdr;
	struct aesgcm_ctx *ctx = mdesc->ctx;
	u8 iv[GCM_AES_IV_SIZE] = {};

	pr_debug("response [seqno %lld type %d version %d sz %d]\n",
		 resp_msg_hdr->msg_seqno, resp_msg_hdr->msg_type, resp_msg_hdr->msg_version,
		 resp_msg_hdr->msg_sz);

	/* Copy response from shared memory to encrypted memory. */
	memcpy(resp_msg, mdesc->response, sizeof(*resp_msg));

	/* Verify that the sequence counter is incremented by 1 */
	if (unlikely(resp_msg_hdr->msg_seqno != (req_msg_hdr->msg_seqno + 1)))
		return -EBADMSG;

	/* Verify response message type and version number. */
	if (resp_msg_hdr->msg_type != (req_msg_hdr->msg_type + 1) ||
	    resp_msg_hdr->msg_version != req_msg_hdr->msg_version)
		return -EBADMSG;

	/*
	 * If the message size is greater than our buffer length then return
	 * an error.
	 */
	if (unlikely((resp_msg_hdr->msg_sz + ctx->authsize) > req->resp_sz))
		return -EBADMSG;

	/* Decrypt the payload */
	memcpy(iv, &resp_msg_hdr->msg_seqno, min(sizeof(iv), sizeof(resp_msg_hdr->msg_seqno)));
	if (!aesgcm_decrypt(ctx, req->resp_buf, resp_msg->payload, resp_msg_hdr->msg_sz,
			    &resp_msg_hdr->algo, AAD_LEN, iv, resp_msg_hdr->authtag))
		return -EBADMSG;

	return 0;
}

static int enc_payload(struct snp_msg_desc *mdesc, u64 seqno, struct snp_guest_req *req)
{
	struct snp_guest_msg *msg = &mdesc->secret_request;
	struct snp_guest_msg_hdr *hdr = &msg->hdr;
	struct aesgcm_ctx *ctx = mdesc->ctx;
	u8 iv[GCM_AES_IV_SIZE] = {};

	memset(msg, 0, sizeof(*msg));

	hdr->algo = SNP_AEAD_AES_256_GCM;
	hdr->hdr_version = MSG_HDR_VER;
	hdr->hdr_sz = sizeof(*hdr);
	hdr->msg_type = req->msg_type;
	hdr->msg_version = req->msg_version;
	hdr->msg_seqno = seqno;
	hdr->msg_vmpck = req->vmpck_id;
	hdr->msg_sz = req->req_sz;

	/* Verify the sequence number is non-zero */
	if (!hdr->msg_seqno)
		return -ENOSR;

	pr_debug("request [seqno %lld type %d version %d sz %d]\n",
		 hdr->msg_seqno, hdr->msg_type, hdr->msg_version, hdr->msg_sz);

	if (WARN_ON((req->req_sz + ctx->authsize) > sizeof(msg->payload)))
		return -EBADMSG;

	memcpy(iv, &hdr->msg_seqno, min(sizeof(iv), sizeof(hdr->msg_seqno)));
	aesgcm_encrypt(ctx, msg->payload, req->req_buf, req->req_sz, &hdr->algo,
		       AAD_LEN, iv, hdr->authtag);

	return 0;
}

static int __handle_guest_request(struct snp_msg_desc *mdesc, struct snp_guest_req *req,
				  struct snp_guest_request_ioctl *rio)
{
	unsigned long req_start = jiffies;
	unsigned int override_npages = 0;
	u64 override_err = 0;
	int rc;

retry_request:
	/*
	 * Call firmware to process the request. In this function the encrypted
	 * message enters shared memory with the host. So after this call the
	 * sequence number must be incremented or the VMPCK must be deleted to
	 * prevent reuse of the IV.
	 */
	rc = snp_issue_guest_request(req, &mdesc->input, rio);
	switch (rc) {
	case -ENOSPC:
		/*
		 * If the extended guest request fails due to having too
		 * small of a certificate data buffer, retry the same
		 * guest request without the extended data request in
		 * order to increment the sequence number and thus avoid
		 * IV reuse.
		 */
		override_npages = mdesc->input.data_npages;
		req->exit_code	= SVM_VMGEXIT_GUEST_REQUEST;

		/*
		 * Override the error to inform callers the given extended
		 * request buffer size was too small and give the caller the
		 * required buffer size.
		 */
		override_err = SNP_GUEST_VMM_ERR(SNP_GUEST_VMM_ERR_INVALID_LEN);

		/*
		 * If this call to the firmware succeeds, the sequence number can
		 * be incremented allowing for continued use of the VMPCK. If
		 * there is an error reflected in the return value, this value
		 * is checked further down and the result will be the deletion
		 * of the VMPCK and the error code being propagated back to the
		 * user as an ioctl() return code.
		 */
		goto retry_request;

	/*
	 * The host may return SNP_GUEST_VMM_ERR_BUSY if the request has been
	 * throttled. Retry in the driver to avoid returning and reusing the
	 * message sequence number on a different message.
	 */
	case -EAGAIN:
		if (jiffies - req_start > SNP_REQ_MAX_RETRY_DURATION) {
			rc = -ETIMEDOUT;
			break;
		}
		schedule_timeout_killable(SNP_REQ_RETRY_DELAY);
		goto retry_request;
	}

	/*
	 * Increment the message sequence number. There is no harm in doing
	 * this now because decryption uses the value stored in the response
	 * structure and any failure will wipe the VMPCK, preventing further
	 * use anyway.
	 */
	snp_inc_msg_seqno(mdesc);

	if (override_err) {
		rio->exitinfo2 = override_err;

		/*
		 * If an extended guest request was issued and the supplied certificate
		 * buffer was not large enough, a standard guest request was issued to
		 * prevent IV reuse. If the standard request was successful, return -EIO
		 * back to the caller as would have originally been returned.
		 */
		if (!rc && override_err == SNP_GUEST_VMM_ERR(SNP_GUEST_VMM_ERR_INVALID_LEN))
			rc = -EIO;
	}

	if (override_npages)
		mdesc->input.data_npages = override_npages;

	return rc;
}

static int snp_send_guest_request(struct snp_msg_desc *mdesc, struct snp_guest_req *req,
				  struct snp_guest_request_ioctl *rio)
{
	u64 seqno;
	int rc;

	guard(mutex)(&snp_cmd_mutex);

	/* Check if the VMPCK is not empty */
	if (is_vmpck_empty(mdesc)) {
		pr_err_ratelimited("VMPCK is disabled\n");
		return -ENOTTY;
	}

	/* Get message sequence and verify that its a non-zero */
	seqno = snp_get_msg_seqno(mdesc);
	if (!seqno)
		return -EIO;

	/* Clear shared memory's response for the host to populate. */
	memset(mdesc->response, 0, sizeof(struct snp_guest_msg));

	/* Encrypt the userspace provided payload in mdesc->secret_request. */
	rc = enc_payload(mdesc, seqno, req);
	if (rc)
		return rc;

	/*
	 * Write the fully encrypted request to the shared unencrypted
	 * request page.
	 */
	memcpy(mdesc->request, &mdesc->secret_request,
	       sizeof(mdesc->secret_request));

	rc = __handle_guest_request(mdesc, req, rio);
	if (rc) {
		if (rc == -EIO &&
		    rio->exitinfo2 == SNP_GUEST_VMM_ERR(SNP_GUEST_VMM_ERR_INVALID_LEN))
			return rc;

		pr_alert("Detected error from ASP request. rc: %d, exitinfo2: 0x%llx\n",
			 rc, rio->exitinfo2);

		snp_disable_vmpck(mdesc);
		return rc;
	}

	rc = verify_and_dec_payload(mdesc, req);
	if (rc) {
		pr_alert("Detected unexpected decode failure from ASP. rc: %d\n", rc);
		snp_disable_vmpck(mdesc);
		return rc;
	}

	return 0;
}

struct snp_req_resp {
	sockptr_t req_data;
	sockptr_t resp_data;
};

static int get_report(struct snp_guest_dev *snp_dev, struct snp_guest_request_ioctl *arg)
{
	struct snp_report_req *report_req = &snp_dev->req.report;
	struct snp_msg_desc *mdesc = snp_dev->msg_desc;
	struct snp_report_resp *report_resp;
	struct snp_guest_req req = {};
	int rc, resp_len;

	if (!arg->req_data || !arg->resp_data)
		return -EINVAL;

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
	req.vmpck_id = vmpck_id;
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
	struct snp_derived_key_req *derived_key_req = &snp_dev->req.derived_key;
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

	if (copy_from_user(derived_key_req, (void __user *)arg->req_data,
			   sizeof(*derived_key_req)))
		return -EFAULT;

	req.msg_version = arg->msg_version;
	req.msg_type = SNP_MSG_KEY_REQ;
	req.vmpck_id = vmpck_id;
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
	struct snp_ext_report_req *report_req = &snp_dev->req.ext_report;
	struct snp_msg_desc *mdesc = snp_dev->msg_desc;
	struct snp_report_resp *report_resp;
	struct snp_guest_req req = {};
	int ret, npages = 0, resp_len;
	sockptr_t certs_address;

	if (sockptr_is_null(io->req_data) || sockptr_is_null(io->resp_data))
		return -EINVAL;

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
	memset(mdesc->certs_data, 0, report_req->certs_len);
	npages = report_req->certs_len >> PAGE_SHIFT;
cmd:
	/*
	 * The intermediate response buffer is used while decrypting the
	 * response payload. Make sure that it has enough space to cover the
	 * authtag.
	 */
	resp_len = sizeof(report_resp->data) + mdesc->ctx->authsize;
	report_resp = kzalloc(resp_len, GFP_KERNEL_ACCOUNT);
	if (!report_resp)
		return -ENOMEM;

	mdesc->input.data_npages = npages;

	req.msg_version = arg->msg_version;
	req.msg_type = SNP_MSG_REPORT_REQ;
	req.vmpck_id = vmpck_id;
	req.req_buf = &report_req->data;
	req.req_sz = sizeof(report_req->data);
	req.resp_buf = report_resp->data;
	req.resp_sz = resp_len;
	req.exit_code = SVM_VMGEXIT_EXT_GUEST_REQUEST;

	ret = snp_send_guest_request(mdesc, &req, arg);

	/* If certs length is invalid then copy the returned length */
	if (arg->vmm_error == SNP_GUEST_VMM_ERR_INVALID_LEN) {
		report_req->certs_len = mdesc->input.data_npages << PAGE_SHIFT;

		if (copy_to_sockptr(io->req_data, report_req, sizeof(*report_req)))
			ret = -EFAULT;
	}

	if (ret)
		goto e_free;

	if (npages && copy_to_sockptr(certs_address, mdesc->certs_data, report_req->certs_len)) {
		ret = -EFAULT;
		goto e_free;
	}

	if (copy_to_sockptr(io->resp_data, report_resp, sizeof(*report_resp)))
		ret = -EFAULT;

e_free:
	kfree(report_resp);
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

static void free_shared_pages(void *buf, size_t sz)
{
	unsigned int npages = PAGE_ALIGN(sz) >> PAGE_SHIFT;
	int ret;

	if (!buf)
		return;

	ret = set_memory_encrypted((unsigned long)buf, npages);
	if (ret) {
		WARN_ONCE(ret, "failed to restore encryption mask (leak it)\n");
		return;
	}

	__free_pages(virt_to_page(buf), get_order(sz));
}

static void *alloc_shared_pages(struct device *dev, size_t sz)
{
	unsigned int npages = PAGE_ALIGN(sz) >> PAGE_SHIFT;
	struct page *page;
	int ret;

	page = alloc_pages(GFP_KERNEL_ACCOUNT, get_order(sz));
	if (!page)
		return NULL;

	ret = set_memory_decrypted((unsigned long)page_address(page), npages);
	if (ret) {
		dev_err(dev, "failed to mark page shared, ret=%d\n", ret);
		__free_pages(page, get_order(sz));
		return NULL;
	}

	return page_address(page);
}

static const struct file_operations snp_guest_fops = {
	.owner	= THIS_MODULE,
	.unlocked_ioctl = snp_guest_ioctl,
};

static u8 *get_vmpck(int id, struct snp_secrets_page *secrets, u32 **seqno)
{
	u8 *key = NULL;

	switch (id) {
	case 0:
		*seqno = &secrets->os_area.msg_seqno_0;
		key = secrets->vmpck0;
		break;
	case 1:
		*seqno = &secrets->os_area.msg_seqno_1;
		key = secrets->vmpck1;
		break;
	case 2:
		*seqno = &secrets->os_area.msg_seqno_2;
		key = secrets->vmpck2;
		break;
	case 3:
		*seqno = &secrets->os_area.msg_seqno_3;
		key = secrets->vmpck3;
		break;
	default:
		break;
	}

	return key;
}

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
	struct sev_guest_platform_data *data;
	struct snp_secrets_page *secrets;
	struct device *dev = &pdev->dev;
	struct snp_guest_dev *snp_dev;
	struct snp_msg_desc *mdesc;
	struct miscdevice *misc;
	void __iomem *mapping;
	int ret;

	BUILD_BUG_ON(sizeof(struct snp_guest_msg) > PAGE_SIZE);

	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return -ENODEV;

	if (!dev->platform_data)
		return -ENODEV;

	data = (struct sev_guest_platform_data *)dev->platform_data;
	mapping = ioremap_encrypted(data->secrets_gpa, PAGE_SIZE);
	if (!mapping)
		return -ENODEV;

	secrets = (__force void *)mapping;

	ret = -ENOMEM;
	snp_dev = devm_kzalloc(&pdev->dev, sizeof(struct snp_guest_dev), GFP_KERNEL);
	if (!snp_dev)
		goto e_unmap;

	mdesc = devm_kzalloc(&pdev->dev, sizeof(struct snp_msg_desc), GFP_KERNEL);
	if (!mdesc)
		goto e_unmap;

	/* Adjust the default VMPCK key based on the executing VMPL level */
	if (vmpck_id == -1)
		vmpck_id = snp_vmpl;

	ret = -EINVAL;
	mdesc->vmpck = get_vmpck(vmpck_id, secrets, &mdesc->os_area_msg_seqno);
	if (!mdesc->vmpck) {
		dev_err(dev, "Invalid VMPCK%d communication key\n", vmpck_id);
		goto e_unmap;
	}

	/* Verify that VMPCK is not zero. */
	if (is_vmpck_empty(mdesc)) {
		dev_err(dev, "Empty VMPCK%d communication key\n", vmpck_id);
		goto e_unmap;
	}

	platform_set_drvdata(pdev, snp_dev);
	snp_dev->dev = dev;
	mdesc->secrets = secrets;

	/* Allocate the shared page used for the request and response message. */
	mdesc->request = alloc_shared_pages(dev, sizeof(struct snp_guest_msg));
	if (!mdesc->request)
		goto e_unmap;

	mdesc->response = alloc_shared_pages(dev, sizeof(struct snp_guest_msg));
	if (!mdesc->response)
		goto e_free_request;

	mdesc->certs_data = alloc_shared_pages(dev, SEV_FW_BLOB_MAX_SIZE);
	if (!mdesc->certs_data)
		goto e_free_response;

	ret = -EIO;
	mdesc->ctx = snp_init_crypto(mdesc->vmpck, VMPCK_KEY_LEN);
	if (!mdesc->ctx)
		goto e_free_cert_data;

	misc = &snp_dev->misc;
	misc->minor = MISC_DYNAMIC_MINOR;
	misc->name = DEVICE_NAME;
	misc->fops = &snp_guest_fops;

	/* Initialize the input addresses for guest request */
	mdesc->input.req_gpa = __pa(mdesc->request);
	mdesc->input.resp_gpa = __pa(mdesc->response);
	mdesc->input.data_gpa = __pa(mdesc->certs_data);

	/* Set the privlevel_floor attribute based on the vmpck_id */
	sev_tsm_ops.privlevel_floor = vmpck_id;

	ret = tsm_register(&sev_tsm_ops, snp_dev);
	if (ret)
		goto e_free_cert_data;

	ret = devm_add_action_or_reset(&pdev->dev, unregister_sev_tsm, NULL);
	if (ret)
		goto e_free_cert_data;

	ret =  misc_register(misc);
	if (ret)
		goto e_free_ctx;

	snp_dev->msg_desc = mdesc;
	dev_info(dev, "Initialized SEV guest driver (using VMPCK%d communication key)\n", vmpck_id);
	return 0;

e_free_ctx:
	kfree(mdesc->ctx);
e_free_cert_data:
	free_shared_pages(mdesc->certs_data, SEV_FW_BLOB_MAX_SIZE);
e_free_response:
	free_shared_pages(mdesc->response, sizeof(struct snp_guest_msg));
e_free_request:
	free_shared_pages(mdesc->request, sizeof(struct snp_guest_msg));
e_unmap:
	iounmap(mapping);
	return ret;
}

static void __exit sev_guest_remove(struct platform_device *pdev)
{
	struct snp_guest_dev *snp_dev = platform_get_drvdata(pdev);
	struct snp_msg_desc *mdesc = snp_dev->msg_desc;

	free_shared_pages(mdesc->certs_data, SEV_FW_BLOB_MAX_SIZE);
	free_shared_pages(mdesc->response, sizeof(struct snp_guest_msg));
	free_shared_pages(mdesc->request, sizeof(struct snp_guest_msg));
	kfree(mdesc->ctx);
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
	.remove_new	= __exit_p(sev_guest_remove),
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
