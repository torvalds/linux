/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 */

#ifndef __RTSX_PCR_H
#define __RTSX_PCR_H

#include <linux/rtsx_pci.h>

#define MIN_DIV_N_PCR		80
#define MAX_DIV_N_PCR		208

#define RTS522A_PM_CTRL3		0xFF7E

#define RTS524A_PME_FORCE_CTL		0xFF78
#define RTS524A_PM_CTRL3		0xFF7E

#define LTR_ACTIVE_LATENCY_DEF		0x883C
#define LTR_IDLE_LATENCY_DEF		0x892C
#define LTR_L1OFF_LATENCY_DEF		0x9003
#define L1_SNOOZE_DELAY_DEF		1
#define LTR_L1OFF_SSPWRGATE_5249_DEF		0xAF
#define LTR_L1OFF_SSPWRGATE_5250_DEF		0xFF
#define LTR_L1OFF_SNOOZE_SSPWRGATE_5249_DEF	0xAC
#define LTR_L1OFF_SNOOZE_SSPWRGATE_5250_DEF	0xF8
#define CMD_TIMEOUT_DEF		100
#define ASPM_MASK_NEG		0xFC
#define MASK_8_BIT_DEF		0xFF

#define SSC_CLOCK_STABLE_WAIT	130

int __rtsx_pci_write_phy_register(struct rtsx_pcr *pcr, u8 addr, u16 val);
int __rtsx_pci_read_phy_register(struct rtsx_pcr *pcr, u8 addr, u16 *val);

void rts5209_init_params(struct rtsx_pcr *pcr);
void rts5229_init_params(struct rtsx_pcr *pcr);
void rtl8411_init_params(struct rtsx_pcr *pcr);
void rtl8402_init_params(struct rtsx_pcr *pcr);
void rts5227_init_params(struct rtsx_pcr *pcr);
void rts522a_init_params(struct rtsx_pcr *pcr);
void rts5249_init_params(struct rtsx_pcr *pcr);
void rts524a_init_params(struct rtsx_pcr *pcr);
void rts525a_init_params(struct rtsx_pcr *pcr);
void rtl8411b_init_params(struct rtsx_pcr *pcr);
void rts5260_init_params(struct rtsx_pcr *pcr);

static inline u8 map_sd_drive(int idx)
{
	u8 sd_drive[4] = {
		0x01,	/* Type D */
		0x02,	/* Type C */
		0x05,	/* Type A */
		0x03	/* Type B */
	};

	return sd_drive[idx];
}

#define rtsx_vendor_setting_valid(reg)		(!((reg) & 0x1000000))
#define rts5209_vendor_setting1_valid(reg)	(!((reg) & 0x80))
#define rts5209_vendor_setting2_valid(reg)	((reg) & 0x80)

#define rtsx_reg_to_aspm(reg)			(((reg) >> 28) & 0x03)
#define rtsx_reg_to_sd30_drive_sel_1v8(reg)	(((reg) >> 26) & 0x03)
#define rtsx_reg_to_sd30_drive_sel_3v3(reg)	(((reg) >> 5) & 0x03)
#define rtsx_reg_to_card_drive_sel(reg)		((((reg) >> 25) & 0x01) << 6)
#define rtsx_reg_check_reverse_socket(reg)	((reg) & 0x4000)
#define rts5209_reg_to_aspm(reg)		(((reg) >> 5) & 0x03)
#define rts5209_reg_check_ms_pmos(reg)		(!((reg) & 0x08))
#define rts5209_reg_to_sd30_drive_sel_1v8(reg)	(((reg) >> 3) & 0x07)
#define rts5209_reg_to_sd30_drive_sel_3v3(reg)	((reg) & 0x07)
#define rts5209_reg_to_card_drive_sel(reg)	((reg) >> 8)
#define rtl8411_reg_to_sd30_drive_sel_3v3(reg)	(((reg) >> 5) & 0x07)
#define rtl8411b_reg_to_sd30_drive_sel_3v3(reg)	((reg) & 0x03)

#define set_pull_ctrl_tables(pcr, __device)				\
do {									\
	pcr->sd_pull_ctl_enable_tbl  = __device##_sd_pull_ctl_enable_tbl;  \
	pcr->sd_pull_ctl_disable_tbl = __device##_sd_pull_ctl_disable_tbl; \
	pcr->ms_pull_ctl_enable_tbl  = __device##_ms_pull_ctl_enable_tbl;  \
	pcr->ms_pull_ctl_disable_tbl = __device##_ms_pull_ctl_disable_tbl; \
} while (0)

/* generic operations */
int rtsx_gops_pm_reset(struct rtsx_pcr *pcr);
int rtsx_set_ltr_latency(struct rtsx_pcr *pcr, u32 latency);
int rtsx_set_l1off_sub(struct rtsx_pcr *pcr, u8 val);
void rtsx_pci_init_ocp(struct rtsx_pcr *pcr);
void rtsx_pci_disable_ocp(struct rtsx_pcr *pcr);
void rtsx_pci_enable_ocp(struct rtsx_pcr *pcr);
int rtsx_pci_get_ocpstat(struct rtsx_pcr *pcr, u8 *val);
void rtsx_pci_clear_ocpstat(struct rtsx_pcr *pcr);
int rtsx_sd_power_off_card3v3(struct rtsx_pcr *pcr);
int rtsx_ms_power_off_card3v3(struct rtsx_pcr *pcr);

#endif
