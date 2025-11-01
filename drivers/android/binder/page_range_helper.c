// SPDX-License-Identifier: GPL-2.0

/* C helper for page_range.rs to work around a CFI violation.
 *
 * Bindgen currently pretends that `enum lru_status` is the same as an integer.
 * This assumption is fine ABI-wise, but once you add CFI to the mix, it
 * triggers a CFI violation because `enum lru_status` gets a different CFI tag.
 *
 * This file contains a workaround until bindgen can be fixed.
 *
 * Copyright (C) 2025 Google LLC.
 */
#include "page_range_helper.h"

unsigned int rust_shrink_free_page(struct list_head *item,
				   struct list_lru_one *list,
				   void *cb_arg);

enum lru_status
rust_shrink_free_page_wrap(struct list_head *item, struct list_lru_one *list,
			   void *cb_arg)
{
	return rust_shrink_free_page(item, list, cb_arg);
}
