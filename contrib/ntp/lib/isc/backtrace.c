/*
 * Copyright (C) 2009  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: backtrace.c,v 1.3 2009/09/02 23:48:02 tbox Exp $ */

/*! \file */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#ifdef HAVE_LIBCTRACE
#include <execinfo.h>
#endif

#include <isc/backtrace.h>
#include <isc/result.h>
#include <isc/util.h>

#ifdef ISC_PLATFORM_USEBACKTRACE
/*
 * Getting a back trace of a running process is tricky and highly platform
 * dependent.  Our current approach is as follows:
 * 1. If the system library supports the "backtrace()" function, use it.
 * 2. Otherwise, if the compiler is gcc and the architecture is x86_64 or IA64,
 *    then use gcc's (hidden) Unwind_Backtrace() function.  Note that this
 *    function doesn't work for C programs on many other architectures.
 * 3. Otherwise, if the architecture x86 or x86_64, try to unwind the stack
 *    frame following frame pointers.  This assumes the executable binary
 *    compiled with frame pointers; this is not always true for x86_64 (rather,
 *    compiler optimizations often disable frame pointers).  The validation
 *    checks in getnextframeptr() hopefully rejects bogus values stored in
 *    the RBP register in such a case.  If the backtrace function itself crashes
 *    due to this problem, the whole package should be rebuilt with
 *    --disable-backtrace.
 */
#ifdef HAVE_LIBCTRACE
#define BACKTRACE_LIBC
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__ia64__))
#define BACKTRACE_GCC
#elif defined(__x86_64__) || defined(__i386__)
#define BACKTRACE_X86STACK
#else
#define BACKTRACE_DISABLED
#endif  /* HAVE_LIBCTRACE */
#else	/* !ISC_PLATFORM_USEBACKTRACE */
#define BACKTRACE_DISABLED
#endif	/* ISC_PLATFORM_USEBACKTRACE */

#ifdef BACKTRACE_LIBC
isc_result_t
isc_backtrace_gettrace(void **addrs, int maxaddrs, int *nframes) {
	int n;

	/*
	 * Validate the arguments: intentionally avoid using REQUIRE().
	 * See notes in backtrace.h.
	 */
	if (addrs == NULL || nframes == NULL)
		return (ISC_R_FAILURE);

	/*
	 * backtrace(3) includes this function itself in the address array,
	 * which should be eliminated from the returned sequence.
	 */
	n = backtrace(addrs, maxaddrs);
	if (n < 2)
		return (ISC_R_NOTFOUND);
	n--;
	memmove(addrs, &addrs[1], sizeof(void *) * n);
	*nframes = n;
	return (ISC_R_SUCCESS);
}
#elif defined(BACKTRACE_GCC)
extern int _Unwind_Backtrace(void* fn, void* a);
extern void* _Unwind_GetIP(void* ctx);

typedef struct {
	void **result;
	int max_depth;
	int skip_count;
	int count;
} trace_arg_t;

static int
btcallback(void *uc, void *opq) {
	trace_arg_t *arg = (trace_arg_t *)opq;

	if (arg->skip_count > 0)
		arg->skip_count--;
	else
		arg->result[arg->count++] = (void *)_Unwind_GetIP(uc);
	if (arg->count == arg->max_depth)
		return (5); /* _URC_END_OF_STACK */

	return (0); /* _URC_NO_REASON */
}

isc_result_t
isc_backtrace_gettrace(void **addrs, int maxaddrs, int *nframes) {
	trace_arg_t arg;

	/* Argument validation: see above. */
	if (addrs == NULL || nframes == NULL)
		return (ISC_R_FAILURE);

	arg.skip_count = 1;
	arg.result = addrs;
	arg.max_depth = maxaddrs;
	arg.count = 0;
	_Unwind_Backtrace(btcallback, &arg);

	*nframes = arg.count;

	return (ISC_R_SUCCESS);
}
#elif defined(BACKTRACE_X86STACK)
#ifdef __x86_64__
static unsigned long
getrbp() {
	__asm("movq %rbp, %rax\n");
}
#endif

