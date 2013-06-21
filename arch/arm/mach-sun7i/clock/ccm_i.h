/*
 * arch/arm/mach-sun7i/clock/ccm_i.h
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

#ifndef __AW_CCMU_I_H__
#define __AW_CCMU_I_H__

#include <linux/kernel.h>
#include <linux/printk.h>
#include <asm/io.h>
#include <asm/div64.h>
#include <mach/includes.h>

#define CCU_DEBUG_LEVEL 2

#if (CCU_DEBUG_LEVEL == 1)
    #define CCU_DBG(format,args...)     do {} while (0)
    #define CCU_INF(format,args...)     do {} while (0)
    #define CCU_ERR(format,args...)     printk(KERN_ERR "[ccu-err] "format,##args)
#elif (CCU_DEBUG_LEVEL == 2)
    #define CCU_DBG(format,args...)     do {} while (0)
    #define CCU_INF(format,args...)     printk(KERN_INFO"[ccu-inf] "format,##args)
    #define CCU_ERR(format,args...)     printk(KERN_ERR "[ccu-err] "format,##args)
#elif (CCU_DEBUG_LEVEL == 3)
    #define CCU_DBG(format,args...)     printk(KERN_INFO"[ccu-dbg] "format,##args)
    #define CCU_INF(format,args...)     printk(KERN_INFO"[ccu-inf] "format,##args)
    #define CCU_ERR(format,args...)     printk(KERN_ERR "[ccu-err] "format,##args)
#endif

/* define system clock id */
typedef enum __AW_CCU_CLK_ID
{
    AW_SYS_CLK_NONE,        /* invalid clock id                                 */

    /* system clock defile */
    AW_SYS_CLK_LOSC,        /* "losc"           ,LOSC, 32768 hz clock           */
    AW_SYS_CLK_HOSC,        /* "hosc"           ,HOSC, 24Mhz clock              */
    AW_SYS_CLK_PLL1,        /* "core_pll"       ,PLL1 clock                     */
    AW_SYS_CLK_PLL2,        /* "audio_pll"      ,PLL2 clock                     */
    AW_SYS_CLK_PLL2X8,      /* "audio_pllx8"    ,PLL2 8x clock                  */
    AW_SYS_CLK_PLL3,        /* "video_pll0"     ,PLL3 clock                     */
    AW_SYS_CLK_PLL3X2,      /* "video_pll0x2"   ,PLL3 2x clock                  */
    AW_SYS_CLK_PLL4,        /* "ve_pll"         ,PLL4 clock                     */
    AW_SYS_CLK_PLL5,        /* "sdram_pll"      ,PLL5 clock                     */
    AW_SYS_CLK_PLL5M,       /* "sdram_pll_m"    ,PLL5 M clock                   */
    AW_SYS_CLK_PLL5P,       /* "sdram_pll_p"    ,PLL5 P clock                   */
    AW_SYS_CLK_PLL6,        /* "sata_pll"       ,PLL6 clock                     */
    AW_SYS_CLK_PLL6M,       /* "sata_pll_m"     ,PLL6 M clock, just for SATA    */
    AW_SYS_CLK_PLL62,       /* "sata_pll_2"     ,PLL6 2 clock, for module       */
    AW_SYS_CLK_PLL6X2,      /* "sata_pllx2"     ,PLL6 2x clock, for module      */
    AW_SYS_CLK_PLL7,        /* "video_pll1"     ,PLL7 clock                     */
    AW_SYS_CLK_PLL7X2,      /* "video_pll1x2"   ,PLL7 2x clock                  */
    AW_SYS_CLK_PLL8,        /* "gpu_pll"        ,PLL8 clock                     */
    AW_SYS_CLK_CPU,         /* "cpu"            ,CPU clock                      */
    AW_SYS_CLK_AXI,         /* "axi"            ,AXI clock                      */
    AW_SYS_CLK_ATB,         /* "atb"            ,ATB clock                      */
    AW_SYS_CLK_AHB,         /* "ahb"            ,AHB clock                      */
    AW_SYS_CLK_APB0,        /* "apb"            ,APB0 clock                     */
    AW_SYS_CLK_APB1,        /* "apb1"           ,APB1 clock                     */

    AW_CCU_CLK_NULL,        /* invalid clock id                                 */

    /* module clock define */
    AW_MOD_CLK_NFC,         /* "nfc"                                            */
    AW_MOD_CLK_MSC,         /* "msc"                                            */
    AW_MOD_CLK_SDC0,        /* "sdc0"                                           */
    AW_MOD_CLK_SDC1,        /* "sdc1"                                           */
    AW_MOD_CLK_SDC2,        /* "sdc2"                                           */
    AW_MOD_CLK_SDC3,        /* "sdc3"                                           */
    AW_MOD_CLK_TS,          /* "ts"                                             */
    AW_MOD_CLK_SS,          /* "ss"                                             */
    AW_MOD_CLK_SPI0,        /* "spi0"                                           */
    AW_MOD_CLK_SPI1,        /* "spi1"                                           */
    AW_MOD_CLK_SPI2,        /* "spi2"                                           */
    AW_MOD_CLK_PATA,        /* "pata"                                           */
    AW_MOD_CLK_IR0,         /* "ir0"                                            */
    AW_MOD_CLK_IR1,         /* "ir1"                                            */
    AW_MOD_CLK_I2S0,        /* "i2s0"                                           */
    AW_MOD_CLK_I2S1,        /* "i2s1"                                           */
    AW_MOD_CLK_I2S2,        /* "i2s2"                                           */
    AW_MOD_CLK_AC97,        /* "ac97"                                           */
    AW_MOD_CLK_SPDIF,       /* "spdif"                                          */
    AW_MOD_CLK_KEYPAD,      /* "key_pad"                                        */
    AW_MOD_CLK_SATA,        /* "sata"                                           */
    AW_MOD_CLK_USBPHY,      /* "usb_phy"                                        */
    AW_MOD_CLK_USBPHY0,     /* "usb_phy0"                                       */
    AW_MOD_CLK_USBPHY1,     /* "usb_phy1"                                       */
    AW_MOD_CLK_USBPHY2,     /* "usb_phy2"                                       */
    AW_MOD_CLK_USBOHCI0,    /* "usb_ohci0"                                      */
    AW_MOD_CLK_USBOHCI1,    /* "usb_ohci1"                                      */
    AW_MOD_CLK_GPS,         /* "com"                                            */
    AW_MOD_CLK_SPI3,        /* "spi3"                                           */
    AW_MOD_CLK_DEBE0,       /* "de_image0"                                      */
    AW_MOD_CLK_DEBE1,       /* "de_image1"                                      */
    AW_MOD_CLK_DEFE0,       /* "de_scale0"                                      */
    AW_MOD_CLK_DEFE1,       /* "de_scale1"                                      */
    AW_MOD_CLK_DEMIX,       /* "de_mix"                                         */
    AW_MOD_CLK_LCD0CH0,     /* "lcd0_ch0"                                       */
    AW_MOD_CLK_LCD1CH0,     /* "lcd1_ch0"                                       */
    AW_MOD_CLK_CSIISP,      /* "csi_isp"                                        */
    AW_MOD_CLK_TVDMOD1,     /* "tvdmod1"                                        */
    AW_MOD_CLK_TVDMOD2,     /* "tvdmod2"                                        */
    AW_MOD_CLK_LCD0CH1_S1,  /* "lcd0_ch1_s1"                                    */
    AW_MOD_CLK_LCD0CH1_S2,  /* "lcd0_ch1_s2"                                    */
    AW_MOD_CLK_LCD1CH1_S1,  /* "lcd1_ch1_s1"                                    */
    AW_MOD_CLK_LCD1CH1_S2,  /* "lcd1_ch1_s2"                                    */
    AW_MOD_CLK_CSI0,        /* "csi0"                                           */
    AW_MOD_CLK_CSI1,        /* "csi1"                                           */
    AW_MOD_CLK_VE,          /* "ve"                                             */
    AW_MOD_CLK_ADDA,        /* "audio_codec"                                    */
    AW_MOD_CLK_AVS,         /* "avs"                                            */
    AW_MOD_CLK_ACE,         /* "ace"                                            */
    AW_MOD_CLK_LVDS,        /* "lvds"                                           */
    AW_MOD_CLK_HDMI,        /* "hdmi"                                           */
    AW_MOD_CLK_MALI,        /* "mali"                                           */
    AW_MOD_CLK_TWI0,        /* "twi0"                                           */
    AW_MOD_CLK_TWI1,        /* "twi1"                                           */
    AW_MOD_CLK_TWI2,        /* "twi2"                                           */
    AW_MOD_CLK_TWI3,        /* "twi3"                                           */
    AW_MOD_CLK_TWI4,        /* "twi4"                                           */
    AW_MOD_CLK_CAN,         /* "can"                                            */
    AW_MOD_CLK_SCR,         /* "scr"                                            */
    AW_MOD_CLK_PS20,        /* "ps0"                                            */
    AW_MOD_CLK_PS21,        /* "ps1"                                            */
    AW_MOD_CLK_UART0,       /* "uart0"                                          */
    AW_MOD_CLK_UART1,       /* "uart1"                                          */
    AW_MOD_CLK_UART2,       /* "uart2"                                          */
    AW_MOD_CLK_UART3,       /* "uart3"                                          */
    AW_MOD_CLK_UART4,       /* "uart4"                                          */
    AW_MOD_CLK_UART5,       /* "uart5"                                          */
    AW_MOD_CLK_UART6,       /* "uart6"                                          */
    AW_MOD_CLK_UART7,       /* "uart7"                                          */
    AW_MOD_CLK_SMPTWD,      /* "smp_twd"                                        */
    AW_MOD_CLK_MBUS,        /* "mbus"                                           */
    AW_MOD_CLK_OUTA,        /* "clkout_a"                                       */
    AW_MOD_CLK_OUTB,        /* "clkout_b"                                       */

    /* clock gating for hang to AHB bus */
    AW_AHB_CLK_USB0,        /* "ahb_usb0"                                       */
    AW_AHB_CLK_EHCI0,       /* "ahb_ehci0"                                      */
    AW_AHB_CLK_OHCI0,       /* "ahb_ohci0"                                      */
    AW_AHB_CLK_SS,          /* "ahb_ss"                                         */
    AW_AHB_CLK_DMA,         /* "ahb_dma"                                        */
    AW_AHB_CLK_BIST,        /* "ahb_bist"                                       */
    AW_AHB_CLK_SDMMC0,      /* "ahb_sdc0"                                       */
    AW_AHB_CLK_SDMMC1,      /* "ahb_sdc1"                                       */
    AW_AHB_CLK_SDMMC2,      /* "ahb_sdc2"                                       */
    AW_AHB_CLK_SDMMC3,      /* "ahb_sdc3"                                       */
    AW_AHB_CLK_MS,          /* "ahb_msc"                                        */
    AW_AHB_CLK_NAND,        /* "ahb_nfc"                                        */
    AW_AHB_CLK_SDRAM,       /* "ahb_sdramc"                                     */
    AW_AHB_CLK_ACE,         /* "ahb_ace"                                        */
    AW_AHB_CLK_EMAC,        /* "ahb_emac"                                       */
    AW_AHB_CLK_TS,          /* "ahb_ts"                                         */
    AW_AHB_CLK_SPI0,        /* "ahb_spi0"                                       */
    AW_AHB_CLK_SPI1,        /* "ahb_spi1"                                       */
    AW_AHB_CLK_SPI2,        /* "ahb_spi2"                                       */
    AW_AHB_CLK_SPI3,        /* "ahb_spi3"                                       */
    AW_AHB_CLK_PATA,        /* "ahb_pata"                                       */
    AW_AHB_CLK_SATA,        /* "ahb_sata"                                       */
    AW_AHB_CLK_GPS,         /* "ahb_com"                                        */
    AW_AHB_CLK_VE,          /* "ahb_ve"                                         */
    AW_AHB_CLK_TVD,         /* "ahb_tvd"                                        */
    AW_AHB_CLK_TVE0,        /* "ahb_tve0"                                       */
    AW_AHB_CLK_TVE1,        /* "ahb_tve1"                                       */
    AW_AHB_CLK_LCD0,        /* "ahb_lcd0"                                       */
    AW_AHB_CLK_LCD1,        /* "ahb_lcd1"                                       */
    AW_AHB_CLK_CSI0,        /* "ahb_csi0"                                       */
    AW_AHB_CLK_CSI1,        /* "ahb_csi1"                                       */
    AW_AHB_CLK_HDMI1,       /* "ahb_hdmi1"                                      */
    AW_AHB_CLK_HDMI,        /* "ahb_hdmi"                                       */
    AW_AHB_CLK_DEBE0,       /* "ahb_de_image0"                                  */
    AW_AHB_CLK_DEBE1,       /* "ahb_de_image1"                                  */
    AW_AHB_CLK_DEFE0,       /* "ahb_de_scale0"                                  */
    AW_AHB_CLK_DEFE1,       /* "ahb_de_scale1"                                  */
    AW_AHB_CLK_GMAC,        /* "ahb_gmac" */
    AW_AHB_CLK_MP,          /* "ahb_de_mix"                                     */
    AW_AHB_CLK_MALI,        /* "ahb_mali"                                       */
    AW_AHB_CLK_EHCI1,       /* "ahb_ehci1"                                      */
    AW_AHB_CLK_OHCI1,       /* "ahb_ohci1"                                      */
    AW_AHB_CLK_STMR,        /* "ahb_stmr"                                       */

    /* clock gating for hang APB bus */
    AW_APB_CLK_ADDA,        /* "apb_audio_codec"                                */
    AW_APB_CLK_SPDIF,       /* "apb_spdif"                                      */
    AW_APB_CLK_AC97,        /* "apb_ac97"                                       */
    AW_APB_CLK_I2S0,        /* "apb_i2s0"                                       */
    AW_APB_CLK_I2S1,        /* "apb_i2s1"                                       */
    AW_APB_CLK_I2S2,        /* "apb_i2s2"                                       */
    AW_APB_CLK_PIO,         /* "apb_pio"                                        */
    AW_APB_CLK_IR0,         /* "apb_ir0"                                        */
    AW_APB_CLK_IR1,         /* "apb_ir1"                                        */
    AW_APB_CLK_KEYPAD,      /* "apb_key_pad"                                    */
    AW_APB_CLK_TWI0,        /* "apb_twi0"                                       */
    AW_APB_CLK_TWI1,        /* "apb_twi1"                                       */
    AW_APB_CLK_TWI2,        /* "apb_twi2"                                       */
    AW_APB_CLK_TWI3,        /* "apb_twi3"                                       */
    AW_APB_CLK_TWI4,        /* "apb_twi4"                                       */
    AW_APB_CLK_CAN,         /* "apb_can"                                        */
    AW_APB_CLK_SCR,         /* "apb_scr"                                        */
    AW_APB_CLK_PS20,        /* "apb_ps0"                                        */
    AW_APB_CLK_PS21,        /* "apb_ps1"                                        */
    AW_APB_CLK_UART0,       /* "apb_uart0"                                      */
    AW_APB_CLK_UART1,       /* "apb_uart1"                                      */
    AW_APB_CLK_UART2,       /* "apb_uart2"                                      */
    AW_APB_CLK_UART3,       /* "apb_uart3"                                      */
    AW_APB_CLK_UART4,       /* "apb_uart4"                                      */
    AW_APB_CLK_UART5,       /* "apb_uart5"                                      */
    AW_APB_CLK_UART6,       /* "apb_uart6"                                      */
    AW_APB_CLK_UART7,       /* "apb_uart7"                                      */

    /* clock gating for access dram */
    AW_DRAM_CLK_VE,         /* "sdram_ve"                                       */
    AW_DRAM_CLK_CSI0,       /* "sdram_csi0"                                     */
    AW_DRAM_CLK_CSI1,       /* "sdram_csi1"                                     */
    AW_DRAM_CLK_TS,         /* "sdram_ts"                                       */
    AW_DRAM_CLK_TVD,        /* "sdram_tvd"                                      */
    AW_DRAM_CLK_TVE0,       /* "sdram_tve0"                                     */
    AW_DRAM_CLK_TVE1,       /* "sdram_tve1"                                     */
    AW_DRAM_CLK_DEFE0,      /* "sdram_de_scale0"                                */
    AW_DRAM_CLK_DEFE1,      /* "sdram_de_scale1"                                */
    AW_DRAM_CLK_DEBE0,      /* "sdram_de_image0"                                */
    AW_DRAM_CLK_DEBE1,      /* "sdram_de_image1"                                */
    AW_DRAM_CLK_DEMP,       /* "sdram_de_mix"                                   */
    AW_DRAM_CLK_ACE,        /* "sdram_ace"                                      */

    AW_CCU_CLK_CNT,         /* invalid clock id                                 */

} __aw_ccu_clk_id_e;

