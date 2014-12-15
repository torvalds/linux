/*
 * AMLOGIC lcd controller driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 * Modify:  Evoke Zhang <evoke.zhang@amlogic.com>
 * compatible dts
 *
 */
#include <linux/init.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <plat/regops.h>
#include <mach/am_regs.h>
#include <mach/lcd_reg.h>
#include <linux/amlogic/vout/lcdoutc.h>
#include <linux/amlogic/vout/aml_lcd_common.h>
#include <mach/clock.h>
#include <mach/mod_gate.h>
#include <asm/fiq.h>
#include <linux/delay.h>
#include <linux/of.h>
#include "lcd_config.h"

#define VPP_OUT_SATURATE	(1 << 0)

static spinlock_t gamma_write_lock;
static spinlock_t lcd_clk_lock;

static Lcd_Config_t *lcd_Conf;
static unsigned char lcd_gamma_init_err = 0;
static unsigned gamma_cntl_port_offset = 0;

void lcd_config_init(Lcd_Config_t *pConf);

#define SS_LEVEL_MAX	7
static const char *lcd_ss_level_table[]={
	"0",
	"0.5%",
	"1%",
	"2%",
	"3%",
	"4%",
	"5%",
};

static void print_lcd_driver_version(void)
{
    printk("lcd driver version: %s%s\n\n", LCD_DRV_DATE, LCD_DRV_TYPE);
}

static void lcd_ports_ctrl_lvds(Bool_t status)
{
	if (status) {
		WRITE_LCD_REG_BITS(LVDS_GEN_CNTL, 1, 3, 1); //enable lvds fifo
		if (lcd_Conf->lcd_basic.lcd_bits == 6)
			WRITE_LCD_REG_BITS(LVDS_PHY_CNTL4, 0x27, 0, 7);	//enable LVDS 3 channels
		else
			WRITE_LCD_REG_BITS(LVDS_PHY_CNTL4, 0x2f, 0, 7); //enable LVDS 4 channels
	}
	else {
		WRITE_LCD_REG_BITS(LVDS_PHY_CNTL3, 0, 0, 1);
		WRITE_LCD_REG_BITS(LVDS_PHY_CNTL5, 0, 11, 1);	//shutdown lvds phy
		WRITE_LCD_REG_BITS(LVDS_PHY_CNTL4, 0, 0, 7);	//disable LVDS 4 channels
		WRITE_LCD_REG_BITS(LVDS_GEN_CNTL, 0, 3, 1);	//disable lvds fifo
	}

	lcd_print("%s: %s\n", __FUNCTION__, (status ? "ON" : "OFF"));
}

static void lcd_ports_ctrl_ttl(Bool_t status)
{
	struct pinctrl_state *s;
	int ret;
	
	if (IS_ERR(lcd_Conf->lcd_misc_ctrl.pin)) {
		printk("set ttl_ports_ctrl pinmux error.\n");
		return;
	}

	if (status) {
	if (lcd_Conf->lcd_basic.lcd_bits == 6) {
			if (lcd_Conf->lcd_timing.de_valid == 0) {
				s = pinctrl_lookup_state(lcd_Conf->lcd_misc_ctrl.pin, "ttl_6bit_hvsync_on");
			}
			else if (lcd_Conf->lcd_timing.hvsync_valid == 0) {
				s = pinctrl_lookup_state(lcd_Conf->lcd_misc_ctrl.pin, "ttl_6bit_de_on");
			}
			else {
				s = pinctrl_lookup_state(lcd_Conf->lcd_misc_ctrl.pin, "ttl_6bit_hvsync_de_on");	//select pinmux
			}
		}
		else {
			if (lcd_Conf->lcd_timing.de_valid == 0) {
				s = pinctrl_lookup_state(lcd_Conf->lcd_misc_ctrl.pin, "ttl_8bit_hvsync_on");
			}	
			else if (lcd_Conf->lcd_timing.hvsync_valid == 0) {
				s = pinctrl_lookup_state(lcd_Conf->lcd_misc_ctrl.pin, "ttl_8bit_de_on");
			}
			else {
				s = pinctrl_lookup_state(lcd_Conf->lcd_misc_ctrl.pin, "ttl_8bit_hvsync_de_on");	//select pinmux
			}
		}
		if (IS_ERR(lcd_Conf->lcd_misc_ctrl.pin)) {
			printk("set ttl_ports_ctrl pinmux error.\n");
			devm_pinctrl_put(lcd_Conf->lcd_misc_ctrl.pin);
			return;
		}

		ret = pinctrl_select_state(lcd_Conf->lcd_misc_ctrl.pin, s);	//set pinmux and lock pins
		if (ret < 0) {
			printk("set ttl_ports_ctrl pinmux error.\n");
			devm_pinctrl_put(lcd_Conf->lcd_misc_ctrl.pin);
			return;
		}
	}else {
		//pinctrl_put(lcd_Conf->lcd_misc_ctrl.pin);	//release pins
		if (lcd_Conf->lcd_basic.lcd_bits == 6) {
			s = pinctrl_lookup_state(lcd_Conf->lcd_misc_ctrl.pin, "ttl_6bit_hvsync_de_off");	//select pinmux
		}
		else {
			s = pinctrl_lookup_state(lcd_Conf->lcd_misc_ctrl.pin, "ttl_8bit_hvsync_de_off");	//select pinmux
		}
		if (IS_ERR(lcd_Conf->lcd_misc_ctrl.pin)) {
			printk("set ttl_ports_ctrl pinmux error.\n");
			devm_pinctrl_put(lcd_Conf->lcd_misc_ctrl.pin);
			return;
		}
		
		ret = pinctrl_select_state(lcd_Conf->lcd_misc_ctrl.pin, s);	//set pinmux and lock pins
		if (ret < 0) {
			printk("set ttl_ports_ctrl pinmux error.\n");
			devm_pinctrl_put(lcd_Conf->lcd_misc_ctrl.pin);
			return;
		}
	}
	lcd_print("%s: %s\n", __FUNCTION__, (status ? "ON" : "OFF"));
}

static void lcd_ports_ctrl_mlvds(Bool_t status)
{
	return;
}

static void lcd_ports_ctrl(Bool_t status)
{
    switch(lcd_Conf->lcd_basic.lcd_type) {
        case LCD_DIGITAL_LVDS:
            lcd_ports_ctrl_lvds(status);
            break;
        case LCD_DIGITAL_TTL:
            lcd_ports_ctrl_ttl(status);
            break;
        case LCD_DIGITAL_MINILVDS:
            lcd_ports_ctrl_mlvds(status);
            break;
        default:
            printk("Invalid LCD type.\n");
            break;
    }
}

#define LCD_GAMMA_RETRY_CNT  1000
static void write_gamma_table(u16 *data, u32 rgb_mask, u16 gamma_coeff, u32 gamma_reverse)
{
	int i;
	int cnt = 0;
	unsigned long flags = 0;
	
	spin_lock_irqsave(&gamma_write_lock, flags);
	rgb_mask = gamma_sel_table[rgb_mask];
	while ((!(READ_LCD_REG((L_GAMMA_CNTL_PORT+gamma_cntl_port_offset)) & (0x1 << LCD_ADR_RDY))) && (cnt < LCD_GAMMA_RETRY_CNT)) {
		udelay(10);
		cnt++;
	};
	WRITE_LCD_REG((L_GAMMA_ADDR_PORT+gamma_cntl_port_offset), (0x1 << LCD_H_AUTO_INC) | (0x1 << rgb_mask) | (0x0 << LCD_HADR));
	if (gamma_reverse == 0) {
		for (i=0;i<256;i++) {
			cnt = 0;
			while ((!( READ_LCD_REG((L_GAMMA_CNTL_PORT+gamma_cntl_port_offset)) & (0x1 << LCD_WR_RDY))) && (cnt < LCD_GAMMA_RETRY_CNT)) {
				udelay(10);
				cnt++;
			};
			WRITE_LCD_REG((L_GAMMA_DATA_PORT+gamma_cntl_port_offset), (data[i] * gamma_coeff / 100));
		}
	}
	else {
		for (i=0;i<256;i++) {
			cnt = 0;
			while ((!( READ_LCD_REG((L_GAMMA_CNTL_PORT+gamma_cntl_port_offset)) & (0x1 << LCD_WR_RDY))) && (cnt < LCD_GAMMA_RETRY_CNT)) {
				udelay(10);
				cnt++;
			};
			WRITE_LCD_REG((L_GAMMA_DATA_PORT+gamma_cntl_port_offset), (data[255-i] * gamma_coeff / 100));
		}
	}
	cnt = 0;
	while ((!(READ_LCD_REG((L_GAMMA_CNTL_PORT+gamma_cntl_port_offset)) & (0x1 << LCD_ADR_RDY))) && (cnt < LCD_GAMMA_RETRY_CNT)) {
		udelay(10);
		cnt++;
	};
	WRITE_LCD_REG((L_GAMMA_ADDR_PORT+gamma_cntl_port_offset), (0x1 << LCD_H_AUTO_INC) | (0x1 << rgb_mask) | (0x23 << LCD_HADR));

	if (cnt >= LCD_GAMMA_RETRY_CNT)
		lcd_gamma_init_err = 1;
	
	spin_unlock_irqrestore(&gamma_write_lock, flags);
}

static void set_gamma_table_lcd(unsigned gamma_en)
{
	lcd_print("%s\n", __FUNCTION__);

	lcd_gamma_init_err = 0;
	if (lcd_Conf->lcd_basic.lcd_type == LCD_DIGITAL_TTL)
		gamma_cntl_port_offset = 0x80;
	else
		gamma_cntl_port_offset = 0x0;
	write_gamma_table(lcd_Conf->lcd_effect.GammaTableR, GAMMA_SEL_R, lcd_Conf->lcd_effect.gamma_r_coeff, ((lcd_Conf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_REVERSE) & 1));
	write_gamma_table(lcd_Conf->lcd_effect.GammaTableG, GAMMA_SEL_G, lcd_Conf->lcd_effect.gamma_g_coeff, ((lcd_Conf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_REVERSE) & 1));
	write_gamma_table(lcd_Conf->lcd_effect.GammaTableB, GAMMA_SEL_B, lcd_Conf->lcd_effect.gamma_b_coeff, ((lcd_Conf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_REVERSE) & 1));

	if (lcd_gamma_init_err) {
		WRITE_LCD_REG_BITS((L_GAMMA_CNTL_PORT + gamma_cntl_port_offset), 0, 0, 1);
		printk("[warning]: write gamma table error, gamma table disabled\n");
	}
	else
		WRITE_LCD_REG_BITS((L_GAMMA_CNTL_PORT + gamma_cntl_port_offset), gamma_en, 0, 1);
}

