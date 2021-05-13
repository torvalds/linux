// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stubs for out-of-line function calls caused by re-using kernel
 * infrastructure at EL2.
 *
 * Copyright (C) 2020 - Google LLC
 */

#include <linux/list.h>

#ifdef CONFIG_DEBUG_LIST
bool __list_add_valid(struct list_head *new, struct list_head *prev,
		      struct list_head *next)
{
		return true;
}

bool __list_del_entry_valid(struct list_head *entry)
{
		return true;
}
#endif
