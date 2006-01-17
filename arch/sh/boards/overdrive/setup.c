/*
 * arch/sh/overdrive/setup.c
 *
 * Copyright (C) 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * STMicroelectronics Overdrive Support.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>

#include <asm/overdrive/overdrive.h>
#include <asm/overdrive/fpga.h>

const char *get_system_type(void)
{
	return "SH7750 Overdrive";
}

/*
 * Initialize the board
 */
int __init platform_setup(void)
{
#ifdef CONFIG_PCI
	init_overdrive_fpga();
	galileo_init();
#endif

        /* Enable RS232 receive buffers */
	writel(0x1e, OVERDRIVE_CTRL);
}