static void write_tcon_double(MLVDS_Tcon_Config_t *mlvds_tcon)
{
    int channel_num = mlvds_tcon->channel_num;
    int hv_sel = (mlvds_tcon->hv_sel) & 1;
    int hstart_1 = mlvds_tcon->tcon_1st_hs_addr;
    int hend_1 = mlvds_tcon->tcon_1st_he_addr;
    int vstart_1 = mlvds_tcon->tcon_1st_vs_addr;
    int vend_1 = mlvds_tcon->tcon_1st_ve_addr;
    int hstart_2 = mlvds_tcon->tcon_2nd_hs_addr;
    int hend_2 = mlvds_tcon->tcon_2nd_he_addr;
    int vstart_2 = mlvds_tcon->tcon_2nd_vs_addr;
    int vend_2 = mlvds_tcon->tcon_2nd_ve_addr;

    switch(channel_num) {
        case 0 :
            WRITE_LCD_REG(MTCON0_1ST_HS_ADDR, hstart_1);
            WRITE_LCD_REG(MTCON0_1ST_HE_ADDR, hend_1);
            WRITE_LCD_REG(MTCON0_1ST_VS_ADDR, vstart_1);
            WRITE_LCD_REG(MTCON0_1ST_VE_ADDR, vend_1);
            WRITE_LCD_REG(MTCON0_2ND_HS_ADDR, hstart_2);
            WRITE_LCD_REG(MTCON0_2ND_HE_ADDR, hend_2);
            WRITE_LCD_REG(MTCON0_2ND_VS_ADDR, vstart_2);
            WRITE_LCD_REG(MTCON0_2ND_VE_ADDR, vend_2);
            WRITE_LCD_REG_BITS(L_TCON_MISC_SEL_ADDR, hv_sel, LCD_STH1_SEL, 1);
            break;
        case 1 :
            WRITE_LCD_REG(MTCON1_1ST_HS_ADDR, hstart_1);
            WRITE_LCD_REG(MTCON1_1ST_HE_ADDR, hend_1);
            WRITE_LCD_REG(MTCON1_1ST_VS_ADDR, vstart_1);
            WRITE_LCD_REG(MTCON1_1ST_VE_ADDR, vend_1);
            WRITE_LCD_REG(MTCON1_2ND_HS_ADDR, hstart_2);
            WRITE_LCD_REG(MTCON1_2ND_HE_ADDR, hend_2);
            WRITE_LCD_REG(MTCON1_2ND_VS_ADDR, vstart_2);
            WRITE_LCD_REG(MTCON1_2ND_VE_ADDR, vend_2);
            WRITE_LCD_REG_BITS(L_TCON_MISC_SEL_ADDR, hv_sel, LCD_CPV1_SEL, 1);
            break;
        case 2 :
            WRITE_LCD_REG(MTCON2_1ST_HS_ADDR, hstart_1);
            WRITE_LCD_REG(MTCON2_1ST_HE_ADDR, hend_1);
            WRITE_LCD_REG(MTCON2_1ST_VS_ADDR, vstart_1);
            WRITE_LCD_REG(MTCON2_1ST_VE_ADDR, vend_1);
            WRITE_LCD_REG(MTCON2_2ND_HS_ADDR, hstart_2);
            WRITE_LCD_REG(MTCON2_2ND_HE_ADDR, hend_2);
            WRITE_LCD_REG(MTCON2_2ND_VS_ADDR, vstart_2);
            WRITE_LCD_REG(MTCON2_2ND_VE_ADDR, vend_2);
            WRITE_LCD_REG_BITS(L_TCON_MISC_SEL_ADDR, hv_sel, LCD_STV1_SEL, 1);
            break;
        case 3 :
            WRITE_LCD_REG(MTCON3_1ST_HS_ADDR, hstart_1);
            WRITE_LCD_REG(MTCON3_1ST_HE_ADDR, hend_1);
            WRITE_LCD_REG(MTCON3_1ST_VS_ADDR, vstart_1);
            WRITE_LCD_REG(MTCON3_1ST_VE_ADDR, vend_1);
            WRITE_LCD_REG(MTCON3_2ND_HS_ADDR, hstart_2);
            WRITE_LCD_REG(MTCON3_2ND_HE_ADDR, hend_2);
            WRITE_LCD_REG(MTCON3_2ND_VS_ADDR, vstart_2);
            WRITE_LCD_REG(MTCON3_2ND_VE_ADDR, vend_2);
            WRITE_LCD_REG_BITS(L_TCON_MISC_SEL_ADDR, hv_sel, LCD_OEV1_SEL, 1);
            break;
        case 4 :
            WRITE_LCD_REG(MTCON4_1ST_HS_ADDR, hstart_1);
            WRITE_LCD_REG(MTCON4_1ST_HE_ADDR, hend_1);
            WRITE_LCD_REG(MTCON4_1ST_VS_ADDR, vstart_1);
            WRITE_LCD_REG(MTCON4_1ST_VE_ADDR, vend_1);
            WRITE_LCD_REG_BITS(L_TCON_MISC_SEL_ADDR, hv_sel, LCD_STH2_SEL, 1);
            break;
        case 5 :
            WRITE_LCD_REG(MTCON5_1ST_HS_ADDR, hstart_1);
            WRITE_LCD_REG(MTCON5_1ST_HE_ADDR, hend_1);
            WRITE_LCD_REG(MTCON5_1ST_VS_ADDR, vstart_1);
            WRITE_LCD_REG(MTCON5_1ST_VE_ADDR, vend_1);
            WRITE_LCD_REG_BITS(L_TCON_MISC_SEL_ADDR, hv_sel, LCD_CPV2_SEL, 1);
            break;
        case 6 :
            WRITE_LCD_REG(MTCON6_1ST_HS_ADDR, hstart_1);
            WRITE_LCD_REG(MTCON6_1ST_HE_ADDR, hend_1);
            WRITE_LCD_REG(MTCON6_1ST_VS_ADDR, vstart_1);
            WRITE_LCD_REG(MTCON6_1ST_VE_ADDR, vend_1);
            WRITE_LCD_REG_BITS(L_TCON_MISC_SEL_ADDR, hv_sel, LCD_OEH_SEL, 1);
            break;
        case 7 :
            WRITE_LCD_REG(MTCON7_1ST_HS_ADDR, hstart_1);
            WRITE_LCD_REG(MTCON7_1ST_HE_ADDR, hend_1);
            WRITE_LCD_REG(MTCON7_1ST_VS_ADDR, vstart_1);
            WRITE_LCD_REG(MTCON7_1ST_VE_ADDR, vend_1);
            WRITE_LCD_REG_BITS(L_TCON_MISC_SEL_ADDR, hv_sel, LCD_OEV3_SEL, 1);
            break;
        default:
            break;
    }
}

static void set_tcon_lvds(Lcd_Config_t *pConf)
{
	Lcd_Timing_t *tcon_adr = &(pConf->lcd_timing);
	unsigned hs_pol_adj, vs_pol_adj;

	lcd_print("%s\n", __FUNCTION__);
	
	set_gamma_table_lcd(((pConf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_EN) & 1));
	
	WRITE_LCD_REG(L_RGB_BASE_ADDR,  pConf->lcd_effect.rgb_base_addr);
	WRITE_LCD_REG(L_RGB_COEFF_ADDR, pConf->lcd_effect.rgb_coeff_addr);
	if (pConf->lcd_effect.dith_user) {
		WRITE_LCD_REG(L_DITH_CNTL_ADDR,  pConf->lcd_effect.dith_cntl_addr);
	}
	else {
		if(pConf->lcd_basic.lcd_bits == 8)
			WRITE_LCD_REG(L_DITH_CNTL_ADDR,  0x400);
		else
			WRITE_LCD_REG(L_DITH_CNTL_ADDR,  0x600);
	}
	
	WRITE_LCD_REG(L_POL_CNTL_ADDR,   (((pConf->lcd_timing.pol_ctrl >> POL_CTRL_CLK) & 1) << LCD_CPH1_POL));
	
	hs_pol_adj = (((pConf->lcd_timing.pol_ctrl >> POL_CTRL_HS) & 1) ? 0 : 1); //1 for low active, 0 for high active.
	vs_pol_adj = (((pConf->lcd_timing.pol_ctrl >> POL_CTRL_VS) & 1) ? 0 : 1); //1 for low active, 0 for high active
	WRITE_LCD_REG(L_POL_CNTL_ADDR, (READ_LCD_REG(L_POL_CNTL_ADDR) | ((0 << LCD_DE_POL) | (vs_pol_adj << LCD_VS_POL) | (hs_pol_adj << LCD_HS_POL)))); //adjust hvsync pol
	WRITE_LCD_REG(L_POL_CNTL_ADDR, (READ_LCD_REG(L_POL_CNTL_ADDR) | ((1 << LCD_TCON_DE_SEL) | (1 << LCD_TCON_VS_SEL) | (1 << LCD_TCON_HS_SEL)))); //enable tcon DE, Hsync, Vsync 
	
	//DE signal
	WRITE_LCD_REG(L_DE_HS_ADDR,		tcon_adr->de_hs_addr);
	WRITE_LCD_REG(L_DE_HE_ADDR,		tcon_adr->de_he_addr);
	WRITE_LCD_REG(L_DE_VS_ADDR,		tcon_adr->de_vs_addr);
	WRITE_LCD_REG(L_DE_VE_ADDR,		tcon_adr->de_ve_addr);
	
	//Hsync signal
	WRITE_LCD_REG(L_HSYNC_HS_ADDR,	tcon_adr->hs_hs_addr);
	WRITE_LCD_REG(L_HSYNC_HE_ADDR,	tcon_adr->hs_he_addr);
	WRITE_LCD_REG(L_HSYNC_VS_ADDR,	tcon_adr->hs_vs_addr);
	WRITE_LCD_REG(L_HSYNC_VE_ADDR,	tcon_adr->hs_ve_addr);
	
	//Vsync signal
	WRITE_LCD_REG(L_VSYNC_HS_ADDR,	tcon_adr->vs_hs_addr);
	WRITE_LCD_REG(L_VSYNC_HE_ADDR,	tcon_adr->vs_he_addr);
	WRITE_LCD_REG(L_VSYNC_VS_ADDR,	tcon_adr->vs_vs_addr);
	WRITE_LCD_REG(L_VSYNC_VE_ADDR,	tcon_adr->vs_ve_addr);

	if(pConf->lcd_misc_ctrl.vpp_sel)
		CLR_LCD_REG_MASK(VPP2_MISC, (VPP_OUT_SATURATE));
	else
		CLR_LCD_REG_MASK(VPP_MISC, (VPP_OUT_SATURATE));
}

static void set_tcon_ttl(Lcd_Config_t *pConf)
{
	Lcd_Timing_t *tcon_adr = &(pConf->lcd_timing);
	unsigned hs_pol_adj, vs_pol_adj;
	
	lcd_print("%s\n", __FUNCTION__);

	set_gamma_table_lcd(((pConf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_EN) & 1));

	WRITE_LCD_REG(RGB_BASE_ADDR,   pConf->lcd_effect.rgb_base_addr);
	WRITE_LCD_REG(RGB_COEFF_ADDR,  pConf->lcd_effect.rgb_coeff_addr);
	if (pConf->lcd_effect.dith_user) {
		WRITE_LCD_REG(DITH_CNTL_ADDR,  pConf->lcd_effect.dith_cntl_addr);
	}
	else {
		if(pConf->lcd_basic.lcd_bits == 8)
			WRITE_LCD_REG(DITH_CNTL_ADDR,  0x400);
		else
			WRITE_LCD_REG(DITH_CNTL_ADDR,  0x600);
	}
	
	WRITE_LCD_REG(POL_CNTL_ADDR,   (((pConf->lcd_timing.pol_ctrl >> POL_CTRL_CLK) & 1) << LCD_CPH1_POL));
	
	hs_pol_adj = (((pConf->lcd_timing.pol_ctrl >> POL_CTRL_HS) & 1) ? 0 : 1); //1 for low active, 0 for high active.
	vs_pol_adj = (((pConf->lcd_timing.pol_ctrl >> POL_CTRL_VS) & 1) ? 0 : 1); //1 for low active, 0 for high active

	//DE signal
	WRITE_LCD_REG(OEH_HS_ADDR,     tcon_adr->de_hs_addr);
	WRITE_LCD_REG(OEH_HE_ADDR,     tcon_adr->de_he_addr);
	WRITE_LCD_REG(OEH_VS_ADDR,     tcon_adr->de_vs_addr);
	WRITE_LCD_REG(OEH_VE_ADDR,     tcon_adr->de_ve_addr);
	
	//Hsync signal
	if (hs_pol_adj == 0) {
		WRITE_LCD_REG(STH1_HS_ADDR,    tcon_adr->hs_hs_addr);
		WRITE_LCD_REG(STH1_HE_ADDR,    tcon_adr->hs_he_addr);
	}
	else {
		WRITE_LCD_REG(STH1_HS_ADDR,    tcon_adr->hs_he_addr);
		WRITE_LCD_REG(STH1_HE_ADDR,    tcon_adr->hs_hs_addr);
	}
	WRITE_LCD_REG(STH1_VS_ADDR,    tcon_adr->hs_vs_addr);
	WRITE_LCD_REG(STH1_VE_ADDR,    tcon_adr->hs_ve_addr);

	//Vsync signal
	WRITE_LCD_REG(STV1_HS_ADDR,    tcon_adr->vs_hs_addr);
	WRITE_LCD_REG(STV1_HE_ADDR,    tcon_adr->vs_he_addr);
	if (vs_pol_adj == 0) {
		WRITE_LCD_REG(STV1_VS_ADDR,    tcon_adr->vs_vs_addr);
		WRITE_LCD_REG(STV1_VE_ADDR,    tcon_adr->vs_ve_addr);
	}
	else {
		WRITE_LCD_REG(STV1_VS_ADDR,    tcon_adr->vs_ve_addr);
		WRITE_LCD_REG(STV1_VE_ADDR,    tcon_adr->vs_vs_addr);	
	}
	
	WRITE_LCD_REG(INV_CNT_ADDR,       0);
	WRITE_LCD_REG(TCON_MISC_SEL_ADDR, ((1 << LCD_STV1_SEL) | (1 << LCD_STV2_SEL)));

	if(pConf->lcd_misc_ctrl.vpp_sel)
		CLR_LCD_REG_MASK(VPP2_MISC, (VPP_OUT_SATURATE));
	else
		CLR_LCD_REG_MASK(VPP_MISC, (VPP_OUT_SATURATE));
}

