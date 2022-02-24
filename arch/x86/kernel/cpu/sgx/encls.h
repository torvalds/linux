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

/* Retrieve the encoded trapnr from the specified return code. */
#define ENCLS_TRAPNR(r) ((r) & ~SGX_ENCLS_FAULT_FLAG)

/* Issue a WARN() about an ENCLS function. */
#define ENCLS_WARN(r, name) {						  \
	do {								  \
		int _r = (r);						  \
		WARN_ONCE(_r, "%s returned %d (0x%x)\n", (name), _r, _r); \
	} while (0);							  \
}

/*
 * encls_faulted() - Check if an ENCLS leaf faulted given an error code
 * @ret:	the return value of an ENCLS leaf function call
 *
 * Return:
 * - true:	ENCLS leaf faulted.
 * - false:	Otherwise.
 */
static inline bool encls_faulted(int ret)
{
	return ret & SGX_ENCLS_FAULT_FLAG;
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
	if (encls_faulted(ret))
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
	_ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FAULT_SGX)		\
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
 *   trapnr with SGX_ENCLS_FAULT_FLAG set on fault
 */
#define __encls_N(rax, rbx_out, inputs...)			\
	({							\
	int ret;						\
	asm volatile(						\
	"1: .byte 0x0f, 0x01, 0xcf;\n\t"			\
	"   xor %%eax,%%eax;\n"					\
	"2:\n"							\
	_ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FAULT_SGX)		\
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
