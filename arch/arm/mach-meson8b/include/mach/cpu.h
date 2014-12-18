/*
 * mach/cpu.h
 *
 */

#ifndef __MACH_MESON8_CPU_H_
#define __MACH_MESON8_CPU_H_

#include <plat/cpu.h>

extern int (*get_cpu_temperature_celius)(void);
int get_cpu_temperature(void);

#define MESON_CPU_TYPE	MESON_CPU_TYPE_MESON8B

#define MESON_CPU_CONTROL_REG (IO_SRAM_BASE + 0x1ff80)
#define MESON_CPU1_CONTROL_ADDR_REG (IO_SRAM_BASE + 0x1ff84)
#define MESON_CPU_STATUS_REG(cpu) (IO_SRAM_BASE + 0x1ff90 +(cpu<<2))

#define MESON_CPU_POWER_CTRL_REG (IO_A9_PERIPH_BASE + 0x8)

#define MESON_CPU_SLEEP		1
#define MESON_CPU_WAKEUP	2

extern int meson_cpu_kill(unsigned int cpu);
extern void meson_cpu_die(unsigned int cpu);
extern int meson_cpu_disable(unsigned int cpu);
extern void meson_secondary_startup(void);
extern void meson_set_cpu_ctrl_reg(int cpu,int is_on);
extern int meson_get_cpu_ctrl_addr(int cpu);
extern void meson_set_cpu_ctrl_addr(uint32_t cpu, const uint32_t addr);
extern void meson_set_cpu_power_ctrl(uint32_t cpu,int is_power_on);

#endif /* __MACH_MESON6_CPU_H_ */
