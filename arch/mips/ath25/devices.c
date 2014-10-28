#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/bootinfo.h>

#include "devices.h"

const char *get_system_type(void)
{
	return "Atheros (unknown)";
}
