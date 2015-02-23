#ifndef _ARCH_POWERPC_MM_VPHN_H_
#define _ARCH_POWERPC_MM_VPHN_H_

/* The H_HOME_NODE_ASSOCIATIVITY h_call returns 6 64-bit registers.
 */
#define VPHN_REGISTER_COUNT 6

/*
 * 6 64-bit registers unpacked into 12 32-bit associativity values. To form
 * the complete property we have to add the length in the first cell.
 */
#define VPHN_ASSOC_BUFSIZE (VPHN_REGISTER_COUNT*sizeof(u64)/sizeof(u32) + 1)

extern int vphn_unpack_associativity(const long *packed, __be32 *unpacked);

#endif
