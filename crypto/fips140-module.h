/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2021 Google LLC
 */

#ifndef _CRYPTO_FIPS140_MODULE_H
#define _CRYPTO_FIPS140_MODULE_H

#include <linux/completion.h>
#include <linux/module.h>

#undef pr_fmt
#define pr_fmt(fmt) "fips140: " fmt

#ifdef CONFIG_CRYPTO_FIPS140_MOD_ERROR_INJECTION
extern char *fips140_broken_alg;
#endif

extern struct completion fips140_tests_done;
extern struct task_struct *fips140_init_thread;

bool __init __must_check fips140_run_selftests(void);

#endif /* _CRYPTO_FIPS140_MODULE_H */
