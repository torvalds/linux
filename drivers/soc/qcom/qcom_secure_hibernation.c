// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/scatterlist.h>
#include <crypto/aead.h>
#include <soc/qcom/qcom_hibernation.h>
#include <../../kernel/power/power.h>
#include <linux/qseecom_kernel.h>
#include <trace/hooks/bl_hib.h>
#include <linux/reboot.h>

#define AUTH_SIZE		16
#define QSEECOM_ALIGN_SIZE      0x40
#define QSEECOM_ALIGN_MASK      (QSEECOM_ALIGN_SIZE - 1)
#define QSEECOM_ALIGN(x)        \
	((x + QSEECOM_ALIGN_MASK) & (~QSEECOM_ALIGN_MASK))


struct s4app_time {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
};

struct wrap_req {
	struct s4app_time save_time;
};

struct wrap_rsp {
	uint8_t wrapped_key_buffer[WRAPPED_KEY_SIZE];
	uint32_t wrapped_key_size;
	uint8_t key_buffer[PAYLOAD_KEY_SIZE];
	uint32_t key_size;
};

struct unwrap_req {
	uint8_t wrapped_key_buffer[WRAPPED_KEY_SIZE];
	uint32_t wrapped_key_size;
	struct s4app_time curr_time;
};

struct unwrap_rsp {
	uint8_t key_buffer[PAYLOAD_KEY_SIZE];
	uint32_t key_size;
};

enum cmd_id {
	WRAP_KEY_CMD = 0,
	UNWRAP_KEY_CMD = 1,
};

struct cmd_req {
	enum cmd_id cmd;
	union {
		struct wrap_req wrapkey_req;
		struct unwrap_req unwrapkey_req;
	};
};

struct cmd_rsp {
	enum cmd_id cmd;
	union {
		struct wrap_rsp wrapkey_rsp;
		struct unwrap_rsp unwrapkey_rsp;
	};
	uint32_t status;
};

static struct qcom_crypto_params *params;
static struct crypto_aead *tfm;
static struct aead_request *req;
static u8 iv_size;
static u8 key[AES256_KEY_SIZE];
static struct qseecom_handle *app_handle;
static int first_encrypt;
static void *temp_out_buf;
static int pos;
static uint8_t *authslot_start;
static unsigned short root_swap_dev;
static struct work_struct save_params_work;
static struct completion write_done;
static unsigned char iv[IV_SIZE];
static uint8_t *compressed_blk_array;
static int blk_array_pos;
static unsigned long nr_pages;
static void *auth_slot;

static void init_sg(struct scatterlist *sg, void *data, unsigned int size)
{
	sg_init_table(sg, 2);
	sg_set_buf(&sg[0], params->aad, sizeof(params->aad));
	sg_set_buf(&sg[1], data, size);
}

static void save_auth(uint8_t *out_buf)
{
	memcpy(authslot_start + (pos * AUTH_SIZE), out_buf + PAGE_SIZE,
		AUTH_SIZE);
	pos++;
}

static void skip_swap_map_write(void *data, bool *skip)
{
	*skip = true;
}

static void increment_iv(unsigned char *iv, u8 size)
{
	int i;
	u16 num, carry = 1;

	i = size - 1;
	do {
		num = (u8)iv[i];
		num += carry;
		iv[i] = num & 0xFF;
		carry = (num > 0xFF) ? 1 : 0;
		i--;
	} while (i >= 0 && carry != 0);
}

