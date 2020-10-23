/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (PHYDM_LA_MODE_SUPPORT)

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	#if (RTL8197F_SUPPORT || RTL8822B_SUPPORT || RTL8192F_SUPPORT)
	#include "rtl8197f/Hal8197FPhyReg.h"
	#include "WlanHAL/HalMac88XX/halmac_reg2.h"
	#else
	#include "WlanHAL/HalHeader/HalComReg.h"
	#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	#if WPP_SOFTWARE_TRACE
	#include "phydm_adc_sampling.tmh"
	#endif
#endif

#if RTL8814B_SUPPORT
boolean phydm_la_finish_addr_recover_8814B(void *dm_void, u32 *finish_addr)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	boolean recover_success;

	if (dm->support_ic_type != ODM_RTL8814B)
		return false;

	if (smp->la_buff_mode == ADCSMP_BUFF_HALF) {
		if (*finish_addr < 0x4000) /*0~0x4000*/
			*finish_addr += 0x8000;

		recover_success = true;
	} else {
		if (*finish_addr >= 0x4000 && *finish_addr < 0x8000)
			recover_success = true;
		else
			recover_success = false;
	}
	pr_debug("[8814B] recover_success=(%d)\n", recover_success);

	return recover_success;
}
#endif

#if RTL8198F_SUPPORT
void phydm_la_pre_run(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;
	u8 i = 0;
	u8 tmp = 0;
	u8 target_polling_bit = BIT(1);

	if (!(dm->support_ic_type & ODM_RTL8198F))
		return;

	if (smp->la_trig_mode == PHYDM_ADC_MAC_TRIG)
		return;

	/*pre run */
	/*force to bb trigger*/
	odm_set_mac_reg(dm, R_0x7c0, BIT(3), 0);
	/*dma_trig_and(AND1) output 1*/
	odm_set_bb_reg(dm, R_0x1ce4, 0xf0000000, 0x0);
	/*r_dma_trigger_AND1_inv = 1*/
	odm_set_bb_reg(dm, R_0x1ce8, BIT5, 1); /*@AND 1 val*/
	/* polling bit for BB ADC mode */
	odm_set_mac_reg(dm, R_0x7c0, BIT(1), 1);

	pr_debug("buf[end:start]=(0x%x~0x%x)\n", buf->end_pos, buf->start_pos);

	do {
		tmp = odm_read_1byte(dm, R_0x7c0);
		if ((tmp & target_polling_bit) == false) {
			pr_debug("LA pre-run fail.\n");
			phydm_la_stop(dm);
			phydm_release_bb_dbg_port(dm);
		} else {
			ODM_delay_ms(100);
			pr_debug("LA pre-run while_cnt = %d.\n", i);
			i++;
		}
	} while (i < 3);

	/*r_dma_trigger_AND1_inv = 0*/
	odm_set_bb_reg(dm, R_0x1ce8, BIT5, 0); /*@AND 1 val*/

	if (smp->la_trig_mode == PHYDM_ADC_MAC_TRIG)
		odm_set_mac_reg(dm, R_0x7c0, BIT(3), 1);
}
#endif

#if (RTL8821C_SUPPORT || RTL8195B_SUPPORT)
void
phydm_la_clk_en(void *dm_void, boolean enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 val = (enable) ? 1 : 0;

	if (!(dm->support_ic_type & (ODM_RTL8195B | ODM_RTL8821C)))
		return;

	if (dm->support_ic_type == ODM_RTL8821C &&
	    dm->cut_version == ODM_CUT_A)
		return;

	odm_set_bb_reg(dm, R_0x95c, BIT(23), val);
}
#endif

#if (RTL8723F_SUPPORT)
void
phydm_la_mac_clk_en(void *dm_void, boolean enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 val = (enable) ? 1 : 0;

	if (!(dm->support_ic_type & ODM_RTL8723F))
		return;

	odm_set_mac_reg(dm, R_0x1008, BIT(1), val);
}
#endif

#if (RTL8197F_SUPPORT)
void
phydm_la_stop_dma_8197f(void *dm_void, enum phydm_backup_type opt)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;

	if (dm->support_ic_type != ODM_RTL8197F)
		return;

	if (opt == PHYDM_BACKUP) {
		/*Stop DMA*/
		smp->backup_dma = odm_get_mac_reg(dm, R_0x300, 0xffff);
		odm_set_mac_reg(dm, R_0x300, 0x7fff, 0x7fff);
	} else { /*restore*/
		/*Resume DMA*/
		odm_set_mac_reg(dm, R_0x300, 0x7fff, smp->backup_dma);
	}
}
#endif

#ifdef PHYDM_COMPILE_LA_STORE_IN_IMEM
void
phydm_la_mv_data_2_tx_buffer(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;

	if (!(dm->support_ic_type & PHYDM_LA_STORE_IN_IMEM_IC))
		return;

	pr_debug("GetTxPktBuf from iMEM\n");
	odm_set_mac_reg(dm, R_0x7c0, BIT(0), 0x0); /*Disable LA mode HW block*/

	/* 98F LA memory loccation is separate from normal
	 * driver use, DMA is no longer required to stop
	 */
	#if (RTL8197F_SUPPORT)
	phydm_la_stop_dma_8197f(dm, PHYDM_BACKUP);
	#endif

	/* @move LA mode content from IMEM to TxPktBuffer
	 * Source : OCPBASE_IMEM 0x00000000
	 * Destination : OCPBASE_TXBUF 0x18780000
	 * Length : 64K
	 */
	GET_HAL_INTERFACE(dm->priv)->init_ddma_handler(dm->priv,
						       OCPBASE_IMEM,
						       OCPBASE_TXBUF
						       + buf->start_pos,
						       0x10000);
}
#endif

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT

void phydm_la_bb_adv_reset_jgr3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct la_adv_trig *adv = &smp->adv_trig_table;

	odm_memory_set(dm, adv, 0, sizeof(struct la_adv_trig));

}

void phydm_la_bb_adv_trig_setting_jgr3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct la_adv_trig *adv = &smp->adv_trig_table;

	pr_debug(" *ADV BB-trig = %d\n", adv->la_adv_bbtrigger_en);

	if (!adv->la_adv_bbtrigger_en) { /*normal LA mode & back to default*/
		/*@AND0*/
		odm_set_bb_reg(dm, R_0x1ce4, BIT(27), 0);

		/*@AND1*/
		odm_set_bb_reg(dm, R_0x1ce4, MASKH4BITS, 0);
		odm_set_bb_reg(dm, R_0x1ce8, BIT(5), 0); /*@AND 1 inv*/
		/*@AND2*/
		odm_set_bb_reg(dm, R_0x1ce8, 0x3c0, 0);
		odm_set_bb_reg(dm, R_0x1ce8, BIT(15), 0); /*@AND 2 inv*/
		/*@AND3*/
		odm_set_bb_reg(dm, R_0x1ce8, 0xf0000, 0);
		odm_set_bb_reg(dm, R_0x1ce8, BIT(25), 0); /*@AND 3 inv*/
		/*@AND4*/
		odm_set_bb_reg(dm, R_0x1cf0, MASKDWORD, 0); /*@AND 4 mask en*/
		odm_set_bb_reg(dm, R_0x1ce8, BIT(26), 0); /*@AND 4 inv*/
	} else {
		/*@AND0 */
		/*path 1 default: enable ori. BB trigger*/
		odm_set_bb_reg(dm, R_0x1ce4, BIT(27),
			       (adv->la_ori_bb_dis ? 1 : 0));

		/* @AND1 */
		odm_set_bb_reg(dm, R_0x1ce8, BIT(5), adv->la_and1_inv);
		odm_set_bb_reg(dm, R_0x1ce4, MASKH4BITS, adv->la_and1_sel);
		odm_set_bb_reg(dm, R_0x1ce8, 0x1f, adv->la_and1_val);

		/*@AND2 */
		odm_set_bb_reg(dm, R_0x1ce8, BIT(15), adv->la_and2_inv);
		odm_set_bb_reg(dm, R_0x1ce8, 0x3c0, adv->la_and2_sel);
		odm_set_bb_reg(dm, R_0x1ce8, 0x7c00, adv->la_and2_val);

		/*@AND3 */
		odm_set_bb_reg(dm, R_0x1ce8, BIT(25), adv->la_and3_inv);
		odm_set_bb_reg(dm, R_0x1ce8, 0xf0000, adv->la_and3_sel);
		odm_set_bb_reg(dm, R_0x1ce8, 0x1f00000, adv->la_and3_val);

		/*@AND4 */
		odm_set_bb_reg(dm, R_0x1ce8, BIT(26), adv->la_and4_inv);
		odm_set_bb_reg(dm, R_0x1cf0, MASKDWORD, adv->la_and4_mask);
		odm_set_bb_reg(dm, R_0x1cec, MASKDWORD, adv->la_and4_bitmap);
	}
}