// Set the mlvds TCON
// this function should support dual gate or singal gate TCON setting.
// singal gate TCON, Scan Function TO DO.
// scan_function   // 0 - Z1, 1 - Z2, 2- Gong
static void set_tcon_mlvds(Lcd_Config_t *pConf)
{
	MLVDS_Tcon_Config_t *mlvds_tconfig_l = pConf->lcd_control.mlvds_tcon_config;
    int dual_gate = pConf->lcd_control.mlvds_config->test_dual_gate;
    int bit_num = pConf->lcd_basic.lcd_bits;
    int pair_num = pConf->lcd_control.mlvds_config->test_pair_num;

    unsigned int data32;

    int pclk_div;
    int ext_pixel = dual_gate ? pConf->lcd_control.mlvds_config->total_line_clk : 0;
    int dual_wr_rd_start;
    int i = 0;
	
	lcd_print("%s.\n", __FUNCTION__);

	set_gamma_table_lcd(((pConf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_EN) & 1));

    WRITE_LCD_REG(L_RGB_BASE_ADDR, pConf->lcd_effect.rgb_base_addr);
    WRITE_LCD_REG(L_RGB_COEFF_ADDR, pConf->lcd_effect.rgb_coeff_addr);
	if (pConf->lcd_effect.dith_user) {
		WRITE_LCD_REG(L_DITH_CNTL_ADDR,  pConf->lcd_effect.dith_cntl_addr);
	}	
	else {
		if(pConf->lcd_basic.lcd_bits == 8)
			WRITE_LCD_REG(L_DITH_CNTL_ADDR,  0x400);
		else
			WRITE_LCD_REG(L_DITH_CNTL_ADDR,  0x600);
	}
    //WRITE_LCD_REG(L_POL_CNTL_ADDR, pConf->pol_cntl_addr);
//    WRITE_LCD_REG(L_INV_CNT_ADDR, pConf->inv_cnt_addr);
//    WRITE_LCD_REG(L_TCON_MISC_SEL_ADDR, pConf->tcon_misc_sel_addr);
//    WRITE_LCD_REG(L_DUAL_PORT_CNTL_ADDR, pConf->dual_port_cntl_addr);
//
    data32 = (0x9867 << tcon_pattern_loop_data) |
             (1 << tcon_pattern_loop_start) |
             (4 << tcon_pattern_loop_end) |
             (1 << ((mlvds_tconfig_l[6].channel_num)+tcon_pattern_enable)); // POL_CHANNEL use pattern generate

    WRITE_LCD_REG(L_TCON_PATTERN_HI,  (data32 >> 16));
    WRITE_LCD_REG(L_TCON_PATTERN_LO, (data32 & 0xffff));

    pclk_div = (bit_num == 8) ? 3 : // phy_clk / 8
                                2 ; // phy_clk / 6
   data32 = (1 << ((mlvds_tconfig_l[7].channel_num)-2+tcon_pclk_enable)) |  // enable PCLK_CHANNEL
            (pclk_div << tcon_pclk_div) |
            (
              (pair_num == 6) ?
              (
              ((bit_num == 8) & dual_gate) ?
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              ) :
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              )
              ) :
              (
              ((bit_num == 8) & dual_gate) ?
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              ) :
              (bit_num == 8) ?
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              ) :
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              )
              )
            );

    WRITE_LCD_REG(TCON_CONTROL_HI,  (data32 >> 16));
    WRITE_LCD_REG(TCON_CONTROL_LO, (data32 & 0xffff));

    WRITE_LCD_REG(L_TCON_DOUBLE_CTL,
                   (1<<(mlvds_tconfig_l[3].channel_num))   // invert CPV
                  );

	// for channel 4-7, set second setting same as first
    WRITE_LCD_REG(L_DE_HS_ADDR, (0x3 << 14) | ext_pixel);   // 0x3 -- enable double_tcon fir channel7:6
    WRITE_LCD_REG(L_DE_HE_ADDR, (0x3 << 14) | ext_pixel);   // 0x3 -- enable double_tcon fir channel5:4
    WRITE_LCD_REG(L_DE_VS_ADDR, (0x3 << 14) | 0);	// 0x3 -- enable double_tcon fir channel3:2
    WRITE_LCD_REG(L_DE_VE_ADDR, (0x3 << 14) | 0);	// 0x3 -- enable double_tcon fir channel1:0	

    dual_wr_rd_start = 0x5d;
    WRITE_LCD_REG(MLVDS_DUAL_GATE_WR_START, dual_wr_rd_start);
    WRITE_LCD_REG(MLVDS_DUAL_GATE_WR_END, dual_wr_rd_start + 1280);
    WRITE_LCD_REG(MLVDS_DUAL_GATE_RD_START, dual_wr_rd_start + ext_pixel - 2);
    WRITE_LCD_REG(MLVDS_DUAL_GATE_RD_END, dual_wr_rd_start + 1280 + ext_pixel - 2);

    WRITE_LCD_REG(MLVDS_SECOND_RESET_CTL, (pConf->lcd_control.mlvds_config->mlvds_insert_start + ext_pixel));

    data32 = (0 << ((mlvds_tconfig_l[5].channel_num)+mlvds_tcon_field_en)) |  // enable EVEN_F on TCON channel 6
             ( (0x0 << mlvds_scan_mode_odd) | (0x0 << mlvds_scan_mode_even)
             ) | (0 << mlvds_scan_mode_start_line);

	WRITE_LCD_REG(MLVDS_DUAL_GATE_CTL_HI,  (data32 >> 16));
	WRITE_LCD_REG(MLVDS_DUAL_GATE_CTL_LO, (data32 & 0xffff));

	lcd_print("write minilvds tcon 0~7.\n");
	for(i = 0; i < 8; i++) {
		write_tcon_double(&mlvds_tconfig_l[i]);
	}
/*	
	if(pConf->lcd_misc_ctrl.vpp_sel)
		CLR_LCD_REG_MASK(VPP2_MISC, (VPP_OUT_SATURATE));
	else
		CLR_LCD_REG_MASK(VPP_MISC, (VPP_OUT_SATURATE));
*/
}

static void set_lcd_spread_spectrum(int ss_level)
{
	unsigned pll_ctrl2, pll_ctrl3, pll_ctrl4;
	lcd_print("%s.\n", __FUNCTION__);
	
	switch (ss_level) {
		case 1:  //about 0.5%
			pll_ctrl2 = 0x16110696;
			pll_ctrl3 = 0x6d625012;
			pll_ctrl4 = 0x130;
			break;
		case 2:  //about 1%
			pll_ctrl2 = 0x16110696;
			pll_ctrl3 = 0x4d625012;
			pll_ctrl4 = 0x130;
			break;
		case 3:  //about 2%
			pll_ctrl2 = 0x16110696;
			pll_ctrl3 = 0x2d425012;
			pll_ctrl4 = 0x130;
			break;
		case 4:  //about 3%
			pll_ctrl2 = 0x16110696;
			pll_ctrl3 = 0x1d425012;
			pll_ctrl4 = 0x130;
			break;
		case 5:  //about 4%
			pll_ctrl2 = 0x16110696;
			pll_ctrl3 = 0x0d125012;
			pll_ctrl4 = 0x130;
			break;
		case 6:  //about 5%
			pll_ctrl2 = 0x16110696;
			pll_ctrl3 = 0x0e425012;
			pll_ctrl4 = 0x130;
			break;
		case 0:	//disable ss
		default:
			pll_ctrl2 = 0x814d3928;
			pll_ctrl3 = 0x6b425012;
			pll_ctrl4 = 0x110;
			break;
	}

	WRITE_LCD_CBUS_REG(HHI_VIID_PLL_CNTL2, pll_ctrl2);
	WRITE_LCD_CBUS_REG(HHI_VIID_PLL_CNTL3, pll_ctrl3);
	WRITE_LCD_CBUS_REG(HHI_VIID_PLL_CNTL4, pll_ctrl4);
}

static void vclk_set_lcd(int lcd_type, unsigned long pll_reg, unsigned long vid_div_reg, unsigned int clk_ctrl_reg)
{
	unsigned xd = 0;
	int wait_loop = PLL_WAIT_LOCK_CNT;
	unsigned pll_lock = 0;
	unsigned long flags = 0;
	spin_lock_irqsave(&lcd_clk_lock, flags);
	
	lcd_print("%s.\n", __FUNCTION__);

	vid_div_reg = ((vid_div_reg & 0x1ffff) | (1 << 16) | (1 << 15) | (0x3 << 0));	//select vid2_pll and enable clk
	xd = (clk_ctrl_reg >> CLK_CTRL_XD) & 0xff;
	
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_CNTL, 0, 19, 1);	//disable vclk2_en 
	udelay(2);

	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_PLL_CNTL, 1, 29, 1);	//reset pll
	WRITE_LCD_CBUS_REG(HHI_VIID_PLL_CNTL, pll_reg|(1<<PLL_CTRL_RST));
	WRITE_LCD_CBUS_REG(HHI_VIID_PLL_CNTL2, 0x814d3928 );
	WRITE_LCD_CBUS_REG(HHI_VIID_PLL_CNTL3, 0x6b425012 );
	WRITE_LCD_CBUS_REG(HHI_VIID_PLL_CNTL4, 0x110 );
	WRITE_LCD_CBUS_REG(HHI_VIID_PLL_CNTL, pll_reg );
	do{
		udelay(50);
		pll_lock = (READ_LCD_CBUS_REG(HHI_VIID_PLL_CNTL) >> PLL_CTRL_LOCK) & 0x1;
		wait_loop--;
	}while((pll_lock == 0) && (wait_loop > 0));
	if (wait_loop == 0)
		printk("[error]: vid2_pll lock failed\n");

	//pll_div2
	WRITE_LCD_CBUS_REG(HHI_VIID_DIVIDER_CNTL, vid_div_reg);
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 1, 7, 1);    //0x104c[7]:SOFT_RESET_POST
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 1, 3, 1);    //0x104c[3]:SOFT_RESET_PRE
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 0, 1, 1);    //0x104c[1]:RESET_N_POST
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 0, 0, 1);    //0x104c[0]:RESET_N_PRE
	udelay(5);
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 0, 3, 1);
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 0, 7, 1);
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 3, 0, 2);
	udelay(5);

	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_DIV, (xd-1), 0, 8);	// setup the XD divider value
	udelay(5);
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_CNTL, 4, 16, 3);	// Bit[18:16] - v2_cntl_clk_in_sel
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_CNTL, 1, 19, 1);	//vclk2_en0
	udelay(2);

	if(lcd_type == LCD_DIGITAL_TTL)
		WRITE_LCD_CBUS_REG_BITS(HHI_VID_CLK_DIV, 8, 20, 4); // [23:20] enct_clk_sel, select v2_clk_div1
	else
		WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_DIV, 8, 12, 4); // [23:20] encl_clk_sel, select v2_clk_div1

	//WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_DIV, 1, 16, 2); // release vclk2_div_reset and enable vclk2_div ??
	udelay(5);

	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_CNTL, 1, 0, 1);	//enable v2_clk_div1
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_CNTL, 1, 15, 1);  //soft reset
	udelay(10);
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_CNTL, 0, 15, 1);  //release soft reset
	udelay(5);
	
	spin_unlock_irqrestore(&lcd_clk_lock, flags);
}

