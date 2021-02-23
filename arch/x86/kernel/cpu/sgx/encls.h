/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _X86_ENCLS_H
#define _X86_ENCLS_H

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <asm/asm.h>
#include <asm/traps.h>
#include "sgx.h"

enum sgx_encls_function {
	ECREATE	= 0x00,
	EADD	= 0x01,
	EINIT	= 0x02,
	EREMOVE	= 0x03,
	EDGBRD	= 0x04,
	EDGBWR	= 0x05,
	EEXTEND	= 0x06,
	ELDU	= 0x08,
	EBLOCK	= 0x09,
	EPA	= 0x0A,
	EWB	= 0x0B,
	ETRACK	= 0x0C,
};

/**
 * ENCLS_FAULT_FLAG - flag signifying an ENCLS return code is a trapnr
 *
 * ENCLS has its own (positive value) error codes and also generates
 * ENCLS specific #GP and #PF faults.  And the ENCLS values get munged
 * with system error codes as everything percolates back up the stack.
 * Unfortunately (for us), we need to precisely identify each unique
 * error code, e.g. the action taken if EWB fails varies based on the
 * type of fault and on the exact SGX error code, i.e. we can't simply
 * convert all faults to -EFAULT.
 *
 * To make all three error types coexist, we set bit 30 to identify an
 * ENCLS fault.  Bit 31 (technically bits N:31) is used to differentiate
 * between positive (faults and SGX error codes) and negative (system
 * error codes) values.
 */
#define ENCLS_FAULT_FLAG 0x40000000

/* Retrieve the encoded trapnr from the specified return code. */
#define ENCLS_TRAPNR(r) ((r) & ~ENCLS_FAULT_FLAG)

/* Issue a WARN() about an ENCLS function. */
#define ENCLS_WARN(r, name) {						  \
	do {								  \
		int _r = (r);						  \
		WARN_ONCE(_r, "%s returned %d (0x%x)\n", (name), _r, _r); \
	} while (0);							  \
}

/**
 * encls_failed() - Check if an ENCLS function failed
 * @ret:	the return value of an ENCLS function call
 *
 * Check if an ENCLS function failed. This happens when the function causes a
 * fault that is not caused by an EPCM conflict or when the function returns a
 * non-zero value.
 */
static inline bool encls_failed(int ret)
{
	if (ret & ENCLS_FAULT_FLAG)
		return ENCLS_TRAPNR(ret) != X86_TRAP_PF;

	return !!ret;
}

/**
 * __encls_ret_N - encode an ENCLS function that returns an error code in EAX
 * @rax:	function number
 * @inputs:	asm inputs for the function
 *
 * Emit assembly for an ENCLS function that returns an error code, e.g. EREMOVE.
 * And because SGX isn't complex enough as it is, function that return an error
 * code also modify flags.
 *
 * Return:
 *	0 on success,
 *	SGX error code on failure
 */
#define __encls_ret_N(rax, inputs...)				\
	({							\
	int ret;						\
	asm volatile(						\
	"1: .byte 0x0f, 0x01, 0xcf;\n\t"			\
	"2:\n"							\
	".section .fixup,\"ax\"\n"				\
	"3: orl $"__stringify(ENCLS_FAULT_FLAG)",%%eax\n"	\
	"   jmp 2b\n"						\
	".previous\n"						\
	_ASM_EXTABLE_FAULT(1b, 3b)				\
	: "=a"(ret)						\
	: "a"(rax), inputs					\
	: "memory", "cc");					\
	ret;							\
	})

#define __encls_ret_1(rax, rcx)		\
	({				\
	__encls_ret_N(rax, "c"(rcx));	\
	})

#define __encls_ret_2(rax, rbx, rcx)		\
	({					\
	__encls_ret_N(rax, "b"(rbx), "c"(rcx));	\
	})

#define __encls_ret_3(rax, rbx, rcx, rdx)			\
	({							\
	__encls_ret_N(rax, "b"(rbx), "c"(rcx), "d"(rdx));	\
	})

/**
 * __encls_N - encode an ENCLS function that doesn't return an error code
 * @rax:	function number
 * @rbx_out:	optional output variable
 * @inputs:	asm inputs for the function
 *
 * Emit assembly for an ENCLS function that does not return an error code, e.g.
 * ECREATE.  Leaves without error codes either succeed or fault.  @rbx_out is an
 * optional parameter for use by EDGBRD, which returns the requested value in
 * RBX.
 *
 * Return:
 *   0 on success,
 *   trapnr with ENCLS_FAULT_FLAG set on fault
 */
#define __encls_N(rax, rbx_out, inputs...)			\
	({							\
	int ret;						\
	asm volatile(						\
	"1: .byte 0x0f, 0x01, 0xcf;\n\t"			\
	"   xor %%eax,%%eax;\n"					\
	"2:\n"							\
	".section .fixup,\"ax\"\n"				\
	"3: orl $"__stringify(ENCLS_FAULT_FLAG)",%%eax\n"	\
	"   jmp 2b\n"						\
	".previous\n"						\
	_ASM_EXTABLE_FAULT(1b, 3b)				\
	: "=a"(ret), "=b"(rbx_out)				\
	: "a"(rax), inputs					\
	: "memory");						\
	ret;							\
	})

#define __encls_2(rax, rbx, rcx)				\
	({							\
	unsigned long ign_rbx_out;				\
	__encls_N(rax, ign_rbx_out, "b"(rbx), "c"(rcx));	\
	})

#define __encls_1_1(rax, data, rcx)			\
	({						\
	unsigned long rbx_out;				\
	int ret = __encls_N(rax, rbx_out, "c"(rcx));	\
	if (!ret)					\
		data = rbx_out;				\
	ret;						\
	})

static inline int __ecreate(struct sgx_pageinfo *pginfo, void *secs)
{
	return __encls_2(ECREATE, pginfo, secs);
}

static inline int __eextend(void *secs, void *addr)
{
	return __encls_2(EEXTEND, secs, addr);
}

static inline int __eadd(struct sgx_pageinfo *pginfo, void *addr)
{
	return __encls_2(EADD, pginfo, addr);
}

static inline int __einit(void *sigstruct, void *token, void *secs)
{
	return __encls_ret_3(EINIT, sigstruct, secs, token);
}

static inline int __eremove(void *addr)
{
	return __encls_ret_1(EREMOVE, addr);
}

static inline int __edbgwr(void *addr, unsigned long *data)
{
	return __encls_2(EDGBWR, *data, addr);
}

static inline int __edbgrd(void *addr, unsigned long *data)
{
	return __encls_1_1(EDGBRD, *data, addr);
}

static inline int __etrack(void *addr)
{
	return __encls_ret_1(ETRACK, addr);
}

static inline int __eldu(struct sgx_pageinfo *pginfo, void *addr,
			 void *va)
{
	return __encls_ret_3(ELDU, pginfo, addr, va);
}

static inline int __eblock(void *addr)
{
	return __encls_ret_1(EBLOCK, addr);
}

static inline int __epa(void *addr)
{
	unsigned long rbx = SGX_PAGE_TYPE_VA;

	return __encls_2(EPA, rbx, addr);
}

static inline int __ewb(struct sgx_pageinfo *pginfo, void *addr,
			void *va)
{
	return __encls_ret_3(EWB, pginfo, addr, va);
}

#endif /* _X86_ENCLS_H */
