/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2021 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTODEV_H__
#define __RK_CRYPTODEV_H__

#include <linux/device.h>
#include <uapi/linux/rk_cryptodev.h>
#include "cryptodev.h"

/* compatibility stuff */
#ifdef CONFIG_COMPAT
#include <linux/compat.h>

/* input of RIOCCRYPT_FD */
struct compat_crypt_fd_op {
	uint32_t	ses;		/* session identifier */
	uint16_t	op;		/* COP_ENCRYPT or COP_DECRYPT */
	uint16_t	flags;		/* see COP_FLAG_* */
	uint32_t	len;		/* length of source data */
	int		src_fd;		/* source data */
	int		dst_fd;		/* pointer to output data */
	compat_uptr_t	mac;/* pointer to output data for hash/MAC operations */
	compat_uptr_t	iv;/* initialization vector for encryption operations */
};

/* input of RIOCCRYPT_FD_MAP/RIOCCRYPT_FD_UNMAP */
struct compat_crypt_fd_map_op {
	int		dma_fd;		/* session identifier */
	uint32_t	phys_addr;	/* physics addr */
};

/* compat ioctls, defined for the above structs */
#define COMPAT_RIOCCRYPT_FD		_IOWR('r', 104, struct compat_crypt_fd_op)
#define COMPAT_RIOCCRYPT_FD_MAP		_IOWR('r', 105, struct compat_crypt_fd_map_op)
#define COMPAT_RIOCCRYPT_FD_UNMAP	_IOW('r',  106, struct compat_crypt_fd_map_op)
#define COMPAT_RIOCCRYPT_CPU_ACCESS	_IOW('r',  107, struct compat_crypt_fd_map_op)
#define COMPAT_RIOCCRYPT_DEV_ACCESS	_IOW('r',  108, struct compat_crypt_fd_map_op)


#endif /* CONFIG_COMPAT */

/* kernel-internal extension to struct crypt_op */
struct kernel_crypt_fd_op {
	struct crypt_fd_op cop;

	int ivlen;
	__u8 iv[EALG_MAX_BLOCK_LEN];

	int digestsize;
	uint8_t hash_output[AALG_MAX_RESULT_LEN];

	struct task_struct *task;
	struct mm_struct *mm;
};

struct kernel_crypt_auth_fd_op {
	struct crypt_auth_fd_op caop;

	int dst_len; /* based on src_len */
	__u8 iv[EALG_MAX_BLOCK_LEN];
	int ivlen;

	struct task_struct *task;
	struct mm_struct *mm;
};

/* kernel-internal extension to struct crypt_fd_map_op */
struct kernel_crypt_fd_map_op {
	struct crypt_fd_map_op mop;
};

/* kernel-internal extension to struct crypt_op */
struct kernel_crypt_rsa_op {
	struct crypt_rsa_op rop;

	struct task_struct *task;
	struct mm_struct *mm;
};

#if IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP_DEV)
int rk_cryptodev_register_dev(struct device *dev, const char *name);
int rk_cryptodev_unregister_dev(struct device *dev);
#else
static inline int rk_cryptodev_register_dev(struct device *dev, const char *name)
{
	return 0;
}

static inline int rk_cryptodev_unregister_dev(struct device *dev)
{
	return 0;
}
#endif

long
rk_cryptodev_ioctl(struct fcrypt *fcr, unsigned int cmd, unsigned long arg_);

long
rk_compat_cryptodev_ioctl(struct fcrypt *fcr, unsigned int cmd, unsigned long arg_);

const char *rk_get_cipher_name(uint32_t id, int *is_stream, int *is_aead);

const char *rk_get_hash_name(uint32_t id, int *is_hmac);

bool rk_cryptodev_multi_thread(const char *name);

#endif