static void clk_util_lvds_set_clk_div(unsigned long divn_sel, unsigned long divn_tcnt, unsigned long div2_en)
{
    // assign          lvds_div_phy_clk_en     = tst_lvds_tmode ? 1'b1         : phy_clk_cntl[10];
    // assign          lvds_div_div2_sel       = tst_lvds_tmode ? atest_i[5]   : phy_clk_cntl[9];
    // assign          lvds_div_sel            = tst_lvds_tmode ? atest_i[7:6] : phy_clk_cntl[8:7];
    // assign          lvds_div_tcnt           = tst_lvds_tmode ? 3'd6         : phy_clk_cntl[6:4];
    // If dividing by 1, just select the divide by 1 path
	if( divn_tcnt == 1 )
		divn_sel = 0;

	WRITE_LCD_REG_BITS(LVDS_PHY_CLK_CNTL, 1, 10, 1);
	WRITE_LCD_REG_BITS(LVDS_PHY_CLK_CNTL, divn_sel, 7, 2);
	WRITE_LCD_REG_BITS(LVDS_PHY_CLK_CNTL, div2_en, 9, 1);
	WRITE_LCD_REG_BITS(LVDS_PHY_CLK_CNTL, ((divn_tcnt-1)&0x7), 4, 3);
}

static void set_pll_lcd(Lcd_Config_t *pConf)
{
    unsigned pll_reg, div_reg, clk_reg;
    int xd;
    int lcd_type, ss_level;
    unsigned pll_div_post = 0, phy_clk_div2 = 0;

    lcd_print("%s\n", __FUNCTION__);

    pll_reg = pConf->lcd_timing.pll_ctrl;
    div_reg = pConf->lcd_timing.div_ctrl;
    clk_reg = pConf->lcd_timing.clk_ctrl;
    ss_level = (clk_reg >> CLK_CTRL_SS) & 0xf;
    xd = (clk_reg >> CLK_CTRL_XD) & 0xff;

    lcd_type = pConf->lcd_basic.lcd_type;

    switch(lcd_type){
        case LCD_DIGITAL_LVDS:
            xd = 1;
            pll_div_post = 7;
            phy_clk_div2 = 0;
            div_reg = (div_reg | (1 << DIV_CTRL_POST_SEL) | (1 << DIV_CTRL_LVDS_CLK_EN) | ((pll_div_post-1) << DIV_CTRL_DIV_POST) | (phy_clk_div2 << DIV_CTRL_PHY_CLK_DIV2));
            break;
        case LCD_DIGITAL_TTL:
            break;
        default:
            break;
    }
    clk_reg = (pConf->lcd_timing.clk_ctrl & ~(0xff << CLK_CTRL_XD)) | (xd << CLK_CTRL_XD);

    vclk_set_lcd(lcd_type, pll_reg, div_reg, clk_reg);
    set_lcd_spread_spectrum(ss_level);

    switch(lcd_type){
        case LCD_DIGITAL_LVDS:
            clk_util_lvds_set_clk_div(1, pll_div_post, phy_clk_div2);
            //    lvds_gen_cntl       <= {10'h0,      // [15:4] unused
            //                            2'h1,       // [5:4] divide by 7 in the PHY
            //                            1'b0,       // [3] fifo_en
            //                            1'b0,       // [2] wr_bist_gate
            //                            2'b00};     // [1:0] fifo_wr mode
            //FIFO_CLK_SEL = 1; // div7
            WRITE_LCD_REG_BITS(LVDS_GEN_CNTL, 1, 4, 2);	//lvds fifo clk div 7

            WRITE_LCD_REG_BITS(LVDS_PHY_CLK_CNTL, 0, 15, 1);	// lvds div reset
            udelay(5);
            WRITE_LCD_REG_BITS(LVDS_PHY_CLK_CNTL, 1, 15, 1);	// Release lvds div reset
            break;
        case LCD_DIGITAL_TTL:
            break;
        default:
            break;
    }
}

static void set_pll_mlvds(Lcd_Config_t *pConf)
{
    int test_bit_num = pConf->lcd_basic.lcd_bits;
    int test_dual_gate = pConf->lcd_control.mlvds_config->test_dual_gate;
    int test_pair_num= pConf->lcd_control.mlvds_config->test_pair_num;
	
    int pll_div_post, phy_clk_div2, FIFO_CLK_SEL, MPCLK_DELAY, MCLK_half, MCLK_half_delay;
    unsigned int data32;
    unsigned long mclk_pattern_dual_6_6;
    int test_high_phase = (test_bit_num != 8) | test_dual_gate;
    unsigned long rd_data;

    unsigned pll_reg, div_reg, clk_reg;
    int xd;
	int lcd_type, ss_level;
	
	lcd_print("%s\n", __FUNCTION__);
	
    pll_reg = pConf->lcd_timing.pll_ctrl;
    div_reg = pConf->lcd_timing.div_ctrl;
	clk_reg = pConf->lcd_timing.clk_ctrl;
	ss_level = (clk_reg >> CLK_CTRL_SS) & 0xf;
	xd = 1;
	
	lcd_type = pConf->lcd_basic.lcd_type;

	switch(pConf->lcd_control.mlvds_config->TL080_phase) {
		case 0 :
			mclk_pattern_dual_6_6 = 0xc3c3c3;
			MCLK_half = 1;
			break;
		case 1 :
			mclk_pattern_dual_6_6 = 0xc3c3c3;
			MCLK_half = 0;
			break;
		case 2 :
			mclk_pattern_dual_6_6 = 0x878787;
			MCLK_half = 1;
			break;
		case 3 :
			mclk_pattern_dual_6_6 = 0x878787;
			MCLK_half = 0;
			break;
		case 4 :
			mclk_pattern_dual_6_6 = 0x3c3c3c;
			MCLK_half = 1;
			break;
		case 5 :
			mclk_pattern_dual_6_6 = 0x3c3c3c;
			MCLK_half = 0;
			break;
		case 6 :
			mclk_pattern_dual_6_6 = 0x787878;
			MCLK_half = 1;
			break;
		default : // case 7
			mclk_pattern_dual_6_6 = 0x787878;
			MCLK_half = 0;
			break;
	}

	pll_div_post = (test_bit_num == 8) ?
					(test_dual_gate ? 4 : 8) :
					(test_dual_gate ? 3 : 6);

    phy_clk_div2 = (test_pair_num != 3);
	
	div_reg = (div_reg | (1 << DIV_CTRL_POST_SEL) | (1 << DIV_CTRL_LVDS_CLK_EN) | ((pll_div_post-1) << DIV_CTRL_DIV_POST) | (phy_clk_div2 << DIV_CTRL_PHY_CLK_DIV2));
	clk_reg = (pConf->lcd_timing.clk_ctrl & ~(0xff << CLK_CTRL_XD)) | (xd << CLK_CTRL_XD);
	lcd_print("ss_level=%u(%s), pll_reg=0x%x, div_reg=0x%x, xd=%d.\n", ss_level, lcd_ss_level_table[ss_level], pll_reg, div_reg, xd);
	vclk_set_lcd(lcd_type, pll_reg, div_reg, clk_reg);
	set_lcd_spread_spectrum(ss_level);
	
	clk_util_lvds_set_clk_div(1, pll_div_post, phy_clk_div2);
	
	//enable v2_clk div
    // WRITE_LCD_CBUS_REG(HHI_VIID_CLK_CNTL, READ_LCD_CBUS_REG(HHI_VIID_CLK_CNTL) | (0xF << 0) );
    // WRITE_LCD_CBUS_REG(HHI_VID_CLK_CNTL, READ_LCD_CBUS_REG(HHI_VID_CLK_CNTL) | (0xF << 0) );

    WRITE_LCD_REG(LVDS_PHY_CNTL0, 0xffff );

    //    lvds_gen_cntl       <= {10'h0,      // [15:4] unused
    //                            2'h1,       // [5:4] divide by 7 in the PHY
    //                            1'b0,       // [3] fifo_en
    //                            1'b0,       // [2] wr_bist_gate
    //                            2'b00};     // [1:0] fifo_wr mode

    FIFO_CLK_SEL = (test_bit_num == 8) ? 2 : // div8
                                    0 ; // div6
    rd_data = READ_LCD_REG(LVDS_GEN_CNTL);
    rd_data = (rd_data & 0xffcf) | (FIFO_CLK_SEL<< 4);
    WRITE_LCD_REG(LVDS_GEN_CNTL, rd_data);

    MPCLK_DELAY = (test_pair_num == 6) ?
                  ((test_bit_num == 8) ? (test_dual_gate ? 5 : 3) : 2) :
                  ((test_bit_num == 8) ? 3 : 3) ;

	MCLK_half_delay = pConf->lcd_control.mlvds_config->phase_select ? MCLK_half :
																			(test_dual_gate & (test_bit_num == 8) & (test_pair_num != 6));

    if(test_high_phase)
    {
        if(test_dual_gate)
        data32 = (MPCLK_DELAY << mpclk_dly) |
                 (((test_bit_num == 8) ? 3 : 2) << mpclk_div) |
                 (1 << use_mpclk) |
                 (MCLK_half_delay << mlvds_clk_half_delay) |
                 (((test_bit_num == 8) ? (
                                           (test_pair_num == 6) ? 0x999999 : // DIV4
                                                                  0x555555   // DIV2
                                         ) :
                                         (
                                           (test_pair_num == 6) ? mclk_pattern_dual_6_6 : //DIV8
                                                                  0x999999   // DIV4
                                         )
                 ) << mlvds_clk_pattern);
        else if(test_bit_num == 8)
            data32 = (MPCLK_DELAY << mpclk_dly) |
                     (((test_bit_num == 8) ? 3 : 2) << mpclk_div) |
                     (1 << use_mpclk) |
                     (0 << mlvds_clk_half_delay) |
                     (0xc3c3c3 << mlvds_clk_pattern);      // DIV 8
        else
            data32 = (MPCLK_DELAY << mpclk_dly) |
                     (((test_bit_num == 8) ? 3 : 2) << mpclk_div) |
                     (1 << use_mpclk) |
                     (0 << mlvds_clk_half_delay) |
                     (
                       (
                         (test_pair_num == 6) ? 0xc3c3c3 : // DIV8
                                                0x999999   // DIV4
                       ) << mlvds_clk_pattern
                     );
    }
    else {
        if(test_pair_num == 6) {
            data32 = (MPCLK_DELAY << mpclk_dly) |
                     (((test_bit_num == 8) ? 3 : 2) << mpclk_div) |
                     (1 << use_mpclk) |
                     (0 << mlvds_clk_half_delay) |
                     (
                       (
                         (test_pair_num == 6) ? 0x999999 : // DIV4
                                                0x555555   // DIV2
                       ) << mlvds_clk_pattern
                     );
        }
        else {
            data32 = (1 << mlvds_clk_half_delay) |
                   (0x555555 << mlvds_clk_pattern);      // DIV 2
        }
    }

    WRITE_LCD_REG(MLVDS_CLK_CTL_HI, (data32 >> 16));
    WRITE_LCD_REG(MLVDS_CLK_CTL_LO, (data32 & 0xffff));

	//pll2_div_sel
	// Set Soft Reset vid_pll_div_pre
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 1, 3, 1);
	// Set Hard Reset vid_pll_div_post
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 0, 1, 1);
	// Set Hard Reset lvds_phy_ser_top
	WRITE_LCD_REG_BITS(LVDS_PHY_CLK_CNTL, 0, 15, 1);
	// Release Hard Reset lvds_phy_ser_top
	WRITE_LCD_REG_BITS(LVDS_PHY_CLK_CNTL, 1, 15, 1);
	// Release Hard Reset vid_pll_div_post
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 1, 1, 1);
	// Release Soft Reset vid_pll_div_pre
	WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 0, 3, 1);
}

static void set_venc_ttl(Lcd_Config_t *pConf)
{
    lcd_print("%s\n", __FUNCTION__);
    WRITE_LCD_REG(ENCT_VIDEO_EN,		0);
#ifdef CONFIG_AM_TV_OUTPUT2
    if(pConf->lcd_misc_ctrl.vpp_sel)
        WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 3, 2, 2);
    else
        WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 3, 0, 2);//viu1 select enct
#else
    WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 0xf, 0, 4);;	//viu1, viu2 select encl