/* define handle for moduel clock */
typedef struct __AW_CCU_CLK {
    __aw_ccu_clk_id_e       id;     /* clock id         */
    __aw_ccu_clk_id_e       parent; /* parent clock id  */
    char                    *name;  /* clock name       */
    __aw_ccu_clk_onff_e     onoff;  /* on/off status    */
    __aw_ccu_clk_reset_e    reset;  /* reset status     */
    __u64                   rate;   /* clock rate, frequency for system clock,
                                     * division for module clock
                                     */
    __s32                   hash;   /* hash value, for fast search without
                                     * string compare
                                     */
} __aw_ccu_clk_t;

typedef struct clk_ops {
    int                  (*set_status)(__aw_ccu_clk_id_e id, __aw_ccu_clk_onff_e status);
    __aw_ccu_clk_onff_e  (*get_status)(__aw_ccu_clk_id_e id                            );
    int                  (*set_parent)(__aw_ccu_clk_id_e id, __aw_ccu_clk_id_e parent  );
    __aw_ccu_clk_id_e    (*get_parent)(__aw_ccu_clk_id_e id                            );
    int                  (*set_rate  )(__aw_ccu_clk_id_e id, __u64 rate                );
    __u64                (*round_rate)(__aw_ccu_clk_id_e id, __u64 rate                );
    __u64                (*get_rate  )(__aw_ccu_clk_id_e id                            );
    int                  (*set_reset )(__aw_ccu_clk_id_e id, __aw_ccu_clk_reset_e      );
    __aw_ccu_clk_reset_e (*get_reset )(__aw_ccu_clk_id_e id                            );
} __clk_ops_t;