void phydm_la_bb_adv_cmd_show_jgr3(void *dm_void, u32 *_used,
				   char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct la_adv_trig *adv = &smp->adv_trig_table;

	PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
		 "  *And0 Disable=%d\n", adv->la_ori_bb_dis);
	PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
		 "  *And1{sel,val,inv}={0x%x,0x%x,%d}\n  *And2{sel,val,inv}={0x%x,0x%x,%d}\n  *And3{sel,val,inv}={0x%x,0x%x,%d}\n",
		 adv->la_and1_sel, adv->la_and1_val, adv->la_and1_inv,
		 adv->la_and2_sel, adv->la_and2_val, adv->la_and2_inv,
		 adv->la_and3_sel, adv->la_and3_val, adv->la_and3_inv);
	PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
		 "  *And4{mask,bitmap,inv}={0x%x,0x%x,%d}\n",
		 adv->la_and4_mask, adv->la_and4_bitmap, adv->la_and4_inv);
}

void phydm_la_bb_adv_cmd_jgr3(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct la_adv_trig *adv = &smp->adv_trig_table;
	u32 var1[10] = {0};
	u32 adv_trig_en;

	if (!(dm->support_ic_type & ODM_IC_JGR3_SERIES))
		return;

	if ((strcmp(input[2], "show") == 0)) {
		phydm_la_bb_adv_cmd_show_jgr3(dm, _used, output, _out_len);
		return;
	}

	PHYDM_SSCANF(input[2], DCMD_HEX, &var1[0]);
	PHYDM_SSCANF(input[3], DCMD_HEX, &var1[1]);
	PHYDM_SSCANF(input[4], DCMD_HEX, &var1[2]);
	PHYDM_SSCANF(input[5], DCMD_HEX, &var1[3]);
	PHYDM_SSCANF(input[6], DCMD_HEX, &var1[4]);

	adv_trig_en = var1[0];

	if (adv_trig_en != 1) {
		PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "Back to Ori-BB-trig\n");
		phydm_la_bb_adv_reset_jgr3(dm);
		return;
	}

	adv->la_adv_bbtrigger_en = true;

	if (var1[1] == 0) {
		adv->la_ori_bb_dis = (boolean)var1[2];
	} else if (var1[1] == 1) {
		adv->la_and1_sel = (u8)var1[2];
		adv->la_and1_val = (u8)var1[3];
		adv->la_and1_inv = (boolean)var1[4];
	} else if (var1[1] == 2) {
		adv->la_and2_sel = (u8)var1[2];
		adv->la_and2_val = (u8)var1[3];
		adv->la_and2_inv = (boolean)var1[4];
	} else if (var1[1] == 3) {
		adv->la_and3_sel = (u8)var1[2];
		adv->la_and3_val = (u8)var1[3];
		adv->la_and2_inv = (boolean)var1[4];
	}  else if (var1[1] == 4) {
		adv->la_and4_mask = var1[2];
		adv->la_and4_bitmap = var1[3];
		adv->la_and4_inv = (boolean)var1[4];
	}

	PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
		 "[Adv_trig_en=%d]\n\n", adv_trig_en);

	phydm_la_bb_adv_cmd_show_jgr3(dm, _used, output, _out_len);
}

