// SPDX-License-Identifier: GPL-2.0-or-later
/* ASPEED CAN device driver
 *
 * Copyright (C) 2023 ASPEED Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/pm_runtime.h>

#define DRIVER_NAME	"aspeed_can"

#define CAN_AC_SEG		(0x0004) /* classic can seg */
#define CAN_FD_SEG		(0x0008) /* can fd seg */
#define CAN_BITITME		(0x0010) /* prescaler */
#define CAN_INTF		(0x0014) /* can interrupt flag */
#define CAN_INTE		(0x0018) /* can interrupt enabled */
#define CAN_TSTAT		(0x001c) /* can transmit status */

#define CAN_CTRL		(0x0028)
#define CAN_ERR_STAT		(0x002c) /* err ctrl and rx/tx err status */

#define CAN_RBUF		(0x0070) /* receive buffer registers 0x070-0x88b(*/
#define CAN_TBUF		(0x0890) /* transmit buffer registers 0x890-0x10ab */

#define CAN_RBUF_ID		(CAN_RBUF + 0x0000)
#define CAN_RBUF_CTL		(CAN_RBUF + 0x0004)
#define CAN_RBUF_TYPE		(CAN_RBUF + 0x0008)
#define CAN_RBUF_ACF		(CAN_RBUF + 0x000C)
#define CAN_RBUF_DATA		(CAN_RBUF + 0x0010)

#define CAN_TBUF_ID		(CAN_TBUF + 0x0000)
#define CAN_TBUF_CTL		(CAN_TBUF + 0x0004)
#define CAN_TBUF_TYPE		(CAN_TBUF + 0x0008)
#define CAN_TBUF_ACF		(CAN_TBUF + 0x000C)
#define CAN_TBUF_DATA		(CAN_TBUF + 0x0010)

#define CAN_MODE_CONFIG		(0x1100)

#define CAN_TBUF_MIRROR		(0x1190)
#define CAN_TBUF_READ_ID	(CAN_TBUF_MIRROR + 0x0000)
#define CAN_TBUF_READ_CTL	(CAN_TBUF_MIRROR + 0x0004)
#define CAN_TBUF_READ_TYPE	(CAN_TBUF_MIRROR + 0x0008)
#define CAN_TBUF_READ_ACF	(CAN_TBUF_MIRROR + 0x000C)
#define CAN_TBUF_READ_DATA	(CAN_TBUF_MIRROR + 0x0010)

/* CAN_AC_SEG(0x0004) bit description */
#define CAN_TIMING_AC_SEG1_MASK		GENMASK(8, 0)
#define CAN_TIMING_AC_SEG2_MASK		GENMASK(22, 16)
#define CAN_TIMING_AC_SJW_MASK		GENMASK(30, 24)

#define CAN_TIMING_AC_SEG1_BITOFF	0
#define CAN_TIMING_AC_SEG2_BITOFF	16
#define CAN_TIMING_AC_SJW_BITOFF	24

/* CAN_FD_SEG(0x0008) bit description */
#define CAN_TIMING_FD_SEG1_MASK		GENMASK(7, 0)
#define CAN_TIMING_FD_SEG2_MASK		GENMASK(22, 16)
#define CAN_TIMING_FD_SJW_MASK		GENMASK(30, 24)

#define CAN_TIMING_FD_SEG1_BITOFF	0
#define CAN_TIMING_FD_SEG2_BITOFF	16
#define CAN_TIMING_FD_SJW_BITOFF	24

/* CAN_BITTIME(0x0010) bit description */
#define CAN_TIMING_PRESC_MASK		GENMASK(4, 0)
#define CAN_TIMING_PRESC_BITOFF		0
#define CAN_TIMING_FD_SSPOFF_MASK	GENMASK(15, 8)
#define CAN_TIMING_FD_SSPOFF_BITOFF	8

/* CAN_INTF(0x0014) bit description */
#define CAN_INT_EIF_BIT			BIT(1) /* error interrupt flag */
#define CAN_INT_TSIF_BIT		BIT(2) /* transmission secondary interrupt flag */
#define CAN_INT_TPIF_BIT		BIT(3) /* transmission primary interrupt flag */
#define CAN_INT_RAFIF_BIT		BIT(4) /* RB almost full interrupt flag */
#define CAN_INT_RFIF_BIT		BIT(5) /* RB full interrupt flag */
#define CAN_INT_ROIF_BIT		BIT(6) /* RB overflow interrupt flag */
#define CAN_INT_RIF_BIT			BIT(7) /* receive interrupt flag */
#define CAN_INT_BEIF_BIT		BIT(8) /* bus error interrupt flag */
#define CAN_INT_ALIF_BIT		BIT(9) /* arbitration loss interrupt flag */
#define CAN_INT_EPIF_BIT		BIT(10) /* error passive interrupt flag */
#define CAN_EPASS_BIT			BIT(30) /* check if device is error passive */
#define CAN_INT_EWARN_BIT		BIT(31) /* error Warning limit reached */

/* CAN_INTE(0x0018) bit description */
#define CAN_INT_EIE_BIT			BIT(1) /* error interrupt enable */
#define CAN_INT_TSIE_BIT		BIT(2) /* transmission secondary interrupt enable */
#define CAN_INT_TPIE_BIT		BIT(3) /* transmission secondary interrupt enable */
#define CAN_INT_RAFIE_BIT		BIT(4) /* RB almost full interrupt enable */
#define CAN_INT_RFIE_BIT		BIT(5) /* RB full interrupt enable */
#define CAN_INT_ROIE_BIT		BIT(6) /* RB overflow interrupt enable */
#define CAN_INT_RIE_BIT			BIT(7) /* receive interrupt enable */
#define CAN_INT_BEIE_BIT		BIT(8) /* bus error interrupt enable */
#define CAN_INT_ALIE_BIT		BIT(9) /* arbitration loss interrupt enable */
#define CAN_INT_EPIE_BIT		BIT(10) /* error passive interrupt enable */

/* CAN_TSTAT(0x001C) bit description */
#define CAN_TSTAT1_MASK			GENMASK(10, 8)
#define CAN_TSTAT1_BITOFF		8
#define CAN_TSTAT2_MASK			GENMASK(26, 24)
#define CAN_TSTAT2_BITOFF		24

