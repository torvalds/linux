#ifndef __ASM_ALPHA_AUXVEC_H
#define __ASM_ALPHA_AUXVEC_H

/* Reserve these numbers for any future use of a VDSO.  */
#if 0
#define AT_SYSINFO		32
#define AT_SYSINFO_EHDR		33
#endif

/* More complete cache descriptions than AT_[DIU]CACHEBSIZE.  If the
   value is -1, then the cache doesn't exist.  Otherwise:

      bit 0-3:	  Cache set-associativity; 0 means fully associative.
      bit 4-7:	  Log2 of cacheline size.
      bit 8-31:	  Size of the entire cache >> 8.
      bit 32-63:  Reserved.
*/

#define AT_L1I_CACHESHAPE	34
#define AT_L1D_CACHESHAPE	35
#define AT_L2_CACHESHAPE	36
#define AT_L3_CACHESHAPE	37

#endif /* __ASM_ALPHA_AUXVEC_H */
