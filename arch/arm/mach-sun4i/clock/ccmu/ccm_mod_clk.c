/*
 * arch/arm/mach-sun4i/clock/ccmu/ccm_mod_clk.c
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <mach/clock.h>
#include "ccm_i.h"

#define make_mod_clk_inf(clk_id, clk_name)  {.id = clk_id, .name = clk_name, }

static __aw_ccu_clk_t aw_ccu_mod_clk[] =
{
    make_mod_clk_inf(AW_MOD_CLK_NONE        , "mclk_none"   ),
    make_mod_clk_inf(AW_MOD_CLK_NFC         , "nfc"         ),
    make_mod_clk_inf(AW_MOD_CLK_MSC         , "msc"         ),
    make_mod_clk_inf(AW_MOD_CLK_SDC0        , "sdc0"        ),
    make_mod_clk_inf(AW_MOD_CLK_SDC1        , "sdc1"        ),
    make_mod_clk_inf(AW_MOD_CLK_SDC2        , "sdc2"        ),
    make_mod_clk_inf(AW_MOD_CLK_SDC3        , "sdc3"        ),
    make_mod_clk_inf(AW_MOD_CLK_TS          , "ts"          ),
    make_mod_clk_inf(AW_MOD_CLK_SS          , "ss"          ),
    make_mod_clk_inf(AW_MOD_CLK_SPI0        , "spi0"        ),
    make_mod_clk_inf(AW_MOD_CLK_SPI1        , "spi1"        ),
    make_mod_clk_inf(AW_MOD_CLK_SPI2        , "spi2"        ),
    make_mod_clk_inf(AW_MOD_CLK_PATA        , "pata"        ),
    make_mod_clk_inf(AW_MOD_CLK_IR0         , "ir0"         ),
    make_mod_clk_inf(AW_MOD_CLK_IR1         , "ir1"         ),
    make_mod_clk_inf(AW_MOD_CLK_I2S         , "i2s"         ),
    make_mod_clk_inf(AW_MOD_CLK_AC97        , "ac97"        ),
    make_mod_clk_inf(AW_MOD_CLK_SPDIF       , "spdif"       ),
    make_mod_clk_inf(AW_MOD_CLK_KEYPAD      , "key_pad"     ),
    make_mod_clk_inf(AW_MOD_CLK_SATA        , "sata"        ),
    make_mod_clk_inf(AW_MOD_CLK_USBPHY      , "usb_phy"     ),
    make_mod_clk_inf(AW_MOD_CLK_USBPHY0     , "usb_phy0"    ),
    make_mod_clk_inf(AW_MOD_CLK_USBPHY1     , "usb_phy1"    ),
    make_mod_clk_inf(AW_MOD_CLK_USBPHY2     , "usb_phy2"    ),
    make_mod_clk_inf(AW_MOD_CLK_USBOHCI0    , "usb_ohci0"   ),
    make_mod_clk_inf(AW_MOD_CLK_USBOHCI1    , "usb_ohci1"   ),
    make_mod_clk_inf(AW_MOD_CLK_GPS         , "com"         ),
    make_mod_clk_inf(AW_MOD_CLK_SPI3        , "spi3"        ),
    make_mod_clk_inf(AW_MOD_CLK_DEBE0       , "de_image0"   ),
    make_mod_clk_inf(AW_MOD_CLK_DEBE1       , "de_image1"   ),
    make_mod_clk_inf(AW_MOD_CLK_DEFE0       , "de_scale0"   ),
    make_mod_clk_inf(AW_MOD_CLK_DEFE1       , "de_scale1"   ),
    make_mod_clk_inf(AW_MOD_CLK_DEMIX       , "de_mix"      ),
    make_mod_clk_inf(AW_MOD_CLK_LCD0CH0     , "lcd0_ch0"    ),
    make_mod_clk_inf(AW_MOD_CLK_LCD1CH0     , "lcd1_ch0"    ),
    make_mod_clk_inf(AW_MOD_CLK_CSIISP      , "csi_isp"     ),
    make_mod_clk_inf(AW_MOD_CLK_TVD         , "tvd"         ),
    make_mod_clk_inf(AW_MOD_CLK_LCD0CH1_S1  , "lcd0_ch1_s1" ),
    make_mod_clk_inf(AW_MOD_CLK_LCD0CH1_S2  , "lcd0_ch1_s2" ),
    make_mod_clk_inf(AW_MOD_CLK_LCD1CH1_S1  , "lcd1_ch1_s1" ),
    make_mod_clk_inf(AW_MOD_CLK_LCD1CH1_S2  , "lcd1_ch1_s2" ),
    make_mod_clk_inf(AW_MOD_CLK_CSI0        , "csi0"        ),
    make_mod_clk_inf(AW_MOD_CLK_CSI1        , "csi1"        ),
    make_mod_clk_inf(AW_MOD_CLK_VE          , "ve"          ),
    make_mod_clk_inf(AW_MOD_CLK_ADDA        , "audio_codec" ),
    make_mod_clk_inf(AW_MOD_CLK_AVS         , "avs"         ),
    make_mod_clk_inf(AW_MOD_CLK_ACE         , "ace"         ),
    make_mod_clk_inf(AW_MOD_CLK_LVDS        , "lvds"        ),
    make_mod_clk_inf(AW_MOD_CLK_HDMI        , "hdmi"        ),
    make_mod_clk_inf(AW_MOD_CLK_MALI        , "mali"        ),
    make_mod_clk_inf(AW_MOD_CLK_TWI0        , "twi0"        ),
    make_mod_clk_inf(AW_MOD_CLK_TWI1        , "twi1"        ),
    make_mod_clk_inf(AW_MOD_CLK_TWI2        , "twi2"        ),
    make_mod_clk_inf(AW_MOD_CLK_CAN         , "can"         ),
    make_mod_clk_inf(AW_MOD_CLK_SCR         , "scr"         ),
    make_mod_clk_inf(AW_MOD_CLK_PS20        , "ps0"         ),
    make_mod_clk_inf(AW_MOD_CLK_PS21        , "ps1"         ),
    make_mod_clk_inf(AW_MOD_CLK_UART0       , "uart0"       ),
    make_mod_clk_inf(AW_MOD_CLK_UART1       , "uart1"       ),
    make_mod_clk_inf(AW_MOD_CLK_UART2       , "uart2"       ),
    make_mod_clk_inf(AW_MOD_CLK_UART3       , "uart3"       ),
    make_mod_clk_inf(AW_MOD_CLK_UART4       , "uart4"       ),
    make_mod_clk_inf(AW_MOD_CLK_UART5       , "uart5"       ),
    make_mod_clk_inf(AW_MOD_CLK_UART6       , "uart6"       ),
    make_mod_clk_inf(AW_MOD_CLK_UART7       , "uart7"       ),
    make_mod_clk_inf(AW_MOD_CLK_AXI_DRAM    , "axi_dram"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_USB0    , "ahb_usb0"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_EHCI0   , "ahb_ehci0"   ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_OHCI0   , "ahb_ohci0"   ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SS      , "ahb_ss"      ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_DMA     , "ahb_dma"     ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_BIST    , "ahb_bist"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SDMMC0  , "ahb_sdc0"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SDMMC1  , "ahb_sdc1"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SDMMC2  , "ahb_sdc2"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SDMMC3  , "ahb_sdc3"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_MS      , "ahb_msc"     ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_NAND    , "ahb_nfc"     ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SDRAM   , "ahb_sdramc"  ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_ACE     , "ahb_ace"     ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_EMAC    , "ahb_emac"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_TS      , "ahb_ts"      ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SPI0    , "ahb_spi0"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SPI1    , "ahb_spi1"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SPI2    , "ahb_spi2"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SPI3    , "ahb_spi3"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_PATA    , "ahb_pata"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_SATA    , "ahb_sata"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_GPS     , "ahb_com"     ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_VE      , "ahb_ve"      ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_TVD     , "ahb_tvd"     ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_TVE0    , "ahb_tve0"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_TVE1    , "ahb_tve1"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_LCD0    , "ahb_lcd0"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_LCD1    , "ahb_lcd1"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_CSI0    , "ahb_csi0"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_CSI1    , "ahb_csi1"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_HDMI    , "ahb_hdmi"    ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_DEBE0   , "ahb_de_image0"),
    make_mod_clk_inf(AW_MOD_CLK_AHB_DEBE1   , "ahb_de_image1"),
    make_mod_clk_inf(AW_MOD_CLK_AHB_DEFE0   , "ahb_de_scale0"),
    make_mod_clk_inf(AW_MOD_CLK_AHB_DEFE1   , "ahb_de_scale1"),
    make_mod_clk_inf(AW_MOD_CLK_AHB_MP      , "ahb_de_mix"  ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_MALI    , "ahb_mali"    ),
    make_mod_clk_inf(AW_MOD_CLK_APB_ADDA    , "apb_audio_codec"),
    make_mod_clk_inf(AW_MOD_CLK_APB_SPDIF   , "apb_spdif"   ),
    make_mod_clk_inf(AW_MOD_CLK_APB_AC97    , "apb_ac97"    ),
    make_mod_clk_inf(AW_MOD_CLK_APB_I2S     , "apb_i2s"     ),
    make_mod_clk_inf(AW_MOD_CLK_APB_PIO     , "apb_pio"     ),
    make_mod_clk_inf(AW_MOD_CLK_APB_IR0     , "apb_ir0"     ),
    make_mod_clk_inf(AW_MOD_CLK_APB_IR1     , "apb_ir1"     ),
    make_mod_clk_inf(AW_MOD_CLK_APB_KEYPAD  , "apb_key_pad" ),
    make_mod_clk_inf(AW_MOD_CLK_APB_TWI0    , "apb_twi0"    ),
    make_mod_clk_inf(AW_MOD_CLK_APB_TWI1    , "apb_twi1"    ),
    make_mod_clk_inf(AW_MOD_CLK_APB_TWI2    , "apb_twi2"    ),
    make_mod_clk_inf(AW_MOD_CLK_APB_CAN     , "apb_can"     ),
    make_mod_clk_inf(AW_MOD_CLK_APB_SCR     , "apb_scr"     ),
    make_mod_clk_inf(AW_MOD_CLK_APB_PS20    , "apb_ps0"     ),
    make_mod_clk_inf(AW_MOD_CLK_APB_PS21    , "apb_ps1"     ),
    make_mod_clk_inf(AW_MOD_CLK_APB_UART0   , "apb_uart0"   ),
    make_mod_clk_inf(AW_MOD_CLK_APB_UART1   , "apb_uart1"   ),
    make_mod_clk_inf(AW_MOD_CLK_APB_UART2   , "apb_uart2"   ),
    make_mod_clk_inf(AW_MOD_CLK_APB_UART3   , "apb_uart3"   ),
    make_mod_clk_inf(AW_MOD_CLK_APB_UART4   , "apb_uart4"   ),
    make_mod_clk_inf(AW_MOD_CLK_APB_UART5   , "apb_uart5"   ),
    make_mod_clk_inf(AW_MOD_CLK_APB_UART6   , "apb_uart6"   ),
    make_mod_clk_inf(AW_MOD_CLK_APB_UART7   , "apb_uart7"   ),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_VE    , "sdram_ve"    ),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_CSI0  , "sdram_csi0"  ),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_CSI1  , "sdram_csi1"  ),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_TS    , "sdram_ts"    ),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_TVD   , "sdram_tvd"   ),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_TVE0  , "sdram_tve0"  ),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_TVE1  , "sdram_tve1"  ),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_DEFE0 , "sdram_de_scale0"),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_DEFE1 , "sdram_de_scale1"),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_DEBE0 , "sdram_de_image0"),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_DEBE1 , "sdram_de_image1"),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_DEMP  , "sdram_de_mix"),
    make_mod_clk_inf(AW_MOD_CLK_SDRAM_ACE   , "sdram_ace"   ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_EHCI1   , "ahb_ehci1"   ),
    make_mod_clk_inf(AW_MOD_CLK_AHB_OHCI1   , "ahb_ohci1"   ),

};


static __aw_ccu_sys_clk_e mod_clk_get_parent(__aw_ccu_mod_clk_e id);
static __aw_ccu_clk_onff_e mod_clk_get_status(__aw_ccu_mod_clk_e id);
static __s64 mod_clk_get_rate(__aw_ccu_mod_clk_e id);
static __aw_ccu_clk_reset_e mod_clk_get_reset(__aw_ccu_mod_clk_e id);

static __s32 mod_clk_set_parent(__aw_ccu_mod_clk_e id, __aw_ccu_sys_clk_e parent);
static __s32 mod_clk_set_status(__aw_ccu_mod_clk_e id, __aw_ccu_clk_onff_e status);
static __s32 mod_clk_set_rate(__aw_ccu_mod_clk_e id, __s64 rate);
static __s32 mod_clk_set_reset(__aw_ccu_mod_clk_e id, __aw_ccu_clk_reset_e reset);


static inline __aw_ccu_sys_clk_e _parse_module0_clk_src(volatile __ccmu_module0_clk_t *reg)
{
    switch(reg->ClkSrc)
    {
        case 0:
            return AW_SYS_CLK_HOSC;
        case 1:
            return AW_SYS_CLK_PLL62;
        case 2:
            return AW_SYS_CLK_PLL5P;
        default:
            return AW_SYS_CLK_NONE;
    }
    return AW_SYS_CLK_NONE;
}


static inline __aw_ccu_sys_clk_e _parse_defemp_clk_src(volatile __ccmu_fedemp_clk_t *reg)
{
    switch(reg->ClkSrc)
    {
        case 0:
            return AW_SYS_CLK_PLL3;
        case 1:
            return AW_SYS_CLK_PLL7;
        case 2:
            return AW_SYS_CLK_PLL5P;
        default:
            return AW_SYS_CLK_NONE;
    }
    return AW_SYS_CLK_NONE;
}


static inline __s32 _set_module0_clk_src(volatile __ccmu_module0_clk_t *reg, __aw_ccu_sys_clk_e parent)
{
    switch(parent)
    {
        case AW_SYS_CLK_HOSC:
            reg->ClkSrc = 0;
            break;
        case AW_SYS_CLK_PLL62:
            reg->ClkSrc = 1;
            break;
        case AW_SYS_CLK_PLL5P:
            reg->ClkSrc = 2;
            break;
        default:
            return -1;
    }

    return 0;
}


static inline __s32 _set_defemp_clk_src(volatile __ccmu_fedemp_clk_t *reg, __aw_ccu_sys_clk_e parent)
{
    switch(parent)
    {
        case AW_SYS_CLK_PLL3:
            reg->ClkSrc = 0;
            break;
        case AW_SYS_CLK_PLL7:
            reg->ClkSrc = 1;
            break;
        case AW_SYS_CLK_PLL5P:
            reg->ClkSrc = 2;
            break;
        default:
            return -1;
    }

    return 0;
}


static inline __aw_ccu_clk_onff_e _get_module0_clk_status(volatile __ccmu_module0_clk_t *reg)
{
    return reg->SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
}


static inline __s32 _set_module0_clk_status(volatile __ccmu_module0_clk_t *reg, __aw_ccu_clk_onff_e status)
{
    reg->SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
    return 0;
}


static inline __aw_ccu_clk_onff_e _get_defemp_clk_status(volatile __ccmu_fedemp_clk_t *reg)
{
    return reg->SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
}


static inline __s32 _set_defemp_clk_status(volatile __ccmu_fedemp_clk_t *reg, __aw_ccu_clk_onff_e status)
{
    reg->SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
    return 0;
}


static inline __u32 _get_module0_clk_rate(volatile __ccmu_module0_clk_t *reg)
{
    return (1<<reg->ClkPreDiv) * (reg->ClkDiv+1);
}


static inline __s32 _set_module0_clk_rate(volatile __ccmu_module0_clk_t *reg, __u64 rate)
{
    if(rate > 16*8)
    {
        return -1;
    }
    else if(rate > 16*4)
    {
        reg->ClkPreDiv = 3;
        reg->ClkDiv    = (rate>>3)-1;
    }
    else if(rate > 16*2)
    {
        reg->ClkPreDiv = 2;
        reg->ClkDiv    = (rate>>2)-1;
    }
    else if(rate > 16*1)
    {
       reg->ClkPreDiv = 1;
        reg->ClkDiv    = (rate>>1)-1;
    }
    else if(rate > 0)
    {
        reg->ClkPreDiv = 0;
        reg->ClkDiv    = rate-1;
    }
    else
    {
        return -1;
    }

    return 0;
}



static __aw_ccu_sys_clk_e mod_clk_get_parent(__aw_ccu_mod_clk_e id)
{
    switch(id)
    {
        case AW_MOD_CLK_NFC:
            return _parse_module0_clk_src(&aw_ccu_reg->NandClk);
        case AW_MOD_CLK_MSC:
            return _parse_module0_clk_src(&aw_ccu_reg->MsClk);
        case AW_MOD_CLK_SDC0:
            return _parse_module0_clk_src(&aw_ccu_reg->SdMmc0Clk);
        case AW_MOD_CLK_SDC1:
            return _parse_module0_clk_src(&aw_ccu_reg->SdMmc1Clk);
        case AW_MOD_CLK_SDC2:
            return _parse_module0_clk_src(&aw_ccu_reg->SdMmc2Clk);
        case AW_MOD_CLK_SDC3:
            return _parse_module0_clk_src(&aw_ccu_reg->SdMmc3Clk);
        case AW_MOD_CLK_TS:
            return _parse_module0_clk_src(&aw_ccu_reg->TsClk);
        case AW_MOD_CLK_SS:
            return _parse_module0_clk_src(&aw_ccu_reg->SsClk);
        case AW_MOD_CLK_SPI0:
            return _parse_module0_clk_src(&aw_ccu_reg->Spi0Clk);
        case AW_MOD_CLK_SPI1:
            return _parse_module0_clk_src(&aw_ccu_reg->Spi1Clk);
        case AW_MOD_CLK_SPI2:
            return _parse_module0_clk_src(&aw_ccu_reg->Spi2Clk);
        case AW_MOD_CLK_PATA:
            return _parse_module0_clk_src(&aw_ccu_reg->PataClk);
        case AW_MOD_CLK_IR0:
            return _parse_module0_clk_src(&aw_ccu_reg->Ir0Clk);
        case AW_MOD_CLK_IR1:
            return _parse_module0_clk_src(&aw_ccu_reg->Ir1Clk);
        case AW_MOD_CLK_I2S:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_AC97:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_SPDIF:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_KEYPAD:
        {
            switch(aw_ccu_reg->KeyPadClk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_HOSC;
                case 2:
                    return AW_SYS_CLK_LOSC;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_SATA:
        {
            switch(aw_ccu_reg->SataClk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_PLL6M;
                default:
                    return AW_SYS_CLK_NONE;
            }
            break;
        }
        case AW_MOD_CLK_USBPHY:
        case AW_MOD_CLK_USBPHY0:
        case AW_MOD_CLK_USBPHY1:
        case AW_MOD_CLK_USBPHY2:
        case AW_MOD_CLK_USBOHCI0:
        case AW_MOD_CLK_USBOHCI1:
            return AW_SYS_CLK_PLL62;
        case AW_MOD_CLK_GPS:
            return AW_SYS_CLK_AHB;
        case AW_MOD_CLK_SPI3:
            return _parse_module0_clk_src(&aw_ccu_reg->Spi3Clk);
        case AW_MOD_CLK_DEBE0:
            return _parse_defemp_clk_src(&aw_ccu_reg->DeBe0Clk);
        case AW_MOD_CLK_DEBE1:
            return _parse_defemp_clk_src(&aw_ccu_reg->DeBe1Clk);
        case AW_MOD_CLK_DEFE0:
            return _parse_defemp_clk_src(&aw_ccu_reg->DeFe0Clk);
        case AW_MOD_CLK_DEFE1:
            return _parse_defemp_clk_src(&aw_ccu_reg->DeFe1Clk);
        case AW_MOD_CLK_DEMIX:
            return _parse_defemp_clk_src(&aw_ccu_reg->DeMpClk);
        case AW_MOD_CLK_LCD0CH0:
        {
            switch(aw_ccu_reg->Lcd0Ch0Clk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL7;
                case 2:
                    return AW_SYS_CLK_PLL3X2;
                case 3:
                    return AW_SYS_CLK_PLL7X2;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_LCD1CH0:
        {
            switch(aw_ccu_reg->Lcd1Ch0Clk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL7;
                case 2:
                    return AW_SYS_CLK_PLL3X2;
                case 3:
                    return AW_SYS_CLK_PLL7X2;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_CSIISP:
        {
            switch(aw_ccu_reg->CsiIspClk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL4;
                case 2:
                    return AW_SYS_CLK_PLL5P;
                default:
                    return AW_SYS_CLK_PLL62;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_TVD:
            return aw_ccu_reg->TvdClk.ClkSrc? AW_SYS_CLK_PLL7 : AW_SYS_CLK_PLL3;
        case AW_MOD_CLK_LCD0CH1_S1:
        case AW_MOD_CLK_LCD0CH1_S2:
        {
            switch(aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Src)
            {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL7;
                case 2:
                    return AW_SYS_CLK_PLL3X2;
                case 3:
                    return AW_SYS_CLK_PLL7X2;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_LCD1CH1_S1:
        case AW_MOD_CLK_LCD1CH1_S2:
        {
            switch(aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Src)
            {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL7;
                case 2:
                    return AW_SYS_CLK_PLL3X2;
                case 3:
                    return AW_SYS_CLK_PLL7X2;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_CSI0:
        {
            switch(aw_ccu_reg->Csi0Clk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_HOSC;
                case 1:
                    return AW_SYS_CLK_PLL3;
                case 2:
                    return AW_SYS_CLK_PLL7;
                case 5:
                    return AW_SYS_CLK_PLL3X2;
                case 6:
                    return AW_SYS_CLK_PLL7X2;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_CSI1:
        {
            switch(aw_ccu_reg->Csi1Clk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_HOSC;
                case 1:
                    return AW_SYS_CLK_PLL3;
                case 2:
                    return AW_SYS_CLK_PLL7;
                case 5:
                    return AW_SYS_CLK_PLL3X2;
                case 6:
                    return AW_SYS_CLK_PLL7X2;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_VE:
            return AW_SYS_CLK_PLL4;
        case AW_MOD_CLK_ADDA:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_AVS:
            return AW_SYS_CLK_HOSC;
        case AW_MOD_CLK_ACE:
            return aw_ccu_reg->AceClk.ClkSrc? AW_SYS_CLK_PLL5P : AW_SYS_CLK_PLL4;
        case AW_MOD_CLK_LVDS:
            return AW_SYS_CLK_NONE;
        case AW_MOD_CLK_HDMI:
        {
            switch(aw_ccu_reg->HdmiClk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL7;
                case 2:
                    return AW_SYS_CLK_PLL3X2;
                case 3:
                    return AW_SYS_CLK_PLL7X2;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_MALI:
        {
            switch(aw_ccu_reg->MaliClk.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL4;
                case 2:
                    return AW_SYS_CLK_PLL5P;
                default:
                    return AW_SYS_CLK_PLL7;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_TWI0:
        case AW_MOD_CLK_TWI1:
        case AW_MOD_CLK_TWI2:
        case AW_MOD_CLK_CAN:
        case AW_MOD_CLK_SCR:
        case AW_MOD_CLK_PS20:
        case AW_MOD_CLK_PS21:
        case AW_MOD_CLK_UART0:
        case AW_MOD_CLK_UART1:
        case AW_MOD_CLK_UART2:
        case AW_MOD_CLK_UART3:
        case AW_MOD_CLK_UART4:
        case AW_MOD_CLK_UART5:
        case AW_MOD_CLK_UART6:
        case AW_MOD_CLK_UART7:
            return AW_SYS_CLK_APB1;

        default:
            return AW_SYS_CLK_NONE;
    }
}


/*
*********************************************************************************************************
*                           mod_clk_get_status
*
*Description: get module clock on/off status;
*
*Arguments  : id    module clock id;
*
*Return     : result;
*               AW_CCU_CLK_OFF, module clock is off;
*               AW_CCU_CLK_ON,  module clock is on;
*
*Notes      :
*
*********************************************************************************************************
*/
static __aw_ccu_clk_onff_e mod_clk_get_status(__aw_ccu_mod_clk_e id)
{
    switch(id)
    {
        case AW_MOD_CLK_NFC:
            return _get_module0_clk_status(&aw_ccu_reg->NandClk);
        case AW_MOD_CLK_MSC:
            return _get_module0_clk_status(&aw_ccu_reg->MsClk);
        case AW_MOD_CLK_SDC0:
            return _get_module0_clk_status(&aw_ccu_reg->SdMmc0Clk);
        case AW_MOD_CLK_SDC1:
            return _get_module0_clk_status(&aw_ccu_reg->SdMmc1Clk);
        case AW_MOD_CLK_SDC2:
            return _get_module0_clk_status(&aw_ccu_reg->SdMmc2Clk);
        case AW_MOD_CLK_SDC3:
            return _get_module0_clk_status(&aw_ccu_reg->SdMmc3Clk);
        case AW_MOD_CLK_TS:
            return _get_module0_clk_status(&aw_ccu_reg->TsClk);
        case AW_MOD_CLK_SS:
            return _get_module0_clk_status(&aw_ccu_reg->SsClk);
        case AW_MOD_CLK_SPI0:
            return _get_module0_clk_status(&aw_ccu_reg->Spi0Clk);
        case AW_MOD_CLK_SPI1:
            return _get_module0_clk_status(&aw_ccu_reg->Spi1Clk);
        case AW_MOD_CLK_SPI2:
            return _get_module0_clk_status(&aw_ccu_reg->Spi2Clk);
        case AW_MOD_CLK_PATA:
            return _get_module0_clk_status(&aw_ccu_reg->PataClk);
        case AW_MOD_CLK_IR0:
            return _get_module0_clk_status(&aw_ccu_reg->Ir0Clk);
        case AW_MOD_CLK_IR1:
            return _get_module0_clk_status(&aw_ccu_reg->Ir1Clk);
        case AW_MOD_CLK_I2S:
            return aw_ccu_reg->I2sClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AC97:
            return aw_ccu_reg->Ac97Clk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SPDIF:
            return aw_ccu_reg->SpdifClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_KEYPAD:
            return aw_ccu_reg->KeyPadClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SATA:
            return aw_ccu_reg->SataClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBPHY:
            if (SUNXI_VER_A10C == sw_get_ic_ver())
                aw_ccu_reg->UsbClk.ClkSwich = 1;
            return aw_ccu_reg->UsbClk.PhySpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBPHY0:
            return aw_ccu_reg->UsbClk.UsbPhy0Rst? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBPHY1:
            return aw_ccu_reg->UsbClk.UsbPhy1Rst? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBPHY2:
            return aw_ccu_reg->UsbClk.UsbPhy2Rst? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBOHCI0:
            return aw_ccu_reg->UsbClk.OHCI0SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBOHCI1:
            return aw_ccu_reg->UsbClk.OHCI0SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_GPS:
            return aw_ccu_reg->GpsClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SPI3:
            return _get_module0_clk_status(&aw_ccu_reg->Spi3Clk);
        case AW_MOD_CLK_DEBE0:
            return _get_defemp_clk_status(&aw_ccu_reg->DeBe0Clk);
        case AW_MOD_CLK_DEBE1:
            return _get_defemp_clk_status(&aw_ccu_reg->DeBe1Clk);
        case AW_MOD_CLK_DEFE0:
            return _get_defemp_clk_status(&aw_ccu_reg->DeFe0Clk);
        case AW_MOD_CLK_DEFE1:
            return _get_defemp_clk_status(&aw_ccu_reg->DeFe1Clk);
        case AW_MOD_CLK_DEMIX:
            return _get_defemp_clk_status(&aw_ccu_reg->DeMpClk);
        case AW_MOD_CLK_LCD0CH0:
            return aw_ccu_reg->Lcd0Ch0Clk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD1CH0:
            return aw_ccu_reg->Lcd1Ch0Clk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_CSIISP:
            return aw_ccu_reg->CsiIspClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_TVD:
            return aw_ccu_reg->TvdClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD0CH1_S1:
            return aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD0CH1_S2:
            return aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD1CH1_S1:
            return aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD1CH1_S2:
            return aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_CSI0:
            return aw_ccu_reg->Csi0Clk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_CSI1:
            return aw_ccu_reg->Csi1Clk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_VE:
            return aw_ccu_reg->VeClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_ADDA:
            return aw_ccu_reg->AddaClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AVS:
            return aw_ccu_reg->AvsClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_ACE:
            return aw_ccu_reg->AceClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LVDS:
            return AW_CCU_CLK_ON;
        case AW_MOD_CLK_HDMI:
            return aw_ccu_reg->HdmiClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_MALI:
            return aw_ccu_reg->MaliClk.SpecClkGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;

        case AW_MOD_CLK_TWI0:
        case AW_MOD_CLK_TWI1:
        case AW_MOD_CLK_TWI2:
        case AW_MOD_CLK_CAN:
        case AW_MOD_CLK_SCR:
        case AW_MOD_CLK_PS20:
        case AW_MOD_CLK_PS21:
        case AW_MOD_CLK_UART0:
        case AW_MOD_CLK_UART1:
        case AW_MOD_CLK_UART2:
        case AW_MOD_CLK_UART3:
        case AW_MOD_CLK_UART4:
        case AW_MOD_CLK_UART5:
        case AW_MOD_CLK_UART6:
        case AW_MOD_CLK_UART7:
            return AW_CCU_CLK_ON;

        case AW_MOD_CLK_AXI_DRAM:
            return aw_ccu_reg->AxiGate.SdramGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_USB0:
            return aw_ccu_reg->AhbGate0.Usb0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_EHCI0:
            return aw_ccu_reg->AhbGate0.Ehci0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_OHCI0:
            return aw_ccu_reg->AhbGate0.Ohci0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_EHCI1:
            return aw_ccu_reg->AhbGate0.Ehci1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_OHCI1:
            return aw_ccu_reg->AhbGate0.Ohci1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SS:
            return aw_ccu_reg->AhbGate0.SsGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_DMA:
            return aw_ccu_reg->AhbGate0.DmaGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_BIST:
            return aw_ccu_reg->AhbGate0.BistGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SDMMC0:
            return aw_ccu_reg->AhbGate0.Sdmmc0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SDMMC1:
            return aw_ccu_reg->AhbGate0.Sdmmc1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SDMMC2:
            return aw_ccu_reg->AhbGate0.Sdmmc2Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SDMMC3:
            return aw_ccu_reg->AhbGate0.Sdmmc3Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_MS:
            return aw_ccu_reg->AhbGate0.MsGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_NAND:
            return aw_ccu_reg->AhbGate0.NandGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SDRAM:
            return aw_ccu_reg->AhbGate0.SdramGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_ACE:
            return aw_ccu_reg->AhbGate0.AceGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_EMAC:
            return aw_ccu_reg->AhbGate0.EmacGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_TS:
            return aw_ccu_reg->AhbGate0.TsGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SPI0:
            return aw_ccu_reg->AhbGate0.Spi0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SPI1:
            return aw_ccu_reg->AhbGate0.Spi1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SPI2:
            return aw_ccu_reg->AhbGate0.Spi2Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SPI3:
            return aw_ccu_reg->AhbGate0.Spi3Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_PATA:
            return aw_ccu_reg->AhbGate0.PataGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_SATA:
            return aw_ccu_reg->AhbGate0.SataGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_GPS:
            return aw_ccu_reg->AhbGate0.GpsGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_VE:
            return aw_ccu_reg->AhbGate1.VeGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_TVD:
            return aw_ccu_reg->AhbGate1.TvdGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_TVE0:
            return aw_ccu_reg->AhbGate1.Tve0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_TVE1:
            return aw_ccu_reg->AhbGate1.Tve1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_LCD0:
            return aw_ccu_reg->AhbGate1.Lcd0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_LCD1:
            return aw_ccu_reg->AhbGate1.Lcd1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_CSI0:
            return aw_ccu_reg->AhbGate1.Csi0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_CSI1:
            return aw_ccu_reg->AhbGate1.Csi1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_HDMI:
            return aw_ccu_reg->AhbGate1.HdmiDGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_DEBE0:
            return aw_ccu_reg->AhbGate1.DeBe0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_DEBE1:
            return aw_ccu_reg->AhbGate1.DeBe1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_DEFE0:
            return aw_ccu_reg->AhbGate1.DeFe0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_DEFE1:
            return aw_ccu_reg->AhbGate1.DeFe1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_MP:
            return aw_ccu_reg->AhbGate1.MpGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AHB_MALI:
            return aw_ccu_reg->AhbGate1.Gpu3DGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_ADDA:
            return aw_ccu_reg->Apb0Gate.AddaGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_SPDIF:
            return aw_ccu_reg->Apb0Gate.SpdifGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_AC97:
            return aw_ccu_reg->Apb0Gate.Ac97Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_I2S:
            return aw_ccu_reg->Apb0Gate.IisGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_PIO:
            return aw_ccu_reg->Apb0Gate.PioGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_IR0:
            return aw_ccu_reg->Apb0Gate.Ir0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_IR1:
            return aw_ccu_reg->Apb0Gate.Ir1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_KEYPAD:
            return aw_ccu_reg->Apb0Gate.KeypadGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_TWI0:
            return aw_ccu_reg->Apb1Gate.Twi0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_TWI1:
            return aw_ccu_reg->Apb1Gate.Twi1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_TWI2:
            return aw_ccu_reg->Apb1Gate.Twi2Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_CAN:
            return aw_ccu_reg->Apb1Gate.CanGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_SCR:
            return aw_ccu_reg->Apb1Gate.ScrGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_PS20:
            return aw_ccu_reg->Apb1Gate.Ps20Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_PS21:
            return aw_ccu_reg->Apb1Gate.Ps21Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_UART0:
            return aw_ccu_reg->Apb1Gate.Uart0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_UART1:
            return aw_ccu_reg->Apb1Gate.Uart1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_UART2:
            return aw_ccu_reg->Apb1Gate.Uart2Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_UART3:
            return aw_ccu_reg->Apb1Gate.Uart3Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_UART4:
            return aw_ccu_reg->Apb1Gate.Uart4Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_UART5:
            return aw_ccu_reg->Apb1Gate.Uart5Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_UART6:
            return aw_ccu_reg->Apb1Gate.Uart6Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_APB_UART7:
            return aw_ccu_reg->Apb1Gate.Uart7Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_VE:
            return aw_ccu_reg->DramGate.VeGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_CSI0:
            return aw_ccu_reg->DramGate.Csi0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_CSI1:
            return aw_ccu_reg->DramGate.Csi1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_TS:
            return aw_ccu_reg->DramGate.TsGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_TVD:
            return aw_ccu_reg->DramGate.TvdGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_TVE0:
            return aw_ccu_reg->DramGate.Tve0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_TVE1:
            return aw_ccu_reg->DramGate.Tve1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_DEFE0:
            return aw_ccu_reg->DramGate.DeFe0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_DEFE1:
            return aw_ccu_reg->DramGate.DeFe1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_DEBE0:
            return aw_ccu_reg->DramGate.DeBe0Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_DEBE1:
            return aw_ccu_reg->DramGate.DeBe1Gate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_DEMP:
            return aw_ccu_reg->DramGate.DeMpGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SDRAM_ACE:
            return aw_ccu_reg->DramGate.AceGate? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        default:
            return AW_CCU_CLK_ON;
    }
    return AW_CCU_CLK_ON;
}


