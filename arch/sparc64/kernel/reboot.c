/* reboot.c: reboot/shutdown/halt/poweroff handling
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/pm.h>

#include <asm/oplib.h>
#include <asm/prom.h>

/* sysctl - toggle power-off restriction for serial console
 * systems in machine_power_off()
 */
int scons_pwroff = 1;

/* This isn't actually used, it exists merely to satisfy the
 * reference in kernel/sys.c
 */
void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

void machine_power_off(void)
{
	if (strcmp(of_console_device->type, "serial") || scons_pwroff)
		prom_halt_power_off();

	prom_halt();
}

void machine_halt(void)
{
	prom_halt();
	panic("Halt failed!");
}

void machine_restart(char *cmd)
{
	char *p;

	p = strchr(reboot_command, '\n');
	if (p)
		*p = 0;
	if (cmd)
		prom_reboot(cmd);
	if (*reboot_command)
		prom_reboot(reboot_command);
	prom_reboot("");
	panic("Reboot failed!");
}

