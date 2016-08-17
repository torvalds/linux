#ifndef __RKCAMSYS_SOC_RK3399_H__
#define __RKCAMSYS_SOC_RK3399_H__

#include "camsys_internal.h"

/*MARVIN REGISTER*/
#define MRV_MIPI_BASE                           0x1C00
#define MRV_MIPI_CTRL                           0x00

/*
*#define CSIHOST_PHY_TEST_CTRL0_OFFSET 0x0030
#define DPHY_TX1RX1_TESTCLR    (1<<0)
#define DPHY_TX1RX1_TESTCLK    (1<<1)

#define CSIHOST_PHY_TEST_CTRL1_OFFSET 0x0034
#define DPHY_TX1RX1_TESTDIN_OFFSET_BITS    (0)
#define DPHY_TX1RX1_TESTDOUT_OFFSET_BITS    (8)
#define DPHY_TX1RX1_TESTEN    (16)
*/

#define GRF_SOC_STATUS21                  (0x2D4)
#define GRF_SOC_STATUS1                   (0x0e2a4)

#define CSIHOST_PHY_TEST_CTRL0            (0x30)
#define CSIHOST_PHY_TEST_CTRL1            (0x34)
#define CSIHOST_N_LANES                   (0x04)
#define CSIHOST_PHY_SHUTDOWNZ             (0x08)
#define CSIHOST_CSI2_RESETN               (0x10)
#define CSIHOST_DPHY_RSTZ                 (0x0c)
#define CSIHOST_PHY_STATE                 (0x14)
#define CSIHOST_DATA_IDS1                 (0x18)
#define CSIHOST_DATA_IDS2                 (0x1C)
#define CSIHOST_ERR1                      (0x20)
#define CSIHOST_ERR2                      (0x24)

/*
*GRF_SOC_CON6
*dphy_rx_forcerxmode 11:8
*isp_mipi_csi_host_sel:1
*disable_isp:0
*bit 0 grf_con_disable_isp
*bit 1 isp_mipi_csi_host_sel  1'b0: mipi csi host
*/
#define GRF_SOC_CON6_OFFSET    (0x0418)
/*bit 0*/
#define MIPI_PHY_DISABLE_ISP_MASK       (0x1 << 16)
#define MIPI_PHY_DISABLE_ISP            (0x0 << 0)
/*bit 1*/
#define ISP_MIPI_CSI_HOST_SEL_OFFSET_MASK       (0x1 << 17)
#define ISP_MIPI_CSI_HOST_SEL_OFFSET_BIT       (0x1)
/*bit 6*/
#define DPHY_RX_CLK_INV_SEL_MASK  (0x1 << 22)
#define DPHY_RX_CLK_INV_SEL   (0x1 << 6)
/*bit 11:8*/
#define DPHY_RX_FORCERXMODE_OFFSET_MASK     (0xF << 24)
#define DPHY_RX_FORCERXMODE_OFFSET_BITS   (8)

/*GRF_SOC_CON7*/
/*dphy_tx0_forcerxmode*/
#define GRF_SOC_CON7_OFFSET  (0x041c)
/*bit 10:7*/
#define FORCETXSTOPMODE_OFFSET_BITS   (7)
#define FORCETXSTOPMODE_MASK   (0xF << 23)

#define DPHY_TX0_FORCERXMODE   (6)
#define DPHY_TX0_FORCERXMODE_MASK   (0x01 << 22)
/*bit 5*/
#define LANE0_TURNDISABLE_BITS  (5)
#define LANE0_TURNDISABLE_MASK  (0x01 << 21)

#define GRF_SOC_STATUS13  (0x04b4)

#define GRF_SOC_CON9_OFFSET        (0x6224)
#define DPHY_RX0_TURNREQUEST_MASK        (0xF << 16)
#define DPHY_RX0_TURNREQUEST_BIT         (0)

#define GRF_SOC_CON21_OFFSET        (0x6254)
#define DPHY_RX0_FORCERXMODE_MASK	(0xF << 20)
#define DPHY_RX0_FORCERXMODE_BIT		(4)
#define DPHY_RX0_FORCETXSTOPMODE_MASK	(0xF << 24)
#define DPHY_RX0_FORCETXSTOPMODE_BIT	(8)
#define DPHY_RX0_TURNDISABLE_MASK        (0xF << 28)
#define DPHY_RX0_TURNDISABLE_BIT		(12)
#define DPHY_RX0_ENABLE_MASK        (0xF << 16)
#define DPHY_RX0_ENABLE_BIT         (0)

