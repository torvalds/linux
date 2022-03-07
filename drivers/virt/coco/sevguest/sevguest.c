// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Encrypted Virtualization Nested Paging (SEV-SNP) guest request interface
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#define pr_fmt(fmt) "SNP: GUEST: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/set_memory.h>
#include <linux/fs.h>
#include <crypto/aead.h>
#include <linux/scatterlist.h>
#include <linux/psp-sev.h>
#include <uapi/linux/sev-guest.h>
#include <uapi/linux/psp-sev.h>

#include <asm/svm.h>
#include <asm/sev.h>

#include "sevguest.h"

#define DEVICE_NAME	"sev-guest"
#define AAD_LEN		48
#define MSG_HDR_VER	1

struct snp_guest_crypto {
	struct crypto_aead *tfm;
	u8 *iv, *authtag;
	int iv_len, a_len;
};

struct snp_guest_dev {
	struct device *dev;
	struct miscdevice misc;

	void *certs_data;
	struct snp_guest_crypto *crypto;
	struct snp_guest_msg *request, *response;
	struct snp_secrets_page_layout *layout;
	struct snp_req_data input;
	u32 *os_area_msg_seqno;
	u8 *vmpck;
};

static u32 vmpck_id;
module_param(vmpck_id, uint, 0444);
MODULE_PARM_DESC(vmpck_id, "The VMPCK ID to use when communicating with the PSP.");

/* Mutex to serialize the shared buffer access and command handling. */
static DEFINE_MUTEX(snp_cmd_mutex);

static bool is_vmpck_empty(struct snp_guest_dev *snp_dev)
{
	char zero_key[VMPCK_KEY_LEN] = {0};

	if (snp_dev->vmpck)
		return !memcmp(snp_dev->vmpck, zero_key, VMPCK_KEY_LEN);

	return true;
}

static void snp_disable_vmpck(struct snp_guest_dev *snp_dev)
{
	memzero_explicit(snp_dev->vmpck, VMPCK_KEY_LEN);
	snp_dev->vmpck = NULL;
}

static inline u64 __snp_get_msg_seqno(struct snp_guest_dev *snp_dev)
{
	u64 count;

	lockdep_assert_held(&snp_cmd_mutex);

	/* Read the current message sequence counter from secrets pages */
	count = *snp_dev->os_area_msg_seqno;

	return count + 1;
}

/* Return a non-zero on success */
static u64 snp_get_msg_seqno(struct snp_guest_dev *snp_dev)
{
	u64 count = __snp_get_msg_seqno(snp_dev);

	/*
	 * The message sequence counter for the SNP guest request is a  64-bit
	 * value but the version 2 of GHCB specification defines a 32-bit storage
	 * for it. If the counter exceeds the 32-bit value then return zero.
	 * The caller should check the return value, but if the caller happens to
	 * not check the value and use it, then the firmware treats zero as an
	 * invalid number and will fail the  message request.
	 */
	if (count >= UINT_MAX) {
		dev_err(snp_dev->dev, "request message sequence counter overflow\n");
		return 0;
	}

	return count;
}

static void snp_inc_msg_seqno(struct snp_guest_dev *snp_dev)
{
	/*
	 * The counter is also incremented by the PSP, so increment it by 2
	 * and save in secrets page.
	 */
	*snp_dev->os_area_msg_seqno += 2;
}

static inline struct snp_guest_dev *to_snp_dev(struct file *file)
{
	struct miscdevice *dev = file->private_data;

	return container_of(dev, struct snp_guest_dev, misc);
}

static struct snp_guest_crypto *init_crypto(struct snp_guest_dev *snp_dev, u8 *key, size_t keylen)
{
	struct snp_guest_crypto *crypto;

	crypto = kzalloc(sizeof(*crypto), GFP_KERNEL_ACCOUNT);
	if (!crypto)
		return NULL;

