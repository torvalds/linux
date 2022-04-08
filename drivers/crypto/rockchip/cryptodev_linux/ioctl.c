/*
 * Driver for /dev/crypto device (aka CryptoDev)
 *
 * Copyright (c) 2004 Michal Ludvig <mludvig@logix.net.nz>, SuSE Labs
 * Copyright (c) 2009,2010,2011 Nikos Mavrogiannopoulos <nmav@gnutls.org>
 * Copyright (c) 2010 Phil Sutter
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
 * Device /dev/crypto provides an interface for
 * accessing kernel CryptoAPI algorithms (ciphers,
 * hashes) from userspace programs.
 *
 * /dev/crypto interface was originally introduced in
 * OpenBSD and this module attempts to keep the API.
 *
 */

#include <crypto/hash.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/ioctl.h>
#include <linux/random.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/scatterlist.h>
#include <linux/rtnetlink.h>
#include <crypto/authenc.h>

#include <linux/sysctl.h>

#include "cryptodev.h"
#include "zc.h"
#include "version.h"
#include "cipherapi.h"

#include "rk_cryptodev.h"

MODULE_AUTHOR("Nikos Mavrogiannopoulos <nmav@gnutls.org>");
MODULE_DESCRIPTION("CryptoDev driver");
MODULE_LICENSE("GPL");

/* ====== Compile-time config ====== */

/* Default (pre-allocated) and maximum size of the job queue.
 * These are free, pending and done items all together. */
#define DEF_COP_RINGSIZE 16
#define MAX_COP_RINGSIZE 64

/* ====== Module parameters ====== */

int cryptodev_verbosity;
module_param(cryptodev_verbosity, int, 0644);
MODULE_PARM_DESC(cryptodev_verbosity, "0: normal, 1: verbose, 2: debug");

/* ====== CryptoAPI ====== */
struct todo_list_item {
	struct list_head __hook;
	struct kernel_crypt_op kcop;
	int result;
};

struct locked_list {
	struct list_head list;
	struct mutex lock;
};

struct crypt_priv {
	struct fcrypt fcrypt;
	struct locked_list free, todo, done;
	int itemcount;
	struct work_struct cryptask;
	wait_queue_head_t user_waiter;
};

#define FILL_SG(sg, ptr, len)					\
	do {							\
		(sg)->page = virt_to_page(ptr);			\
		(sg)->offset = offset_in_page(ptr);		\
		(sg)->length = len;				\
		(sg)->dma_address = 0;				\
	} while (0)

/* cryptodev's own workqueue, keeps crypto tasks from disturbing the force */
static struct workqueue_struct *cryptodev_wq;
static atomic_t cryptodev_sess = ATOMIC_INIT(1);

