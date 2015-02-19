#ifndef _ASM_POWERPC_UNALIGNED_H
#define _ASM_POWERPC_UNALIGNED_H

#ifdef __KERNEL__

/*
 * The PowerPC can do unaligned accesses itself based on its endian mode.
 */
#include <linux/unaligned/access_ok.h>
#include <linux/unaligned/generic.h>

#ifdef __LITTLE_ENDIAN__
#define get_unaligned	__get_unaligned_le
#define put_unaligned	__put_unaligned_le
#else
#define get_unaligned	__get_unaligned_be
#define put_unaligned	__put_unaligned_be
#endif

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_UNALIGNED_H */
