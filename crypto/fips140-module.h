/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2021 Google LLC
 */

#ifndef _CRYPTO_FIPS140_MODULE_H
#define _CRYPTO_FIPS140_MODULE_H

#include <linux/completion.h>
#include <linux/module.h>
#include <generated/utsrelease.h>

#undef pr_fmt
#define pr_fmt(fmt) "fips140: " fmt

/*
 * This is the name and version number of the module that are shown on the FIPS
 * certificate.
 */
#define FIPS140_MODULE_NAME "Android Kernel Cryptographic Module"
#define FIPS140_MODULE_VERSION UTS_RELEASE

/* fips140-eval-testing.c */
#ifdef CONFIG_CRYPTO_FIPS140_MOD_EVAL_TESTING
void fips140_inject_selftest_failure(const char *impl, u8 *result);
void fips140_inject_integrity_failure(u8 *textcopy);
bool fips140_eval_testing_init(void);
#else
static inline void fips140_inject_selftest_failure(const char *impl, u8 *result)
{
}
static inline void fips140_inject_integrity_failure(u8 *textcopy)
{
}
static inline bool fips140_eval_testing_init(void)
{
	return true;
}
#endif /* !CONFIG_CRYPTO_FIPS140_MOD_EVAL_TESTING */

/* fips140-module.c */
extern struct completion fips140_tests_done;
extern struct task_struct *fips140_init_thread;
bool fips140_is_approved_service(const char *name);
const char *fips140_module_version(void);

/* fips140-selftests.c */
bool __init __must_check fips140_run_selftests(void);

#endif /* _CRYPTO_FIPS140_MODULE_H */
