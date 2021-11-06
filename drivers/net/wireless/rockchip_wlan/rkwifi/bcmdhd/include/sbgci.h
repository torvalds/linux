/*
 * SiliconBackplane GCI core hardware definitions
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _SBGCI_H
#define _SBGCI_H

#include <bcmutils.h>

#if !defined(_LANGUAGE_ASSEMBLY) && !defined(__ASSEMBLY__)

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	/* PAD */

#define GCI_OFFSETOF(sih, reg) \
	(AOB_ENAB(sih) ? OFFSETOF(gciregs_t, reg) : OFFSETOF(chipcregs_t, reg))
#define GCI_CORE_IDX(sih) (AOB_ENAB(sih) ? si_findcoreidx(sih, GCI_CORE_ID, 0) : SI_CC_IDX)

typedef volatile struct {
	uint32 gci_corecaps0;				/* 0x000 */
	uint32 gci_corecaps1;				/* 0x004 */
	uint32 gci_corecaps2;				/* 0x008 */
	uint32 gci_corectrl;				/* 0x00c */
	uint32 gci_corestat;				/* 0x010 */
	uint32 gci_intstat;				/* 0x014 */
	uint32 gci_intmask;				/* 0x018 */
	uint32 gci_wakemask;				/* 0x01c */
	uint32 gci_levelintstat;			/* 0x020 */
	uint32 gci_eventintstat;			/* 0x024 */
	uint32 gci_wakelevelintstat;			/* 0x028 */
	uint32 gci_wakeeventintstat;			/* 0x02c */
	uint32 semaphoreintstatus;			/* 0x030 */
	uint32 semaphoreintmask;			/* 0x034 */
	uint32 semaphorerequest;			/* 0x038 */
	uint32 semaphorereserve;			/* 0x03c */
	uint32 gci_indirect_addr;			/* 0x040 */
	uint32 gci_gpioctl;				/* 0x044 */
	uint32 gci_gpiostatus;				/* 0x048 */
	uint32 gci_gpiomask;				/* 0x04c */
	uint32 gci_eventsummary;			/* 0x050 */
	uint32 gci_miscctl;				/* 0x054 */
	uint32 gci_gpiointmask;				/* 0x058 */
	uint32 gci_gpiowakemask;			/* 0x05c */
	uint32 gci_input[32];				/* 0x060 */
	uint32 gci_event[32];				/* 0x0e0 */
	uint32 gci_output[4];				/* 0x160 */
	uint32 gci_control_0;				/* 0x170 */
	uint32 gci_control_1;				/* 0x174 */
	uint32 gci_intpolreg;				/* 0x178 */
	uint32 gci_levelintmask;			/* 0x17c */
	uint32 gci_eventintmask;			/* 0x180 */
	uint32 wakelevelintmask;			/* 0x184 */
	uint32 wakeeventintmask;			/* 0x188 */
	uint32 hwmask;					/* 0x18c */
	uint32 PAD;
	uint32 gci_inbandeventintmask;			/* 0x194 */
	uint32 PAD;
	uint32 gci_inbandeventstatus;			/* 0x19c */
	uint32 gci_seciauxtx;				/* 0x1a0 */
	uint32 gci_seciauxrx;				/* 0x1a4 */
	uint32 gci_secitx_datatag;			/* 0x1a8 */
	uint32 gci_secirx_datatag;			/* 0x1ac */
	uint32 gci_secitx_datamask;			/* 0x1b0 */
	uint32 gci_seciusef0tx_reg;			/* 0x1b4 */
	uint32 gci_secif0tx_offset;			/* 0x1b8 */
	uint32 gci_secif0rx_offset;			/* 0x1bc */
	uint32 gci_secif1tx_offset;			/* 0x1c0 */
	uint32 gci_rxfifo_common_ctrl;			/* 0x1c4 */
	uint32 gci_rxfifoctrl;				/* 0x1c8 */
	uint32 gci_hw_sema_status;			/* 0x1cc */
	uint32 gci_seciuartescval;			/* 0x1d0 */
	uint32 gic_seciuartautobaudctr;			/* 0x1d4 */
	uint32 gci_secififolevel;			/* 0x1d8 */
	uint32 gci_seciuartdata;			/* 0x1dc */
	uint32 gci_secibauddiv;				/* 0x1e0 */
	uint32 gci_secifcr;				/* 0x1e4 */
	uint32 gci_secilcr;				/* 0x1e8 */
	uint32 gci_secimcr;				/* 0x1ec */
	uint32 gci_secilsr;				/* 0x1f0 */
	uint32 gci_secimsr;				/* 0x1f4 */
	uint32 gci_baudadj;				/* 0x1f8 */
	uint32 gci_inbandintmask;			/* 0x1fc */
	uint32 gci_chipctrl;				/* 0x200 */
	uint32 gci_chipsts;				/* 0x204 */
	uint32 gci_gpioout;				/* 0x208 */
	uint32 gci_gpioout_read;			/* 0x20C */
	uint32 gci_mpwaketx;				/* 0x210 */
	uint32 gci_mpwakedetect;			/* 0x214 */
	uint32 gci_seciin_ctrl;				/* 0x218 */
	uint32 gci_seciout_ctrl;			/* 0x21C */
	uint32 gci_seciin_auxfifo_en;			/* 0x220 */
	uint32 gci_seciout_txen_txbr;			/* 0x224 */
	uint32 gci_seciin_rxbrstatus;			/* 0x228 */
	uint32 gci_seciin_rxerrstatus;			/* 0x22C */
	uint32 gci_seciin_fcstatus;			/* 0x230 */
	uint32 gci_seciout_txstatus;			/* 0x234 */
	uint32 gci_seciout_txbrstatus;			/* 0x238 */
	uint32 wlan_mem_info;				/* 0x23C */
	uint32 wlan_bankxinfo;				/* 0x240 */
	uint32 bt_smem_select;				/* 0x244 */
	uint32 bt_smem_stby;				/* 0x248 */
	uint32 bt_smem_status;				/* 0x24C */
	uint32 wlan_bankxactivepda;			/* 0x250 */
	uint32 wlan_bankxsleeppda;			/* 0x254 */
	uint32 wlan_bankxkill;				/* 0x258 */
	uint32 reset_override;				/* 0x25C */
	uint32 ip_id;					/* 0x260 */
	uint32 lpo_safe_zone;				/* 0x264 */
	uint32 function_sel_control_and_status;		/* 0x268 */
	uint32 bt_smem_control0;			/* 0x26C */
	uint32 bt_smem_control1;			/* 0x270 */
	uint32 PAD[PADSZ(0x274, 0x2fc)];		/* 0x274-0x2fc */
	uint32 gci_chipid;				/* 0x300 */
	uint32 PAD[PADSZ(0x304, 0x30c)];		/* 0x304-0x30c */
	uint32 otpstatus;				/* 0x310 */
	uint32 otpcontrol;				/* 0x314 */
	uint32 otpprog;					/* 0x318 */
	uint32 otplayout;				/* 0x31c */
	uint32 otplayoutextension;			/* 0x320 */
	uint32 otpcontrol1;				/* 0x324 */
	uint32 otpprogdata;				/* 0x328 */
	uint32 PAD[PADSZ(0x32c, 0x3f8)];		/* 0x32c-0x3f8 */
	uint32 otpECCstatus;				/* 0x3FC */
	uint32 gci_rffe_rfem_data0;			/* 0x400 */
	uint32 gci_rffe_rfem_data1;			/* 0x404 */
	uint32 gci_rffe_rfem_data2;			/* 0x408 */
	uint32 gci_rffe_rfem_data3;			/* 0x40c */
	uint32 gci_rffe_rfem_addr;			/* 0x410 */
	uint32 gci_rffe_config;				/* 0x414 */
	uint32 gci_rffe_clk_ctrl;			/* 0x418 */
	uint32 gci_rffe_ctrl;				/* 0x41c */
	uint32 gci_rffe_misc_ctrl;			/* 0x420 */
	uint32 gci_rffe_rfem_reg0_field_ctrl;		/* 0x424 */
	uint32 PAD[PADSZ(0x428, 0x438)];		/* 0x428-0x438 */
	uint32 gci_rffe_rfem_mapping_mux0;		/* 0x43c */
	uint32 gci_rffe_rfem_mapping_mux1;		/* 0x440 */
	uint32 gci_rffe_rfem_mapping_mux2;		/* 0x444 */
	uint32 gci_rffe_rfem_mapping_mux3;		/* 0x448 */
	uint32 gci_rffe_rfem_mapping_mux4;		/* 0x44c */
	uint32 gci_rffe_rfem_mapping_mux5;		/* 0x450 */
	uint32 gci_rffe_rfem_mapping_mux6;		/* 0x454 */
	uint32 gci_rffe_rfem_mapping_mux7;		/* 0x458 */
	uint32 gci_rffe_change_detect_ovr_wlmc;		/* 0x45c */
	uint32 gci_rffe_change_detect_ovr_wlac;		/* 0x460 */
	uint32 gci_rffe_change_detect_ovr_wlsc;		/* 0x464 */
	uint32 gci_rffe_change_detect_ovr_btmc;		/* 0x468 */
	uint32 gci_rffe_change_detect_ovr_btsc;		/* 0x46c */
	uint32 gci_cncb_ctrl_status;			/* 0x470 */
	uint32 gci_cncb_2g_force_unlock;		/* 0x474 */
	uint32 gci_cncb_5g_force_unlock;		/* 0x478 */
	uint32 gci_cncb_2g_reset_pulse_width;		/* 0x47c */
	uint32 gci_cncb_5g_reset_pulse_width;		/* 0x480 */
	uint32 gci_cncb_lut_indirect_addr;		/* 0x484 */
	uint32 gci_cncb_2g_lut;				/* 0x488 */
	uint32 gci_cncb_5g_lut;				/* 0x48c */
	uint32 gci_cncb_glitch_filter_width;		/* 0x490 */
	uint32 PAD[PADSZ(0x494, 0x5fc)];		/* 0x494-0x5fc */
	uint32 sgr_fifo_control_reg_5g;			/* 0x600 */
	uint32 sgr_fifo_control_reg_2g;			/* 0x604 */
	uint32 sgr_fifo_control_reg_bt;			/* 0x608 */
	uint32 PAD;					/* 0x60c */
	uint32 sgr_rx_fifo0_read_reg0;			/* 0x610 */
	uint32 sgr_rx_fifo0_read_reg1;			/* 0x614 */
	uint32 sgr_rx_fifo0_read_reg2;			/* 0x618 */
	uint32 sgr_rx_fifo1_read_reg0;			/* 0x61c */
	uint32 sgr_rx_fifo1_read_reg1;			/* 0x620 */
	uint32 sgr_rx_fifo1_read_reg2;			/* 0x624 */
	uint32 sgr_rx_fifo2_read_reg0;			/* 0x628 */
	uint32 sgr_rx_fifo2_read_reg1;			/* 0x62c */
	uint32 sgr_rx_fifo2_read_reg2;			/* 0x630 */
	uint32 sgr_rx_fifo3_read_reg0;			/* 0x634 */
	uint32 sgr_rx_fifo3_read_reg1;			/* 0x638 */
	uint32 sgr_rx_fifo3_read_reg2;			/* 0x63c */
	uint32 sgr_rx_fifo4_read_reg0;			/* 0x640 */
	uint32 sgr_rx_fifo4_read_reg1;			/* 0x644 */
	uint32 sgr_rx_fifo4_read_reg2;			/* 0x648 */
	uint32 sgr_rx_fifo5_read_reg0;			/* 0x64c */
	uint32 sgr_rx_fifo5_read_reg1;			/* 0x650 */
	uint32 sgr_rx_fifo5_read_reg2;			/* 0x654 */
	uint32 sgr_rx_fifo6_read_reg0;			/* 0x658 */
	uint32 sgr_rx_fifo6_read_reg1;			/* 0x65c */
	uint32 sgr_rx_fifo6_read_reg2;			/* 0x660 */
	uint32 sgr_rx_fifo7_read_reg0;			/* 0x664 */
	uint32 sgr_rx_fifo7_read_reg1;			/* 0x668 */
	uint32 sgr_rx_fifo7_read_reg2;			/* 0x66c */
	uint32 sgr_rx_fifo8_read_reg0;			/* 0x670 */
	uint32 sgr_rx_fifo8_read_reg1;			/* 0x674 */
	uint32 sgr_rx_fifo8_read_reg2;			/* 0x678 */
	uint32 sgr_rx_fifo0_read_status;		/* 0x67c */
	uint32 sgr_rx_fifo1_read_status;		/* 0x680 */
	uint32 sgr_rx_fifo2_read_status;		/* 0x684 */
	uint32 sgr_rx_fifo3_read_status;		/* 0x688 */
	uint32 sgr_rx_fifo4_read_status;		/* 0x68c */
	uint32 sgr_rx_fifo5_read_status;		/* 0x690 */
	uint32 sgr_rx_fifo6_read_status;		/* 0x694 */
	uint32 sgr_rx_fifo7_read_status;		/* 0x698 */
	uint32 sgr_rx_fifo8_read_status;		/* 0x69c */
	uint32 wl_tx_fifo_data_idx_reg;			/* 0x6a0 */
	uint32 wl_tx_fifo_data_reg0;			/* 0x6a4 */
	uint32 wl_tx_fifo_data_reg1;			/* 0x6a8 */
	uint32 wl_tx_fifo_data_reg2;			/* 0x6ac */
	uint32 mac_main_core_tx_fifo_data_idx_reg;	/* 0x6b0 */
	uint32 mac_main_core_tx_fifo_data_reg0;		/* 0x6b4 */
	uint32 mac_main_core_tx_fifo_data_reg1;		/* 0x6b8 */
	uint32 mac_main_core_tx_fifo_data_reg2;		/* 0x6bc */
	uint32 mac_aux_core_tx_fifo_data_idx_reg;	/* 0x6c0 */
	uint32 mac_aux_core_tx_fifo_data_reg0;		/* 0x6c4 */
	uint32 mac_aux_core_tx_fifo_data_reg1;		/* 0x6c8 */
	uint32 mac_aux_core_tx_fifo_data_reg2;		/* 0x6cc */
	uint32 bt_tx_fifo_data_idx_reg;			/* 0x6d0 */
	uint32 bt_tx_fifo_data_reg0;			/* 0x6d4 */
	uint32 bt_tx_fifo_data_reg1;			/* 0x6d8 */
	uint32 bt_tx_fifo_data_reg2;			/* 0x6dc */
	uint32 wci2_tx_fifo_data_reg0;			/* 0x6e0 */
	uint32 wci2_tx_fifo_data_reg1;			/* 0x6e4 */
	uint32 sgt_tx_fifo_ctrl;			/* 0x6e8 */
	uint32 sgt_fifo_status_hpri;			/* 0x6ec */
	uint32 sgt_fifo_status_norm;			/* 0x6f0 */
	uint32 sgt_fifo_status_lpri;			/* 0x6f4 */
	uint32 PAD[PADSZ(0x6f8, 0x7a0)];		/* 0x6f8-0x7a0 */
	uint32 sg_timestamp_fifo_ctrl;			/* 0x7a4 */
	uint32 sgr_timestamp_data_rx;			/* 0x7a8 */
	uint32 sgr_timestamp_data_tx;			/* 0x7ac */
	uint32 sgr_fifo_int_reg;			/* 0x7b0 */
	uint32 sgr_fifo_int_mask_reg;			/* 0x7b4 */
	uint32 sgt_fifo_int_reg;			/* 0x7b8 */
	uint32 sgt_fifo_int_mask_reg;			/* 0x7bc */
	uint32 sg_fifo_debug_bus;			/* 0x7c0 */
	uint32 PAD[PADSZ(0x7c4, 0xbfc)];		/* 0x7c4-0xbfc */
	uint32 lhl_core_capab_adr;			/* 0xC00 */
	uint32 lhl_main_ctl_adr;			/* 0xC04 */
	uint32 lhl_pmu_ctl_adr;				/* 0xC08 */
	uint32 lhl_extlpo_ctl_adr;			/* 0xC0C */
	uint32 lpo_ctl_adr;				/* 0xC10 */
	uint32 lhl_lpo2_ctl_adr;			/* 0xC14 */
	uint32 lhl_osc32k_ctl_adr;			/* 0xC18 */
	uint32 lhl_clk_status_adr;			/* 0xC1C */
	uint32 lhl_clk_det_ctl_adr;			/* 0xC20 */
	uint32 lhl_clk_sel_adr;				/* 0xC24 */
	uint32 hidoff_cnt_adr[2];			/* 0xC28-0xC2C */
	uint32 lhl_autoclk_ctl_adr;			/* 0xC30 */
	uint32 PAD;					/* reserved */
	uint32 lhl_hibtim_adr;				/* 0xC38 */
	uint32 lhl_wl_ilp_val_adr;			/* 0xC3C */
	uint32 lhl_wl_armtim0_intrp_adr;		/* 0xC40 */
	uint32 lhl_wl_armtim0_st_adr;			/* 0xC44 */
	uint32 lhl_wl_armtim0_adr;			/* 0xC48 */
	uint32 PAD[PADSZ(0xc4c, 0xc6c)];		/* 0xC4C-0xC6C */
	uint32 lhl_wl_mactim0_intrp_adr;		/* 0xC70 */
	uint32 lhl_wl_mactim0_st_adr;			/* 0xC74 */
	uint32 lhl_wl_mactim_int0_adr;			/* 0xC78 */
	uint32 lhl_wl_mactim_frac0_adr;			/* 0xC7C */
	uint32 lhl_wl_mactim1_intrp_adr;		/* 0xC80 */
	uint32 lhl_wl_mactim1_st_adr;			/* 0xC84 */
	uint32 lhl_wl_mactim_int1_adr;			/* 0xC88 */
	uint32 lhl_wl_mactim_frac1_adr;			/* 0xC8C */
	uint32 lhl_wl_mactim2_intrp_adr;		/* 0xC90 */
	uint32 lhl_wl_mactim2_st_adr;			/* 0xC94 */
	uint32 lhl_wl_mactim_int2_adr;			/* 0xC98 */
	uint32 lhl_wl_mactim_frac2_adr;			/* 0xC9C */
	uint32 PAD[PADSZ(0xca0, 0xcac)];		/* 0xCA0-0xCAC */
	uint32 gpio_int_en_port_adr[4];			/* 0xCB0-0xCBC */
	uint32 gpio_int_st_port_adr[4];			/* 0xCC0-0xCCC */
	uint32 gpio_ctrl_iocfg_p_adr[40];		/* 0xCD0-0xD6C */
	uint32 lhl_lp_up_ctl1_adr;			/* 0xd70 */
	uint32 lhl_lp_dn_ctl1_adr;			/* 0xd74 */
	uint32 PAD[PADSZ(0xd78, 0xdb4)];		/* 0xd78-0xdb4 */
	uint32 lhl_sleep_timer_adr;			/* 0xDB8 */
	uint32 lhl_sleep_timer_ctl_adr;			/* 0xDBC */
	uint32 lhl_sleep_timer_load_val_adr;		/* 0xDC0 */
	uint32 lhl_lp_main_ctl_adr;			/* 0xDC4 */
	uint32 lhl_lp_up_ctl_adr;			/* 0xDC8 */
	uint32 lhl_lp_dn_ctl_adr;			/* 0xDCC */
	uint32 gpio_gctrl_iocfg_p0_p39_adr;		/* 0xDD0 */
	uint32 gpio_gdsctrl_iocfg_p0_p25_p30_p39_adr;	/* 0xDD4 */
	uint32 gpio_gdsctrl_iocfg_p26_p29_adr;		/* 0xDD8 */
	uint32 PAD[PADSZ(0xddc, 0xdf8)];		/* 0xDDC-0xDF8 */
	uint32 lhl_gpio_din0_adr;			/* 0xDFC */
	uint32 lhl_gpio_din1_adr;			/* 0xE00 */
	uint32 lhl_wkup_status_adr;			/* 0xE04 */
	uint32 lhl_ctl_adr;				/* 0xE08 */
	uint32 lhl_adc_ctl_adr;				/* 0xE0C */
	uint32 lhl_qdxyz_in_dly_adr;			/* 0xE10 */
	uint32 lhl_optctl_adr;				/* 0xE14 */
	uint32 lhl_optct2_adr;				/* 0xE18 */
	uint32 lhl_scanp_cntr_init_val_adr;		/* 0xE1C */
	uint32 lhl_opt_togg_val_adr[6];			/* 0xE20-0xE34 */
	uint32 lhl_optx_smp_val_adr;			/* 0xE38 */
	uint32 lhl_opty_smp_val_adr;			/* 0xE3C */
	uint32 lhl_optz_smp_val_adr;			/* 0xE40 */
	uint32 lhl_hidoff_keepstate_adr[3];		/* 0xE44-0xE4C */
	uint32 lhl_bt_slmboot_ctl0_adr[4];		/* 0xE50-0xE5C */
	uint32 lhl_wl_fw_ctl;				/* 0xE60 */
	uint32 lhl_wl_hw_ctl_adr[2];			/* 0xE64-0xE68 */
	uint32 lhl_bt_hw_ctl_adr;			/* 0xE6C */
	uint32 lhl_top_pwrseq_en_adr;			/* 0xE70 */
	uint32 lhl_top_pwrdn_ctl_adr;			/* 0xE74 */
	uint32 lhl_top_pwrup_ctl_adr;			/* 0xE78 */
	uint32 lhl_top_pwrseq_ctl_adr;			/* 0xE7C */
	uint32 lhl_top_pwrdn2_ctl_adr;			/* 0xE80 */
	uint32 lhl_top_pwrup2_ctl_adr;			/* 0xE84 */
	uint32 wpt_regon_intrp_cfg_adr;			/* 0xE88 */
	uint32 bt_regon_intrp_cfg_adr;			/* 0xE8C */
	uint32 wl_regon_intrp_cfg_adr;			/* 0xE90 */
	uint32 regon_intrp_st_adr;			/* 0xE94 */
	uint32 regon_intrp_en_adr;			/* 0xE98 */
	uint32 PAD[PADSZ(0xe9c, 0xeb4)];		/* 0xe9c-0xeb4 */
	uint32 lhl_lp_main_ctl1_adr;			/* 0xeb8 */
	uint32 lhl_lp_up_ctl2_adr;			/* 0xebc */
	uint32 lhl_lp_dn_ctl2_adr;			/* 0xec0 */
	uint32 lhl_lp_up_ctl3_adr;			/* 0xec4 */
	uint32 lhl_lp_dn_ctl3_adr;			/* 0xec8 */
	uint32 PAD[PADSZ(0xecc, 0xed8)];		/* 0xecc-0xed8 */
	uint32 lhl_lp_main_ctl2_adr;			/* 0xedc */
	uint32 lhl_lp_up_ctl4_adr;			/* 0xee0 */
	uint32 lhl_lp_dn_ctl4_adr;			/* 0xee4 */
	uint32 lhl_lp_up_ctl5_adr;			/* 0xee8 */
	uint32 lhl_lp_dn_ctl5_adr;			/* 0xeec */
	uint32 lhl_top_pwrdn3_ctl_adr;			/* 0xEF0 */
	uint32 lhl_top_pwrup3_ctl_adr;			/* 0xEF4 */
	uint32 PAD[PADSZ(0xef8, 0xf00)];		/* 0xEF8 - 0xF00 */
	uint32 error_status;				/* 0xF04 */
	uint32 error_parity;				/* 0xF08 */
	uint32 PAD;					/* 0xF0C */
	uint32 msg_buf_0[8];				/* 0xF10 - 0xF2C */
	uint32 PAD[PADSZ(0xf30, 0xf3c)];		/* 0xF30 - 0xF3C */
	uint32 CTRL_REG0;				/* 0xF40 */
	uint32 CTRL_REG1;				/* 0xF44 */
	uint32 chipID;					/* 0xF48 */
	uint32 PAD[PADSZ(0xf4c, 0xf54)];		/* 0xF4C - 0xF54 */
	uint32 timestamp_mask0;				/* 0xf58 */
	uint32 timestamp_mask1;				/* 0xf5c */
	uint32 wl_event_rdAddress;			/* 0xF60 */
	uint32 bt_event_rdAddress;			/* 0xF64 */
	uint32 interrupt_Address;			/* 0xF68 */
	uint32 PAD[PADSZ(0xf6c, 0xf70)];                /* 0xF6c - 0xF70 */
	uint32 coex_error_status;			/* 0xF74 */
	uint32 coex_error_parity;			/* 0xF78 */
	uint32 PAD;					/* 0xF7C */
	uint32 ar_buf_01[4];				/* 0xF80 - 0xF8C */
	uint32 PAD[PADSZ(0xf90,0xfac)];			/* 0xF90 - 0xFAC */
	uint32 coex_ctrl_reg0;				/* 0xFB0 */
	uint32 coex_ctrl_reg1;				/* 0xFB4 */
	uint32 coex_chip_id;				/* 0xFB8 */
	uint32 PAD[PADSZ(0xfbc, 0xfcc)];		/* 0xFBC - 0xFCC */
	uint32 coex_wl_event_rd;			/* 0xFD0 */
	uint32 coex_bt_event_rd;			/* 0xFD4 */
	uint32 coex_interrupt;				/* 0xFD8 */
	uint32 PAD;					/* 0xFDC */
	uint32 spmi_shared_reg_status_intMask_adr;	/* 0xFE0 */
	uint32 spmi_shared_reg_status_intStatus_adr;	/* 0xFE4 */
	uint32 spmi_shared_reg_status_wakeMask_adr;	/* 0xFE8 */
	uint32 spmi_shared_event_map_idx_adr;		/* 0xFEC */
	uint32 spmi_shared_event_map_data_adr;		/* 0xFF0 */
	uint32 spmi_coex_event_gpr_status_adr;		/* 0xFF4 */
} gciregs_t;

