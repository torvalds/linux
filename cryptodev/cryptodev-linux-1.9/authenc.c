/*
 * Driver for /dev/crypto device (aka CryptoDev)
 *
 * Copyright (c) 2011, 2012 OpenSSL Software Foundation, Inc.
 *
 * Author: Nikos Mavrogiannopoulos
 *
 * This file is part of linux cryptodev.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * This file handles the AEAD part of /dev/crypto.
 *
 */

#include <crypto/hash.h>
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/ioctl.h>
#include <linux/random.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <crypto/cryptodev.h>
#include <crypto/scatterwalk.h>
#include <linux/scatterlist.h>
#include "cryptodev_int.h"
#include "zc.h"
#include "util.h"
#include "cryptlib.h"
#include "version.h"


/* make caop->dst available in scatterlist.
 * (caop->src is assumed to be equal to caop->dst)
 */
static int get_userbuf_tls(struct csession *ses, struct kernel_crypt_auth_op *kcaop,
			struct scatterlist **dst_sg)
{
	int pagecount = 0;
	struct crypt_auth_op *caop = &kcaop->caop;
	int rc;

	if (caop->dst == NULL)
		return -EINVAL;

	if (ses->alignmask) {
		if (!IS_ALIGNED((unsigned long)caop->dst, ses->alignmask))
			dwarning(2, "careful - source address %p is not %d byte aligned",
					caop->dst, ses->alignmask + 1);
	}

	if (kcaop->dst_len == 0) {
		dwarning(1, "Destination length cannot be zero");
		return -EINVAL;
	}

	pagecount = PAGECOUNT(caop->dst, kcaop->dst_len);

	ses->used_pages = pagecount;
	ses->readonly_pages = 0;

	rc = adjust_sg_array(ses, pagecount);
	if (rc)
		return rc;

	rc = __get_userbuf(caop->dst, kcaop->dst_len, 1, pagecount,
	                   ses->pages, ses->sg, kcaop->task, kcaop->mm);
	if (unlikely(rc)) {
		derr(1, "failed to get user pages for data input");
		return -EINVAL;
	}

	(*dst_sg) = ses->sg;

	return 0;
}


#define MAX_SRTP_AUTH_DATA_DIFF 256

/* Makes caop->auth_src available as scatterlist.
 * It also provides a pointer to caop->dst, which however,
 * is assumed to be within the caop->auth_src buffer. If not
 * (if their difference exceeds MAX_SRTP_AUTH_DATA_DIFF) it
 * returns error.
 */
static int get_userbuf_srtp(struct csession *ses, struct kernel_crypt_auth_op *kcaop,
			struct scatterlist **auth_sg, struct scatterlist **dst_sg)
{
	int pagecount, diff;
	int auth_pagecount = 0;
	struct crypt_auth_op *caop = &kcaop->caop;
	int rc;

	if (caop->dst == NULL && caop->auth_src == NULL) {
		derr(1, "dst and auth_src cannot be both null");
		return -EINVAL;
	}

	if (ses->alignmask) {
		if (!IS_ALIGNED((unsigned long)caop->dst, ses->alignmask))
			dwarning(2, "careful - source address %p is not %d byte aligned",
					caop->dst, ses->alignmask + 1);
		if (!IS_ALIGNED((unsigned long)caop->auth_src, ses->alignmask))
			dwarning(2, "careful - source address %p is not %d byte aligned",
					caop->auth_src, ses->alignmask + 1);
	}

	if (unlikely(kcaop->dst_len == 0 || caop->auth_len == 0)) {
		dwarning(1, "Destination length cannot be zero");
		return -EINVAL;
	}

	/* Note that in SRTP auth data overlap with data to be encrypted (dst)
         */

	auth_pagecount = PAGECOUNT(caop->auth_src, caop->auth_len);
	diff = (int)(caop->src - caop->auth_src);
	if (diff > MAX_SRTP_AUTH_DATA_DIFF || diff < 0) {
		dwarning(1, "auth_src must overlap with src (diff: %d).", diff);
		return -EINVAL;
	}

	pagecount = auth_pagecount;

	rc = adjust_sg_array(ses, pagecount*2); /* double pages to have pages for dst(=auth_src) */
	if (rc) {
		derr(1, "cannot adjust sg array");
		return rc;
	}

	rc = __get_userbuf(caop->auth_src, caop->auth_len, 1, auth_pagecount,
			   ses->pages, ses->sg, kcaop->task, kcaop->mm);
	if (unlikely(rc)) {
		derr(1, "failed to get user pages for data input");
		return -EINVAL;
	}

