/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Google, Inc.
 */

#ifndef _LINUX_PAGE_RANGE_HELPER_H
#define _LINUX_PAGE_RANGE_HELPER_H

#include <linux/list_lru.h>

enum lru_status
rust_shrink_free_page_wrap(struct list_head *item, struct list_lru_one *list,
			   void *cb_arg);

#endif /* _LINUX_PAGE_RANGE_HELPER_H */