/*
*********************************************************************************************************
*                           mod_clk_get_rate
*
*Description: get module clock rate;
*
*Arguments  : id    module clock id;
*
*Return     : module clock division;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s64 mod_clk_get_rate(__aw_ccu_mod_clk_e id)
{
    switch(id)
    {
        case AW_MOD_CLK_NFC:
            return _get_module0_clk_rate(&aw_ccu_reg->NandClk);
        case AW_MOD_CLK_MSC:
            return _get_module0_clk_rate(&aw_ccu_reg->MsClk);
        case AW_MOD_CLK_SDC0:
            return _get_module0_clk_rate(&aw_ccu_reg->SdMmc0Clk);
        case AW_MOD_CLK_SDC1:
            return _get_module0_clk_rate(&aw_ccu_reg->SdMmc1Clk);
        case AW_MOD_CLK_SDC2:
            return _get_module0_clk_rate(&aw_ccu_reg->SdMmc2Clk);
        case AW_MOD_CLK_SDC3:
            return _get_module0_clk_rate(&aw_ccu_reg->SdMmc3Clk);
        case AW_MOD_CLK_TS:
            return _get_module0_clk_rate(&aw_ccu_reg->TsClk);
        case AW_MOD_CLK_SS:
            return _get_module0_clk_rate(&aw_ccu_reg->SsClk);
        case AW_MOD_CLK_SPI0:
            return _get_module0_clk_rate(&aw_ccu_reg->Spi0Clk);
        case AW_MOD_CLK_SPI1:
            return _get_module0_clk_rate(&aw_ccu_reg->Spi1Clk);
        case AW_MOD_CLK_SPI2:
            return _get_module0_clk_rate(&aw_ccu_reg->Spi2Clk);
        case AW_MOD_CLK_PATA:
            return _get_module0_clk_rate(&aw_ccu_reg->PataClk);
        case AW_MOD_CLK_IR0:
            return _get_module0_clk_rate(&aw_ccu_reg->Ir0Clk);
        case AW_MOD_CLK_IR1:
            return _get_module0_clk_rate(&aw_ccu_reg->Ir1Clk);
        case AW_MOD_CLK_I2S:
            return (1 << aw_ccu_reg->I2sClk.ClkDiv);
        case AW_MOD_CLK_AC97:
            return (1 << aw_ccu_reg->Ac97Clk.ClkDiv);
        case AW_MOD_CLK_SPDIF:
            return (1 << aw_ccu_reg->SpdifClk.ClkDiv);
        case AW_MOD_CLK_KEYPAD:
            return (1 << aw_ccu_reg->KeyPadClk.ClkPreDiv) * (aw_ccu_reg->KeyPadClk.ClkDiv + 1);
        case AW_MOD_CLK_SATA:
        case AW_MOD_CLK_USBPHY:
        case AW_MOD_CLK_USBPHY0:
        case AW_MOD_CLK_USBPHY1:
        case AW_MOD_CLK_USBPHY2:
        case AW_MOD_CLK_USBOHCI0:
        case AW_MOD_CLK_USBOHCI1:
        case AW_MOD_CLK_GPS:
            return 1;
        case AW_MOD_CLK_SPI3:
            return _get_module0_clk_rate(&aw_ccu_reg->Spi3Clk);
        case AW_MOD_CLK_DEBE0:
            return aw_ccu_reg->DeBe0Clk.ClkDiv + 1;
        case AW_MOD_CLK_DEBE1:
            return aw_ccu_reg->DeBe1Clk.ClkDiv + 1;
        case AW_MOD_CLK_DEFE0:
            return aw_ccu_reg->DeFe0Clk.ClkDiv + 1;
        case AW_MOD_CLK_DEFE1:
            return aw_ccu_reg->DeFe1Clk.ClkDiv + 1;
        case AW_MOD_CLK_DEMIX:
            return aw_ccu_reg->DeMpClk.ClkDiv + 1;
        case AW_MOD_CLK_LCD0CH0:
        case AW_MOD_CLK_LCD1CH0:
            return 1;
        case AW_MOD_CLK_CSIISP:
            return aw_ccu_reg->CsiIspClk.ClkDiv + 1;
        case AW_MOD_CLK_TVD:
            return 1;
        case AW_MOD_CLK_LCD0CH1_S1:
            return (aw_ccu_reg->Lcd0Ch1Clk.ClkDiv + 1) * (aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Src + 1);
        case AW_MOD_CLK_LCD0CH1_S2:
            return aw_ccu_reg->Lcd0Ch1Clk.ClkDiv + 1;
        case AW_MOD_CLK_LCD1CH1_S1:
            return (aw_ccu_reg->Lcd1Ch1Clk.ClkDiv + 1) * (aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Src + 1);
        case AW_MOD_CLK_LCD1CH1_S2:
            return aw_ccu_reg->Lcd1Ch1Clk.ClkDiv + 1;
        case AW_MOD_CLK_CSI0:
            return aw_ccu_reg->Csi0Clk.ClkDiv + 1;
        case AW_MOD_CLK_CSI1:
            return aw_ccu_reg->Csi1Clk.ClkDiv + 1;
        case AW_MOD_CLK_VE:
            return (aw_ccu_reg->VeClk.ClkDiv + 1);
        case AW_MOD_CLK_ADDA:
        case AW_MOD_CLK_AVS:
            return 1;
        case AW_MOD_CLK_ACE:
            return (aw_ccu_reg->AceClk.ClkDiv + 1);
        case AW_MOD_CLK_LVDS:
            return 1;
        case AW_MOD_CLK_HDMI:
            return (aw_ccu_reg->HdmiClk.ClkDiv + 1);
        case AW_MOD_CLK_MALI:
            return (aw_ccu_reg->MaliClk.ClkDiv + 1);
        default:
            return 1;
    }
}


/*
*********************************************************************************************************
*                           mod_clk_get_reset
*
*Description: get module clock reset status;
*
*Arguments  : id    module clock id;
*
*Return     : result,
*               AW_CCU_CLK_RESET,   module clock reset valid;
*               AW_CCU_CLK_NRESET,  module clock reset invalid;
*
*Notes      :
*
*********************************************************************************************************
*/
static __aw_ccu_clk_reset_e mod_clk_get_reset(__aw_ccu_mod_clk_e id)
{
    switch(id)
    {
        case AW_MOD_CLK_NFC:
        case AW_MOD_CLK_MSC:
        case AW_MOD_CLK_SDC0:
        case AW_MOD_CLK_SDC1:
        case AW_MOD_CLK_SDC2:
        case AW_MOD_CLK_SDC3:
        case AW_MOD_CLK_TS:
        case AW_MOD_CLK_SS:
        case AW_MOD_CLK_SPI0:
        case AW_MOD_CLK_SPI1:
        case AW_MOD_CLK_SPI2:
        case AW_MOD_CLK_PATA:
        case AW_MOD_CLK_IR0:
        case AW_MOD_CLK_IR1:
        case AW_MOD_CLK_I2S:
        case AW_MOD_CLK_AC97:
        case AW_MOD_CLK_SPDIF:
        case AW_MOD_CLK_KEYPAD:
        case AW_MOD_CLK_SATA:
        case AW_MOD_CLK_USBPHY:
        case AW_MOD_CLK_USBPHY0:
        case AW_MOD_CLK_USBPHY1:
        case AW_MOD_CLK_USBPHY2:
            return AW_CCU_CLK_NRESET;

        case AW_MOD_CLK_USBOHCI0:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_USBOHCI1:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_GPS:
            return aw_ccu_reg->GpsClk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_SPI3:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_DEBE0:
            return aw_ccu_reg->DeBe0Clk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_DEBE1:
            return aw_ccu_reg->DeBe1Clk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_DEFE0:
            return aw_ccu_reg->DeFe0Clk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_DEFE1:
            return aw_ccu_reg->DeFe1Clk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_DEMIX:
            return aw_ccu_reg->DeMpClk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_LCD0CH0:
            return aw_ccu_reg->Lcd0Ch0Clk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_LCD1CH0:
            return aw_ccu_reg->Lcd1Ch0Clk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_CSIISP:
        case AW_MOD_CLK_TVD:
        case AW_MOD_CLK_LCD0CH1_S1:
        case AW_MOD_CLK_LCD0CH1_S2:
        case AW_MOD_CLK_LCD1CH1_S1:
        case AW_MOD_CLK_LCD1CH1_S2:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_CSI0:
            return aw_ccu_reg->Csi0Clk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_CSI1:
            return aw_ccu_reg->Csi1Clk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_VE:
            return aw_ccu_reg->VeClk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_ADDA:
        case AW_MOD_CLK_AVS:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_ACE:
            return aw_ccu_reg->AceClk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_LVDS:
            return aw_ccu_reg->LvdsClk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_HDMI:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_MALI:
            return aw_ccu_reg->MaliClk.Reset? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        default:
            return AW_CCU_CLK_NRESET;
    }

}