/* Prepare session for future use. */
static int
crypto_create_session(struct fcrypt *fcr, struct session_op *sop)
{
	struct csession	*ses_new = NULL, *ses_ptr;
	int ret = 0;
	const char *alg_name = NULL;
	const char *hash_name = NULL;
	int hmac_mode = 1, stream = 0, aead = 0;
	/*
	 * With composite aead ciphers, only ckey is used and it can cover all the
	 * structure space; otherwise both keys may be used simultaneously but they
	 * are confined to their spaces
	 */
	struct {
		uint8_t ckey[CRYPTO_CIPHER_MAX_KEY_LEN];
		uint8_t mkey[CRYPTO_HMAC_MAX_KEY_LEN];
		/* padding space for aead keys */
		uint8_t pad[RTA_SPACE(sizeof(struct crypto_authenc_key_param))];
	} keys;

	/* Does the request make sense? */
	if (unlikely(!sop->cipher && !sop->mac)) {
		ddebug(1, "Both 'cipher' and 'mac' unset.");
		return -EINVAL;
	}

	switch (sop->cipher) {
	case 0:
		break;
	case CRYPTO_DES_CBC:
		alg_name = "cbc(des)";
		break;
	case CRYPTO_3DES_CBC:
		alg_name = "cbc(des3_ede)";
		break;
	case CRYPTO_BLF_CBC:
		alg_name = "cbc(blowfish)";
		break;
	case CRYPTO_AES_CBC:
		alg_name = "cbc(aes)";
		break;
	case CRYPTO_AES_ECB:
		alg_name = "ecb(aes)";
		break;
	case CRYPTO_AES_XTS:
		alg_name = "xts(aes)";
		break;
	case CRYPTO_CAMELLIA_CBC:
		alg_name = "cbc(camellia)";
		break;
	case CRYPTO_AES_CTR:
		alg_name = "ctr(aes)";
		stream = 1;
		break;
	case CRYPTO_AES_GCM:
		alg_name = "gcm(aes)";
		stream = 1;
		aead = 1;
		break;
	case CRYPTO_TLS11_AES_CBC_HMAC_SHA1:
		alg_name = "tls11(hmac(sha1),cbc(aes))";
		stream = 0;
		aead = 1;
		break;
	case CRYPTO_TLS12_AES_CBC_HMAC_SHA256:
		alg_name = "tls12(hmac(sha256),cbc(aes))";
		stream = 0;
		aead = 1;
		break;
	case CRYPTO_NULL:
		alg_name = "ecb(cipher_null)";
		stream = 1;
		break;
	default:
		alg_name = rk_get_cipher_name(sop->cipher, &stream, &aead);
		if (!alg_name) {
			ddebug(1, "bad cipher: %d", sop->cipher);
			return -EINVAL;
		}
		break;
	}

	switch (sop->mac) {
	case 0:
		break;
	case CRYPTO_MD5_HMAC:
		hash_name = "hmac(md5)";
		break;
	case CRYPTO_RIPEMD160_HMAC:
		hash_name = "hmac(rmd160)";
		break;
	case CRYPTO_SHA1_HMAC:
		hash_name = "hmac(sha1)";
		break;
	case CRYPTO_SHA2_224_HMAC:
		hash_name = "hmac(sha224)";
		break;

	case CRYPTO_SHA2_256_HMAC:
		hash_name = "hmac(sha256)";
		break;
	case CRYPTO_SHA2_384_HMAC:
		hash_name = "hmac(sha384)";
		break;
	case CRYPTO_SHA2_512_HMAC:
		hash_name = "hmac(sha512)";
		break;

	/* non-hmac cases */
	case CRYPTO_MD5:
		hash_name = "md5";
		hmac_mode = 0;
		break;
	case CRYPTO_RIPEMD160:
		hash_name = "rmd160";
		hmac_mode = 0;
		break;
	case CRYPTO_SHA1:
		hash_name = "sha1";
		hmac_mode = 0;
		break;
	case CRYPTO_SHA2_224:
		hash_name = "sha224";
		hmac_mode = 0;
		break;
	case CRYPTO_SHA2_256:
		hash_name = "sha256";
		hmac_mode = 0;
		break;
	case CRYPTO_SHA2_384:
		hash_name = "sha384";
		hmac_mode = 0;
		break;
	case CRYPTO_SHA2_512:
		hash_name = "sha512";
		hmac_mode = 0;
		break;
	default:
		hash_name = rk_get_hash_name(sop->mac, &hmac_mode);
		if (!hash_name) {
			ddebug(1, "bad mac: %d", sop->mac);
			return -EINVAL;
		}
		break;
	}

	/* Create a session and put it to the list. Zeroing the structure helps
	 * also with a single exit point in case of errors */
	ses_new = kzalloc(sizeof(*ses_new), GFP_KERNEL);
	if (!ses_new)
		return -ENOMEM;

	/* Set-up crypto transform. */
	if (alg_name) {
		unsigned int keylen;
		ret = cryptodev_get_cipher_keylen(&keylen, sop, aead);
		if (unlikely(ret < 0)) {
			ddebug(1, "Setting key failed for %s-%zu.",
				alg_name, (size_t)sop->keylen*8);
			goto session_error;
		}

		ret = cryptodev_get_cipher_key(keys.ckey, sop, aead);
		if (unlikely(ret < 0))
			goto session_error;

		ret = cryptodev_cipher_init(&ses_new->cdata, alg_name, keys.ckey,
						keylen, stream, aead);
		if (ret < 0) {
			ddebug(1, "Failed to load cipher for %s", alg_name);
			goto session_error;
		}
	}

	if (hash_name && aead == 0) {
		if (unlikely(sop->mackeylen > CRYPTO_HMAC_MAX_KEY_LEN)) {
			ddebug(1, "Setting key failed for %s-%zu.",
				hash_name, (size_t)sop->mackeylen*8);
			ret = -EINVAL;
			goto session_error;
		}

		if (sop->mackey && unlikely(copy_from_user(keys.mkey, sop->mackey,
					    sop->mackeylen))) {
			ret = -EFAULT;
			goto session_error;
		}

		ret = cryptodev_hash_init(&ses_new->hdata, hash_name, hmac_mode,
							keys.mkey, sop->mackeylen);
		if (ret != 0) {
			ddebug(1, "Failed to load hash for %s", hash_name);
			goto session_error;
		}

		ret = cryptodev_hash_reset(&ses_new->hdata);
		if (ret != 0) {
			goto session_error;
		}
	}

	ses_new->alignmask = max(ses_new->cdata.alignmask,
	                                          ses_new->hdata.alignmask);
	ddebug(2, "got alignmask %d", ses_new->alignmask);

	ses_new->array_size = DEFAULT_PREALLOC_PAGES;
	ddebug(2, "preallocating for %d user pages", ses_new->array_size);
	ses_new->pages = kzalloc(ses_new->array_size *
			sizeof(struct page *), GFP_KERNEL);
	ses_new->sg = kzalloc(ses_new->array_size *
			sizeof(struct scatterlist), GFP_KERNEL);
	if (ses_new->sg == NULL || ses_new->pages == NULL) {
		ddebug(0, "Memory error");
		ret = -ENOMEM;
		goto session_error;
	}

	/* Non-multithreaded can only create one session */
	if (!rk_cryptodev_multi_thread(NULL) &&
	    !atomic_dec_and_test(&cryptodev_sess)) {
		atomic_inc(&cryptodev_sess);
		ddebug(2, "Non-multithreaded can only create one session. sess = %d",
		       atomic_read(&cryptodev_sess));
		ret = -EBUSY;
		goto session_error;
	}

	/* put the new session to the list */
	get_random_bytes(&ses_new->sid, sizeof(ses_new->sid));
	mutex_init(&ses_new->sem);

	mutex_lock(&fcr->sem);
restart:
	list_for_each_entry(ses_ptr, &fcr->list, entry) {
		/* Check for duplicate SID */
		if (unlikely(ses_new->sid == ses_ptr->sid)) {
			get_random_bytes(&ses_new->sid, sizeof(ses_new->sid));
			/* Unless we have a broken RNG this
			   shouldn't loop forever... ;-) */
			goto restart;
		}
	}

	list_add(&ses_new->entry, &fcr->list);
	mutex_unlock(&fcr->sem);

	/* Fill in some values for the user. */
	sop->ses = ses_new->sid;
	return 0;

	/* We count on ses_new to be initialized with zeroes
	 * Since hdata and cdata are embedded within ses_new, it follows that
	 * hdata->init and cdata->init are either zero or one as they have been
	 * initialized or not */
session_error:
	cryptodev_hash_deinit(&ses_new->hdata);
	cryptodev_cipher_deinit(&ses_new->cdata);
	kfree(ses_new->sg);
	kfree(ses_new->pages);
	kfree(ses_new);
	return ret;
}

