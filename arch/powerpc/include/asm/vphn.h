/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_VPHN_H
#define _ASM_POWERPC_VPHN_H

/* The H_HOME_NODE_ASSOCIATIVITY h_call returns 6 64-bit registers. */
#define VPHN_REGISTER_COUNT 6

/*
 * 6 64-bit registers unpacked into up to 24 be32 associativity values. To
 * form the complete property we have to add the length in the first cell.
 */
#define VPHN_ASSOC_BUFSIZE (VPHN_REGISTER_COUNT*sizeof(u64)/sizeof(u16) + 1)

/*
 * The H_HOME_NODE_ASSOCIATIVITY hcall takes two values for flags:
 * 1 for retrieving associativity information for a guest cpu
 * 2 for retrieving associativity information for a host/hypervisor cpu
 */
#define VPHN_FLAG_VCPU	1
#define VPHN_FLAG_PCPU	2

long hcall_vphn(unsigned long cpu, u64 flags, __be32 *associativity);

#endif // _ASM_POWERPC_VPHN_H
