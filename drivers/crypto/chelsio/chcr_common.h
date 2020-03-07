/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020 Chelsio Communications.  All rights reserved. */

#ifndef __CHCR_COMMON_H__
#define __CHCR_COMMON_H__

#include "cxgb4.h"

enum chcr_state {
	CHCR_INIT = 0,
	CHCR_ATTACH,
	CHCR_DETACH,
};

struct chcr_dev {
	spinlock_t lock_chcr_dev; /* chcr dev structure lock */
	enum chcr_state state;
	atomic_t inflight;
	int wqretry;
	struct delayed_work detach_work;
	struct completion detach_comp;
	unsigned char tx_channel_id;
};

struct uld_ctx {
	struct list_head entry;
	struct cxgb4_lld_info lldi;
	struct chcr_dev dev;
};

struct uld_ctx *assign_chcr_device(void);
#endif /* __CHCR_COMMON_H__ */
