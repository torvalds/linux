#include <linux/config.h>

#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/cacheflush.h>
#include <asm/qemu.h>

static void qemu_machine_restart(char *command)
{
	volatile unsigned int *reg = (unsigned int *)QEMU_RESTART_REG;

	set_c0_status(ST0_BEV | ST0_ERL);
	change_c0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);
	flush_cache_all();
	write_c0_wired(0);
	*reg = 42;
	while (1)
		cpu_wait();
}

static void qemu_machine_halt(void)
{
	volatile unsigned int *reg = (unsigned int *)QEMU_HALT_REG;

	*reg = 42;
	while (1)
		cpu_wait();
}

void qemu_reboot_setup(void)
{
	_machine_restart = qemu_machine_restart;
	_machine_halt = qemu_machine_halt;
}