void phydm_la_cmd_fast_jgr3(void *dm_void, char input[][16], u32 *_used,
			    char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct la_adv_trig *adv = &smp->adv_trig_table;
	enum auto_detection_state ad_mode;
	const u8 ofdm_codeword[8] = {0xb, 0xf, 0xa, 0xe, 0x9, 0xd, 0x8, 0xc};
	u32 codeword;
	u8 rate_idx;
	u32 trig_time_cca = 0;
	s32 val_sign32_tmp = 0;
	u32 var[10] = {0};
	u8 bw = *dm->band_width;

	if (!(dm->support_ic_type & ODM_IC_JGR3_SERIES)) {
		PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "Only Support for JGR-3 ICs\n");
		return;
	}

	if (bw > 2) {
		PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "Not Support for BW > %dM\n", 20 << bw);
		return;
	}

	PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var[0]);
	PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var[1]);
	PHYDM_SSCANF(input[4], DCMD_DECIMAL, &var[2]);

	trig_time_cca = ((smp->smp_number_max >> (bw + 1)) / 10)
			- (2 << (2 - bw)) - (2 - bw);

	if (var[0] < 10) {
	/*=== [Type: 0 ~ 10] : CCA P-edge trigger ==========================*/
		/*--- Basic Trigger Setting --------------------------------*/
		smp->la_trig_mode = 1;
		smp->la_trig_sig_sel = 2;
		smp->la_trigger_time = trig_time_cca;
		smp->la_mac_mask_or_hdr_sel = 0;
		smp->la_trigger_edge = 0;
		smp->la_smp_rate = 2 - bw;
		smp->la_count = 0;
		if (var[0] == 0) { /*AGC*/
			smp->la_dma_type = 5;
			smp->la_dbg_port = 0x870;
		} else if (var[0] == 1) { /*EVM*/
			smp->la_dma_type = 4;
			smp->la_dbg_port = 0x392;
		} else if (var[0] == 2) { /*SNR*/
			smp->la_dma_type = 4;
			if (var[1] == 0)
				smp->la_dbg_port = 0x89e;
			else
				smp->la_dbg_port = 0xa9e;
		} else if (var[0] == 3) { /*CFO*/
			smp->la_dma_type = 4;
			if (var[1] == 0)
				smp->la_dbg_port = 0x88c;
			else
				smp->la_dbg_port = 0xa8c;
		}  else if (var[0] == 4) { /*ADC*/
			if (var[1] == 0) {
				smp->la_dma_type = 0;
				smp->la_dbg_port = 0x880;
			} else {
				smp->la_dma_type = 1;
				smp->la_dbg_port = 0xa80;
			}
		}
		/*--- Adv-Trigger Setting------------------------------------*/
		adv->la_adv_bbtrigger_en = false;
	} else if (var[0] < 20) {
	/*=== [Type: 10 ~ 19]: RX-EVM Trigger ===============================*/
		/*--- Basic Trigger Setting ---------------------------------*/
		smp->la_trig_mode = 0;
		smp->la_trig_sig_sel = 0;
		smp->la_mac_mask_or_hdr_sel = 0;
		smp->la_trigger_edge = 0;
		smp->la_smp_rate = 2 - bw;
		smp->la_count = 0;
		smp->la_dma_type = 4;
		smp->la_dbg_port = 0x392;

		/*--- Adv-Trigger Setting -----------------------------------*/
		phydm_la_bb_adv_reset_jgr3(dm);
		adv->la_adv_bbtrigger_en = true;

		/*And[0]*/
		adv->la_ori_bb_dis = true;

		/*And[1]*/
		adv->la_and1_inv = 0;
		adv->la_and1_sel = 4; /*RX-state*/
		if (var[2] == 0) {
			/*L-preamble 8+8+4 = 20*/
			smp->la_trigger_time = trig_time_cca - 20;
			/*Legacy Data*/
			adv->la_and1_val = 5;
		} else if (var[2] == 1) {
			/*HT-preamble (8+8+4) + (8+4+4*Nrx) = 32 + Nrx * 4*/
			smp->la_trigger_time = trig_time_cca - 32 -
					       (dm->num_rf_path * 4);
			/*HT Data*/
			adv->la_and1_val = 18;
		} else {
			/*VHT-preamble (8+8+4) + (8+4+4*Nrx) +4 = 36 + Nrx * 4*/
			smp->la_trigger_time = trig_time_cca - 36 -
					       (dm->num_rf_path * 4);
			/*VHT Data*/
			adv->la_and1_val = 18;
		}

		/*And[2]*/
		adv->la_and2_inv = 0;
		adv->la_and2_sel = 0; /*Disable*/

		/*And[3]*/
		adv->la_and2_inv = 0;
		adv->la_and3_sel = 0; /*Disable*/

		/*And[4]*/
		adv->la_and4_inv = 0;

		if (var[0] == 11) {
			/*[>= -X dB]*/
			if (var[1] == 2) {
				adv->la_and4_bitmap = 0;
				adv->la_and4_mask = 0x1;
			} else if (var[1] == 4) {
				adv->la_and4_bitmap = 0;
				adv->la_and4_mask = 0x3;
			} else if (var[1] == 8) {
				adv->la_and4_bitmap = 0;
				adv->la_and4_mask = 0x7;
			} else if (var[1] == 16) {
				adv->la_and4_bitmap = 0;
				adv->la_and4_mask = 0xf;
			} else if (var[1] == 32) {
				adv->la_and4_bitmap = 0;
				adv->la_and4_mask = 0x1f;
			} else if (var[1] == 64) {
				adv->la_and4_bitmap = 0;
				adv->la_and4_mask = 0x3f;
			} else {
				PDM_SNPF(*_out_len, *_used, output + *_used,
					 *_out_len - *_used,
					 "Not Support >= -%d dB\n", var[1]);
				return;
			}
		} else if (var[0] == 10) {
			/*[<= -X dB]*/
			if (var[1] == 2) {
				adv->la_and4_bitmap = 0x7e;
				adv->la_and4_mask = 0x7e;
			} else if (var[1] == 4) {
				adv->la_and4_bitmap = 0x7c;
				adv->la_and4_mask = 0x7c;
			} else if (var[1] == 8) {
				adv->la_and4_bitmap = 0x78;
				adv->la_and4_mask = 0x78;
			} else if (var[1] == 16) {
				adv->la_and4_bitmap = 0x70;
				adv->la_and4_mask = 0x70;
			} else if (var[1] == 32) {
				adv->la_and4_bitmap = 0x60;
				adv->la_and4_mask = 0x60;
			} else if (var[1] == 64) {
				adv->la_and4_bitmap = 0x40;
				adv->la_and4_mask = 0x40;
			} else {
				PDM_SNPF(*_out_len, *_used, output + *_used,
					 *_out_len - *_used,
					 "Not Support <= -%d dB\n", var[1]);
				return;
			}
		} else if (var[0] == 12) {
			/*[= -X dB]*/
			val_sign32_tmp = 0 - (s32)var[1];
			adv->la_and4_bitmap = (u32)(val_sign32_tmp & 0x7f);
			adv->la_and4_mask = 0x7f;
		}
	} else if (var[0] < 30) {
	/*=== [Type: 20 ~ 29]: RX-Rate Trigger ==============================*/
		/*--- Basic Trigger Setting ---------------------------------*/
		smp->la_trig_mode = 0;
		smp->la_trig_sig_sel = 0;
		smp->la_mac_mask_or_hdr_sel = 0;
		smp->la_trigger_edge = 0;
		smp->la_smp_rate = 2 - bw;
		smp->la_count = 0;
		smp->la_dma_type = 4;

		rate_idx = (u8)var[1];

		/*--- Adv-Trigger Setting -----------------------------------*/
		phydm_la_bb_adv_reset_jgr3(dm);
		adv->la_adv_bbtrigger_en = true;

		/*And[0]*/
		adv->la_ori_bb_dis = true;

		/*And[1]*/
		adv->la_and1_inv = 0;
		adv->la_and1_sel = 4; /*RX-state*/

		if (rate_idx <= ODM_RATE54M && rate_idx >= ODM_RATE6M) {
			ad_mode = AD_LEGACY_MODE;
			codeword = (u32)ofdm_codeword[rate_idx - ODM_RATE6M];
			smp->la_dbg_port = 0x3a9;
			/*L-preamble 8+8 = 16*/
			smp->la_trigger_time = trig_time_cca - 20;
			/*Legacy Data*/
			adv->la_and1_val = 5;
		} else if (rate_idx <= ODM_RATEMCS31) {
			ad_mode = AD_HT_MODE;
			codeword = (u32)(rate_idx - ODM_RATEMCS0);
			smp->la_dbg_port = 0x3aa;
			/*HT-preamble (8+8+4) + (8+4+4*Nrx) = 32 + Nrx * 4*/
			smp->la_trigger_time = trig_time_cca - 32 -
					       (dm->num_rf_path * 4);
			/*HT,VHT Data*/
			adv->la_and1_val = 18;
		} else if (rate_idx <= ODM_RATEVHTSS4MCS9) {
			ad_mode = AD_VHT_MODE;
			codeword = (u32)phydm_rate_order_compute(dm, rate_idx);
			codeword--;
			smp->la_dbg_port = 0x3ab;
			/*VHT-preamble (8+8+4) + (8+4+4*Nrx) = 36 + Nrx * 4*/
			smp->la_trigger_time = trig_time_cca - 36 -
					       (dm->num_rf_path * 4);
			/*HT,VHT Data*/
			adv->la_and1_val = 18;
		} else {
			PDM_SNPF(*_out_len, *_used, output + *_used,
				 *_out_len - *_used,
				 "Not Support\n");
			return;
		}

		/*And[2]*/
		adv->la_and2_inv = 0;
		adv->la_and2_sel = 0; /*Disable*/

		/*And[3]*/
		adv->la_and2_inv = 0;
		adv->la_and3_sel = 0; /*Disable*/

		/*And[4]*/
		adv->la_and4_inv = 0;

		if (var[0] == 20) {
			if (ad_mode == AD_LEGACY_MODE) {
				adv->la_and4_bitmap = codeword;
				adv->la_and4_mask = 0x3000000f;
			} else if (ad_mode == AD_HT_MODE) {
				adv->la_and4_bitmap = (2 << 28) | codeword;
				adv->la_and4_mask = 0x3000003f;
			}  else { /* AD_VHT_MODE*/
				adv->la_and4_bitmap = (1 << 28) |
						      (codeword << 4);
				adv->la_and4_mask = 0x300000f0;
			}
		} else {
			PDM_SNPF(*_out_len, *_used, output + *_used,
				 *_out_len - *_used,
				 "Not Support\n");
			return;
		}
	} else {
		PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "Not Support\n");
		return;
	}
	PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
		 "[Basic-Trigger]\n");
	PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
		 "  *echo lamode 1 %d %d %d %d %d %x %d %d %d\n\n",
		 smp->la_trig_mode, smp->la_trig_sig_sel, smp->la_dma_type,
		 smp->la_trigger_time, smp->la_mac_mask_or_hdr_sel,
		 smp->la_dbg_port, smp->la_trigger_edge, smp->la_smp_rate,
		 smp->la_count);
	pr_debug("echo lamode 1 %d %d %d %d %d %x %d %d %d\n\n",
		 smp->la_trig_mode, smp->la_trig_sig_sel, smp->la_dma_type,
		 smp->la_trigger_time, smp->la_mac_mask_or_hdr_sel,
		 smp->la_dbg_port, smp->la_trigger_edge, smp->la_smp_rate,
		 smp->la_count);

	if (adv->la_adv_bbtrigger_en) {
		PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "[Adv-Trigger]\n");
		PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "  *And0 Disable=%d\n", adv->la_ori_bb_dis);
		PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "  *And1{sel,val,inv}={0x%x,0x%x,%d}\n  *And2{sel,val,inv}={0x%x,0x%x,%d}\n  *And3{sel,val,inv}={0x%x,0x%x,%d}\n",
			 adv->la_and1_sel, adv->la_and1_val, adv->la_and1_inv,
			 adv->la_and2_sel, adv->la_and2_val, adv->la_and2_inv,
			 adv->la_and3_sel, adv->la_and3_val, adv->la_and3_inv);
		PDM_SNPF(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "  *And4{mask,bitmap,inv}={0x%x,0x%x,%d}\n",
			 adv->la_and4_mask, adv->la_and4_bitmap,
			 adv->la_and4_inv);
	}
	phydm_la_set(dm);
}

