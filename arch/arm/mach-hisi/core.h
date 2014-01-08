#ifndef __HISILICON_CORE_H
#define __HISILICON_CORE_H

#include <linux/reboot.h>

extern void hi3xxx_set_cpu_jump(int cpu, void *jump_addr);
extern int hi3xxx_get_cpu_jump(int cpu);
extern void secondary_startup(void);
extern struct smp_operations hi3xxx_smp_ops;

extern void hi3xxx_cpu_die(unsigned int cpu);
extern int hi3xxx_cpu_kill(unsigned int cpu);
extern void hi3xxx_set_cpu(int cpu, bool enable);

#endif