	crypto->tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(crypto->tfm))
		goto e_free;

	if (crypto_aead_setkey(crypto->tfm, key, keylen))
		goto e_free_crypto;

	crypto->iv_len = crypto_aead_ivsize(crypto->tfm);
	crypto->iv = kmalloc(crypto->iv_len, GFP_KERNEL_ACCOUNT);
	if (!crypto->iv)
		goto e_free_crypto;

	if (crypto_aead_authsize(crypto->tfm) > MAX_AUTHTAG_LEN) {
		if (crypto_aead_setauthsize(crypto->tfm, MAX_AUTHTAG_LEN)) {
			dev_err(snp_dev->dev, "failed to set authsize to %d\n", MAX_AUTHTAG_LEN);
			goto e_free_iv;
		}
	}

	crypto->a_len = crypto_aead_authsize(crypto->tfm);
	crypto->authtag = kmalloc(crypto->a_len, GFP_KERNEL_ACCOUNT);
	if (!crypto->authtag)
		goto e_free_auth;

	return crypto;

e_free_auth:
	kfree(crypto->authtag);
e_free_iv:
	kfree(crypto->iv);
e_free_crypto:
	crypto_free_aead(crypto->tfm);
e_free:
	kfree(crypto);

	return NULL;
}

static void deinit_crypto(struct snp_guest_crypto *crypto)
{
	crypto_free_aead(crypto->tfm);
	kfree(crypto->iv);
	kfree(crypto->authtag);
	kfree(crypto);
}

