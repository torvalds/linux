/*
 * arch/arm/mach-sun3i/clock/csp_ccmu/ccmu/ccm_mod_clk_freq.c
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

static char* _modClkName[CSP_CCM_MOD_CLK_TOTAL_NUM] =
{
    "nfc",
    "msc",//memory stick controller
    "sdc0",
    "sdc1",
    "sdc2",
    "sdc3",
    "de_image1",
    "de_image0",
    "de_scale1",
    "de_scale0",
    "ve",
    "csi1",
    "csi0",
    "ir",

    "ac97",
    "i2s",
    "spdif",
    "audio_codec",
    "ace",//audio/compressed engine

    "ss",
    "ts",

    "usb_phy0",
    "usb_phy1",
    "usb_phy2",
    "avs",

    "ata",

    "de_mix",

    "key_pad",
    "com",

    "tvenc_1x",
    "tvenc_2x",

    "tcon0_0",
    "tcon0_1",
    "tcon1_0",
    "tcon1_1",
    "lvds",


    "ahb_usb0",
    "ahb_usb1",
    "ahb_ss",
    "ahb_ata",
    "ahb_tvenc",
    "ahb_csi0",
    "dmac",
    "ahb_sdc0",
    "ahb_sdc1",
    "ahb_sdc2",
    "ahb_sdc3",
    "ahb_msc",
    "ahb_nfc",
    "ahb_sdramc",
    "ahb_tcon0",
    "ahb_ve",
    "bist",
    "emac",
    "ahb_ts",
    "spi0",
    "spi1",
    "spi2",
    "ahb_usb2",
    "ahb_csi1",
    "ahb_com",
    "ahb_ace",
    "ahb_de_scale0",
    "ahb_de_image0",
    "ahb_de_mix",
    "ahb_de_scale1",
    "ahb_de_image1",
    "ahb_tcon1",


    "key_pad",
    "twi2",
    "twi0",
    "twi1",
    "pio",
    "uart0",
    "uart1",
    "uart2",
    "uart3",
    "apb_audio_codec",
    "apb_ir",
    "apb_i2s",
    "apb_spdif",
    "apb_ac97",
    "ps0",
    "ps1",
    "uart4",
    "uart5",
    "uart6",
    "uart7",
    "can",
    "smc",//smart card controller

    "sdram_output",
    "sdram_de_scale0",
    "sdram_de_scale1",
    "sdram_de_image0",
    "sdram_de_image1",
    "sdram_csi0",
    "sdram_csi1",
    "sdram_de_mix",
    "sdram_ve",
    "sdram_ace",//audio/compress engine
    "sdram_ts",
    "sdram_com_engine",
};


extern s32 _mod_clk_is_on( CSP_CCM_modClkNo_t clkNo );//if clock if off, regVal == 0

u32 _pll_get_freq(CSP_CCM_sysClkNo_t sysClkNo);

extern u32 _sdram_get_rate( void );
extern u32 _aHb_get_rate( void );
extern u32 _aPb_get_rate( void );

#define  _CCM_CHECK_EQUAL(varVal, cmpVal, retVal)  do{\
    if(cmpVal != varVal){\
    CCM_msg("src clock %d !=",varVal);CCM_inf(#cmpVal);\
    return retVal;} }while(0)

u32 _get_HOSC_rate(void)
{
    return 24*MHz;
}

u32 _get_LOSC_rate(void)
{
    return 32*1000;
}

 /*********************************************************************
 * If there are multiple source clocks to select , I use a uniform method GET_BITS_VAL(regVal, _pos, Len)
 * to get the source  clock of clock which number is enumerated as clkNo. Following are the 3 methods to get
 * the 3 parameters.
 *********************************************************************/

static  u32
_mod_clk_REG_OFST( CSP_CCM_modClkNo_t clkNo)
{
	switch (clkNo)  {
	case CSP_CCM_MOD_CLK_NFC://0/2/3---video PLL/sdram PLL/core PLL
	case CSP_CCM_MOD_CLK_MSC:
		return CCM_O_NFC_MSC_CLK_R;

	case CSP_CCM_MOD_CLK_SDC0:
	case CSP_CCM_MOD_CLK_SDC1:
		return CCM_O_SDC01_CLK_R;

	case CSP_CCM_MOD_CLK_SDC2:
	case CSP_CCM_MOD_CLK_SDC3:
		return CCM_O_SDC23_CLK_R;

	case CSP_CCM_MOD_CLK_DE_IMAGE1:
	case CSP_CCM_MOD_CLK_DE_IMAGE0:
	case CSP_CCM_MOD_CLK_DE_SCALE1:
	case CSP_CCM_MOD_CLK_DE_SCALE0:
		return CCM_O_DE_CLK_R;

	case CSP_CCM_MOD_CLK_CSI1:
	case CSP_CCM_MOD_CLK_CSI0:
		return CCM_O_CSI_CLK_R;

	case CSP_CCM_MOD_CLK_IR:
		return CCM_O_IR_CLK_R;

	case CSP_CCM_MOD_CLK_ACE:
		return CCM_O_AUDIO_CLK_R;

	case CSP_CCM_MOD_CLK_SS:
	case CSP_CCM_MOD_CLK_TS:
		return CCM_O_TS_SS_CLK_R;

	case CSP_CCM_MOD_CLK_ATA:
		return CCM_O_ATA_CLK_R;

	case CSP_CCM_MOD_CLK_DE_MIX:
		return CCM_O_DE_MIX_CLK_R;

	case CSP_CCM_MOD_CLK_KEY_PAD:
		return CCM_O_MISC_CLK_R;

	case CSP_CCM_MOD_CLK_TVENC_1X:
	case CSP_CCM_MOD_CLK_TVENC_2X:
		return CCM_O_TVENC_CLK_R;

	case CSP_CCM_MOD_CLK_TCON0_0:
	case CSP_CCM_MOD_CLK_TCON0_1:
	case CSP_CCM_MOD_CLK_TCON1_0:
	case CSP_CCM_MOD_CLK_TCON1_1:
	case CSP_CCM_MOD_CLK_LVDS:
		return CCM_O_TCON_CLK_R;

	default:
		return 0;
	}
}


