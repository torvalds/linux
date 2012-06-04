/*
 * arch/arm/mach-sun3i/clock/csp_ccmu/ccmu/ccm_mod_clk_status.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "ccm_i.h"
#include "spec_ccm.h"

s32
_mod_clk_set_status( CSP_CCM_modClkNo_t clkNo, u8 clkOn )
{
    switch (clkNo)
    {
    case CSP_CCM_MOD_CLK_NFC:
        clkOn ? SET_BIT(CCM_NFC_MSC_CLK_R, 15) : CLEAR_BIT(CCM_NFC_MSC_CLK_R, 15);
        break;

    case CSP_CCM_MOD_CLK_MSC:
        clkOn ? SET_BIT(CCM_NFC_MSC_CLK_R, 7) : CLEAR_BIT(CCM_NFC_MSC_CLK_R, 7);
        break;

    case CSP_CCM_MOD_CLK_SDC0:
        clkOn ? SET_BIT(CCM_SDC01_CLK_R, 7) : CLEAR_BIT(CCM_SDC01_CLK_R, 7);
        break;
    case CSP_CCM_MOD_CLK_SDC1:
        clkOn ? SET_BIT(CCM_SDC01_CLK_R, 15) : CLEAR_BIT(CCM_SDC01_CLK_R, 15);
        break;

    case CSP_CCM_MOD_CLK_SDC2:
        clkOn ? SET_BIT(CCM_SDC23_CLK_R, 7) : CLEAR_BIT(CCM_SDC23_CLK_R, 7);
        break;
    case CSP_CCM_MOD_CLK_SDC3:
        clkOn ? SET_BIT(CCM_SDC23_CLK_R, 15) : CLEAR_BIT(CCM_SDC23_CLK_R, 15);
        break;

    case CSP_CCM_MOD_CLK_DE_IMAGE1:
        clkOn ? SET_BIT(CCM_DE_CLK_R, 31) : CLEAR_BIT(CCM_DE_CLK_R, 31);
      //  clkOn ? SET_BIT(CCM_MISC_CLK_R, DE_IMAGE1_RST_INVAL_BIT) : CLEAR_BIT(CCM_MISC_CLK_R, DE_IMAGE1_RST_INVAL_BIT);
        break;
    case CSP_CCM_MOD_CLK_DE_IMAGE0:
        clkOn ? SET_BIT(CCM_DE_CLK_R, 23) : CLEAR_BIT(CCM_DE_CLK_R, 23);
      //  clkOn ? SET_BIT(CCM_MISC_CLK_R, DE_IMAGE0_RST_INVAL_BIT) : CLEAR_BIT(CCM_MISC_CLK_R, DE_IMAGE0_RST_INVAL_BIT);
        break;

    case CSP_CCM_MOD_CLK_DE_SCALE1:
        clkOn ? SET_BIT(CCM_DE_CLK_R, 15) : CLEAR_BIT(CCM_DE_CLK_R, 15);
     //   clkOn ? SET_BIT(CCM_MISC_CLK_R, DE_SCALE1_RST_INVAL_BIT) : CLEAR_BIT(CCM_MISC_CLK_R, DE_SCALE1_RST_INVAL_BIT);
        break;
    case CSP_CCM_MOD_CLK_DE_SCALE0:
        clkOn ? SET_BIT(CCM_DE_CLK_R, 7) : CLEAR_BIT(CCM_DE_CLK_R, 7);
      //  clkOn ? SET_BIT(CCM_MISC_CLK_R, DE_SCALE0_RST_INVAL_BIT) : CLEAR_BIT(CCM_MISC_CLK_R, DE_SCALE0_RST_INVAL_BIT);
        break;

    case CSP_CCM_MOD_CLK_VE:
        clkOn ? SET_BIT(CCM_VE_CLK_R, 7) : CLEAR_BIT(CCM_VE_CLK_R, 7);
        break;

    case CSP_CCM_MOD_CLK_CSI1:
        clkOn ? SET_BIT(CCM_CSI_CLK_R, 15) : CLEAR_BIT(CCM_CSI_CLK_R, 15);
        break;
    case CSP_CCM_MOD_CLK_CSI0:
        clkOn ? SET_BIT(CCM_CSI_CLK_R, 7) : CLEAR_BIT(CCM_CSI_CLK_R, 7);
        break;

    case CSP_CCM_MOD_CLK_IR:
        clkOn ? SET_BIT(CCM_IR_CLK_R, 7) : CLEAR_BIT(CCM_IR_CLK_R, 7);
        break;

    case CSP_CCM_MOD_CLK_AC97:
        clkOn ? SET_BIT(CCM_AUDIO_CLK_R, 0) : CLEAR_BIT(CCM_AUDIO_CLK_R, 0);
        break;
    case CSP_CCM_MOD_CLK_I2S:
        clkOn ? SET_BIT(CCM_AUDIO_CLK_R, 1) : CLEAR_BIT(CCM_AUDIO_CLK_R, 1);
        break;
    case CSP_CCM_MOD_CLK_SPDIF:
        clkOn ? SET_BIT(CCM_AUDIO_CLK_R, 2) : CLEAR_BIT(CCM_AUDIO_CLK_R, 2);
        break;
    case CSP_CCM_MOD_CLK_AUDIO_CODEC:
        clkOn ? SET_BIT(CCM_AUDIO_CLK_R, 3) : CLEAR_BIT(CCM_AUDIO_CLK_R, 3);
        break;
    case CSP_CCM_MOD_CLK_ACE:
        clkOn ? SET_BIT(CCM_AUDIO_CLK_R, 23) : CLEAR_BIT(CCM_AUDIO_CLK_R, 23);
        break;

    case CSP_CCM_MOD_CLK_SS:
        clkOn ? SET_BIT(CCM_TS_SS_CLK_R, 15) : CLEAR_BIT(CCM_TS_SS_CLK_R, 15);
        break;
    case CSP_CCM_MOD_CLK_TS:
        clkOn ? SET_BIT(CCM_TS_SS_CLK_R,7) : CLEAR_BIT(CCM_TS_SS_CLK_R, 7);
        break;

    case CSP_CCM_MOD_CLK_USB_PHY0:
        if (clkOn){
            SET_BIT(CCM_AVS_USB_CLK_R, 4);
          //  SET_BIT(CCM_AVS_USB_CLK_R, USB_PHY0_RST_INVAL_BIT);
        }
        else{
            CLEAR_BIT(CCM_AVS_USB_CLK_R, 4);
        //    CLEAR_BIT(CCM_AVS_USB_CLK_R, USB_PHY0_RST_INVAL_BIT);
        }
        break;//end case
    case CSP_CCM_MOD_CLK_USB_PHY1:
        if (clkOn){
            SET_BIT(CCM_AVS_USB_CLK_R, 5);
         //   SET_BIT(CCM_AVS_USB_CLK_R, USB_PHY1_RST_INVAL_BIT);
        }
        else{
            CLEAR_BIT(CCM_AVS_USB_CLK_R, 5);
         //   CLEAR_BIT(CCM_AVS_USB_CLK_R, USB_PHY1_RST_INVAL_BIT);
        }
        break;//end case
    case CSP_CCM_MOD_CLK_USB_PHY2:
        if (clkOn){
            SET_BIT(CCM_AVS_USB_CLK_R, 5);
          //  SET_BIT(CCM_AVS_USB_CLK_R, USB_PHY2_RST_INVAL_BIT);
        }
        else{
            CLEAR_BIT(CCM_AVS_USB_CLK_R, 5);
           // CLEAR_BIT(CCM_AVS_USB_CLK_R, USB_PHY2_RST_INVAL_BIT);
        }
        break;//end case
    case CSP_CCM_MOD_CLK_AVS:
        clkOn ? SET_BIT(CCM_AVS_USB_CLK_R, 8) : CLEAR_BIT(CCM_AVS_USB_CLK_R, 8);
        break;

    case CSP_CCM_MOD_CLK_ATA:
        clkOn ? SET_BIT(CCM_ATA_CLK_R, 7) : CLEAR_BIT(CCM_ATA_CLK_R, 7);
        break;

    case CSP_CCM_MOD_CLK_DE_MIX:
        clkOn ? SET_BIT(CCM_DE_MIX_CLK_R, 15) : CLEAR_BIT(CCM_DE_MIX_CLK_R, 15);
        break;

    case CSP_CCM_MOD_CLK_KEY_PAD:
        clkOn ? SET_BIT(CCM_MISC_CLK_R, 15) : CLEAR_BIT(CCM_MISC_CLK_R, 15);
        break;

    case CSP_CCM_MOD_CLK_COM:////////////////////////////////////////////////////////////
        //clkOn ? SET_BIT(CCM_MISC_CLK_R, 0) : CLEAR_BIT(CCM_MISC_CLK_R, 0);
        break;

    case CSP_CCM_MOD_CLK_TVENC_1X:
        clkOn ? SET_BIT(CCM_TVENC_CLK_R, 17) : CLEAR_BIT(CCM_TVENC_CLK_R, 17);
        break;
    case CSP_CCM_MOD_CLK_TVENC_2X:
        clkOn ? SET_BIT(CCM_TVENC_CLK_R, 19) : CLEAR_BIT(CCM_TVENC_CLK_R, 19);
        break;
    case CSP_CCM_MOD_CLK_TCON0_0:
        clkOn ? SET_BIT(CCM_TCON_CLK_R, 6) : CLEAR_BIT(CCM_TCON_CLK_R, 6);
        break;
    case CSP_CCM_MOD_CLK_TCON0_1:
        clkOn ? SET_BIT(CCM_TCON_CLK_R, 7) : CLEAR_BIT(CCM_TCON_CLK_R, 7);
        break;
    case CSP_CCM_MOD_CLK_TCON1_0:
        clkOn ? SET_BIT(CCM_TCON_CLK_R, 22) : CLEAR_BIT(CCM_TCON_CLK_R, 22);
        break;
    case CSP_CCM_MOD_CLK_TCON1_1:
        clkOn ? SET_BIT(CCM_TCON_CLK_R, 23) : CLEAR_BIT(CCM_TCON_CLK_R, 23);
        break;

    default:
        return AW_CCMU_FAIL;
    }

    return AW_CCMU_OK;
}


s32 _mod_clk_is_on( CSP_CCM_modClkNo_t clkNo )
{
    switch (clkNo)
    {
    case CSP_CCM_MOD_CLK_NFC:
        return TEST_BIT(CCM_NFC_MSC_CLK_R, 15);

    case CSP_CCM_MOD_CLK_MSC:
        return TEST_BIT(CCM_NFC_MSC_CLK_R, 7);

    case CSP_CCM_MOD_CLK_SDC0:
        return TEST_BIT(CCM_SDC01_CLK_R, 7);
    case CSP_CCM_MOD_CLK_SDC1:
        return TEST_BIT(CCM_SDC01_CLK_R, 15);

    case CSP_CCM_MOD_CLK_SDC2:
        return TEST_BIT(CCM_SDC23_CLK_R, 7);
    case CSP_CCM_MOD_CLK_SDC3:
        return TEST_BIT(CCM_SDC23_CLK_R, 15);

    case CSP_CCM_MOD_CLK_DE_IMAGE1:
        return TEST_BIT(CCM_DE_CLK_R, 31);
    case CSP_CCM_MOD_CLK_DE_IMAGE0:
        return TEST_BIT(CCM_DE_CLK_R, 23);
    case CSP_CCM_MOD_CLK_DE_SCALE1:
        return TEST_BIT(CCM_DE_CLK_R, 15);
    case CSP_CCM_MOD_CLK_DE_SCALE0:
        return TEST_BIT(CCM_DE_CLK_R, 7);

    case CSP_CCM_MOD_CLK_VE:
        return TEST_BIT(CCM_VE_CLK_R, 7);

    case CSP_CCM_MOD_CLK_CSI1:
        return TEST_BIT(CCM_CSI_CLK_R, 15);
    case CSP_CCM_MOD_CLK_CSI0:
        return TEST_BIT(CCM_CSI_CLK_R, 7);

    case CSP_CCM_MOD_CLK_IR:
        return TEST_BIT(CCM_IR_CLK_R, 7);

    case CSP_CCM_MOD_CLK_AC97:
        return TEST_BIT(CCM_AUDIO_CLK_R, 0);
    case CSP_CCM_MOD_CLK_I2S:
        return TEST_BIT(CCM_AUDIO_CLK_R, 1);
    case CSP_CCM_MOD_CLK_SPDIF:
        return TEST_BIT(CCM_AUDIO_CLK_R, 2);
    case CSP_CCM_MOD_CLK_AUDIO_CODEC:
        return TEST_BIT(CCM_AUDIO_CLK_R, 3);
    case CSP_CCM_MOD_CLK_ACE:
        return TEST_BIT(CCM_AUDIO_CLK_R, 23);

    case CSP_CCM_MOD_CLK_SS:
        return TEST_BIT(CCM_TS_SS_CLK_R, 15);
    case CSP_CCM_MOD_CLK_TS:
        return TEST_BIT(CCM_TS_SS_CLK_R,7);

    case CSP_CCM_MOD_CLK_USB_PHY0:
        return TEST_BIT(CCM_AVS_USB_CLK_R, 4);
    case CSP_CCM_MOD_CLK_USB_PHY1:
    case CSP_CCM_MOD_CLK_USB_PHY2:
        return TEST_BIT(CCM_AVS_USB_CLK_R, 5);
    case CSP_CCM_MOD_CLK_AVS:
        return TEST_BIT(CCM_AVS_USB_CLK_R, 8);

    case CSP_CCM_MOD_CLK_ATA:
        return TEST_BIT(CCM_ATA_CLK_R, 7);

    case CSP_CCM_MOD_CLK_DE_MIX:
        return TEST_BIT(CCM_DE_MIX_CLK_R, 15);

    case CSP_CCM_MOD_CLK_KEY_PAD:
        return TEST_BIT(CCM_MISC_CLK_R, 15);

    case CSP_CCM_MOD_CLK_COM:
    	return 1; //always on
        break;

    case CSP_CCM_MOD_CLK_TVENC_1X:
        return TEST_BIT(CCM_TVENC_CLK_R, 17);
    case CSP_CCM_MOD_CLK_TVENC_2X:
        return TEST_BIT(CCM_TVENC_CLK_R, 19);
    case CSP_CCM_MOD_CLK_TCON0_0:
        return TEST_BIT(CCM_TCON_CLK_R, 6);
    case CSP_CCM_MOD_CLK_TCON0_1:
        return TEST_BIT(CCM_TCON_CLK_R, 7);
    case CSP_CCM_MOD_CLK_TCON1_0:
        return TEST_BIT(CCM_TCON_CLK_R, 22);
    case CSP_CCM_MOD_CLK_TCON1_1:
        return TEST_BIT(CCM_TCON_CLK_R, 23);

    default:
        return 0;
    }

    return 0;
}



 static s32
_set_ahb_device_clk_gating( CSP_CCM_modClkNo_t clkNo, s32 clkOn)
{
    u32 pos = (u32) ( clkNo - CSP_CCM_MOD_CLK_AHB_USB0 );

    clkOn ? SET_BIT(CCM_AHB_GATE_R, pos) : CLEAR_BIT(CCM_AHB_GATE_R, pos);

    return AW_CCMU_OK;
}

static s32
_get_ahb_device_clk_gating( CSP_CCM_modClkNo_t clkNo )
{
    u32 pos = (u32)(clkNo - CSP_CCM_MOD_CLK_AHB_USB0);

    return TEST_BIT(CCM_AHB_GATE_R, pos);
}

 static s32
_set_apb_device_clk_gating( CSP_CCM_modClkNo_t clkNo, s32 clkOn )
{
    u32 pos = (u32)(clkNo - CSP_CCM_MOD_CLK_APB_KEY_PAD);

    clkOn ? SET_BIT(CCM_APB_GATE_R, pos) : CLEAR_BIT(CCM_APB_GATE_R, pos);

    return AW_CCMU_OK;
}
static s32
_get_apb_device_clk_gating( CSP_CCM_modClkNo_t clkNo )
{
    u32 pos = (u32)(clkNo - CSP_CCM_MOD_CLK_APB_KEY_PAD);

    return TEST_BIT(CCM_APB_GATE_R, pos);
}


static u32
_get_sdram_device_pos( CSP_CCM_modClkNo_t clkNo )
{
    u32 pos = (u32)(clkNo - CSP_CCM_MOD_CLK_SDRAM_OUTPUT + 15);

    if (pos > 22){
        pos += 1;//bit 23 is reserved
    }
    if (pos > 25){
        pos += 2;//bit 26 and 27 is reserved
    }

    return pos;
}
 static s32
_set_sdram_device_clk_gating( CSP_CCM_modClkNo_t clkNo, s32 clkOn)
{
    u32 pos = 0;

    pos = _get_sdram_device_pos(clkNo);

    clkOn ? SET_BIT(CCM_SDRAM_PLL_R, pos) : CLEAR_BIT(CCM_SDRAM_PLL_R, pos);

    return AW_CCMU_OK;
}

static s32
_get_sdram_device_clk_gating( CSP_CCM_modClkNo_t clkNo )
{
    u32 pos = 0;

    pos = _get_sdram_device_pos(clkNo);

    return TEST_BIT(CCM_SDRAM_PLL_R, pos);
}

s32     CSP_CCM_set_mod_clk_status(CSP_CCM_modClkNo_t modClkNo, s32 onOrOff)
{
    if (CSP_CCM_MOD_CLK_LVDS >= modClkNo){
        return _mod_clk_set_status(modClkNo, onOrOff);
    }
    if (CSP_CCM_MOD_CLK_AHB_USB0 <= modClkNo && CSP_CCM_MOD_CLK_AHB_TCON1 >= modClkNo){
        return _set_ahb_device_clk_gating(modClkNo, onOrOff);
    }
    if (CSP_CCM_MOD_CLK_APB_KEY_PAD <= modClkNo && CSP_CCM_MOD_CLK_APB_SMC >= modClkNo){
        return _set_apb_device_clk_gating(modClkNo, onOrOff);
    }
    if (CSP_CCM_MOD_CLK_SDRAM_OUTPUT <= modClkNo && CSP_CCM_MOD_CLK_SDRAM_COM_ENGINE >= modClkNo){
        return _set_sdram_device_clk_gating(modClkNo, onOrOff);
    }
    return AW_CCMU_FAIL;
}

s32  CSP_CCM_get_mod_clk_status(CSP_CCM_modClkNo_t modClkNo)
{
    if (CSP_CCM_MOD_CLK_LVDS >= modClkNo){
        return _mod_clk_is_on(modClkNo);
    }
    if (CSP_CCM_MOD_CLK_AHB_USB0 <= modClkNo && CSP_CCM_MOD_CLK_AHB_TCON1 >= modClkNo){
        return _get_ahb_device_clk_gating(modClkNo);
    }
    if (CSP_CCM_MOD_CLK_APB_KEY_PAD <= modClkNo && CSP_CCM_MOD_CLK_APB_SMC >= modClkNo){
        return _get_apb_device_clk_gating(modClkNo);
    }
    if (CSP_CCM_MOD_CLK_SDRAM_DE_SCALE0 <= modClkNo && CSP_CCM_MOD_CLK_SDRAM_COM_ENGINE >= modClkNo){
        return _get_sdram_device_clk_gating(modClkNo);
    }
    return 0;
}