/* Everything that needs to be done when removing a session. */
static inline void
crypto_destroy_session(struct csession *ses_ptr)
{
	if (!mutex_trylock(&ses_ptr->sem)) {
		ddebug(2, "Waiting for semaphore of sid=0x%08X", ses_ptr->sid);
		mutex_lock(&ses_ptr->sem);
	}
	ddebug(2, "Removed session 0x%08X", ses_ptr->sid);
	cryptodev_cipher_deinit(&ses_ptr->cdata);
	cryptodev_hash_deinit(&ses_ptr->hdata);
	ddebug(2, "freeing space for %d user pages", ses_ptr->array_size);
	kfree(ses_ptr->pages);
	kfree(ses_ptr->sg);
	mutex_unlock(&ses_ptr->sem);
	mutex_destroy(&ses_ptr->sem);
	kfree(ses_ptr);

	/* Non-multithreaded can only create one session */
	if (!rk_cryptodev_multi_thread(NULL)) {
		atomic_inc(&cryptodev_sess);
		ddebug(2, "Release cryptodev_sess = %d", atomic_read(&cryptodev_sess));
	}
}

/* Look up a session by ID and remove. */
static int
crypto_finish_session(struct fcrypt *fcr, uint32_t sid)
{
	struct csession *tmp, *ses_ptr;
	struct list_head *head;
	int ret = 0;

	mutex_lock(&fcr->sem);
	head = &fcr->list;
	list_for_each_entry_safe(ses_ptr, tmp, head, entry) {
		if (ses_ptr->sid == sid) {
			list_del(&ses_ptr->entry);
			crypto_destroy_session(ses_ptr);
			break;
		}
	}

	if (unlikely(!ses_ptr)) {
		derr(1, "Session with sid=0x%08X not found!", sid);
		ret = -ENOENT;
	}
	mutex_unlock(&fcr->sem);

	return ret;
}

/* Remove all sessions when closing the file */
static int
crypto_finish_all_sessions(struct fcrypt *fcr)
{
	struct csession *tmp, *ses_ptr;
	struct list_head *head;

	mutex_lock(&fcr->sem);

	head = &fcr->list;
	list_for_each_entry_safe(ses_ptr, tmp, head, entry) {
		list_del(&ses_ptr->entry);
		crypto_destroy_session(ses_ptr);
	}
	mutex_unlock(&fcr->sem);

	return 0;
}

/* Look up session by session ID. The returned session is locked. */
struct csession *
crypto_get_session_by_sid(struct fcrypt *fcr, uint32_t sid)
{
	struct csession *ses_ptr, *retval = NULL;

	if (unlikely(fcr == NULL))
		return NULL;

	mutex_lock(&fcr->sem);
	list_for_each_entry(ses_ptr, &fcr->list, entry) {
		if (ses_ptr->sid == sid) {
			mutex_lock(&ses_ptr->sem);
			retval = ses_ptr;
			break;
		}
	}
	mutex_unlock(&fcr->sem);

	return retval;
}

static void mutex_lock_double(struct mutex *a, struct mutex *b)
{
	if (b < a)
		swap(a, b);

	mutex_lock(a);
	mutex_lock_nested(b, SINGLE_DEPTH_NESTING);
}

int
crypto_get_sessions_by_sid(struct fcrypt *fcr,
			   uint32_t sid_1, struct csession **ses_ptr_1,
			   uint32_t sid_2, struct csession **ses_ptr_2)
{
	struct csession *ses_ptr;
	int retval;

	if (unlikely(fcr == NULL)) {
		retval = -ENOENT;
		goto out;
	}

	if (sid_1 == sid_2) {
		retval = -EDEADLK;
		goto out;
	}

	mutex_lock(&fcr->sem);

	list_for_each_entry(ses_ptr, &fcr->list, entry) {
		if (ses_ptr->sid == sid_1)
			*ses_ptr_1 = ses_ptr;
		else if (ses_ptr->sid == sid_2)
			*ses_ptr_2 = ses_ptr;
	}

	if (*ses_ptr_1 && *ses_ptr_2) {
		mutex_lock_double(&(*ses_ptr_1)->sem, &(*ses_ptr_2)->sem);
		retval = 0;
	} else {
		retval = -ENOENT;
	}

	mutex_unlock(&fcr->sem);

out:
	if (retval) {
		*ses_ptr_1 = NULL;
		*ses_ptr_2 = NULL;
	}
	return retval;
}