#endif
    WRITE_LCD_REG(ENCT_VIDEO_MODE,        0);
    WRITE_LCD_REG(ENCT_VIDEO_MODE_ADV,    0x0008);

    WRITE_LCD_REG(ENCT_VIDEO_FILT_CTRL,    0x1000);  // bypass filter

    WRITE_LCD_REG(ENCT_VIDEO_MAX_PXCNT,    pConf->lcd_basic.h_period - 1);
    WRITE_LCD_REG(ENCT_VIDEO_MAX_LNCNT,    pConf->lcd_basic.v_period - 1);

    WRITE_LCD_REG(ENCT_VIDEO_HAVON_BEGIN,  pConf->lcd_timing.video_on_pixel);
    WRITE_LCD_REG(ENCT_VIDEO_HAVON_END,    pConf->lcd_basic.h_active - 1 + pConf->lcd_timing.video_on_pixel);
    WRITE_LCD_REG(ENCT_VIDEO_VAVON_BLINE,  pConf->lcd_timing.video_on_line);
    WRITE_LCD_REG(ENCT_VIDEO_VAVON_ELINE,  pConf->lcd_basic.v_active - 1 + pConf->lcd_timing.video_on_line);

    WRITE_LCD_REG(ENCT_VIDEO_HSO_BEGIN,    15);
    WRITE_LCD_REG(ENCT_VIDEO_HSO_END,      31);
    WRITE_LCD_REG(ENCT_VIDEO_VSO_BEGIN,    15);
    WRITE_LCD_REG(ENCT_VIDEO_VSO_END,      31);
    WRITE_LCD_REG(ENCT_VIDEO_VSO_BLINE,    0);
    WRITE_LCD_REG(ENCT_VIDEO_VSO_ELINE,    2);

    WRITE_LCD_REG(ENCT_VIDEO_RGBIN_CTRL, 	(1 << 0));//(1 << 1) | (1 << 0));	//bit[0] 1:RGB, 0:YUV

    WRITE_LCD_REG(ENCT_VIDEO_EN,           1);  // enable enct
}

static void set_venc_lvds(Lcd_Config_t *pConf)
{
	lcd_print("%s\n",__FUNCTION__);

	WRITE_LCD_REG(ENCL_VIDEO_EN, 0);
#ifdef CONFIG_AM_TV_OUTPUT2
	if	(pConf->lcd_misc_ctrl.vpp_sel) {
		WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 0, 2, 2); //viu2 select encl
	}
	else {
		WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 0, 0, 2);//viu1 select encl
	}
#else
	WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 0, 0, 4);; //viu1, viu2 select encl
#endif
	
	WRITE_LCD_REG(ENCL_VIDEO_MODE,        0);
	WRITE_LCD_REG(ENCL_VIDEO_MODE_ADV,    0x8); // Sampling rate: 1

	WRITE_LCD_REG(ENCL_VIDEO_FILT_CTRL,   0x1000); // bypass filter

	WRITE_LCD_REG(ENCL_VIDEO_MAX_PXCNT,   pConf->lcd_basic.h_period - 1);
	WRITE_LCD_REG(ENCL_VIDEO_MAX_LNCNT,   pConf->lcd_basic.v_period - 1);

	WRITE_LCD_REG(ENCL_VIDEO_HAVON_BEGIN, pConf->lcd_timing.video_on_pixel);
	WRITE_LCD_REG(ENCL_VIDEO_HAVON_END,   pConf->lcd_basic.h_active - 1 + pConf->lcd_timing.video_on_pixel);
	WRITE_LCD_REG(ENCL_VIDEO_VAVON_BLINE, pConf->lcd_timing.video_on_line);
	WRITE_LCD_REG(ENCL_VIDEO_VAVON_ELINE, pConf->lcd_basic.v_active - 1  + pConf->lcd_timing.video_on_line);

	WRITE_LCD_REG(ENCL_VIDEO_HSO_BEGIN,   10);//pConf->lcd_timing.hs_hs_addr);
	WRITE_LCD_REG(ENCL_VIDEO_HSO_END,     16);//pConf->lcd_timing.hs_he_addr);
	WRITE_LCD_REG(ENCL_VIDEO_VSO_BEGIN,   pConf->lcd_timing.vso_hstart);
	WRITE_LCD_REG(ENCL_VIDEO_VSO_END,     pConf->lcd_timing.vso_hstart);
	WRITE_LCD_REG(ENCL_VIDEO_VSO_BLINE,   pConf->lcd_timing.vso_vstart);
	WRITE_LCD_REG(ENCL_VIDEO_VSO_ELINE,   pConf->lcd_timing.vso_vstart + 2);

	WRITE_LCD_REG(ENCL_VIDEO_RGBIN_CTRL,  (1 << 0));//(1 << 1) | (1 << 0));	//bit[0] 1:RGB, 0:YUV

	WRITE_LCD_REG(ENCL_VIDEO_EN,          1);	// enable encl
}

static void set_venc_mlvds(Lcd_Config_t *pConf)
{
    int ext_pixel = pConf->lcd_control.mlvds_config->test_dual_gate ? pConf->lcd_control.mlvds_config->total_line_clk : 0;
	int active_h_start = pConf->lcd_timing.video_on_pixel;
	int active_v_start = pConf->lcd_timing.video_on_line;
	int width = pConf->lcd_basic.h_active;
	int height = pConf->lcd_basic.v_active;
	int max_height = pConf->lcd_basic.v_period;
	
	lcd_print("%s\n", __FUNCTION__);

    WRITE_LCD_REG(ENCL_VIDEO_EN,           0);

#ifdef CONFIG_AM_TV_OUTPUT2
    if(pConf->lcd_misc_ctrl.vpp_sel){
        WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 0, 2, 2);	//viu2 select encl
    }
    else{
		WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 0, 0, 2);//viu1 select encl
    }
#else
    WRITE_LCD_REG_BITS(VPU_VIU_VENC_MUX_CTRL, 0, 0, 4);;	//viu1, viu2 select encl
#endif	

	WRITE_LCD_REG(ENCL_VIDEO_MODE,             0x0040 | (1<<14)); // Enable Hsync and equalization pulse switch in center; bit[14] cfg_de_v = 1
	WRITE_LCD_REG(ENCL_VIDEO_MODE_ADV,         0x0008); // Sampling rate: 1
	
	// bypass filter
 	WRITE_LCD_REG(ENCL_VIDEO_FILT_CTRL,			0x1000);
	
	WRITE_LCD_REG(ENCL_VIDEO_YFP1_HTIME,       active_h_start);
	WRITE_LCD_REG(ENCL_VIDEO_YFP2_HTIME,       active_h_start + width);

	WRITE_LCD_REG(ENCL_VIDEO_MAX_PXCNT,        pConf->lcd_control.mlvds_config->total_line_clk - 1 + ext_pixel);
	WRITE_LCD_REG(ENCL_VIDEO_MAX_LNCNT,        max_height - 1);

	WRITE_LCD_REG(ENCL_VIDEO_HAVON_BEGIN,      active_h_start);
	WRITE_LCD_REG(ENCL_VIDEO_HAVON_END,        active_h_start + width - 1);  // for dual_gate mode still read 1408 pixel at first half of line
	WRITE_LCD_REG(ENCL_VIDEO_VAVON_BLINE,      active_v_start);
	WRITE_LCD_REG(ENCL_VIDEO_VAVON_ELINE,      active_v_start + height -1);  //15+768-1);

	WRITE_LCD_REG(ENCL_VIDEO_HSO_BEGIN,        24);
	WRITE_LCD_REG(ENCL_VIDEO_HSO_END,          1420 + ext_pixel);
	WRITE_LCD_REG(ENCL_VIDEO_VSO_BEGIN,        1400 + ext_pixel);
	WRITE_LCD_REG(ENCL_VIDEO_VSO_END,          1410 + ext_pixel);
	WRITE_LCD_REG(ENCL_VIDEO_VSO_BLINE,        1);
	WRITE_LCD_REG(ENCL_VIDEO_VSO_ELINE,        3);

	WRITE_LCD_REG(ENCL_VIDEO_RGBIN_CTRL, 	(1 << 0));//(1 << 1) | (1 << 0));	//bit[0] 1:RGB, 0:YUV

	// enable encl
    WRITE_LCD_REG(ENCL_VIDEO_EN,		1);
}

static void set_control_lvds(Lcd_Config_t *pConf)
{
	unsigned lvds_repack, pn_swap, bit_num;
	unsigned data32;
	
	lcd_print("%s\n", __FUNCTION__);

	WRITE_LCD_REG_BITS(LVDS_GEN_CNTL, 0, 3, 1); // disable lvds fifo
	
    data32 = (0x00 << LVDS_blank_data_r) |
             (0x00 << LVDS_blank_data_g) |
             (0x00 << LVDS_blank_data_b) ; 
    WRITE_LCD_REG(LVDS_BLANK_DATA_HI, (data32 >> 16));
    WRITE_LCD_REG(LVDS_BLANK_DATA_LO, (data32 & 0xffff));
	
	lvds_repack = (pConf->lcd_control.lvds_config->lvds_repack) & 0x1;
	pn_swap = (pConf->lcd_control.lvds_config->pn_swap) & 0x1;

	switch(pConf->lcd_basic.lcd_bits) {
		case 10:
			bit_num=0;
			break;
		case 8:
			bit_num=1;
			break;
		case 6:
			bit_num=2;
			break;
		case 4:
			bit_num=3;
			break;
		default:
			bit_num=1;
			break;
	}
	
	WRITE_LCD_REG_BITS(MLVDS_CONTROL, 0, 0, 1);  //disable mlvds

	WRITE_LCD_REG(LVDS_PACK_CNTL_ADDR, 
					( lvds_repack<<0 ) | // repack
					( 0<<2 ) | // odd_even
					( 0<<3 ) | // reserve
					( 0<<4 ) | // lsb first
					( pn_swap<<5 ) | // pn swap
					( 0<<6 ) | // dual port
					( 0<<7 ) | // use tcon control
					( bit_num<<8 ) | // 0:10bits, 1:8bits, 2:6bits, 3:4bits.
					( 0<<10 ) | //r_select  //0:R, 1:G, 2:B, 3:0
					( 1<<12 ) | //g_select  //0:R, 1:G, 2:B, 3:0
					( 2<<14 ));  //b_select  //0:R, 1:G, 2:B, 3:0; 
				   
    WRITE_LCD_REG_BITS(LVDS_GEN_CNTL, 1, 0, 1);  //fifo enable
	//WRITE_LCD_REG_BITS(LVDS_GEN_CNTL, 1, 3, 1);  //enable fifo
}

static void set_control_ttl(Lcd_Config_t *pConf)
{
	unsigned rb_port_swap, rgb_bit_swap;
	
	rb_port_swap = (unsigned)(pConf->lcd_control.ttl_config->rb_swap);
	rgb_bit_swap = (unsigned)(pConf->lcd_control.ttl_config->bit_swap);
	
	WRITE_LCD_REG(DUAL_PORT_CNTL_ADDR, (rb_port_swap << LCD_RGB_SWP) | (rgb_bit_swap << LCD_BIT_SWP));
}