/* CAN_CTRL(0x0028) bit description */
#define CAN_CTRL_BUSOFF_BIT		BIT(0)
#define CAN_CTRL_LBMIMOD_BIT		BIT(5) /* set loop back mode, internal */
#define CAN_CTRL_LBMEMOD_BIT		BIT(6) /* set loop back mode, external */
#define CAN_CTRL_RST_BIT		BIT(7) /* set reset bit */
#define CAN_CTRL_TPE_BIT		BIT(12) /* transmit primary enable */
#define CAN_CTRL_STBY_BIT		BIT(13) /* transceiver standby */
#define CAN_CTRL_TBSEL_BIT		BIT(15) /* message writen in STB */
#define CAN_CTRL_TTBM_BIT		BIT(20) /* set TTTBM as 1->full TTCAN mode */
#define CAN_CTRL_TSMODE_BIT		BIT(21) /* set TSMODE as 1->FIFO mode */
#define CAN_CTRL_TSNEXT_BIT		BIT(22) /* transmit buffer secondary NEXT */
#define CAN_CTRL_RSTAT_NOT_EMPTY_MASKT	GENMASK(25, 24)
#define CAN_CTRL_RREL_BIT		BIT(28) /* receive buffer release */

/* CAN_ERR(0x002c) bit description */
#define CAN_ERR_EWL_MASK		GENMASK(3, 0) /* programmable error warning limit */
#define CAN_ERR_EWL_BITOFF		0
#define CAN_ERR_AFWL_MASK		GENMASK(7, 4) /* receive buffer almost full warning limit */
#define CAN_ERR_AFWL_BITOFF		4
#define CAN_ERR_ALC_MASK		GENMASK(12, 8) /* arbitration lost capture */
#define CAN_ERR_ALC_BITOFF		8
#define CAN_ERR_KOER_MASK		GENMASK(15, 13) /* kind of error */
#define CAN_ERR_KOER_BITOFF		13
#define CAN_ERR_RECNT_MASK		GENMASK(23, 16) /* receive error count */
#define CAN_ERR_RECNT_BITOFF		16
#define CAN_ERR_TECNT_MASK		GENMASK(31, 24) /* transmit error count */
#define CAN_ERR_TECNT_BITOFF		24

/* CAN BUF bit description*/
#define CAN_BUF_ID_BFF_MASK		GENMASK(28, 18) /* frame identifier */
#define CAN_BUF_ID_BFF_BITOFF		18
#define CAN_BUF_ID_EFF_MASK		GENMASK(28, 0) /* identifier extension */
#define CAN_BUF_ID_EFF_BITOFF		0
#define CAN_BUF_DLC_MASK		GENMASK(10, 0) /* data length code */
#define CAN_BUF_IDE_BIT			BIT(16) /* identifier extension */
#define CAN_BUF_FDF_BIT			BIT(17) /* CAN FD frame format */
#define CAN_BUF_BRS_BIT			BIT(18) /* CAN FD bit rate switch enable */
#define CAN_BUF_RMF_BIT			BIT(20) /* remot frame */

#define KOER_BIT_ERROR_MASK		(BIT(0))
#define KOER_FORM_ERROR_MASK		(BIT(1))
#define KOER_STUFF_ERROR_MASK		(BIT(1) | BIT(0))
#define KOER_ACK_ERROR_MASK		(BIT(2))
#define KOER_CRC_ERROR_MASK		(BIT(2) | BIT(0))
#define KOER_OTH_ERROR_MASK		(BIT(2) | BIT(1))

#define STAT_AFWL			0x03
#define STAT_EWL			0x0b

struct aspeed_can_data {
	u32 flags;
	const struct can_bittiming_const *bittiming_const;
	const struct can_bittiming_const *data_bittiming_const;
	u32 btr_ts2_shift;
	u32 btr_sjw_shift;
	u32 btr_fdssp_shift;
};

struct aspeed_can_priv {
	/* Fix the location of "struct can_priv can"
	 * in this struct.
	 */
	struct can_priv can;
	void __iomem *reg_base;
	struct device *dev;
	/* Lock for synchronizing TX interrupt handling */
	spinlock_t tx_lock;
	u32 tx_head;
	u32 tx_tail;
	u32 tx_max;
	struct napi_struct napi;
	unsigned long irq_flags;
	struct clk *clk;
	struct reset_control *reset;
	const struct aspeed_can_data *data;
};

static const struct can_bittiming_const aspeed_can_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 2,
	.tseg1_max = 513,
	.tseg2_min = 1,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 128,
	.brp_inc = 1,
};

static const struct can_bittiming_const aspeed_canfd_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 2,
	.tseg1_max = 257,
	.tseg2_min = 1,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 128,
	.brp_inc = 1,
};

inline void aspeed_can_set_bit(struct aspeed_can_priv *priv,
			       u32 reg_off, u32 bit)
{
	u32 reg_val;

	reg_val = readl(priv->reg_base + reg_off);
	reg_val |= bit;
	writel(reg_val, priv->reg_base + reg_off);
}

inline void aspeed_can_clr_bit(struct aspeed_can_priv *priv,
			       u32 reg_off, u32 bit)
{
	u32 reg_val;

	reg_val = readl(priv->reg_base + reg_off);
	reg_val &= ~(bit);
	writel(reg_val, priv->reg_base + reg_off);
}

inline void aspeed_can_clr_irq_bits(struct aspeed_can_priv *priv,
				    u32 reg_off, u32 bits)
{
	writel(bits, priv->reg_base + reg_off);
}

static bool aspeed_can_check_reset_mode(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);

	if (!(readl(priv->reg_base + CAN_CTRL) & CAN_CTRL_RST_BIT))
		return false;

	return true;
}

static int aspeed_can_set_reset_mode(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	unsigned long timeout;

	aspeed_can_set_bit(priv, CAN_CTRL, CAN_CTRL_RST_BIT);

	timeout = jiffies + (1 * HZ);
	while (!aspeed_can_check_reset_mode(ndev)) {
		if (time_after(jiffies, timeout)) {
			netdev_warn(ndev, "timed out for config mode\n");
			return -ETIMEDOUT;
		}

		usleep_range(500, 10000);
	}

	priv->tx_head = 0;
	priv->tx_tail = 0;

	return 0;
}

static int aspeed_can_exit_reset_mode(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	unsigned long timeout;

	aspeed_can_clr_bit(priv, CAN_CTRL, CAN_CTRL_RST_BIT);

	timeout = jiffies + (1 * HZ);
	while (aspeed_can_check_reset_mode(ndev)) {
		if (time_after(jiffies, timeout)) {
			netdev_warn(ndev, "timed out for config mode\n");
			return -ETIMEDOUT;
		}

		usleep_range(500, 10000);
	}

	return 0;
}

void aspeed_can_err_init(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	u32 reg_val;

	reg_val = (STAT_AFWL << CAN_ERR_AFWL_BITOFF) & CAN_ERR_AFWL_MASK;
	reg_val |= (STAT_EWL << CAN_ERR_EWL_BITOFF) & CAN_ERR_EWL_MASK;

	writel(reg_val, priv->reg_base + CAN_ERR_STAT);
}