#ifdef CIOCCPHASH
/* Copy the hash state from one session to another */
static int
crypto_copy_hash_state(struct fcrypt *fcr, uint32_t dst_sid, uint32_t src_sid)
{
	struct csession *src_ses, *dst_ses;
	int ret;

	ret = crypto_get_sessions_by_sid(fcr, src_sid, &src_ses,
					 dst_sid, &dst_ses);
	if (unlikely(ret)) {
		derr(1, "Failed to get sesssions with sid=0x%08X sid=%0x08X!",
		     src_sid, dst_sid);
		return ret;
	}

	ret = cryptodev_hash_copy(&dst_ses->hdata, &src_ses->hdata);
	crypto_put_session(src_ses);
	crypto_put_session(dst_ses);
	return ret;
}
#endif /* CIOCCPHASH */

static void cryptask_routine(struct work_struct *work)
{
	struct crypt_priv *pcr = container_of(work, struct crypt_priv, cryptask);
	struct todo_list_item *item;
	LIST_HEAD(tmp);

	/* fetch all pending jobs into the temporary list */
	mutex_lock(&pcr->todo.lock);
	list_cut_position(&tmp, &pcr->todo.list, pcr->todo.list.prev);
	mutex_unlock(&pcr->todo.lock);

	/* handle each job locklessly */
	list_for_each_entry(item, &tmp, __hook) {
		item->result = crypto_run(&pcr->fcrypt, &item->kcop);
		if (unlikely(item->result))
			derr(0, "crypto_run() failed: %d", item->result);
	}

	/* push all handled jobs to the done list at once */
	mutex_lock(&pcr->done.lock);
	list_splice_tail(&tmp, &pcr->done.list);
	mutex_unlock(&pcr->done.lock);

	/* wake for POLLIN */
	wake_up_interruptible(&pcr->user_waiter);
}

/* ====== /dev/crypto ====== */
static atomic_t cryptodev_node = ATOMIC_INIT(1);

static int
cryptodev_open(struct inode *inode, struct file *filp)
{
	struct todo_list_item *tmp, *tmp_next;
	struct crypt_priv *pcr;
	int i;

	/* Non-multithreaded can only be opened once */
	if (!rk_cryptodev_multi_thread(NULL) &&
	    !atomic_dec_and_test(&cryptodev_node)) {
		atomic_inc(&cryptodev_node);
		ddebug(2, "Non-multithreaded can only be opened once. node = %d",
		       atomic_read(&cryptodev_node));
		return -EBUSY;
	}

	/* make sure sess == 1 after open */
	atomic_set(&cryptodev_sess, 1);

	pcr = kzalloc(sizeof(*pcr), GFP_KERNEL);
	if (!pcr)
		return -ENOMEM;
	filp->private_data = pcr;

	mutex_init(&pcr->fcrypt.sem);
	mutex_init(&pcr->free.lock);
	mutex_init(&pcr->todo.lock);
	mutex_init(&pcr->done.lock);

	INIT_LIST_HEAD(&pcr->fcrypt.list);
	INIT_LIST_HEAD(&pcr->fcrypt.dma_map_list);
	INIT_LIST_HEAD(&pcr->free.list);
	INIT_LIST_HEAD(&pcr->todo.list);
	INIT_LIST_HEAD(&pcr->done.list);

	INIT_WORK(&pcr->cryptask, cryptask_routine);

	init_waitqueue_head(&pcr->user_waiter);

	for (i = 0; i < DEF_COP_RINGSIZE; i++) {
		tmp = kzalloc(sizeof(struct todo_list_item), GFP_KERNEL);
		if (!tmp)
			goto err_ringalloc;
		pcr->itemcount++;
		ddebug(2, "allocated new item at %p", tmp);
		list_add(&tmp->__hook, &pcr->free.list);
	}

	ddebug(2, "Cryptodev handle initialised, %d elements in queue",
			DEF_COP_RINGSIZE);
	return 0;

/* In case of errors, free any memory allocated so far */
err_ringalloc:
	list_for_each_entry_safe(tmp, tmp_next, &pcr->free.list, __hook) {
		list_del(&tmp->__hook);
		kfree(tmp);
	}
	mutex_destroy(&pcr->done.lock);
	mutex_destroy(&pcr->todo.lock);
	mutex_destroy(&pcr->free.lock);
	mutex_destroy(&pcr->fcrypt.sem);
	kfree(pcr);
	filp->private_data = NULL;
	return -ENOMEM;
}

