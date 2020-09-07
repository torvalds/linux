/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Encrypted Register State Support
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#ifndef __ASM_ENCRYPTED_STATE_H
#define __ASM_ENCRYPTED_STATE_H

#include <linux/types.h>

#define GHCB_SEV_CPUID_REQ	0x004UL
#define		GHCB_CPUID_REQ_EAX	0
#define		GHCB_CPUID_REQ_EBX	1
#define		GHCB_CPUID_REQ_ECX	2
#define		GHCB_CPUID_REQ_EDX	3
#define		GHCB_CPUID_REQ(fn, reg) (GHCB_SEV_CPUID_REQ | \
					(((unsigned long)reg & 3) << 30) | \
					(((unsigned long)fn) << 32))

#define GHCB_SEV_CPUID_RESP	0x005UL
#define GHCB_SEV_TERMINATE	0x100UL

#define	GHCB_SEV_GHCB_RESP_CODE(v)	((v) & 0xfff)
#define	VMGEXIT()			{ asm volatile("rep; vmmcall\n\r"); }

void do_vc_no_ghcb(struct pt_regs *regs, unsigned long exit_code);

static inline u64 lower_bits(u64 val, unsigned int bits)
{
	u64 mask = (1ULL << bits) - 1;

	return (val & mask);
}

#endif
