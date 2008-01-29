#include <linux/init.h>
#include <linux/platform_device.h>

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

static struct platform_device pcspeaker_pdev = {
	.name	= "pcspkr",
	.id	= -1,
};

static int __init qemu_platform_devinit(void)
{
	platform_device_register(&pcspeaker_pdev);

	return 0;
}

device_initcall(qemu_platform_devinit);
