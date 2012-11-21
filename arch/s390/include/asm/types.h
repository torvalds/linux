/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/types.h"
 */
#ifndef _S390_TYPES_H
#define _S390_TYPES_H

#include <uapi/asm/types.h>

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */

#ifndef __ASSEMBLY__

#ifndef CONFIG_64BIT
typedef union {
	unsigned long long pair;
	struct {
		unsigned long even;
		unsigned long odd;
	} subreg;
} register_pair;

#endif /* ! CONFIG_64BIT   */
#endif /* __ASSEMBLY__  */
#endif /* _S390_TYPES_H */
