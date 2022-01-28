/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Linaro Ltd
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 */
#ifndef ___DTPM_SUBSYS_H__
#define ___DTPM_SUBSYS_H__

extern struct dtpm_subsys_ops dtpm_cpu_ops;

struct dtpm_subsys_ops *dtpm_subsys[] = {
#ifdef CONFIG_DTPM_CPU
	&dtpm_cpu_ops,
#endif
};

#endif