static u8
_mod_clk_SRC_BITS_POS( CSP_CCM_modClkNo_t clkNo)
{
    switch (clkNo)//this switch aim to get *bitPos
    {
    case CSP_CCM_MOD_CLK_NFC://first class start
    case CSP_CCM_MOD_CLK_SDC1:
    case CSP_CCM_MOD_CLK_SDC3:
    case CSP_CCM_MOD_CLK_SS: //
    case CSP_CCM_MOD_CLK_DE_SCALE1://2nd class start
    case CSP_CCM_MOD_CLK_CSI1:
    case CSP_CCM_MOD_CLK_DE_MIX:
    case CSP_CCM_MOD_CLK_KEY_PAD:
        return 12U;

    case CSP_CCM_MOD_CLK_MSC://first class start
    case CSP_CCM_MOD_CLK_SDC0:
    case CSP_CCM_MOD_CLK_SDC2:
    case CSP_CCM_MOD_CLK_TS:
    case CSP_CCM_MOD_CLK_ATA://
    case CSP_CCM_MOD_CLK_DE_SCALE0://first class start
    case CSP_CCM_MOD_CLK_CSI0:
        return 4U;

    case CSP_CCM_MOD_CLK_DE_IMAGE1:
        return 28U;

    case CSP_CCM_MOD_CLK_DE_IMAGE0:
    case CSP_CCM_MOD_CLK_ACE:
        return 20U;

    case CSP_CCM_MOD_CLK_IR:
        return 8U;

    case CSP_CCM_MOD_CLK_TVENC_1X:
        return 16U;
    case CSP_CCM_MOD_CLK_TVENC_2X:
        return 18U;
    case CSP_CCM_MOD_CLK_TCON0_0:
        return 4U;
    case CSP_CCM_MOD_CLK_TCON1_0:
        return 20U;
    case CSP_CCM_MOD_CLK_LVDS:
        return 16U;

    default:
        return 255;/////////////////////////////////
    }
}

u8 _mod_clk_SRC_BITS_LEN( CSP_CCM_modClkNo_t clkNo )
{
    switch (clkNo)
    {
    case CSP_CCM_MOD_CLK_KEY_PAD:
    case CSP_CCM_MOD_CLK_TVENC_1X:
    case CSP_CCM_MOD_CLK_TVENC_2X:
    case CSP_CCM_MOD_CLK_TCON0_0:
    case CSP_CCM_MOD_CLK_TCON0_1:
    case CSP_CCM_MOD_CLK_LVDS:
        return 1U;

    default:
        return 2U;
    }
}

