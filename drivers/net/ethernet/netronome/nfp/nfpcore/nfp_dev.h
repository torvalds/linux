/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef _NFP_DEV_H_
#define _NFP_DEV_H_

enum nfp_dev_id {
	NFP_DEV_NFP6000,
	NFP_DEV_CNT,
};

struct nfp_dev_info {
	const char *chip_names;
};

extern const struct nfp_dev_info nfp_dev_info[NFP_DEV_CNT];

#endif
