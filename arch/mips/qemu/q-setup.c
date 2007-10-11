#include <linux/init.h>

#include <asm/i8253.h>
#include <asm/io.h>
#include <asm/time.h>

extern void qemu_reboot_setup(void);

#define QEMU_PORT_BASE 0xb4000000

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
	set_io_port_base(QEMU_PORT_BASE);
	qemu_reboot_setup();
}
