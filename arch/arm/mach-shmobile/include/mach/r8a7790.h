#ifndef __ASM_R8A7790_H__
#define __ASM_R8A7790_H__

#include <mach/rcar-gen2.h>

void r8a7790_add_standard_devices(void);
void r8a7790_add_dt_devices(void);
void r8a7790_clock_init(void);
void r8a7790_pinmux_init(void);
void r8a7790_init_early(void);
extern struct smp_operations r8a7790_smp_ops;

#endif /* __ASM_R8A7790_H__ */