static int enc_dec_message(struct snp_guest_crypto *crypto, struct snp_guest_msg *msg,
			   u8 *src_buf, u8 *dst_buf, size_t len, bool enc)
{
	struct snp_guest_msg_hdr *hdr = &msg->hdr;
	struct scatterlist src[3], dst[3];
	DECLARE_CRYPTO_WAIT(wait);
	struct aead_request *req;
	int ret;

	req = aead_request_alloc(crypto->tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	/*
	 * AEAD memory operations:
	 * +------ AAD -------+------- DATA -----+---- AUTHTAG----+
	 * |  msg header      |  plaintext       |  hdr->authtag  |
	 * | bytes 30h - 5Fh  |    or            |                |
	 * |                  |   cipher         |                |
	 * +------------------+------------------+----------------+
	 */
	sg_init_table(src, 3);
	sg_set_buf(&src[0], &hdr->algo, AAD_LEN);
	sg_set_buf(&src[1], src_buf, hdr->msg_sz);
	sg_set_buf(&src[2], hdr->authtag, crypto->a_len);

	sg_init_table(dst, 3);
	sg_set_buf(&dst[0], &hdr->algo, AAD_LEN);
	sg_set_buf(&dst[1], dst_buf, hdr->msg_sz);
	sg_set_buf(&dst[2], hdr->authtag, crypto->a_len);

	aead_request_set_ad(req, AAD_LEN);
	aead_request_set_tfm(req, crypto->tfm);
	aead_request_set_callback(req, 0, crypto_req_done, &wait);

	aead_request_set_crypt(req, src, dst, len, crypto->iv);
	ret = crypto_wait_req(enc ? crypto_aead_encrypt(req) : crypto_aead_decrypt(req), &wait);

	aead_request_free(req);
	return ret;
}

static int __enc_payload(struct snp_guest_dev *snp_dev, struct snp_guest_msg *msg,
			 void *plaintext, size_t len)
{
	struct snp_guest_crypto *crypto = snp_dev->crypto;
	struct snp_guest_msg_hdr *hdr = &msg->hdr;

	memset(crypto->iv, 0, crypto->iv_len);
	memcpy(crypto->iv, &hdr->msg_seqno, sizeof(hdr->msg_seqno));

	return enc_dec_message(crypto, msg, plaintext, msg->payload, len, true);
}

static int dec_payload(struct snp_guest_dev *snp_dev, struct snp_guest_msg *msg,
		       void *plaintext, size_t len)
{
	struct snp_guest_crypto *crypto = snp_dev->crypto;
	struct snp_guest_msg_hdr *hdr = &msg->hdr;

	/* Build IV with response buffer sequence number */
	memset(crypto->iv, 0, crypto->iv_len);
	memcpy(crypto->iv, &hdr->msg_seqno, sizeof(hdr->msg_seqno));

	return enc_dec_message(crypto, msg, msg->payload, plaintext, len, false);
}

static int verify_and_dec_payload(struct snp_guest_dev *snp_dev, void *payload, u32 sz)
{
	struct snp_guest_crypto *crypto = snp_dev->crypto;
	struct snp_guest_msg *resp = snp_dev->response;
	struct snp_guest_msg *req = snp_dev->request;
	struct snp_guest_msg_hdr *req_hdr = &req->hdr;
	struct snp_guest_msg_hdr *resp_hdr = &resp->hdr;

	dev_dbg(snp_dev->dev, "response [seqno %lld type %d version %d sz %d]\n",
		resp_hdr->msg_seqno, resp_hdr->msg_type, resp_hdr->msg_version, resp_hdr->msg_sz);

	/* Verify that the sequence counter is incremented by 1 */
	if (unlikely(resp_hdr->msg_seqno != (req_hdr->msg_seqno + 1)))
		return -EBADMSG;

	/* Verify response message type and version number. */
	if (resp_hdr->msg_type != (req_hdr->msg_type + 1) ||
	    resp_hdr->msg_version != req_hdr->msg_version)
		return -EBADMSG;

	/*
	 * If the message size is greater than our buffer length then return
	 * an error.
	 */
	if (unlikely((resp_hdr->msg_sz + crypto->a_len) > sz))
		return -EBADMSG;

	/* Decrypt the payload */
	return dec_payload(snp_dev, resp, payload, resp_hdr->msg_sz + crypto->a_len);
}

static bool enc_payload(struct snp_guest_dev *snp_dev, u64 seqno, int version, u8 type,
			void *payload, size_t sz)
{
	struct snp_guest_msg *req = snp_dev->request;
	struct snp_guest_msg_hdr *hdr = &req->hdr;

	memset(req, 0, sizeof(*req));

	hdr->algo = SNP_AEAD_AES_256_GCM;
	hdr->hdr_version = MSG_HDR_VER;
	hdr->hdr_sz = sizeof(*hdr);
	hdr->msg_type = type;
	hdr->msg_version = version;
	hdr->msg_seqno = seqno;
	hdr->msg_vmpck = vmpck_id;
	hdr->msg_sz = sz;

	/* Verify the sequence number is non-zero */
	if (!hdr->msg_seqno)
		return -ENOSR;

	dev_dbg(snp_dev->dev, "request [seqno %lld type %d version %d sz %d]\n",
		hdr->msg_seqno, hdr->msg_type, hdr->msg_version, hdr->msg_sz);

	return __enc_payload(snp_dev, req, payload, sz);
}

static int handle_guest_request(struct snp_guest_dev *snp_dev, u64 exit_code, int msg_ver,
				u8 type, void *req_buf, size_t req_sz, void *resp_buf,
				u32 resp_sz, __u64 *fw_err)
{
	unsigned long err;
	u64 seqno;
	int rc;

	/* Get message sequence and verify that its a non-zero */
	seqno = snp_get_msg_seqno(snp_dev);
	if (!seqno)
		return -EIO;

	memset(snp_dev->response, 0, sizeof(struct snp_guest_msg));

	/* Encrypt the userspace provided payload */
	rc = enc_payload(snp_dev, seqno, msg_ver, type, req_buf, req_sz);
	if (rc)
		return rc;

	/* Call firmware to process the request */
	rc = snp_issue_guest_request(exit_code, &snp_dev->input, &err);
	if (fw_err)
		*fw_err = err;

	if (rc)
		return rc;

	/*
	 * The verify_and_dec_payload() will fail only if the hypervisor is
	 * actively modifying the message header or corrupting the encrypted payload.
	 * This hints that hypervisor is acting in a bad faith. Disable the VMPCK so that
	 * the key cannot be used for any communication. The key is disabled to ensure
	 * that AES-GCM does not use the same IV while encrypting the request payload.
	 */
	rc = verify_and_dec_payload(snp_dev, resp_buf, resp_sz);
	if (rc) {
		dev_alert(snp_dev->dev,
			  "Detected unexpected decode failure, disabling the vmpck_id %d\n",
			  vmpck_id);
		snp_disable_vmpck(snp_dev);
		return rc;
	}

	/* Increment to new message sequence after payload decryption was successful. */
	snp_inc_msg_seqno(snp_dev);

	return 0;
}

static int get_report(struct snp_guest_dev *snp_dev, struct snp_guest_request_ioctl *arg)
{
	struct snp_guest_crypto *crypto = snp_dev->crypto;
	struct snp_report_resp *resp;
	struct snp_report_req req;
	int rc, resp_len;

	lockdep_assert_held(&snp_cmd_mutex);

	if (!arg->req_data || !arg->resp_data)
		return -EINVAL;

	if (copy_from_user(&req, (void __user *)arg->req_data, sizeof(req)))
		return -EFAULT;

	/*
	 * The intermediate response buffer is used while decrypting the
	 * response payload. Make sure that it has enough space to cover the
	 * authtag.
	 */
	resp_len = sizeof(resp->data) + crypto->a_len;
	resp = kzalloc(resp_len, GFP_KERNEL_ACCOUNT);
	if (!resp)
		return -ENOMEM;

	rc = handle_guest_request(snp_dev, SVM_VMGEXIT_GUEST_REQUEST, arg->msg_version,
				  SNP_MSG_REPORT_REQ, &req, sizeof(req), resp->data,
				  resp_len, &arg->fw_err);
	if (rc)
		goto e_free;

	if (copy_to_user((void __user *)arg->resp_data, resp, sizeof(*resp)))
		rc = -EFAULT;

e_free:
	kfree(resp);
	return rc;
}

static int get_derived_key(struct snp_guest_dev *snp_dev, struct snp_guest_request_ioctl *arg)
{
	struct snp_guest_crypto *crypto = snp_dev->crypto;
	struct snp_derived_key_resp resp = {0};
	struct snp_derived_key_req req;
	int rc, resp_len;
	/* Response data is 64 bytes and max authsize for GCM is 16 bytes. */
	u8 buf[64 + 16];

	lockdep_assert_held(&snp_cmd_mutex);

	if (!arg->req_data || !arg->resp_data)
		return -EINVAL;

	/*
	 * The intermediate response buffer is used while decrypting the
	 * response payload. Make sure that it has enough space to cover the
	 * authtag.
	 */
	resp_len = sizeof(resp.data) + crypto->a_len;
	if (sizeof(buf) < resp_len)
		return -ENOMEM;

	if (copy_from_user(&req, (void __user *)arg->req_data, sizeof(req)))
		return -EFAULT;

	rc = handle_guest_request(snp_dev, SVM_VMGEXIT_GUEST_REQUEST, arg->msg_version,
				  SNP_MSG_KEY_REQ, &req, sizeof(req), buf, resp_len,
				  &arg->fw_err);
	if (rc)
		return rc;

	memcpy(resp.data, buf, sizeof(resp.data));
	if (copy_to_user((void __user *)arg->resp_data, &resp, sizeof(resp)))
		rc = -EFAULT;

	/* The response buffer contains the sensitive data, explicitly clear it. */
	memzero_explicit(buf, sizeof(buf));
	memzero_explicit(&resp, sizeof(resp));
	return rc;
}

static int get_ext_report(struct snp_guest_dev *snp_dev, struct snp_guest_request_ioctl *arg)
{
	struct snp_guest_crypto *crypto = snp_dev->crypto;
	struct snp_ext_report_req req;
	struct snp_report_resp *resp;
	int ret, npages = 0, resp_len;

	lockdep_assert_held(&snp_cmd_mutex);

	if (!arg->req_data || !arg->resp_data)
		return -EINVAL;

	if (copy_from_user(&req, (void __user *)arg->req_data, sizeof(req)))
		return -EFAULT;

	/* userspace does not want certificate data */
	if (!req.certs_len || !req.certs_address)
		goto cmd;

	if (req.certs_len > SEV_FW_BLOB_MAX_SIZE ||
	    !IS_ALIGNED(req.certs_len, PAGE_SIZE))
		return -EINVAL;

	if (!access_ok((const void __user *)req.certs_address, req.certs_len))
		return -EFAULT;

	/*
	 * Initialize the intermediate buffer with all zeros. This buffer
	 * is used in the guest request message to get the certs blob from
	 * the host. If host does not supply any certs in it, then copy
	 * zeros to indicate that certificate data was not provided.
	 */
	memset(snp_dev->certs_data, 0, req.certs_len);
	npages = req.certs_len >> PAGE_SHIFT;
cmd:
	/*
	 * The intermediate response buffer is used while decrypting the
	 * response payload. Make sure that it has enough space to cover the
	 * authtag.
	 */
	resp_len = sizeof(resp->data) + crypto->a_len;
	resp = kzalloc(resp_len, GFP_KERNEL_ACCOUNT);
	if (!resp)
		return -ENOMEM;

	snp_dev->input.data_npages = npages;
	ret = handle_guest_request(snp_dev, SVM_VMGEXIT_EXT_GUEST_REQUEST, arg->msg_version,
				   SNP_MSG_REPORT_REQ, &req.data,
				   sizeof(req.data), resp->data, resp_len, &arg->fw_err);

	/* If certs length is invalid then copy the returned length */
	if (arg->fw_err == SNP_GUEST_REQ_INVALID_LEN) {
		req.certs_len = snp_dev->input.data_npages << PAGE_SHIFT;

		if (copy_to_user((void __user *)arg->req_data, &req, sizeof(req)))
			ret = -EFAULT;
	}

	if (ret)
		goto e_free;

	if (npages &&
	    copy_to_user((void __user *)req.certs_address, snp_dev->certs_data,
			 req.certs_len)) {
		ret = -EFAULT;
		goto e_free;
	}

	if (copy_to_user((void __user *)arg->resp_data, resp, sizeof(*resp)))
		ret = -EFAULT;

e_free:
	kfree(resp);
	return ret;
}

static long snp_guest_ioctl(struct file *file, unsigned int ioctl, unsigned long arg)
{
	struct snp_guest_dev *snp_dev = to_snp_dev(file);
	void __user *argp = (void __user *)arg;
	struct snp_guest_request_ioctl input;
	int ret = -ENOTTY;

	if (copy_from_user(&input, argp, sizeof(input)))
		return -EFAULT;

	input.fw_err = 0xff;

	/* Message version must be non-zero */
	if (!input.msg_version)
		return -EINVAL;

	mutex_lock(&snp_cmd_mutex);

	/* Check if the VMPCK is not empty */
	if (is_vmpck_empty(snp_dev)) {
		dev_err_ratelimited(snp_dev->dev, "VMPCK is disabled\n");
		mutex_unlock(&snp_cmd_mutex);
		return -ENOTTY;
	}

	switch (ioctl) {
	case SNP_GET_REPORT:
		ret = get_report(snp_dev, &input);
		break;
	case SNP_GET_DERIVED_KEY:
		ret = get_derived_key(snp_dev, &input);
		break;
	case SNP_GET_EXT_REPORT:
		ret = get_ext_report(snp_dev, &input);
		break;
	default:
		break;
	}

	mutex_unlock(&snp_cmd_mutex);

	if (input.fw_err && copy_to_user(argp, &input, sizeof(input)))
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

static void *alloc_shared_pages(size_t sz)
{
	unsigned int npages = PAGE_ALIGN(sz) >> PAGE_SHIFT;
	struct page *page;
	int ret;

	page = alloc_pages(GFP_KERNEL_ACCOUNT, get_order(sz));
	if (IS_ERR(page))
		return NULL;

	ret = set_memory_decrypted((unsigned long)page_address(page), npages);
	if (ret) {
		pr_err("failed to mark page shared, ret=%d\n", ret);
		__free_pages(page, get_order(sz));
		return NULL;
	}

	return page_address(page);
}

static const struct file_operations snp_guest_fops = {
	.owner	= THIS_MODULE,
	.unlocked_ioctl = snp_guest_ioctl,
};

static u8 *get_vmpck(int id, struct snp_secrets_page_layout *layout, u32 **seqno)
{
	u8 *key = NULL;

	switch (id) {
	case 0:
		*seqno = &layout->os_area.msg_seqno_0;
		key = layout->vmpck0;
		break;
	case 1:
		*seqno = &layout->os_area.msg_seqno_1;
		key = layout->vmpck1;
		break;
	case 2:
		*seqno = &layout->os_area.msg_seqno_2;
		key = layout->vmpck2;
		break;
	case 3:
		*seqno = &layout->os_area.msg_seqno_3;
		key = layout->vmpck3;
		break;
	default:
		break;
	}

	return key;
}

static int __init snp_guest_probe(struct platform_device *pdev)
{
	struct snp_secrets_page_layout *layout;
	struct snp_guest_platform_data *data;
	struct device *dev = &pdev->dev;
	struct snp_guest_dev *snp_dev;
	struct miscdevice *misc;
	int ret;

	if (!dev->platform_data)
		return -ENODEV;

	data = (struct snp_guest_platform_data *)dev->platform_data;
	layout = (__force void *)ioremap_encrypted(data->secrets_gpa, PAGE_SIZE);
	if (!layout)
		return -ENODEV;

	ret = -ENOMEM;
	snp_dev = devm_kzalloc(&pdev->dev, sizeof(struct snp_guest_dev), GFP_KERNEL);
	if (!snp_dev)
		goto e_unmap;

	ret = -EINVAL;
	snp_dev->vmpck = get_vmpck(vmpck_id, layout, &snp_dev->os_area_msg_seqno);
	if (!snp_dev->vmpck) {
		dev_err(dev, "invalid vmpck id %d\n", vmpck_id);
		goto e_unmap;
	}

	/* Verify that VMPCK is not zero. */
	if (is_vmpck_empty(snp_dev)) {
		dev_err(dev, "vmpck id %d is null\n", vmpck_id);
		goto e_unmap;
	}

	platform_set_drvdata(pdev, snp_dev);
	snp_dev->dev = dev;
	snp_dev->layout = layout;

	/* Allocate the shared page used for the request and response message. */
	snp_dev->request = alloc_shared_pages(sizeof(struct snp_guest_msg));
	if (!snp_dev->request)
		goto e_unmap;

	snp_dev->response = alloc_shared_pages(sizeof(struct snp_guest_msg));
	if (!snp_dev->response)
		goto e_free_request;

	snp_dev->certs_data = alloc_shared_pages(SEV_FW_BLOB_MAX_SIZE);
	if (!snp_dev->certs_data)
		goto e_free_response;

	ret = -EIO;
	snp_dev->crypto = init_crypto(snp_dev, snp_dev->vmpck, VMPCK_KEY_LEN);
	if (!snp_dev->crypto)
		goto e_free_cert_data;

	misc = &snp_dev->misc;
	misc->minor = MISC_DYNAMIC_MINOR;
	misc->name = DEVICE_NAME;
	misc->fops = &snp_guest_fops;

	/* initial the input address for guest request */
	snp_dev->input.req_gpa = __pa(snp_dev->request);
	snp_dev->input.resp_gpa = __pa(snp_dev->response);
	snp_dev->input.data_gpa = __pa(snp_dev->certs_data);

	ret =  misc_register(misc);
	if (ret)
		goto e_free_cert_data;

	dev_info(dev, "Initialized SNP guest driver (using vmpck_id %d)\n", vmpck_id);
	return 0;

e_free_cert_data:
	free_shared_pages(snp_dev->certs_data, SEV_FW_BLOB_MAX_SIZE);
e_free_response:
	free_shared_pages(snp_dev->response, sizeof(struct snp_guest_msg));
e_free_request:
	free_shared_pages(snp_dev->request, sizeof(struct snp_guest_msg));
e_unmap:
	iounmap(layout);
	return ret;
}

static int __exit snp_guest_remove(struct platform_device *pdev)
{
	struct snp_guest_dev *snp_dev = platform_get_drvdata(pdev);

	free_shared_pages(snp_dev->certs_data, SEV_FW_BLOB_MAX_SIZE);
	free_shared_pages(snp_dev->response, sizeof(struct snp_guest_msg));
	free_shared_pages(snp_dev->request, sizeof(struct snp_guest_msg));
	deinit_crypto(snp_dev->crypto);
	misc_deregister(&snp_dev->misc);

	return 0;
}

static struct platform_driver snp_guest_driver = {
	.remove		= __exit_p(snp_guest_remove),
	.driver		= {
		.name = "snp-guest",
	},
};

module_platform_driver_probe(snp_guest_driver, snp_guest_probe);

MODULE_AUTHOR("Brijesh Singh <brijesh.singh@amd.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
MODULE_DESCRIPTION("AMD SNP Guest Driver");
