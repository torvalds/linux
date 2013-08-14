#ifndef __ASM_R8A7790_H__
#define __ASM_R8A7790_H__

void r8a7790_add_standard_devices(void);
void r8a7790_clock_init(void);
void r8a7790_pinmux_init(void);
void r8a7790_init_delay(void);
void r8a7790_timer_init(void);

#define MD(nr) BIT(nr)
u32 r8a7790_read_mode_pins(void);

#endif /* __ASM_R8A7790_H__ */