/*********************************************************************
* Method	 :    		_mod_clk_set_src_clk
* Description: Use SET_BITS to set src clock.
* Parameters :
@CSP_CCM_modClkNo_t clkNo
@CSP_CCM_sysClkNo_t srcClk
* Returns    :   CSP_CCM_err_t
* Note       :
*********************************************************************/
static CSP_CCM_err_t
_mod_clk_set_src_clk(CSP_CCM_modClkNo_t clkNo, CSP_CCM_sysClkNo_t srcClk)
{
    u32 regOfst      = 0;
    u32 srcBitsPos      = 0;
    u8  srcBitsLen      = 1;
    u32 srcBitsVal      = 0;


    if (CSP_CCM_MOD_CLK_AHB_USB0 <= clkNo && clkNo <= CSP_CCM_MOD_CLK_AHB_TCON1)
    {
        if (CSP_CCM_SYS_CLK_AHB == srcClk){
            return CSP_CCM_ERR_NONE;
        }
        CCM_msg("src of clk[%d] must be CSP_CCM_SYS_CLK_AHB. not [%d].\n", clkNo, srcClk);
        return CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE;
    }

    if (CSP_CCM_MOD_CLK_APB_KEY_PAD <= clkNo && clkNo <= CSP_CCM_MOD_CLK_APB_SMC)
    {
        if (CSP_CCM_SYS_CLK_APB == srcClk){
            return CSP_CCM_ERR_NONE;
        }
        CCM_msg("src of clk[%d] must be CSP_CCM_SYS_CLK_APB. not [%d].\n", clkNo, srcClk);
        return CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE;
    }

    if (CSP_CCM_MOD_CLK_SDRAM_OUTPUT <= clkNo && clkNo <= CSP_CCM_MOD_CLK_SDRAM_COM_ENGINE)
    {
        if (CSP_CCM_SYS_CLK_SDRAM == srcClk){
            return CSP_CCM_ERR_NONE;
        }
        CCM_msg("src of clk[%d] must be CSP_CCM_SYS_CLK_SDRAM. not [%d].\n", clkNo, srcClk);
        return CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE;
    }

    switch (clkNo)
    {
    case CSP_CCM_MOD_CLK_USB_PHY0://source must be HOSC
    case CSP_CCM_MOD_CLK_USB_PHY1://source must be HOSC
    case CSP_CCM_MOD_CLK_USB_PHY2://source must be HOSC
    case CSP_CCM_MOD_CLK_AVS://source must be HOSC
    case CSP_CCM_MOD_CLK_COM:////////////////
        _CCM_CHECK_EQUAL(srcClk, CSP_CCM_SYS_CLK_HOSC, CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE);
        return CSP_CCM_ERR_NONE;

    case CSP_CCM_MOD_CLK_VE://must be VE PLL
        _CCM_CHECK_EQUAL(srcClk, CSP_CCM_SYS_CLK_VE_PLL, CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE);
        return CSP_CCM_ERR_NONE;

    case CSP_CCM_MOD_CLK_I2S://must be audio PLL_8X
        _CCM_CHECK_EQUAL(srcClk, CSP_CCM_SYS_CLK_AUDIO_PLL_8X, CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE);

    case CSP_CCM_MOD_CLK_AC97://source must be audio PLL_4x
        _CCM_CHECK_EQUAL(srcClk, CSP_CCM_SYS_CLK_AUDIO_PLL, CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE);

    case CSP_CCM_MOD_CLK_AUDIO_CODEC://source must be audio PLL
        _CCM_CHECK_EQUAL(srcClk, CSP_CCM_SYS_CLK_AUDIO_PLL, CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE);
        return CSP_CCM_ERR_NONE;
    case CSP_CCM_MOD_CLK_SPDIF://source must be audio PLL
        _CCM_CHECK_EQUAL(srcClk, CSP_CCM_SYS_CLK_AUDIO_PLL_4X, CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE);
        return CSP_CCM_ERR_NONE;

    case CSP_CCM_MOD_CLK_TCON0_1://should always be TVENC_CLK0
        _CCM_CHECK_EQUAL(srcClk, CSP_CCM_SYS_CLK_TVENC_0, CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE);
        return CSP_CCM_ERR_NONE;

    case CSP_CCM_MOD_CLK_TCON1_1://should always be TVEN_CLK1
        _CCM_CHECK_EQUAL(srcClk, CSP_CCM_SYS_CLK_TVENC_1, CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE);
        return CSP_CCM_ERR_NONE;

    case CSP_CCM_MOD_CLK_NFC:
    case CSP_CCM_MOD_CLK_MSC:
    case CSP_CCM_MOD_CLK_SDC0:
    case CSP_CCM_MOD_CLK_SDC1:
    case CSP_CCM_MOD_CLK_SDC2:
    case CSP_CCM_MOD_CLK_SDC3:
    case CSP_CCM_MOD_CLK_SS:
    case CSP_CCM_MOD_CLK_TS:
    case CSP_CCM_MOD_CLK_ATA:
        if (CSP_CCM_SYS_CLK_VIDEO_PLL0 == srcClk){
            srcBitsVal = 0U;
        }
        else if (CSP_CCM_SYS_CLK_SDRAM_PLL == srcClk){
            srcBitsVal = 2U;
        }
        else if (CSP_CCM_SYS_CLK_CORE_PLL == srcClk){
            srcBitsVal = 3U;
        }
        else goto errSrcClk;
        break;

    case CSP_CCM_MOD_CLK_DE_IMAGE1:
    case CSP_CCM_MOD_CLK_DE_IMAGE0:
    case CSP_CCM_MOD_CLK_DE_SCALE1:
    case CSP_CCM_MOD_CLK_DE_SCALE0:
    case CSP_CCM_MOD_CLK_CSI1:
    case CSP_CCM_MOD_CLK_CSI0:
        if (CSP_CCM_SYS_CLK_VIDEO_PLL0 == srcClk){
            srcBitsVal = 0U;
        }
        else if (CSP_CCM_SYS_CLK_VIDEO_PLL1 == srcClk){
            srcBitsVal = 1U;
        }
        else if (CSP_CCM_SYS_CLK_SDRAM_PLL == srcClk){
            srcBitsVal = 2U;
        }
        else goto errSrcClk;
        break;

    case CSP_CCM_MOD_CLK_DE_MIX:
        if (CSP_CCM_SYS_CLK_VIDEO_PLL0 == srcClk){
            srcBitsVal = 0U;
        }
        else if (CSP_CCM_SYS_CLK_VIDEO_PLL1 == srcClk){
            srcBitsVal = 1U;
        }
        else if (CSP_CCM_SYS_CLK_SDRAM_PLL == srcClk){
            srcBitsVal = 2U;
        }
        else goto errSrcClk;
        break;

    case  CSP_CCM_MOD_CLK_IR:
        if (CSP_CCM_SYS_CLK_VIDEO_PLL0 == srcClk){
            srcBitsVal = 0U;
        }
        else if (CSP_CCM_SYS_CLK_HOSC == srcClk){
            srcBitsVal = 1U;
        }
        else if (CSP_CCM_SYS_CLK_CORE_PLL == srcClk ){
            srcBitsVal = 3U;
        }
        else goto errSrcClk;
        SET_BITS(CCM_IR_CLK_R, 8, 2, srcBitsVal);
        break;

    case CSP_CCM_MOD_CLK_ACE:
        if (CSP_CCM_SYS_CLK_VIDEO_PLL0 == srcClk){
            srcBitsVal = 0;
        }
        else if (CSP_CCM_SYS_CLK_VE_PLL == srcClk){
            srcBitsVal = 1U;
        }
        else if (CSP_CCM_SYS_CLK_SDRAM_PLL == srcClk){
            srcBitsVal = 2U;
        }
        else if (CSP_CCM_SYS_CLK_CORE_PLL == srcClk){
            srcBitsVal = 3U;
        }
        else goto errSrcClk;
        break;

    case CSP_CCM_MOD_CLK_KEY_PAD:
        if (CSP_CCM_SYS_CLK_HOSC == srcClk){
            srcBitsVal = 1;
        }
        else if (CSP_CCM_SYS_CLK_LOSC == srcClk){
            srcBitsVal = 0;
        }
        else goto errSrcClk;
        break;

    case CSP_CCM_MOD_CLK_TVENC_1X:
    case CSP_CCM_MOD_CLK_TVENC_2X:
        if (CSP_CCM_SYS_CLK_TVENC_1 == srcClk){
            srcBitsVal = 1;
        }
        else if (CSP_CCM_SYS_CLK_TVENC_0 == srcClk){
            srcBitsVal = 0;
        }
        else goto errSrcClk;
        break;

    case CSP_CCM_MOD_CLK_TCON0_0:
    case CSP_CCM_MOD_CLK_TCON1_0:
    case CSP_CCM_MOD_CLK_LVDS:
        if (CSP_CCM_SYS_CLK_VIDEO_PLL1 == srcClk){
            srcBitsVal = 1;
        }
        else if (CSP_CCM_SYS_CLK_VIDEO_PLL0 == srcClk){
            srcBitsVal = 0;
        }
        else goto errSrcClk;
        break;

    default:
        goto errSrcClk;
    }

    regOfst     = _mod_clk_REG_OFST(clkNo);
    srcBitsPos  = _mod_clk_SRC_BITS_POS(clkNo);
    srcBitsLen  = _mod_clk_SRC_BITS_LEN(clkNo);

    CCM_inf("L[%d]set src clk: (regOfst, pos, len, val)(0x%x, %d, %d, %d).\n",
        __LINE__, regOfst, srcBitsPos, srcBitsLen, srcBitsVal);
    SET_BITS(CCM_REG(regOfst), srcBitsPos, srcBitsLen, srcBitsVal);

    return CSP_CCM_ERR_NONE;

errSrcClk:
    CCM_msg("error!source clk [%d] is not available for [%d]!\n", srcClk, clkNo);
    return CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE;
}



