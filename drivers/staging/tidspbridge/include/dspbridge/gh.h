/*
 * gh.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef GH_
#define GH_
#include <dspbridge/host_os.h>

extern struct gh_t_hash_tab *gh_create(u32 val_size,
	u32 (*hash)(const void *), bool (*match)(const void *,
	const void *), void (*delete) (void *));
extern void gh_delete(struct gh_t_hash_tab *hash_tab);
extern void *gh_find(struct gh_t_hash_tab *hash_tab, const void *key);
extern void *gh_insert(struct gh_t_hash_tab *hash_tab, const void *key,
		       const void *value);
#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
void gh_iterate(struct gh_t_hash_tab *hash_tab,
	void (*callback)(void *, void *), void *user_data);
#endif
#endif /* GH_ */
