// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip crypto
 *
 * Copyright (c) 2021, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */
#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/rtnetlink.h>
#include <linux/sysctl.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/dma-buf.h>
#include <linux/list.h>

#include "version.h"
#include "cipherapi.h"
#include "rk_cryptodev.h"

#define MAX_CRYPTO_DEV		1
#define MAX_CRYPTO_NAME_LEN	64

struct dma_fd_map_node {
	struct kernel_crypt_fd_map_op fd_map;
	struct sg_table *sgtbl;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *dma_attach;
	struct list_head	list;
};

struct crypto_dev_info {
	struct device *dev;
	char name[MAX_CRYPTO_NAME_LEN];
	bool is_multi_thread;
};

static struct crypto_dev_info g_dev_infos[MAX_CRYPTO_DEV];

/*
 * rk_cryptodev_register_dev - register crypto device into rk_cryptodev.
 * @dev:	[in]	crypto device to register
 * @name:	[in]	crypto device name to register
 */
int rk_cryptodev_register_dev(struct device *dev, const char *name)
{
	uint32_t i;

	if (WARN_ON(!dev))
		return -EINVAL;

	if (WARN_ON(!name))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(g_dev_infos); i++) {
		if (!g_dev_infos[i].dev) {
			memset(&g_dev_infos[i], 0x00, sizeof(g_dev_infos[i]));

			g_dev_infos[i].dev = dev;
			strncpy(g_dev_infos[i].name, name, sizeof(g_dev_infos[i].name));

			g_dev_infos[i].is_multi_thread = strstr(g_dev_infos[i].name, "multi");
			dev_info(dev, "register to cryptodev ok!\n");
			return 0;
		}
	}

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(rk_cryptodev_register_dev);

/*
 * rk_cryptodev_unregister_dev - unregister crypto device from rk_cryptodev
 * @dev:	[in]	crypto device to unregister
 */
int rk_cryptodev_unregister_dev(struct device *dev)
{
	uint32_t i;

	if (WARN_ON(!dev))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(g_dev_infos); i++) {
		if (g_dev_infos[i].dev == dev) {
			memset(&g_dev_infos[i], 0x00, sizeof(g_dev_infos[i]));
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(rk_cryptodev_unregister_dev);

static struct device *rk_cryptodev_find_dev(const char *name)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(g_dev_infos); i++) {
		if (g_dev_infos[i].dev)
			return g_dev_infos[i].dev;
	}

	return NULL;
}

/* this function has to be called from process context */
static int fill_kcop_fd_from_cop(struct kernel_crypt_fd_op *kcop, struct fcrypt *fcr)
{
	struct crypt_fd_op *cop = &kcop->cop;
	struct csession *ses_ptr;
	int rc;

	/* this also enters ses_ptr->sem */
	ses_ptr = crypto_get_session_by_sid(fcr, cop->ses);
	if (unlikely(!ses_ptr)) {
		derr(1, "invalid session ID=0x%08X", cop->ses);
		return -EINVAL;
	}
	kcop->ivlen = cop->iv ? ses_ptr->cdata.ivsize : 0;
	kcop->digestsize = 0; /* will be updated during operation */

	crypto_put_session(ses_ptr);

	kcop->task = current;
	kcop->mm = current->mm;

	if (cop->iv) {
		rc = copy_from_user(kcop->iv, cop->iv, kcop->ivlen);
		if (unlikely(rc)) {
			derr(1, "error copying IV (%d bytes), returned %d for addr %p",
			     kcop->ivlen, rc, cop->iv);
			return -EFAULT;
		}
	}

	return 0;
}


/* this function has to be called from process context */
static int fill_cop_fd_from_kcop(struct kernel_crypt_fd_op *kcop, struct fcrypt *fcr)
{
	int ret;

	if (kcop->digestsize) {
		ret = copy_to_user(kcop->cop.mac,
				  kcop->hash_output, kcop->digestsize);
		if (unlikely(ret))
			return -EFAULT;
	}
	if (kcop->ivlen && kcop->cop.flags & COP_FLAG_WRITE_IV) {
		ret = copy_to_user(kcop->cop.iv,
				   kcop->iv, kcop->ivlen);
		if (unlikely(ret))
			return -EFAULT;
	}
	return 0;
}

static int kcop_fd_from_user(struct kernel_crypt_fd_op *kcop,
			struct fcrypt *fcr, void __user *arg)
{
	if (unlikely(copy_from_user(&kcop->cop, arg, sizeof(kcop->cop))))
		return -EFAULT;

	return fill_kcop_fd_from_cop(kcop, fcr);
}

static int kcop_fd_to_user(struct kernel_crypt_fd_op *kcop,
			   struct fcrypt *fcr, void __user *arg)
{
	int ret;

	ret = fill_cop_fd_from_kcop(kcop, fcr);
	if (unlikely(ret)) {
		derr(1, "Error in fill_cop_from_kcop");
		return ret;
	}

	if (unlikely(copy_to_user(arg, &kcop->cop, sizeof(kcop->cop)))) {
		derr(1, "Cannot copy to userspace");
		return -EFAULT;
	}

	return 0;
}

static int
hash_n_crypt_fd(struct csession *ses_ptr, struct crypt_fd_op *cop,
		struct scatterlist *src_sg, struct scatterlist *dst_sg,
		uint32_t len)
{
	int ret;

	/* Always hash before encryption and after decryption. Maybe
	 * we should introduce a flag to switch... TBD later on.
	 */
	if (cop->op == COP_ENCRYPT) {
		if (ses_ptr->hdata.init != 0) {
			ret = cryptodev_hash_update(&ses_ptr->hdata,
						    src_sg, len);
			if (unlikely(ret))
				goto out_err;
		}
		if (ses_ptr->cdata.init != 0) {
			ret = cryptodev_cipher_encrypt(&ses_ptr->cdata,
						       src_sg, dst_sg, len);

			if (unlikely(ret))
				goto out_err;
		}
	} else {
		if (ses_ptr->cdata.init != 0) {
			ret = cryptodev_cipher_decrypt(&ses_ptr->cdata,
						       src_sg, dst_sg, len);

			if (unlikely(ret))
				goto out_err;
		}

		if (ses_ptr->hdata.init != 0) {
			ret = cryptodev_hash_update(&ses_ptr->hdata,
						    dst_sg, len);
			if (unlikely(ret))
				goto out_err;
		}
	}
	return 0;
out_err:
	derr(0, "CryptoAPI failure: %d", ret);
	return ret;
}

static int get_dmafd_sgtbl(int dma_fd, unsigned int dma_len, enum dma_data_direction dir,
			   struct sg_table **sg_tbl, struct dma_buf_attachment **dma_attach,
			   struct dma_buf **dmabuf)
{
	struct device *crypto_dev = rk_cryptodev_find_dev(NULL);

