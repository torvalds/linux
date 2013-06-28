/*
 * arch/arm/mach-sun7i/include/mach/clock.h
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

#ifndef __AW_CLOCK_H__
#define __AW_CLOCK_H__

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/clocksource.h>

#define CCU_LOCK_INIT(lock)     spin_lock_init(lock)
#define CCU_LOCK_DEINIT(lock)   do{} while(0)
#define CCU_LOCK(lock, flags)   spin_lock_irqsave((lock), (flags))
#define CCU_UNLOCK(lock, flags) spin_unlock_irqrestore((lock), (flags))
#define DEFINE_FLAGS(x)         unsigned long x

/* define clock error type      */
typedef enum __AW_CCU_ERR {
    AW_CCU_ERR_NONE     =  0,
    AW_CCU_ERR_PARA_NUL = -1,
    AW_CCU_ERR_PARA_INV = -2,

} __aw_ccu_err_e;


typedef enum __AW_CCU_CLK_ONOFF {
    AW_CCU_CLK_OFF      = 0,
    AW_CCU_CLK_ON       = 1,

} __aw_ccu_clk_onff_e;


typedef enum __AW_CCU_CLK_RESET {
    AW_CCU_CLK_RESET    = 0,
    AW_CCU_CLK_NRESET   = 1,

} __aw_ccu_clk_reset_e;

/*
 * We encourage you use macro rather than clock name directly.
 * This macro making driver porting more convenience in
 * different platform.
 */
/* define system clock name */
#define CLK_SYS_LOSC            "losc"
#define CLK_SYS_HOSC            "hosc"
#define CLK_SYS_PLL1            "core_pll"
#define CLK_SYS_PLL2            "audio_pll"
#define CLK_SYS_PLL2X8          "audio_pllx8"
#define CLK_SYS_PLL3            "video_pll0"
#define CLK_SYS_PLL3X2          "video_pll0x2"
#define CLK_SYS_PLL4            "ve_pll"
#define CLK_SYS_PLL5            "sdram_pll"
#define CLK_SYS_PLL5M           "sdram_pll_m"
#define CLK_SYS_PLL5P           "sdram_pll_p"
#define CLK_SYS_PLL6            "sata_pll"
#define CLK_SYS_PLL6M           "sata_pll_m"
#define CLK_SYS_PLL62           "sata_pll_2"
#define CLK_SYS_PLL6X2          "sata_pllx2"
#define CLK_SYS_PLL7            "video_pll1"
#define CLK_SYS_PLL7X2          "video_pll1x2"
#define CLK_SYS_PLL8            "gpu_pll"
#define CLK_SYS_CPU             "cpu"
#define CLK_SYS_AXI             "axi"
#define CLK_SYS_ATB             "atb"
#define CLK_SYS_AHB             "ahb"
#define CLK_SYS_APB0            "apb"
#define CLK_SYS_APB1            "apb1"

