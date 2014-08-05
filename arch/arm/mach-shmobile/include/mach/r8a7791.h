#ifndef __ASM_R8A7791_H__
#define __ASM_R8A7791_H__

void r8a7791_add_standard_devices(void);
void r8a7791_add_dt_devices(void);
void r8a7791_clock_init(void);
void r8a7791_pinmux_init(void);
extern struct smp_operations r8a7791_smp_ops;

#endif /* __ASM_R8A7791_H__ */