	if (!crypto_dev)
		return -EINVAL;

	*sg_tbl     = NULL;
	*dmabuf     = NULL;
	*dma_attach = NULL;

	*dmabuf = dma_buf_get(dma_fd);
	if (IS_ERR(*dmabuf)) {
		derr(1, "dmabuf error! ret = %d", (int)PTR_ERR(*dmabuf));
		*dmabuf = NULL;
		goto error;
	}

	*dma_attach = dma_buf_attach(*dmabuf, crypto_dev);
	if (IS_ERR(*dma_attach)) {
		derr(1, "dma_attach error! ret = %d", (int)PTR_ERR(*dma_attach));
		*dma_attach = NULL;
		goto error;
	}

	/*
	 * DMA_TO_DEVICE  : cache clean for input data
	 * DMA_FROM_DEVICE: cache invalidate for output data
	 */
	*sg_tbl = dma_buf_map_attachment(*dma_attach, dir);
	if (IS_ERR(*sg_tbl)) {
		derr(1, "sg_tbl error! ret = %d", (int)PTR_ERR(*sg_tbl));
		*sg_tbl = NULL;
		goto error;
	}

	/* cache invalidate for input data */
	if (dir == DMA_TO_DEVICE)
		dma_sync_sg_for_cpu(crypto_dev, (*sg_tbl)->sgl, (*sg_tbl)->nents, DMA_FROM_DEVICE);

	return 0;
error:
	if (*sg_tbl)
		dma_buf_unmap_attachment(*dma_attach, *sg_tbl, dir);

	if (*dma_attach)
		dma_buf_detach(*dmabuf, *dma_attach);

	if (*dmabuf)
		dma_buf_put(*dmabuf);

	return -EINVAL;
}

static int put_dmafd_sgtbl(int dma_fd, enum dma_data_direction dir,
			   struct sg_table *sg_tbl, struct dma_buf_attachment *dma_attach,
			   struct dma_buf *dmabuf)
{
	struct device *crypto_dev = rk_cryptodev_find_dev(NULL);

	if (!crypto_dev)
		return -EINVAL;

	if (!sg_tbl || !dma_attach || !dmabuf)
		return -EINVAL;

	/* cache clean for output data */
	if (dir == DMA_FROM_DEVICE)
		dma_sync_sg_for_device(crypto_dev, sg_tbl->sgl, sg_tbl->nents, DMA_TO_DEVICE);

	/*
	 * DMA_TO_DEVICE  : do nothing for input data
	 * DMA_FROM_DEVICE: cache invalidate for output data
	 */
	dma_buf_unmap_attachment(dma_attach, sg_tbl, dir);
	dma_buf_detach(dmabuf, dma_attach);
	dma_buf_put(dmabuf);

	return 0;
}

static struct dma_fd_map_node *dma_fd_find_node(struct fcrypt *fcr, int dma_fd)
{
	struct dma_fd_map_node *map_node = NULL;

	mutex_lock(&fcr->sem);

	list_for_each_entry(map_node, &fcr->dma_map_list, list) {
		if (unlikely(map_node->fd_map.mop.dma_fd == dma_fd)) {
			mutex_unlock(&fcr->sem);
			return map_node;
		}
	}

	mutex_unlock(&fcr->sem);

	return NULL;
}

/* This is the main crypto function - zero-copy edition */
static int __crypto_fd_run(struct fcrypt *fcr, struct csession *ses_ptr,
			   struct kernel_crypt_fd_op *kcop)
{
	struct crypt_fd_op *cop = &kcop->cop;
	struct dma_buf *dma_buf_in = NULL, *dma_buf_out = NULL;
	struct sg_table sg_tmp;
	struct sg_table *sg_tbl_in = NULL, *sg_tbl_out = NULL;
	struct dma_buf_attachment *dma_attach_in = NULL, *dma_attach_out = NULL;
	struct dma_fd_map_node *node_src = NULL, *node_dst = NULL;
	int ret = 0;

	node_src = dma_fd_find_node(fcr, kcop->cop.src_fd);
	if (node_src) {
		sg_tbl_in = node_src->sgtbl;
	} else {
		ret = get_dmafd_sgtbl(kcop->cop.src_fd, kcop->cop.len, DMA_TO_DEVICE,
				&sg_tbl_in, &dma_attach_in, &dma_buf_in);
		if (unlikely(ret)) {
			derr(1, "Error get_dmafd_sgtbl src.");
			goto exit;
		}
	}

	/* only cipher has dst */
	if (ses_ptr->cdata.init) {
		node_dst = dma_fd_find_node(fcr, kcop->cop.dst_fd);
		if (node_dst) {
			sg_tbl_out = node_dst->sgtbl;
		} else {
			ret = get_dmafd_sgtbl(kcop->cop.dst_fd, kcop->cop.len, DMA_FROM_DEVICE,
				&sg_tbl_out, &dma_attach_out, &dma_buf_out);
			if (unlikely(ret)) {
				derr(1, "Error get_dmafd_sgtbl dst.");
				goto exit;
			}
		}
	} else {
		memset(&sg_tmp, 0x00, sizeof(sg_tmp));
		sg_tbl_out = &sg_tmp;
	}

	ret = hash_n_crypt_fd(ses_ptr, cop, sg_tbl_in->sgl, sg_tbl_out->sgl, cop->len);

exit:
	if (dma_buf_in)
		put_dmafd_sgtbl(kcop->cop.src_fd, DMA_TO_DEVICE,
				sg_tbl_in, dma_attach_in, dma_buf_in);

	if (dma_buf_out)
		put_dmafd_sgtbl(kcop->cop.dst_fd, DMA_FROM_DEVICE,
				sg_tbl_out, dma_attach_out, dma_buf_out);
	return ret;
}

