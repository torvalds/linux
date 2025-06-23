/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#ifndef __LINUX_BPF_PERF_EVENT_H__
#define __LINUX_BPF_PERF_EVENT_H__

#include <asm/bpf_perf_event.h>

struct bpf_perf_event_data {
	bpf_user_pt_regs_t regs;
	__u64 sample_period;
	__u64 addr;
};

#endif /* __LINUX_BPF_PERF_EVENT_H__ */