static int
cryptodev_release(struct inode *inode, struct file *filp)
{
	struct crypt_priv *pcr = filp->private_data;
	struct todo_list_item *item, *item_safe;
	int items_freed = 0;

	if (!pcr)
		return 0;

	/* Non-multithreaded can only be opened once */
	if (!rk_cryptodev_multi_thread(NULL)) {
		atomic_inc(&cryptodev_node);
		ddebug(2, "Release cryptodev_node = %d", atomic_read(&cryptodev_node));
	}

	cancel_work_sync(&pcr->cryptask);

	list_splice_tail(&pcr->todo.list, &pcr->free.list);
	list_splice_tail(&pcr->done.list, &pcr->free.list);

	list_for_each_entry_safe(item, item_safe, &pcr->free.list, __hook) {
		ddebug(2, "freeing item at %p", item);
		list_del(&item->__hook);
		kfree(item);
		items_freed++;
	}

	if (items_freed != pcr->itemcount) {
		derr(0, "freed %d items, but %d should exist!",
				items_freed, pcr->itemcount);
	}

	crypto_finish_all_sessions(&pcr->fcrypt);

	mutex_destroy(&pcr->done.lock);
	mutex_destroy(&pcr->todo.lock);
	mutex_destroy(&pcr->free.lock);
	mutex_destroy(&pcr->fcrypt.sem);

	kfree(pcr);
	filp->private_data = NULL;

	ddebug(2, "Cryptodev handle deinitialised, %d elements freed",
			items_freed);
	return 0;
}

static int
clonefd(struct file *filp)
{
	int ret;
	ret = get_unused_fd_flags(0);
	if (ret >= 0) {
			get_file(filp);
			fd_install(ret, filp);
	}

	return ret;
}

#ifdef ENABLE_ASYNC
/* enqueue a job for asynchronous completion
 *
 * returns:
 * -EBUSY when there are no free queue slots left
 *        (and the number of slots has reached it MAX_COP_RINGSIZE)
 * -EFAULT when there was a memory allocation error
 * 0 on success */
static int crypto_async_run(struct crypt_priv *pcr, struct kernel_crypt_op *kcop)
{
	struct todo_list_item *item = NULL;

	if (unlikely(kcop->cop.flags & COP_FLAG_NO_ZC))
		return -EINVAL;

	mutex_lock(&pcr->free.lock);
	if (likely(!list_empty(&pcr->free.list))) {
		item = list_first_entry(&pcr->free.list,
				struct todo_list_item, __hook);
		list_del(&item->__hook);
	} else if (pcr->itemcount < MAX_COP_RINGSIZE) {
		pcr->itemcount++;
	} else {
		mutex_unlock(&pcr->free.lock);
		return -EBUSY;
	}
	mutex_unlock(&pcr->free.lock);

	if (unlikely(!item)) {
		item = kzalloc(sizeof(struct todo_list_item), GFP_KERNEL);
		if (unlikely(!item))
			return -EFAULT;
		dinfo(1, "increased item count to %d", pcr->itemcount);
	}

	memcpy(&item->kcop, kcop, sizeof(struct kernel_crypt_op));

	mutex_lock(&pcr->todo.lock);
	list_add_tail(&item->__hook, &pcr->todo.list);
	mutex_unlock(&pcr->todo.lock);

	queue_work(cryptodev_wq, &pcr->cryptask);
	return 0;
}

/* get the first completed job from the "done" queue
 *
 * returns:
 * -EBUSY if no completed jobs are ready (yet)
 * the return value of crypto_run() otherwise */
static int crypto_async_fetch(struct crypt_priv *pcr,
		struct kernel_crypt_op *kcop)
{
	struct todo_list_item *item;
	int retval;

	mutex_lock(&pcr->done.lock);
	if (list_empty(&pcr->done.list)) {
		mutex_unlock(&pcr->done.lock);
		return -EBUSY;
	}
	item = list_first_entry(&pcr->done.list, struct todo_list_item, __hook);
	list_del(&item->__hook);
	mutex_unlock(&pcr->done.lock);

	memcpy(kcop, &item->kcop, sizeof(struct kernel_crypt_op));
	retval = item->result;

	mutex_lock(&pcr->free.lock);
	list_add_tail(&item->__hook, &pcr->free.list);
	mutex_unlock(&pcr->free.lock);

	/* wake for POLLOUT */
	wake_up_interruptible(&pcr->user_waiter);

	return retval;
}
#endif

/* this function has to be called from process context */
static int fill_kcop_from_cop(struct kernel_crypt_op *kcop, struct fcrypt *fcr)
{
	struct crypt_op *cop = &kcop->cop;
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
			derr(1, "error copying IV (%d bytes), copy_from_user returned %d for address %p",
					kcop->ivlen, rc, cop->iv);
			return -EFAULT;
		}
	}

	return 0;
}

/* this function has to be called from process context */
static int fill_cop_from_kcop(struct kernel_crypt_op *kcop, struct fcrypt *fcr)
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

static int kcop_from_user(struct kernel_crypt_op *kcop,
			struct fcrypt *fcr, void __user *arg)
{
	if (unlikely(copy_from_user(&kcop->cop, arg, sizeof(kcop->cop))))
		return -EFAULT;

	return fill_kcop_from_cop(kcop, fcr);
}

