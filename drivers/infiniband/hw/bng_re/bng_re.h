/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_RE_H__
#define __BNG_RE_H__

#define BNG_RE_ADEV_NAME		"bng_en"

#define BNG_RE_DESC	"Broadcom 800G RoCE Driver"

#define	rdev_to_dev(rdev)	((rdev) ? (&(rdev)->ibdev.dev) : NULL)

struct bng_re_en_dev_info {
	struct bng_re_dev *rdev;
	struct bnge_auxr_dev *auxr_dev;
};

struct bng_re_dev {
	struct ib_device		ibdev;
	struct net_device		*netdev;
	struct auxiliary_device         *adev;
	struct bnge_auxr_dev		*aux_dev;
	int				fn_id;
};

#endif
