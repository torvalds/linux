/* GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see http://www.gnu.org/licenses
 *
 * Please  visit http://www.xyratex.com/contact if you need additional
 * information or have any questions.
 *
 * GPL HEADER END
 */

/*
 * Copyright 2012 Xyratex Technology Limited
 *
 * Copyright (c) 2012, Intel Corporation.
 */

#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include "../../../include/linux/libcfs/libcfs.h"
#include "../../../include/linux/libcfs/linux/linux-crypto.h"
/**
 *  Array of  hash algorithm speed in MByte per second
 */
static int cfs_crypto_hash_speeds[CFS_HASH_ALG_MAX];



static int cfs_crypto_hash_alloc(unsigned char alg_id,
				 const struct cfs_crypto_hash_type **type,
				 struct hash_desc *desc, unsigned char *key,
				 unsigned int key_len)
{
	int     err = 0;

	*type = cfs_crypto_hash_type(alg_id);

	if (*type == NULL) {
		CWARN("Unsupported hash algorithm id = %d, max id is %d\n",
		      alg_id, CFS_HASH_ALG_MAX);
		return -EINVAL;
	}
	desc->tfm = crypto_alloc_hash((*type)->cht_name, 0, 0);

	if (desc->tfm == NULL)
		return -EINVAL;

	if (IS_ERR(desc->tfm)) {
		CDEBUG(D_INFO, "Failed to alloc crypto hash %s\n",
		       (*type)->cht_name);
		return PTR_ERR(desc->tfm);
	}

	desc->flags = 0;

	/** Shash have different logic for initialization then digest
	 * shash: crypto_hash_setkey, crypto_hash_init
	 * digest: crypto_digest_init, crypto_digest_setkey
	 * Skip this function for digest, because we use shash logic at
	 * cfs_crypto_hash_alloc.
	 */
	if (key != NULL) {
		err = crypto_hash_setkey(desc->tfm, key, key_len);
	} else if ((*type)->cht_key != 0) {
		err = crypto_hash_setkey(desc->tfm,
					 (unsigned char *)&((*type)->cht_key),
					 (*type)->cht_size);
	}

	if (err != 0) {
		crypto_free_hash(desc->tfm);
		return err;
	}

	CDEBUG(D_INFO, "Using crypto hash: %s (%s) speed %d MB/s\n",
	       (crypto_hash_tfm(desc->tfm))->__crt_alg->cra_name,
	       (crypto_hash_tfm(desc->tfm))->__crt_alg->cra_driver_name,
	       cfs_crypto_hash_speeds[alg_id]);

	return crypto_hash_init(desc);
}

int cfs_crypto_hash_digest(unsigned char alg_id,
			   const void *buf, unsigned int buf_len,
			   unsigned char *key, unsigned int key_len,
			   unsigned char *hash, unsigned int *hash_len)
{
	struct scatterlist	sl;
	struct hash_desc	hdesc;
	int			err;
	const struct cfs_crypto_hash_type	*type;

	if (buf == NULL || buf_len == 0 || hash_len == NULL)
		return -EINVAL;

	err = cfs_crypto_hash_alloc(alg_id, &type, &hdesc, key, key_len);
	if (err != 0)
		return err;

	if (hash == NULL || *hash_len < type->cht_size) {
		*hash_len = type->cht_size;
		crypto_free_hash(hdesc.tfm);
		return -ENOSPC;
	}
	sg_init_one(&sl, (void *)buf, buf_len);

	hdesc.flags = 0;
	err = crypto_hash_digest(&hdesc, &sl, sl.length, hash);
	crypto_free_hash(hdesc.tfm);

	return err;
}
EXPORT_SYMBOL(cfs_crypto_hash_digest);

struct cfs_crypto_hash_desc *
	cfs_crypto_hash_init(unsigned char alg_id,
			     unsigned char *key, unsigned int key_len)
{

	struct  hash_desc       *hdesc;
	int		     err;
	const struct cfs_crypto_hash_type       *type;

	hdesc = kmalloc(sizeof(*hdesc), 0);
	if (hdesc == NULL)
		return ERR_PTR(-ENOMEM);

	err = cfs_crypto_hash_alloc(alg_id, &type, hdesc, key, key_len);

	if (err) {
		kfree(hdesc);
		return ERR_PTR(err);
	}
	return (struct cfs_crypto_hash_desc *)hdesc;
}
EXPORT_SYMBOL(cfs_crypto_hash_init);

