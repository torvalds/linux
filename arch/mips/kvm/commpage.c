/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * commpage, currently used for Virtual COP0 registers.
 * Mapped into the guest kernel @ 0x0.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/bootmem.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>

#include <linux/kvm_host.h>

#include "commpage.h"

void kvm_mips_commpage_init(struct kvm_vcpu *vcpu)
{
	struct kvm_mips_commpage *page = vcpu->arch.kseg0_commpage;

	/* Specific init values for fields */
	vcpu->arch.cop0 = &page->cop0;
}