/* define module clock name */
#define CLK_MOD_NFC             "nfc"
#define CLK_MOD_MSC             "msc"
#define CLK_MOD_SDC0            "sdc0"
#define CLK_MOD_SDC1            "sdc1"
#define CLK_MOD_SDC2            "sdc2"
#define CLK_MOD_SDC3            "sdc3"
#define CLK_MOD_TS              "ts"
#define CLK_MOD_SS              "ss"
#define CLK_MOD_SPI0            "spi0"
#define CLK_MOD_SPI1            "spi1"
#define CLK_MOD_SPI2            "spi2"
#define CLK_MOD_PATA            "pata"
#define CLK_MOD_IR0             "ir0"
#define CLK_MOD_IR1             "ir1"
#define CLK_MOD_I2S0            "i2s0"
#define CLK_MOD_I2S1            "i2s1"
#define CLK_MOD_I2S2            "i2s2"
#define CLK_MOD_AC97            "ac97"
#define CLK_MOD_SPDIF           "spdif"
#define CLK_MOD_KEYPAD          "key_pad"
#define CLK_MOD_SATA            "sata"
#define CLK_MOD_USBPHY          "usb_phy"
#define CLK_MOD_USBPHY0         "usb_phy0"
#define CLK_MOD_USBPHY1         "usb_phy1"
#define CLK_MOD_USBPHY2         "usb_phy2"
#define CLK_MOD_USBOHCI0        "usb_ohci0"
#define CLK_MOD_USBOHCI1        "usb_ohci1"
#define CLK_MOD_GPS             "com"
#define CLK_MOD_SPI3            "spi3"
#define CLK_MOD_DEBE0           "de_image0"
#define CLK_MOD_DEBE1           "de_image1"
#define CLK_MOD_DEFE0           "de_scale0"
#define CLK_MOD_DEFE1           "de_scale1"
#define CLK_MOD_DEMIX           "de_mix"
#define CLK_MOD_LCD0CH0         "lcd0_ch0"
#define CLK_MOD_LCD1CH0         "lcd1_ch0"
#define CLK_MOD_CSIISP          "csi_isp"
#define CLK_MOD_TVDMOD1         "tvdmod1"
#define CLK_MOD_TVDMOD2         "tvdmod2"
#define CLK_MOD_LCD0CH1_S1      "lcd0_ch1_s1"
#define CLK_MOD_LCD0CH1_S2      "lcd0_ch1_s2"
#define CLK_MOD_LCD1CH1_S1      "lcd1_ch1_s1"
#define CLK_MOD_LCD1CH1_S2      "lcd1_ch1_s2"
#define CLK_MOD_CSI0            "csi0"
#define CLK_MOD_CSI1            "csi1"
#define CLK_MOD_VE              "ve"
#define CLK_MOD_ADDA            "audio_codec"
#define CLK_MOD_AVS             "avs"
#define CLK_MOD_ACE             "ace"
#define CLK_MOD_LVDS            "lvds"
#define CLK_MOD_HDMI            "hdmi"
#define CLK_MOD_MALI            "mali"
#define CLK_MOD_TWI0            "twi0"
#define CLK_MOD_TWI1            "twi1"
#define CLK_MOD_TWI2            "twi2"
#define CLK_MOD_TWI3            "twi3"
#define CLK_MOD_TWI4            "twi4"
#define CLK_MOD_CAN             "can"
#define CLK_MOD_SCR             "scr"
#define CLK_MOD_PS20            "ps0"
#define CLK_MOD_PS21            "ps1"
#define CLK_MOD_UART0           "uart0"
#define CLK_MOD_UART1           "uart1"
#define CLK_MOD_UART2           "uart2"
#define CLK_MOD_UART3           "uart3"
#define CLK_MOD_UART4           "uart4"
#define CLK_MOD_UART5           "uart5"
#define CLK_MOD_UART6           "uart6"
#define CLK_MOD_UART7           "uart7"
#define CLK_MOD_SMPTWD          "smp_twd"
#define CLK_MOD_MBUS            "mbus"
#define CLK_MOD_OUTA            "clkout_a"
#define CLK_MOD_OUTB            "clkout_b"

/* define ahb module gating clock */
#define CLK_AHB_USB0            "ahb_usb0"
#define CLK_AHB_EHCI0           "ahb_ehci0"
#define CLK_AHB_OHCI0           "ahb_ohci0"
#define CLK_AHB_SS              "ahb_ss"
#define CLK_AHB_DMA             "ahb_dma"
#define CLK_AHB_BIST            "ahb_bist"
#define CLK_AHB_SDMMC0          "ahb_sdc0"
#define CLK_AHB_SDMMC1          "ahb_sdc1"
#define CLK_AHB_SDMMC2          "ahb_sdc2"
#define CLK_AHB_SDMMC3          "ahb_sdc3"
#define CLK_AHB_MS              "ahb_msc"
#define CLK_AHB_NAND            "ahb_nfc"
#define CLK_AHB_SDRAM           "ahb_sdramc"
#define CLK_AHB_ACE             "ahb_ace"
#define CLK_AHB_EMAC            "ahb_emac"
#define CLK_AHB_TS              "ahb_ts"
#define CLK_AHB_SPI0            "ahb_spi0"
#define CLK_AHB_SPI1            "ahb_spi1"
#define CLK_AHB_SPI2            "ahb_spi2"
#define CLK_AHB_SPI3            "ahb_spi3"
#define CLK_AHB_PATA            "ahb_pata"
#define CLK_AHB_SATA            "ahb_sata"
#define CLK_AHB_GPS             "ahb_com"
#define CLK_AHB_VE              "ahb_ve"
#define CLK_AHB_TVD             "ahb_tvd"
#define CLK_AHB_TVE0            "ahb_tve0"
#define CLK_AHB_TVE1            "ahb_tve1"
#define CLK_AHB_LCD0            "ahb_lcd0"
#define CLK_AHB_LCD1            "ahb_lcd1"
#define CLK_AHB_CSI0            "ahb_csi0"
#define CLK_AHB_CSI1            "ahb_csi1"
#define CLK_AHB_HDMI1           "ahb_hdmi1"
#define CLK_AHB_HDMI            "ahb_hdmi"
#define CLK_AHB_DEBE0           "ahb_de_image0"
#define CLK_AHB_DEBE1           "ahb_de_image1"
#define CLK_AHB_DEFE0           "ahb_de_scale0"
#define CLK_AHB_DEFE1           "ahb_de_scale1"
#define CLK_AHB_GMAC            "ahb_gmac"
#define CLK_AHB_MP              "ahb_de_mix"
#define CLK_AHB_MALI            "ahb_mali"
#define CLK_AHB_EHCI1           "ahb_ehci1"
#define CLK_AHB_OHCI1           "ahb_ohci1"
#define CLK_AHB_STMR            "ahb_stmr"