	ses->used_pages = pagecount;
	ses->readonly_pages = 0;

	(*auth_sg) = ses->sg;

	(*dst_sg) = ses->sg + auth_pagecount;
	sg_init_table(*dst_sg, auth_pagecount);
	sg_copy(ses->sg, (*dst_sg), caop->auth_len);
	(*dst_sg) = sg_advance(*dst_sg, diff);
	if (*dst_sg == NULL) {
		release_user_pages(ses);
		derr(1, "failed to get enough pages for auth data");
		return -EINVAL;
	}

	return 0;
}

/*
 * Return tag (digest) length for authenticated encryption
 * If the cipher and digest are separate, hdata.init is set - just return
 * digest length. Otherwise return digest length for aead ciphers
 */
static int cryptodev_get_tag_len(struct csession *ses_ptr)
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
static int cryptodev_get_dst_len(struct crypt_auth_op *caop, struct csession *ses_ptr)
{
	int dst_len = caop->len;
	if (caop->op == COP_DECRYPT)
		return dst_len;

	dst_len += caop->tag_len;

	/* for TLS always add some padding so the total length is rounded to
	 * cipher block size */
	if (caop->flags & COP_FLAG_AEAD_TLS_TYPE) {
		int bs = ses_ptr->cdata.blocksize;
		dst_len += bs - (dst_len % bs);
	}

	return dst_len;
}