int cfs_crypto_hash_update_page(struct cfs_crypto_hash_desc *hdesc,
				struct page *page, unsigned int offset,
				unsigned int len)
{
	struct scatterlist sl;

	sg_init_table(&sl, 1);
	sg_set_page(&sl, page, len, offset & ~CFS_PAGE_MASK);

	return crypto_hash_update((struct hash_desc *)hdesc, &sl, sl.length);
}
EXPORT_SYMBOL(cfs_crypto_hash_update_page);

int cfs_crypto_hash_update(struct cfs_crypto_hash_desc *hdesc,
			   const void *buf, unsigned int buf_len)
{
	struct scatterlist sl;

	sg_init_one(&sl, (void *)buf, buf_len);

	return crypto_hash_update((struct hash_desc *)hdesc, &sl, sl.length);
}
EXPORT_SYMBOL(cfs_crypto_hash_update);

/*      If hash_len pointer is NULL - destroy descriptor. */
int cfs_crypto_hash_final(struct cfs_crypto_hash_desc *hdesc,
			  unsigned char *hash, unsigned int *hash_len)
{
	int     err;
	int     size = crypto_hash_digestsize(((struct hash_desc *)hdesc)->tfm);

	if (hash_len == NULL) {
		crypto_free_hash(((struct hash_desc *)hdesc)->tfm);
		kfree(hdesc);
		return 0;
	}
	if (hash == NULL || *hash_len < size) {
		*hash_len = size;
		return -ENOSPC;
	}
	err = crypto_hash_final((struct hash_desc *) hdesc, hash);

	if (err < 0) {
		/* May be caller can fix error */
		return err;
	}
	crypto_free_hash(((struct hash_desc *)hdesc)->tfm);
	kfree(hdesc);
	return err;
}
EXPORT_SYMBOL(cfs_crypto_hash_final);

static void cfs_crypto_performance_test(unsigned char alg_id,
					const unsigned char *buf,
					unsigned int buf_len)
{
	unsigned long		   start, end;
	int			     bcount, err = 0;
	int			     sec = 1; /* do test only 1 sec */
	unsigned char		   hash[64];
	unsigned int		    hash_len = 64;

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		err = cfs_crypto_hash_digest(alg_id, buf, buf_len, NULL, 0,
					     hash, &hash_len);
		if (err)
			break;

	}
	end = jiffies;

	if (err) {
		cfs_crypto_hash_speeds[alg_id] =  -1;
		CDEBUG(D_INFO, "Crypto hash algorithm %s, err = %d\n",
		       cfs_crypto_hash_name(alg_id), err);
	} else {
		unsigned long   tmp;
		tmp = ((bcount * buf_len / jiffies_to_msecs(end - start)) *
		       1000) / (1024 * 1024);
		cfs_crypto_hash_speeds[alg_id] = (int)tmp;
	}
	CDEBUG(D_INFO, "Crypto hash algorithm %s speed = %d MB/s\n",
	       cfs_crypto_hash_name(alg_id), cfs_crypto_hash_speeds[alg_id]);
}

int cfs_crypto_hash_speed(unsigned char hash_alg)
{
	if (hash_alg < CFS_HASH_ALG_MAX)
		return cfs_crypto_hash_speeds[hash_alg];
	else
		return -1;
}
EXPORT_SYMBOL(cfs_crypto_hash_speed);

/**
 * Do performance test for all hash algorithms.
 */
static int cfs_crypto_test_hashes(void)
{
	unsigned char	   i;
	unsigned char	   *data;
	unsigned int	    j;
	/* Data block size for testing hash. Maximum
	 * kmalloc size for 2.6.18 kernel is 128K */
	unsigned int	    data_len = 1 * 128 * 1024;

	data = kmalloc(data_len, 0);
	if (data == NULL) {
		CERROR("Failed to allocate mem\n");
		return -ENOMEM;
	}

	for (j = 0; j < data_len; j++)
		data[j] = j & 0xff;

	for (i = 0; i < CFS_HASH_ALG_MAX; i++)
		cfs_crypto_performance_test(i, data, data_len);

	kfree(data);
	return 0;
}

static int adler32;

int cfs_crypto_register(void)
{
	request_module("crc32c");

	adler32 = cfs_crypto_adler32_register();

	/* check all algorithms and do performance test */
	cfs_crypto_test_hashes();
	return 0;
}
void cfs_crypto_unregister(void)
{
	if (adler32 == 0)
		cfs_crypto_adler32_unregister();

	return;
}
