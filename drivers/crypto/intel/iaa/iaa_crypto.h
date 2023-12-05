/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021 Intel Corporation. All rights rsvd. */

#ifndef __IAA_CRYPTO_H__
#define __IAA_CRYPTO_H__

#include <linux/crypto.h>
#include <linux/idxd.h>
#include <uapi/linux/idxd.h>

#define IDXD_SUBDRIVER_NAME		"crypto"

/* Representation of IAA workqueue */
struct iaa_wq {
	struct list_head	list;
	struct idxd_wq		*wq;

	struct iaa_device	*iaa_device;
};

/* Representation of IAA device with wqs, populated by probe */
struct iaa_device {
	struct list_head		list;
	struct idxd_device		*idxd;

	int				n_wq;
	struct list_head		wqs;
};

struct wq_table_entry {
	struct idxd_wq **wqs;
	int	max_wqs;
	int	n_wqs;
	int	cur_wq;
};

#endif
