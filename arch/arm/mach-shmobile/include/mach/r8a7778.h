/*
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 * Copyright (C) 2013  Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __ASM_R8A7778_H__
#define __ASM_R8A7778_H__

#include <linux/sh_eth.h>
#include <linux/platform_data/camera-rcar.h>

/* HPB-DMA slave IDs */
enum {
	HPBDMA_SLAVE_DUMMY,
	HPBDMA_SLAVE_SDHI0_TX,
	HPBDMA_SLAVE_SDHI0_RX,
};

extern void r8a7778_add_standard_devices(void);
extern void r8a7778_add_standard_devices_dt(void);
extern void r8a7778_add_ether_device(struct sh_eth_plat_data *pdata);
extern void r8a7778_add_vin_device(int id,
				   struct rcar_vin_platform_data *pdata);
extern void r8a7778_add_dt_devices(void);

extern void r8a7778_init_late(void);
extern void r8a7778_init_delay(void);
extern void r8a7778_init_irq_dt(void);
extern void r8a7778_clock_init(void);
extern void r8a7778_init_irq_extpin(int irlm);
extern void r8a7778_pinmux_init(void);

extern int r8a7778_usb_phy_power(bool enable);

#endif /* __ASM_R8A7778_H__ */