#endif

void
phydm_la_buffer_print(void *dm_void, char input[][16], u32 *_used,
		      char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;
	u64 la_pattern_msb, la_pattern_lsb;
	u64 la_pattern, la_pattern_part;
	s64 tmp_s64;
	u64 mask = 0xffffffff;
	u8 mask_length = 0;
	u32 i;
	u32 idx;
	u32 var[10] = {0};

	if (!buf->octet || buf->length == 0 || buf->length < smp->smp_number)
		return;

	PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var[0]);
	PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var[1]);
	PHYDM_SSCANF(input[4], DCMD_DECIMAL, &var[2]);
	PHYDM_SSCANF(input[5], DCMD_DECIMAL, &var[3]);

	pr_debug("echo lamode 1 %d %d %d %d %d %x %d %d %d\n\n",
		 smp->la_trig_mode, smp->la_trig_sig_sel, smp->la_dma_type,
		 smp->la_trigger_time, smp->la_mac_mask_or_hdr_sel,
		 smp->la_dbg_port, smp->la_trigger_edge, smp->la_smp_rate,
		 smp->la_count);
	pr_debug("[LA Data Dump] smp_number = %d\n", smp->smp_number);
	pr_debug("Dump_Start\n");

	if (var[0] == 0) {
		for (i = 0; i < smp->smp_number; i++) {
			idx = i << 1;
			pr_debug("%08x%08x\n", buf->octet[idx],
				 buf->octet[idx + 1]);
		}
	} else if (var[0] == 1) {
		/*------------------------*/
		if (var[1] == 0)
			pr_debug("[Hex]\n");
		else if (var[1] == 1)
			pr_debug("[Dec unsigned]\n");
		else if (var[1] == 2)
			pr_debug("[Dec signed]\n");

		pr_debug("BIT[%d:%d]\n", var[3], var[2]);

		if (var[2] > var[3]) {
			pr_debug("[Warning] BIT_L > BIT_H\n");
			return;
		}

		mask_length = (u8)(var[3] - var[2] + 1);
		mask = phydm_gen_bitmask(mask_length) << var[2];
		/*------------------------*/
		for (i = 0; i < smp->smp_number; i++) {
			idx = i << 1;
			la_pattern_msb = (u64)buf->octet[idx];
			la_pattern_lsb = (u64)buf->octet[idx + 1];
			la_pattern = (la_pattern_msb << 32) | la_pattern_lsb;
			la_pattern_part = (la_pattern & mask) >> var[2];

			if (var[1] == 0) {
				pr_debug("0x%llx\n", la_pattern_part);
			} else if (var[1] == 1) {
				pr_debug("%llu\n", la_pattern_part);
			} else if (var[1] == 2) {
				tmp_s64 = phydm_cnvrt_2_sign_64(la_pattern_part,
								mask_length);
				pr_debug("%lld\n", tmp_s64);
			}
		}
	}
	pr_debug("Dump_End\n\n");
}

void
phydm_la_buffer_release(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;

	if (buf->length != 0x0) {
		odm_free_memory(dm, buf->octet, buf->length);
		buf->length = 0x0;
	}
}

boolean
phydm_la_buffer_allocate(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	#endif
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;
	boolean ret = true;

	pr_debug("[LA mode BufferAllocate]\n");

	if (buf->length == 0) {
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (PlatformAllocateMemoryWithZero(adapter, (void **)&
						   buf->octet,
						   buf->buffer_size) !=
						   RT_STATUS_SUCCESS)
			ret = false;
	#else
		odm_allocate_memory(dm, (void **)&buf->octet, buf->buffer_size);

		if (!buf->octet)
			ret = false;
	#endif

		if (ret)
			buf->length = buf->buffer_size;
	}

	return ret;
}

void phydm_la_access_tx_pkt_buf(void *dm_void, u32 addr, u32 buff_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;
	u32 page;
	u32 data_l = 0, data_h = 0;

	#if (RTL8192F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8192F) {
		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		indirect_access_sdram_8192f(dm->adapter, TX_PACKET_BUFFER,
					    TRUE, (u16)addr >> 3, 0,
					    &data_h, &data_l);
		#else
		odm_write_1byte(dm, R_0x0106, 0x69);
		odm_set_mac_reg(dm, R_0x0140, MASKDWORD, addr >> 3);
		data_l = odm_get_mac_reg(dm, R_0x0144, MASKDWORD);
		data_h = odm_get_mac_reg(dm, R_0x0148, MASKDWORD);
		odm_write_1byte(dm, R_0x0106, 0x0);
		#endif
	} else
	#endif
	{
		/* Reg140=0x780+(addr>>12),
		 * addr=0x30~0x3F, total 16 pages
		 */
		page = addr >> 12;

		if (page != smp->txff_page) {
			smp->txff_page = page;
			odm_set_mac_reg(dm, R_0x0140, MASKLWORD, 0x780 + page);
		}
		data_l = odm_read_4byte(dm, R_0x8000 + (addr & 0xfff));
		data_h = odm_read_4byte(dm, R_0x8000 + (addr & 0xfff) + 4);
	}

	buf->octet[buff_idx] = data_h;
	buf->octet[buff_idx + 1] = data_l;

	/*@==== [Print LA Patterns] ==========================================*/
	if (smp->is_la_print)
		pr_debug("%08x%08x\n", data_h, data_l);
}