/*
 * calculate hash value of a string.
 *
 * @string: string
 *
 * Return hash value.
 */
static inline __s32 ccu_clk_calc_hash(char *string)
{
    __s32 tmpLen, i, tmpHash = 0;

    if (!string) {
        return 0;
    }

    tmpLen = strlen(string);
    for (i = 0; i < tmpLen; i++) {
        tmpHash += string[i];
    }

    return tmpHash;
}

static inline __u64 ccu_clk_uldiv(__u64 dividend, __u32 divisior)
{
    __u32 rem = 0;

    /* quotient stored in dividend */
    rem = do_div(dividend, divisior);
    if (0 != rem)
        dividend += 1;

    return dividend;
}


extern __aw_ccu_clk_t aw_ccu_clk_tbl[];
extern __ccu_clk_t    aw_clock[];

extern __ccmu_reg_list_t *aw_ccu_reg;

/* clock operations */
extern __clk_ops_t sys_clk_ops;
extern __clk_ops_t mod_clk_ops;

int aw_ccu_init(void);
int aw_ccu_exit(void);
int aw_ccu_get_clk(__aw_ccu_clk_id_e id, __ccu_clk_t *clk);

struct core_pll_factor_t {
    __u8 FactorN;
    __u8 FactorK;
    __u8 FactorM;
    __u8 FactorP;
};

int ccm_clk_get_pll_para(struct core_pll_factor_t *factor, __u64 rate);

#endif
