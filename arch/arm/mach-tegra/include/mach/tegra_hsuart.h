/*
 * Copyright (C) 2010 Google, Inc.
 * Author: Jaikumar Ganesh <jaikumar@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_TEGRA_HSUART_H
#define __ASM_ARCH_TEGRA_HSUART_H

/* Optional platform device data for tegra_hsuart driver. */
struct tegra_hsuart_platform_data {
	void (*exit_lpm_cb)(struct uart_port *);
	void (*rx_done_cb)(struct uart_port *);
};

#endif