static int crypto_fd_run(struct fcrypt *fcr, struct kernel_crypt_fd_op *kcop)
{
	struct csession *ses_ptr;
	struct crypt_fd_op *cop = &kcop->cop;
	int ret = -EINVAL;

	if (unlikely(cop->op != COP_ENCRYPT && cop->op != COP_DECRYPT)) {
		ddebug(1, "invalid operation op=%u", cop->op);
		return -EINVAL;
	}

	/* this also enters ses_ptr->sem */
	ses_ptr = crypto_get_session_by_sid(fcr, cop->ses);
	if (unlikely(!ses_ptr)) {
		derr(1, "invalid session ID=0x%08X", cop->ses);
		return -EINVAL;
	}

	if (ses_ptr->hdata.init != 0 && (cop->flags == 0 || cop->flags & COP_FLAG_RESET)) {
		ret = cryptodev_hash_reset(&ses_ptr->hdata);
		if (unlikely(ret)) {
			derr(1, "error in cryptodev_hash_reset()");
			goto out_unlock;
		}
	}

	if (ses_ptr->cdata.init != 0) {
		int blocksize = ses_ptr->cdata.blocksize;

		if (unlikely(cop->len % blocksize)) {
			derr(1, "data size (%u) isn't a multiple of block size (%u)",
				cop->len, blocksize);
			ret = -EINVAL;
			goto out_unlock;
		}

		cryptodev_cipher_set_iv(&ses_ptr->cdata, kcop->iv,
					min(ses_ptr->cdata.ivsize, kcop->ivlen));
	}

	if (likely(cop->len)) {
		ret = __crypto_fd_run(fcr, ses_ptr, kcop);
		if (unlikely(ret))
			goto out_unlock;
	}

	if (ses_ptr->cdata.init != 0) {
		cryptodev_cipher_get_iv(&ses_ptr->cdata, kcop->iv,
					min(ses_ptr->cdata.ivsize, kcop->ivlen));
	}

	if (ses_ptr->hdata.init != 0 &&
		((cop->flags & COP_FLAG_FINAL) ||
		 (!(cop->flags & COP_FLAG_UPDATE) || cop->len == 0))) {

		ret = cryptodev_hash_final(&ses_ptr->hdata, kcop->hash_output);
		if (unlikely(ret)) {
			derr(0, "CryptoAPI failure: %d", ret);
			goto out_unlock;
		}
		kcop->digestsize = ses_ptr->hdata.digestsize;
	}

out_unlock:
	crypto_put_session(ses_ptr);

	return ret;
}

static int kcop_map_fd_from_user(struct kernel_crypt_fd_map_op *kcop,
			struct fcrypt *fcr, void __user *arg)
{
	if (unlikely(copy_from_user(&kcop->mop, arg, sizeof(kcop->mop))))
		return -EFAULT;

	return 0;
}

static int kcop_map_fd_to_user(struct kernel_crypt_fd_map_op *kcop,
			   struct fcrypt *fcr, void __user *arg)
{
	if (unlikely(copy_to_user(arg, &kcop->mop, sizeof(kcop->mop)))) {
		derr(1, "Cannot copy to userspace");
		return -EFAULT;
	}

	return 0;
}

static int dma_fd_map_for_user(struct fcrypt *fcr, struct kernel_crypt_fd_map_op *kmop)
{
	struct device *crypto_dev = NULL;
	struct dma_fd_map_node *map_node = NULL;

	/* check if dma_fd is already mapped */
	map_node = dma_fd_find_node(fcr, kmop->mop.dma_fd);
	if (map_node) {
		kmop->mop.phys_addr = map_node->fd_map.mop.phys_addr;
		return 0;
	}

	crypto_dev = rk_cryptodev_find_dev(NULL);
	if (!crypto_dev)
		return -EINVAL;

	map_node = kzalloc(sizeof(*map_node), GFP_KERNEL);
	if (!map_node)
		return -ENOMEM;

	map_node->dmabuf = dma_buf_get(kmop->mop.dma_fd);
	if (IS_ERR(map_node->dmabuf)) {
		derr(1, "dmabuf error! ret = %d", (int)PTR_ERR(map_node->dmabuf));
		map_node->dmabuf = NULL;
		goto error;
	}

	map_node->dma_attach = dma_buf_attach(map_node->dmabuf, crypto_dev);
	if (IS_ERR(map_node->dma_attach)) {
		derr(1, "dma_attach error! ret = %d", (int)PTR_ERR(map_node->dma_attach));
		map_node->dma_attach = NULL;
		goto error;
	}

	map_node->sgtbl = dma_buf_map_attachment(map_node->dma_attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(map_node->sgtbl)) {
		derr(1, "sg_tbl error! ret = %d", (int)PTR_ERR(map_node->sgtbl));
		map_node->sgtbl = NULL;
		goto error;
	}

	map_node->fd_map.mop.dma_fd    = kmop->mop.dma_fd;
	map_node->fd_map.mop.phys_addr = map_node->sgtbl->sgl->dma_address;

	mutex_lock(&fcr->sem);
	list_add(&map_node->list, &fcr->dma_map_list);
	mutex_unlock(&fcr->sem);

	kmop->mop.phys_addr = map_node->fd_map.mop.phys_addr;

	return 0;
error:
	if (map_node->sgtbl)
		dma_buf_unmap_attachment(map_node->dma_attach, map_node->sgtbl, DMA_BIDIRECTIONAL);

	if (map_node->dma_attach)
		dma_buf_detach(map_node->dmabuf, map_node->dma_attach);

	if (map_node->dmabuf)
		dma_buf_put(map_node->dmabuf);

	kfree(map_node);

	return -EINVAL;
}

static int dma_fd_unmap_for_user(struct fcrypt *fcr, struct kernel_crypt_fd_map_op *kmop)
{
	struct dma_fd_map_node *tmp, *map_node;
	bool is_found = false;
	int ret = 0;

	mutex_lock(&fcr->sem);
	list_for_each_entry_safe(map_node, tmp, &fcr->dma_map_list, list) {
		if (map_node->fd_map.mop.dma_fd == kmop->mop.dma_fd &&
		    map_node->fd_map.mop.phys_addr == kmop->mop.phys_addr) {
			dma_buf_unmap_attachment(map_node->dma_attach, map_node->sgtbl,
						 DMA_BIDIRECTIONAL);
			dma_buf_detach(map_node->dmabuf, map_node->dma_attach);
			dma_buf_put(map_node->dmabuf);
			list_del(&map_node->list);
			kfree(map_node);
			kmop->mop.phys_addr = 0;
			is_found = true;
			break;
		}
	}

	if (unlikely(!is_found)) {
		derr(1, "dmafd =0x%08X not found!", kmop->mop.dma_fd);
		ret = -ENOENT;
		mutex_unlock(&fcr->sem);
		goto exit;
	}

	mutex_unlock(&fcr->sem);

exit:
	return ret;
}

static int dma_fd_begin_cpu_access(struct fcrypt *fcr, struct kernel_crypt_fd_map_op *kmop)
{
	struct dma_fd_map_node *map_node = NULL;

	map_node = dma_fd_find_node(fcr, kmop->mop.dma_fd);
	if (unlikely(!map_node)) {
		derr(1, "dmafd =0x%08X not found!", kmop->mop.dma_fd);
		return -ENOENT;
	}

	return dma_buf_begin_cpu_access(map_node->dmabuf, DMA_BIDIRECTIONAL);
}