#define GRF_SOC_CON23_OFFSET        (0x625c)
#define DPHY_TX1RX1_TURNDISABLE_MASK        (0xF << 28)
#define DPHY_TX1RX1_TURNDISABLE_BIT         (12)
#define DPHY_TX1RX1_FORCERXMODE_MASK	(0xF << 20)
#define DPHY_TX1RX1_FORCERXMODE_BIT			(4)
#define DPHY_TX1RX1_FORCETXSTOPMODE_MASK	(0xF << 24)
#define DPHY_TX1RX1_FORCETXSTOPMODE_BIT		(8)
#define DPHY_TX1RX1_ENABLE_MASK        (0xF << 16)
#define DPHY_TX1RX1_ENABLE_BIT         (0)

#define GRF_SOC_CON24_OFFSET			(0x6260)
#define DPHY_TX1RX1_MASTERSLAVEZ_MASK	(0x1 << 23)
#define DPHY_TX1RX1_MASTERSLAVEZ_BIT	(7)
#define DPHY_TX1RX1_BASEDIR_MASK		(0x1 << 21)
#define DPHY_TX1RX1_BASEDIR_BIT			(5)
#define DPHY_RX1_MASK					(0x1 << 20)
#define DPHY_RX1_SEL_BIT				(4)

#define DPHY_TX1RX1_TURNREQUEST_MASK    (0xF << 16)
#define DPHY_TX1RX1_TURNREQUEST_BIT     (0)

#define GRF_SOC_CON25_OFFSET        (0x6264)
#define DPHY_RX0_TESTCLK_MASK        (0x1 << 25)
#define DPHY_RX0_TESTCLK_BIT         (9)
#define DPHY_RX0_TESTCLR_MASK        (0x1 << 26)
#define DPHY_RX0_TESTCLR_BIT         (10)
#define DPHY_RX0_TESTDIN_MASK		 (0xFF << 16)
#define DPHY_RX0_TESTDIN_BIT		 (0)
#define DPHY_RX0_TESTEN_MASK		 (0x1 << 24)
#define DPHY_RX0_TESTEN_BIT			 (8)

/*dphy_rx_rxclkactivehs*/
/*dphy_rx_direction*/
/*dphy_rx_ulpsactivenot_0...3*/

/*LOW POWER MODE SET*/
/*base*/
#define MIPI_CSI_DPHY_CTRL_LANE_ENABLE_OFFSET  (0x00)
#define MIPI_CSI_DPHY_CTRL_LANE_ENABLE_OFFSET_BIT  (2)

#define MIPI_CSI_DPHY_CTRL_PWRCTL_OFFSET  (0x04)
#define MIPI_CSI_DPHY_CTRL_DIG_RST_OFFSET  (0x80)
#define MIPI_CSI_DPHY_CTRL_SIG_INV_OFFSET   (0x84)

/*Configure the count time of the THS-SETTLE by protocol.*/
#define MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET  (0x00)
/*MSB enable for pin_rxdatahs_
*1: enable
*0: disable
*/
#define MIPI_CSI_DPHY_LANEX_MSB_EN_OFFSET  (0x38)

#define CSIHOST_N_LANES_OFFSET 0x04
#define CSIHOST_N_LANES_OFFSET_BIT (0)

#define DSIHOST_PHY_SHUTDOWNZ             (0x00a0)
#define DSIHOST_DPHY_RSTZ                 (0x00a0)
#define DSIHOST_PHY_TEST_CTRL0            (0x00b4)
#define DSIHOST_PHY_TEST_CTRL1            (0x00b8)

#define write_grf_reg(addr, val)           \
	__raw_writel(val, (void *)(addr + para->camsys_dev->rk_grf_base))
#define read_grf_reg(addr)                 \
	__raw_readl((void *)(addr + para->camsys_dev->rk_grf_base))
#define mask_grf_reg(addr, msk, val)       \
	write_grf_reg(addr, (val) | ((~(msk)) & read_grf_reg(addr)))

#define write_cru_reg(addr, val)           \
	__raw_writel(val, (void *)(addr + para->camsys_dev->rk_cru_base))

/*#define cru_writel(v, o)	do {writel(v, RK_CRU_VIRT + (o)); dsb();} \
*				while (0)
*/

#define write_csihost_reg(addr, val)       \
	__raw_writel(val, (void *)(addr + phy_virt))
#define read_csihost_reg(addr)             \
	__raw_readl((void *)(addr + phy_virt))
/*csi phy*/
#define write_csiphy_reg(addr, val)       \
	__raw_writel(val, (void *)(addr + csiphy_virt))
#define read_csiphy_reg(addr)             \
	__raw_readl((void *)(addr + csiphy_virt))

#define write_dsihost_reg(addr, val)       \
	__raw_writel(val, (void *)(addr + dsiphy_virt))
#define read_dsihost_reg(addr)             \
	__raw_readl((void *)(addr + dsiphy_virt))

#endif