/*
*********************************************************************************************************
*                           mod_clk_set_parent
*
*Description: set clock parent id for module clock;
*
*Arguments  : id        module clock id;
*             parent    parent clock id;
*
*Return     : result;
*               0,  set parent successed;
*              !0,  set parent failed;
*Notes      :
*
*********************************************************************************************************
*/
static __s32 mod_clk_set_parent(__aw_ccu_mod_clk_e id, __aw_ccu_sys_clk_e parent)
{
    switch(id)
    {
        case AW_MOD_CLK_NFC:
            return _set_module0_clk_src(&aw_ccu_reg->NandClk, parent);
        case AW_MOD_CLK_MSC:
            return _set_module0_clk_src(&aw_ccu_reg->MsClk, parent);
        case AW_MOD_CLK_SDC0:
            return _set_module0_clk_src(&aw_ccu_reg->SdMmc0Clk, parent);
        case AW_MOD_CLK_SDC1:
            return _set_module0_clk_src(&aw_ccu_reg->SdMmc1Clk, parent);
        case AW_MOD_CLK_SDC2:
            return _set_module0_clk_src(&aw_ccu_reg->SdMmc2Clk, parent);
        case AW_MOD_CLK_SDC3:
            return _set_module0_clk_src(&aw_ccu_reg->SdMmc3Clk, parent);
        case AW_MOD_CLK_TS:
            return _set_module0_clk_src(&aw_ccu_reg->TsClk, parent);
        case AW_MOD_CLK_SS:
            return _set_module0_clk_src(&aw_ccu_reg->SsClk, parent);
        case AW_MOD_CLK_SPI0:
            return _set_module0_clk_src(&aw_ccu_reg->Spi0Clk, parent);
        case AW_MOD_CLK_SPI1:
            return _set_module0_clk_src(&aw_ccu_reg->Spi1Clk, parent);
        case AW_MOD_CLK_SPI2:
            return _set_module0_clk_src(&aw_ccu_reg->Spi2Clk, parent);
        case AW_MOD_CLK_PATA:
            return _set_module0_clk_src(&aw_ccu_reg->PataClk, parent);
        case AW_MOD_CLK_IR0:
            return _set_module0_clk_src(&aw_ccu_reg->Ir0Clk, parent);
        case AW_MOD_CLK_IR1:
            return _set_module0_clk_src(&aw_ccu_reg->Ir1Clk, parent);
        case AW_MOD_CLK_I2S:
            return (parent == AW_SYS_CLK_PLL2)? 0 : -1;
        case AW_MOD_CLK_AC97:
            return (parent == AW_SYS_CLK_PLL2)? 0 : -1;
        case AW_MOD_CLK_SPDIF:
            return (parent == AW_SYS_CLK_PLL2)? 0 : -1;
        case AW_MOD_CLK_KEYPAD:
        {
            switch(parent)
            {
                case AW_SYS_CLK_HOSC:
                    aw_ccu_reg->KeyPadClk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_LOSC:
                    aw_ccu_reg->KeyPadClk.ClkSrc = 2;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_SATA:
        {
            if(parent == AW_SYS_CLK_PLL6M)
            {
                aw_ccu_reg->SataClk.ClkSrc = 0;
                return 0;
            }

            return -1;
        }
        case AW_MOD_CLK_USBPHY:
        case AW_MOD_CLK_USBPHY0:
        case AW_MOD_CLK_USBPHY1:
        case AW_MOD_CLK_USBPHY2:
        case AW_MOD_CLK_USBOHCI0:
        case AW_MOD_CLK_USBOHCI1:
        {
            if(parent == AW_SYS_CLK_PLL62)
            {
                aw_ccu_reg->UsbClk.OHCIClkSrc = 0;
                return 0;
            }

            return -1;
        }
        case AW_MOD_CLK_SPI3:
            return _set_module0_clk_src(&aw_ccu_reg->Spi3Clk, parent);
        case AW_MOD_CLK_DEBE0:
            return _set_defemp_clk_src(&aw_ccu_reg->DeBe0Clk, parent);
        case AW_MOD_CLK_DEBE1:
            return _set_defemp_clk_src(&aw_ccu_reg->DeBe1Clk, parent);
        case AW_MOD_CLK_DEFE0:
            return _set_defemp_clk_src(&aw_ccu_reg->DeFe0Clk, parent);
        case AW_MOD_CLK_DEFE1:
            return _set_defemp_clk_src(&aw_ccu_reg->DeFe1Clk, parent);
        case AW_MOD_CLK_DEMIX:
            return _set_defemp_clk_src(&aw_ccu_reg->DeMpClk, parent);
        case AW_MOD_CLK_LCD0CH0:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->Lcd0Ch0Clk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL3X2:
                    aw_ccu_reg->Lcd0Ch0Clk.ClkSrc = 2;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->Lcd0Ch0Clk.ClkSrc = 1;
                    return 0;
                case AW_SYS_CLK_PLL7X2:
                    aw_ccu_reg->Lcd0Ch0Clk.ClkSrc = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_LCD1CH0:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->Lcd1Ch0Clk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL3X2:
                    aw_ccu_reg->Lcd1Ch0Clk.ClkSrc = 2;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->Lcd1Ch0Clk.ClkSrc = 1;
                    return 0;
                case AW_SYS_CLK_PLL7X2:
                    aw_ccu_reg->Lcd1Ch0Clk.ClkSrc = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_CSIISP:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->CsiIspClk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL4:
                    aw_ccu_reg->CsiIspClk.ClkSrc = 1;
                    return 0;
                case AW_SYS_CLK_PLL5:
                    aw_ccu_reg->CsiIspClk.ClkSrc = 2;
                    return 0;
                case AW_SYS_CLK_PLL62:
                    aw_ccu_reg->CsiIspClk.ClkSrc = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_TVD:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->TvdClk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->TvdClk.ClkSrc = 1;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_LCD0CH1_S1:
            return 0;
        case AW_MOD_CLK_LCD0CH1_S2:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Src = 0;
                    return 0;
                case AW_SYS_CLK_PLL3X2:
                    aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Src = 2;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Src = 1;
                    return 0;
                case AW_SYS_CLK_PLL7X2:
                    aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Src = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_LCD1CH1_S1:
            return 0;
        case AW_MOD_CLK_LCD1CH1_S2:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Src = 0;
                    return 0;
                case AW_SYS_CLK_PLL3X2:
                    aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Src = 2;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Src = 1;
                    return 0;
                case AW_SYS_CLK_PLL7X2:
                    aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Src = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_CSI0:
        {
            switch(parent)
            {
                case AW_SYS_CLK_HOSC:
                    aw_ccu_reg->Csi0Clk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->Csi0Clk.ClkSrc = 1;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->Csi0Clk.ClkSrc = 2;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_CSI1:
        {
            switch(parent)
            {
                case AW_SYS_CLK_HOSC:
                    aw_ccu_reg->Csi1Clk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->Csi1Clk.ClkSrc = 1;
                    return 0;
                case AW_SYS_CLK_PLL3X2:
                    aw_ccu_reg->Csi1Clk.ClkSrc = 5;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->Csi1Clk.ClkSrc = 2;
                    return 0;
                case AW_SYS_CLK_PLL7X2:
                    aw_ccu_reg->Csi1Clk.ClkSrc = 6;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_VE:
            return (parent == AW_SYS_CLK_PLL4)? 0 : -1;
        case AW_MOD_CLK_ADDA:
            return (parent == AW_SYS_CLK_PLL2)? 0 : -1;
        case AW_MOD_CLK_AVS:
            return (parent == AW_SYS_CLK_HOSC)? 0 : -1;
        case AW_MOD_CLK_ACE:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL4:
                    aw_ccu_reg->AceClk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL5P:
                    aw_ccu_reg->AceClk.ClkSrc = 1;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_HDMI:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL3:
                {
                    aw_ccu_reg->HdmiClk.ClkSrc = 0;
                    return 0;
                }
                case AW_SYS_CLK_PLL3X2:
                {
                    aw_ccu_reg->HdmiClk.ClkSrc = 2;
                    return 0;
                }
                case AW_SYS_CLK_PLL7:
                {
                    aw_ccu_reg->HdmiClk.ClkSrc = 1;
                    return 0;
                }
                case AW_SYS_CLK_PLL7X2:
                {
                    aw_ccu_reg->HdmiClk.ClkSrc = 3;
                    return 0;
                }
                default:
                    return -1;
            }
        }
        case AW_MOD_CLK_MALI:
        {
            switch(parent)
            {
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->MaliClk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL4:
                    aw_ccu_reg->MaliClk.ClkSrc = 1;
                    return 0;
                case AW_SYS_CLK_PLL5P:
                    aw_ccu_reg->MaliClk.ClkSrc = 2;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->MaliClk.ClkSrc = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_GPS:
        {
            return (parent == AW_SYS_CLK_AHB)? 0 : -1;
        }

        case AW_MOD_CLK_TWI0:
        case AW_MOD_CLK_TWI1:
        case AW_MOD_CLK_TWI2:
        case AW_MOD_CLK_CAN:
        case AW_MOD_CLK_SCR:
        case AW_MOD_CLK_PS20:
        case AW_MOD_CLK_PS21:
        case AW_MOD_CLK_UART0:
        case AW_MOD_CLK_UART1:
        case AW_MOD_CLK_UART2:
        case AW_MOD_CLK_UART3:
        case AW_MOD_CLK_UART4:
        case AW_MOD_CLK_UART5:
        case AW_MOD_CLK_UART6:
        case AW_MOD_CLK_UART7:
            return (parent == AW_SYS_CLK_APB1)? 0 : -1;

        case AW_MOD_CLK_LVDS:
        default:
            return (parent == AW_SYS_CLK_NONE)? 0 : -1;
    }
    return (parent == AW_SYS_CLK_NONE)? 0 : -1;
}


/*
*********************************************************************************************************
*                           mod_clk_set_status
*
*Description: set module clock on/off status;
*
*Arguments  : id        module clock id;
*             status    module clock on/off status;
*
*Return     : result
*               0,  set module clock on/off status successed;
*              !0,  set module clock on/off status failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s32 mod_clk_set_status(__aw_ccu_mod_clk_e id, __aw_ccu_clk_onff_e status)
{
    switch(id)
    {
        case AW_MOD_CLK_NFC:
            return _set_module0_clk_status(&aw_ccu_reg->NandClk, status);
        case AW_MOD_CLK_MSC:
            return _set_module0_clk_status(&aw_ccu_reg->MsClk, status);
        case AW_MOD_CLK_SDC0:
            return _set_module0_clk_status(&aw_ccu_reg->SdMmc0Clk, status);
        case AW_MOD_CLK_SDC1:
            return _set_module0_clk_status(&aw_ccu_reg->SdMmc1Clk, status);
        case AW_MOD_CLK_SDC2:
            return _set_module0_clk_status(&aw_ccu_reg->SdMmc2Clk, status);
        case AW_MOD_CLK_SDC3:
            return _set_module0_clk_status(&aw_ccu_reg->SdMmc3Clk, status);
        case AW_MOD_CLK_TS:
            return _set_module0_clk_status(&aw_ccu_reg->TsClk, status);
        case AW_MOD_CLK_SS:
            return _set_module0_clk_status(&aw_ccu_reg->SsClk, status);
        case AW_MOD_CLK_SPI0:
            return _set_module0_clk_status(&aw_ccu_reg->Spi0Clk, status);
        case AW_MOD_CLK_SPI1:
            return _set_module0_clk_status(&aw_ccu_reg->Spi1Clk, status);
        case AW_MOD_CLK_SPI2:
            return _set_module0_clk_status(&aw_ccu_reg->Spi2Clk, status);
        case AW_MOD_CLK_PATA:
            return _set_module0_clk_status(&aw_ccu_reg->PataClk, status);
        case AW_MOD_CLK_IR0:
            return _set_module0_clk_status(&aw_ccu_reg->Ir0Clk, status);
        case AW_MOD_CLK_IR1:
            return _set_module0_clk_status(&aw_ccu_reg->Ir1Clk, status);
        case AW_MOD_CLK_I2S:
            aw_ccu_reg->I2sClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AC97:
            aw_ccu_reg->Ac97Clk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SPDIF:
            aw_ccu_reg->SpdifClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_KEYPAD:
        {
            aw_ccu_reg->KeyPadClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_SATA:
        {
            aw_ccu_reg->SataClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            aw_ccu_reg->Pll6Ctl.OutputEn = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_USBPHY:
        {
            if (SUNXI_VER_A10C == sw_get_ic_ver())
                aw_ccu_reg->UsbClk.ClkSwich = 1;
            aw_ccu_reg->UsbClk.PhySpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_USBPHY0:
        {
            aw_ccu_reg->UsbClk.UsbPhy0Rst = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_USBPHY1:
        {
            aw_ccu_reg->UsbClk.UsbPhy1Rst = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_USBPHY2:
        {
            aw_ccu_reg->UsbClk.UsbPhy2Rst = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        }

        case AW_MOD_CLK_USBOHCI0:
            aw_ccu_reg->UsbClk.OHCI0SpecClkGate = ((status == AW_CCU_CLK_OFF)? 0 : 1);
            return 0;
        case AW_MOD_CLK_USBOHCI1:
            aw_ccu_reg->UsbClk.OHCI1SpecClkGate = ((status == AW_CCU_CLK_OFF)? 0 : 1);
            return 0;
        case AW_MOD_CLK_GPS:
        {
            aw_ccu_reg->GpsClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_SPI3:
            return _set_module0_clk_status(&aw_ccu_reg->Spi3Clk, status);
        case AW_MOD_CLK_DEBE0:
            return _set_defemp_clk_status(&aw_ccu_reg->DeBe0Clk, status);
        case AW_MOD_CLK_DEBE1:
            return _set_defemp_clk_status(&aw_ccu_reg->DeBe1Clk, status);
        case AW_MOD_CLK_DEFE0:
            return _set_defemp_clk_status(&aw_ccu_reg->DeFe0Clk, status);
        case AW_MOD_CLK_DEFE1:
            return _set_defemp_clk_status(&aw_ccu_reg->DeFe1Clk, status);
        case AW_MOD_CLK_DEMIX:
            return _set_defemp_clk_status(&aw_ccu_reg->DeMpClk, status);
        case AW_MOD_CLK_LCD0CH0:
            aw_ccu_reg->Lcd0Ch0Clk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_LCD1CH0:
            aw_ccu_reg->Lcd1Ch0Clk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_CSIISP:
            aw_ccu_reg->CsiIspClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_TVD:
            aw_ccu_reg->TvdClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_LCD0CH1_S1:
            aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Gate = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_MOD_CLK_LCD0CH1_S2:
            aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Gate = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_MOD_CLK_LCD1CH1_S1:
            aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Gate = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_MOD_CLK_LCD1CH1_S2:
            aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Gate = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_MOD_CLK_CSI0:
            aw_ccu_reg->Csi0Clk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_CSI1:
            aw_ccu_reg->Csi1Clk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_VE:
            aw_ccu_reg->VeClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_ADDA:
            aw_ccu_reg->AddaClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AVS:
            aw_ccu_reg->AvsClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_ACE:
            aw_ccu_reg->AceClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_LVDS:
            return 0;
        case AW_MOD_CLK_HDMI:
            aw_ccu_reg->HdmiClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_MALI:
            aw_ccu_reg->MaliClk.SpecClkGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;

        case AW_MOD_CLK_TWI0:
        case AW_MOD_CLK_TWI1:
        case AW_MOD_CLK_TWI2:
        case AW_MOD_CLK_CAN:
        case AW_MOD_CLK_SCR:
        case AW_MOD_CLK_PS20:
        case AW_MOD_CLK_PS21:
        case AW_MOD_CLK_UART0:
        case AW_MOD_CLK_UART1:
        case AW_MOD_CLK_UART2:
        case AW_MOD_CLK_UART3:
        case AW_MOD_CLK_UART4:
        case AW_MOD_CLK_UART5:
        case AW_MOD_CLK_UART6:
        case AW_MOD_CLK_UART7:
            return 0;

        case AW_MOD_CLK_AXI_DRAM:
            aw_ccu_reg->AxiGate.SdramGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_USB0:
            aw_ccu_reg->AhbGate0.Usb0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_EHCI0:
            aw_ccu_reg->AhbGate0.Ehci0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_OHCI0:
            aw_ccu_reg->AhbGate0.Ohci0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_EHCI1:
            aw_ccu_reg->AhbGate0.Ehci1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_OHCI1:
            aw_ccu_reg->AhbGate0.Ohci1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SS:
            aw_ccu_reg->AhbGate0.SsGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;

        case AW_MOD_CLK_AHB_DMA:
            aw_ccu_reg->AhbGate0.DmaGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_BIST:
            aw_ccu_reg->AhbGate0.BistGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SDMMC0:
            aw_ccu_reg->AhbGate0.Sdmmc0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SDMMC1:
            aw_ccu_reg->AhbGate0.Sdmmc1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SDMMC2:
            aw_ccu_reg->AhbGate0.Sdmmc2Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SDMMC3:
            aw_ccu_reg->AhbGate0.Sdmmc3Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_MS:
            aw_ccu_reg->AhbGate0.MsGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_NAND:
            aw_ccu_reg->AhbGate0.NandGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SDRAM:
            aw_ccu_reg->AhbGate0.SdramGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_ACE:
            aw_ccu_reg->AhbGate0.AceGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_EMAC:
            aw_ccu_reg->AhbGate0.EmacGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_TS:
            aw_ccu_reg->AhbGate0.TsGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SPI0:
            aw_ccu_reg->AhbGate0.Spi0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SPI1:
            aw_ccu_reg->AhbGate0.Spi1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SPI2:
            aw_ccu_reg->AhbGate0.Spi2Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SPI3:
            aw_ccu_reg->AhbGate0.Spi3Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_PATA:
            aw_ccu_reg->AhbGate0.PataGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_SATA:
            aw_ccu_reg->AhbGate0.SataGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_GPS:
            aw_ccu_reg->AhbGate0.GpsGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_VE:
            aw_ccu_reg->AhbGate1.VeGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_TVD:
            aw_ccu_reg->AhbGate1.TvdGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_TVE0:
            aw_ccu_reg->AhbGate1.Tve0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_TVE1:
            aw_ccu_reg->AhbGate1.Tve1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_LCD0:
            aw_ccu_reg->AhbGate1.Lcd0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_LCD1:
            aw_ccu_reg->AhbGate1.Lcd1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_CSI0:
            aw_ccu_reg->AhbGate1.Csi0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_CSI1:
            aw_ccu_reg->AhbGate1.Csi1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_HDMI:
            aw_ccu_reg->AhbGate1.HdmiDGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_DEBE0:
            aw_ccu_reg->AhbGate1.DeBe0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_DEBE1:
            aw_ccu_reg->AhbGate1.DeBe1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_DEFE0:
            aw_ccu_reg->AhbGate1.DeFe0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_DEFE1:
            aw_ccu_reg->AhbGate1.DeFe1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_MP:
            aw_ccu_reg->AhbGate1.MpGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_AHB_MALI:
            aw_ccu_reg->AhbGate1.Gpu3DGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;

        case AW_MOD_CLK_APB_ADDA:
            aw_ccu_reg->Apb0Gate.AddaGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_SPDIF:
            aw_ccu_reg->Apb0Gate.SpdifGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_AC97:
            aw_ccu_reg->Apb0Gate.Ac97Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_I2S:
            aw_ccu_reg->Apb0Gate.IisGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_PIO:
            aw_ccu_reg->Apb0Gate.PioGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_IR0:
            aw_ccu_reg->Apb0Gate.Ir0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_IR1:
            aw_ccu_reg->Apb0Gate.Ir1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_KEYPAD:
            aw_ccu_reg->Apb0Gate.KeypadGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_TWI0:
            aw_ccu_reg->Apb1Gate.Twi0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_TWI1:
            aw_ccu_reg->Apb1Gate.Twi1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_TWI2:
            aw_ccu_reg->Apb1Gate.Twi2Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_CAN:
            aw_ccu_reg->Apb1Gate.CanGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_SCR:
            aw_ccu_reg->Apb1Gate.ScrGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_PS20:
            aw_ccu_reg->Apb1Gate.Ps20Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_PS21:
            aw_ccu_reg->Apb1Gate.Ps21Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_UART0:
            aw_ccu_reg->Apb1Gate.Uart0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_UART1:
            aw_ccu_reg->Apb1Gate.Uart1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_UART2:
            aw_ccu_reg->Apb1Gate.Uart2Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_UART3:
            aw_ccu_reg->Apb1Gate.Uart3Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_UART4:
            aw_ccu_reg->Apb1Gate.Uart4Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_UART5:
            aw_ccu_reg->Apb1Gate.Uart5Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_UART6:
            aw_ccu_reg->Apb1Gate.Uart6Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_APB_UART7:
            aw_ccu_reg->Apb1Gate.Uart7Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_VE:
            aw_ccu_reg->DramGate.VeGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_CSI0:
            aw_ccu_reg->DramGate.Csi0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_CSI1:
            aw_ccu_reg->DramGate.Csi1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_TS:
            aw_ccu_reg->DramGate.TsGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_TVD:
            aw_ccu_reg->DramGate.TvdGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_TVE0:
            aw_ccu_reg->DramGate.Tve0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_TVE1:
            aw_ccu_reg->DramGate.Tve1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_DEFE0:
            aw_ccu_reg->DramGate.DeFe0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_DEFE1:
            aw_ccu_reg->DramGate.DeFe1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_DEBE0:
            aw_ccu_reg->DramGate.DeBe0Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_DEBE1:
            aw_ccu_reg->DramGate.DeBe1Gate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_DEMP:
            aw_ccu_reg->DramGate.DeMpGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;
        case AW_MOD_CLK_SDRAM_ACE:
            aw_ccu_reg->DramGate.AceGate = (status == AW_CCU_CLK_OFF)? 0 : 1;
            return 0;

        default:
            return -1;
    }
}


/*
*********************************************************************************************************
*                           mod_clk_set_rate
*
*Description: set module clock division;
*
*Arguments  : id    module clock id;
*             rate  module clock division;
*
*Return     : result
*               0,  set module clock rate successed;
*              !0,  set module clock rate failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s32 mod_clk_set_rate(__aw_ccu_mod_clk_e id, __s64 rate)
{
    switch(id)
    {
        case AW_MOD_CLK_NFC:
            return _set_module0_clk_rate(&aw_ccu_reg->NandClk, rate);
        case AW_MOD_CLK_MSC:
            return _set_module0_clk_rate(&aw_ccu_reg->MsClk, rate);
        case AW_MOD_CLK_SDC0:
            return _set_module0_clk_rate(&aw_ccu_reg->SdMmc0Clk, rate);
        case AW_MOD_CLK_SDC1:
            return _set_module0_clk_rate(&aw_ccu_reg->SdMmc1Clk, rate);
        case AW_MOD_CLK_SDC2:
            return _set_module0_clk_rate(&aw_ccu_reg->SdMmc2Clk, rate);
        case AW_MOD_CLK_SDC3:
            return _set_module0_clk_rate(&aw_ccu_reg->SdMmc3Clk, rate);
        case AW_MOD_CLK_TS:
            return _set_module0_clk_rate(&aw_ccu_reg->TsClk, rate);
        case AW_MOD_CLK_SS:
            return _set_module0_clk_rate(&aw_ccu_reg->SsClk, rate);
        case AW_MOD_CLK_SPI0:
            return _set_module0_clk_rate(&aw_ccu_reg->Spi0Clk, rate);
        case AW_MOD_CLK_SPI1:
            return _set_module0_clk_rate(&aw_ccu_reg->Spi1Clk, rate);
        case AW_MOD_CLK_SPI2:
            return _set_module0_clk_rate(&aw_ccu_reg->Spi2Clk, rate);
        case AW_MOD_CLK_PATA:
            return _set_module0_clk_rate(&aw_ccu_reg->PataClk, rate);
        case AW_MOD_CLK_IR0:
            return _set_module0_clk_rate(&aw_ccu_reg->Ir0Clk, rate);
        case AW_MOD_CLK_IR1:
            return _set_module0_clk_rate(&aw_ccu_reg->Ir1Clk, rate);
        case AW_MOD_CLK_I2S:
        {
            switch(rate)
            {
                case 1:
                    aw_ccu_reg->I2sClk.ClkDiv = 0;
                    return 0;
                case 2:
                    aw_ccu_reg->I2sClk.ClkDiv = 1;
                    return 0;
                case 4:
                    aw_ccu_reg->I2sClk.ClkDiv = 2;
                    return 0;
                case 8:
                    aw_ccu_reg->I2sClk.ClkDiv = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_AC97:
        {
            switch(rate)
            {
                case 1:
                    aw_ccu_reg->Ac97Clk.ClkDiv = 0;
                    return 0;
                case 2:
                    aw_ccu_reg->Ac97Clk.ClkDiv = 1;
                    return 0;
                case 4:
                    aw_ccu_reg->Ac97Clk.ClkDiv = 2;
                    return 0;
                case 8:
                    aw_ccu_reg->Ac97Clk.ClkDiv = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_SPDIF:
        {
            switch(rate)
            {
                case 1:
                    aw_ccu_reg->SpdifClk.ClkDiv = 0;
                    return 0;
                case 2:
                    aw_ccu_reg->SpdifClk.ClkDiv = 1;
                    return 0;
                case 4:
                    aw_ccu_reg->SpdifClk.ClkDiv = 2;
                    return 0;
                case 8:
                    aw_ccu_reg->SpdifClk.ClkDiv = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_KEYPAD:
        {
            if(rate > 32*8)
            {
                return -1;
            }
            else if(rate > 32*4)
            {
                aw_ccu_reg->KeyPadClk.ClkPreDiv = 3;
                aw_ccu_reg->KeyPadClk.ClkDiv    = (rate>>3)-1;
            }
            else if(rate > 32*2)
            {
                aw_ccu_reg->KeyPadClk.ClkPreDiv = 2;
                aw_ccu_reg->KeyPadClk.ClkDiv    = (rate>>2)-1;
            }
            else if(rate > 32*1)
            {
                aw_ccu_reg->KeyPadClk.ClkPreDiv = 1;
                aw_ccu_reg->KeyPadClk.ClkDiv    = (rate>>1)-1;
            }
            else if(rate > 32*0)
            {
                aw_ccu_reg->KeyPadClk.ClkPreDiv = 0;
                aw_ccu_reg->KeyPadClk.ClkDiv    = rate-1;
            }
            else
            {
                return -1;
            }
            return 0;
        }
        case AW_MOD_CLK_SPI3:
            return _set_module0_clk_rate(&aw_ccu_reg->Spi3Clk, rate);
        case AW_MOD_CLK_DEBE0:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->DeBe0Clk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_DEBE1:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->DeBe1Clk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_DEFE0:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->DeFe0Clk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_DEFE1:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->DeFe1Clk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_DEMIX:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->DeMpClk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_CSIISP:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->CsiIspClk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_LCD0CH1_S1:
        {
            if(rate == (aw_ccu_reg->Lcd0Ch1Clk.ClkDiv+1))
            {
                aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Src = 0;
                return 0;
            }
            else if(rate == ((aw_ccu_reg->Lcd0Ch1Clk.ClkDiv+1)<<1))
            {
                aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Src = 1;
                return 0;
            }

            return 0;
        }
        case AW_MOD_CLK_LCD0CH1_S2:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->Lcd0Ch1Clk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_LCD1CH1_S1:
        {
            if(rate == (aw_ccu_reg->Lcd1Ch1Clk.ClkDiv+1))
            {
                aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Src = 0;
                return 0;
            }
            else if(rate == ((aw_ccu_reg->Lcd1Ch1Clk.ClkDiv+1)<<1))
            {
                aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Src = 1;
                return 0;
            }

            return 0;
        }
        case AW_MOD_CLK_LCD1CH1_S2:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->Lcd1Ch1Clk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_CSI0:
        {
            if((rate < 1) || (rate > 32))
            {
                return -1;
            }
            aw_ccu_reg->Csi0Clk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_CSI1:
        {
            if((rate < 1) || (rate > 32))
            {
                return -1;
            }
            aw_ccu_reg->Csi1Clk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_VE:
        {
            if((rate < 1) || (rate > 8))
            {
                return -1;
            }
            aw_ccu_reg->VeClk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_ACE:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->AceClk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_HDMI:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->HdmiClk.ClkDiv = rate-1;
            return 0;
        }
        case AW_MOD_CLK_MALI:
        {
            if((rate < 1) || (rate > 16))
            {
                return -1;
            }
            aw_ccu_reg->MaliClk.ClkDiv = rate-1;
            return 0;
        }

        case AW_MOD_CLK_LCD0CH0:
        case AW_MOD_CLK_LCD1CH0:
        case AW_MOD_CLK_LVDS:
        case AW_MOD_CLK_TVD:
        case AW_MOD_CLK_ADDA:
        case AW_MOD_CLK_SATA:
        case AW_MOD_CLK_USBPHY:
        case AW_MOD_CLK_USBPHY0:
        case AW_MOD_CLK_USBPHY1:
        case AW_MOD_CLK_USBPHY2:
        case AW_MOD_CLK_USBOHCI0:
        case AW_MOD_CLK_USBOHCI1:
        case AW_MOD_CLK_GPS:
        case AW_MOD_CLK_AVS:
        default:
            return (rate == 1)? 0 : -1;
    }
    return (rate == 1)? 0 : -1;
}


/*
*********************************************************************************************************
*                           mod_clk_set_reset
*
*Description: set module clock reset status
*
*Arguments  : id    module clock id;
*             reset module clock reset status;
*
*Return     : result;
*               0,  set module clock reset status successed;
*              !0,  set module clock reset status failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s32 mod_clk_set_reset(__aw_ccu_mod_clk_e id, __aw_ccu_clk_reset_e reset)
{
    switch(id)
    {
        case AW_MOD_CLK_NFC:
        case AW_MOD_CLK_MSC:
        case AW_MOD_CLK_SDC0:
        case AW_MOD_CLK_SDC1:
        case AW_MOD_CLK_SDC2:
        case AW_MOD_CLK_SDC3:
        case AW_MOD_CLK_TS:
        case AW_MOD_CLK_SS:
        case AW_MOD_CLK_SPI0:
        case AW_MOD_CLK_SPI1:
        case AW_MOD_CLK_SPI2:
        case AW_MOD_CLK_PATA:
        case AW_MOD_CLK_IR0:
        case AW_MOD_CLK_IR1:
        case AW_MOD_CLK_I2S:
        case AW_MOD_CLK_AC97:
        case AW_MOD_CLK_SPDIF:
        case AW_MOD_CLK_KEYPAD:
        case AW_MOD_CLK_SATA:
        case AW_MOD_CLK_USBPHY:
        case AW_MOD_CLK_USBOHCI0:
        case AW_MOD_CLK_USBOHCI1:
            return (reset == AW_CCU_CLK_NRESET)? 0 : -1;
        case AW_MOD_CLK_USBPHY0:
        case AW_MOD_CLK_USBPHY1:
        case AW_MOD_CLK_USBPHY2:
            return 0;
        case AW_MOD_CLK_GPS:
        {
            aw_ccu_reg->GpsClk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEBE0:
        {
            aw_ccu_reg->DeBe0Clk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEBE1:
        {
            aw_ccu_reg->DeBe1Clk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEFE0:
        {
            aw_ccu_reg->DeFe0Clk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEFE1:
        {
            aw_ccu_reg->DeFe1Clk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEMIX:
        {
            aw_ccu_reg->DeMpClk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_LCD0CH0:
        {
            aw_ccu_reg->Lcd0Ch0Clk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_LCD1CH0:
        {
            aw_ccu_reg->Lcd1Ch0Clk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_CSI0:
        {
            aw_ccu_reg->Csi0Clk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_CSI1:
        {
            aw_ccu_reg->Csi1Clk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_VE:
        {
            aw_ccu_reg->VeClk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_ACE:
            aw_ccu_reg->AceClk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        case AW_MOD_CLK_LVDS:
            aw_ccu_reg->LvdsClk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;
        case AW_MOD_CLK_MALI:
            aw_ccu_reg->MaliClk.Reset = (reset == AW_CCU_CLK_RESET)? 0 : 1;
            return 0;

        case AW_MOD_CLK_ADDA:
        case AW_MOD_CLK_CSIISP:
        case AW_MOD_CLK_TVD:
        case AW_MOD_CLK_LCD0CH1_S1:
        case AW_MOD_CLK_LCD0CH1_S2:
        case AW_MOD_CLK_LCD1CH1_S1:
        case AW_MOD_CLK_LCD1CH1_S2:
        case AW_MOD_CLK_SPI3:
        case AW_MOD_CLK_AVS:
        case AW_MOD_CLK_HDMI:
        default:
            return (reset == AW_CCU_CLK_NRESET)? 0 : -1;
    }
    return (reset == AW_CCU_CLK_NRESET)? 0 : -1;
}


/*
*********************************************************************************************************
*                           aw_ccu_get_mod_clk_cnt
*
*Description: get the count of the module clock.
*
*Arguments  : none
*
*Return     : count of the module clock;
*
*Notes      :
*
*********************************************************************************************************
*/
__s32 aw_ccu_get_mod_clk_cnt(void)
{
    return (__u32)AW_MOD_CLK_CNT;
}


/*
*********************************************************************************************************
*                           mod_clk_get_rate_hz
*
*Description: get module clock rate based on hz;
*
*Arguments  : id    module clock id;
*
*Return     : module clock division;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s64 mod_clk_get_rate_hz(__aw_ccu_mod_clk_e id)
{
    __s64               tmpRate;
    __aw_ccu_clk_t      *tmpParent;

    tmpRate = mod_clk_get_rate(id);
    tmpParent = aw_ccu_get_sys_clk(mod_clk_get_parent(id));

    return ccu_clk_uldiv(tmpParent->rate, tmpRate);
}


/*
*********************************************************************************************************
*                           mod_clk_set_rate_hz
*
*Description: set module clock rate based on hz;
*
*Arguments  : id    module clock id;
*             rate  module clock division;
*
*Return     : result
*               0,  set module clock rate successed;
*              !0,  set module clock rate failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s32 mod_clk_set_rate_hz(__aw_ccu_mod_clk_e id, __s64 rate)
{
    __aw_ccu_clk_t      *tmpParent;

    tmpParent = aw_ccu_get_sys_clk(mod_clk_get_parent(id));
    return mod_clk_set_rate(id, ccu_clk_uldiv((tmpParent->rate + (rate>>1)), rate));
}


/*
*********************************************************************************************************
*                           aw_ccu_get_mod_clk
*
*Description: get module clock information by clock id.
*
*Arguments  : id    module clock id;
*
*Return     : module clock handle, return NULL if get clock information failed;
*
*Notes      :
*
*********************************************************************************************************
*/
__aw_ccu_clk_t *aw_ccu_get_mod_clk(__aw_ccu_mod_clk_e id)
{
    __s32   tmpIdx = (__u32)id;

    /* check if clock id is valid   */
    if((id < AW_MOD_CLK_NONE) || (id >= AW_MOD_CLK_CNT))
    {
        CCU_ERR("ID is invalid when get module clock information!\n");
        return NULL;
    }

    /* query module clock information from hardware */
    aw_ccu_mod_clk[tmpIdx].parent = mod_clk_get_parent(id);
    aw_ccu_mod_clk[tmpIdx].onoff  = mod_clk_get_status(id);
    aw_ccu_mod_clk[tmpIdx].rate   = mod_clk_get_rate_hz(id);
    aw_ccu_mod_clk[tmpIdx].reset  = mod_clk_get_reset(id);
    aw_ccu_mod_clk[tmpIdx].hash   = ccu_clk_calc_hash(aw_ccu_mod_clk[tmpIdx].name);

    return &aw_ccu_mod_clk[tmpIdx];
}


/*
*********************************************************************************************************
*                           aw_ccu_set_mod_clk
*
*Description: set module clock parameters;
*
*Arguments  : clk   handle of module clock;
*
*Return     : error type.
*
*Notes      :
*
*********************************************************************************************************
*/
__aw_ccu_err_e aw_ccu_set_mod_clk(__aw_ccu_clk_t *clk)
{
    __aw_ccu_clk_t  tmpClk;

    if(!clk)
    {
        CCU_ERR("Clock handle is NULL when set system clock!\n");
        return AW_CCU_ERR_PARA_NULL;
    }

    /* backup old parameter */
    tmpClk.parent = mod_clk_get_parent(clk->id);
    tmpClk.onoff  = mod_clk_get_status(clk->id);
    tmpClk.reset  = mod_clk_get_reset(clk->id);
    tmpClk.rate   = mod_clk_get_rate_hz(clk->id);

    /* try to set new parameter */
    if(!mod_clk_set_parent(clk->id, clk->parent))
    {
        if(!mod_clk_set_rate_hz(clk->id, clk->rate))
        {
            if(!mod_clk_set_status(clk->id, clk->onoff))
            {
                if(!mod_clk_set_reset(clk->id, clk->reset))
                {
                    /* update managemer parameter  */
                    aw_ccu_mod_clk[(__u32)clk->id].parent = clk->parent;
                    aw_ccu_mod_clk[(__u32)clk->id].onoff  = clk->onoff;
                    aw_ccu_mod_clk[(__u32)clk->id].reset  = clk->reset;
                    aw_ccu_mod_clk[(__u32)clk->id].rate   = clk->rate;

                    return AW_CCU_ERR_NONE;
                }
                else
                {
                    CCU_ERR("set %s reset status to %d failed!\n", clk->name, clk->reset);
                }

                /* resetore on/off status */
                mod_clk_set_status(clk->id, tmpClk.onoff);
            }
            else
            {
                CCU_ERR("set %s on/off status to %d failed!\n", clk->name, clk->onoff);
            }

            /* restore clock rate */
            mod_clk_set_rate_hz(clk->id, tmpClk.rate);
        }
        else
        {
            CCU_ERR("set %s clock rate to %lld failed!\n", clk->name, clk->rate);
        }

        /* restore clock parent */
        mod_clk_set_parent(clk->id, tmpClk.parent);
    }
    else
    {
        CCU_ERR("set %s clock parent to (id = %d) failed!\n", clk->name, (__s32)clk->parent);
    }

    /* update clock manager paremter */
    aw_ccu_mod_clk[(__u32)clk->id].parent = tmpClk.parent;
    aw_ccu_mod_clk[(__u32)clk->id].onoff  = tmpClk.onoff;
    aw_ccu_mod_clk[(__u32)clk->id].reset  = tmpClk.reset;
    aw_ccu_mod_clk[(__u32)clk->id].rate   = tmpClk.rate;

    return AW_CCU_ERR_PARA_INVALID;
}