/* define apb module gatine clock */
#define CLK_APB_ADDA            "apb_audio_codec"
#define CLK_APB_SPDIF           "apb_spdif"
#define CLK_APB_AC97            "apb_ac97"
#define CLK_APB_I2S0            "apb_i2s0"
#define CLK_APB_I2S1            "apb_i2s1"
#define CLK_APB_I2S2            "apb_i2s2"
#define CLK_APB_PIO             "apb_pio"
#define CLK_APB_IR0             "apb_ir0"
#define CLK_APB_IR1             "apb_ir1"
#define CLK_APB_KEYPAD          "apb_key_pad"
#define CLK_APB_TWI0            "apb_twi0"
#define CLK_APB_TWI1            "apb_twi1"
#define CLK_APB_TWI2            "apb_twi2"
#define CLK_APB_TWI3            "apb_twi3"
#define CLK_APB_TWI4            "apb_twi4"
#define CLK_APB_CAN             "apb_can"
#define CLK_APB_SCR             "apb_scr"
#define CLK_APB_PS20            "apb_ps0"
#define CLK_APB_PS21            "apb_ps1"
#define CLK_APB_UART0           "apb_uart0"
#define CLK_APB_UART1           "apb_uart1"
#define CLK_APB_UART2           "apb_uart2"
#define CLK_APB_UART3           "apb_uart3"
#define CLK_APB_UART4           "apb_uart4"
#define CLK_APB_UART5           "apb_uart5"
#define CLK_APB_UART6           "apb_uart6"
#define CLK_APB_UART7           "apb_uart7"

/* define dram module gating clock */
#define CLK_DRAM_VE             "sdram_ve"
#define CLK_DRAM_CSI0           "sdram_csi0"
#define CLK_DRAM_CSI1           "sdram_csi1"
#define CLK_DRAM_TS             "sdram_ts"
#define CLK_DRAM_TVD            "sdram_tvd"
#define CLK_DRAM_TVE0           "sdram_tve0"
#define CLK_DRAM_TVE1           "sdram_tve1"
#define CLK_DRAM_DEFE0          "sdram_de_scale0"
#define CLK_DRAM_DEFE1          "sdram_de_scale1"
#define CLK_DRAM_DEBE0          "sdram_de_image0"
#define CLK_DRAM_DEBE1          "sdram_de_image1"
#define CLK_DRAM_DEMP           "sdram_de_mix"
#define CLK_DRAM_ACE            "sdram_ace"

struct __AW_CCU_CLK;
struct clk_ops;

typedef struct clk {
    struct __AW_CCU_CLK *aw_clk;    /* clock handle from ccu csp */
    struct clk_ops      *ops;       /* clock operation handle */
    int                 enable;     /* enable count, when it down to 0, it will be disalbe */
    spinlock_t          lock;       /* to synchronize the clock setting */
} __ccu_clk_t;

int clk_reset(struct clk *clk, int reset);
const char *clk_name(struct clk *clk);
cycle_t aw_clksrc_read(struct clocksource *cs);

#endif
