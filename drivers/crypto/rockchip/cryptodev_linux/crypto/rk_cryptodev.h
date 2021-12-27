/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2021 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTODEV_H__
#define __RK_CRYPTODEV_H__

#include <linux/types.h>
#include <linux/version.h>
#include "cryptodev.h"

#ifndef __KERNEL__
#define __user
#endif

/* input of RIOCCRYPT_FD */
struct crypt_fd_op {
	__u32	ses;		/* session identifier */
	__u16	op;		/* COP_ENCRYPT or COP_DECRYPT */
	__u16	flags;		/* see COP_FLAG_* */
	__u32	len;		/* length of source data */
	int	src_fd;		/* source data */
	int	dst_fd;		/* pointer to output data */
	/* pointer to output data for hash/MAC operations */
	__u8	__user *mac;
	/* initialization vector for encryption operations */
	__u8	__user *iv;
};

/* input of RIOCCRYPT_FD_MAP/RIOCCRYPT_FD_UNMAP */
struct crypt_fd_map_op {
	int	dma_fd;		/* session identifier */
	__u32	phys_addr;	/* physics addr */
};

#define RIOCCRYPT_FD		_IOWR('r', 104, struct crypt_fd_op)
#define RIOCCRYPT_FD_MAP	_IOWR('r', 105, struct crypt_fd_map_op)
#define RIOCCRYPT_FD_UNMAP	_IOW('r',  106, struct crypt_fd_map_op)
#define RIOCCRYPT_CPU_ACCESS	_IOW('r',  107, struct crypt_fd_map_op)
#define RIOCCRYPT_DEV_ACCESS	_IOW('r',  108, struct crypt_fd_map_op)

#endif