static void **
getnextframeptr(void **sp) {
	void **newsp = (void **)*sp;

	/*
	 * Perform sanity check for the new frame pointer, derived from
	 * google glog.  This can actually be bogus depending on compiler.
	 */

	/* prohibit the stack frames from growing downwards */
	if (newsp <= sp)
		return (NULL);

	/* A heuristics to reject "too large" frame: this actually happened. */
	if ((char *)newsp - (char *)sp > 100000)
		return (NULL);

	/*
	 * Not sure if other checks used in glog are needed at this moment.
	 * For our purposes we don't have to consider non-contiguous frames,
	 * for example.
	 */

	return (newsp);
}

isc_result_t
isc_backtrace_gettrace(void **addrs, int maxaddrs, int *nframes) {
	int i = 0;
	void **sp;

	/* Argument validation: see above. */
	if (addrs == NULL || nframes == NULL)
		return (ISC_R_FAILURE);

#ifdef __x86_64__
	sp = (void **)getrbp();
	if (sp == NULL)
		return (ISC_R_NOTFOUND);
	/*
	 * sp is the frame ptr of this function itself due to the call to
	 * getrbp(), so need to unwind one frame for consistency.
	 */
	sp = getnextframeptr(sp);
#else
	/*
	 * i386: the frame pointer is stored 2 words below the address for the
	 * first argument.  Note that the body of this function cannot be
	 * inlined since it depends on the address of the function argument.
	 */
	sp = (void **)&addrs - 2;
#endif

	while (sp != NULL && i < maxaddrs) {
		addrs[i++] = *(sp + 1);
		sp = getnextframeptr(sp);
	}

	*nframes = i;

	return (ISC_R_SUCCESS);
}
#elif defined(BACKTRACE_DISABLED)
isc_result_t
isc_backtrace_gettrace(void **addrs, int maxaddrs, int *nframes) {
	/* Argument validation: see above. */
	if (addrs == NULL || nframes == NULL)
		return (ISC_R_FAILURE);

	UNUSED(maxaddrs);

	return (ISC_R_NOTIMPLEMENTED);
}
#endif

isc_result_t
isc_backtrace_getsymbolfromindex(int idx, const void **addrp,
				 const char **symbolp)
{
	REQUIRE(addrp != NULL && *addrp == NULL);
	REQUIRE(symbolp != NULL && *symbolp == NULL);

	if (idx < 0 || idx >= isc__backtrace_nsymbols)
		return (ISC_R_RANGE);

	*addrp = isc__backtrace_symtable[idx].addr;
	*symbolp = isc__backtrace_symtable[idx].symbol;
	return (ISC_R_SUCCESS);
}

static int
symtbl_compare(const void *addr, const void *entryarg) {
	const isc_backtrace_symmap_t *entry = entryarg;
	const isc_backtrace_symmap_t *end =
		&isc__backtrace_symtable[isc__backtrace_nsymbols - 1];

	if (isc__backtrace_nsymbols == 1 || entry == end) {
		if (addr >= entry->addr) {
			/*
			 * If addr is equal to or larger than that of the last
			 * entry of the table, we cannot be sure if this is
			 * within a valid range so we consider it valid.
			 */
			return (0);
		}
		return (-1);
	}

	/* entry + 1 is a valid entry from now on. */
	if (addr < entry->addr)
		return (-1);
	else if (addr >= (entry + 1)->addr)
		return (1);
	return (0);
}

isc_result_t
isc_backtrace_getsymbol(const void *addr, const char **symbolp,
			unsigned long *offsetp)
{
	isc_result_t result = ISC_R_SUCCESS;
	isc_backtrace_symmap_t *found;

	/*
	 * Validate the arguments: intentionally avoid using REQUIRE().
	 * See notes in backtrace.h.
	 */
	if (symbolp == NULL || *symbolp != NULL || offsetp == NULL)
		return (ISC_R_FAILURE);

	if (isc__backtrace_nsymbols < 1)
		return (ISC_R_NOTFOUND);

	/*
	 * Search the table for the entry that meets:
	 * entry.addr <= addr < next_entry.addr.
	 */
	found = bsearch(addr, isc__backtrace_symtable, isc__backtrace_nsymbols,
			sizeof(isc__backtrace_symtable[0]), symtbl_compare);
	if (found == NULL)
		result = ISC_R_NOTFOUND;
	else {
		*symbolp = found->symbol;
		*offsetp = (u_long)((const char *)addr - (char *)found->addr);
	}

	return (result);
}
