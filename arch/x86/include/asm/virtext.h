/* SPDX-License-Identifier: GPL-2.0-only */
/* CPU virtualization extensions handling
 *
 * This should carry the code for handling CPU virtualization extensions
 * that needs to live in the kernel core.
 *
 * Author: Eduardo Habkost <ehabkost@redhat.com>
 *
 * Copyright (C) 2008, Red Hat Inc.
 *
 * Contains code from KVM, Copyright (C) 2006 Qumranet, Inc.
 */
#ifndef _ASM_X86_VIRTEX_H
#define _ASM_X86_VIRTEX_H

#include <asm/processor.h>

#include <asm/vmx.h>
#include <asm/svm.h>
#include <asm/tlbflush.h>

/*
 * SVM functions:
 */
/** Disable SVM on the current CPU
 */
static inline void cpu_svm_disable(void)
{
	uint64_t efer;

	wrmsrl(MSR_VM_HSAVE_PA, 0);
	rdmsrl(MSR_EFER, efer);
	if (efer & EFER_SVME) {
		/*
		 * Force GIF=1 prior to disabling SVM to ensure INIT and NMI
		 * aren't blocked, e.g. if a fatal error occurred between CLGI
		 * and STGI.  Note, STGI may #UD if SVM is disabled from NMI
		 * context between reading EFER and executing STGI.  In that
		 * case, GIF must already be set, otherwise the NMI would have
		 * been blocked, so just eat the fault.
		 */
		asm_volatile_goto("1: stgi\n\t"
				  _ASM_EXTABLE(1b, %l[fault])
				  ::: "memory" : fault);
fault:
		wrmsrl(MSR_EFER, efer & ~EFER_SVME);
	}
}

#endif /* _ASM_X86_VIRTEX_H */