static CSP_CCM_sysClkNo_t
_mod_clk_get_src_clk_no( CSP_CCM_modClkNo_t clkNo )
{
	u8 bitPos = 0;
	u8 bitLen = 0;
	u8 bitsVal = 0;
	u32 regOfst = 0;

	if (CSP_CCM_MOD_CLK_AHB_USB0 <= clkNo && clkNo <= CSP_CCM_MOD_CLK_AHB_TCON1) {
		return CSP_CCM_SYS_CLK_AHB;
	}
	if (CSP_CCM_MOD_CLK_APB_KEY_PAD <= clkNo && clkNo <= CSP_CCM_MOD_CLK_APB_SMC) {
		return CSP_CCM_SYS_CLK_APB;
	}
	if (CSP_CCM_MOD_CLK_SDRAM_OUTPUT <= clkNo && clkNo <= CSP_CCM_MOD_CLK_SDRAM_COM_ENGINE){
		return CSP_CCM_SYS_CLK_SDRAM;
	}

	switch (clkNo) {
	case CSP_CCM_MOD_CLK_COM:
	case CSP_CCM_MOD_CLK_USB_PHY0://source must be HOSC
	case CSP_CCM_MOD_CLK_USB_PHY1://source must be HOSC
	case CSP_CCM_MOD_CLK_USB_PHY2://source must be HOSC
	case CSP_CCM_MOD_CLK_AVS://source must be HOSC
		return CSP_CCM_SYS_CLK_HOSC;

	case CSP_CCM_MOD_CLK_VE://must be VE PLL
		return CSP_CCM_SYS_CLK_VE_PLL;

	case CSP_CCM_MOD_CLK_AUDIO_CODEC://source must be audio PLL
	case CSP_CCM_MOD_CLK_AC97:
		return CSP_CCM_SYS_CLK_AUDIO_PLL;//if only one source, return error

	case CSP_CCM_MOD_CLK_SPDIF://source must be audio PLL
		return CSP_CCM_SYS_CLK_AUDIO_PLL_4X;

	case CSP_CCM_MOD_CLK_I2S://must be audio pll
		return CSP_CCM_SYS_CLK_AUDIO_PLL_8X;

	case CSP_CCM_MOD_CLK_TCON0_1:
		return CSP_CCM_SYS_CLK_TVENC_0;

	case CSP_CCM_MOD_CLK_TCON1_1:
		return CSP_CCM_SYS_CLK_TVENC_1;

	default:
		break;
	}

	bitPos = _mod_clk_SRC_BITS_POS(clkNo);
	if (255 == bitPos){
		CCM_msg("Exception: Get src clk fail for clock[%d]!\n", clkNo);
		return CSP_CCM_SYS_CLK_TOTAL_NUM;
	}
	bitLen = _mod_clk_SRC_BITS_LEN(clkNo);
	regOfst = _mod_clk_REG_OFST(clkNo);
	if (0 == regOfst){
		CCM_msg("Fail to get reg offset for clock[%d]!\n", clkNo);
		return CSP_CCM_SYS_CLK_TOTAL_NUM;
	}

	bitsVal = GET_BITS_VAL(CCM_REG(regOfst), bitPos, bitLen);


	switch (clkNo)
	{
		//Fist class: sourced from video PLL-0/SDRAM PLL/Core PLL
	case CSP_CCM_MOD_CLK_NFC:
	case CSP_CCM_MOD_CLK_MSC:
	case CSP_CCM_MOD_CLK_SDC0:
	case CSP_CCM_MOD_CLK_SDC1:
	case CSP_CCM_MOD_CLK_SDC2:
	case CSP_CCM_MOD_CLK_SDC3:
	case CSP_CCM_MOD_CLK_SS:
	case CSP_CCM_MOD_CLK_TS:
	case CSP_CCM_MOD_CLK_ATA:
		if (0 == bitsVal){
			return CSP_CCM_SYS_CLK_VIDEO_PLL0;
		}
		if (2U == bitsVal){
			return CSP_CCM_SYS_CLK_SDRAM_PLL;
		}
		if (3U == bitsVal){
			return CSP_CCM_SYS_CLK_CORE_PLL;
		}
		break;

//2nd class: sourced from video PLL-0/SDRAM PLL/Video PLL 1
	case CSP_CCM_MOD_CLK_DE_IMAGE1:
	case CSP_CCM_MOD_CLK_DE_IMAGE0:
	case CSP_CCM_MOD_CLK_DE_SCALE1:
	case CSP_CCM_MOD_CLK_DE_SCALE0:
	case CSP_CCM_MOD_CLK_CSI1:
	case CSP_CCM_MOD_CLK_CSI0:
	case CSP_CCM_MOD_CLK_DE_MIX:
		if (0U == bitsVal){
			return CSP_CCM_SYS_CLK_VIDEO_PLL0;
		}
		if (1U == bitsVal){
			return CSP_CCM_SYS_CLK_VIDEO_PLL1;
		}
		if (2U == bitsVal){
			return CSP_CCM_SYS_CLK_SDRAM_PLL;
		}
		break;

	case CSP_CCM_MOD_CLK_KEY_PAD:
		return bitsVal ? CSP_CCM_SYS_CLK_HOSC : CSP_CCM_SYS_CLK_LOSC;

		//fix me: TV encoder
	case CSP_CCM_MOD_CLK_TVENC_1X:
	case CSP_CCM_MOD_CLK_TVENC_2X:
		return bitsVal ? CSP_CCM_SYS_CLK_TVENC_1: CSP_CCM_SYS_CLK_TVENC_0;

	case CSP_CCM_MOD_CLK_TCON0_0:
	case CSP_CCM_MOD_CLK_TCON1_0:
	case CSP_CCM_MOD_CLK_LVDS:
		return bitsVal ? CSP_CCM_SYS_CLK_VIDEO_PLL1 : CSP_CCM_SYS_CLK_VIDEO_PLL0;
	case CSP_CCM_MOD_CLK_IR: // ir module
		if (0 == bitsVal){
			return CSP_CCM_SYS_CLK_VIDEO_PLL0;
		}
		if (1U == bitsVal){
			return CSP_CCM_SYS_CLK_HOSC;
		}
		if (3U == bitsVal){
			return CSP_CCM_SYS_CLK_CORE_PLL;
		}
		break;

	case CSP_CCM_MOD_CLK_ACE: // ace module
		if (0 == bitsVal){
			return CSP_CCM_SYS_CLK_VIDEO_PLL0;
		}
		if (1U == bitsVal){
			return CSP_CCM_SYS_CLK_VE_PLL;
		}
		if (2U == bitsVal){
			return CSP_CCM_SYS_CLK_SDRAM_PLL;
		}
		if (3U == bitsVal){
			return CSP_CCM_SYS_CLK_CORE_PLL;
		}
		break;

	default:
		break;
	}

	return CSP_CCM_SYS_CLK_TOTAL_NUM;
}

