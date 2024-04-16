/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef __LOONGARCH_PERF_EVENT_H__
#define __LOONGARCH_PERF_EVENT_H__

#define perf_arch_bpf_user_pt_regs(regs) (struct user_pt_regs *)regs

#endif /* __LOONGARCH_PERF_EVENT_H__ */
