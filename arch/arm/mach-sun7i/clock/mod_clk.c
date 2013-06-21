/*
 * arch/arm/mach-sun7i/clock/ccmu/pll_cfg.c
 * (c) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * James Deng <csjamesdeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <mach/includes.h>

#include "ccm_i.h"

static __aw_ccu_clk_id_e    mod_clk_get_parent(__aw_ccu_clk_id_e id);
static __aw_ccu_clk_onff_e  mod_clk_get_status(__aw_ccu_clk_id_e id);
static __s64                mod_clk_get_rate  (__aw_ccu_clk_id_e id);
static __aw_ccu_clk_reset_e mod_clk_get_reset (__aw_ccu_clk_id_e id);

static __s32 mod_clk_set_parent(__aw_ccu_clk_id_e id, __aw_ccu_clk_id_e parent  );
static __s32 mod_clk_set_status(__aw_ccu_clk_id_e id, __aw_ccu_clk_onff_e status);
static __s32 mod_clk_set_rate  (__aw_ccu_clk_id_e id, __s64 rate                );
static __s32 mod_clk_set_reset (__aw_ccu_clk_id_e id, __aw_ccu_clk_reset_e reset);

static inline __aw_ccu_clk_id_e _parse_module0_clk_src(volatile __ccmu_module0_clk_t *reg)
{
    switch (reg->ClkSrc) {
        case 0:
            return AW_SYS_CLK_HOSC;
        case 1:
            return AW_SYS_CLK_PLL6;
        case 2:
            return AW_SYS_CLK_PLL5P;
        default:
            return AW_SYS_CLK_NONE;
    }
    return AW_SYS_CLK_NONE;
}

static inline __aw_ccu_clk_id_e _parse_defemp_clk_src(volatile __ccmu_fedemp_clk_t *reg)
{
    switch (reg->ClkSrc) {
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

static inline __s32 _set_module0_clk_src(volatile __ccmu_module0_clk_t *reg, __aw_ccu_clk_id_e parent)
{
    switch (parent) {
        case AW_SYS_CLK_HOSC:
            reg->ClkSrc = 0;
            break;
        case AW_SYS_CLK_PLL6:
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

static inline __s32 _set_defemp_clk_src(volatile __ccmu_fedemp_clk_t *reg, __aw_ccu_clk_id_e parent)
{
    switch (parent) {
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
    return reg->SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
}

static inline __s32 _set_module0_clk_status(volatile __ccmu_module0_clk_t *reg, __aw_ccu_clk_onff_e status)
{
    reg->SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
    return 0;
}

static inline __aw_ccu_clk_onff_e _get_defemp_clk_status(volatile __ccmu_fedemp_clk_t *reg)
{
    return reg->SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
}

static inline __s32 _set_defemp_clk_status(volatile __ccmu_fedemp_clk_t *reg, __aw_ccu_clk_onff_e status)
{
    reg->SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
    return 0;
}

static inline __u32 _get_module0_clk_rate(volatile __ccmu_module0_clk_t *reg)
{
    return (1 << reg->ClkPreDiv) * (reg->ClkDiv + 1);
}

static inline __s32 _set_module0_clk_rate(volatile __ccmu_module0_clk_t *reg, __u64 rate)
{
    if (rate > 16 * 8) {
        return -1;
    } else if (rate > 16 * 4) {
        reg->ClkPreDiv = 3;
        //reg->ClkDiv    = (rate>>3)-1;
        reg->ClkDiv    = ((rate + 7) >> 3) - 1;
    } else if (rate > 16 * 2) {
        reg->ClkPreDiv = 2;
        //reg->ClkDiv    = (rate>>2)-1;
        reg->ClkDiv    = ((rate + 3) >> 2) - 1;
    } else if (rate > 16 * 1) {
        reg->ClkPreDiv = 1;
        //reg->ClkDiv    = (rate>>1)-1;
        reg->ClkDiv    = ((rate + 1) >> 1) - 1;
    } else if (rate > 0) {
        reg->ClkPreDiv = 0;
        reg->ClkDiv    = rate - 1;
    } else {
        CCU_ERR("clock (reg:0x%08x) rate %d invlid\n", (__u32)reg, (__u32)rate);
        return -1;
    }

    return 0;
}

/*
 * Get clock parent for module clock.
 *
 * @id:     module clock id.
 *
 */
