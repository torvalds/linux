/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __REALTEK_RTL_PCI_PS_H__
#define __REALTEK_RTL_PCI_PS_H__

#define MAX_SW_LPS_SLEEP_INTV	5

/*---------------------------------------------
 * 3 The value of cmd: 4 bits
 *---------------------------------------------
 */
#define    PWR_CMD_READ		0x00
#define    PWR_CMD_WRITE	0x01
#define    PWR_CMD_POLLING	0x02
#define    PWR_CMD_DELAY	0x03
#define    PWR_CMD_END		0x04

/* define the base address of each block */
#define	PWR_BASEADDR_MAC	0x00
#define	PWR_BASEADDR_USB	0x01
#define	PWR_BASEADDR_PCIE	0x02
#define	PWR_BASEADDR_SDIO	0x03

#define	PWR_FAB_ALL_MSK		(BIT(0)|BIT(1)|BIT(2)|BIT(3))
#define	PWR_CUT_TESTCHIP_MSK	BIT(0)
#define	PWR_CUT_A_MSK		BIT(1)
#define	PWR_CUT_B_MSK		BIT(2)
#define	PWR_CUT_C_MSK		BIT(3)
#define	PWR_CUT_D_MSK		BIT(4)
#define	PWR_CUT_E_MSK		BIT(5)
#define	PWR_CUT_F_MSK		BIT(6)
#define	PWR_CUT_G_MSK		BIT(7)
#define	PWR_CUT_ALL_MSK		0xFF
#define PWR_INTF_SDIO_MSK	BIT(0)
#define PWR_INTF_USB_MSK	BIT(1)
#define PWR_INTF_PCI_MSK	BIT(2)
#define PWR_INTF_ALL_MSK	(BIT(0)|BIT(1)|BIT(2)|BIT(3))

enum pwrseq_delay_unit {
	PWRSEQ_DELAY_US,
	PWRSEQ_DELAY_MS,
};

struct wlan_pwr_cfg {
	u16 offset;
	u8 cut_msk;
	u8 fab_msk:4;
	u8 interface_msk:4;
	u8 base:4;
	u8 cmd:4;
	u8 msk;
	u8 value;
};

#define	GET_PWR_CFG_OFFSET(__PWR_CMD)	(__PWR_CMD.offset)
#define	GET_PWR_CFG_CUT_MASK(__PWR_CMD)	(__PWR_CMD.cut_msk)
#define	GET_PWR_CFG_FAB_MASK(__PWR_CMD)	(__PWR_CMD.fab_msk)
#define	GET_PWR_CFG_INTF_MASK(__PWR_CMD)	(__PWR_CMD.interface_msk)
#define	GET_PWR_CFG_BASE(__PWR_CMD)	(__PWR_CMD.base)
#define	GET_PWR_CFG_CMD(__PWR_CMD)	(__PWR_CMD.cmd)
#define	GET_PWR_CFG_MASK(__PWR_CMD)	(__PWR_CMD.msk)
#define	GET_PWR_CFG_VALUE(__PWR_CMD)	(__PWR_CMD.value)

bool rtl_hal_pwrseqcmdparsing(struct rtl_priv *rtlpriv, u8 cut_version,
			      u8 fab_version, u8 interface_type,
			      struct wlan_pwr_cfg pwrcfgcmd[]);

bool rtl_ps_set_rf_state(struct ieee80211_hw *hw,
			 enum rf_pwrstate state_toset, u32 changesource);
bool rtl_ps_enable_nic(struct ieee80211_hw *hw);
bool rtl_ps_disable_nic(struct ieee80211_hw *hw);
void rtl_ips_nic_off(struct ieee80211_hw *hw);
void rtl_ips_nic_on(struct ieee80211_hw *hw);
void rtl_ips_nic_off_wq_callback(void *data);
void rtl_lps_enter(struct ieee80211_hw *hw);
void rtl_lps_leave(struct ieee80211_hw *hw);

void rtl_swlps_beacon(struct ieee80211_hw *hw, void *data, unsigned int len);
void rtl_swlps_wq_callback(void *data);
void rtl_swlps_rfon_wq_callback(void *data);
void rtl_swlps_rf_awake(struct ieee80211_hw *hw);
void rtl_swlps_rf_sleep(struct ieee80211_hw *hw);
void rtl_p2p_ps_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state);
void rtl_p2p_info(struct ieee80211_hw *hw, void *data, unsigned int len);
void rtl_lps_change_work_callback(struct work_struct *work);

#endif