void phydm_la_get_tx_pkt_buf(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;
	u32 i = 0, value32 = 0;
	u32 addr = 0, finish_addr = 0; /* @(unit: 8Byte)*/
	boolean is_round_up = false;
	u32 addr_8byte = 0;
	u32 round_up_point = 0;
	#if (RTL8814B_SUPPORT)
	boolean recover_success = true;
	#endif

	odm_memory_set(dm, buf->octet, 0, buf->length);
	pr_debug("GetTxPktBuf\n");

	/*@==== [Get LA Report] ==============================================*/
	if (dm->support_ic_type & ODM_RTL8192F) {
		value32 = odm_read_4byte(dm, R_0x7f0);
		is_round_up = (boolean)((value32 & BIT(31)) >> 31);
		finish_addr = (value32 & 0x7FFF8000) >> 15; /*@16 bit (unit: 8Byte)*/
	} else {
		odm_write_1byte(dm, R_0x0106, 0x69);
		value32 = odm_read_4byte(dm, R_0x7c0);
		is_round_up = (boolean)((value32 & BIT(31)) >> 31);

		if (dm->support_ic_type & PHYDM_LA_STORE_IN_IMEM_IC)
			finish_addr = (value32 & 0x7FFF8000) >> 15; /*@16 bit (unit: 8Byte)*/
		else
			finish_addr = (value32 & 0x7FFF0000) >> 16; /*@15bit (unit: 8Byte)*/
	}

	#if (RTL8814B_SUPPORT)
	recover_success = phydm_la_finish_addr_recover_8814B(dm, &finish_addr);
	#endif

	pr_debug("start_addr = ((0x%x)), end_addr = ((0x%x)), buffer_size = ((0x%x))\n",
		 buf->start_pos, buf->end_pos, buf->buffer_size);
	if (is_round_up) {
		pr_debug("buf_start(0x%x)|----2---->|finish_addr(0x%x)|----1---->|buf_end(0x%x)\n",
			 buf->start_pos, finish_addr << 3, buf->end_pos);
		addr = (finish_addr + 2) << 3; /*+1 or +2 ??*/
		round_up_point = (buf->end_pos - addr) >> 3; /*@Byte to 8Byte*/
		smp->smp_number = smp->smp_number_max;
		pr_debug("is_round_up=(%d), round_up_point=(%d), 0x7c0/0x7F0=(0x%x), smp_number=(%d)\n",
			 is_round_up, round_up_point, value32, smp->smp_number);
	} else {
		pr_debug("buf_start(0x%x)|------->|finish_addr(0x%x)             |buf_end(0x%x)\n",
			 buf->start_pos, finish_addr << 3, buf->end_pos);
		addr = buf->start_pos;
		addr_8byte = addr >> 3;
		smp->smp_number = DIFF_2(addr_8byte, finish_addr);

		pr_debug("is_round_up=(%d), smp_number=(%d)\n",
			 is_round_up, smp->smp_number);
	}

	/*@==== [Get LA Patterns in TXFF] ====================================*/
	pr_debug("Dump_Start\n");
	#ifdef PHYDM_COMPILE_LA_STORE_IN_IMEM
	phydm_la_mv_data_2_tx_buffer(dm);
	#endif

	#if (RTL8814B_SUPPORT)
	if ((dm->support_ic_type & ODM_RTL8814B) && !recover_success) {
		addr = buf->start_pos;
		smp->smp_number = smp->smp_number_max;
	}
	#endif

	for (i = 0; i < smp->smp_number; i++) {
		phydm_la_access_tx_pkt_buf(dm, addr, i << 1);
		addr += 8;

		if (addr >= buf->end_pos)
			addr = buf->start_pos; /*Ring buffer*/
	}

	#if (RTL8197F_SUPPORT)
	phydm_la_stop_dma_8197f(dm, PHYDM_RESTORE);
	#endif
	pr_debug("Dump_End\n");
}

void phydm_la_set_trig_src(void *dm_void, u8 la_trig_mode)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 reg = (dm->support_ic_type == ODM_RTL8192F) ? R_0x7f0 : R_0x7c0;

	if (la_trig_mode == PHYDM_ADC_MAC_TRIG)
		odm_set_mac_reg(dm, reg, BIT(3), 1);
	else
		odm_set_mac_reg(dm, reg, BIT(3), 0);
}

void phydm_la_set_mac_iq_dump(void *dm_void, boolean impossible_trig_condi)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	u32 reg_value = 0;
	u32 reg1 = 0, reg2 = 0, reg3 = 0;

	if (dm->support_ic_type & ODM_RTL8192F) {
		reg1 = R_0x7f0;
		reg2 = R_0x7f4;
		reg3 = R_0x7f8;
	} else {
		reg1 = R_0x7c0;
		reg2 = R_0x7c4;
		reg3 = R_0x7c8;
	}

	odm_write_1byte(dm, reg1, 0); /*@clear all reg1*/
	/*@Enable LA mode HW block*/
	odm_set_mac_reg(dm, reg1, BIT(0), 1);

	#if (RTL8723F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8723F)
		phydm_la_mac_clk_en(dm, true);
	#endif

	if (smp->la_trig_mode == PHYDM_MAC_TRIG) {
		smp->la_dump_mode = LA_MAC_DBG_DUMP;
		/*polling bit for MAC mode*/
		odm_set_mac_reg(dm, reg1, BIT(2), 1);
		/*trigger mode for MAC*/
		odm_set_mac_reg(dm, reg1, 0x18,	smp->la_trigger_edge);
		pr_debug("[MAC_trig] ref_mask=(0x%x), ref_value=(0x%x), dbg_port =(0x%x)\n",
			 smp->la_mac_mask_or_hdr_sel, smp->la_trig_sig_sel,
			 smp->la_dbg_port);
		/*@[Set MAC Debug Port]*/
		odm_set_mac_reg(dm, R_0xf4, BIT(16), 1);
		odm_set_mac_reg(dm, R_0x38, 0xff0000, smp->la_dbg_port);
		odm_set_mac_reg(dm, reg2, MASKDWORD,
				smp->la_mac_mask_or_hdr_sel);
		odm_set_mac_reg(dm, reg3, MASKDWORD, smp->la_trig_sig_sel);
	} else {
		smp->la_dump_mode = LA_BB_ADC_DUMP;

		if (smp->la_trig_mode == PHYDM_ADC_MAC_TRIG) {
			/*polling bit for MAC trigger event*/
			if (impossible_trig_condi)
				phydm_la_set_trig_src(dm, PHYDM_ADC_BB_TRIG);
			else
				phydm_la_set_trig_src(dm, PHYDM_ADC_MAC_TRIG);

			odm_set_mac_reg(dm, reg1, 0xc0,	smp->la_trig_sig_sel);

			if (smp->la_trig_sig_sel == ADCSMP_TRIG_REG) {
				/* @manual trigger reg1[5] = 0->1*/
				odm_set_mac_reg(dm, reg1, BIT(5), 1);
			}
		}
		/*polling bit for BB ADC mode*/
		odm_set_mac_reg(dm, reg1, BIT(1), 1);
	}

	reg_value = odm_get_mac_reg(dm, reg1, 0xff);
	pr_debug("4. [Set MAC IQ dump] 0x%x[7:0]=(0x%x)\n", reg1, reg_value);

	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD,
		    ("4. [Set MAC IQ dump] 0x%x[7:0]=(0x%x)\n", reg1,
		    reg_value));
	#endif
}

void phydm_la_set_bb_dbg_port(void *dm_void, boolean impossible_trig_condi)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;

	u8	trig_mode = smp->la_trig_mode;
	u32	trig_sel = smp->la_trig_sig_sel;
	u32	dbg_port = smp->la_dbg_port;

	if (trig_mode == PHYDM_MAC_TRIG)
		trig_sel = 0; /*@ignore this setting*/

	/*set BB debug port*/
	if (impossible_trig_condi) {
		dbg_port = 0xf;
		trig_sel = 0;
		pr_debug("[BB Setting] fake-trigger!\n");
	}

	if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3, dbg_port)) {
		pr_debug(" *Set dbg_port=(0x%x)\n", dbg_port);
	} else {
		dbg_port = phydm_get_bb_dbg_port_idx(dm);
		pr_debug("[Set dbg_port fail!] Curr-DbgPort=0x%x\n", dbg_port);
	}

	/*@debug port bit*/
	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x95c, 0x1f, trig_sel);
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		odm_set_bb_reg(dm, R_0x1ce4, 0x3e000, trig_sel);
	#endif
	} else {
		odm_set_bb_reg(dm, R_0x9a0, 0x1f, trig_sel);
	}

	if (smp->la_trig_mode == PHYDM_ADC_BB_TRIG) {
		pr_debug(" *Set dbg_port[BIT] = %d\n", trig_sel);

		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD,
			    (" *Set dbg_port[BIT] = %d\n", trig_sel));
		#endif
	}
}