static CSP_CCM_err_t
_mod_clk_set_divide_ratio( CSP_CCM_modClkNo_t clkNo, const u32 divideRatio )
{
    u32 bitsRatio = 0;
    //CSP_CCM_sysClkNo_t srcClk = CSP_CCM_SYS_CLK_TOTAL_NUM;
    u32 bitPos = 0;

    if (1 == divideRatio){
        if (CSP_CCM_MOD_CLK_AHB_USB0 <= clkNo && clkNo <= CSP_CCM_MOD_CLK_AHB_TCON1){
                return CSP_CCM_ERR_NONE;
        }
        if (CSP_CCM_MOD_CLK_APB_KEY_PAD <= clkNo && clkNo <= CSP_CCM_MOD_CLK_APB_SMC){
                return CSP_CCM_ERR_NONE;
        }
        if (CSP_CCM_MOD_CLK_SDRAM_OUTPUT <= clkNo && clkNo <= CSP_CCM_MOD_CLK_SDRAM_COM_ENGINE){
                return CSP_CCM_ERR_NONE;
        }
    }

    switch (clkNo)
    {
    case CSP_CCM_MOD_CLK_VE:
    case CSP_CCM_MOD_CLK_AC97:
    case CSP_CCM_MOD_CLK_SPDIF:
    case CSP_CCM_MOD_CLK_AUDIO_CODEC:
    case CSP_CCM_MOD_CLK_USB_PHY0:
    case CSP_CCM_MOD_CLK_USB_PHY1:
    case CSP_CCM_MOD_CLK_USB_PHY2:
    case CSP_CCM_MOD_CLK_AVS:
        if ( 1 != divideRatio){
            return CSP_CCM_ERR_DIVIDE_RATIO;
        }
        break;

    case CSP_CCM_MOD_CLK_NFC://n+1 , n=1~15
        bitsRatio = divideRatio - 1;
        SET_BITS(CCM_NFC_MSC_CLK_R, 8, 4, bitsRatio);
        break;
    case CSP_CCM_MOD_CLK_MSC:
        SET_BITS(CCM_NFC_MSC_CLK_R, 0, 4, divideRatio - 1);
        break;

    case CSP_CCM_MOD_CLK_SDC0:
        SET_BITS(CCM_SDC01_CLK_R, 0, 4, divideRatio - 1);
        break;
    case CSP_CCM_MOD_CLK_SDC1:
        SET_BITS(CCM_SDC01_CLK_R, 8, 4, divideRatio -1);
        break;
    case CSP_CCM_MOD_CLK_SDC2:
        SET_BITS(CCM_SDC23_CLK_R, 0, 4, divideRatio -1);
        break;
    case CSP_CCM_MOD_CLK_SDC3:
        SET_BITS(CCM_SDC23_CLK_R, 8, 4, divideRatio - 1);
        break;

    case CSP_CCM_MOD_CLK_SS:
        SET_BITS(CCM_TS_SS_CLK_R, 8, 4, divideRatio - 1);
        break;
    case CSP_CCM_MOD_CLK_TS:
        SET_BITS(CCM_TS_SS_CLK_R, 0, 4, divideRatio - 1);
        break;

    case CSP_CCM_MOD_CLK_DE_IMAGE1:
        SET_BITS(CCM_DE_CLK_R, 24, 3, divideRatio - 1);
        break;
    case CSP_CCM_MOD_CLK_DE_IMAGE0:
        SET_BITS(CCM_DE_CLK_R, 16, 3, divideRatio - 1);
        break;

    case CSP_CCM_MOD_CLK_DE_SCALE1:
        SET_BITS(CCM_DE_CLK_R, 8, 3, divideRatio - 1);
        break;
    case CSP_CCM_MOD_CLK_DE_SCALE0:
        SET_BITS(CCM_DE_CLK_R, 0, 3, divideRatio - 1);
        break;


    case CSP_CCM_MOD_CLK_CSI1:
        SET_BITS(CCM_CSI_CLK_R, 8, 4, divideRatio - 1);
        break;
    case CSP_CCM_MOD_CLK_CSI0:
        SET_BITS(CCM_CSI_CLK_R, 0, 4, divideRatio - 1);
        break;

    case CSP_CCM_MOD_CLK_IR:
        SET_BITS(CCM_IR_CLK_R, 0, 6, divideRatio - 1);
        break;


    case CSP_CCM_MOD_CLK_I2S:
        if (2 == divideRatio || 4 == divideRatio || 8 == divideRatio){
            bitsRatio = divideRatio>>2;
        }
        else return CSP_CCM_ERR_DIVIDE_RATIO;
        SET_BITS(CCM_AUDIO_CLK_R, 8, 2, bitsRatio);
        break;

    case CSP_CCM_MOD_CLK_ACE:
        SET_BITS(CCM_AUDIO_CLK_R, 16, 3, divideRatio - 1);
        break;

    case CSP_CCM_MOD_CLK_ATA:
        SET_BITS(CCM_ATA_CLK_R, 0, 4, divideRatio - 1);
        break;

    case CSP_CCM_MOD_CLK_DE_MIX:
        SET_BITS(CCM_DE_MIX_CLK_R, 8, 3, divideRatio - 1);
        break;
    case CSP_CCM_MOD_CLK_KEY_PAD://///////////////////////////////
        if (1 == divideRatio || 64 == divideRatio || 128 == divideRatio){
            bitsRatio = divideRatio>>6;
        }
        else if (256 == divideRatio){
            bitsRatio = 3U;
        }
        else return CSP_CCM_ERR_DIVIDE_RATIO;
        SET_BITS(CCM_MISC_CLK_R, 8, 2, bitsRatio);
        break;

//From vito: There is spec error---
//        preScale from tve_clk0/1 should be controlled by TCON0_1&&TCON1_1
    case CSP_CCM_MOD_CLK_TVENC_1X://source from clk0 clk1
//         srcClk = _mod_clk_get_src_clk_no(clkNo);
//         if ( !(CSP_CCM_SYS_CLK_TVENC_0 == srcClk || CSP_CCM_SYS_CLK_TVENC_1 == srcClk )){
//             break;
//         }
//         bitPos = CSP_CCM_SYS_CLK_TVENC_1 == srcClk ? 14 : 6;
//         if (1 == divideRatio){
//             CLEAR_BIT(CCM_TVENC_CLK_R, bitPos);
//         }
//         else if (2 == divideRatio){
//             SET_BIT(CCM_TVENC_CLK_R, bitPos);//Set preScale !!!not real divide ratio
//         }
//         else return CSP_CCM_ERR_DIVIDE_RATIO;

    case CSP_CCM_MOD_CLK_TVENC_2X://source from clk0 clk1
        if (1 != divideRatio){
            return CSP_CCM_ERR_DIVIDE_RATIO;
        }
        break;

    case CSP_CCM_MOD_CLK_TCON0_0:
    case CSP_CCM_MOD_CLK_TCON1_0:
        break;

    case CSP_CCM_MOD_CLK_TCON0_1:
    case CSP_CCM_MOD_CLK_TCON1_1:
        bitPos = (CSP_CCM_MOD_CLK_TCON1_1 == clkNo) ? 14 : 6;
        if (1 == divideRatio){
            CLEAR_BIT(CCM_TVENC_CLK_R, bitPos);
        }
        else if (2 == divideRatio){
            SET_BIT(CCM_TVENC_CLK_R, bitPos);//Set preScale !!!not real divide ratio
        }
        else return CSP_CCM_ERR_DIVIDE_RATIO;
        break;

    default:
        return CSP_CCM_ERR_DIVIDE_RATIO;
    }

    return CSP_CCM_ERR_NONE;
}


