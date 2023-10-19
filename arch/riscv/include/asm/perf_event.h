/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 SiFive
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#ifndef _ASM_RISCV_PERF_EVENT_H
#define _ASM_RISCV_PERF_EVENT_H

#include <linux/perf_event.h>
#define perf_arch_bpf_user_pt_regs(regs) (struct user_regs_struct *)regs
#endif /* _ASM_RISCV_PERF_EVENT_H */