static void set_control_mlvds(Lcd_Config_t *pConf)
{
	int test_bit_num = pConf->lcd_basic.lcd_bits;
    int test_pair_num = pConf->lcd_control.mlvds_config->test_pair_num;
    int test_dual_gate = pConf->lcd_control.mlvds_config->test_dual_gate;
    int scan_function = pConf->lcd_control.mlvds_config->scan_function;     //0:U->D,L->R  //1:D->U,R->L
    int mlvds_insert_start;
    unsigned int reset_offset;
    unsigned int reset_length;

    unsigned long data32;
	
	lcd_print("%s\n", __FUNCTION__);
    
    mlvds_insert_start = test_dual_gate ?
                           ((test_bit_num == 8) ? ((test_pair_num == 6) ? 0x9f : 0xa9) :
                                                  ((test_pair_num == 6) ? pConf->lcd_control.mlvds_config->mlvds_insert_start : 0xa7)
                           ) :
                           (
                             (test_pair_num == 6) ? ((test_bit_num == 8) ? 0xa9 : 0xa7) :
                                                    ((test_bit_num == 8) ? 0xae : 0xad)
                           );

    // Enable the LVDS PHY (power down bits)
	WRITE_LCD_REG_BITS(LVDS_PHY_CNTL1, 0x7f, 8, 7);

    data32 = (0x00 << LVDS_blank_data_r) |
             (0x00 << LVDS_blank_data_g) |
             (0x00 << LVDS_blank_data_b) ;
    WRITE_LCD_REG(LVDS_BLANK_DATA_HI,  (data32 >> 16));
    WRITE_LCD_REG(LVDS_BLANK_DATA_LO, (data32 & 0xffff));

    data32 = 0x7fffffff; //  '0'x1 + '1'x32 + '0'x2
    WRITE_LCD_REG(MLVDS_RESET_PATTERN_HI,  (data32 >> 16));
    WRITE_LCD_REG(MLVDS_RESET_PATTERN_LO, (data32 & 0xffff));
    data32 = 0x8000; // '0'x1 + '1'x32 + '0'x2
    WRITE_LCD_REG(MLVDS_RESET_PATTERN_EXT,  (data32 & 0xffff));

    reset_length = 1+32+2;
    reset_offset = test_bit_num - (reset_length%test_bit_num);

    data32 = (reset_offset << mLVDS_reset_offset) |
             (reset_length << mLVDS_reset_length) |
             ((test_pair_num == 6) << mLVDS_data_write_toggle) |
             ((test_pair_num != 6) << mLVDS_data_write_ini) |
             ((test_pair_num == 6) << mLVDS_data_latch_1_toggle) |
             (0 << mLVDS_data_latch_1_ini) |
             ((test_pair_num == 6) << mLVDS_data_latch_0_toggle) |
             (1 << mLVDS_data_latch_0_ini) |
             ((test_pair_num == 6) << mLVDS_reset_1_select) |
             (mlvds_insert_start << mLVDS_reset_start);
    WRITE_LCD_REG(MLVDS_CONFIG_HI, (data32 >> 16));
    WRITE_LCD_REG(MLVDS_CONFIG_LO, (data32 & 0xffff));

    data32 = (1 << mLVDS_double_pattern) |  //POL double pattern
			 (0x3f << mLVDS_ins_reset) |
             (test_dual_gate << mLVDS_dual_gate) |
             ((test_bit_num == 8) << mLVDS_bit_num) |
             ((test_pair_num == 6) << mLVDS_pair_num) |
             (0 << mLVDS_msb_first) |
             (0 << mLVDS_PORT_SWAP) |
             ((scan_function==1 ? 1:0) << mLVDS_MLSB_SWAP) |
             (0 << mLVDS_PN_SWAP) |
             (1 << mLVDS_en);
    WRITE_LCD_REG(MLVDS_CONTROL,  (data32 & 0xffff));

    WRITE_LCD_REG(LVDS_PACK_CNTL_ADDR,
                   ( 0 ) | // repack
                   ( 0<<2 ) | // odd_even
                   ( 0<<3 ) | // reserve
                   ( 0<<4 ) | // lsb first
                   ( 0<<5 ) | // pn swap
                   ( 0<<6 ) | // dual port
                   ( 0<<7 ) | // use tcon control
                   ( 1<<8 ) | // 0:10bits, 1:8bits, 2:6bits, 3:4bits.
                   ( (scan_function==1 ? 2:0)<<10 ) |  //r_select // 0:R, 1:G, 2:B, 3:0
                   ( 1<<12 ) |                        //g_select
                   ( (scan_function==1 ? 0:2)<<14 ));  //b_select

    WRITE_LCD_REG(L_POL_CNTL_ADDR,  (1 << LCD_DCLK_SEL) |
       //(0x1 << LCD_HS_POL) |
       (0x1 << LCD_VS_POL)
    );
	
	WRITE_LCD_REG_BITS(LVDS_GEN_CNTL, 1, 3, 1); // enable fifo
}

static void init_phy_lvds(Lcd_Config_t *pConf)
{
    unsigned swing_ctrl;
    lcd_print("%s\n", __FUNCTION__);
	
    WRITE_LCD_REG(LVDS_PHY_CNTL3, 0xee1);
    WRITE_LCD_REG(LVDS_PHY_CNTL4 ,0);

	switch (pConf->lcd_control.lvds_config->lvds_vswing) {
		case 0:
			swing_ctrl = 0xaf20;
			break;
		case 1:
			swing_ctrl = 0xaf40;
			break;
		case 2:
			swing_ctrl = 0xa840;
			break;
		case 3:
			swing_ctrl = 0xa880;
			break;
		case 4:
			swing_ctrl = 0xa8c0;
			break;
		default:
			swing_ctrl = 0xaf40;
			break;
	}
	WRITE_LCD_REG(LVDS_PHY_CNTL5, swing_ctrl);

	WRITE_LCD_REG(LVDS_PHY_CNTL0,0x001f);
	WRITE_LCD_REG(LVDS_PHY_CNTL1,0xffff);

    WRITE_LCD_REG(LVDS_PHY_CNTL6,0xcccc);
    WRITE_LCD_REG(LVDS_PHY_CNTL7,0xcccc);
    WRITE_LCD_REG(LVDS_PHY_CNTL8,0xcccc);
}

static void set_video_adjust(Lcd_Config_t *pConf)
{
	lcd_print("vadj_brightness = 0x%x, vadj_contrast = 0x%x, vadj_saturation = 0x%x.\n", pConf->lcd_effect.vadj_brightness, pConf->lcd_effect.vadj_contrast, pConf->lcd_effect.vadj_saturation);
	WRITE_LCD_REG(VPP_VADJ2_Y, (pConf->lcd_effect.vadj_brightness << 8) | (pConf->lcd_effect.vadj_contrast << 0));
	WRITE_LCD_REG(VPP_VADJ2_MA_MB, (pConf->lcd_effect.vadj_saturation << 16));
	WRITE_LCD_REG(VPP_VADJ2_MC_MD, (pConf->lcd_effect.vadj_saturation << 0));
	WRITE_LCD_REG(VPP_VADJ_CTRL, 0xf);	//enable video adjust
}

static void switch_lcd_gates(Lcd_Type_t lcd_type)
{
	switch(lcd_type){
		case LCD_DIGITAL_TTL:
			switch_mod_gate_by_name("tcon", 1);
			switch_mod_gate_by_name("lvds", 0);
			break;
		case LCD_DIGITAL_LVDS:
		case LCD_DIGITAL_MINILVDS:
			switch_mod_gate_by_name("lvds", 1);
			switch_mod_gate_by_name("tcon", 0);
			break;
		default:
			break;
	}
}

static void _init_lcd_driver(Lcd_Config_t *pConf)
{
    int lcd_type = pConf->lcd_basic.lcd_type;
    unsigned char ss_level = (pConf->lcd_timing.clk_ctrl >> CLK_CTRL_SS) & 0xf;

    print_lcd_driver_version();
    switch_lcd_gates(lcd_type);

    printk("Init LCD mode: %s, %s(%u) %ubit, %ux%u@%u.%uHz, ss_level=%u(%s)\n", pConf->lcd_basic.model_name, lcd_type_table[lcd_type], lcd_type, pConf->lcd_basic.lcd_bits, pConf->lcd_basic.h_active, pConf->lcd_basic.v_active, (pConf->lcd_timing.sync_duration_num / 10), (pConf->lcd_timing.sync_duration_num % 10), ss_level, lcd_ss_level_table[ss_level]);

    switch(lcd_type){
        case LCD_DIGITAL_LVDS:
            set_pll_lcd(pConf);
            set_venc_lvds(pConf);
            set_tcon_lvds(pConf);
            set_control_lvds(pConf);
            init_phy_lvds(pConf);
            break;
        case LCD_DIGITAL_TTL:
            set_pll_lcd(pConf);
            set_venc_ttl(pConf);
            set_tcon_ttl(pConf);
            set_control_ttl(pConf);
            break;
        case LCD_DIGITAL_MINILVDS:
            set_pll_mlvds(pConf);
            set_venc_mlvds(pConf);
            set_tcon_mlvds(pConf);
            set_control_mlvds(pConf);
            init_phy_lvds(pConf);
            break;
        default:
            printk("Invalid LCD type.\n");
            break;
    }
    set_video_adjust(pConf);
    printk("%s finished.\n", __FUNCTION__);
}

static void _disable_lcd_driver(Lcd_Config_t *pConf)
{
    WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 0, 11, 1);	//close lvds phy clk gate: 0x104c[11]
    WRITE_LCD_REG_BITS(LVDS_GEN_CNTL, 0, 3, 1);	//disable lvds fifo

    WRITE_LCD_REG(ENCT_VIDEO_EN, 0);	//disable enct
    WRITE_LCD_REG(ENCL_VIDEO_EN, 0);	//disable encl

    WRITE_LCD_CBUS_REG_BITS(HHI_VIID_CLK_CNTL, 0, 0, 5);	//close vclk2 gate: 0x104b[4:0]

    WRITE_LCD_CBUS_REG_BITS(HHI_VIID_DIVIDER_CNTL, 0, 16, 1);	//close vid2_pll gate: 0x104c[16]

    WRITE_LCD_CBUS_REG_BITS(HHI_VIID_PLL_CNTL, 1, 30, 1);		//power down vid2_pll: 0x1047[30]

    switch_mod_gate_by_name("tcon", 0);
    switch_mod_gate_by_name("lvds", 0);
    printk("disable lcd display driver.\n");
}

static void _enable_vsync_interrupt(void)
{
	if ((READ_LCD_REG(ENCT_VIDEO_EN) & 1) || (READ_LCD_REG(ENCL_VIDEO_EN) & 1)) {
		WRITE_LCD_REG(VENC_INTCTRL, 0x200);
	}
	else{
		WRITE_LCD_REG(VENC_INTCTRL, 0x2);
	}
}

static void lcd_test(unsigned num)
{
	unsigned venc_video_mode, venc_test_base;
	
	if (lcd_Conf->lcd_basic.lcd_type == LCD_DIGITAL_TTL) {
		venc_video_mode = ENCT_VIDEO_MODE_ADV;
		venc_test_base = ENCT_TST_EN;
	}
	else {
		venc_video_mode = ENCL_VIDEO_MODE_ADV;
		venc_test_base = ENCL_TST_EN;
	}
	
	switch (num) {
		case 0:
			WRITE_LCD_REG(venc_video_mode, 0x8);
			printk("disable bist pattern\n");
			break;
		case 1:
			WRITE_LCD_REG(venc_video_mode, 0);
			WRITE_LCD_REG((venc_test_base+1), 1);
			WRITE_LCD_REG((venc_test_base+5), lcd_Conf->lcd_timing.video_on_pixel);
			WRITE_LCD_REG((venc_test_base+6), (lcd_Conf->lcd_basic.h_active / 9));
			WRITE_LCD_REG(venc_test_base, 1);
			printk("show bist pattern 1: Color Bar\n");
			break;
		case 2:
			WRITE_LCD_REG(venc_video_mode, 0);
			WRITE_LCD_REG((venc_test_base+1), 2);
			WRITE_LCD_REG(venc_test_base, 1);
			printk("show bist pattern 2: Thin Line\n");
			break;
		case 3:
			WRITE_LCD_REG(venc_video_mode, 0);
			WRITE_LCD_REG((venc_test_base+1), 3);
			WRITE_LCD_REG(venc_test_base, 1);
			printk("show bist pattern 3: Dot Grid\n");
			break;
		case 4:
			WRITE_LCD_REG(venc_video_mode, 0);
			WRITE_LCD_REG((venc_test_base+1), 0);
			WRITE_LCD_REG((venc_test_base+2), 0x200);
			WRITE_LCD_REG((venc_test_base+3), 0x200);
			WRITE_LCD_REG((venc_test_base+4), 0x200);
			WRITE_LCD_REG(venc_test_base, 1);
			printk("show test pattern 4: Gray\n");
			break;
		case 5:
			WRITE_LCD_REG(venc_video_mode, 0);
			WRITE_LCD_REG((venc_test_base+1), 0);
			WRITE_LCD_REG((venc_test_base+2), 0);
			WRITE_LCD_REG((venc_test_base+3), 0);
			WRITE_LCD_REG((venc_test_base+4), 0x3ff);
			WRITE_LCD_REG(venc_test_base, 1);
			printk("show test pattern 5: Red\n");
			break;
		case 6:
			WRITE_LCD_REG(venc_video_mode, 0);
			WRITE_LCD_REG((venc_test_base+1), 0);
			WRITE_LCD_REG((venc_test_base+2), 0x3ff);
			WRITE_LCD_REG((venc_test_base+3), 0);
			WRITE_LCD_REG((venc_test_base+4), 0);
			WRITE_LCD_REG(venc_test_base, 1);
			printk("show test pattern 6: Green\n");
			break;
		case 7:
			WRITE_LCD_REG(venc_video_mode, 0);
			WRITE_LCD_REG((venc_test_base+1), 0);
			WRITE_LCD_REG((venc_test_base+2), 0);
			WRITE_LCD_REG((venc_test_base+3), 0x3ff);
			WRITE_LCD_REG((venc_test_base+4), 0);
			WRITE_LCD_REG(venc_test_base, 1);
			printk("show test pattern 7: Blue\n");
			break;
		default:
			printk("un-support pattern num\n");
			break;
	}
}

