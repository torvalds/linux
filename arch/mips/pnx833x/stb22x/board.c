/*
 *  board.c: STB225 board support.
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 *
 *  Based on software written by:
 *      Nikita Youshchenko <yoush@debian.org>, based on PNX8550 code.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <asm/bootinfo.h>
#include <linux/mm.h>
#include <pnx833x.h>
#include <gpio.h>

/* endianess twiddlers */
#define PNX8335_DEBUG0 0x4400
#define PNX8335_DEBUG1 0x4404
#define PNX8335_DEBUG2 0x4408
#define PNX8335_DEBUG3 0x440c
#define PNX8335_DEBUG4 0x4410
#define PNX8335_DEBUG5 0x4414
#define PNX8335_DEBUG6 0x4418
#define PNX8335_DEBUG7 0x441c

int prom_argc;
char **prom_argv, **prom_envp;

extern void prom_init_cmdline(void);
extern char *prom_getenv(char *envname);

const char *get_system_type(void)
{
	return "NXP STB22x";
}

static inline unsigned long env_or_default(char *env, unsigned long dfl)
{
	char *str = prom_getenv(env);
	return str ? simple_strtol(str, 0, 0) : dfl;
}

void __init prom_init(void)
{
	unsigned long memsize;

	prom_argc = fw_arg0;
	prom_argv = (char **)fw_arg1;
	prom_envp = (char **)fw_arg2;

	prom_init_cmdline();

	memsize = env_or_default("memsize", 0x02000000);
	add_memory_region(0, memsize, BOOT_MEM_RAM);
}

void __init pnx833x_board_setup(void)
{
	pnx833x_gpio_select_function_alt(4);
	pnx833x_gpio_select_output(4);
	pnx833x_gpio_select_function_alt(5);
	pnx833x_gpio_select_input(5);
	pnx833x_gpio_select_function_alt(6);
	pnx833x_gpio_select_input(6);
	pnx833x_gpio_select_function_alt(7);
	pnx833x_gpio_select_output(7);

	pnx833x_gpio_select_function_alt(25);
	pnx833x_gpio_select_function_alt(26);

	pnx833x_gpio_select_function_alt(27);
	pnx833x_gpio_select_function_alt(28);
	pnx833x_gpio_select_function_alt(29);
	pnx833x_gpio_select_function_alt(30);
	pnx833x_gpio_select_function_alt(31);
	pnx833x_gpio_select_function_alt(32);
	pnx833x_gpio_select_function_alt(33);

#if IS_ENABLED(CONFIG_MTD_NAND_PLATFORM)
	/* Setup MIU for NAND access on CS0...
	 *
	 * (it seems that we must also configure CS1 for reliable operation,
	 * otherwise the first read ID command will fail if it's read as 4 bytes
	 * but pass if it's read as 1 word.)
	 */

	/* Setup MIU CS0 & CS1 timing */
	PNX833X_MIU_SEL0 = 0;
	PNX833X_MIU_SEL1 = 0;
	PNX833X_MIU_SEL0_TIMING = 0x50003081;
	PNX833X_MIU_SEL1_TIMING = 0x50003081;

	/* Setup GPIO 00 for use as MIU CS1 (CS0 is not multiplexed, so does not need this) */
	pnx833x_gpio_select_function_alt(0);

	/* Setup GPIO 04 to input NAND read/busy signal */
	pnx833x_gpio_select_function_io(4);
	pnx833x_gpio_select_input(4);

	/* Setup GPIO 05 to disable NAND write protect */
	pnx833x_gpio_select_function_io(5);
	pnx833x_gpio_select_output(5);
	pnx833x_gpio_write(1, 5);

#elif IS_ENABLED(CONFIG_MTD_CFI)

	/* Set up MIU for 16-bit NOR access on CS0 and CS1... */

	/* Setup MIU CS0 & CS1 timing */
	PNX833X_MIU_SEL0 = 1;
	PNX833X_MIU_SEL1 = 1;
	PNX833X_MIU_SEL0_TIMING = 0x6A08D082;
	PNX833X_MIU_SEL1_TIMING = 0x6A08D082;

	/* Setup GPIO 00 for use as MIU CS1 (CS0 is not multiplexed, so does not need this) */
	pnx833x_gpio_select_function_alt(0);
#endif
}
