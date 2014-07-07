/*
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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

#ifndef __ASM_R8A7740_H__
#define __ASM_R8A7740_H__

/*
 * MD_CKx pin
 */
#define MD_CK2	(1 << 2)
#define MD_CK1	(1 << 1)
#define MD_CK0	(1 << 0)

/* DMA slave IDs */
enum {
	SHDMA_SLAVE_INVALID,
	SHDMA_SLAVE_SDHI0_RX,
	SHDMA_SLAVE_SDHI0_TX,
	SHDMA_SLAVE_SDHI1_RX,
	SHDMA_SLAVE_SDHI1_TX,
	SHDMA_SLAVE_SDHI2_RX,
	SHDMA_SLAVE_SDHI2_TX,
	SHDMA_SLAVE_FSIA_RX,
	SHDMA_SLAVE_FSIA_TX,
	SHDMA_SLAVE_FSIB_TX,
	SHDMA_SLAVE_USBHS_TX,
	SHDMA_SLAVE_USBHS_RX,
	SHDMA_SLAVE_MMCIF_TX,
	SHDMA_SLAVE_MMCIF_RX,
};

extern void r8a7740_meram_workaround(void);
extern void r8a7740_init_irq_of(void);
extern void r8a7740_map_io(void);
extern void r8a7740_add_early_devices(void);
extern void r8a7740_add_standard_devices(void);
extern void r8a7740_add_standard_devices_dt(void);
extern void r8a7740_clock_init(u8 md_ck);
extern void r8a7740_pinmux_init(void);
extern void r8a7740_pm_init(void);

#ifdef CONFIG_PM
extern void __init r8a7740_init_pm_domains(void);
#else
static inline void r8a7740_init_pm_domains(void) {}
#endif /* CONFIG_PM */

#endif /* __ASM_R8A7740_H__ */
