/*
 * arch/arm/mach-sun7i/clock/ccm.c
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

#include "ccm_i.h"

#define make_aw_clk_inf(clk_id, clk_name)   {.id = clk_id, .name = clk_name}
__ccmu_reg_list_t *aw_ccu_reg;

__aw_ccu_clk_t aw_ccu_clk_tbl[] =
{
    make_aw_clk_inf(AW_SYS_CLK_NONE,        "sys_none"          ),
    make_aw_clk_inf(AW_SYS_CLK_LOSC,        CLK_SYS_LOSC        ),
    make_aw_clk_inf(AW_SYS_CLK_HOSC,        CLK_SYS_HOSC        ),
    make_aw_clk_inf(AW_SYS_CLK_PLL1,        CLK_SYS_PLL1        ),
    make_aw_clk_inf(AW_SYS_CLK_PLL2,        CLK_SYS_PLL2        ),
    make_aw_clk_inf(AW_SYS_CLK_PLL2X8,      CLK_SYS_PLL2X8      ),
    make_aw_clk_inf(AW_SYS_CLK_PLL3,        CLK_SYS_PLL3        ),
    make_aw_clk_inf(AW_SYS_CLK_PLL3X2,      CLK_SYS_PLL3X2      ),
    make_aw_clk_inf(AW_SYS_CLK_PLL4,        CLK_SYS_PLL4        ),
    make_aw_clk_inf(AW_SYS_CLK_PLL5,        CLK_SYS_PLL5        ),
    make_aw_clk_inf(AW_SYS_CLK_PLL5M,       CLK_SYS_PLL5M       ),
    make_aw_clk_inf(AW_SYS_CLK_PLL5P,       CLK_SYS_PLL5P       ),
    make_aw_clk_inf(AW_SYS_CLK_PLL6,        CLK_SYS_PLL6        ),
    make_aw_clk_inf(AW_SYS_CLK_PLL6M,       CLK_SYS_PLL6M       ),
    make_aw_clk_inf(AW_SYS_CLK_PLL62,       CLK_SYS_PLL62       ),
    make_aw_clk_inf(AW_SYS_CLK_PLL6X2,      CLK_SYS_PLL6X2      ),
    make_aw_clk_inf(AW_SYS_CLK_PLL7,        CLK_SYS_PLL7        ),
    make_aw_clk_inf(AW_SYS_CLK_PLL7X2,      CLK_SYS_PLL7X2      ),
    make_aw_clk_inf(AW_SYS_CLK_PLL8,        CLK_SYS_PLL8        ),
    make_aw_clk_inf(AW_SYS_CLK_CPU,         CLK_SYS_CPU         ),
    make_aw_clk_inf(AW_SYS_CLK_AXI,         CLK_SYS_AXI         ),
    make_aw_clk_inf(AW_SYS_CLK_ATB,         CLK_SYS_ATB         ),
    make_aw_clk_inf(AW_SYS_CLK_AHB,         CLK_SYS_AHB         ),
    make_aw_clk_inf(AW_SYS_CLK_APB0,        CLK_SYS_APB0        ),
    make_aw_clk_inf(AW_SYS_CLK_APB1,        CLK_SYS_APB1        ),
    make_aw_clk_inf(AW_CCU_CLK_NULL,        "null"              ),
    make_aw_clk_inf(AW_MOD_CLK_NFC,         CLK_MOD_NFC         ),
    make_aw_clk_inf(AW_MOD_CLK_MSC,         CLK_MOD_MSC         ),
    make_aw_clk_inf(AW_MOD_CLK_SDC0,        CLK_MOD_SDC0        ),
    make_aw_clk_inf(AW_MOD_CLK_SDC1,        CLK_MOD_SDC1        ),
    make_aw_clk_inf(AW_MOD_CLK_SDC2,        CLK_MOD_SDC2        ),
    make_aw_clk_inf(AW_MOD_CLK_SDC3,        CLK_MOD_SDC3        ),
    make_aw_clk_inf(AW_MOD_CLK_TS,          CLK_MOD_TS          ),
    make_aw_clk_inf(AW_MOD_CLK_SS,          CLK_MOD_SS          ),
    make_aw_clk_inf(AW_MOD_CLK_SPI0,        CLK_MOD_SPI0        ),
    make_aw_clk_inf(AW_MOD_CLK_SPI1,        CLK_MOD_SPI1        ),
    make_aw_clk_inf(AW_MOD_CLK_SPI2,        CLK_MOD_SPI2        ),
    make_aw_clk_inf(AW_MOD_CLK_PATA,        CLK_MOD_PATA        ),
    make_aw_clk_inf(AW_MOD_CLK_IR0,         CLK_MOD_IR0         ),
    make_aw_clk_inf(AW_MOD_CLK_IR1,         CLK_MOD_IR1         ),
    make_aw_clk_inf(AW_MOD_CLK_I2S0,        CLK_MOD_I2S0        ),
    make_aw_clk_inf(AW_MOD_CLK_I2S1,        CLK_MOD_I2S1        ),
    make_aw_clk_inf(AW_MOD_CLK_I2S2,        CLK_MOD_I2S2        ),
    make_aw_clk_inf(AW_MOD_CLK_AC97,        CLK_MOD_AC97        ),
    make_aw_clk_inf(AW_MOD_CLK_SPDIF,       CLK_MOD_SPDIF       ),
    make_aw_clk_inf(AW_MOD_CLK_KEYPAD,      CLK_MOD_KEYPAD      ),
    make_aw_clk_inf(AW_MOD_CLK_SATA,        CLK_MOD_SATA        ),
    make_aw_clk_inf(AW_MOD_CLK_USBPHY,      CLK_MOD_USBPHY      ),
    make_aw_clk_inf(AW_MOD_CLK_USBPHY0,     CLK_MOD_USBPHY0     ),
    make_aw_clk_inf(AW_MOD_CLK_USBPHY1,     CLK_MOD_USBPHY1     ),
    make_aw_clk_inf(AW_MOD_CLK_USBPHY2,     CLK_MOD_USBPHY2     ),
    make_aw_clk_inf(AW_MOD_CLK_USBOHCI0,    CLK_MOD_USBOHCI0    ),
    make_aw_clk_inf(AW_MOD_CLK_USBOHCI1,    CLK_MOD_USBOHCI1    ),
    make_aw_clk_inf(AW_MOD_CLK_GPS,         CLK_MOD_GPS         ),
    make_aw_clk_inf(AW_MOD_CLK_SPI3,        CLK_MOD_SPI3        ),
    make_aw_clk_inf(AW_MOD_CLK_DEBE0,       CLK_MOD_DEBE0       ),
    make_aw_clk_inf(AW_MOD_CLK_DEBE1,       CLK_MOD_DEBE1       ),
    make_aw_clk_inf(AW_MOD_CLK_DEFE0,       CLK_MOD_DEFE0       ),
    make_aw_clk_inf(AW_MOD_CLK_DEFE1,       CLK_MOD_DEFE1       ),
    make_aw_clk_inf(AW_MOD_CLK_DEMIX,       CLK_MOD_DEMIX       ),
    make_aw_clk_inf(AW_MOD_CLK_LCD0CH0,     CLK_MOD_LCD0CH0     ),
    make_aw_clk_inf(AW_MOD_CLK_LCD1CH0,     CLK_MOD_LCD1CH0     ),
    make_aw_clk_inf(AW_MOD_CLK_CSIISP,      CLK_MOD_CSIISP      ),
    make_aw_clk_inf(AW_MOD_CLK_TVDMOD1,     CLK_MOD_TVDMOD1     ),
    make_aw_clk_inf(AW_MOD_CLK_TVDMOD2,     CLK_MOD_TVDMOD2     ),
    make_aw_clk_inf(AW_MOD_CLK_LCD0CH1_S1,  CLK_MOD_LCD0CH1_S1  ),
    make_aw_clk_inf(AW_MOD_CLK_LCD0CH1_S2,  CLK_MOD_LCD0CH1_S2  ),
    make_aw_clk_inf(AW_MOD_CLK_LCD1CH1_S1,  CLK_MOD_LCD1CH1_S1  ),
    make_aw_clk_inf(AW_MOD_CLK_LCD1CH1_S2,  CLK_MOD_LCD1CH1_S2  ),
    make_aw_clk_inf(AW_MOD_CLK_CSI0,        CLK_MOD_CSI0        ),
    make_aw_clk_inf(AW_MOD_CLK_CSI1,        CLK_MOD_CSI1        ),
    make_aw_clk_inf(AW_MOD_CLK_VE,          CLK_MOD_VE          ),
    make_aw_clk_inf(AW_MOD_CLK_ADDA,        CLK_MOD_ADDA        ),
    make_aw_clk_inf(AW_MOD_CLK_AVS,         CLK_MOD_AVS         ),
    make_aw_clk_inf(AW_MOD_CLK_ACE,         CLK_MOD_ACE         ),
    make_aw_clk_inf(AW_MOD_CLK_LVDS,        CLK_MOD_LVDS        ),
    make_aw_clk_inf(AW_MOD_CLK_HDMI,        CLK_MOD_HDMI        ),
    make_aw_clk_inf(AW_MOD_CLK_MALI,        CLK_MOD_MALI        ),
    make_aw_clk_inf(AW_MOD_CLK_TWI0,        CLK_MOD_TWI0        ),
    make_aw_clk_inf(AW_MOD_CLK_TWI1,        CLK_MOD_TWI1        ),
    make_aw_clk_inf(AW_MOD_CLK_TWI2,        CLK_MOD_TWI2        ),
    make_aw_clk_inf(AW_MOD_CLK_TWI3,        CLK_MOD_TWI3        ),
    make_aw_clk_inf(AW_MOD_CLK_TWI4,        CLK_MOD_TWI4        ),
    make_aw_clk_inf(AW_MOD_CLK_CAN,         CLK_MOD_CAN         ),
    make_aw_clk_inf(AW_MOD_CLK_SCR,         CLK_MOD_SCR         ),
    make_aw_clk_inf(AW_MOD_CLK_PS20,        CLK_MOD_PS20        ),
    make_aw_clk_inf(AW_MOD_CLK_PS21,        CLK_MOD_PS21        ),
    make_aw_clk_inf(AW_MOD_CLK_UART0,       CLK_MOD_UART0       ),
    make_aw_clk_inf(AW_MOD_CLK_UART1,       CLK_MOD_UART1       ),
    make_aw_clk_inf(AW_MOD_CLK_UART2,       CLK_MOD_UART2       ),
    make_aw_clk_inf(AW_MOD_CLK_UART3,       CLK_MOD_UART3       ),
    make_aw_clk_inf(AW_MOD_CLK_UART4,       CLK_MOD_UART4       ),
    make_aw_clk_inf(AW_MOD_CLK_UART5,       CLK_MOD_UART5       ),
    make_aw_clk_inf(AW_MOD_CLK_UART6,       CLK_MOD_UART6       ),
    make_aw_clk_inf(AW_MOD_CLK_UART7,       CLK_MOD_UART7       ),
    make_aw_clk_inf(AW_MOD_CLK_SMPTWD,      CLK_MOD_SMPTWD      ),
    make_aw_clk_inf(AW_MOD_CLK_MBUS,        CLK_MOD_MBUS        ),
    make_aw_clk_inf(AW_MOD_CLK_OUTA,        CLK_MOD_OUTA        ),
    make_aw_clk_inf(AW_MOD_CLK_OUTB,        CLK_MOD_OUTB        ),
    make_aw_clk_inf(AW_AHB_CLK_USB0,        CLK_AHB_USB0        ),
    make_aw_clk_inf(AW_AHB_CLK_EHCI0,       CLK_AHB_EHCI0       ),
    make_aw_clk_inf(AW_AHB_CLK_OHCI0,       CLK_AHB_OHCI0       ),
    make_aw_clk_inf(AW_AHB_CLK_SS,          CLK_AHB_SS          ),
    make_aw_clk_inf(AW_AHB_CLK_DMA,         CLK_AHB_DMA         ),
    make_aw_clk_inf(AW_AHB_CLK_BIST,        CLK_AHB_BIST        ),
    make_aw_clk_inf(AW_AHB_CLK_SDMMC0,      CLK_AHB_SDMMC0      ),
    make_aw_clk_inf(AW_AHB_CLK_SDMMC1,      CLK_AHB_SDMMC1      ),
    make_aw_clk_inf(AW_AHB_CLK_SDMMC2,      CLK_AHB_SDMMC2      ),
    make_aw_clk_inf(AW_AHB_CLK_SDMMC3,      CLK_AHB_SDMMC3      ),
    make_aw_clk_inf(AW_AHB_CLK_MS,          CLK_AHB_MS          ),
    make_aw_clk_inf(AW_AHB_CLK_NAND,        CLK_AHB_NAND        ),
    make_aw_clk_inf(AW_AHB_CLK_SDRAM,       CLK_AHB_SDRAM       ),
    make_aw_clk_inf(AW_AHB_CLK_ACE,         CLK_AHB_ACE         ),
    make_aw_clk_inf(AW_AHB_CLK_EMAC,        CLK_AHB_EMAC        ),
    make_aw_clk_inf(AW_AHB_CLK_TS,          CLK_AHB_TS          ),
    make_aw_clk_inf(AW_AHB_CLK_SPI0,        CLK_AHB_SPI0        ),
    make_aw_clk_inf(AW_AHB_CLK_SPI1,        CLK_AHB_SPI1        ),
    make_aw_clk_inf(AW_AHB_CLK_SPI2,        CLK_AHB_SPI2        ),
    make_aw_clk_inf(AW_AHB_CLK_SPI3,        CLK_AHB_SPI3        ),
    make_aw_clk_inf(AW_AHB_CLK_PATA,        CLK_AHB_PATA        ),
    make_aw_clk_inf(AW_AHB_CLK_SATA,        CLK_AHB_SATA        ),
    make_aw_clk_inf(AW_AHB_CLK_GPS,         CLK_AHB_GPS         ),
    make_aw_clk_inf(AW_AHB_CLK_VE,          CLK_AHB_VE          ),
    make_aw_clk_inf(AW_AHB_CLK_TVD,         CLK_AHB_TVD         ),
    make_aw_clk_inf(AW_AHB_CLK_TVE0,        CLK_AHB_TVE0        ),
    make_aw_clk_inf(AW_AHB_CLK_TVE1,        CLK_AHB_TVE1        ),
    make_aw_clk_inf(AW_AHB_CLK_LCD0,        CLK_AHB_LCD0        ),
    make_aw_clk_inf(AW_AHB_CLK_LCD1,        CLK_AHB_LCD1        ),
    make_aw_clk_inf(AW_AHB_CLK_CSI0,        CLK_AHB_CSI0        ),
    make_aw_clk_inf(AW_AHB_CLK_CSI1,        CLK_AHB_CSI1        ),
    make_aw_clk_inf(AW_AHB_CLK_HDMI1,       CLK_AHB_HDMI1       ),
    make_aw_clk_inf(AW_AHB_CLK_HDMI,        CLK_AHB_HDMI        ),
    make_aw_clk_inf(AW_AHB_CLK_DEBE0,       CLK_AHB_DEBE0       ),
    make_aw_clk_inf(AW_AHB_CLK_DEBE1,       CLK_AHB_DEBE1       ),
    make_aw_clk_inf(AW_AHB_CLK_DEFE0,       CLK_AHB_DEFE0       ),
    make_aw_clk_inf(AW_AHB_CLK_DEFE1,       CLK_AHB_DEFE1       ),
    make_aw_clk_inf(AW_AHB_CLK_GMAC,        CLK_AHB_GMAC        ),
    make_aw_clk_inf(AW_AHB_CLK_MP,          CLK_AHB_MP          ),
    make_aw_clk_inf(AW_AHB_CLK_MALI,        CLK_AHB_MALI        ),
    make_aw_clk_inf(AW_AHB_CLK_EHCI1,       CLK_AHB_EHCI1       ),
    make_aw_clk_inf(AW_AHB_CLK_OHCI1,       CLK_AHB_OHCI1       ),
    make_aw_clk_inf(AW_AHB_CLK_STMR,        CLK_AHB_STMR        ),
    make_aw_clk_inf(AW_APB_CLK_ADDA,        CLK_APB_ADDA        ),
    make_aw_clk_inf(AW_APB_CLK_SPDIF,       CLK_APB_SPDIF       ),
    make_aw_clk_inf(AW_APB_CLK_AC97,        CLK_APB_AC97        ),
    make_aw_clk_inf(AW_APB_CLK_I2S0,        CLK_APB_I2S0        ),
    make_aw_clk_inf(AW_APB_CLK_I2S1,        CLK_APB_I2S1        ),
    make_aw_clk_inf(AW_APB_CLK_I2S2,        CLK_APB_I2S2        ),
    make_aw_clk_inf(AW_APB_CLK_PIO,         CLK_APB_PIO         ),
    make_aw_clk_inf(AW_APB_CLK_IR0,         CLK_APB_IR0         ),
    make_aw_clk_inf(AW_APB_CLK_IR1,         CLK_APB_IR1         ),
    make_aw_clk_inf(AW_APB_CLK_KEYPAD,      CLK_APB_KEYPAD      ),
    make_aw_clk_inf(AW_APB_CLK_TWI0,        CLK_APB_TWI0        ),
    make_aw_clk_inf(AW_APB_CLK_TWI1,        CLK_APB_TWI1        ),
    make_aw_clk_inf(AW_APB_CLK_TWI2,        CLK_APB_TWI2        ),
    make_aw_clk_inf(AW_APB_CLK_TWI3,        CLK_APB_TWI3        ),
    make_aw_clk_inf(AW_APB_CLK_TWI4,        CLK_APB_TWI4        ),
    make_aw_clk_inf(AW_APB_CLK_CAN,         CLK_APB_CAN         ),
    make_aw_clk_inf(AW_APB_CLK_SCR,         CLK_APB_SCR         ),
    make_aw_clk_inf(AW_APB_CLK_PS20,        CLK_APB_PS20        ),
    make_aw_clk_inf(AW_APB_CLK_PS21,        CLK_APB_PS21        ),
    make_aw_clk_inf(AW_APB_CLK_UART0,       CLK_APB_UART0       ),
    make_aw_clk_inf(AW_APB_CLK_UART1,       CLK_APB_UART1       ),
    make_aw_clk_inf(AW_APB_CLK_UART2,       CLK_APB_UART2       ),
    make_aw_clk_inf(AW_APB_CLK_UART3,       CLK_APB_UART3       ),
    make_aw_clk_inf(AW_APB_CLK_UART4,       CLK_APB_UART4       ),
    make_aw_clk_inf(AW_APB_CLK_UART5,       CLK_APB_UART5       ),
    make_aw_clk_inf(AW_APB_CLK_UART6,       CLK_APB_UART6       ),
    make_aw_clk_inf(AW_APB_CLK_UART7,       CLK_APB_UART7       ),
    make_aw_clk_inf(AW_DRAM_CLK_VE,         CLK_DRAM_VE         ),
    make_aw_clk_inf(AW_DRAM_CLK_CSI0,       CLK_DRAM_CSI0       ),
    make_aw_clk_inf(AW_DRAM_CLK_CSI1,       CLK_DRAM_CSI1       ),
    make_aw_clk_inf(AW_DRAM_CLK_TS,         CLK_DRAM_TS         ),
    make_aw_clk_inf(AW_DRAM_CLK_TVD,        CLK_DRAM_TVD        ),
    make_aw_clk_inf(AW_DRAM_CLK_TVE0,       CLK_DRAM_TVE0       ),
    make_aw_clk_inf(AW_DRAM_CLK_TVE1,       CLK_DRAM_TVE1       ),
    make_aw_clk_inf(AW_DRAM_CLK_DEFE0,      CLK_DRAM_DEFE0      ),
    make_aw_clk_inf(AW_DRAM_CLK_DEFE1,      CLK_DRAM_DEFE1      ),
    make_aw_clk_inf(AW_DRAM_CLK_DEBE0,      CLK_DRAM_DEBE0      ),
    make_aw_clk_inf(AW_DRAM_CLK_DEBE1,      CLK_DRAM_DEBE1      ),
    make_aw_clk_inf(AW_DRAM_CLK_DEMP,       CLK_DRAM_DEMP       ),
    make_aw_clk_inf(AW_DRAM_CLK_ACE,        CLK_DRAM_ACE        ),
    make_aw_clk_inf(AW_CCU_CLK_CNT,         "count"             ),
};

/*
 * aw ccu unit init.
 *
 * Return 0.
 */
