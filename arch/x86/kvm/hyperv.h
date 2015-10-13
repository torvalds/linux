/*
 * KVM Microsoft Hyper-V emulation
 *
 * derived from arch/x86/kvm/x86.c
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright (C) 2008 Qumranet, Inc.
 * Copyright IBM Corporation, 2008
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Amit Shah    <amit.shah@qumranet.com>
 *   Ben-Ami Yassour <benami@il.ibm.com>
 *   Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef __ARCH_X86_KVM_HYPERV_H__
#define __ARCH_X86_KVM_HYPERV_H__

int kvm_hv_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data, bool host);
int kvm_hv_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata);
bool kvm_hv_hypercall_enabled(struct kvm *kvm);
int kvm_hv_hypercall(struct kvm_vcpu *vcpu);

#endif
