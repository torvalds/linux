// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <linux/export.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
EXPORT_SYMBOL_GPL(kvmppc_hv_entry_trampoline);
#endif
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
EXPORT_SYMBOL_GPL(kvmppc_entry_trampoline);
#endif

