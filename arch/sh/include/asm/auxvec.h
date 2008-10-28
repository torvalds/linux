#ifndef __ASM_SH_AUXVEC_H
#define __ASM_SH_AUXVEC_H

/*
 * Architecture-neutral AT_ values in 0-17, leave some room
 * for more of them.
 */

/*
 * This entry gives some information about the FPU initialization
 * performed by the kernel.
 */
#define AT_FPUCW		18	/* Used FPU control word.  */

#if defined(CONFIG_VSYSCALL) || !defined(__KERNEL__)
/*
 * Only define this in the vsyscall case, the entry point to
 * the vsyscall page gets placed here. The kernel will attempt
 * to build a gate VMA we don't care about otherwise..
 */
#define AT_SYSINFO_EHDR		33
#endif

/*
 * More complete cache descriptions than AT_[DIU]CACHEBSIZE.  If the
 * value is -1, then the cache doesn't exist.  Otherwise:
 *
 *    bit 0-3:	  Cache set-associativity; 0 means fully associative.
 *    bit 4-7:	  Log2 of cacheline size.
 *    bit 8-31:	  Size of the entire cache >> 8.
 */
#define AT_L1I_CACHESHAPE	34
#define AT_L1D_CACHESHAPE	35
#define AT_L2_CACHESHAPE	36

#endif /* __ASM_SH_AUXVEC_H */