static void encrypt_page(void *data, void *buf)
{
	struct scatterlist sg_in[2], sg_out[2];
	struct crypto_wait wait;
	int ret = 0;

	/* Allocate a request object */
	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto err_aead;
	}

	crypto_init_wait(&wait);
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				crypto_req_done, &wait);

	ret = crypto_aead_setauthsize(tfm, AUTH_SIZE);
	iv_size = crypto_aead_ivsize(tfm);
	if (iv_size && first_encrypt) {
		get_random_bytes(params->iv, iv_size);
		memcpy((void *)iv, params->iv, IV_SIZE);
	}

	ret = crypto_aead_setkey(tfm, key, AES256_KEY_SIZE);
	if (ret) {
		pr_err("Error setting key: %d\n", ret);
		goto out;
	}
	crypto_aead_clear_flags(tfm, ~0);

	memset(temp_out_buf, 0, 2 * PAGE_SIZE);
	init_sg(sg_in, buf, PAGE_SIZE);
	init_sg(sg_out, temp_out_buf, PAGE_SIZE + AUTH_SIZE);
	aead_request_set_ad(req, sizeof(params->aad));

	increment_iv(iv, IV_SIZE);
	aead_request_set_crypt(req, sg_in, sg_out, PAGE_SIZE, iv);
	crypto_aead_encrypt(req);
	ret = crypto_wait_req(ret, &wait);
	if (ret) {
		pr_err("Error encrypting data: %d\n", ret);
		goto out;
	}

	memcpy(buf, temp_out_buf, PAGE_SIZE);
	save_auth(temp_out_buf);

	if (first_encrypt)
		first_encrypt = 0;

out:
	aead_request_free(req);
	return;
err_aead:
	free_pages((unsigned long)temp_out_buf, 1);
}

static int read_authpage_count(void)
{
	unsigned long total_auth_size;
	unsigned int num_auth_pages;

	total_auth_size = params->authslot_count * AUTH_SIZE;
	num_auth_pages = total_auth_size / PAGE_SIZE;
	if (total_auth_size % PAGE_SIZE)
		num_auth_pages += 1;

	return num_auth_pages;
}

static void hib_init_batch(struct hib_bio_batch *hb)
{
	atomic_set(&hb->count, 0);
	init_waitqueue_head(&hb->wait);
	hb->error = BLK_STS_OK;
	blk_start_plug(&hb->plug);
}

static void hib_finish_batch(struct hib_bio_batch *hb)
{
	blk_finish_plug(&hb->plug);
}

static void hib_end_io(struct bio *bio)
{
	struct hib_bio_batch *hb = bio->bi_private;
	struct page *page = bio_first_page_all(bio);

	if (bio->bi_status) {
		pr_alert("Read-error on swap-device (%u:%u:%lu)\n",
			MAJOR(bio_dev(bio)), MINOR(bio_dev(bio)),
			(unsigned long long)bio->bi_iter.bi_sector);
	}

	if (bio_data_dir(bio) == WRITE)
		put_page(page);

	if (bio->bi_status && !hb->error)
		hb->error = bio->bi_status;
	if (atomic_dec_and_test(&hb->count))
		wake_up(&hb->wait);

	bio_put(bio);
}

static int hib_submit_io(blk_opf_t opf, int op_flags, pgoff_t page_off, void *addr,
				struct hib_bio_batch *hb)
{
	struct page *page = virt_to_page(addr);
	struct bio *bio;
	int error = 0;

	bio = bio_alloc(hiber_bdev, 1, opf, GFP_NOIO | __GFP_HIGH);
	bio->bi_iter.bi_sector = page_off * (PAGE_SIZE >> 9);
	bio_set_dev(bio, hiber_bdev);
	bio_set_op_attrs(bio, REQ_OP_WRITE, op_flags);

	if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
		pr_err("Adding page to bio failed at %llu\n",
			(unsigned long long)bio->bi_iter.bi_sector);
		bio_put(bio);
		return -EFAULT;
	}

	if (hb) {
		bio->bi_end_io = hib_end_io;
		bio->bi_private = hb;
		atomic_inc(&hb->count);
		submit_bio(bio);
	} else {
		error = submit_bio_wait(bio);
		bio_put(bio);
	}

	return error;
}

static int hib_wait_io(struct hib_bio_batch *hb)
{
	/*
	 * We are relying on the behavior of blk_plug that a thread with
	 * a plug will flush the plug list before sleeping.
	 */
	wait_event(hb->wait, atomic_read(&hb->count) == 0);
	return blk_status_to_errno(hb->error);
}