void aspeed_can_interrupt_conf(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	u32 inte;

	inte = CAN_INT_EIE_BIT | CAN_INT_TSIE_BIT | CAN_INT_TPIE_BIT |
	       CAN_INT_RAFIE_BIT | CAN_INT_RFIE_BIT | CAN_INT_ROIE_BIT |
	       CAN_INT_RIE_BIT | CAN_INT_BEIE_BIT | CAN_INT_ALIE_BIT |
	       CAN_INT_EPIE_BIT;

	writel(inte, priv->reg_base + CAN_INTE);
}

void aspeed_can_loopback_ext_conf(struct net_device *ndev, bool enable)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);

	if (enable)
		aspeed_can_set_bit(priv, CAN_CTRL, CAN_CTRL_LBMEMOD_BIT);
	else
		aspeed_can_clr_bit(priv, CAN_CTRL, CAN_CTRL_LBMEMOD_BIT);
}

void aspeed_can_loopback_int_conf(struct net_device *ndev, bool enable)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);

	if (enable)
		aspeed_can_set_bit(priv, CAN_CTRL, CAN_CTRL_LBMIMOD_BIT);
	else
		aspeed_can_clr_bit(priv, CAN_CTRL, CAN_CTRL_LBMIMOD_BIT);
}

void aspeed_can_fd_init(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);

	aspeed_can_set_bit(priv, CAN_MODE_CONFIG, BIT(0));
}