//***********************************************
// sysfs api for video
//***********************************************
static ssize_t lcd_video_vso_read(struct class *class, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "read vso start: %u\n", lcd_Conf->lcd_timing.vso_vstart);
}

static ssize_t lcd_video_vso_write(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    unsigned int ret;
    unsigned int temp;

    temp = 10;
    ret = sscanf(buf, "%u", &temp);
    lcd_Conf->lcd_timing.vso_vstart = (unsigned short)temp;
    lcd_Conf->lcd_timing.vso_user = 1;
    WRITE_LCD_REG(ENCL_VIDEO_VSO_BLINE,   lcd_Conf->lcd_timing.vso_vstart);
    WRITE_LCD_REG(ENCL_VIDEO_VSO_ELINE,   lcd_Conf->lcd_timing.vso_vstart + 2);
    printk("set vso start: %u\n", lcd_Conf->lcd_timing.vso_vstart);

    if (ret != 1 || ret !=2)
        return -EINVAL;

    return count;
    //return 0;
}

static struct class_attribute lcd_video_class_attrs[] = {
    __ATTR(vso,  S_IRUGO | S_IWUSR, lcd_video_vso_read, lcd_video_vso_write),
};

static int creat_lcd_video_attr(Lcd_Config_t *pConf)
{
    int i;

    for(i=0;i<ARRAY_SIZE(lcd_video_class_attrs);i++) {
        if (class_create_file(pConf->lcd_misc_ctrl.debug_class, &lcd_video_class_attrs[i])) {
            printk("create lcd_video attribute %s fail\n", lcd_video_class_attrs[i].attr.name);
        }
    }

    return 0;
}

static int remove_lcd_video_attr(Lcd_Config_t *pConf)
{
    int i;

    if (pConf->lcd_misc_ctrl.debug_class == NULL)
        return -1;

    for(i=0;i<ARRAY_SIZE(lcd_video_class_attrs);i++) {
        class_remove_file(pConf->lcd_misc_ctrl.debug_class, &lcd_video_class_attrs[i]);
    }

    return 0;
}
//***********************************************

static DEFINE_MUTEX(lcd_init_mutex);
static void lcd_module_enable(void)
{
	mutex_lock(&lcd_init_mutex);

	_init_lcd_driver(lcd_Conf);
	lcd_Conf->lcd_power_ctrl.power_ctrl(ON);
	_enable_vsync_interrupt();
	lcd_Conf->lcd_misc_ctrl.lcd_status = 1;
	mutex_unlock(&lcd_init_mutex);
}

static void lcd_module_disable(void)
{
	mutex_lock(&lcd_init_mutex);
	lcd_Conf->lcd_misc_ctrl.lcd_status = 0;
	lcd_Conf->lcd_power_ctrl.power_ctrl(OFF);
	_disable_lcd_driver(lcd_Conf);
	mutex_unlock(&lcd_init_mutex);
}

static unsigned error_abs(unsigned num1, unsigned num2)
{
	if (num1 >= num2)
		return num1 - num2;
	else
		return num2 - num1;
}