static int kcop_to_user(struct kernel_crypt_op *kcop,
			struct fcrypt *fcr, void __user *arg)
{
	int ret;

	ret = fill_cop_from_kcop(kcop, fcr);
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

static inline void tfm_info_to_alg_info(struct alg_info *dst, struct crypto_tfm *tfm)
{
	snprintf(dst->cra_name, CRYPTODEV_MAX_ALG_NAME,
			"%s", crypto_tfm_alg_name(tfm));
	snprintf(dst->cra_driver_name, CRYPTODEV_MAX_ALG_NAME,
			"%s", crypto_tfm_alg_driver_name(tfm));
}

#ifndef CRYPTO_ALG_KERN_DRIVER_ONLY
static unsigned int is_known_accelerated(struct crypto_tfm *tfm)
{
	const char *name = crypto_tfm_alg_driver_name(tfm);

	if (name == NULL)
		return 1; /* assume accelerated */

	/* look for known crypto engine names */
	if (strstr(name, "-talitos")	||
	    !strncmp(name, "mv-", 3)	||
	    !strncmp(name, "atmel-", 6)	||
	    strstr(name, "geode")	||
	    strstr(name, "hifn")	||
	    strstr(name, "-ixp4xx")	||
	    strstr(name, "-omap")	||
	    strstr(name, "-picoxcell")	||
	    strstr(name, "-s5p")	||
	    strstr(name, "-ppc4xx")	||
	    strstr(name, "-caam")	||
	    strstr(name, "-n2"))
		return 1;

	return 0;
}
#endif

static int get_session_info(struct fcrypt *fcr, struct session_info_op *siop)
{
	struct csession *ses_ptr;
	struct crypto_tfm *tfm;

	/* this also enters ses_ptr->sem */
	ses_ptr = crypto_get_session_by_sid(fcr, siop->ses);
	if (unlikely(!ses_ptr)) {
		derr(1, "invalid session ID=0x%08X", siop->ses);
		return -EINVAL;
	}

	siop->flags = 0;

	if (ses_ptr->cdata.init) {
		if (ses_ptr->cdata.aead == 0)
			tfm = cryptodev_crypto_blkcipher_tfm(ses_ptr->cdata.async.s);
		else
			tfm = crypto_aead_tfm(ses_ptr->cdata.async.as);
		tfm_info_to_alg_info(&siop->cipher_info, tfm);
#ifdef CRYPTO_ALG_KERN_DRIVER_ONLY
		if (tfm->__crt_alg->cra_flags & CRYPTO_ALG_KERN_DRIVER_ONLY)
			siop->flags |= SIOP_FLAG_KERNEL_DRIVER_ONLY;
#else
		if (is_known_accelerated(tfm))
			siop->flags |= SIOP_FLAG_KERNEL_DRIVER_ONLY;
#endif
	}
	if (ses_ptr->hdata.init) {
		tfm = crypto_ahash_tfm(ses_ptr->hdata.async.s);
		tfm_info_to_alg_info(&siop->hash_info, tfm);
#ifdef CRYPTO_ALG_KERN_DRIVER_ONLY
		if (tfm->__crt_alg->cra_flags & CRYPTO_ALG_KERN_DRIVER_ONLY)
			siop->flags |= SIOP_FLAG_KERNEL_DRIVER_ONLY;
#else
		if (is_known_accelerated(tfm))
			siop->flags |= SIOP_FLAG_KERNEL_DRIVER_ONLY;
#endif
	}

	siop->alignmask = ses_ptr->alignmask;

	crypto_put_session(ses_ptr);
	return 0;
}

static long
cryptodev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg_)
{
	void __user *arg = (void __user *)arg_;
	int __user *p = arg;
	struct session_op sop;
	struct kernel_crypt_op kcop;
	struct kernel_crypt_auth_op kcaop;
	struct crypt_priv *pcr = filp->private_data;
	struct fcrypt *fcr;
	struct session_info_op siop;
#ifdef CIOCCPHASH
	struct cphash_op cphop;
#endif
	uint32_t ses;
	int ret, fd;

	if (unlikely(!pcr))
		BUG();

	fcr = &pcr->fcrypt;

	switch (cmd) {
	case CIOCASYMFEAT:
		return put_user(0, p);
	case CRIOGET:
		fd = clonefd(filp);
		ret = put_user(fd, p);
		if (unlikely(ret)) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0))
			sys_close(fd);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0))
			ksys_close(fd);
#else
			close_fd(fd);
#endif
			return ret;
		}
		return ret;
	case CIOCGSESSION:
		if (unlikely(copy_from_user(&sop, arg, sizeof(sop))))
			return -EFAULT;

		ret = crypto_create_session(fcr, &sop);
		if (unlikely(ret))
			return ret;
		ret = copy_to_user(arg, &sop, sizeof(sop));
		if (unlikely(ret)) {
			crypto_finish_session(fcr, sop.ses);
			return -EFAULT;
		}
		return ret;
	case CIOCFSESSION:
		ret = get_user(ses, (uint32_t __user *)arg);
		if (unlikely(ret))
			return ret;
		ret = crypto_finish_session(fcr, ses);
		return ret;
	case CIOCGSESSINFO:
		if (unlikely(copy_from_user(&siop, arg, sizeof(siop))))
			return -EFAULT;

		ret = get_session_info(fcr, &siop);
		if (unlikely(ret))
			return ret;
		return copy_to_user(arg, &siop, sizeof(siop));