static u32
_mod_clk_get_divide_ratio( CSP_CCM_modClkNo_t clkNo )
{
    u32 divideRatio = 1;

    switch (clkNo)
    {
    case CSP_CCM_MOD_CLK_NFC:
        divideRatio = GET_BITS_VAL(CCM_NFC_MSC_CLK_R, 8, 4) + 1;
        break;
    case CSP_CCM_MOD_CLK_MSC:
        divideRatio = GET_BITS_VAL(CCM_NFC_MSC_CLK_R, 0, 4) + 1;
        break;

    case CSP_CCM_MOD_CLK_SDC0:
        divideRatio = GET_BITS_VAL(CCM_SDC01_CLK_R, 0, 4) + 1;
        break;
    case CSP_CCM_MOD_CLK_SDC1:
        divideRatio = GET_BITS_VAL(CCM_SDC01_CLK_R, 8, 4) + 1;
        break;
    case CSP_CCM_MOD_CLK_SDC2:
        divideRatio = GET_BITS_VAL(CCM_SDC23_CLK_R, 0, 4) + 1;
        break;
    case CSP_CCM_MOD_CLK_SDC3:
        divideRatio = GET_BITS_VAL(CCM_SDC23_CLK_R, 8, 4) + 1;
        break;

    case CSP_CCM_MOD_CLK_SS:
        divideRatio = GET_BITS_VAL(CCM_TS_SS_CLK_R, 8, 4) + 1;
        break;
    case CSP_CCM_MOD_CLK_TS:
        divideRatio = GET_BITS_VAL(CCM_TS_SS_CLK_R, 0, 4) + 1;
        break;

    case CSP_CCM_MOD_CLK_DE_IMAGE1:
        divideRatio = GET_BITS_VAL(CCM_DE_CLK_R, 24, 3) + 1;
        break;
    case CSP_CCM_MOD_CLK_DE_IMAGE0:
        divideRatio = GET_BITS_VAL(CCM_DE_CLK_R, 16, 3) + 1;
        break;

    case CSP_CCM_MOD_CLK_DE_SCALE1:
        divideRatio = GET_BITS_VAL(CCM_DE_CLK_R, 8, 3) + 1;
        break;
    case CSP_CCM_MOD_CLK_DE_SCALE0:
        divideRatio = GET_BITS_VAL(CCM_DE_CLK_R, 0, 3) + 1;
        break;

    case CSP_CCM_MOD_CLK_VE:
        return 1U;/////////////////////////////////

    case CSP_CCM_MOD_CLK_CSI1:
        divideRatio = GET_BITS_VAL(CCM_CSI_CLK_R, 8, 4) + 1;
        break;
    case CSP_CCM_MOD_CLK_CSI0:
        divideRatio = GET_BITS_VAL(CCM_CSI_CLK_R, 0, 4) + 1;
        break;

    case CSP_CCM_MOD_CLK_IR:
        divideRatio = GET_BITS_VAL(CCM_IR_CLK_R, 0, 6) + 1;
        break;


    case CSP_CCM_MOD_CLK_I2S:
        divideRatio = 2<<(GET_BITS_VAL(CCM_AUDIO_CLK_R, 8, 2) + 1);
        break;
    case CSP_CCM_MOD_CLK_AC97:
    case CSP_CCM_MOD_CLK_SPDIF:
    case CSP_CCM_MOD_CLK_AUDIO_CODEC:
        divideRatio = 1;
        break;

    case CSP_CCM_MOD_CLK_ACE:
        divideRatio = GET_BITS_VAL(CCM_AUDIO_CLK_R, 16, 3) + 1;
        break;

    case CSP_CCM_MOD_CLK_USB_PHY0:
    case CSP_CCM_MOD_CLK_USB_PHY1:
    case CSP_CCM_MOD_CLK_USB_PHY2:
    case CSP_CCM_MOD_CLK_AVS:
        divideRatio = 1;
        break;

    case CSP_CCM_MOD_CLK_ATA:
        divideRatio = GET_BITS_VAL(CCM_ATA_CLK_R, 0, 4) + 1;
        break;
    case CSP_CCM_MOD_CLK_DE_MIX:
        divideRatio = GET_BITS_VAL(CCM_DE_MIX_CLK_R, 8, 3) + 1;
        break;
    case CSP_CCM_MOD_CLK_KEY_PAD:
        divideRatio = GET_BITS_VAL(CCM_MISC_CLK_R, 8, 2) + 1;
        divideRatio = !divideRatio ? 1 : ( 64 * 2<<( divideRatio - 1 ) );
        break;

    default:
        break;
    }

    return divideRatio;
}

