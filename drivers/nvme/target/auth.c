// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe over Fabrics DH-HMAC-CHAP authentication.
 * Copyright (c) 2020 Hannes Reinecke, SUSE Software Solutions.
 * All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <crypto/hash.h>
#include <linux/crc32.h>
#include <linux/base64.h>
#include <linux/ctype.h>
#include <linux/random.h>
#include <linux/nvme-auth.h>
#include <asm/unaligned.h>

#include "nvmet.h"

int nvmet_auth_set_key(struct nvmet_host *host, const char *secret,
		       bool set_ctrl)
{
	unsigned char key_hash;
	char *dhchap_secret;

	if (sscanf(secret, "DHHC-1:%hhd:%*s", &key_hash) != 1)
		return -EINVAL;
	if (key_hash > 3) {
		pr_warn("Invalid DH-HMAC-CHAP hash id %d\n",
			 key_hash);
		return -EINVAL;
	}
	if (key_hash > 0) {
		/* Validate selected hash algorithm */
		const char *hmac = nvme_auth_hmac_name(key_hash);

		if (!crypto_has_shash(hmac, 0, 0)) {
			pr_err("DH-HMAC-CHAP hash %s unsupported\n", hmac);
			return -ENOTSUPP;
		}
	}
	dhchap_secret = kstrdup(secret, GFP_KERNEL);
	if (!dhchap_secret)
		return -ENOMEM;
	if (set_ctrl) {
		host->dhchap_ctrl_secret = strim(dhchap_secret);
		host->dhchap_ctrl_key_hash = key_hash;
	} else {
		host->dhchap_secret = strim(dhchap_secret);
		host->dhchap_key_hash = key_hash;
	}
	return 0;
}

int nvmet_setup_auth(struct nvmet_ctrl *ctrl)
{
	int ret = 0;
	struct nvmet_host_link *p;
	struct nvmet_host *host = NULL;
	const char *hash_name;

	down_read(&nvmet_config_sem);
	if (nvmet_is_disc_subsys(ctrl->subsys))
		goto out_unlock;

	if (ctrl->subsys->allow_any_host)
		goto out_unlock;

	list_for_each_entry(p, &ctrl->subsys->hosts, entry) {
		pr_debug("check %s\n", nvmet_host_name(p->host));
		if (strcmp(nvmet_host_name(p->host), ctrl->hostnqn))
			continue;
		host = p->host;
		break;
	}
	if (!host) {
		pr_debug("host %s not found\n", ctrl->hostnqn);
		ret = -EPERM;
		goto out_unlock;
	}

	if (!host->dhchap_secret) {
		pr_debug("No authentication provided\n");
		goto out_unlock;
	}

	if (host->dhchap_hash_id == ctrl->shash_id) {
		pr_debug("Re-use existing hash ID %d\n",
			 ctrl->shash_id);
	} else {
		hash_name = nvme_auth_hmac_name(host->dhchap_hash_id);
		if (!hash_name) {
			pr_warn("Hash ID %d invalid\n", host->dhchap_hash_id);
			ret = -EINVAL;
			goto out_unlock;
		}
		ctrl->shash_id = host->dhchap_hash_id;
	}

	/* Skip the 'DHHC-1:XX:' prefix */
	nvme_auth_free_key(ctrl->host_key);
	ctrl->host_key = nvme_auth_extract_key(host->dhchap_secret + 10,
					       host->dhchap_key_hash);
	if (IS_ERR(ctrl->host_key)) {
		ret = PTR_ERR(ctrl->host_key);
		ctrl->host_key = NULL;
		goto out_free_hash;
	}
	pr_debug("%s: using hash %s key %*ph\n", __func__,
		 ctrl->host_key->hash > 0 ?
		 nvme_auth_hmac_name(ctrl->host_key->hash) : "none",
		 (int)ctrl->host_key->len, ctrl->host_key->key);

	nvme_auth_free_key(ctrl->ctrl_key);
	if (!host->dhchap_ctrl_secret) {
		ctrl->ctrl_key = NULL;
		goto out_unlock;
	}

	ctrl->ctrl_key = nvme_auth_extract_key(host->dhchap_ctrl_secret + 10,
					       host->dhchap_ctrl_key_hash);
	if (IS_ERR(ctrl->ctrl_key)) {
		ret = PTR_ERR(ctrl->ctrl_key);
		ctrl->ctrl_key = NULL;
	}
	pr_debug("%s: using ctrl hash %s key %*ph\n", __func__,
		 ctrl->ctrl_key->hash > 0 ?
		 nvme_auth_hmac_name(ctrl->ctrl_key->hash) : "none",
		 (int)ctrl->ctrl_key->len, ctrl->ctrl_key->key);

out_free_hash:
	if (ret) {
		if (ctrl->host_key) {
			nvme_auth_free_key(ctrl->host_key);
			ctrl->host_key = NULL;
		}
		ctrl->shash_id = 0;
	}
out_unlock:
	up_read(&nvmet_config_sem);

	return ret;
}

void nvmet_auth_sq_free(struct nvmet_sq *sq)
{
	kfree(sq->dhchap_c1);
	sq->dhchap_c1 = NULL;
	kfree(sq->dhchap_c2);
	sq->dhchap_c2 = NULL;
	kfree(sq->dhchap_skey);
	sq->dhchap_skey = NULL;
}

void nvmet_destroy_auth(struct nvmet_ctrl *ctrl)
{
	ctrl->shash_id = 0;

	if (ctrl->host_key) {
		nvme_auth_free_key(ctrl->host_key);
		ctrl->host_key = NULL;
	}
	if (ctrl->ctrl_key) {
		nvme_auth_free_key(ctrl->ctrl_key);
		ctrl->ctrl_key = NULL;
	}
}