#ifdef CIOCCPHASH
	case CIOCCPHASH:
		if (unlikely(copy_from_user(&cphop, arg, sizeof(cphop))))
			return -EFAULT;
		return crypto_copy_hash_state(fcr, cphop.dst_ses, cphop.src_ses);
#endif /* CIOCPHASH */
	case CIOCCRYPT:
		if (unlikely(ret = kcop_from_user(&kcop, fcr, arg))) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = crypto_run(fcr, &kcop);
		if (unlikely(ret)) {
			dwarning(1, "Error in crypto_run");
			return ret;
		}

		return kcop_to_user(&kcop, fcr, arg);
	case CIOCAUTHCRYPT:
		if (unlikely(ret = cryptodev_kcaop_from_user(&kcaop, fcr, arg))) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = crypto_auth_run(fcr, &kcaop);
		if (unlikely(ret)) {
			dwarning(1, "Error in crypto_auth_run");
			return ret;
		}
		return cryptodev_kcaop_to_user(&kcaop, fcr, arg);
#ifdef ENABLE_ASYNC
	case CIOCASYNCCRYPT:
		if (unlikely(ret = kcop_from_user(&kcop, fcr, arg)))
			return ret;

		return crypto_async_run(pcr, &kcop);
	case CIOCASYNCFETCH:
		ret = crypto_async_fetch(pcr, &kcop);
		if (unlikely(ret))
			return ret;

		return kcop_to_user(&kcop, fcr, arg);
#endif
	default:
		return rk_cryptodev_ioctl(fcr, cmd, arg_);
	}
}

/* compatibility code for 32bit userlands */
#ifdef CONFIG_COMPAT

static inline void
compat_to_session_op(struct compat_session_op *compat, struct session_op *sop)
{
	sop->cipher = compat->cipher;
	sop->mac = compat->mac;
	sop->keylen = compat->keylen;

	sop->key       = compat_ptr(compat->key);
	sop->mackeylen = compat->mackeylen;
	sop->mackey    = compat_ptr(compat->mackey);
	sop->ses       = compat->ses;
}

static inline void
session_op_to_compat(struct session_op *sop, struct compat_session_op *compat)
{
	compat->cipher = sop->cipher;
	compat->mac = sop->mac;
	compat->keylen = sop->keylen;

	compat->key       = ptr_to_compat(sop->key);
	compat->mackeylen = sop->mackeylen;
	compat->mackey    = ptr_to_compat(sop->mackey);
	compat->ses       = sop->ses;
}

static inline void
compat_to_crypt_op(struct compat_crypt_op *compat, struct crypt_op *cop)
{
	cop->ses = compat->ses;
	cop->op = compat->op;
	cop->flags = compat->flags;
	cop->len = compat->len;

	cop->src = compat_ptr(compat->src);
	cop->dst = compat_ptr(compat->dst);
	cop->mac = compat_ptr(compat->mac);
	cop->iv  = compat_ptr(compat->iv);
}

static inline void
crypt_op_to_compat(struct crypt_op *cop, struct compat_crypt_op *compat)
{
	compat->ses = cop->ses;
	compat->op = cop->op;
	compat->flags = cop->flags;
	compat->len = cop->len;

	compat->src = ptr_to_compat(cop->src);
	compat->dst = ptr_to_compat(cop->dst);
	compat->mac = ptr_to_compat(cop->mac);
	compat->iv  = ptr_to_compat(cop->iv);
}

static int compat_kcop_from_user(struct kernel_crypt_op *kcop,
                                 struct fcrypt *fcr, void __user *arg)
{
	struct compat_crypt_op compat_cop;

	if (unlikely(copy_from_user(&compat_cop, arg, sizeof(compat_cop))))
		return -EFAULT;
	compat_to_crypt_op(&compat_cop, &kcop->cop);

	return fill_kcop_from_cop(kcop, fcr);
}

static int compat_kcop_to_user(struct kernel_crypt_op *kcop,
                               struct fcrypt *fcr, void __user *arg)
{
	int ret;
	struct compat_crypt_op compat_cop;

	ret = fill_cop_from_kcop(kcop, fcr);
	if (unlikely(ret)) {
		dwarning(1, "Error in fill_cop_from_kcop");
		return ret;
	}
	crypt_op_to_compat(&kcop->cop, &compat_cop);

	if (unlikely(copy_to_user(arg, &compat_cop, sizeof(compat_cop)))) {
		dwarning(1, "Error copying to user");
		return -EFAULT;
	}
	return 0;
}

