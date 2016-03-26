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
	unsigned int    cht_key;	/**< init key by default (valid for
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

/**
 * Return hash algorithm information for the specified algorithm identifier
 *
 * Hash information includes algorithm name, initial seed, hash size.
 *
 * \retval	cfs_crypto_hash_type for valid ID (CFS_HASH_ALG_*)
 * \retval	NULL for unknown algorithm identifier
 */
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

/**
 * Return hash name for hash algorithm identifier
 *
 * \param[in]	hash_alg hash alrgorithm id (CFS_HASH_ALG_*)
 *
 * \retval	string name of known hash algorithm
 * \retval	"unknown" if hash algorithm is unknown
 */
static inline const char *cfs_crypto_hash_name(unsigned char hash_alg)
{
	const struct cfs_crypto_hash_type *ht;

	ht = cfs_crypto_hash_type(hash_alg);
	if (ht)
		return ht->cht_name;
	return "unknown";
}

/**
 * Return digest size for hash algorithm type
 *
 * \param[in]	hash_alg hash alrgorithm id (CFS_HASH_ALG_*)
 *
 * \retval	hash algorithm digest size in bytes
 * \retval	0 if hash algorithm type is unknown
 */
static inline int cfs_crypto_hash_digestsize(unsigned char hash_alg)
{
	const struct cfs_crypto_hash_type *ht;

	ht = cfs_crypto_hash_type(hash_alg);
	if (ht)
		return ht->cht_size;
	return 0;
}

/**
 * Find hash algorithm ID for the specified algorithm name
 *
 * \retval	hash algorithm ID for valid ID (CFS_HASH_ALG_*)
 * \retval	CFS_HASH_ALG_UNKNOWN for unknown algorithm name
 */
static inline unsigned char cfs_crypto_hash_alg(const char *algname)
{
	unsigned char   i;

	for (i = 0; i < CFS_HASH_ALG_MAX; i++)
		if (!strcmp(hash_types[i].cht_name, algname))
			break;
	return (i == CFS_HASH_ALG_MAX ? 0xFF : i);
}

int cfs_crypto_hash_digest(unsigned char hash_alg,
			   const void *buf, unsigned int buf_len,
			   unsigned char *key, unsigned int key_len,
			   unsigned char *hash, unsigned int *hash_len);

/* cfs crypto hash descriptor */
struct cfs_crypto_hash_desc;

struct cfs_crypto_hash_desc*
	cfs_crypto_hash_init(unsigned char hash_alg,
			     unsigned char *key, unsigned int key_len);
int cfs_crypto_hash_update_page(struct cfs_crypto_hash_desc *desc,
				struct page *page, unsigned int offset,
				unsigned int len);
int cfs_crypto_hash_update(struct cfs_crypto_hash_desc *desc, const void *buf,
			   unsigned int buf_len);
int cfs_crypto_hash_final(struct cfs_crypto_hash_desc *desc,
			  unsigned char *hash, unsigned int *hash_len);
int cfs_crypto_register(void);
void cfs_crypto_unregister(void);
int cfs_crypto_hash_speed(unsigned char hash_alg);
#endif
