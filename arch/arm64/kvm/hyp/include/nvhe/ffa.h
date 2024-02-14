/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 - Google LLC
 * Author: Andrew Walbran <qwandor@google.com>
 */
#ifndef __KVM_HYP_FFA_H
#define __KVM_HYP_FFA_H

#include <asm/kvm_host.h>

#define FFA_MIN_FUNC_NUM 0x60
#define FFA_MAX_FUNC_NUM 0x7F

int hyp_ffa_init(void *pages);
bool kvm_host_ffa_handler(struct kvm_cpu_context *host_ctxt);

#endif /* __KVM_HYP_FFA_H */