void phydm_la_set_bb(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;

	u8	trig_mode = smp->la_trig_mode;
	u8	edge = smp->la_trigger_edge;
	u8	smp_rate = smp->la_smp_rate;
	u8	dma_type = smp->la_dma_type;
	u32	dbg_port_hdr_sel = 0;
	char	*trig_mode_word = NULL;

	pr_debug("3. [BB Setting] mode=(%d), Edge=(%s), smp_rate=(%dM), Dma_type=(%d)\n",
		 trig_mode,
		 (edge == 0) ? "P" : "N", 80 >> smp_rate, dma_type);

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		if (trig_mode == PHYDM_ADC_RF0_TRIG)
			dbg_port_hdr_sel = 9; /*@DBGOUT_RFC_a[31:0]*/
		else if (trig_mode == PHYDM_ADC_RF1_TRIG)
			dbg_port_hdr_sel = 8; /*@DBGOUT_RFC_b[31:0]*/
		else if ((trig_mode == PHYDM_ADC_BB_TRIG) ||
			 (trig_mode == PHYDM_ADC_MAC_TRIG)) {
			if (smp->la_mac_mask_or_hdr_sel <= 0xf)
				dbg_port_hdr_sel = smp->la_mac_mask_or_hdr_sel;
			else
				dbg_port_hdr_sel = 0;
		}

		phydm_bb_dbg_port_header_sel(dm, dbg_port_hdr_sel);

		odm_set_bb_reg(dm, R_0x8b4, BIT(7), 1);/*@update rpt every pkt*/
		odm_set_bb_reg(dm, R_0x95c, 0xf00, dma_type);
		/*@0: posedge, 1: negedge*/
		odm_set_bb_reg(dm, R_0x95c, BIT(31), edge);
		odm_set_bb_reg(dm, R_0x95c, 0xe0, smp_rate);
		/*	@(0:) '80MHz'
		 *	(1:) '40MHz'
		 *	(2:) '20MHz'
		 *	(3:) '10MHz'
		 *	(4:) '5MHz'
		 *	(5:) '2.5MHz'
		 *	(6:) '1.25MHz'
		 *	(7:) '160MHz (for BW160 ic)'
		 */
		#if (RTL8821C_SUPPORT || RTL8195B_SUPPORT)
		phydm_la_clk_en(dm, true);
		#endif

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		odm_set_bb_reg(dm, R_0x1eb4, BIT(23), 0x1);/*@update rpt every pkt*/
		/*@MAC-PHY timing*/
		odm_set_bb_reg(dm, R_0x1ce4, BIT(7) | BIT(6), 0);
		odm_set_bb_reg(dm, R_0x1cf4, BIT(23), 1); /*@LA mode on*/
		odm_set_bb_reg(dm, R_0x1ce4, 0x3f, dma_type);
		/*@0: posedge, 1: negedge ??*/
		odm_set_bb_reg(dm, R_0x1ce4, BIT(26), edge);
		odm_set_bb_reg(dm, R_0x1ce4, 0x700, smp_rate);

		phydm_la_bb_adv_trig_setting_jgr3(dm);
	#endif
	} else {
		if (dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8192F))
			odm_set_bb_reg(dm, R_0xd00, BIT(26), 0x1); /*@update rpt every pkt*/

		#if (RTL8192F_SUPPORT)
		if ((dm->support_ic_type & ODM_RTL8192F))
			/*@LA reset HW block enable for true-mac asic*/
			odm_set_bb_reg(dm, R_0x9a0, BIT(15), 1);
		#endif

		odm_set_bb_reg(dm, R_0x9a0, 0xf00, dma_type);
		/*@0: posedge, 1: negedge*/
		odm_set_bb_reg(dm, R_0x9a0, BIT(31), edge);
		odm_set_bb_reg(dm, R_0x9a0, 0xe0, smp_rate);
		/*	@(0:) '80MHz'
		 *	(1:) '40MHz'
		 *	(2:) '20MHz'
		 *	(3:) '10MHz'
		 *	(4:) '5MHz'
		 *	(5:) '2.5MHz'
		 *	(6:) '1.25MHz'
		 *	(7:) '160MHz (for BW160 ic)'
		 */
	}
}

void phydm_la_set_mac_trigger_time(void *dm_void, u32 trigger_time_mu_sec)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 time_unit_num = 0;
	u32 unit = 0;

	if (trigger_time_mu_sec < 128)
		unit = 0; /*unit: 1mu sec*/
	else if (trigger_time_mu_sec < 256)
		unit = 1; /*unit: 2mu sec*/
	else if (trigger_time_mu_sec < 512)
		unit = 2; /*unit: 4mu sec*/
	else if (trigger_time_mu_sec < 1024)
		unit = 3; /*unit: 8mu sec*/
	else if (trigger_time_mu_sec < 2048)
		unit = 4; /*unit: 16mu sec*/
	else if (trigger_time_mu_sec < 4096)
		unit = 5; /*unit: 32mu sec*/
	else if (trigger_time_mu_sec < 8192)
		unit = 6; /*unit: 64mu sec*/
	else if (trigger_time_mu_sec < 16384)
		if (dm->support_ic_type & ODM_RTL8723F)
			unit = 7; /*unit: 128mu sec*/

	time_unit_num = (u8)(trigger_time_mu_sec >> unit);

	pr_debug("2. [Set Trigger Time] Trig_Time = ((%d)) * unit = ((2^%d us))\n",
		 time_unit_num, unit);
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, (
		    "3. [Set Trigger Time] Trig_Time = ((%d)) * unit = ((2^%d us))\n",
		    time_unit_num, unit));
	#endif

	if (dm->support_ic_type & ODM_RTL8192F) {
		odm_set_mac_reg(dm, R_0x7fc, BIT(2) | BIT(1) | BIT(0), unit);
		odm_set_mac_reg(dm, R_0x7f0, 0x7f00, (time_unit_num & 0x7f));
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_RTL8814B) {
		odm_set_mac_reg(dm, R_0x7cc, BIT(20) | BIT(19) | BIT(18), unit);
		odm_set_mac_reg(dm, R_0x7c0, 0x7f00, (time_unit_num & 0x7f));
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		odm_set_mac_reg(dm, R_0x7cc, BIT(18) | BIT(17) | BIT(16), unit);
		odm_set_mac_reg(dm, R_0x7c0, 0x7f00, (time_unit_num & 0x7f));
	#endif
	} else {
		odm_set_mac_reg(dm, R_0x7cc, BIT(20) | BIT(19) | BIT(18), unit);
		odm_set_mac_reg(dm, R_0x7c0, 0x7f00, (time_unit_num & 0x7f));
	}
}

void phydm_la_set_buff_mode(void *dm_void, enum la_buff_mode mode)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	struct rtl8192cd_priv		*priv = dm->priv;
	u8 normal_LA_on = priv->pmib->miscEntry.normal_LA_on;
