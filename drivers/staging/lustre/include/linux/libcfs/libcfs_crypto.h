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
 */

#ifndef _LIBCFS_CRYPTO_H
#define _LIBCFS_CRYPTO_H

struct cfs_crypto_hash_type {
	char		*cht_name;      /**< hash algorithm name, equal to
					 * format name for crypto api */
	unsigned int    cht_key;	/**< init key by default (vaild for
					 * 4 bytes context like crc32, adler */
	unsigned int    cht_size;       /**< hash digest size */
};

enum cfs_crypto_hash_alg {
	CFS_HASH_ALG_NULL       = 0,
	CFS_HASH_ALG_ADLER32,
	CFS_HASH_ALG_CRC32,
	CFS_HASH_ALG_MD5,
	CFS_HASH_ALG_SHA1,
	CFS_HASH_ALG_SHA256,
	CFS_HASH_ALG_SHA384,
	CFS_HASH_ALG_SHA512,
	CFS_HASH_ALG_CRC32C,
	CFS_HASH_ALG_MAX
};

static struct cfs_crypto_hash_type hash_types[] = {
	[CFS_HASH_ALG_NULL]    = { "null",     0,      0 },
	[CFS_HASH_ALG_ADLER32] = { "adler32",  1,      4 },
	[CFS_HASH_ALG_CRC32]   = { "crc32",   ~0,      4 },
	[CFS_HASH_ALG_CRC32C]  = { "crc32c",  ~0,      4 },
	[CFS_HASH_ALG_MD5]     = { "md5",      0,     16 },
	[CFS_HASH_ALG_SHA1]    = { "sha1",     0,     20 },
	[CFS_HASH_ALG_SHA256]  = { "sha256",   0,     32 },
	[CFS_HASH_ALG_SHA384]  = { "sha384",   0,     48 },
	[CFS_HASH_ALG_SHA512]  = { "sha512",   0,     64 },
};

/**    Return pointer to type of hash for valid hash algorithm identifier */
static inline const struct cfs_crypto_hash_type *
		    cfs_crypto_hash_type(unsigned char hash_alg)
{
	struct cfs_crypto_hash_type *ht;

	if (hash_alg < CFS_HASH_ALG_MAX) {
		ht = &hash_types[hash_alg];
		if (ht->cht_name)
			return ht;
	}
	return NULL;
}

/**     Return hash name for valid hash algorithm identifier or "unknown" */
static inline const char *cfs_crypto_hash_name(unsigned char hash_alg)
{
	const struct cfs_crypto_hash_type *ht;

	ht = cfs_crypto_hash_type(hash_alg);
	if (ht)
		return ht->cht_name;
	else
		return "unknown";
}

/**     Return digest size for valid algorithm identifier or 0 */
static inline int cfs_crypto_hash_digestsize(unsigned char hash_alg)
{
	const struct cfs_crypto_hash_type *ht;

	ht = cfs_crypto_hash_type(hash_alg);
	if (ht)
		return ht->cht_size;
	else
		return 0;
}

/**     Return hash identifier for valid hash algorithm name or 0xFF */
static inline unsigned char cfs_crypto_hash_alg(const char *algname)
{
	unsigned char   i;

	for (i = 0; i < CFS_HASH_ALG_MAX; i++)
		if (!strcmp(hash_types[i].cht_name, algname))
			break;
	return (i == CFS_HASH_ALG_MAX ? 0xFF : i);
}

/**     Calculate hash digest for buffer.
 *      @param alg	    id of hash algorithm
 *      @param buf	    buffer of data
 *      @param buf_len	buffer len
 *      @param key	    initial value for algorithm, if it is NULL,
 *			    default initial value should be used.
 *      @param key_len	len of initial value
 *      @param hash	   [out] pointer to hash, if it is NULL, hash_len is
 *			    set to valid digest size in bytes, retval -ENOSPC.
 *      @param hash_len       [in,out] size of hash buffer
 *      @returns	      status of operation
 *      @retval -EINVAL       if buf, buf_len, hash_len or alg_id is invalid
 *      @retval -ENODEV       if this algorithm is unsupported
 *      @retval -ENOSPC       if pointer to hash is NULL, or hash_len less than
 *			    digest size
 *      @retval 0	     for success
 *      @retval < 0	   other errors from lower layers.
 */
int cfs_crypto_hash_digest(unsigned char alg,
			   const void *buf, unsigned int buf_len,
			   unsigned char *key, unsigned int key_len,
			   unsigned char *hash, unsigned int *hash_len);

/* cfs crypto hash descriptor */
struct cfs_crypto_hash_desc;

/**     Allocate and initialize desriptor for hash algorithm.
 *      @param alg	    algorithm id
 *      @param key	    initial value for algorithm, if it is NULL,
 *			    default initial value should be used.
 *      @param key_len	len of initial value
 *      @returns	      pointer to descriptor of hash instance
 *      @retval ERR_PTR(error) when errors occured.
 */
struct cfs_crypto_hash_desc*
	cfs_crypto_hash_init(unsigned char alg,
			     unsigned char *key, unsigned int key_len);

/**    Update digest by part of data.
 *     @param desc	      hash descriptor
 *     @param page	      data page
 *     @param offset	    data offset
 *     @param len	       data len
 *     @returns		 status of operation
 *     @retval 0		for success.
 */
int cfs_crypto_hash_update_page(struct cfs_crypto_hash_desc *desc,
				struct page *page, unsigned int offset,
				unsigned int len);

/**    Update digest by part of data.
 *     @param desc	      hash descriptor
 *     @param buf	       pointer to data buffer
 *     @param buf_len	   size of data at buffer
 *     @returns		 status of operation
 *     @retval 0		for success.
 */
int cfs_crypto_hash_update(struct cfs_crypto_hash_desc *desc, const void *buf,
			   unsigned int buf_len);

/**    Finalize hash calculation, copy hash digest to buffer, destroy hash
 *     descriptor.
 *     @param desc	      hash descriptor
 *     @param hash	      buffer pointer to store hash digest
 *     @param hash_len	  pointer to hash buffer size, if NULL
 *			      destory hash descriptor
 *     @returns		 status of operation
 *     @retval -ENOSPC	  if hash is NULL, or *hash_len less than
 *			      digest size
 *     @retval 0		for success
 *     @retval < 0	      other errors from lower layers.
 */
int cfs_crypto_hash_final(struct cfs_crypto_hash_desc *desc,
			  unsigned char *hash, unsigned int *hash_len);
/**
 *      Register crypto hash algorithms
 */
int cfs_crypto_register(void);

/**
 *      Unregister
 */
void cfs_crypto_unregister(void);

/**     Return hash speed in Mbytes per second for valid hash algorithm
 *      identifier. If test was unsuccessfull -1 would be return.
 */
int cfs_crypto_hash_speed(unsigned char hash_alg);
#endif
