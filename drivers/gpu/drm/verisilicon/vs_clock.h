/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */


#ifndef __VS_CLOCK_H_
#define __VS_CLOCK_H_

static inline u32 saif_get_reg(u32 addr,u32 shift,u32 mask)
{
	void __iomem *io_addr = ioremap(addr, 0x10000);
    u32 tmp;
    tmp = readl(io_addr);
    tmp = (tmp & mask) >> shift;
    return tmp;
}

static inline void saif_set_reg(u32 addr,u32 data,u32 shift,u32 mask)
{
	void __iomem *io_addr = ioremap(addr, 0x10000);

    u32 tmp;
    tmp = readl(io_addr);
    tmp &= ~mask;
    tmp |= (data<<shift) & mask;
    writel(tmp,io_addr);
}

static inline void saif_assert_rst(u32 addr,u32 addr_status,u32 mask)
{
	void __iomem *io_addr = ioremap(addr, 0x4);
	
	void __iomem *io_addr_status = ioremap(addr_status, 0x4);

    u32 tmp;
    tmp = readl(io_addr);
    tmp |= mask;
    writel(tmp,io_addr);
    do{
        tmp = readl(io_addr_status);
    }while((tmp&mask)!=0);
}

static inline void saif_clear_rst (u32 addr,u32 addr_status,u32 mask)
{
	void __iomem *io_addr = ioremap(addr, 0x4);
	
	void __iomem *io_addr_status = ioremap(addr_status, 0x4);

    u32 tmp;
    tmp = readl(io_addr);
    tmp &= ~mask;
    writel(tmp,io_addr);
    do{
        tmp = readl(io_addr_status);
    }while((tmp&mask)!=mask);
}

#define  U0_DW_UART__SAIF_BD_APB__BASE_ADDR                                                                   0x0010000000 
#define  U1_DW_UART__SAIF_BD_APB__BASE_ADDR                                                                   0x0010010000 
#define  U2_DW_UART__SAIF_BD_APB__BASE_ADDR                                                                   0x0010020000 
#define  U0_DW_I2C__SAIF_BD_APB__BASE_ADDR                                                                    0x0010030000 
#define  U1_DW_I2C__SAIF_BD_APB__BASE_ADDR                                                                    0x0010040000 
#define  U2_DW_I2C__SAIF_BD_APB__BASE_ADDR                                                                    0x0010050000 
#define  U0_SSP_SPI__SAIF_BD_APB__BASE_ADDR                                                                   0x0010060000 
#define  U1_SSP_SPI__SAIF_BD_APB__BASE_ADDR                                                                   0x0010070000 
#define  U2_SSP_SPI__SAIF_BD_APB__BASE_ADDR                                                                   0x0010080000 
#define  U0_TDM16SLOT__SAIF_BD_APB__BASE_ADDR                                                                 0x0010090000 
#define  U0_CDNS_SPDIF__SAIF_BD_APB__BASE_ADDR                                                                0x00100A0000 
#define  U0_PWMDAC__SAIF_BD_APB__BASE_ADDR                                                                    0x00100B0000 
#define  U0_PDM_4MIC__SAIF_BD_APB__BASE_ADDR                                                                  0x00100D0000 
#define  U0_I2SRX_3CH__SAIF_BD_APB__BASE_ADDR                                                                 0x00100E0000 
#define  U0_CDN_USB__SAIF_BD_APB__BASE_ADDR                                                                   0x0010100000 
#define  U0_CDN_USB__SAIF_BD_APBUTMI__BASE_ADDR                                                               0x0010200000 
#define  U0_PLDA_PCIE__SAIF_BD_APB__BASE_ADDR                                                                 0x0010210000 
#define  U1_PLDA_PCIE__SAIF_BD_APB__BASE_ADDR                                                                 0x0010220000 
#define  U0_STG_CRG__SAIF_BD_APBS__BASE_ADDR                                                                  0x0010230000
#define  U0_STG_SYSCON__SAIF_BD_APBS__BASE_ADDR                                                               0x0010240000
#define  U3_DW_UART__SAIF_BD_APB__BASE_ADDR                                                                   0x0012000000 
#define  U4_DW_UART__SAIF_BD_APB__BASE_ADDR                                                                   0x0012010000 
#define  U5_DW_UART__SAIF_BD_APB__BASE_ADDR                                                                   0x0012020000 
#define  U3_DW_I2C__SAIF_BD_APB__BASE_ADDR                                                                    0x0012030000 
#define  U4_DW_I2C__SAIF_BD_APB__BASE_ADDR                                                                    0x0012040000 
#define  U5_DW_I2C__SAIF_BD_APB__BASE_ADDR                                                                    0x0012050000 
#define  U6_DW_I2C__SAIF_BD_APB__BASE_ADDR                                                                    0x0012060000 
#define  U3_SSP_SPI__SAIF_BD_APB__BASE_ADDR                                                                   0x0012070000 
#define  U4_SSP_SPI__SAIF_BD_APB__BASE_ADDR                                                                   0x0012080000 
#define  U5_SSP_SPI__SAIF_BD_APB__BASE_ADDR                                                                   0x0012090000 
#define  U6_SSP_SPI__SAIF_BD_APB__BASE_ADDR                                                                   0x00120A0000 
#define  U0_I2STX_4CH__SAIF_BD_APB__BASE_ADDR                                                                 0x00120B0000 
#define  U1_I2STX_4CH__SAIF_BD_APB__BASE_ADDR                                                                 0x00120C0000 
#define  U0_PWM_8CH__SAIF_BD_APB__BASE_ADDR                                                                   0x00120D0000 
#define  U0_TEMP_SENSOR__SAIF_BD_APB__BASE_ADDR                                                               0x00120E0000 
#define  U0_DDR_SFT7110__SAIF_BD_APB_PHY__BASE_ADDR                                                           0x0013000000 
#define  U0_CDNS_QSPI__SAIF_BD_APB__BASE_ADDR                                                                 0x0013010000 
#define  U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR                                                                  0x0013020000 
#define  U0_SYS_SYSCON__SAIF_BD_APBS__BASE_ADDR                                                               0x0013030000 
#define  U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR                                                                0x0013040000 
#define  U0_SI5_TIMER__SAIF_BD_APB__BASE_ADDR                                                                 0x0013050000 
#define  U0_MAILBOX__SAIF_BD_APB__BASE_ADDR                                                                   0x0013060000 
#define  U0_DSKIT_WDT__SAIF_BD_APB__BASE_ADDR                                                                 0x0013070000 
#define  U0_INT_CTRL__SAIF_BD_APB__BASE_ADDR                                                                  0x0013080000 
#define  U0_CODAJ12__SAIF_BD_APB__BASE_ADDR                                                                   0x0013090000 
#define  U0_WAVE511__SAIF_BD_APB__BASE_ADDR                                                                   0x00130A0000 
#define  U0_WAVE420L__SAIF_BD_APB__BASE_ADDR                                                                  0x00130B0000 
#define  U0_IMG_GPU__SAIF_BD_APB__BASE_ADDR                                                                   0x00130C0000 
#define  U0_CAN_CTRL__SAIF_BD_APB__BASE_ADDR                                                                  0x00130D0000 
#define  U1_CAN_CTRL__SAIF_BD_APB__BASE_ADDR                                                                  0x00130E0000 
#define  U0_SFT7110_NOC_BUS__SAIF_BD_AXI_CPUPER__BASE_ADDR                                                    0x0015000000 
#define  U0_DDR_SFT7110__SAIF_BD_APB__BASE_ADDR                                                               0x0015700000 
#define  U0_SEC_TOP__SAIF_BD_AHB__BASE_ADDR                                                                   0x0016000000 
#define  U0_SEC_TOP__SAIF_BD_AHB_ALG__BASE_ADDR                                                               0x0016000000 
#define  U0_SEC_TOP__SAIF_BD_AHB_DMA__BASE_ADDR                                                               0x0016008000 
#define  U0_SEC_TOP__SAIF_BD_AHB_TRNG__BASE_ADDR                                                              0x001600C000 
#define  U0_DW_SDIO__SAIF_BD_AHB__BASE_ADDR                                                                   0x0016010000 
#define  U1_DW_SDIO__SAIF_BD_AHB__BASE_ADDR                                                                   0x0016020000 
#define  U0_DW_GMAC5_AXI64__SAIF_BD_AHB__BASE_ADDR                                                            0x0016030000 
#define  U1_DW_GMAC5_AXI64__SAIF_BD_AHB__BASE_ADDR                                                            0x0016040000 
#define  U0_DW_DMA1P_8CH_56HS__SAIF_BD_AHB__BASE_ADDR                                                         0x0016050000 
#define  U0_AON_CRG__SAIF_BD_APBS__BASE_ADDR                                                                  0x0017000000 
#define  U0_AON_SYSCON__SAIF_BD_APBS__BASE_ADDR                                                               0x0017010000 
#define  U0_AON_IOMUX__SAIF_BD_APBS__BASE_ADDR                                                                0x0017020000 
#define  U0_PMU__SAIF_BD_APB__BASE_ADDR                                                                       0x0017030000 
#define  U0_RTC_HMS__SAIF_BD_APB__BASE_ADDR                                                                   0x0017040000 
#define  U0_OTPC__SAIF_BD_APB__BASE_ADDR                                                                      0x0017050000 
#define  U0_TDM16SLOT__SAIF_BD_AHB__BASE_ADDR                                                                 0x00170C0000 
#define  U0_IMG_GPU__SAIF_BD_SOCIF__BASE_ADDR                                                                 0x0018000000 
#define  U0_VIN__SAIF_BD_CSI0_APB__BASE_ADDR                                                                  0x0019800000 
#define  U0_CRG__SAIF_BD_APB__BASE_ADDR                                                                       0x0019810000 
#define  U0_M31DPHY_APBCFG__SAIF_BD_APB__BASE_ADDR                                                            0x0019820000 
#define  U0_SYSCON__SAIF_BD_APB__BASE_ADDR                                                                    0x0019840000 
#define  U0_DOM_ISP_TOP__SAIF_BD_AXIS_ISP_AXI4S0__BASE_ADDR                                                   0x0019870000 
#define  U0_HIFI4__SAIF_BD_AXI_DSP_S_DRAM1__BASE_ADDR                                                         0x0020008000
#define  U0_HIFI4__SAIF_BD_AXI_DSP_S_DRAM0__BASE_ADDR                                                         0x0020010000
#define  U0_HIFI4__SAIF_BD_AXI_DSP_S_IRAM0__BASE_ADDR                                                         0x0020020000
#define  U0_HIFI4__SAIF_BD_AXI_DSP_S_IRAM1__BASE_ADDR                                                         0x0020030000
#define  U0_CDNS_QSPI__SAIF_BD_AHB__BASE_ADDR                                                                 0x0021000000
#define  U0_DC8200__SAIF_BD_AHB_S0__BASE_ADDR                                                                 0x0029400000
#define  U0_DC8200__SAIF_BD_AHB_S1__BASE_ADDR                                                                 0x0029480000
#define  U0_HDMI_TX__SAIF_BD_APBS__BASE_ADDR                                                                  0x0029590000
#define  U0_DOM_VOUT_SYSCON__SAIF_BD_APBS__BASE_ADDR                                                          0x00295B0000
#define  U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR                                                             0x00295C0000
#define  U0_CDNS_DSITX__SAIF_BD_APBS__BASE_ADDR                                                               0x00295D0000
#define  U0_MIPITX_APBIF__SAIF_BD_APBS__BASE_ADDR                                                             0x00295E0000
#define  U0_INTMEM_ROM_SRAM__SAIF_BD_SPSRAM_SYS__BASE_ADDR                                                    0x002A000000
#define  U0_PLDA_PCIE__SAIF_BD_AHB_CSR__BASE_ADDR                                                             0x002B000000
#define  U1_PLDA_PCIE__SAIF_BD_AHB_CSR__BASE_ADDR                                                             0x002C000000
#define  U0_PLDA_PCIE__SAIF_BD_AXI_SLV0_MEM32B__BASE_ADDR                                                     0x0030000000
#define  U1_PLDA_PCIE__SAIF_BD_AXI_SLV0_MEM32B__BASE_ADDR                                                     0x0038000000
#define  U0_DDR_SFT7110__SAIF_BD_AXI_MEM_PORT__BASE_ADDR                                                      0x0040000000
#define  U0_DDR_SFT7110__SAIF_BD_AXI_SYS_PORT__BASE_ADDR                                                      0x0440000000
#define  U0_PLDA_PCIE__SAIF_BD_AXI_SLV0_CONFIG__BASE_ADDR                                                     0x0900000000
#define  U0_PLDA_PCIE__SAIF_BD_AXI_SLV0_MEM64B__BASE_ADDR                                                     0x0940000000
#define  U1_PLDA_PCIE__SAIF_BD_AXI_SLV0_CONFIG__BASE_ADDR                                                     0x0980000000
#define  U1_PLDA_PCIE__SAIF_BD_AXI_SLV0_MEM64B__BASE_ADDR                                                     0x09C0000000

//#define SYS_CRG_BASE_ADDR 0x0
#define CLK_CPU_ROOT_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x0U)
#define CLK_CPU_CORE_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x4U)
#define CLK_CPU_BUS_CTRL_REG_ADDR                                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x8U)
#define CLK_GPU_ROOT_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xCU)
#define CLK_PERH_ROOT_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x10U)
#define CLK_BUS_ROOT_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x14U)
#define CLK_NOCSTG_BUS_CTRL_REG_ADDR                                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x18U)
#define CLK_AXI_CFG0_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1CU)
#define CLK_STG_AXIAHB_CTRL_REG_ADDR                                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x20U)
#define CLK_AHB0_CTRL_REG_ADDR                                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x24U)
#define CLK_AHB1_CTRL_REG_ADDR                                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x28U)
#define CLK_APB_BUS_FUNC_CTRL_REG_ADDR                               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2CU)
#define CLK_APB0_CTRL_REG_ADDR                                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x30U)
#define CLK_PLL0_DIV2_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x34U)
#define CLK_PLL1_DIV2_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x38U)
#define CLK_PLL2_DIV2_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x3CU)
#define CLK_AUDIO_ROOT_CTRL_REG_ADDR                                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x40U)
#define CLK_MCLK_INNER_CTRL_REG_ADDR                                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x44U)
#define CLK_MCLK_CTRL_REG_ADDR                                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x48U)
#define MCLK_OUT_CTRL_REG_ADDR                                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x4CU)
#define CLK_ISP_2X_CTRL_REG_ADDR                                     (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x50U)
#define CLK_ISP_AXI_CTRL_REG_ADDR                                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x54U)
#define CLK_GCLK0_CTRL_REG_ADDR                                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x58U)
#define CLK_GCLK1_CTRL_REG_ADDR                                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x5CU)
#define CLK_GCLK2_CTRL_REG_ADDR                                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x60U)
#define CLK_U0_U7MC_SFT7110_CORE_CLK_CTRL_REG_ADDR                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x64U)
#define CLK_U0_U7MC_SFT7110_CORE_CLK1_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x68U)
#define CLK_U0_U7MC_SFT7110_CORE_CLK2_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x6CU)
#define CLK_U0_U7MC_SFT7110_CORE_CLK3_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x70U)
#define CLK_U0_U7MC_SFT7110_CORE_CLK4_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x74U)
#define CLK_U0_U7MC_SFT7110_DEBUG_CLK_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x78U)
#define CLK_U0_U7MC_SFT7110_RTC_TOGGLE_CTRL_REG_ADDR                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x7CU)
#define CLK_U0_U7MC_SFT7110_TRACE_CLK0_CTRL_REG_ADDR                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x80U)
#define CLK_U0_U7MC_SFT7110_TRACE_CLK1_CTRL_REG_ADDR                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x84U)
#define CLK_U0_U7MC_SFT7110_TRACE_CLK2_CTRL_REG_ADDR                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x88U)
#define CLK_U0_U7MC_SFT7110_TRACE_CLK3_CTRL_REG_ADDR                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x8CU)
#define CLK_U0_U7MC_SFT7110_TRACE_CLK4_CTRL_REG_ADDR                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x90U)
#define CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_CTRL_REG_ADDR              (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x94U)
#define CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_CTRL_REG_ADDR             (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x98U)
#define CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_CTRL_REG_ADDR         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x9CU)
#define CLK_OSC_DIV2_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xA0U)
#define CLK_PLL1_DIV4_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xA4U)
#define CLK_PLL1_DIV8_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xA8U)
#define CLK_DDR_BUS_CTRL_REG_ADDR                                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xACU)
#define CLK_U0_DDR_SFT7110_CLK_AXI_CTRL_REG_ADDR                     (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xB0U)
#define CLK_GPU_CORE_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xB4U)
#define CLK_U0_IMG_GPU_CORE_CLK_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xB8U)
#define CLK_U0_IMG_GPU_SYS_CLK_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xBCU)
#define CLK_U0_IMG_GPU_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xC0U)
#define CLK_U0_IMG_GPU_RTC_TOGGLE_CTRL_REG_ADDR                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xC4U)
#define CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_CTRL_REG_ADDR             (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xC8U)
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_CTRL_REG_ADDR (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xCCU)
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_CTRL_REG_ADDR (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xD0U)
#define CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_CTRL_REG_ADDR             (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xD4U)
#define CLK_HIFI4_CORE_CTRL_REG_ADDR                                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xD8U)
#define CLK_HIFI4_AXI_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xDCU)
#define CLK_U0_AXI_CFG1_DEC_CLK_MAIN_CTRL_REG_ADDR                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xE0U)
#define CLK_U0_AXI_CFG1_DEC_CLK_AHB_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xE4U)
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_CTRL_REG_ADDR (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xE8U)
#define CLK_VOUT_AXI_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xECU)
#define CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_CTRL_REG_ADDR            (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xF0U)
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_CTRL_REG_ADDR (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xF4U)
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_CTRL_REG_ADDR (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xF8U)
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_CTRL_REG_ADDR (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0xFCU)
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_CTRL_REG_ADDR (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x100U)
#define CLK_JPEGC_AXI_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x104U)
#define CLK_U0_CODAJ12_CLK_AXI_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x108U)
#define CLK_U0_CODAJ12_CLK_CORE_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x10CU)
#define CLK_U0_CODAJ12_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x110U)
#define CLK_VDEC_AXI_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x114U)
#define CLK_U0_WAVE511_CLK_AXI_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x118U)
#define CLK_U0_WAVE511_CLK_BPU_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x11CU)
#define CLK_U0_WAVE511_CLK_VCE_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x120U)
#define CLK_U0_WAVE511_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x124U)
#define CLK_U0_VDEC_JPG_ARB_JPGCLK_CTRL_REG_ADDR                     (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x128U)
#define CLK_U0_VDEC_JPG_ARB_MAINCLK_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x12CU)
#define CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_CTRL_REG_ADDR            (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x130U)
#define CLK_VENC_AXI_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x134U)
#define CLK_U0_WAVE420L_CLK_AXI_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x138U)
#define CLK_U0_WAVE420L_CLK_BPU_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x13CU)
#define CLK_U0_WAVE420L_CLK_VCE_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x140U)
#define CLK_U0_WAVE420L_CLK_APB_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x144U)
#define CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_CTRL_REG_ADDR            (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x148U)
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_CTRL_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x14CU)
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_CTRL_REG_ADDR                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x150U)
#define CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x154U)
#define CLK_U2_AXIMEM_128B_CLK_AXI_CTRL_REG_ADDR                     (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x158U)
#define CLK_U0_CDNS_QSPI_CLK_AHB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x15CU)
#define CLK_U0_CDNS_QSPI_CLK_APB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x160U)
#define CLK_QSPI_REF_SRC_CTRL_REG_ADDR                               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x164U)
#define CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x168U)
#define CLK_U0_DW_SDIO_CLK_AHB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x16CU)
#define CLK_U1_DW_SDIO_CLK_AHB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x170U)
#define CLK_U0_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x174U)
#define CLK_U1_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x178U)
#define CLK_USB_125M_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x17CU)
#define CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_CTRL_REG_ADDR             (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x180U)
#define CLK_U1_DW_GMAC5_AXI64_CLK_AHB_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x184U)
#define CLK_U1_DW_GMAC5_AXI64_CLK_AXI_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x188U)
#define CLK_GMAC_SRC_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x18CU)
#define CLK_GMAC1_GTXCLK_CTRL_REG_ADDR                               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x190U)
#define CLK_GMAC1_RMII_RTX_CTRL_REG_ADDR                             (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x194U)
#define CLK_U1_DW_GMAC5_AXI64_CLK_PTP_CTRL_REG_ADDR                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x198U)
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_CTRL_REG_ADDR                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x19CU)
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_CTRL_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1A0U)
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1A4U)
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_CTRL_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1A8U)
#define CLK_GMAC1_GTXC_CTRL_REG_ADDR                                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1ACU)
#define CLK_GMAC0_GTXCLK_CTRL_REG_ADDR                               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1B0U)
#define CLK_GMAC0_PTP_CTRL_REG_ADDR                                  (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1B4U)
#define CLK_GMAC_PHY_CTRL_REG_ADDR                                   (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1B8U)
#define CLK_GMAC0_GTXC_CTRL_REG_ADDR                                 (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1BCU)
#define CLK_U0_SYS_IOMUX_PCLK_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1C0U)
#define CLK_U0_MAILBOX_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1C4U)
#define CLK_U0_INT_CTRL_CLK_APB_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1C8U)
#define CLK_U0_CAN_CTRL_CLK_APB_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1CCU)
#define CLK_U0_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1D0U)
#define CLK_U0_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1D4U)
#define CLK_U1_CAN_CTRL_CLK_APB_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1D8U)
#define CLK_U1_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1DCU)
#define CLK_U1_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1E0U)
#define CLK_U0_PWM_8CH_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1E4U)
#define CLK_U0_DSKIT_WDT_CLK_APB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1E8U)
#define CLK_U0_DSKIT_WDT_CLK_WDT_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1ECU)
#define CLK_U0_SI5_TIMER_CLK_APB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1F0U)
#define CLK_U0_SI5_TIMER_CLK_TIMER0_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1F4U)
#define CLK_U0_SI5_TIMER_CLK_TIMER1_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1F8U)
#define CLK_U0_SI5_TIMER_CLK_TIMER2_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1FCU)
#define CLK_U0_SI5_TIMER_CLK_TIMER3_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x200U)
#define CLK_U0_TEMP_SENSOR_CLK_APB_CTRL_REG_ADDR                     (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x204U)
#define CLK_U0_TEMP_SENSOR_CLK_TEMP_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x208U)
#define CLK_U0_SSP_SPI_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x20CU)
#define CLK_U1_SSP_SPI_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x210U)
#define CLK_U2_SSP_SPI_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x214U)
#define CLK_U3_SSP_SPI_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x218U)
#define CLK_U4_SSP_SPI_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x21CU)
#define CLK_U5_SSP_SPI_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x220U)
#define CLK_U6_SSP_SPI_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x224U)
#define CLK_U0_DW_I2C_CLK_APB_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x228U)
#define CLK_U1_DW_I2C_CLK_APB_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x22CU)
#define CLK_U2_DW_I2C_CLK_APB_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x230U)
#define CLK_U3_DW_I2C_CLK_APB_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x234U)
#define CLK_U4_DW_I2C_CLK_APB_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x238U)
#define CLK_U5_DW_I2C_CLK_APB_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x23CU)
#define CLK_U6_DW_I2C_CLK_APB_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x240U)
#define CLK_U0_DW_UART_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x244U)
#define CLK_U0_DW_UART_CLK_CORE_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x248U)
#define CLK_U1_DW_UART_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x24CU)
#define CLK_U1_DW_UART_CLK_CORE_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x250U)
#define CLK_U2_DW_UART_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x254U)
#define CLK_U2_DW_UART_CLK_CORE_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x258U)
#define CLK_U3_DW_UART_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x25CU)
#define CLK_U3_DW_UART_CLK_CORE_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x260U)
#define CLK_U4_DW_UART_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x264U)
#define CLK_U4_DW_UART_CLK_CORE_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x268U)
#define CLK_U5_DW_UART_CLK_APB_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x26CU)
#define CLK_U5_DW_UART_CLK_CORE_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x270U)
#define CLK_U0_PWMDAC_CLK_APB_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x274U)
#define CLK_U0_PWMDAC_CLK_CORE_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x278U)
#define CLK_U0_CDNS_SPDIF_CLK_APB_CTRL_REG_ADDR                      (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x27CU)
#define CLK_U0_CDNS_SPDIF_CLK_CORE_CTRL_REG_ADDR                     (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x280U)
#define CLK_U0_I2STX_4CH_CLK_APB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x284U)
#define CLK_I2STX_4CH0_BCLK_MST_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x288U)
#define CLK_I2STX_4CH0_BCLK_MST_INV_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x28CU)
#define CLK_I2STX_4CH0_LRCK_MST_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x290U)
#define CLK_U0_I2STX_4CH_BCLK_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x294U)
#define CLK_U0_I2STX_4CH_BCLK_N_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x298U)
#define CLK_U0_I2STX_4CH_LRCK_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x29CU)
#define CLK_U1_I2STX_4CH_CLK_APB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2A0U)
#define CLK_I2STX_4CH1_BCLK_MST_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2A4U)
#define CLK_I2STX_4CH1_BCLK_MST_INV_CTRL_REG_ADDR                    (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2A8U)
#define CLK_I2STX_4CH1_LRCK_MST_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2ACU)
#define CLK_U1_I2STX_4CH_BCLK_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2B0U)
#define CLK_U1_I2STX_4CH_BCLK_N_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2B4U)
#define CLK_U1_I2STX_4CH_LRCK_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2B8U)
#define CLK_U0_I2SRX_3CH_CLK_APB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2BCU)
#define CLK_I2SRX_3CH_BCLK_MST_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2C0U)
#define CLK_I2SRX_3CH_BCLK_MST_INV_CTRL_REG_ADDR                     (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2C4U)
#define CLK_I2SRX_3CH_LRCK_MST_CTRL_REG_ADDR                         (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2C8U)
#define CLK_U0_I2SRX_3CH_BCLK_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2CCU)
#define CLK_U0_I2SRX_3CH_BCLK_N_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2D0U)
#define CLK_U0_I2SRX_3CH_LRCK_CTRL_REG_ADDR                          (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2D4U)
#define CLK_U0_PDM_4MIC_CLK_DMIC_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2D8U)
#define CLK_U0_PDM_4MIC_CLK_APB_CTRL_REG_ADDR                        (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2DCU)
#define CLK_U0_TDM16SLOT_CLK_AHB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2E0U)
#define CLK_U0_TDM16SLOT_CLK_APB_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2E4U)
#define CLK_TDM_INTERNAL_CTRL_REG_ADDR                               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2E8U)
#define CLK_U0_TDM16SLOT_CLK_TDM_CTRL_REG_ADDR                       (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2ECU)
#define CLK_U0_TDM16SLOT_CLK_TDM_N_CTRL_REG_ADDR                     (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2F0U)
#define CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_CTRL_REG_ADDR             (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2F4U)


#define SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2F8U)
#define SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2FCU)
#define SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x300U)
#define SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x304U)

#define SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x308U)
#define SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x30CU)
#define SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x310U)
#define SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR               (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR + 0x314U)


#define CLK_CPU_ROOT_SW_SHIFT                                        24
#define CLK_CPU_ROOT_SW_MASK                                         0x1000000U
#define CLK_CPU_ROOT_SW_CLK_OSC_DATA                                 0
#define CLK_CPU_ROOT_SW_CLK_PLL0_DATA                                1
#define CLK_CPU_CORE_DIV_SHIFT                                       0
#define CLK_CPU_CORE_DIV_MASK                                        0x7U
#define CLK_CPU_BUS_DIV_SHIFT                                        0
#define CLK_CPU_BUS_DIV_MASK                                         0x3U
#define CLK_GPU_ROOT_SW_SHIFT                                        24
#define CLK_GPU_ROOT_SW_MASK                                         0x1000000U
#define CLK_GPU_ROOT_SW_CLK_PLL2_DATA                                0
#define CLK_GPU_ROOT_SW_CLK_PLL1_DATA                                1
#define CLK_PERH_ROOT_SW_SHIFT                                       24
#define CLK_PERH_ROOT_SW_MASK                                        0x1000000U
#define CLK_PERH_ROOT_SW_CLK_PLL0_DATA                               0
#define CLK_PERH_ROOT_SW_CLK_PLL2_DATA                               1
#define CLK_PERH_ROOT_DIV_SHIFT                                      0
#define CLK_PERH_ROOT_DIV_MASK                                       0x3U
#define CLK_BUS_ROOT_SW_SHIFT                                        24
#define CLK_BUS_ROOT_SW_MASK                                         0x1000000U
#define CLK_BUS_ROOT_SW_CLK_OSC_DATA                                 0
#define CLK_BUS_ROOT_SW_CLK_PLL2_DATA                                1
#define CLK_NOCSTG_BUS_DIV_SHIFT                                     0
#define CLK_NOCSTG_BUS_DIV_MASK                                      0x3U
#define CLK_AXI_CFG0_DIV_SHIFT                                       0
#define CLK_AXI_CFG0_DIV_MASK                                        0x3U
#define CLK_STG_AXIAHB_DIV_SHIFT                                     0
#define CLK_STG_AXIAHB_DIV_MASK                                      0x3U
#define CLK_AHB0_ENABLE_DATA                                         1
#define CLK_AHB0_DISABLE_DATA                                        0
#define CLK_AHB0_EN_SHIFT                                            31
#define CLK_AHB0_EN_MASK                                             0x80000000U
#define CLK_AHB1_ENABLE_DATA                                         1
#define CLK_AHB1_DISABLE_DATA                                        0
#define CLK_AHB1_EN_SHIFT                                            31
#define CLK_AHB1_EN_MASK                                             0x80000000U
#define CLK_APB_BUS_FUNC_DIV_SHIFT                                   0
#define CLK_APB_BUS_FUNC_DIV_MASK                                    0xFU
#define CLK_APB0_ENABLE_DATA                                         1
#define CLK_APB0_DISABLE_DATA                                        0
#define CLK_APB0_EN_SHIFT                                            31
#define CLK_APB0_EN_MASK                                             0x80000000U
#define CLK_PLL0_DIV2_DIV_SHIFT                                      0
#define CLK_PLL0_DIV2_DIV_MASK                                       0x3U
#define CLK_PLL1_DIV2_DIV_SHIFT                                      0
#define CLK_PLL1_DIV2_DIV_MASK                                       0x3U
#define CLK_PLL2_DIV2_DIV_SHIFT                                      0
#define CLK_PLL2_DIV2_DIV_MASK                                       0x3U
#define CLK_AUDIO_ROOT_DIV_SHIFT                                     0
#define CLK_AUDIO_ROOT_DIV_MASK                                      0xFU
#define CLK_MCLK_INNER_DIV_SHIFT                                     0
#define CLK_MCLK_INNER_DIV_MASK                                      0x7FU
#define CLK_MCLK_SW_SHIFT                                            24
#define CLK_MCLK_SW_MASK                                             0x1000000U
#define CLK_MCLK_SW_CLK_MCLK_INNER_DATA                              0
#define CLK_MCLK_SW_CLK_MCLK_EXT_DATA                                1
#define MCLK_OUT_ENABLE_DATA                                         1
#define MCLK_OUT_DISABLE_DATA                                        0
#define MCLK_OUT_EN_SHIFT                                            31
#define MCLK_OUT_EN_MASK                                             0x80000000U
#define CLK_ISP_2X_SW_SHIFT                                          24
#define CLK_ISP_2X_SW_MASK                                           0x1000000U
#define CLK_ISP_2X_SW_CLK_PLL2_DATA                                  0
#define CLK_ISP_2X_SW_CLK_PLL1_DATA                                  1
#define CLK_ISP_2X_DIV_SHIFT                                         0
#define CLK_ISP_2X_DIV_MASK                                          0xFU
#define CLK_ISP_AXI_DIV_SHIFT                                        0
#define CLK_ISP_AXI_DIV_MASK                                         0x7U
#define CLK_GCLK0_ENABLE_DATA                                        1
#define CLK_GCLK0_DISABLE_DATA                                       0
#define CLK_GCLK0_EN_SHIFT                                           31
#define CLK_GCLK0_EN_MASK                                            0x80000000U
#define CLK_GCLK0_DIV_SHIFT                                          0
#define CLK_GCLK0_DIV_MASK                                           0x3FU
#define CLK_GCLK1_ENABLE_DATA                                        1
#define CLK_GCLK1_DISABLE_DATA                                       0
#define CLK_GCLK1_EN_SHIFT                                           31
#define CLK_GCLK1_EN_MASK                                            0x80000000U
#define CLK_GCLK1_DIV_SHIFT                                          0
#define CLK_GCLK1_DIV_MASK                                           0x3FU
#define CLK_GCLK2_ENABLE_DATA                                        1
#define CLK_GCLK2_DISABLE_DATA                                       0
#define CLK_GCLK2_EN_SHIFT                                           31
#define CLK_GCLK2_EN_MASK                                            0x80000000U
#define CLK_GCLK2_DIV_SHIFT                                          0
#define CLK_GCLK2_DIV_MASK                                           0x3FU
#define CLK_U0_U7MC_SFT7110_CORE_CLK_ENABLE_DATA                     1
#define CLK_U0_U7MC_SFT7110_CORE_CLK_DISABLE_DATA                    0
#define CLK_U0_U7MC_SFT7110_CORE_CLK_EN_SHIFT                        31
#define CLK_U0_U7MC_SFT7110_CORE_CLK_EN_MASK                         0x80000000U
#define CLK_U0_U7MC_SFT7110_CORE_CLK1_ENABLE_DATA                    1
#define CLK_U0_U7MC_SFT7110_CORE_CLK1_DISABLE_DATA                   0
#define CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_SHIFT                       31
#define CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_MASK                        0x80000000U
#define CLK_U0_U7MC_SFT7110_CORE_CLK2_ENABLE_DATA                    1
#define CLK_U0_U7MC_SFT7110_CORE_CLK2_DISABLE_DATA                   0
#define CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_SHIFT                       31
#define CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_MASK                        0x80000000U
#define CLK_U0_U7MC_SFT7110_CORE_CLK3_ENABLE_DATA                    1
#define CLK_U0_U7MC_SFT7110_CORE_CLK3_DISABLE_DATA                   0
#define CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_SHIFT                       31
#define CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_MASK                        0x80000000U
#define CLK_U0_U7MC_SFT7110_CORE_CLK4_ENABLE_DATA                    1
#define CLK_U0_U7MC_SFT7110_CORE_CLK4_DISABLE_DATA                   0
#define CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_SHIFT                       31
#define CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_MASK                        0x80000000U
#define CLK_U0_U7MC_SFT7110_DEBUG_CLK_ENABLE_DATA                    1
#define CLK_U0_U7MC_SFT7110_DEBUG_CLK_DISABLE_DATA                   0
#define CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_SHIFT                       31
#define CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_MASK                        0x80000000U
#define CLK_U0_U7MC_SFT7110_RTC_TOGGLE_DIV_SHIFT                     0
#define CLK_U0_U7MC_SFT7110_RTC_TOGGLE_DIV_MASK                      0x7U
#define CLK_U0_U7MC_SFT7110_TRACE_CLK0_ENABLE_DATA                   1
#define CLK_U0_U7MC_SFT7110_TRACE_CLK0_DISABLE_DATA                  0
#define CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_SHIFT                      31
#define CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_MASK                       0x80000000U
#define CLK_U0_U7MC_SFT7110_TRACE_CLK1_ENABLE_DATA                   1
#define CLK_U0_U7MC_SFT7110_TRACE_CLK1_DISABLE_DATA                  0
#define CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_SHIFT                      31
#define CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_MASK                       0x80000000U
#define CLK_U0_U7MC_SFT7110_TRACE_CLK2_ENABLE_DATA                   1
#define CLK_U0_U7MC_SFT7110_TRACE_CLK2_DISABLE_DATA                  0
#define CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_SHIFT                      31
#define CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_MASK                       0x80000000U
#define CLK_U0_U7MC_SFT7110_TRACE_CLK3_ENABLE_DATA                   1
#define CLK_U0_U7MC_SFT7110_TRACE_CLK3_DISABLE_DATA                  0
#define CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_SHIFT                      31
#define CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_MASK                       0x80000000U
#define CLK_U0_U7MC_SFT7110_TRACE_CLK4_ENABLE_DATA                   1
#define CLK_U0_U7MC_SFT7110_TRACE_CLK4_DISABLE_DATA                  0
#define CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_SHIFT                      31
#define CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_MASK                       0x80000000U
#define CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_ENABLE_DATA                1
#define CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_DISABLE_DATA               0
#define CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_SHIFT                   31
#define CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_MASK                    0x80000000U
#define CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_ENABLE_DATA               1
#define CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_DISABLE_DATA              0
#define CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_SHIFT                  31
#define CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_MASK                   0x80000000U
#define CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_ENABLE_DATA           1
#define CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_DISABLE_DATA          0
#define CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_SHIFT              31
#define CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_MASK               0x80000000U
#define CLK_OSC_DIV2_DIV_SHIFT                                       0
#define CLK_OSC_DIV2_DIV_MASK                                        0x3U
#define CLK_PLL1_DIV4_DIV_SHIFT                                      0
#define CLK_PLL1_DIV4_DIV_MASK                                       0x3U
#define CLK_PLL1_DIV8_DIV_SHIFT                                      0
#define CLK_PLL1_DIV8_DIV_MASK                                       0x3U
#define CLK_DDR_BUS_SW_SHIFT                                         24
#define CLK_DDR_BUS_SW_MASK                                          0x3000000U
#define CLK_DDR_BUS_SW_CLK_OSC_DIV2_DATA                             0
#define CLK_DDR_BUS_SW_CLK_PLL1_DIV2_DATA                            1
#define CLK_DDR_BUS_SW_CLK_PLL1_DIV4_DATA                            2
#define CLK_DDR_BUS_SW_CLK_PLL1_DIV8_DATA                            3
#define CLK_U0_DDR_SFT7110_CLK_AXI_ENABLE_DATA                       1
#define CLK_U0_DDR_SFT7110_CLK_AXI_DISABLE_DATA                      0
#define CLK_U0_DDR_SFT7110_CLK_AXI_EN_SHIFT                          31
#define CLK_U0_DDR_SFT7110_CLK_AXI_EN_MASK                           0x80000000U
#define CLK_GPU_CORE_DIV_SHIFT                                       0
#define CLK_GPU_CORE_DIV_MASK                                        0x7U
#define CLK_U0_IMG_GPU_CORE_CLK_ENABLE_DATA                          1
#define CLK_U0_IMG_GPU_CORE_CLK_DISABLE_DATA                         0
#define CLK_U0_IMG_GPU_CORE_CLK_EN_SHIFT                             31
#define CLK_U0_IMG_GPU_CORE_CLK_EN_MASK                              0x80000000U
#define CLK_U0_IMG_GPU_SYS_CLK_ENABLE_DATA                           1
#define CLK_U0_IMG_GPU_SYS_CLK_DISABLE_DATA                          0
#define CLK_U0_IMG_GPU_SYS_CLK_EN_SHIFT                              31
#define CLK_U0_IMG_GPU_SYS_CLK_EN_MASK                               0x80000000U
#define CLK_U0_IMG_GPU_CLK_APB_ENABLE_DATA                           1
#define CLK_U0_IMG_GPU_CLK_APB_DISABLE_DATA                          0
#define CLK_U0_IMG_GPU_CLK_APB_EN_SHIFT                              31
#define CLK_U0_IMG_GPU_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U0_IMG_GPU_RTC_TOGGLE_ENABLE_DATA                        1
#define CLK_U0_IMG_GPU_RTC_TOGGLE_DISABLE_DATA                       0
#define CLK_U0_IMG_GPU_RTC_TOGGLE_EN_SHIFT                           31
#define CLK_U0_IMG_GPU_RTC_TOGGLE_EN_MASK                            0x80000000U
#define CLK_U0_IMG_GPU_RTC_TOGGLE_DIV_SHIFT                          0
#define CLK_U0_IMG_GPU_RTC_TOGGLE_DIV_MASK                           0xFU
#define CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_ENABLE_DATA               1
#define CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_DISABLE_DATA              0
#define CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_SHIFT                  31
#define CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_MASK                   0x80000000U
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_ENABLE_DATA 1
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_DISABLE_DATA 0
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_SHIFT   31
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_MASK    0x80000000U
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_ENABLE_DATA   1
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_DISABLE_DATA  0
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_SHIFT      31
#define CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_MASK       0x80000000U
#define CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_ENABLE_DATA               1
#define CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_DISABLE_DATA              0
#define CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_SHIFT                  31
#define CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_MASK                   0x80000000U
#define CLK_HIFI4_CORE_DIV_SHIFT                                     0
#define CLK_HIFI4_CORE_DIV_MASK                                      0xFU
#define CLK_HIFI4_AXI_DIV_SHIFT                                      0
#define CLK_HIFI4_AXI_DIV_MASK                                       0x3U
#define CLK_U0_AXI_CFG1_DEC_CLK_MAIN_ENABLE_DATA                     1
#define CLK_U0_AXI_CFG1_DEC_CLK_MAIN_DISABLE_DATA                    0
#define CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_SHIFT                        31
#define CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_MASK                         0x80000000U
#define CLK_U0_AXI_CFG1_DEC_CLK_AHB_ENABLE_DATA                      1
#define CLK_U0_AXI_CFG1_DEC_CLK_AHB_DISABLE_DATA                     0
#define CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_SHIFT                         31
#define CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_MASK                          0x80000000U
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_ENABLE_DATA 1
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_DISABLE_DATA 0
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_SHIFT   31
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_MASK    0x80000000U
#define CLK_VOUT_AXI_DIV_SHIFT                                       0
#define CLK_VOUT_AXI_DIV_MASK                                        0x7U
#define CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_ENABLE_DATA              1
#define CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_DISABLE_DATA             0
#define CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_SHIFT                 31
#define CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_MASK                  0x80000000U
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_ENABLE_DATA 1
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_DISABLE_DATA 0
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_SHIFT   31
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_MASK    0x80000000U
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_ENABLE_DATA 1
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_DISABLE_DATA 0
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_SHIFT   31
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_MASK    0x80000000U
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_ENABLE_DATA 1
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_DISABLE_DATA 0
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_SHIFT 31
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_MASK 0x80000000U
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_DIV_SHIFT 0
#define CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_DIV_MASK 0x3U
#define CLK_JPEGC_AXI_DIV_SHIFT                                      0
#define CLK_JPEGC_AXI_DIV_MASK                                       0x1FU
#define CLK_U0_CODAJ12_CLK_AXI_ENABLE_DATA                           1
#define CLK_U0_CODAJ12_CLK_AXI_DISABLE_DATA                          0
#define CLK_U0_CODAJ12_CLK_AXI_EN_SHIFT                              31
#define CLK_U0_CODAJ12_CLK_AXI_EN_MASK                               0x80000000U
#define CLK_U0_CODAJ12_CLK_CORE_ENABLE_DATA                          1
#define CLK_U0_CODAJ12_CLK_CORE_DISABLE_DATA                         0
#define CLK_U0_CODAJ12_CLK_CORE_EN_SHIFT                             31
#define CLK_U0_CODAJ12_CLK_CORE_EN_MASK                              0x80000000U
#define CLK_U0_CODAJ12_CLK_CORE_DIV_SHIFT                            0
#define CLK_U0_CODAJ12_CLK_CORE_DIV_MASK                             0x1FU
#define CLK_U0_CODAJ12_CLK_APB_ENABLE_DATA                           1
#define CLK_U0_CODAJ12_CLK_APB_DISABLE_DATA                          0
#define CLK_U0_CODAJ12_CLK_APB_EN_SHIFT                              31
#define CLK_U0_CODAJ12_CLK_APB_EN_MASK                               0x80000000U
#define CLK_VDEC_AXI_DIV_SHIFT                                       0
#define CLK_VDEC_AXI_DIV_MASK                                        0x7U
#define CLK_U0_WAVE511_CLK_AXI_ENABLE_DATA                           1
#define CLK_U0_WAVE511_CLK_AXI_DISABLE_DATA                          0
#define CLK_U0_WAVE511_CLK_AXI_EN_SHIFT                              31
#define CLK_U0_WAVE511_CLK_AXI_EN_MASK                               0x80000000U
#define CLK_U0_WAVE511_CLK_BPU_ENABLE_DATA                           1
#define CLK_U0_WAVE511_CLK_BPU_DISABLE_DATA                          0
#define CLK_U0_WAVE511_CLK_BPU_EN_SHIFT                              31
#define CLK_U0_WAVE511_CLK_BPU_EN_MASK                               0x80000000U
#define CLK_U0_WAVE511_CLK_BPU_DIV_SHIFT                             0
#define CLK_U0_WAVE511_CLK_BPU_DIV_MASK                              0x7U
#define CLK_U0_WAVE511_CLK_VCE_ENABLE_DATA                           1
#define CLK_U0_WAVE511_CLK_VCE_DISABLE_DATA                          0
#define CLK_U0_WAVE511_CLK_VCE_EN_SHIFT                              31
#define CLK_U0_WAVE511_CLK_VCE_EN_MASK                               0x80000000U
#define CLK_U0_WAVE511_CLK_VCE_DIV_SHIFT                             0
#define CLK_U0_WAVE511_CLK_VCE_DIV_MASK                              0x7U
#define CLK_U0_WAVE511_CLK_APB_ENABLE_DATA                           1
#define CLK_U0_WAVE511_CLK_APB_DISABLE_DATA                          0
#define CLK_U0_WAVE511_CLK_APB_EN_SHIFT                              31
#define CLK_U0_WAVE511_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U0_VDEC_JPG_ARB_JPGCLK_ENABLE_DATA                       1
#define CLK_U0_VDEC_JPG_ARB_JPGCLK_DISABLE_DATA                      0
#define CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_SHIFT                          31
#define CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_MASK                           0x80000000U
#define CLK_U0_VDEC_JPG_ARB_MAINCLK_ENABLE_DATA                      1
#define CLK_U0_VDEC_JPG_ARB_MAINCLK_DISABLE_DATA                     0
#define CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_SHIFT                         31
#define CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_MASK                          0x80000000U
#define CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_ENABLE_DATA              1
#define CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_DISABLE_DATA             0
#define CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_SHIFT                 31
#define CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_MASK                  0x80000000U
#define CLK_VENC_AXI_DIV_SHIFT                                       0
#define CLK_VENC_AXI_DIV_MASK                                        0xFU
#define CLK_U0_WAVE420L_CLK_AXI_ENABLE_DATA                          1
#define CLK_U0_WAVE420L_CLK_AXI_DISABLE_DATA                         0
#define CLK_U0_WAVE420L_CLK_AXI_EN_SHIFT                             31
#define CLK_U0_WAVE420L_CLK_AXI_EN_MASK                              0x80000000U
#define CLK_U0_WAVE420L_CLK_BPU_ENABLE_DATA                          1
#define CLK_U0_WAVE420L_CLK_BPU_DISABLE_DATA                         0
#define CLK_U0_WAVE420L_CLK_BPU_EN_SHIFT                             31
#define CLK_U0_WAVE420L_CLK_BPU_EN_MASK                              0x80000000U
#define CLK_U0_WAVE420L_CLK_BPU_DIV_SHIFT                            0
#define CLK_U0_WAVE420L_CLK_BPU_DIV_MASK                             0xFU
#define CLK_U0_WAVE420L_CLK_VCE_ENABLE_DATA                          1
#define CLK_U0_WAVE420L_CLK_VCE_DISABLE_DATA                         0
#define CLK_U0_WAVE420L_CLK_VCE_EN_SHIFT                             31
#define CLK_U0_WAVE420L_CLK_VCE_EN_MASK                              0x80000000U
#define CLK_U0_WAVE420L_CLK_VCE_DIV_SHIFT                            0
#define CLK_U0_WAVE420L_CLK_VCE_DIV_MASK                             0xFU
#define CLK_U0_WAVE420L_CLK_APB_ENABLE_DATA                          1
#define CLK_U0_WAVE420L_CLK_APB_DISABLE_DATA                         0
#define CLK_U0_WAVE420L_CLK_APB_EN_SHIFT                             31
#define CLK_U0_WAVE420L_CLK_APB_EN_MASK                              0x80000000U
#define CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_ENABLE_DATA              1
#define CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_DISABLE_DATA             0
#define CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_SHIFT                 31
#define CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_MASK                  0x80000000U
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_ENABLE_DATA                 1
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_DISABLE_DATA                0
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_SHIFT                    31
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_MASK                     0x80000000U
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_ENABLE_DATA                     1
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DISABLE_DATA                    0
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_SHIFT                        31
#define CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_MASK                         0x80000000U
#define CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_ENABLE_DATA                    1
#define CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_DISABLE_DATA                   0
#define CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_SHIFT                       31
#define CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_MASK                        0x80000000U
#define CLK_U2_AXIMEM_128B_CLK_AXI_ENABLE_DATA                       1
#define CLK_U2_AXIMEM_128B_CLK_AXI_DISABLE_DATA                      0
#define CLK_U2_AXIMEM_128B_CLK_AXI_EN_SHIFT                          31
#define CLK_U2_AXIMEM_128B_CLK_AXI_EN_MASK                           0x80000000U
#define CLK_U0_CDNS_QSPI_CLK_AHB_ENABLE_DATA                         1
#define CLK_U0_CDNS_QSPI_CLK_AHB_DISABLE_DATA                        0
#define CLK_U0_CDNS_QSPI_CLK_AHB_EN_SHIFT                            31
#define CLK_U0_CDNS_QSPI_CLK_AHB_EN_MASK                             0x80000000U
#define CLK_U0_CDNS_QSPI_CLK_APB_ENABLE_DATA                         1
#define CLK_U0_CDNS_QSPI_CLK_APB_DISABLE_DATA                        0
#define CLK_U0_CDNS_QSPI_CLK_APB_EN_SHIFT                            31
#define CLK_U0_CDNS_QSPI_CLK_APB_EN_MASK                             0x80000000U
#define CLK_QSPI_REF_SRC_DIV_SHIFT                                   0
#define CLK_QSPI_REF_SRC_DIV_MASK                                    0x1FU
#define CLK_U0_CDNS_QSPI_CLK_REF_ENABLE_DATA                         1
#define CLK_U0_CDNS_QSPI_CLK_REF_DISABLE_DATA                        0
#define CLK_U0_CDNS_QSPI_CLK_REF_EN_SHIFT                            31
#define CLK_U0_CDNS_QSPI_CLK_REF_EN_MASK                             0x80000000U
#define CLK_U0_CDNS_QSPI_CLK_REF_SW_SHIFT                            24
#define CLK_U0_CDNS_QSPI_CLK_REF_SW_MASK                             0x1000000U
#define CLK_U0_CDNS_QSPI_CLK_REF_SW_CLK_OSC_DATA                     0
#define CLK_U0_CDNS_QSPI_CLK_REF_SW_CLK_QSPI_REF_SRC_DATA            1
#define CLK_U0_DW_SDIO_CLK_AHB_ENABLE_DATA                           1
#define CLK_U0_DW_SDIO_CLK_AHB_DISABLE_DATA                          0
#define CLK_U0_DW_SDIO_CLK_AHB_EN_SHIFT                              31
#define CLK_U0_DW_SDIO_CLK_AHB_EN_MASK                               0x80000000U
#define CLK_U1_DW_SDIO_CLK_AHB_ENABLE_DATA                           1
#define CLK_U1_DW_SDIO_CLK_AHB_DISABLE_DATA                          0
#define CLK_U1_DW_SDIO_CLK_AHB_EN_SHIFT                              31
#define CLK_U1_DW_SDIO_CLK_AHB_EN_MASK                               0x80000000U
#define CLK_U0_DW_SDIO_CLK_SDCARD_ENABLE_DATA                        1
#define CLK_U0_DW_SDIO_CLK_SDCARD_DISABLE_DATA                       0
#define CLK_U0_DW_SDIO_CLK_SDCARD_EN_SHIFT                           31
#define CLK_U0_DW_SDIO_CLK_SDCARD_EN_MASK                            0x80000000U
#define CLK_U0_DW_SDIO_CLK_SDCARD_DIV_SHIFT                          0
#define CLK_U0_DW_SDIO_CLK_SDCARD_DIV_MASK                           0xFU
#define CLK_U1_DW_SDIO_CLK_SDCARD_ENABLE_DATA                        1
#define CLK_U1_DW_SDIO_CLK_SDCARD_DISABLE_DATA                       0
#define CLK_U1_DW_SDIO_CLK_SDCARD_EN_SHIFT                           31
#define CLK_U1_DW_SDIO_CLK_SDCARD_EN_MASK                            0x80000000U
#define CLK_U1_DW_SDIO_CLK_SDCARD_DIV_SHIFT                          0
#define CLK_U1_DW_SDIO_CLK_SDCARD_DIV_MASK                           0xFU
#define CLK_USB_125M_DIV_SHIFT                                       0
#define CLK_USB_125M_DIV_MASK                                        0xFU
#define CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_ENABLE_DATA               1
#define CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_DISABLE_DATA              0
#define CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_SHIFT                  31
#define CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_MASK                   0x80000000U
#define CLK_U1_DW_GMAC5_AXI64_CLK_AHB_ENABLE_DATA                    1
#define CLK_U1_DW_GMAC5_AXI64_CLK_AHB_DISABLE_DATA                   0
#define CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_SHIFT                       31
#define CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_MASK                        0x80000000U
#define CLK_U1_DW_GMAC5_AXI64_CLK_AXI_ENABLE_DATA                    1
#define CLK_U1_DW_GMAC5_AXI64_CLK_AXI_DISABLE_DATA                   0
#define CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_SHIFT                       31
#define CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_MASK                        0x80000000U
#define CLK_GMAC_SRC_DIV_SHIFT                                       0
#define CLK_GMAC_SRC_DIV_MASK                                        0x7U
#define CLK_GMAC1_GTXCLK_DIV_SHIFT                                   0
#define CLK_GMAC1_GTXCLK_DIV_MASK                                    0xFU
#define CLK_GMAC1_RMII_RTX_DIV_SHIFT                                 0
#define CLK_GMAC1_RMII_RTX_DIV_MASK                                  0x1FU
#define CLK_U1_DW_GMAC5_AXI64_CLK_PTP_ENABLE_DATA                    1
#define CLK_U1_DW_GMAC5_AXI64_CLK_PTP_DISABLE_DATA                   0
#define CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_SHIFT                       31
#define CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_MASK                        0x80000000U
#define CLK_U1_DW_GMAC5_AXI64_CLK_PTP_DIV_SHIFT                      0
#define CLK_U1_DW_GMAC5_AXI64_CLK_PTP_DIV_MASK                       0x1FU
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_SHIFT                        24
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_MASK                         0x1000000U
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_CLK_GMAC1_RGMII_RXIN_DATA    0
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_CLK_GMAC1_RMII_RTX_DATA      1
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_DLY_SHIFT                       0
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_DLY_MASK                        0x3FU
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_DATA               1
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_UN_POLARITY_DATA            0
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_SHIFT              30
#define CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_MASK               0x40000000U
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_ENABLE_DATA                     1
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_DISABLE_DATA                    0
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_SHIFT                        31
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_MASK                         0x80000000U
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_SHIFT                        24
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_MASK                         0x1000000U
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_CLK_GMAC1_GTXCLK_DATA        0
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_CLK_GMAC1_RMII_RTX_DATA      1
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_DATA               1
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_UN_POLARITY_DATA            0
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_SHIFT              30
#define CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_MASK               0x40000000U
#define CLK_GMAC1_GTXC_ENABLE_DATA                                   1
#define CLK_GMAC1_GTXC_DISABLE_DATA                                  0
#define CLK_GMAC1_GTXC_EN_SHIFT                                      31
#define CLK_GMAC1_GTXC_EN_MASK                                       0x80000000U
#define CLK_GMAC1_GTXC_DLY_SHIFT                                     0
#define CLK_GMAC1_GTXC_DLY_MASK                                      0x3FU
#define CLK_GMAC0_GTXCLK_ENABLE_DATA                                 1
#define CLK_GMAC0_GTXCLK_DISABLE_DATA                                0
#define CLK_GMAC0_GTXCLK_EN_SHIFT                                    31
#define CLK_GMAC0_GTXCLK_EN_MASK                                     0x80000000U
#define CLK_GMAC0_GTXCLK_DIV_SHIFT                                   0
#define CLK_GMAC0_GTXCLK_DIV_MASK                                    0xFU
#define CLK_GMAC0_PTP_ENABLE_DATA                                    1
#define CLK_GMAC0_PTP_DISABLE_DATA                                   0
#define CLK_GMAC0_PTP_EN_SHIFT                                       31
#define CLK_GMAC0_PTP_EN_MASK                                        0x80000000U
#define CLK_GMAC0_PTP_DIV_SHIFT                                      0
#define CLK_GMAC0_PTP_DIV_MASK                                       0x1FU
#define CLK_GMAC_PHY_ENABLE_DATA                                     1
#define CLK_GMAC_PHY_DISABLE_DATA                                    0
#define CLK_GMAC_PHY_EN_SHIFT                                        31
#define CLK_GMAC_PHY_EN_MASK                                         0x80000000U
#define CLK_GMAC_PHY_DIV_SHIFT                                       0
#define CLK_GMAC_PHY_DIV_MASK                                        0x1FU
#define CLK_GMAC0_GTXC_ENABLE_DATA                                   1
#define CLK_GMAC0_GTXC_DISABLE_DATA                                  0
#define CLK_GMAC0_GTXC_EN_SHIFT                                      31
#define CLK_GMAC0_GTXC_EN_MASK                                       0x80000000U
#define CLK_GMAC0_GTXC_DLY_SHIFT                                     0
#define CLK_GMAC0_GTXC_DLY_MASK                                      0x3FU
#define CLK_U0_SYS_IOMUX_PCLK_ENABLE_DATA                            1
#define CLK_U0_SYS_IOMUX_PCLK_DISABLE_DATA                           0
#define CLK_U0_SYS_IOMUX_PCLK_EN_SHIFT                               31
#define CLK_U0_SYS_IOMUX_PCLK_EN_MASK                                0x80000000U
#define CLK_U0_MAILBOX_CLK_APB_ENABLE_DATA                           1
#define CLK_U0_MAILBOX_CLK_APB_DISABLE_DATA                          0
#define CLK_U0_MAILBOX_CLK_APB_EN_SHIFT                              31
#define CLK_U0_MAILBOX_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U0_INT_CTRL_CLK_APB_ENABLE_DATA                          1
#define CLK_U0_INT_CTRL_CLK_APB_DISABLE_DATA                         0
#define CLK_U0_INT_CTRL_CLK_APB_EN_SHIFT                             31
#define CLK_U0_INT_CTRL_CLK_APB_EN_MASK                              0x80000000U
#define CLK_U0_CAN_CTRL_CLK_APB_ENABLE_DATA                          1
#define CLK_U0_CAN_CTRL_CLK_APB_DISABLE_DATA                         0
#define CLK_U0_CAN_CTRL_CLK_APB_EN_SHIFT                             31
#define CLK_U0_CAN_CTRL_CLK_APB_EN_MASK                              0x80000000U
#define CLK_U0_CAN_CTRL_CLK_TIMER_ENABLE_DATA                        1
#define CLK_U0_CAN_CTRL_CLK_TIMER_DISABLE_DATA                       0
#define CLK_U0_CAN_CTRL_CLK_TIMER_EN_SHIFT                           31
#define CLK_U0_CAN_CTRL_CLK_TIMER_EN_MASK                            0x80000000U
#define CLK_U0_CAN_CTRL_CLK_TIMER_DIV_SHIFT                          0
#define CLK_U0_CAN_CTRL_CLK_TIMER_DIV_MASK                           0x1FU
#define CLK_U0_CAN_CTRL_CLK_CAN_ENABLE_DATA                          1
#define CLK_U0_CAN_CTRL_CLK_CAN_DISABLE_DATA                         0
#define CLK_U0_CAN_CTRL_CLK_CAN_EN_SHIFT                             31
#define CLK_U0_CAN_CTRL_CLK_CAN_EN_MASK                              0x80000000U
#define CLK_U0_CAN_CTRL_CLK_CAN_DIV_SHIFT                            0
#define CLK_U0_CAN_CTRL_CLK_CAN_DIV_MASK                             0x3FU
#define CLK_U1_CAN_CTRL_CLK_APB_ENABLE_DATA                          1
#define CLK_U1_CAN_CTRL_CLK_APB_DISABLE_DATA                         0
#define CLK_U1_CAN_CTRL_CLK_APB_EN_SHIFT                             31
#define CLK_U1_CAN_CTRL_CLK_APB_EN_MASK                              0x80000000U
#define CLK_U1_CAN_CTRL_CLK_TIMER_ENABLE_DATA                        1
#define CLK_U1_CAN_CTRL_CLK_TIMER_DISABLE_DATA                       0
#define CLK_U1_CAN_CTRL_CLK_TIMER_EN_SHIFT                           31
#define CLK_U1_CAN_CTRL_CLK_TIMER_EN_MASK                            0x80000000U
#define CLK_U1_CAN_CTRL_CLK_TIMER_DIV_SHIFT                          0
#define CLK_U1_CAN_CTRL_CLK_TIMER_DIV_MASK                           0x1FU
#define CLK_U1_CAN_CTRL_CLK_CAN_ENABLE_DATA                          1
#define CLK_U1_CAN_CTRL_CLK_CAN_DISABLE_DATA                         0
#define CLK_U1_CAN_CTRL_CLK_CAN_EN_SHIFT                             31
#define CLK_U1_CAN_CTRL_CLK_CAN_EN_MASK                              0x80000000U
#define CLK_U1_CAN_CTRL_CLK_CAN_DIV_SHIFT                            0
#define CLK_U1_CAN_CTRL_CLK_CAN_DIV_MASK                             0x3FU
#define CLK_U0_PWM_8CH_CLK_APB_ENABLE_DATA                           1
#define CLK_U0_PWM_8CH_CLK_APB_DISABLE_DATA                          0
#define CLK_U0_PWM_8CH_CLK_APB_EN_SHIFT                              31
#define CLK_U0_PWM_8CH_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U0_DSKIT_WDT_CLK_APB_ENABLE_DATA                         1
#define CLK_U0_DSKIT_WDT_CLK_APB_DISABLE_DATA                        0
#define CLK_U0_DSKIT_WDT_CLK_APB_EN_SHIFT                            31
#define CLK_U0_DSKIT_WDT_CLK_APB_EN_MASK                             0x80000000U
#define CLK_U0_DSKIT_WDT_CLK_WDT_ENABLE_DATA                         1
#define CLK_U0_DSKIT_WDT_CLK_WDT_DISABLE_DATA                        0
#define CLK_U0_DSKIT_WDT_CLK_WDT_EN_SHIFT                            31
#define CLK_U0_DSKIT_WDT_CLK_WDT_EN_MASK                             0x80000000U
#define CLK_U0_SI5_TIMER_CLK_APB_ENABLE_DATA                         1
#define CLK_U0_SI5_TIMER_CLK_APB_DISABLE_DATA                        0
#define CLK_U0_SI5_TIMER_CLK_APB_EN_SHIFT                            31
#define CLK_U0_SI5_TIMER_CLK_APB_EN_MASK                             0x80000000U
#define CLK_U0_SI5_TIMER_CLK_TIMER0_ENABLE_DATA                      1
#define CLK_U0_SI5_TIMER_CLK_TIMER0_DISABLE_DATA                     0
#define CLK_U0_SI5_TIMER_CLK_TIMER0_EN_SHIFT                         31
#define CLK_U0_SI5_TIMER_CLK_TIMER0_EN_MASK                          0x80000000U
#define CLK_U0_SI5_TIMER_CLK_TIMER1_ENABLE_DATA                      1
#define CLK_U0_SI5_TIMER_CLK_TIMER1_DISABLE_DATA                     0
#define CLK_U0_SI5_TIMER_CLK_TIMER1_EN_SHIFT                         31
#define CLK_U0_SI5_TIMER_CLK_TIMER1_EN_MASK                          0x80000000U
#define CLK_U0_SI5_TIMER_CLK_TIMER2_ENABLE_DATA                      1
#define CLK_U0_SI5_TIMER_CLK_TIMER2_DISABLE_DATA                     0
#define CLK_U0_SI5_TIMER_CLK_TIMER2_EN_SHIFT                         31
#define CLK_U0_SI5_TIMER_CLK_TIMER2_EN_MASK                          0x80000000U
#define CLK_U0_SI5_TIMER_CLK_TIMER3_ENABLE_DATA                      1
#define CLK_U0_SI5_TIMER_CLK_TIMER3_DISABLE_DATA                     0
#define CLK_U0_SI5_TIMER_CLK_TIMER3_EN_SHIFT                         31
#define CLK_U0_SI5_TIMER_CLK_TIMER3_EN_MASK                          0x80000000U
#define CLK_U0_TEMP_SENSOR_CLK_APB_ENABLE_DATA                       1
#define CLK_U0_TEMP_SENSOR_CLK_APB_DISABLE_DATA                      0
#define CLK_U0_TEMP_SENSOR_CLK_APB_EN_SHIFT                          31
#define CLK_U0_TEMP_SENSOR_CLK_APB_EN_MASK                           0x80000000U
#define CLK_U0_TEMP_SENSOR_CLK_TEMP_ENABLE_DATA                      1
#define CLK_U0_TEMP_SENSOR_CLK_TEMP_DISABLE_DATA                     0
#define CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_SHIFT                         31
#define CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_MASK                          0x80000000U
#define CLK_U0_TEMP_SENSOR_CLK_TEMP_DIV_SHIFT                        0
#define CLK_U0_TEMP_SENSOR_CLK_TEMP_DIV_MASK                         0x1FU
#define CLK_U0_SSP_SPI_CLK_APB_ENABLE_DATA                           1
#define CLK_U0_SSP_SPI_CLK_APB_DISABLE_DATA                          0
#define CLK_U0_SSP_SPI_CLK_APB_EN_SHIFT                              31
#define CLK_U0_SSP_SPI_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U1_SSP_SPI_CLK_APB_ENABLE_DATA                           1
#define CLK_U1_SSP_SPI_CLK_APB_DISABLE_DATA                          0
#define CLK_U1_SSP_SPI_CLK_APB_EN_SHIFT                              31
#define CLK_U1_SSP_SPI_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U2_SSP_SPI_CLK_APB_ENABLE_DATA                           1
#define CLK_U2_SSP_SPI_CLK_APB_DISABLE_DATA                          0
#define CLK_U2_SSP_SPI_CLK_APB_EN_SHIFT                              31
#define CLK_U2_SSP_SPI_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U3_SSP_SPI_CLK_APB_ENABLE_DATA                           1
#define CLK_U3_SSP_SPI_CLK_APB_DISABLE_DATA                          0
#define CLK_U3_SSP_SPI_CLK_APB_EN_SHIFT                              31
#define CLK_U3_SSP_SPI_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U4_SSP_SPI_CLK_APB_ENABLE_DATA                           1
#define CLK_U4_SSP_SPI_CLK_APB_DISABLE_DATA                          0
#define CLK_U4_SSP_SPI_CLK_APB_EN_SHIFT                              31
#define CLK_U4_SSP_SPI_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U5_SSP_SPI_CLK_APB_ENABLE_DATA                           1
#define CLK_U5_SSP_SPI_CLK_APB_DISABLE_DATA                          0
#define CLK_U5_SSP_SPI_CLK_APB_EN_SHIFT                              31
#define CLK_U5_SSP_SPI_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U6_SSP_SPI_CLK_APB_ENABLE_DATA                           1
#define CLK_U6_SSP_SPI_CLK_APB_DISABLE_DATA                          0
#define CLK_U6_SSP_SPI_CLK_APB_EN_SHIFT                              31
#define CLK_U6_SSP_SPI_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U0_DW_I2C_CLK_APB_ENABLE_DATA                            1
#define CLK_U0_DW_I2C_CLK_APB_DISABLE_DATA                           0
#define CLK_U0_DW_I2C_CLK_APB_EN_SHIFT                               31
#define CLK_U0_DW_I2C_CLK_APB_EN_MASK                                0x80000000U
#define CLK_U1_DW_I2C_CLK_APB_ENABLE_DATA                            1
#define CLK_U1_DW_I2C_CLK_APB_DISABLE_DATA                           0
#define CLK_U1_DW_I2C_CLK_APB_EN_SHIFT                               31
#define CLK_U1_DW_I2C_CLK_APB_EN_MASK                                0x80000000U
#define CLK_U2_DW_I2C_CLK_APB_ENABLE_DATA                            1
#define CLK_U2_DW_I2C_CLK_APB_DISABLE_DATA                           0
#define CLK_U2_DW_I2C_CLK_APB_EN_SHIFT                               31
#define CLK_U2_DW_I2C_CLK_APB_EN_MASK                                0x80000000U
#define CLK_U3_DW_I2C_CLK_APB_ENABLE_DATA                            1
#define CLK_U3_DW_I2C_CLK_APB_DISABLE_DATA                           0
#define CLK_U3_DW_I2C_CLK_APB_EN_SHIFT                               31
#define CLK_U3_DW_I2C_CLK_APB_EN_MASK                                0x80000000U
#define CLK_U4_DW_I2C_CLK_APB_ENABLE_DATA                            1
#define CLK_U4_DW_I2C_CLK_APB_DISABLE_DATA                           0
#define CLK_U4_DW_I2C_CLK_APB_EN_SHIFT                               31
#define CLK_U4_DW_I2C_CLK_APB_EN_MASK                                0x80000000U
#define CLK_U5_DW_I2C_CLK_APB_ENABLE_DATA                            1
#define CLK_U5_DW_I2C_CLK_APB_DISABLE_DATA                           0
#define CLK_U5_DW_I2C_CLK_APB_EN_SHIFT                               31
#define CLK_U5_DW_I2C_CLK_APB_EN_MASK                                0x80000000U
#define CLK_U6_DW_I2C_CLK_APB_ENABLE_DATA                            1
#define CLK_U6_DW_I2C_CLK_APB_DISABLE_DATA                           0
#define CLK_U6_DW_I2C_CLK_APB_EN_SHIFT                               31
#define CLK_U6_DW_I2C_CLK_APB_EN_MASK                                0x80000000U
#define CLK_U0_DW_UART_CLK_APB_ENABLE_DATA                           1
#define CLK_U0_DW_UART_CLK_APB_DISABLE_DATA                          0
#define CLK_U0_DW_UART_CLK_APB_EN_SHIFT                              31
#define CLK_U0_DW_UART_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U0_DW_UART_CLK_CORE_ENABLE_DATA                          1
#define CLK_U0_DW_UART_CLK_CORE_DISABLE_DATA                         0
#define CLK_U0_DW_UART_CLK_CORE_EN_SHIFT                             31
#define CLK_U0_DW_UART_CLK_CORE_EN_MASK                              0x80000000U
#define CLK_U1_DW_UART_CLK_APB_ENABLE_DATA                           1
#define CLK_U1_DW_UART_CLK_APB_DISABLE_DATA                          0
#define CLK_U1_DW_UART_CLK_APB_EN_SHIFT                              31
#define CLK_U1_DW_UART_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U1_DW_UART_CLK_CORE_ENABLE_DATA                          1
#define CLK_U1_DW_UART_CLK_CORE_DISABLE_DATA                         0
#define CLK_U1_DW_UART_CLK_CORE_EN_SHIFT                             31
#define CLK_U1_DW_UART_CLK_CORE_EN_MASK                              0x80000000U
#define CLK_U2_DW_UART_CLK_APB_ENABLE_DATA                           1
#define CLK_U2_DW_UART_CLK_APB_DISABLE_DATA                          0
#define CLK_U2_DW_UART_CLK_APB_EN_SHIFT                              31
#define CLK_U2_DW_UART_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U2_DW_UART_CLK_CORE_ENABLE_DATA                          1
#define CLK_U2_DW_UART_CLK_CORE_DISABLE_DATA                         0
#define CLK_U2_DW_UART_CLK_CORE_EN_SHIFT                             31
#define CLK_U2_DW_UART_CLK_CORE_EN_MASK                              0x80000000U
#define CLK_U3_DW_UART_CLK_APB_ENABLE_DATA                           1
#define CLK_U3_DW_UART_CLK_APB_DISABLE_DATA                          0
#define CLK_U3_DW_UART_CLK_APB_EN_SHIFT                              31
#define CLK_U3_DW_UART_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U3_DW_UART_CLK_CORE_ENABLE_DATA                          1
#define CLK_U3_DW_UART_CLK_CORE_DISABLE_DATA                         0
#define CLK_U3_DW_UART_CLK_CORE_EN_SHIFT                             31
#define CLK_U3_DW_UART_CLK_CORE_EN_MASK                              0x80000000U
#define CLK_U3_DW_UART_CLK_CORE_DIV_SHIFT                            0
#define CLK_U3_DW_UART_CLK_CORE_DIV_MASK                             0x1FFFFU
#define CLK_U4_DW_UART_CLK_APB_ENABLE_DATA                           1
#define CLK_U4_DW_UART_CLK_APB_DISABLE_DATA                          0
#define CLK_U4_DW_UART_CLK_APB_EN_SHIFT                              31
#define CLK_U4_DW_UART_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U4_DW_UART_CLK_CORE_ENABLE_DATA                          1
#define CLK_U4_DW_UART_CLK_CORE_DISABLE_DATA                         0
#define CLK_U4_DW_UART_CLK_CORE_EN_SHIFT                             31
#define CLK_U4_DW_UART_CLK_CORE_EN_MASK                              0x80000000U
#define CLK_U4_DW_UART_CLK_CORE_DIV_SHIFT                            0
#define CLK_U4_DW_UART_CLK_CORE_DIV_MASK                             0x1FFFFU
#define CLK_U5_DW_UART_CLK_APB_ENABLE_DATA                           1
#define CLK_U5_DW_UART_CLK_APB_DISABLE_DATA                          0
#define CLK_U5_DW_UART_CLK_APB_EN_SHIFT                              31
#define CLK_U5_DW_UART_CLK_APB_EN_MASK                               0x80000000U
#define CLK_U5_DW_UART_CLK_CORE_ENABLE_DATA                          1
#define CLK_U5_DW_UART_CLK_CORE_DISABLE_DATA                         0
#define CLK_U5_DW_UART_CLK_CORE_EN_SHIFT                             31
#define CLK_U5_DW_UART_CLK_CORE_EN_MASK                              0x80000000U
#define CLK_U5_DW_UART_CLK_CORE_DIV_SHIFT                            0
#define CLK_U5_DW_UART_CLK_CORE_DIV_MASK                             0x1FFFFU
#define CLK_U0_PWMDAC_CLK_APB_ENABLE_DATA                            1
#define CLK_U0_PWMDAC_CLK_APB_DISABLE_DATA                           0
#define CLK_U0_PWMDAC_CLK_APB_EN_SHIFT                               31
#define CLK_U0_PWMDAC_CLK_APB_EN_MASK                                0x80000000U
#define CLK_U0_PWMDAC_CLK_CORE_ENABLE_DATA                           1
#define CLK_U0_PWMDAC_CLK_CORE_DISABLE_DATA                          0
#define CLK_U0_PWMDAC_CLK_CORE_EN_SHIFT                              31
#define CLK_U0_PWMDAC_CLK_CORE_EN_MASK                               0x80000000U
#define CLK_U0_PWMDAC_CLK_CORE_DIV_SHIFT                             0
#define CLK_U0_PWMDAC_CLK_CORE_DIV_MASK                              0x1FFU
#define CLK_U0_CDNS_SPDIF_CLK_APB_ENABLE_DATA                        1
#define CLK_U0_CDNS_SPDIF_CLK_APB_DISABLE_DATA                       0
#define CLK_U0_CDNS_SPDIF_CLK_APB_EN_SHIFT                           31
#define CLK_U0_CDNS_SPDIF_CLK_APB_EN_MASK                            0x80000000U
#define CLK_U0_CDNS_SPDIF_CLK_CORE_ENABLE_DATA                       1
#define CLK_U0_CDNS_SPDIF_CLK_CORE_DISABLE_DATA                      0
#define CLK_U0_CDNS_SPDIF_CLK_CORE_EN_SHIFT                          31
#define CLK_U0_CDNS_SPDIF_CLK_CORE_EN_MASK                           0x80000000U
#define CLK_U0_I2STX_4CH_CLK_APB_ENABLE_DATA                         1
#define CLK_U0_I2STX_4CH_CLK_APB_DISABLE_DATA                        0
#define CLK_U0_I2STX_4CH_CLK_APB_EN_SHIFT                            31
#define CLK_U0_I2STX_4CH_CLK_APB_EN_MASK                             0x80000000U
#define CLK_I2STX_4CH0_BCLK_MST_ENABLE_DATA                          1
#define CLK_I2STX_4CH0_BCLK_MST_DISABLE_DATA                         0
#define CLK_I2STX_4CH0_BCLK_MST_EN_SHIFT                             31
#define CLK_I2STX_4CH0_BCLK_MST_EN_MASK                              0x80000000U
#define CLK_I2STX_4CH0_BCLK_MST_DIV_SHIFT                            0
#define CLK_I2STX_4CH0_BCLK_MST_DIV_MASK                             0x3FU
#define CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_DATA                    1
#define CLK_I2STX_4CH0_BCLK_MST_INV_UN_POLARITY_DATA                 0
#define CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_SHIFT                   30
#define CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_MASK                    0x40000000U
#define CLK_I2STX_4CH0_LRCK_MST_SW_SHIFT                             24
#define CLK_I2STX_4CH0_LRCK_MST_SW_MASK                              0x1000000U
#define CLK_I2STX_4CH0_LRCK_MST_SW_CLK_I2STX_4CH0_BCLK_MST_INV_DATA  0
#define CLK_I2STX_4CH0_LRCK_MST_SW_CLK_I2STX_4CH0_BCLK_MST_DATA      1
#define CLK_I2STX_4CH0_LRCK_MST_DIV_SHIFT                            0
#define CLK_I2STX_4CH0_LRCK_MST_DIV_MASK                             0x7FU
#define CLK_U0_I2STX_4CH_BCLK_SW_SHIFT                               24
#define CLK_U0_I2STX_4CH_BCLK_SW_MASK                                0x1000000U
#define CLK_U0_I2STX_4CH_BCLK_SW_CLK_I2STX_4CH0_BCLK_MST_DATA        0
#define CLK_U0_I2STX_4CH_BCLK_SW_CLK_I2STX_BCLK_EXT_DATA             1
#define CLK_U0_I2STX_4CH_BCLK_N_POLARITY_DATA                        1
#define CLK_U0_I2STX_4CH_BCLK_N_UN_POLARITY_DATA                     0
#define CLK_U0_I2STX_4CH_BCLK_N_POLARITY_SHIFT                       30
#define CLK_U0_I2STX_4CH_BCLK_N_POLARITY_MASK                        0x40000000U
#define CLK_U0_I2STX_4CH_LRCK_SW_SHIFT                               24
#define CLK_U0_I2STX_4CH_LRCK_SW_MASK                                0x1000000U
#define CLK_U0_I2STX_4CH_LRCK_SW_CLK_I2STX_4CH0_LRCK_MST_DATA        0
#define CLK_U0_I2STX_4CH_LRCK_SW_CLK_I2STX_LRCK_EXT_DATA             1
#define CLK_U1_I2STX_4CH_CLK_APB_ENABLE_DATA                         1
#define CLK_U1_I2STX_4CH_CLK_APB_DISABLE_DATA                        0
#define CLK_U1_I2STX_4CH_CLK_APB_EN_SHIFT                            31
#define CLK_U1_I2STX_4CH_CLK_APB_EN_MASK                             0x80000000U
#define CLK_I2STX_4CH1_BCLK_MST_ENABLE_DATA                          1
#define CLK_I2STX_4CH1_BCLK_MST_DISABLE_DATA                         0
#define CLK_I2STX_4CH1_BCLK_MST_EN_SHIFT                             31
#define CLK_I2STX_4CH1_BCLK_MST_EN_MASK                              0x80000000U
#define CLK_I2STX_4CH1_BCLK_MST_DIV_SHIFT                            0
#define CLK_I2STX_4CH1_BCLK_MST_DIV_MASK                             0x3FU
#define CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_DATA                    1
#define CLK_I2STX_4CH1_BCLK_MST_INV_UN_POLARITY_DATA                 0
#define CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_SHIFT                   30
#define CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_MASK                    0x40000000U
#define CLK_I2STX_4CH1_LRCK_MST_SW_SHIFT                             24
#define CLK_I2STX_4CH1_LRCK_MST_SW_MASK                              0x1000000U
#define CLK_I2STX_4CH1_LRCK_MST_SW_CLK_I2STX_4CH1_BCLK_MST_INV_DATA  0
#define CLK_I2STX_4CH1_LRCK_MST_SW_CLK_I2STX_4CH1_BCLK_MST_DATA      1
#define CLK_I2STX_4CH1_LRCK_MST_DIV_SHIFT                            0
#define CLK_I2STX_4CH1_LRCK_MST_DIV_MASK                             0x7FU
#define CLK_U1_I2STX_4CH_BCLK_SW_SHIFT                               24
#define CLK_U1_I2STX_4CH_BCLK_SW_MASK                                0x1000000U
#define CLK_U1_I2STX_4CH_BCLK_SW_CLK_I2STX_4CH1_BCLK_MST_DATA        0
#define CLK_U1_I2STX_4CH_BCLK_SW_CLK_I2STX_BCLK_EXT_DATA             1
#define CLK_U1_I2STX_4CH_BCLK_N_POLARITY_DATA                        1
#define CLK_U1_I2STX_4CH_BCLK_N_UN_POLARITY_DATA                     0
#define CLK_U1_I2STX_4CH_BCLK_N_POLARITY_SHIFT                       30
#define CLK_U1_I2STX_4CH_BCLK_N_POLARITY_MASK                        0x40000000U
#define CLK_U1_I2STX_4CH_LRCK_SW_SHIFT                               24
#define CLK_U1_I2STX_4CH_LRCK_SW_MASK                                0x1000000U
#define CLK_U1_I2STX_4CH_LRCK_SW_CLK_I2STX_4CH1_LRCK_MST_DATA        0
#define CLK_U1_I2STX_4CH_LRCK_SW_CLK_I2STX_LRCK_EXT_DATA             1
#define CLK_U0_I2SRX_3CH_CLK_APB_ENABLE_DATA                         1
#define CLK_U0_I2SRX_3CH_CLK_APB_DISABLE_DATA                        0
#define CLK_U0_I2SRX_3CH_CLK_APB_EN_SHIFT                            31
#define CLK_U0_I2SRX_3CH_CLK_APB_EN_MASK                             0x80000000U
#define CLK_I2SRX_3CH_BCLK_MST_ENABLE_DATA                           1
#define CLK_I2SRX_3CH_BCLK_MST_DISABLE_DATA                          0
#define CLK_I2SRX_3CH_BCLK_MST_EN_SHIFT                              31
#define CLK_I2SRX_3CH_BCLK_MST_EN_MASK                               0x80000000U
#define CLK_I2SRX_3CH_BCLK_MST_DIV_SHIFT                             0
#define CLK_I2SRX_3CH_BCLK_MST_DIV_MASK                              0x3FU
#define CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_DATA                     1
#define CLK_I2SRX_3CH_BCLK_MST_INV_UN_POLARITY_DATA                  0
#define CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_SHIFT                    30
#define CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_MASK                     0x40000000U
#define CLK_I2SRX_3CH_LRCK_MST_SW_SHIFT                              24
#define CLK_I2SRX_3CH_LRCK_MST_SW_MASK                               0x1000000U
#define CLK_I2SRX_3CH_LRCK_MST_SW_CLK_I2SRX_3CH_BCLK_MST_INV_DATA    0
#define CLK_I2SRX_3CH_LRCK_MST_SW_CLK_I2SRX_3CH_BCLK_MST_DATA        1
#define CLK_I2SRX_3CH_LRCK_MST_DIV_SHIFT                             0
#define CLK_I2SRX_3CH_LRCK_MST_DIV_MASK                              0x7FU
#define CLK_U0_I2SRX_3CH_BCLK_SW_SHIFT                               24
#define CLK_U0_I2SRX_3CH_BCLK_SW_MASK                                0x1000000U
#define CLK_U0_I2SRX_3CH_BCLK_SW_CLK_I2SRX_3CH_BCLK_MST_DATA         0
#define CLK_U0_I2SRX_3CH_BCLK_SW_CLK_I2SRX_BCLK_EXT_DATA             1
#define CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_DATA                        1
#define CLK_U0_I2SRX_3CH_BCLK_N_UN_POLARITY_DATA                     0
#define CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_SHIFT                       30
#define CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_MASK                        0x40000000U
#define CLK_U0_I2SRX_3CH_LRCK_SW_SHIFT                               24
#define CLK_U0_I2SRX_3CH_LRCK_SW_MASK                                0x1000000U
#define CLK_U0_I2SRX_3CH_LRCK_SW_CLK_I2SRX_3CH_LRCK_MST_DATA         0
#define CLK_U0_I2SRX_3CH_LRCK_SW_CLK_I2SRX_LRCK_EXT_DATA             1
#define CLK_U0_PDM_4MIC_CLK_DMIC_ENABLE_DATA                         1
#define CLK_U0_PDM_4MIC_CLK_DMIC_DISABLE_DATA                        0
#define CLK_U0_PDM_4MIC_CLK_DMIC_EN_SHIFT                            31
#define CLK_U0_PDM_4MIC_CLK_DMIC_EN_MASK                             0x80000000U
#define CLK_U0_PDM_4MIC_CLK_DMIC_DIV_SHIFT                           0
#define CLK_U0_PDM_4MIC_CLK_DMIC_DIV_MASK                            0x7FU
#define CLK_U0_PDM_4MIC_CLK_APB_ENABLE_DATA                          1
#define CLK_U0_PDM_4MIC_CLK_APB_DISABLE_DATA                         0
#define CLK_U0_PDM_4MIC_CLK_APB_EN_SHIFT                             31
#define CLK_U0_PDM_4MIC_CLK_APB_EN_MASK                              0x80000000U
#define CLK_U0_TDM16SLOT_CLK_AHB_ENABLE_DATA                         1
#define CLK_U0_TDM16SLOT_CLK_AHB_DISABLE_DATA                        0
#define CLK_U0_TDM16SLOT_CLK_AHB_EN_SHIFT                            31
#define CLK_U0_TDM16SLOT_CLK_AHB_EN_MASK                             0x80000000U
#define CLK_U0_TDM16SLOT_CLK_APB_ENABLE_DATA                         1
#define CLK_U0_TDM16SLOT_CLK_APB_DISABLE_DATA                        0
#define CLK_U0_TDM16SLOT_CLK_APB_EN_SHIFT                            31
#define CLK_U0_TDM16SLOT_CLK_APB_EN_MASK                             0x80000000U
#define CLK_TDM_INTERNAL_ENABLE_DATA                                 1
#define CLK_TDM_INTERNAL_DISABLE_DATA                                0
#define CLK_TDM_INTERNAL_EN_SHIFT                                    31
#define CLK_TDM_INTERNAL_EN_MASK                                     0x80000000U
#define CLK_TDM_INTERNAL_DIV_SHIFT                                   0
#define CLK_TDM_INTERNAL_DIV_MASK                                    0x7FU
#define CLK_U0_TDM16SLOT_CLK_TDM_SW_SHIFT                            24
#define CLK_U0_TDM16SLOT_CLK_TDM_SW_MASK                             0x1000000U
#define CLK_U0_TDM16SLOT_CLK_TDM_SW_CLK_TDM_INTERNAL_DATA            0
#define CLK_U0_TDM16SLOT_CLK_TDM_SW_CLK_TDM_EXT_DATA                 1
#define CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_DATA                     1
#define CLK_U0_TDM16SLOT_CLK_TDM_N_UN_POLARITY_DATA                  0
#define CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_SHIFT                    30
#define CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_MASK                     0x40000000U
#define CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_DIV_SHIFT                 0
#define CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_DIV_MASK                  0x7U



#define RSTN_U0_JTAG2APB_PRESETN_SHIFT                               0
#define RSTN_U0_JTAG2APB_PRESETN_MASK                                (0x1 << 0)
#define RSTN_U0_JTAG2APB_PRESETN_ASSERT                              1
#define RSTN_U0_JTAG2APB_PRESETN_CLEAR                               0
#define RSTN_U0_SYS_SYSCON_PRESETN_SHIFT                             1
#define RSTN_U0_SYS_SYSCON_PRESETN_MASK                              (0x1 << 1)
#define RSTN_U0_SYS_SYSCON_PRESETN_ASSERT                            1
#define RSTN_U0_SYS_SYSCON_PRESETN_CLEAR                             0
#define RSTN_U0_SYS_IOMUX_PRESETN_SHIFT                              2
#define RSTN_U0_SYS_IOMUX_PRESETN_MASK                               (0x1 << 2)
#define RSTN_U0_SYS_IOMUX_PRESETN_ASSERT                             1
#define RSTN_U0_SYS_IOMUX_PRESETN_CLEAR                              0
#define RST_U0_U7MC_SFT7110_RST_BUS_SHIFT                            3
#define RST_U0_U7MC_SFT7110_RST_BUS_MASK                             (0x1 << 3)
#define RST_U0_U7MC_SFT7110_RST_BUS_ASSERT                           1
#define RST_U0_U7MC_SFT7110_RST_BUS_CLEAR                            0
#define RST_U0_U7MC_SFT7110_DEBUG_RESET_SHIFT                        4
#define RST_U0_U7MC_SFT7110_DEBUG_RESET_MASK                         (0x1 << 4)
#define RST_U0_U7MC_SFT7110_DEBUG_RESET_ASSERT                       1
#define RST_U0_U7MC_SFT7110_DEBUG_RESET_CLEAR                        0
#define RST_U0_U7MC_SFT7110_RST_CORE0_SHIFT                          5
#define RST_U0_U7MC_SFT7110_RST_CORE0_MASK                           (0x1 << 5)
#define RST_U0_U7MC_SFT7110_RST_CORE0_ASSERT                         1
#define RST_U0_U7MC_SFT7110_RST_CORE0_CLEAR                          0
#define RST_U0_U7MC_SFT7110_RST_CORE1_SHIFT                          6
#define RST_U0_U7MC_SFT7110_RST_CORE1_MASK                           (0x1 << 6)
#define RST_U0_U7MC_SFT7110_RST_CORE1_ASSERT                         1
#define RST_U0_U7MC_SFT7110_RST_CORE1_CLEAR                          0
#define RST_U0_U7MC_SFT7110_RST_CORE2_SHIFT                          7
#define RST_U0_U7MC_SFT7110_RST_CORE2_MASK                           (0x1 << 7)
#define RST_U0_U7MC_SFT7110_RST_CORE2_ASSERT                         1
#define RST_U0_U7MC_SFT7110_RST_CORE2_CLEAR                          0
#define RST_U0_U7MC_SFT7110_RST_CORE3_SHIFT                          8
#define RST_U0_U7MC_SFT7110_RST_CORE3_MASK                           (0x1 << 8)
#define RST_U0_U7MC_SFT7110_RST_CORE3_ASSERT                         1
#define RST_U0_U7MC_SFT7110_RST_CORE3_CLEAR                          0
#define RST_U0_U7MC_SFT7110_RST_CORE4_SHIFT                          9
#define RST_U0_U7MC_SFT7110_RST_CORE4_MASK                           (0x1 << 9)
#define RST_U0_U7MC_SFT7110_RST_CORE4_ASSERT                         1
#define RST_U0_U7MC_SFT7110_RST_CORE4_CLEAR                          0
#define RST_U0_U7MC_SFT7110_RST_CORE0_ST_SHIFT                       10
#define RST_U0_U7MC_SFT7110_RST_CORE0_ST_MASK                        (0x1 << 10)
#define RST_U0_U7MC_SFT7110_RST_CORE0_ST_ASSERT                      1
#define RST_U0_U7MC_SFT7110_RST_CORE0_ST_CLEAR                       0
#define RST_U0_U7MC_SFT7110_RST_CORE1_ST_SHIFT                       11
#define RST_U0_U7MC_SFT7110_RST_CORE1_ST_MASK                        (0x1 << 11)
#define RST_U0_U7MC_SFT7110_RST_CORE1_ST_ASSERT                      1
#define RST_U0_U7MC_SFT7110_RST_CORE1_ST_CLEAR                       0
#define RST_U0_U7MC_SFT7110_RST_CORE2_ST_SHIFT                       12
#define RST_U0_U7MC_SFT7110_RST_CORE2_ST_MASK                        (0x1 << 12)
#define RST_U0_U7MC_SFT7110_RST_CORE2_ST_ASSERT                      1
#define RST_U0_U7MC_SFT7110_RST_CORE2_ST_CLEAR                       0
#define RST_U0_U7MC_SFT7110_RST_CORE3_ST_SHIFT                       13
#define RST_U0_U7MC_SFT7110_RST_CORE3_ST_MASK                        (0x1 << 13)
#define RST_U0_U7MC_SFT7110_RST_CORE3_ST_ASSERT                      1
#define RST_U0_U7MC_SFT7110_RST_CORE3_ST_CLEAR                       0
#define RST_U0_U7MC_SFT7110_RST_CORE4_ST_SHIFT                       14
#define RST_U0_U7MC_SFT7110_RST_CORE4_ST_MASK                        (0x1 << 14)
#define RST_U0_U7MC_SFT7110_RST_CORE4_ST_ASSERT                      1
#define RST_U0_U7MC_SFT7110_RST_CORE4_ST_CLEAR                       0
#define RST_U0_U7MC_SFT7110_TRACE_RST0_SHIFT                         15
#define RST_U0_U7MC_SFT7110_TRACE_RST0_MASK                          (0x1 << 15)
#define RST_U0_U7MC_SFT7110_TRACE_RST0_ASSERT                        1
#define RST_U0_U7MC_SFT7110_TRACE_RST0_CLEAR                         0
#define RST_U0_U7MC_SFT7110_TRACE_RST1_SHIFT                         16
#define RST_U0_U7MC_SFT7110_TRACE_RST1_MASK                          (0x1 << 16)
#define RST_U0_U7MC_SFT7110_TRACE_RST1_ASSERT                        1
#define RST_U0_U7MC_SFT7110_TRACE_RST1_CLEAR                         0
#define RST_U0_U7MC_SFT7110_TRACE_RST2_SHIFT                         17
#define RST_U0_U7MC_SFT7110_TRACE_RST2_MASK                          (0x1 << 17)
#define RST_U0_U7MC_SFT7110_TRACE_RST2_ASSERT                        1
#define RST_U0_U7MC_SFT7110_TRACE_RST2_CLEAR                         0
#define RST_U0_U7MC_SFT7110_TRACE_RST3_SHIFT                         18
#define RST_U0_U7MC_SFT7110_TRACE_RST3_MASK                          (0x1 << 18)
#define RST_U0_U7MC_SFT7110_TRACE_RST3_ASSERT                        1
#define RST_U0_U7MC_SFT7110_TRACE_RST3_CLEAR                         0
#define RST_U0_U7MC_SFT7110_TRACE_RST4_SHIFT                         19
#define RST_U0_U7MC_SFT7110_TRACE_RST4_MASK                          (0x1 << 19)
#define RST_U0_U7MC_SFT7110_TRACE_RST4_ASSERT                        1
#define RST_U0_U7MC_SFT7110_TRACE_RST4_CLEAR                         0
#define RST_U0_U7MC_SFT7110_TRACE_COM_RST_SHIFT                      20
#define RST_U0_U7MC_SFT7110_TRACE_COM_RST_MASK                       (0x1 << 20)
#define RST_U0_U7MC_SFT7110_TRACE_COM_RST_ASSERT                     1
#define RST_U0_U7MC_SFT7110_TRACE_COM_RST_CLEAR                      0
#define RSTN_U0_IMG_GPU_RSTN_APB_SHIFT                               21
#define RSTN_U0_IMG_GPU_RSTN_APB_MASK                                (0x1 << 21)
#define RSTN_U0_IMG_GPU_RSTN_APB_ASSERT                              1
#define RSTN_U0_IMG_GPU_RSTN_APB_CLEAR                               0
#define RSTN_U0_IMG_GPU_RSTN_DOMA_SHIFT                              22
#define RSTN_U0_IMG_GPU_RSTN_DOMA_MASK                               (0x1 << 22)
#define RSTN_U0_IMG_GPU_RSTN_DOMA_ASSERT                             1
#define RSTN_U0_IMG_GPU_RSTN_DOMA_CLEAR                              0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_SHIFT                23
#define RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_MASK                 (0x1 << 23)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_ASSERT               1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_CLEAR                0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_SHIFT            24
#define RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_MASK             (0x1 << 24)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_ASSERT           1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_CLEAR            0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_SHIFT                25
#define RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_MASK                 (0x1 << 25)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_ASSERT               1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_CLEAR                0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_SHIFT               26
#define RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_MASK                (0x1 << 26)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_ASSERT              1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_CLEAR               0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_SHIFT                27
#define RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_MASK                 (0x1 << 27)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_ASSERT               1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_CLEAR                0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_SHIFT                28
#define RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_MASK                 (0x1 << 28)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_ASSERT               1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_CLEAR                0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_SHIFT                   29
#define RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_MASK                    (0x1 << 29)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_ASSERT                  1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_CLEAR                   0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_SHIFT                30
#define RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_MASK                 (0x1 << 30)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_ASSERT               1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_CLEAR                0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_SHIFT               31
#define RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_MASK                (0x1 << 31)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_ASSERT              1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_CLEAR               0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_SHIFT               0
#define RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_MASK                (0x1 << 0)
#define RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_ASSERT              1
#define RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_CLEAR               0
#define RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_SHIFT                          1
#define RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_MASK                           (0x1 << 1)
#define RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_ASSERT                         1
#define RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_CLEAR                          0
#define RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_SHIFT                         2
#define RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_MASK                          (0x1 << 2)
#define RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_ASSERT                        1
#define RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_CLEAR                         0
#define RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_SHIFT                         3
#define RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_MASK                          (0x1 << 3)
#define RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_ASSERT                        1
#define RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_CLEAR                         0
#define RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_SHIFT                     4
#define RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_MASK                      (0x1 << 4)
#define RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_ASSERT                    1
#define RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_CLEAR                     0
#define RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_SHIFT                        5
#define RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_MASK                         (0x1 << 5)
#define RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_ASSERT                       1
#define RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_CLEAR                        0
#define RSTN_U0_DDR_SFT7110_RSTN_AXI_SHIFT                           6
#define RSTN_U0_DDR_SFT7110_RSTN_AXI_MASK                            (0x1 << 6)
#define RSTN_U0_DDR_SFT7110_RSTN_AXI_ASSERT                          1
#define RSTN_U0_DDR_SFT7110_RSTN_AXI_CLEAR                           0
#define RSTN_U0_DDR_SFT7110_RSTN_OSC_SHIFT                           7
#define RSTN_U0_DDR_SFT7110_RSTN_OSC_MASK                            (0x1 << 7)
#define RSTN_U0_DDR_SFT7110_RSTN_OSC_ASSERT                          1
#define RSTN_U0_DDR_SFT7110_RSTN_OSC_CLEAR                           0
#define RSTN_U0_DDR_SFT7110_RSTN_APB_SHIFT                           8
#define RSTN_U0_DDR_SFT7110_RSTN_APB_MASK                            (0x1 << 8)
#define RSTN_U0_DDR_SFT7110_RSTN_APB_ASSERT                          1
#define RSTN_U0_DDR_SFT7110_RSTN_APB_CLEAR                           0
#define RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_SHIFT    9
#define RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_MASK     (0x1 << 9)
#define RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_ASSERT   1
#define RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_CLEAR    0
#define RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_SHIFT      10
#define RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_MASK       (0x1 << 10)
#define RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_ASSERT     1
#define RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_CLEAR      0
#define RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_SHIFT   11
#define RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_MASK    (0x1 << 11)
#define RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_ASSERT  1
#define RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_CLEAR   0
#define RSTN_U0_CODAJ12_RSTN_AXI_SHIFT                               12
#define RSTN_U0_CODAJ12_RSTN_AXI_MASK                                (0x1 << 12)
#define RSTN_U0_CODAJ12_RSTN_AXI_ASSERT                              1
#define RSTN_U0_CODAJ12_RSTN_AXI_CLEAR                               0
#define RSTN_U0_CODAJ12_RSTN_CORE_SHIFT                              13
#define RSTN_U0_CODAJ12_RSTN_CORE_MASK                               (0x1 << 13)
#define RSTN_U0_CODAJ12_RSTN_CORE_ASSERT                             1
#define RSTN_U0_CODAJ12_RSTN_CORE_CLEAR                              0
#define RSTN_U0_CODAJ12_RSTN_APB_SHIFT                               14
#define RSTN_U0_CODAJ12_RSTN_APB_MASK                                (0x1 << 14)
#define RSTN_U0_CODAJ12_RSTN_APB_ASSERT                              1
#define RSTN_U0_CODAJ12_RSTN_APB_CLEAR                               0
#define RSTN_U0_WAVE511_RSTN_AXI_SHIFT                               15
#define RSTN_U0_WAVE511_RSTN_AXI_MASK                                (0x1 << 15)
#define RSTN_U0_WAVE511_RSTN_AXI_ASSERT                              1
#define RSTN_U0_WAVE511_RSTN_AXI_CLEAR                               0
#define RSTN_U0_WAVE511_RSTN_BPU_SHIFT                               16
#define RSTN_U0_WAVE511_RSTN_BPU_MASK                                (0x1 << 16)
#define RSTN_U0_WAVE511_RSTN_BPU_ASSERT                              1
#define RSTN_U0_WAVE511_RSTN_BPU_CLEAR                               0
#define RSTN_U0_WAVE511_RSTN_VCE_SHIFT                               17
#define RSTN_U0_WAVE511_RSTN_VCE_MASK                                (0x1 << 17)
#define RSTN_U0_WAVE511_RSTN_VCE_ASSERT                              1
#define RSTN_U0_WAVE511_RSTN_VCE_CLEAR                               0
#define RSTN_U0_WAVE511_RSTN_APB_SHIFT                               18
#define RSTN_U0_WAVE511_RSTN_APB_MASK                                (0x1 << 18)
#define RSTN_U0_WAVE511_RSTN_APB_ASSERT                              1
#define RSTN_U0_WAVE511_RSTN_APB_CLEAR                               0
#define RSTN_U0_VDEC_JPG_ARB_JPGRESETN_SHIFT                         19
#define RSTN_U0_VDEC_JPG_ARB_JPGRESETN_MASK                          (0x1 << 19)
#define RSTN_U0_VDEC_JPG_ARB_JPGRESETN_ASSERT                        1
#define RSTN_U0_VDEC_JPG_ARB_JPGRESETN_CLEAR                         0
#define RSTN_U0_VDEC_JPG_ARB_MAINRESETN_SHIFT                        20
#define RSTN_U0_VDEC_JPG_ARB_MAINRESETN_MASK                         (0x1 << 20)
#define RSTN_U0_VDEC_JPG_ARB_MAINRESETN_ASSERT                       1
#define RSTN_U0_VDEC_JPG_ARB_MAINRESETN_CLEAR                        0
#define RSTN_U0_AXIMEM_128B_RSTN_AXI_SHIFT                           21
#define RSTN_U0_AXIMEM_128B_RSTN_AXI_MASK                            (0x1 << 21)
#define RSTN_U0_AXIMEM_128B_RSTN_AXI_ASSERT                          1
#define RSTN_U0_AXIMEM_128B_RSTN_AXI_CLEAR                           0
#define RSTN_U0_WAVE420L_RSTN_AXI_SHIFT                              22
#define RSTN_U0_WAVE420L_RSTN_AXI_MASK                               (0x1 << 22)
#define RSTN_U0_WAVE420L_RSTN_AXI_ASSERT                             1
#define RSTN_U0_WAVE420L_RSTN_AXI_CLEAR                              0
#define RSTN_U0_WAVE420L_RSTN_BPU_SHIFT                              23
#define RSTN_U0_WAVE420L_RSTN_BPU_MASK                               (0x1 << 23)
#define RSTN_U0_WAVE420L_RSTN_BPU_ASSERT                             1
#define RSTN_U0_WAVE420L_RSTN_BPU_CLEAR                              0
#define RSTN_U0_WAVE420L_RSTN_VCE_SHIFT                              24
#define RSTN_U0_WAVE420L_RSTN_VCE_MASK                               (0x1 << 24)
#define RSTN_U0_WAVE420L_RSTN_VCE_ASSERT                             1
#define RSTN_U0_WAVE420L_RSTN_VCE_CLEAR                              0
#define RSTN_U0_WAVE420L_RSTN_APB_SHIFT                              25
#define RSTN_U0_WAVE420L_RSTN_APB_MASK                               (0x1 << 25)
#define RSTN_U0_WAVE420L_RSTN_APB_ASSERT                             1
#define RSTN_U0_WAVE420L_RSTN_APB_CLEAR                              0
#define RSTN_U1_AXIMEM_128B_RSTN_AXI_SHIFT                           26
#define RSTN_U1_AXIMEM_128B_RSTN_AXI_MASK                            (0x1 << 26)
#define RSTN_U1_AXIMEM_128B_RSTN_AXI_ASSERT                          1
#define RSTN_U1_AXIMEM_128B_RSTN_AXI_CLEAR                           0
#define RSTN_U2_AXIMEM_128B_RSTN_AXI_SHIFT                           27
#define RSTN_U2_AXIMEM_128B_RSTN_AXI_MASK                            (0x1 << 27)
#define RSTN_U2_AXIMEM_128B_RSTN_AXI_ASSERT                          1
#define RSTN_U2_AXIMEM_128B_RSTN_AXI_CLEAR                           0
#define RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_SHIFT                       28
#define RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_MASK                        (0x1 << 28)
#define RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_ASSERT                      1
#define RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_CLEAR                       0
#define RSTN_U0_CDNS_QSPI_RSTN_AHB_SHIFT                             29
#define RSTN_U0_CDNS_QSPI_RSTN_AHB_MASK                              (0x1 << 29)
#define RSTN_U0_CDNS_QSPI_RSTN_AHB_ASSERT                            1
#define RSTN_U0_CDNS_QSPI_RSTN_AHB_CLEAR                             0
#define RSTN_U0_CDNS_QSPI_RSTN_APB_SHIFT                             30
#define RSTN_U0_CDNS_QSPI_RSTN_APB_MASK                              (0x1 << 30)
#define RSTN_U0_CDNS_QSPI_RSTN_APB_ASSERT                            1
#define RSTN_U0_CDNS_QSPI_RSTN_APB_CLEAR                             0
#define RSTN_U0_CDNS_QSPI_RSTN_REF_SHIFT                             31
#define RSTN_U0_CDNS_QSPI_RSTN_REF_MASK                              (0x1 << 31)
#define RSTN_U0_CDNS_QSPI_RSTN_REF_ASSERT                            1
#define RSTN_U0_CDNS_QSPI_RSTN_REF_CLEAR                             0
#define RSTN_U0_DW_SDIO_RSTN_AHB_SHIFT                               0
#define RSTN_U0_DW_SDIO_RSTN_AHB_MASK                                (0x1 << 0)
#define RSTN_U0_DW_SDIO_RSTN_AHB_ASSERT                              1
#define RSTN_U0_DW_SDIO_RSTN_AHB_CLEAR                               0
#define RSTN_U1_DW_SDIO_RSTN_AHB_SHIFT                               1
#define RSTN_U1_DW_SDIO_RSTN_AHB_MASK                                (0x1 << 1)
#define RSTN_U1_DW_SDIO_RSTN_AHB_ASSERT                              1
#define RSTN_U1_DW_SDIO_RSTN_AHB_CLEAR                               0
#define RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_SHIFT                       2
#define RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_MASK                        (0x1 << 2)
#define RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_ASSERT                      1
#define RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_CLEAR                       0
#define RSTN_U1_DW_GMAC5_AXI64_HRESET_N_SHIFT                        3
#define RSTN_U1_DW_GMAC5_AXI64_HRESET_N_MASK                         (0x1 << 3)
#define RSTN_U1_DW_GMAC5_AXI64_HRESET_N_ASSERT                       1
#define RSTN_U1_DW_GMAC5_AXI64_HRESET_N_CLEAR                        0
#define RSTN_U0_MAILBOX_PRESETN_SHIFT                                4
#define RSTN_U0_MAILBOX_PRESETN_MASK                                 (0x1 << 4)
#define RSTN_U0_MAILBOX_PRESETN_ASSERT                               1
#define RSTN_U0_MAILBOX_PRESETN_CLEAR                                0
#define RSTN_U0_SSP_SPI_RSTN_APB_SHIFT                               5
#define RSTN_U0_SSP_SPI_RSTN_APB_MASK                                (0x1 << 5)
#define RSTN_U0_SSP_SPI_RSTN_APB_ASSERT                              1
#define RSTN_U0_SSP_SPI_RSTN_APB_CLEAR                               0
#define RSTN_U1_SSP_SPI_RSTN_APB_SHIFT                               6
#define RSTN_U1_SSP_SPI_RSTN_APB_MASK                                (0x1 << 6)
#define RSTN_U1_SSP_SPI_RSTN_APB_ASSERT                              1
#define RSTN_U1_SSP_SPI_RSTN_APB_CLEAR                               0
#define RSTN_U2_SSP_SPI_RSTN_APB_SHIFT                               7
#define RSTN_U2_SSP_SPI_RSTN_APB_MASK                                (0x1 << 7)
#define RSTN_U2_SSP_SPI_RSTN_APB_ASSERT                              1
#define RSTN_U2_SSP_SPI_RSTN_APB_CLEAR                               0
#define RSTN_U3_SSP_SPI_RSTN_APB_SHIFT                               8
#define RSTN_U3_SSP_SPI_RSTN_APB_MASK                                (0x1 << 8)
#define RSTN_U3_SSP_SPI_RSTN_APB_ASSERT                              1
#define RSTN_U3_SSP_SPI_RSTN_APB_CLEAR                               0
#define RSTN_U4_SSP_SPI_RSTN_APB_SHIFT                               9
#define RSTN_U4_SSP_SPI_RSTN_APB_MASK                                (0x1 << 9)
#define RSTN_U4_SSP_SPI_RSTN_APB_ASSERT                              1
#define RSTN_U4_SSP_SPI_RSTN_APB_CLEAR                               0
#define RSTN_U5_SSP_SPI_RSTN_APB_SHIFT                               10
#define RSTN_U5_SSP_SPI_RSTN_APB_MASK                                (0x1 << 10)
#define RSTN_U5_SSP_SPI_RSTN_APB_ASSERT                              1
#define RSTN_U5_SSP_SPI_RSTN_APB_CLEAR                               0
#define RSTN_U6_SSP_SPI_RSTN_APB_SHIFT                               11
#define RSTN_U6_SSP_SPI_RSTN_APB_MASK                                (0x1 << 11)
#define RSTN_U6_SSP_SPI_RSTN_APB_ASSERT                              1
#define RSTN_U6_SSP_SPI_RSTN_APB_CLEAR                               0
#define RSTN_U0_DW_I2C_RSTN_APB_SHIFT                                12
#define RSTN_U0_DW_I2C_RSTN_APB_MASK                                 (0x1 << 12)
#define RSTN_U0_DW_I2C_RSTN_APB_ASSERT                               1
#define RSTN_U0_DW_I2C_RSTN_APB_CLEAR                                0
#define RSTN_U1_DW_I2C_RSTN_APB_SHIFT                                13
#define RSTN_U1_DW_I2C_RSTN_APB_MASK                                 (0x1 << 13)
#define RSTN_U1_DW_I2C_RSTN_APB_ASSERT                               1
#define RSTN_U1_DW_I2C_RSTN_APB_CLEAR                                0
#define RSTN_U2_DW_I2C_RSTN_APB_SHIFT                                14
#define RSTN_U2_DW_I2C_RSTN_APB_MASK                                 (0x1 << 14)
#define RSTN_U2_DW_I2C_RSTN_APB_ASSERT                               1
#define RSTN_U2_DW_I2C_RSTN_APB_CLEAR                                0
#define RSTN_U3_DW_I2C_RSTN_APB_SHIFT                                15
#define RSTN_U3_DW_I2C_RSTN_APB_MASK                                 (0x1 << 15)
#define RSTN_U3_DW_I2C_RSTN_APB_ASSERT                               1
#define RSTN_U3_DW_I2C_RSTN_APB_CLEAR                                0
#define RSTN_U4_DW_I2C_RSTN_APB_SHIFT                                16
#define RSTN_U4_DW_I2C_RSTN_APB_MASK                                 (0x1 << 16)
#define RSTN_U4_DW_I2C_RSTN_APB_ASSERT                               1
#define RSTN_U4_DW_I2C_RSTN_APB_CLEAR                                0
#define RSTN_U5_DW_I2C_RSTN_APB_SHIFT                                17
#define RSTN_U5_DW_I2C_RSTN_APB_MASK                                 (0x1 << 17)
#define RSTN_U5_DW_I2C_RSTN_APB_ASSERT                               1
#define RSTN_U5_DW_I2C_RSTN_APB_CLEAR                                0
#define RSTN_U6_DW_I2C_RSTN_APB_SHIFT                                18
#define RSTN_U6_DW_I2C_RSTN_APB_MASK                                 (0x1 << 18)
#define RSTN_U6_DW_I2C_RSTN_APB_ASSERT                               1
#define RSTN_U6_DW_I2C_RSTN_APB_CLEAR                                0
#define RSTN_U0_DW_UART_RSTN_APB_SHIFT                               19
#define RSTN_U0_DW_UART_RSTN_APB_MASK                                (0x1 << 19)
#define RSTN_U0_DW_UART_RSTN_APB_ASSERT                              1
#define RSTN_U0_DW_UART_RSTN_APB_CLEAR                               0
#define RSTN_U0_DW_UART_RSTN_CORE_SHIFT                              20
#define RSTN_U0_DW_UART_RSTN_CORE_MASK                               (0x1 << 20)
#define RSTN_U0_DW_UART_RSTN_CORE_ASSERT                             1
#define RSTN_U0_DW_UART_RSTN_CORE_CLEAR                              0
#define RSTN_U1_DW_UART_RSTN_APB_SHIFT                               21
#define RSTN_U1_DW_UART_RSTN_APB_MASK                                (0x1 << 21)
#define RSTN_U1_DW_UART_RSTN_APB_ASSERT                              1
#define RSTN_U1_DW_UART_RSTN_APB_CLEAR                               0
#define RSTN_U1_DW_UART_RSTN_CORE_SHIFT                              22
#define RSTN_U1_DW_UART_RSTN_CORE_MASK                               (0x1 << 22)
#define RSTN_U1_DW_UART_RSTN_CORE_ASSERT                             1
#define RSTN_U1_DW_UART_RSTN_CORE_CLEAR                              0
#define RSTN_U2_DW_UART_RSTN_APB_SHIFT                               23
#define RSTN_U2_DW_UART_RSTN_APB_MASK                                (0x1 << 23)
#define RSTN_U2_DW_UART_RSTN_APB_ASSERT                              1
#define RSTN_U2_DW_UART_RSTN_APB_CLEAR                               0
#define RSTN_U2_DW_UART_RSTN_CORE_SHIFT                              24
#define RSTN_U2_DW_UART_RSTN_CORE_MASK                               (0x1 << 24)
#define RSTN_U2_DW_UART_RSTN_CORE_ASSERT                             1
#define RSTN_U2_DW_UART_RSTN_CORE_CLEAR                              0
#define RSTN_U3_DW_UART_RSTN_APB_SHIFT                               25
#define RSTN_U3_DW_UART_RSTN_APB_MASK                                (0x1 << 25)
#define RSTN_U3_DW_UART_RSTN_APB_ASSERT                              1
#define RSTN_U3_DW_UART_RSTN_APB_CLEAR                               0
#define RSTN_U3_DW_UART_RSTN_CORE_SHIFT                              26
#define RSTN_U3_DW_UART_RSTN_CORE_MASK                               (0x1 << 26)
#define RSTN_U3_DW_UART_RSTN_CORE_ASSERT                             1
#define RSTN_U3_DW_UART_RSTN_CORE_CLEAR                              0
#define RSTN_U4_DW_UART_RSTN_APB_SHIFT                               27
#define RSTN_U4_DW_UART_RSTN_APB_MASK                                (0x1 << 27)
#define RSTN_U4_DW_UART_RSTN_APB_ASSERT                              1
#define RSTN_U4_DW_UART_RSTN_APB_CLEAR                               0
#define RSTN_U4_DW_UART_RSTN_CORE_SHIFT                              28
#define RSTN_U4_DW_UART_RSTN_CORE_MASK                               (0x1 << 28)
#define RSTN_U4_DW_UART_RSTN_CORE_ASSERT                             1
#define RSTN_U4_DW_UART_RSTN_CORE_CLEAR                              0
#define RSTN_U5_DW_UART_RSTN_APB_SHIFT                               29
#define RSTN_U5_DW_UART_RSTN_APB_MASK                                (0x1 << 29)
#define RSTN_U5_DW_UART_RSTN_APB_ASSERT                              1
#define RSTN_U5_DW_UART_RSTN_APB_CLEAR                               0
#define RSTN_U5_DW_UART_RSTN_CORE_SHIFT                              30
#define RSTN_U5_DW_UART_RSTN_CORE_MASK                               (0x1 << 30)
#define RSTN_U5_DW_UART_RSTN_CORE_ASSERT                             1
#define RSTN_U5_DW_UART_RSTN_CORE_CLEAR                              0
#define RSTN_U0_CDNS_SPDIF_RSTN_APB_SHIFT                            31
#define RSTN_U0_CDNS_SPDIF_RSTN_APB_MASK                             (0x1 << 31)
#define RSTN_U0_CDNS_SPDIF_RSTN_APB_ASSERT                           1
#define RSTN_U0_CDNS_SPDIF_RSTN_APB_CLEAR                            0
#define RSTN_U0_PWMDAC_RSTN_APB_SHIFT                                0
#define RSTN_U0_PWMDAC_RSTN_APB_MASK                                 (0x1 << 0)
#define RSTN_U0_PWMDAC_RSTN_APB_ASSERT                               1
#define RSTN_U0_PWMDAC_RSTN_APB_CLEAR                                0
#define RSTN_U0_PDM_4MIC_RSTN_DMIC_SHIFT                             1
#define RSTN_U0_PDM_4MIC_RSTN_DMIC_MASK                              (0x1 << 1)
#define RSTN_U0_PDM_4MIC_RSTN_DMIC_ASSERT                            1
#define RSTN_U0_PDM_4MIC_RSTN_DMIC_CLEAR                             0
#define RSTN_U0_PDM_4MIC_RSTN_APB_SHIFT                              2
#define RSTN_U0_PDM_4MIC_RSTN_APB_MASK                               (0x1 << 2)
#define RSTN_U0_PDM_4MIC_RSTN_APB_ASSERT                             1
#define RSTN_U0_PDM_4MIC_RSTN_APB_CLEAR                              0
#define RSTN_U0_I2SRX_3CH_RSTN_APB_SHIFT                             3
#define RSTN_U0_I2SRX_3CH_RSTN_APB_MASK                              (0x1 << 3)
#define RSTN_U0_I2SRX_3CH_RSTN_APB_ASSERT                            1
#define RSTN_U0_I2SRX_3CH_RSTN_APB_CLEAR                             0
#define RSTN_U0_I2SRX_3CH_RSTN_BCLK_SHIFT                            4
#define RSTN_U0_I2SRX_3CH_RSTN_BCLK_MASK                             (0x1 << 4)
#define RSTN_U0_I2SRX_3CH_RSTN_BCLK_ASSERT                           1
#define RSTN_U0_I2SRX_3CH_RSTN_BCLK_CLEAR                            0
#define RSTN_U0_I2STX_4CH_RSTN_APB_SHIFT                             5
#define RSTN_U0_I2STX_4CH_RSTN_APB_MASK                              (0x1 << 5)
#define RSTN_U0_I2STX_4CH_RSTN_APB_ASSERT                            1
#define RSTN_U0_I2STX_4CH_RSTN_APB_CLEAR                             0
#define RSTN_U0_I2STX_4CH_RSTN_BCLK_SHIFT                            6
#define RSTN_U0_I2STX_4CH_RSTN_BCLK_MASK                             (0x1 << 6)
#define RSTN_U0_I2STX_4CH_RSTN_BCLK_ASSERT                           1
#define RSTN_U0_I2STX_4CH_RSTN_BCLK_CLEAR                            0
#define RSTN_U1_I2STX_4CH_RSTN_APB_SHIFT                             7
#define RSTN_U1_I2STX_4CH_RSTN_APB_MASK                              (0x1 << 7)
#define RSTN_U1_I2STX_4CH_RSTN_APB_ASSERT                            1
#define RSTN_U1_I2STX_4CH_RSTN_APB_CLEAR                             0
#define RSTN_U1_I2STX_4CH_RSTN_BCLK_SHIFT                            8
#define RSTN_U1_I2STX_4CH_RSTN_BCLK_MASK                             (0x1 << 8)
#define RSTN_U1_I2STX_4CH_RSTN_BCLK_ASSERT                           1
#define RSTN_U1_I2STX_4CH_RSTN_BCLK_CLEAR                            0
#define RSTN_U0_TDM16SLOT_RSTN_AHB_SHIFT                             9
#define RSTN_U0_TDM16SLOT_RSTN_AHB_MASK                              (0x1 << 9)
#define RSTN_U0_TDM16SLOT_RSTN_AHB_ASSERT                            1
#define RSTN_U0_TDM16SLOT_RSTN_AHB_CLEAR                             0
#define RSTN_U0_TDM16SLOT_RSTN_TDM_SHIFT                             10
#define RSTN_U0_TDM16SLOT_RSTN_TDM_MASK                              (0x1 << 10)
#define RSTN_U0_TDM16SLOT_RSTN_TDM_ASSERT                            1
#define RSTN_U0_TDM16SLOT_RSTN_TDM_CLEAR                             0
#define RSTN_U0_TDM16SLOT_RSTN_APB_SHIFT                             11
#define RSTN_U0_TDM16SLOT_RSTN_APB_MASK                              (0x1 << 11)
#define RSTN_U0_TDM16SLOT_RSTN_APB_ASSERT                            1
#define RSTN_U0_TDM16SLOT_RSTN_APB_CLEAR                             0
#define RSTN_U0_PWM_8CH_RSTN_APB_SHIFT                               12
#define RSTN_U0_PWM_8CH_RSTN_APB_MASK                                (0x1 << 12)
#define RSTN_U0_PWM_8CH_RSTN_APB_ASSERT                              1
#define RSTN_U0_PWM_8CH_RSTN_APB_CLEAR                               0
#define RSTN_U0_DSKIT_WDT_RSTN_APB_SHIFT                             13
#define RSTN_U0_DSKIT_WDT_RSTN_APB_MASK                              (0x1 << 13)
#define RSTN_U0_DSKIT_WDT_RSTN_APB_ASSERT                            1
#define RSTN_U0_DSKIT_WDT_RSTN_APB_CLEAR                             0
#define RSTN_U0_DSKIT_WDT_RSTN_WDT_SHIFT                             14
#define RSTN_U0_DSKIT_WDT_RSTN_WDT_MASK                              (0x1 << 14)
#define RSTN_U0_DSKIT_WDT_RSTN_WDT_ASSERT                            1
#define RSTN_U0_DSKIT_WDT_RSTN_WDT_CLEAR                             0
#define RSTN_U0_CAN_CTRL_RSTN_APB_SHIFT                              15
#define RSTN_U0_CAN_CTRL_RSTN_APB_MASK                               (0x1 << 15)
#define RSTN_U0_CAN_CTRL_RSTN_APB_ASSERT                             1
#define RSTN_U0_CAN_CTRL_RSTN_APB_CLEAR                              0
#define RSTN_U0_CAN_CTRL_RSTN_CAN_SHIFT                              16
#define RSTN_U0_CAN_CTRL_RSTN_CAN_MASK                               (0x1 << 16)
#define RSTN_U0_CAN_CTRL_RSTN_CAN_ASSERT                             1
#define RSTN_U0_CAN_CTRL_RSTN_CAN_CLEAR                              0
#define RSTN_U0_CAN_CTRL_RSTN_TIMER_SHIFT                            17
#define RSTN_U0_CAN_CTRL_RSTN_TIMER_MASK                             (0x1 << 17)
#define RSTN_U0_CAN_CTRL_RSTN_TIMER_ASSERT                           1
#define RSTN_U0_CAN_CTRL_RSTN_TIMER_CLEAR                            0
#define RSTN_U1_CAN_CTRL_RSTN_APB_SHIFT                              18
#define RSTN_U1_CAN_CTRL_RSTN_APB_MASK                               (0x1 << 18)
#define RSTN_U1_CAN_CTRL_RSTN_APB_ASSERT                             1
#define RSTN_U1_CAN_CTRL_RSTN_APB_CLEAR                              0
#define RSTN_U1_CAN_CTRL_RSTN_CAN_SHIFT                              19
#define RSTN_U1_CAN_CTRL_RSTN_CAN_MASK                               (0x1 << 19)
#define RSTN_U1_CAN_CTRL_RSTN_CAN_ASSERT                             1
#define RSTN_U1_CAN_CTRL_RSTN_CAN_CLEAR                              0
#define RSTN_U1_CAN_CTRL_RSTN_TIMER_SHIFT                            20
#define RSTN_U1_CAN_CTRL_RSTN_TIMER_MASK                             (0x1 << 20)
#define RSTN_U1_CAN_CTRL_RSTN_TIMER_ASSERT                           1
#define RSTN_U1_CAN_CTRL_RSTN_TIMER_CLEAR                            0
#define RSTN_U0_SI5_TIMER_RSTN_APB_SHIFT                             21
#define RSTN_U0_SI5_TIMER_RSTN_APB_MASK                              (0x1 << 21)
#define RSTN_U0_SI5_TIMER_RSTN_APB_ASSERT                            1
#define RSTN_U0_SI5_TIMER_RSTN_APB_CLEAR                             0
#define RSTN_U0_SI5_TIMER_RSTN_TIMER0_SHIFT                          22
#define RSTN_U0_SI5_TIMER_RSTN_TIMER0_MASK                           (0x1 << 22)
#define RSTN_U0_SI5_TIMER_RSTN_TIMER0_ASSERT                         1
#define RSTN_U0_SI5_TIMER_RSTN_TIMER0_CLEAR                          0
#define RSTN_U0_SI5_TIMER_RSTN_TIMER1_SHIFT                          23
#define RSTN_U0_SI5_TIMER_RSTN_TIMER1_MASK                           (0x1 << 23)
#define RSTN_U0_SI5_TIMER_RSTN_TIMER1_ASSERT                         1
#define RSTN_U0_SI5_TIMER_RSTN_TIMER1_CLEAR                          0
#define RSTN_U0_SI5_TIMER_RSTN_TIMER2_SHIFT                          24
#define RSTN_U0_SI5_TIMER_RSTN_TIMER2_MASK                           (0x1 << 24)
#define RSTN_U0_SI5_TIMER_RSTN_TIMER2_ASSERT                         1
#define RSTN_U0_SI5_TIMER_RSTN_TIMER2_CLEAR                          0
#define RSTN_U0_SI5_TIMER_RSTN_TIMER3_SHIFT                          25
#define RSTN_U0_SI5_TIMER_RSTN_TIMER3_MASK                           (0x1 << 25)
#define RSTN_U0_SI5_TIMER_RSTN_TIMER3_ASSERT                         1
#define RSTN_U0_SI5_TIMER_RSTN_TIMER3_CLEAR                          0
#define RSTN_U0_INT_CTRL_RSTN_APB_SHIFT                              26
#define RSTN_U0_INT_CTRL_RSTN_APB_MASK                               (0x1 << 26)
#define RSTN_U0_INT_CTRL_RSTN_APB_ASSERT                             1
#define RSTN_U0_INT_CTRL_RSTN_APB_CLEAR                              0
#define RSTN_U0_TEMP_SENSOR_RSTN_APB_SHIFT                           27
#define RSTN_U0_TEMP_SENSOR_RSTN_APB_MASK                            (0x1 << 27)
#define RSTN_U0_TEMP_SENSOR_RSTN_APB_ASSERT                          1
#define RSTN_U0_TEMP_SENSOR_RSTN_APB_CLEAR                           0
#define RSTN_U0_TEMP_SENSOR_RSTN_TEMP_SHIFT                          28
#define RSTN_U0_TEMP_SENSOR_RSTN_TEMP_MASK                           (0x1 << 28)
#define RSTN_U0_TEMP_SENSOR_RSTN_TEMP_ASSERT                         1
#define RSTN_U0_TEMP_SENSOR_RSTN_TEMP_CLEAR                          0
#define RSTN_U0_JTAG_CERTIFICATION_RST_N_SHIFT                       29
#define RSTN_U0_JTAG_CERTIFICATION_RST_N_MASK                        (0x1 << 29)
#define RSTN_U0_JTAG_CERTIFICATION_RST_N_ASSERT                      1
#define RSTN_U0_JTAG_CERTIFICATION_RST_N_CLEAR                       0

#define _SWITCH_CLOCK_CLK_CPU_ROOT_SOURCE_CLK_OSC_ 	saif_set_reg(CLK_CPU_ROOT_CTRL_REG_ADDR, CLK_CPU_ROOT_SW_CLK_OSC_DATA, CLK_CPU_ROOT_SW_SHIFT, CLK_CPU_ROOT_SW_MASK)
#define _SWITCH_CLOCK_CLK_CPU_ROOT_SOURCE_CLK_PLL0_ 	saif_set_reg(CLK_CPU_ROOT_CTRL_REG_ADDR, CLK_CPU_ROOT_SW_CLK_PLL0_DATA, CLK_CPU_ROOT_SW_SHIFT, CLK_CPU_ROOT_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_CPU_ROOT_ 		saif_get_reg(CLK_CPU_ROOT_CTRL_REG_ADDR, CLK_CPU_ROOT_SW_SHIFT, CLK_CPU_ROOT_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_CPU_ROOT_(x) 		saif_set_reg(CLK_CPU_ROOT_CTRL_REG_ADDR, x, CLK_CPU_ROOT_SW_SHIFT, CLK_CPU_ROOT_SW_MASK)
#define _DIVIDE_CLOCK_CLK_CPU_CORE_(div) 			saif_set_reg(CLK_CPU_CORE_CTRL_REG_ADDR, div, CLK_CPU_CORE_DIV_SHIFT, CLK_CPU_CORE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_CPU_CORE_ 		saif_get_reg(CLK_CPU_CORE_CTRL_REG_ADDR, CLK_CPU_CORE_DIV_SHIFT, CLK_CPU_CORE_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_CPU_BUS_(div) 			saif_set_reg(CLK_CPU_BUS_CTRL_REG_ADDR, div, CLK_CPU_BUS_DIV_SHIFT, CLK_CPU_BUS_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_CPU_BUS_ 		saif_get_reg(CLK_CPU_BUS_CTRL_REG_ADDR, CLK_CPU_BUS_DIV_SHIFT, CLK_CPU_BUS_DIV_MASK)
#define _SWITCH_CLOCK_CLK_GPU_ROOT_SOURCE_CLK_PLL2_ 	saif_set_reg(CLK_GPU_ROOT_CTRL_REG_ADDR, CLK_GPU_ROOT_SW_CLK_PLL2_DATA, CLK_GPU_ROOT_SW_SHIFT, CLK_GPU_ROOT_SW_MASK)
#define _SWITCH_CLOCK_CLK_GPU_ROOT_SOURCE_CLK_PLL1_ 	saif_set_reg(CLK_GPU_ROOT_CTRL_REG_ADDR, CLK_GPU_ROOT_SW_CLK_PLL1_DATA, CLK_GPU_ROOT_SW_SHIFT, CLK_GPU_ROOT_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_GPU_ROOT_ 		saif_get_reg(CLK_GPU_ROOT_CTRL_REG_ADDR, CLK_GPU_ROOT_SW_SHIFT, CLK_GPU_ROOT_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_GPU_ROOT_(x) 		saif_set_reg(CLK_GPU_ROOT_CTRL_REG_ADDR, x, CLK_GPU_ROOT_SW_SHIFT, CLK_GPU_ROOT_SW_MASK)
#define _SWITCH_CLOCK_CLK_PERH_ROOT_SOURCE_CLK_PLL0_ 	saif_set_reg(CLK_PERH_ROOT_CTRL_REG_ADDR, CLK_PERH_ROOT_SW_CLK_PLL0_DATA, CLK_PERH_ROOT_SW_SHIFT, CLK_PERH_ROOT_SW_MASK)
#define _SWITCH_CLOCK_CLK_PERH_ROOT_SOURCE_CLK_PLL2_ 	saif_set_reg(CLK_PERH_ROOT_CTRL_REG_ADDR, CLK_PERH_ROOT_SW_CLK_PLL2_DATA, CLK_PERH_ROOT_SW_SHIFT, CLK_PERH_ROOT_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_PERH_ROOT_ 		saif_get_reg(CLK_PERH_ROOT_CTRL_REG_ADDR, CLK_PERH_ROOT_SW_SHIFT, CLK_PERH_ROOT_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_PERH_ROOT_(x) 		saif_set_reg(CLK_PERH_ROOT_CTRL_REG_ADDR, x, CLK_PERH_ROOT_SW_SHIFT, CLK_PERH_ROOT_SW_MASK)
#define _DIVIDE_CLOCK_CLK_PERH_ROOT_(div) 			saif_set_reg(CLK_PERH_ROOT_CTRL_REG_ADDR, div, CLK_PERH_ROOT_DIV_SHIFT, CLK_PERH_ROOT_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_PERH_ROOT_ 		saif_get_reg(CLK_PERH_ROOT_CTRL_REG_ADDR, CLK_PERH_ROOT_DIV_SHIFT, CLK_PERH_ROOT_DIV_MASK)
#define _SWITCH_CLOCK_CLK_BUS_ROOT_SOURCE_CLK_OSC_ 	saif_set_reg(CLK_BUS_ROOT_CTRL_REG_ADDR, CLK_BUS_ROOT_SW_CLK_OSC_DATA, CLK_BUS_ROOT_SW_SHIFT, CLK_BUS_ROOT_SW_MASK)
#define _SWITCH_CLOCK_CLK_BUS_ROOT_SOURCE_CLK_PLL2_ 	saif_set_reg(CLK_BUS_ROOT_CTRL_REG_ADDR, CLK_BUS_ROOT_SW_CLK_PLL2_DATA, CLK_BUS_ROOT_SW_SHIFT, CLK_BUS_ROOT_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_BUS_ROOT_ 		saif_get_reg(CLK_BUS_ROOT_CTRL_REG_ADDR, CLK_BUS_ROOT_SW_SHIFT, CLK_BUS_ROOT_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_BUS_ROOT_(x) 		saif_set_reg(CLK_BUS_ROOT_CTRL_REG_ADDR, x, CLK_BUS_ROOT_SW_SHIFT, CLK_BUS_ROOT_SW_MASK)
#define _DIVIDE_CLOCK_CLK_NOCSTG_BUS_(div) 			saif_set_reg(CLK_NOCSTG_BUS_CTRL_REG_ADDR, div, CLK_NOCSTG_BUS_DIV_SHIFT, CLK_NOCSTG_BUS_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_NOCSTG_BUS_ 		saif_get_reg(CLK_NOCSTG_BUS_CTRL_REG_ADDR, CLK_NOCSTG_BUS_DIV_SHIFT, CLK_NOCSTG_BUS_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_AXI_CFG0_(div) 			saif_set_reg(CLK_AXI_CFG0_CTRL_REG_ADDR, div, CLK_AXI_CFG0_DIV_SHIFT, CLK_AXI_CFG0_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_AXI_CFG0_ 		saif_get_reg(CLK_AXI_CFG0_CTRL_REG_ADDR, CLK_AXI_CFG0_DIV_SHIFT, CLK_AXI_CFG0_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_STG_AXIAHB_(div) 			saif_set_reg(CLK_STG_AXIAHB_CTRL_REG_ADDR, div, CLK_STG_AXIAHB_DIV_SHIFT, CLK_STG_AXIAHB_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_STG_AXIAHB_ 		saif_get_reg(CLK_STG_AXIAHB_CTRL_REG_ADDR, CLK_STG_AXIAHB_DIV_SHIFT, CLK_STG_AXIAHB_DIV_MASK)
#define _ENABLE_CLOCK_CLK_AHB0_ 			saif_set_reg(CLK_AHB0_CTRL_REG_ADDR, CLK_AHB0_ENABLE_DATA, CLK_AHB0_EN_SHIFT, CLK_AHB0_EN_MASK)
#define _DISABLE_CLOCK_CLK_AHB0_ 			saif_set_reg(CLK_AHB0_CTRL_REG_ADDR, CLK_AHB0_DISABLE_DATA, CLK_AHB0_EN_SHIFT, CLK_AHB0_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_AHB0_ 		saif_get_reg(CLK_AHB0_CTRL_REG_ADDR, CLK_AHB0_EN_SHIFT, CLK_AHB0_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_AHB0_(x) 		saif_set_reg(CLK_AHB0_CTRL_REG_ADDR, x, CLK_AHB0_EN_SHIFT, CLK_AHB0_EN_MASK)
#define _ENABLE_CLOCK_CLK_AHB1_ 			saif_set_reg(CLK_AHB1_CTRL_REG_ADDR, CLK_AHB1_ENABLE_DATA, CLK_AHB1_EN_SHIFT, CLK_AHB1_EN_MASK)
#define _DISABLE_CLOCK_CLK_AHB1_ 			saif_set_reg(CLK_AHB1_CTRL_REG_ADDR, CLK_AHB1_DISABLE_DATA, CLK_AHB1_EN_SHIFT, CLK_AHB1_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_AHB1_ 		saif_get_reg(CLK_AHB1_CTRL_REG_ADDR, CLK_AHB1_EN_SHIFT, CLK_AHB1_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_AHB1_(x) 		saif_set_reg(CLK_AHB1_CTRL_REG_ADDR, x, CLK_AHB1_EN_SHIFT, CLK_AHB1_EN_MASK)
#define _DIVIDE_CLOCK_CLK_APB_BUS_FUNC_(div) 			saif_set_reg(CLK_APB_BUS_FUNC_CTRL_REG_ADDR, div, CLK_APB_BUS_FUNC_DIV_SHIFT, CLK_APB_BUS_FUNC_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_APB_BUS_FUNC_ 		saif_get_reg(CLK_APB_BUS_FUNC_CTRL_REG_ADDR, CLK_APB_BUS_FUNC_DIV_SHIFT, CLK_APB_BUS_FUNC_DIV_MASK)
#define _ENABLE_CLOCK_CLK_APB0_ 			saif_set_reg(CLK_APB0_CTRL_REG_ADDR, CLK_APB0_ENABLE_DATA, CLK_APB0_EN_SHIFT, CLK_APB0_EN_MASK)
#define _DISABLE_CLOCK_CLK_APB0_ 			saif_set_reg(CLK_APB0_CTRL_REG_ADDR, CLK_APB0_DISABLE_DATA, CLK_APB0_EN_SHIFT, CLK_APB0_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_APB0_ 		saif_get_reg(CLK_APB0_CTRL_REG_ADDR, CLK_APB0_EN_SHIFT, CLK_APB0_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_APB0_(x) 		saif_set_reg(CLK_APB0_CTRL_REG_ADDR, x, CLK_APB0_EN_SHIFT, CLK_APB0_EN_MASK)
#define _DIVIDE_CLOCK_CLK_PLL0_DIV2_(div) 			saif_set_reg(CLK_PLL0_DIV2_CTRL_REG_ADDR, div, CLK_PLL0_DIV2_DIV_SHIFT, CLK_PLL0_DIV2_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_PLL0_DIV2_ 		saif_get_reg(CLK_PLL0_DIV2_CTRL_REG_ADDR, CLK_PLL0_DIV2_DIV_SHIFT, CLK_PLL0_DIV2_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_PLL1_DIV2_(div) 			saif_set_reg(CLK_PLL1_DIV2_CTRL_REG_ADDR, div, CLK_PLL1_DIV2_DIV_SHIFT, CLK_PLL1_DIV2_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_PLL1_DIV2_ 		saif_get_reg(CLK_PLL1_DIV2_CTRL_REG_ADDR, CLK_PLL1_DIV2_DIV_SHIFT, CLK_PLL1_DIV2_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_PLL2_DIV2_(div) 			saif_set_reg(CLK_PLL2_DIV2_CTRL_REG_ADDR, div, CLK_PLL2_DIV2_DIV_SHIFT, CLK_PLL2_DIV2_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_PLL2_DIV2_ 		saif_get_reg(CLK_PLL2_DIV2_CTRL_REG_ADDR, CLK_PLL2_DIV2_DIV_SHIFT, CLK_PLL2_DIV2_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_AUDIO_ROOT_(div) 			saif_set_reg(CLK_AUDIO_ROOT_CTRL_REG_ADDR, div, CLK_AUDIO_ROOT_DIV_SHIFT, CLK_AUDIO_ROOT_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_AUDIO_ROOT_ 		saif_get_reg(CLK_AUDIO_ROOT_CTRL_REG_ADDR, CLK_AUDIO_ROOT_DIV_SHIFT, CLK_AUDIO_ROOT_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_MCLK_INNER_(div) 			saif_set_reg(CLK_MCLK_INNER_CTRL_REG_ADDR, div, CLK_MCLK_INNER_DIV_SHIFT, CLK_MCLK_INNER_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_MCLK_INNER_ 		saif_get_reg(CLK_MCLK_INNER_CTRL_REG_ADDR, CLK_MCLK_INNER_DIV_SHIFT, CLK_MCLK_INNER_DIV_MASK)
#define _SWITCH_CLOCK_CLK_MCLK_SOURCE_CLK_MCLK_INNER_ 	saif_set_reg(CLK_MCLK_CTRL_REG_ADDR, CLK_MCLK_SW_CLK_MCLK_INNER_DATA, CLK_MCLK_SW_SHIFT, CLK_MCLK_SW_MASK)
#define _SWITCH_CLOCK_CLK_MCLK_SOURCE_CLK_MCLK_EXT_ 	saif_set_reg(CLK_MCLK_CTRL_REG_ADDR, CLK_MCLK_SW_CLK_MCLK_EXT_DATA, CLK_MCLK_SW_SHIFT, CLK_MCLK_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_MCLK_ 		saif_get_reg(CLK_MCLK_CTRL_REG_ADDR, CLK_MCLK_SW_SHIFT, CLK_MCLK_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_MCLK_(x) 		saif_set_reg(CLK_MCLK_CTRL_REG_ADDR, x, CLK_MCLK_SW_SHIFT, CLK_MCLK_SW_MASK)
#define _ENABLE_CLOCK_MCLK_OUT_ 			saif_set_reg(MCLK_OUT_CTRL_REG_ADDR, MCLK_OUT_ENABLE_DATA, MCLK_OUT_EN_SHIFT, MCLK_OUT_EN_MASK)
#define _DISABLE_CLOCK_MCLK_OUT_ 			saif_set_reg(MCLK_OUT_CTRL_REG_ADDR, MCLK_OUT_DISABLE_DATA, MCLK_OUT_EN_SHIFT, MCLK_OUT_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_MCLK_OUT_ 		saif_get_reg(MCLK_OUT_CTRL_REG_ADDR, MCLK_OUT_EN_SHIFT, MCLK_OUT_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_MCLK_OUT_(x) 		saif_set_reg(MCLK_OUT_CTRL_REG_ADDR, x, MCLK_OUT_EN_SHIFT, MCLK_OUT_EN_MASK)
#define _SWITCH_CLOCK_CLK_ISP_2X_SOURCE_CLK_PLL2_ 	saif_set_reg(CLK_ISP_2X_CTRL_REG_ADDR, CLK_ISP_2X_SW_CLK_PLL2_DATA, CLK_ISP_2X_SW_SHIFT, CLK_ISP_2X_SW_MASK)
#define _SWITCH_CLOCK_CLK_ISP_2X_SOURCE_CLK_PLL1_ 	saif_set_reg(CLK_ISP_2X_CTRL_REG_ADDR, CLK_ISP_2X_SW_CLK_PLL1_DATA, CLK_ISP_2X_SW_SHIFT, CLK_ISP_2X_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_ISP_2X_ 		saif_get_reg(CLK_ISP_2X_CTRL_REG_ADDR, CLK_ISP_2X_SW_SHIFT, CLK_ISP_2X_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_ISP_2X_(x) 		saif_set_reg(CLK_ISP_2X_CTRL_REG_ADDR, x, CLK_ISP_2X_SW_SHIFT, CLK_ISP_2X_SW_MASK)
#define _DIVIDE_CLOCK_CLK_ISP_2X_(div) 			saif_set_reg(CLK_ISP_2X_CTRL_REG_ADDR, div, CLK_ISP_2X_DIV_SHIFT, CLK_ISP_2X_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_ISP_2X_ 		saif_get_reg(CLK_ISP_2X_CTRL_REG_ADDR, CLK_ISP_2X_DIV_SHIFT, CLK_ISP_2X_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_ISP_AXI_(div) 			saif_set_reg(CLK_ISP_AXI_CTRL_REG_ADDR, div, CLK_ISP_AXI_DIV_SHIFT, CLK_ISP_AXI_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_ISP_AXI_ 		saif_get_reg(CLK_ISP_AXI_CTRL_REG_ADDR, CLK_ISP_AXI_DIV_SHIFT, CLK_ISP_AXI_DIV_MASK)
#define _ENABLE_CLOCK_CLK_GCLK0_ 			saif_set_reg(CLK_GCLK0_CTRL_REG_ADDR, CLK_GCLK0_ENABLE_DATA, CLK_GCLK0_EN_SHIFT, CLK_GCLK0_EN_MASK)
#define _DISABLE_CLOCK_CLK_GCLK0_ 			saif_set_reg(CLK_GCLK0_CTRL_REG_ADDR, CLK_GCLK0_DISABLE_DATA, CLK_GCLK0_EN_SHIFT, CLK_GCLK0_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_GCLK0_ 		saif_get_reg(CLK_GCLK0_CTRL_REG_ADDR, CLK_GCLK0_EN_SHIFT, CLK_GCLK0_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_GCLK0_(x) 		saif_set_reg(CLK_GCLK0_CTRL_REG_ADDR, x, CLK_GCLK0_EN_SHIFT, CLK_GCLK0_EN_MASK)
#define _DIVIDE_CLOCK_CLK_GCLK0_(div) 			saif_set_reg(CLK_GCLK0_CTRL_REG_ADDR, div, CLK_GCLK0_DIV_SHIFT, CLK_GCLK0_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GCLK0_ 		saif_get_reg(CLK_GCLK0_CTRL_REG_ADDR, CLK_GCLK0_DIV_SHIFT, CLK_GCLK0_DIV_MASK)
#define _ENABLE_CLOCK_CLK_GCLK1_ 			saif_set_reg(CLK_GCLK1_CTRL_REG_ADDR, CLK_GCLK1_ENABLE_DATA, CLK_GCLK1_EN_SHIFT, CLK_GCLK1_EN_MASK)
#define _DISABLE_CLOCK_CLK_GCLK1_ 			saif_set_reg(CLK_GCLK1_CTRL_REG_ADDR, CLK_GCLK1_DISABLE_DATA, CLK_GCLK1_EN_SHIFT, CLK_GCLK1_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_GCLK1_ 		saif_get_reg(CLK_GCLK1_CTRL_REG_ADDR, CLK_GCLK1_EN_SHIFT, CLK_GCLK1_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_GCLK1_(x) 		saif_set_reg(CLK_GCLK1_CTRL_REG_ADDR, x, CLK_GCLK1_EN_SHIFT, CLK_GCLK1_EN_MASK)
#define _DIVIDE_CLOCK_CLK_GCLK1_(div) 			saif_set_reg(CLK_GCLK1_CTRL_REG_ADDR, div, CLK_GCLK1_DIV_SHIFT, CLK_GCLK1_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GCLK1_ 		saif_get_reg(CLK_GCLK1_CTRL_REG_ADDR, CLK_GCLK1_DIV_SHIFT, CLK_GCLK1_DIV_MASK)
#define _ENABLE_CLOCK_CLK_GCLK2_ 			saif_set_reg(CLK_GCLK2_CTRL_REG_ADDR, CLK_GCLK2_ENABLE_DATA, CLK_GCLK2_EN_SHIFT, CLK_GCLK2_EN_MASK)
#define _DISABLE_CLOCK_CLK_GCLK2_ 			saif_set_reg(CLK_GCLK2_CTRL_REG_ADDR, CLK_GCLK2_DISABLE_DATA, CLK_GCLK2_EN_SHIFT, CLK_GCLK2_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_GCLK2_ 		saif_get_reg(CLK_GCLK2_CTRL_REG_ADDR, CLK_GCLK2_EN_SHIFT, CLK_GCLK2_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_GCLK2_(x) 		saif_set_reg(CLK_GCLK2_CTRL_REG_ADDR, x, CLK_GCLK2_EN_SHIFT, CLK_GCLK2_EN_MASK)
#define _DIVIDE_CLOCK_CLK_GCLK2_(div) 			saif_set_reg(CLK_GCLK2_CTRL_REG_ADDR, div, CLK_GCLK2_DIV_SHIFT, CLK_GCLK2_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GCLK2_ 		saif_get_reg(CLK_GCLK2_CTRL_REG_ADDR, CLK_GCLK2_DIV_SHIFT, CLK_GCLK2_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK_ENABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK_DISABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_CORE_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_CORE_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK1_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK1_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK1_ENABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK1_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK1_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK1_DISABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK1_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_CORE_CLK1_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK1_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK1_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK1_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK2_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK2_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK2_ENABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK2_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK2_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK2_DISABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK2_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_CORE_CLK2_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK2_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK2_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK2_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK3_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK3_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK3_ENABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK3_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK3_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK3_DISABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK3_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_CORE_CLK3_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK3_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK3_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK3_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK4_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK4_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK4_ENABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_CORE_CLK4_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK4_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK4_DISABLE_DATA, CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK4_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_CORE_CLK4_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_CORE_CLK4_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_CORE_CLK4_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_SHIFT, CLK_U0_U7MC_SFT7110_CORE_CLK4_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_DEBUG_CLK_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_DEBUG_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_DEBUG_CLK_ENABLE_DATA, CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_DEBUG_CLK_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_DEBUG_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_DEBUG_CLK_DISABLE_DATA, CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_DEBUG_CLK_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_DEBUG_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_DEBUG_CLK_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_DEBUG_CLK_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_DEBUG_CLK_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_U7MC_SFT7110_RTC_TOGGLE_(div) 			saif_set_reg(CLK_U0_U7MC_SFT7110_RTC_TOGGLE_CTRL_REG_ADDR, div, CLK_U0_U7MC_SFT7110_RTC_TOGGLE_DIV_SHIFT, CLK_U0_U7MC_SFT7110_RTC_TOGGLE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_U7MC_SFT7110_RTC_TOGGLE_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_RTC_TOGGLE_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_RTC_TOGGLE_DIV_SHIFT, CLK_U0_U7MC_SFT7110_RTC_TOGGLE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK0_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK0_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK0_ENABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK0_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK0_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK0_DISABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK0_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK0_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK0_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK0_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK0_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK1_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK1_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK1_ENABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK1_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK1_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK1_DISABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK1_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK1_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK1_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK1_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK1_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK2_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK2_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK2_ENABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK2_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK2_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK2_DISABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK2_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK2_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK2_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK2_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK2_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK3_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK3_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK3_ENABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK3_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK3_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK3_DISABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK3_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK3_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK3_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK3_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK3_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK4_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK4_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK4_ENABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_CLK4_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK4_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK4_DISABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK4_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK4_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_CLK4_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_CLK4_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_CLK4_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_ENABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_ 			saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_DISABLE_DATA, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_ 		saif_get_reg(CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_CTRL_REG_ADDR, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_(x) 		saif_set_reg(CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_CTRL_REG_ADDR, x, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_SHIFT, CLK_U0_U7MC_SFT7110_TRACE_COM_CLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_ENABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_DISABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_ 		saif_get_reg(CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_(x) 		saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_CTRL_REG_ADDR, x, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_CPU_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_ENABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_DISABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_ 		saif_get_reg(CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_(x) 		saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_CTRL_REG_ADDR, x, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_AXICFG0_AXI_EN_MASK)
#define _DIVIDE_CLOCK_CLK_OSC_DIV2_(div) 			saif_set_reg(CLK_OSC_DIV2_CTRL_REG_ADDR, div, CLK_OSC_DIV2_DIV_SHIFT, CLK_OSC_DIV2_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_OSC_DIV2_ 		saif_get_reg(CLK_OSC_DIV2_CTRL_REG_ADDR, CLK_OSC_DIV2_DIV_SHIFT, CLK_OSC_DIV2_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_PLL1_DIV4_(div) 			saif_set_reg(CLK_PLL1_DIV4_CTRL_REG_ADDR, div, CLK_PLL1_DIV4_DIV_SHIFT, CLK_PLL1_DIV4_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_PLL1_DIV4_ 		saif_get_reg(CLK_PLL1_DIV4_CTRL_REG_ADDR, CLK_PLL1_DIV4_DIV_SHIFT, CLK_PLL1_DIV4_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_PLL1_DIV8_(div) 			saif_set_reg(CLK_PLL1_DIV8_CTRL_REG_ADDR, div, CLK_PLL1_DIV8_DIV_SHIFT, CLK_PLL1_DIV8_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_PLL1_DIV8_ 		saif_get_reg(CLK_PLL1_DIV8_CTRL_REG_ADDR, CLK_PLL1_DIV8_DIV_SHIFT, CLK_PLL1_DIV8_DIV_MASK)
#define _SWITCH_CLOCK_CLK_DDR_BUS_SOURCE_CLK_OSC_DIV2_ 	saif_set_reg(CLK_DDR_BUS_CTRL_REG_ADDR, CLK_DDR_BUS_SW_CLK_OSC_DIV2_DATA, CLK_DDR_BUS_SW_SHIFT, CLK_DDR_BUS_SW_MASK)
#define _SWITCH_CLOCK_CLK_DDR_BUS_SOURCE_CLK_PLL1_DIV2_ 	saif_set_reg(CLK_DDR_BUS_CTRL_REG_ADDR, CLK_DDR_BUS_SW_CLK_PLL1_DIV2_DATA, CLK_DDR_BUS_SW_SHIFT, CLK_DDR_BUS_SW_MASK)
#define _SWITCH_CLOCK_CLK_DDR_BUS_SOURCE_CLK_PLL1_DIV4_ 	saif_set_reg(CLK_DDR_BUS_CTRL_REG_ADDR, CLK_DDR_BUS_SW_CLK_PLL1_DIV4_DATA, CLK_DDR_BUS_SW_SHIFT, CLK_DDR_BUS_SW_MASK)
#define _SWITCH_CLOCK_CLK_DDR_BUS_SOURCE_CLK_PLL1_DIV8_ 	saif_set_reg(CLK_DDR_BUS_CTRL_REG_ADDR, CLK_DDR_BUS_SW_CLK_PLL1_DIV8_DATA, CLK_DDR_BUS_SW_SHIFT, CLK_DDR_BUS_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_DDR_BUS_ 		saif_get_reg(CLK_DDR_BUS_CTRL_REG_ADDR, CLK_DDR_BUS_SW_SHIFT, CLK_DDR_BUS_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_DDR_BUS_(x) 		saif_set_reg(CLK_DDR_BUS_CTRL_REG_ADDR, x, CLK_DDR_BUS_SW_SHIFT, CLK_DDR_BUS_SW_MASK)
#define _ENABLE_CLOCK_CLK_U0_DDR_SFT7110_CLK_AXI_ 			saif_set_reg(CLK_U0_DDR_SFT7110_CLK_AXI_CTRL_REG_ADDR, CLK_U0_DDR_SFT7110_CLK_AXI_ENABLE_DATA, CLK_U0_DDR_SFT7110_CLK_AXI_EN_SHIFT, CLK_U0_DDR_SFT7110_CLK_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DDR_SFT7110_CLK_AXI_ 			saif_set_reg(CLK_U0_DDR_SFT7110_CLK_AXI_CTRL_REG_ADDR, CLK_U0_DDR_SFT7110_CLK_AXI_DISABLE_DATA, CLK_U0_DDR_SFT7110_CLK_AXI_EN_SHIFT, CLK_U0_DDR_SFT7110_CLK_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DDR_SFT7110_CLK_AXI_ 		saif_get_reg(CLK_U0_DDR_SFT7110_CLK_AXI_CTRL_REG_ADDR, CLK_U0_DDR_SFT7110_CLK_AXI_EN_SHIFT, CLK_U0_DDR_SFT7110_CLK_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DDR_SFT7110_CLK_AXI_(x) 		saif_set_reg(CLK_U0_DDR_SFT7110_CLK_AXI_CTRL_REG_ADDR, x, CLK_U0_DDR_SFT7110_CLK_AXI_EN_SHIFT, CLK_U0_DDR_SFT7110_CLK_AXI_EN_MASK)
#define _DIVIDE_CLOCK_CLK_GPU_CORE_(div) 			saif_set_reg(CLK_GPU_CORE_CTRL_REG_ADDR, div, CLK_GPU_CORE_DIV_SHIFT, CLK_GPU_CORE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GPU_CORE_ 		saif_get_reg(CLK_GPU_CORE_CTRL_REG_ADDR, CLK_GPU_CORE_DIV_SHIFT, CLK_GPU_CORE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_IMG_GPU_CORE_CLK_ 			saif_set_reg(CLK_U0_IMG_GPU_CORE_CLK_CTRL_REG_ADDR, CLK_U0_IMG_GPU_CORE_CLK_ENABLE_DATA, CLK_U0_IMG_GPU_CORE_CLK_EN_SHIFT, CLK_U0_IMG_GPU_CORE_CLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_IMG_GPU_CORE_CLK_ 			saif_set_reg(CLK_U0_IMG_GPU_CORE_CLK_CTRL_REG_ADDR, CLK_U0_IMG_GPU_CORE_CLK_DISABLE_DATA, CLK_U0_IMG_GPU_CORE_CLK_EN_SHIFT, CLK_U0_IMG_GPU_CORE_CLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_IMG_GPU_CORE_CLK_ 		saif_get_reg(CLK_U0_IMG_GPU_CORE_CLK_CTRL_REG_ADDR, CLK_U0_IMG_GPU_CORE_CLK_EN_SHIFT, CLK_U0_IMG_GPU_CORE_CLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_IMG_GPU_CORE_CLK_(x) 		saif_set_reg(CLK_U0_IMG_GPU_CORE_CLK_CTRL_REG_ADDR, x, CLK_U0_IMG_GPU_CORE_CLK_EN_SHIFT, CLK_U0_IMG_GPU_CORE_CLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_IMG_GPU_SYS_CLK_ 			saif_set_reg(CLK_U0_IMG_GPU_SYS_CLK_CTRL_REG_ADDR, CLK_U0_IMG_GPU_SYS_CLK_ENABLE_DATA, CLK_U0_IMG_GPU_SYS_CLK_EN_SHIFT, CLK_U0_IMG_GPU_SYS_CLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_IMG_GPU_SYS_CLK_ 			saif_set_reg(CLK_U0_IMG_GPU_SYS_CLK_CTRL_REG_ADDR, CLK_U0_IMG_GPU_SYS_CLK_DISABLE_DATA, CLK_U0_IMG_GPU_SYS_CLK_EN_SHIFT, CLK_U0_IMG_GPU_SYS_CLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_IMG_GPU_SYS_CLK_ 		saif_get_reg(CLK_U0_IMG_GPU_SYS_CLK_CTRL_REG_ADDR, CLK_U0_IMG_GPU_SYS_CLK_EN_SHIFT, CLK_U0_IMG_GPU_SYS_CLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_IMG_GPU_SYS_CLK_(x) 		saif_set_reg(CLK_U0_IMG_GPU_SYS_CLK_CTRL_REG_ADDR, x, CLK_U0_IMG_GPU_SYS_CLK_EN_SHIFT, CLK_U0_IMG_GPU_SYS_CLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_IMG_GPU_CLK_APB_ 			saif_set_reg(CLK_U0_IMG_GPU_CLK_APB_CTRL_REG_ADDR, CLK_U0_IMG_GPU_CLK_APB_ENABLE_DATA, CLK_U0_IMG_GPU_CLK_APB_EN_SHIFT, CLK_U0_IMG_GPU_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_IMG_GPU_CLK_APB_ 			saif_set_reg(CLK_U0_IMG_GPU_CLK_APB_CTRL_REG_ADDR, CLK_U0_IMG_GPU_CLK_APB_DISABLE_DATA, CLK_U0_IMG_GPU_CLK_APB_EN_SHIFT, CLK_U0_IMG_GPU_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_IMG_GPU_CLK_APB_ 		saif_get_reg(CLK_U0_IMG_GPU_CLK_APB_CTRL_REG_ADDR, CLK_U0_IMG_GPU_CLK_APB_EN_SHIFT, CLK_U0_IMG_GPU_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_IMG_GPU_CLK_APB_(x) 		saif_set_reg(CLK_U0_IMG_GPU_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_IMG_GPU_CLK_APB_EN_SHIFT, CLK_U0_IMG_GPU_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_IMG_GPU_RTC_TOGGLE_ 			saif_set_reg(CLK_U0_IMG_GPU_RTC_TOGGLE_CTRL_REG_ADDR, CLK_U0_IMG_GPU_RTC_TOGGLE_ENABLE_DATA, CLK_U0_IMG_GPU_RTC_TOGGLE_EN_SHIFT, CLK_U0_IMG_GPU_RTC_TOGGLE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_IMG_GPU_RTC_TOGGLE_ 			saif_set_reg(CLK_U0_IMG_GPU_RTC_TOGGLE_CTRL_REG_ADDR, CLK_U0_IMG_GPU_RTC_TOGGLE_DISABLE_DATA, CLK_U0_IMG_GPU_RTC_TOGGLE_EN_SHIFT, CLK_U0_IMG_GPU_RTC_TOGGLE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_IMG_GPU_RTC_TOGGLE_ 		saif_get_reg(CLK_U0_IMG_GPU_RTC_TOGGLE_CTRL_REG_ADDR, CLK_U0_IMG_GPU_RTC_TOGGLE_EN_SHIFT, CLK_U0_IMG_GPU_RTC_TOGGLE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_IMG_GPU_RTC_TOGGLE_(x) 		saif_set_reg(CLK_U0_IMG_GPU_RTC_TOGGLE_CTRL_REG_ADDR, x, CLK_U0_IMG_GPU_RTC_TOGGLE_EN_SHIFT, CLK_U0_IMG_GPU_RTC_TOGGLE_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_IMG_GPU_RTC_TOGGLE_(div) 			saif_set_reg(CLK_U0_IMG_GPU_RTC_TOGGLE_CTRL_REG_ADDR, div, CLK_U0_IMG_GPU_RTC_TOGGLE_DIV_SHIFT, CLK_U0_IMG_GPU_RTC_TOGGLE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_IMG_GPU_RTC_TOGGLE_ 		saif_get_reg(CLK_U0_IMG_GPU_RTC_TOGGLE_CTRL_REG_ADDR, CLK_U0_IMG_GPU_RTC_TOGGLE_DIV_SHIFT, CLK_U0_IMG_GPU_RTC_TOGGLE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_ENABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_DISABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_ 		saif_get_reg(CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_(x) 		saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_CTRL_REG_ADDR, x, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_GPU_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_ 			saif_set_reg(CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_CTRL_REG_ADDR, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_ENABLE_DATA, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_SHIFT, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_ 			saif_set_reg(CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_CTRL_REG_ADDR, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_DISABLE_DATA, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_SHIFT, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_ 		saif_get_reg(CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_CTRL_REG_ADDR, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_SHIFT, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_(x) 		saif_set_reg(CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_CTRL_REG_ADDR, x, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_SHIFT, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISPCORE_2X_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_ 			saif_set_reg(CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_CTRL_REG_ADDR, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_ENABLE_DATA, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_SHIFT, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_ 			saif_set_reg(CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_CTRL_REG_ADDR, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_DISABLE_DATA, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_SHIFT, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_ 		saif_get_reg(CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_CTRL_REG_ADDR, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_SHIFT, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_(x) 		saif_set_reg(CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_CTRL_REG_ADDR, x, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_SHIFT, CLK_U0_DOM_ISP_TOP_CLK_DOM_ISP_TOP_CLK_ISP_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_ENABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_DISABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_ 		saif_get_reg(CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_(x) 		saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_CTRL_REG_ADDR, x, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_ISP_AXI_EN_MASK)
#define _DIVIDE_CLOCK_CLK_HIFI4_CORE_(div) 			saif_set_reg(CLK_HIFI4_CORE_CTRL_REG_ADDR, div, CLK_HIFI4_CORE_DIV_SHIFT, CLK_HIFI4_CORE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_HIFI4_CORE_ 		saif_get_reg(CLK_HIFI4_CORE_CTRL_REG_ADDR, CLK_HIFI4_CORE_DIV_SHIFT, CLK_HIFI4_CORE_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_HIFI4_AXI_(div) 			saif_set_reg(CLK_HIFI4_AXI_CTRL_REG_ADDR, div, CLK_HIFI4_AXI_DIV_SHIFT, CLK_HIFI4_AXI_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_HIFI4_AXI_ 		saif_get_reg(CLK_HIFI4_AXI_CTRL_REG_ADDR, CLK_HIFI4_AXI_DIV_SHIFT, CLK_HIFI4_AXI_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_AXI_CFG1_DEC_CLK_MAIN_ 			saif_set_reg(CLK_U0_AXI_CFG1_DEC_CLK_MAIN_CTRL_REG_ADDR, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_ENABLE_DATA, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_SHIFT, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_AXI_CFG1_DEC_CLK_MAIN_ 			saif_set_reg(CLK_U0_AXI_CFG1_DEC_CLK_MAIN_CTRL_REG_ADDR, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_DISABLE_DATA, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_SHIFT, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG1_DEC_CLK_MAIN_ 		saif_get_reg(CLK_U0_AXI_CFG1_DEC_CLK_MAIN_CTRL_REG_ADDR, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_SHIFT, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG1_DEC_CLK_MAIN_(x) 		saif_set_reg(CLK_U0_AXI_CFG1_DEC_CLK_MAIN_CTRL_REG_ADDR, x, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_SHIFT, CLK_U0_AXI_CFG1_DEC_CLK_MAIN_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_AXI_CFG1_DEC_CLK_AHB_ 			saif_set_reg(CLK_U0_AXI_CFG1_DEC_CLK_AHB_CTRL_REG_ADDR, CLK_U0_AXI_CFG1_DEC_CLK_AHB_ENABLE_DATA, CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_SHIFT, CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_AXI_CFG1_DEC_CLK_AHB_ 			saif_set_reg(CLK_U0_AXI_CFG1_DEC_CLK_AHB_CTRL_REG_ADDR, CLK_U0_AXI_CFG1_DEC_CLK_AHB_DISABLE_DATA, CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_SHIFT, CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG1_DEC_CLK_AHB_ 		saif_get_reg(CLK_U0_AXI_CFG1_DEC_CLK_AHB_CTRL_REG_ADDR, CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_SHIFT, CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG1_DEC_CLK_AHB_(x) 		saif_set_reg(CLK_U0_AXI_CFG1_DEC_CLK_AHB_CTRL_REG_ADDR, x, CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_SHIFT, CLK_U0_AXI_CFG1_DEC_CLK_AHB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_ 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_ENABLE_DATA, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_ 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_DISABLE_DATA, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_ 		saif_get_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_(x) 		saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_CTRL_REG_ADDR, x, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_SRC_EN_MASK)
#define _DIVIDE_CLOCK_CLK_VOUT_AXI_(div) 			saif_set_reg(CLK_VOUT_AXI_CTRL_REG_ADDR, div, CLK_VOUT_AXI_DIV_SHIFT, CLK_VOUT_AXI_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_VOUT_AXI_ 		saif_get_reg(CLK_VOUT_AXI_CTRL_REG_ADDR, CLK_VOUT_AXI_DIV_SHIFT, CLK_VOUT_AXI_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_ENABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_DISABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_ 		saif_get_reg(CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_(x) 		saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_CTRL_REG_ADDR, x, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_DISP_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_ 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_ENABLE_DATA, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_ 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_DISABLE_DATA, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_ 		saif_get_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_(x) 		saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_CTRL_REG_ADDR, x, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AHB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_ 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_ENABLE_DATA, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_ 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_DISABLE_DATA, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_ 		saif_get_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_(x) 		saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_CTRL_REG_ADDR, x, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_VOUT_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_ 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_ENABLE_DATA, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_ 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_DISABLE_DATA, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_ 		saif_get_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_(x) 		saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_CTRL_REG_ADDR, x, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_HDMITX0_MCLK_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_(div) 			saif_set_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_CTRL_REG_ADDR, div, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_DIV_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_ 		saif_get_reg(CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_CTRL_REG_ADDR, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_DIV_SHIFT, CLK_U0_DOM_VOUT_TOP_CLK_DOM_VOUT_TOP_CLK_MIPIPHY_REF_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_JPEGC_AXI_(div) 			saif_set_reg(CLK_JPEGC_AXI_CTRL_REG_ADDR, div, CLK_JPEGC_AXI_DIV_SHIFT, CLK_JPEGC_AXI_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_JPEGC_AXI_ 		saif_get_reg(CLK_JPEGC_AXI_CTRL_REG_ADDR, CLK_JPEGC_AXI_DIV_SHIFT, CLK_JPEGC_AXI_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_CODAJ12_CLK_AXI_ 			saif_set_reg(CLK_U0_CODAJ12_CLK_AXI_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_AXI_ENABLE_DATA, CLK_U0_CODAJ12_CLK_AXI_EN_SHIFT, CLK_U0_CODAJ12_CLK_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CODAJ12_CLK_AXI_ 			saif_set_reg(CLK_U0_CODAJ12_CLK_AXI_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_AXI_DISABLE_DATA, CLK_U0_CODAJ12_CLK_AXI_EN_SHIFT, CLK_U0_CODAJ12_CLK_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CODAJ12_CLK_AXI_ 		saif_get_reg(CLK_U0_CODAJ12_CLK_AXI_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_AXI_EN_SHIFT, CLK_U0_CODAJ12_CLK_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CODAJ12_CLK_AXI_(x) 		saif_set_reg(CLK_U0_CODAJ12_CLK_AXI_CTRL_REG_ADDR, x, CLK_U0_CODAJ12_CLK_AXI_EN_SHIFT, CLK_U0_CODAJ12_CLK_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_CODAJ12_CLK_CORE_ 			saif_set_reg(CLK_U0_CODAJ12_CLK_CORE_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_CORE_ENABLE_DATA, CLK_U0_CODAJ12_CLK_CORE_EN_SHIFT, CLK_U0_CODAJ12_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CODAJ12_CLK_CORE_ 			saif_set_reg(CLK_U0_CODAJ12_CLK_CORE_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_CORE_DISABLE_DATA, CLK_U0_CODAJ12_CLK_CORE_EN_SHIFT, CLK_U0_CODAJ12_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CODAJ12_CLK_CORE_ 		saif_get_reg(CLK_U0_CODAJ12_CLK_CORE_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_CORE_EN_SHIFT, CLK_U0_CODAJ12_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CODAJ12_CLK_CORE_(x) 		saif_set_reg(CLK_U0_CODAJ12_CLK_CORE_CTRL_REG_ADDR, x, CLK_U0_CODAJ12_CLK_CORE_EN_SHIFT, CLK_U0_CODAJ12_CLK_CORE_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_CODAJ12_CLK_CORE_(div) 			saif_set_reg(CLK_U0_CODAJ12_CLK_CORE_CTRL_REG_ADDR, div, CLK_U0_CODAJ12_CLK_CORE_DIV_SHIFT, CLK_U0_CODAJ12_CLK_CORE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_CODAJ12_CLK_CORE_ 		saif_get_reg(CLK_U0_CODAJ12_CLK_CORE_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_CORE_DIV_SHIFT, CLK_U0_CODAJ12_CLK_CORE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_CODAJ12_CLK_APB_ 			saif_set_reg(CLK_U0_CODAJ12_CLK_APB_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_APB_ENABLE_DATA, CLK_U0_CODAJ12_CLK_APB_EN_SHIFT, CLK_U0_CODAJ12_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CODAJ12_CLK_APB_ 			saif_set_reg(CLK_U0_CODAJ12_CLK_APB_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_APB_DISABLE_DATA, CLK_U0_CODAJ12_CLK_APB_EN_SHIFT, CLK_U0_CODAJ12_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CODAJ12_CLK_APB_ 		saif_get_reg(CLK_U0_CODAJ12_CLK_APB_CTRL_REG_ADDR, CLK_U0_CODAJ12_CLK_APB_EN_SHIFT, CLK_U0_CODAJ12_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CODAJ12_CLK_APB_(x) 		saif_set_reg(CLK_U0_CODAJ12_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_CODAJ12_CLK_APB_EN_SHIFT, CLK_U0_CODAJ12_CLK_APB_EN_MASK)
#define _DIVIDE_CLOCK_CLK_VDEC_AXI_(div) 			saif_set_reg(CLK_VDEC_AXI_CTRL_REG_ADDR, div, CLK_VDEC_AXI_DIV_SHIFT, CLK_VDEC_AXI_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_VDEC_AXI_ 		saif_get_reg(CLK_VDEC_AXI_CTRL_REG_ADDR, CLK_VDEC_AXI_DIV_SHIFT, CLK_VDEC_AXI_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_WAVE511_CLK_AXI_ 			saif_set_reg(CLK_U0_WAVE511_CLK_AXI_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_AXI_ENABLE_DATA, CLK_U0_WAVE511_CLK_AXI_EN_SHIFT, CLK_U0_WAVE511_CLK_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_WAVE511_CLK_AXI_ 			saif_set_reg(CLK_U0_WAVE511_CLK_AXI_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_AXI_DISABLE_DATA, CLK_U0_WAVE511_CLK_AXI_EN_SHIFT, CLK_U0_WAVE511_CLK_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE511_CLK_AXI_ 		saif_get_reg(CLK_U0_WAVE511_CLK_AXI_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_AXI_EN_SHIFT, CLK_U0_WAVE511_CLK_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE511_CLK_AXI_(x) 		saif_set_reg(CLK_U0_WAVE511_CLK_AXI_CTRL_REG_ADDR, x, CLK_U0_WAVE511_CLK_AXI_EN_SHIFT, CLK_U0_WAVE511_CLK_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_WAVE511_CLK_BPU_ 			saif_set_reg(CLK_U0_WAVE511_CLK_BPU_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_BPU_ENABLE_DATA, CLK_U0_WAVE511_CLK_BPU_EN_SHIFT, CLK_U0_WAVE511_CLK_BPU_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_WAVE511_CLK_BPU_ 			saif_set_reg(CLK_U0_WAVE511_CLK_BPU_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_BPU_DISABLE_DATA, CLK_U0_WAVE511_CLK_BPU_EN_SHIFT, CLK_U0_WAVE511_CLK_BPU_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE511_CLK_BPU_ 		saif_get_reg(CLK_U0_WAVE511_CLK_BPU_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_BPU_EN_SHIFT, CLK_U0_WAVE511_CLK_BPU_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE511_CLK_BPU_(x) 		saif_set_reg(CLK_U0_WAVE511_CLK_BPU_CTRL_REG_ADDR, x, CLK_U0_WAVE511_CLK_BPU_EN_SHIFT, CLK_U0_WAVE511_CLK_BPU_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_WAVE511_CLK_BPU_(div) 			saif_set_reg(CLK_U0_WAVE511_CLK_BPU_CTRL_REG_ADDR, div, CLK_U0_WAVE511_CLK_BPU_DIV_SHIFT, CLK_U0_WAVE511_CLK_BPU_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_WAVE511_CLK_BPU_ 		saif_get_reg(CLK_U0_WAVE511_CLK_BPU_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_BPU_DIV_SHIFT, CLK_U0_WAVE511_CLK_BPU_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_WAVE511_CLK_VCE_ 			saif_set_reg(CLK_U0_WAVE511_CLK_VCE_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_VCE_ENABLE_DATA, CLK_U0_WAVE511_CLK_VCE_EN_SHIFT, CLK_U0_WAVE511_CLK_VCE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_WAVE511_CLK_VCE_ 			saif_set_reg(CLK_U0_WAVE511_CLK_VCE_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_VCE_DISABLE_DATA, CLK_U0_WAVE511_CLK_VCE_EN_SHIFT, CLK_U0_WAVE511_CLK_VCE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE511_CLK_VCE_ 		saif_get_reg(CLK_U0_WAVE511_CLK_VCE_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_VCE_EN_SHIFT, CLK_U0_WAVE511_CLK_VCE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE511_CLK_VCE_(x) 		saif_set_reg(CLK_U0_WAVE511_CLK_VCE_CTRL_REG_ADDR, x, CLK_U0_WAVE511_CLK_VCE_EN_SHIFT, CLK_U0_WAVE511_CLK_VCE_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_WAVE511_CLK_VCE_(div) 			saif_set_reg(CLK_U0_WAVE511_CLK_VCE_CTRL_REG_ADDR, div, CLK_U0_WAVE511_CLK_VCE_DIV_SHIFT, CLK_U0_WAVE511_CLK_VCE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_WAVE511_CLK_VCE_ 		saif_get_reg(CLK_U0_WAVE511_CLK_VCE_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_VCE_DIV_SHIFT, CLK_U0_WAVE511_CLK_VCE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_WAVE511_CLK_APB_ 			saif_set_reg(CLK_U0_WAVE511_CLK_APB_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_APB_ENABLE_DATA, CLK_U0_WAVE511_CLK_APB_EN_SHIFT, CLK_U0_WAVE511_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_WAVE511_CLK_APB_ 			saif_set_reg(CLK_U0_WAVE511_CLK_APB_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_APB_DISABLE_DATA, CLK_U0_WAVE511_CLK_APB_EN_SHIFT, CLK_U0_WAVE511_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE511_CLK_APB_ 		saif_get_reg(CLK_U0_WAVE511_CLK_APB_CTRL_REG_ADDR, CLK_U0_WAVE511_CLK_APB_EN_SHIFT, CLK_U0_WAVE511_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE511_CLK_APB_(x) 		saif_set_reg(CLK_U0_WAVE511_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_WAVE511_CLK_APB_EN_SHIFT, CLK_U0_WAVE511_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_VDEC_JPG_ARB_JPGCLK_ 			saif_set_reg(CLK_U0_VDEC_JPG_ARB_JPGCLK_CTRL_REG_ADDR, CLK_U0_VDEC_JPG_ARB_JPGCLK_ENABLE_DATA, CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_SHIFT, CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_VDEC_JPG_ARB_JPGCLK_ 			saif_set_reg(CLK_U0_VDEC_JPG_ARB_JPGCLK_CTRL_REG_ADDR, CLK_U0_VDEC_JPG_ARB_JPGCLK_DISABLE_DATA, CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_SHIFT, CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_VDEC_JPG_ARB_JPGCLK_ 		saif_get_reg(CLK_U0_VDEC_JPG_ARB_JPGCLK_CTRL_REG_ADDR, CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_SHIFT, CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_VDEC_JPG_ARB_JPGCLK_(x) 		saif_set_reg(CLK_U0_VDEC_JPG_ARB_JPGCLK_CTRL_REG_ADDR, x, CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_SHIFT, CLK_U0_VDEC_JPG_ARB_JPGCLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_VDEC_JPG_ARB_MAINCLK_ 			saif_set_reg(CLK_U0_VDEC_JPG_ARB_MAINCLK_CTRL_REG_ADDR, CLK_U0_VDEC_JPG_ARB_MAINCLK_ENABLE_DATA, CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_SHIFT, CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_VDEC_JPG_ARB_MAINCLK_ 			saif_set_reg(CLK_U0_VDEC_JPG_ARB_MAINCLK_CTRL_REG_ADDR, CLK_U0_VDEC_JPG_ARB_MAINCLK_DISABLE_DATA, CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_SHIFT, CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_VDEC_JPG_ARB_MAINCLK_ 		saif_get_reg(CLK_U0_VDEC_JPG_ARB_MAINCLK_CTRL_REG_ADDR, CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_SHIFT, CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_VDEC_JPG_ARB_MAINCLK_(x) 		saif_set_reg(CLK_U0_VDEC_JPG_ARB_MAINCLK_CTRL_REG_ADDR, x, CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_SHIFT, CLK_U0_VDEC_JPG_ARB_MAINCLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_ENABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_DISABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_ 		saif_get_reg(CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_(x) 		saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_CTRL_REG_ADDR, x, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_VDEC_AXI_EN_MASK)
#define _DIVIDE_CLOCK_CLK_VENC_AXI_(div) 			saif_set_reg(CLK_VENC_AXI_CTRL_REG_ADDR, div, CLK_VENC_AXI_DIV_SHIFT, CLK_VENC_AXI_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_VENC_AXI_ 		saif_get_reg(CLK_VENC_AXI_CTRL_REG_ADDR, CLK_VENC_AXI_DIV_SHIFT, CLK_VENC_AXI_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_WAVE420L_CLK_AXI_ 			saif_set_reg(CLK_U0_WAVE420L_CLK_AXI_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_AXI_ENABLE_DATA, CLK_U0_WAVE420L_CLK_AXI_EN_SHIFT, CLK_U0_WAVE420L_CLK_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_WAVE420L_CLK_AXI_ 			saif_set_reg(CLK_U0_WAVE420L_CLK_AXI_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_AXI_DISABLE_DATA, CLK_U0_WAVE420L_CLK_AXI_EN_SHIFT, CLK_U0_WAVE420L_CLK_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE420L_CLK_AXI_ 		saif_get_reg(CLK_U0_WAVE420L_CLK_AXI_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_AXI_EN_SHIFT, CLK_U0_WAVE420L_CLK_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE420L_CLK_AXI_(x) 		saif_set_reg(CLK_U0_WAVE420L_CLK_AXI_CTRL_REG_ADDR, x, CLK_U0_WAVE420L_CLK_AXI_EN_SHIFT, CLK_U0_WAVE420L_CLK_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_WAVE420L_CLK_BPU_ 			saif_set_reg(CLK_U0_WAVE420L_CLK_BPU_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_BPU_ENABLE_DATA, CLK_U0_WAVE420L_CLK_BPU_EN_SHIFT, CLK_U0_WAVE420L_CLK_BPU_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_WAVE420L_CLK_BPU_ 			saif_set_reg(CLK_U0_WAVE420L_CLK_BPU_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_BPU_DISABLE_DATA, CLK_U0_WAVE420L_CLK_BPU_EN_SHIFT, CLK_U0_WAVE420L_CLK_BPU_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE420L_CLK_BPU_ 		saif_get_reg(CLK_U0_WAVE420L_CLK_BPU_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_BPU_EN_SHIFT, CLK_U0_WAVE420L_CLK_BPU_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE420L_CLK_BPU_(x) 		saif_set_reg(CLK_U0_WAVE420L_CLK_BPU_CTRL_REG_ADDR, x, CLK_U0_WAVE420L_CLK_BPU_EN_SHIFT, CLK_U0_WAVE420L_CLK_BPU_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_WAVE420L_CLK_BPU_(div) 			saif_set_reg(CLK_U0_WAVE420L_CLK_BPU_CTRL_REG_ADDR, div, CLK_U0_WAVE420L_CLK_BPU_DIV_SHIFT, CLK_U0_WAVE420L_CLK_BPU_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_WAVE420L_CLK_BPU_ 		saif_get_reg(CLK_U0_WAVE420L_CLK_BPU_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_BPU_DIV_SHIFT, CLK_U0_WAVE420L_CLK_BPU_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_WAVE420L_CLK_VCE_ 			saif_set_reg(CLK_U0_WAVE420L_CLK_VCE_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_VCE_ENABLE_DATA, CLK_U0_WAVE420L_CLK_VCE_EN_SHIFT, CLK_U0_WAVE420L_CLK_VCE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_WAVE420L_CLK_VCE_ 			saif_set_reg(CLK_U0_WAVE420L_CLK_VCE_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_VCE_DISABLE_DATA, CLK_U0_WAVE420L_CLK_VCE_EN_SHIFT, CLK_U0_WAVE420L_CLK_VCE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE420L_CLK_VCE_ 		saif_get_reg(CLK_U0_WAVE420L_CLK_VCE_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_VCE_EN_SHIFT, CLK_U0_WAVE420L_CLK_VCE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE420L_CLK_VCE_(x) 		saif_set_reg(CLK_U0_WAVE420L_CLK_VCE_CTRL_REG_ADDR, x, CLK_U0_WAVE420L_CLK_VCE_EN_SHIFT, CLK_U0_WAVE420L_CLK_VCE_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_WAVE420L_CLK_VCE_(div) 			saif_set_reg(CLK_U0_WAVE420L_CLK_VCE_CTRL_REG_ADDR, div, CLK_U0_WAVE420L_CLK_VCE_DIV_SHIFT, CLK_U0_WAVE420L_CLK_VCE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_WAVE420L_CLK_VCE_ 		saif_get_reg(CLK_U0_WAVE420L_CLK_VCE_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_VCE_DIV_SHIFT, CLK_U0_WAVE420L_CLK_VCE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_WAVE420L_CLK_APB_ 			saif_set_reg(CLK_U0_WAVE420L_CLK_APB_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_APB_ENABLE_DATA, CLK_U0_WAVE420L_CLK_APB_EN_SHIFT, CLK_U0_WAVE420L_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_WAVE420L_CLK_APB_ 			saif_set_reg(CLK_U0_WAVE420L_CLK_APB_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_APB_DISABLE_DATA, CLK_U0_WAVE420L_CLK_APB_EN_SHIFT, CLK_U0_WAVE420L_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE420L_CLK_APB_ 		saif_get_reg(CLK_U0_WAVE420L_CLK_APB_CTRL_REG_ADDR, CLK_U0_WAVE420L_CLK_APB_EN_SHIFT, CLK_U0_WAVE420L_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_WAVE420L_CLK_APB_(x) 		saif_set_reg(CLK_U0_WAVE420L_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_WAVE420L_CLK_APB_EN_SHIFT, CLK_U0_WAVE420L_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_ENABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_DISABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_ 		saif_get_reg(CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_(x) 		saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_CTRL_REG_ADDR, x, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_VENC_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_ 			saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_ENABLE_DATA, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_ 			saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_DISABLE_DATA, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_ 		saif_get_reg(CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_(x) 		saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_CTRL_REG_ADDR, x, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DIV_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_AXI_CFG0_DEC_CLK_MAIN_ 			saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_MAIN_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_ENABLE_DATA, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_AXI_CFG0_DEC_CLK_MAIN_ 			saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_MAIN_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_DISABLE_DATA, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG0_DEC_CLK_MAIN_ 		saif_get_reg(CLK_U0_AXI_CFG0_DEC_CLK_MAIN_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG0_DEC_CLK_MAIN_(x) 		saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_MAIN_CTRL_REG_ADDR, x, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_MAIN_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_ 			saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_ENABLE_DATA, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_ 			saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_DISABLE_DATA, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_ 		saif_get_reg(CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_CTRL_REG_ADDR, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_(x) 		saif_set_reg(CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_CTRL_REG_ADDR, x, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_SHIFT, CLK_U0_AXI_CFG0_DEC_CLK_HIFI4_EN_MASK)
#define _ENABLE_CLOCK_CLK_U2_AXIMEM_128B_CLK_AXI_ 			saif_set_reg(CLK_U2_AXIMEM_128B_CLK_AXI_CTRL_REG_ADDR, CLK_U2_AXIMEM_128B_CLK_AXI_ENABLE_DATA, CLK_U2_AXIMEM_128B_CLK_AXI_EN_SHIFT, CLK_U2_AXIMEM_128B_CLK_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U2_AXIMEM_128B_CLK_AXI_ 			saif_set_reg(CLK_U2_AXIMEM_128B_CLK_AXI_CTRL_REG_ADDR, CLK_U2_AXIMEM_128B_CLK_AXI_DISABLE_DATA, CLK_U2_AXIMEM_128B_CLK_AXI_EN_SHIFT, CLK_U2_AXIMEM_128B_CLK_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U2_AXIMEM_128B_CLK_AXI_ 		saif_get_reg(CLK_U2_AXIMEM_128B_CLK_AXI_CTRL_REG_ADDR, CLK_U2_AXIMEM_128B_CLK_AXI_EN_SHIFT, CLK_U2_AXIMEM_128B_CLK_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U2_AXIMEM_128B_CLK_AXI_(x) 		saif_set_reg(CLK_U2_AXIMEM_128B_CLK_AXI_CTRL_REG_ADDR, x, CLK_U2_AXIMEM_128B_CLK_AXI_EN_SHIFT, CLK_U2_AXIMEM_128B_CLK_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_QSPI_CLK_AHB_ 			saif_set_reg(CLK_U0_CDNS_QSPI_CLK_AHB_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_AHB_ENABLE_DATA, CLK_U0_CDNS_QSPI_CLK_AHB_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_AHB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_QSPI_CLK_AHB_ 			saif_set_reg(CLK_U0_CDNS_QSPI_CLK_AHB_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_AHB_DISABLE_DATA, CLK_U0_CDNS_QSPI_CLK_AHB_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_AHB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_QSPI_CLK_AHB_ 		saif_get_reg(CLK_U0_CDNS_QSPI_CLK_AHB_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_AHB_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_AHB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_QSPI_CLK_AHB_(x) 		saif_set_reg(CLK_U0_CDNS_QSPI_CLK_AHB_CTRL_REG_ADDR, x, CLK_U0_CDNS_QSPI_CLK_AHB_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_AHB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_QSPI_CLK_APB_ 			saif_set_reg(CLK_U0_CDNS_QSPI_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_APB_ENABLE_DATA, CLK_U0_CDNS_QSPI_CLK_APB_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_QSPI_CLK_APB_ 			saif_set_reg(CLK_U0_CDNS_QSPI_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_APB_DISABLE_DATA, CLK_U0_CDNS_QSPI_CLK_APB_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_QSPI_CLK_APB_ 		saif_get_reg(CLK_U0_CDNS_QSPI_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_APB_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_QSPI_CLK_APB_(x) 		saif_set_reg(CLK_U0_CDNS_QSPI_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_CDNS_QSPI_CLK_APB_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_APB_EN_MASK)
#define _DIVIDE_CLOCK_CLK_QSPI_REF_SRC_(div) 			saif_set_reg(CLK_QSPI_REF_SRC_CTRL_REG_ADDR, div, CLK_QSPI_REF_SRC_DIV_SHIFT, CLK_QSPI_REF_SRC_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_QSPI_REF_SRC_ 		saif_get_reg(CLK_QSPI_REF_SRC_CTRL_REG_ADDR, CLK_QSPI_REF_SRC_DIV_SHIFT, CLK_QSPI_REF_SRC_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_QSPI_CLK_REF_ 			saif_set_reg(CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_REF_ENABLE_DATA, CLK_U0_CDNS_QSPI_CLK_REF_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_REF_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_QSPI_CLK_REF_ 			saif_set_reg(CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_REF_DISABLE_DATA, CLK_U0_CDNS_QSPI_CLK_REF_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_REF_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_QSPI_CLK_REF_ 		saif_get_reg(CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_REF_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_REF_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_QSPI_CLK_REF_(x) 		saif_set_reg(CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR, x, CLK_U0_CDNS_QSPI_CLK_REF_EN_SHIFT, CLK_U0_CDNS_QSPI_CLK_REF_EN_MASK)
#define _SWITCH_CLOCK_CLK_U0_CDNS_QSPI_CLK_REF_SOURCE_CLK_OSC_ 	saif_set_reg(CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_REF_SW_CLK_OSC_DATA, CLK_U0_CDNS_QSPI_CLK_REF_SW_SHIFT, CLK_U0_CDNS_QSPI_CLK_REF_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_CDNS_QSPI_CLK_REF_SOURCE_CLK_QSPI_REF_SRC_ 	saif_set_reg(CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_REF_SW_CLK_QSPI_REF_SRC_DATA, CLK_U0_CDNS_QSPI_CLK_REF_SW_SHIFT, CLK_U0_CDNS_QSPI_CLK_REF_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_CDNS_QSPI_CLK_REF_ 		saif_get_reg(CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR, CLK_U0_CDNS_QSPI_CLK_REF_SW_SHIFT, CLK_U0_CDNS_QSPI_CLK_REF_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_CDNS_QSPI_CLK_REF_(x) 		saif_set_reg(CLK_U0_CDNS_QSPI_CLK_REF_CTRL_REG_ADDR, x, CLK_U0_CDNS_QSPI_CLK_REF_SW_SHIFT, CLK_U0_CDNS_QSPI_CLK_REF_SW_MASK)
#define _ENABLE_CLOCK_CLK_U0_DW_SDIO_CLK_AHB_ 			saif_set_reg(CLK_U0_DW_SDIO_CLK_AHB_CTRL_REG_ADDR, CLK_U0_DW_SDIO_CLK_AHB_ENABLE_DATA, CLK_U0_DW_SDIO_CLK_AHB_EN_SHIFT, CLK_U0_DW_SDIO_CLK_AHB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DW_SDIO_CLK_AHB_ 			saif_set_reg(CLK_U0_DW_SDIO_CLK_AHB_CTRL_REG_ADDR, CLK_U0_DW_SDIO_CLK_AHB_DISABLE_DATA, CLK_U0_DW_SDIO_CLK_AHB_EN_SHIFT, CLK_U0_DW_SDIO_CLK_AHB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DW_SDIO_CLK_AHB_ 		saif_get_reg(CLK_U0_DW_SDIO_CLK_AHB_CTRL_REG_ADDR, CLK_U0_DW_SDIO_CLK_AHB_EN_SHIFT, CLK_U0_DW_SDIO_CLK_AHB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DW_SDIO_CLK_AHB_(x) 		saif_set_reg(CLK_U0_DW_SDIO_CLK_AHB_CTRL_REG_ADDR, x, CLK_U0_DW_SDIO_CLK_AHB_EN_SHIFT, CLK_U0_DW_SDIO_CLK_AHB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_SDIO_CLK_AHB_ 			saif_set_reg(CLK_U1_DW_SDIO_CLK_AHB_CTRL_REG_ADDR, CLK_U1_DW_SDIO_CLK_AHB_ENABLE_DATA, CLK_U1_DW_SDIO_CLK_AHB_EN_SHIFT, CLK_U1_DW_SDIO_CLK_AHB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_SDIO_CLK_AHB_ 			saif_set_reg(CLK_U1_DW_SDIO_CLK_AHB_CTRL_REG_ADDR, CLK_U1_DW_SDIO_CLK_AHB_DISABLE_DATA, CLK_U1_DW_SDIO_CLK_AHB_EN_SHIFT, CLK_U1_DW_SDIO_CLK_AHB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_SDIO_CLK_AHB_ 		saif_get_reg(CLK_U1_DW_SDIO_CLK_AHB_CTRL_REG_ADDR, CLK_U1_DW_SDIO_CLK_AHB_EN_SHIFT, CLK_U1_DW_SDIO_CLK_AHB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_SDIO_CLK_AHB_(x) 		saif_set_reg(CLK_U1_DW_SDIO_CLK_AHB_CTRL_REG_ADDR, x, CLK_U1_DW_SDIO_CLK_AHB_EN_SHIFT, CLK_U1_DW_SDIO_CLK_AHB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DW_SDIO_CLK_SDCARD_ 			saif_set_reg(CLK_U0_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, CLK_U0_DW_SDIO_CLK_SDCARD_ENABLE_DATA, CLK_U0_DW_SDIO_CLK_SDCARD_EN_SHIFT, CLK_U0_DW_SDIO_CLK_SDCARD_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DW_SDIO_CLK_SDCARD_ 			saif_set_reg(CLK_U0_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, CLK_U0_DW_SDIO_CLK_SDCARD_DISABLE_DATA, CLK_U0_DW_SDIO_CLK_SDCARD_EN_SHIFT, CLK_U0_DW_SDIO_CLK_SDCARD_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DW_SDIO_CLK_SDCARD_ 		saif_get_reg(CLK_U0_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, CLK_U0_DW_SDIO_CLK_SDCARD_EN_SHIFT, CLK_U0_DW_SDIO_CLK_SDCARD_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DW_SDIO_CLK_SDCARD_(x) 		saif_set_reg(CLK_U0_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, x, CLK_U0_DW_SDIO_CLK_SDCARD_EN_SHIFT, CLK_U0_DW_SDIO_CLK_SDCARD_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_DW_SDIO_CLK_SDCARD_(div) 			saif_set_reg(CLK_U0_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, div, CLK_U0_DW_SDIO_CLK_SDCARD_DIV_SHIFT, CLK_U0_DW_SDIO_CLK_SDCARD_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_DW_SDIO_CLK_SDCARD_ 		saif_get_reg(CLK_U0_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, CLK_U0_DW_SDIO_CLK_SDCARD_DIV_SHIFT, CLK_U0_DW_SDIO_CLK_SDCARD_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_SDIO_CLK_SDCARD_ 			saif_set_reg(CLK_U1_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, CLK_U1_DW_SDIO_CLK_SDCARD_ENABLE_DATA, CLK_U1_DW_SDIO_CLK_SDCARD_EN_SHIFT, CLK_U1_DW_SDIO_CLK_SDCARD_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_SDIO_CLK_SDCARD_ 			saif_set_reg(CLK_U1_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, CLK_U1_DW_SDIO_CLK_SDCARD_DISABLE_DATA, CLK_U1_DW_SDIO_CLK_SDCARD_EN_SHIFT, CLK_U1_DW_SDIO_CLK_SDCARD_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_SDIO_CLK_SDCARD_ 		saif_get_reg(CLK_U1_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, CLK_U1_DW_SDIO_CLK_SDCARD_EN_SHIFT, CLK_U1_DW_SDIO_CLK_SDCARD_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_SDIO_CLK_SDCARD_(x) 		saif_set_reg(CLK_U1_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, x, CLK_U1_DW_SDIO_CLK_SDCARD_EN_SHIFT, CLK_U1_DW_SDIO_CLK_SDCARD_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U1_DW_SDIO_CLK_SDCARD_(div) 			saif_set_reg(CLK_U1_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, div, CLK_U1_DW_SDIO_CLK_SDCARD_DIV_SHIFT, CLK_U1_DW_SDIO_CLK_SDCARD_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U1_DW_SDIO_CLK_SDCARD_ 		saif_get_reg(CLK_U1_DW_SDIO_CLK_SDCARD_CTRL_REG_ADDR, CLK_U1_DW_SDIO_CLK_SDCARD_DIV_SHIFT, CLK_U1_DW_SDIO_CLK_SDCARD_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_USB_125M_(div) 			saif_set_reg(CLK_USB_125M_CTRL_REG_ADDR, div, CLK_USB_125M_DIV_SHIFT, CLK_USB_125M_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_USB_125M_ 		saif_get_reg(CLK_USB_125M_CTRL_REG_ADDR, CLK_USB_125M_DIV_SHIFT, CLK_USB_125M_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_ENABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_ 			saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_DISABLE_DATA, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_ 		saif_get_reg(CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_CTRL_REG_ADDR, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_(x) 		saif_set_reg(CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_CTRL_REG_ADDR, x, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_SHIFT, CLK_U0_SFT7110_NOC_BUS_CLK_STG_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_AHB_ 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_AHB_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_ENABLE_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_AHB_ 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_AHB_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_DISABLE_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_AHB_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_AHB_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_AHB_(x) 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_AHB_CTRL_REG_ADDR, x, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_AHB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_AXI_ 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_AXI_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_ENABLE_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_AXI_ 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_AXI_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_DISABLE_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_AXI_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_AXI_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_AXI_(x) 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_AXI_CTRL_REG_ADDR, x, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_AXI_EN_MASK)
#define _DIVIDE_CLOCK_CLK_GMAC_SRC_(div) 			saif_set_reg(CLK_GMAC_SRC_CTRL_REG_ADDR, div, CLK_GMAC_SRC_DIV_SHIFT, CLK_GMAC_SRC_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GMAC_SRC_ 		saif_get_reg(CLK_GMAC_SRC_CTRL_REG_ADDR, CLK_GMAC_SRC_DIV_SHIFT, CLK_GMAC_SRC_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_GMAC1_GTXCLK_(div) 			saif_set_reg(CLK_GMAC1_GTXCLK_CTRL_REG_ADDR, div, CLK_GMAC1_GTXCLK_DIV_SHIFT, CLK_GMAC1_GTXCLK_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GMAC1_GTXCLK_ 		saif_get_reg(CLK_GMAC1_GTXCLK_CTRL_REG_ADDR, CLK_GMAC1_GTXCLK_DIV_SHIFT, CLK_GMAC1_GTXCLK_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_GMAC1_RMII_RTX_(div) 			saif_set_reg(CLK_GMAC1_RMII_RTX_CTRL_REG_ADDR, div, CLK_GMAC1_RMII_RTX_DIV_SHIFT, CLK_GMAC1_RMII_RTX_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GMAC1_RMII_RTX_ 		saif_get_reg(CLK_GMAC1_RMII_RTX_CTRL_REG_ADDR, CLK_GMAC1_RMII_RTX_DIV_SHIFT, CLK_GMAC1_RMII_RTX_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_PTP_ 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_PTP_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_ENABLE_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_PTP_ 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_PTP_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_DISABLE_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_PTP_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_PTP_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_PTP_(x) 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_PTP_CTRL_REG_ADDR, x, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_PTP_(div) 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_PTP_CTRL_REG_ADDR, div, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_DIV_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_PTP_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_PTP_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_DIV_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_PTP_DIV_MASK)
#define _SWITCH_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_RX_SOURCE_CLK_GMAC1_RGMII_RXIN_ 	saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_CLK_GMAC1_RGMII_RXIN_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_MASK)
#define _SWITCH_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_RX_SOURCE_CLK_GMAC1_RMII_RTX_ 	saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_CLK_GMAC1_RMII_RTX_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_RX_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_RX_(x) 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_CTRL_REG_ADDR, x, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_SW_MASK)
#define _SET_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_RX_(dly) 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_CTRL_REG_ADDR, dly, CLK_U1_DW_GMAC5_AXI64_CLK_RX_DLY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_DLY_MASK)
#define _GET_CLOCK_DELAY_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_RX_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_RX_DLY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_DLY_MASK)
#define _SET_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_ 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_ 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_UN_POLARITY_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_(x) 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_CTRL_REG_ADDR, x, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_RX_INV_POLARITY_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_TX_ 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_ENABLE_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_TX_ 			saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_DISABLE_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_TX_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_TX_(x) 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR, x, CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_EN_MASK)
#define _SWITCH_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_TX_SOURCE_CLK_GMAC1_GTXCLK_ 	saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_CLK_GMAC1_GTXCLK_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_MASK)
#define _SWITCH_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_TX_SOURCE_CLK_GMAC1_RMII_RTX_ 	saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_CLK_GMAC1_RMII_RTX_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_TX_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_TX_(x) 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_CTRL_REG_ADDR, x, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_SW_MASK)
#define _SET_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_ 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_ 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_UN_POLARITY_DATA, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_ 		saif_get_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_CTRL_REG_ADDR, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_(x) 		saif_set_reg(CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_CTRL_REG_ADDR, x, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_SHIFT, CLK_U1_DW_GMAC5_AXI64_CLK_TX_INV_POLARITY_MASK)
#define _ENABLE_CLOCK_CLK_GMAC1_GTXC_ 			saif_set_reg(CLK_GMAC1_GTXC_CTRL_REG_ADDR, CLK_GMAC1_GTXC_ENABLE_DATA, CLK_GMAC1_GTXC_EN_SHIFT, CLK_GMAC1_GTXC_EN_MASK)
#define _DISABLE_CLOCK_CLK_GMAC1_GTXC_ 			saif_set_reg(CLK_GMAC1_GTXC_CTRL_REG_ADDR, CLK_GMAC1_GTXC_DISABLE_DATA, CLK_GMAC1_GTXC_EN_SHIFT, CLK_GMAC1_GTXC_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_GMAC1_GTXC_ 		saif_get_reg(CLK_GMAC1_GTXC_CTRL_REG_ADDR, CLK_GMAC1_GTXC_EN_SHIFT, CLK_GMAC1_GTXC_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_GMAC1_GTXC_(x) 		saif_set_reg(CLK_GMAC1_GTXC_CTRL_REG_ADDR, x, CLK_GMAC1_GTXC_EN_SHIFT, CLK_GMAC1_GTXC_EN_MASK)
#define _SET_CLOCK_CLK_GMAC1_GTXC_(dly) 			saif_set_reg(CLK_GMAC1_GTXC_CTRL_REG_ADDR, dly, CLK_GMAC1_GTXC_DLY_SHIFT, CLK_GMAC1_GTXC_DLY_MASK)
#define _GET_CLOCK_DELAY_STATUS_CLK_GMAC1_GTXC_ 		saif_get_reg(CLK_GMAC1_GTXC_CTRL_REG_ADDR, CLK_GMAC1_GTXC_DLY_SHIFT, CLK_GMAC1_GTXC_DLY_MASK)
#define _ENABLE_CLOCK_CLK_GMAC0_GTXCLK_ 			saif_set_reg(CLK_GMAC0_GTXCLK_CTRL_REG_ADDR, CLK_GMAC0_GTXCLK_ENABLE_DATA, CLK_GMAC0_GTXCLK_EN_SHIFT, CLK_GMAC0_GTXCLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_GMAC0_GTXCLK_ 			saif_set_reg(CLK_GMAC0_GTXCLK_CTRL_REG_ADDR, CLK_GMAC0_GTXCLK_DISABLE_DATA, CLK_GMAC0_GTXCLK_EN_SHIFT, CLK_GMAC0_GTXCLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_GMAC0_GTXCLK_ 		saif_get_reg(CLK_GMAC0_GTXCLK_CTRL_REG_ADDR, CLK_GMAC0_GTXCLK_EN_SHIFT, CLK_GMAC0_GTXCLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_GMAC0_GTXCLK_(x) 		saif_set_reg(CLK_GMAC0_GTXCLK_CTRL_REG_ADDR, x, CLK_GMAC0_GTXCLK_EN_SHIFT, CLK_GMAC0_GTXCLK_EN_MASK)
#define _DIVIDE_CLOCK_CLK_GMAC0_GTXCLK_(div) 			saif_set_reg(CLK_GMAC0_GTXCLK_CTRL_REG_ADDR, div, CLK_GMAC0_GTXCLK_DIV_SHIFT, CLK_GMAC0_GTXCLK_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GMAC0_GTXCLK_ 		saif_get_reg(CLK_GMAC0_GTXCLK_CTRL_REG_ADDR, CLK_GMAC0_GTXCLK_DIV_SHIFT, CLK_GMAC0_GTXCLK_DIV_MASK)
#define _ENABLE_CLOCK_CLK_GMAC0_PTP_ 			saif_set_reg(CLK_GMAC0_PTP_CTRL_REG_ADDR, CLK_GMAC0_PTP_ENABLE_DATA, CLK_GMAC0_PTP_EN_SHIFT, CLK_GMAC0_PTP_EN_MASK)
#define _DISABLE_CLOCK_CLK_GMAC0_PTP_ 			saif_set_reg(CLK_GMAC0_PTP_CTRL_REG_ADDR, CLK_GMAC0_PTP_DISABLE_DATA, CLK_GMAC0_PTP_EN_SHIFT, CLK_GMAC0_PTP_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_GMAC0_PTP_ 		saif_get_reg(CLK_GMAC0_PTP_CTRL_REG_ADDR, CLK_GMAC0_PTP_EN_SHIFT, CLK_GMAC0_PTP_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_GMAC0_PTP_(x) 		saif_set_reg(CLK_GMAC0_PTP_CTRL_REG_ADDR, x, CLK_GMAC0_PTP_EN_SHIFT, CLK_GMAC0_PTP_EN_MASK)
#define _DIVIDE_CLOCK_CLK_GMAC0_PTP_(div) 			saif_set_reg(CLK_GMAC0_PTP_CTRL_REG_ADDR, div, CLK_GMAC0_PTP_DIV_SHIFT, CLK_GMAC0_PTP_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GMAC0_PTP_ 		saif_get_reg(CLK_GMAC0_PTP_CTRL_REG_ADDR, CLK_GMAC0_PTP_DIV_SHIFT, CLK_GMAC0_PTP_DIV_MASK)
#define _ENABLE_CLOCK_CLK_GMAC_PHY_ 			saif_set_reg(CLK_GMAC_PHY_CTRL_REG_ADDR, CLK_GMAC_PHY_ENABLE_DATA, CLK_GMAC_PHY_EN_SHIFT, CLK_GMAC_PHY_EN_MASK)
#define _DISABLE_CLOCK_CLK_GMAC_PHY_ 			saif_set_reg(CLK_GMAC_PHY_CTRL_REG_ADDR, CLK_GMAC_PHY_DISABLE_DATA, CLK_GMAC_PHY_EN_SHIFT, CLK_GMAC_PHY_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_GMAC_PHY_ 		saif_get_reg(CLK_GMAC_PHY_CTRL_REG_ADDR, CLK_GMAC_PHY_EN_SHIFT, CLK_GMAC_PHY_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_GMAC_PHY_(x) 		saif_set_reg(CLK_GMAC_PHY_CTRL_REG_ADDR, x, CLK_GMAC_PHY_EN_SHIFT, CLK_GMAC_PHY_EN_MASK)
#define _DIVIDE_CLOCK_CLK_GMAC_PHY_(div) 			saif_set_reg(CLK_GMAC_PHY_CTRL_REG_ADDR, div, CLK_GMAC_PHY_DIV_SHIFT, CLK_GMAC_PHY_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_GMAC_PHY_ 		saif_get_reg(CLK_GMAC_PHY_CTRL_REG_ADDR, CLK_GMAC_PHY_DIV_SHIFT, CLK_GMAC_PHY_DIV_MASK)
#define _ENABLE_CLOCK_CLK_GMAC0_GTXC_ 			saif_set_reg(CLK_GMAC0_GTXC_CTRL_REG_ADDR, CLK_GMAC0_GTXC_ENABLE_DATA, CLK_GMAC0_GTXC_EN_SHIFT, CLK_GMAC0_GTXC_EN_MASK)
#define _DISABLE_CLOCK_CLK_GMAC0_GTXC_ 			saif_set_reg(CLK_GMAC0_GTXC_CTRL_REG_ADDR, CLK_GMAC0_GTXC_DISABLE_DATA, CLK_GMAC0_GTXC_EN_SHIFT, CLK_GMAC0_GTXC_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_GMAC0_GTXC_ 		saif_get_reg(CLK_GMAC0_GTXC_CTRL_REG_ADDR, CLK_GMAC0_GTXC_EN_SHIFT, CLK_GMAC0_GTXC_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_GMAC0_GTXC_(x) 		saif_set_reg(CLK_GMAC0_GTXC_CTRL_REG_ADDR, x, CLK_GMAC0_GTXC_EN_SHIFT, CLK_GMAC0_GTXC_EN_MASK)
#define _SET_CLOCK_CLK_GMAC0_GTXC_(dly) 			saif_set_reg(CLK_GMAC0_GTXC_CTRL_REG_ADDR, dly, CLK_GMAC0_GTXC_DLY_SHIFT, CLK_GMAC0_GTXC_DLY_MASK)
#define _GET_CLOCK_DELAY_STATUS_CLK_GMAC0_GTXC_ 		saif_get_reg(CLK_GMAC0_GTXC_CTRL_REG_ADDR, CLK_GMAC0_GTXC_DLY_SHIFT, CLK_GMAC0_GTXC_DLY_MASK)
#define _ENABLE_CLOCK_CLK_U0_SYS_IOMUX_PCLK_ 			saif_set_reg(CLK_U0_SYS_IOMUX_PCLK_CTRL_REG_ADDR, CLK_U0_SYS_IOMUX_PCLK_ENABLE_DATA, CLK_U0_SYS_IOMUX_PCLK_EN_SHIFT, CLK_U0_SYS_IOMUX_PCLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SYS_IOMUX_PCLK_ 			saif_set_reg(CLK_U0_SYS_IOMUX_PCLK_CTRL_REG_ADDR, CLK_U0_SYS_IOMUX_PCLK_DISABLE_DATA, CLK_U0_SYS_IOMUX_PCLK_EN_SHIFT, CLK_U0_SYS_IOMUX_PCLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SYS_IOMUX_PCLK_ 		saif_get_reg(CLK_U0_SYS_IOMUX_PCLK_CTRL_REG_ADDR, CLK_U0_SYS_IOMUX_PCLK_EN_SHIFT, CLK_U0_SYS_IOMUX_PCLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SYS_IOMUX_PCLK_(x) 		saif_set_reg(CLK_U0_SYS_IOMUX_PCLK_CTRL_REG_ADDR, x, CLK_U0_SYS_IOMUX_PCLK_EN_SHIFT, CLK_U0_SYS_IOMUX_PCLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_MAILBOX_CLK_APB_ 			saif_set_reg(CLK_U0_MAILBOX_CLK_APB_CTRL_REG_ADDR, CLK_U0_MAILBOX_CLK_APB_ENABLE_DATA, CLK_U0_MAILBOX_CLK_APB_EN_SHIFT, CLK_U0_MAILBOX_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_MAILBOX_CLK_APB_ 			saif_set_reg(CLK_U0_MAILBOX_CLK_APB_CTRL_REG_ADDR, CLK_U0_MAILBOX_CLK_APB_DISABLE_DATA, CLK_U0_MAILBOX_CLK_APB_EN_SHIFT, CLK_U0_MAILBOX_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_MAILBOX_CLK_APB_ 		saif_get_reg(CLK_U0_MAILBOX_CLK_APB_CTRL_REG_ADDR, CLK_U0_MAILBOX_CLK_APB_EN_SHIFT, CLK_U0_MAILBOX_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_MAILBOX_CLK_APB_(x) 		saif_set_reg(CLK_U0_MAILBOX_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_MAILBOX_CLK_APB_EN_SHIFT, CLK_U0_MAILBOX_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_INT_CTRL_CLK_APB_ 			saif_set_reg(CLK_U0_INT_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U0_INT_CTRL_CLK_APB_ENABLE_DATA, CLK_U0_INT_CTRL_CLK_APB_EN_SHIFT, CLK_U0_INT_CTRL_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_INT_CTRL_CLK_APB_ 			saif_set_reg(CLK_U0_INT_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U0_INT_CTRL_CLK_APB_DISABLE_DATA, CLK_U0_INT_CTRL_CLK_APB_EN_SHIFT, CLK_U0_INT_CTRL_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_INT_CTRL_CLK_APB_ 		saif_get_reg(CLK_U0_INT_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U0_INT_CTRL_CLK_APB_EN_SHIFT, CLK_U0_INT_CTRL_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_INT_CTRL_CLK_APB_(x) 		saif_set_reg(CLK_U0_INT_CTRL_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_INT_CTRL_CLK_APB_EN_SHIFT, CLK_U0_INT_CTRL_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_CAN_CTRL_CLK_APB_ 			saif_set_reg(CLK_U0_CAN_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_APB_ENABLE_DATA, CLK_U0_CAN_CTRL_CLK_APB_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CAN_CTRL_CLK_APB_ 			saif_set_reg(CLK_U0_CAN_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_APB_DISABLE_DATA, CLK_U0_CAN_CTRL_CLK_APB_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CAN_CTRL_CLK_APB_ 		saif_get_reg(CLK_U0_CAN_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_APB_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CAN_CTRL_CLK_APB_(x) 		saif_set_reg(CLK_U0_CAN_CTRL_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_CAN_CTRL_CLK_APB_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_CAN_CTRL_CLK_TIMER_ 			saif_set_reg(CLK_U0_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_TIMER_ENABLE_DATA, CLK_U0_CAN_CTRL_CLK_TIMER_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_TIMER_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CAN_CTRL_CLK_TIMER_ 			saif_set_reg(CLK_U0_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_TIMER_DISABLE_DATA, CLK_U0_CAN_CTRL_CLK_TIMER_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_TIMER_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CAN_CTRL_CLK_TIMER_ 		saif_get_reg(CLK_U0_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_TIMER_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_TIMER_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CAN_CTRL_CLK_TIMER_(x) 		saif_set_reg(CLK_U0_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, x, CLK_U0_CAN_CTRL_CLK_TIMER_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_TIMER_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_CAN_CTRL_CLK_TIMER_(div) 			saif_set_reg(CLK_U0_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, div, CLK_U0_CAN_CTRL_CLK_TIMER_DIV_SHIFT, CLK_U0_CAN_CTRL_CLK_TIMER_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_CAN_CTRL_CLK_TIMER_ 		saif_get_reg(CLK_U0_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_TIMER_DIV_SHIFT, CLK_U0_CAN_CTRL_CLK_TIMER_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_CAN_CTRL_CLK_CAN_ 			saif_set_reg(CLK_U0_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_CAN_ENABLE_DATA, CLK_U0_CAN_CTRL_CLK_CAN_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_CAN_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CAN_CTRL_CLK_CAN_ 			saif_set_reg(CLK_U0_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_CAN_DISABLE_DATA, CLK_U0_CAN_CTRL_CLK_CAN_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_CAN_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CAN_CTRL_CLK_CAN_ 		saif_get_reg(CLK_U0_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_CAN_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_CAN_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CAN_CTRL_CLK_CAN_(x) 		saif_set_reg(CLK_U0_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, x, CLK_U0_CAN_CTRL_CLK_CAN_EN_SHIFT, CLK_U0_CAN_CTRL_CLK_CAN_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_CAN_CTRL_CLK_CAN_(div) 			saif_set_reg(CLK_U0_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, div, CLK_U0_CAN_CTRL_CLK_CAN_DIV_SHIFT, CLK_U0_CAN_CTRL_CLK_CAN_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_CAN_CTRL_CLK_CAN_ 		saif_get_reg(CLK_U0_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, CLK_U0_CAN_CTRL_CLK_CAN_DIV_SHIFT, CLK_U0_CAN_CTRL_CLK_CAN_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U1_CAN_CTRL_CLK_APB_ 			saif_set_reg(CLK_U1_CAN_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_APB_ENABLE_DATA, CLK_U1_CAN_CTRL_CLK_APB_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_CAN_CTRL_CLK_APB_ 			saif_set_reg(CLK_U1_CAN_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_APB_DISABLE_DATA, CLK_U1_CAN_CTRL_CLK_APB_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_CAN_CTRL_CLK_APB_ 		saif_get_reg(CLK_U1_CAN_CTRL_CLK_APB_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_APB_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_CAN_CTRL_CLK_APB_(x) 		saif_set_reg(CLK_U1_CAN_CTRL_CLK_APB_CTRL_REG_ADDR, x, CLK_U1_CAN_CTRL_CLK_APB_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U1_CAN_CTRL_CLK_TIMER_ 			saif_set_reg(CLK_U1_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_TIMER_ENABLE_DATA, CLK_U1_CAN_CTRL_CLK_TIMER_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_TIMER_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_CAN_CTRL_CLK_TIMER_ 			saif_set_reg(CLK_U1_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_TIMER_DISABLE_DATA, CLK_U1_CAN_CTRL_CLK_TIMER_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_TIMER_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_CAN_CTRL_CLK_TIMER_ 		saif_get_reg(CLK_U1_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_TIMER_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_TIMER_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_CAN_CTRL_CLK_TIMER_(x) 		saif_set_reg(CLK_U1_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, x, CLK_U1_CAN_CTRL_CLK_TIMER_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_TIMER_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U1_CAN_CTRL_CLK_TIMER_(div) 			saif_set_reg(CLK_U1_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, div, CLK_U1_CAN_CTRL_CLK_TIMER_DIV_SHIFT, CLK_U1_CAN_CTRL_CLK_TIMER_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U1_CAN_CTRL_CLK_TIMER_ 		saif_get_reg(CLK_U1_CAN_CTRL_CLK_TIMER_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_TIMER_DIV_SHIFT, CLK_U1_CAN_CTRL_CLK_TIMER_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U1_CAN_CTRL_CLK_CAN_ 			saif_set_reg(CLK_U1_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_CAN_ENABLE_DATA, CLK_U1_CAN_CTRL_CLK_CAN_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_CAN_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_CAN_CTRL_CLK_CAN_ 			saif_set_reg(CLK_U1_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_CAN_DISABLE_DATA, CLK_U1_CAN_CTRL_CLK_CAN_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_CAN_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_CAN_CTRL_CLK_CAN_ 		saif_get_reg(CLK_U1_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_CAN_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_CAN_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_CAN_CTRL_CLK_CAN_(x) 		saif_set_reg(CLK_U1_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, x, CLK_U1_CAN_CTRL_CLK_CAN_EN_SHIFT, CLK_U1_CAN_CTRL_CLK_CAN_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U1_CAN_CTRL_CLK_CAN_(div) 			saif_set_reg(CLK_U1_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, div, CLK_U1_CAN_CTRL_CLK_CAN_DIV_SHIFT, CLK_U1_CAN_CTRL_CLK_CAN_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U1_CAN_CTRL_CLK_CAN_ 		saif_get_reg(CLK_U1_CAN_CTRL_CLK_CAN_CTRL_REG_ADDR, CLK_U1_CAN_CTRL_CLK_CAN_DIV_SHIFT, CLK_U1_CAN_CTRL_CLK_CAN_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_PWM_8CH_CLK_APB_ 			saif_set_reg(CLK_U0_PWM_8CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_PWM_8CH_CLK_APB_ENABLE_DATA, CLK_U0_PWM_8CH_CLK_APB_EN_SHIFT, CLK_U0_PWM_8CH_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_PWM_8CH_CLK_APB_ 			saif_set_reg(CLK_U0_PWM_8CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_PWM_8CH_CLK_APB_DISABLE_DATA, CLK_U0_PWM_8CH_CLK_APB_EN_SHIFT, CLK_U0_PWM_8CH_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_PWM_8CH_CLK_APB_ 		saif_get_reg(CLK_U0_PWM_8CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_PWM_8CH_CLK_APB_EN_SHIFT, CLK_U0_PWM_8CH_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_PWM_8CH_CLK_APB_(x) 		saif_set_reg(CLK_U0_PWM_8CH_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_PWM_8CH_CLK_APB_EN_SHIFT, CLK_U0_PWM_8CH_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DSKIT_WDT_CLK_APB_ 			saif_set_reg(CLK_U0_DSKIT_WDT_CLK_APB_CTRL_REG_ADDR, CLK_U0_DSKIT_WDT_CLK_APB_ENABLE_DATA, CLK_U0_DSKIT_WDT_CLK_APB_EN_SHIFT, CLK_U0_DSKIT_WDT_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DSKIT_WDT_CLK_APB_ 			saif_set_reg(CLK_U0_DSKIT_WDT_CLK_APB_CTRL_REG_ADDR, CLK_U0_DSKIT_WDT_CLK_APB_DISABLE_DATA, CLK_U0_DSKIT_WDT_CLK_APB_EN_SHIFT, CLK_U0_DSKIT_WDT_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DSKIT_WDT_CLK_APB_ 		saif_get_reg(CLK_U0_DSKIT_WDT_CLK_APB_CTRL_REG_ADDR, CLK_U0_DSKIT_WDT_CLK_APB_EN_SHIFT, CLK_U0_DSKIT_WDT_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DSKIT_WDT_CLK_APB_(x) 		saif_set_reg(CLK_U0_DSKIT_WDT_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_DSKIT_WDT_CLK_APB_EN_SHIFT, CLK_U0_DSKIT_WDT_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DSKIT_WDT_CLK_WDT_ 			saif_set_reg(CLK_U0_DSKIT_WDT_CLK_WDT_CTRL_REG_ADDR, CLK_U0_DSKIT_WDT_CLK_WDT_ENABLE_DATA, CLK_U0_DSKIT_WDT_CLK_WDT_EN_SHIFT, CLK_U0_DSKIT_WDT_CLK_WDT_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DSKIT_WDT_CLK_WDT_ 			saif_set_reg(CLK_U0_DSKIT_WDT_CLK_WDT_CTRL_REG_ADDR, CLK_U0_DSKIT_WDT_CLK_WDT_DISABLE_DATA, CLK_U0_DSKIT_WDT_CLK_WDT_EN_SHIFT, CLK_U0_DSKIT_WDT_CLK_WDT_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DSKIT_WDT_CLK_WDT_ 		saif_get_reg(CLK_U0_DSKIT_WDT_CLK_WDT_CTRL_REG_ADDR, CLK_U0_DSKIT_WDT_CLK_WDT_EN_SHIFT, CLK_U0_DSKIT_WDT_CLK_WDT_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DSKIT_WDT_CLK_WDT_(x) 		saif_set_reg(CLK_U0_DSKIT_WDT_CLK_WDT_CTRL_REG_ADDR, x, CLK_U0_DSKIT_WDT_CLK_WDT_EN_SHIFT, CLK_U0_DSKIT_WDT_CLK_WDT_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_APB_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_APB_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_APB_ENABLE_DATA, CLK_U0_SI5_TIMER_CLK_APB_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_APB_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_APB_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_APB_DISABLE_DATA, CLK_U0_SI5_TIMER_CLK_APB_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_APB_ 		saif_get_reg(CLK_U0_SI5_TIMER_CLK_APB_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_APB_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_APB_(x) 		saif_set_reg(CLK_U0_SI5_TIMER_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_SI5_TIMER_CLK_APB_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_TIMER0_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER0_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER0_ENABLE_DATA, CLK_U0_SI5_TIMER_CLK_TIMER0_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER0_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_TIMER0_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER0_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER0_DISABLE_DATA, CLK_U0_SI5_TIMER_CLK_TIMER0_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER0_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_TIMER0_ 		saif_get_reg(CLK_U0_SI5_TIMER_CLK_TIMER0_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER0_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER0_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_TIMER0_(x) 		saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER0_CTRL_REG_ADDR, x, CLK_U0_SI5_TIMER_CLK_TIMER0_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER0_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_TIMER1_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER1_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER1_ENABLE_DATA, CLK_U0_SI5_TIMER_CLK_TIMER1_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER1_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_TIMER1_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER1_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER1_DISABLE_DATA, CLK_U0_SI5_TIMER_CLK_TIMER1_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER1_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_TIMER1_ 		saif_get_reg(CLK_U0_SI5_TIMER_CLK_TIMER1_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER1_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER1_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_TIMER1_(x) 		saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER1_CTRL_REG_ADDR, x, CLK_U0_SI5_TIMER_CLK_TIMER1_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER1_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_TIMER2_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER2_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER2_ENABLE_DATA, CLK_U0_SI5_TIMER_CLK_TIMER2_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER2_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_TIMER2_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER2_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER2_DISABLE_DATA, CLK_U0_SI5_TIMER_CLK_TIMER2_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER2_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_TIMER2_ 		saif_get_reg(CLK_U0_SI5_TIMER_CLK_TIMER2_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER2_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER2_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_TIMER2_(x) 		saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER2_CTRL_REG_ADDR, x, CLK_U0_SI5_TIMER_CLK_TIMER2_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER2_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_TIMER3_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER3_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER3_ENABLE_DATA, CLK_U0_SI5_TIMER_CLK_TIMER3_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER3_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SI5_TIMER_CLK_TIMER3_ 			saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER3_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER3_DISABLE_DATA, CLK_U0_SI5_TIMER_CLK_TIMER3_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER3_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_TIMER3_ 		saif_get_reg(CLK_U0_SI5_TIMER_CLK_TIMER3_CTRL_REG_ADDR, CLK_U0_SI5_TIMER_CLK_TIMER3_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER3_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SI5_TIMER_CLK_TIMER3_(x) 		saif_set_reg(CLK_U0_SI5_TIMER_CLK_TIMER3_CTRL_REG_ADDR, x, CLK_U0_SI5_TIMER_CLK_TIMER3_EN_SHIFT, CLK_U0_SI5_TIMER_CLK_TIMER3_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_TEMP_SENSOR_CLK_APB_ 			saif_set_reg(CLK_U0_TEMP_SENSOR_CLK_APB_CTRL_REG_ADDR, CLK_U0_TEMP_SENSOR_CLK_APB_ENABLE_DATA, CLK_U0_TEMP_SENSOR_CLK_APB_EN_SHIFT, CLK_U0_TEMP_SENSOR_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_TEMP_SENSOR_CLK_APB_ 			saif_set_reg(CLK_U0_TEMP_SENSOR_CLK_APB_CTRL_REG_ADDR, CLK_U0_TEMP_SENSOR_CLK_APB_DISABLE_DATA, CLK_U0_TEMP_SENSOR_CLK_APB_EN_SHIFT, CLK_U0_TEMP_SENSOR_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_TEMP_SENSOR_CLK_APB_ 		saif_get_reg(CLK_U0_TEMP_SENSOR_CLK_APB_CTRL_REG_ADDR, CLK_U0_TEMP_SENSOR_CLK_APB_EN_SHIFT, CLK_U0_TEMP_SENSOR_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_TEMP_SENSOR_CLK_APB_(x) 		saif_set_reg(CLK_U0_TEMP_SENSOR_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_TEMP_SENSOR_CLK_APB_EN_SHIFT, CLK_U0_TEMP_SENSOR_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_TEMP_SENSOR_CLK_TEMP_ 			saif_set_reg(CLK_U0_TEMP_SENSOR_CLK_TEMP_CTRL_REG_ADDR, CLK_U0_TEMP_SENSOR_CLK_TEMP_ENABLE_DATA, CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_SHIFT, CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_TEMP_SENSOR_CLK_TEMP_ 			saif_set_reg(CLK_U0_TEMP_SENSOR_CLK_TEMP_CTRL_REG_ADDR, CLK_U0_TEMP_SENSOR_CLK_TEMP_DISABLE_DATA, CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_SHIFT, CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_TEMP_SENSOR_CLK_TEMP_ 		saif_get_reg(CLK_U0_TEMP_SENSOR_CLK_TEMP_CTRL_REG_ADDR, CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_SHIFT, CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_TEMP_SENSOR_CLK_TEMP_(x) 		saif_set_reg(CLK_U0_TEMP_SENSOR_CLK_TEMP_CTRL_REG_ADDR, x, CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_SHIFT, CLK_U0_TEMP_SENSOR_CLK_TEMP_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_TEMP_SENSOR_CLK_TEMP_(div) 			saif_set_reg(CLK_U0_TEMP_SENSOR_CLK_TEMP_CTRL_REG_ADDR, div, CLK_U0_TEMP_SENSOR_CLK_TEMP_DIV_SHIFT, CLK_U0_TEMP_SENSOR_CLK_TEMP_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_TEMP_SENSOR_CLK_TEMP_ 		saif_get_reg(CLK_U0_TEMP_SENSOR_CLK_TEMP_CTRL_REG_ADDR, CLK_U0_TEMP_SENSOR_CLK_TEMP_DIV_SHIFT, CLK_U0_TEMP_SENSOR_CLK_TEMP_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U0_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U0_SSP_SPI_CLK_APB_ENABLE_DATA, CLK_U0_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U0_SSP_SPI_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U0_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U0_SSP_SPI_CLK_APB_DISABLE_DATA, CLK_U0_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U0_SSP_SPI_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_SSP_SPI_CLK_APB_ 		saif_get_reg(CLK_U0_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U0_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U0_SSP_SPI_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_SSP_SPI_CLK_APB_(x) 		saif_set_reg(CLK_U0_SSP_SPI_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U0_SSP_SPI_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U1_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U1_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U1_SSP_SPI_CLK_APB_ENABLE_DATA, CLK_U1_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U1_SSP_SPI_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U1_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U1_SSP_SPI_CLK_APB_DISABLE_DATA, CLK_U1_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U1_SSP_SPI_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_SSP_SPI_CLK_APB_ 		saif_get_reg(CLK_U1_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U1_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U1_SSP_SPI_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_SSP_SPI_CLK_APB_(x) 		saif_set_reg(CLK_U1_SSP_SPI_CLK_APB_CTRL_REG_ADDR, x, CLK_U1_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U1_SSP_SPI_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U2_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U2_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U2_SSP_SPI_CLK_APB_ENABLE_DATA, CLK_U2_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U2_SSP_SPI_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U2_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U2_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U2_SSP_SPI_CLK_APB_DISABLE_DATA, CLK_U2_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U2_SSP_SPI_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U2_SSP_SPI_CLK_APB_ 		saif_get_reg(CLK_U2_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U2_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U2_SSP_SPI_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U2_SSP_SPI_CLK_APB_(x) 		saif_set_reg(CLK_U2_SSP_SPI_CLK_APB_CTRL_REG_ADDR, x, CLK_U2_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U2_SSP_SPI_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U3_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U3_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U3_SSP_SPI_CLK_APB_ENABLE_DATA, CLK_U3_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U3_SSP_SPI_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U3_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U3_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U3_SSP_SPI_CLK_APB_DISABLE_DATA, CLK_U3_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U3_SSP_SPI_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U3_SSP_SPI_CLK_APB_ 		saif_get_reg(CLK_U3_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U3_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U3_SSP_SPI_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U3_SSP_SPI_CLK_APB_(x) 		saif_set_reg(CLK_U3_SSP_SPI_CLK_APB_CTRL_REG_ADDR, x, CLK_U3_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U3_SSP_SPI_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U4_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U4_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U4_SSP_SPI_CLK_APB_ENABLE_DATA, CLK_U4_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U4_SSP_SPI_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U4_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U4_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U4_SSP_SPI_CLK_APB_DISABLE_DATA, CLK_U4_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U4_SSP_SPI_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U4_SSP_SPI_CLK_APB_ 		saif_get_reg(CLK_U4_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U4_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U4_SSP_SPI_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U4_SSP_SPI_CLK_APB_(x) 		saif_set_reg(CLK_U4_SSP_SPI_CLK_APB_CTRL_REG_ADDR, x, CLK_U4_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U4_SSP_SPI_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U5_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U5_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U5_SSP_SPI_CLK_APB_ENABLE_DATA, CLK_U5_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U5_SSP_SPI_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U5_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U5_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U5_SSP_SPI_CLK_APB_DISABLE_DATA, CLK_U5_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U5_SSP_SPI_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U5_SSP_SPI_CLK_APB_ 		saif_get_reg(CLK_U5_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U5_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U5_SSP_SPI_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U5_SSP_SPI_CLK_APB_(x) 		saif_set_reg(CLK_U5_SSP_SPI_CLK_APB_CTRL_REG_ADDR, x, CLK_U5_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U5_SSP_SPI_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U6_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U6_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U6_SSP_SPI_CLK_APB_ENABLE_DATA, CLK_U6_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U6_SSP_SPI_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U6_SSP_SPI_CLK_APB_ 			saif_set_reg(CLK_U6_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U6_SSP_SPI_CLK_APB_DISABLE_DATA, CLK_U6_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U6_SSP_SPI_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U6_SSP_SPI_CLK_APB_ 		saif_get_reg(CLK_U6_SSP_SPI_CLK_APB_CTRL_REG_ADDR, CLK_U6_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U6_SSP_SPI_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U6_SSP_SPI_CLK_APB_(x) 		saif_set_reg(CLK_U6_SSP_SPI_CLK_APB_CTRL_REG_ADDR, x, CLK_U6_SSP_SPI_CLK_APB_EN_SHIFT, CLK_U6_SSP_SPI_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U0_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U0_DW_I2C_CLK_APB_ENABLE_DATA, CLK_U0_DW_I2C_CLK_APB_EN_SHIFT, CLK_U0_DW_I2C_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U0_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U0_DW_I2C_CLK_APB_DISABLE_DATA, CLK_U0_DW_I2C_CLK_APB_EN_SHIFT, CLK_U0_DW_I2C_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DW_I2C_CLK_APB_ 		saif_get_reg(CLK_U0_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U0_DW_I2C_CLK_APB_EN_SHIFT, CLK_U0_DW_I2C_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DW_I2C_CLK_APB_(x) 		saif_set_reg(CLK_U0_DW_I2C_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_DW_I2C_CLK_APB_EN_SHIFT, CLK_U0_DW_I2C_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U1_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U1_DW_I2C_CLK_APB_ENABLE_DATA, CLK_U1_DW_I2C_CLK_APB_EN_SHIFT, CLK_U1_DW_I2C_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U1_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U1_DW_I2C_CLK_APB_DISABLE_DATA, CLK_U1_DW_I2C_CLK_APB_EN_SHIFT, CLK_U1_DW_I2C_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_I2C_CLK_APB_ 		saif_get_reg(CLK_U1_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U1_DW_I2C_CLK_APB_EN_SHIFT, CLK_U1_DW_I2C_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_I2C_CLK_APB_(x) 		saif_set_reg(CLK_U1_DW_I2C_CLK_APB_CTRL_REG_ADDR, x, CLK_U1_DW_I2C_CLK_APB_EN_SHIFT, CLK_U1_DW_I2C_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U2_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U2_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U2_DW_I2C_CLK_APB_ENABLE_DATA, CLK_U2_DW_I2C_CLK_APB_EN_SHIFT, CLK_U2_DW_I2C_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U2_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U2_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U2_DW_I2C_CLK_APB_DISABLE_DATA, CLK_U2_DW_I2C_CLK_APB_EN_SHIFT, CLK_U2_DW_I2C_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U2_DW_I2C_CLK_APB_ 		saif_get_reg(CLK_U2_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U2_DW_I2C_CLK_APB_EN_SHIFT, CLK_U2_DW_I2C_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U2_DW_I2C_CLK_APB_(x) 		saif_set_reg(CLK_U2_DW_I2C_CLK_APB_CTRL_REG_ADDR, x, CLK_U2_DW_I2C_CLK_APB_EN_SHIFT, CLK_U2_DW_I2C_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U3_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U3_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U3_DW_I2C_CLK_APB_ENABLE_DATA, CLK_U3_DW_I2C_CLK_APB_EN_SHIFT, CLK_U3_DW_I2C_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U3_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U3_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U3_DW_I2C_CLK_APB_DISABLE_DATA, CLK_U3_DW_I2C_CLK_APB_EN_SHIFT, CLK_U3_DW_I2C_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U3_DW_I2C_CLK_APB_ 		saif_get_reg(CLK_U3_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U3_DW_I2C_CLK_APB_EN_SHIFT, CLK_U3_DW_I2C_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U3_DW_I2C_CLK_APB_(x) 		saif_set_reg(CLK_U3_DW_I2C_CLK_APB_CTRL_REG_ADDR, x, CLK_U3_DW_I2C_CLK_APB_EN_SHIFT, CLK_U3_DW_I2C_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U4_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U4_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U4_DW_I2C_CLK_APB_ENABLE_DATA, CLK_U4_DW_I2C_CLK_APB_EN_SHIFT, CLK_U4_DW_I2C_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U4_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U4_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U4_DW_I2C_CLK_APB_DISABLE_DATA, CLK_U4_DW_I2C_CLK_APB_EN_SHIFT, CLK_U4_DW_I2C_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U4_DW_I2C_CLK_APB_ 		saif_get_reg(CLK_U4_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U4_DW_I2C_CLK_APB_EN_SHIFT, CLK_U4_DW_I2C_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U4_DW_I2C_CLK_APB_(x) 		saif_set_reg(CLK_U4_DW_I2C_CLK_APB_CTRL_REG_ADDR, x, CLK_U4_DW_I2C_CLK_APB_EN_SHIFT, CLK_U4_DW_I2C_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U5_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U5_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U5_DW_I2C_CLK_APB_ENABLE_DATA, CLK_U5_DW_I2C_CLK_APB_EN_SHIFT, CLK_U5_DW_I2C_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U5_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U5_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U5_DW_I2C_CLK_APB_DISABLE_DATA, CLK_U5_DW_I2C_CLK_APB_EN_SHIFT, CLK_U5_DW_I2C_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U5_DW_I2C_CLK_APB_ 		saif_get_reg(CLK_U5_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U5_DW_I2C_CLK_APB_EN_SHIFT, CLK_U5_DW_I2C_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U5_DW_I2C_CLK_APB_(x) 		saif_set_reg(CLK_U5_DW_I2C_CLK_APB_CTRL_REG_ADDR, x, CLK_U5_DW_I2C_CLK_APB_EN_SHIFT, CLK_U5_DW_I2C_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U6_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U6_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U6_DW_I2C_CLK_APB_ENABLE_DATA, CLK_U6_DW_I2C_CLK_APB_EN_SHIFT, CLK_U6_DW_I2C_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U6_DW_I2C_CLK_APB_ 			saif_set_reg(CLK_U6_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U6_DW_I2C_CLK_APB_DISABLE_DATA, CLK_U6_DW_I2C_CLK_APB_EN_SHIFT, CLK_U6_DW_I2C_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U6_DW_I2C_CLK_APB_ 		saif_get_reg(CLK_U6_DW_I2C_CLK_APB_CTRL_REG_ADDR, CLK_U6_DW_I2C_CLK_APB_EN_SHIFT, CLK_U6_DW_I2C_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U6_DW_I2C_CLK_APB_(x) 		saif_set_reg(CLK_U6_DW_I2C_CLK_APB_CTRL_REG_ADDR, x, CLK_U6_DW_I2C_CLK_APB_EN_SHIFT, CLK_U6_DW_I2C_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U0_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U0_DW_UART_CLK_APB_ENABLE_DATA, CLK_U0_DW_UART_CLK_APB_EN_SHIFT, CLK_U0_DW_UART_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U0_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U0_DW_UART_CLK_APB_DISABLE_DATA, CLK_U0_DW_UART_CLK_APB_EN_SHIFT, CLK_U0_DW_UART_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DW_UART_CLK_APB_ 		saif_get_reg(CLK_U0_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U0_DW_UART_CLK_APB_EN_SHIFT, CLK_U0_DW_UART_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DW_UART_CLK_APB_(x) 		saif_set_reg(CLK_U0_DW_UART_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_DW_UART_CLK_APB_EN_SHIFT, CLK_U0_DW_UART_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U0_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U0_DW_UART_CLK_CORE_ENABLE_DATA, CLK_U0_DW_UART_CLK_CORE_EN_SHIFT, CLK_U0_DW_UART_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U0_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U0_DW_UART_CLK_CORE_DISABLE_DATA, CLK_U0_DW_UART_CLK_CORE_EN_SHIFT, CLK_U0_DW_UART_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U0_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U0_DW_UART_CLK_CORE_EN_SHIFT, CLK_U0_DW_UART_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DW_UART_CLK_CORE_(x) 		saif_set_reg(CLK_U0_DW_UART_CLK_CORE_CTRL_REG_ADDR, x, CLK_U0_DW_UART_CLK_CORE_EN_SHIFT, CLK_U0_DW_UART_CLK_CORE_EN_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U1_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U1_DW_UART_CLK_APB_ENABLE_DATA, CLK_U1_DW_UART_CLK_APB_EN_SHIFT, CLK_U1_DW_UART_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U1_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U1_DW_UART_CLK_APB_DISABLE_DATA, CLK_U1_DW_UART_CLK_APB_EN_SHIFT, CLK_U1_DW_UART_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_UART_CLK_APB_ 		saif_get_reg(CLK_U1_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U1_DW_UART_CLK_APB_EN_SHIFT, CLK_U1_DW_UART_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_UART_CLK_APB_(x) 		saif_set_reg(CLK_U1_DW_UART_CLK_APB_CTRL_REG_ADDR, x, CLK_U1_DW_UART_CLK_APB_EN_SHIFT, CLK_U1_DW_UART_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U1_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U1_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U1_DW_UART_CLK_CORE_ENABLE_DATA, CLK_U1_DW_UART_CLK_CORE_EN_SHIFT, CLK_U1_DW_UART_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U1_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U1_DW_UART_CLK_CORE_DISABLE_DATA, CLK_U1_DW_UART_CLK_CORE_EN_SHIFT, CLK_U1_DW_UART_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U1_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U1_DW_UART_CLK_CORE_EN_SHIFT, CLK_U1_DW_UART_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_DW_UART_CLK_CORE_(x) 		saif_set_reg(CLK_U1_DW_UART_CLK_CORE_CTRL_REG_ADDR, x, CLK_U1_DW_UART_CLK_CORE_EN_SHIFT, CLK_U1_DW_UART_CLK_CORE_EN_MASK)
#define _ENABLE_CLOCK_CLK_U2_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U2_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U2_DW_UART_CLK_APB_ENABLE_DATA, CLK_U2_DW_UART_CLK_APB_EN_SHIFT, CLK_U2_DW_UART_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U2_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U2_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U2_DW_UART_CLK_APB_DISABLE_DATA, CLK_U2_DW_UART_CLK_APB_EN_SHIFT, CLK_U2_DW_UART_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U2_DW_UART_CLK_APB_ 		saif_get_reg(CLK_U2_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U2_DW_UART_CLK_APB_EN_SHIFT, CLK_U2_DW_UART_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U2_DW_UART_CLK_APB_(x) 		saif_set_reg(CLK_U2_DW_UART_CLK_APB_CTRL_REG_ADDR, x, CLK_U2_DW_UART_CLK_APB_EN_SHIFT, CLK_U2_DW_UART_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U2_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U2_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U2_DW_UART_CLK_CORE_ENABLE_DATA, CLK_U2_DW_UART_CLK_CORE_EN_SHIFT, CLK_U2_DW_UART_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U2_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U2_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U2_DW_UART_CLK_CORE_DISABLE_DATA, CLK_U2_DW_UART_CLK_CORE_EN_SHIFT, CLK_U2_DW_UART_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U2_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U2_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U2_DW_UART_CLK_CORE_EN_SHIFT, CLK_U2_DW_UART_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U2_DW_UART_CLK_CORE_(x) 		saif_set_reg(CLK_U2_DW_UART_CLK_CORE_CTRL_REG_ADDR, x, CLK_U2_DW_UART_CLK_CORE_EN_SHIFT, CLK_U2_DW_UART_CLK_CORE_EN_MASK)
#define _ENABLE_CLOCK_CLK_U3_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U3_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U3_DW_UART_CLK_APB_ENABLE_DATA, CLK_U3_DW_UART_CLK_APB_EN_SHIFT, CLK_U3_DW_UART_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U3_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U3_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U3_DW_UART_CLK_APB_DISABLE_DATA, CLK_U3_DW_UART_CLK_APB_EN_SHIFT, CLK_U3_DW_UART_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U3_DW_UART_CLK_APB_ 		saif_get_reg(CLK_U3_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U3_DW_UART_CLK_APB_EN_SHIFT, CLK_U3_DW_UART_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U3_DW_UART_CLK_APB_(x) 		saif_set_reg(CLK_U3_DW_UART_CLK_APB_CTRL_REG_ADDR, x, CLK_U3_DW_UART_CLK_APB_EN_SHIFT, CLK_U3_DW_UART_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U3_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U3_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U3_DW_UART_CLK_CORE_ENABLE_DATA, CLK_U3_DW_UART_CLK_CORE_EN_SHIFT, CLK_U3_DW_UART_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U3_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U3_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U3_DW_UART_CLK_CORE_DISABLE_DATA, CLK_U3_DW_UART_CLK_CORE_EN_SHIFT, CLK_U3_DW_UART_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U3_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U3_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U3_DW_UART_CLK_CORE_EN_SHIFT, CLK_U3_DW_UART_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U3_DW_UART_CLK_CORE_(x) 		saif_set_reg(CLK_U3_DW_UART_CLK_CORE_CTRL_REG_ADDR, x, CLK_U3_DW_UART_CLK_CORE_EN_SHIFT, CLK_U3_DW_UART_CLK_CORE_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U3_DW_UART_CLK_CORE_(div) 			saif_set_reg(CLK_U3_DW_UART_CLK_CORE_CTRL_REG_ADDR, div, CLK_U3_DW_UART_CLK_CORE_DIV_SHIFT, CLK_U3_DW_UART_CLK_CORE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U3_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U3_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U3_DW_UART_CLK_CORE_DIV_SHIFT, CLK_U3_DW_UART_CLK_CORE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U4_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U4_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U4_DW_UART_CLK_APB_ENABLE_DATA, CLK_U4_DW_UART_CLK_APB_EN_SHIFT, CLK_U4_DW_UART_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U4_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U4_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U4_DW_UART_CLK_APB_DISABLE_DATA, CLK_U4_DW_UART_CLK_APB_EN_SHIFT, CLK_U4_DW_UART_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U4_DW_UART_CLK_APB_ 		saif_get_reg(CLK_U4_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U4_DW_UART_CLK_APB_EN_SHIFT, CLK_U4_DW_UART_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U4_DW_UART_CLK_APB_(x) 		saif_set_reg(CLK_U4_DW_UART_CLK_APB_CTRL_REG_ADDR, x, CLK_U4_DW_UART_CLK_APB_EN_SHIFT, CLK_U4_DW_UART_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U4_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U4_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U4_DW_UART_CLK_CORE_ENABLE_DATA, CLK_U4_DW_UART_CLK_CORE_EN_SHIFT, CLK_U4_DW_UART_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U4_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U4_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U4_DW_UART_CLK_CORE_DISABLE_DATA, CLK_U4_DW_UART_CLK_CORE_EN_SHIFT, CLK_U4_DW_UART_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U4_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U4_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U4_DW_UART_CLK_CORE_EN_SHIFT, CLK_U4_DW_UART_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U4_DW_UART_CLK_CORE_(x) 		saif_set_reg(CLK_U4_DW_UART_CLK_CORE_CTRL_REG_ADDR, x, CLK_U4_DW_UART_CLK_CORE_EN_SHIFT, CLK_U4_DW_UART_CLK_CORE_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U4_DW_UART_CLK_CORE_(div) 			saif_set_reg(CLK_U4_DW_UART_CLK_CORE_CTRL_REG_ADDR, div, CLK_U4_DW_UART_CLK_CORE_DIV_SHIFT, CLK_U4_DW_UART_CLK_CORE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U4_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U4_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U4_DW_UART_CLK_CORE_DIV_SHIFT, CLK_U4_DW_UART_CLK_CORE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U5_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U5_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U5_DW_UART_CLK_APB_ENABLE_DATA, CLK_U5_DW_UART_CLK_APB_EN_SHIFT, CLK_U5_DW_UART_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U5_DW_UART_CLK_APB_ 			saif_set_reg(CLK_U5_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U5_DW_UART_CLK_APB_DISABLE_DATA, CLK_U5_DW_UART_CLK_APB_EN_SHIFT, CLK_U5_DW_UART_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U5_DW_UART_CLK_APB_ 		saif_get_reg(CLK_U5_DW_UART_CLK_APB_CTRL_REG_ADDR, CLK_U5_DW_UART_CLK_APB_EN_SHIFT, CLK_U5_DW_UART_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U5_DW_UART_CLK_APB_(x) 		saif_set_reg(CLK_U5_DW_UART_CLK_APB_CTRL_REG_ADDR, x, CLK_U5_DW_UART_CLK_APB_EN_SHIFT, CLK_U5_DW_UART_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U5_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U5_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U5_DW_UART_CLK_CORE_ENABLE_DATA, CLK_U5_DW_UART_CLK_CORE_EN_SHIFT, CLK_U5_DW_UART_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U5_DW_UART_CLK_CORE_ 			saif_set_reg(CLK_U5_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U5_DW_UART_CLK_CORE_DISABLE_DATA, CLK_U5_DW_UART_CLK_CORE_EN_SHIFT, CLK_U5_DW_UART_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U5_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U5_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U5_DW_UART_CLK_CORE_EN_SHIFT, CLK_U5_DW_UART_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U5_DW_UART_CLK_CORE_(x) 		saif_set_reg(CLK_U5_DW_UART_CLK_CORE_CTRL_REG_ADDR, x, CLK_U5_DW_UART_CLK_CORE_EN_SHIFT, CLK_U5_DW_UART_CLK_CORE_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U5_DW_UART_CLK_CORE_(div) 			saif_set_reg(CLK_U5_DW_UART_CLK_CORE_CTRL_REG_ADDR, div, CLK_U5_DW_UART_CLK_CORE_DIV_SHIFT, CLK_U5_DW_UART_CLK_CORE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U5_DW_UART_CLK_CORE_ 		saif_get_reg(CLK_U5_DW_UART_CLK_CORE_CTRL_REG_ADDR, CLK_U5_DW_UART_CLK_CORE_DIV_SHIFT, CLK_U5_DW_UART_CLK_CORE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_PWMDAC_CLK_APB_ 			saif_set_reg(CLK_U0_PWMDAC_CLK_APB_CTRL_REG_ADDR, CLK_U0_PWMDAC_CLK_APB_ENABLE_DATA, CLK_U0_PWMDAC_CLK_APB_EN_SHIFT, CLK_U0_PWMDAC_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_PWMDAC_CLK_APB_ 			saif_set_reg(CLK_U0_PWMDAC_CLK_APB_CTRL_REG_ADDR, CLK_U0_PWMDAC_CLK_APB_DISABLE_DATA, CLK_U0_PWMDAC_CLK_APB_EN_SHIFT, CLK_U0_PWMDAC_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_PWMDAC_CLK_APB_ 		saif_get_reg(CLK_U0_PWMDAC_CLK_APB_CTRL_REG_ADDR, CLK_U0_PWMDAC_CLK_APB_EN_SHIFT, CLK_U0_PWMDAC_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_PWMDAC_CLK_APB_(x) 		saif_set_reg(CLK_U0_PWMDAC_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_PWMDAC_CLK_APB_EN_SHIFT, CLK_U0_PWMDAC_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_PWMDAC_CLK_CORE_ 			saif_set_reg(CLK_U0_PWMDAC_CLK_CORE_CTRL_REG_ADDR, CLK_U0_PWMDAC_CLK_CORE_ENABLE_DATA, CLK_U0_PWMDAC_CLK_CORE_EN_SHIFT, CLK_U0_PWMDAC_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_PWMDAC_CLK_CORE_ 			saif_set_reg(CLK_U0_PWMDAC_CLK_CORE_CTRL_REG_ADDR, CLK_U0_PWMDAC_CLK_CORE_DISABLE_DATA, CLK_U0_PWMDAC_CLK_CORE_EN_SHIFT, CLK_U0_PWMDAC_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_PWMDAC_CLK_CORE_ 		saif_get_reg(CLK_U0_PWMDAC_CLK_CORE_CTRL_REG_ADDR, CLK_U0_PWMDAC_CLK_CORE_EN_SHIFT, CLK_U0_PWMDAC_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_PWMDAC_CLK_CORE_(x) 		saif_set_reg(CLK_U0_PWMDAC_CLK_CORE_CTRL_REG_ADDR, x, CLK_U0_PWMDAC_CLK_CORE_EN_SHIFT, CLK_U0_PWMDAC_CLK_CORE_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_PWMDAC_CLK_CORE_(div) 			saif_set_reg(CLK_U0_PWMDAC_CLK_CORE_CTRL_REG_ADDR, div, CLK_U0_PWMDAC_CLK_CORE_DIV_SHIFT, CLK_U0_PWMDAC_CLK_CORE_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_PWMDAC_CLK_CORE_ 		saif_get_reg(CLK_U0_PWMDAC_CLK_CORE_CTRL_REG_ADDR, CLK_U0_PWMDAC_CLK_CORE_DIV_SHIFT, CLK_U0_PWMDAC_CLK_CORE_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_SPDIF_CLK_APB_ 			saif_set_reg(CLK_U0_CDNS_SPDIF_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_SPDIF_CLK_APB_ENABLE_DATA, CLK_U0_CDNS_SPDIF_CLK_APB_EN_SHIFT, CLK_U0_CDNS_SPDIF_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_SPDIF_CLK_APB_ 			saif_set_reg(CLK_U0_CDNS_SPDIF_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_SPDIF_CLK_APB_DISABLE_DATA, CLK_U0_CDNS_SPDIF_CLK_APB_EN_SHIFT, CLK_U0_CDNS_SPDIF_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_SPDIF_CLK_APB_ 		saif_get_reg(CLK_U0_CDNS_SPDIF_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_SPDIF_CLK_APB_EN_SHIFT, CLK_U0_CDNS_SPDIF_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_SPDIF_CLK_APB_(x) 		saif_set_reg(CLK_U0_CDNS_SPDIF_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_CDNS_SPDIF_CLK_APB_EN_SHIFT, CLK_U0_CDNS_SPDIF_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_SPDIF_CLK_CORE_ 			saif_set_reg(CLK_U0_CDNS_SPDIF_CLK_CORE_CTRL_REG_ADDR, CLK_U0_CDNS_SPDIF_CLK_CORE_ENABLE_DATA, CLK_U0_CDNS_SPDIF_CLK_CORE_EN_SHIFT, CLK_U0_CDNS_SPDIF_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_SPDIF_CLK_CORE_ 			saif_set_reg(CLK_U0_CDNS_SPDIF_CLK_CORE_CTRL_REG_ADDR, CLK_U0_CDNS_SPDIF_CLK_CORE_DISABLE_DATA, CLK_U0_CDNS_SPDIF_CLK_CORE_EN_SHIFT, CLK_U0_CDNS_SPDIF_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_SPDIF_CLK_CORE_ 		saif_get_reg(CLK_U0_CDNS_SPDIF_CLK_CORE_CTRL_REG_ADDR, CLK_U0_CDNS_SPDIF_CLK_CORE_EN_SHIFT, CLK_U0_CDNS_SPDIF_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_SPDIF_CLK_CORE_(x) 		saif_set_reg(CLK_U0_CDNS_SPDIF_CLK_CORE_CTRL_REG_ADDR, x, CLK_U0_CDNS_SPDIF_CLK_CORE_EN_SHIFT, CLK_U0_CDNS_SPDIF_CLK_CORE_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_I2STX_4CH_CLK_APB_ 			saif_set_reg(CLK_U0_I2STX_4CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_CLK_APB_ENABLE_DATA, CLK_U0_I2STX_4CH_CLK_APB_EN_SHIFT, CLK_U0_I2STX_4CH_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_I2STX_4CH_CLK_APB_ 			saif_set_reg(CLK_U0_I2STX_4CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_CLK_APB_DISABLE_DATA, CLK_U0_I2STX_4CH_CLK_APB_EN_SHIFT, CLK_U0_I2STX_4CH_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_I2STX_4CH_CLK_APB_ 		saif_get_reg(CLK_U0_I2STX_4CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_CLK_APB_EN_SHIFT, CLK_U0_I2STX_4CH_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_I2STX_4CH_CLK_APB_(x) 		saif_set_reg(CLK_U0_I2STX_4CH_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_I2STX_4CH_CLK_APB_EN_SHIFT, CLK_U0_I2STX_4CH_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_I2STX_4CH0_BCLK_MST_ 			saif_set_reg(CLK_I2STX_4CH0_BCLK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH0_BCLK_MST_ENABLE_DATA, CLK_I2STX_4CH0_BCLK_MST_EN_SHIFT, CLK_I2STX_4CH0_BCLK_MST_EN_MASK)
#define _DISABLE_CLOCK_CLK_I2STX_4CH0_BCLK_MST_ 			saif_set_reg(CLK_I2STX_4CH0_BCLK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH0_BCLK_MST_DISABLE_DATA, CLK_I2STX_4CH0_BCLK_MST_EN_SHIFT, CLK_I2STX_4CH0_BCLK_MST_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_I2STX_4CH0_BCLK_MST_ 		saif_get_reg(CLK_I2STX_4CH0_BCLK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH0_BCLK_MST_EN_SHIFT, CLK_I2STX_4CH0_BCLK_MST_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_I2STX_4CH0_BCLK_MST_(x) 		saif_set_reg(CLK_I2STX_4CH0_BCLK_MST_CTRL_REG_ADDR, x, CLK_I2STX_4CH0_BCLK_MST_EN_SHIFT, CLK_I2STX_4CH0_BCLK_MST_EN_MASK)
#define _DIVIDE_CLOCK_CLK_I2STX_4CH0_BCLK_MST_(div) 			saif_set_reg(CLK_I2STX_4CH0_BCLK_MST_CTRL_REG_ADDR, div, CLK_I2STX_4CH0_BCLK_MST_DIV_SHIFT, CLK_I2STX_4CH0_BCLK_MST_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_I2STX_4CH0_BCLK_MST_ 		saif_get_reg(CLK_I2STX_4CH0_BCLK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH0_BCLK_MST_DIV_SHIFT, CLK_I2STX_4CH0_BCLK_MST_DIV_MASK)
#define _SET_CLOCK_CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_ 		saif_set_reg(CLK_I2STX_4CH0_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_DATA, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_ 		saif_set_reg(CLK_I2STX_4CH0_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2STX_4CH0_BCLK_MST_INV_UN_POLARITY_DATA, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_I2STX_4CH0_BCLK_MST_INV_ 		saif_get_reg(CLK_I2STX_4CH0_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_I2STX_4CH0_BCLK_MST_INV_(x) 		saif_set_reg(CLK_I2STX_4CH0_BCLK_MST_INV_CTRL_REG_ADDR, x, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2STX_4CH0_BCLK_MST_INV_POLARITY_MASK)
#define _SWITCH_CLOCK_CLK_I2STX_4CH0_LRCK_MST_SOURCE_CLK_I2STX_4CH0_BCLK_MST_INV_ 	saif_set_reg(CLK_I2STX_4CH0_LRCK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH0_LRCK_MST_SW_CLK_I2STX_4CH0_BCLK_MST_INV_DATA, CLK_I2STX_4CH0_LRCK_MST_SW_SHIFT, CLK_I2STX_4CH0_LRCK_MST_SW_MASK)
#define _SWITCH_CLOCK_CLK_I2STX_4CH0_LRCK_MST_SOURCE_CLK_I2STX_4CH0_BCLK_MST_ 	saif_set_reg(CLK_I2STX_4CH0_LRCK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH0_LRCK_MST_SW_CLK_I2STX_4CH0_BCLK_MST_DATA, CLK_I2STX_4CH0_LRCK_MST_SW_SHIFT, CLK_I2STX_4CH0_LRCK_MST_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_I2STX_4CH0_LRCK_MST_ 		saif_get_reg(CLK_I2STX_4CH0_LRCK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH0_LRCK_MST_SW_SHIFT, CLK_I2STX_4CH0_LRCK_MST_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_I2STX_4CH0_LRCK_MST_(x) 		saif_set_reg(CLK_I2STX_4CH0_LRCK_MST_CTRL_REG_ADDR, x, CLK_I2STX_4CH0_LRCK_MST_SW_SHIFT, CLK_I2STX_4CH0_LRCK_MST_SW_MASK)
#define _DIVIDE_CLOCK_CLK_I2STX_4CH0_LRCK_MST_(div) 			saif_set_reg(CLK_I2STX_4CH0_LRCK_MST_CTRL_REG_ADDR, div, CLK_I2STX_4CH0_LRCK_MST_DIV_SHIFT, CLK_I2STX_4CH0_LRCK_MST_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_I2STX_4CH0_LRCK_MST_ 		saif_get_reg(CLK_I2STX_4CH0_LRCK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH0_LRCK_MST_DIV_SHIFT, CLK_I2STX_4CH0_LRCK_MST_DIV_MASK)
#define _SWITCH_CLOCK_CLK_U0_I2STX_4CH_BCLK_SOURCE_CLK_I2STX_4CH0_BCLK_MST_ 	saif_set_reg(CLK_U0_I2STX_4CH_BCLK_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_BCLK_SW_CLK_I2STX_4CH0_BCLK_MST_DATA, CLK_U0_I2STX_4CH_BCLK_SW_SHIFT, CLK_U0_I2STX_4CH_BCLK_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_I2STX_4CH_BCLK_SOURCE_CLK_I2STX_BCLK_EXT_ 	saif_set_reg(CLK_U0_I2STX_4CH_BCLK_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_BCLK_SW_CLK_I2STX_BCLK_EXT_DATA, CLK_U0_I2STX_4CH_BCLK_SW_SHIFT, CLK_U0_I2STX_4CH_BCLK_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_I2STX_4CH_BCLK_ 		saif_get_reg(CLK_U0_I2STX_4CH_BCLK_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_BCLK_SW_SHIFT, CLK_U0_I2STX_4CH_BCLK_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_I2STX_4CH_BCLK_(x) 		saif_set_reg(CLK_U0_I2STX_4CH_BCLK_CTRL_REG_ADDR, x, CLK_U0_I2STX_4CH_BCLK_SW_SHIFT, CLK_U0_I2STX_4CH_BCLK_SW_MASK)
#define _SET_CLOCK_CLK_U0_I2STX_4CH_BCLK_N_POLARITY_ 		saif_set_reg(CLK_U0_I2STX_4CH_BCLK_N_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_DATA, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_SHIFT, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_U0_I2STX_4CH_BCLK_N_POLARITY_ 		saif_set_reg(CLK_U0_I2STX_4CH_BCLK_N_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_BCLK_N_UN_POLARITY_DATA, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_SHIFT, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_U0_I2STX_4CH_BCLK_N_ 		saif_get_reg(CLK_U0_I2STX_4CH_BCLK_N_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_SHIFT, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_U0_I2STX_4CH_BCLK_N_(x) 		saif_set_reg(CLK_U0_I2STX_4CH_BCLK_N_CTRL_REG_ADDR, x, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_SHIFT, CLK_U0_I2STX_4CH_BCLK_N_POLARITY_MASK)
#define _SWITCH_CLOCK_CLK_U0_I2STX_4CH_LRCK_SOURCE_CLK_I2STX_4CH0_LRCK_MST_ 	saif_set_reg(CLK_U0_I2STX_4CH_LRCK_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_LRCK_SW_CLK_I2STX_4CH0_LRCK_MST_DATA, CLK_U0_I2STX_4CH_LRCK_SW_SHIFT, CLK_U0_I2STX_4CH_LRCK_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_I2STX_4CH_LRCK_SOURCE_CLK_I2STX_LRCK_EXT_ 	saif_set_reg(CLK_U0_I2STX_4CH_LRCK_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_LRCK_SW_CLK_I2STX_LRCK_EXT_DATA, CLK_U0_I2STX_4CH_LRCK_SW_SHIFT, CLK_U0_I2STX_4CH_LRCK_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_I2STX_4CH_LRCK_ 		saif_get_reg(CLK_U0_I2STX_4CH_LRCK_CTRL_REG_ADDR, CLK_U0_I2STX_4CH_LRCK_SW_SHIFT, CLK_U0_I2STX_4CH_LRCK_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_I2STX_4CH_LRCK_(x) 		saif_set_reg(CLK_U0_I2STX_4CH_LRCK_CTRL_REG_ADDR, x, CLK_U0_I2STX_4CH_LRCK_SW_SHIFT, CLK_U0_I2STX_4CH_LRCK_SW_MASK)
#define _ENABLE_CLOCK_CLK_U1_I2STX_4CH_CLK_APB_ 			saif_set_reg(CLK_U1_I2STX_4CH_CLK_APB_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_CLK_APB_ENABLE_DATA, CLK_U1_I2STX_4CH_CLK_APB_EN_SHIFT, CLK_U1_I2STX_4CH_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U1_I2STX_4CH_CLK_APB_ 			saif_set_reg(CLK_U1_I2STX_4CH_CLK_APB_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_CLK_APB_DISABLE_DATA, CLK_U1_I2STX_4CH_CLK_APB_EN_SHIFT, CLK_U1_I2STX_4CH_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U1_I2STX_4CH_CLK_APB_ 		saif_get_reg(CLK_U1_I2STX_4CH_CLK_APB_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_CLK_APB_EN_SHIFT, CLK_U1_I2STX_4CH_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U1_I2STX_4CH_CLK_APB_(x) 		saif_set_reg(CLK_U1_I2STX_4CH_CLK_APB_CTRL_REG_ADDR, x, CLK_U1_I2STX_4CH_CLK_APB_EN_SHIFT, CLK_U1_I2STX_4CH_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_I2STX_4CH1_BCLK_MST_ 			saif_set_reg(CLK_I2STX_4CH1_BCLK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH1_BCLK_MST_ENABLE_DATA, CLK_I2STX_4CH1_BCLK_MST_EN_SHIFT, CLK_I2STX_4CH1_BCLK_MST_EN_MASK)
#define _DISABLE_CLOCK_CLK_I2STX_4CH1_BCLK_MST_ 			saif_set_reg(CLK_I2STX_4CH1_BCLK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH1_BCLK_MST_DISABLE_DATA, CLK_I2STX_4CH1_BCLK_MST_EN_SHIFT, CLK_I2STX_4CH1_BCLK_MST_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_I2STX_4CH1_BCLK_MST_ 		saif_get_reg(CLK_I2STX_4CH1_BCLK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH1_BCLK_MST_EN_SHIFT, CLK_I2STX_4CH1_BCLK_MST_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_I2STX_4CH1_BCLK_MST_(x) 		saif_set_reg(CLK_I2STX_4CH1_BCLK_MST_CTRL_REG_ADDR, x, CLK_I2STX_4CH1_BCLK_MST_EN_SHIFT, CLK_I2STX_4CH1_BCLK_MST_EN_MASK)
#define _DIVIDE_CLOCK_CLK_I2STX_4CH1_BCLK_MST_(div) 			saif_set_reg(CLK_I2STX_4CH1_BCLK_MST_CTRL_REG_ADDR, div, CLK_I2STX_4CH1_BCLK_MST_DIV_SHIFT, CLK_I2STX_4CH1_BCLK_MST_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_I2STX_4CH1_BCLK_MST_ 		saif_get_reg(CLK_I2STX_4CH1_BCLK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH1_BCLK_MST_DIV_SHIFT, CLK_I2STX_4CH1_BCLK_MST_DIV_MASK)
#define _SET_CLOCK_CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_ 		saif_set_reg(CLK_I2STX_4CH1_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_DATA, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_ 		saif_set_reg(CLK_I2STX_4CH1_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2STX_4CH1_BCLK_MST_INV_UN_POLARITY_DATA, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_I2STX_4CH1_BCLK_MST_INV_ 		saif_get_reg(CLK_I2STX_4CH1_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_I2STX_4CH1_BCLK_MST_INV_(x) 		saif_set_reg(CLK_I2STX_4CH1_BCLK_MST_INV_CTRL_REG_ADDR, x, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2STX_4CH1_BCLK_MST_INV_POLARITY_MASK)
#define _SWITCH_CLOCK_CLK_I2STX_4CH1_LRCK_MST_SOURCE_CLK_I2STX_4CH1_BCLK_MST_INV_ 	saif_set_reg(CLK_I2STX_4CH1_LRCK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH1_LRCK_MST_SW_CLK_I2STX_4CH1_BCLK_MST_INV_DATA, CLK_I2STX_4CH1_LRCK_MST_SW_SHIFT, CLK_I2STX_4CH1_LRCK_MST_SW_MASK)
#define _SWITCH_CLOCK_CLK_I2STX_4CH1_LRCK_MST_SOURCE_CLK_I2STX_4CH1_BCLK_MST_ 	saif_set_reg(CLK_I2STX_4CH1_LRCK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH1_LRCK_MST_SW_CLK_I2STX_4CH1_BCLK_MST_DATA, CLK_I2STX_4CH1_LRCK_MST_SW_SHIFT, CLK_I2STX_4CH1_LRCK_MST_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_I2STX_4CH1_LRCK_MST_ 		saif_get_reg(CLK_I2STX_4CH1_LRCK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH1_LRCK_MST_SW_SHIFT, CLK_I2STX_4CH1_LRCK_MST_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_I2STX_4CH1_LRCK_MST_(x) 		saif_set_reg(CLK_I2STX_4CH1_LRCK_MST_CTRL_REG_ADDR, x, CLK_I2STX_4CH1_LRCK_MST_SW_SHIFT, CLK_I2STX_4CH1_LRCK_MST_SW_MASK)
#define _DIVIDE_CLOCK_CLK_I2STX_4CH1_LRCK_MST_(div) 			saif_set_reg(CLK_I2STX_4CH1_LRCK_MST_CTRL_REG_ADDR, div, CLK_I2STX_4CH1_LRCK_MST_DIV_SHIFT, CLK_I2STX_4CH1_LRCK_MST_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_I2STX_4CH1_LRCK_MST_ 		saif_get_reg(CLK_I2STX_4CH1_LRCK_MST_CTRL_REG_ADDR, CLK_I2STX_4CH1_LRCK_MST_DIV_SHIFT, CLK_I2STX_4CH1_LRCK_MST_DIV_MASK)
#define _SWITCH_CLOCK_CLK_U1_I2STX_4CH_BCLK_SOURCE_CLK_I2STX_4CH1_BCLK_MST_ 	saif_set_reg(CLK_U1_I2STX_4CH_BCLK_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_BCLK_SW_CLK_I2STX_4CH1_BCLK_MST_DATA, CLK_U1_I2STX_4CH_BCLK_SW_SHIFT, CLK_U1_I2STX_4CH_BCLK_SW_MASK)
#define _SWITCH_CLOCK_CLK_U1_I2STX_4CH_BCLK_SOURCE_CLK_I2STX_BCLK_EXT_ 	saif_set_reg(CLK_U1_I2STX_4CH_BCLK_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_BCLK_SW_CLK_I2STX_BCLK_EXT_DATA, CLK_U1_I2STX_4CH_BCLK_SW_SHIFT, CLK_U1_I2STX_4CH_BCLK_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U1_I2STX_4CH_BCLK_ 		saif_get_reg(CLK_U1_I2STX_4CH_BCLK_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_BCLK_SW_SHIFT, CLK_U1_I2STX_4CH_BCLK_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U1_I2STX_4CH_BCLK_(x) 		saif_set_reg(CLK_U1_I2STX_4CH_BCLK_CTRL_REG_ADDR, x, CLK_U1_I2STX_4CH_BCLK_SW_SHIFT, CLK_U1_I2STX_4CH_BCLK_SW_MASK)
#define _SET_CLOCK_CLK_U1_I2STX_4CH_BCLK_N_POLARITY_ 		saif_set_reg(CLK_U1_I2STX_4CH_BCLK_N_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_DATA, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_SHIFT, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_U1_I2STX_4CH_BCLK_N_POLARITY_ 		saif_set_reg(CLK_U1_I2STX_4CH_BCLK_N_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_BCLK_N_UN_POLARITY_DATA, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_SHIFT, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_U1_I2STX_4CH_BCLK_N_ 		saif_get_reg(CLK_U1_I2STX_4CH_BCLK_N_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_SHIFT, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_U1_I2STX_4CH_BCLK_N_(x) 		saif_set_reg(CLK_U1_I2STX_4CH_BCLK_N_CTRL_REG_ADDR, x, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_SHIFT, CLK_U1_I2STX_4CH_BCLK_N_POLARITY_MASK)
#define _SWITCH_CLOCK_CLK_U1_I2STX_4CH_LRCK_SOURCE_CLK_I2STX_4CH1_LRCK_MST_ 	saif_set_reg(CLK_U1_I2STX_4CH_LRCK_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_LRCK_SW_CLK_I2STX_4CH1_LRCK_MST_DATA, CLK_U1_I2STX_4CH_LRCK_SW_SHIFT, CLK_U1_I2STX_4CH_LRCK_SW_MASK)
#define _SWITCH_CLOCK_CLK_U1_I2STX_4CH_LRCK_SOURCE_CLK_I2STX_LRCK_EXT_ 	saif_set_reg(CLK_U1_I2STX_4CH_LRCK_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_LRCK_SW_CLK_I2STX_LRCK_EXT_DATA, CLK_U1_I2STX_4CH_LRCK_SW_SHIFT, CLK_U1_I2STX_4CH_LRCK_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U1_I2STX_4CH_LRCK_ 		saif_get_reg(CLK_U1_I2STX_4CH_LRCK_CTRL_REG_ADDR, CLK_U1_I2STX_4CH_LRCK_SW_SHIFT, CLK_U1_I2STX_4CH_LRCK_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U1_I2STX_4CH_LRCK_(x) 		saif_set_reg(CLK_U1_I2STX_4CH_LRCK_CTRL_REG_ADDR, x, CLK_U1_I2STX_4CH_LRCK_SW_SHIFT, CLK_U1_I2STX_4CH_LRCK_SW_MASK)
#define _ENABLE_CLOCK_CLK_U0_I2SRX_3CH_CLK_APB_ 			saif_set_reg(CLK_U0_I2SRX_3CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_CLK_APB_ENABLE_DATA, CLK_U0_I2SRX_3CH_CLK_APB_EN_SHIFT, CLK_U0_I2SRX_3CH_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_I2SRX_3CH_CLK_APB_ 			saif_set_reg(CLK_U0_I2SRX_3CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_CLK_APB_DISABLE_DATA, CLK_U0_I2SRX_3CH_CLK_APB_EN_SHIFT, CLK_U0_I2SRX_3CH_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_I2SRX_3CH_CLK_APB_ 		saif_get_reg(CLK_U0_I2SRX_3CH_CLK_APB_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_CLK_APB_EN_SHIFT, CLK_U0_I2SRX_3CH_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_I2SRX_3CH_CLK_APB_(x) 		saif_set_reg(CLK_U0_I2SRX_3CH_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_I2SRX_3CH_CLK_APB_EN_SHIFT, CLK_U0_I2SRX_3CH_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_I2SRX_3CH_BCLK_MST_ 			saif_set_reg(CLK_I2SRX_3CH_BCLK_MST_CTRL_REG_ADDR, CLK_I2SRX_3CH_BCLK_MST_ENABLE_DATA, CLK_I2SRX_3CH_BCLK_MST_EN_SHIFT, CLK_I2SRX_3CH_BCLK_MST_EN_MASK)
#define _DISABLE_CLOCK_CLK_I2SRX_3CH_BCLK_MST_ 			saif_set_reg(CLK_I2SRX_3CH_BCLK_MST_CTRL_REG_ADDR, CLK_I2SRX_3CH_BCLK_MST_DISABLE_DATA, CLK_I2SRX_3CH_BCLK_MST_EN_SHIFT, CLK_I2SRX_3CH_BCLK_MST_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_I2SRX_3CH_BCLK_MST_ 		saif_get_reg(CLK_I2SRX_3CH_BCLK_MST_CTRL_REG_ADDR, CLK_I2SRX_3CH_BCLK_MST_EN_SHIFT, CLK_I2SRX_3CH_BCLK_MST_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_I2SRX_3CH_BCLK_MST_(x) 		saif_set_reg(CLK_I2SRX_3CH_BCLK_MST_CTRL_REG_ADDR, x, CLK_I2SRX_3CH_BCLK_MST_EN_SHIFT, CLK_I2SRX_3CH_BCLK_MST_EN_MASK)
#define _DIVIDE_CLOCK_CLK_I2SRX_3CH_BCLK_MST_(div) 			saif_set_reg(CLK_I2SRX_3CH_BCLK_MST_CTRL_REG_ADDR, div, CLK_I2SRX_3CH_BCLK_MST_DIV_SHIFT, CLK_I2SRX_3CH_BCLK_MST_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_I2SRX_3CH_BCLK_MST_ 		saif_get_reg(CLK_I2SRX_3CH_BCLK_MST_CTRL_REG_ADDR, CLK_I2SRX_3CH_BCLK_MST_DIV_SHIFT, CLK_I2SRX_3CH_BCLK_MST_DIV_MASK)
#define _SET_CLOCK_CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_ 		saif_set_reg(CLK_I2SRX_3CH_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_DATA, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_ 		saif_set_reg(CLK_I2SRX_3CH_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2SRX_3CH_BCLK_MST_INV_UN_POLARITY_DATA, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_I2SRX_3CH_BCLK_MST_INV_ 		saif_get_reg(CLK_I2SRX_3CH_BCLK_MST_INV_CTRL_REG_ADDR, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_I2SRX_3CH_BCLK_MST_INV_(x) 		saif_set_reg(CLK_I2SRX_3CH_BCLK_MST_INV_CTRL_REG_ADDR, x, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_SHIFT, CLK_I2SRX_3CH_BCLK_MST_INV_POLARITY_MASK)
#define _SWITCH_CLOCK_CLK_I2SRX_3CH_LRCK_MST_SOURCE_CLK_I2SRX_3CH_BCLK_MST_INV_ 	saif_set_reg(CLK_I2SRX_3CH_LRCK_MST_CTRL_REG_ADDR, CLK_I2SRX_3CH_LRCK_MST_SW_CLK_I2SRX_3CH_BCLK_MST_INV_DATA, CLK_I2SRX_3CH_LRCK_MST_SW_SHIFT, CLK_I2SRX_3CH_LRCK_MST_SW_MASK)
#define _SWITCH_CLOCK_CLK_I2SRX_3CH_LRCK_MST_SOURCE_CLK_I2SRX_3CH_BCLK_MST_ 	saif_set_reg(CLK_I2SRX_3CH_LRCK_MST_CTRL_REG_ADDR, CLK_I2SRX_3CH_LRCK_MST_SW_CLK_I2SRX_3CH_BCLK_MST_DATA, CLK_I2SRX_3CH_LRCK_MST_SW_SHIFT, CLK_I2SRX_3CH_LRCK_MST_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_I2SRX_3CH_LRCK_MST_ 		saif_get_reg(CLK_I2SRX_3CH_LRCK_MST_CTRL_REG_ADDR, CLK_I2SRX_3CH_LRCK_MST_SW_SHIFT, CLK_I2SRX_3CH_LRCK_MST_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_I2SRX_3CH_LRCK_MST_(x) 		saif_set_reg(CLK_I2SRX_3CH_LRCK_MST_CTRL_REG_ADDR, x, CLK_I2SRX_3CH_LRCK_MST_SW_SHIFT, CLK_I2SRX_3CH_LRCK_MST_SW_MASK)
#define _DIVIDE_CLOCK_CLK_I2SRX_3CH_LRCK_MST_(div) 			saif_set_reg(CLK_I2SRX_3CH_LRCK_MST_CTRL_REG_ADDR, div, CLK_I2SRX_3CH_LRCK_MST_DIV_SHIFT, CLK_I2SRX_3CH_LRCK_MST_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_I2SRX_3CH_LRCK_MST_ 		saif_get_reg(CLK_I2SRX_3CH_LRCK_MST_CTRL_REG_ADDR, CLK_I2SRX_3CH_LRCK_MST_DIV_SHIFT, CLK_I2SRX_3CH_LRCK_MST_DIV_MASK)
#define _SWITCH_CLOCK_CLK_U0_I2SRX_3CH_BCLK_SOURCE_CLK_I2SRX_3CH_BCLK_MST_ 	saif_set_reg(CLK_U0_I2SRX_3CH_BCLK_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_BCLK_SW_CLK_I2SRX_3CH_BCLK_MST_DATA, CLK_U0_I2SRX_3CH_BCLK_SW_SHIFT, CLK_U0_I2SRX_3CH_BCLK_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_I2SRX_3CH_BCLK_SOURCE_CLK_I2SRX_BCLK_EXT_ 	saif_set_reg(CLK_U0_I2SRX_3CH_BCLK_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_BCLK_SW_CLK_I2SRX_BCLK_EXT_DATA, CLK_U0_I2SRX_3CH_BCLK_SW_SHIFT, CLK_U0_I2SRX_3CH_BCLK_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_I2SRX_3CH_BCLK_ 		saif_get_reg(CLK_U0_I2SRX_3CH_BCLK_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_BCLK_SW_SHIFT, CLK_U0_I2SRX_3CH_BCLK_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_I2SRX_3CH_BCLK_(x) 		saif_set_reg(CLK_U0_I2SRX_3CH_BCLK_CTRL_REG_ADDR, x, CLK_U0_I2SRX_3CH_BCLK_SW_SHIFT, CLK_U0_I2SRX_3CH_BCLK_SW_MASK)
#define _SET_CLOCK_CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_ 		saif_set_reg(CLK_U0_I2SRX_3CH_BCLK_N_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_DATA, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_SHIFT, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_ 		saif_set_reg(CLK_U0_I2SRX_3CH_BCLK_N_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_BCLK_N_UN_POLARITY_DATA, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_SHIFT, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_U0_I2SRX_3CH_BCLK_N_ 		saif_get_reg(CLK_U0_I2SRX_3CH_BCLK_N_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_SHIFT, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_U0_I2SRX_3CH_BCLK_N_(x) 		saif_set_reg(CLK_U0_I2SRX_3CH_BCLK_N_CTRL_REG_ADDR, x, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_SHIFT, CLK_U0_I2SRX_3CH_BCLK_N_POLARITY_MASK)
#define _SWITCH_CLOCK_CLK_U0_I2SRX_3CH_LRCK_SOURCE_CLK_I2SRX_3CH_LRCK_MST_ 	saif_set_reg(CLK_U0_I2SRX_3CH_LRCK_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_LRCK_SW_CLK_I2SRX_3CH_LRCK_MST_DATA, CLK_U0_I2SRX_3CH_LRCK_SW_SHIFT, CLK_U0_I2SRX_3CH_LRCK_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_I2SRX_3CH_LRCK_SOURCE_CLK_I2SRX_LRCK_EXT_ 	saif_set_reg(CLK_U0_I2SRX_3CH_LRCK_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_LRCK_SW_CLK_I2SRX_LRCK_EXT_DATA, CLK_U0_I2SRX_3CH_LRCK_SW_SHIFT, CLK_U0_I2SRX_3CH_LRCK_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_I2SRX_3CH_LRCK_ 		saif_get_reg(CLK_U0_I2SRX_3CH_LRCK_CTRL_REG_ADDR, CLK_U0_I2SRX_3CH_LRCK_SW_SHIFT, CLK_U0_I2SRX_3CH_LRCK_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_I2SRX_3CH_LRCK_(x) 		saif_set_reg(CLK_U0_I2SRX_3CH_LRCK_CTRL_REG_ADDR, x, CLK_U0_I2SRX_3CH_LRCK_SW_SHIFT, CLK_U0_I2SRX_3CH_LRCK_SW_MASK)
#define _ENABLE_CLOCK_CLK_U0_PDM_4MIC_CLK_DMIC_ 			saif_set_reg(CLK_U0_PDM_4MIC_CLK_DMIC_CTRL_REG_ADDR, CLK_U0_PDM_4MIC_CLK_DMIC_ENABLE_DATA, CLK_U0_PDM_4MIC_CLK_DMIC_EN_SHIFT, CLK_U0_PDM_4MIC_CLK_DMIC_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_PDM_4MIC_CLK_DMIC_ 			saif_set_reg(CLK_U0_PDM_4MIC_CLK_DMIC_CTRL_REG_ADDR, CLK_U0_PDM_4MIC_CLK_DMIC_DISABLE_DATA, CLK_U0_PDM_4MIC_CLK_DMIC_EN_SHIFT, CLK_U0_PDM_4MIC_CLK_DMIC_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_PDM_4MIC_CLK_DMIC_ 		saif_get_reg(CLK_U0_PDM_4MIC_CLK_DMIC_CTRL_REG_ADDR, CLK_U0_PDM_4MIC_CLK_DMIC_EN_SHIFT, CLK_U0_PDM_4MIC_CLK_DMIC_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_PDM_4MIC_CLK_DMIC_(x) 		saif_set_reg(CLK_U0_PDM_4MIC_CLK_DMIC_CTRL_REG_ADDR, x, CLK_U0_PDM_4MIC_CLK_DMIC_EN_SHIFT, CLK_U0_PDM_4MIC_CLK_DMIC_EN_MASK)
#define _DIVIDE_CLOCK_CLK_U0_PDM_4MIC_CLK_DMIC_(div) 			saif_set_reg(CLK_U0_PDM_4MIC_CLK_DMIC_CTRL_REG_ADDR, div, CLK_U0_PDM_4MIC_CLK_DMIC_DIV_SHIFT, CLK_U0_PDM_4MIC_CLK_DMIC_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_PDM_4MIC_CLK_DMIC_ 		saif_get_reg(CLK_U0_PDM_4MIC_CLK_DMIC_CTRL_REG_ADDR, CLK_U0_PDM_4MIC_CLK_DMIC_DIV_SHIFT, CLK_U0_PDM_4MIC_CLK_DMIC_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_PDM_4MIC_CLK_APB_ 			saif_set_reg(CLK_U0_PDM_4MIC_CLK_APB_CTRL_REG_ADDR, CLK_U0_PDM_4MIC_CLK_APB_ENABLE_DATA, CLK_U0_PDM_4MIC_CLK_APB_EN_SHIFT, CLK_U0_PDM_4MIC_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_PDM_4MIC_CLK_APB_ 			saif_set_reg(CLK_U0_PDM_4MIC_CLK_APB_CTRL_REG_ADDR, CLK_U0_PDM_4MIC_CLK_APB_DISABLE_DATA, CLK_U0_PDM_4MIC_CLK_APB_EN_SHIFT, CLK_U0_PDM_4MIC_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_PDM_4MIC_CLK_APB_ 		saif_get_reg(CLK_U0_PDM_4MIC_CLK_APB_CTRL_REG_ADDR, CLK_U0_PDM_4MIC_CLK_APB_EN_SHIFT, CLK_U0_PDM_4MIC_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_PDM_4MIC_CLK_APB_(x) 		saif_set_reg(CLK_U0_PDM_4MIC_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_PDM_4MIC_CLK_APB_EN_SHIFT, CLK_U0_PDM_4MIC_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_TDM16SLOT_CLK_AHB_ 			saif_set_reg(CLK_U0_TDM16SLOT_CLK_AHB_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_AHB_ENABLE_DATA, CLK_U0_TDM16SLOT_CLK_AHB_EN_SHIFT, CLK_U0_TDM16SLOT_CLK_AHB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_TDM16SLOT_CLK_AHB_ 			saif_set_reg(CLK_U0_TDM16SLOT_CLK_AHB_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_AHB_DISABLE_DATA, CLK_U0_TDM16SLOT_CLK_AHB_EN_SHIFT, CLK_U0_TDM16SLOT_CLK_AHB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_TDM16SLOT_CLK_AHB_ 		saif_get_reg(CLK_U0_TDM16SLOT_CLK_AHB_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_AHB_EN_SHIFT, CLK_U0_TDM16SLOT_CLK_AHB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_TDM16SLOT_CLK_AHB_(x) 		saif_set_reg(CLK_U0_TDM16SLOT_CLK_AHB_CTRL_REG_ADDR, x, CLK_U0_TDM16SLOT_CLK_AHB_EN_SHIFT, CLK_U0_TDM16SLOT_CLK_AHB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_TDM16SLOT_CLK_APB_ 			saif_set_reg(CLK_U0_TDM16SLOT_CLK_APB_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_APB_ENABLE_DATA, CLK_U0_TDM16SLOT_CLK_APB_EN_SHIFT, CLK_U0_TDM16SLOT_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_TDM16SLOT_CLK_APB_ 			saif_set_reg(CLK_U0_TDM16SLOT_CLK_APB_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_APB_DISABLE_DATA, CLK_U0_TDM16SLOT_CLK_APB_EN_SHIFT, CLK_U0_TDM16SLOT_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_TDM16SLOT_CLK_APB_ 		saif_get_reg(CLK_U0_TDM16SLOT_CLK_APB_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_APB_EN_SHIFT, CLK_U0_TDM16SLOT_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_TDM16SLOT_CLK_APB_(x) 		saif_set_reg(CLK_U0_TDM16SLOT_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_TDM16SLOT_CLK_APB_EN_SHIFT, CLK_U0_TDM16SLOT_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_TDM_INTERNAL_ 			saif_set_reg(CLK_TDM_INTERNAL_CTRL_REG_ADDR, CLK_TDM_INTERNAL_ENABLE_DATA, CLK_TDM_INTERNAL_EN_SHIFT, CLK_TDM_INTERNAL_EN_MASK)
#define _DISABLE_CLOCK_CLK_TDM_INTERNAL_ 			saif_set_reg(CLK_TDM_INTERNAL_CTRL_REG_ADDR, CLK_TDM_INTERNAL_DISABLE_DATA, CLK_TDM_INTERNAL_EN_SHIFT, CLK_TDM_INTERNAL_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_TDM_INTERNAL_ 		saif_get_reg(CLK_TDM_INTERNAL_CTRL_REG_ADDR, CLK_TDM_INTERNAL_EN_SHIFT, CLK_TDM_INTERNAL_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_TDM_INTERNAL_(x) 		saif_set_reg(CLK_TDM_INTERNAL_CTRL_REG_ADDR, x, CLK_TDM_INTERNAL_EN_SHIFT, CLK_TDM_INTERNAL_EN_MASK)
#define _DIVIDE_CLOCK_CLK_TDM_INTERNAL_(div) 			saif_set_reg(CLK_TDM_INTERNAL_CTRL_REG_ADDR, div, CLK_TDM_INTERNAL_DIV_SHIFT, CLK_TDM_INTERNAL_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_TDM_INTERNAL_ 		saif_get_reg(CLK_TDM_INTERNAL_CTRL_REG_ADDR, CLK_TDM_INTERNAL_DIV_SHIFT, CLK_TDM_INTERNAL_DIV_MASK)
#define _SWITCH_CLOCK_CLK_U0_TDM16SLOT_CLK_TDM_SOURCE_CLK_TDM_INTERNAL_ 	saif_set_reg(CLK_U0_TDM16SLOT_CLK_TDM_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_TDM_SW_CLK_TDM_INTERNAL_DATA, CLK_U0_TDM16SLOT_CLK_TDM_SW_SHIFT, CLK_U0_TDM16SLOT_CLK_TDM_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_TDM16SLOT_CLK_TDM_SOURCE_CLK_TDM_EXT_ 	saif_set_reg(CLK_U0_TDM16SLOT_CLK_TDM_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_TDM_SW_CLK_TDM_EXT_DATA, CLK_U0_TDM16SLOT_CLK_TDM_SW_SHIFT, CLK_U0_TDM16SLOT_CLK_TDM_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_TDM16SLOT_CLK_TDM_ 		saif_get_reg(CLK_U0_TDM16SLOT_CLK_TDM_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_TDM_SW_SHIFT, CLK_U0_TDM16SLOT_CLK_TDM_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_TDM16SLOT_CLK_TDM_(x) 		saif_set_reg(CLK_U0_TDM16SLOT_CLK_TDM_CTRL_REG_ADDR, x, CLK_U0_TDM16SLOT_CLK_TDM_SW_SHIFT, CLK_U0_TDM16SLOT_CLK_TDM_SW_MASK)
#define _SET_CLOCK_CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_ 		saif_set_reg(CLK_U0_TDM16SLOT_CLK_TDM_N_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_DATA, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_SHIFT, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_MASK)
#define _UNSET_CLOCK_CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_ 		saif_set_reg(CLK_U0_TDM16SLOT_CLK_TDM_N_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_TDM_N_UN_POLARITY_DATA, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_SHIFT, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_MASK)
#define _GET_CLOCK_POLARITY_STATUS_CLK_U0_TDM16SLOT_CLK_TDM_N_ 		saif_get_reg(CLK_U0_TDM16SLOT_CLK_TDM_N_CTRL_REG_ADDR, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_SHIFT, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_MASK)
#define _SET_CLOCK_POLARITY_STATUS_CLK_U0_TDM16SLOT_CLK_TDM_N_(x) 		saif_set_reg(CLK_U0_TDM16SLOT_CLK_TDM_N_CTRL_REG_ADDR, x, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_SHIFT, CLK_U0_TDM16SLOT_CLK_TDM_N_POLARITY_MASK)
#define _DIVIDE_CLOCK_CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_(div) 			saif_set_reg(CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_CTRL_REG_ADDR, div, CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_DIV_SHIFT, CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_ 		saif_get_reg(CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_CTRL_REG_ADDR, CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_DIV_SHIFT, CLK_U0_JTAG_CERTIFICATION_TRNG_CLK_DIV_MASK)


#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_JTAG2APB_PRESETN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_JTAG2APB_PRESETN_SHIFT, RSTN_U0_JTAG2APB_PRESETN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_JTAG2APB_PRESETN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_JTAG2APB_PRESETN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_JTAG2APB_PRESETN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_JTAG2APB_PRESETN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SYS_SYSCON_PRESETN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SYS_SYSCON_PRESETN_SHIFT, RSTN_U0_SYS_SYSCON_PRESETN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SYS_SYSCON_PRESETN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SYS_SYSCON_PRESETN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SYS_SYSCON_PRESETN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SYS_SYSCON_PRESETN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SYS_IOMUX_PRESETN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SYS_IOMUX_PRESETN_SHIFT, RSTN_U0_SYS_IOMUX_PRESETN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SYS_IOMUX_PRESETN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SYS_IOMUX_PRESETN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SYS_IOMUX_PRESETN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SYS_IOMUX_PRESETN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_BUS_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_BUS_SHIFT, RST_U0_U7MC_SFT7110_RST_BUS_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_BUS_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_BUS_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_BUS_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_BUS_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_DEBUG_RESET_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_DEBUG_RESET_SHIFT, RST_U0_U7MC_SFT7110_DEBUG_RESET_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_DEBUG_RESET_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_DEBUG_RESET_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_DEBUG_RESET_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_DEBUG_RESET_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE0_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE0_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE0_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE0_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE0_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE0_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE0_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE1_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE1_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE1_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE1_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE1_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE1_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE1_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE2_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE2_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE2_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE2_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE2_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE2_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE2_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE3_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE3_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE3_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE3_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE3_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE3_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE3_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE4_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE4_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE4_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE4_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE4_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE4_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE4_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE0_ST_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE0_ST_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE0_ST_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE0_ST_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE0_ST_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE0_ST_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE0_ST_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE1_ST_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE1_ST_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE1_ST_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE1_ST_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE1_ST_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE1_ST_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE1_ST_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE2_ST_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE2_ST_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE2_ST_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE2_ST_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE2_ST_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE2_ST_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE2_ST_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE3_ST_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE3_ST_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE3_ST_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE3_ST_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE3_ST_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE3_ST_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE3_ST_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE4_ST_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE4_ST_SHIFT, RST_U0_U7MC_SFT7110_RST_CORE4_ST_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE4_ST_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE4_ST_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_RST_CORE4_ST_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_RST_CORE4_ST_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST0_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST0_SHIFT, RST_U0_U7MC_SFT7110_TRACE_RST0_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST0_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST0_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST0_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST0_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST1_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST1_SHIFT, RST_U0_U7MC_SFT7110_TRACE_RST1_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST1_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST1_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST1_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST1_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST2_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST2_SHIFT, RST_U0_U7MC_SFT7110_TRACE_RST2_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST2_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST2_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST2_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST2_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST3_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST3_SHIFT, RST_U0_U7MC_SFT7110_TRACE_RST3_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST3_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST3_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST3_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST3_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST4_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST4_SHIFT, RST_U0_U7MC_SFT7110_TRACE_RST4_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST4_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST4_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_RST4_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_RST4_MASK)
#define _READ_RESET_STATUS_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_COM_RST_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_COM_RST_SHIFT, RST_U0_U7MC_SFT7110_TRACE_COM_RST_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_COM_RST_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_COM_RST_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_U7MC_SFT7110_TRACE_COM_RST_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RST_U0_U7MC_SFT7110_TRACE_COM_RST_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_IMG_GPU_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_IMG_GPU_RSTN_APB_SHIFT, RSTN_U0_IMG_GPU_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_IMG_GPU_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_IMG_GPU_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_IMG_GPU_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_IMG_GPU_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_IMG_GPU_RSTN_DOMA_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_IMG_GPU_RSTN_DOMA_SHIFT, RSTN_U0_IMG_GPU_RSTN_DOMA_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_IMG_GPU_RSTN_DOMA_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_IMG_GPU_RSTN_DOMA_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_IMG_GPU_RSTN_DOMA_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_IMG_GPU_RSTN_DOMA_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_APB_BUS_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_AXICFG0_AXI_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_CPU_AXI_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_DISP_AXI_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_GPU_AXI_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_ISP_AXI_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_DDRC_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_STG_AXI_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_VDEC_AXI_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_SHIFT, RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_SFT7110_NOC_BUS_RESET_VENC_AXI_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_SHIFT, RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG1_DEC_RSTN_AHB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_SHIFT, RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG1_DEC_RSTN_MAIN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_SHIFT, RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_SHIFT, RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_MAIN_DIV_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_SHIFT, RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXI_CFG0_DEC_RSTN_HIFI4_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_AXI_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_AXI_SHIFT, RSTN_U0_DDR_SFT7110_RSTN_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_AXI_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_AXI_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_OSC_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_OSC_SHIFT, RSTN_U0_DDR_SFT7110_RSTN_OSC_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_OSC_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_OSC_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_OSC_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_OSC_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_APB_SHIFT, RSTN_U0_DDR_SFT7110_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DDR_SFT7110_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DDR_SFT7110_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_SHIFT, RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_IP_TOP_RESET_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_SHIFT, RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_ISP_TOP_RSTN_DOM_ISP_TOP_RSTN_ISP_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_SHIFT, RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_DOM_VOUT_TOP_RSTN_DOM_VOUT_TOP_RSTN_VOUT_SRC_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CODAJ12_RSTN_AXI_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_AXI_SHIFT, RSTN_U0_CODAJ12_RSTN_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CODAJ12_RSTN_AXI_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CODAJ12_RSTN_AXI_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CODAJ12_RSTN_CORE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_CORE_SHIFT, RSTN_U0_CODAJ12_RSTN_CORE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CODAJ12_RSTN_CORE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CODAJ12_RSTN_CORE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_CORE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CODAJ12_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_APB_SHIFT, RSTN_U0_CODAJ12_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CODAJ12_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CODAJ12_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CODAJ12_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_WAVE511_RSTN_AXI_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_AXI_SHIFT, RSTN_U0_WAVE511_RSTN_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_WAVE511_RSTN_AXI_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_WAVE511_RSTN_AXI_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_WAVE511_RSTN_BPU_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_BPU_SHIFT, RSTN_U0_WAVE511_RSTN_BPU_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_WAVE511_RSTN_BPU_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_BPU_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_WAVE511_RSTN_BPU_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_BPU_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_WAVE511_RSTN_VCE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_VCE_SHIFT, RSTN_U0_WAVE511_RSTN_VCE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_WAVE511_RSTN_VCE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_VCE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_WAVE511_RSTN_VCE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_VCE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_WAVE511_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_APB_SHIFT, RSTN_U0_WAVE511_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_WAVE511_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_WAVE511_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE511_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_VDEC_JPG_ARB_JPGRESETN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_VDEC_JPG_ARB_JPGRESETN_SHIFT, RSTN_U0_VDEC_JPG_ARB_JPGRESETN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_VDEC_JPG_ARB_JPGRESETN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_VDEC_JPG_ARB_JPGRESETN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_VDEC_JPG_ARB_JPGRESETN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_VDEC_JPG_ARB_JPGRESETN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_VDEC_JPG_ARB_MAINRESETN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_VDEC_JPG_ARB_MAINRESETN_SHIFT, RSTN_U0_VDEC_JPG_ARB_MAINRESETN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_VDEC_JPG_ARB_MAINRESETN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_VDEC_JPG_ARB_MAINRESETN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_VDEC_JPG_ARB_MAINRESETN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_VDEC_JPG_ARB_MAINRESETN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_AXIMEM_128B_RSTN_AXI_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXIMEM_128B_RSTN_AXI_SHIFT, RSTN_U0_AXIMEM_128B_RSTN_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_AXIMEM_128B_RSTN_AXI_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXIMEM_128B_RSTN_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_AXIMEM_128B_RSTN_AXI_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_AXIMEM_128B_RSTN_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_WAVE420L_RSTN_AXI_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_AXI_SHIFT, RSTN_U0_WAVE420L_RSTN_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_WAVE420L_RSTN_AXI_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_WAVE420L_RSTN_AXI_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_WAVE420L_RSTN_BPU_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_BPU_SHIFT, RSTN_U0_WAVE420L_RSTN_BPU_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_WAVE420L_RSTN_BPU_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_BPU_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_WAVE420L_RSTN_BPU_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_BPU_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_WAVE420L_RSTN_VCE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_VCE_SHIFT, RSTN_U0_WAVE420L_RSTN_VCE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_WAVE420L_RSTN_VCE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_VCE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_WAVE420L_RSTN_VCE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_VCE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_WAVE420L_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_APB_SHIFT, RSTN_U0_WAVE420L_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_WAVE420L_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_WAVE420L_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_WAVE420L_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_AXIMEM_128B_RSTN_AXI_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U1_AXIMEM_128B_RSTN_AXI_SHIFT, RSTN_U1_AXIMEM_128B_RSTN_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_AXIMEM_128B_RSTN_AXI_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U1_AXIMEM_128B_RSTN_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_AXIMEM_128B_RSTN_AXI_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U1_AXIMEM_128B_RSTN_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U2_AXIMEM_128B_RSTN_AXI_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U2_AXIMEM_128B_RSTN_AXI_SHIFT, RSTN_U2_AXIMEM_128B_RSTN_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U2_AXIMEM_128B_RSTN_AXI_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U2_AXIMEM_128B_RSTN_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U2_AXIMEM_128B_RSTN_AXI_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U2_AXIMEM_128B_RSTN_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_SHIFT, RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_INTMEM_ROM_SRAM_RSTN_ROM_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_AHB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_AHB_SHIFT, RSTN_U0_CDNS_QSPI_RSTN_AHB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_AHB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_AHB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_AHB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_AHB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_APB_SHIFT, RSTN_U0_CDNS_QSPI_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_REF_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_REF_SHIFT, RSTN_U0_CDNS_QSPI_RSTN_REF_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_REF_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_REF_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_QSPI_RSTN_REF_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT1_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS1_REG_ADDR, RSTN_U0_CDNS_QSPI_RSTN_REF_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DW_SDIO_RSTN_AHB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_SDIO_RSTN_AHB_SHIFT, RSTN_U0_DW_SDIO_RSTN_AHB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DW_SDIO_RSTN_AHB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_SDIO_RSTN_AHB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DW_SDIO_RSTN_AHB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_SDIO_RSTN_AHB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_DW_SDIO_RSTN_AHB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_SDIO_RSTN_AHB_SHIFT, RSTN_U1_DW_SDIO_RSTN_AHB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_DW_SDIO_RSTN_AHB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_SDIO_RSTN_AHB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_DW_SDIO_RSTN_AHB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_SDIO_RSTN_AHB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_SHIFT, RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_GMAC5_AXI64_ARESETN_I_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_DW_GMAC5_AXI64_HRESET_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_GMAC5_AXI64_HRESET_N_SHIFT, RSTN_U1_DW_GMAC5_AXI64_HRESET_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_DW_GMAC5_AXI64_HRESET_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_GMAC5_AXI64_HRESET_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_DW_GMAC5_AXI64_HRESET_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_GMAC5_AXI64_HRESET_N_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_MAILBOX_PRESETN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_MAILBOX_PRESETN_SHIFT, RSTN_U0_MAILBOX_PRESETN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_MAILBOX_PRESETN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_MAILBOX_PRESETN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_MAILBOX_PRESETN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_MAILBOX_PRESETN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SSP_SPI_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_SSP_SPI_RSTN_APB_SHIFT, RSTN_U0_SSP_SPI_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SSP_SPI_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_SSP_SPI_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SSP_SPI_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_SSP_SPI_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_SSP_SPI_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_SSP_SPI_RSTN_APB_SHIFT, RSTN_U1_SSP_SPI_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_SSP_SPI_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_SSP_SPI_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_SSP_SPI_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_SSP_SPI_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U2_SSP_SPI_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_SSP_SPI_RSTN_APB_SHIFT, RSTN_U2_SSP_SPI_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U2_SSP_SPI_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_SSP_SPI_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U2_SSP_SPI_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_SSP_SPI_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U3_SSP_SPI_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_SSP_SPI_RSTN_APB_SHIFT, RSTN_U3_SSP_SPI_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U3_SSP_SPI_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_SSP_SPI_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U3_SSP_SPI_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_SSP_SPI_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U4_SSP_SPI_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_SSP_SPI_RSTN_APB_SHIFT, RSTN_U4_SSP_SPI_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U4_SSP_SPI_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_SSP_SPI_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U4_SSP_SPI_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_SSP_SPI_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U5_SSP_SPI_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_SSP_SPI_RSTN_APB_SHIFT, RSTN_U5_SSP_SPI_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U5_SSP_SPI_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_SSP_SPI_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U5_SSP_SPI_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_SSP_SPI_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U6_SSP_SPI_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U6_SSP_SPI_RSTN_APB_SHIFT, RSTN_U6_SSP_SPI_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U6_SSP_SPI_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U6_SSP_SPI_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U6_SSP_SPI_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U6_SSP_SPI_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DW_I2C_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_I2C_RSTN_APB_SHIFT, RSTN_U0_DW_I2C_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DW_I2C_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_I2C_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DW_I2C_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_I2C_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_DW_I2C_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_I2C_RSTN_APB_SHIFT, RSTN_U1_DW_I2C_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_DW_I2C_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_I2C_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_DW_I2C_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_I2C_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U2_DW_I2C_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_I2C_RSTN_APB_SHIFT, RSTN_U2_DW_I2C_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U2_DW_I2C_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_I2C_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U2_DW_I2C_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_I2C_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U3_DW_I2C_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_I2C_RSTN_APB_SHIFT, RSTN_U3_DW_I2C_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U3_DW_I2C_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_I2C_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U3_DW_I2C_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_I2C_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U4_DW_I2C_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_I2C_RSTN_APB_SHIFT, RSTN_U4_DW_I2C_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U4_DW_I2C_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_I2C_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U4_DW_I2C_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_I2C_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U5_DW_I2C_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_I2C_RSTN_APB_SHIFT, RSTN_U5_DW_I2C_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U5_DW_I2C_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_I2C_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U5_DW_I2C_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_I2C_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U6_DW_I2C_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U6_DW_I2C_RSTN_APB_SHIFT, RSTN_U6_DW_I2C_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U6_DW_I2C_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U6_DW_I2C_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U6_DW_I2C_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U6_DW_I2C_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DW_UART_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_UART_RSTN_APB_SHIFT, RSTN_U0_DW_UART_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DW_UART_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_UART_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DW_UART_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_UART_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DW_UART_RSTN_CORE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_UART_RSTN_CORE_SHIFT, RSTN_U0_DW_UART_RSTN_CORE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DW_UART_RSTN_CORE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_UART_RSTN_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DW_UART_RSTN_CORE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_DW_UART_RSTN_CORE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_DW_UART_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_UART_RSTN_APB_SHIFT, RSTN_U1_DW_UART_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_DW_UART_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_UART_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_DW_UART_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_UART_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_DW_UART_RSTN_CORE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_UART_RSTN_CORE_SHIFT, RSTN_U1_DW_UART_RSTN_CORE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_DW_UART_RSTN_CORE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_UART_RSTN_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_DW_UART_RSTN_CORE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U1_DW_UART_RSTN_CORE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U2_DW_UART_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_UART_RSTN_APB_SHIFT, RSTN_U2_DW_UART_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U2_DW_UART_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_UART_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U2_DW_UART_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_UART_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U2_DW_UART_RSTN_CORE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_UART_RSTN_CORE_SHIFT, RSTN_U2_DW_UART_RSTN_CORE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U2_DW_UART_RSTN_CORE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_UART_RSTN_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U2_DW_UART_RSTN_CORE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U2_DW_UART_RSTN_CORE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U3_DW_UART_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_UART_RSTN_APB_SHIFT, RSTN_U3_DW_UART_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U3_DW_UART_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_UART_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U3_DW_UART_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_UART_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U3_DW_UART_RSTN_CORE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_UART_RSTN_CORE_SHIFT, RSTN_U3_DW_UART_RSTN_CORE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U3_DW_UART_RSTN_CORE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_UART_RSTN_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U3_DW_UART_RSTN_CORE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U3_DW_UART_RSTN_CORE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U4_DW_UART_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_UART_RSTN_APB_SHIFT, RSTN_U4_DW_UART_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U4_DW_UART_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_UART_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U4_DW_UART_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_UART_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U4_DW_UART_RSTN_CORE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_UART_RSTN_CORE_SHIFT, RSTN_U4_DW_UART_RSTN_CORE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U4_DW_UART_RSTN_CORE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_UART_RSTN_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U4_DW_UART_RSTN_CORE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U4_DW_UART_RSTN_CORE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U5_DW_UART_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_UART_RSTN_APB_SHIFT, RSTN_U5_DW_UART_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U5_DW_UART_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_UART_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U5_DW_UART_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_UART_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U5_DW_UART_RSTN_CORE_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_UART_RSTN_CORE_SHIFT, RSTN_U5_DW_UART_RSTN_CORE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U5_DW_UART_RSTN_CORE_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_UART_RSTN_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U5_DW_UART_RSTN_CORE_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U5_DW_UART_RSTN_CORE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_SPDIF_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_CDNS_SPDIF_RSTN_APB_SHIFT, RSTN_U0_CDNS_SPDIF_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_SPDIF_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_CDNS_SPDIF_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_SPDIF_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT2_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS2_REG_ADDR, RSTN_U0_CDNS_SPDIF_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_PWMDAC_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PWMDAC_RSTN_APB_SHIFT, RSTN_U0_PWMDAC_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_PWMDAC_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PWMDAC_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_PWMDAC_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PWMDAC_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_PDM_4MIC_RSTN_DMIC_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PDM_4MIC_RSTN_DMIC_SHIFT, RSTN_U0_PDM_4MIC_RSTN_DMIC_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_PDM_4MIC_RSTN_DMIC_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PDM_4MIC_RSTN_DMIC_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_PDM_4MIC_RSTN_DMIC_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PDM_4MIC_RSTN_DMIC_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_PDM_4MIC_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PDM_4MIC_RSTN_APB_SHIFT, RSTN_U0_PDM_4MIC_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_PDM_4MIC_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PDM_4MIC_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_PDM_4MIC_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PDM_4MIC_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_I2SRX_3CH_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2SRX_3CH_RSTN_APB_SHIFT, RSTN_U0_I2SRX_3CH_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_I2SRX_3CH_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2SRX_3CH_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_I2SRX_3CH_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2SRX_3CH_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_I2SRX_3CH_RSTN_BCLK_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2SRX_3CH_RSTN_BCLK_SHIFT, RSTN_U0_I2SRX_3CH_RSTN_BCLK_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_I2SRX_3CH_RSTN_BCLK_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2SRX_3CH_RSTN_BCLK_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_I2SRX_3CH_RSTN_BCLK_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2SRX_3CH_RSTN_BCLK_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_I2STX_4CH_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2STX_4CH_RSTN_APB_SHIFT, RSTN_U0_I2STX_4CH_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_I2STX_4CH_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2STX_4CH_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_I2STX_4CH_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2STX_4CH_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_I2STX_4CH_RSTN_BCLK_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2STX_4CH_RSTN_BCLK_SHIFT, RSTN_U0_I2STX_4CH_RSTN_BCLK_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_I2STX_4CH_RSTN_BCLK_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2STX_4CH_RSTN_BCLK_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_I2STX_4CH_RSTN_BCLK_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_I2STX_4CH_RSTN_BCLK_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_I2STX_4CH_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_I2STX_4CH_RSTN_APB_SHIFT, RSTN_U1_I2STX_4CH_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_I2STX_4CH_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_I2STX_4CH_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_I2STX_4CH_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_I2STX_4CH_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_I2STX_4CH_RSTN_BCLK_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_I2STX_4CH_RSTN_BCLK_SHIFT, RSTN_U1_I2STX_4CH_RSTN_BCLK_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_I2STX_4CH_RSTN_BCLK_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_I2STX_4CH_RSTN_BCLK_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_I2STX_4CH_RSTN_BCLK_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_I2STX_4CH_RSTN_BCLK_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_AHB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_AHB_SHIFT, RSTN_U0_TDM16SLOT_RSTN_AHB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_AHB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_AHB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_AHB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_AHB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_TDM_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_TDM_SHIFT, RSTN_U0_TDM16SLOT_RSTN_TDM_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_TDM_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_TDM_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_TDM_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_TDM_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_APB_SHIFT, RSTN_U0_TDM16SLOT_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_TDM16SLOT_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TDM16SLOT_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_PWM_8CH_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PWM_8CH_RSTN_APB_SHIFT, RSTN_U0_PWM_8CH_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_PWM_8CH_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PWM_8CH_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_PWM_8CH_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_PWM_8CH_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DSKIT_WDT_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_DSKIT_WDT_RSTN_APB_SHIFT, RSTN_U0_DSKIT_WDT_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DSKIT_WDT_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_DSKIT_WDT_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DSKIT_WDT_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_DSKIT_WDT_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DSKIT_WDT_RSTN_WDT_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_DSKIT_WDT_RSTN_WDT_SHIFT, RSTN_U0_DSKIT_WDT_RSTN_WDT_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DSKIT_WDT_RSTN_WDT_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_DSKIT_WDT_RSTN_WDT_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DSKIT_WDT_RSTN_WDT_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_DSKIT_WDT_RSTN_WDT_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_APB_SHIFT, RSTN_U0_CAN_CTRL_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_CAN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_CAN_SHIFT, RSTN_U0_CAN_CTRL_RSTN_CAN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_CAN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_CAN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_CAN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_CAN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_TIMER_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_TIMER_SHIFT, RSTN_U0_CAN_CTRL_RSTN_TIMER_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_TIMER_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_TIMER_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CAN_CTRL_RSTN_TIMER_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_CAN_CTRL_RSTN_TIMER_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_APB_SHIFT, RSTN_U1_CAN_CTRL_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_CAN_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_CAN_SHIFT, RSTN_U1_CAN_CTRL_RSTN_CAN_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_CAN_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_CAN_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_CAN_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_CAN_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_TIMER_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_TIMER_SHIFT, RSTN_U1_CAN_CTRL_RSTN_TIMER_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_TIMER_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_TIMER_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U1_CAN_CTRL_RSTN_TIMER_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U1_CAN_CTRL_RSTN_TIMER_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_APB_SHIFT, RSTN_U0_SI5_TIMER_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER0_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER0_SHIFT, RSTN_U0_SI5_TIMER_RSTN_TIMER0_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER0_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER0_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER0_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER0_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER1_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER1_SHIFT, RSTN_U0_SI5_TIMER_RSTN_TIMER1_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER1_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER1_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER1_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER1_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER2_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER2_SHIFT, RSTN_U0_SI5_TIMER_RSTN_TIMER2_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER2_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER2_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER2_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER2_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER3_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER3_SHIFT, RSTN_U0_SI5_TIMER_RSTN_TIMER3_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER3_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER3_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_SI5_TIMER_RSTN_TIMER3_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_SI5_TIMER_RSTN_TIMER3_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_INT_CTRL_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_INT_CTRL_RSTN_APB_SHIFT, RSTN_U0_INT_CTRL_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_INT_CTRL_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_INT_CTRL_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_INT_CTRL_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_INT_CTRL_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_TEMP_SENSOR_RSTN_APB_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TEMP_SENSOR_RSTN_APB_SHIFT, RSTN_U0_TEMP_SENSOR_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_TEMP_SENSOR_RSTN_APB_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TEMP_SENSOR_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_TEMP_SENSOR_RSTN_APB_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TEMP_SENSOR_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_TEMP_SENSOR_RSTN_TEMP_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TEMP_SENSOR_RSTN_TEMP_SHIFT, RSTN_U0_TEMP_SENSOR_RSTN_TEMP_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_TEMP_SENSOR_RSTN_TEMP_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TEMP_SENSOR_RSTN_TEMP_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_TEMP_SENSOR_RSTN_TEMP_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_TEMP_SENSOR_RSTN_TEMP_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_JTAG_CERTIFICATION_RST_N_ 	saif_get_reg(SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_JTAG_CERTIFICATION_RST_N_SHIFT, RSTN_U0_JTAG_CERTIFICATION_RST_N_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_JTAG_CERTIFICATION_RST_N_ 	saif_assert_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_JTAG_CERTIFICATION_RST_N_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_JTAG_CERTIFICATION_RST_N_ 	saif_clear_rst(SYS_CRG_RSTGEN_SOFTWARE_RESET_ASSERT3_REG_ADDR, SYS_CRG_RSTGEN_SOFTWARE_RESET_STATUS3_REG_ADDR, RSTN_U0_JTAG_CERTIFICATION_RST_N_MASK)



//#define DOM_VOUT_CRG_BASE_ADDR 0x0
#define CLK_APB_CTRL_REG_ADDR                                        (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x0U)
#define CLK_DC8200_PIX0_CTRL_REG_ADDR                                (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x4U)
#define CLK_DSI_SYS_CTRL_REG_ADDR                                    (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x8U)
#define CLK_TX_ESC_CTRL_REG_ADDR                                     (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0xCU)
#define CLK_U0_DC8200_CLK_AXI_CTRL_REG_ADDR                          (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x10U)
#define CLK_U0_DC8200_CLK_CORE_CTRL_REG_ADDR                         (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x14U)
#define CLK_U0_DC8200_CLK_AHB_CTRL_REG_ADDR                          (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x18U)
#define CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR                         (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x1CU)
#define CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR                         (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x20U)
#define CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR                       (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x24U)
#define CLK_U0_CDNS_DSITX_CLK_APB_CTRL_REG_ADDR                      (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x28U)
#define CLK_U0_CDNS_DSITX_CLK_SYS_CTRL_REG_ADDR                      (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x2CU)
#define CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR                      (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x30U)
#define CLK_U0_CDNS_DSITX_CLK_TXESC_CTRL_REG_ADDR                    (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x34U)
#define CLK_U0_MIPITX_DPHY_CLK_TXESC_CTRL_REG_ADDR                   (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x38U)
#define CLK_U0_HDMI_TX_CLK_MCLK_CTRL_REG_ADDR                        (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x3CU)
#define CLK_U0_HDMI_TX_CLK_BCLK_CTRL_REG_ADDR                        (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x40U)
#define CLK_U0_HDMI_TX_CLK_SYS_CTRL_REG_ADDR                         (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x44U)


#define DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR          (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x48U)

#define DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR          (U0_DOM_VOUT_CRG__SAIF_BD_APBS__BASE_ADDR + 0x4CU)


#define CLK_APB_DIV_SHIFT                                            0
#define CLK_APB_DIV_MASK                                             0xFU
#define CLK_DC8200_PIX0_DIV_SHIFT                                    0
#define CLK_DC8200_PIX0_DIV_MASK                                     0x3FU
#define CLK_DSI_SYS_DIV_SHIFT                                        0
#define CLK_DSI_SYS_DIV_MASK                                         0x1FU
#define CLK_TX_ESC_DIV_SHIFT                                         0
#define CLK_TX_ESC_DIV_MASK                                          0x1FU
#define CLK_U0_DC8200_CLK_AXI_ENABLE_DATA                            1
#define CLK_U0_DC8200_CLK_AXI_DISABLE_DATA                           0
#define CLK_U0_DC8200_CLK_AXI_EN_SHIFT                               31
#define CLK_U0_DC8200_CLK_AXI_EN_MASK                                0x80000000U
#define CLK_U0_DC8200_CLK_CORE_ENABLE_DATA                           1
#define CLK_U0_DC8200_CLK_CORE_DISABLE_DATA                          0
#define CLK_U0_DC8200_CLK_CORE_EN_SHIFT                              31
#define CLK_U0_DC8200_CLK_CORE_EN_MASK                               0x80000000U
#define CLK_U0_DC8200_CLK_AHB_ENABLE_DATA                            1
#define CLK_U0_DC8200_CLK_AHB_DISABLE_DATA                           0
#define CLK_U0_DC8200_CLK_AHB_EN_SHIFT                               31
#define CLK_U0_DC8200_CLK_AHB_EN_MASK                                0x80000000U
#define CLK_U0_DC8200_CLK_PIX0_ENABLE_DATA                           1
#define CLK_U0_DC8200_CLK_PIX0_DISABLE_DATA                          0
#define CLK_U0_DC8200_CLK_PIX0_EN_SHIFT                              31
#define CLK_U0_DC8200_CLK_PIX0_EN_MASK                               0x80000000U
#define CLK_U0_DC8200_CLK_PIX0_SW_SHIFT                              24
#define CLK_U0_DC8200_CLK_PIX0_SW_MASK                               0x1000000U
#define CLK_U0_DC8200_CLK_PIX0_SW_CLK_DC8200_PIX0_DATA               0
#define CLK_U0_DC8200_CLK_PIX0_SW_CLK_HDMITX0_PIXELCLK_DATA          1
#define CLK_U0_DC8200_CLK_PIX1_ENABLE_DATA                           1
#define CLK_U0_DC8200_CLK_PIX1_DISABLE_DATA                          0
#define CLK_U0_DC8200_CLK_PIX1_EN_SHIFT                              31
#define CLK_U0_DC8200_CLK_PIX1_EN_MASK                               0x80000000U
#define CLK_U0_DC8200_CLK_PIX1_SW_SHIFT                              24
#define CLK_U0_DC8200_CLK_PIX1_SW_MASK                               0x1000000U
#define CLK_U0_DC8200_CLK_PIX1_SW_CLK_DC8200_PIX0_DATA               0
#define CLK_U0_DC8200_CLK_PIX1_SW_CLK_HDMITX0_PIXELCLK_DATA          1
#define CLK_DOM_VOUT_TOP_LCD_CLK_ENABLE_DATA                         1
#define CLK_DOM_VOUT_TOP_LCD_CLK_DISABLE_DATA                        0
#define CLK_DOM_VOUT_TOP_LCD_CLK_EN_SHIFT                            31
#define CLK_DOM_VOUT_TOP_LCD_CLK_EN_MASK                             0x80000000U
#define CLK_DOM_VOUT_TOP_LCD_CLK_SW_SHIFT                            24
#define CLK_DOM_VOUT_TOP_LCD_CLK_SW_MASK                             0x1000000U
#define CLK_DOM_VOUT_TOP_LCD_CLK_SW_CLK_U0_DC8200_CLK_PIX0_OUT_DATA  0
#define CLK_DOM_VOUT_TOP_LCD_CLK_SW_CLK_U0_DC8200_CLK_PIX1_OUT_DATA  1
#define CLK_U0_CDNS_DSITX_CLK_APB_ENABLE_DATA                        1
#define CLK_U0_CDNS_DSITX_CLK_APB_DISABLE_DATA                       0
#define CLK_U0_CDNS_DSITX_CLK_APB_EN_SHIFT                           31
#define CLK_U0_CDNS_DSITX_CLK_APB_EN_MASK                            0x80000000U
#define CLK_U0_CDNS_DSITX_CLK_SYS_ENABLE_DATA                        1
#define CLK_U0_CDNS_DSITX_CLK_SYS_DISABLE_DATA                       0
#define CLK_U0_CDNS_DSITX_CLK_SYS_EN_SHIFT                           31
#define CLK_U0_CDNS_DSITX_CLK_SYS_EN_MASK                            0x80000000U
#define CLK_U0_CDNS_DSITX_CLK_DPI_ENABLE_DATA                        1
#define CLK_U0_CDNS_DSITX_CLK_DPI_DISABLE_DATA                       0
#define CLK_U0_CDNS_DSITX_CLK_DPI_EN_SHIFT                           31
#define CLK_U0_CDNS_DSITX_CLK_DPI_EN_MASK                            0x80000000U
#define CLK_U0_CDNS_DSITX_CLK_DPI_SW_SHIFT                           24
#define CLK_U0_CDNS_DSITX_CLK_DPI_SW_MASK                            0x1000000U
#define CLK_U0_CDNS_DSITX_CLK_DPI_SW_CLK_DC8200_PIX0_DATA            0
#define CLK_U0_CDNS_DSITX_CLK_DPI_SW_CLK_HDMITX0_PIXELCLK_DATA       1
#define CLK_U0_CDNS_DSITX_CLK_TXESC_ENABLE_DATA                      1
#define CLK_U0_CDNS_DSITX_CLK_TXESC_DISABLE_DATA                     0
#define CLK_U0_CDNS_DSITX_CLK_TXESC_EN_SHIFT                         31
#define CLK_U0_CDNS_DSITX_CLK_TXESC_EN_MASK                          0x80000000U
#define CLK_U0_MIPITX_DPHY_CLK_TXESC_ENABLE_DATA                     1
#define CLK_U0_MIPITX_DPHY_CLK_TXESC_DISABLE_DATA                    0
#define CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_SHIFT                        31
#define CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_MASK                         0x80000000U
#define CLK_U0_HDMI_TX_CLK_MCLK_ENABLE_DATA                          1
#define CLK_U0_HDMI_TX_CLK_MCLK_DISABLE_DATA                         0
#define CLK_U0_HDMI_TX_CLK_MCLK_EN_SHIFT                             31
#define CLK_U0_HDMI_TX_CLK_MCLK_EN_MASK                              0x80000000U
#define CLK_U0_HDMI_TX_CLK_BCLK_ENABLE_DATA                          1
#define CLK_U0_HDMI_TX_CLK_BCLK_DISABLE_DATA                         0
#define CLK_U0_HDMI_TX_CLK_BCLK_EN_SHIFT                             31
#define CLK_U0_HDMI_TX_CLK_BCLK_EN_MASK                              0x80000000U
#define CLK_U0_HDMI_TX_CLK_SYS_ENABLE_DATA                           1
#define CLK_U0_HDMI_TX_CLK_SYS_DISABLE_DATA                          0
#define CLK_U0_HDMI_TX_CLK_SYS_EN_SHIFT                              31
#define CLK_U0_HDMI_TX_CLK_SYS_EN_MASK                               0x80000000U



#define RSTN_U0_DC8200_RSTN_AXI_SHIFT                                0
#define RSTN_U0_DC8200_RSTN_AXI_MASK                                 (0x1 << 0)
#define RSTN_U0_DC8200_RSTN_AXI_ASSERT                               1
#define RSTN_U0_DC8200_RSTN_AXI_CLEAR                                0
#define RSTN_U0_DC8200_RSTN_AHB_SHIFT                                1
#define RSTN_U0_DC8200_RSTN_AHB_MASK                                 (0x1 << 1)
#define RSTN_U0_DC8200_RSTN_AHB_ASSERT                               1
#define RSTN_U0_DC8200_RSTN_AHB_CLEAR                                0
#define RSTN_U0_DC8200_RSTN_CORE_SHIFT                               2
#define RSTN_U0_DC8200_RSTN_CORE_MASK                                (0x1 << 2)
#define RSTN_U0_DC8200_RSTN_CORE_ASSERT                              1
#define RSTN_U0_DC8200_RSTN_CORE_CLEAR                               0
#define RSTN_U0_CDNS_DSITX_RSTN_DPI_SHIFT                            3
#define RSTN_U0_CDNS_DSITX_RSTN_DPI_MASK                             (0x1 << 3)
#define RSTN_U0_CDNS_DSITX_RSTN_DPI_ASSERT                           1
#define RSTN_U0_CDNS_DSITX_RSTN_DPI_CLEAR                            0
#define RSTN_U0_CDNS_DSITX_RSTN_APB_SHIFT                            4
#define RSTN_U0_CDNS_DSITX_RSTN_APB_MASK                             (0x1 << 4)
#define RSTN_U0_CDNS_DSITX_RSTN_APB_ASSERT                           1
#define RSTN_U0_CDNS_DSITX_RSTN_APB_CLEAR                            0
#define RSTN_U0_CDNS_DSITX_RSTN_RXESC_SHIFT                          5
#define RSTN_U0_CDNS_DSITX_RSTN_RXESC_MASK                           (0x1 << 5)
#define RSTN_U0_CDNS_DSITX_RSTN_RXESC_ASSERT                         1
#define RSTN_U0_CDNS_DSITX_RSTN_RXESC_CLEAR                          0
#define RSTN_U0_CDNS_DSITX_RSTN_SYS_SHIFT                            6
#define RSTN_U0_CDNS_DSITX_RSTN_SYS_MASK                             (0x1 << 6)
#define RSTN_U0_CDNS_DSITX_RSTN_SYS_ASSERT                           1
#define RSTN_U0_CDNS_DSITX_RSTN_SYS_CLEAR                            0
#define RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_SHIFT                       7
#define RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_MASK                        (0x1 << 7)
#define RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_ASSERT                      1
#define RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_CLEAR                       0
#define RSTN_U0_CDNS_DSITX_RSTN_TXESC_SHIFT                          8
#define RSTN_U0_CDNS_DSITX_RSTN_TXESC_MASK                           (0x1 << 8)
#define RSTN_U0_CDNS_DSITX_RSTN_TXESC_ASSERT                         1
#define RSTN_U0_CDNS_DSITX_RSTN_TXESC_CLEAR                          0
#define RSTN_U0_HDMI_TX_RSTN_HDMI_SHIFT                              9
#define RSTN_U0_HDMI_TX_RSTN_HDMI_MASK                               (0x1 << 9)
#define RSTN_U0_HDMI_TX_RSTN_HDMI_ASSERT                             1
#define RSTN_U0_HDMI_TX_RSTN_HDMI_CLEAR                              0
#define RSTN_U0_MIPITX_DPHY_RSTN_SYS_SHIFT                           10
#define RSTN_U0_MIPITX_DPHY_RSTN_SYS_MASK                            (0x1 << 10)
#define RSTN_U0_MIPITX_DPHY_RSTN_SYS_ASSERT                          1
#define RSTN_U0_MIPITX_DPHY_RSTN_SYS_CLEAR                           0
#define RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_SHIFT                      11
#define RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_MASK                       (0x1 << 11)
#define RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_ASSERT                     1
#define RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_CLEAR                      0

#define _DIVIDE_CLOCK_CLK_APB_(div) 			saif_set_reg(CLK_APB_CTRL_REG_ADDR, div, CLK_APB_DIV_SHIFT, CLK_APB_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_APB_ 		saif_get_reg(CLK_APB_CTRL_REG_ADDR, CLK_APB_DIV_SHIFT, CLK_APB_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_DC8200_PIX0_(div) 			saif_set_reg(CLK_DC8200_PIX0_CTRL_REG_ADDR, div, CLK_DC8200_PIX0_DIV_SHIFT, CLK_DC8200_PIX0_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_DC8200_PIX0_ 		saif_get_reg(CLK_DC8200_PIX0_CTRL_REG_ADDR, CLK_DC8200_PIX0_DIV_SHIFT, CLK_DC8200_PIX0_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_DSI_SYS_(div) 			saif_set_reg(CLK_DSI_SYS_CTRL_REG_ADDR, div, CLK_DSI_SYS_DIV_SHIFT, CLK_DSI_SYS_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_DSI_SYS_ 		saif_get_reg(CLK_DSI_SYS_CTRL_REG_ADDR, CLK_DSI_SYS_DIV_SHIFT, CLK_DSI_SYS_DIV_MASK)
#define _DIVIDE_CLOCK_CLK_TX_ESC_(div) 			saif_set_reg(CLK_TX_ESC_CTRL_REG_ADDR, div, CLK_TX_ESC_DIV_SHIFT, CLK_TX_ESC_DIV_MASK)
#define _GET_CLOCK_DIVIDE_STATUS_CLK_TX_ESC_ 		saif_get_reg(CLK_TX_ESC_CTRL_REG_ADDR, CLK_TX_ESC_DIV_SHIFT, CLK_TX_ESC_DIV_MASK)
#define _ENABLE_CLOCK_CLK_U0_DC8200_CLK_AXI_ 			saif_set_reg(CLK_U0_DC8200_CLK_AXI_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_AXI_ENABLE_DATA, CLK_U0_DC8200_CLK_AXI_EN_SHIFT, CLK_U0_DC8200_CLK_AXI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DC8200_CLK_AXI_ 			saif_set_reg(CLK_U0_DC8200_CLK_AXI_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_AXI_DISABLE_DATA, CLK_U0_DC8200_CLK_AXI_EN_SHIFT, CLK_U0_DC8200_CLK_AXI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_AXI_ 		saif_get_reg(CLK_U0_DC8200_CLK_AXI_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_AXI_EN_SHIFT, CLK_U0_DC8200_CLK_AXI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_AXI_(x) 		saif_set_reg(CLK_U0_DC8200_CLK_AXI_CTRL_REG_ADDR, x, CLK_U0_DC8200_CLK_AXI_EN_SHIFT, CLK_U0_DC8200_CLK_AXI_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DC8200_CLK_CORE_ 			saif_set_reg(CLK_U0_DC8200_CLK_CORE_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_CORE_ENABLE_DATA, CLK_U0_DC8200_CLK_CORE_EN_SHIFT, CLK_U0_DC8200_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DC8200_CLK_CORE_ 			saif_set_reg(CLK_U0_DC8200_CLK_CORE_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_CORE_DISABLE_DATA, CLK_U0_DC8200_CLK_CORE_EN_SHIFT, CLK_U0_DC8200_CLK_CORE_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_CORE_ 		saif_get_reg(CLK_U0_DC8200_CLK_CORE_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_CORE_EN_SHIFT, CLK_U0_DC8200_CLK_CORE_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_CORE_(x) 		saif_set_reg(CLK_U0_DC8200_CLK_CORE_CTRL_REG_ADDR, x, CLK_U0_DC8200_CLK_CORE_EN_SHIFT, CLK_U0_DC8200_CLK_CORE_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DC8200_CLK_AHB_ 			saif_set_reg(CLK_U0_DC8200_CLK_AHB_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_AHB_ENABLE_DATA, CLK_U0_DC8200_CLK_AHB_EN_SHIFT, CLK_U0_DC8200_CLK_AHB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DC8200_CLK_AHB_ 			saif_set_reg(CLK_U0_DC8200_CLK_AHB_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_AHB_DISABLE_DATA, CLK_U0_DC8200_CLK_AHB_EN_SHIFT, CLK_U0_DC8200_CLK_AHB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_AHB_ 		saif_get_reg(CLK_U0_DC8200_CLK_AHB_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_AHB_EN_SHIFT, CLK_U0_DC8200_CLK_AHB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_AHB_(x) 		saif_set_reg(CLK_U0_DC8200_CLK_AHB_CTRL_REG_ADDR, x, CLK_U0_DC8200_CLK_AHB_EN_SHIFT, CLK_U0_DC8200_CLK_AHB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_DC8200_CLK_PIX0_ 			saif_set_reg(CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX0_ENABLE_DATA, CLK_U0_DC8200_CLK_PIX0_EN_SHIFT, CLK_U0_DC8200_CLK_PIX0_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DC8200_CLK_PIX0_ 			saif_set_reg(CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX0_DISABLE_DATA, CLK_U0_DC8200_CLK_PIX0_EN_SHIFT, CLK_U0_DC8200_CLK_PIX0_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_PIX0_ 		saif_get_reg(CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX0_EN_SHIFT, CLK_U0_DC8200_CLK_PIX0_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_PIX0_(x) 		saif_set_reg(CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR, x, CLK_U0_DC8200_CLK_PIX0_EN_SHIFT, CLK_U0_DC8200_CLK_PIX0_EN_MASK)
#define _SWITCH_CLOCK_CLK_U0_DC8200_CLK_PIX0_SOURCE_CLK_DC8200_PIX0_ 	saif_set_reg(CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX0_SW_CLK_DC8200_PIX0_DATA, CLK_U0_DC8200_CLK_PIX0_SW_SHIFT, CLK_U0_DC8200_CLK_PIX0_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_DC8200_CLK_PIX0_SOURCE_CLK_HDMITX0_PIXELCLK_ 	saif_set_reg(CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX0_SW_CLK_HDMITX0_PIXELCLK_DATA, CLK_U0_DC8200_CLK_PIX0_SW_SHIFT, CLK_U0_DC8200_CLK_PIX0_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_DC8200_CLK_PIX0_ 		saif_get_reg(CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX0_SW_SHIFT, CLK_U0_DC8200_CLK_PIX0_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_DC8200_CLK_PIX0_(x) 		saif_set_reg(CLK_U0_DC8200_CLK_PIX0_CTRL_REG_ADDR, x, CLK_U0_DC8200_CLK_PIX0_SW_SHIFT, CLK_U0_DC8200_CLK_PIX0_SW_MASK)
#define _ENABLE_CLOCK_CLK_U0_DC8200_CLK_PIX1_ 			saif_set_reg(CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX1_ENABLE_DATA, CLK_U0_DC8200_CLK_PIX1_EN_SHIFT, CLK_U0_DC8200_CLK_PIX1_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_DC8200_CLK_PIX1_ 			saif_set_reg(CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX1_DISABLE_DATA, CLK_U0_DC8200_CLK_PIX1_EN_SHIFT, CLK_U0_DC8200_CLK_PIX1_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_PIX1_ 		saif_get_reg(CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX1_EN_SHIFT, CLK_U0_DC8200_CLK_PIX1_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_DC8200_CLK_PIX1_(x) 		saif_set_reg(CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR, x, CLK_U0_DC8200_CLK_PIX1_EN_SHIFT, CLK_U0_DC8200_CLK_PIX1_EN_MASK)
#define _SWITCH_CLOCK_CLK_U0_DC8200_CLK_PIX1_SOURCE_CLK_DC8200_PIX0_ 	saif_set_reg(CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX1_SW_CLK_DC8200_PIX0_DATA, CLK_U0_DC8200_CLK_PIX1_SW_SHIFT, CLK_U0_DC8200_CLK_PIX1_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_DC8200_CLK_PIX1_SOURCE_CLK_HDMITX0_PIXELCLK_ 	saif_set_reg(CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX1_SW_CLK_HDMITX0_PIXELCLK_DATA, CLK_U0_DC8200_CLK_PIX1_SW_SHIFT, CLK_U0_DC8200_CLK_PIX1_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_DC8200_CLK_PIX1_ 		saif_get_reg(CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR, CLK_U0_DC8200_CLK_PIX1_SW_SHIFT, CLK_U0_DC8200_CLK_PIX1_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_DC8200_CLK_PIX1_(x) 		saif_set_reg(CLK_U0_DC8200_CLK_PIX1_CTRL_REG_ADDR, x, CLK_U0_DC8200_CLK_PIX1_SW_SHIFT, CLK_U0_DC8200_CLK_PIX1_SW_MASK)
#define _ENABLE_CLOCK_CLK_DOM_VOUT_TOP_LCD_CLK_ 			saif_set_reg(CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR, CLK_DOM_VOUT_TOP_LCD_CLK_ENABLE_DATA, CLK_DOM_VOUT_TOP_LCD_CLK_EN_SHIFT, CLK_DOM_VOUT_TOP_LCD_CLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_DOM_VOUT_TOP_LCD_CLK_ 			saif_set_reg(CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR, CLK_DOM_VOUT_TOP_LCD_CLK_DISABLE_DATA, CLK_DOM_VOUT_TOP_LCD_CLK_EN_SHIFT, CLK_DOM_VOUT_TOP_LCD_CLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_DOM_VOUT_TOP_LCD_CLK_ 		saif_get_reg(CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR, CLK_DOM_VOUT_TOP_LCD_CLK_EN_SHIFT, CLK_DOM_VOUT_TOP_LCD_CLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_DOM_VOUT_TOP_LCD_CLK_(x) 		saif_set_reg(CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR, x, CLK_DOM_VOUT_TOP_LCD_CLK_EN_SHIFT, CLK_DOM_VOUT_TOP_LCD_CLK_EN_MASK)
#define _SWITCH_CLOCK_CLK_DOM_VOUT_TOP_LCD_CLK_SOURCE_CLK_U0_DC8200_CLK_PIX0_OUT_ 	saif_set_reg(CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR, CLK_DOM_VOUT_TOP_LCD_CLK_SW_CLK_U0_DC8200_CLK_PIX0_OUT_DATA, CLK_DOM_VOUT_TOP_LCD_CLK_SW_SHIFT, CLK_DOM_VOUT_TOP_LCD_CLK_SW_MASK)
#define _SWITCH_CLOCK_CLK_DOM_VOUT_TOP_LCD_CLK_SOURCE_CLK_U0_DC8200_CLK_PIX1_OUT_ 	saif_set_reg(CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR, CLK_DOM_VOUT_TOP_LCD_CLK_SW_CLK_U0_DC8200_CLK_PIX1_OUT_DATA, CLK_DOM_VOUT_TOP_LCD_CLK_SW_SHIFT, CLK_DOM_VOUT_TOP_LCD_CLK_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_DOM_VOUT_TOP_LCD_CLK_ 		saif_get_reg(CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR, CLK_DOM_VOUT_TOP_LCD_CLK_SW_SHIFT, CLK_DOM_VOUT_TOP_LCD_CLK_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_DOM_VOUT_TOP_LCD_CLK_(x) 		saif_set_reg(CLK_DOM_VOUT_TOP_LCD_CLK_CTRL_REG_ADDR, x, CLK_DOM_VOUT_TOP_LCD_CLK_SW_SHIFT, CLK_DOM_VOUT_TOP_LCD_CLK_SW_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_DSITX_CLK_APB_ 			saif_set_reg(CLK_U0_CDNS_DSITX_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_APB_ENABLE_DATA, CLK_U0_CDNS_DSITX_CLK_APB_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_APB_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_DSITX_CLK_APB_ 			saif_set_reg(CLK_U0_CDNS_DSITX_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_APB_DISABLE_DATA, CLK_U0_CDNS_DSITX_CLK_APB_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_APB_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_DSITX_CLK_APB_ 		saif_get_reg(CLK_U0_CDNS_DSITX_CLK_APB_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_APB_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_APB_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_DSITX_CLK_APB_(x) 		saif_set_reg(CLK_U0_CDNS_DSITX_CLK_APB_CTRL_REG_ADDR, x, CLK_U0_CDNS_DSITX_CLK_APB_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_APB_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_DSITX_CLK_SYS_ 			saif_set_reg(CLK_U0_CDNS_DSITX_CLK_SYS_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_SYS_ENABLE_DATA, CLK_U0_CDNS_DSITX_CLK_SYS_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_SYS_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_DSITX_CLK_SYS_ 			saif_set_reg(CLK_U0_CDNS_DSITX_CLK_SYS_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_SYS_DISABLE_DATA, CLK_U0_CDNS_DSITX_CLK_SYS_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_SYS_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_DSITX_CLK_SYS_ 		saif_get_reg(CLK_U0_CDNS_DSITX_CLK_SYS_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_SYS_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_SYS_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_DSITX_CLK_SYS_(x) 		saif_set_reg(CLK_U0_CDNS_DSITX_CLK_SYS_CTRL_REG_ADDR, x, CLK_U0_CDNS_DSITX_CLK_SYS_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_SYS_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_DSITX_CLK_DPI_ 			saif_set_reg(CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_DPI_ENABLE_DATA, CLK_U0_CDNS_DSITX_CLK_DPI_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_DPI_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_DSITX_CLK_DPI_ 			saif_set_reg(CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_DPI_DISABLE_DATA, CLK_U0_CDNS_DSITX_CLK_DPI_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_DPI_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_DSITX_CLK_DPI_ 		saif_get_reg(CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_DPI_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_DPI_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_DSITX_CLK_DPI_(x) 		saif_set_reg(CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR, x, CLK_U0_CDNS_DSITX_CLK_DPI_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_DPI_EN_MASK)
#define _SWITCH_CLOCK_CLK_U0_CDNS_DSITX_CLK_DPI_SOURCE_CLK_DC8200_PIX0_ 	saif_set_reg(CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_DPI_SW_CLK_DC8200_PIX0_DATA, CLK_U0_CDNS_DSITX_CLK_DPI_SW_SHIFT, CLK_U0_CDNS_DSITX_CLK_DPI_SW_MASK)
#define _SWITCH_CLOCK_CLK_U0_CDNS_DSITX_CLK_DPI_SOURCE_CLK_HDMITX0_PIXELCLK_ 	saif_set_reg(CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_DPI_SW_CLK_HDMITX0_PIXELCLK_DATA, CLK_U0_CDNS_DSITX_CLK_DPI_SW_SHIFT, CLK_U0_CDNS_DSITX_CLK_DPI_SW_MASK)
#define _GET_CLOCK_SOURCE_STATUS_CLK_U0_CDNS_DSITX_CLK_DPI_ 		saif_get_reg(CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_DPI_SW_SHIFT, CLK_U0_CDNS_DSITX_CLK_DPI_SW_MASK)
#define _SET_CLOCK_SOURCE_STATUS_CLK_U0_CDNS_DSITX_CLK_DPI_(x) 		saif_set_reg(CLK_U0_CDNS_DSITX_CLK_DPI_CTRL_REG_ADDR, x, CLK_U0_CDNS_DSITX_CLK_DPI_SW_SHIFT, CLK_U0_CDNS_DSITX_CLK_DPI_SW_MASK)
#define _ENABLE_CLOCK_CLK_U0_CDNS_DSITX_CLK_TXESC_ 			saif_set_reg(CLK_U0_CDNS_DSITX_CLK_TXESC_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_TXESC_ENABLE_DATA, CLK_U0_CDNS_DSITX_CLK_TXESC_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_TXESC_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_CDNS_DSITX_CLK_TXESC_ 			saif_set_reg(CLK_U0_CDNS_DSITX_CLK_TXESC_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_TXESC_DISABLE_DATA, CLK_U0_CDNS_DSITX_CLK_TXESC_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_TXESC_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_DSITX_CLK_TXESC_ 		saif_get_reg(CLK_U0_CDNS_DSITX_CLK_TXESC_CTRL_REG_ADDR, CLK_U0_CDNS_DSITX_CLK_TXESC_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_TXESC_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_CDNS_DSITX_CLK_TXESC_(x) 		saif_set_reg(CLK_U0_CDNS_DSITX_CLK_TXESC_CTRL_REG_ADDR, x, CLK_U0_CDNS_DSITX_CLK_TXESC_EN_SHIFT, CLK_U0_CDNS_DSITX_CLK_TXESC_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_MIPITX_DPHY_CLK_TXESC_ 			saif_set_reg(CLK_U0_MIPITX_DPHY_CLK_TXESC_CTRL_REG_ADDR, CLK_U0_MIPITX_DPHY_CLK_TXESC_ENABLE_DATA, CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_SHIFT, CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_MIPITX_DPHY_CLK_TXESC_ 			saif_set_reg(CLK_U0_MIPITX_DPHY_CLK_TXESC_CTRL_REG_ADDR, CLK_U0_MIPITX_DPHY_CLK_TXESC_DISABLE_DATA, CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_SHIFT, CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_MIPITX_DPHY_CLK_TXESC_ 		saif_get_reg(CLK_U0_MIPITX_DPHY_CLK_TXESC_CTRL_REG_ADDR, CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_SHIFT, CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_MIPITX_DPHY_CLK_TXESC_(x) 		saif_set_reg(CLK_U0_MIPITX_DPHY_CLK_TXESC_CTRL_REG_ADDR, x, CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_SHIFT, CLK_U0_MIPITX_DPHY_CLK_TXESC_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_HDMI_TX_CLK_MCLK_ 			saif_set_reg(CLK_U0_HDMI_TX_CLK_MCLK_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_MCLK_ENABLE_DATA, CLK_U0_HDMI_TX_CLK_MCLK_EN_SHIFT, CLK_U0_HDMI_TX_CLK_MCLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_HDMI_TX_CLK_MCLK_ 			saif_set_reg(CLK_U0_HDMI_TX_CLK_MCLK_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_MCLK_DISABLE_DATA, CLK_U0_HDMI_TX_CLK_MCLK_EN_SHIFT, CLK_U0_HDMI_TX_CLK_MCLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_HDMI_TX_CLK_MCLK_ 		saif_get_reg(CLK_U0_HDMI_TX_CLK_MCLK_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_MCLK_EN_SHIFT, CLK_U0_HDMI_TX_CLK_MCLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_HDMI_TX_CLK_MCLK_(x) 		saif_set_reg(CLK_U0_HDMI_TX_CLK_MCLK_CTRL_REG_ADDR, x, CLK_U0_HDMI_TX_CLK_MCLK_EN_SHIFT, CLK_U0_HDMI_TX_CLK_MCLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_HDMI_TX_CLK_BCLK_ 			saif_set_reg(CLK_U0_HDMI_TX_CLK_BCLK_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_BCLK_ENABLE_DATA, CLK_U0_HDMI_TX_CLK_BCLK_EN_SHIFT, CLK_U0_HDMI_TX_CLK_BCLK_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_HDMI_TX_CLK_BCLK_ 			saif_set_reg(CLK_U0_HDMI_TX_CLK_BCLK_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_BCLK_DISABLE_DATA, CLK_U0_HDMI_TX_CLK_BCLK_EN_SHIFT, CLK_U0_HDMI_TX_CLK_BCLK_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_HDMI_TX_CLK_BCLK_ 		saif_get_reg(CLK_U0_HDMI_TX_CLK_BCLK_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_BCLK_EN_SHIFT, CLK_U0_HDMI_TX_CLK_BCLK_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_HDMI_TX_CLK_BCLK_(x) 		saif_set_reg(CLK_U0_HDMI_TX_CLK_BCLK_CTRL_REG_ADDR, x, CLK_U0_HDMI_TX_CLK_BCLK_EN_SHIFT, CLK_U0_HDMI_TX_CLK_BCLK_EN_MASK)
#define _ENABLE_CLOCK_CLK_U0_HDMI_TX_CLK_SYS_ 			saif_set_reg(CLK_U0_HDMI_TX_CLK_SYS_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_SYS_ENABLE_DATA, CLK_U0_HDMI_TX_CLK_SYS_EN_SHIFT, CLK_U0_HDMI_TX_CLK_SYS_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_HDMI_TX_CLK_SYS_ 			saif_set_reg(CLK_U0_HDMI_TX_CLK_SYS_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_SYS_DISABLE_DATA, CLK_U0_HDMI_TX_CLK_SYS_EN_SHIFT, CLK_U0_HDMI_TX_CLK_SYS_EN_MASK)
#define _GET_CLOCK_ENABLE_STATUS_CLK_U0_HDMI_TX_CLK_SYS_ 		saif_get_reg(CLK_U0_HDMI_TX_CLK_SYS_CTRL_REG_ADDR, CLK_U0_HDMI_TX_CLK_SYS_EN_SHIFT, CLK_U0_HDMI_TX_CLK_SYS_EN_MASK)
#define _SET_CLOCK_ENABLE_STATUS_CLK_U0_HDMI_TX_CLK_SYS_(x) 		saif_set_reg(CLK_U0_HDMI_TX_CLK_SYS_CTRL_REG_ADDR, x, CLK_U0_HDMI_TX_CLK_SYS_EN_SHIFT, CLK_U0_HDMI_TX_CLK_SYS_EN_MASK)


#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DC8200_RSTN_AXI_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_AXI_SHIFT, RSTN_U0_DC8200_RSTN_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DC8200_RSTN_AXI_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DC8200_RSTN_AXI_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_AXI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DC8200_RSTN_AHB_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_AHB_SHIFT, RSTN_U0_DC8200_RSTN_AHB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DC8200_RSTN_AHB_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_AHB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DC8200_RSTN_AHB_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_AHB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_DC8200_RSTN_CORE_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_CORE_SHIFT, RSTN_U0_DC8200_RSTN_CORE_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_DC8200_RSTN_CORE_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_DC8200_RSTN_CORE_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_DC8200_RSTN_CORE_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_DPI_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_DPI_SHIFT, RSTN_U0_CDNS_DSITX_RSTN_DPI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_DPI_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_DPI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_DPI_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_DPI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_APB_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_APB_SHIFT, RSTN_U0_CDNS_DSITX_RSTN_APB_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_APB_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_APB_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_APB_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_APB_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_RXESC_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_RXESC_SHIFT, RSTN_U0_CDNS_DSITX_RSTN_RXESC_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_RXESC_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_RXESC_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_RXESC_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_RXESC_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_SYS_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_SYS_SHIFT, RSTN_U0_CDNS_DSITX_RSTN_SYS_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_SYS_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_SYS_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_SYS_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_SYS_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_SHIFT, RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_TXBYTEHS_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_TXESC_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_TXESC_SHIFT, RSTN_U0_CDNS_DSITX_RSTN_TXESC_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_TXESC_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_TXESC_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_CDNS_DSITX_RSTN_TXESC_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_CDNS_DSITX_RSTN_TXESC_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_HDMI_TX_RSTN_HDMI_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_HDMI_TX_RSTN_HDMI_SHIFT, RSTN_U0_HDMI_TX_RSTN_HDMI_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_HDMI_TX_RSTN_HDMI_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_HDMI_TX_RSTN_HDMI_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_HDMI_TX_RSTN_HDMI_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_HDMI_TX_RSTN_HDMI_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_MIPITX_DPHY_RSTN_SYS_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_MIPITX_DPHY_RSTN_SYS_SHIFT, RSTN_U0_MIPITX_DPHY_RSTN_SYS_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_MIPITX_DPHY_RSTN_SYS_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_MIPITX_DPHY_RSTN_SYS_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_MIPITX_DPHY_RSTN_SYS_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_MIPITX_DPHY_RSTN_SYS_MASK)
#define _READ_RESET_STATUS_RSTGEN_RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_ 	saif_get_reg(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_SHIFT, RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_MASK)
#define _ASSERT_RESET_RSTGEN_RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_ 	saif_assert_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_MASK)
#define _CLEAR_RESET_RSTGEN_RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_ 	saif_clear_rst(DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR, DOM_VOUT_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR, RSTN_U0_MIPITX_DPHY_RSTN_TXBYTEHS_MASK)

#define DOM_VOUT_SYSCONSAIF__SYSCFG_0_ADDR                 (U0_DOM_VOUT_SYSCON__SAIF_BD_APBS__BASE_ADDR + 0x0U)
#define U0_CDNS_DSITX_SCFG_SRAM_CONFIG_WIDTH               0x8U
#define U0_CDNS_DSITX_SCFG_SRAM_CONFIG_SHIFT               0x0U
#define U0_CDNS_DSITX_SCFG_SRAM_CONFIG_MASK                0xFFU
#define U0_CDNS_DSITX_DSI_TEST_GENERIC_CTRL_WIDTH          0x10U
#define U0_CDNS_DSITX_DSI_TEST_GENERIC_CTRL_SHIFT          0x8U
#define U0_CDNS_DSITX_DSI_TEST_GENERIC_CTRL_MASK           0xFFFF00U
#define DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR                 (U0_DOM_VOUT_SYSCON__SAIF_BD_APBS__BASE_ADDR + 0x4U)
#define U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS_WIDTH        0x10U
#define U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS_SHIFT        0x0U
#define U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS_MASK         0xFFFFU
#define U0_DC8200_CACTIVE_WIDTH                            0x1U
#define U0_DC8200_CACTIVE_SHIFT                            0x10U
#define U0_DC8200_CACTIVE_MASK                             0x10000U
#define U0_DC8200_CSYSACK_WIDTH                            0x1U
#define U0_DC8200_CSYSACK_SHIFT                            0x11U
#define U0_DC8200_CSYSACK_MASK                             0x20000U
#define U0_DC8200_CSYSREQ_WIDTH                            0x1U
#define U0_DC8200_CSYSREQ_SHIFT                            0x12U
#define U0_DC8200_CSYSREQ_MASK                             0x40000U
#define U0_DC8200_DISABLERAMCLOCKGATING_WIDTH              0x1U
#define U0_DC8200_DISABLERAMCLOCKGATING_SHIFT              0x13U
#define U0_DC8200_DISABLERAMCLOCKGATING_MASK               0x80000U
#define U0_DISPLAY_PANEL_MUX_PANEL_SEL_WIDTH               0x1U
#define U0_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT               0x14U
#define U0_DISPLAY_PANEL_MUX_PANEL_SEL_MASK                0x100000U
#define U0_DSITX_DATA_MAPPING_DP_MODE_WIDTH                0x3U
#define U0_DSITX_DATA_MAPPING_DP_MODE_SHIFT                0x15U
#define U0_DSITX_DATA_MAPPING_DP_MODE_MASK                 0xE00000U
#define U0_DSITX_DATA_MAPPING_DPI_DP_SEL_WIDTH             0x1U
#define U0_DSITX_DATA_MAPPING_DPI_DP_SEL_SHIFT             0x18U
#define U0_DSITX_DATA_MAPPING_DPI_DP_SEL_MASK              0x1000000U
#define U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH_WIDTH            0x1U
#define U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH_SHIFT            0x19U
#define U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH_MASK             0x2000000U
#define U0_HDMI_DATA_MAPPING_DP_YUV_MODE_WIDTH             0x2U
#define U0_HDMI_DATA_MAPPING_DP_YUV_MODE_SHIFT             0x1AU
#define U0_HDMI_DATA_MAPPING_DP_YUV_MODE_MASK              0xC000000U
#define U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH_WIDTH           0x2U
#define U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH_SHIFT           0x1CU
#define U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH_MASK            0x30000000U
#define U0_HDMI_DATA_MAPPING_DPI_DP_SEL_WIDTH              0x1U
#define U0_HDMI_DATA_MAPPING_DPI_DP_SEL_SHIFT              0x1EU
#define U0_HDMI_DATA_MAPPING_DPI_DP_SEL_MASK               0x40000000U
#define DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR                 (U0_DOM_VOUT_SYSCON__SAIF_BD_APBS__BASE_ADDR + 0x8U)
#define U0_LCD_DATA_MAPPING_DP_RGB_FMT_WIDTH               0x2U
#define U0_LCD_DATA_MAPPING_DP_RGB_FMT_SHIFT               0x0U
#define U0_LCD_DATA_MAPPING_DP_RGB_FMT_MASK                0x3U
#define U0_LCD_DATA_MAPPING_DPI_DP_SEL_WIDTH               0x1U
#define U0_LCD_DATA_MAPPING_DPI_DP_SEL_SHIFT               0x2U
#define U0_LCD_DATA_MAPPING_DPI_DP_SEL_MASK                0x4U
#define U1_DISPLAY_PANEL_MUX_PANEL_SEL_WIDTH               0x1U
#define U1_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT               0x3U
#define U1_DISPLAY_PANEL_MUX_PANEL_SEL_MASK                0x8U
#define U2_DISPLAY_PANEL_MUX_PANEL_SEL_WIDTH               0x1U
#define U2_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT               0x4U
#define U2_DISPLAY_PANEL_MUX_PANEL_SEL_MASK                0x10U
#define DOM_VOUT_SYSCONSAIF__SYSCFG_12_ADDR                (U0_DOM_VOUT_SYSCON__SAIF_BD_APBS__BASE_ADDR + 0xcU)
#define VOUT_TEST_REG0_WIDTH                               0x20U
#define VOUT_TEST_REG0_SHIFT                               0x0U
#define VOUT_TEST_REG0_MASK                                0xFFFFFFFFU
#define DOM_VOUT_SYSCONSAIF__SYSCFG_16_ADDR                (U0_DOM_VOUT_SYSCON__SAIF_BD_APBS__BASE_ADDR + 0x10U)
#define VOUT_TEST_REG1_WIDTH                               0x20U
#define VOUT_TEST_REG1_SHIFT                               0x0U
#define VOUT_TEST_REG1_MASK                                0xFFFFFFFFU
#define DOM_VOUT_SYSCONSAIF__SYSCFG_20_ADDR                (U0_DOM_VOUT_SYSCON__SAIF_BD_APBS__BASE_ADDR + 0x14U)
#define VOUT_TEST_REG2_WIDTH                               0x20U
#define VOUT_TEST_REG2_SHIFT                               0x0U
#define VOUT_TEST_REG2_MASK                                0xFFFFFFFFU
#define DOM_VOUT_SYSCONSAIF__SYSCFG_24_ADDR                (U0_DOM_VOUT_SYSCON__SAIF_BD_APBS__BASE_ADDR + 0x18U)
#define VOUT_TEST_REG3_WIDTH                               0x20U
#define VOUT_TEST_REG3_SHIFT                               0x0U
#define VOUT_TEST_REG3_MASK                                0xFFFFFFFFU
#define GET_U0_CDNS_DSITX_SCFG_SRAM_CONFIG                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_0_ADDR,U0_CDNS_DSITX_SCFG_SRAM_CONFIG_SHIFT,U0_CDNS_DSITX_SCFG_SRAM_CONFIG_MASK)
#define SET_U0_CDNS_DSITX_SCFG_SRAM_CONFIG(data)           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_0_ADDR,data,U0_CDNS_DSITX_SCFG_SRAM_CONFIG_SHIFT,U0_CDNS_DSITX_SCFG_SRAM_CONFIG_MASK)
#define GET_U0_CDNS_DSITX_DSI_TEST_GENERIC_CTRL            saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_0_ADDR,U0_CDNS_DSITX_DSI_TEST_GENERIC_CTRL_SHIFT,U0_CDNS_DSITX_DSI_TEST_GENERIC_CTRL_MASK)
#define GET_U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS          saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS_SHIFT,U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS_MASK)
#define SET_U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS(data)    saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS_SHIFT,U0_CDNS_DSITX_DSI_TEST_GENERIC_STATUS_MASK)
#define GET_U0_DC8200_CACTIVE                              saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_DC8200_CACTIVE_SHIFT,U0_DC8200_CACTIVE_MASK)
#define GET_U0_DC8200_CSYSACK                              saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_DC8200_CSYSACK_SHIFT,U0_DC8200_CSYSACK_MASK)
#define GET_U0_DC8200_CSYSREQ                              saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_DC8200_CSYSREQ_SHIFT,U0_DC8200_CSYSREQ_MASK)
#define SET_U0_DC8200_CSYSREQ(data)                        saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_DC8200_CSYSREQ_SHIFT,U0_DC8200_CSYSREQ_MASK)
#define GET_U0_DC8200_DISABLERAMCLOCKGATING                saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_DC8200_DISABLERAMCLOCKGATING_SHIFT,U0_DC8200_DISABLERAMCLOCKGATING_MASK)
#define SET_U0_DC8200_DISABLERAMCLOCKGATING(data)          saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_DC8200_DISABLERAMCLOCKGATING_SHIFT,U0_DC8200_DISABLERAMCLOCKGATING_MASK)
#define GET_U0_DISPLAY_PANEL_MUX_PANEL_SEL                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT,U0_DISPLAY_PANEL_MUX_PANEL_SEL_MASK)
#define SET_U0_DISPLAY_PANEL_MUX_PANEL_SEL(data)           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT,U0_DISPLAY_PANEL_MUX_PANEL_SEL_MASK)
#define GET_U0_DSITX_DATA_MAPPING_DP_MODE                  saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_DSITX_DATA_MAPPING_DP_MODE_SHIFT,U0_DSITX_DATA_MAPPING_DP_MODE_MASK)
#define SET_U0_DSITX_DATA_MAPPING_DP_MODE(data)            saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_DSITX_DATA_MAPPING_DP_MODE_SHIFT,U0_DSITX_DATA_MAPPING_DP_MODE_MASK)
#define GET_U0_DSITX_DATA_MAPPING_DPI_DP_SEL               saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_DSITX_DATA_MAPPING_DPI_DP_SEL_SHIFT,U0_DSITX_DATA_MAPPING_DPI_DP_SEL_MASK)
#define SET_U0_DSITX_DATA_MAPPING_DPI_DP_SEL(data)         saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_DSITX_DATA_MAPPING_DPI_DP_SEL_SHIFT,U0_DSITX_DATA_MAPPING_DPI_DP_SEL_MASK)
#define GET_U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH              saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH_SHIFT,U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH_MASK)
#define SET_U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH(data)        saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH_SHIFT,U0_HDMI_DATA_MAPPING_DP_BIT_DEPTH_MASK)
#define GET_U0_HDMI_DATA_MAPPING_DP_YUV_MODE               saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_HDMI_DATA_MAPPING_DP_YUV_MODE_SHIFT,U0_HDMI_DATA_MAPPING_DP_YUV_MODE_MASK)
#define SET_U0_HDMI_DATA_MAPPING_DP_YUV_MODE(data)         saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_HDMI_DATA_MAPPING_DP_YUV_MODE_SHIFT,U0_HDMI_DATA_MAPPING_DP_YUV_MODE_MASK)
#define GET_U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH             saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH_SHIFT,U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH_MASK)
#define SET_U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH(data)       saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH_SHIFT,U0_HDMI_DATA_MAPPING_DPI_BIT_DEPTH_MASK)
#define GET_U0_HDMI_DATA_MAPPING_DPI_DP_SEL                saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,U0_HDMI_DATA_MAPPING_DPI_DP_SEL_SHIFT,U0_HDMI_DATA_MAPPING_DPI_DP_SEL_MASK)
#define SET_U0_HDMI_DATA_MAPPING_DPI_DP_SEL(data)          saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_4_ADDR,data,U0_HDMI_DATA_MAPPING_DPI_DP_SEL_SHIFT,U0_HDMI_DATA_MAPPING_DPI_DP_SEL_MASK)
#define GET_U0_LCD_DATA_MAPPING_DP_RGB_FMT                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR,U0_LCD_DATA_MAPPING_DP_RGB_FMT_SHIFT,U0_LCD_DATA_MAPPING_DP_RGB_FMT_MASK)
#define SET_U0_LCD_DATA_MAPPING_DP_RGB_FMT(data)           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR,data,U0_LCD_DATA_MAPPING_DP_RGB_FMT_SHIFT,U0_LCD_DATA_MAPPING_DP_RGB_FMT_MASK)
#define GET_U0_LCD_DATA_MAPPING_DPI_DP_SEL                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR,U0_LCD_DATA_MAPPING_DPI_DP_SEL_SHIFT,U0_LCD_DATA_MAPPING_DPI_DP_SEL_MASK)
#define SET_U0_LCD_DATA_MAPPING_DPI_DP_SEL(data)           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR,data,U0_LCD_DATA_MAPPING_DPI_DP_SEL_SHIFT,U0_LCD_DATA_MAPPING_DPI_DP_SEL_MASK)
#define GET_U1_DISPLAY_PANEL_MUX_PANEL_SEL                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR,U1_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT,U1_DISPLAY_PANEL_MUX_PANEL_SEL_MASK)
#define SET_U1_DISPLAY_PANEL_MUX_PANEL_SEL(data)           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR,data,U1_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT,U1_DISPLAY_PANEL_MUX_PANEL_SEL_MASK)
#define GET_U2_DISPLAY_PANEL_MUX_PANEL_SEL                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR,U2_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT,U2_DISPLAY_PANEL_MUX_PANEL_SEL_MASK)
#define SET_U2_DISPLAY_PANEL_MUX_PANEL_SEL(data)           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_8_ADDR,data,U2_DISPLAY_PANEL_MUX_PANEL_SEL_SHIFT,U2_DISPLAY_PANEL_MUX_PANEL_SEL_MASK)
#define GET_VOUT_TEST_REG0                                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_12_ADDR,VOUT_TEST_REG0_SHIFT,VOUT_TEST_REG0_MASK)
#define SET_VOUT_TEST_REG0(data)                           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_12_ADDR,data,VOUT_TEST_REG0_SHIFT,VOUT_TEST_REG0_MASK)
#define GET_VOUT_TEST_REG1                                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_16_ADDR,VOUT_TEST_REG1_SHIFT,VOUT_TEST_REG1_MASK)
#define SET_VOUT_TEST_REG1(data)                           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_16_ADDR,data,VOUT_TEST_REG1_SHIFT,VOUT_TEST_REG1_MASK)
#define GET_VOUT_TEST_REG2                                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_20_ADDR,VOUT_TEST_REG2_SHIFT,VOUT_TEST_REG2_MASK)
#define SET_VOUT_TEST_REG2(data)                           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_20_ADDR,data,VOUT_TEST_REG2_SHIFT,VOUT_TEST_REG2_MASK)
#define GET_VOUT_TEST_REG3                                 saif_get_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_24_ADDR,VOUT_TEST_REG3_SHIFT,VOUT_TEST_REG3_MASK)
#define SET_VOUT_TEST_REG3(data)                           saif_set_reg(DOM_VOUT_SYSCONSAIF__SYSCFG_24_ADDR,data,VOUT_TEST_REG3_SHIFT,VOUT_TEST_REG3_MASK)

#endif /* __VS_CLOCK_H_ */