static int dma_fd_end_cpu_access(struct fcrypt *fcr, struct kernel_crypt_fd_map_op *kmop)
{
	struct dma_fd_map_node *map_node = NULL;

	map_node = dma_fd_find_node(fcr, kmop->mop.dma_fd);
	if (unlikely(!map_node)) {
		derr(1, "dmafd =0x%08X not found!", kmop->mop.dma_fd);
		return -ENOENT;
	}

	return dma_buf_end_cpu_access(map_node->dmabuf, DMA_BIDIRECTIONAL);
}

static int kcop_rsa_from_user(struct kernel_crypt_rsa_op *kcop,
			struct fcrypt *fcr, void __user *arg)
{
	if (unlikely(copy_from_user(&kcop->rop, arg, sizeof(kcop->rop))))
		return -EFAULT;

	return 0;
}

static int kcop_rsa_to_user(struct kernel_crypt_rsa_op *kcop,
			   struct fcrypt *fcr, void __user *arg)
{
	if (unlikely(copy_to_user(arg, &kcop->rop, sizeof(kcop->rop)))) {
		derr(1, "Cannot copy to userspace");
		return -EFAULT;
	}

	return 0;
}

static int crypto_rsa_run(struct fcrypt *fcr, struct kernel_crypt_rsa_op *krop)
{
	int ret;
	u8 *key = NULL, *in = NULL, *out = NULL;
	u32 out_len_max;
	struct crypt_rsa_op *rop = &krop->rop;
	const char *driver = "rsa-rk";
	struct crypto_akcipher *tfm = NULL;
	struct akcipher_request *req = NULL;
	struct crypto_wait wait;
	struct scatterlist src, dst;
	bool is_priv_key = (rop->flags & COP_FLAG_RSA_PRIV) == COP_FLAG_RSA_PRIV;

	/* The key size cannot exceed RK_RSA_BER_KEY_MAX Byte */
	if (rop->key_len > RK_RSA_BER_KEY_MAX)
		return -ENOKEY;

	if (rop->in_len  > RK_RSA_KEY_MAX_BYTES ||
	    rop->out_len > RK_RSA_KEY_MAX_BYTES)
		return -EINVAL;

	tfm = crypto_alloc_akcipher(driver, 0, 0);
	if (IS_ERR(tfm)) {
		ddebug(2, "alg: akcipher: Failed to load tfm for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ddebug(2, "akcipher_request_alloc failed\n");
		ret = -ENOMEM;
		goto exit;
	}

	key = kzalloc(rop->key_len, GFP_KERNEL);
	if (!key) {
		ret = -ENOMEM;
		goto exit;
	}

	if (unlikely(copy_from_user(key, u64_to_user_ptr(rop->key), rop->key_len))) {
		ret = -EFAULT;
		goto exit;
	}

	in = kzalloc(rop->in_len, GFP_KERNEL);
	if (!in) {
		ret = -ENOMEM;
		goto exit;
	}

	if (unlikely(copy_from_user(in, u64_to_user_ptr(rop->in), rop->in_len))) {
		ret = -EFAULT;
		goto exit;
	}

	if (is_priv_key)
		ret = crypto_akcipher_set_priv_key(tfm, key, rop->key_len);
	else
		ret = crypto_akcipher_set_pub_key(tfm, key, rop->key_len);
	if (ret) {
		derr(1, "crypto_akcipher_set_%s_key error[%d]",
		     is_priv_key ? "priv" : "pub", ret);
		ret = -ENOKEY;
		goto exit;
	}

	out_len_max = crypto_akcipher_maxsize(tfm);
	out = kzalloc(out_len_max, GFP_KERNEL);
	if (!out) {
		ret = -ENOMEM;
		goto exit;
	}

	sg_init_one(&src, in, rop->in_len);
	sg_init_one(&dst, out, out_len_max);

	crypto_init_wait(&wait);
	akcipher_request_set_crypt(req, &src, &dst, rop->in_len, out_len_max);

	switch (rop->op) {
	case AOP_ENCRYPT:
		ret = crypto_wait_req(crypto_akcipher_encrypt(req), &wait);
		break;
	case AOP_DECRYPT:
		ret = crypto_wait_req(crypto_akcipher_decrypt(req), &wait);
		break;
	default:
		derr(1, "unknown ops %x", rop->op);
		ret = -EINVAL;
		break;
	}

	if (ret) {
		derr(1, "alg: akcipher: failed %d\n", ret);
		goto exit;
	}

	if (unlikely(copy_to_user(u64_to_user_ptr(rop->out), out, req->dst_len))) {
		derr(1, "Cannot copy to userspace");
		ret = -EFAULT;
		goto exit;
	}

	rop->out_len = req->dst_len;
exit:
	kfree(out);
	kfree(in);
	kfree(key);
	akcipher_request_free(req);
	crypto_free_akcipher(tfm);

	return ret;
}

/* Typical AEAD (i.e. GCM) encryption/decryption.
 * During decryption the tag is verified.
 */
static int rk_auth_fd_n_crypt(struct csession *ses_ptr, struct kernel_crypt_auth_fd_op *kcaop,
			      struct scatterlist *auth_sg, uint32_t auth_len,
			      struct scatterlist *src_sg,
			      struct scatterlist *dst_sg, uint32_t len)
{
	int ret;
	struct crypt_auth_fd_op *caop = &kcaop->caop;
	int max_tag_len;

	max_tag_len = cryptodev_cipher_get_tag_size(&ses_ptr->cdata);
	if (unlikely(caop->tag_len > max_tag_len)) {
		derr(0, "Illegal tag length: %d", caop->tag_len);
		return -EINVAL;
	}

	if (caop->tag_len)
		cryptodev_cipher_set_tag_size(&ses_ptr->cdata, caop->tag_len);
	else
		caop->tag_len = max_tag_len;

	cryptodev_cipher_auth(&ses_ptr->cdata, auth_sg, auth_len);

	if (caop->op == COP_ENCRYPT) {
		ret = cryptodev_cipher_encrypt(&ses_ptr->cdata,
					       src_sg, dst_sg, len);
		if (unlikely(ret)) {
			derr(0, "cryptodev_cipher_encrypt: %d", ret);
			return ret;
		}
	} else {
		ret = cryptodev_cipher_decrypt(&ses_ptr->cdata,
					       src_sg, dst_sg, len);

		if (unlikely(ret)) {
			derr(0, "cryptodev_cipher_decrypt: %d", ret);
			return ret;
		}
	}

	return 0;
}

static void sg_init_table_set_page(struct scatterlist *sgl_dst, unsigned int nents_dst,
				   struct scatterlist *sgl_src, unsigned int len)
{
	sg_init_table(sgl_dst, nents_dst);
	sg_set_page(sgl_dst, sg_page(sgl_src), len, sgl_src->offset);

	sg_dma_address(sgl_dst) = sg_dma_address(sgl_src);
	sg_dma_len(sgl_dst)     = len;
}

/* This is the main crypto function - zero-copy edition */
static int crypto_auth_fd_zc_rk(struct fcrypt *fcr, struct csession *ses_ptr,
				  struct kernel_crypt_auth_fd_op *kcaop)
{
	struct crypt_auth_fd_op *caop = &kcaop->caop;
	struct dma_buf *dma_buf_in = NULL, *dma_buf_out = NULL, *dma_buf_auth = NULL;
	struct sg_table *sg_tbl_in = NULL, *sg_tbl_out = NULL, *sg_tbl_auth = NULL;
	struct dma_buf_attachment *dma_attach_in = NULL, *dma_attach_out = NULL;
	struct dma_buf_attachment *dma_attach_auth = NULL;
	struct dma_fd_map_node *node_src = NULL, *node_dst = NULL, *node_auth = NULL;
	struct scatterlist *dst_sg, *src_sg;
	struct scatterlist auth_src[2], auth_dst[2], src[2], dst[2], tag[2];
	unsigned char *tag_buf = NULL;
	int ret = 0;

	node_src = dma_fd_find_node(fcr, caop->src_fd);
	if (node_src) {
		sg_tbl_in = node_src->sgtbl;
	} else {
		ret = get_dmafd_sgtbl(caop->src_fd, caop->len, DMA_TO_DEVICE,
				      &sg_tbl_in, &dma_attach_in, &dma_buf_in);
		if (unlikely(ret)) {
			derr(1, "Error get_dmafd_sgtbl src.");
			goto exit;
		}
	}

	node_dst = dma_fd_find_node(fcr, caop->dst_fd);
	if (node_dst) {
		sg_tbl_out = node_dst->sgtbl;
	} else {
		ret = get_dmafd_sgtbl(caop->dst_fd, caop->len, DMA_FROM_DEVICE,
				      &sg_tbl_out, &dma_attach_out, &dma_buf_out);
		if (unlikely(ret)) {
			derr(1, "Error get_dmafd_sgtbl dst.");
			goto exit;
		}
	}

	src_sg = sg_tbl_in->sgl;
	dst_sg = sg_tbl_out->sgl;

	if (caop->auth_len > 0) {
		node_auth = dma_fd_find_node(fcr, caop->auth_fd);
		if (node_auth) {
			sg_tbl_auth = node_auth->sgtbl;
		} else {
			ret = get_dmafd_sgtbl(caop->auth_fd, caop->auth_len, DMA_TO_DEVICE,
					      &sg_tbl_auth, &dma_attach_auth, &dma_buf_auth);
			if (unlikely(ret)) {
				derr(1, "Error get_dmafd_sgtbl auth.");
				goto exit;
			}
		}

		sg_init_table_set_page(auth_src, ARRAY_SIZE(auth_src),
				       sg_tbl_auth->sgl, caop->auth_len);

		sg_init_table_set_page(auth_dst, ARRAY_SIZE(auth_dst),
				       sg_tbl_auth->sgl, caop->auth_len);

		sg_init_table_set_page(src, ARRAY_SIZE(src),
				       sg_tbl_in->sgl, caop->len);

		sg_init_table_set_page(dst, ARRAY_SIZE(dst),
				       sg_tbl_out->sgl, caop->len);

		sg_chain(auth_src, 2, src);
		sg_chain(auth_dst, 2, dst);
		src_sg = auth_src;
		dst_sg = auth_dst;
	}

	/* get tag */
	if (caop->tag && caop->tag_len > 0) {
		tag_buf = kcalloc(caop->tag_len, sizeof(*tag_buf), GFP_KERNEL);
		if (unlikely(!tag_buf)) {
			derr(1, "unable to kcalloc %d.", caop->tag_len);
			ret = -EFAULT;
			goto exit;
		}

		ret = copy_from_user(tag_buf, u64_to_user_ptr((u64)caop->tag), caop->tag_len);
		if (unlikely(ret)) {
			derr(1, "unable to copy tag data from userspace.");
			ret = -EFAULT;
			goto exit;
		}

		sg_init_table(tag, 2);
		sg_set_buf(tag, tag_buf, caop->tag_len);

		if (caop->op == COP_ENCRYPT)
			sg_chain(dst, 2, tag);
		else
			sg_chain(src, 2, tag);
	}

	if (caop->op == COP_ENCRYPT)
		ret = rk_auth_fd_n_crypt(ses_ptr, kcaop, NULL, caop->auth_len,
					 src_sg, dst_sg, caop->len);
	else
		ret = rk_auth_fd_n_crypt(ses_ptr, kcaop, NULL, caop->auth_len,
					 src_sg, dst_sg, caop->len + caop->tag_len);

	if (!ret && caop->op == COP_ENCRYPT && tag_buf) {
		ret = copy_to_user(u64_to_user_ptr((u64)kcaop->caop.tag), tag_buf, caop->tag_len);
		if (unlikely(ret)) {
			derr(1, "Error in copying to userspace");
			ret = -EFAULT;
			goto exit;
		}
	}

exit:
	kfree(tag_buf);

	if (dma_buf_in)
		put_dmafd_sgtbl(caop->src_fd, DMA_TO_DEVICE,
				sg_tbl_in, dma_attach_in, dma_buf_in);

	if (dma_buf_out)
		put_dmafd_sgtbl(caop->dst_fd, DMA_FROM_DEVICE,
				sg_tbl_out, dma_attach_out, dma_buf_out);

	if (dma_buf_auth)
		put_dmafd_sgtbl(caop->auth_fd, DMA_TO_DEVICE,
				sg_tbl_auth, dma_attach_auth, dma_buf_auth);

	return ret;
}

static int __crypto_auth_fd_run_zc(struct fcrypt *fcr, struct csession *ses_ptr,
				   struct kernel_crypt_auth_fd_op *kcaop)
{
	struct crypt_auth_fd_op *caop = &kcaop->caop;
	int ret;

	if (caop->flags & COP_FLAG_AEAD_RK_TYPE)
		ret = crypto_auth_fd_zc_rk(fcr, ses_ptr, kcaop);
	else
		ret = -EINVAL; /* other types, not implemented */

	return ret;
}

static int crypto_auth_fd_run(struct fcrypt *fcr, struct kernel_crypt_auth_fd_op *kcaop)
{
	struct csession *ses_ptr;
	struct crypt_auth_fd_op *caop = &kcaop->caop;
	int ret = -EINVAL;

	if (unlikely(caop->op != COP_ENCRYPT && caop->op != COP_DECRYPT)) {
		ddebug(1, "invalid operation op=%u", caop->op);
		return -EINVAL;
	}

	/* this also enters ses_ptr->sem */
	ses_ptr = crypto_get_session_by_sid(fcr, caop->ses);
	if (unlikely(!ses_ptr)) {
		derr(1, "invalid session ID=0x%08X", caop->ses);
		return -EINVAL;
	}

	if (unlikely(ses_ptr->cdata.init == 0)) {
		derr(1, "cipher context not initialized");
		ret = -EINVAL;
		goto out_unlock;
	}

	/* If we have a hash/mac handle reset its state */
	if (ses_ptr->hdata.init != 0) {
		ret = cryptodev_hash_reset(&ses_ptr->hdata);
		if (unlikely(ret)) {
			derr(1, "error in cryptodev_hash_reset()");
			goto out_unlock;
		}
	}

	cryptodev_cipher_set_iv(&ses_ptr->cdata, kcaop->iv,
				min(ses_ptr->cdata.ivsize, kcaop->ivlen));

	ret = __crypto_auth_fd_run_zc(fcr, ses_ptr, kcaop);
	if (unlikely(ret)) {
		derr(1, "error in __crypto_auth_fd_run_zc()");
		goto out_unlock;
	}

	ret = 0;

	cryptodev_cipher_get_iv(&ses_ptr->cdata, kcaop->iv,
				min(ses_ptr->cdata.ivsize, kcaop->ivlen));

out_unlock:
	crypto_put_session(ses_ptr);
	return ret;
}

/*
 * Return tag (digest) length for authenticated encryption
 * If the cipher and digest are separate, hdata.init is set - just return
 * digest length. Otherwise return digest length for aead ciphers
 */
static int rk_cryptodev_get_tag_len(struct csession *ses_ptr)
{
	if (ses_ptr->hdata.init)
		return ses_ptr->hdata.digestsize;
	else
		return cryptodev_cipher_get_tag_size(&ses_ptr->cdata);
}

/*
 * Calculate destination buffer length for authenticated encryption. The
 * expectation is that user-space code allocates exactly the same space for
 * destination buffer before calling cryptodev. The result is cipher-dependent.
 */
static int rk_cryptodev_fd_get_dst_len(struct crypt_auth_fd_op *caop, struct csession *ses_ptr)
{
	int dst_len = caop->len;

	if (caop->op == COP_DECRYPT)
		return dst_len;

	dst_len += caop->tag_len;

	/* for TLS always add some padding so the total length is rounded to
	 * cipher block size
	 */
	if (caop->flags & COP_FLAG_AEAD_TLS_TYPE) {
		int bs = ses_ptr->cdata.blocksize;

		dst_len += bs - (dst_len % bs);
	}

	return dst_len;
}

static int fill_kcaop_fd_from_caop(struct kernel_crypt_auth_fd_op *kcaop, struct fcrypt *fcr)
{
	struct crypt_auth_fd_op *caop = &kcaop->caop;
	struct csession *ses_ptr;
	int ret;

	/* this also enters ses_ptr->sem */
	ses_ptr = crypto_get_session_by_sid(fcr, caop->ses);
	if (unlikely(!ses_ptr)) {
		derr(1, "invalid session ID=0x%08X", caop->ses);
		return -EINVAL;
	}

	if (caop->tag_len == 0)
		caop->tag_len = rk_cryptodev_get_tag_len(ses_ptr);

	kcaop->ivlen   = caop->iv ? ses_ptr->cdata.ivsize : 0;
	kcaop->dst_len = rk_cryptodev_fd_get_dst_len(caop, ses_ptr);
	kcaop->task    = current;
	kcaop->mm      = current->mm;

	if (caop->iv) {
		ret = copy_from_user(kcaop->iv, u64_to_user_ptr((u64)caop->iv), kcaop->ivlen);
		if (unlikely(ret)) {
			derr(1, "error copy_from_user IV (%d bytes) returned %d for address %llu",
			     kcaop->ivlen, ret, caop->iv);
			ret = -EFAULT;
			goto out_unlock;
		}
	}

	ret = 0;

out_unlock:
	crypto_put_session(ses_ptr);
	return ret;
}

static int fill_caop_fd_from_kcaop(struct kernel_crypt_auth_fd_op *kcaop, struct fcrypt *fcr)
{
	int ret;

	kcaop->caop.len = kcaop->dst_len;

	if (kcaop->ivlen && kcaop->caop.flags & COP_FLAG_WRITE_IV) {
		ret = copy_to_user(u64_to_user_ptr((u64)kcaop->caop.iv), kcaop->iv, kcaop->ivlen);
		if (unlikely(ret)) {
			derr(1, "Error in copying iv to userspace");
			return -EFAULT;
		}
	}

	return 0;
}

static int kcaop_fd_from_user(struct kernel_crypt_auth_fd_op *kcaop,
			      struct fcrypt *fcr, void __user *arg)
{
	if (unlikely(copy_from_user(&kcaop->caop, arg, sizeof(kcaop->caop)))) {
		derr(1, "Error in copying from userspace");
		return -EFAULT;
	}

	return fill_kcaop_fd_from_caop(kcaop, fcr);
}

static int kcaop_fd_to_user(struct kernel_crypt_auth_fd_op *kcaop,
			    struct fcrypt *fcr, void __user *arg)
{
	int ret;

	ret = fill_caop_fd_from_kcaop(kcaop, fcr);
	if (unlikely(ret)) {
		derr(1, "Error in fill_caop_from_kcaop");
		return ret;
	}

	if (unlikely(copy_to_user(arg, &kcaop->caop, sizeof(kcaop->caop)))) {
		derr(1, "Cannot copy to userspace");
		return -EFAULT;
	}

	return 0;
}

long
rk_cryptodev_ioctl(struct fcrypt *fcr, unsigned int cmd, unsigned long arg_)
{
	struct kernel_crypt_fd_op kcop;
	struct kernel_crypt_fd_map_op kmop;
	struct kernel_crypt_rsa_op krop;
	struct kernel_crypt_auth_fd_op kcaop;
	void __user *arg = (void __user *)arg_;
	int ret;

	switch (cmd) {
	case RIOCCRYPT_FD:
		ret = kcop_fd_from_user(&kcop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = crypto_fd_run(fcr, &kcop);
		if (unlikely(ret)) {
			dwarning(1, "Error in crypto_run");
			return ret;
		}

		return kcop_fd_to_user(&kcop, fcr, arg);
	case RIOCAUTHCRYPT_FD:
		ret = kcaop_fd_from_user(&kcaop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = crypto_auth_fd_run(fcr, &kcaop);
		if (unlikely(ret)) {
			dwarning(1, "Error in crypto_run");
			return ret;
		}

		return kcaop_fd_to_user(&kcaop, fcr, arg);
	case RIOCCRYPT_FD_MAP:
		ret = kcop_map_fd_from_user(&kmop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = dma_fd_map_for_user(fcr, &kmop);
		if (unlikely(ret)) {
			dwarning(1, "Error in dma_fd_map_for_user");
			return ret;
		}

		return kcop_map_fd_to_user(&kmop, fcr, arg);
	case RIOCCRYPT_FD_UNMAP:
		ret = kcop_map_fd_from_user(&kmop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = dma_fd_unmap_for_user(fcr, &kmop);
		if (unlikely(ret))
			dwarning(1, "Error in dma_fd_unmap_for_user");

		return ret;
	case RIOCCRYPT_CPU_ACCESS:
		ret = kcop_map_fd_from_user(&kmop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = dma_fd_begin_cpu_access(fcr, &kmop);
		if (unlikely(ret))
			dwarning(1, "Error in dma_fd_begin_cpu_access");

		return ret;
	case RIOCCRYPT_DEV_ACCESS:
		ret = kcop_map_fd_from_user(&kmop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = dma_fd_end_cpu_access(fcr, &kmop);
		if (unlikely(ret))
			dwarning(1, "Error in dma_fd_end_cpu_access");

		return ret;
	case RIOCCRYPT_RSA_CRYPT:
		ret = kcop_rsa_from_user(&krop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = crypto_rsa_run(fcr, &krop);
		if (unlikely(ret)) {
			dwarning(1, "Error in rsa_run");
			return ret;
		}

		return kcop_rsa_to_user(&krop, fcr, arg);
	default:
		return -EINVAL;
	}
}

/* compatibility code for 32bit userlands */
#ifdef CONFIG_COMPAT

static inline void
compat_to_crypt_fd_op(struct compat_crypt_fd_op *compat, struct crypt_fd_op *cop)
{
	cop->ses    = compat->ses;
	cop->op     = compat->op;
	cop->flags  = compat->flags;
	cop->len    = compat->len;

	cop->src_fd = compat->src_fd;
	cop->dst_fd = compat->dst_fd;
	cop->mac    = compat_ptr(compat->mac);
	cop->iv     = compat_ptr(compat->iv);
}

static inline void
crypt_fd_op_to_compat(struct crypt_fd_op *cop, struct compat_crypt_fd_op *compat)
{
	compat->ses    = cop->ses;
	compat->op     = cop->op;
	compat->flags  = cop->flags;
	compat->len    = cop->len;

	compat->src_fd = cop->src_fd;
	compat->dst_fd = cop->dst_fd;
	compat->mac    = ptr_to_compat(cop->mac);
	compat->iv     = ptr_to_compat(cop->iv);
}

static int compat_kcop_fd_from_user(struct kernel_crypt_fd_op *kcop,
				    struct fcrypt *fcr, void __user *arg)
{
	struct compat_crypt_fd_op compat_cop;

	if (unlikely(copy_from_user(&compat_cop, arg, sizeof(compat_cop))))
		return -EFAULT;
	compat_to_crypt_fd_op(&compat_cop, &kcop->cop);

	return fill_kcop_fd_from_cop(kcop, fcr);
}

static int compat_kcop_fd_to_user(struct kernel_crypt_fd_op *kcop,
				  struct fcrypt *fcr, void __user *arg)
{
	int ret;
	struct compat_crypt_fd_op compat_cop;

	ret = fill_cop_fd_from_kcop(kcop, fcr);
	if (unlikely(ret)) {
		dwarning(1, "Error in fill_cop_from_kcop");
		return ret;
	}
	crypt_fd_op_to_compat(&kcop->cop, &compat_cop);

	if (unlikely(copy_to_user(arg, &compat_cop, sizeof(compat_cop)))) {
		dwarning(1, "Error copying to user");
		return -EFAULT;
	}
	return 0;
}

static inline void
compat_to_crypt_fd_map_op(struct compat_crypt_fd_map_op *compat, struct crypt_fd_map_op *mop)
{
	mop->dma_fd    = compat->dma_fd;
	mop->phys_addr = compat->phys_addr;
}

static inline void
crypt_fd_map_op_to_compat(struct crypt_fd_map_op *mop, struct compat_crypt_fd_map_op *compat)
{
	compat->dma_fd    = mop->dma_fd;
	compat->phys_addr = mop->phys_addr;
}

static int compat_kcop_map_fd_from_user(struct kernel_crypt_fd_map_op *kcop,
			struct fcrypt *fcr, void __user *arg)
{
	struct compat_crypt_fd_map_op compat_mop;

	if (unlikely(copy_from_user(&compat_mop, arg, sizeof(compat_mop))))
		return -EFAULT;

	compat_to_crypt_fd_map_op(&compat_mop, &kcop->mop);

	return 0;
}

static int compat_kcop_map_fd_to_user(struct kernel_crypt_fd_map_op *kcop,
			   struct fcrypt *fcr, void __user *arg)
{
	struct compat_crypt_fd_map_op compat_mop;

	crypt_fd_map_op_to_compat(&kcop->mop, &compat_mop);
	if (unlikely(copy_to_user(arg, &compat_mop, sizeof(compat_mop)))) {
		derr(1, "Cannot copy to userspace");
		return -EFAULT;
	}

	return 0;
}

long
rk_compat_cryptodev_ioctl(struct fcrypt *fcr, unsigned int cmd, unsigned long arg_)
{
	struct kernel_crypt_fd_op kcop;
	struct kernel_crypt_fd_map_op kmop;
	void __user *arg = (void __user *)arg_;
	int ret;

	switch (cmd) {
	case COMPAT_RIOCCRYPT_FD:
		ret = compat_kcop_fd_from_user(&kcop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = crypto_fd_run(fcr, &kcop);
		if (unlikely(ret)) {
			dwarning(1, "Error in crypto_run");
			return ret;
		}

		return compat_kcop_fd_to_user(&kcop, fcr, arg);
	case COMPAT_RIOCCRYPT_FD_MAP:
		ret = compat_kcop_map_fd_from_user(&kmop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = dma_fd_map_for_user(fcr, &kmop);
		if (unlikely(ret)) {
			dwarning(1, "Error in dma_fd_map_for_user");
			return ret;
		}

		return compat_kcop_map_fd_to_user(&kmop, fcr, arg);
	case COMPAT_RIOCCRYPT_FD_UNMAP:
		ret = compat_kcop_map_fd_from_user(&kmop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = dma_fd_unmap_for_user(fcr, &kmop);
		if (unlikely(ret))
			dwarning(1, "Error in dma_fd_unmap_for_user");

		return ret;
	case COMPAT_RIOCCRYPT_CPU_ACCESS:
		ret = compat_kcop_map_fd_from_user(&kmop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = dma_fd_begin_cpu_access(fcr, &kmop);
		if (unlikely(ret)) {
			dwarning(1, "Error in dma_fd_begin_cpu_access");
			return ret;
		}

		return compat_kcop_map_fd_to_user(&kmop, fcr, arg);
	case COMPAT_RIOCCRYPT_DEV_ACCESS:
		ret = compat_kcop_map_fd_from_user(&kmop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = dma_fd_end_cpu_access(fcr, &kmop);
		if (unlikely(ret))
			dwarning(1, "Error in dma_fd_end_cpu_access");

		return ret;
	default:
		return rk_cryptodev_ioctl(fcr, cmd, arg_);
	}
}

#endif /* CONFIG_COMPAT */

struct cipher_algo_name_map {
	uint32_t	id;
	const char	*name;
	int		is_stream;
	int		is_aead;
};

struct hash_algo_name_map {
	uint32_t	id;
	const char	*name;
	int		is_hmac;
};

static const struct cipher_algo_name_map c_algo_map_tbl[] = {
	{CRYPTO_RK_DES_ECB,     "ecb-des-rk",      0, 0},
	{CRYPTO_RK_DES_CBC,     "cbc-des-rk",      0, 0},
	{CRYPTO_RK_DES_CFB,     "cfb-des-rk",      0, 0},
	{CRYPTO_RK_DES_OFB,     "ofb-des-rk",      0, 0},
	{CRYPTO_RK_3DES_ECB,    "ecb-des3_ede-rk", 0, 0},
	{CRYPTO_RK_3DES_CBC,    "cbc-des3_ede-rk", 0, 0},
	{CRYPTO_RK_3DES_CFB,    "cfb-des3_ede-rk", 0, 0},
	{CRYPTO_RK_3DES_OFB,    "ofb-des3_ede-rk", 0, 0},
	{CRYPTO_RK_SM4_ECB,     "ecb-sm4-rk",      0, 0},
	{CRYPTO_RK_SM4_CBC,     "cbc-sm4-rk",      0, 0},
	{CRYPTO_RK_SM4_CFB,     "cfb-sm4-rk",      0, 0},
	{CRYPTO_RK_SM4_OFB,     "ofb-sm4-rk",      0, 0},
	{CRYPTO_RK_SM4_CTS,     "cts-sm4-rk",      0, 0},
	{CRYPTO_RK_SM4_CTR,     "ctr-sm4-rk",      1, 0},
	{CRYPTO_RK_SM4_XTS,     "xts-sm4-rk",      0, 0},
	{CRYPTO_RK_SM4_CCM,     "ccm-sm4-rk",      1, 1},
	{CRYPTO_RK_SM4_GCM,     "gcm-sm4-rk",      1, 1},
	{CRYPTO_RK_AES_ECB,     "ecb-aes-rk",      0, 0},
	{CRYPTO_RK_AES_CBC,     "cbc-aes-rk",      0, 0},
	{CRYPTO_RK_AES_CFB,     "cfb-aes-rk",      0, 0},
	{CRYPTO_RK_AES_OFB,     "ofb-aes-rk",      0, 0},
	{CRYPTO_RK_AES_CTS,     "cts-aes-rk",      0, 0},
	{CRYPTO_RK_AES_CTR,     "ctr-aes-rk",      1, 0},
	{CRYPTO_RK_AES_XTS,     "xts-aes-rk",      0, 0},
	{CRYPTO_RK_AES_CCM,     "ccm-aes-rk",      1, 1},
	{CRYPTO_RK_AES_GCM,     "gcm-aes-rk",      1, 1},
};

static const struct hash_algo_name_map h_algo_map_tbl[] = {

	{CRYPTO_RK_MD5,         "md5-rk",         0},
	{CRYPTO_RK_SHA1,        "sha1-rk",        0},
	{CRYPTO_RK_SHA224,      "sha224-rk",      0},
	{CRYPTO_RK_SHA256,      "sha256-rk",      0},
	{CRYPTO_RK_SHA384,      "sha384-rk",      0},
	{CRYPTO_RK_SHA512,      "sha512-rk",      0},
	{CRYPTO_RK_SHA512_224,  "sha512_224-rk",  0},
	{CRYPTO_RK_SHA512_256,  "sha512_256-rk",  0},
	{CRYPTO_RK_SM3,         "sm3-rk",         0},
	{CRYPTO_RK_MD5_HMAC,    "hmac-md5-rk",    1},
	{CRYPTO_RK_SHA1_HMAC,   "hmac-sha1-rk",   1},
	{CRYPTO_RK_SHA256_HMAC, "hmac-sha256-rk", 1},
	{CRYPTO_RK_SHA512_HMAC, "hmac-sha512-rk", 1},
	{CRYPTO_RK_SM3_HMAC,    "hmac-sm3-rk",    1},
	{CRYPTO_RK_SM4_CMAC,    "cmac-sm4-rk",    1},
	{CRYPTO_RK_SM4_CBC_MAC, "cbcmac-sm4-rk",  1},
	{CRYPTO_RK_AES_CMAC,    "cmac-aes-rk",    1},
	{CRYPTO_RK_AES_CBC_MAC, "cbcmac-aes-rk",  1},
};

const char *rk_get_cipher_name(uint32_t id, int *is_stream, int *is_aead)
{
	uint32_t i;

	*is_stream  = 0;
	*is_aead    = 0;

	for (i = 0; i < ARRAY_SIZE(c_algo_map_tbl); i++) {
		if (id == c_algo_map_tbl[i].id) {
			*is_stream = c_algo_map_tbl[i].is_stream;
			*is_aead   = c_algo_map_tbl[i].is_aead;
			return c_algo_map_tbl[i].name;
		}
	}

	return NULL;
}

const char *rk_get_hash_name(uint32_t id, int *is_hmac)
{
	uint32_t i;

	*is_hmac    = 0;

	for (i = 0; i < ARRAY_SIZE(h_algo_map_tbl); i++) {
		if (id == h_algo_map_tbl[i].id) {
			*is_hmac = h_algo_map_tbl[i].is_hmac;
			return h_algo_map_tbl[i].name;
		}
	}

	return NULL;
}

bool rk_cryptodev_multi_thread(const char *name)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(g_dev_infos); i++) {
		if (g_dev_infos[i].dev)
			return g_dev_infos[i].is_multi_thread;
	}

	return false;
}