static void generate_clk_parameter(Lcd_Config_t *pConf)
{
    unsigned pll_n = 0, pll_m = 0, pll_od = 0;
    unsigned vid_div_pre = 0, crt_xd = 0;

    unsigned m, n, od, div_pre, div_post, xd;
    unsigned od_sel, pre_div_sel;
    unsigned div_pre_sel_max, crt_xd_max;
    unsigned f_ref, pll_vco, fout_pll, div_pre_out, div_post_out, final_freq, iflogic_vid_clk_in_max;
    unsigned min_error = MAX_ERROR;
    unsigned error = MAX_ERROR;
    unsigned clk_num = 0;
    unsigned fin, fout;

    fin = FIN_FREQ; //kHz
    fout = pConf->lcd_timing.lcd_clk / 1000; //kHz

    switch (pConf->lcd_basic.lcd_type) {
        case LCD_DIGITAL_LVDS:
            div_pre_sel_max = DIV_PRE_SEL_MAX;
            div_post = 7;
            crt_xd_max = 1;
            iflogic_vid_clk_in_max = LVDS_MAX_VID_CLK_IN;
            break;
        case LCD_DIGITAL_TTL:
            div_pre_sel_max = DIV_PRE_SEL_MAX;
            div_post = 1;
            crt_xd_max = CRT_VID_DIV_MAX;
            iflogic_vid_clk_in_max = TTL_MAX_VID_CLK_IN;
            break;
        case LCD_DIGITAL_MINILVDS:
            div_pre_sel_max = DIV_PRE_SEL_MAX;
            div_post = 6;
            crt_xd_max = 1;
            iflogic_vid_clk_in_max = MLVDS_MAX_VID_CLK_IN;
            break;
        default:
            div_pre_sel_max = DIV_PRE_SEL_MAX;
            div_post = 1;
            crt_xd_max = 1;
            iflogic_vid_clk_in_max = LCD_VENC_MAX_CLK_IN;
            break;
    }

    switch (pConf->lcd_basic.lcd_type) {
        case LCD_DIGITAL_LVDS:
        case LCD_DIGITAL_TTL:
            for (n = PLL_N_MIN; n <= PLL_N_MAX; n++) {
                f_ref = fin / n;
                if ((f_ref >= PLL_FREF_MIN) && (f_ref <= PLL_FREF_MAX))    {
                    for (m = PLL_M_MIN; m <= PLL_M_MAX; m++) {
                        pll_vco = f_ref * m;
                        if ((pll_vco >= PLL_VCO_MIN) && (pll_vco <= PLL_VCO_MAX)) {
                            for (od_sel = OD_SEL_MAX; od_sel > 0; od_sel--) {
                                od = od_table[od_sel - 1];
                                fout_pll = pll_vco / od;
                            if (fout_pll <= DIV_PRE_MAX_CLK_IN) {
                                    for (pre_div_sel = 0; pre_div_sel < div_pre_sel_max; pre_div_sel++) {
                                        div_pre = div_pre_table[pre_div_sel];
                                        div_pre_out = fout_pll / div_pre;
                                        if (div_pre_out <= DIV_POST_MAX_CLK_IN) {
                                            div_post_out = div_pre_out / div_post;
                                            if (div_post_out <= CRT_VID_MAX_CLK_IN) {
                                                for (xd = 1; xd <= crt_xd_max; xd++) {
                                                    final_freq = div_post_out / xd;
                                                    if (final_freq < LCD_VENC_MAX_CLK_IN) {
                                                        if (final_freq < iflogic_vid_clk_in_max) {
                                                            error = error_abs(final_freq, fout);
                                                            if (error < min_error) {
                                                                min_error = error;
                                                                pll_m = m;
                                                                pll_n = n;
                                                                pll_od = od_sel - 1;
                                                                vid_div_pre = pre_div_sel;
                                                                crt_xd = xd;
                                                                clk_num++;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            break;
        default:
            break;
    }
    if (clk_num > 0) {
        pConf->lcd_timing.pll_ctrl = (pll_od << PLL_CTRL_OD) | (pll_n << PLL_CTRL_N) | (pll_m << PLL_CTRL_M);
        pConf->lcd_timing.div_ctrl = 0x18803 | (vid_div_pre << DIV_CTRL_DIV_PRE);
        pConf->lcd_timing.clk_ctrl = (pConf->lcd_timing.clk_ctrl & ~(0xff << CLK_CTRL_XD)) | (crt_xd << CLK_CTRL_XD);
    }
    else {
        pConf->lcd_timing.pll_ctrl = (1 << PLL_CTRL_OD) | (1 << PLL_CTRL_N) | (32 << PLL_CTRL_M);
        pConf->lcd_timing.div_ctrl = 0x18803;
        pConf->lcd_timing.clk_ctrl = (pConf->lcd_timing.clk_ctrl & ~(0xff << CLK_CTRL_XD)) | (7 << CLK_CTRL_XD);
        printk("Out of clock range, reset to default setting!\n");
    }
}

static void lcd_sync_duration(Lcd_Config_t *pConf)
{
	unsigned m, n, od, pre_div, xd, post_div;
	unsigned h_period, v_period, sync_duration;
	unsigned lcd_clk;

	m = ((pConf->lcd_timing.pll_ctrl) >> PLL_CTRL_M) & 0x1ff;
	n = ((pConf->lcd_timing.pll_ctrl) >> PLL_CTRL_N) & 0x1f;
	od = ((pConf->lcd_timing.pll_ctrl) >> PLL_CTRL_OD) & 0x3;
	od = od_table[od];
	pre_div = ((pConf->lcd_timing.div_ctrl) >> DIV_CTRL_DIV_PRE) & 0x7;
	pre_div = div_pre_table[pre_div];
	
	h_period = pConf->lcd_basic.h_period;
	v_period = pConf->lcd_basic.v_period;
	
	switch(pConf->lcd_basic.lcd_type) {
		case LCD_DIGITAL_LVDS:
			xd = 1;
			post_div = 7;
			break;
		case LCD_DIGITAL_TTL:
			xd = ((pConf->lcd_timing.clk_ctrl) >> CLK_CTRL_XD) & 0xff;
			post_div = 1;
			break;
		case LCD_DIGITAL_MINILVDS:
			xd = 1;
			post_div = 6;
			break;
		default:
			xd = ((pConf->lcd_timing.clk_ctrl) >> CLK_CTRL_XD) & 0xff;
			post_div = 1;
			break;
	}
	
	lcd_clk = ((m * FIN_FREQ) / (n * od * pre_div * post_div * xd)) * 1000;
	pConf->lcd_timing.lcd_clk = lcd_clk;
	sync_duration = ((lcd_clk / h_period) * 100) / v_period;
	sync_duration = (sync_duration + 5) / 10;
	
	pConf->lcd_timing.sync_duration_num = sync_duration;
	pConf->lcd_timing.sync_duration_den = 10;
	printk("lcd_clk=%u.%03uMHz, frame_rate=%u.%uHz.\n\n", (lcd_clk / 1000000), ((lcd_clk / 1000) % 1000), 
			(sync_duration / pConf->lcd_timing.sync_duration_den), ((sync_duration * 10 / pConf->lcd_timing.sync_duration_den) % 10));
}

static void lcd_tcon_config(Lcd_Config_t *pConf)
{
	unsigned short de_hstart, de_vstart;
	unsigned short hstart, hend, vstart, vend;
	unsigned short h_delay = 0;
	unsigned short h_offset = 0, v_offset = 0, vsync_h_phase=0;
	
	switch (pConf->lcd_basic.lcd_type) {
		case LCD_DIGITAL_LVDS:
			h_delay = LVDS_DELAY;
			break;
		case LCD_DIGITAL_TTL:
			h_delay = TTL_DELAY;
			break;
		default:
			h_delay = 0;
			break;
	}
#if 0
	h_offset = (pConf->lcd_timing.h_offset & 0xffff);
	v_offset = (pConf->lcd_timing.v_offset & 0xffff);
	if ((pConf->lcd_timing.h_offset >> 31) & 1)
		de_hstart = (pConf->lcd_timing.video_on_pixel + pConf->lcd_basic.h_period + h_delay + h_offset) % pConf->lcd_basic.h_period;
	else
		de_hstart = (pConf->lcd_timing.video_on_pixel + pConf->lcd_basic.h_period + h_delay - h_offset) % pConf->lcd_basic.h_period;
	if ((pConf->lcd_timing.v_offset >> 31) & 1)
		de_vstart = (pConf->lcd_timing.video_on_line + pConf->lcd_basic.v_period + v_offset) % pConf->lcd_basic.v_period;
	else
		de_vstart = (pConf->lcd_timing.video_on_line + pConf->lcd_basic.v_period - v_offset) % pConf->lcd_basic.v_period;
	
	hstart = (de_hstart + pConf->lcd_basic.h_period - pConf->lcd_timing.hsync_bp) % pConf->lcd_basic.h_period;
	hend = (de_hstart + pConf->lcd_basic.h_period - pConf->lcd_timing.hsync_bp + pConf->lcd_timing.hsync_width) % pConf->lcd_basic.h_period;	
	pConf->lcd_timing.hs_hs_addr = hstart;
	pConf->lcd_timing.hs_he_addr = hend;
	pConf->lcd_timing.hs_vs_addr = 0;
	pConf->lcd_timing.hs_ve_addr = pConf->lcd_basic.v_period - 1;
	
	vsync_h_phase = (pConf->lcd_timing.vsync_h_phase & 0xffff);
	if ((pConf->lcd_timing.vsync_h_phase >> 31) & 1) //negative
		vsync_h_phase = (hstart + pConf->lcd_basic.h_period - vsync_h_phase) % pConf->lcd_basic.h_period;
	else	//positive
		vsync_h_phase = (hstart + pConf->lcd_basic.h_period + vsync_h_phase) % pConf->lcd_basic.h_period;
	pConf->lcd_timing.vs_hs_addr = vsync_h_phase;
	pConf->lcd_timing.vs_he_addr = vsync_h_phase;
	vstart = (de_vstart + pConf->lcd_basic.v_period - pConf->lcd_timing.vsync_bp) % pConf->lcd_basic.v_period;
	vend = (de_vstart + pConf->lcd_basic.v_period - pConf->lcd_timing.vsync_bp + pConf->lcd_timing.vsync_width) % pConf->lcd_basic.v_period;
	pConf->lcd_timing.vs_vs_addr = vstart;
	pConf->lcd_timing.vs_ve_addr = vend;
	
	pConf->lcd_timing.de_hs_addr = de_hstart;
	pConf->lcd_timing.de_he_addr = (de_hstart + pConf->lcd_basic.h_active) % pConf->lcd_basic.h_period;
	pConf->lcd_timing.de_vs_addr = de_vstart;
	pConf->lcd_timing.de_ve_addr = (de_vstart + pConf->lcd_basic.v_active - 1) % pConf->lcd_basic.v_period;
#else
    pConf->lcd_timing.video_on_pixel = pConf->lcd_basic.h_period - pConf->lcd_basic.h_active - 1 -h_delay;
    pConf->lcd_timing.video_on_line = pConf->lcd_basic.v_period - pConf->lcd_basic.v_active;

    h_offset = (pConf->lcd_timing.h_offset & 0xffff);
    v_offset = (pConf->lcd_timing.v_offset & 0xffff);
    if ((pConf->lcd_timing.h_offset >> 31) & 1)
        de_hstart = (pConf->lcd_basic.h_period - pConf->lcd_basic.h_active - 1 + pConf->lcd_basic.h_period - h_offset) % pConf->lcd_basic.h_period;
    else
        de_hstart = (pConf->lcd_basic.h_period - pConf->lcd_basic.h_active - 1 + h_offset) % pConf->lcd_basic.h_period;
    if ((pConf->lcd_timing.v_offset >> 31) & 1)
        de_vstart = (pConf->lcd_basic.v_period - pConf->lcd_basic.v_active + pConf->lcd_basic.v_period - v_offset) % pConf->lcd_basic.v_period;
    else
        de_vstart = (pConf->lcd_basic.v_period - pConf->lcd_basic.v_active + v_offset) % pConf->lcd_basic.v_period;

    hstart = (de_hstart + pConf->lcd_basic.h_period - pConf->lcd_timing.hsync_bp) % pConf->lcd_basic.h_period;
    hend = (de_hstart + pConf->lcd_basic.h_period - pConf->lcd_timing.hsync_bp + pConf->lcd_timing.hsync_width) % pConf->lcd_basic.h_period;	
    pConf->lcd_timing.hs_hs_addr = hstart;
    pConf->lcd_timing.hs_he_addr = hend;
    pConf->lcd_timing.hs_vs_addr = 0;
    pConf->lcd_timing.hs_ve_addr = pConf->lcd_basic.v_period - 1;

    vsync_h_phase = (pConf->lcd_timing.vsync_h_phase & 0xffff);
    if ((pConf->lcd_timing.vsync_h_phase >> 31) & 1) //negative
        vsync_h_phase = (hstart + pConf->lcd_basic.h_period - vsync_h_phase) % pConf->lcd_basic.h_period;
    else //positive
        vsync_h_phase = (hstart + pConf->lcd_basic.h_period + vsync_h_phase) % pConf->lcd_basic.h_period;
    pConf->lcd_timing.vs_hs_addr = vsync_h_phase;
    pConf->lcd_timing.vs_he_addr = vsync_h_phase;
    vstart = (de_vstart + pConf->lcd_basic.v_period - pConf->lcd_timing.vsync_bp) % pConf->lcd_basic.v_period;
    vend = (de_vstart + pConf->lcd_basic.v_period - pConf->lcd_timing.vsync_bp + pConf->lcd_timing.vsync_width) % pConf->lcd_basic.v_period;
    pConf->lcd_timing.vs_vs_addr = vstart;
    pConf->lcd_timing.vs_ve_addr = vend;

    pConf->lcd_timing.de_hs_addr = de_hstart;
    pConf->lcd_timing.de_he_addr = (de_hstart + pConf->lcd_basic.h_active) % pConf->lcd_basic.h_period;
    pConf->lcd_timing.de_vs_addr = de_vstart;
    pConf->lcd_timing.de_ve_addr = (de_vstart + pConf->lcd_basic.v_active - 1) % pConf->lcd_basic.v_period;
#endif

    if (pConf->lcd_timing.vso_user == 0) {
        //pConf->lcd_timing.vso_hstart = pConf->lcd_timing.vs_hs_addr;
        pConf->lcd_timing.vso_vstart = pConf->lcd_timing.vs_vs_addr;
    }

    //lcd_print("hs_hs_addr=%d, hs_he_addr=%d, hs_vs_addr=%d, hs_ve_addr=%d\n", pConf->lcd_timing.hs_hs_addr, pConf->lcd_timing.hs_he_addr, pConf->lcd_timing.hs_vs_addr, pConf->lcd_timing.hs_ve_addr);
    //lcd_print("vs_hs_addr=%d, vs_he_addr=%d, vs_vs_addr=%d, vs_ve_addr=%d\n", pConf->lcd_timing.vs_hs_addr, pConf->lcd_timing.vs_he_addr, pConf->lcd_timing.vs_vs_addr, pConf->lcd_timing.vs_ve_addr);
    //lcd_print("de_hs_addr=%d, de_he_addr=%d, de_vs_addr=%d, de_ve_addr=%d\n", pConf->lcd_timing.de_hs_addr, pConf->lcd_timing.de_he_addr, pConf->lcd_timing.de_vs_addr, pConf->lcd_timing.de_ve_addr);
}

static void lcd_control_config_pre(Lcd_Config_t *pConf)
{
    unsigned ss_level;

    if (pConf->lcd_timing.lcd_clk < 200) {//prepare refer clock for frame_rate setting
        pConf->lcd_timing.lcd_clk = (pConf->lcd_timing.lcd_clk * pConf->lcd_basic.h_period * pConf->lcd_basic.v_period);
    }

    ss_level = ((pConf->lcd_timing.clk_ctrl >> CLK_CTRL_SS) & 0xf);
    ss_level = ((ss_level >= SS_LEVEL_MAX) ? (SS_LEVEL_MAX-1) : ss_level);

    switch (pConf->lcd_basic.lcd_type) {
        case LCD_DIGITAL_LVDS:
            if (pConf->lcd_control.lvds_config->lvds_repack_user == 0) {
                if (pConf->lcd_basic.lcd_bits == 6)
                    pConf->lcd_control.lvds_config->lvds_repack = 0;
                else
                    pConf->lcd_control.lvds_config->lvds_repack = 1;
            }
            break;
        default:
            break;
    }
    pConf->lcd_timing.clk_ctrl = ((pConf->lcd_timing.clk_ctrl & (~(0xf << CLK_CTRL_SS))) | (ss_level << CLK_CTRL_SS));
}

#ifdef CONFIG_USE_OF
static LVDS_Config_t lcd_lvds_config = {
    .lvds_vswing = 1,
    .lvds_repack_user = 0,
    .lvds_repack = 0,
    .pn_swap = 0,
};

static TTL_Config_t lcd_ttl_config = {
    .rb_swap = 0,
    .bit_swap = 0,
};

static MLVDS_Config_t lcd_mlvds_config = {
    .mlvds_insert_start = 0x45,
    .total_line_clk = 1448,
    .test_dual_gate = 1,
    .test_pair_num = 6,
    .scan_function = 1,
    .phase_select = 1,
    .TL080_phase =3,
};

static Lcd_Config_t lcd_config = {
    .lcd_timing = {
        .lcd_clk = 40000000,
        .clk_ctrl = ((1 << CLK_CTRL_AUTO) | (0 << CLK_CTRL_SS)),
        .hvsync_valid = 1,
        .de_valid = 1,
        .pol_ctrl = ((0 << POL_CTRL_CLK) | (1 << POL_CTRL_DE) | (0 << POL_CTRL_VS) | (0 << POL_CTRL_HS)),
    },
    .lcd_effect = {
        .rgb_base_addr = 0xf0,
        .rgb_coeff_addr = 0x74a,
        .dith_user = 0,
        .vadj_brightness = 0x0,
        .vadj_contrast = 0x80,
        .vadj_saturation = 0x100,
        .gamma_ctrl = ((0 << GAMMA_CTRL_REVERSE) | (1 << LCD_GAMMA_EN)),
        .gamma_r_coeff = 100,
        .gamma_g_coeff = 100,
        .gamma_b_coeff = 100,
        .set_gamma_table = set_gamma_table_lcd,
    },
    .lcd_control = {
        .lvds_config = &lcd_lvds_config,
        .ttl_config = &lcd_ttl_config,
        .mlvds_config = &lcd_mlvds_config,
    },
    .lcd_power_ctrl = {
        .power_on_step = 0,
        .power_off_step = 0,
        .power_ctrl = NULL,
        .power_ctrl_video = NULL,
    },
};

Lcd_Config_t* get_lcd_config(void)
{
    return &lcd_config;
}
#endif

static void lcd_config_assign(Lcd_Config_t *pConf)
{
    pConf->lcd_timing.vso_hstart = 10; //for video process
    pConf->lcd_timing.vso_vstart = 10; //for video process
    pConf->lcd_timing.vso_user = 0; //use default config

    pConf->lcd_power_ctrl.ports_ctrl = lcd_ports_ctrl;

    pConf->lcd_misc_ctrl.vpp_sel = 0;
    if (pConf->lcd_basic.lcd_type == LCD_DIGITAL_TTL) {
        if (READ_LCD_REG(ENCT_VIDEO_EN) & 1)
            pConf->lcd_misc_ctrl.lcd_status = 1;
        else
            pConf->lcd_misc_ctrl.lcd_status = 0;

    }
    else {
        if (READ_LCD_REG(ENCL_VIDEO_EN) & 1)
            pConf->lcd_misc_ctrl.lcd_status = 1;
        else
            pConf->lcd_misc_ctrl.lcd_status = 0;
    }
    pConf->lcd_misc_ctrl.module_enable = lcd_module_enable;
    pConf->lcd_misc_ctrl.module_disable = lcd_module_disable;
    pConf->lcd_misc_ctrl.lcd_test = lcd_test;
    pConf->lcd_misc_ctrl.print_version = print_lcd_driver_version;
}

void lcd_config_init(Lcd_Config_t *pConf)
{
    lcd_control_config_pre(pConf);

    if ((pConf->lcd_timing.clk_ctrl >> CLK_CTRL_AUTO) & 1) {
        printk("\nAuto generate clock parameters.\n");
        generate_clk_parameter(pConf);
    }
    else {
        printk("\nCustome clock parameters.\n");
    }
    printk("pll_ctrl=0x%x, div_ctrl=0x%x, clk_ctrl=0x%x.\n", pConf->lcd_timing.pll_ctrl, pConf->lcd_timing.div_ctrl, pConf->lcd_timing.clk_ctrl);

    lcd_sync_duration(pConf);
    lcd_tcon_config(pConf);
}

void lcd_config_probe(Lcd_Config_t *pConf)
{
    spin_lock_init(&gamma_write_lock);
    spin_lock_init(&lcd_clk_lock);

    lcd_Conf = pConf;
    lcd_config_assign(pConf);

    creat_lcd_video_attr(pConf);
}

void lcd_config_remove(Lcd_Config_t *pConf)
{
    remove_lcd_video_attr(pConf);
}