#define	GCI_CAP0_REV_MASK	0x000000ff

/* GCI Capabilities registers */
#define GCI_CORE_CAP_0_COREREV_MASK			0xFF
#define GCI_CORE_CAP_0_COREREV_SHIFT			0

#define GCI_INDIRECT_ADDRESS_REG_REGINDEX_MASK		0x3F
#define GCI_INDIRECT_ADDRESS_REG_REGINDEX_SHIFT		0
#define GCI_INDIRECT_ADDRESS_REG_GPIOINDEX_MASK		0xF
#define GCI_INDIRECT_ADDRESS_REG_GPIOINDEX_SHIFT	16

#define WLAN_BANKX_SLEEPPDA_REG_SLEEPPDA_MASK		0xFFFF

#define WLAN_BANKX_PKILL_REG_SLEEPPDA_MASK		0x1

/* WLAN BankXInfo Register */
#define WLAN_BANKXINFO_BANK_SIZE_MASK			0x00FFF000
#define WLAN_BANKXINFO_BANK_SIZE_SHIFT			12

/* WLAN Mem Info Register */
#define WLAN_MEM_INFO_REG_NUMSOCRAMBANKS_MASK		0x000000FF
#define WLAN_MEM_INFO_REG_NUMSOCRAMBANKS_SHIFT		0

#define WLAN_MEM_INFO_REG_NUMD11MACBM_MASK		0x0000FF00
#define WLAN_MEM_INFO_REG_NUMD11MACBM_SHIFT		8

#define WLAN_MEM_INFO_REG_NUMD11MACUCM_MASK		0x00FF0000
#define WLAN_MEM_INFO_REG_NUMD11MACUCM_SHIFT		16

#define WLAN_MEM_INFO_REG_NUMD11MACSHM_MASK		0xFF000000
#define WLAN_MEM_INFO_REG_NUMD11MACSHM_SHIFT		24

/* GCI chip status register 9 */
#define GCI_CST9_SCAN_DIS	(1u << 31u)	/* scan core disable */

/* GCI Output register indices */
#define GCI_OUTPUT_IDX_0	0
#define GCI_OUTPUT_IDX_1	1
#define GCI_OUTPUT_IDX_2	2
#define GCI_OUTPUT_IDX_3	3

#endif /* !_LANGUAGE_ASSEMBLY && !__ASSEMBLY__ */

#endif	/* _SBGCI_H */
