/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 IBM Corporation
 * Author: Nayna Jain
 *
 * PowerPC secure variable operations.
 */
#ifndef SECVAR_OPS_H
#define SECVAR_OPS_H

#include <linux/types.h>
#include <linux/errno.h>

extern const struct secvar_operations *secvar_ops;

struct secvar_operations {
	int (*get)(const char *key, uint64_t key_len, u8 *data,
		   uint64_t *data_size);
	int (*get_next)(const char *key, uint64_t *key_len,
			uint64_t keybufsize);
	int (*set)(const char *key, uint64_t key_len, u8 *data,
		   uint64_t data_size);
};

#ifdef CONFIG_PPC_SECURE_BOOT

extern void set_secvar_ops(const struct secvar_operations *ops);

#else

static inline void set_secvar_ops(const struct secvar_operations *ops) { }

#endif

#endif