int aw_ccu_init(void)
{
    CCU_INF("%s\n", __func__);

    /* initialise the CCU io base */
    aw_ccu_reg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE;

    return 0;
}

/*
 * aw ccu unit exit.
 *
 * Return 0.
 */
int aw_ccu_exit(void)
{
    CCU_INF("%s\n", __func__);

    aw_ccu_reg = NULL;

    return 0;
}

/*
 * Get clock information by clock id.
 *
 * @id:     clock id
 * @clk:    clock information
 *
 * Returns 0 if success, -1 indicates invalid clock id.
 */
int aw_ccu_get_clk(__aw_ccu_clk_id_e id, __ccu_clk_t *clk)
{
    __aw_ccu_clk_t *tmp_clk;

    if (clk && (id < AW_CCU_CLK_NULL)) {
        tmp_clk = &aw_ccu_clk_tbl[id];

        /* set clock operation handle */
        clk->ops = &sys_clk_ops;
        clk->aw_clk = tmp_clk;

        /* query system clock information from hardware */
        tmp_clk->parent = sys_clk_ops.get_parent(id);
        tmp_clk->onoff  = sys_clk_ops.get_status(id);
        tmp_clk->rate   = sys_clk_ops.get_rate(id);
        tmp_clk->hash   = ccu_clk_calc_hash(tmp_clk->name);
    } else if (clk && (id < AW_CCU_CLK_CNT)) {
        tmp_clk = &aw_ccu_clk_tbl[id];

        /* set clock operation handle */
        clk->ops = &mod_clk_ops;
        clk->aw_clk = tmp_clk;

        /* query system clock information from hardware */
        tmp_clk->parent = mod_clk_ops.get_parent(id);
        tmp_clk->onoff  = mod_clk_ops.get_status(id);
        tmp_clk->reset  = mod_clk_ops.get_reset(id);
        tmp_clk->rate   = mod_clk_ops.get_rate(id);
        tmp_clk->hash   = ccu_clk_calc_hash(tmp_clk->name);
    } else {
        CCU_ERR("%s: invalid clock id\n", __func__);
        return -1;
    }

    return 0;
}
