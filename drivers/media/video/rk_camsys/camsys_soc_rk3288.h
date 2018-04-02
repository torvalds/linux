/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RKCAMSYS_SOC_RK3288_H__
#define __RKCAMSYS_SOC_RK3288_H__

#include "camsys_internal.h"

/*MARVIN REGISTER*/
#define MRV_MIPI_BASE                           0x1C00
#define MRV_MIPI_CTRL                           0x00

/*
*GRF_SOC_CON14
*bit 0     dphy_rx0_testclr
*bit 1     dphy_rx0_testclk
*bit 2     dphy_rx0_testen
*bit 3:10 dphy_rx0_testdin
*/
#define GRF_SOC_CON14_OFFSET    (0x027c)
#define DPHY_RX0_TESTCLR_MASK   (0x1 << 16)
#define DPHY_RX0_TESTCLK_MASK   (0x1 << 17)
#define DPHY_RX0_TESTEN_MASK    (0x1 << 18)
#define DPHY_RX0_TESTDIN_MASK   (0xff << 19)

#define DPHY_RX0_TESTCLR    (0x1 << 0)
#define DPHY_RX0_TESTCLK    (0x1 << 1)
#define DPHY_RX0_TESTEN     (0x1 << 2)
#define DPHY_RX0_TESTDIN_OFFSET    (3)

#define DPHY_TX1RX1_ENABLECLK_MASK   (0x1 << 28)
#define DPHY_RX1_SRC_SEL_MASK        (0x1 << 29)
#define DPHY_TX1RX1_MASTERSLAVEZ_MASK (0x1 << 30)
#define DPHY_TX1RX1_BASEDIR_OFFSET  (0x1 << 31)

#define DPHY_TX1RX1_ENABLECLK           (0x1 << 12)
#define DPHY_TX1RX1_DISABLECLK          (0x0 << 12)
#define DPHY_RX1_SRC_SEL_ISP          (0x1 << 13)
#define DPHY_RX1_SRC_SEL_CSI          (0x0 << 13)
#define DPHY_TX1RX1_SLAVEZ            (0x0 << 14)
#define DPHY_TX1RX1_BASEDIR_REC       (0x1 << 15)

/*
*GRF_SOC_CON6
*bit 0 grf_con_disable_isp
*bit 1 grf_con_isp_dphy_sel  1'b0 mipi phy rx0
*/
#define GRF_SOC_CON6_OFFSET    (0x025c)
#define MIPI_PHY_DISABLE_ISP_MASK       (0x1 << 16)
#define MIPI_PHY_DISABLE_ISP            (0x0 << 0)

#define DSI_CSI_TESTBUS_SEL_MASK        (0x1 << 30)
#define DSI_CSI_TESTBUS_SEL_OFFSET_BIT  (14)

#define MIPI_PHY_DPHYSEL_OFFSET_MASK (0x1 << 17)
#define MIPI_PHY_DPHYSEL_OFFSET_BIT (0x1)

/*
*GRF_SOC_CON10
*bit12:15 grf_dphy_rx0_enable
*bit 0:3 turn disable
*/
#define GRF_SOC_CON10_OFFSET                (0x026c)
#define DPHY_RX0_TURN_DISABLE_MASK          (0xf << 16)
#define DPHY_RX0_TURN_DISABLE_OFFSET_BITS   (0x0)
#define DPHY_RX0_ENABLE_MASK                (0xf << 28)
#define DPHY_RX0_ENABLE_OFFSET_BITS         (12)

/*
*GRF_SOC_CON9
*bit12:15 grf_dphy_rx0_enable
*bit 0:3 turn disable
*/
#define GRF_SOC_CON9_OFFSET                (0x0268)
#define DPHY_TX1RX1_TURN_DISABLE_MASK          (0xf << 16)
#define DPHY_TX1RX1_TURN_DISABLE_OFFSET_BITS   (0x0)
#define DPHY_TX1RX1_ENABLE_MASK                (0xf << 28)
#define DPHY_TX1RX1_ENABLE_OFFSET_BITS         (12)
#define DPHY_TX1RX1_FORCE_RX_MODE_MASK         (0xf << 20)
#define DPHY_TX1RX1_FORCE_RX_MODE_OFFSET_BITS  (0x0)

/*
*GRF_SOC_CON15
*bit 0:3   turn request
*/
#define GRF_SOC_CON15_OFFSET                (0x03a4)
#define DPHY_RX0_TURN_REQUEST_MASK          (0xf << 16)
#define DPHY_RX0_TURN_REQUEST_OFFSET_BITS   (0x0)

#define DPHY_TX1RX1_TURN_REQUEST_MASK          (0xf << 20)
#define DPHY_TX1RX1_TURN_REQUEST_OFFSET_BITS   (0x0)

#define GRF_SOC_STATUS21                  (0x2D4)

#define CSIHOST_PHY_TEST_CTRL0            (0x30)
#define CSIHOST_PHY_TEST_CTRL1            (0x34)
#define CSIHOST_PHY_SHUTDOWNZ             (0x08)
#define CSIHOST_DPHY_RSTZ                 (0x0c)
#define CSIHOST_N_LANES                   (0x04)
#define CSIHOST_CSI2_RESETN               (0x10)
#define CSIHOST_PHY_STATE                 (0x14)
#define CSIHOST_DATA_IDS1                 (0x18)
#define CSIHOST_DATA_IDS2                 (0x1C)
#define CSIHOST_ERR1                      (0x20)
#define CSIHOST_ERR2                      (0x24)

#define write_grf_reg(addr, val)           \
	__raw_writel(val, (void *)(addr + para->camsys_dev->rk_grf_base))
#define read_grf_reg(addr)                 \
	__raw_readl((void *)(addr + para->camsys_dev->rk_grf_base))
#define mask_grf_reg(addr, msk, val)       \
	write_grf_reg(addr, (val) | ((~(msk)) & read_grf_reg(addr)))
#ifdef CONFIG_ARM64
#define cru_writel(v, o)	\
	do {writel(v, RK_CRU_VIRT + (o)); } \
				while (0)

#define write_csihost_reg(addr, val)       \
	__raw_writel(val, addr + (void __force __iomem *)(phy_virt))
#define read_csihost_reg(addr)             \
	__raw_readl(addr + (void __force __iomem *)(phy_virt))
#else
#define cru_writel(v, o)	\
	do {writel(v, RK_CRU_VIRT + (o)); dsb(); } \
				while (0)

#define write_csihost_reg(addr, val)       \
	__raw_writel(val, addr + IOMEM(phy_virt))
#define read_csihost_reg(addr)             \
	__raw_readl(addr + IOMEM(phy_virt))
#endif
#endif
