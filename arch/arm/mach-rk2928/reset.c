#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/system.h>
#include <linux/string.h>

static void rk2928_arch_reset(char mode, const char *cmd)
{
	while (1);
}

void (*arch_reset)(char, const char *) = rk2928_arch_reset;
