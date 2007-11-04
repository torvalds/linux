#include <linux/init.h>

#include <asm/i8253.h>
#include <asm/io.h>
#include <asm/time.h>

extern void qemu_reboot_setup(void);

const char *get_system_type(void)
{
	return "Qemu";
}

void __init plat_time_init(void)
{
	setup_pit_timer();
}

void __init plat_mem_setup(void)
{
	qemu_reboot_setup();
}