/*********************************************************************
* Method	 :    		_get_special_clk_freq
* Description:
* Parameters :
	@CSP_CCM_clkNo_t clkNo
* Returns    :   u32
* Note       : source clocks of special clocks are oscillate and PLL
*********************************************************************/
u32 _mod_clk_get_rate( CSP_CCM_modClkNo_t clkNo )
{
    CSP_CCM_sysClkNo_t srcClk = CSP_CCM_SYS_CLK_TOTAL_NUM;
    u32 srcRate            = 0;
    u32 divideRatio = 1;
    u32 freq = 1;//in case of /0 exception

    if (CLK_STATUS_ON != _mod_clk_is_on(clkNo)){
        CCM_msg("Cann't get rate for the clk is Off!\n");
        return CSP_CCM_ERR_CLK_IS_OFF;
    }

    srcClk  = _mod_clk_get_src_clk_no(clkNo);/////////////////////////////////

    if (CSP_CCM_SYS_CLK_HOSC == srcClk){
        srcRate = _get_HOSC_rate();
    }
    else if (CSP_CCM_SYS_CLK_LOSC == srcClk){
        srcRate = _get_LOSC_rate();
    }
    else if (CSP_CCM_SYS_CLK_VIDEO_PLL1_2X >= srcClk){
        srcRate = _pll_get_freq(srcClk);
    }
    else return CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE;

    divideRatio = _mod_clk_get_divide_ratio(clkNo);

    freq = srcRate/divideRatio;

    return freq;
}

static CSP_CCM_err_t
_mod_clk_reset_ctl(CSP_CCM_modClkNo_t clkNo,s32 resetIsValid)
{
    CSP_CCM_err_t errNo = CSP_CCM_ERR_NONE;

    switch (clkNo)
    {
    case CSP_CCM_MOD_CLK_VE:
        (!resetIsValid )? SET_BIT(CCM_VE_CLK_R, 5) : CLEAR_BIT(CCM_VE_CLK_R, 5);
        break;

    case CSP_CCM_MOD_CLK_ACE:
        (!resetIsValid )? SET_BIT(CCM_AUDIO_CLK_R, 22) : CLEAR_BIT(CCM_AUDIO_CLK_R, 22);
        break;

    case CSP_CCM_MOD_CLK_USB_PHY0:
        resetIsValid ? CLEAR_BIT(CCM_AVS_USB_CLK_R, USB_PHY0_RST_INVAL_BIT)
                     : SET_BIT(CCM_AVS_USB_CLK_R, USB_PHY0_RST_INVAL_BIT);
        break;

    case CSP_CCM_MOD_CLK_USB_PHY1:
        resetIsValid ? CLEAR_BIT(CCM_AVS_USB_CLK_R, USB_PHY1_RST_INVAL_BIT)
                     : SET_BIT(CCM_AVS_USB_CLK_R, USB_PHY1_RST_INVAL_BIT);
        break;

    case CSP_CCM_MOD_CLK_USB_PHY2:
        resetIsValid ? CLEAR_BIT(CCM_AVS_USB_CLK_R, USB_PHY2_RST_INVAL_BIT)
                     : SET_BIT(CCM_AVS_USB_CLK_R, USB_PHY2_RST_INVAL_BIT);
        break;

    case CSP_CCM_MOD_CLK_COM:
        (!resetIsValid )? SET_BIT(CCM_MISC_CLK_R, 0) : CLEAR_BIT(CCM_MISC_CLK_R, 0);
        break;

    case CSP_CCM_MOD_CLK_DE_IMAGE1:
        (!resetIsValid) ? SET_BIT(CCM_MISC_CLK_R, DE_IMAGE1_RST_INVAL_BIT) : CLEAR_BIT(CCM_MISC_CLK_R, DE_IMAGE1_RST_INVAL_BIT);
        break;

    case CSP_CCM_MOD_CLK_DE_IMAGE0:
        (!resetIsValid) ? SET_BIT(CCM_MISC_CLK_R, DE_IMAGE0_RST_INVAL_BIT) : CLEAR_BIT(CCM_MISC_CLK_R, DE_IMAGE0_RST_INVAL_BIT);
        break;

    case CSP_CCM_MOD_CLK_DE_SCALE1:
        (!resetIsValid) ? SET_BIT(CCM_MISC_CLK_R, DE_SCALE1_RST_INVAL_BIT) : CLEAR_BIT(CCM_MISC_CLK_R, DE_SCALE1_RST_INVAL_BIT);
        break;

    case CSP_CCM_MOD_CLK_DE_SCALE0:
        (!resetIsValid) ? SET_BIT(CCM_MISC_CLK_R, DE_SCALE0_RST_INVAL_BIT) : CLEAR_BIT(CCM_MISC_CLK_R, DE_SCALE0_RST_INVAL_BIT);
        break;

    default:
        errNo = CSP_CCM_ERR_RESET_CONTROL_DENIED;//not supported
        CCM_msg("clock %s has not reset control fields!\n", _modClkName[clkNo]);
    }

    return errNo;
}