static __aw_ccu_clk_id_e mod_clk_get_parent(__aw_ccu_clk_id_e id)
{
    switch (id) {
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
        case AW_MOD_CLK_I2S0:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_I2S1:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_I2S2:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_AC97:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_SPDIF:
            return AW_SYS_CLK_PLL2;
        case AW_MOD_CLK_KEYPAD: {
            switch (aw_ccu_reg->KeyPadClk.ClkSrc) {
                case 0:
                    return AW_SYS_CLK_HOSC;
                case 2:
                    return AW_SYS_CLK_LOSC;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_SATA: {
            switch (aw_ccu_reg->SataClk.ClkSrc) {
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
        /* REMOVED */
        //case AW_MOD_CLK_GPS: {
        //    switch (aw_ccu_reg->GpsClk.ClkSrc) {
        //        case 0:
        //            return AW_SYS_CLK_HOSC;
        //        case 1:
        //            return AW_SYS_CLK_PLL6;
        //        case 2:
        //            return AW_SYS_CLK_PLL7;
        //        default:
        //            return AW_SYS_CLK_PLL4;
        //    }
        //}
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
        case AW_MOD_CLK_LCD0CH0: {
            switch (aw_ccu_reg->Lcd0Ch0Clk.ClkSrc) {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL7;
                case 2:
                    return AW_SYS_CLK_PLL3X2;
                case 3:
                    return AW_SYS_CLK_PLL62;
                default:
                    return AW_SYS_CLK_NONE;
            }
            return AW_SYS_CLK_NONE;
        }
        case AW_MOD_CLK_LCD1CH0: {
            switch (aw_ccu_reg->Lcd1Ch0Clk.ClkSrc) {
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
        case AW_MOD_CLK_CSIISP: {
            switch (aw_ccu_reg->CsiIspClk.ClkSrc) {
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
        case AW_MOD_CLK_TVDMOD1:
            return aw_ccu_reg->TvdClk.Clk1Src ? AW_SYS_CLK_PLL7 : AW_SYS_CLK_PLL3;
        case AW_MOD_CLK_TVDMOD2:
            return aw_ccu_reg->TvdClk.Clk2Src ? AW_SYS_CLK_PLL7 : AW_SYS_CLK_PLL3;

        case AW_MOD_CLK_LCD0CH1_S1:
        case AW_MOD_CLK_LCD0CH1_S2: {
            switch (aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Src) {
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
        case AW_MOD_CLK_LCD1CH1_S2: {
            switch (aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Src) {
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
        case AW_MOD_CLK_CSI0: {
            switch (aw_ccu_reg->Csi0Clk.ClkSrc) {
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
        case AW_MOD_CLK_CSI1: {
            switch (aw_ccu_reg->Csi1Clk.ClkSrc) {
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
            return aw_ccu_reg->AceClk.ClkSrc ? AW_SYS_CLK_PLL5P : AW_SYS_CLK_PLL4;
        case AW_MOD_CLK_LVDS:
            return AW_SYS_CLK_NONE;
        case AW_MOD_CLK_HDMI: {
            switch (aw_ccu_reg->HdmiClk.ClkSrc) {
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
        case AW_MOD_CLK_MALI: {
            switch (aw_ccu_reg->MaliClk.ClkSrc) {
                case 0:
                    return AW_SYS_CLK_PLL3;
                case 1:
                    return AW_SYS_CLK_PLL4;
                case 2:
                    return AW_SYS_CLK_PLL5P;
                case 3:
                    return AW_SYS_CLK_PLL7;
                case 4:
                    return AW_SYS_CLK_PLL8;
                default:
                    aw_ccu_reg->MaliClk.ClkSrc = 4;
                    return AW_SYS_CLK_PLL8;
            }
        }
        case AW_MOD_CLK_MBUS: {
            switch (aw_ccu_reg->MBusClk.ClkSrc) {
                case 0:
                    return AW_SYS_CLK_HOSC;
                case 1:
                    return AW_SYS_CLK_PLL6X2;
                case 2:
                    return AW_SYS_CLK_PLL5P;
                default:
                    aw_ccu_reg->MBusClk.ClkSrc = 2;
                    return AW_SYS_CLK_PLL5P;
            }
        }
        case AW_MOD_CLK_OUTA: {
            if ((aw_ccu_reg->ClkOutA.ClkSrc == 0) || (aw_ccu_reg->ClkOutA.ClkSrc == 2))
                return AW_SYS_CLK_HOSC;
            else if (aw_ccu_reg->ClkOutA.ClkSrc == 1)
                return AW_SYS_CLK_LOSC;
            else {
                aw_ccu_reg->ClkOutA.ClkSrc = 1;
                return AW_SYS_CLK_LOSC;
            }
        }
        case AW_MOD_CLK_OUTB: {
            if ((aw_ccu_reg->ClkOutB.ClkSrc == 0) || (aw_ccu_reg->ClkOutB.ClkSrc == 2))
                return AW_SYS_CLK_HOSC;
            else if (aw_ccu_reg->ClkOutB.ClkSrc == 1)
                return AW_SYS_CLK_LOSC;
            else {
                aw_ccu_reg->ClkOutB.ClkSrc = 1;
                return AW_SYS_CLK_LOSC;
            }
        }
        case AW_MOD_CLK_TWI0:
        case AW_MOD_CLK_TWI1:
        case AW_MOD_CLK_TWI2:
        case AW_MOD_CLK_TWI3:
        case AW_MOD_CLK_TWI4:
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
 * Get module clock on/off status.
 *
 * @id:     module clock id.
 *
 */
static __aw_ccu_clk_onff_e mod_clk_get_status(__aw_ccu_clk_id_e id)
{
    switch (id) {
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
        /* REMOVED */
        //case AW_MOD_CLK_PATA:
        //    return _get_module0_clk_status(&aw_ccu_reg->PataClk);
        case AW_MOD_CLK_IR0:
            return _get_module0_clk_status(&aw_ccu_reg->Ir0Clk);
        case AW_MOD_CLK_IR1:
            return _get_module0_clk_status(&aw_ccu_reg->Ir1Clk);
        case AW_MOD_CLK_I2S0:
            return aw_ccu_reg->I2s0Clk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_I2S1:
            return aw_ccu_reg->I2s1Clk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_I2S2:
            return aw_ccu_reg->I2s2Clk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AC97:
            return aw_ccu_reg->Ac97Clk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SPDIF:
            return aw_ccu_reg->SpdifClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_KEYPAD:
            return aw_ccu_reg->KeyPadClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_SATA:
            return aw_ccu_reg->SataClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBPHY:
            return aw_ccu_reg->UsbClk.PhySpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBPHY0:
            return aw_ccu_reg->UsbClk.UsbPhy0Rst ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBPHY1:
            return aw_ccu_reg->UsbClk.UsbPhy1Rst ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBPHY2:
            return aw_ccu_reg->UsbClk.UsbPhy2Rst ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBOHCI0:
            return aw_ccu_reg->UsbClk.OHCI0SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_USBOHCI1:
            return aw_ccu_reg->UsbClk.OHCI0SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        /* REMOVED */
        //case AW_MOD_CLK_GPS:
        //    return aw_ccu_reg->GpsClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
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
            return aw_ccu_reg->Lcd0Ch0Clk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD1CH0:
            return aw_ccu_reg->Lcd1Ch0Clk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_CSIISP:
            return aw_ccu_reg->CsiIspClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_TVDMOD1:
            return aw_ccu_reg->TvdClk.Clk1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_TVDMOD2:
            return aw_ccu_reg->TvdClk.Clk2Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD0CH1_S1:
            return aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD0CH1_S2:
            return aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD1CH1_S1:
            return aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LCD1CH1_S2:
            return aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_CSI0:
            return aw_ccu_reg->Csi0Clk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_CSI1:
            return aw_ccu_reg->Csi1Clk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_VE:
            return aw_ccu_reg->VeClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_ADDA:
            return aw_ccu_reg->AddaClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_AVS:
            return aw_ccu_reg->AvsClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_ACE:
            return aw_ccu_reg->AceClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_LVDS:
            return AW_CCU_CLK_ON;
        case AW_MOD_CLK_HDMI:
            return aw_ccu_reg->HdmiClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_MALI:
            return aw_ccu_reg->MaliClk.SpecClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_MBUS:
            return aw_ccu_reg->MBusClk.ClkGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_OUTA:
            return aw_ccu_reg->ClkOutA.ClkEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_OUTB:
            return aw_ccu_reg->ClkOutA.ClkEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_MOD_CLK_TWI0:
        case AW_MOD_CLK_TWI1:
        case AW_MOD_CLK_TWI2:
        case AW_MOD_CLK_TWI3:
        case AW_MOD_CLK_TWI4:
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

        case AW_AHB_CLK_USB0:
            return aw_ccu_reg->AhbGate0.Usb0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_EHCI0:
            return aw_ccu_reg->AhbGate0.Ehci0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_OHCI0:
            return aw_ccu_reg->AhbGate0.Ohci0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_EHCI1:
            return aw_ccu_reg->AhbGate0.Ehci1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_OHCI1:
            return aw_ccu_reg->AhbGate0.Ohci1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SS:
            return aw_ccu_reg->AhbGate0.SsGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_DMA:
            return aw_ccu_reg->AhbGate0.DmaGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_BIST:
            return aw_ccu_reg->AhbGate0.BistGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SDMMC0:
            return aw_ccu_reg->AhbGate0.Sdmmc0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SDMMC1:
            return aw_ccu_reg->AhbGate0.Sdmmc1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SDMMC2:
            return aw_ccu_reg->AhbGate0.Sdmmc2Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SDMMC3:
            return aw_ccu_reg->AhbGate0.Sdmmc3Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_MS:
            return aw_ccu_reg->AhbGate0.MsGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_NAND:
            return aw_ccu_reg->AhbGate0.NandGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SDRAM:
            return aw_ccu_reg->AhbGate0.SdramGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_ACE:
            return aw_ccu_reg->AhbGate0.AceGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_EMAC:
            return aw_ccu_reg->AhbGate0.EmacGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_TS:
            return aw_ccu_reg->AhbGate0.TsGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SPI0:
            return aw_ccu_reg->AhbGate0.Spi0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SPI1:
            return aw_ccu_reg->AhbGate0.Spi1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SPI2:
            return aw_ccu_reg->AhbGate0.Spi2Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SPI3:
            return aw_ccu_reg->AhbGate0.Spi3Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        /* REMOVED */
        //case AW_AHB_CLK_PATA:
        //    return aw_ccu_reg->AhbGate0.PataGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_SATA:
            return aw_ccu_reg->AhbGate0.SataGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        /* REMOVED */
        //case AW_AHB_CLK_GPS:
        //    return aw_ccu_reg->AhbGate0.GpsGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_VE:
            return aw_ccu_reg->AhbGate1.VeGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_TVD:
            return aw_ccu_reg->AhbGate1.TvdGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_TVE0:
            return aw_ccu_reg->AhbGate1.Tve0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_TVE1:
            return aw_ccu_reg->AhbGate1.Tve1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_LCD0:
            return aw_ccu_reg->AhbGate1.Lcd0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_LCD1:
            return aw_ccu_reg->AhbGate1.Lcd1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_CSI0:
            return aw_ccu_reg->AhbGate1.Csi0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_CSI1:
            return aw_ccu_reg->AhbGate1.Csi1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_HDMI1:
            return aw_ccu_reg->AhbGate1.Hdmi1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_HDMI:
            return aw_ccu_reg->AhbGate1.HdmiDGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_DEBE0:
            return aw_ccu_reg->AhbGate1.DeBe0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_DEBE1:
            return aw_ccu_reg->AhbGate1.DeBe1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_DEFE0:
            return aw_ccu_reg->AhbGate1.DeFe0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_DEFE1:
            return aw_ccu_reg->AhbGate1.DeFe1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_GMAC:
            return aw_ccu_reg->AhbGate1.GmacGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_MP:
            return aw_ccu_reg->AhbGate1.MpGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_AHB_CLK_MALI:
            return aw_ccu_reg->AhbGate1.Gpu3DGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_ADDA:
            return aw_ccu_reg->Apb0Gate.AddaGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_SPDIF:
            return aw_ccu_reg->Apb0Gate.SpdifGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_AC97:
            return aw_ccu_reg->Apb0Gate.Ac97Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_I2S0:
            return aw_ccu_reg->Apb0Gate.Iis0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_I2S1:
            return aw_ccu_reg->Apb0Gate.Iis1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_I2S2:
            return aw_ccu_reg->Apb0Gate.Iis2Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_PIO:
            return aw_ccu_reg->Apb0Gate.PioGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_IR0:
            return aw_ccu_reg->Apb0Gate.Ir0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_IR1:
            return aw_ccu_reg->Apb0Gate.Ir1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_KEYPAD:
            return aw_ccu_reg->Apb0Gate.KeypadGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_TWI0:
            return aw_ccu_reg->Apb1Gate.Twi0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_TWI1:
            return aw_ccu_reg->Apb1Gate.Twi1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_TWI2:
            return aw_ccu_reg->Apb1Gate.Twi2Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_TWI3:
            return aw_ccu_reg->Apb1Gate.Twi3Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_TWI4:
            return aw_ccu_reg->Apb1Gate.Twi4Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_CAN:
            return aw_ccu_reg->Apb1Gate.CanGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_SCR:
            return aw_ccu_reg->Apb1Gate.ScrGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_PS20:
            return aw_ccu_reg->Apb1Gate.Ps20Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_PS21:
            return aw_ccu_reg->Apb1Gate.Ps21Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_UART0:
            return aw_ccu_reg->Apb1Gate.Uart0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_UART1:
            return aw_ccu_reg->Apb1Gate.Uart1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_UART2:
            return aw_ccu_reg->Apb1Gate.Uart2Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_UART3:
            return aw_ccu_reg->Apb1Gate.Uart3Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_UART4:
            return aw_ccu_reg->Apb1Gate.Uart4Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_UART5:
            return aw_ccu_reg->Apb1Gate.Uart5Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_UART6:
            return aw_ccu_reg->Apb1Gate.Uart6Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_APB_CLK_UART7:
            return aw_ccu_reg->Apb1Gate.Uart7Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_VE:
            return aw_ccu_reg->DramGate.VeGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_CSI0:
            return aw_ccu_reg->DramGate.Csi0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_CSI1:
            return aw_ccu_reg->DramGate.Csi1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_TS:
            return aw_ccu_reg->DramGate.TsGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_TVD:
            return aw_ccu_reg->DramGate.TvdGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_TVE0:
            return aw_ccu_reg->DramGate.Tve0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_TVE1:
            return aw_ccu_reg->DramGate.Tve1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_DEFE0:
            return aw_ccu_reg->DramGate.DeFe0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_DEFE1:
            return aw_ccu_reg->DramGate.DeFe1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_DEBE0:
            return aw_ccu_reg->DramGate.DeBe0Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_DEBE1:
            return aw_ccu_reg->DramGate.DeBe1Gate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_DEMP:
            return aw_ccu_reg->DramGate.DeMpGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_DRAM_CLK_ACE:
            return aw_ccu_reg->DramGate.AceGate ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        default:
            return AW_CCU_CLK_ON;
    }
    return AW_CCU_CLK_ON;
}

/*
 * Get module clock rate.
 *
 * @id:     module clock id.
 *
 */
static __s64 mod_clk_get_rate(__aw_ccu_clk_id_e id)
{
    switch (id) {
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
        case AW_MOD_CLK_I2S0:
            return (1 << aw_ccu_reg->I2s0Clk.ClkDiv);
        case AW_MOD_CLK_I2S1:
            return (1 << aw_ccu_reg->I2s1Clk.ClkDiv);
        case AW_MOD_CLK_I2S2:
            return (1 << aw_ccu_reg->I2s2Clk.ClkDiv);
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
        /* REMOVED */
        //case AW_MOD_CLK_GPS:
        //    return aw_ccu_reg->GpsClk.ClkDivRatio + 1;
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
        case AW_MOD_CLK_TVDMOD1:
            return aw_ccu_reg->TvdClk.Clk1Div + 1;
        case AW_MOD_CLK_TVDMOD2:
            return aw_ccu_reg->TvdClk.Clk2Div + 1;
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
        case AW_MOD_CLK_MBUS:
            return (aw_ccu_reg->MBusClk.ClkDivM + 1) * (1 << aw_ccu_reg->MBusClk.ClkDivN);
        case AW_MOD_CLK_OUTA: {
            if (aw_ccu_reg->ClkOutA.ClkSrc == 0)
                return (aw_ccu_reg->ClkOutA.ClkDivM + 1) * (1 << aw_ccu_reg->ClkOutA.ClkDivN) * 750;
            else
                return (aw_ccu_reg->ClkOutA.ClkDivM + 1) * (1 << aw_ccu_reg->ClkOutA.ClkDivN);
        }
        case AW_MOD_CLK_OUTB: {
            if (aw_ccu_reg->ClkOutB.ClkSrc == 0)
                return (aw_ccu_reg->ClkOutB.ClkDivM + 1) * (1 << aw_ccu_reg->ClkOutB.ClkDivN) * 750;
            else
                return (aw_ccu_reg->ClkOutB.ClkDivM + 1) * (1 << aw_ccu_reg->ClkOutB.ClkDivN);
        }
        default:
            return 1;
    }
}

/*
 * Set clock parent id for module clock.
 *
 * @id:     module clock id
 * @parent: parent clock id
 *
 */
static __s32 mod_clk_set_parent(__aw_ccu_clk_id_e id, __aw_ccu_clk_id_e parent)
{
    switch (id) {
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
        case AW_MOD_CLK_I2S0:
            return (parent == AW_SYS_CLK_PLL2) ? 0 : -1;
        case AW_MOD_CLK_I2S1:
            return (parent == AW_SYS_CLK_PLL2) ? 0 : -1;
        case AW_MOD_CLK_I2S2:
            return (parent == AW_SYS_CLK_PLL2) ? 0 : -1;
        case AW_MOD_CLK_AC97:
            return (parent == AW_SYS_CLK_PLL2) ? 0 : -1;
        case AW_MOD_CLK_SPDIF:
            return (parent == AW_SYS_CLK_PLL2) ? 0 : -1;
        case AW_MOD_CLK_KEYPAD: {
            switch (parent) {
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
        case AW_MOD_CLK_SATA: {
            if (parent == AW_SYS_CLK_PLL6M) {
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
        case AW_MOD_CLK_USBOHCI1: {
            /* REMOVED */
            //if (parent == AW_SYS_CLK_PLL62) {
            //    aw_ccu_reg->UsbClk.OHCIClkSrc = 0;
            //    return 0;
            //}

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
        case AW_MOD_CLK_LCD0CH0: {
            switch (parent) {
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->Lcd0Ch0Clk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL3X2:
                    aw_ccu_reg->Lcd0Ch0Clk.ClkSrc = 2;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->Lcd0Ch0Clk.ClkSrc = 1;
                    return 0;
                case AW_SYS_CLK_PLL6X2:
                    aw_ccu_reg->Lcd0Ch0Clk.ClkSrc = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_LCD1CH0: {
            switch (parent) {
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
        case AW_MOD_CLK_CSIISP: {
            switch (parent) {
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
        case AW_MOD_CLK_TVDMOD1: {
            if (parent == AW_SYS_CLK_PLL3)
                aw_ccu_reg->TvdClk.Clk1Src = 0;
            else if (parent == AW_SYS_CLK_PLL7)
                aw_ccu_reg->TvdClk.Clk1Src = 1;
            else
                return -1;
            return 0;
        }
        case AW_MOD_CLK_TVDMOD2: {
            if (parent == AW_SYS_CLK_PLL3)
                aw_ccu_reg->TvdClk.Clk2Src = 0;
            else if (parent == AW_SYS_CLK_PLL7)
                aw_ccu_reg->TvdClk.Clk2Src = 1;
            else
                return -1;
            return 0;
        }
        case AW_MOD_CLK_LCD0CH1_S1:
            return 0;
        case AW_MOD_CLK_LCD0CH1_S2: {
            switch (parent) {
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
        case AW_MOD_CLK_LCD1CH1_S2: {
            switch (parent) {
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
        case AW_MOD_CLK_CSI0: {
            switch (parent) {
                case AW_SYS_CLK_HOSC:
                    aw_ccu_reg->Csi0Clk.ClkSrc = 0;
                    return 0;
                case AW_SYS_CLK_PLL3:
                    aw_ccu_reg->Csi0Clk.ClkSrc = 1;
                    return 0;
                case AW_SYS_CLK_PLL3X2:
                    aw_ccu_reg->Csi0Clk.ClkSrc = 5;
                    return 0;
                case AW_SYS_CLK_PLL7:
                    aw_ccu_reg->Csi0Clk.ClkSrc = 2;
                    return 0;
                case AW_SYS_CLK_PLL7X2:
                    aw_ccu_reg->Csi0Clk.ClkSrc = 6;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_CSI1: {
            switch (parent) {
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
            return (parent == AW_SYS_CLK_PLL4) ? 0 : -1;
        case AW_MOD_CLK_ADDA:
            return (parent == AW_SYS_CLK_PLL2) ? 0 : -1;
        case AW_MOD_CLK_AVS:
            return (parent == AW_SYS_CLK_HOSC) ? 0 : -1;
        case AW_MOD_CLK_ACE: {
            switch (parent) {
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
        case AW_MOD_CLK_HDMI: {
            switch (parent) {
                case AW_SYS_CLK_PLL3: {
                    aw_ccu_reg->HdmiClk.ClkSrc = 0;
                    return 0;
                }
                case AW_SYS_CLK_PLL3X2: {
                    aw_ccu_reg->HdmiClk.ClkSrc = 2;
                    return 0;
                }
                case AW_SYS_CLK_PLL7: {
                    aw_ccu_reg->HdmiClk.ClkSrc = 1;
                    return 0;
                }
                case AW_SYS_CLK_PLL7X2: {
                    aw_ccu_reg->HdmiClk.ClkSrc = 3;
                    return 0;
                }
                default:
                    return -1;
            }
        }
        case AW_MOD_CLK_MALI: {
            switch (parent) {
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
                case AW_SYS_CLK_PLL8:
                    aw_ccu_reg->MaliClk.ClkSrc = 4;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_MBUS: {
            switch (parent) {
                case AW_SYS_CLK_HOSC:
                    aw_ccu_reg->MBusClk.ClkSrc = 0;
                    break;
                case AW_SYS_CLK_PLL6X2:
                    aw_ccu_reg->MBusClk.ClkSrc = 1;
                    break;
                case AW_SYS_CLK_PLL5P:
                    aw_ccu_reg->MBusClk.ClkSrc = 2;
                    break;
                default:
                    return -1;
            }
            return 0;
        }

        case AW_MOD_CLK_OUTA: {
            if (parent == AW_SYS_CLK_HOSC)
                aw_ccu_reg->ClkOutA.ClkSrc = 2;
            else if (parent == AW_SYS_CLK_LOSC)
                aw_ccu_reg->ClkOutA.ClkSrc = 1;
            else
                return -1;
            return 0;
        }
        case AW_MOD_CLK_OUTB: {
            if (parent == AW_SYS_CLK_HOSC)
                aw_ccu_reg->ClkOutB.ClkSrc = 2;
            else if (parent == AW_SYS_CLK_LOSC)
                aw_ccu_reg->ClkOutB.ClkSrc = 1;
            else
                return -1;
            return 0;
        }
        /* REMOVED */
        //case AW_MOD_CLK_GPS: {
        //    switch (parent) {
        //        case AW_SYS_CLK_HOSC:
        //            aw_ccu_reg->GpsClk.ClkSrc = 0;
        //            break;
        //        case AW_SYS_CLK_PLL6:
        //            aw_ccu_reg->GpsClk.ClkSrc = 1;
        //            break;
        //        case AW_SYS_CLK_PLL7:
        //            aw_ccu_reg->GpsClk.ClkSrc = 2;
        //            break;
        //        case AW_SYS_CLK_PLL4:
        //            aw_ccu_reg->GpsClk.ClkSrc = 3;
        //            break;
        //        default:
        //            return -1;
        //    }
        //    return 0;
        //}

        case AW_MOD_CLK_TWI0:
        case AW_MOD_CLK_TWI1:
        case AW_MOD_CLK_TWI2:
        case AW_MOD_CLK_TWI3:
        case AW_MOD_CLK_TWI4:
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
            return (parent == AW_SYS_CLK_APB1) ? 0 : -1;

        case AW_MOD_CLK_LVDS:
        default:
            return (parent == AW_SYS_CLK_NONE) ? 0 : -1;
    }
    return (parent == AW_SYS_CLK_NONE) ? 0 : -1;
}

/*
 * Set module clock on/off status.
 *
 * @id:     module clock id
 * @status: module clock on/off status
 *
 */
static __s32 mod_clk_set_status(__aw_ccu_clk_id_e id, __aw_ccu_clk_onff_e status)
{
    switch (id) {
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
        case AW_MOD_CLK_I2S0:
            aw_ccu_reg->I2s0Clk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_I2S1:
            aw_ccu_reg->I2s1Clk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_I2S2:
            aw_ccu_reg->I2s2Clk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_AC97:
            aw_ccu_reg->Ac97Clk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_SPDIF:
            aw_ccu_reg->SpdifClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_KEYPAD: {
            aw_ccu_reg->KeyPadClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_SATA: {
            aw_ccu_reg->SataClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            aw_ccu_reg->Pll6Ctl.OutputEn = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_USBPHY: {
            aw_ccu_reg->UsbClk.PhySpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_USBPHY0: {
            aw_ccu_reg->UsbClk.UsbPhy0Rst = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_USBPHY1: {
            aw_ccu_reg->UsbClk.UsbPhy1Rst = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_USBPHY2: {
            aw_ccu_reg->UsbClk.UsbPhy2Rst = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        }

        case AW_MOD_CLK_USBOHCI0:
            aw_ccu_reg->UsbClk.OHCI0SpecClkGate = ((status == AW_CCU_CLK_OFF) ? 0 : 1);
            return 0;
        case AW_MOD_CLK_USBOHCI1:
            aw_ccu_reg->UsbClk.OHCI1SpecClkGate = ((status == AW_CCU_CLK_OFF) ? 0 : 1);
            return 0;
        /* REMOVED */
        //case AW_MOD_CLK_GPS: {
        //    aw_ccu_reg->GpsClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
        //    return 0;
        //}
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
            aw_ccu_reg->Lcd0Ch0Clk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_LCD1CH0:
            aw_ccu_reg->Lcd1Ch0Clk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_CSIISP:
            aw_ccu_reg->CsiIspClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_TVDMOD1:
            aw_ccu_reg->TvdClk.Clk1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_TVDMOD2:
            aw_ccu_reg->TvdClk.Clk2Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_LCD0CH1_S1:
            aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Gate = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_MOD_CLK_LCD0CH1_S2:
            aw_ccu_reg->Lcd0Ch1Clk.SpecClk2Gate = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_MOD_CLK_LCD1CH1_S1:
            aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Gate = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_MOD_CLK_LCD1CH1_S2:
            aw_ccu_reg->Lcd1Ch1Clk.SpecClk2Gate = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_MOD_CLK_CSI0:
            aw_ccu_reg->Csi0Clk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_CSI1:
            aw_ccu_reg->Csi1Clk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_VE:
            aw_ccu_reg->VeClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_ADDA:
            aw_ccu_reg->AddaClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_AVS:
            aw_ccu_reg->AvsClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_ACE:
            aw_ccu_reg->AceClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_LVDS:
            return 0;
        case AW_MOD_CLK_HDMI:
            aw_ccu_reg->HdmiClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_MALI:
            aw_ccu_reg->MaliClk.SpecClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_MBUS:
            aw_ccu_reg->MBusClk.ClkGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_OUTA:
            aw_ccu_reg->ClkOutA.ClkEn = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_OUTB:
            aw_ccu_reg->ClkOutB.ClkEn = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;

        case AW_MOD_CLK_TWI0:
        case AW_MOD_CLK_TWI1:
        case AW_MOD_CLK_TWI2:
        case AW_MOD_CLK_TWI3:
        case AW_MOD_CLK_TWI4:
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

        case AW_AHB_CLK_USB0:
            aw_ccu_reg->AhbGate0.Usb0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_EHCI0:
            aw_ccu_reg->AhbGate0.Ehci0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_OHCI0:
            aw_ccu_reg->AhbGate0.Ohci0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_EHCI1:
            aw_ccu_reg->AhbGate0.Ehci1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_OHCI1:
            aw_ccu_reg->AhbGate0.Ohci1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SS:
            aw_ccu_reg->AhbGate0.SsGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;

        case AW_AHB_CLK_DMA:
            aw_ccu_reg->AhbGate0.DmaGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_BIST:
            aw_ccu_reg->AhbGate0.BistGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SDMMC0:
            aw_ccu_reg->AhbGate0.Sdmmc0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SDMMC1:
            aw_ccu_reg->AhbGate0.Sdmmc1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SDMMC2:
            aw_ccu_reg->AhbGate0.Sdmmc2Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SDMMC3:
            aw_ccu_reg->AhbGate0.Sdmmc3Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_MS:
            aw_ccu_reg->AhbGate0.MsGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_NAND:
            aw_ccu_reg->AhbGate0.NandGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SDRAM:
            aw_ccu_reg->AhbGate0.SdramGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_ACE:
            aw_ccu_reg->AhbGate0.AceGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_EMAC:
            aw_ccu_reg->AhbGate0.EmacGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_TS:
            aw_ccu_reg->AhbGate0.TsGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SPI0:
            aw_ccu_reg->AhbGate0.Spi0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SPI1:
            aw_ccu_reg->AhbGate0.Spi1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SPI2:
            aw_ccu_reg->AhbGate0.Spi2Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_SPI3:
            aw_ccu_reg->AhbGate0.Spi3Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        /* REMOVED */
        //case AW_AHB_CLK_PATA:
        //    aw_ccu_reg->AhbGate0.PataGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
        //    return 0;
        case AW_AHB_CLK_SATA:
            aw_ccu_reg->AhbGate0.SataGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        /* REMOVED */
        //case AW_AHB_CLK_GPS:
        //    aw_ccu_reg->AhbGate0.GpsGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
        //    return 0;
        case AW_AHB_CLK_STMR:
            aw_ccu_reg->AhbGate0.StmrGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_VE:
            aw_ccu_reg->AhbGate1.VeGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_TVD:
            aw_ccu_reg->AhbGate1.TvdGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_TVE0:
            aw_ccu_reg->AhbGate1.Tve0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_TVE1:
            aw_ccu_reg->AhbGate1.Tve1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_LCD0:
            aw_ccu_reg->AhbGate1.Lcd0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_LCD1:
            aw_ccu_reg->AhbGate1.Lcd1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_CSI0:
            aw_ccu_reg->AhbGate1.Csi0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_CSI1:
            aw_ccu_reg->AhbGate1.Csi1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_HDMI1:
            aw_ccu_reg->AhbGate1.Hdmi1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_HDMI:
            aw_ccu_reg->AhbGate1.HdmiDGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_DEBE0:
            aw_ccu_reg->AhbGate1.DeBe0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_DEBE1:
            aw_ccu_reg->AhbGate1.DeBe1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_DEFE0:
            aw_ccu_reg->AhbGate1.DeFe0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_DEFE1:
            aw_ccu_reg->AhbGate1.DeFe1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_GMAC:
            aw_ccu_reg->AhbGate1.GmacGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_MP:
            aw_ccu_reg->AhbGate1.MpGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_AHB_CLK_MALI:
            aw_ccu_reg->AhbGate1.Gpu3DGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;

        case AW_APB_CLK_ADDA:
            aw_ccu_reg->Apb0Gate.AddaGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_SPDIF:
            aw_ccu_reg->Apb0Gate.SpdifGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_AC97:
            aw_ccu_reg->Apb0Gate.Ac97Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_I2S0:
            aw_ccu_reg->Apb0Gate.Iis0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_I2S1:
            aw_ccu_reg->Apb0Gate.Iis1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_I2S2:
            aw_ccu_reg->Apb0Gate.Iis2Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_PIO:
            aw_ccu_reg->Apb0Gate.PioGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_IR0:
            aw_ccu_reg->Apb0Gate.Ir0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_IR1:
            aw_ccu_reg->Apb0Gate.Ir1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_KEYPAD:
            aw_ccu_reg->Apb0Gate.KeypadGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_TWI0:
            aw_ccu_reg->Apb1Gate.Twi0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_TWI1:
            aw_ccu_reg->Apb1Gate.Twi1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_TWI2:
            aw_ccu_reg->Apb1Gate.Twi2Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_TWI3:
            aw_ccu_reg->Apb1Gate.Twi3Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_TWI4:
            aw_ccu_reg->Apb1Gate.Twi4Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_CAN:
            aw_ccu_reg->Apb1Gate.CanGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_SCR:
            aw_ccu_reg->Apb1Gate.ScrGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_PS20:
            aw_ccu_reg->Apb1Gate.Ps20Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_PS21:
            aw_ccu_reg->Apb1Gate.Ps21Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_UART0:
            aw_ccu_reg->Apb1Gate.Uart0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_UART1:
            aw_ccu_reg->Apb1Gate.Uart1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_UART2:
            aw_ccu_reg->Apb1Gate.Uart2Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_UART3:
            aw_ccu_reg->Apb1Gate.Uart3Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_UART4:
            aw_ccu_reg->Apb1Gate.Uart4Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_UART5:
            aw_ccu_reg->Apb1Gate.Uart5Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_UART6:
            aw_ccu_reg->Apb1Gate.Uart6Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_APB_CLK_UART7:
            aw_ccu_reg->Apb1Gate.Uart7Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_VE:
            aw_ccu_reg->DramGate.VeGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_CSI0:
            aw_ccu_reg->DramGate.Csi0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_CSI1:
            aw_ccu_reg->DramGate.Csi1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_TS:
            aw_ccu_reg->DramGate.TsGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_TVD:
            aw_ccu_reg->DramGate.TvdGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_TVE0:
            aw_ccu_reg->DramGate.Tve0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_TVE1:
            aw_ccu_reg->DramGate.Tve1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_DEFE0:
            aw_ccu_reg->DramGate.DeFe0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_DEFE1:
            aw_ccu_reg->DramGate.DeFe1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_DEBE0:
            aw_ccu_reg->DramGate.DeBe0Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_DEBE1:
            aw_ccu_reg->DramGate.DeBe1Gate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_DEMP:
            aw_ccu_reg->DramGate.DeMpGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;
        case AW_DRAM_CLK_ACE:
            aw_ccu_reg->DramGate.AceGate = (status == AW_CCU_CLK_OFF) ? 0 : 1;
            return 0;

        default:
            return -1;
    }
}

/*
 * Set module clock division.
 *
 * @id:     module clock id
 * @rate:   module clock division
 *
 */
static __s32 mod_clk_set_rate(__aw_ccu_clk_id_e id, __s64 rate)
{
    switch (id) {
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
        case AW_MOD_CLK_I2S0: {
            switch (rate) {
                case 1:
                    aw_ccu_reg->I2s0Clk.ClkDiv = 0;
                    return 0;
                case 2:
                    aw_ccu_reg->I2s0Clk.ClkDiv = 1;
                    return 0;
                case 4:
                    aw_ccu_reg->I2s0Clk.ClkDiv = 2;
                    return 0;
                case 8:
                    aw_ccu_reg->I2s0Clk.ClkDiv = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_I2S1: {
            switch (rate) {
                case 1:
                    aw_ccu_reg->I2s1Clk.ClkDiv = 0;
                    return 0;
                case 2:
                    aw_ccu_reg->I2s1Clk.ClkDiv = 1;
                    return 0;
                case 4:
                    aw_ccu_reg->I2s1Clk.ClkDiv = 2;
                    return 0;
                case 8:
                    aw_ccu_reg->I2s1Clk.ClkDiv = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_I2S2: {
            switch (rate) {
                case 1:
                    aw_ccu_reg->I2s2Clk.ClkDiv = 0;
                    return 0;
                case 2:
                    aw_ccu_reg->I2s2Clk.ClkDiv = 1;
                    return 0;
                case 4:
                    aw_ccu_reg->I2s2Clk.ClkDiv = 2;
                    return 0;
                case 8:
                    aw_ccu_reg->I2s2Clk.ClkDiv = 3;
                    return 0;
                default:
                    return -1;
            }
            return -1;
        }
        case AW_MOD_CLK_AC97: {
            switch (rate) {
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
        case AW_MOD_CLK_SPDIF: {
            switch (rate) {
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
        case AW_MOD_CLK_KEYPAD: {
            if (rate > 32 * 8) {
                return -1;
            } else if (rate > 32 * 4) {
                aw_ccu_reg->KeyPadClk.ClkPreDiv = 3;
                //aw_ccu_reg->KeyPadClk.ClkDiv    = (rate>>3)-1;
                aw_ccu_reg->KeyPadClk.ClkDiv    = ((rate + 7) >> 3) - 1;
            } else if (rate > 32 * 2) {
                aw_ccu_reg->KeyPadClk.ClkPreDiv = 2;
                //aw_ccu_reg->KeyPadClk.ClkDiv    = (rate>>2)-1;
                aw_ccu_reg->KeyPadClk.ClkDiv    = ((rate + 3) >> 2) - 1;
            } else if (rate > 32 * 1) {
                aw_ccu_reg->KeyPadClk.ClkPreDiv = 1;
                //aw_ccu_reg->KeyPadClk.ClkDiv    = (rate>>1)-1;
                aw_ccu_reg->KeyPadClk.ClkDiv    = ((rate + 1) >> 1) - 1;
            } else if (rate > 32 * 0) {
                aw_ccu_reg->KeyPadClk.ClkPreDiv = 0;
                aw_ccu_reg->KeyPadClk.ClkDiv    = rate - 1;
            } else {
                return -1;
            }
            return 0;
        }
        case AW_MOD_CLK_SPI3:
            return _set_module0_clk_rate(&aw_ccu_reg->Spi3Clk, rate);
        case AW_MOD_CLK_DEBE0: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->DeBe0Clk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_DEBE1: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->DeBe1Clk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_DEFE0: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->DeFe0Clk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_DEFE1: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->DeFe1Clk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_DEMIX: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->DeMpClk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_CSIISP: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->CsiIspClk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_LCD0CH1_S1: {
            if (rate == (aw_ccu_reg->Lcd0Ch1Clk.ClkDiv + 1)) {
                aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Src = 0;
                return 0;
            } else if (rate == ((aw_ccu_reg->Lcd0Ch1Clk.ClkDiv + 1) << 1)) {
                aw_ccu_reg->Lcd0Ch1Clk.SpecClk1Src = 1;
                return 0;
            }

            return 0;
        }
        case AW_MOD_CLK_LCD0CH1_S2: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->Lcd0Ch1Clk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_LCD1CH1_S1: {
            if (rate == (aw_ccu_reg->Lcd1Ch1Clk.ClkDiv + 1)) {
                aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Src = 0;
                return 0;
            } else if (rate == ((aw_ccu_reg->Lcd1Ch1Clk.ClkDiv + 1) << 1)) {
                aw_ccu_reg->Lcd1Ch1Clk.SpecClk1Src = 1;
                return 0;
            }

            return 0;
        }
        case AW_MOD_CLK_LCD1CH1_S2: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->Lcd1Ch1Clk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_CSI0: {
            if ((rate < 1) || (rate > 32)) {
                return -1;
            }
            aw_ccu_reg->Csi0Clk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_CSI1: {
            if ((rate < 1) || (rate > 32)) {
                return -1;
            }
            aw_ccu_reg->Csi1Clk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_VE: {
            if ((rate < 1) || (rate > 8)) {
                return -1;
            }
            aw_ccu_reg->VeClk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_ACE: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->AceClk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_HDMI: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->HdmiClk.ClkDiv = rate - 1;
            return 0;
        }
        case AW_MOD_CLK_MALI: {
            if ((rate < 1) || (rate > 16)) {
                return -1;
            }
            aw_ccu_reg->MaliClk.ClkDiv = rate - 1;
            return 0;
        }
        /* REMOVED */
        //case AW_MOD_CLK_GPS: {
        //    if ((rate > 0) && (rate < 9)) {
        //        aw_ccu_reg->GpsClk.ClkDivRatio = rate - 1;
        //        return 0;
        //    }
        //    return -1;
        //}
        case AW_MOD_CLK_TVDMOD1: {
            if ((rate > 0) && (rate < 17)) {
                aw_ccu_reg->TvdClk.Clk1Div = rate - 1;
                return 0;
            }
            return -1;
        }
        case AW_MOD_CLK_TVDMOD2: {
            if ((rate > 0) && (rate < 17)) {
                aw_ccu_reg->TvdClk.Clk2Div = rate - 1;
                return 0;
            }
            return -1;
        }

        case AW_MOD_CLK_MBUS: {
#if 0
            if (rate > 16 * 8) {
                return -1;
            } else if (rate > 16 * 4) {
                aw_ccu_reg->MBusClk.ClkDivN = 3;
                //aw_ccu_reg->MBusClk.ClkDivM = (rate>>3)-1;
                aw_ccu_reg->MBusClk.ClkDivM = ((rate + 7) >> 3) - 1;
            } else if (rate > 16 * 2) {
                aw_ccu_reg->MBusClk.ClkDivN = 2;
                //aw_ccu_reg->MBusClk.ClkDivM = (rate>>2)-1;
                aw_ccu_reg->MBusClk.ClkDivM = ((rate + 3) >> 2) - 1;
            } else if (rate > 16 * 1) {
                aw_ccu_reg->MBusClk.ClkDivN = 1;
                //aw_ccu_reg->MBusClk.ClkDivM = (rate>>1)-1;
                aw_ccu_reg->MBusClk.ClkDivM = ((rate + 1) >> 1) - 1;
            } else if (rate > 16 * 0) {
                aw_ccu_reg->MBusClk.ClkDivN = 0;
                aw_ccu_reg->MBusClk.ClkDivM = rate - 1;
            } else {
                return -1;
            }
#else
            /* valid frequency of mbus:
             *  200M: N=1, M=2, rate = 6
             *  300M: N=1, M=1, rate = 4
             *  400M: N=0, M=2, rate = 3
             *
             *  FIXME
             *  mbus will up to 400M when cpu@1008M, sys vdd@1.3V
             *  delay 20us util mbus stable.
             */
            __ccmu_mbus_clk_reg015c_t mbusclk = aw_ccu_reg->MBusClk;
            if (6 == rate) {
                mbusclk.ClkDivN = 1;
                mbusclk.ClkDivM = 2;
                aw_ccu_reg->MBusClk = mbusclk;
                __delay((1008000000 >> 20) * 20);
            } else if (4 == rate) {
                mbusclk.ClkDivN = 1;
                mbusclk.ClkDivM = 1;
                aw_ccu_reg->MBusClk = mbusclk;
                __delay((1008000000 >> 20) * 20);
            } else if (3 == rate) {
                mbusclk.ClkDivN = 0;
                mbusclk.ClkDivM = 2;
                aw_ccu_reg->MBusClk = mbusclk;
                __delay((1008000000 >> 20) * 20);
            } else {
                return -1;
            }
#endif
            return 0;
        }

        case AW_MOD_CLK_OUTA: {
            __u32 tmp_rate = rate;
            if (!(tmp_rate % 750)) {
                aw_ccu_reg->ClkOutA.ClkSrc = 0;
                rate = tmp_rate / 750;
            }

            if (rate > 32 * 8) {
                return -1;
            } else if (rate > 32 * 4) {
                aw_ccu_reg->ClkOutA.ClkDivN = 3;
                //aw_ccu_reg->ClkOutA.ClkDivM = (rate>>3)-1;
                aw_ccu_reg->ClkOutA.ClkDivM = ((rate + 7) >> 3) - 1;
            } else if (rate > 32 * 2) {
                aw_ccu_reg->ClkOutA.ClkDivN = 2;
                //aw_ccu_reg->ClkOutA.ClkDivM = (rate>>2)-1;
                aw_ccu_reg->ClkOutA.ClkDivM = ((rate + 3) >> 2) - 1;
            } else if (rate > 32 * 1) {
                aw_ccu_reg->ClkOutA.ClkDivN = 1;
                //aw_ccu_reg->ClkOutA.ClkDivM = (rate>>1)-1;
                aw_ccu_reg->ClkOutA.ClkDivM = ((rate + 1) >> 1) - 1;
            } else if (rate > 32 * 0) {
                aw_ccu_reg->ClkOutA.ClkDivN = 0;
                aw_ccu_reg->ClkOutA.ClkDivM = rate - 1;
            } else {
                return -1;
            }
            return 0;
        }

        case AW_MOD_CLK_OUTB: {
            __u32 tmp_rate = rate;

            if (!(tmp_rate % 750)) {
                aw_ccu_reg->ClkOutB.ClkSrc = 0;
                rate = tmp_rate / 750;
            }

            if (rate > 32 * 8) {
                return -1;
            } else if (rate > 32 * 4) {
                aw_ccu_reg->ClkOutB.ClkDivN = 3;
                //aw_ccu_reg->ClkOutB.ClkDivM = (rate>>3)-1;
                aw_ccu_reg->ClkOutB.ClkDivM = ((rate + 7) >> 3) - 1;
            } else if (rate > 32 * 2) {
                aw_ccu_reg->ClkOutB.ClkDivN = 2;
                //aw_ccu_reg->ClkOutB.ClkDivM = (rate>>2)-1;
                aw_ccu_reg->ClkOutB.ClkDivM = ((rate + 3) >> 2) - 1;
            } else if (rate > 32 * 1) {
                aw_ccu_reg->ClkOutB.ClkDivN = 1;
                //aw_ccu_reg->ClkOutB.ClkDivM = (rate>>1)-1;
                aw_ccu_reg->ClkOutB.ClkDivM = ((rate + 1) >> 1) - 1;
            } else if (rate > 32 * 0) {
                aw_ccu_reg->ClkOutB.ClkDivN = 0;
                aw_ccu_reg->ClkOutB.ClkDivM = rate - 1;
            } else {
                return -1;
            }
            return 0;
        }

        case AW_MOD_CLK_LCD0CH0:
        case AW_MOD_CLK_LCD1CH0:
        case AW_MOD_CLK_LVDS:
        case AW_MOD_CLK_ADDA:
        case AW_MOD_CLK_SATA:
        case AW_MOD_CLK_USBPHY:
        case AW_MOD_CLK_USBPHY0:
        case AW_MOD_CLK_USBPHY1:
        case AW_MOD_CLK_USBPHY2:
        case AW_MOD_CLK_USBOHCI0:
        case AW_MOD_CLK_USBOHCI1:
        case AW_MOD_CLK_AVS:
        default:
            return (rate == 1) ? 0 : -1;
    }
    return (rate == 1) ? 0 : -1;
}

/*
 * Get module clock rate based on hz.
 *
 * @id:     module clock id
 *
 */
static __u64 mod_clk_get_rate_hz(__aw_ccu_clk_id_e id)
{
    return ccu_clk_uldiv(sys_clk_ops.get_rate(mod_clk_get_parent(id)), mod_clk_get_rate(id));
}

/*
 * Set module clock rate based on hz.
 *
 * @id:     module clock id
 * @rate:   module clock division
 *
 */
static int mod_clk_set_rate_hz(__aw_ccu_clk_id_e id, __u64 rate)
{
    __u64   parent_rate = sys_clk_ops.get_rate(mod_clk_get_parent(id));

    rate = ccu_clk_uldiv(parent_rate, rate);

    return mod_clk_set_rate(id, rate);
}

/*
 * Get module clock reset status.
 *
 * @id:     module clock id.
 *
 */
static __aw_ccu_clk_reset_e mod_clk_get_reset(__aw_ccu_clk_id_e id)
{
    switch (id) {
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
        case AW_MOD_CLK_I2S0:
        case AW_MOD_CLK_I2S1:
        case AW_MOD_CLK_I2S2:
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
        /* REMOVED */
        //case AW_MOD_CLK_GPS:
        //    return aw_ccu_reg->GpsClk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_SPI3:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_DEBE0:
            return aw_ccu_reg->DeBe0Clk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_DEBE1:
            return aw_ccu_reg->DeBe1Clk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_DEFE0:
            return aw_ccu_reg->DeFe0Clk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_DEFE1:
            return aw_ccu_reg->DeFe1Clk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_DEMIX:
            return aw_ccu_reg->DeMpClk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_LCD0CH0:
            return aw_ccu_reg->Lcd0Ch0Clk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_LCD1CH0:
            return aw_ccu_reg->Lcd1Ch0Clk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_CSIISP:
        case AW_MOD_CLK_TVDMOD1:
        case AW_MOD_CLK_TVDMOD2:
        case AW_MOD_CLK_LCD0CH1_S1:
        case AW_MOD_CLK_LCD0CH1_S2:
        case AW_MOD_CLK_LCD1CH1_S1:
        case AW_MOD_CLK_LCD1CH1_S2:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_CSI0:
            return aw_ccu_reg->Csi0Clk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_CSI1:
            return aw_ccu_reg->Csi1Clk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_VE:
            return aw_ccu_reg->VeClk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_ADDA:
        case AW_MOD_CLK_AVS:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_ACE:
            return aw_ccu_reg->AceClk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_LVDS:
            return aw_ccu_reg->LvdsClk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        case AW_MOD_CLK_HDMI:
            return AW_CCU_CLK_NRESET;
        case AW_MOD_CLK_MALI:
            return aw_ccu_reg->MaliClk.Reset ? AW_CCU_CLK_NRESET : AW_CCU_CLK_RESET;
        default:
            return AW_CCU_CLK_NRESET;
    }

}

/*
 * Set module clock reset status.
 *
 * @id:     module clock id
 * @reset:  reset status
 *
 */
static __s32 mod_clk_set_reset(__aw_ccu_clk_id_e id, __aw_ccu_clk_reset_e reset)
{
    switch (id) {
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
        case AW_MOD_CLK_I2S0:
        case AW_MOD_CLK_I2S1:
        case AW_MOD_CLK_I2S2:
        case AW_MOD_CLK_AC97:
        case AW_MOD_CLK_SPDIF:
        case AW_MOD_CLK_KEYPAD:
        case AW_MOD_CLK_SATA:
        case AW_MOD_CLK_USBPHY:
        case AW_MOD_CLK_USBOHCI0:
        case AW_MOD_CLK_USBOHCI1:
            return (reset == AW_CCU_CLK_NRESET) ? 0 : -1;
        case AW_MOD_CLK_USBPHY0:
        case AW_MOD_CLK_USBPHY1:
        case AW_MOD_CLK_USBPHY2:
            return 0;
        /* REMOVED */
        //case AW_MOD_CLK_GPS: {
        //    aw_ccu_reg->GpsClk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
        //    return 0;
        //}
        case AW_MOD_CLK_DEBE0: {
            aw_ccu_reg->DeBe0Clk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEBE1: {
            aw_ccu_reg->DeBe1Clk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEFE0: {
            aw_ccu_reg->DeFe0Clk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEFE1: {
            aw_ccu_reg->DeFe1Clk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_DEMIX: {
            aw_ccu_reg->DeMpClk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_LCD0CH0: {
            aw_ccu_reg->Lcd0Ch0Clk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_LCD1CH0: {
            aw_ccu_reg->Lcd1Ch0Clk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_CSI0: {
            aw_ccu_reg->Csi0Clk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_CSI1: {
            aw_ccu_reg->Csi1Clk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_VE: {
            aw_ccu_reg->VeClk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        }
        case AW_MOD_CLK_ACE:
            aw_ccu_reg->AceClk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_LVDS:
            aw_ccu_reg->LvdsClk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;
        case AW_MOD_CLK_MALI:
            aw_ccu_reg->MaliClk.Reset = (reset == AW_CCU_CLK_RESET) ? 0 : 1;
            return 0;

        case AW_MOD_CLK_ADDA:
        case AW_MOD_CLK_CSIISP:
        case AW_MOD_CLK_TVDMOD1:
        case AW_MOD_CLK_TVDMOD2:
        case AW_MOD_CLK_LCD0CH1_S1:
        case AW_MOD_CLK_LCD0CH1_S2:
        case AW_MOD_CLK_LCD1CH1_S1:
        case AW_MOD_CLK_LCD1CH1_S2:
        case AW_MOD_CLK_SPI3:
        case AW_MOD_CLK_AVS:
        case AW_MOD_CLK_HDMI:
        default:
            return (reset == AW_CCU_CLK_NRESET) ? 0 : -1;
    }
    return (reset == AW_CCU_CLK_NRESET) ? 0 : -1;
}

__clk_ops_t mod_clk_ops = {
    .set_status = mod_clk_set_status,
    .get_status = mod_clk_get_status,
    .set_parent = mod_clk_set_parent,
    .get_parent = mod_clk_get_parent,
    .get_rate = mod_clk_get_rate_hz,
    .set_rate = mod_clk_set_rate_hz,
    .round_rate = 0,
    .get_reset = mod_clk_get_reset,
    .set_reset = mod_clk_set_reset,
};
