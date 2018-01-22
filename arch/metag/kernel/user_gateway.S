/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 * This file contains code that can be accessed from userspace and can
 * access certain kernel data structures without the overhead of a system
 * call.
 */

#include <asm/metag_regs.h>
#include <asm/user_gateway.h>

/*
 * User helpers.
 *
 * These are segment of kernel provided user code reachable from user space
 * at a fixed address in kernel memory.  This is used to provide user space
 * with some operations which require kernel help because of unimplemented
 * native feature and/or instructions in some Meta CPUs. The idea is for
 * this code to be executed directly in user mode for best efficiency but
 * which is too intimate with the kernel counter part to be left to user
 * libraries.  The kernel reserves the right to change this code as needed
 * without warning. Only the entry points and their results are guaranteed
 * to be stable.
 *
 * Each segment is 64-byte aligned.  This mechanism should be used only for
 * for things that are really small and justified, and not be abused freely.
 */
	.text
	.global	___user_gateway_start
___user_gateway_start:

	/* get_tls
	 * Offset:	 0
	 * Description:	 Get the TLS pointer for this process.
	 */
	.global	___kuser_get_tls
	.type	___kuser_get_tls,function
___kuser_get_tls:
	MOVT	D1Ar1,#HI(USER_GATEWAY_PAGE + USER_GATEWAY_TLS)
	ADD	D1Ar1,D1Ar1,#LO(USER_GATEWAY_PAGE + USER_GATEWAY_TLS)
	MOV	D1Ar3,TXENABLE
	AND	D1Ar3,D1Ar3,#(TXENABLE_THREAD_BITS)
	LSR	D1Ar3,D1Ar3,#(TXENABLE_THREAD_S - 2)
	GETD	D0Re0,[D1Ar1+D1Ar3]
___kuser_get_tls_end:		/* Beyond this point the read will complete */
	MOV	PC,D1RtP
	.size	___kuser_get_tls,.-___kuser_get_tls
	.global	___kuser_get_tls_end

	/* cmpxchg
	 * Offset:	 64
	 * Description:  Replace the value at 'ptr' with 'newval' if the current
	 *		 value is 'oldval'. Return zero if we succeeded,
	 *		 non-zero otherwise.
	 *
	 * Reference prototype:
	 *
	 *	int __kuser_cmpxchg(int oldval, int newval, unsigned long *ptr)
	 *
	 */
	.balign 64
	.global ___kuser_cmpxchg
	.type   ___kuser_cmpxchg,function
___kuser_cmpxchg:
#ifdef CONFIG_SMP
	/*
	 * We must use LNKGET/LNKSET with an SMP kernel because the other method
	 * does not provide atomicity across multiple CPUs.
	 */
0:	LNKGETD	D0Re0,[D1Ar3]
	CMP	D0Re0,D1Ar1
	LNKSETDZ [D1Ar3],D0Ar2
	BNZ	1f
	DEFR	D0Re0,TXSTAT
	ANDT	D0Re0,D0Re0,#HI(0x3f000000)
	CMPT	D0Re0,#HI(0x02000000)
	BNE	0b
#ifdef CONFIG_METAG_LNKGET_AROUND_CACHE
	DCACHE  [D1Ar3], D0Re0
#endif
1:	MOV	D0Re0,#1
	XORZ	D0Re0,D0Re0,D0Re0
	MOV	PC,D1RtP
#else
	GETD	D0Re0,[D1Ar3]
	CMP	D0Re0,D1Ar1
	SETDZ	[D1Ar3],D0Ar2
___kuser_cmpxchg_end:		/* Beyond this point the write will complete */
	MOV	D0Re0,#1
	XORZ	D0Re0,D0Re0,D0Re0
	MOV	PC,D1RtP
#endif /* CONFIG_SMP */
	.size	___kuser_cmpxchg,.-___kuser_cmpxchg
	.global	___kuser_cmpxchg_end

	.global	___user_gateway_end
___user_gateway_end:
