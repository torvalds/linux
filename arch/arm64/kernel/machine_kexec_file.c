// SPDX-License-Identifier: GPL-2.0
/*
 * kexec_file for arm64
 *
 * Copyright (C) 2018 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 */

#define pr_fmt(fmt) "kexec_file: " fmt

#include <linux/kexec.h>

const struct kexec_file_ops * const kexec_file_loaders[] = {
	NULL
};
