// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/pcmcia/pxa/pxa_cm_x2xx.c
 *
 * Compulab Ltd., 2003, 2007, 2008
 * Mike Rapoport <mike@compulab.co.il>
 */

#include <linux/module.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

int cmx255_pcmcia_init(void);
int cmx270_pcmcia_init(void);
void cmx255_pcmcia_exit(void);
void cmx270_pcmcia_exit(void);

static int __init cmx2xx_pcmcia_init(void)
{
	int ret = -ENODEV;

	if (machine_is_armcore() && cpu_is_pxa25x())
		ret = cmx255_pcmcia_init();
	else if (machine_is_armcore() && cpu_is_pxa27x())
		ret = cmx270_pcmcia_init();

	return ret;
}

static void __exit cmx2xx_pcmcia_exit(void)
{
	if (machine_is_armcore() && cpu_is_pxa25x())
		cmx255_pcmcia_exit();
	else if (machine_is_armcore() && cpu_is_pxa27x())
		cmx270_pcmcia_exit();
}

module_init(cmx2xx_pcmcia_init);
module_exit(cmx2xx_pcmcia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("CM-x2xx PCMCIA driver");
