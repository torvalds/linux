/*
 *  arch/arm/mach-$chip/printk.c
 *
 *  Copyright (C) 2012 AllWinner Limited
 *  Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/console.h>
#include <linux/init.h>

#include <linux/io.h>
#include <mach/includes.h>

void aw_put_char(u32 uart_base, u8 val)
{
	while (!(readl(IO_ADDRESS(uart_base) + AW_UART_USR) & 0x2));
	writel(val, IO_ADDRESS(uart_base) + AW_UART_THR);
}

void aw_put_string(u32 uart_base, char *buf, int n)
{
	int len = n;
	int i = 0;

	for (i=0; i<len; i++) {
		aw_put_char(uart_base, buf[i]);
	}
}

static char aw_printk_buf[4096];
int aw_printk(u32 uart_base, const char *fmt, ...)
{
        int n = 0;
        va_list ap;

        va_start(ap, fmt);
        n = vscnprintf(aw_printk_buf, sizeof(aw_printk_buf), fmt, ap);
        aw_put_string(uart_base, aw_printk_buf, n);
        va_end(ap);
        return (int)0;
}

#if 0
int printk(const char *fmt, ...)
{
        int n = 0;
        va_list ap;

        va_start(ap, fmt);
        n = vscnprintf(aw_printk_buf, sizeof(aw_printk_buf), fmt, ap);
        aw_put_string(0x1c28000, aw_printk_buf, n);
        va_end(ap);
        return (int)0;
}
#endif