static void aspeed_can_reg_dump(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	u32 reg_val;
	u32 i;

	reg_val = readl(priv->reg_base + CAN_AC_SEG);
	netdev_info(ndev, "(REG004) CAN_AC_SEG = 0x%08x\n", reg_val);
	netdev_info(ndev, "         ac_seg1  (8:0): 0x%02x\n",
		    (u32)((reg_val & CAN_TIMING_AC_SEG1_MASK) >>
			  CAN_TIMING_AC_SEG1_BITOFF));
	netdev_info(ndev, "         ac_seg2(22:16): 0x%02x\n",
		    (u32)((reg_val & CAN_TIMING_AC_SEG2_MASK) >>
			  CAN_TIMING_AC_SEG2_BITOFF));
	netdev_info(ndev, "         ac_sjw (30:24): 0x%02x\n",
		    (u32)((reg_val & CAN_TIMING_AC_SJW_MASK) >>
			  CAN_TIMING_AC_SJW_BITOFF));

	reg_val = readl(priv->reg_base + CAN_FD_SEG);
	netdev_info(ndev, "(REG008) CAN_FD_SEG = 0x%08x\n", reg_val);
	netdev_info(ndev, "         fd_seg1  (7:0): 0x%02x\n",
		    (u32)((reg_val & CAN_TIMING_FD_SEG1_MASK) >>
			  CAN_TIMING_FD_SEG1_BITOFF));
	netdev_info(ndev, "         fd_seg2(22:16): 0x%02x\n",
		    (u32)((reg_val & CAN_TIMING_FD_SEG2_MASK) >>
			  CAN_TIMING_FD_SEG2_BITOFF));
	netdev_info(ndev, "         fd_sjw (30:24): 0x%02x\n",
		    (u32)((reg_val & CAN_TIMING_FD_SJW_MASK) >>
			  CAN_TIMING_FD_SJW_BITOFF));

	reg_val = readl(priv->reg_base + CAN_BITITME);
	netdev_info(ndev, "(REG010) CAN_BITITME = 0x%08x\n", reg_val);
	netdev_info(ndev, "	 prescaler (4:0): 0x%02x\n",
		    (u32)((reg_val & CAN_TIMING_PRESC_MASK) >>
			  CAN_TIMING_PRESC_BITOFF));
	netdev_info(ndev, "	 fd_sspoff(15:8): 0x%02x\n",
		    (u32)((reg_val & CAN_TIMING_FD_SSPOFF_MASK) >>
			  CAN_TIMING_FD_SSPOFF_BITOFF));

	reg_val = readl(priv->reg_base + CAN_INTF);
	netdev_info(ndev, "(REG014) CAN_INTF = 0x%08x\n", reg_val);
	netdev_info(ndev, "	 EIF   (1): %d\n", (reg_val & CAN_INT_EIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TSIF  (2): %d\n", (reg_val & CAN_INT_TSIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TPIF  (3): %d\n", (reg_val & CAN_INT_TPIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 RAFIF (4): %d\n", (reg_val & CAN_INT_RAFIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 RFIF  (5): %d\n", (reg_val & CAN_INT_RFIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 ROIF  (6): %d\n", (reg_val & CAN_INT_ROIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 RIF   (7): %d\n", (reg_val & CAN_INT_RIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 BEIF  (8): %d\n", (reg_val & CAN_INT_BEIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 EPIF (10): %d\n", (reg_val & CAN_INT_EPIF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 EPASS(30): %d\n", (reg_val & CAN_EPASS_BIT) ? 1 : 0);
	netdev_info(ndev, "	 EWARN(31): %d\n", (reg_val & CAN_INT_EWARN_BIT) ? 1 : 0);

	reg_val = readl(priv->reg_base + CAN_INTE);
	netdev_info(ndev, "(REG018) CAN_INTE = 0x%08x\n", reg_val);
	netdev_info(ndev, "	 EIE   (1): %d\n", (reg_val & CAN_INT_EIE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TSIE  (2): %d\n", (reg_val & CAN_INT_TSIE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TPIE  (3): %d\n", (reg_val & CAN_INT_TPIE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 RAFIE (4): %d\n", (reg_val & CAN_INT_RAFIE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 RFIE  (5): %d\n", (reg_val & CAN_INT_RFIE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 ROIE  (6): %d\n", (reg_val & CAN_INT_ROIE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 RIE   (7): %d\n", (reg_val & CAN_INT_RIE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 BEIE  (8): %d\n", (reg_val & CAN_INT_BEIE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 EPIE (10): %d\n", (reg_val & CAN_INT_EPIE_BIT) ? 1 : 0);

	reg_val = readl(priv->reg_base + CAN_TSTAT);
	netdev_info(ndev, "(REG01C) CAN_TSTAT = 0x%08x\n", reg_val);
	netdev_info(ndev, "	 TSTAT1 (10:8): 0x%02x\n",
		    (u32)((reg_val & CAN_TSTAT1_MASK) >> CAN_TSTAT1_BITOFF));
	netdev_info(ndev, "	 TSTAT2(26:24): 0x%02x\n",
		    (u32)((reg_val & CAN_TSTAT2_MASK) >> CAN_TSTAT2_BITOFF));
	netdev_info(ndev, "	     000 : no tx\n");
	netdev_info(ndev, "	     001 : on-going\n");
	netdev_info(ndev, "	     010 : lost arbitration\n");
	netdev_info(ndev, "	     011 : transmitted\n");
	netdev_info(ndev, "	     100 : aborted\n");
	netdev_info(ndev, "	     101 : disturbed\n");
	netdev_info(ndev, "	     110 : reject\n");

	reg_val = readl(priv->reg_base + CAN_CTRL);
	netdev_info(ndev, "(REG028) CAN_CTRL = 0x%08x\n", reg_val);
	netdev_info(ndev, "	 BUSOFF     (0): %d\n", (reg_val & CAN_CTRL_BUSOFF_BIT) ? 1 : 0);
	netdev_info(ndev, "	 LBMIMOD    (5): %d\n", (reg_val & CAN_CTRL_LBMIMOD_BIT) ? 1 : 0);
	netdev_info(ndev, "	 LBMEMOD    (6): %d\n", (reg_val & CAN_CTRL_LBMEMOD_BIT) ? 1 : 0);
	netdev_info(ndev, "	 RST        (7): %d\n", (reg_val & CAN_CTRL_RST_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TPE       (12): %d\n", (reg_val & CAN_CTRL_TPE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 STBY      (13): %d\n", (reg_val & CAN_CTRL_STBY_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TBSEL     (15): %d\n", (reg_val & CAN_CTRL_TBSEL_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TTBM      (20): %d\n", (reg_val & CAN_CTRL_TTBM_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TSMODE    (21): %d\n", (reg_val & CAN_CTRL_TSMODE_BIT) ? 1 : 0);
	netdev_info(ndev, "	 TSNEXT    (22): %d\n", (reg_val & CAN_CTRL_TSNEXT_BIT) ? 1 : 0);
	netdev_info(ndev, "	 RSTAT  (25:24): 0x%02x\n",
		    (u32)((reg_val & CAN_CTRL_RSTAT_NOT_EMPTY_MASKT) >> 24));
	netdev_info(ndev, "	 RREL      (28): %d\n", (reg_val & CAN_CTRL_RREL_BIT) ? 1 : 0);

	reg_val = readl(priv->reg_base + CAN_ERR_STAT);
	netdev_info(ndev, "(REG02C) ERR_STAT = 0x%08x\n", reg_val);
	netdev_info(ndev, "	 EWL     (3:0): 0x%02x\n",
		    (u32)((reg_val & CAN_ERR_EWL_MASK) >> CAN_ERR_EWL_BITOFF));
	netdev_info(ndev, "	 AFWL    (7:4): 0x%02x\n",
		    (u32)((reg_val & CAN_ERR_AFWL_MASK) >> CAN_ERR_AFWL_BITOFF));
	netdev_info(ndev, "	 ALC    (12:8): 0x%02x\n",
		    (u32)((reg_val & CAN_ERR_ALC_MASK) >> CAN_ERR_ALC_BITOFF));
	netdev_info(ndev, "	 KOER  (15:13): 0x%02x\n",
		    (u32)((reg_val & CAN_ERR_KOER_MASK) >> CAN_ERR_KOER_BITOFF));
	netdev_info(ndev, "             000 : no error\n");
	netdev_info(ndev, "             001 : bit error\n");
	netdev_info(ndev, "             010 : form error\n");
	netdev_info(ndev, "             011 : stuff error\n");
	netdev_info(ndev, "             100 : ack error\n");
	netdev_info(ndev, "             101 : crc error\n");
	netdev_info(ndev, "             110 : other error\n");
	netdev_info(ndev, "	 RECNT (23:16): 0x%02x\n",
		    (u32)((reg_val & CAN_ERR_RECNT_MASK) >> CAN_ERR_RECNT_BITOFF));
	netdev_info(ndev, "	 TECNT (31:24): 0x%02x\n",
		    (u32)((reg_val & CAN_ERR_TECNT_MASK) >> CAN_ERR_TECNT_BITOFF));

	for (i = 0; i < 0x50; i += 4)
		netdev_info(ndev, "REG(%03x): 0x%08x\n", i, readl(priv->reg_base + i));
}

static int aspeed_can_set_bittiming(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct can_bittiming *dbt = &priv->can.data_bittiming;
	u32 btr0, btr1;
	u32 can_fd_ssp;

	/* check whether the CAN controller is in reset mode */
	if (!aspeed_can_check_reset_mode(ndev)) {
		netdev_alert(ndev,
			     "BUG! Cannot set bittiming - not in reset mode\n");
		return -EPERM;
	}

	/* parameter sanity */
	if (bt->brp < 1 || bt->prop_seg + bt->phase_seg1 < 1 ||
	    bt->phase_seg2 < 1 || bt->sjw < 1) {
		netdev_alert(ndev,
			     "invalid bittiming parameters\n");
		netdev_alert(ndev,
			     "brp: %x, prop: %x, seg1: %x, seg2: %x, sjw: %x\n",
			     bt->brp, bt->prop_seg, bt->phase_seg1,
			     bt->phase_seg2, bt->sjw);
		return -EPERM;
	}

	/* setting prescaler value in PRESC Register */
	btr0 = (bt->brp - 1);

	/* setting time segment 1 in SEG_1 Register */
	btr1 = (1 + bt->prop_seg + bt->phase_seg1 - 2);

	/* Setting Time Segment 2 in SEG_2 Register */
	btr1 |= (bt->phase_seg2 - 1) << priv->data->btr_ts2_shift;

	/* Setting Synchronous jump width in BTR Register */
	btr1 |= (bt->sjw - 1) << priv->data->btr_sjw_shift;

	writel(btr1, priv->reg_base + CAN_AC_SEG);

	if (dbt->prop_seg != 0 && dbt->phase_seg1 != 0 &&
	    dbt->phase_seg2 != 0) {
		if (bt->brp != dbt->brp) {
			netdev_alert(ndev,
				     "nominal (%d) and data (%d) prescaler isn't the same\n",
				     bt->brp, dbt->brp);
			return -EPERM;
		}

		if (dbt->brp < 1 || dbt->prop_seg + dbt->phase_seg1 < 1 ||
		    dbt->phase_seg2 < 1 || bt->sjw < 1) {
			netdev_alert(ndev,
				     "invalid data bittiming parameters\n");
			netdev_alert(ndev,
				     "brp: %x, prop: %x, seg1: %x, seg2: %x, sjw: %x\n",
				     dbt->brp, dbt->prop_seg, dbt->phase_seg1,
				     dbt->phase_seg2, dbt->sjw);
			return -EPERM;
		}

		/* Setting Time Segment 1 in BTR Register */
		btr1 = 1 + dbt->prop_seg + dbt->phase_seg1 - 2;

		/* Setting Time Segment 2 in BTR Register */
		btr1 |= (dbt->phase_seg2 - 1) << priv->data->btr_ts2_shift;

		/* Setting Synchronous jump width in BTR Register */
		btr1 |= (dbt->sjw - 1) << priv->data->btr_sjw_shift;

		writel(btr1, priv->reg_base + CAN_FD_SEG);

		/* seg_1 + 1 */
		can_fd_ssp = 1 + dbt->prop_seg + dbt->phase_seg1 + 1;

		btr0 |= can_fd_ssp << priv->data->btr_fdssp_shift;

		netdev_dbg(ndev, "btr0: 0x%08x, brp: 0x%08x, seg_1: 0x%08x, seg_2: 0x%08x, sjw: %x\n",
			   btr0, bt->brp, 1 + bt->prop_seg + bt->phase_seg1, bt->phase_seg2, bt->sjw);

		netdev_dbg(ndev, "FD btr1: 0x%08x, seg_1: 0x%08x, seg_2: 0x%08x, sjw: %x, ssp: %x\n",
			   btr1, dbt->prop_seg + dbt->phase_seg1, dbt->phase_seg2, dbt->sjw, can_fd_ssp);
	}

	writel(btr0, priv->reg_base + CAN_BITITME);

	return 0;
}

static int aspeed_can_chip_start(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	int err;

	/* Check if it is in reset mode */
	err = aspeed_can_set_reset_mode(ndev);
	if (err < 0)
		return err;

	err = aspeed_can_set_bittiming(ndev);
	if (err < 0)
		return err;

	aspeed_can_fd_init(ndev);

	aspeed_can_err_init(ndev);
	aspeed_can_interrupt_conf(ndev);

	aspeed_can_loopback_ext_conf(ndev, false);
	aspeed_can_loopback_int_conf(ndev, false);

	/* PTB mode */
	aspeed_can_clr_bit(priv, CAN_CTRL, CAN_CTRL_TBSEL_BIT);

	err = aspeed_can_exit_reset_mode(ndev);
	if (err < 0)
		return err;

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;
}

static int aspeed_can_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int ret;

	switch (mode) {
	case CAN_MODE_START:
		ret = aspeed_can_chip_start(ndev);
		if (ret < 0) {
			netdev_err(ndev, "aspeed_can_chip_start failed!\n");
			return ret;
		}
		netif_wake_queue(ndev);
		break;
	default:
		netdev_err(ndev, "unexpect can mode: %d\n", (u32)mode);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static void aspeed_can_write_frame(struct net_device *ndev,
				   struct sk_buff *skb)
{
	u32 id;
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u32 i;
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	u32 buf_ctrl = 0;

	netdev_dbg(ndev, "can_id = 0x%08x\n", cf->can_id);
	/* Watch carefully on the bit sequence */
	if (cf->can_id & CAN_EFF_FLAG) {
		id = (cf->can_id & CAN_EFF_MASK) << CAN_BUF_ID_EFF_BITOFF;
		buf_ctrl |= CAN_BUF_IDE_BIT;
	} else {
		/* Standard CAN ID format */
		id = (cf->can_id & CAN_SFF_MASK) << CAN_BUF_ID_BFF_BITOFF;
	}

	if (cf->can_id & CAN_RTR_FLAG)
		buf_ctrl |= CAN_BUF_RMF_BIT;

	buf_ctrl |= can_fd_len2dlc(cf->len);

	if (can_is_canfd_skb(skb)) {
		buf_ctrl |= CAN_BUF_FDF_BIT;
		if (cf->flags & CANFD_BRS)
			buf_ctrl |= CAN_BUF_BRS_BIT;
	}

	can_put_echo_skb(skb, ndev, 0, 0);

	priv->tx_head++;

	writel(id, priv->reg_base + CAN_TBUF_ID);
	writel(buf_ctrl, priv->reg_base + CAN_TBUF_CTL);
	netdev_dbg(ndev, "TX id (Mirror): 0x%08x\n",
		   readl(priv->reg_base + CAN_TBUF_READ_ID));
	netdev_dbg(ndev, "TX ctrl (M): 0x%08x\n",
		   readl(priv->reg_base + CAN_TBUF_READ_CTL));

	writel(0x0, priv->reg_base + CAN_TBUF_TYPE);
	writel(0x0, priv->reg_base + CAN_TBUF_ACF);

	if (cf->can_id & CAN_RTR_FLAG)
		return;

	for (i = 0; i < (cf->len / 4 + 3) * 4; i += 4) {
		netdev_dbg(ndev, "data(%d): 0x%08x\n", i, *(u32 *)(cf->data + i));
		writel(*(u32 *)(cf->data + i),
		       priv->reg_base + CAN_TBUF_DATA + i);

		if (i < 8) {
			netdev_dbg(ndev, "TX BUF (0x%02x) 0x%08x",
				   i,
				   readl(priv->reg_base + CAN_TBUF_DATA + i));
		}
	}
}

static int aspeed_can_start_frame_xmit(struct sk_buff *skb,
				       struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	unsigned long flags;

	/* something has not been transferred */
	if (readl(priv->reg_base + CAN_CTRL) & CAN_CTRL_TPE_BIT)
		return -ENOSPC;

	spin_lock_irqsave(&priv->tx_lock, flags);

	netif_stop_queue(ndev);

	aspeed_can_write_frame(ndev, skb);

	/* Mark buffer as ready for transmit */
	aspeed_can_set_bit(priv, CAN_CTRL, CAN_CTRL_TPE_BIT);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return 0;
}

static netdev_tx_t aspeed_can_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	int ret;

	if (can_dev_dropped_skb(ndev, skb))
		return NETDEV_TX_OK;

	ret = aspeed_can_start_frame_xmit(skb, ndev);
	if (ret < 0) {
		netdev_err(ndev, "BUG!, TX full when queue awake!\n");
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

static int aspeed_can_rx(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	u32 data;
	u32 rx_stat;
	u32 buf_ctrl_reg;
	u32 reg_val;
	u32 i;

	rx_stat = readl(priv->reg_base + CAN_CTRL);
	if (!(rx_stat & CAN_CTRL_RSTAT_NOT_EMPTY_MASKT))
		return 0;

	buf_ctrl_reg = readl(priv->reg_base + CAN_RBUF_CTL);
	if (buf_ctrl_reg & CAN_BUF_FDF_BIT)
		skb = alloc_canfd_skb(ndev, &cf);
	else
		skb = alloc_can_skb(ndev, (struct can_frame **)&cf);

	if (!skb) {
		stats->rx_dropped++;
		return 0;
	}

	reg_val = readl(priv->reg_base + CAN_RBUF_ID);
	netdev_dbg(ndev, "buf_ctrl_reg = 0x%08x\n", buf_ctrl_reg);
	if (buf_ctrl_reg & CAN_BUF_IDE_BIT) {
		cf->can_id = reg_val & CAN_BUF_ID_EFF_MASK;
		cf->can_id |= CAN_EFF_FLAG;
		netdev_dbg(ndev, "EXT CAN ID: 0x%08x\n", cf->can_id);
	} else {
		cf->can_id = (reg_val & CAN_BUF_ID_BFF_MASK) >>
			     CAN_BUF_ID_BFF_BITOFF;
		netdev_dbg(ndev, "CAN ID: 0x%08x\n", (u32)(reg_val >> 18));
	}

	if (buf_ctrl_reg & CAN_BUF_RMF_BIT)
		cf->can_id |= CAN_RTR_FLAG;

	if (buf_ctrl_reg & CAN_BUF_FDF_BIT)
		cf->len = can_fd_dlc2len(buf_ctrl_reg & CAN_BUF_DLC_MASK);
	else
		cf->len = can_cc_dlc2len(buf_ctrl_reg & CAN_BUF_DLC_MASK);

	/* Check the frame received is FD or not*/
	for (i = 0; i < cf->len; i += 4) {
		data = readl(priv->reg_base + CAN_RBUF_DATA + i);
		*(u32 *)(cf->data + i) = data;
		netdev_dbg(ndev, "CAN RX DATA (0x%02x) 0x%08x\n",
			   i, *(u32 *)(cf->data + i));
	}

	if (!(cf->can_id & CAN_RTR_FLAG))
		stats->rx_bytes += cf->len;

	stats->rx_packets++;

	/* release frame */
	aspeed_can_set_bit(priv, CAN_CTRL, CAN_CTRL_RREL_BIT);

	netif_receive_skb(skb);

	return 1;
}

static enum can_state aspeed_can_current_error_state(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	u32 status;
	u32 rx_cnt;
	u32 tx_cnt;
	u32 ctrl;

	ctrl = readl(priv->reg_base + CAN_CTRL);
	status = readl(priv->reg_base + CAN_INTF);
	rx_cnt = (readl(priv->reg_base + CAN_ERR_STAT) & CAN_ERR_RECNT_MASK) >>
		 CAN_ERR_RECNT_BITOFF;
	tx_cnt = (readl(priv->reg_base + CAN_ERR_STAT) & CAN_ERR_TECNT_MASK) >>
		 CAN_ERR_TECNT_BITOFF;

	if (ctrl & CAN_CTRL_BUSOFF_BIT)
		return CAN_STATE_BUS_OFF;
	else if ((status & CAN_EPASS_BIT) == CAN_EPASS_BIT)
		return CAN_STATE_ERROR_PASSIVE;
	else if (rx_cnt > 96 || tx_cnt > 96)
		return CAN_STATE_ERROR_WARNING;
	else
		return CAN_STATE_ERROR_ACTIVE;
}

static void aspeed_can_set_error_state(struct net_device *ndev,
				       enum can_state new_state,
				       struct can_frame *cf)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	u32 ecr = readl(priv->reg_base + CAN_ERR_STAT);
	u32 txerr = (ecr & CAN_ERR_RECNT_MASK) >> CAN_ERR_RECNT_BITOFF;
	u32 rxerr = (ecr & CAN_ERR_TECNT_MASK) >> CAN_ERR_TECNT_BITOFF;
	enum can_state tx_state = txerr >= rxerr ? new_state : 0;
	enum can_state rx_state = txerr <= rxerr ? new_state : 0;

	/* non-ERROR states are handled elsewhere */
	if (WARN_ON(new_state > CAN_STATE_ERROR_PASSIVE))
		return;

	can_change_state(ndev, cf, tx_state, rx_state);

	if (cf) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}
}

static void aspeed_can_update_error_state_after_rxtx(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	enum can_state old_state = priv->can.state;
	enum can_state new_state;

	/* changing error state due to successful frame RX/TX can only
	 * occur from these states
	 */
	if (old_state != CAN_STATE_ERROR_WARNING &&
	    old_state != CAN_STATE_ERROR_PASSIVE)
		return;

	new_state = aspeed_can_current_error_state(ndev);

	if (new_state != old_state) {
		struct sk_buff *skb;
		struct can_frame *cf;

		skb = alloc_can_err_skb(ndev, &cf);

		aspeed_can_set_error_state(ndev, new_state, skb ? cf : NULL);

		if (skb)
			netif_rx(skb);
	}
}

static void aspeed_can_err_interrupt(struct net_device *ndev, u32 isr)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame cf = { };
	u32 err;
	u32 ctrl;
	u32 koer;
	u32 recnt;
	u32 tecnt;

	netdev_err(ndev, "in error interrupt.\n");

	aspeed_can_reg_dump(ndev);

	err = readl(priv->reg_base + CAN_ERR_STAT);
	ctrl = readl(priv->reg_base + CAN_CTRL);

	koer = (err & CAN_ERR_KOER_MASK) >> CAN_ERR_KOER_BITOFF;
	recnt = (err & CAN_ERR_RECNT_MASK) >> CAN_ERR_RECNT_BITOFF;
	tecnt = (err & CAN_ERR_TECNT_MASK) >> CAN_ERR_TECNT_BITOFF;

	if (ctrl & CAN_CTRL_BUSOFF_BIT) {
		priv->can.state = CAN_STATE_BUS_OFF;
		priv->can.can_stats.bus_off++;
		/* Leave device in Config Mode in bus-off state */
		aspeed_can_set_bit(priv, CAN_CTRL, CAN_CTRL_RST_BIT);
		can_bus_off(ndev);
		cf.can_id |= CAN_ERR_BUSOFF;
	} else {
		enum can_state new_state = aspeed_can_current_error_state(ndev);

		if (new_state != priv->can.state)
			aspeed_can_set_error_state(ndev, new_state, &cf);
	}

	if (isr & CAN_INT_ALIF_BIT) {
		priv->can.can_stats.arbitration_lost++;
		cf.can_id |= CAN_ERR_LOSTARB;
		cf.data[0] = CAN_ERR_LOSTARB_UNSPEC;
	}

	if (isr & CAN_INT_ROIF_BIT) {
		stats->rx_over_errors++;
		stats->rx_errors++;
		cf.can_id |= CAN_ERR_CRTL;
		cf.data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
	}

	/* Check for error interrupt */
	if (isr & CAN_INT_BEIF_BIT) {
		bool berr_reporting = false;

		if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING) {
			berr_reporting = true;
			cf.can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
		}

		if (koer == KOER_ACK_ERROR_MASK) {
			netdev_err(ndev, "ACK error exists\n");
			stats->tx_errors++;
			if (berr_reporting) {
				cf.can_id |= CAN_ERR_ACK;
				cf.data[3] = CAN_ERR_PROT_LOC_ACK;
			}
		}

		if (koer == KOER_BIT_ERROR_MASK) {
			netdev_err(ndev, "BIT error exists\n");
			stats->tx_errors++;
			if (berr_reporting) {
				cf.can_id |= CAN_ERR_PROT;
				cf.data[2] = CAN_ERR_PROT_BIT;
			}
		}

		if (koer == KOER_STUFF_ERROR_MASK) {
			netdev_err(ndev, "STUFF error exists\n");
			stats->rx_errors++;
			if (berr_reporting) {
				cf.can_id |= CAN_ERR_PROT;
				cf.data[2] = CAN_ERR_PROT_STUFF;
			}
		}

		if (koer == KOER_FORM_ERROR_MASK) {
			netdev_err(ndev, "FORM error exists\n");
			stats->rx_errors++;
			if (berr_reporting) {
				cf.can_id |= CAN_ERR_PROT;
				cf.data[2] = CAN_ERR_PROT_FORM;
			}
		}

		if (koer == KOER_CRC_ERROR_MASK) {
			netdev_err(ndev, "CRC error exists\n");
			stats->rx_errors++;
			if (berr_reporting) {
				cf.can_id |= CAN_ERR_PROT;
				cf.data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
			}
		}

		priv->can.can_stats.bus_error++;
	}

	if (cf.can_id) {
		struct can_frame *skb_cf;
		struct sk_buff *skb = alloc_can_err_skb(ndev, &skb_cf);

		if (skb) {
			skb_cf->can_id |= cf.can_id;
			memcpy(skb_cf->data, cf.data, CAN_ERR_DLC);
			netif_rx(skb);
		}
	}
}

static int aspeed_can_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	int work_done = 0;
	u32 rx_stat;
	u32 ier;

	netdev_dbg(ndev, "quota = %x\n", quota);

	rx_stat = readl(priv->reg_base + CAN_CTRL);

	while ((rx_stat & CAN_CTRL_RSTAT_NOT_EMPTY_MASKT) != 0 &&
	       (work_done < quota)) {
		netdev_dbg(ndev, "work_done = %d\n", work_done);
		work_done += aspeed_can_rx(ndev);
		rx_stat = readl(priv->reg_base + CAN_CTRL);
	}

	if (work_done)
		aspeed_can_update_error_state_after_rxtx(ndev);

	if (work_done < quota) {
		if (napi_complete_done(napi, work_done)) {
			ier = readl(priv->reg_base + CAN_INTE);
			ier |= CAN_INT_RIE_BIT;
			writel(ier, priv->reg_base + CAN_INTE);
		}
	}

	return work_done;
}

static void aspeed_can_tx_interrupt(struct net_device *ndev, u32 intr_flag)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	unsigned int frames_in_fifo;
	int frames_sent = 1; /* TXOK => at least 1 frame was sent */
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_lock, flags);

	frames_in_fifo = priv->tx_head - priv->tx_tail;

	if (WARN_ON_ONCE(frames_in_fifo != 1)) {
		if (intr_flag & CAN_INT_TPIF_BIT) {
			aspeed_can_clr_irq_bits(priv, CAN_INTF, CAN_INT_TPIF_BIT);
		} else {
			netdev_warn(ndev, "TSIF is preparing...\n");
			aspeed_can_clr_irq_bits(priv, CAN_INTF, CAN_INT_TSIF_BIT);
		}

		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return;
	}

	if (intr_flag & CAN_INT_TPIF_BIT) {
		aspeed_can_clr_irq_bits(priv, CAN_INTF, CAN_INT_TPIF_BIT);
	} else {
		netdev_warn(ndev, "TSIF is preparing...\n");
		aspeed_can_clr_irq_bits(priv, CAN_INTF, CAN_INT_TSIF_BIT);
	}

	while (frames_sent--) {
		stats->tx_bytes += can_get_echo_skb(ndev, priv->tx_tail %
						    priv->tx_max, NULL);
		priv->tx_tail++;
		stats->tx_packets++;
	}

	netif_wake_queue(ndev);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	aspeed_can_update_error_state_after_rxtx(ndev);
}

static irqreturn_t aspeed_can_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	u32 isr;
	u32 ier;
	u32 isr_errors;
	u32 rx_int_mask = CAN_INT_RIF_BIT;

	isr = readl(priv->reg_base + CAN_INTF);
	if (!isr)
		return IRQ_NONE;

	netdev_dbg(ndev, "isr = 0x%08x\n", isr);

	/* Check for Tx interrupt and Processing it */
	if (isr & (CAN_INT_TPIF_BIT | CAN_INT_TSIF_BIT))
		aspeed_can_tx_interrupt(ndev, isr);

	if (isr & CAN_INT_RAFIF_BIT) {
		netdev_warn(ndev, "Receive buffer is almost full\n");
		aspeed_can_clr_irq_bits(priv, CAN_INTF, CAN_INT_RAFIF_BIT);
	}

	if (isr & CAN_INT_RFIF_BIT) {
		netdev_warn(ndev, "Receive buffer is full\n");
		aspeed_can_clr_irq_bits(priv, CAN_INTF, CAN_INT_RFIF_BIT);
	}

	/* Check for the type of error interrupt and Processing it */
	isr_errors = isr & (CAN_INT_EIF_BIT | CAN_INT_ROIF_BIT |
			    CAN_INT_BEIF_BIT | CAN_INT_ALIF_BIT |
			    CAN_INT_EPIF_BIT | CAN_INT_EWARN_BIT);
	if (isr_errors) {
		aspeed_can_clr_irq_bits(priv, CAN_INTF, isr_errors);
		aspeed_can_err_interrupt(ndev, isr);
	}

	/* Check for the type of receive interrupt and Processing it */
	if (isr & rx_int_mask) {
		aspeed_can_clr_irq_bits(priv, CAN_INTF, rx_int_mask);
		ier = readl(priv->reg_base + CAN_INTE);
		ier &= ~rx_int_mask; /* CAN_INT_RIE_BIT */
		writel(ier, priv->reg_base + CAN_INTE);
		napi_schedule(&priv->napi);
	}

	return IRQ_HANDLED;
}

static void aspeed_can_chip_stop(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	int ret;

	/* Disable interrupts and leave the can in configuration mode */
	ret = aspeed_can_set_reset_mode(ndev);
	if (ret < 0)
		netdev_dbg(ndev, "aspeed_can_set_reset_mode() Failed\n");

	priv->can.state = CAN_STATE_STOPPED;
}

static int aspeed_can_open(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	int ret;

	ret = pm_runtime_get_sync(priv->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, ret);
		goto err;
	}

	ret = request_irq(ndev->irq, aspeed_can_interrupt, priv->irq_flags,
			  ndev->name, ndev);
	if (ret < 0) {
		netdev_err(ndev, "irq allocation for CAN failed\n");
		goto err;
	}

	/* Set chip into reset mode */
	ret = aspeed_can_set_reset_mode(ndev);
	if (ret < 0) {
		netdev_err(ndev, "mode resetting failed!\n");
		goto err_irq;
	}

	/* Common open */
	ret = open_candev(ndev);
	if (ret)
		goto err_irq;

	ret = aspeed_can_chip_start(ndev);
	if (ret < 0) {
		netdev_err(ndev, "aspeed_can_chip_start failed!\n");
		goto err_candev;
	}

	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	return 0;

err_candev:
	close_candev(ndev);
err_irq:
	free_irq(ndev->irq, ndev);
err:
	pm_runtime_put(priv->dev);

	return ret;
}

static int aspeed_can_close(struct net_device *ndev)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	aspeed_can_chip_stop(ndev);
	free_irq(ndev->irq, ndev);
	close_candev(ndev);
	pm_runtime_put(priv->dev);

	return 0;
}

static int aspeed_can_get_berr_counter(const struct net_device *ndev,
				       struct can_berr_counter *bec)
{
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	int ret;

	ret = pm_runtime_get_sync(priv->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, ret);
		pm_runtime_put(priv->dev);
		return ret;
	}

	bec->rxerr = (readl(priv->reg_base + CAN_ERR_STAT) &
		      CAN_ERR_RECNT_MASK) >> CAN_ERR_RECNT_BITOFF;
	bec->txerr = (readl(priv->reg_base + CAN_ERR_STAT) &
		      CAN_ERR_TECNT_MASK) >> CAN_ERR_TECNT_BITOFF;

	pm_runtime_put(priv->dev);

	return 0;
}

static int aspeed_can_get_auto_tdcv(const struct net_device *ndev, u32 *tdcv)
{
	(void)ndev;

	/* need to fix on A1 */
	*tdcv = 0x52;

	return 0;
}

static const struct net_device_ops aspeed_can_netdev_ops = {
	.ndo_open	= aspeed_can_open,
	.ndo_stop	= aspeed_can_close,
	.ndo_start_xmit	= aspeed_can_start_xmit,
	.ndo_change_mtu	= can_change_mtu,
};

static const struct ethtool_ops aspeed_can_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static int __maybe_unused aspeed_can_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
		aspeed_can_chip_stop(ndev);
	}

	return pm_runtime_force_suspend(dev);
}

static int __maybe_unused aspeed_can_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "pm_runtime_force_resume failed on resume\n");
		return ret;
	}

	if (netif_running(ndev)) {
		ret = aspeed_can_chip_start(ndev);
		if (ret) {
			dev_err(dev, "aspeed_can_chip_start failed on resume\n");
			return ret;
		}

		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}

static int __maybe_unused aspeed_can_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct aspeed_can_priv *priv = netdev_priv(ndev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused aspeed_can_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct aspeed_can_priv *priv = netdev_priv(ndev);
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops aspeed_can_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(aspeed_can_suspend, aspeed_can_resume)
	SET_RUNTIME_PM_OPS(aspeed_can_runtime_suspend, aspeed_can_runtime_resume, NULL)
};

static const struct aspeed_can_data aspeed_canfd_data = {
	.flags = 0,
	.bittiming_const = &aspeed_can_bittiming_const,
	.data_bittiming_const = &aspeed_canfd_bittiming_const,
	.btr_ts2_shift = 16,
	.btr_sjw_shift = 24,
	.btr_fdssp_shift = 8,
};

/* Match table for OF platform binding */
static const struct of_device_id aspeed_can_of_match[] = {
	{ .compatible = "aspeed,canfd", .data = &aspeed_canfd_data },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, aspeed_can_of_match);

static int aspeed_can_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct aspeed_can_priv *priv;
	int ret;
	/* Fixed to temporarily. */
	u32 rx_max = 3;
	u32 tx_max = 1;
	u32 can_clk;

	ndev = alloc_candev(sizeof(struct aspeed_can_priv), tx_max);
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->data = of_device_get_match_data(&pdev->dev);
	if (!priv->data) {
		ret = -ENODEV;
		goto err_free;
	}

	priv->dev = &pdev->dev;
	priv->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->reg_base)) {
		ret = PTR_ERR(priv->reg_base);
		goto err;
	};

	priv->can.bittiming_const = priv->data->bittiming_const;
	priv->can.data_bittiming_const = priv->data->data_bittiming_const;
	priv->can.do_set_mode = aspeed_can_do_set_mode;
	priv->can.do_get_berr_counter = aspeed_can_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
				       CAN_CTRLMODE_BERR_REPORTING |
				       CAN_CTRLMODE_FD |
				       CAN_CTRLMODE_CC_LEN8_DLC |
				       CAN_CTRLMODE_TDC_AUTO;
	priv->can.do_get_auto_tdcv = aspeed_can_get_auto_tdcv;

	priv->tx_max = tx_max;
	spin_lock_init(&priv->tx_lock);

	/* Get IRQ for the device */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_free;

	ndev->irq = ret;

	/* We support local echo */
	ndev->flags |= IFF_ECHO;

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &aspeed_can_netdev_ops;
	ndev->ethtool_ops = &aspeed_can_ethtool_ops;

	/* Getting the CAN can_clk info */
	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "missing clock\n");
		return PTR_ERR(priv->clk);
	}

	can_clk = clk_get_rate(priv->clk);
	if (!can_clk) {
		dev_err(&pdev->dev, "invalid clock\n");
		return -EINVAL;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(&pdev->dev, "can not enable the clock\n");
		return ret;
	}

	priv->can.clock.freq = can_clk;

	priv->reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	reset_control_deassert(priv->reset);

	ret = aspeed_can_set_reset_mode(ndev);
	if (ret < 0)
		goto err;

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, ret);
		goto err_disableclks;
	}

	netif_napi_add_weight(ndev, &priv->napi, aspeed_can_rx_poll, rx_max);

	ret = register_candev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "fail to register failed (err=%d)\n", ret);
		goto err_disableclks;
	}

	pm_runtime_put(&pdev->dev);

	netdev_dbg(ndev, "reg_base = 0x%p, irq = %d, clock = %d\n",
		   priv->reg_base, ndev->irq, priv->can.clock.freq);

	return 0;

err_disableclks:
	pm_runtime_put(priv->dev);
	pm_runtime_disable(&pdev->dev);
err_free:
	free_candev(ndev);
err:
	return ret;
}

static int aspeed_can_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct aspeed_can_priv *priv = netdev_priv(ndev);

	reset_control_assert(priv->reset);
	unregister_candev(ndev);
	pm_runtime_disable(&pdev->dev);
	free_candev(ndev);

	return 0;
}

static struct platform_driver aspeed_can_driver = {
	.probe = aspeed_can_probe,
	.remove	= aspeed_can_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.pm = &aspeed_can_dev_pm_ops,
		.of_match_table	= aspeed_can_of_match,
	},
};

module_platform_driver(aspeed_can_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chin-Ting Kuo <chin-ting_kuo@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED CAN interface");
