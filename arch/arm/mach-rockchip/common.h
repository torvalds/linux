#ifndef __MACH_ROCKCHIP_COMMON_H
#define __MACH_ROCKCHIP_COMMON_H

extern unsigned long rockchip_boot_fn;
extern struct smp_operations rockchip_smp_ops;

#define BOOT_MODE_NORMAL		0
#define BOOT_MODE_FACTORY2		1
#define BOOT_MODE_RECOVERY		2
#define BOOT_MODE_CHARGE		3
#define BOOT_MODE_POWER_TEST		4
#define BOOT_MODE_OFFMODE_CHARGING	5
#define BOOT_MODE_REBOOT		6
#define BOOT_MODE_PANIC			7
#define BOOT_MODE_WATCHDOG		8

extern int rockchip_boot_mode(void);
extern void __init rockchip_boot_mode_init(u32 flag, u32 mode);
extern void rockchip_restart_get_boot_mode(const char *cmd, u32 *flag, u32 *mode);

#endif
