#ifndef __ASM_CPU_TYPE_H
#define __ASM_CPU_TYPE_H

/*
 * Sparc (general) CPU types
 */
enum sparc_cpu {
  sun4        = 0x00,
  sun4c       = 0x01,
  sun4m       = 0x02,
  sun4d       = 0x03,
  sun4e       = 0x04,
  sun4u       = 0x05, /* V8 ploos ploos */
  sun_unknown = 0x06,
  ap1000      = 0x07, /* almost a sun4m */
  sparc_leon  = 0x08, /* Leon SoC */
};

#ifdef CONFIG_SPARC32
extern enum sparc_cpu sparc_cpu_model;

#define ARCH_SUN4C (sparc_cpu_model==sun4c)

#define SUN4M_NCPUS            4              /* Architectural limit of sun4m. */

#else

#define sparc_cpu_model sun4u

/* This cannot ever be a sun4c :) That's just history. */
#define ARCH_SUN4C 0
#endif

#endif /* __ASM_CPU_TYPE_H */