#endif
	u32 buff_size_base = 0;
	u32 end_pos_tmp = 0;

	smp->la_buff_mode = mode;
	switch (dm->support_ic_type) {
	case ODM_RTL8814A:
		buff_size_base = 0x10000;
		end_pos_tmp = 0x40000;
		break;
	case ODM_RTL8822B:
	case ODM_RTL8822C:
	case ODM_RTL8812F:
		buff_size_base = 0x20000; /*@WIN: TX_FIFO_SIZE_LA_8822C*/
		end_pos_tmp = 0x40000;
		break;
	case ODM_RTL8814B:
		buff_size_base = 0x30000;
		end_pos_tmp = 0x60000;
		break;
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	case ODM_RTL8197F:
	case ODM_RTL8198F:
	case ODM_RTL8197G:
		buff_size_base = 0x10000;
		end_pos_tmp = (normal_LA_on == 1) ? 0x20000 : 0x10000;
		break;
#endif
	case ODM_RTL8192F:
		buff_size_base = 0xE000;
		end_pos_tmp = 0x10000;
		break;
	case ODM_RTL8821C:
		buff_size_base = 0x8000;
		end_pos_tmp = 0x10000;
		break;
	case ODM_RTL8195B:
		buff_size_base = 0x4000;
		end_pos_tmp = 0x8000;
		break;
	case ODM_RTL8723F:
		buff_size_base = 0x20000;
		end_pos_tmp = 0x60000;
		break;
	default:
		pr_debug("[%s] Warning!", __func__);
		break;
	}

	buf->buffer_size = buff_size_base;

	if (dm->support_ic_type & ODM_RTL8814B) {
		if (mode == ADCSMP_BUFF_HALF) {
			odm_set_mac_reg(dm, R_0x7cc, BIT(21), 0);
		} else {
			buf->buffer_size = buf->buffer_size << 1;
			odm_set_mac_reg(dm, R_0x7cc, BIT(21), 1);
		}
	} else if (dm->support_ic_type & FULL_BUFF_MODE_SUPPORT) {
		if (mode == ADCSMP_BUFF_HALF) {
			odm_set_mac_reg(dm, R_0x7cc, BIT(30), 0);
		} else {
			buf->buffer_size = buf->buffer_size << 1;
			odm_set_mac_reg(dm, R_0x7cc, BIT(30), 1);
		}
	}

	buf->end_pos = end_pos_tmp;
	buf->start_pos = end_pos_tmp - buf->buffer_size;
	smp->smp_number_max = buf->buffer_size >> 3;

	pr_debug("start_addr=(0x%x), end_addr=(0x%x), buffer_size=(0x%x), smp_number_max=(%d)\n",
		 buf->start_pos, buf->end_pos, buf->buffer_size,
		 smp->smp_number_max);
}

void phydm_la_adc_smp_start(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	u8 tmp_u1b = 0;
	u8 i = 0;
	u8 polling_bit = 0;
	u8 bkp_val = 0;
	boolean polling_ok = false;
	boolean impossible_trig_condi = (smp->en_fake_trig) ? true : false;

	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD,
		    ("1. [BB Setting] Mode=(%d), DbgPort=(0x%x), Edge=(%d), SmpRate=(%d), Trig_Sel=(0x%x), Dma_type=(%d)\n",
		    smp->la_trig_mode, smp->la_dbg_port, smp->la_trigger_edge,
		    smp->la_smp_rate, smp->la_trig_sig_sel, smp->la_dma_type));
	#endif
	pr_debug("1. [BB Setting] trig_mode = ((%d)), dbg_port = ((0x%x)), Trig_Edge = ((%d)), smp_rate = ((%d)), Trig_Sel = ((0x%x)), Dma_type = ((%d))\n",
		 smp->la_trig_mode, smp->la_dbg_port, smp->la_trigger_edge,
		 smp->la_smp_rate, smp->la_trig_sig_sel, smp->la_dma_type);

	if(dm->support_ic_type & ODM_RTL8723F)
		bkp_val = (u8)odm_get_mac_reg(dm, R_0x1008, BIT(1));

	phydm_la_set_mac_trigger_time(dm, smp->la_trigger_time);
	phydm_la_set_bb(dm);
	phydm_la_set_bb_dbg_port(dm, impossible_trig_condi);
	phydm_la_set_mac_iq_dump(dm, impossible_trig_condi);

	#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_stop(dm->priv);
	#endif

	if (impossible_trig_condi) {
		ODM_delay_ms(100);
		phydm_la_set_bb_dbg_port(dm, false);

		if (smp->la_trig_mode == PHYDM_ADC_MAC_TRIG) {
			phydm_la_set_trig_src(dm, PHYDM_ADC_MAC_TRIG);
		}
	}
#if RTL8198F_SUPPORT
	phydm_la_pre_run(dm);
#endif
	polling_bit = (smp->la_dump_mode == LA_BB_ADC_DUMP) ? BIT(1) : BIT(2);
	do { /*Polling time always use 100ms, when it exceed 2s, break loop*/
		if (dm->support_ic_type & ODM_RTL8192F)
			tmp_u1b = odm_read_1byte(dm, R_0x7f0);
		else
			tmp_u1b = odm_read_1byte(dm, R_0x7c0);

		pr_debug("[%d] polling rpt=((0x%x))\n", i, tmp_u1b);

		if (smp->adc_smp_state != ADCSMP_STATE_SET) {
			pr_debug("[state Error] state != ADCSMP_STATE_SET\n");
			break;

		} else if (tmp_u1b & polling_bit) {
			ODM_delay_ms(100);
			i++;
			continue;
		} else {
			pr_debug("[LA Query OK] polling_bit=%d\n", polling_bit);
			polling_ok = true;
			break;
		}
	} while (i < 20);

	if (smp->adc_smp_state == ADCSMP_STATE_SET) {
		if (polling_ok)
			phydm_la_get_tx_pkt_buf(dm);
		else
			pr_debug("[Polling timeout]\n");
	}

	#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_resume(dm->priv);
	#endif

	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (smp->adc_smp_state == ADCSMP_STATE_SET)
		smp->adc_smp_state = ADCSMP_STATE_QUERY;
	#endif

	pr_debug("[LA mode] la_count = ((%d))\n", smp->la_count);
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD,
		    ("[LA mode] la_count = ((%d))\n", smp->la_count));
	#endif

	phydm_la_stop(dm);

	if (smp->la_count == 0) {
		pr_debug("LA Dump finished ---------->\n\n\n");
		phydm_release_bb_dbg_port(dm);

		#if (RTL8821C_SUPPORT || RTL8195B_SUPPORT)
		phydm_la_clk_en(dm, false);
		#endif
		#if (RTL8723F_SUPPORT)
		if(dm->support_ic_type & ODM_RTL8723F)
			phydm_la_mac_clk_en(dm, (bkp_val == 1) ? true : false);
		#endif
	} else {
		smp->la_count--;
		pr_debug("LA Dump more ---------->\n\n\n");
		phydm_la_set(dm);
	}
}

void phydm_la_set(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean is_set_success = true;
	struct rt_adcsmp *smp = &dm->adcsmp;

	if (smp->adc_smp_state != ADCSMP_STATE_IDLE)
		is_set_success = false;
	else if (smp->adc_smp_buf.length == 0)
		is_set_success = phydm_la_buffer_allocate(dm);

	if (!is_set_success) {
		pr_debug("[LA Set Fail] LA_State=(%d)\n", smp->adc_smp_state);
		return;
	}

	smp->adc_smp_state = ADCSMP_STATE_SET;

	pr_debug("[LA Set Success] LA_State=(%d)\n", smp->adc_smp_state);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

	pr_debug("ADCSmp_work_item_index=(%d)\n", smp->la_work_item_index);

	if (smp->la_work_item_index != 0) {
		odm_schedule_work_item(&smp->adc_smp_work_item_1);
		smp->la_work_item_index = 0;
	} else {
		odm_schedule_work_item(&smp->adc_smp_work_item);
		smp->la_work_item_index = 1;
	}
#else
	phydm_la_adc_smp_start(dm);
#endif
}

void phydm_la_cmd(void *dm_void, char input[][16], u32 *_used, char *output,
		  u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_LA_MODE))
		return;

#ifdef PHYDM_COMPILE_LA_STORE_IN_IMEM
	if (dm->support_ic_type & PHYDM_LA_STORE_IN_IMEM_IC) {
		if (dm->is_download_fw)
			return;
	}
	#if RTL8198F_SUPPORT
	if (dm->support_ic_type & ODM_RTL8198F) {
		if (!*dm->mp_mode && !dm->priv->pmib->miscEntry.normal_LA_on) {
			pr_debug("plz re-set normal_LA_on = 1 & DnUp.\n");
			return;
		}
	}
	#endif