CSP_CCM_err_t CSP_CCM_mod_clk_reset_control(CSP_CCM_modClkNo_t clkNo,s32 resetIsValid)
{
    if (CSP_CCM_MOD_CLK_TOTAL_NUM <= clkNo){
        CCM_msg("clk number %d beyond range!\n", clkNo);
        return CSP_CCM_ERR_CLK_NO_INVALID;
    }
    return _mod_clk_reset_ctl(clkNo, resetIsValid);
}

/************************************************************************/
/* Special clock: In technical view, I define special clock is the clock which configured
 * and used for the only one corresponding module. i.e., not shared clock.
 * In this sense, CPU/AHB/APB/SDRAM/TVENCO/TVENC1 can also be treated as special clocks
 * for they use the same interface to configure as module clocks*/
/************************************************************************/
s32 _CCM_config_special_clk( const CSP_CCM_modClkPara_t* specClk )
{
    CSP_CCM_modClkNo_t clkNo    = CSP_CCM_MOD_CLK_TOTAL_NUM;
    CSP_CCM_err_t errNo = CSP_CCM_ERR_NONE;

    CCM_CHECK_NULL_PARA(specClk, CSP_CCM_ERR_PARA_NULL);
    clkNo = specClk->clkNo;
    if (clkNo >= CSP_CCM_MOD_CLK_TOTAL_NUM){
        CCM_msg("not supported module clock number %d\n", clkNo);
        return CSP_CCM_ERR_CLK_NUM_NOT_SUPPORTED;
    }

    CSP_CCM_set_mod_clk_status(specClk->clkNo, (s32)specClk->isOn);
    errNo = _mod_clk_set_src_clk(clkNo, specClk->srcClk);
    if (CSP_CCM_ERR_NONE != errNo){
        return errNo;
    }
    errNo = _mod_clk_set_divide_ratio(clkNo, specClk->divideRatio);
    //errNo = _mod_clk_reset_ctl(clkNo, specClk->resetIsValid);

    return errNo;
}



u32 CSP_CCM_get_mod_clk_freq(CSP_CCM_modClkNo_t modClkNo)
{
    if (modClkNo >= CSP_CCM_MOD_CLK_TOTAL_NUM){
        CCM_msg("Too Big module clock number!\n");
        return FREQ_0;
    }
    if (CSP_CCM_MOD_CLK_LVDS >= modClkNo){
        return _mod_clk_get_rate(modClkNo);
    }
    if (CSP_CCM_MOD_CLK_AHB_USB0 <= modClkNo && CSP_CCM_MOD_CLK_AHB_TCON1 >= modClkNo){
        return _aHb_get_rate();
    }
    if (CSP_CCM_MOD_CLK_APB_KEY_PAD <= modClkNo && CSP_CCM_MOD_CLK_APB_SMC >= modClkNo){
        return _aPb_get_rate();
    }
    if (CSP_CCM_MOD_CLK_SDRAM_DE_SCALE0 <= modClkNo && CSP_CCM_MOD_CLK_SDRAM_COM_ENGINE >= modClkNo){
        return _sdram_get_rate();
    }

    return FREQ_0;
}


s32 CSP_CCM_set_mod_clk_freq(const CSP_CCM_modClkPara_t* modClk)
{
    CCM_CHECK_NULL_PARA(modClk, AW_CCMU_FAIL);
    return _CCM_config_special_clk(modClk);
}


CSP_CCM_err_t CSP_CCM_get_mod_clk_info( CSP_CCM_modClkNo_t clkNo, CSP_CCM_modClkInfo_t* pInfo )
{
    int index = 0;

    CCM_CHECK_NULL_PARA(pInfo, CSP_CCM_ERR_NULL_PARA);
    if (clkNo >= CSP_CCM_MOD_CLK_TOTAL_NUM){
        CCM_msg("ClkNo[%d] is out of range!\n", clkNo);
        return CSP_CCM_ERR_PARA_VALUE;
    }
    index = (int)(clkNo);
    pInfo->pName        = _modClkName[index];
    pInfo->clkId        = clkNo;
    pInfo->srcClkId     = _mod_clk_get_src_clk_no(clkNo);
    pInfo->divideRatio  = _mod_clk_get_divide_ratio(clkNo);

    return CSP_CCM_ERR_NONE;
}
