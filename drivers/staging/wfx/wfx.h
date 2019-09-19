/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common private data for Silicon Labs WFx chips.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 */
#ifndef WFX_H
#define WFX_H

#include "bh.h"
#include "main.h"
#include "hif_api_general.h"

struct hwbus_ops;

struct wfx_dev {
	struct wfx_platform_data pdata;
	struct device		*dev;
	const struct hwbus_ops	*hwbus_ops;
	void			*hwbus_priv;

	u8			keyset;
	struct hif_ind_startup	hw_caps;
	struct wfx_hif		hif;
};

#endif /* WFX_H */
