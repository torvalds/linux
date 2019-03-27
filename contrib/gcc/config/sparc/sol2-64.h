/* Definitions of target machine for GCC, for bi-arch SPARC
   running Solaris 2, defaulting to 64-bit code generation.  */

#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
  (MASK_V9 + MASK_PTR64 + MASK_64BIT /* + MASK_HARD_QUAD */ + \
   MASK_STACK_BIAS + MASK_APP_REGS + MASK_FPU + MASK_LONG_DOUBLE_128)