static long
cryptodev_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg_)
{
	void __user *arg = (void __user *)arg_;
	struct crypt_priv *pcr = file->private_data;
	struct fcrypt *fcr;
	struct session_op sop;
	struct compat_session_op compat_sop;
	struct kernel_crypt_op kcop;
	struct kernel_crypt_auth_op kcaop;
	int ret;

	if (unlikely(!pcr))
		BUG();

	fcr = &pcr->fcrypt;

	switch (cmd) {
	case CIOCASYMFEAT:
	case CRIOGET:
	case CIOCFSESSION:
	case CIOCGSESSINFO:
		return cryptodev_ioctl(file, cmd, arg_);

	case COMPAT_CIOCGSESSION:
		if (unlikely(copy_from_user(&compat_sop, arg,
					    sizeof(compat_sop))))
			return -EFAULT;
		compat_to_session_op(&compat_sop, &sop);

		ret = crypto_create_session(fcr, &sop);
		if (unlikely(ret))
			return ret;

		session_op_to_compat(&sop, &compat_sop);
		ret = copy_to_user(arg, &compat_sop, sizeof(compat_sop));
		if (unlikely(ret)) {
			crypto_finish_session(fcr, sop.ses);
			return -EFAULT;
		}
		return ret;

	case COMPAT_CIOCCRYPT:
		ret = compat_kcop_from_user(&kcop, fcr, arg);
		if (unlikely(ret))
			return ret;

		ret = crypto_run(fcr, &kcop);
		if (unlikely(ret))
			return ret;

		return compat_kcop_to_user(&kcop, fcr, arg);

	case COMPAT_CIOCAUTHCRYPT:
		ret = compat_kcaop_from_user(&kcaop, fcr, arg);
		if (unlikely(ret)) {
			dwarning(1, "Error copying from user");
			return ret;
		}

		ret = crypto_auth_run(fcr, &kcaop);
		if (unlikely(ret)) {
			dwarning(1, "Error in crypto_auth_run");
			return ret;
		}
		return compat_kcaop_to_user(&kcaop, fcr, arg);
#ifdef ENABLE_ASYNC
	case COMPAT_CIOCASYNCCRYPT:
		if (unlikely(ret = compat_kcop_from_user(&kcop, fcr, arg)))
			return ret;

		return crypto_async_run(pcr, &kcop);
	case COMPAT_CIOCASYNCFETCH:
		ret = crypto_async_fetch(pcr, &kcop);
		if (unlikely(ret))
			return ret;

		return compat_kcop_to_user(&kcop, fcr, arg);
#endif
	default:
		return rk_compat_cryptodev_ioctl(fcr, cmd, arg_);
	}
}

#endif /* CONFIG_COMPAT */

static unsigned int cryptodev_poll(struct file *file, poll_table *wait)
{
	struct crypt_priv *pcr = file->private_data;
	unsigned int ret = 0;

	poll_wait(file, &pcr->user_waiter, wait);

	if (!list_empty_careful(&pcr->done.list))
		ret |= POLLIN | POLLRDNORM;
	if (!list_empty_careful(&pcr->free.list) || pcr->itemcount < MAX_COP_RINGSIZE)
		ret |= POLLOUT | POLLWRNORM;

	return ret;
}

static const struct file_operations cryptodev_fops = {
	.owner = THIS_MODULE,
	.open = cryptodev_open,
	.release = cryptodev_release,
	.unlocked_ioctl = cryptodev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = cryptodev_compat_ioctl,
#endif /* CONFIG_COMPAT */
	.poll = cryptodev_poll,
};

static struct miscdevice cryptodev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "crypto",
	.fops = &cryptodev_fops,
	.mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
};

static int __init
cryptodev_register(void)
{
	int rc;

	rc = misc_register(&cryptodev);
	if (unlikely(rc)) {
		pr_err(PFX "registration of /dev/crypto failed\n");
		return rc;
	}

	return 0;
}

static void __exit
cryptodev_deregister(void)
{
	misc_deregister(&cryptodev);
}

/* ====== Module init/exit ====== */
static struct ctl_table verbosity_ctl_dir[] = {
	{
		.procname       = "cryptodev_verbosity",
		.data           = &cryptodev_verbosity,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
	{},
};

static struct ctl_table verbosity_ctl_root[] = {
	{
		.procname       = "ioctl",
		.mode           = 0555,
		.child          = verbosity_ctl_dir,
	},
	{},
};
static struct ctl_table_header *verbosity_sysctl_header;
static int __init init_cryptodev(void)
{
	int rc;

	cryptodev_wq = create_workqueue("cryptodev_queue");
	if (unlikely(!cryptodev_wq)) {
		pr_err(PFX "failed to allocate the cryptodev workqueue\n");
		return -EFAULT;
	}

	rc = cryptodev_register();
	if (unlikely(rc)) {
		destroy_workqueue(cryptodev_wq);
		return rc;
	}

	verbosity_sysctl_header = register_sysctl_table(verbosity_ctl_root);

	pr_info(PFX "driver %s loaded.\n", VERSION);

	return 0;
}

static void __exit exit_cryptodev(void)
{
	flush_workqueue(cryptodev_wq);
	destroy_workqueue(cryptodev_wq);

	if (verbosity_sysctl_header)
		unregister_sysctl_table(verbosity_sysctl_header);

	cryptodev_deregister();
	pr_info(PFX "driver unloaded.\n");
}

module_init(init_cryptodev);
module_exit(exit_cryptodev);