bool nvmet_check_auth_status(struct nvmet_req *req)
{
	if (req->sq->ctrl->host_key &&
	    !req->sq->authenticated)
		return false;
	return true;
}

int nvmet_auth_host_hash(struct nvmet_req *req, u8 *response,
			 unsigned int shash_len)
{
	struct crypto_shash *shash_tfm;
	struct shash_desc *shash;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	const char *hash_name;
	u8 *challenge = req->sq->dhchap_c1, *host_response;
	u8 buf[4];
	int ret;

	hash_name = nvme_auth_hmac_name(ctrl->shash_id);
	if (!hash_name) {
		pr_warn("Hash ID %d invalid\n", ctrl->shash_id);
		return -EINVAL;
	}

	shash_tfm = crypto_alloc_shash(hash_name, 0, 0);
	if (IS_ERR(shash_tfm)) {
		pr_err("failed to allocate shash %s\n", hash_name);
		return PTR_ERR(shash_tfm);
	}

	if (shash_len != crypto_shash_digestsize(shash_tfm)) {
		pr_debug("%s: hash len mismatch (len %d digest %d)\n",
			 __func__, shash_len,
			 crypto_shash_digestsize(shash_tfm));
		ret = -EINVAL;
		goto out_free_tfm;
	}

	host_response = nvme_auth_transform_key(ctrl->host_key, ctrl->hostnqn);
	if (IS_ERR(host_response)) {
		ret = PTR_ERR(host_response);
		goto out_free_tfm;
	}

	ret = crypto_shash_setkey(shash_tfm, host_response,
				  ctrl->host_key->len);
	if (ret)
		goto out_free_response;

	pr_debug("ctrl %d qid %d host response seq %u transaction %d\n",
		 ctrl->cntlid, req->sq->qid, req->sq->dhchap_s1,
		 req->sq->dhchap_tid);

	shash = kzalloc(sizeof(*shash) + crypto_shash_descsize(shash_tfm),
			GFP_KERNEL);
	if (!shash) {
		ret = -ENOMEM;
		goto out_free_response;
	}
	shash->tfm = shash_tfm;
	ret = crypto_shash_init(shash);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, challenge, shash_len);
	if (ret)
		goto out;
	put_unaligned_le32(req->sq->dhchap_s1, buf);
	ret = crypto_shash_update(shash, buf, 4);
	if (ret)
		goto out;
	put_unaligned_le16(req->sq->dhchap_tid, buf);
	ret = crypto_shash_update(shash, buf, 2);
	if (ret)
		goto out;
	memset(buf, 0, 4);
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, "HostHost", 8);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->hostnqn, strlen(ctrl->hostnqn));
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->subsysnqn,
				  strlen(ctrl->subsysnqn));
	if (ret)
		goto out;
	ret = crypto_shash_final(shash, response);
out:
	if (challenge != req->sq->dhchap_c1)
		kfree(challenge);
	kfree(shash);
out_free_response:
	kfree_sensitive(host_response);
out_free_tfm:
	crypto_free_shash(shash_tfm);
	return 0;
}

int nvmet_auth_ctrl_hash(struct nvmet_req *req, u8 *response,
			 unsigned int shash_len)
{
	struct crypto_shash *shash_tfm;
	struct shash_desc *shash;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	const char *hash_name;
	u8 *challenge = req->sq->dhchap_c2, *ctrl_response;
	u8 buf[4];
	int ret;

	hash_name = nvme_auth_hmac_name(ctrl->shash_id);
	if (!hash_name) {
		pr_warn("Hash ID %d invalid\n", ctrl->shash_id);
		return -EINVAL;
	}

	shash_tfm = crypto_alloc_shash(hash_name, 0, 0);
	if (IS_ERR(shash_tfm)) {
		pr_err("failed to allocate shash %s\n", hash_name);
		return PTR_ERR(shash_tfm);
	}

	if (shash_len != crypto_shash_digestsize(shash_tfm)) {
		pr_debug("%s: hash len mismatch (len %d digest %d)\n",
			 __func__, shash_len,
			 crypto_shash_digestsize(shash_tfm));
		ret = -EINVAL;
		goto out_free_tfm;
	}

	ctrl_response = nvme_auth_transform_key(ctrl->ctrl_key,
						ctrl->subsysnqn);
	if (IS_ERR(ctrl_response)) {
		ret = PTR_ERR(ctrl_response);
		goto out_free_tfm;
	}

	ret = crypto_shash_setkey(shash_tfm, ctrl_response,
				  ctrl->ctrl_key->len);
	if (ret)
		goto out_free_response;

	shash = kzalloc(sizeof(*shash) + crypto_shash_descsize(shash_tfm),
			GFP_KERNEL);
	if (!shash) {
		ret = -ENOMEM;
		goto out_free_response;
	}
	shash->tfm = shash_tfm;

	ret = crypto_shash_init(shash);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, challenge, shash_len);
	if (ret)
		goto out;
	put_unaligned_le32(req->sq->dhchap_s2, buf);
	ret = crypto_shash_update(shash, buf, 4);
	if (ret)
		goto out;
	put_unaligned_le16(req->sq->dhchap_tid, buf);
	ret = crypto_shash_update(shash, buf, 2);
	if (ret)
		goto out;
	memset(buf, 0, 4);
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, "Controller", 10);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->subsysnqn,
			    strlen(ctrl->subsysnqn));
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->hostnqn, strlen(ctrl->hostnqn));
	if (ret)
		goto out;
	ret = crypto_shash_final(shash, response);
out:
	if (challenge != req->sq->dhchap_c2)
		kfree(challenge);
	kfree(shash);
out_free_response:
	kfree_sensitive(ctrl_response);
out_free_tfm:
	crypto_free_shash(shash_tfm);
	return 0;
}