static int fill_kcaop_from_caop(struct kernel_crypt_auth_op *kcaop, struct fcrypt *fcr)
{
	struct crypt_auth_op *caop = &kcaop->caop;
	struct csession *ses_ptr;
	int ret;

	/* this also enters ses_ptr->sem */
	ses_ptr = crypto_get_session_by_sid(fcr, caop->ses);
	if (unlikely(!ses_ptr)) {
		derr(1, "invalid session ID=0x%08X", caop->ses);
		return -EINVAL;
	}

	if (caop->flags & COP_FLAG_AEAD_TLS_TYPE || caop->flags & COP_FLAG_AEAD_SRTP_TYPE) {
		if (caop->src != caop->dst) {
			derr(1, "Non-inplace encryption and decryption is not efficient and not implemented");
			ret = -EINVAL;
			goto out_unlock;
		}
	}

	if (caop->tag_len == 0)
		caop->tag_len = cryptodev_get_tag_len(ses_ptr);

	kcaop->ivlen = caop->iv ? ses_ptr->cdata.ivsize : 0;
	kcaop->dst_len = cryptodev_get_dst_len(caop, ses_ptr);
	kcaop->task = current;
	kcaop->mm = current->mm;

	if (caop->iv) {
		ret = copy_from_user(kcaop->iv, caop->iv, kcaop->ivlen);
		if (unlikely(ret)) {
			derr(1, "error copying IV (%d bytes), copy_from_user returned %d for address %p",
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

static int fill_caop_from_kcaop(struct kernel_crypt_auth_op *kcaop, struct fcrypt *fcr)
{
	int ret;

	kcaop->caop.len = kcaop->dst_len;

	if (kcaop->ivlen && kcaop->caop.flags & COP_FLAG_WRITE_IV) {
		ret = copy_to_user(kcaop->caop.iv,
				kcaop->iv, kcaop->ivlen);
		if (unlikely(ret)) {
			derr(1, "Error in copying to userspace");
			return -EFAULT;
		}
	}
	return 0;
}


int kcaop_from_user(struct kernel_crypt_auth_op *kcaop,
			struct fcrypt *fcr, void __user *arg)
{
	if (unlikely(copy_from_user(&kcaop->caop, arg, sizeof(kcaop->caop)))) {
		derr(1, "Error in copying from userspace");
		return -EFAULT;
	}

	return fill_kcaop_from_caop(kcaop, fcr);
}

int kcaop_to_user(struct kernel_crypt_auth_op *kcaop,
		struct fcrypt *fcr, void __user *arg)
{
	int ret;

	ret = fill_caop_from_kcaop(kcaop, fcr);
	if (unlikely(ret)) {
		derr(1, "fill_caop_from_kcaop");
		return ret;
	}

	if (unlikely(copy_to_user(arg, &kcaop->caop, sizeof(kcaop->caop)))) {
		derr(1, "Error in copying to userspace");
		return -EFAULT;
	}
	return 0;
}

static void copy_tls_hash(struct scatterlist *dst_sg, int len, void *hash, int hash_len)
{
	scatterwalk_map_and_copy(hash, dst_sg, len, hash_len, 1);
}

static void read_tls_hash(struct scatterlist *dst_sg, int len, void *hash, int hash_len)
{
	scatterwalk_map_and_copy(hash, dst_sg, len - hash_len, hash_len, 0);
}

static int pad_record(struct scatterlist *dst_sg, int len, int block_size)
{
	uint8_t pad[block_size];
	int pad_size = block_size - (len % block_size);

	memset(pad, pad_size - 1, pad_size);

	scatterwalk_map_and_copy(pad, dst_sg, len, pad_size, 1);

	return pad_size;
}

static int verify_tls_record_pad(struct scatterlist *dst_sg, int len, int block_size)
{
	uint8_t pad[256]; /* the maximum allowed */
	uint8_t pad_size;
	int i;

	scatterwalk_map_and_copy(&pad_size, dst_sg, len - 1, 1, 0);

	if (pad_size + 1 > len) {
		derr(1, "Pad size: %d", pad_size);
		return -EBADMSG;
	}

	scatterwalk_map_and_copy(pad, dst_sg, len - pad_size - 1, pad_size + 1, 0);

	for (i = 0; i < pad_size; i++)
		if (pad[i] != pad_size) {
			derr(1, "Pad size: %u, pad: %d", pad_size, pad[i]);
			return -EBADMSG;
		}

	return pad_size + 1;
}

/* Authenticate and encrypt the TLS way (also perform padding).
 * During decryption it verifies the pad and tag and returns -EBADMSG on error.
 */
static int
tls_auth_n_crypt(struct csession *ses_ptr, struct kernel_crypt_auth_op *kcaop,
		 struct scatterlist *auth_sg, uint32_t auth_len,
		 struct scatterlist *dst_sg, uint32_t len)
{
	int ret, fail = 0;
	struct crypt_auth_op *caop = &kcaop->caop;
	uint8_t vhash[AALG_MAX_RESULT_LEN];
	uint8_t hash_output[AALG_MAX_RESULT_LEN];

	/* TLS authenticates the plaintext except for the padding.
	 */
	if (caop->op == COP_ENCRYPT) {
		if (ses_ptr->hdata.init != 0) {
			if (auth_len > 0) {
				ret = cryptodev_hash_update(&ses_ptr->hdata,
								auth_sg, auth_len);
				if (unlikely(ret)) {
					derr(0, "cryptodev_hash_update: %d", ret);
					return ret;
				}
			}

			if (len > 0) {
				ret = cryptodev_hash_update(&ses_ptr->hdata,
								dst_sg, len);
				if (unlikely(ret)) {
					derr(0, "cryptodev_hash_update: %d", ret);
					return ret;
				}
			}

			ret = cryptodev_hash_final(&ses_ptr->hdata, hash_output);
			if (unlikely(ret)) {
				derr(0, "cryptodev_hash_final: %d", ret);
				return ret;
			}

			copy_tls_hash(dst_sg, len, hash_output, caop->tag_len);
			len += caop->tag_len;
		}

		if (ses_ptr->cdata.init != 0) {
			if (ses_ptr->cdata.blocksize > 1) {
				ret = pad_record(dst_sg, len, ses_ptr->cdata.blocksize);
				len += ret;
			}

			ret = cryptodev_cipher_encrypt(&ses_ptr->cdata,
							dst_sg, dst_sg, len);
			if (unlikely(ret)) {
				derr(0, "cryptodev_cipher_encrypt: %d", ret);
				return ret;
			}
		}
	} else {
		if (ses_ptr->cdata.init != 0) {
			ret = cryptodev_cipher_decrypt(&ses_ptr->cdata,
							dst_sg, dst_sg, len);

			if (unlikely(ret)) {
				derr(0, "cryptodev_cipher_decrypt: %d", ret);
				return ret;
			}

			if (ses_ptr->cdata.blocksize > 1) {
				ret = verify_tls_record_pad(dst_sg, len, ses_ptr->cdata.blocksize);
				if (unlikely(ret < 0)) {
					derr(2, "verify_record_pad: %d", ret);
					fail = 1;
				} else {
					len -= ret;
				}
			}
		}

		if (ses_ptr->hdata.init != 0) {
			if (unlikely(caop->tag_len > sizeof(vhash) || caop->tag_len > len)) {
				derr(1, "Illegal tag len size");
				return -EINVAL;
			}

			read_tls_hash(dst_sg, len, vhash, caop->tag_len);
			len -= caop->tag_len;

			if (auth_len > 0) {
				ret = cryptodev_hash_update(&ses_ptr->hdata,
								auth_sg, auth_len);
				if (unlikely(ret)) {
					derr(0, "cryptodev_hash_update: %d", ret);
					return ret;
				}
			}

			if (len > 0) {
				ret = cryptodev_hash_update(&ses_ptr->hdata,
									dst_sg, len);
				if (unlikely(ret)) {
					derr(0, "cryptodev_hash_update: %d", ret);
					return ret;
				}
			}

			ret = cryptodev_hash_final(&ses_ptr->hdata, hash_output);
			if (unlikely(ret)) {
				derr(0, "cryptodev_hash_final: %d", ret);
				return ret;
			}

			if (memcmp(vhash, hash_output, caop->tag_len) != 0 || fail != 0) {
				derr(2, "MAC verification failed (tag_len: %d)", caop->tag_len);
				return -EBADMSG;
			}
		}
	}
	kcaop->dst_len = len;
	return 0;
}

/* Authenticate and encrypt the SRTP way. During decryption
 * it verifies the tag and returns -EBADMSG on error.
 */
static int
srtp_auth_n_crypt(struct csession *ses_ptr, struct kernel_crypt_auth_op *kcaop,
		  struct scatterlist *auth_sg, uint32_t auth_len,
		  struct scatterlist *dst_sg, uint32_t len)
{
	int ret, fail = 0;
	struct crypt_auth_op *caop = &kcaop->caop;
	uint8_t vhash[AALG_MAX_RESULT_LEN];
	uint8_t hash_output[AALG_MAX_RESULT_LEN];

	/* SRTP authenticates the encrypted data.
	 */
	if (caop->op == COP_ENCRYPT) {
		if (ses_ptr->cdata.init != 0) {
			ret = cryptodev_cipher_encrypt(&ses_ptr->cdata,
							dst_sg, dst_sg, len);
			if (unlikely(ret)) {
				derr(0, "cryptodev_cipher_encrypt: %d", ret);
				return ret;
			}
		}

		if (ses_ptr->hdata.init != 0) {
			if (auth_len > 0) {
				ret = cryptodev_hash_update(&ses_ptr->hdata,
								auth_sg, auth_len);
				if (unlikely(ret)) {
					derr(0, "cryptodev_hash_update: %d", ret);
					return ret;
				}
			}

			ret = cryptodev_hash_final(&ses_ptr->hdata, hash_output);
			if (unlikely(ret)) {
				derr(0, "cryptodev_hash_final: %d", ret);
				return ret;
			}

			if (unlikely(copy_to_user(caop->tag, hash_output, caop->tag_len)))
				return -EFAULT;
		}

	} else {
		if (ses_ptr->hdata.init != 0) {
			if (unlikely(caop->tag_len > sizeof(vhash) || caop->tag_len > len)) {
				derr(1, "Illegal tag len size");
				return -EINVAL;
			}

			if (unlikely(copy_from_user(vhash, caop->tag, caop->tag_len)))
				return -EFAULT;

			ret = cryptodev_hash_update(&ses_ptr->hdata,
							auth_sg, auth_len);
			if (unlikely(ret)) {
				derr(0, "cryptodev_hash_update: %d", ret);
				return ret;
			}

			ret = cryptodev_hash_final(&ses_ptr->hdata, hash_output);
			if (unlikely(ret)) {
				derr(0, "cryptodev_hash_final: %d", ret);
				return ret;
			}

			if (memcmp(vhash, hash_output, caop->tag_len) != 0 || fail != 0) {
				derr(2, "MAC verification failed");
				return -EBADMSG;
			}
		}

		if (ses_ptr->cdata.init != 0) {
			ret = cryptodev_cipher_decrypt(&ses_ptr->cdata,
							dst_sg, dst_sg, len);

			if (unlikely(ret)) {
				derr(0, "cryptodev_cipher_decrypt: %d", ret);
				return ret;
			}
		}

	}
	kcaop->dst_len = len;
	return 0;
}

/* Typical AEAD (i.e. GCM) encryption/decryption.
 * During decryption the tag is verified.
 */
static int
auth_n_crypt(struct csession *ses_ptr, struct kernel_crypt_auth_op *kcaop,
		  struct scatterlist *auth_sg, uint32_t auth_len,
		  struct scatterlist *src_sg,
		  struct scatterlist *dst_sg, uint32_t len)
{
	int ret;
	struct crypt_auth_op *caop = &kcaop->caop;
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
		kcaop->dst_len = len + caop->tag_len;
		caop->tag = caop->dst + len;
	} else {
		ret = cryptodev_cipher_decrypt(&ses_ptr->cdata,
						src_sg, dst_sg, len);

		if (unlikely(ret)) {
			derr(0, "cryptodev_cipher_decrypt: %d", ret);
			return ret;
		}
		kcaop->dst_len = len - caop->tag_len;
		caop->tag = caop->dst + len - caop->tag_len;
	}

	return 0;
}

/* This is the main crypto function - zero-copy edition */
static int
__crypto_auth_run_zc(struct csession *ses_ptr, struct kernel_crypt_auth_op *kcaop)
{
	struct scatterlist *dst_sg, *auth_sg, *src_sg;
	struct crypt_auth_op *caop = &kcaop->caop;
	int ret = 0;

	if (caop->flags & COP_FLAG_AEAD_SRTP_TYPE) {
		if (unlikely(ses_ptr->cdata.init != 0 &&
		             (ses_ptr->cdata.stream == 0 ||
			      ses_ptr->cdata.aead != 0))) {
			derr(0, "Only stream modes are allowed in SRTP mode (but not AEAD)");
			return -EINVAL;
		}

		ret = get_userbuf_srtp(ses_ptr, kcaop, &auth_sg, &dst_sg);
		if (unlikely(ret)) {
			derr(1, "get_userbuf_srtp(): Error getting user pages.");
			return ret;
		}

		ret = srtp_auth_n_crypt(ses_ptr, kcaop, auth_sg, caop->auth_len,
			   dst_sg, caop->len);

		release_user_pages(ses_ptr);
	} else { /* TLS and normal cases. Here auth data are usually small
	          * so we just copy them to a free page, instead of trying
	          * to map them.
	          */
		unsigned char *auth_buf = NULL;
		struct scatterlist tmp;

		if (unlikely(caop->auth_len > PAGE_SIZE)) {
			derr(1, "auth data len is excessive.");
			return -EINVAL;
		}

		auth_buf = (char *)__get_free_page(GFP_KERNEL);
		if (unlikely(!auth_buf)) {
			derr(1, "unable to get a free page.");
			return -ENOMEM;
		}

		if (caop->auth_src && caop->auth_len > 0) {
			if (unlikely(copy_from_user(auth_buf, caop->auth_src, caop->auth_len))) {
				derr(1, "unable to copy auth data from userspace.");
				ret = -EFAULT;
				goto free_auth_buf;
			}

			sg_init_one(&tmp, auth_buf, caop->auth_len);
			auth_sg = &tmp;
		} else {
			auth_sg = NULL;
		}

		if (caop->flags & COP_FLAG_AEAD_TLS_TYPE && ses_ptr->cdata.aead == 0) {
			ret = get_userbuf_tls(ses_ptr, kcaop, &dst_sg);
			if (unlikely(ret)) {
				derr(1, "get_userbuf_tls(): Error getting user pages.");
				goto free_auth_buf;
			}

			ret = tls_auth_n_crypt(ses_ptr, kcaop, auth_sg, caop->auth_len,
				   dst_sg, caop->len);
		} else {
			if (unlikely(ses_ptr->cdata.init == 0 ||
			             (ses_ptr->cdata.stream == 0 &&
				      ses_ptr->cdata.aead == 0))) {
				derr(0, "Only stream and AEAD ciphers are allowed for authenc");
				ret = -EINVAL;
				goto free_auth_buf;
			}

			ret = get_userbuf(ses_ptr, caop->src, caop->len, caop->dst, kcaop->dst_len,
					  kcaop->task, kcaop->mm, &src_sg, &dst_sg);
			if (unlikely(ret)) {
				derr(1, "get_userbuf(): Error getting user pages.");
				goto free_auth_buf;
			}

			ret = auth_n_crypt(ses_ptr, kcaop, auth_sg, caop->auth_len,
					   src_sg, dst_sg, caop->len);
		}

		release_user_pages(ses_ptr);

free_auth_buf:
		free_page((unsigned long)auth_buf);
	}

	return ret;
}


int crypto_auth_run(struct fcrypt *fcr, struct kernel_crypt_auth_op *kcaop)
{
	struct csession *ses_ptr;
	struct crypt_auth_op *caop = &kcaop->caop;
	int ret;

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

	ret = __crypto_auth_run_zc(ses_ptr, kcaop);
	if (unlikely(ret)) {
		derr(1, "error in __crypto_auth_run_zc()");
		goto out_unlock;
	}

	ret = 0;

	cryptodev_cipher_get_iv(&ses_ptr->cdata, kcaop->iv,
				min(ses_ptr->cdata.ivsize, kcaop->ivlen));

out_unlock:
	crypto_put_session(ses_ptr);
	return ret;
}