static int write_page(void *buf, sector_t offset, struct hib_bio_batch *hb)
{
	void *src;
	int ret;

	if (!offset)
		return -ENOSPC;

	if (hb) {
		src = (void *)__get_free_page(GFP_NOIO | __GFP_NOWARN |
						__GFP_NORETRY);
		if (src) {
			copy_page(src, buf);
		} else {
			ret = hib_wait_io(hb); /* Free pages */
			if (ret)
				return ret;
			src = (void *)__get_free_page(GFP_NOIO | __GFP_NOWARN |
							__GFP_NORETRY);
			if (src) {
				copy_page(src, buf);
			} else {
				WARN_ON_ONCE(1);
				hb = NULL;/* Go synchronous */
				src = buf;
			}
		}
	} else {
		src = buf;
	}
	return hib_submit_io(REQ_OP_WRITE, REQ_SYNC, offset, src, hb);
}

/*
 * Number of pages compressed at one time. This is inline with UNC_PAGES
 * in kernel/power/swap.c.
 */
#define UNCMP_PAGES   32

static uint32_t get_size_of_compression_block_array(unsigned long pages)
{
	/*
	 * Get the max index based on total no. of pages. Current compression
	 * algorithm compresses each UNC_PAGES pages to x pages. Use this logic to
	 * get the max index.
	 */
	uint32_t max_index = DIV_ROUND_UP(pages, UNCMP_PAGES);

	uint32_t size = ALIGN((max_index * sizeof(*compressed_blk_array)), PAGE_SIZE);

	return size;
}

static void save_auth_and_params_to_disk(struct work_struct *work)
{
	int cur_slot;
	void *authpage;
	int params_slot;
	int authslot_count = 0;
	int authpage_count = read_authpage_count();
	struct hib_bio_batch hb;
	int err2, i = 0;

	hib_init_batch(&hb);

	/*
	 * Allocate a page to save the encryption params
	 */
	params_slot = alloc_swapdev_block(root_swap_dev);

	if (auth_slot) {
		*(int *)auth_slot = params_slot + 1;

		/* Currently bootloader code does the following to
		 * calculate the authentication slot index.
		 * authslot = NrMetaPages + NrCopyPages + NrSwapMapPages +
		 * HDR_SWP_INFO_NUM_PAGES;
		 *
		 * However, with compression enabled, we cannot apply the
		 * above logic to get the authentication slot. So this
		 * data should be provided to the BL for decryption to work.
		 *
		 * In the current implementation, BL doesn't make use of
		 * the swap_map_pages for restoring the hibernation image. So these pages
		 * could be used for other purposes. Use this to store the
		 * authentication slot number. This data will be stored at index as
		 * that of the first swap_map_page.
		 */
		write_page(auth_slot, 1, &hb);
	}

	authpage = authslot_start;
	while (authslot_count < authpage_count) {
		cur_slot = alloc_swapdev_block(root_swap_dev);
		write_page(authpage, cur_slot, &hb);
		authpage = (unsigned char *)authpage + PAGE_SIZE;
		authslot_count++;
	}
	params->authslot_count = authslot_count;
	write_page(params, params_slot, &hb);

	/*
	 * Write the array holding the compressed block count to disk
	 */
	if (compressed_blk_array) {
		uint32_t size = get_size_of_compression_block_array(nr_pages);

		for (i = 0; i < size / PAGE_SIZE; i++) {
			cur_slot = alloc_swapdev_block(root_swap_dev);
			write_page(compressed_blk_array + (i * PAGE_SIZE), cur_slot, &hb);
		}
	}

	err2 = hib_wait_io(&hb);
	hib_finish_batch(&hb);
	complete_all(&write_done);
}

static void save_params_to_disk(void *data, unsigned short root_swap)
{
	root_swap_dev = root_swap;
	queue_work(system_wq, &save_params_work);
}

