#include <linux/init.h>
#include <linux/kernel.h>
#include <cpu/pfc.h>

static int __init plat_pinmux_setup(void)
{
	return sh_pfc_register("pfc-sh7722", NULL, 0);
}

arch_initcall(plat_pinmux_setup);
