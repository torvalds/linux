/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * fs-verity user API
 *
 * These ioctls can be used on filesystems that support fs-verity.  See the
 * "User API" section of Documentation/filesystems/fsverity.rst.
 *
 * Copyright 2019 Google LLC
 */
#ifndef _LINUX_FSVERITY_H
#define _LINUX_FSVERITY_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FS_VERITY_HASH_ALG_SHA256	1
#define FS_VERITY_HASH_ALG_SHA512	2

struct fsverity_enable_arg {
	__u32 version;
	__u32 hash_algorithm;
	__u32 block_size;
	__u32 salt_size;
	__u64 salt_ptr;
	__u32 sig_size;
	__u32 __reserved1;
	__u64 sig_ptr;
	__u64 __reserved2[11];
};

struct fsverity_digest {
	__u16 digest_algorithm;
	__u16 digest_size; /* input/output */
	__u8 digest[];
};

/*
 * Struct containing a file's Merkle tree properties.  The fs-verity file digest
 * is the hash of this struct.  A userspace program needs this struct only if it
 * needs to compute fs-verity file digests itself, e.g. in order to sign files.
 * It isn't needed just to enable fs-verity on a file.
 *
 * Note: when computing the file digest, 'sig_size' and 'signature' must be left
 * zero and empty, respectively.  These fields are present only because some
 * filesystems reuse this struct as part of their on-disk format.
 */
struct fsverity_descriptor {
	__u8 version;		/* must be 1 */
	__u8 hash_algorithm;	/* Merkle tree hash algorithm */
	__u8 log_blocksize;	/* log2 of size of data and tree blocks */
	__u8 salt_size;		/* size of salt in bytes; 0 if none */
	__le32 __reserved_0x04;	/* must be 0 */
	__le64 data_size;	/* size of file the Merkle tree is built over */
	__u8 root_hash[64];	/* Merkle tree root hash */
	__u8 salt[32];		/* salt prepended to each hashed block */
	__u8 __reserved[144];	/* must be 0's */
};

/*
 * Format in which fs-verity file digests are signed in built-in signatures.
 * This is the same as 'struct fsverity_digest', except here some magic bytes
 * are prepended to provide some context about what is being signed in case the
 * same key is used for non-fsverity purposes, and here the fields have fixed
 * endianness.
 *
 * This struct is specific to the built-in signature verification support, which
 * is optional.  fs-verity users may also verify signatures in userspace, in
 * which case userspace is responsible for deciding on what bytes are signed.
 * This struct may still be used, but it doesn't have to be.  For example,
 * userspace could instead use a string like "sha256:$digest_as_hex_string".
 */
struct fsverity_formatted_digest {
	char magic[8];			/* must be "FSVerity" */
	__le16 digest_algorithm;
	__le16 digest_size;
	__u8 digest[];
};

#define FS_VERITY_METADATA_TYPE_MERKLE_TREE	1
#define FS_VERITY_METADATA_TYPE_DESCRIPTOR	2
#define FS_VERITY_METADATA_TYPE_SIGNATURE	3

struct fsverity_read_metadata_arg {
	__u64 metadata_type;
	__u64 offset;
	__u64 length;
	__u64 buf_ptr;
	__u64 __reserved;
};

#define FS_IOC_ENABLE_VERITY	_IOW('f', 133, struct fsverity_enable_arg)
#define FS_IOC_MEASURE_VERITY	_IOWR('f', 134, struct fsverity_digest)
#define FS_IOC_READ_VERITY_METADATA \
	_IOWR('f', 135, struct fsverity_read_metadata_arg)

#endif /* _LINUX_FSVERITY_H */
