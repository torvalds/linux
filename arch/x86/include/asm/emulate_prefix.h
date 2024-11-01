/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_EMULATE_PREFIX_H
#define _ASM_X86_EMULATE_PREFIX_H

/*
 * Virt escape sequences to trigger instruction emulation;
 * ideally these would decode to 'whole' instruction and not destroy
 * the instruction stream; sadly this is not true for the 'kvm' one :/
 */

#define __XEN_EMULATE_PREFIX  0x0f,0x0b,0x78,0x65,0x6e  /* ud2 ; .ascii "xen" */
#define __KVM_EMULATE_PREFIX  0x0f,0x0b,0x6b,0x76,0x6d	/* ud2 ; .ascii "kvm" */

#endif
