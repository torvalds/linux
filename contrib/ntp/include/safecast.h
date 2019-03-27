#ifndef SAFECAST_H
#define SAFECAST_H

#include <limits.h>
static inline int size2int_chk(size_t v)
{
	if (v > INT_MAX)
		abort();
	return (int)(v);
}

static inline int size2int_sat(size_t v)
{
	return (v > INT_MAX) ? INT_MAX : (int)v;
}

/* Compilers can emit warning about increased alignment requirements
 * when casting pointers. The impact is tricky: on machines where
 * alignment is just a performance issue (x86,x64,...) this might just
 * cause a performance penalty. On others, an address error can occur
 * and the process dies...
 *
 * Still, there are many cases where the pointer arithmetic and the
 * buffer alignment make sure this does not happen. OTOH, the compiler
 * doesn't know this and still emits warnings.
 *
 * The following cast macros are going through void pointers to tell
 * the compiler that there is no alignment requirement to watch.
 */
#define UA_PTR(ptype,pval) ((ptype *)(void*)(pval))
#define UAC_PTR(ptype,pval) ((const ptype *)(const void*)(pval))
#define UAV_PTR(ptype,pval) ((volatile ptype *)(volatile void*)(pval))

#endif