#endif

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	/*@dbg_print("echo cmd input_num = %d\n", input_num);*/

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "=====[LA Mode Help] =============================\n");
		/*Trigger*/
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "BB_trig:  1 0 {DbgPort Bit} {DMA#} {TrigTime} {DbgPort_head(Jgr2)}\n\t{DbgPort} {Edge: 0(P),1(N)} {f_smp:80 >> N} {Capture num}\n\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "MAC_trig: 1 1 {0-ok/1-fail/2-cca} {DMA#} {TrigTime} {DbgPort_head(Jgr2)}\n\t{DbgPort} {N/A} {f_smp:80 >> N} {Cpture num}\n\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "All: {En} {0:ADC_BB_trig,1:ADC MAC_trig,2:RF0,3:RF1,4:MAC}\n\t{BB:dbg_port[bit],BB_MAC:0-ok/1-fail/2-cca,MAC:ref} {DMA#} {TrigTime}\n\t{DbgPort_head/ref_mask} {dbg_port} {0:P_Edge, 1:N_Edge} {SpRate:0-80M,1-40M,2-20M} {Capture num}\n\n");
		/*Adv-Trig*/
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "adv show\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "adv {adv_trig_en} {0:And[0]_disable} {en}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "adv {adv_trig_en} {1~3: And[3:0]} {Sel} {Val} {Inv}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "adv {adv_trig_en} {4: And[4]} {BitMask} {BitVal} {Inv}\n\n");
		#endif
		/*Setting*/
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "set {1:tx_buff_size} {0: half, 1:full}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "set {2:Fake Trigger} {en}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "set {3:Auto Print} {en}\n\n");
		/*Print*/
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "print {0: all(Hex)}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "print {1: partial} {0:hex 1:dec 2: s-dec} {bit_L} {bit_H}\n\n");

		/*Fast Trigger*/
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fast {0: CCA trig & AGC Dbg Port}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fast {1: CCA trig & EVM Dbg Port}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fast {2: CCA trig & SNR Dbg Port}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fast {3: CCA trig & CFO Dbg Port}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fast {4: CCA trig & ADC output Dbg Port}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fast {10: EVM>=-X dB, 11: EVM<=-X dB} {X=2/4/8/16/32/64} {0:Lgcy, 1:HT}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fast {12: EVM=-X dB} {X} {0:Lgcy, 1:HT}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fast {20: RX-rate-idx=X} {X}\n");

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "=================================================\n");
	} else if ((strcmp(input[1], "print") == 0)) {
		phydm_la_buffer_print(dm, input, &used, output, &out_len);
#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if ((strcmp(input[1], "fast") == 0)) {
		phydm_la_cmd_fast_jgr3(dm, input, &used, output, &out_len);

	} else if ((strcmp(input[1], "adv") == 0)) {
		phydm_la_bb_adv_cmd_jgr3(dm, input, &used, output, &out_len);
#endif
	} else if ((strcmp(input[1], "set") == 0)) {
		PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);

		if (var1[1] == 1) {
			PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var1[2]);
			phydm_la_set_buff_mode(dm, (enum la_buff_mode)var1[2]);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Buff_mode=(%d/2)\n", smp->la_buff_mode + 1);
		} else if (var1[1] == 2) {
			PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var1[2]);
			smp->en_fake_trig = (boolean)var1[2];

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "en_fake_trig=(%d)\n", smp->en_fake_trig);
		} else if (var1[1] == 3) {
			PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var1[2]);
			smp->is_la_print = (boolean)var1[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Auto print=(%d)\n", smp->is_la_print);
		}
	} else if (var1[0] == 1) {
		PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);

		smp->la_trig_mode = (u8)var1[1];

		if (smp->la_trig_mode == PHYDM_MAC_TRIG)
			PHYDM_SSCANF(input[3], DCMD_HEX, &var1[2]);
		else
			PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var1[2]);
		smp->la_trig_sig_sel = var1[2];

		PHYDM_SSCANF(input[4], DCMD_DECIMAL, &var1[3]);
		PHYDM_SSCANF(input[5], DCMD_DECIMAL, &var1[4]);
		PHYDM_SSCANF(input[6], DCMD_HEX, &var1[5]);
		PHYDM_SSCANF(input[7], DCMD_HEX, &var1[6]);
		PHYDM_SSCANF(input[8], DCMD_DECIMAL, &var1[7]);
		PHYDM_SSCANF(input[9], DCMD_DECIMAL, &var1[8]);
		PHYDM_SSCANF(input[10], DCMD_DECIMAL, &var1[9]);

		smp->la_dma_type = (u8)var1[3];
		smp->la_trigger_time = var1[4]; /*unit: us*/
		smp->la_mac_mask_or_hdr_sel = var1[5];
		smp->la_dbg_port = var1[6];
		smp->la_trigger_edge = (u8)var1[7];
		smp->la_smp_rate = (u8)(var1[8] & 0x7);
		smp->la_count = var1[9];

		pr_debug("echo lamode %d %d %d %d %d %d %x %d %d %d\n",
			 var1[0], var1[1], var1[2], var1[3], var1[4],
			 var1[5], var1[6], var1[7], var1[8], var1[9]);
		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD,
			    ("echo lamode %d %d %d %d %d %d %x %d %d %d\n",
			    var1[0], var1[1], var1[2], var1[3],
			    var1[4], var1[5], var1[6], var1[7],
			    var1[8], var1[9]));
		#endif

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "a.En= ((1)),  b.mode = ((%d)), c.Trig_Sel = ((0x%x)), d.Dma_type = ((%d))\n",
			 smp->la_trig_mode, smp->la_trig_sig_sel,
			 smp->la_dma_type);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "e.Trig_Time = ((%dus)), f.Dbg_head/mac_ref_mask = ((0x%x)), g.dbg_port = ((0x%x))\n",
			 smp->la_trigger_time,
			 smp->la_mac_mask_or_hdr_sel, smp->la_dbg_port);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "h.Trig_edge = ((%d)), i.smp rate = ((%d MHz)), j.Cap_num = ((%d))\n",
			 smp->la_trigger_edge, (80 >> smp->la_smp_rate),
			 smp->la_count);

		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "k.en_new_bbtrigger = ((%d))\n",
			 smp->adv_trig_table.la_adv_bbtrigger_en);
		#endif

		phydm_la_set(dm);
	} else {
		phydm_la_stop(dm);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Disable LA mode\n");
	}

	*_used = used;
	*_out_len = out_len;
}

void phydm_la_stop(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;

	smp->adc_smp_state = ADCSMP_STATE_IDLE;
}

void phydm_la_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct rt_adcsmp *smp = &dm->adcsmp;
	struct rt_adcsmp_string *buf = &smp->adc_smp_buf;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_LA_MODE))
		return;

	smp->adc_smp_state = ADCSMP_STATE_IDLE;
	smp->is_la_print = true;
	smp->en_fake_trig = false;
	smp->txff_page = 0xffffffff;
	phydm_la_set_buff_mode(dm, ADCSMP_BUFF_HALF);

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	phydm_la_bb_adv_reset_jgr3(dm);
	#endif
}

void adc_smp_de_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_LA_MODE))
		return;

	phydm_la_stop(dm);
	phydm_la_buffer_release(dm);
}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
void adc_smp_work_item_callback(void *context)
{
	void *adapter = (void *)context;
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;
	struct rt_adcsmp *smp = &dm->adcsmp;

	pr_debug("[WorkItem Call back] LA_State=(%d)\n", smp->adc_smp_state);
	phydm_la_adc_smp_start(dm);
}
#endif
#endif /*@endif PHYDM_LA_MODE_SUPPORT*/