static int poweroff_notifier(struct notifier_block *nb,
				unsigned long event, void *unused)
{
	switch (event) {

	case (SYS_POWER_OFF):
		if (authslot_start)
			wait_for_completion(&write_done);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block poweroff_nb = {
	.notifier_call = poweroff_notifier,
};

static int get_key_from_ta(void)
{
	int ret;
	int req_len, rsp_len;

	struct cmd_req *req = (struct cmd_req *)app_handle->sbuf;
	struct cmd_rsp *rsp = NULL;

	req_len = sizeof(struct cmd_req);
	if (req_len & QSEECOM_ALIGN_MASK)
		req_len = QSEECOM_ALIGN(req_len);

	rsp = (struct cmd_rsp *)(app_handle->sbuf + req_len);
	rsp_len = sizeof(struct cmd_rsp);
	if (rsp_len & QSEECOM_ALIGN_MASK)
		rsp_len = QSEECOM_ALIGN(rsp_len);

	memset(req, 0, req_len);
	memset(rsp, 0, rsp_len);

	req->cmd = WRAP_KEY_CMD;
	req->wrapkey_req.save_time.hour = 4;
	rsp->wrapkey_rsp.wrapped_key_size = WRAPPED_KEY_SIZE;

	ret = qseecom_send_command(app_handle, req, req_len, rsp, rsp_len);
	if (!ret) {
		memcpy(params->key_blob, rsp->wrapkey_rsp.wrapped_key_buffer,
			WRAPPED_KEY_SIZE);
		memcpy(key, rsp->wrapkey_rsp.key_buffer, AES256_KEY_SIZE);
	}
	return ret;
}

static int init_aead(void)
{
	if (!tfm) {
		tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
		if (IS_ERR(tfm)) {
			pr_err("Error crypto_alloc_aead: %d\n",	PTR_ERR(tfm));
			return PTR_ERR(tfm);
		}
	}
	return 0;
}

static int init_ta_and_set_key(void)
{
	const uint32_t shared_buffer_len = 4096;
	int ret;

	ret = qseecom_start_app(&app_handle, "secs2d", shared_buffer_len);
	if (ret) {
		pr_err("qseecom_start_app failed: %d\n", ret);
		return ret;
	}

	ret = get_key_from_ta();
	if (ret)
		pr_err("set_key returned %d\n", ret);

	ret = qseecom_shutdown_app(&app_handle);
	if (ret)
		pr_err("qseecom_shutdown_app failed: %d\n", ret);

	return ret;
}

static int alloc_auth_memory(void)
{
	unsigned long total_auth_size;

	/* Number of Auth slots is equal to the number of image pages */
	params->authslot_count = snapshot_get_image_size();
	total_auth_size = params->authslot_count * AUTH_SIZE;

	authslot_start = vmalloc(total_auth_size);
	if (!authslot_start)
		return -ENOMEM;
	return 0;
}

void deinit_aes_encrypt(void)
{
	if (temp_out_buf) {
		free_pages((unsigned long)temp_out_buf, 1);
		temp_out_buf = NULL;
	}

	if (tfm) {
		crypto_free_aead(tfm);
		tfm = NULL;
	}

	memset(key, 0, AES256_KEY_SIZE);
	memset(params->key_blob, 0, WRAPPED_KEY_SIZE);
	kfree(params);
}

static void cleanup_cmp_blk_array(void)
{
	blk_array_pos = 0;
	if (compressed_blk_array) {
		kvfree((void *)compressed_blk_array);
		compressed_blk_array = NULL;
	}
	if (auth_slot) {
		free_page((unsigned long)auth_slot);
		auth_slot = NULL;

	}
}

static int hibernate_pm_notifier(struct notifier_block *nb,
				unsigned long event, void *unused)
{
	int ret = NOTIFY_DONE;

	switch (event) {

	case (PM_HIBERNATION_PREPARE):
		params = kmalloc(sizeof(struct qcom_crypto_params), GFP_KERNEL);
		if (!params)
			return NOTIFY_BAD;

		ret = init_aead();
		if (ret) {
			pr_err("%s: Failed init_aead(): %d\n", __func__, ret);
			goto err_aead;
		}

		ret = init_ta_and_set_key();
		if (ret) {
			pr_err("%s: Failed to init TA: %d\n", __func__, ret);
			goto err_setkey;
		}

		temp_out_buf = (void *)__get_free_pages(GFP_KERNEL, 1);
		if (!temp_out_buf) {
			pr_err("%s: Failed alloc_auth_memory %d\n", __func__, ret);
			ret = -1;
			goto err_setkey;
		}
		init_completion(&write_done);
		break;

	case (PM_POST_HIBERNATION):
		deinit_aes_encrypt();
		cleanup_cmp_blk_array();
		break;

	default:
		WARN_ONCE(1, "Invalid PM Notifier\n");
		break;
	}

	return NOTIFY_DONE;

err_setkey:
	memset(params->key_blob, 0, WRAPPED_KEY_SIZE);
	memset(key, 0, AES256_KEY_SIZE);
	crypto_free_aead(tfm);
err_aead:
	kfree(params);
	return NOTIFY_BAD;
}

static struct notifier_block pm_nb = {
	.notifier_call = hibernate_pm_notifier,
};

static void init_aes_encrypt(void *data, void *unused)
{
	int ret;

	/*
	 * Encryption results in two things:
	 * 1. Encrypted data
	 * 2. Auth
	 * Save the Auth data of all pages locally and return only the
	 * encrypted page to the caller. Allocate memory to save the auth.
	 */
	ret = alloc_auth_memory();
	if (ret) {
		pr_err("%s: Failed alloc_auth_memory %d\n", __func__, ret);
		goto err_auth;
	}

	first_encrypt = 1;
	pos = 0;
	memcpy(params->aad, "SECURE_S2D!!", sizeof(params->aad));
	params->authsize = AUTH_SIZE;
	return;
err_auth:
	memset(params->key_blob, 0, WRAPPED_KEY_SIZE);
	memset(key, 0, AES256_KEY_SIZE);
	crypto_free_aead(tfm);
	kfree(params);
}

/*
 * Bit(part of swsusp_header_flags) to indicate if the image is uncompressed
 * or not. This is inline with SF_NOCOMPRESS_MODE defined in
 * kernel/power/power.h.
 */
#define SF_NOCOMPRESS_MODE      2

static void hibernated_do_mem_alloc(void *data, unsigned long pages,
	unsigned int swsusp_header_flags, int *ret)
{
	uint32_t size;

	/* total no. of pages in the snapshot image */
	nr_pages = pages;

	if (!(swsusp_header_flags & SF_NOCOMPRESS_MODE)) {
		size = get_size_of_compression_block_array(pages);

		compressed_blk_array = kvzalloc(size, GFP_KERNEL);
		if (!compressed_blk_array) {
			*ret = -ENOMEM;
			return;
		}

		/* Allocate memory to hold authentication slot start */
		auth_slot = (void *)get_zeroed_page(GFP_KERNEL);
		if (!auth_slot) {
			pr_err("Failed to allocate page for storing authentication tag slot number\n");
			*ret = -ENOMEM;
		}
	}
}

static void hibernate_save_cmp_len(void *data, size_t cmp_len)
{
	uint8_t pages;

	pages = DIV_ROUND_UP(cmp_len, PAGE_SIZE);
	compressed_blk_array[blk_array_pos++] = pages;
}

static int __init qcom_secure_hibernattion_init(void)
{
	int ret;

	register_trace_android_vh_encrypt_page(encrypt_page, NULL);
	register_trace_android_vh_init_aes_encrypt(init_aes_encrypt, NULL);
	register_trace_android_vh_skip_swap_map_write(skip_swap_map_write, NULL);
	register_trace_android_vh_post_image_save(save_params_to_disk, NULL);
	register_trace_android_vh_hibernate_save_cmp_len(hibernate_save_cmp_len, NULL);
	register_trace_android_vh_hibernated_do_mem_alloc(hibernated_do_mem_alloc, NULL);

	ret = register_pm_notifier(&pm_nb);
	if (ret) {
		pr_err("%s: Failed to register nb: %d\n", __func__, ret);
		return ret;
	}
	ret = register_reboot_notifier(&poweroff_nb);
	if (ret) {
		pr_err("%s: Failed to register nb: %d\n", __func__, ret);
		return ret;
	}
	INIT_WORK(&save_params_work, save_auth_and_params_to_disk);
	return 0;
}

module_init(qcom_secure_hibernattion_init);

MODULE_DESCRIPTION("Framework to encrypt a page using a trusted application");
MODULE_LICENSE("GPL");
