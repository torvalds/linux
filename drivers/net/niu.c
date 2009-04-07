/* niu.c: Neptune ethernet driver.
 *
 * Copyright (C) 2007, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/mii.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <linux/log2.h>
#include <linux/jiffies.h>
#include <linux/crc32.h>

#include <linux/io.h>

#ifdef CONFIG_SPARC64
#include <linux/of_device.h>
#endif

#include "niu.h"

#define DRV_MODULE_NAME		"niu"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"1.0"
#define DRV_MODULE_RELDATE	"Nov 14, 2008"

static char version[] __devinitdata =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("NIU ethernet driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

#ifndef DMA_44BIT_MASK
#define DMA_44BIT_MASK	0x00000fffffffffffULL
#endif

#ifndef readq
static u64 readq(void __iomem *reg)
{
	return ((u64) readl(reg)) | (((u64) readl(reg + 4UL)) << 32);
}

static void writeq(u64 val, void __iomem *reg)
{
	writel(val & 0xffffffff, reg);
	writel(val >> 32, reg + 0x4UL);
}
#endif

static struct pci_device_id niu_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_SUN, 0xabcd)},
	{}
};

MODULE_DEVICE_TABLE(pci, niu_pci_tbl);

#define NIU_TX_TIMEOUT			(5 * HZ)

#define nr64(reg)		readq(np->regs + (reg))
#define nw64(reg, val)		writeq((val), np->regs + (reg))

#define nr64_mac(reg)		readq(np->mac_regs + (reg))
#define nw64_mac(reg, val)	writeq((val), np->mac_regs + (reg))

#define nr64_ipp(reg)		readq(np->regs + np->ipp_off + (reg))
#define nw64_ipp(reg, val)	writeq((val), np->regs + np->ipp_off + (reg))

#define nr64_pcs(reg)		readq(np->regs + np->pcs_off + (reg))
#define nw64_pcs(reg, val)	writeq((val), np->regs + np->pcs_off + (reg))

#define nr64_xpcs(reg)		readq(np->regs + np->xpcs_off + (reg))
#define nw64_xpcs(reg, val)	writeq((val), np->regs + np->xpcs_off + (reg))

#define NIU_MSG_DEFAULT (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

static int niu_debug;
static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "NIU debug level");

#define niudbg(TYPE, f, a...) \
do {	if ((np)->msg_enable & NETIF_MSG_##TYPE) \
		printk(KERN_DEBUG PFX f, ## a); \
} while (0)

#define niuinfo(TYPE, f, a...) \
do {	if ((np)->msg_enable & NETIF_MSG_##TYPE) \
		printk(KERN_INFO PFX f, ## a); \
} while (0)

#define niuwarn(TYPE, f, a...) \
do {	if ((np)->msg_enable & NETIF_MSG_##TYPE) \
		printk(KERN_WARNING PFX f, ## a); \
} while (0)

#define niu_lock_parent(np, flags) \
	spin_lock_irqsave(&np->parent->lock, flags)
#define niu_unlock_parent(np, flags) \
	spin_unlock_irqrestore(&np->parent->lock, flags)

static int serdes_init_10g_serdes(struct niu *np);

static int __niu_wait_bits_clear_mac(struct niu *np, unsigned long reg,
				     u64 bits, int limit, int delay)
{
	while (--limit >= 0) {
		u64 val = nr64_mac(reg);

		if (!(val & bits))
			break;
		udelay(delay);
	}
	if (limit < 0)
		return -ENODEV;
	return 0;
}

static int __niu_set_and_wait_clear_mac(struct niu *np, unsigned long reg,
					u64 bits, int limit, int delay,
					const char *reg_name)
{
	int err;

	nw64_mac(reg, bits);
	err = __niu_wait_bits_clear_mac(np, reg, bits, limit, delay);
	if (err)
		dev_err(np->device, PFX "%s: bits (%llx) of register %s "
			"would not clear, val[%llx]\n",
			np->dev->name, (unsigned long long) bits, reg_name,
			(unsigned long long) nr64_mac(reg));
	return err;
}

#define niu_set_and_wait_clear_mac(NP, REG, BITS, LIMIT, DELAY, REG_NAME) \
({	BUILD_BUG_ON(LIMIT <= 0 || DELAY < 0); \
	__niu_set_and_wait_clear_mac(NP, REG, BITS, LIMIT, DELAY, REG_NAME); \
})

static int __niu_wait_bits_clear_ipp(struct niu *np, unsigned long reg,
				     u64 bits, int limit, int delay)
{
	while (--limit >= 0) {
		u64 val = nr64_ipp(reg);

		if (!(val & bits))
			break;
		udelay(delay);
	}
	if (limit < 0)
		return -ENODEV;
	return 0;
}

static int __niu_set_and_wait_clear_ipp(struct niu *np, unsigned long reg,
					u64 bits, int limit, int delay,
					const char *reg_name)
{
	int err;
	u64 val;

	val = nr64_ipp(reg);
	val |= bits;
	nw64_ipp(reg, val);

	err = __niu_wait_bits_clear_ipp(np, reg, bits, limit, delay);
	if (err)
		dev_err(np->device, PFX "%s: bits (%llx) of register %s "
			"would not clear, val[%llx]\n",
			np->dev->name, (unsigned long long) bits, reg_name,
			(unsigned long long) nr64_ipp(reg));
	return err;
}

#define niu_set_and_wait_clear_ipp(NP, REG, BITS, LIMIT, DELAY, REG_NAME) \
({	BUILD_BUG_ON(LIMIT <= 0 || DELAY < 0); \
	__niu_set_and_wait_clear_ipp(NP, REG, BITS, LIMIT, DELAY, REG_NAME); \
})

static int __niu_wait_bits_clear(struct niu *np, unsigned long reg,
				 u64 bits, int limit, int delay)
{
	while (--limit >= 0) {
		u64 val = nr64(reg);

		if (!(val & bits))
			break;
		udelay(delay);
	}
	if (limit < 0)
		return -ENODEV;
	return 0;
}

#define niu_wait_bits_clear(NP, REG, BITS, LIMIT, DELAY) \
({	BUILD_BUG_ON(LIMIT <= 0 || DELAY < 0); \
	__niu_wait_bits_clear(NP, REG, BITS, LIMIT, DELAY); \
})

static int __niu_set_and_wait_clear(struct niu *np, unsigned long reg,
				    u64 bits, int limit, int delay,
				    const char *reg_name)
{
	int err;

	nw64(reg, bits);
	err = __niu_wait_bits_clear(np, reg, bits, limit, delay);
	if (err)
		dev_err(np->device, PFX "%s: bits (%llx) of register %s "
			"would not clear, val[%llx]\n",
			np->dev->name, (unsigned long long) bits, reg_name,
			(unsigned long long) nr64(reg));
	return err;
}

#define niu_set_and_wait_clear(NP, REG, BITS, LIMIT, DELAY, REG_NAME) \
({	BUILD_BUG_ON(LIMIT <= 0 || DELAY < 0); \
	__niu_set_and_wait_clear(NP, REG, BITS, LIMIT, DELAY, REG_NAME); \
})

static void niu_ldg_rearm(struct niu *np, struct niu_ldg *lp, int on)
{
	u64 val = (u64) lp->timer;

	if (on)
		val |= LDG_IMGMT_ARM;

	nw64(LDG_IMGMT(lp->ldg_num), val);
}

static int niu_ldn_irq_enable(struct niu *np, int ldn, int on)
{
	unsigned long mask_reg, bits;
	u64 val;

	if (ldn < 0 || ldn > LDN_MAX)
		return -EINVAL;

	if (ldn < 64) {
		mask_reg = LD_IM0(ldn);
		bits = LD_IM0_MASK;
	} else {
		mask_reg = LD_IM1(ldn - 64);
		bits = LD_IM1_MASK;
	}

	val = nr64(mask_reg);
	if (on)
		val &= ~bits;
	else
		val |= bits;
	nw64(mask_reg, val);

	return 0;
}

static int niu_enable_ldn_in_ldg(struct niu *np, struct niu_ldg *lp, int on)
{
	struct niu_parent *parent = np->parent;
	int i;

	for (i = 0; i <= LDN_MAX; i++) {
		int err;

		if (parent->ldg_map[i] != lp->ldg_num)
			continue;

		err = niu_ldn_irq_enable(np, i, on);
		if (err)
			return err;
	}
	return 0;
}

static int niu_enable_interrupts(struct niu *np, int on)
{
	int i;

	for (i = 0; i < np->num_ldg; i++) {
		struct niu_ldg *lp = &np->ldg[i];
		int err;

		err = niu_enable_ldn_in_ldg(np, lp, on);
		if (err)
			return err;
	}
	for (i = 0; i < np->num_ldg; i++)
		niu_ldg_rearm(np, &np->ldg[i], on);

	return 0;
}

static u32 phy_encode(u32 type, int port)
{
	return (type << (port * 2));
}

static u32 phy_decode(u32 val, int port)
{
	return (val >> (port * 2)) & PORT_TYPE_MASK;
}

static int mdio_wait(struct niu *np)
{
	int limit = 1000;
	u64 val;

	while (--limit > 0) {
		val = nr64(MIF_FRAME_OUTPUT);
		if ((val >> MIF_FRAME_OUTPUT_TA_SHIFT) & 0x1)
			return val & MIF_FRAME_OUTPUT_DATA;

		udelay(10);
	}

	return -ENODEV;
}

static int mdio_read(struct niu *np, int port, int dev, int reg)
{
	int err;

	nw64(MIF_FRAME_OUTPUT, MDIO_ADDR_OP(port, dev, reg));
	err = mdio_wait(np);
	if (err < 0)
		return err;

	nw64(MIF_FRAME_OUTPUT, MDIO_READ_OP(port, dev));
	return mdio_wait(np);
}

static int mdio_write(struct niu *np, int port, int dev, int reg, int data)
{
	int err;

	nw64(MIF_FRAME_OUTPUT, MDIO_ADDR_OP(port, dev, reg));
	err = mdio_wait(np);
	if (err < 0)
		return err;

	nw64(MIF_FRAME_OUTPUT, MDIO_WRITE_OP(port, dev, data));
	err = mdio_wait(np);
	if (err < 0)
		return err;

	return 0;
}

static int mii_read(struct niu *np, int port, int reg)
{
	nw64(MIF_FRAME_OUTPUT, MII_READ_OP(port, reg));
	return mdio_wait(np);
}

static int mii_write(struct niu *np, int port, int reg, int data)
{
	int err;

	nw64(MIF_FRAME_OUTPUT, MII_WRITE_OP(port, reg, data));
	err = mdio_wait(np);
	if (err < 0)
		return err;

	return 0;
}

static int esr2_set_tx_cfg(struct niu *np, unsigned long channel, u32 val)
{
	int err;

	err = mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			 ESR2_TI_PLL_TX_CFG_L(channel),
			 val & 0xffff);
	if (!err)
		err = mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
				 ESR2_TI_PLL_TX_CFG_H(channel),
				 val >> 16);
	return err;
}

static int esr2_set_rx_cfg(struct niu *np, unsigned long channel, u32 val)
{
	int err;

	err = mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			 ESR2_TI_PLL_RX_CFG_L(channel),
			 val & 0xffff);
	if (!err)
		err = mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
				 ESR2_TI_PLL_RX_CFG_H(channel),
				 val >> 16);
	return err;
}

/* Mode is always 10G fiber.  */
static int serdes_init_niu_10g_fiber(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	u32 tx_cfg, rx_cfg;
	unsigned long i;

	tx_cfg = (PLL_TX_CFG_ENTX | PLL_TX_CFG_SWING_1375MV);
	rx_cfg = (PLL_RX_CFG_ENRX | PLL_RX_CFG_TERM_0P8VDDT |
		  PLL_RX_CFG_ALIGN_ENA | PLL_RX_CFG_LOS_LTHRESH |
		  PLL_RX_CFG_EQ_LP_ADAPTIVE);

	if (lp->loopback_mode == LOOPBACK_PHY) {
		u16 test_cfg = PLL_TEST_CFG_LOOPBACK_CML_DIS;

		mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			   ESR2_TI_PLL_TEST_CFG_L, test_cfg);

		tx_cfg |= PLL_TX_CFG_ENTEST;
		rx_cfg |= PLL_RX_CFG_ENTEST;
	}

	/* Initialize all 4 lanes of the SERDES.  */
	for (i = 0; i < 4; i++) {
		int err = esr2_set_tx_cfg(np, i, tx_cfg);
		if (err)
			return err;
	}

	for (i = 0; i < 4; i++) {
		int err = esr2_set_rx_cfg(np, i, rx_cfg);
		if (err)
			return err;
	}

	return 0;
}

static int serdes_init_niu_1g_serdes(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	u16 pll_cfg, pll_sts;
	int max_retry = 100;
	u64 uninitialized_var(sig), mask, val;
	u32 tx_cfg, rx_cfg;
	unsigned long i;
	int err;

	tx_cfg = (PLL_TX_CFG_ENTX | PLL_TX_CFG_SWING_1375MV |
		  PLL_TX_CFG_RATE_HALF);
	rx_cfg = (PLL_RX_CFG_ENRX | PLL_RX_CFG_TERM_0P8VDDT |
		  PLL_RX_CFG_ALIGN_ENA | PLL_RX_CFG_LOS_LTHRESH |
		  PLL_RX_CFG_RATE_HALF);

	if (np->port == 0)
		rx_cfg |= PLL_RX_CFG_EQ_LP_ADAPTIVE;

	if (lp->loopback_mode == LOOPBACK_PHY) {
		u16 test_cfg = PLL_TEST_CFG_LOOPBACK_CML_DIS;

		mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			   ESR2_TI_PLL_TEST_CFG_L, test_cfg);

		tx_cfg |= PLL_TX_CFG_ENTEST;
		rx_cfg |= PLL_RX_CFG_ENTEST;
	}

	/* Initialize PLL for 1G */
	pll_cfg = (PLL_CFG_ENPLL | PLL_CFG_MPY_8X);

	err = mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			 ESR2_TI_PLL_CFG_L, pll_cfg);
	if (err) {
		dev_err(np->device, PFX "NIU Port %d "
			"serdes_init_niu_1g_serdes: "
			"mdio write to ESR2_TI_PLL_CFG_L failed", np->port);
		return err;
	}

	pll_sts = PLL_CFG_ENPLL;

	err = mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			 ESR2_TI_PLL_STS_L, pll_sts);
	if (err) {
		dev_err(np->device, PFX "NIU Port %d "
			"serdes_init_niu_1g_serdes: "
			"mdio write to ESR2_TI_PLL_STS_L failed", np->port);
		return err;
	}

	udelay(200);

	/* Initialize all 4 lanes of the SERDES.  */
	for (i = 0; i < 4; i++) {
		err = esr2_set_tx_cfg(np, i, tx_cfg);
		if (err)
			return err;
	}

	for (i = 0; i < 4; i++) {
		err = esr2_set_rx_cfg(np, i, rx_cfg);
		if (err)
			return err;
	}

	switch (np->port) {
	case 0:
		val = (ESR_INT_SRDY0_P0 | ESR_INT_DET0_P0);
		mask = val;
		break;

	case 1:
		val = (ESR_INT_SRDY0_P1 | ESR_INT_DET0_P1);
		mask = val;
		break;

	default:
		return -EINVAL;
	}

	while (max_retry--) {
		sig = nr64(ESR_INT_SIGNALS);
		if ((sig & mask) == val)
			break;

		mdelay(500);
	}

	if ((sig & mask) != val) {
		dev_err(np->device, PFX "Port %u signal bits [%08x] are not "
			"[%08x]\n", np->port, (int) (sig & mask), (int) val);
		return -ENODEV;
	}

	return 0;
}

static int serdes_init_niu_10g_serdes(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	u32 tx_cfg, rx_cfg, pll_cfg, pll_sts;
	int max_retry = 100;
	u64 uninitialized_var(sig), mask, val;
	unsigned long i;
	int err;

	tx_cfg = (PLL_TX_CFG_ENTX | PLL_TX_CFG_SWING_1375MV);
	rx_cfg = (PLL_RX_CFG_ENRX | PLL_RX_CFG_TERM_0P8VDDT |
		  PLL_RX_CFG_ALIGN_ENA | PLL_RX_CFG_LOS_LTHRESH |
		  PLL_RX_CFG_EQ_LP_ADAPTIVE);

	if (lp->loopback_mode == LOOPBACK_PHY) {
		u16 test_cfg = PLL_TEST_CFG_LOOPBACK_CML_DIS;

		mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			   ESR2_TI_PLL_TEST_CFG_L, test_cfg);

		tx_cfg |= PLL_TX_CFG_ENTEST;
		rx_cfg |= PLL_RX_CFG_ENTEST;
	}

	/* Initialize PLL for 10G */
	pll_cfg = (PLL_CFG_ENPLL | PLL_CFG_MPY_10X);

	err = mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			 ESR2_TI_PLL_CFG_L, pll_cfg & 0xffff);
	if (err) {
		dev_err(np->device, PFX "NIU Port %d "
			"serdes_init_niu_10g_serdes: "
			"mdio write to ESR2_TI_PLL_CFG_L failed", np->port);
		return err;
	}

	pll_sts = PLL_CFG_ENPLL;

	err = mdio_write(np, np->port, NIU_ESR2_DEV_ADDR,
			 ESR2_TI_PLL_STS_L, pll_sts & 0xffff);
	if (err) {
		dev_err(np->device, PFX "NIU Port %d "
			"serdes_init_niu_10g_serdes: "
			"mdio write to ESR2_TI_PLL_STS_L failed", np->port);
		return err;
	}

	udelay(200);

	/* Initialize all 4 lanes of the SERDES.  */
	for (i = 0; i < 4; i++) {
		err = esr2_set_tx_cfg(np, i, tx_cfg);
		if (err)
			return err;
	}

	for (i = 0; i < 4; i++) {
		err = esr2_set_rx_cfg(np, i, rx_cfg);
		if (err)
			return err;
	}

	/* check if serdes is ready */

	switch (np->port) {
	case 0:
		mask = ESR_INT_SIGNALS_P0_BITS;
		val = (ESR_INT_SRDY0_P0 |
		       ESR_INT_DET0_P0 |
		       ESR_INT_XSRDY_P0 |
		       ESR_INT_XDP_P0_CH3 |
		       ESR_INT_XDP_P0_CH2 |
		       ESR_INT_XDP_P0_CH1 |
		       ESR_INT_XDP_P0_CH0);
		break;

	case 1:
		mask = ESR_INT_SIGNALS_P1_BITS;
		val = (ESR_INT_SRDY0_P1 |
		       ESR_INT_DET0_P1 |
		       ESR_INT_XSRDY_P1 |
		       ESR_INT_XDP_P1_CH3 |
		       ESR_INT_XDP_P1_CH2 |
		       ESR_INT_XDP_P1_CH1 |
		       ESR_INT_XDP_P1_CH0);
		break;

	default:
		return -EINVAL;
	}

	while (max_retry--) {
		sig = nr64(ESR_INT_SIGNALS);
		if ((sig & mask) == val)
			break;

		mdelay(500);
	}

	if ((sig & mask) != val) {
		pr_info(PFX "NIU Port %u signal bits [%08x] are not "
			"[%08x] for 10G...trying 1G\n",
			np->port, (int) (sig & mask), (int) val);

		/* 10G failed, try initializing at 1G */
		err = serdes_init_niu_1g_serdes(np);
		if (!err) {
			np->flags &= ~NIU_FLAGS_10G;
			np->mac_xcvr = MAC_XCVR_PCS;
		}  else {
			dev_err(np->device, PFX "Port %u 10G/1G SERDES "
				"Link Failed \n", np->port);
			return -ENODEV;
		}
	}
	return 0;
}

static int esr_read_rxtx_ctrl(struct niu *np, unsigned long chan, u32 *val)
{
	int err;

	err = mdio_read(np, np->port, NIU_ESR_DEV_ADDR, ESR_RXTX_CTRL_L(chan));
	if (err >= 0) {
		*val = (err & 0xffff);
		err = mdio_read(np, np->port, NIU_ESR_DEV_ADDR,
				ESR_RXTX_CTRL_H(chan));
		if (err >= 0)
			*val |= ((err & 0xffff) << 16);
		err = 0;
	}
	return err;
}

static int esr_read_glue0(struct niu *np, unsigned long chan, u32 *val)
{
	int err;

	err = mdio_read(np, np->port, NIU_ESR_DEV_ADDR,
			ESR_GLUE_CTRL0_L(chan));
	if (err >= 0) {
		*val = (err & 0xffff);
		err = mdio_read(np, np->port, NIU_ESR_DEV_ADDR,
				ESR_GLUE_CTRL0_H(chan));
		if (err >= 0) {
			*val |= ((err & 0xffff) << 16);
			err = 0;
		}
	}
	return err;
}

static int esr_read_reset(struct niu *np, u32 *val)
{
	int err;

	err = mdio_read(np, np->port, NIU_ESR_DEV_ADDR,
			ESR_RXTX_RESET_CTRL_L);
	if (err >= 0) {
		*val = (err & 0xffff);
		err = mdio_read(np, np->port, NIU_ESR_DEV_ADDR,
				ESR_RXTX_RESET_CTRL_H);
		if (err >= 0) {
			*val |= ((err & 0xffff) << 16);
			err = 0;
		}
	}
	return err;
}

static int esr_write_rxtx_ctrl(struct niu *np, unsigned long chan, u32 val)
{
	int err;

	err = mdio_write(np, np->port, NIU_ESR_DEV_ADDR,
			 ESR_RXTX_CTRL_L(chan), val & 0xffff);
	if (!err)
		err = mdio_write(np, np->port, NIU_ESR_DEV_ADDR,
				 ESR_RXTX_CTRL_H(chan), (val >> 16));
	return err;
}

static int esr_write_glue0(struct niu *np, unsigned long chan, u32 val)
{
	int err;

	err = mdio_write(np, np->port, NIU_ESR_DEV_ADDR,
			ESR_GLUE_CTRL0_L(chan), val & 0xffff);
	if (!err)
		err = mdio_write(np, np->port, NIU_ESR_DEV_ADDR,
				 ESR_GLUE_CTRL0_H(chan), (val >> 16));
	return err;
}

static int esr_reset(struct niu *np)
{
	u32 uninitialized_var(reset);
	int err;

	err = mdio_write(np, np->port, NIU_ESR_DEV_ADDR,
			 ESR_RXTX_RESET_CTRL_L, 0x0000);
	if (err)
		return err;
	err = mdio_write(np, np->port, NIU_ESR_DEV_ADDR,
			 ESR_RXTX_RESET_CTRL_H, 0xffff);
	if (err)
		return err;
	udelay(200);

	err = mdio_write(np, np->port, NIU_ESR_DEV_ADDR,
			 ESR_RXTX_RESET_CTRL_L, 0xffff);
	if (err)
		return err;
	udelay(200);

	err = mdio_write(np, np->port, NIU_ESR_DEV_ADDR,
			 ESR_RXTX_RESET_CTRL_H, 0x0000);
	if (err)
		return err;
	udelay(200);

	err = esr_read_reset(np, &reset);
	if (err)
		return err;
	if (reset != 0) {
		dev_err(np->device, PFX "Port %u ESR_RESET "
			"did not clear [%08x]\n",
			np->port, reset);
		return -ENODEV;
	}

	return 0;
}

static int serdes_init_10g(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	unsigned long ctrl_reg, test_cfg_reg, i;
	u64 ctrl_val, test_cfg_val, sig, mask, val;
	int err;

	switch (np->port) {
	case 0:
		ctrl_reg = ENET_SERDES_0_CTRL_CFG;
		test_cfg_reg = ENET_SERDES_0_TEST_CFG;
		break;
	case 1:
		ctrl_reg = ENET_SERDES_1_CTRL_CFG;
		test_cfg_reg = ENET_SERDES_1_TEST_CFG;
		break;

	default:
		return -EINVAL;
	}
	ctrl_val = (ENET_SERDES_CTRL_SDET_0 |
		    ENET_SERDES_CTRL_SDET_1 |
		    ENET_SERDES_CTRL_SDET_2 |
		    ENET_SERDES_CTRL_SDET_3 |
		    (0x5 << ENET_SERDES_CTRL_EMPH_0_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_1_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_2_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_3_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_0_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_1_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_2_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_3_SHIFT));
	test_cfg_val = 0;

	if (lp->loopback_mode == LOOPBACK_PHY) {
		test_cfg_val |= ((ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_0_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_1_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_2_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_3_SHIFT));
	}

	nw64(ctrl_reg, ctrl_val);
	nw64(test_cfg_reg, test_cfg_val);

	/* Initialize all 4 lanes of the SERDES.  */
	for (i = 0; i < 4; i++) {
		u32 rxtx_ctrl, glue0;

		err = esr_read_rxtx_ctrl(np, i, &rxtx_ctrl);
		if (err)
			return err;
		err = esr_read_glue0(np, i, &glue0);
		if (err)
			return err;

		rxtx_ctrl &= ~(ESR_RXTX_CTRL_VMUXLO);
		rxtx_ctrl |= (ESR_RXTX_CTRL_ENSTRETCH |
			      (2 << ESR_RXTX_CTRL_VMUXLO_SHIFT));

		glue0 &= ~(ESR_GLUE_CTRL0_SRATE |
			   ESR_GLUE_CTRL0_THCNT |
			   ESR_GLUE_CTRL0_BLTIME);
		glue0 |= (ESR_GLUE_CTRL0_RXLOSENAB |
			  (0xf << ESR_GLUE_CTRL0_SRATE_SHIFT) |
			  (0xff << ESR_GLUE_CTRL0_THCNT_SHIFT) |
			  (BLTIME_300_CYCLES <<
			   ESR_GLUE_CTRL0_BLTIME_SHIFT));

		err = esr_write_rxtx_ctrl(np, i, rxtx_ctrl);
		if (err)
			return err;
		err = esr_write_glue0(np, i, glue0);
		if (err)
			return err;
	}

	err = esr_reset(np);
	if (err)
		return err;

	sig = nr64(ESR_INT_SIGNALS);
	switch (np->port) {
	case 0:
		mask = ESR_INT_SIGNALS_P0_BITS;
		val = (ESR_INT_SRDY0_P0 |
		       ESR_INT_DET0_P0 |
		       ESR_INT_XSRDY_P0 |
		       ESR_INT_XDP_P0_CH3 |
		       ESR_INT_XDP_P0_CH2 |
		       ESR_INT_XDP_P0_CH1 |
		       ESR_INT_XDP_P0_CH0);
		break;

	case 1:
		mask = ESR_INT_SIGNALS_P1_BITS;
		val = (ESR_INT_SRDY0_P1 |
		       ESR_INT_DET0_P1 |
		       ESR_INT_XSRDY_P1 |
		       ESR_INT_XDP_P1_CH3 |
		       ESR_INT_XDP_P1_CH2 |
		       ESR_INT_XDP_P1_CH1 |
		       ESR_INT_XDP_P1_CH0);
		break;

	default:
		return -EINVAL;
	}

	if ((sig & mask) != val) {
		if (np->flags & NIU_FLAGS_HOTPLUG_PHY) {
			np->flags &= ~NIU_FLAGS_HOTPLUG_PHY_PRESENT;
			return 0;
		}
		dev_err(np->device, PFX "Port %u signal bits [%08x] are not "
			"[%08x]\n", np->port, (int) (sig & mask), (int) val);
		return -ENODEV;
	}
	if (np->flags & NIU_FLAGS_HOTPLUG_PHY)
		np->flags |= NIU_FLAGS_HOTPLUG_PHY_PRESENT;
	return 0;
}

static int serdes_init_1g(struct niu *np)
{
	u64 val;

	val = nr64(ENET_SERDES_1_PLL_CFG);
	val &= ~ENET_SERDES_PLL_FBDIV2;
	switch (np->port) {
	case 0:
		val |= ENET_SERDES_PLL_HRATE0;
		break;
	case 1:
		val |= ENET_SERDES_PLL_HRATE1;
		break;
	case 2:
		val |= ENET_SERDES_PLL_HRATE2;
		break;
	case 3:
		val |= ENET_SERDES_PLL_HRATE3;
		break;
	default:
		return -EINVAL;
	}
	nw64(ENET_SERDES_1_PLL_CFG, val);

	return 0;
}

static int serdes_init_1g_serdes(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	unsigned long ctrl_reg, test_cfg_reg, pll_cfg, i;
	u64 ctrl_val, test_cfg_val, sig, mask, val;
	int err;
	u64 reset_val, val_rd;

	val = ENET_SERDES_PLL_HRATE0 | ENET_SERDES_PLL_HRATE1 |
		ENET_SERDES_PLL_HRATE2 | ENET_SERDES_PLL_HRATE3 |
		ENET_SERDES_PLL_FBDIV0;
	switch (np->port) {
	case 0:
		reset_val =  ENET_SERDES_RESET_0;
		ctrl_reg = ENET_SERDES_0_CTRL_CFG;
		test_cfg_reg = ENET_SERDES_0_TEST_CFG;
		pll_cfg = ENET_SERDES_0_PLL_CFG;
		break;
	case 1:
		reset_val =  ENET_SERDES_RESET_1;
		ctrl_reg = ENET_SERDES_1_CTRL_CFG;
		test_cfg_reg = ENET_SERDES_1_TEST_CFG;
		pll_cfg = ENET_SERDES_1_PLL_CFG;
		break;

	default:
		return -EINVAL;
	}
	ctrl_val = (ENET_SERDES_CTRL_SDET_0 |
		    ENET_SERDES_CTRL_SDET_1 |
		    ENET_SERDES_CTRL_SDET_2 |
		    ENET_SERDES_CTRL_SDET_3 |
		    (0x5 << ENET_SERDES_CTRL_EMPH_0_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_1_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_2_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_3_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_0_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_1_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_2_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_3_SHIFT));
	test_cfg_val = 0;

	if (lp->loopback_mode == LOOPBACK_PHY) {
		test_cfg_val |= ((ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_0_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_1_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_2_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_3_SHIFT));
	}

	nw64(ENET_SERDES_RESET, reset_val);
	mdelay(20);
	val_rd = nr64(ENET_SERDES_RESET);
	val_rd &= ~reset_val;
	nw64(pll_cfg, val);
	nw64(ctrl_reg, ctrl_val);
	nw64(test_cfg_reg, test_cfg_val);
	nw64(ENET_SERDES_RESET, val_rd);
	mdelay(2000);

	/* Initialize all 4 lanes of the SERDES.  */
	for (i = 0; i < 4; i++) {
		u32 rxtx_ctrl, glue0;

		err = esr_read_rxtx_ctrl(np, i, &rxtx_ctrl);
		if (err)
			return err;
		err = esr_read_glue0(np, i, &glue0);
		if (err)
			return err;

		rxtx_ctrl &= ~(ESR_RXTX_CTRL_VMUXLO);
		rxtx_ctrl |= (ESR_RXTX_CTRL_ENSTRETCH |
			      (2 << ESR_RXTX_CTRL_VMUXLO_SHIFT));

		glue0 &= ~(ESR_GLUE_CTRL0_SRATE |
			   ESR_GLUE_CTRL0_THCNT |
			   ESR_GLUE_CTRL0_BLTIME);
		glue0 |= (ESR_GLUE_CTRL0_RXLOSENAB |
			  (0xf << ESR_GLUE_CTRL0_SRATE_SHIFT) |
			  (0xff << ESR_GLUE_CTRL0_THCNT_SHIFT) |
			  (BLTIME_300_CYCLES <<
			   ESR_GLUE_CTRL0_BLTIME_SHIFT));

		err = esr_write_rxtx_ctrl(np, i, rxtx_ctrl);
		if (err)
			return err;
		err = esr_write_glue0(np, i, glue0);
		if (err)
			return err;
	}


	sig = nr64(ESR_INT_SIGNALS);
	switch (np->port) {
	case 0:
		val = (ESR_INT_SRDY0_P0 | ESR_INT_DET0_P0);
		mask = val;
		break;

	case 1:
		val = (ESR_INT_SRDY0_P1 | ESR_INT_DET0_P1);
		mask = val;
		break;

	default:
		return -EINVAL;
	}

	if ((sig & mask) != val) {
		dev_err(np->device, PFX "Port %u signal bits [%08x] are not "
			"[%08x]\n", np->port, (int) (sig & mask), (int) val);
		return -ENODEV;
	}

	return 0;
}

static int link_status_1g_serdes(struct niu *np, int *link_up_p)
{
	struct niu_link_config *lp = &np->link_config;
	int link_up;
	u64 val;
	u16 current_speed;
	unsigned long flags;
	u8 current_duplex;

	link_up = 0;
	current_speed = SPEED_INVALID;
	current_duplex = DUPLEX_INVALID;

	spin_lock_irqsave(&np->lock, flags);

	val = nr64_pcs(PCS_MII_STAT);

	if (val & PCS_MII_STAT_LINK_STATUS) {
		link_up = 1;
		current_speed = SPEED_1000;
		current_duplex = DUPLEX_FULL;
	}

	lp->active_speed = current_speed;
	lp->active_duplex = current_duplex;
	spin_unlock_irqrestore(&np->lock, flags);

	*link_up_p = link_up;
	return 0;
}

static int link_status_10g_serdes(struct niu *np, int *link_up_p)
{
	unsigned long flags;
	struct niu_link_config *lp = &np->link_config;
	int link_up = 0;
	int link_ok = 1;
	u64 val, val2;
	u16 current_speed;
	u8 current_duplex;

	if (!(np->flags & NIU_FLAGS_10G))
		return link_status_1g_serdes(np, link_up_p);

	current_speed = SPEED_INVALID;
	current_duplex = DUPLEX_INVALID;
	spin_lock_irqsave(&np->lock, flags);

	val = nr64_xpcs(XPCS_STATUS(0));
	val2 = nr64_mac(XMAC_INTER2);
	if (val2 & 0x01000000)
		link_ok = 0;

	if ((val & 0x1000ULL) && link_ok) {
		link_up = 1;
		current_speed = SPEED_10000;
		current_duplex = DUPLEX_FULL;
	}
	lp->active_speed = current_speed;
	lp->active_duplex = current_duplex;
	spin_unlock_irqrestore(&np->lock, flags);
	*link_up_p = link_up;
	return 0;
}

static int link_status_mii(struct niu *np, int *link_up_p)
{
	struct niu_link_config *lp = &np->link_config;
	int err;
	int bmsr, advert, ctrl1000, stat1000, lpa, bmcr, estatus;
	int supported, advertising, active_speed, active_duplex;

	err = mii_read(np, np->phy_addr, MII_BMCR);
	if (unlikely(err < 0))
		return err;
	bmcr = err;

	err = mii_read(np, np->phy_addr, MII_BMSR);
	if (unlikely(err < 0))
		return err;
	bmsr = err;

	err = mii_read(np, np->phy_addr, MII_ADVERTISE);
	if (unlikely(err < 0))
		return err;
	advert = err;

	err = mii_read(np, np->phy_addr, MII_LPA);
	if (unlikely(err < 0))
		return err;
	lpa = err;

	if (likely(bmsr & BMSR_ESTATEN)) {
		err = mii_read(np, np->phy_addr, MII_ESTATUS);
		if (unlikely(err < 0))
			return err;
		estatus = err;

		err = mii_read(np, np->phy_addr, MII_CTRL1000);
		if (unlikely(err < 0))
			return err;
		ctrl1000 = err;

		err = mii_read(np, np->phy_addr, MII_STAT1000);
		if (unlikely(err < 0))
			return err;
		stat1000 = err;
	} else
		estatus = ctrl1000 = stat1000 = 0;

	supported = 0;
	if (bmsr & BMSR_ANEGCAPABLE)
		supported |= SUPPORTED_Autoneg;
	if (bmsr & BMSR_10HALF)
		supported |= SUPPORTED_10baseT_Half;
	if (bmsr & BMSR_10FULL)
		supported |= SUPPORTED_10baseT_Full;
	if (bmsr & BMSR_100HALF)
		supported |= SUPPORTED_100baseT_Half;
	if (bmsr & BMSR_100FULL)
		supported |= SUPPORTED_100baseT_Full;
	if (estatus & ESTATUS_1000_THALF)
		supported |= SUPPORTED_1000baseT_Half;
	if (estatus & ESTATUS_1000_TFULL)
		supported |= SUPPORTED_1000baseT_Full;
	lp->supported = supported;

	advertising = 0;
	if (advert & ADVERTISE_10HALF)
		advertising |= ADVERTISED_10baseT_Half;
	if (advert & ADVERTISE_10FULL)
		advertising |= ADVERTISED_10baseT_Full;
	if (advert & ADVERTISE_100HALF)
		advertising |= ADVERTISED_100baseT_Half;
	if (advert & ADVERTISE_100FULL)
		advertising |= ADVERTISED_100baseT_Full;
	if (ctrl1000 & ADVERTISE_1000HALF)
		advertising |= ADVERTISED_1000baseT_Half;
	if (ctrl1000 & ADVERTISE_1000FULL)
		advertising |= ADVERTISED_1000baseT_Full;

	if (bmcr & BMCR_ANENABLE) {
		int neg, neg1000;

		lp->active_autoneg = 1;
		advertising |= ADVERTISED_Autoneg;

		neg = advert & lpa;
		neg1000 = (ctrl1000 << 2) & stat1000;

		if (neg1000 & (LPA_1000FULL | LPA_1000HALF))
			active_speed = SPEED_1000;
		else if (neg & LPA_100)
			active_speed = SPEED_100;
		else if (neg & (LPA_10HALF | LPA_10FULL))
			active_speed = SPEED_10;
		else
			active_speed = SPEED_INVALID;

		if ((neg1000 & LPA_1000FULL) || (neg & LPA_DUPLEX))
			active_duplex = DUPLEX_FULL;
		else if (active_speed != SPEED_INVALID)
			active_duplex = DUPLEX_HALF;
		else
			active_duplex = DUPLEX_INVALID;
	} else {
		lp->active_autoneg = 0;

		if ((bmcr & BMCR_SPEED1000) && !(bmcr & BMCR_SPEED100))
			active_speed = SPEED_1000;
		else if (bmcr & BMCR_SPEED100)
			active_speed = SPEED_100;
		else
			active_speed = SPEED_10;

		if (bmcr & BMCR_FULLDPLX)
			active_duplex = DUPLEX_FULL;
		else
			active_duplex = DUPLEX_HALF;
	}

	lp->active_advertising = advertising;
	lp->active_speed = active_speed;
	lp->active_duplex = active_duplex;
	*link_up_p = !!(bmsr & BMSR_LSTATUS);

	return 0;
}

static int link_status_1g_rgmii(struct niu *np, int *link_up_p)
{
	struct niu_link_config *lp = &np->link_config;
	u16 current_speed, bmsr;
	unsigned long flags;
	u8 current_duplex;
	int err, link_up;

	link_up = 0;
	current_speed = SPEED_INVALID;
	current_duplex = DUPLEX_INVALID;

	spin_lock_irqsave(&np->lock, flags);

	err = -EINVAL;

	err = mii_read(np, np->phy_addr, MII_BMSR);
	if (err < 0)
		goto out;

	bmsr = err;
	if (bmsr & BMSR_LSTATUS) {
		u16 adv, lpa, common, estat;

		err = mii_read(np, np->phy_addr, MII_ADVERTISE);
		if (err < 0)
			goto out;
		adv = err;

		err = mii_read(np, np->phy_addr, MII_LPA);
		if (err < 0)
			goto out;
		lpa = err;

		common = adv & lpa;

		err = mii_read(np, np->phy_addr, MII_ESTATUS);
		if (err < 0)
			goto out;
		estat = err;
		link_up = 1;
		current_speed = SPEED_1000;
		current_duplex = DUPLEX_FULL;

	}
	lp->active_speed = current_speed;
	lp->active_duplex = current_duplex;
	err = 0;

out:
	spin_unlock_irqrestore(&np->lock, flags);

	*link_up_p = link_up;
	return err;
}

static int link_status_1g(struct niu *np, int *link_up_p)
{
	struct niu_link_config *lp = &np->link_config;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&np->lock, flags);

	err = link_status_mii(np, link_up_p);
	lp->supported |= SUPPORTED_TP;
	lp->active_advertising |= ADVERTISED_TP;

	spin_unlock_irqrestore(&np->lock, flags);
	return err;
}

static int bcm8704_reset(struct niu *np)
{
	int err, limit;

	err = mdio_read(np, np->phy_addr,
			BCM8704_PHYXS_DEV_ADDR, MII_BMCR);
	if (err < 0)
		return err;
	err |= BMCR_RESET;
	err = mdio_write(np, np->phy_addr, BCM8704_PHYXS_DEV_ADDR,
			 MII_BMCR, err);
	if (err)
		return err;

	limit = 1000;
	while (--limit >= 0) {
		err = mdio_read(np, np->phy_addr,
				BCM8704_PHYXS_DEV_ADDR, MII_BMCR);
		if (err < 0)
			return err;
		if (!(err & BMCR_RESET))
			break;
	}
	if (limit < 0) {
		dev_err(np->device, PFX "Port %u PHY will not reset "
			"(bmcr=%04x)\n", np->port, (err & 0xffff));
		return -ENODEV;
	}
	return 0;
}

/* When written, certain PHY registers need to be read back twice
 * in order for the bits to settle properly.
 */
static int bcm8704_user_dev3_readback(struct niu *np, int reg)
{
	int err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR, reg);
	if (err < 0)
		return err;
	err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR, reg);
	if (err < 0)
		return err;
	return 0;
}

static int bcm8706_init_user_dev3(struct niu *np)
{
	int err;


	err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR,
			BCM8704_USER_OPT_DIGITAL_CTRL);
	if (err < 0)
		return err;
	err &= ~USER_ODIG_CTRL_GPIOS;
	err |= (0x3 << USER_ODIG_CTRL_GPIOS_SHIFT);
	err |=  USER_ODIG_CTRL_RESV2;
	err = mdio_write(np, np->phy_addr, BCM8704_USER_DEV3_ADDR,
			 BCM8704_USER_OPT_DIGITAL_CTRL, err);
	if (err)
		return err;

	mdelay(1000);

	return 0;
}

static int bcm8704_init_user_dev3(struct niu *np)
{
	int err;

	err = mdio_write(np, np->phy_addr,
			 BCM8704_USER_DEV3_ADDR, BCM8704_USER_CONTROL,
			 (USER_CONTROL_OPTXRST_LVL |
			  USER_CONTROL_OPBIASFLT_LVL |
			  USER_CONTROL_OBTMPFLT_LVL |
			  USER_CONTROL_OPPRFLT_LVL |
			  USER_CONTROL_OPTXFLT_LVL |
			  USER_CONTROL_OPRXLOS_LVL |
			  USER_CONTROL_OPRXFLT_LVL |
			  USER_CONTROL_OPTXON_LVL |
			  (0x3f << USER_CONTROL_RES1_SHIFT)));
	if (err)
		return err;

	err = mdio_write(np, np->phy_addr,
			 BCM8704_USER_DEV3_ADDR, BCM8704_USER_PMD_TX_CONTROL,
			 (USER_PMD_TX_CTL_XFP_CLKEN |
			  (1 << USER_PMD_TX_CTL_TX_DAC_TXD_SH) |
			  (2 << USER_PMD_TX_CTL_TX_DAC_TXCK_SH) |
			  USER_PMD_TX_CTL_TSCK_LPWREN));
	if (err)
		return err;

	err = bcm8704_user_dev3_readback(np, BCM8704_USER_CONTROL);
	if (err)
		return err;
	err = bcm8704_user_dev3_readback(np, BCM8704_USER_PMD_TX_CONTROL);
	if (err)
		return err;

	err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR,
			BCM8704_USER_OPT_DIGITAL_CTRL);
	if (err < 0)
		return err;
	err &= ~USER_ODIG_CTRL_GPIOS;
	err |= (0x3 << USER_ODIG_CTRL_GPIOS_SHIFT);
	err = mdio_write(np, np->phy_addr, BCM8704_USER_DEV3_ADDR,
			 BCM8704_USER_OPT_DIGITAL_CTRL, err);
	if (err)
		return err;

	mdelay(1000);

	return 0;
}

static int mrvl88x2011_act_led(struct niu *np, int val)
{
	int	err;

	err  = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV2_ADDR,
		MRVL88X2011_LED_8_TO_11_CTL);
	if (err < 0)
		return err;

	err &= ~MRVL88X2011_LED(MRVL88X2011_LED_ACT,MRVL88X2011_LED_CTL_MASK);
	err |=  MRVL88X2011_LED(MRVL88X2011_LED_ACT,val);

	return mdio_write(np, np->phy_addr, MRVL88X2011_USER_DEV2_ADDR,
			  MRVL88X2011_LED_8_TO_11_CTL, err);
}

static int mrvl88x2011_led_blink_rate(struct niu *np, int rate)
{
	int	err;

	err = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV2_ADDR,
			MRVL88X2011_LED_BLINK_CTL);
	if (err >= 0) {
		err &= ~MRVL88X2011_LED_BLKRATE_MASK;
		err |= (rate << 4);

		err = mdio_write(np, np->phy_addr, MRVL88X2011_USER_DEV2_ADDR,
				 MRVL88X2011_LED_BLINK_CTL, err);
	}

	return err;
}

static int xcvr_init_10g_mrvl88x2011(struct niu *np)
{
	int	err;

	/* Set LED functions */
	err = mrvl88x2011_led_blink_rate(np, MRVL88X2011_LED_BLKRATE_134MS);
	if (err)
		return err;

	/* led activity */
	err = mrvl88x2011_act_led(np, MRVL88X2011_LED_CTL_OFF);
	if (err)
		return err;

	err = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV3_ADDR,
			MRVL88X2011_GENERAL_CTL);
	if (err < 0)
		return err;

	err |= MRVL88X2011_ENA_XFPREFCLK;

	err = mdio_write(np, np->phy_addr, MRVL88X2011_USER_DEV3_ADDR,
			 MRVL88X2011_GENERAL_CTL, err);
	if (err < 0)
		return err;

	err = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV1_ADDR,
			MRVL88X2011_PMA_PMD_CTL_1);
	if (err < 0)
		return err;

	if (np->link_config.loopback_mode == LOOPBACK_MAC)
		err |= MRVL88X2011_LOOPBACK;
	else
		err &= ~MRVL88X2011_LOOPBACK;

	err = mdio_write(np, np->phy_addr, MRVL88X2011_USER_DEV1_ADDR,
			 MRVL88X2011_PMA_PMD_CTL_1, err);
	if (err < 0)
		return err;

	/* Enable PMD  */
	return mdio_write(np, np->phy_addr, MRVL88X2011_USER_DEV1_ADDR,
			  MRVL88X2011_10G_PMD_TX_DIS, MRVL88X2011_ENA_PMDTX);
}


static int xcvr_diag_bcm870x(struct niu *np)
{
	u16 analog_stat0, tx_alarm_status;
	int err = 0;

#if 1
	err = mdio_read(np, np->phy_addr, BCM8704_PMA_PMD_DEV_ADDR,
			MII_STAT1000);
	if (err < 0)
		return err;
	pr_info(PFX "Port %u PMA_PMD(MII_STAT1000) [%04x]\n",
		np->port, err);

	err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR, 0x20);
	if (err < 0)
		return err;
	pr_info(PFX "Port %u USER_DEV3(0x20) [%04x]\n",
		np->port, err);

	err = mdio_read(np, np->phy_addr, BCM8704_PHYXS_DEV_ADDR,
			MII_NWAYTEST);
	if (err < 0)
		return err;
	pr_info(PFX "Port %u PHYXS(MII_NWAYTEST) [%04x]\n",
		np->port, err);
#endif

	/* XXX dig this out it might not be so useful XXX */
	err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR,
			BCM8704_USER_ANALOG_STATUS0);
	if (err < 0)
		return err;
	err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR,
			BCM8704_USER_ANALOG_STATUS0);
	if (err < 0)
		return err;
	analog_stat0 = err;

	err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR,
			BCM8704_USER_TX_ALARM_STATUS);
	if (err < 0)
		return err;
	err = mdio_read(np, np->phy_addr, BCM8704_USER_DEV3_ADDR,
			BCM8704_USER_TX_ALARM_STATUS);
	if (err < 0)
		return err;
	tx_alarm_status = err;

	if (analog_stat0 != 0x03fc) {
		if ((analog_stat0 == 0x43bc) && (tx_alarm_status != 0)) {
			pr_info(PFX "Port %u cable not connected "
				"or bad cable.\n", np->port);
		} else if (analog_stat0 == 0x639c) {
			pr_info(PFX "Port %u optical module is bad "
				"or missing.\n", np->port);
		}
	}

	return 0;
}

static int xcvr_10g_set_lb_bcm870x(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	int err;

	err = mdio_read(np, np->phy_addr, BCM8704_PCS_DEV_ADDR,
			MII_BMCR);
	if (err < 0)
		return err;

	err &= ~BMCR_LOOPBACK;

	if (lp->loopback_mode == LOOPBACK_MAC)
		err |= BMCR_LOOPBACK;

	err = mdio_write(np, np->phy_addr, BCM8704_PCS_DEV_ADDR,
			 MII_BMCR, err);
	if (err)
		return err;

	return 0;
}

static int xcvr_init_10g_bcm8706(struct niu *np)
{
	int err = 0;
	u64 val;

	if ((np->flags & NIU_FLAGS_HOTPLUG_PHY) &&
	    (np->flags & NIU_FLAGS_HOTPLUG_PHY_PRESENT) == 0)
			return err;

	val = nr64_mac(XMAC_CONFIG);
	val &= ~XMAC_CONFIG_LED_POLARITY;
	val |= XMAC_CONFIG_FORCE_LED_ON;
	nw64_mac(XMAC_CONFIG, val);

	val = nr64(MIF_CONFIG);
	val |= MIF_CONFIG_INDIRECT_MODE;
	nw64(MIF_CONFIG, val);

	err = bcm8704_reset(np);
	if (err)
		return err;

	err = xcvr_10g_set_lb_bcm870x(np);
	if (err)
		return err;

	err = bcm8706_init_user_dev3(np);
	if (err)
		return err;

	err = xcvr_diag_bcm870x(np);
	if (err)
		return err;

	return 0;
}

static int xcvr_init_10g_bcm8704(struct niu *np)
{
	int err;

	err = bcm8704_reset(np);
	if (err)
		return err;

	err = bcm8704_init_user_dev3(np);
	if (err)
		return err;

	err = xcvr_10g_set_lb_bcm870x(np);
	if (err)
		return err;

	err =  xcvr_diag_bcm870x(np);
	if (err)
		return err;

	return 0;
}

static int xcvr_init_10g(struct niu *np)
{
	int phy_id, err;
	u64 val;

	val = nr64_mac(XMAC_CONFIG);
	val &= ~XMAC_CONFIG_LED_POLARITY;
	val |= XMAC_CONFIG_FORCE_LED_ON;
	nw64_mac(XMAC_CONFIG, val);

	/* XXX shared resource, lock parent XXX */
	val = nr64(MIF_CONFIG);
	val |= MIF_CONFIG_INDIRECT_MODE;
	nw64(MIF_CONFIG, val);

	phy_id = phy_decode(np->parent->port_phy, np->port);
	phy_id = np->parent->phy_probe_info.phy_id[phy_id][np->port];

	/* handle different phy types */
	switch (phy_id & NIU_PHY_ID_MASK) {
	case NIU_PHY_ID_MRVL88X2011:
		err = xcvr_init_10g_mrvl88x2011(np);
		break;

	default: /* bcom 8704 */
		err = xcvr_init_10g_bcm8704(np);
		break;
	}

	return 0;
}

static int mii_reset(struct niu *np)
{
	int limit, err;

	err = mii_write(np, np->phy_addr, MII_BMCR, BMCR_RESET);
	if (err)
		return err;

	limit = 1000;
	while (--limit >= 0) {
		udelay(500);
		err = mii_read(np, np->phy_addr, MII_BMCR);
		if (err < 0)
			return err;
		if (!(err & BMCR_RESET))
			break;
	}
	if (limit < 0) {
		dev_err(np->device, PFX "Port %u MII would not reset, "
			"bmcr[%04x]\n", np->port, err);
		return -ENODEV;
	}

	return 0;
}

static int xcvr_init_1g_rgmii(struct niu *np)
{
	int err;
	u64 val;
	u16 bmcr, bmsr, estat;

	val = nr64(MIF_CONFIG);
	val &= ~MIF_CONFIG_INDIRECT_MODE;
	nw64(MIF_CONFIG, val);

	err = mii_reset(np);
	if (err)
		return err;

	err = mii_read(np, np->phy_addr, MII_BMSR);
	if (err < 0)
		return err;
	bmsr = err;

	estat = 0;
	if (bmsr & BMSR_ESTATEN) {
		err = mii_read(np, np->phy_addr, MII_ESTATUS);
		if (err < 0)
			return err;
		estat = err;
	}

	bmcr = 0;
	err = mii_write(np, np->phy_addr, MII_BMCR, bmcr);
	if (err)
		return err;

	if (bmsr & BMSR_ESTATEN) {
		u16 ctrl1000 = 0;

		if (estat & ESTATUS_1000_TFULL)
			ctrl1000 |= ADVERTISE_1000FULL;
		err = mii_write(np, np->phy_addr, MII_CTRL1000, ctrl1000);
		if (err)
			return err;
	}

	bmcr = (BMCR_SPEED1000 | BMCR_FULLDPLX);

	err = mii_write(np, np->phy_addr, MII_BMCR, bmcr);
	if (err)
		return err;

	err = mii_read(np, np->phy_addr, MII_BMCR);
	if (err < 0)
		return err;
	bmcr = mii_read(np, np->phy_addr, MII_BMCR);

	err = mii_read(np, np->phy_addr, MII_BMSR);
	if (err < 0)
		return err;

	return 0;
}

static int mii_init_common(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	u16 bmcr, bmsr, adv, estat;
	int err;

	err = mii_reset(np);
	if (err)
		return err;

	err = mii_read(np, np->phy_addr, MII_BMSR);
	if (err < 0)
		return err;
	bmsr = err;

	estat = 0;
	if (bmsr & BMSR_ESTATEN) {
		err = mii_read(np, np->phy_addr, MII_ESTATUS);
		if (err < 0)
			return err;
		estat = err;
	}

	bmcr = 0;
	err = mii_write(np, np->phy_addr, MII_BMCR, bmcr);
	if (err)
		return err;

	if (lp->loopback_mode == LOOPBACK_MAC) {
		bmcr |= BMCR_LOOPBACK;
		if (lp->active_speed == SPEED_1000)
			bmcr |= BMCR_SPEED1000;
		if (lp->active_duplex == DUPLEX_FULL)
			bmcr |= BMCR_FULLDPLX;
	}

	if (lp->loopback_mode == LOOPBACK_PHY) {
		u16 aux;

		aux = (BCM5464R_AUX_CTL_EXT_LB |
		       BCM5464R_AUX_CTL_WRITE_1);
		err = mii_write(np, np->phy_addr, BCM5464R_AUX_CTL, aux);
		if (err)
			return err;
	}

	if (lp->autoneg) {
		u16 ctrl1000;

		adv = ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP;
		if ((bmsr & BMSR_10HALF) &&
			(lp->advertising & ADVERTISED_10baseT_Half))
			adv |= ADVERTISE_10HALF;
		if ((bmsr & BMSR_10FULL) &&
			(lp->advertising & ADVERTISED_10baseT_Full))
			adv |= ADVERTISE_10FULL;
		if ((bmsr & BMSR_100HALF) &&
			(lp->advertising & ADVERTISED_100baseT_Half))
			adv |= ADVERTISE_100HALF;
		if ((bmsr & BMSR_100FULL) &&
			(lp->advertising & ADVERTISED_100baseT_Full))
			adv |= ADVERTISE_100FULL;
		err = mii_write(np, np->phy_addr, MII_ADVERTISE, adv);
		if (err)
			return err;

		if (likely(bmsr & BMSR_ESTATEN)) {
			ctrl1000 = 0;
			if ((estat & ESTATUS_1000_THALF) &&
				(lp->advertising & ADVERTISED_1000baseT_Half))
				ctrl1000 |= ADVERTISE_1000HALF;
			if ((estat & ESTATUS_1000_TFULL) &&
				(lp->advertising & ADVERTISED_1000baseT_Full))
				ctrl1000 |= ADVERTISE_1000FULL;
			err = mii_write(np, np->phy_addr,
					MII_CTRL1000, ctrl1000);
			if (err)
				return err;
		}

		bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
	} else {
		/* !lp->autoneg */
		int fulldpx;

		if (lp->duplex == DUPLEX_FULL) {
			bmcr |= BMCR_FULLDPLX;
			fulldpx = 1;
		} else if (lp->duplex == DUPLEX_HALF)
			fulldpx = 0;
		else
			return -EINVAL;

		if (lp->speed == SPEED_1000) {
			/* if X-full requested while not supported, or
			   X-half requested while not supported... */
			if ((fulldpx && !(estat & ESTATUS_1000_TFULL)) ||
				(!fulldpx && !(estat & ESTATUS_1000_THALF)))
				return -EINVAL;
			bmcr |= BMCR_SPEED1000;
		} else if (lp->speed == SPEED_100) {
			if ((fulldpx && !(bmsr & BMSR_100FULL)) ||
				(!fulldpx && !(bmsr & BMSR_100HALF)))
				return -EINVAL;
			bmcr |= BMCR_SPEED100;
		} else if (lp->speed == SPEED_10) {
			if ((fulldpx && !(bmsr & BMSR_10FULL)) ||
				(!fulldpx && !(bmsr & BMSR_10HALF)))
				return -EINVAL;
		} else
			return -EINVAL;
	}

	err = mii_write(np, np->phy_addr, MII_BMCR, bmcr);
	if (err)
		return err;

#if 0
	err = mii_read(np, np->phy_addr, MII_BMCR);
	if (err < 0)
		return err;
	bmcr = err;

	err = mii_read(np, np->phy_addr, MII_BMSR);
	if (err < 0)
		return err;
	bmsr = err;

	pr_info(PFX "Port %u after MII init bmcr[%04x] bmsr[%04x]\n",
		np->port, bmcr, bmsr);
#endif

	return 0;
}

static int xcvr_init_1g(struct niu *np)
{
	u64 val;

	/* XXX shared resource, lock parent XXX */
	val = nr64(MIF_CONFIG);
	val &= ~MIF_CONFIG_INDIRECT_MODE;
	nw64(MIF_CONFIG, val);

	return mii_init_common(np);
}

static int niu_xcvr_init(struct niu *np)
{
	const struct niu_phy_ops *ops = np->phy_ops;
	int err;

	err = 0;
	if (ops->xcvr_init)
		err = ops->xcvr_init(np);

	return err;
}

static int niu_serdes_init(struct niu *np)
{
	const struct niu_phy_ops *ops = np->phy_ops;
	int err;

	err = 0;
	if (ops->serdes_init)
		err = ops->serdes_init(np);

	return err;
}

static void niu_init_xif(struct niu *);
static void niu_handle_led(struct niu *, int status);

static int niu_link_status_common(struct niu *np, int link_up)
{
	struct niu_link_config *lp = &np->link_config;
	struct net_device *dev = np->dev;
	unsigned long flags;

	if (!netif_carrier_ok(dev) && link_up) {
		niuinfo(LINK, "%s: Link is up at %s, %s duplex\n",
		       dev->name,
		       (lp->active_speed == SPEED_10000 ?
			"10Gb/sec" :
			(lp->active_speed == SPEED_1000 ?
			 "1Gb/sec" :
			 (lp->active_speed == SPEED_100 ?
			  "100Mbit/sec" : "10Mbit/sec"))),
		       (lp->active_duplex == DUPLEX_FULL ?
			"full" : "half"));

		spin_lock_irqsave(&np->lock, flags);
		niu_init_xif(np);
		niu_handle_led(np, 1);
		spin_unlock_irqrestore(&np->lock, flags);

		netif_carrier_on(dev);
	} else if (netif_carrier_ok(dev) && !link_up) {
		niuwarn(LINK, "%s: Link is down\n", dev->name);
		spin_lock_irqsave(&np->lock, flags);
		niu_handle_led(np, 0);
		spin_unlock_irqrestore(&np->lock, flags);
		netif_carrier_off(dev);
	}

	return 0;
}

static int link_status_10g_mrvl(struct niu *np, int *link_up_p)
{
	int err, link_up, pma_status, pcs_status;

	link_up = 0;

	err = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV1_ADDR,
			MRVL88X2011_10G_PMD_STATUS_2);
	if (err < 0)
		goto out;

	/* Check PMA/PMD Register: 1.0001.2 == 1 */
	err = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV1_ADDR,
			MRVL88X2011_PMA_PMD_STATUS_1);
	if (err < 0)
		goto out;

	pma_status = ((err & MRVL88X2011_LNK_STATUS_OK) ? 1 : 0);

        /* Check PMC Register : 3.0001.2 == 1: read twice */
	err = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV3_ADDR,
			MRVL88X2011_PMA_PMD_STATUS_1);
	if (err < 0)
		goto out;

	err = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV3_ADDR,
			MRVL88X2011_PMA_PMD_STATUS_1);
	if (err < 0)
		goto out;

	pcs_status = ((err & MRVL88X2011_LNK_STATUS_OK) ? 1 : 0);

        /* Check XGXS Register : 4.0018.[0-3,12] */
	err = mdio_read(np, np->phy_addr, MRVL88X2011_USER_DEV4_ADDR,
			MRVL88X2011_10G_XGXS_LANE_STAT);
	if (err < 0)
		goto out;

	if (err == (PHYXS_XGXS_LANE_STAT_ALINGED | PHYXS_XGXS_LANE_STAT_LANE3 |
		    PHYXS_XGXS_LANE_STAT_LANE2 | PHYXS_XGXS_LANE_STAT_LANE1 |
		    PHYXS_XGXS_LANE_STAT_LANE0 | PHYXS_XGXS_LANE_STAT_MAGIC |
		    0x800))
		link_up = (pma_status && pcs_status) ? 1 : 0;

	np->link_config.active_speed = SPEED_10000;
	np->link_config.active_duplex = DUPLEX_FULL;
	err = 0;
out:
	mrvl88x2011_act_led(np, (link_up ?
				 MRVL88X2011_LED_CTL_PCS_ACT :
				 MRVL88X2011_LED_CTL_OFF));

	*link_up_p = link_up;
	return err;
}

static int link_status_10g_bcm8706(struct niu *np, int *link_up_p)
{
	int err, link_up;
	link_up = 0;

	err = mdio_read(np, np->phy_addr, BCM8704_PMA_PMD_DEV_ADDR,
			BCM8704_PMD_RCV_SIGDET);
	if (err < 0)
		goto out;
	if (!(err & PMD_RCV_SIGDET_GLOBAL)) {
		err = 0;
		goto out;
	}

	err = mdio_read(np, np->phy_addr, BCM8704_PCS_DEV_ADDR,
			BCM8704_PCS_10G_R_STATUS);
	if (err < 0)
		goto out;

	if (!(err & PCS_10G_R_STATUS_BLK_LOCK)) {
		err = 0;
		goto out;
	}

	err = mdio_read(np, np->phy_addr, BCM8704_PHYXS_DEV_ADDR,
			BCM8704_PHYXS_XGXS_LANE_STAT);
	if (err < 0)
		goto out;
	if (err != (PHYXS_XGXS_LANE_STAT_ALINGED |
		    PHYXS_XGXS_LANE_STAT_MAGIC |
		    PHYXS_XGXS_LANE_STAT_PATTEST |
		    PHYXS_XGXS_LANE_STAT_LANE3 |
		    PHYXS_XGXS_LANE_STAT_LANE2 |
		    PHYXS_XGXS_LANE_STAT_LANE1 |
		    PHYXS_XGXS_LANE_STAT_LANE0)) {
		err = 0;
		np->link_config.active_speed = SPEED_INVALID;
		np->link_config.active_duplex = DUPLEX_INVALID;
		goto out;
	}

	link_up = 1;
	np->link_config.active_speed = SPEED_10000;
	np->link_config.active_duplex = DUPLEX_FULL;
	err = 0;

out:
	*link_up_p = link_up;
	if (np->flags & NIU_FLAGS_HOTPLUG_PHY)
		err = 0;
	return err;
}

static int link_status_10g_bcom(struct niu *np, int *link_up_p)
{
	int err, link_up;

	link_up = 0;

	err = mdio_read(np, np->phy_addr, BCM8704_PMA_PMD_DEV_ADDR,
			BCM8704_PMD_RCV_SIGDET);
	if (err < 0)
		goto out;
	if (!(err & PMD_RCV_SIGDET_GLOBAL)) {
		err = 0;
		goto out;
	}

	err = mdio_read(np, np->phy_addr, BCM8704_PCS_DEV_ADDR,
			BCM8704_PCS_10G_R_STATUS);
	if (err < 0)
		goto out;
	if (!(err & PCS_10G_R_STATUS_BLK_LOCK)) {
		err = 0;
		goto out;
	}

	err = mdio_read(np, np->phy_addr, BCM8704_PHYXS_DEV_ADDR,
			BCM8704_PHYXS_XGXS_LANE_STAT);
	if (err < 0)
		goto out;

	if (err != (PHYXS_XGXS_LANE_STAT_ALINGED |
		    PHYXS_XGXS_LANE_STAT_MAGIC |
		    PHYXS_XGXS_LANE_STAT_LANE3 |
		    PHYXS_XGXS_LANE_STAT_LANE2 |
		    PHYXS_XGXS_LANE_STAT_LANE1 |
		    PHYXS_XGXS_LANE_STAT_LANE0)) {
		err = 0;
		goto out;
	}

	link_up = 1;
	np->link_config.active_speed = SPEED_10000;
	np->link_config.active_duplex = DUPLEX_FULL;
	err = 0;

out:
	*link_up_p = link_up;
	return err;
}

static int link_status_10g(struct niu *np, int *link_up_p)
{
	unsigned long flags;
	int err = -EINVAL;

	spin_lock_irqsave(&np->lock, flags);

	if (np->link_config.loopback_mode == LOOPBACK_DISABLED) {
		int phy_id;

		phy_id = phy_decode(np->parent->port_phy, np->port);
		phy_id = np->parent->phy_probe_info.phy_id[phy_id][np->port];

		/* handle different phy types */
		switch (phy_id & NIU_PHY_ID_MASK) {
		case NIU_PHY_ID_MRVL88X2011:
			err = link_status_10g_mrvl(np, link_up_p);
			break;

		default: /* bcom 8704 */
			err = link_status_10g_bcom(np, link_up_p);
			break;
		}
	}

	spin_unlock_irqrestore(&np->lock, flags);

	return err;
}

static int niu_10g_phy_present(struct niu *np)
{
	u64 sig, mask, val;

	sig = nr64(ESR_INT_SIGNALS);
	switch (np->port) {
	case 0:
		mask = ESR_INT_SIGNALS_P0_BITS;
		val = (ESR_INT_SRDY0_P0 |
		       ESR_INT_DET0_P0 |
		       ESR_INT_XSRDY_P0 |
		       ESR_INT_XDP_P0_CH3 |
		       ESR_INT_XDP_P0_CH2 |
		       ESR_INT_XDP_P0_CH1 |
		       ESR_INT_XDP_P0_CH0);
		break;

	case 1:
		mask = ESR_INT_SIGNALS_P1_BITS;
		val = (ESR_INT_SRDY0_P1 |
		       ESR_INT_DET0_P1 |
		       ESR_INT_XSRDY_P1 |
		       ESR_INT_XDP_P1_CH3 |
		       ESR_INT_XDP_P1_CH2 |
		       ESR_INT_XDP_P1_CH1 |
		       ESR_INT_XDP_P1_CH0);
		break;

	default:
		return 0;
	}

	if ((sig & mask) != val)
		return 0;
	return 1;
}

static int link_status_10g_hotplug(struct niu *np, int *link_up_p)
{
	unsigned long flags;
	int err = 0;
	int phy_present;
	int phy_present_prev;

	spin_lock_irqsave(&np->lock, flags);

	if (np->link_config.loopback_mode == LOOPBACK_DISABLED) {
		phy_present_prev = (np->flags & NIU_FLAGS_HOTPLUG_PHY_PRESENT) ?
			1 : 0;
		phy_present = niu_10g_phy_present(np);
		if (phy_present != phy_present_prev) {
			/* state change */
			if (phy_present) {
				np->flags |= NIU_FLAGS_HOTPLUG_PHY_PRESENT;
				if (np->phy_ops->xcvr_init)
					err = np->phy_ops->xcvr_init(np);
				if (err) {
					/* debounce */
					np->flags &= ~NIU_FLAGS_HOTPLUG_PHY_PRESENT;
				}
			} else {
				np->flags &= ~NIU_FLAGS_HOTPLUG_PHY_PRESENT;
				*link_up_p = 0;
				niuwarn(LINK, "%s: Hotplug PHY Removed\n",
					np->dev->name);
			}
		}
		if (np->flags & NIU_FLAGS_HOTPLUG_PHY_PRESENT)
			err = link_status_10g_bcm8706(np, link_up_p);
	}

	spin_unlock_irqrestore(&np->lock, flags);

	return err;
}

static int niu_link_status(struct niu *np, int *link_up_p)
{
	const struct niu_phy_ops *ops = np->phy_ops;
	int err;

	err = 0;
	if (ops->link_status)
		err = ops->link_status(np, link_up_p);

	return err;
}

static void niu_timer(unsigned long __opaque)
{
	struct niu *np = (struct niu *) __opaque;
	unsigned long off;
	int err, link_up;

	err = niu_link_status(np, &link_up);
	if (!err)
		niu_link_status_common(np, link_up);

	if (netif_carrier_ok(np->dev))
		off = 5 * HZ;
	else
		off = 1 * HZ;
	np->timer.expires = jiffies + off;

	add_timer(&np->timer);
}

static const struct niu_phy_ops phy_ops_10g_serdes = {
	.serdes_init		= serdes_init_10g_serdes,
	.link_status		= link_status_10g_serdes,
};

static const struct niu_phy_ops phy_ops_10g_serdes_niu = {
	.serdes_init		= serdes_init_niu_10g_serdes,
	.link_status		= link_status_10g_serdes,
};

static const struct niu_phy_ops phy_ops_1g_serdes_niu = {
	.serdes_init		= serdes_init_niu_1g_serdes,
	.link_status		= link_status_1g_serdes,
};

static const struct niu_phy_ops phy_ops_1g_rgmii = {
	.xcvr_init		= xcvr_init_1g_rgmii,
	.link_status		= link_status_1g_rgmii,
};

static const struct niu_phy_ops phy_ops_10g_fiber_niu = {
	.serdes_init		= serdes_init_niu_10g_fiber,
	.xcvr_init		= xcvr_init_10g,
	.link_status		= link_status_10g,
};

static const struct niu_phy_ops phy_ops_10g_fiber = {
	.serdes_init		= serdes_init_10g,
	.xcvr_init		= xcvr_init_10g,
	.link_status		= link_status_10g,
};

static const struct niu_phy_ops phy_ops_10g_fiber_hotplug = {
	.serdes_init		= serdes_init_10g,
	.xcvr_init		= xcvr_init_10g_bcm8706,
	.link_status		= link_status_10g_hotplug,
};

static const struct niu_phy_ops phy_ops_10g_copper = {
	.serdes_init		= serdes_init_10g,
	.link_status		= link_status_10g, /* XXX */
};

static const struct niu_phy_ops phy_ops_1g_fiber = {
	.serdes_init		= serdes_init_1g,
	.xcvr_init		= xcvr_init_1g,
	.link_status		= link_status_1g,
};

static const struct niu_phy_ops phy_ops_1g_copper = {
	.xcvr_init		= xcvr_init_1g,
	.link_status		= link_status_1g,
};

struct niu_phy_template {
	const struct niu_phy_ops	*ops;
	u32				phy_addr_base;
};

static const struct niu_phy_template phy_template_niu_10g_fiber = {
	.ops		= &phy_ops_10g_fiber_niu,
	.phy_addr_base	= 16,
};

static const struct niu_phy_template phy_template_niu_10g_serdes = {
	.ops		= &phy_ops_10g_serdes_niu,
	.phy_addr_base	= 0,
};

static const struct niu_phy_template phy_template_niu_1g_serdes = {
	.ops		= &phy_ops_1g_serdes_niu,
	.phy_addr_base	= 0,
};

static const struct niu_phy_template phy_template_10g_fiber = {
	.ops		= &phy_ops_10g_fiber,
	.phy_addr_base	= 8,
};

static const struct niu_phy_template phy_template_10g_fiber_hotplug = {
	.ops		= &phy_ops_10g_fiber_hotplug,
	.phy_addr_base	= 8,
};

static const struct niu_phy_template phy_template_10g_copper = {
	.ops		= &phy_ops_10g_copper,
	.phy_addr_base	= 10,
};

static const struct niu_phy_template phy_template_1g_fiber = {
	.ops		= &phy_ops_1g_fiber,
	.phy_addr_base	= 0,
};

static const struct niu_phy_template phy_template_1g_copper = {
	.ops		= &phy_ops_1g_copper,
	.phy_addr_base	= 0,
};

static const struct niu_phy_template phy_template_1g_rgmii = {
	.ops		= &phy_ops_1g_rgmii,
	.phy_addr_base	= 0,
};

static const struct niu_phy_template phy_template_10g_serdes = {
	.ops		= &phy_ops_10g_serdes,
	.phy_addr_base	= 0,
};

static int niu_atca_port_num[4] = {
	0, 0,  11, 10
};

static int serdes_init_10g_serdes(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	unsigned long ctrl_reg, test_cfg_reg, pll_cfg, i;
	u64 ctrl_val, test_cfg_val, sig, mask, val;
	u64 reset_val;

	switch (np->port) {
	case 0:
		reset_val =  ENET_SERDES_RESET_0;
		ctrl_reg = ENET_SERDES_0_CTRL_CFG;
		test_cfg_reg = ENET_SERDES_0_TEST_CFG;
		pll_cfg = ENET_SERDES_0_PLL_CFG;
		break;
	case 1:
		reset_val =  ENET_SERDES_RESET_1;
		ctrl_reg = ENET_SERDES_1_CTRL_CFG;
		test_cfg_reg = ENET_SERDES_1_TEST_CFG;
		pll_cfg = ENET_SERDES_1_PLL_CFG;
		break;

	default:
		return -EINVAL;
	}
	ctrl_val = (ENET_SERDES_CTRL_SDET_0 |
		    ENET_SERDES_CTRL_SDET_1 |
		    ENET_SERDES_CTRL_SDET_2 |
		    ENET_SERDES_CTRL_SDET_3 |
		    (0x5 << ENET_SERDES_CTRL_EMPH_0_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_1_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_2_SHIFT) |
		    (0x5 << ENET_SERDES_CTRL_EMPH_3_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_0_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_1_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_2_SHIFT) |
		    (0x1 << ENET_SERDES_CTRL_LADJ_3_SHIFT));
	test_cfg_val = 0;

	if (lp->loopback_mode == LOOPBACK_PHY) {
		test_cfg_val |= ((ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_0_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_1_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_2_SHIFT) |
				 (ENET_TEST_MD_PAD_LOOPBACK <<
				  ENET_SERDES_TEST_MD_3_SHIFT));
	}

	esr_reset(np);
	nw64(pll_cfg, ENET_SERDES_PLL_FBDIV2);
	nw64(ctrl_reg, ctrl_val);
	nw64(test_cfg_reg, test_cfg_val);

	/* Initialize all 4 lanes of the SERDES.  */
	for (i = 0; i < 4; i++) {
		u32 rxtx_ctrl, glue0;
		int err;

		err = esr_read_rxtx_ctrl(np, i, &rxtx_ctrl);
		if (err)
			return err;
		err = esr_read_glue0(np, i, &glue0);
		if (err)
			return err;

		rxtx_ctrl &= ~(ESR_RXTX_CTRL_VMUXLO);
		rxtx_ctrl |= (ESR_RXTX_CTRL_ENSTRETCH |
			      (2 << ESR_RXTX_CTRL_VMUXLO_SHIFT));

		glue0 &= ~(ESR_GLUE_CTRL0_SRATE |
			   ESR_GLUE_CTRL0_THCNT |
			   ESR_GLUE_CTRL0_BLTIME);
		glue0 |= (ESR_GLUE_CTRL0_RXLOSENAB |
			  (0xf << ESR_GLUE_CTRL0_SRATE_SHIFT) |
			  (0xff << ESR_GLUE_CTRL0_THCNT_SHIFT) |
			  (BLTIME_300_CYCLES <<
			   ESR_GLUE_CTRL0_BLTIME_SHIFT));

		err = esr_write_rxtx_ctrl(np, i, rxtx_ctrl);
		if (err)
			return err;
		err = esr_write_glue0(np, i, glue0);
		if (err)
			return err;
	}


	sig = nr64(ESR_INT_SIGNALS);
	switch (np->port) {
	case 0:
		mask = ESR_INT_SIGNALS_P0_BITS;
		val = (ESR_INT_SRDY0_P0 |
		       ESR_INT_DET0_P0 |
		       ESR_INT_XSRDY_P0 |
		       ESR_INT_XDP_P0_CH3 |
		       ESR_INT_XDP_P0_CH2 |
		       ESR_INT_XDP_P0_CH1 |
		       ESR_INT_XDP_P0_CH0);
		break;

	case 1:
		mask = ESR_INT_SIGNALS_P1_BITS;
		val = (ESR_INT_SRDY0_P1 |
		       ESR_INT_DET0_P1 |
		       ESR_INT_XSRDY_P1 |
		       ESR_INT_XDP_P1_CH3 |
		       ESR_INT_XDP_P1_CH2 |
		       ESR_INT_XDP_P1_CH1 |
		       ESR_INT_XDP_P1_CH0);
		break;

	default:
		return -EINVAL;
	}

	if ((sig & mask) != val) {
		int err;
		err = serdes_init_1g_serdes(np);
		if (!err) {
			np->flags &= ~NIU_FLAGS_10G;
			np->mac_xcvr = MAC_XCVR_PCS;
		}  else {
			dev_err(np->device, PFX "Port %u 10G/1G SERDES Link Failed \n",
			 np->port);
			return -ENODEV;
		}
	}

	return 0;
}

static int niu_determine_phy_disposition(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	u8 plat_type = parent->plat_type;
	const struct niu_phy_template *tp;
	u32 phy_addr_off = 0;

	if (plat_type == PLAT_TYPE_NIU) {
		switch (np->flags &
			(NIU_FLAGS_10G |
			 NIU_FLAGS_FIBER |
			 NIU_FLAGS_XCVR_SERDES)) {
		case NIU_FLAGS_10G | NIU_FLAGS_XCVR_SERDES:
			/* 10G Serdes */
			tp = &phy_template_niu_10g_serdes;
			break;
		case NIU_FLAGS_XCVR_SERDES:
			/* 1G Serdes */
			tp = &phy_template_niu_1g_serdes;
			break;
		case NIU_FLAGS_10G | NIU_FLAGS_FIBER:
			/* 10G Fiber */
		default:
			tp = &phy_template_niu_10g_fiber;
			phy_addr_off += np->port;
			break;
		}
	} else {
		switch (np->flags &
			(NIU_FLAGS_10G |
			 NIU_FLAGS_FIBER |
			 NIU_FLAGS_XCVR_SERDES)) {
		case 0:
			/* 1G copper */
			tp = &phy_template_1g_copper;
			if (plat_type == PLAT_TYPE_VF_P0)
				phy_addr_off = 10;
			else if (plat_type == PLAT_TYPE_VF_P1)
				phy_addr_off = 26;

			phy_addr_off += (np->port ^ 0x3);
			break;

		case NIU_FLAGS_10G:
			/* 10G copper */
			tp = &phy_template_10g_copper;
			break;

		case NIU_FLAGS_FIBER:
			/* 1G fiber */
			tp = &phy_template_1g_fiber;
			break;

		case NIU_FLAGS_10G | NIU_FLAGS_FIBER:
			/* 10G fiber */
			tp = &phy_template_10g_fiber;
			if (plat_type == PLAT_TYPE_VF_P0 ||
			    plat_type == PLAT_TYPE_VF_P1)
				phy_addr_off = 8;
			phy_addr_off += np->port;
			if (np->flags & NIU_FLAGS_HOTPLUG_PHY) {
				tp = &phy_template_10g_fiber_hotplug;
				if (np->port == 0)
					phy_addr_off = 8;
				if (np->port == 1)
					phy_addr_off = 12;
			}
			break;

		case NIU_FLAGS_10G | NIU_FLAGS_XCVR_SERDES:
		case NIU_FLAGS_XCVR_SERDES | NIU_FLAGS_FIBER:
		case NIU_FLAGS_XCVR_SERDES:
			switch(np->port) {
			case 0:
			case 1:
				tp = &phy_template_10g_serdes;
				break;
			case 2:
			case 3:
				tp = &phy_template_1g_rgmii;
				break;
			default:
				return -EINVAL;
				break;
			}
			phy_addr_off = niu_atca_port_num[np->port];
			break;

		default:
			return -EINVAL;
		}
	}

	np->phy_ops = tp->ops;
	np->phy_addr = tp->phy_addr_base + phy_addr_off;

	return 0;
}

static int niu_init_link(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	int err, ignore;

	if (parent->plat_type == PLAT_TYPE_NIU) {
		err = niu_xcvr_init(np);
		if (err)
			return err;
		msleep(200);
	}
	err = niu_serdes_init(np);
	if (err)
		return err;
	msleep(200);
	err = niu_xcvr_init(np);
	if (!err)
		niu_link_status(np, &ignore);
	return 0;
}

static void niu_set_primary_mac(struct niu *np, unsigned char *addr)
{
	u16 reg0 = addr[4] << 8 | addr[5];
	u16 reg1 = addr[2] << 8 | addr[3];
	u16 reg2 = addr[0] << 8 | addr[1];

	if (np->flags & NIU_FLAGS_XMAC) {
		nw64_mac(XMAC_ADDR0, reg0);
		nw64_mac(XMAC_ADDR1, reg1);
		nw64_mac(XMAC_ADDR2, reg2);
	} else {
		nw64_mac(BMAC_ADDR0, reg0);
		nw64_mac(BMAC_ADDR1, reg1);
		nw64_mac(BMAC_ADDR2, reg2);
	}
}

static int niu_num_alt_addr(struct niu *np)
{
	if (np->flags & NIU_FLAGS_XMAC)
		return XMAC_NUM_ALT_ADDR;
	else
		return BMAC_NUM_ALT_ADDR;
}

static int niu_set_alt_mac(struct niu *np, int index, unsigned char *addr)
{
	u16 reg0 = addr[4] << 8 | addr[5];
	u16 reg1 = addr[2] << 8 | addr[3];
	u16 reg2 = addr[0] << 8 | addr[1];

	if (index >= niu_num_alt_addr(np))
		return -EINVAL;

	if (np->flags & NIU_FLAGS_XMAC) {
		nw64_mac(XMAC_ALT_ADDR0(index), reg0);
		nw64_mac(XMAC_ALT_ADDR1(index), reg1);
		nw64_mac(XMAC_ALT_ADDR2(index), reg2);
	} else {
		nw64_mac(BMAC_ALT_ADDR0(index), reg0);
		nw64_mac(BMAC_ALT_ADDR1(index), reg1);
		nw64_mac(BMAC_ALT_ADDR2(index), reg2);
	}

	return 0;
}

static int niu_enable_alt_mac(struct niu *np, int index, int on)
{
	unsigned long reg;
	u64 val, mask;

	if (index >= niu_num_alt_addr(np))
		return -EINVAL;

	if (np->flags & NIU_FLAGS_XMAC) {
		reg = XMAC_ADDR_CMPEN;
		mask = 1 << index;
	} else {
		reg = BMAC_ADDR_CMPEN;
		mask = 1 << (index + 1);
	}

	val = nr64_mac(reg);
	if (on)
		val |= mask;
	else
		val &= ~mask;
	nw64_mac(reg, val);

	return 0;
}

static void __set_rdc_table_num_hw(struct niu *np, unsigned long reg,
				   int num, int mac_pref)
{
	u64 val = nr64_mac(reg);
	val &= ~(HOST_INFO_MACRDCTBLN | HOST_INFO_MPR);
	val |= num;
	if (mac_pref)
		val |= HOST_INFO_MPR;
	nw64_mac(reg, val);
}

static int __set_rdc_table_num(struct niu *np,
			       int xmac_index, int bmac_index,
			       int rdc_table_num, int mac_pref)
{
	unsigned long reg;

	if (rdc_table_num & ~HOST_INFO_MACRDCTBLN)
		return -EINVAL;
	if (np->flags & NIU_FLAGS_XMAC)
		reg = XMAC_HOST_INFO(xmac_index);
	else
		reg = BMAC_HOST_INFO(bmac_index);
	__set_rdc_table_num_hw(np, reg, rdc_table_num, mac_pref);
	return 0;
}

static int niu_set_primary_mac_rdc_table(struct niu *np, int table_num,
					 int mac_pref)
{
	return __set_rdc_table_num(np, 17, 0, table_num, mac_pref);
}

static int niu_set_multicast_mac_rdc_table(struct niu *np, int table_num,
					   int mac_pref)
{
	return __set_rdc_table_num(np, 16, 8, table_num, mac_pref);
}

static int niu_set_alt_mac_rdc_table(struct niu *np, int idx,
				     int table_num, int mac_pref)
{
	if (idx >= niu_num_alt_addr(np))
		return -EINVAL;
	return __set_rdc_table_num(np, idx, idx + 1, table_num, mac_pref);
}

static u64 vlan_entry_set_parity(u64 reg_val)
{
	u64 port01_mask;
	u64 port23_mask;

	port01_mask = 0x00ff;
	port23_mask = 0xff00;

	if (hweight64(reg_val & port01_mask) & 1)
		reg_val |= ENET_VLAN_TBL_PARITY0;
	else
		reg_val &= ~ENET_VLAN_TBL_PARITY0;

	if (hweight64(reg_val & port23_mask) & 1)
		reg_val |= ENET_VLAN_TBL_PARITY1;
	else
		reg_val &= ~ENET_VLAN_TBL_PARITY1;

	return reg_val;
}

static void vlan_tbl_write(struct niu *np, unsigned long index,
			   int port, int vpr, int rdc_table)
{
	u64 reg_val = nr64(ENET_VLAN_TBL(index));

	reg_val &= ~((ENET_VLAN_TBL_VPR |
		      ENET_VLAN_TBL_VLANRDCTBLN) <<
		     ENET_VLAN_TBL_SHIFT(port));
	if (vpr)
		reg_val |= (ENET_VLAN_TBL_VPR <<
			    ENET_VLAN_TBL_SHIFT(port));
	reg_val |= (rdc_table << ENET_VLAN_TBL_SHIFT(port));

	reg_val = vlan_entry_set_parity(reg_val);

	nw64(ENET_VLAN_TBL(index), reg_val);
}

static void vlan_tbl_clear(struct niu *np)
{
	int i;

	for (i = 0; i < ENET_VLAN_TBL_NUM_ENTRIES; i++)
		nw64(ENET_VLAN_TBL(i), 0);
}

static int tcam_wait_bit(struct niu *np, u64 bit)
{
	int limit = 1000;

	while (--limit > 0) {
		if (nr64(TCAM_CTL) & bit)
			break;
		udelay(1);
	}
	if (limit < 0)
		return -ENODEV;

	return 0;
}

static int tcam_flush(struct niu *np, int index)
{
	nw64(TCAM_KEY_0, 0x00);
	nw64(TCAM_KEY_MASK_0, 0xff);
	nw64(TCAM_CTL, (TCAM_CTL_RWC_TCAM_WRITE | index));

	return tcam_wait_bit(np, TCAM_CTL_STAT);
}

#if 0
static int tcam_read(struct niu *np, int index,
		     u64 *key, u64 *mask)
{
	int err;

	nw64(TCAM_CTL, (TCAM_CTL_RWC_TCAM_READ | index));
	err = tcam_wait_bit(np, TCAM_CTL_STAT);
	if (!err) {
		key[0] = nr64(TCAM_KEY_0);
		key[1] = nr64(TCAM_KEY_1);
		key[2] = nr64(TCAM_KEY_2);
		key[3] = nr64(TCAM_KEY_3);
		mask[0] = nr64(TCAM_KEY_MASK_0);
		mask[1] = nr64(TCAM_KEY_MASK_1);
		mask[2] = nr64(TCAM_KEY_MASK_2);
		mask[3] = nr64(TCAM_KEY_MASK_3);
	}
	return err;
}
#endif

static int tcam_write(struct niu *np, int index,
		      u64 *key, u64 *mask)
{
	nw64(TCAM_KEY_0, key[0]);
	nw64(TCAM_KEY_1, key[1]);
	nw64(TCAM_KEY_2, key[2]);
	nw64(TCAM_KEY_3, key[3]);
	nw64(TCAM_KEY_MASK_0, mask[0]);
	nw64(TCAM_KEY_MASK_1, mask[1]);
	nw64(TCAM_KEY_MASK_2, mask[2]);
	nw64(TCAM_KEY_MASK_3, mask[3]);
	nw64(TCAM_CTL, (TCAM_CTL_RWC_TCAM_WRITE | index));

	return tcam_wait_bit(np, TCAM_CTL_STAT);
}

#if 0
static int tcam_assoc_read(struct niu *np, int index, u64 *data)
{
	int err;

	nw64(TCAM_CTL, (TCAM_CTL_RWC_RAM_READ | index));
	err = tcam_wait_bit(np, TCAM_CTL_STAT);
	if (!err)
		*data = nr64(TCAM_KEY_1);

	return err;
}
#endif

static int tcam_assoc_write(struct niu *np, int index, u64 assoc_data)
{
	nw64(TCAM_KEY_1, assoc_data);
	nw64(TCAM_CTL, (TCAM_CTL_RWC_RAM_WRITE | index));

	return tcam_wait_bit(np, TCAM_CTL_STAT);
}

static void tcam_enable(struct niu *np, int on)
{
	u64 val = nr64(FFLP_CFG_1);

	if (on)
		val &= ~FFLP_CFG_1_TCAM_DIS;
	else
		val |= FFLP_CFG_1_TCAM_DIS;
	nw64(FFLP_CFG_1, val);
}

static void tcam_set_lat_and_ratio(struct niu *np, u64 latency, u64 ratio)
{
	u64 val = nr64(FFLP_CFG_1);

	val &= ~(FFLP_CFG_1_FFLPINITDONE |
		 FFLP_CFG_1_CAMLAT |
		 FFLP_CFG_1_CAMRATIO);
	val |= (latency << FFLP_CFG_1_CAMLAT_SHIFT);
	val |= (ratio << FFLP_CFG_1_CAMRATIO_SHIFT);
	nw64(FFLP_CFG_1, val);

	val = nr64(FFLP_CFG_1);
	val |= FFLP_CFG_1_FFLPINITDONE;
	nw64(FFLP_CFG_1, val);
}

static int tcam_user_eth_class_enable(struct niu *np, unsigned long class,
				      int on)
{
	unsigned long reg;
	u64 val;

	if (class < CLASS_CODE_ETHERTYPE1 ||
	    class > CLASS_CODE_ETHERTYPE2)
		return -EINVAL;

	reg = L2_CLS(class - CLASS_CODE_ETHERTYPE1);
	val = nr64(reg);
	if (on)
		val |= L2_CLS_VLD;
	else
		val &= ~L2_CLS_VLD;
	nw64(reg, val);

	return 0;
}

#if 0
static int tcam_user_eth_class_set(struct niu *np, unsigned long class,
				   u64 ether_type)
{
	unsigned long reg;
	u64 val;

	if (class < CLASS_CODE_ETHERTYPE1 ||
	    class > CLASS_CODE_ETHERTYPE2 ||
	    (ether_type & ~(u64)0xffff) != 0)
		return -EINVAL;

	reg = L2_CLS(class - CLASS_CODE_ETHERTYPE1);
	val = nr64(reg);
	val &= ~L2_CLS_ETYPE;
	val |= (ether_type << L2_CLS_ETYPE_SHIFT);
	nw64(reg, val);

	return 0;
}
#endif

static int tcam_user_ip_class_enable(struct niu *np, unsigned long class,
				     int on)
{
	unsigned long reg;
	u64 val;

	if (class < CLASS_CODE_USER_PROG1 ||
	    class > CLASS_CODE_USER_PROG4)
		return -EINVAL;

	reg = L3_CLS(class - CLASS_CODE_USER_PROG1);
	val = nr64(reg);
	if (on)
		val |= L3_CLS_VALID;
	else
		val &= ~L3_CLS_VALID;
	nw64(reg, val);

	return 0;
}

static int tcam_user_ip_class_set(struct niu *np, unsigned long class,
				  int ipv6, u64 protocol_id,
				  u64 tos_mask, u64 tos_val)
{
	unsigned long reg;
	u64 val;

	if (class < CLASS_CODE_USER_PROG1 ||
	    class > CLASS_CODE_USER_PROG4 ||
	    (protocol_id & ~(u64)0xff) != 0 ||
	    (tos_mask & ~(u64)0xff) != 0 ||
	    (tos_val & ~(u64)0xff) != 0)
		return -EINVAL;

	reg = L3_CLS(class - CLASS_CODE_USER_PROG1);
	val = nr64(reg);
	val &= ~(L3_CLS_IPVER | L3_CLS_PID |
		 L3_CLS_TOSMASK | L3_CLS_TOS);
	if (ipv6)
		val |= L3_CLS_IPVER;
	val |= (protocol_id << L3_CLS_PID_SHIFT);
	val |= (tos_mask << L3_CLS_TOSMASK_SHIFT);
	val |= (tos_val << L3_CLS_TOS_SHIFT);
	nw64(reg, val);

	return 0;
}

static int tcam_early_init(struct niu *np)
{
	unsigned long i;
	int err;

	tcam_enable(np, 0);
	tcam_set_lat_and_ratio(np,
			       DEFAULT_TCAM_LATENCY,
			       DEFAULT_TCAM_ACCESS_RATIO);
	for (i = CLASS_CODE_ETHERTYPE1; i <= CLASS_CODE_ETHERTYPE2; i++) {
		err = tcam_user_eth_class_enable(np, i, 0);
		if (err)
			return err;
	}
	for (i = CLASS_CODE_USER_PROG1; i <= CLASS_CODE_USER_PROG4; i++) {
		err = tcam_user_ip_class_enable(np, i, 0);
		if (err)
			return err;
	}

	return 0;
}

static int tcam_flush_all(struct niu *np)
{
	unsigned long i;

	for (i = 0; i < np->parent->tcam_num_entries; i++) {
		int err = tcam_flush(np, i);
		if (err)
			return err;
	}
	return 0;
}

static u64 hash_addr_regval(unsigned long index, unsigned long num_entries)
{
	return ((u64)index | (num_entries == 1 ?
			      HASH_TBL_ADDR_AUTOINC : 0));
}

#if 0
static int hash_read(struct niu *np, unsigned long partition,
		     unsigned long index, unsigned long num_entries,
		     u64 *data)
{
	u64 val = hash_addr_regval(index, num_entries);
	unsigned long i;

	if (partition >= FCRAM_NUM_PARTITIONS ||
	    index + num_entries > FCRAM_SIZE)
		return -EINVAL;

	nw64(HASH_TBL_ADDR(partition), val);
	for (i = 0; i < num_entries; i++)
		data[i] = nr64(HASH_TBL_DATA(partition));

	return 0;
}
#endif

static int hash_write(struct niu *np, unsigned long partition,
		      unsigned long index, unsigned long num_entries,
		      u64 *data)
{
	u64 val = hash_addr_regval(index, num_entries);
	unsigned long i;

	if (partition >= FCRAM_NUM_PARTITIONS ||
	    index + (num_entries * 8) > FCRAM_SIZE)
		return -EINVAL;

	nw64(HASH_TBL_ADDR(partition), val);
	for (i = 0; i < num_entries; i++)
		nw64(HASH_TBL_DATA(partition), data[i]);

	return 0;
}

static void fflp_reset(struct niu *np)
{
	u64 val;

	nw64(FFLP_CFG_1, FFLP_CFG_1_PIO_FIO_RST);
	udelay(10);
	nw64(FFLP_CFG_1, 0);

	val = FFLP_CFG_1_FCRAMOUTDR_NORMAL | FFLP_CFG_1_FFLPINITDONE;
	nw64(FFLP_CFG_1, val);
}

static void fflp_set_timings(struct niu *np)
{
	u64 val = nr64(FFLP_CFG_1);

	val &= ~FFLP_CFG_1_FFLPINITDONE;
	val |= (DEFAULT_FCRAMRATIO << FFLP_CFG_1_FCRAMRATIO_SHIFT);
	nw64(FFLP_CFG_1, val);

	val = nr64(FFLP_CFG_1);
	val |= FFLP_CFG_1_FFLPINITDONE;
	nw64(FFLP_CFG_1, val);

	val = nr64(FCRAM_REF_TMR);
	val &= ~(FCRAM_REF_TMR_MAX | FCRAM_REF_TMR_MIN);
	val |= (DEFAULT_FCRAM_REFRESH_MAX << FCRAM_REF_TMR_MAX_SHIFT);
	val |= (DEFAULT_FCRAM_REFRESH_MIN << FCRAM_REF_TMR_MIN_SHIFT);
	nw64(FCRAM_REF_TMR, val);
}

static int fflp_set_partition(struct niu *np, u64 partition,
			      u64 mask, u64 base, int enable)
{
	unsigned long reg;
	u64 val;

	if (partition >= FCRAM_NUM_PARTITIONS ||
	    (mask & ~(u64)0x1f) != 0 ||
	    (base & ~(u64)0x1f) != 0)
		return -EINVAL;

	reg = FLW_PRT_SEL(partition);

	val = nr64(reg);
	val &= ~(FLW_PRT_SEL_EXT | FLW_PRT_SEL_MASK | FLW_PRT_SEL_BASE);
	val |= (mask << FLW_PRT_SEL_MASK_SHIFT);
	val |= (base << FLW_PRT_SEL_BASE_SHIFT);
	if (enable)
		val |= FLW_PRT_SEL_EXT;
	nw64(reg, val);

	return 0;
}

static int fflp_disable_all_partitions(struct niu *np)
{
	unsigned long i;

	for (i = 0; i < FCRAM_NUM_PARTITIONS; i++) {
		int err = fflp_set_partition(np, 0, 0, 0, 0);
		if (err)
			return err;
	}
	return 0;
}

static void fflp_llcsnap_enable(struct niu *np, int on)
{
	u64 val = nr64(FFLP_CFG_1);

	if (on)
		val |= FFLP_CFG_1_LLCSNAP;
	else
		val &= ~FFLP_CFG_1_LLCSNAP;
	nw64(FFLP_CFG_1, val);
}

static void fflp_errors_enable(struct niu *np, int on)
{
	u64 val = nr64(FFLP_CFG_1);

	if (on)
		val &= ~FFLP_CFG_1_ERRORDIS;
	else
		val |= FFLP_CFG_1_ERRORDIS;
	nw64(FFLP_CFG_1, val);
}

static int fflp_hash_clear(struct niu *np)
{
	struct fcram_hash_ipv4 ent;
	unsigned long i;

	/* IPV4 hash entry with valid bit clear, rest is don't care.  */
	memset(&ent, 0, sizeof(ent));
	ent.header = HASH_HEADER_EXT;

	for (i = 0; i < FCRAM_SIZE; i += sizeof(ent)) {
		int err = hash_write(np, 0, i, 1, (u64 *) &ent);
		if (err)
			return err;
	}
	return 0;
}

static int fflp_early_init(struct niu *np)
{
	struct niu_parent *parent;
	unsigned long flags;
	int err;

	niu_lock_parent(np, flags);

	parent = np->parent;
	err = 0;
	if (!(parent->flags & PARENT_FLGS_CLS_HWINIT)) {
		niudbg(PROBE, "fflp_early_init: Initting hw on port %u\n",
		       np->port);
		if (np->parent->plat_type != PLAT_TYPE_NIU) {
			fflp_reset(np);
			fflp_set_timings(np);
			err = fflp_disable_all_partitions(np);
			if (err) {
				niudbg(PROBE, "fflp_disable_all_partitions "
				       "failed, err=%d\n", err);
				goto out;
			}
		}

		err = tcam_early_init(np);
		if (err) {
			niudbg(PROBE, "tcam_early_init failed, err=%d\n",
			       err);
			goto out;
		}
		fflp_llcsnap_enable(np, 1);
		fflp_errors_enable(np, 0);
		nw64(H1POLY, 0);
		nw64(H2POLY, 0);

		err = tcam_flush_all(np);
		if (err) {
			niudbg(PROBE, "tcam_flush_all failed, err=%d\n",
			       err);
			goto out;
		}
		if (np->parent->plat_type != PLAT_TYPE_NIU) {
			err = fflp_hash_clear(np);
			if (err) {
				niudbg(PROBE, "fflp_hash_clear failed, "
				       "err=%d\n", err);
				goto out;
			}
		}

		vlan_tbl_clear(np);

		niudbg(PROBE, "fflp_early_init: Success\n");
		parent->flags |= PARENT_FLGS_CLS_HWINIT;
	}
out:
	niu_unlock_parent(np, flags);
	return err;
}

static int niu_set_flow_key(struct niu *np, unsigned long class_code, u64 key)
{
	if (class_code < CLASS_CODE_USER_PROG1 ||
	    class_code > CLASS_CODE_SCTP_IPV6)
		return -EINVAL;

	nw64(FLOW_KEY(class_code - CLASS_CODE_USER_PROG1), key);
	return 0;
}

static int niu_set_tcam_key(struct niu *np, unsigned long class_code, u64 key)
{
	if (class_code < CLASS_CODE_USER_PROG1 ||
	    class_code > CLASS_CODE_SCTP_IPV6)
		return -EINVAL;

	nw64(TCAM_KEY(class_code - CLASS_CODE_USER_PROG1), key);
	return 0;
}

/* Entries for the ports are interleaved in the TCAM */
static u16 tcam_get_index(struct niu *np, u16 idx)
{
	/* One entry reserved for IP fragment rule */
	if (idx >= (np->clas.tcam_sz - 1))
		idx = 0;
	return (np->clas.tcam_top + ((idx+1) * np->parent->num_ports));
}

static u16 tcam_get_size(struct niu *np)
{
	/* One entry reserved for IP fragment rule */
	return np->clas.tcam_sz - 1;
}

static u16 tcam_get_valid_entry_cnt(struct niu *np)
{
	/* One entry reserved for IP fragment rule */
	return np->clas.tcam_valid_entries - 1;
}

static void niu_rx_skb_append(struct sk_buff *skb, struct page *page,
			      u32 offset, u32 size)
{
	int i = skb_shinfo(skb)->nr_frags;
	skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

	frag->page = page;
	frag->page_offset = offset;
	frag->size = size;

	skb->len += size;
	skb->data_len += size;
	skb->truesize += size;

	skb_shinfo(skb)->nr_frags = i + 1;
}

static unsigned int niu_hash_rxaddr(struct rx_ring_info *rp, u64 a)
{
	a >>= PAGE_SHIFT;
	a ^= (a >> ilog2(MAX_RBR_RING_SIZE));

	return (a & (MAX_RBR_RING_SIZE - 1));
}

static struct page *niu_find_rxpage(struct rx_ring_info *rp, u64 addr,
				    struct page ***link)
{
	unsigned int h = niu_hash_rxaddr(rp, addr);
	struct page *p, **pp;

	addr &= PAGE_MASK;
	pp = &rp->rxhash[h];
	for (; (p = *pp) != NULL; pp = (struct page **) &p->mapping) {
		if (p->index == addr) {
			*link = pp;
			break;
		}
	}

	return p;
}

static void niu_hash_page(struct rx_ring_info *rp, struct page *page, u64 base)
{
	unsigned int h = niu_hash_rxaddr(rp, base);

	page->index = base;
	page->mapping = (struct address_space *) rp->rxhash[h];
	rp->rxhash[h] = page;
}

static int niu_rbr_add_page(struct niu *np, struct rx_ring_info *rp,
			    gfp_t mask, int start_index)
{
	struct page *page;
	u64 addr;
	int i;

	page = alloc_page(mask);
	if (!page)
		return -ENOMEM;

	addr = np->ops->map_page(np->device, page, 0,
				 PAGE_SIZE, DMA_FROM_DEVICE);

	niu_hash_page(rp, page, addr);
	if (rp->rbr_blocks_per_page > 1)
		atomic_add(rp->rbr_blocks_per_page - 1,
			   &compound_head(page)->_count);

	for (i = 0; i < rp->rbr_blocks_per_page; i++) {
		__le32 *rbr = &rp->rbr[start_index + i];

		*rbr = cpu_to_le32(addr >> RBR_DESCR_ADDR_SHIFT);
		addr += rp->rbr_block_size;
	}

	return 0;
}

static void niu_rbr_refill(struct niu *np, struct rx_ring_info *rp, gfp_t mask)
{
	int index = rp->rbr_index;

	rp->rbr_pending++;
	if ((rp->rbr_pending % rp->rbr_blocks_per_page) == 0) {
		int err = niu_rbr_add_page(np, rp, mask, index);

		if (unlikely(err)) {
			rp->rbr_pending--;
			return;
		}

		rp->rbr_index += rp->rbr_blocks_per_page;
		BUG_ON(rp->rbr_index > rp->rbr_table_size);
		if (rp->rbr_index == rp->rbr_table_size)
			rp->rbr_index = 0;

		if (rp->rbr_pending >= rp->rbr_kick_thresh) {
			nw64(RBR_KICK(rp->rx_channel), rp->rbr_pending);
			rp->rbr_pending = 0;
		}
	}
}

static int niu_rx_pkt_ignore(struct niu *np, struct rx_ring_info *rp)
{
	unsigned int index = rp->rcr_index;
	int num_rcr = 0;

	rp->rx_dropped++;
	while (1) {
		struct page *page, **link;
		u64 addr, val;
		u32 rcr_size;

		num_rcr++;

		val = le64_to_cpup(&rp->rcr[index]);
		addr = (val & RCR_ENTRY_PKT_BUF_ADDR) <<
			RCR_ENTRY_PKT_BUF_ADDR_SHIFT;
		page = niu_find_rxpage(rp, addr, &link);

		rcr_size = rp->rbr_sizes[(val & RCR_ENTRY_PKTBUFSZ) >>
					 RCR_ENTRY_PKTBUFSZ_SHIFT];
		if ((page->index + PAGE_SIZE) - rcr_size == addr) {
			*link = (struct page *) page->mapping;
			np->ops->unmap_page(np->device, page->index,
					    PAGE_SIZE, DMA_FROM_DEVICE);
			page->index = 0;
			page->mapping = NULL;
			__free_page(page);
			rp->rbr_refill_pending++;
		}

		index = NEXT_RCR(rp, index);
		if (!(val & RCR_ENTRY_MULTI))
			break;

	}
	rp->rcr_index = index;

	return num_rcr;
}

static int niu_process_rx_pkt(struct napi_struct *napi, struct niu *np,
			      struct rx_ring_info *rp)
{
	unsigned int index = rp->rcr_index;
	struct sk_buff *skb;
	int len, num_rcr;

	skb = netdev_alloc_skb(np->dev, RX_SKB_ALLOC_SIZE);
	if (unlikely(!skb))
		return niu_rx_pkt_ignore(np, rp);

	num_rcr = 0;
	while (1) {
		struct page *page, **link;
		u32 rcr_size, append_size;
		u64 addr, val, off;

		num_rcr++;

		val = le64_to_cpup(&rp->rcr[index]);

		len = (val & RCR_ENTRY_L2_LEN) >>
			RCR_ENTRY_L2_LEN_SHIFT;
		len -= ETH_FCS_LEN;

		addr = (val & RCR_ENTRY_PKT_BUF_ADDR) <<
			RCR_ENTRY_PKT_BUF_ADDR_SHIFT;
		page = niu_find_rxpage(rp, addr, &link);

		rcr_size = rp->rbr_sizes[(val & RCR_ENTRY_PKTBUFSZ) >>
					 RCR_ENTRY_PKTBUFSZ_SHIFT];

		off = addr & ~PAGE_MASK;
		append_size = rcr_size;
		if (num_rcr == 1) {
			int ptype;

			off += 2;
			append_size -= 2;

			ptype = (val >> RCR_ENTRY_PKT_TYPE_SHIFT);
			if ((ptype == RCR_PKT_TYPE_TCP ||
			     ptype == RCR_PKT_TYPE_UDP) &&
			    !(val & (RCR_ENTRY_NOPORT |
				     RCR_ENTRY_ERROR)))
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			else
				skb->ip_summed = CHECKSUM_NONE;
		}
		if (!(val & RCR_ENTRY_MULTI))
			append_size = len - skb->len;

		niu_rx_skb_append(skb, page, off, append_size);
		if ((page->index + rp->rbr_block_size) - rcr_size == addr) {
			*link = (struct page *) page->mapping;
			np->ops->unmap_page(np->device, page->index,
					    PAGE_SIZE, DMA_FROM_DEVICE);
			page->index = 0;
			page->mapping = NULL;
			rp->rbr_refill_pending++;
		} else
			get_page(page);

		index = NEXT_RCR(rp, index);
		if (!(val & RCR_ENTRY_MULTI))
			break;

	}
	rp->rcr_index = index;

	skb_reserve(skb, NET_IP_ALIGN);
	__pskb_pull_tail(skb, min(len, NIU_RXPULL_MAX));

	rp->rx_packets++;
	rp->rx_bytes += skb->len;

	skb->protocol = eth_type_trans(skb, np->dev);
	skb_record_rx_queue(skb, rp->rx_channel);
	napi_gro_receive(napi, skb);

	return num_rcr;
}

static int niu_rbr_fill(struct niu *np, struct rx_ring_info *rp, gfp_t mask)
{
	int blocks_per_page = rp->rbr_blocks_per_page;
	int err, index = rp->rbr_index;

	err = 0;
	while (index < (rp->rbr_table_size - blocks_per_page)) {
		err = niu_rbr_add_page(np, rp, mask, index);
		if (err)
			break;

		index += blocks_per_page;
	}

	rp->rbr_index = index;
	return err;
}

static void niu_rbr_free(struct niu *np, struct rx_ring_info *rp)
{
	int i;

	for (i = 0; i < MAX_RBR_RING_SIZE; i++) {
		struct page *page;

		page = rp->rxhash[i];
		while (page) {
			struct page *next = (struct page *) page->mapping;
			u64 base = page->index;

			np->ops->unmap_page(np->device, base, PAGE_SIZE,
					    DMA_FROM_DEVICE);
			page->index = 0;
			page->mapping = NULL;

			__free_page(page);

			page = next;
		}
	}

	for (i = 0; i < rp->rbr_table_size; i++)
		rp->rbr[i] = cpu_to_le32(0);
	rp->rbr_index = 0;
}

static int release_tx_packet(struct niu *np, struct tx_ring_info *rp, int idx)
{
	struct tx_buff_info *tb = &rp->tx_buffs[idx];
	struct sk_buff *skb = tb->skb;
	struct tx_pkt_hdr *tp;
	u64 tx_flags;
	int i, len;

	tp = (struct tx_pkt_hdr *) skb->data;
	tx_flags = le64_to_cpup(&tp->flags);

	rp->tx_packets++;
	rp->tx_bytes += (((tx_flags & TXHDR_LEN) >> TXHDR_LEN_SHIFT) -
			 ((tx_flags & TXHDR_PAD) / 2));

	len = skb_headlen(skb);
	np->ops->unmap_single(np->device, tb->mapping,
			      len, DMA_TO_DEVICE);

	if (le64_to_cpu(rp->descr[idx]) & TX_DESC_MARK)
		rp->mark_pending--;

	tb->skb = NULL;
	do {
		idx = NEXT_TX(rp, idx);
		len -= MAX_TX_DESC_LEN;
	} while (len > 0);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		tb = &rp->tx_buffs[idx];
		BUG_ON(tb->skb != NULL);
		np->ops->unmap_page(np->device, tb->mapping,
				    skb_shinfo(skb)->frags[i].size,
				    DMA_TO_DEVICE);
		idx = NEXT_TX(rp, idx);
	}

	dev_kfree_skb(skb);

	return idx;
}

#define NIU_TX_WAKEUP_THRESH(rp)		((rp)->pending / 4)

static void niu_tx_work(struct niu *np, struct tx_ring_info *rp)
{
	struct netdev_queue *txq;
	u16 pkt_cnt, tmp;
	int cons, index;
	u64 cs;

	index = (rp - np->tx_rings);
	txq = netdev_get_tx_queue(np->dev, index);

	cs = rp->tx_cs;
	if (unlikely(!(cs & (TX_CS_MK | TX_CS_MMK))))
		goto out;

	tmp = pkt_cnt = (cs & TX_CS_PKT_CNT) >> TX_CS_PKT_CNT_SHIFT;
	pkt_cnt = (pkt_cnt - rp->last_pkt_cnt) &
		(TX_CS_PKT_CNT >> TX_CS_PKT_CNT_SHIFT);

	rp->last_pkt_cnt = tmp;

	cons = rp->cons;

	niudbg(TX_DONE, "%s: niu_tx_work() pkt_cnt[%u] cons[%d]\n",
	       np->dev->name, pkt_cnt, cons);

	while (pkt_cnt--)
		cons = release_tx_packet(np, rp, cons);

	rp->cons = cons;
	smp_mb();

out:
	if (unlikely(netif_tx_queue_stopped(txq) &&
		     (niu_tx_avail(rp) > NIU_TX_WAKEUP_THRESH(rp)))) {
		__netif_tx_lock(txq, smp_processor_id());
		if (netif_tx_queue_stopped(txq) &&
		    (niu_tx_avail(rp) > NIU_TX_WAKEUP_THRESH(rp)))
			netif_tx_wake_queue(txq);
		__netif_tx_unlock(txq);
	}
}

static inline void niu_sync_rx_discard_stats(struct niu *np,
					     struct rx_ring_info *rp,
					     const int limit)
{
	/* This elaborate scheme is needed for reading the RX discard
	 * counters, as they are only 16-bit and can overflow quickly,
	 * and because the overflow indication bit is not usable as
	 * the counter value does not wrap, but remains at max value
	 * 0xFFFF.
	 *
	 * In theory and in practice counters can be lost in between
	 * reading nr64() and clearing the counter nw64().  For this
	 * reason, the number of counter clearings nw64() is
	 * limited/reduced though the limit parameter.
	 */
	int rx_channel = rp->rx_channel;
	u32 misc, wred;

	/* RXMISC (Receive Miscellaneous Discard Count), covers the
	 * following discard events: IPP (Input Port Process),
	 * FFLP/TCAM, Full RCR (Receive Completion Ring) RBR (Receive
	 * Block Ring) prefetch buffer is empty.
	 */
	misc = nr64(RXMISC(rx_channel));
	if (unlikely((misc & RXMISC_COUNT) > limit)) {
		nw64(RXMISC(rx_channel), 0);
		rp->rx_errors += misc & RXMISC_COUNT;

		if (unlikely(misc & RXMISC_OFLOW))
			dev_err(np->device, "rx-%d: Counter overflow "
				"RXMISC discard\n", rx_channel);

		niudbg(RX_ERR, "%s-rx-%d: MISC drop=%u over=%u\n",
		       np->dev->name, rx_channel, misc, misc-limit);
	}

	/* WRED (Weighted Random Early Discard) by hardware */
	wred = nr64(RED_DIS_CNT(rx_channel));
	if (unlikely((wred & RED_DIS_CNT_COUNT) > limit)) {
		nw64(RED_DIS_CNT(rx_channel), 0);
		rp->rx_dropped += wred & RED_DIS_CNT_COUNT;

		if (unlikely(wred & RED_DIS_CNT_OFLOW))
			dev_err(np->device, "rx-%d: Counter overflow "
				"WRED discard\n", rx_channel);

		niudbg(RX_ERR, "%s-rx-%d: WRED drop=%u over=%u\n",
		       np->dev->name, rx_channel, wred, wred-limit);
	}
}

static int niu_rx_work(struct napi_struct *napi, struct niu *np,
		       struct rx_ring_info *rp, int budget)
{
	int qlen, rcr_done = 0, work_done = 0;
	struct rxdma_mailbox *mbox = rp->mbox;
	u64 stat;

#if 1
	stat = nr64(RX_DMA_CTL_STAT(rp->rx_channel));
	qlen = nr64(RCRSTAT_A(rp->rx_channel)) & RCRSTAT_A_QLEN;
#else
	stat = le64_to_cpup(&mbox->rx_dma_ctl_stat);
	qlen = (le64_to_cpup(&mbox->rcrstat_a) & RCRSTAT_A_QLEN);
#endif
	mbox->rx_dma_ctl_stat = 0;
	mbox->rcrstat_a = 0;

	niudbg(RX_STATUS, "%s: niu_rx_work(chan[%d]), stat[%llx] qlen=%d\n",
	       np->dev->name, rp->rx_channel, (unsigned long long) stat, qlen);

	rcr_done = work_done = 0;
	qlen = min(qlen, budget);
	while (work_done < qlen) {
		rcr_done += niu_process_rx_pkt(napi, np, rp);
		work_done++;
	}

	if (rp->rbr_refill_pending >= rp->rbr_kick_thresh) {
		unsigned int i;

		for (i = 0; i < rp->rbr_refill_pending; i++)
			niu_rbr_refill(np, rp, GFP_ATOMIC);
		rp->rbr_refill_pending = 0;
	}

	stat = (RX_DMA_CTL_STAT_MEX |
		((u64)work_done << RX_DMA_CTL_STAT_PKTREAD_SHIFT) |
		((u64)rcr_done << RX_DMA_CTL_STAT_PTRREAD_SHIFT));

	nw64(RX_DMA_CTL_STAT(rp->rx_channel), stat);

	/* Only sync discards stats when qlen indicate potential for drops */
	if (qlen > 10)
		niu_sync_rx_discard_stats(np, rp, 0x7FFF);

	return work_done;
}

static int niu_poll_core(struct niu *np, struct niu_ldg *lp, int budget)
{
	u64 v0 = lp->v0;
	u32 tx_vec = (v0 >> 32);
	u32 rx_vec = (v0 & 0xffffffff);
	int i, work_done = 0;

	niudbg(INTR, "%s: niu_poll_core() v0[%016llx]\n",
	       np->dev->name, (unsigned long long) v0);

	for (i = 0; i < np->num_tx_rings; i++) {
		struct tx_ring_info *rp = &np->tx_rings[i];
		if (tx_vec & (1 << rp->tx_channel))
			niu_tx_work(np, rp);
		nw64(LD_IM0(LDN_TXDMA(rp->tx_channel)), 0);
	}

	for (i = 0; i < np->num_rx_rings; i++) {
		struct rx_ring_info *rp = &np->rx_rings[i];

		if (rx_vec & (1 << rp->rx_channel)) {
			int this_work_done;

			this_work_done = niu_rx_work(&lp->napi, np, rp,
						     budget);

			budget -= this_work_done;
			work_done += this_work_done;
		}
		nw64(LD_IM0(LDN_RXDMA(rp->rx_channel)), 0);
	}

	return work_done;
}

static int niu_poll(struct napi_struct *napi, int budget)
{
	struct niu_ldg *lp = container_of(napi, struct niu_ldg, napi);
	struct niu *np = lp->np;
	int work_done;

	work_done = niu_poll_core(np, lp, budget);

	if (work_done < budget) {
		napi_complete(napi);
		niu_ldg_rearm(np, lp, 1);
	}
	return work_done;
}

static void niu_log_rxchan_errors(struct niu *np, struct rx_ring_info *rp,
				  u64 stat)
{
	dev_err(np->device, PFX "%s: RX channel %u errors ( ",
		np->dev->name, rp->rx_channel);

	if (stat & RX_DMA_CTL_STAT_RBR_TMOUT)
		printk("RBR_TMOUT ");
	if (stat & RX_DMA_CTL_STAT_RSP_CNT_ERR)
		printk("RSP_CNT ");
	if (stat & RX_DMA_CTL_STAT_BYTE_EN_BUS)
		printk("BYTE_EN_BUS ");
	if (stat & RX_DMA_CTL_STAT_RSP_DAT_ERR)
		printk("RSP_DAT ");
	if (stat & RX_DMA_CTL_STAT_RCR_ACK_ERR)
		printk("RCR_ACK ");
	if (stat & RX_DMA_CTL_STAT_RCR_SHA_PAR)
		printk("RCR_SHA_PAR ");
	if (stat & RX_DMA_CTL_STAT_RBR_PRE_PAR)
		printk("RBR_PRE_PAR ");
	if (stat & RX_DMA_CTL_STAT_CONFIG_ERR)
		printk("CONFIG ");
	if (stat & RX_DMA_CTL_STAT_RCRINCON)
		printk("RCRINCON ");
	if (stat & RX_DMA_CTL_STAT_RCRFULL)
		printk("RCRFULL ");
	if (stat & RX_DMA_CTL_STAT_RBRFULL)
		printk("RBRFULL ");
	if (stat & RX_DMA_CTL_STAT_RBRLOGPAGE)
		printk("RBRLOGPAGE ");
	if (stat & RX_DMA_CTL_STAT_CFIGLOGPAGE)
		printk("CFIGLOGPAGE ");
	if (stat & RX_DMA_CTL_STAT_DC_FIFO_ERR)
		printk("DC_FIDO ");

	printk(")\n");
}

static int niu_rx_error(struct niu *np, struct rx_ring_info *rp)
{
	u64 stat = nr64(RX_DMA_CTL_STAT(rp->rx_channel));
	int err = 0;


	if (stat & (RX_DMA_CTL_STAT_CHAN_FATAL |
		    RX_DMA_CTL_STAT_PORT_FATAL))
		err = -EINVAL;

	if (err) {
		dev_err(np->device, PFX "%s: RX channel %u error, stat[%llx]\n",
			np->dev->name, rp->rx_channel,
			(unsigned long long) stat);

		niu_log_rxchan_errors(np, rp, stat);
	}

	nw64(RX_DMA_CTL_STAT(rp->rx_channel),
	     stat & RX_DMA_CTL_WRITE_CLEAR_ERRS);

	return err;
}

static void niu_log_txchan_errors(struct niu *np, struct tx_ring_info *rp,
				  u64 cs)
{
	dev_err(np->device, PFX "%s: TX channel %u errors ( ",
		np->dev->name, rp->tx_channel);

	if (cs & TX_CS_MBOX_ERR)
		printk("MBOX ");
	if (cs & TX_CS_PKT_SIZE_ERR)
		printk("PKT_SIZE ");
	if (cs & TX_CS_TX_RING_OFLOW)
		printk("TX_RING_OFLOW ");
	if (cs & TX_CS_PREF_BUF_PAR_ERR)
		printk("PREF_BUF_PAR ");
	if (cs & TX_CS_NACK_PREF)
		printk("NACK_PREF ");
	if (cs & TX_CS_NACK_PKT_RD)
		printk("NACK_PKT_RD ");
	if (cs & TX_CS_CONF_PART_ERR)
		printk("CONF_PART ");
	if (cs & TX_CS_PKT_PRT_ERR)
		printk("PKT_PTR ");

	printk(")\n");
}

static int niu_tx_error(struct niu *np, struct tx_ring_info *rp)
{
	u64 cs, logh, logl;

	cs = nr64(TX_CS(rp->tx_channel));
	logh = nr64(TX_RNG_ERR_LOGH(rp->tx_channel));
	logl = nr64(TX_RNG_ERR_LOGL(rp->tx_channel));

	dev_err(np->device, PFX "%s: TX channel %u error, "
		"cs[%llx] logh[%llx] logl[%llx]\n",
		np->dev->name, rp->tx_channel,
		(unsigned long long) cs,
		(unsigned long long) logh,
		(unsigned long long) logl);

	niu_log_txchan_errors(np, rp, cs);

	return -ENODEV;
}

static int niu_mif_interrupt(struct niu *np)
{
	u64 mif_status = nr64(MIF_STATUS);
	int phy_mdint = 0;

	if (np->flags & NIU_FLAGS_XMAC) {
		u64 xrxmac_stat = nr64_mac(XRXMAC_STATUS);

		if (xrxmac_stat & XRXMAC_STATUS_PHY_MDINT)
			phy_mdint = 1;
	}

	dev_err(np->device, PFX "%s: MIF interrupt, "
		"stat[%llx] phy_mdint(%d)\n",
		np->dev->name, (unsigned long long) mif_status, phy_mdint);

	return -ENODEV;
}

static void niu_xmac_interrupt(struct niu *np)
{
	struct niu_xmac_stats *mp = &np->mac_stats.xmac;
	u64 val;

	val = nr64_mac(XTXMAC_STATUS);
	if (val & XTXMAC_STATUS_FRAME_CNT_EXP)
		mp->tx_frames += TXMAC_FRM_CNT_COUNT;
	if (val & XTXMAC_STATUS_BYTE_CNT_EXP)
		mp->tx_bytes += TXMAC_BYTE_CNT_COUNT;
	if (val & XTXMAC_STATUS_TXFIFO_XFR_ERR)
		mp->tx_fifo_errors++;
	if (val & XTXMAC_STATUS_TXMAC_OFLOW)
		mp->tx_overflow_errors++;
	if (val & XTXMAC_STATUS_MAX_PSIZE_ERR)
		mp->tx_max_pkt_size_errors++;
	if (val & XTXMAC_STATUS_TXMAC_UFLOW)
		mp->tx_underflow_errors++;

	val = nr64_mac(XRXMAC_STATUS);
	if (val & XRXMAC_STATUS_LCL_FLT_STATUS)
		mp->rx_local_faults++;
	if (val & XRXMAC_STATUS_RFLT_DET)
		mp->rx_remote_faults++;
	if (val & XRXMAC_STATUS_LFLT_CNT_EXP)
		mp->rx_link_faults += LINK_FAULT_CNT_COUNT;
	if (val & XRXMAC_STATUS_ALIGNERR_CNT_EXP)
		mp->rx_align_errors += RXMAC_ALIGN_ERR_CNT_COUNT;
	if (val & XRXMAC_STATUS_RXFRAG_CNT_EXP)
		mp->rx_frags += RXMAC_FRAG_CNT_COUNT;
	if (val & XRXMAC_STATUS_RXMULTF_CNT_EXP)
		mp->rx_mcasts += RXMAC_MC_FRM_CNT_COUNT;
	if (val & XRXMAC_STATUS_RXBCAST_CNT_EXP)
		mp->rx_bcasts += RXMAC_BC_FRM_CNT_COUNT;
	if (val & XRXMAC_STATUS_RXBCAST_CNT_EXP)
		mp->rx_bcasts += RXMAC_BC_FRM_CNT_COUNT;
	if (val & XRXMAC_STATUS_RXHIST1_CNT_EXP)
		mp->rx_hist_cnt1 += RXMAC_HIST_CNT1_COUNT;
	if (val & XRXMAC_STATUS_RXHIST2_CNT_EXP)
		mp->rx_hist_cnt2 += RXMAC_HIST_CNT2_COUNT;
	if (val & XRXMAC_STATUS_RXHIST3_CNT_EXP)
		mp->rx_hist_cnt3 += RXMAC_HIST_CNT3_COUNT;
	if (val & XRXMAC_STATUS_RXHIST4_CNT_EXP)
		mp->rx_hist_cnt4 += RXMAC_HIST_CNT4_COUNT;
	if (val & XRXMAC_STATUS_RXHIST5_CNT_EXP)
		mp->rx_hist_cnt5 += RXMAC_HIST_CNT5_COUNT;
	if (val & XRXMAC_STATUS_RXHIST6_CNT_EXP)
		mp->rx_hist_cnt6 += RXMAC_HIST_CNT6_COUNT;
	if (val & XRXMAC_STATUS_RXHIST7_CNT_EXP)
		mp->rx_hist_cnt7 += RXMAC_HIST_CNT7_COUNT;
	if (val & XRXMAC_STAT_MSK_RXOCTET_CNT_EXP)
		mp->rx_octets += RXMAC_BT_CNT_COUNT;
	if (val & XRXMAC_STATUS_CVIOLERR_CNT_EXP)
		mp->rx_code_violations += RXMAC_CD_VIO_CNT_COUNT;
	if (val & XRXMAC_STATUS_LENERR_CNT_EXP)
		mp->rx_len_errors += RXMAC_MPSZER_CNT_COUNT;
	if (val & XRXMAC_STATUS_CRCERR_CNT_EXP)
		mp->rx_crc_errors += RXMAC_CRC_ER_CNT_COUNT;
	if (val & XRXMAC_STATUS_RXUFLOW)
		mp->rx_underflows++;
	if (val & XRXMAC_STATUS_RXOFLOW)
		mp->rx_overflows++;

	val = nr64_mac(XMAC_FC_STAT);
	if (val & XMAC_FC_STAT_TX_MAC_NPAUSE)
		mp->pause_off_state++;
	if (val & XMAC_FC_STAT_TX_MAC_PAUSE)
		mp->pause_on_state++;
	if (val & XMAC_FC_STAT_RX_MAC_RPAUSE)
		mp->pause_received++;
}

static void niu_bmac_interrupt(struct niu *np)
{
	struct niu_bmac_stats *mp = &np->mac_stats.bmac;
	u64 val;

	val = nr64_mac(BTXMAC_STATUS);
	if (val & BTXMAC_STATUS_UNDERRUN)
		mp->tx_underflow_errors++;
	if (val & BTXMAC_STATUS_MAX_PKT_ERR)
		mp->tx_max_pkt_size_errors++;
	if (val & BTXMAC_STATUS_BYTE_CNT_EXP)
		mp->tx_bytes += BTXMAC_BYTE_CNT_COUNT;
	if (val & BTXMAC_STATUS_FRAME_CNT_EXP)
		mp->tx_frames += BTXMAC_FRM_CNT_COUNT;

	val = nr64_mac(BRXMAC_STATUS);
	if (val & BRXMAC_STATUS_OVERFLOW)
		mp->rx_overflows++;
	if (val & BRXMAC_STATUS_FRAME_CNT_EXP)
		mp->rx_frames += BRXMAC_FRAME_CNT_COUNT;
	if (val & BRXMAC_STATUS_ALIGN_ERR_EXP)
		mp->rx_align_errors += BRXMAC_ALIGN_ERR_CNT_COUNT;
	if (val & BRXMAC_STATUS_CRC_ERR_EXP)
		mp->rx_crc_errors += BRXMAC_ALIGN_ERR_CNT_COUNT;
	if (val & BRXMAC_STATUS_LEN_ERR_EXP)
		mp->rx_len_errors += BRXMAC_CODE_VIOL_ERR_CNT_COUNT;

	val = nr64_mac(BMAC_CTRL_STATUS);
	if (val & BMAC_CTRL_STATUS_NOPAUSE)
		mp->pause_off_state++;
	if (val & BMAC_CTRL_STATUS_PAUSE)
		mp->pause_on_state++;
	if (val & BMAC_CTRL_STATUS_PAUSE_RECV)
		mp->pause_received++;
}

static int niu_mac_interrupt(struct niu *np)
{
	if (np->flags & NIU_FLAGS_XMAC)
		niu_xmac_interrupt(np);
	else
		niu_bmac_interrupt(np);

	return 0;
}

static void niu_log_device_error(struct niu *np, u64 stat)
{
	dev_err(np->device, PFX "%s: Core device errors ( ",
		np->dev->name);

	if (stat & SYS_ERR_MASK_META2)
		printk("META2 ");
	if (stat & SYS_ERR_MASK_META1)
		printk("META1 ");
	if (stat & SYS_ERR_MASK_PEU)
		printk("PEU ");
	if (stat & SYS_ERR_MASK_TXC)
		printk("TXC ");
	if (stat & SYS_ERR_MASK_RDMC)
		printk("RDMC ");
	if (stat & SYS_ERR_MASK_TDMC)
		printk("TDMC ");
	if (stat & SYS_ERR_MASK_ZCP)
		printk("ZCP ");
	if (stat & SYS_ERR_MASK_FFLP)
		printk("FFLP ");
	if (stat & SYS_ERR_MASK_IPP)
		printk("IPP ");
	if (stat & SYS_ERR_MASK_MAC)
		printk("MAC ");
	if (stat & SYS_ERR_MASK_SMX)
		printk("SMX ");

	printk(")\n");
}

static int niu_device_error(struct niu *np)
{
	u64 stat = nr64(SYS_ERR_STAT);

	dev_err(np->device, PFX "%s: Core device error, stat[%llx]\n",
		np->dev->name, (unsigned long long) stat);

	niu_log_device_error(np, stat);

	return -ENODEV;
}

static int niu_slowpath_interrupt(struct niu *np, struct niu_ldg *lp,
			      u64 v0, u64 v1, u64 v2)
{

	int i, err = 0;

	lp->v0 = v0;
	lp->v1 = v1;
	lp->v2 = v2;

	if (v1 & 0x00000000ffffffffULL) {
		u32 rx_vec = (v1 & 0xffffffff);

		for (i = 0; i < np->num_rx_rings; i++) {
			struct rx_ring_info *rp = &np->rx_rings[i];

			if (rx_vec & (1 << rp->rx_channel)) {
				int r = niu_rx_error(np, rp);
				if (r) {
					err = r;
				} else {
					if (!v0)
						nw64(RX_DMA_CTL_STAT(rp->rx_channel),
						     RX_DMA_CTL_STAT_MEX);
				}
			}
		}
	}
	if (v1 & 0x7fffffff00000000ULL) {
		u32 tx_vec = (v1 >> 32) & 0x7fffffff;

		for (i = 0; i < np->num_tx_rings; i++) {
			struct tx_ring_info *rp = &np->tx_rings[i];

			if (tx_vec & (1 << rp->tx_channel)) {
				int r = niu_tx_error(np, rp);
				if (r)
					err = r;
			}
		}
	}
	if ((v0 | v1) & 0x8000000000000000ULL) {
		int r = niu_mif_interrupt(np);
		if (r)
			err = r;
	}
	if (v2) {
		if (v2 & 0x01ef) {
			int r = niu_mac_interrupt(np);
			if (r)
				err = r;
		}
		if (v2 & 0x0210) {
			int r = niu_device_error(np);
			if (r)
				err = r;
		}
	}

	if (err)
		niu_enable_interrupts(np, 0);

	return err;
}

static void niu_rxchan_intr(struct niu *np, struct rx_ring_info *rp,
			    int ldn)
{
	struct rxdma_mailbox *mbox = rp->mbox;
	u64 stat_write, stat = le64_to_cpup(&mbox->rx_dma_ctl_stat);

	stat_write = (RX_DMA_CTL_STAT_RCRTHRES |
		      RX_DMA_CTL_STAT_RCRTO);
	nw64(RX_DMA_CTL_STAT(rp->rx_channel), stat_write);

	niudbg(INTR, "%s: rxchan_intr stat[%llx]\n",
	       np->dev->name, (unsigned long long) stat);
}

static void niu_txchan_intr(struct niu *np, struct tx_ring_info *rp,
			    int ldn)
{
	rp->tx_cs = nr64(TX_CS(rp->tx_channel));

	niudbg(INTR, "%s: txchan_intr cs[%llx]\n",
	       np->dev->name, (unsigned long long) rp->tx_cs);
}

static void __niu_fastpath_interrupt(struct niu *np, int ldg, u64 v0)
{
	struct niu_parent *parent = np->parent;
	u32 rx_vec, tx_vec;
	int i;

	tx_vec = (v0 >> 32);
	rx_vec = (v0 & 0xffffffff);

	for (i = 0; i < np->num_rx_rings; i++) {
		struct rx_ring_info *rp = &np->rx_rings[i];
		int ldn = LDN_RXDMA(rp->rx_channel);

		if (parent->ldg_map[ldn] != ldg)
			continue;

		nw64(LD_IM0(ldn), LD_IM0_MASK);
		if (rx_vec & (1 << rp->rx_channel))
			niu_rxchan_intr(np, rp, ldn);
	}

	for (i = 0; i < np->num_tx_rings; i++) {
		struct tx_ring_info *rp = &np->tx_rings[i];
		int ldn = LDN_TXDMA(rp->tx_channel);

		if (parent->ldg_map[ldn] != ldg)
			continue;

		nw64(LD_IM0(ldn), LD_IM0_MASK);
		if (tx_vec & (1 << rp->tx_channel))
			niu_txchan_intr(np, rp, ldn);
	}
}

static void niu_schedule_napi(struct niu *np, struct niu_ldg *lp,
			      u64 v0, u64 v1, u64 v2)
{
	if (likely(napi_schedule_prep(&lp->napi))) {
		lp->v0 = v0;
		lp->v1 = v1;
		lp->v2 = v2;
		__niu_fastpath_interrupt(np, lp->ldg_num, v0);
		__napi_schedule(&lp->napi);
	}
}

static irqreturn_t niu_interrupt(int irq, void *dev_id)
{
	struct niu_ldg *lp = dev_id;
	struct niu *np = lp->np;
	int ldg = lp->ldg_num;
	unsigned long flags;
	u64 v0, v1, v2;

	if (netif_msg_intr(np))
		printk(KERN_DEBUG PFX "niu_interrupt() ldg[%p](%d) ",
		       lp, ldg);

	spin_lock_irqsave(&np->lock, flags);

	v0 = nr64(LDSV0(ldg));
	v1 = nr64(LDSV1(ldg));
	v2 = nr64(LDSV2(ldg));

	if (netif_msg_intr(np))
		printk("v0[%llx] v1[%llx] v2[%llx]\n",
		       (unsigned long long) v0,
		       (unsigned long long) v1,
		       (unsigned long long) v2);

	if (unlikely(!v0 && !v1 && !v2)) {
		spin_unlock_irqrestore(&np->lock, flags);
		return IRQ_NONE;
	}

	if (unlikely((v0 & ((u64)1 << LDN_MIF)) || v1 || v2)) {
		int err = niu_slowpath_interrupt(np, lp, v0, v1, v2);
		if (err)
			goto out;
	}
	if (likely(v0 & ~((u64)1 << LDN_MIF)))
		niu_schedule_napi(np, lp, v0, v1, v2);
	else
		niu_ldg_rearm(np, lp, 1);
out:
	spin_unlock_irqrestore(&np->lock, flags);

	return IRQ_HANDLED;
}

static void niu_free_rx_ring_info(struct niu *np, struct rx_ring_info *rp)
{
	if (rp->mbox) {
		np->ops->free_coherent(np->device,
				       sizeof(struct rxdma_mailbox),
				       rp->mbox, rp->mbox_dma);
		rp->mbox = NULL;
	}
	if (rp->rcr) {
		np->ops->free_coherent(np->device,
				       MAX_RCR_RING_SIZE * sizeof(__le64),
				       rp->rcr, rp->rcr_dma);
		rp->rcr = NULL;
		rp->rcr_table_size = 0;
		rp->rcr_index = 0;
	}
	if (rp->rbr) {
		niu_rbr_free(np, rp);

		np->ops->free_coherent(np->device,
				       MAX_RBR_RING_SIZE * sizeof(__le32),
				       rp->rbr, rp->rbr_dma);
		rp->rbr = NULL;
		rp->rbr_table_size = 0;
		rp->rbr_index = 0;
	}
	kfree(rp->rxhash);
	rp->rxhash = NULL;
}

static void niu_free_tx_ring_info(struct niu *np, struct tx_ring_info *rp)
{
	if (rp->mbox) {
		np->ops->free_coherent(np->device,
				       sizeof(struct txdma_mailbox),
				       rp->mbox, rp->mbox_dma);
		rp->mbox = NULL;
	}
	if (rp->descr) {
		int i;

		for (i = 0; i < MAX_TX_RING_SIZE; i++) {
			if (rp->tx_buffs[i].skb)
				(void) release_tx_packet(np, rp, i);
		}

		np->ops->free_coherent(np->device,
				       MAX_TX_RING_SIZE * sizeof(__le64),
				       rp->descr, rp->descr_dma);
		rp->descr = NULL;
		rp->pending = 0;
		rp->prod = 0;
		rp->cons = 0;
		rp->wrap_bit = 0;
	}
}

static void niu_free_channels(struct niu *np)
{
	int i;

	if (np->rx_rings) {
		for (i = 0; i < np->num_rx_rings; i++) {
			struct rx_ring_info *rp = &np->rx_rings[i];

			niu_free_rx_ring_info(np, rp);
		}
		kfree(np->rx_rings);
		np->rx_rings = NULL;
		np->num_rx_rings = 0;
	}

	if (np->tx_rings) {
		for (i = 0; i < np->num_tx_rings; i++) {
			struct tx_ring_info *rp = &np->tx_rings[i];

			niu_free_tx_ring_info(np, rp);
		}
		kfree(np->tx_rings);
		np->tx_rings = NULL;
		np->num_tx_rings = 0;
	}
}

static int niu_alloc_rx_ring_info(struct niu *np,
				  struct rx_ring_info *rp)
{
	BUILD_BUG_ON(sizeof(struct rxdma_mailbox) != 64);

	rp->rxhash = kzalloc(MAX_RBR_RING_SIZE * sizeof(struct page *),
			     GFP_KERNEL);
	if (!rp->rxhash)
		return -ENOMEM;

	rp->mbox = np->ops->alloc_coherent(np->device,
					   sizeof(struct rxdma_mailbox),
					   &rp->mbox_dma, GFP_KERNEL);
	if (!rp->mbox)
		return -ENOMEM;
	if ((unsigned long)rp->mbox & (64UL - 1)) {
		dev_err(np->device, PFX "%s: Coherent alloc gives misaligned "
			"RXDMA mailbox %p\n", np->dev->name, rp->mbox);
		return -EINVAL;
	}

	rp->rcr = np->ops->alloc_coherent(np->device,
					  MAX_RCR_RING_SIZE * sizeof(__le64),
					  &rp->rcr_dma, GFP_KERNEL);
	if (!rp->rcr)
		return -ENOMEM;
	if ((unsigned long)rp->rcr & (64UL - 1)) {
		dev_err(np->device, PFX "%s: Coherent alloc gives misaligned "
			"RXDMA RCR table %p\n", np->dev->name, rp->rcr);
		return -EINVAL;
	}
	rp->rcr_table_size = MAX_RCR_RING_SIZE;
	rp->rcr_index = 0;

	rp->rbr = np->ops->alloc_coherent(np->device,
					  MAX_RBR_RING_SIZE * sizeof(__le32),
					  &rp->rbr_dma, GFP_KERNEL);
	if (!rp->rbr)
		return -ENOMEM;
	if ((unsigned long)rp->rbr & (64UL - 1)) {
		dev_err(np->device, PFX "%s: Coherent alloc gives misaligned "
			"RXDMA RBR table %p\n", np->dev->name, rp->rbr);
		return -EINVAL;
	}
	rp->rbr_table_size = MAX_RBR_RING_SIZE;
	rp->rbr_index = 0;
	rp->rbr_pending = 0;

	return 0;
}

static void niu_set_max_burst(struct niu *np, struct tx_ring_info *rp)
{
	int mtu = np->dev->mtu;

	/* These values are recommended by the HW designers for fair
	 * utilization of DRR amongst the rings.
	 */
	rp->max_burst = mtu + 32;
	if (rp->max_burst > 4096)
		rp->max_burst = 4096;
}

static int niu_alloc_tx_ring_info(struct niu *np,
				  struct tx_ring_info *rp)
{
	BUILD_BUG_ON(sizeof(struct txdma_mailbox) != 64);

	rp->mbox = np->ops->alloc_coherent(np->device,
					   sizeof(struct txdma_mailbox),
					   &rp->mbox_dma, GFP_KERNEL);
	if (!rp->mbox)
		return -ENOMEM;
	if ((unsigned long)rp->mbox & (64UL - 1)) {
		dev_err(np->device, PFX "%s: Coherent alloc gives misaligned "
			"TXDMA mailbox %p\n", np->dev->name, rp->mbox);
		return -EINVAL;
	}

	rp->descr = np->ops->alloc_coherent(np->device,
					    MAX_TX_RING_SIZE * sizeof(__le64),
					    &rp->descr_dma, GFP_KERNEL);
	if (!rp->descr)
		return -ENOMEM;
	if ((unsigned long)rp->descr & (64UL - 1)) {
		dev_err(np->device, PFX "%s: Coherent alloc gives misaligned "
			"TXDMA descr table %p\n", np->dev->name, rp->descr);
		return -EINVAL;
	}

	rp->pending = MAX_TX_RING_SIZE;
	rp->prod = 0;
	rp->cons = 0;
	rp->wrap_bit = 0;

	/* XXX make these configurable... XXX */
	rp->mark_freq = rp->pending / 4;

	niu_set_max_burst(np, rp);

	return 0;
}

static void niu_size_rbr(struct niu *np, struct rx_ring_info *rp)
{
	u16 bss;

	bss = min(PAGE_SHIFT, 15);

	rp->rbr_block_size = 1 << bss;
	rp->rbr_blocks_per_page = 1 << (PAGE_SHIFT-bss);

	rp->rbr_sizes[0] = 256;
	rp->rbr_sizes[1] = 1024;
	if (np->dev->mtu > ETH_DATA_LEN) {
		switch (PAGE_SIZE) {
		case 4 * 1024:
			rp->rbr_sizes[2] = 4096;
			break;

		default:
			rp->rbr_sizes[2] = 8192;
			break;
		}
	} else {
		rp->rbr_sizes[2] = 2048;
	}
	rp->rbr_sizes[3] = rp->rbr_block_size;
}

static int niu_alloc_channels(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	int first_rx_channel, first_tx_channel;
	int i, port, err;

	port = np->port;
	first_rx_channel = first_tx_channel = 0;
	for (i = 0; i < port; i++) {
		first_rx_channel += parent->rxchan_per_port[i];
		first_tx_channel += parent->txchan_per_port[i];
	}

	np->num_rx_rings = parent->rxchan_per_port[port];
	np->num_tx_rings = parent->txchan_per_port[port];

	np->dev->real_num_tx_queues = np->num_tx_rings;

	np->rx_rings = kzalloc(np->num_rx_rings * sizeof(struct rx_ring_info),
			       GFP_KERNEL);
	err = -ENOMEM;
	if (!np->rx_rings)
		goto out_err;

	for (i = 0; i < np->num_rx_rings; i++) {
		struct rx_ring_info *rp = &np->rx_rings[i];

		rp->np = np;
		rp->rx_channel = first_rx_channel + i;

		err = niu_alloc_rx_ring_info(np, rp);
		if (err)
			goto out_err;

		niu_size_rbr(np, rp);

		/* XXX better defaults, configurable, etc... XXX */
		rp->nonsyn_window = 64;
		rp->nonsyn_threshold = rp->rcr_table_size - 64;
		rp->syn_window = 64;
		rp->syn_threshold = rp->rcr_table_size - 64;
		rp->rcr_pkt_threshold = 16;
		rp->rcr_timeout = 8;
		rp->rbr_kick_thresh = RBR_REFILL_MIN;
		if (rp->rbr_kick_thresh < rp->rbr_blocks_per_page)
			rp->rbr_kick_thresh = rp->rbr_blocks_per_page;

		err = niu_rbr_fill(np, rp, GFP_KERNEL);
		if (err)
			return err;
	}

	np->tx_rings = kzalloc(np->num_tx_rings * sizeof(struct tx_ring_info),
			       GFP_KERNEL);
	err = -ENOMEM;
	if (!np->tx_rings)
		goto out_err;

	for (i = 0; i < np->num_tx_rings; i++) {
		struct tx_ring_info *rp = &np->tx_rings[i];

		rp->np = np;
		rp->tx_channel = first_tx_channel + i;

		err = niu_alloc_tx_ring_info(np, rp);
		if (err)
			goto out_err;
	}

	return 0;

out_err:
	niu_free_channels(np);
	return err;
}

static int niu_tx_cs_sng_poll(struct niu *np, int channel)
{
	int limit = 1000;

	while (--limit > 0) {
		u64 val = nr64(TX_CS(channel));
		if (val & TX_CS_SNG_STATE)
			return 0;
	}
	return -ENODEV;
}

static int niu_tx_channel_stop(struct niu *np, int channel)
{
	u64 val = nr64(TX_CS(channel));

	val |= TX_CS_STOP_N_GO;
	nw64(TX_CS(channel), val);

	return niu_tx_cs_sng_poll(np, channel);
}

static int niu_tx_cs_reset_poll(struct niu *np, int channel)
{
	int limit = 1000;

	while (--limit > 0) {
		u64 val = nr64(TX_CS(channel));
		if (!(val & TX_CS_RST))
			return 0;
	}
	return -ENODEV;
}

static int niu_tx_channel_reset(struct niu *np, int channel)
{
	u64 val = nr64(TX_CS(channel));
	int err;

	val |= TX_CS_RST;
	nw64(TX_CS(channel), val);

	err = niu_tx_cs_reset_poll(np, channel);
	if (!err)
		nw64(TX_RING_KICK(channel), 0);

	return err;
}

static int niu_tx_channel_lpage_init(struct niu *np, int channel)
{
	u64 val;

	nw64(TX_LOG_MASK1(channel), 0);
	nw64(TX_LOG_VAL1(channel), 0);
	nw64(TX_LOG_MASK2(channel), 0);
	nw64(TX_LOG_VAL2(channel), 0);
	nw64(TX_LOG_PAGE_RELO1(channel), 0);
	nw64(TX_LOG_PAGE_RELO2(channel), 0);
	nw64(TX_LOG_PAGE_HDL(channel), 0);

	val  = (u64)np->port << TX_LOG_PAGE_VLD_FUNC_SHIFT;
	val |= (TX_LOG_PAGE_VLD_PAGE0 | TX_LOG_PAGE_VLD_PAGE1);
	nw64(TX_LOG_PAGE_VLD(channel), val);

	/* XXX TXDMA 32bit mode? XXX */

	return 0;
}

static void niu_txc_enable_port(struct niu *np, int on)
{
	unsigned long flags;
	u64 val, mask;

	niu_lock_parent(np, flags);
	val = nr64(TXC_CONTROL);
	mask = (u64)1 << np->port;
	if (on) {
		val |= TXC_CONTROL_ENABLE | mask;
	} else {
		val &= ~mask;
		if ((val & ~TXC_CONTROL_ENABLE) == 0)
			val &= ~TXC_CONTROL_ENABLE;
	}
	nw64(TXC_CONTROL, val);
	niu_unlock_parent(np, flags);
}

static void niu_txc_set_imask(struct niu *np, u64 imask)
{
	unsigned long flags;
	u64 val;

	niu_lock_parent(np, flags);
	val = nr64(TXC_INT_MASK);
	val &= ~TXC_INT_MASK_VAL(np->port);
	val |= (imask << TXC_INT_MASK_VAL_SHIFT(np->port));
	niu_unlock_parent(np, flags);
}

static void niu_txc_port_dma_enable(struct niu *np, int on)
{
	u64 val = 0;

	if (on) {
		int i;

		for (i = 0; i < np->num_tx_rings; i++)
			val |= (1 << np->tx_rings[i].tx_channel);
	}
	nw64(TXC_PORT_DMA(np->port), val);
}

static int niu_init_one_tx_channel(struct niu *np, struct tx_ring_info *rp)
{
	int err, channel = rp->tx_channel;
	u64 val, ring_len;

	err = niu_tx_channel_stop(np, channel);
	if (err)
		return err;

	err = niu_tx_channel_reset(np, channel);
	if (err)
		return err;

	err = niu_tx_channel_lpage_init(np, channel);
	if (err)
		return err;

	nw64(TXC_DMA_MAX(channel), rp->max_burst);
	nw64(TX_ENT_MSK(channel), 0);

	if (rp->descr_dma & ~(TX_RNG_CFIG_STADDR_BASE |
			      TX_RNG_CFIG_STADDR)) {
		dev_err(np->device, PFX "%s: TX ring channel %d "
			"DMA addr (%llx) is not aligned.\n",
			np->dev->name, channel,
			(unsigned long long) rp->descr_dma);
		return -EINVAL;
	}

	/* The length field in TX_RNG_CFIG is measured in 64-byte
	 * blocks.  rp->pending is the number of TX descriptors in
	 * our ring, 8 bytes each, thus we divide by 8 bytes more
	 * to get the proper value the chip wants.
	 */
	ring_len = (rp->pending / 8);

	val = ((ring_len << TX_RNG_CFIG_LEN_SHIFT) |
	       rp->descr_dma);
	nw64(TX_RNG_CFIG(channel), val);

	if (((rp->mbox_dma >> 32) & ~TXDMA_MBH_MBADDR) ||
	    ((u32)rp->mbox_dma & ~TXDMA_MBL_MBADDR)) {
		dev_err(np->device, PFX "%s: TX ring channel %d "
			"MBOX addr (%llx) is has illegal bits.\n",
			np->dev->name, channel,
			(unsigned long long) rp->mbox_dma);
		return -EINVAL;
	}
	nw64(TXDMA_MBH(channel), rp->mbox_dma >> 32);
	nw64(TXDMA_MBL(channel), rp->mbox_dma & TXDMA_MBL_MBADDR);

	nw64(TX_CS(channel), 0);

	rp->last_pkt_cnt = 0;

	return 0;
}

static void niu_init_rdc_groups(struct niu *np)
{
	struct niu_rdc_tables *tp = &np->parent->rdc_group_cfg[np->port];
	int i, first_table_num = tp->first_table_num;

	for (i = 0; i < tp->num_tables; i++) {
		struct rdc_table *tbl = &tp->tables[i];
		int this_table = first_table_num + i;
		int slot;

		for (slot = 0; slot < NIU_RDC_TABLE_SLOTS; slot++)
			nw64(RDC_TBL(this_table, slot),
			     tbl->rxdma_channel[slot]);
	}

	nw64(DEF_RDC(np->port), np->parent->rdc_default[np->port]);
}

static void niu_init_drr_weight(struct niu *np)
{
	int type = phy_decode(np->parent->port_phy, np->port);
	u64 val;

	switch (type) {
	case PORT_TYPE_10G:
		val = PT_DRR_WEIGHT_DEFAULT_10G;
		break;

	case PORT_TYPE_1G:
	default:
		val = PT_DRR_WEIGHT_DEFAULT_1G;
		break;
	}
	nw64(PT_DRR_WT(np->port), val);
}

static int niu_init_hostinfo(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	struct niu_rdc_tables *tp = &parent->rdc_group_cfg[np->port];
	int i, err, num_alt = niu_num_alt_addr(np);
	int first_rdc_table = tp->first_table_num;

	err = niu_set_primary_mac_rdc_table(np, first_rdc_table, 1);
	if (err)
		return err;

	err = niu_set_multicast_mac_rdc_table(np, first_rdc_table, 1);
	if (err)
		return err;

	for (i = 0; i < num_alt; i++) {
		err = niu_set_alt_mac_rdc_table(np, i, first_rdc_table, 1);
		if (err)
			return err;
	}

	return 0;
}

static int niu_rx_channel_reset(struct niu *np, int channel)
{
	return niu_set_and_wait_clear(np, RXDMA_CFIG1(channel),
				      RXDMA_CFIG1_RST, 1000, 10,
				      "RXDMA_CFIG1");
}

static int niu_rx_channel_lpage_init(struct niu *np, int channel)
{
	u64 val;

	nw64(RX_LOG_MASK1(channel), 0);
	nw64(RX_LOG_VAL1(channel), 0);
	nw64(RX_LOG_MASK2(channel), 0);
	nw64(RX_LOG_VAL2(channel), 0);
	nw64(RX_LOG_PAGE_RELO1(channel), 0);
	nw64(RX_LOG_PAGE_RELO2(channel), 0);
	nw64(RX_LOG_PAGE_HDL(channel), 0);

	val  = (u64)np->port << RX_LOG_PAGE_VLD_FUNC_SHIFT;
	val |= (RX_LOG_PAGE_VLD_PAGE0 | RX_LOG_PAGE_VLD_PAGE1);
	nw64(RX_LOG_PAGE_VLD(channel), val);

	return 0;
}

static void niu_rx_channel_wred_init(struct niu *np, struct rx_ring_info *rp)
{
	u64 val;

	val = (((u64)rp->nonsyn_window << RDC_RED_PARA_WIN_SHIFT) |
	       ((u64)rp->nonsyn_threshold << RDC_RED_PARA_THRE_SHIFT) |
	       ((u64)rp->syn_window << RDC_RED_PARA_WIN_SYN_SHIFT) |
	       ((u64)rp->syn_threshold << RDC_RED_PARA_THRE_SYN_SHIFT));
	nw64(RDC_RED_PARA(rp->rx_channel), val);
}

static int niu_compute_rbr_cfig_b(struct rx_ring_info *rp, u64 *ret)
{
	u64 val = 0;

	switch (rp->rbr_block_size) {
	case 4 * 1024:
		val |= (RBR_BLKSIZE_4K << RBR_CFIG_B_BLKSIZE_SHIFT);
		break;
	case 8 * 1024:
		val |= (RBR_BLKSIZE_8K << RBR_CFIG_B_BLKSIZE_SHIFT);
		break;
	case 16 * 1024:
		val |= (RBR_BLKSIZE_16K << RBR_CFIG_B_BLKSIZE_SHIFT);
		break;
	case 32 * 1024:
		val |= (RBR_BLKSIZE_32K << RBR_CFIG_B_BLKSIZE_SHIFT);
		break;
	default:
		return -EINVAL;
	}
	val |= RBR_CFIG_B_VLD2;
	switch (rp->rbr_sizes[2]) {
	case 2 * 1024:
		val |= (RBR_BUFSZ2_2K << RBR_CFIG_B_BUFSZ2_SHIFT);
		break;
	case 4 * 1024:
		val |= (RBR_BUFSZ2_4K << RBR_CFIG_B_BUFSZ2_SHIFT);
		break;
	case 8 * 1024:
		val |= (RBR_BUFSZ2_8K << RBR_CFIG_B_BUFSZ2_SHIFT);
		break;
	case 16 * 1024:
		val |= (RBR_BUFSZ2_16K << RBR_CFIG_B_BUFSZ2_SHIFT);
		break;

	default:
		return -EINVAL;
	}
	val |= RBR_CFIG_B_VLD1;
	switch (rp->rbr_sizes[1]) {
	case 1 * 1024:
		val |= (RBR_BUFSZ1_1K << RBR_CFIG_B_BUFSZ1_SHIFT);
		break;
	case 2 * 1024:
		val |= (RBR_BUFSZ1_2K << RBR_CFIG_B_BUFSZ1_SHIFT);
		break;
	case 4 * 1024:
		val |= (RBR_BUFSZ1_4K << RBR_CFIG_B_BUFSZ1_SHIFT);
		break;
	case 8 * 1024:
		val |= (RBR_BUFSZ1_8K << RBR_CFIG_B_BUFSZ1_SHIFT);
		break;

	default:
		return -EINVAL;
	}
	val |= RBR_CFIG_B_VLD0;
	switch (rp->rbr_sizes[0]) {
	case 256:
		val |= (RBR_BUFSZ0_256 << RBR_CFIG_B_BUFSZ0_SHIFT);
		break;
	case 512:
		val |= (RBR_BUFSZ0_512 << RBR_CFIG_B_BUFSZ0_SHIFT);
		break;
	case 1 * 1024:
		val |= (RBR_BUFSZ0_1K << RBR_CFIG_B_BUFSZ0_SHIFT);
		break;
	case 2 * 1024:
		val |= (RBR_BUFSZ0_2K << RBR_CFIG_B_BUFSZ0_SHIFT);
		break;

	default:
		return -EINVAL;
	}

	*ret = val;
	return 0;
}

static int niu_enable_rx_channel(struct niu *np, int channel, int on)
{
	u64 val = nr64(RXDMA_CFIG1(channel));
	int limit;

	if (on)
		val |= RXDMA_CFIG1_EN;
	else
		val &= ~RXDMA_CFIG1_EN;
	nw64(RXDMA_CFIG1(channel), val);

	limit = 1000;
	while (--limit > 0) {
		if (nr64(RXDMA_CFIG1(channel)) & RXDMA_CFIG1_QST)
			break;
		udelay(10);
	}
	if (limit <= 0)
		return -ENODEV;
	return 0;
}

static int niu_init_one_rx_channel(struct niu *np, struct rx_ring_info *rp)
{
	int err, channel = rp->rx_channel;
	u64 val;

	err = niu_rx_channel_reset(np, channel);
	if (err)
		return err;

	err = niu_rx_channel_lpage_init(np, channel);
	if (err)
		return err;

	niu_rx_channel_wred_init(np, rp);

	nw64(RX_DMA_ENT_MSK(channel), RX_DMA_ENT_MSK_RBR_EMPTY);
	nw64(RX_DMA_CTL_STAT(channel),
	     (RX_DMA_CTL_STAT_MEX |
	      RX_DMA_CTL_STAT_RCRTHRES |
	      RX_DMA_CTL_STAT_RCRTO |
	      RX_DMA_CTL_STAT_RBR_EMPTY));
	nw64(RXDMA_CFIG1(channel), rp->mbox_dma >> 32);
	nw64(RXDMA_CFIG2(channel), (rp->mbox_dma & 0x00000000ffffffc0));
	nw64(RBR_CFIG_A(channel),
	     ((u64)rp->rbr_table_size << RBR_CFIG_A_LEN_SHIFT) |
	     (rp->rbr_dma & (RBR_CFIG_A_STADDR_BASE | RBR_CFIG_A_STADDR)));
	err = niu_compute_rbr_cfig_b(rp, &val);
	if (err)
		return err;
	nw64(RBR_CFIG_B(channel), val);
	nw64(RCRCFIG_A(channel),
	     ((u64)rp->rcr_table_size << RCRCFIG_A_LEN_SHIFT) |
	     (rp->rcr_dma & (RCRCFIG_A_STADDR_BASE | RCRCFIG_A_STADDR)));
	nw64(RCRCFIG_B(channel),
	     ((u64)rp->rcr_pkt_threshold << RCRCFIG_B_PTHRES_SHIFT) |
	     RCRCFIG_B_ENTOUT |
	     ((u64)rp->rcr_timeout << RCRCFIG_B_TIMEOUT_SHIFT));

	err = niu_enable_rx_channel(np, channel, 1);
	if (err)
		return err;

	nw64(RBR_KICK(channel), rp->rbr_index);

	val = nr64(RX_DMA_CTL_STAT(channel));
	val |= RX_DMA_CTL_STAT_RBR_EMPTY;
	nw64(RX_DMA_CTL_STAT(channel), val);

	return 0;
}

static int niu_init_rx_channels(struct niu *np)
{
	unsigned long flags;
	u64 seed = jiffies_64;
	int err, i;

	niu_lock_parent(np, flags);
	nw64(RX_DMA_CK_DIV, np->parent->rxdma_clock_divider);
	nw64(RED_RAN_INIT, RED_RAN_INIT_OPMODE | (seed & RED_RAN_INIT_VAL));
	niu_unlock_parent(np, flags);

	/* XXX RXDMA 32bit mode? XXX */

	niu_init_rdc_groups(np);
	niu_init_drr_weight(np);

	err = niu_init_hostinfo(np);
	if (err)
		return err;

	for (i = 0; i < np->num_rx_rings; i++) {
		struct rx_ring_info *rp = &np->rx_rings[i];

		err = niu_init_one_rx_channel(np, rp);
		if (err)
			return err;
	}

	return 0;
}

static int niu_set_ip_frag_rule(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	struct niu_classifier *cp = &np->clas;
	struct niu_tcam_entry *tp;
	int index, err;

	index = cp->tcam_top;
	tp = &parent->tcam[index];

	/* Note that the noport bit is the same in both ipv4 and
	 * ipv6 format TCAM entries.
	 */
	memset(tp, 0, sizeof(*tp));
	tp->key[1] = TCAM_V4KEY1_NOPORT;
	tp->key_mask[1] = TCAM_V4KEY1_NOPORT;
	tp->assoc_data = (TCAM_ASSOCDATA_TRES_USE_OFFSET |
			  ((u64)0 << TCAM_ASSOCDATA_OFFSET_SHIFT));
	err = tcam_write(np, index, tp->key, tp->key_mask);
	if (err)
		return err;
	err = tcam_assoc_write(np, index, tp->assoc_data);
	if (err)
		return err;
	tp->valid = 1;
	cp->tcam_valid_entries++;

	return 0;
}

static int niu_init_classifier_hw(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	struct niu_classifier *cp = &np->clas;
	int i, err;

	nw64(H1POLY, cp->h1_init);
	nw64(H2POLY, cp->h2_init);

	err = niu_init_hostinfo(np);
	if (err)
		return err;

	for (i = 0; i < ENET_VLAN_TBL_NUM_ENTRIES; i++) {
		struct niu_vlan_rdc *vp = &cp->vlan_mappings[i];

		vlan_tbl_write(np, i, np->port,
			       vp->vlan_pref, vp->rdc_num);
	}

	for (i = 0; i < cp->num_alt_mac_mappings; i++) {
		struct niu_altmac_rdc *ap = &cp->alt_mac_mappings[i];

		err = niu_set_alt_mac_rdc_table(np, ap->alt_mac_num,
						ap->rdc_num, ap->mac_pref);
		if (err)
			return err;
	}

	for (i = CLASS_CODE_USER_PROG1; i <= CLASS_CODE_SCTP_IPV6; i++) {
		int index = i - CLASS_CODE_USER_PROG1;

		err = niu_set_tcam_key(np, i, parent->tcam_key[index]);
		if (err)
			return err;
		err = niu_set_flow_key(np, i, parent->flow_key[index]);
		if (err)
			return err;
	}

	err = niu_set_ip_frag_rule(np);
	if (err)
		return err;

	tcam_enable(np, 1);

	return 0;
}

static int niu_zcp_write(struct niu *np, int index, u64 *data)
{
	nw64(ZCP_RAM_DATA0, data[0]);
	nw64(ZCP_RAM_DATA1, data[1]);
	nw64(ZCP_RAM_DATA2, data[2]);
	nw64(ZCP_RAM_DATA3, data[3]);
	nw64(ZCP_RAM_DATA4, data[4]);
	nw64(ZCP_RAM_BE, ZCP_RAM_BE_VAL);
	nw64(ZCP_RAM_ACC,
	     (ZCP_RAM_ACC_WRITE |
	      (0 << ZCP_RAM_ACC_ZFCID_SHIFT) |
	      (ZCP_RAM_SEL_CFIFO(np->port) << ZCP_RAM_ACC_RAM_SEL_SHIFT)));

	return niu_wait_bits_clear(np, ZCP_RAM_ACC, ZCP_RAM_ACC_BUSY,
				   1000, 100);
}

static int niu_zcp_read(struct niu *np, int index, u64 *data)
{
	int err;

	err = niu_wait_bits_clear(np, ZCP_RAM_ACC, ZCP_RAM_ACC_BUSY,
				  1000, 100);
	if (err) {
		dev_err(np->device, PFX "%s: ZCP read busy won't clear, "
			"ZCP_RAM_ACC[%llx]\n", np->dev->name,
			(unsigned long long) nr64(ZCP_RAM_ACC));
		return err;
	}

	nw64(ZCP_RAM_ACC,
	     (ZCP_RAM_ACC_READ |
	      (0 << ZCP_RAM_ACC_ZFCID_SHIFT) |
	      (ZCP_RAM_SEL_CFIFO(np->port) << ZCP_RAM_ACC_RAM_SEL_SHIFT)));

	err = niu_wait_bits_clear(np, ZCP_RAM_ACC, ZCP_RAM_ACC_BUSY,
				  1000, 100);
	if (err) {
		dev_err(np->device, PFX "%s: ZCP read busy2 won't clear, "
			"ZCP_RAM_ACC[%llx]\n", np->dev->name,
			(unsigned long long) nr64(ZCP_RAM_ACC));
		return err;
	}

	data[0] = nr64(ZCP_RAM_DATA0);
	data[1] = nr64(ZCP_RAM_DATA1);
	data[2] = nr64(ZCP_RAM_DATA2);
	data[3] = nr64(ZCP_RAM_DATA3);
	data[4] = nr64(ZCP_RAM_DATA4);

	return 0;
}

static void niu_zcp_cfifo_reset(struct niu *np)
{
	u64 val = nr64(RESET_CFIFO);

	val |= RESET_CFIFO_RST(np->port);
	nw64(RESET_CFIFO, val);
	udelay(10);

	val &= ~RESET_CFIFO_RST(np->port);
	nw64(RESET_CFIFO, val);
}

static int niu_init_zcp(struct niu *np)
{
	u64 data[5], rbuf[5];
	int i, max, err;

	if (np->parent->plat_type != PLAT_TYPE_NIU) {
		if (np->port == 0 || np->port == 1)
			max = ATLAS_P0_P1_CFIFO_ENTRIES;
		else
			max = ATLAS_P2_P3_CFIFO_ENTRIES;
	} else
		max = NIU_CFIFO_ENTRIES;

	data[0] = 0;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0;
	data[4] = 0;

	for (i = 0; i < max; i++) {
		err = niu_zcp_write(np, i, data);
		if (err)
			return err;
		err = niu_zcp_read(np, i, rbuf);
		if (err)
			return err;
	}

	niu_zcp_cfifo_reset(np);
	nw64(CFIFO_ECC(np->port), 0);
	nw64(ZCP_INT_STAT, ZCP_INT_STAT_ALL);
	(void) nr64(ZCP_INT_STAT);
	nw64(ZCP_INT_MASK, ZCP_INT_MASK_ALL);

	return 0;
}

static void niu_ipp_write(struct niu *np, int index, u64 *data)
{
	u64 val = nr64_ipp(IPP_CFIG);

	nw64_ipp(IPP_CFIG, val | IPP_CFIG_DFIFO_PIO_W);
	nw64_ipp(IPP_DFIFO_WR_PTR, index);
	nw64_ipp(IPP_DFIFO_WR0, data[0]);
	nw64_ipp(IPP_DFIFO_WR1, data[1]);
	nw64_ipp(IPP_DFIFO_WR2, data[2]);
	nw64_ipp(IPP_DFIFO_WR3, data[3]);
	nw64_ipp(IPP_DFIFO_WR4, data[4]);
	nw64_ipp(IPP_CFIG, val & ~IPP_CFIG_DFIFO_PIO_W);
}

static void niu_ipp_read(struct niu *np, int index, u64 *data)
{
	nw64_ipp(IPP_DFIFO_RD_PTR, index);
	data[0] = nr64_ipp(IPP_DFIFO_RD0);
	data[1] = nr64_ipp(IPP_DFIFO_RD1);
	data[2] = nr64_ipp(IPP_DFIFO_RD2);
	data[3] = nr64_ipp(IPP_DFIFO_RD3);
	data[4] = nr64_ipp(IPP_DFIFO_RD4);
}

static int niu_ipp_reset(struct niu *np)
{
	return niu_set_and_wait_clear_ipp(np, IPP_CFIG, IPP_CFIG_SOFT_RST,
					  1000, 100, "IPP_CFIG");
}

static int niu_init_ipp(struct niu *np)
{
	u64 data[5], rbuf[5], val;
	int i, max, err;

	if (np->parent->plat_type != PLAT_TYPE_NIU) {
		if (np->port == 0 || np->port == 1)
			max = ATLAS_P0_P1_DFIFO_ENTRIES;
		else
			max = ATLAS_P2_P3_DFIFO_ENTRIES;
	} else
		max = NIU_DFIFO_ENTRIES;

	data[0] = 0;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0;
	data[4] = 0;

	for (i = 0; i < max; i++) {
		niu_ipp_write(np, i, data);
		niu_ipp_read(np, i, rbuf);
	}

	(void) nr64_ipp(IPP_INT_STAT);
	(void) nr64_ipp(IPP_INT_STAT);

	err = niu_ipp_reset(np);
	if (err)
		return err;

	(void) nr64_ipp(IPP_PKT_DIS);
	(void) nr64_ipp(IPP_BAD_CS_CNT);
	(void) nr64_ipp(IPP_ECC);

	(void) nr64_ipp(IPP_INT_STAT);

	nw64_ipp(IPP_MSK, ~IPP_MSK_ALL);

	val = nr64_ipp(IPP_CFIG);
	val &= ~IPP_CFIG_IP_MAX_PKT;
	val |= (IPP_CFIG_IPP_ENABLE |
		IPP_CFIG_DFIFO_ECC_EN |
		IPP_CFIG_DROP_BAD_CRC |
		IPP_CFIG_CKSUM_EN |
		(0x1ffff << IPP_CFIG_IP_MAX_PKT_SHIFT));
	nw64_ipp(IPP_CFIG, val);

	return 0;
}

static void niu_handle_led(struct niu *np, int status)
{
	u64 val;
	val = nr64_mac(XMAC_CONFIG);

	if ((np->flags & NIU_FLAGS_10G) != 0 &&
	    (np->flags & NIU_FLAGS_FIBER) != 0) {
		if (status) {
			val |= XMAC_CONFIG_LED_POLARITY;
			val &= ~XMAC_CONFIG_FORCE_LED_ON;
		} else {
			val |= XMAC_CONFIG_FORCE_LED_ON;
			val &= ~XMAC_CONFIG_LED_POLARITY;
		}
	}

	nw64_mac(XMAC_CONFIG, val);
}

static void niu_init_xif_xmac(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	u64 val;

	if (np->flags & NIU_FLAGS_XCVR_SERDES) {
		val = nr64(MIF_CONFIG);
		val |= MIF_CONFIG_ATCA_GE;
		nw64(MIF_CONFIG, val);
	}

	val = nr64_mac(XMAC_CONFIG);
	val &= ~XMAC_CONFIG_SEL_POR_CLK_SRC;

	val |= XMAC_CONFIG_TX_OUTPUT_EN;

	if (lp->loopback_mode == LOOPBACK_MAC) {
		val &= ~XMAC_CONFIG_SEL_POR_CLK_SRC;
		val |= XMAC_CONFIG_LOOPBACK;
	} else {
		val &= ~XMAC_CONFIG_LOOPBACK;
	}

	if (np->flags & NIU_FLAGS_10G) {
		val &= ~XMAC_CONFIG_LFS_DISABLE;
	} else {
		val |= XMAC_CONFIG_LFS_DISABLE;
		if (!(np->flags & NIU_FLAGS_FIBER) &&
		    !(np->flags & NIU_FLAGS_XCVR_SERDES))
			val |= XMAC_CONFIG_1G_PCS_BYPASS;
		else
			val &= ~XMAC_CONFIG_1G_PCS_BYPASS;
	}

	val &= ~XMAC_CONFIG_10G_XPCS_BYPASS;

	if (lp->active_speed == SPEED_100)
		val |= XMAC_CONFIG_SEL_CLK_25MHZ;
	else
		val &= ~XMAC_CONFIG_SEL_CLK_25MHZ;

	nw64_mac(XMAC_CONFIG, val);

	val = nr64_mac(XMAC_CONFIG);
	val &= ~XMAC_CONFIG_MODE_MASK;
	if (np->flags & NIU_FLAGS_10G) {
		val |= XMAC_CONFIG_MODE_XGMII;
	} else {
		if (lp->active_speed == SPEED_1000)
			val |= XMAC_CONFIG_MODE_GMII;
		else
			val |= XMAC_CONFIG_MODE_MII;
	}

	nw64_mac(XMAC_CONFIG, val);
}

static void niu_init_xif_bmac(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	u64 val;

	val = BMAC_XIF_CONFIG_TX_OUTPUT_EN;

	if (lp->loopback_mode == LOOPBACK_MAC)
		val |= BMAC_XIF_CONFIG_MII_LOOPBACK;
	else
		val &= ~BMAC_XIF_CONFIG_MII_LOOPBACK;

	if (lp->active_speed == SPEED_1000)
		val |= BMAC_XIF_CONFIG_GMII_MODE;
	else
		val &= ~BMAC_XIF_CONFIG_GMII_MODE;

	val &= ~(BMAC_XIF_CONFIG_LINK_LED |
		 BMAC_XIF_CONFIG_LED_POLARITY);

	if (!(np->flags & NIU_FLAGS_10G) &&
	    !(np->flags & NIU_FLAGS_FIBER) &&
	    lp->active_speed == SPEED_100)
		val |= BMAC_XIF_CONFIG_25MHZ_CLOCK;
	else
		val &= ~BMAC_XIF_CONFIG_25MHZ_CLOCK;

	nw64_mac(BMAC_XIF_CONFIG, val);
}

static void niu_init_xif(struct niu *np)
{
	if (np->flags & NIU_FLAGS_XMAC)
		niu_init_xif_xmac(np);
	else
		niu_init_xif_bmac(np);
}

static void niu_pcs_mii_reset(struct niu *np)
{
	int limit = 1000;
	u64 val = nr64_pcs(PCS_MII_CTL);
	val |= PCS_MII_CTL_RST;
	nw64_pcs(PCS_MII_CTL, val);
	while ((--limit >= 0) && (val & PCS_MII_CTL_RST)) {
		udelay(100);
		val = nr64_pcs(PCS_MII_CTL);
	}
}

static void niu_xpcs_reset(struct niu *np)
{
	int limit = 1000;
	u64 val = nr64_xpcs(XPCS_CONTROL1);
	val |= XPCS_CONTROL1_RESET;
	nw64_xpcs(XPCS_CONTROL1, val);
	while ((--limit >= 0) && (val & XPCS_CONTROL1_RESET)) {
		udelay(100);
		val = nr64_xpcs(XPCS_CONTROL1);
	}
}

static int niu_init_pcs(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;
	u64 val;

	switch (np->flags & (NIU_FLAGS_10G |
			     NIU_FLAGS_FIBER |
			     NIU_FLAGS_XCVR_SERDES)) {
	case NIU_FLAGS_FIBER:
		/* 1G fiber */
		nw64_pcs(PCS_CONF, PCS_CONF_MASK | PCS_CONF_ENABLE);
		nw64_pcs(PCS_DPATH_MODE, 0);
		niu_pcs_mii_reset(np);
		break;

	case NIU_FLAGS_10G:
	case NIU_FLAGS_10G | NIU_FLAGS_FIBER:
	case NIU_FLAGS_10G | NIU_FLAGS_XCVR_SERDES:
		/* 10G SERDES */
		if (!(np->flags & NIU_FLAGS_XMAC))
			return -EINVAL;

		/* 10G copper or fiber */
		val = nr64_mac(XMAC_CONFIG);
		val &= ~XMAC_CONFIG_10G_XPCS_BYPASS;
		nw64_mac(XMAC_CONFIG, val);

		niu_xpcs_reset(np);

		val = nr64_xpcs(XPCS_CONTROL1);
		if (lp->loopback_mode == LOOPBACK_PHY)
			val |= XPCS_CONTROL1_LOOPBACK;
		else
			val &= ~XPCS_CONTROL1_LOOPBACK;
		nw64_xpcs(XPCS_CONTROL1, val);

		nw64_xpcs(XPCS_DESKEW_ERR_CNT, 0);
		(void) nr64_xpcs(XPCS_SYMERR_CNT01);
		(void) nr64_xpcs(XPCS_SYMERR_CNT23);
		break;


	case NIU_FLAGS_XCVR_SERDES:
		/* 1G SERDES */
		niu_pcs_mii_reset(np);
		nw64_pcs(PCS_CONF, PCS_CONF_MASK | PCS_CONF_ENABLE);
		nw64_pcs(PCS_DPATH_MODE, 0);
		break;

	case 0:
		/* 1G copper */
	case NIU_FLAGS_XCVR_SERDES | NIU_FLAGS_FIBER:
		/* 1G RGMII FIBER */
		nw64_pcs(PCS_DPATH_MODE, PCS_DPATH_MODE_MII);
		niu_pcs_mii_reset(np);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int niu_reset_tx_xmac(struct niu *np)
{
	return niu_set_and_wait_clear_mac(np, XTXMAC_SW_RST,
					  (XTXMAC_SW_RST_REG_RS |
					   XTXMAC_SW_RST_SOFT_RST),
					  1000, 100, "XTXMAC_SW_RST");
}

static int niu_reset_tx_bmac(struct niu *np)
{
	int limit;

	nw64_mac(BTXMAC_SW_RST, BTXMAC_SW_RST_RESET);
	limit = 1000;
	while (--limit >= 0) {
		if (!(nr64_mac(BTXMAC_SW_RST) & BTXMAC_SW_RST_RESET))
			break;
		udelay(100);
	}
	if (limit < 0) {
		dev_err(np->device, PFX "Port %u TX BMAC would not reset, "
			"BTXMAC_SW_RST[%llx]\n",
			np->port,
			(unsigned long long) nr64_mac(BTXMAC_SW_RST));
		return -ENODEV;
	}

	return 0;
}

static int niu_reset_tx_mac(struct niu *np)
{
	if (np->flags & NIU_FLAGS_XMAC)
		return niu_reset_tx_xmac(np);
	else
		return niu_reset_tx_bmac(np);
}

static void niu_init_tx_xmac(struct niu *np, u64 min, u64 max)
{
	u64 val;

	val = nr64_mac(XMAC_MIN);
	val &= ~(XMAC_MIN_TX_MIN_PKT_SIZE |
		 XMAC_MIN_RX_MIN_PKT_SIZE);
	val |= (min << XMAC_MIN_RX_MIN_PKT_SIZE_SHFT);
	val |= (min << XMAC_MIN_TX_MIN_PKT_SIZE_SHFT);
	nw64_mac(XMAC_MIN, val);

	nw64_mac(XMAC_MAX, max);

	nw64_mac(XTXMAC_STAT_MSK, ~(u64)0);

	val = nr64_mac(XMAC_IPG);
	if (np->flags & NIU_FLAGS_10G) {
		val &= ~XMAC_IPG_IPG_XGMII;
		val |= (IPG_12_15_XGMII << XMAC_IPG_IPG_XGMII_SHIFT);
	} else {
		val &= ~XMAC_IPG_IPG_MII_GMII;
		val |= (IPG_12_MII_GMII << XMAC_IPG_IPG_MII_GMII_SHIFT);
	}
	nw64_mac(XMAC_IPG, val);

	val = nr64_mac(XMAC_CONFIG);
	val &= ~(XMAC_CONFIG_ALWAYS_NO_CRC |
		 XMAC_CONFIG_STRETCH_MODE |
		 XMAC_CONFIG_VAR_MIN_IPG_EN |
		 XMAC_CONFIG_TX_ENABLE);
	nw64_mac(XMAC_CONFIG, val);

	nw64_mac(TXMAC_FRM_CNT, 0);
	nw64_mac(TXMAC_BYTE_CNT, 0);
}

static void niu_init_tx_bmac(struct niu *np, u64 min, u64 max)
{
	u64 val;

	nw64_mac(BMAC_MIN_FRAME, min);
	nw64_mac(BMAC_MAX_FRAME, max);

	nw64_mac(BTXMAC_STATUS_MASK, ~(u64)0);
	nw64_mac(BMAC_CTRL_TYPE, 0x8808);
	nw64_mac(BMAC_PREAMBLE_SIZE, 7);

	val = nr64_mac(BTXMAC_CONFIG);
	val &= ~(BTXMAC_CONFIG_FCS_DISABLE |
		 BTXMAC_CONFIG_ENABLE);
	nw64_mac(BTXMAC_CONFIG, val);
}

static void niu_init_tx_mac(struct niu *np)
{
	u64 min, max;

	min = 64;
	if (np->dev->mtu > ETH_DATA_LEN)
		max = 9216;
	else
		max = 1522;

	/* The XMAC_MIN register only accepts values for TX min which
	 * have the low 3 bits cleared.
	 */
	BUILD_BUG_ON(min & 0x7);

	if (np->flags & NIU_FLAGS_XMAC)
		niu_init_tx_xmac(np, min, max);
	else
		niu_init_tx_bmac(np, min, max);
}

static int niu_reset_rx_xmac(struct niu *np)
{
	int limit;

	nw64_mac(XRXMAC_SW_RST,
		 XRXMAC_SW_RST_REG_RS | XRXMAC_SW_RST_SOFT_RST);
	limit = 1000;
	while (--limit >= 0) {
		if (!(nr64_mac(XRXMAC_SW_RST) & (XRXMAC_SW_RST_REG_RS |
						 XRXMAC_SW_RST_SOFT_RST)))
		    break;
		udelay(100);
	}
	if (limit < 0) {
		dev_err(np->device, PFX "Port %u RX XMAC would not reset, "
			"XRXMAC_SW_RST[%llx]\n",
			np->port,
			(unsigned long long) nr64_mac(XRXMAC_SW_RST));
		return -ENODEV;
	}

	return 0;
}

static int niu_reset_rx_bmac(struct niu *np)
{
	int limit;

	nw64_mac(BRXMAC_SW_RST, BRXMAC_SW_RST_RESET);
	limit = 1000;
	while (--limit >= 0) {
		if (!(nr64_mac(BRXMAC_SW_RST) & BRXMAC_SW_RST_RESET))
			break;
		udelay(100);
	}
	if (limit < 0) {
		dev_err(np->device, PFX "Port %u RX BMAC would not reset, "
			"BRXMAC_SW_RST[%llx]\n",
			np->port,
			(unsigned long long) nr64_mac(BRXMAC_SW_RST));
		return -ENODEV;
	}

	return 0;
}

static int niu_reset_rx_mac(struct niu *np)
{
	if (np->flags & NIU_FLAGS_XMAC)
		return niu_reset_rx_xmac(np);
	else
		return niu_reset_rx_bmac(np);
}

static void niu_init_rx_xmac(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	struct niu_rdc_tables *tp = &parent->rdc_group_cfg[np->port];
	int first_rdc_table = tp->first_table_num;
	unsigned long i;
	u64 val;

	nw64_mac(XMAC_ADD_FILT0, 0);
	nw64_mac(XMAC_ADD_FILT1, 0);
	nw64_mac(XMAC_ADD_FILT2, 0);
	nw64_mac(XMAC_ADD_FILT12_MASK, 0);
	nw64_mac(XMAC_ADD_FILT00_MASK, 0);
	for (i = 0; i < MAC_NUM_HASH; i++)
		nw64_mac(XMAC_HASH_TBL(i), 0);
	nw64_mac(XRXMAC_STAT_MSK, ~(u64)0);
	niu_set_primary_mac_rdc_table(np, first_rdc_table, 1);
	niu_set_multicast_mac_rdc_table(np, first_rdc_table, 1);

	val = nr64_mac(XMAC_CONFIG);
	val &= ~(XMAC_CONFIG_RX_MAC_ENABLE |
		 XMAC_CONFIG_PROMISCUOUS |
		 XMAC_CONFIG_PROMISC_GROUP |
		 XMAC_CONFIG_ERR_CHK_DIS |
		 XMAC_CONFIG_RX_CRC_CHK_DIS |
		 XMAC_CONFIG_RESERVED_MULTICAST |
		 XMAC_CONFIG_RX_CODEV_CHK_DIS |
		 XMAC_CONFIG_ADDR_FILTER_EN |
		 XMAC_CONFIG_RCV_PAUSE_ENABLE |
		 XMAC_CONFIG_STRIP_CRC |
		 XMAC_CONFIG_PASS_FLOW_CTRL |
		 XMAC_CONFIG_MAC2IPP_PKT_CNT_EN);
	val |= (XMAC_CONFIG_HASH_FILTER_EN);
	nw64_mac(XMAC_CONFIG, val);

	nw64_mac(RXMAC_BT_CNT, 0);
	nw64_mac(RXMAC_BC_FRM_CNT, 0);
	nw64_mac(RXMAC_MC_FRM_CNT, 0);
	nw64_mac(RXMAC_FRAG_CNT, 0);
	nw64_mac(RXMAC_HIST_CNT1, 0);
	nw64_mac(RXMAC_HIST_CNT2, 0);
	nw64_mac(RXMAC_HIST_CNT3, 0);
	nw64_mac(RXMAC_HIST_CNT4, 0);
	nw64_mac(RXMAC_HIST_CNT5, 0);
	nw64_mac(RXMAC_HIST_CNT6, 0);
	nw64_mac(RXMAC_HIST_CNT7, 0);
	nw64_mac(RXMAC_MPSZER_CNT, 0);
	nw64_mac(RXMAC_CRC_ER_CNT, 0);
	nw64_mac(RXMAC_CD_VIO_CNT, 0);
	nw64_mac(LINK_FAULT_CNT, 0);
}

static void niu_init_rx_bmac(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	struct niu_rdc_tables *tp = &parent->rdc_group_cfg[np->port];
	int first_rdc_table = tp->first_table_num;
	unsigned long i;
	u64 val;

	nw64_mac(BMAC_ADD_FILT0, 0);
	nw64_mac(BMAC_ADD_FILT1, 0);
	nw64_mac(BMAC_ADD_FILT2, 0);
	nw64_mac(BMAC_ADD_FILT12_MASK, 0);
	nw64_mac(BMAC_ADD_FILT00_MASK, 0);
	for (i = 0; i < MAC_NUM_HASH; i++)
		nw64_mac(BMAC_HASH_TBL(i), 0);
	niu_set_primary_mac_rdc_table(np, first_rdc_table, 1);
	niu_set_multicast_mac_rdc_table(np, first_rdc_table, 1);
	nw64_mac(BRXMAC_STATUS_MASK, ~(u64)0);

	val = nr64_mac(BRXMAC_CONFIG);
	val &= ~(BRXMAC_CONFIG_ENABLE |
		 BRXMAC_CONFIG_STRIP_PAD |
		 BRXMAC_CONFIG_STRIP_FCS |
		 BRXMAC_CONFIG_PROMISC |
		 BRXMAC_CONFIG_PROMISC_GRP |
		 BRXMAC_CONFIG_ADDR_FILT_EN |
		 BRXMAC_CONFIG_DISCARD_DIS);
	val |= (BRXMAC_CONFIG_HASH_FILT_EN);
	nw64_mac(BRXMAC_CONFIG, val);

	val = nr64_mac(BMAC_ADDR_CMPEN);
	val |= BMAC_ADDR_CMPEN_EN0;
	nw64_mac(BMAC_ADDR_CMPEN, val);
}

static void niu_init_rx_mac(struct niu *np)
{
	niu_set_primary_mac(np, np->dev->dev_addr);

	if (np->flags & NIU_FLAGS_XMAC)
		niu_init_rx_xmac(np);
	else
		niu_init_rx_bmac(np);
}

static void niu_enable_tx_xmac(struct niu *np, int on)
{
	u64 val = nr64_mac(XMAC_CONFIG);

	if (on)
		val |= XMAC_CONFIG_TX_ENABLE;
	else
		val &= ~XMAC_CONFIG_TX_ENABLE;
	nw64_mac(XMAC_CONFIG, val);
}

static void niu_enable_tx_bmac(struct niu *np, int on)
{
	u64 val = nr64_mac(BTXMAC_CONFIG);

	if (on)
		val |= BTXMAC_CONFIG_ENABLE;
	else
		val &= ~BTXMAC_CONFIG_ENABLE;
	nw64_mac(BTXMAC_CONFIG, val);
}

static void niu_enable_tx_mac(struct niu *np, int on)
{
	if (np->flags & NIU_FLAGS_XMAC)
		niu_enable_tx_xmac(np, on);
	else
		niu_enable_tx_bmac(np, on);
}

static void niu_enable_rx_xmac(struct niu *np, int on)
{
	u64 val = nr64_mac(XMAC_CONFIG);

	val &= ~(XMAC_CONFIG_HASH_FILTER_EN |
		 XMAC_CONFIG_PROMISCUOUS);

	if (np->flags & NIU_FLAGS_MCAST)
		val |= XMAC_CONFIG_HASH_FILTER_EN;
	if (np->flags & NIU_FLAGS_PROMISC)
		val |= XMAC_CONFIG_PROMISCUOUS;

	if (on)
		val |= XMAC_CONFIG_RX_MAC_ENABLE;
	else
		val &= ~XMAC_CONFIG_RX_MAC_ENABLE;
	nw64_mac(XMAC_CONFIG, val);
}

static void niu_enable_rx_bmac(struct niu *np, int on)
{
	u64 val = nr64_mac(BRXMAC_CONFIG);

	val &= ~(BRXMAC_CONFIG_HASH_FILT_EN |
		 BRXMAC_CONFIG_PROMISC);

	if (np->flags & NIU_FLAGS_MCAST)
		val |= BRXMAC_CONFIG_HASH_FILT_EN;
	if (np->flags & NIU_FLAGS_PROMISC)
		val |= BRXMAC_CONFIG_PROMISC;

	if (on)
		val |= BRXMAC_CONFIG_ENABLE;
	else
		val &= ~BRXMAC_CONFIG_ENABLE;
	nw64_mac(BRXMAC_CONFIG, val);
}

static void niu_enable_rx_mac(struct niu *np, int on)
{
	if (np->flags & NIU_FLAGS_XMAC)
		niu_enable_rx_xmac(np, on);
	else
		niu_enable_rx_bmac(np, on);
}

static int niu_init_mac(struct niu *np)
{
	int err;

	niu_init_xif(np);
	err = niu_init_pcs(np);
	if (err)
		return err;

	err = niu_reset_tx_mac(np);
	if (err)
		return err;
	niu_init_tx_mac(np);
	err = niu_reset_rx_mac(np);
	if (err)
		return err;
	niu_init_rx_mac(np);

	/* This looks hookey but the RX MAC reset we just did will
	 * undo some of the state we setup in niu_init_tx_mac() so we
	 * have to call it again.  In particular, the RX MAC reset will
	 * set the XMAC_MAX register back to it's default value.
	 */
	niu_init_tx_mac(np);
	niu_enable_tx_mac(np, 1);

	niu_enable_rx_mac(np, 1);

	return 0;
}

static void niu_stop_one_tx_channel(struct niu *np, struct tx_ring_info *rp)
{
	(void) niu_tx_channel_stop(np, rp->tx_channel);
}

static void niu_stop_tx_channels(struct niu *np)
{
	int i;

	for (i = 0; i < np->num_tx_rings; i++) {
		struct tx_ring_info *rp = &np->tx_rings[i];

		niu_stop_one_tx_channel(np, rp);
	}
}

static void niu_reset_one_tx_channel(struct niu *np, struct tx_ring_info *rp)
{
	(void) niu_tx_channel_reset(np, rp->tx_channel);
}

static void niu_reset_tx_channels(struct niu *np)
{
	int i;

	for (i = 0; i < np->num_tx_rings; i++) {
		struct tx_ring_info *rp = &np->tx_rings[i];

		niu_reset_one_tx_channel(np, rp);
	}
}

static void niu_stop_one_rx_channel(struct niu *np, struct rx_ring_info *rp)
{
	(void) niu_enable_rx_channel(np, rp->rx_channel, 0);
}

static void niu_stop_rx_channels(struct niu *np)
{
	int i;

	for (i = 0; i < np->num_rx_rings; i++) {
		struct rx_ring_info *rp = &np->rx_rings[i];

		niu_stop_one_rx_channel(np, rp);
	}
}

static void niu_reset_one_rx_channel(struct niu *np, struct rx_ring_info *rp)
{
	int channel = rp->rx_channel;

	(void) niu_rx_channel_reset(np, channel);
	nw64(RX_DMA_ENT_MSK(channel), RX_DMA_ENT_MSK_ALL);
	nw64(RX_DMA_CTL_STAT(channel), 0);
	(void) niu_enable_rx_channel(np, channel, 0);
}

static void niu_reset_rx_channels(struct niu *np)
{
	int i;

	for (i = 0; i < np->num_rx_rings; i++) {
		struct rx_ring_info *rp = &np->rx_rings[i];

		niu_reset_one_rx_channel(np, rp);
	}
}

static void niu_disable_ipp(struct niu *np)
{
	u64 rd, wr, val;
	int limit;

	rd = nr64_ipp(IPP_DFIFO_RD_PTR);
	wr = nr64_ipp(IPP_DFIFO_WR_PTR);
	limit = 100;
	while (--limit >= 0 && (rd != wr)) {
		rd = nr64_ipp(IPP_DFIFO_RD_PTR);
		wr = nr64_ipp(IPP_DFIFO_WR_PTR);
	}
	if (limit < 0 &&
	    (rd != 0 && wr != 1)) {
		dev_err(np->device, PFX "%s: IPP would not quiesce, "
			"rd_ptr[%llx] wr_ptr[%llx]\n",
			np->dev->name,
			(unsigned long long) nr64_ipp(IPP_DFIFO_RD_PTR),
			(unsigned long long) nr64_ipp(IPP_DFIFO_WR_PTR));
	}

	val = nr64_ipp(IPP_CFIG);
	val &= ~(IPP_CFIG_IPP_ENABLE |
		 IPP_CFIG_DFIFO_ECC_EN |
		 IPP_CFIG_DROP_BAD_CRC |
		 IPP_CFIG_CKSUM_EN);
	nw64_ipp(IPP_CFIG, val);

	(void) niu_ipp_reset(np);
}

static int niu_init_hw(struct niu *np)
{
	int i, err;

	niudbg(IFUP, "%s: Initialize TXC\n", np->dev->name);
	niu_txc_enable_port(np, 1);
	niu_txc_port_dma_enable(np, 1);
	niu_txc_set_imask(np, 0);

	niudbg(IFUP, "%s: Initialize TX channels\n", np->dev->name);
	for (i = 0; i < np->num_tx_rings; i++) {
		struct tx_ring_info *rp = &np->tx_rings[i];

		err = niu_init_one_tx_channel(np, rp);
		if (err)
			return err;
	}

	niudbg(IFUP, "%s: Initialize RX channels\n", np->dev->name);
	err = niu_init_rx_channels(np);
	if (err)
		goto out_uninit_tx_channels;

	niudbg(IFUP, "%s: Initialize classifier\n", np->dev->name);
	err = niu_init_classifier_hw(np);
	if (err)
		goto out_uninit_rx_channels;

	niudbg(IFUP, "%s: Initialize ZCP\n", np->dev->name);
	err = niu_init_zcp(np);
	if (err)
		goto out_uninit_rx_channels;

	niudbg(IFUP, "%s: Initialize IPP\n", np->dev->name);
	err = niu_init_ipp(np);
	if (err)
		goto out_uninit_rx_channels;

	niudbg(IFUP, "%s: Initialize MAC\n", np->dev->name);
	err = niu_init_mac(np);
	if (err)
		goto out_uninit_ipp;

	return 0;

out_uninit_ipp:
	niudbg(IFUP, "%s: Uninit IPP\n", np->dev->name);
	niu_disable_ipp(np);

out_uninit_rx_channels:
	niudbg(IFUP, "%s: Uninit RX channels\n", np->dev->name);
	niu_stop_rx_channels(np);
	niu_reset_rx_channels(np);

out_uninit_tx_channels:
	niudbg(IFUP, "%s: Uninit TX channels\n", np->dev->name);
	niu_stop_tx_channels(np);
	niu_reset_tx_channels(np);

	return err;
}

static void niu_stop_hw(struct niu *np)
{
	niudbg(IFDOWN, "%s: Disable interrupts\n", np->dev->name);
	niu_enable_interrupts(np, 0);

	niudbg(IFDOWN, "%s: Disable RX MAC\n", np->dev->name);
	niu_enable_rx_mac(np, 0);

	niudbg(IFDOWN, "%s: Disable IPP\n", np->dev->name);
	niu_disable_ipp(np);

	niudbg(IFDOWN, "%s: Stop TX channels\n", np->dev->name);
	niu_stop_tx_channels(np);

	niudbg(IFDOWN, "%s: Stop RX channels\n", np->dev->name);
	niu_stop_rx_channels(np);

	niudbg(IFDOWN, "%s: Reset TX channels\n", np->dev->name);
	niu_reset_tx_channels(np);

	niudbg(IFDOWN, "%s: Reset RX channels\n", np->dev->name);
	niu_reset_rx_channels(np);
}

static void niu_set_irq_name(struct niu *np)
{
	int port = np->port;
	int i, j = 1;

	sprintf(np->irq_name[0], "%s:MAC", np->dev->name);

	if (port == 0) {
		sprintf(np->irq_name[1], "%s:MIF", np->dev->name);
		sprintf(np->irq_name[2], "%s:SYSERR", np->dev->name);
		j = 3;
	}

	for (i = 0; i < np->num_ldg - j; i++) {
		if (i < np->num_rx_rings)
			sprintf(np->irq_name[i+j], "%s-rx-%d",
				np->dev->name, i);
		else if (i < np->num_tx_rings + np->num_rx_rings)
			sprintf(np->irq_name[i+j], "%s-tx-%d", np->dev->name,
				i - np->num_rx_rings);
	}
}

static int niu_request_irq(struct niu *np)
{
	int i, j, err;

	niu_set_irq_name(np);

	err = 0;
	for (i = 0; i < np->num_ldg; i++) {
		struct niu_ldg *lp = &np->ldg[i];

		err = request_irq(lp->irq, niu_interrupt,
				  IRQF_SHARED | IRQF_SAMPLE_RANDOM,
				  np->irq_name[i], lp);
		if (err)
			goto out_free_irqs;

	}

	return 0;

out_free_irqs:
	for (j = 0; j < i; j++) {
		struct niu_ldg *lp = &np->ldg[j];

		free_irq(lp->irq, lp);
	}
	return err;
}

static void niu_free_irq(struct niu *np)
{
	int i;

	for (i = 0; i < np->num_ldg; i++) {
		struct niu_ldg *lp = &np->ldg[i];

		free_irq(lp->irq, lp);
	}
}

static void niu_enable_napi(struct niu *np)
{
	int i;

	for (i = 0; i < np->num_ldg; i++)
		napi_enable(&np->ldg[i].napi);
}

static void niu_disable_napi(struct niu *np)
{
	int i;

	for (i = 0; i < np->num_ldg; i++)
		napi_disable(&np->ldg[i].napi);
}

static int niu_open(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);
	int err;

	netif_carrier_off(dev);

	err = niu_alloc_channels(np);
	if (err)
		goto out_err;

	err = niu_enable_interrupts(np, 0);
	if (err)
		goto out_free_channels;

	err = niu_request_irq(np);
	if (err)
		goto out_free_channels;

	niu_enable_napi(np);

	spin_lock_irq(&np->lock);

	err = niu_init_hw(np);
	if (!err) {
		init_timer(&np->timer);
		np->timer.expires = jiffies + HZ;
		np->timer.data = (unsigned long) np;
		np->timer.function = niu_timer;

		err = niu_enable_interrupts(np, 1);
		if (err)
			niu_stop_hw(np);
	}

	spin_unlock_irq(&np->lock);

	if (err) {
		niu_disable_napi(np);
		goto out_free_irq;
	}

	netif_tx_start_all_queues(dev);

	if (np->link_config.loopback_mode != LOOPBACK_DISABLED)
		netif_carrier_on(dev);

	add_timer(&np->timer);

	return 0;

out_free_irq:
	niu_free_irq(np);

out_free_channels:
	niu_free_channels(np);

out_err:
	return err;
}

static void niu_full_shutdown(struct niu *np, struct net_device *dev)
{
	cancel_work_sync(&np->reset_task);

	niu_disable_napi(np);
	netif_tx_stop_all_queues(dev);

	del_timer_sync(&np->timer);

	spin_lock_irq(&np->lock);

	niu_stop_hw(np);

	spin_unlock_irq(&np->lock);
}

static int niu_close(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);

	niu_full_shutdown(np, dev);

	niu_free_irq(np);

	niu_free_channels(np);

	niu_handle_led(np, 0);

	return 0;
}

static void niu_sync_xmac_stats(struct niu *np)
{
	struct niu_xmac_stats *mp = &np->mac_stats.xmac;

	mp->tx_frames += nr64_mac(TXMAC_FRM_CNT);
	mp->tx_bytes += nr64_mac(TXMAC_BYTE_CNT);

	mp->rx_link_faults += nr64_mac(LINK_FAULT_CNT);
	mp->rx_align_errors += nr64_mac(RXMAC_ALIGN_ERR_CNT);
	mp->rx_frags += nr64_mac(RXMAC_FRAG_CNT);
	mp->rx_mcasts += nr64_mac(RXMAC_MC_FRM_CNT);
	mp->rx_bcasts += nr64_mac(RXMAC_BC_FRM_CNT);
	mp->rx_hist_cnt1 += nr64_mac(RXMAC_HIST_CNT1);
	mp->rx_hist_cnt2 += nr64_mac(RXMAC_HIST_CNT2);
	mp->rx_hist_cnt3 += nr64_mac(RXMAC_HIST_CNT3);
	mp->rx_hist_cnt4 += nr64_mac(RXMAC_HIST_CNT4);
	mp->rx_hist_cnt5 += nr64_mac(RXMAC_HIST_CNT5);
	mp->rx_hist_cnt6 += nr64_mac(RXMAC_HIST_CNT6);
	mp->rx_hist_cnt7 += nr64_mac(RXMAC_HIST_CNT7);
	mp->rx_octets += nr64_mac(RXMAC_BT_CNT);
	mp->rx_code_violations += nr64_mac(RXMAC_CD_VIO_CNT);
	mp->rx_len_errors += nr64_mac(RXMAC_MPSZER_CNT);
	mp->rx_crc_errors += nr64_mac(RXMAC_CRC_ER_CNT);
}

static void niu_sync_bmac_stats(struct niu *np)
{
	struct niu_bmac_stats *mp = &np->mac_stats.bmac;

	mp->tx_bytes += nr64_mac(BTXMAC_BYTE_CNT);
	mp->tx_frames += nr64_mac(BTXMAC_FRM_CNT);

	mp->rx_frames += nr64_mac(BRXMAC_FRAME_CNT);
	mp->rx_align_errors += nr64_mac(BRXMAC_ALIGN_ERR_CNT);
	mp->rx_crc_errors += nr64_mac(BRXMAC_ALIGN_ERR_CNT);
	mp->rx_len_errors += nr64_mac(BRXMAC_CODE_VIOL_ERR_CNT);
}

static void niu_sync_mac_stats(struct niu *np)
{
	if (np->flags & NIU_FLAGS_XMAC)
		niu_sync_xmac_stats(np);
	else
		niu_sync_bmac_stats(np);
}

static void niu_get_rx_stats(struct niu *np)
{
	unsigned long pkts, dropped, errors, bytes;
	int i;

	pkts = dropped = errors = bytes = 0;
	for (i = 0; i < np->num_rx_rings; i++) {
		struct rx_ring_info *rp = &np->rx_rings[i];

		niu_sync_rx_discard_stats(np, rp, 0);

		pkts += rp->rx_packets;
		bytes += rp->rx_bytes;
		dropped += rp->rx_dropped;
		errors += rp->rx_errors;
	}
	np->dev->stats.rx_packets = pkts;
	np->dev->stats.rx_bytes = bytes;
	np->dev->stats.rx_dropped = dropped;
	np->dev->stats.rx_errors = errors;
}

static void niu_get_tx_stats(struct niu *np)
{
	unsigned long pkts, errors, bytes;
	int i;

	pkts = errors = bytes = 0;
	for (i = 0; i < np->num_tx_rings; i++) {
		struct tx_ring_info *rp = &np->tx_rings[i];

		pkts += rp->tx_packets;
		bytes += rp->tx_bytes;
		errors += rp->tx_errors;
	}
	np->dev->stats.tx_packets = pkts;
	np->dev->stats.tx_bytes = bytes;
	np->dev->stats.tx_errors = errors;
}

static struct net_device_stats *niu_get_stats(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);

	niu_get_rx_stats(np);
	niu_get_tx_stats(np);

	return &dev->stats;
}

static void niu_load_hash_xmac(struct niu *np, u16 *hash)
{
	int i;

	for (i = 0; i < 16; i++)
		nw64_mac(XMAC_HASH_TBL(i), hash[i]);
}

static void niu_load_hash_bmac(struct niu *np, u16 *hash)
{
	int i;

	for (i = 0; i < 16; i++)
		nw64_mac(BMAC_HASH_TBL(i), hash[i]);
}

static void niu_load_hash(struct niu *np, u16 *hash)
{
	if (np->flags & NIU_FLAGS_XMAC)
		niu_load_hash_xmac(np, hash);
	else
		niu_load_hash_bmac(np, hash);
}

static void niu_set_rx_mode(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);
	int i, alt_cnt, err;
	struct dev_addr_list *addr;
	unsigned long flags;
	u16 hash[16] = { 0, };

	spin_lock_irqsave(&np->lock, flags);
	niu_enable_rx_mac(np, 0);

	np->flags &= ~(NIU_FLAGS_MCAST | NIU_FLAGS_PROMISC);
	if (dev->flags & IFF_PROMISC)
		np->flags |= NIU_FLAGS_PROMISC;
	if ((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 0))
		np->flags |= NIU_FLAGS_MCAST;

	alt_cnt = dev->uc_count;
	if (alt_cnt > niu_num_alt_addr(np)) {
		alt_cnt = 0;
		np->flags |= NIU_FLAGS_PROMISC;
	}

	if (alt_cnt) {
		int index = 0;

		for (addr = dev->uc_list; addr; addr = addr->next) {
			err = niu_set_alt_mac(np, index,
					      addr->da_addr);
			if (err)
				printk(KERN_WARNING PFX "%s: Error %d "
				       "adding alt mac %d\n",
				       dev->name, err, index);
			err = niu_enable_alt_mac(np, index, 1);
			if (err)
				printk(KERN_WARNING PFX "%s: Error %d "
				       "enabling alt mac %d\n",
				       dev->name, err, index);

			index++;
		}
	} else {
		int alt_start;
		if (np->flags & NIU_FLAGS_XMAC)
			alt_start = 0;
		else
			alt_start = 1;
		for (i = alt_start; i < niu_num_alt_addr(np); i++) {
			err = niu_enable_alt_mac(np, i, 0);
			if (err)
				printk(KERN_WARNING PFX "%s: Error %d "
				       "disabling alt mac %d\n",
				       dev->name, err, i);
		}
	}
	if (dev->flags & IFF_ALLMULTI) {
		for (i = 0; i < 16; i++)
			hash[i] = 0xffff;
	} else if (dev->mc_count > 0) {
		for (addr = dev->mc_list; addr; addr = addr->next) {
			u32 crc = ether_crc_le(ETH_ALEN, addr->da_addr);

			crc >>= 24;
			hash[crc >> 4] |= (1 << (15 - (crc & 0xf)));
		}
	}

	if (np->flags & NIU_FLAGS_MCAST)
		niu_load_hash(np, hash);

	niu_enable_rx_mac(np, 1);
	spin_unlock_irqrestore(&np->lock, flags);
}

static int niu_set_mac_addr(struct net_device *dev, void *p)
{
	struct niu *np = netdev_priv(dev);
	struct sockaddr *addr = p;
	unsigned long flags;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);

	if (!netif_running(dev))
		return 0;

	spin_lock_irqsave(&np->lock, flags);
	niu_enable_rx_mac(np, 0);
	niu_set_primary_mac(np, dev->dev_addr);
	niu_enable_rx_mac(np, 1);
	spin_unlock_irqrestore(&np->lock, flags);

	return 0;
}

static int niu_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

static void niu_netif_stop(struct niu *np)
{
	np->dev->trans_start = jiffies;	/* prevent tx timeout */

	niu_disable_napi(np);

	netif_tx_disable(np->dev);
}

static void niu_netif_start(struct niu *np)
{
	/* NOTE: unconditional netif_wake_queue is only appropriate
	 * so long as all callers are assured to have free tx slots
	 * (such as after niu_init_hw).
	 */
	netif_tx_wake_all_queues(np->dev);

	niu_enable_napi(np);

	niu_enable_interrupts(np, 1);
}

static void niu_reset_buffers(struct niu *np)
{
	int i, j, k, err;

	if (np->rx_rings) {
		for (i = 0; i < np->num_rx_rings; i++) {
			struct rx_ring_info *rp = &np->rx_rings[i];

			for (j = 0, k = 0; j < MAX_RBR_RING_SIZE; j++) {
				struct page *page;

				page = rp->rxhash[j];
				while (page) {
					struct page *next =
						(struct page *) page->mapping;
					u64 base = page->index;
					base = base >> RBR_DESCR_ADDR_SHIFT;
					rp->rbr[k++] = cpu_to_le32(base);
					page = next;
				}
			}
			for (; k < MAX_RBR_RING_SIZE; k++) {
				err = niu_rbr_add_page(np, rp, GFP_ATOMIC, k);
				if (unlikely(err))
					break;
			}

			rp->rbr_index = rp->rbr_table_size - 1;
			rp->rcr_index = 0;
			rp->rbr_pending = 0;
			rp->rbr_refill_pending = 0;
		}
	}
	if (np->tx_rings) {
		for (i = 0; i < np->num_tx_rings; i++) {
			struct tx_ring_info *rp = &np->tx_rings[i];

			for (j = 0; j < MAX_TX_RING_SIZE; j++) {
				if (rp->tx_buffs[j].skb)
					(void) release_tx_packet(np, rp, j);
			}

			rp->pending = MAX_TX_RING_SIZE;
			rp->prod = 0;
			rp->cons = 0;
			rp->wrap_bit = 0;
		}
	}
}

static void niu_reset_task(struct work_struct *work)
{
	struct niu *np = container_of(work, struct niu, reset_task);
	unsigned long flags;
	int err;

	spin_lock_irqsave(&np->lock, flags);
	if (!netif_running(np->dev)) {
		spin_unlock_irqrestore(&np->lock, flags);
		return;
	}

	spin_unlock_irqrestore(&np->lock, flags);

	del_timer_sync(&np->timer);

	niu_netif_stop(np);

	spin_lock_irqsave(&np->lock, flags);

	niu_stop_hw(np);

	spin_unlock_irqrestore(&np->lock, flags);

	niu_reset_buffers(np);

	spin_lock_irqsave(&np->lock, flags);

	err = niu_init_hw(np);
	if (!err) {
		np->timer.expires = jiffies + HZ;
		add_timer(&np->timer);
		niu_netif_start(np);
	}

	spin_unlock_irqrestore(&np->lock, flags);
}

static void niu_tx_timeout(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);

	dev_err(np->device, PFX "%s: Transmit timed out, resetting\n",
		dev->name);

	schedule_work(&np->reset_task);
}

static void niu_set_txd(struct tx_ring_info *rp, int index,
			u64 mapping, u64 len, u64 mark,
			u64 n_frags)
{
	__le64 *desc = &rp->descr[index];

	*desc = cpu_to_le64(mark |
			    (n_frags << TX_DESC_NUM_PTR_SHIFT) |
			    (len << TX_DESC_TR_LEN_SHIFT) |
			    (mapping & TX_DESC_SAD));
}

static u64 niu_compute_tx_flags(struct sk_buff *skb, struct ethhdr *ehdr,
				u64 pad_bytes, u64 len)
{
	u16 eth_proto, eth_proto_inner;
	u64 csum_bits, l3off, ihl, ret;
	u8 ip_proto;
	int ipv6;

	eth_proto = be16_to_cpu(ehdr->h_proto);
	eth_proto_inner = eth_proto;
	if (eth_proto == ETH_P_8021Q) {
		struct vlan_ethhdr *vp = (struct vlan_ethhdr *) ehdr;
		__be16 val = vp->h_vlan_encapsulated_proto;

		eth_proto_inner = be16_to_cpu(val);
	}

	ipv6 = ihl = 0;
	switch (skb->protocol) {
	case cpu_to_be16(ETH_P_IP):
		ip_proto = ip_hdr(skb)->protocol;
		ihl = ip_hdr(skb)->ihl;
		break;
	case cpu_to_be16(ETH_P_IPV6):
		ip_proto = ipv6_hdr(skb)->nexthdr;
		ihl = (40 >> 2);
		ipv6 = 1;
		break;
	default:
		ip_proto = ihl = 0;
		break;
	}

	csum_bits = TXHDR_CSUM_NONE;
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		u64 start, stuff;

		csum_bits = (ip_proto == IPPROTO_TCP ?
			     TXHDR_CSUM_TCP :
			     (ip_proto == IPPROTO_UDP ?
			      TXHDR_CSUM_UDP : TXHDR_CSUM_SCTP));

		start = skb_transport_offset(skb) -
			(pad_bytes + sizeof(struct tx_pkt_hdr));
		stuff = start + skb->csum_offset;

		csum_bits |= (start / 2) << TXHDR_L4START_SHIFT;
		csum_bits |= (stuff / 2) << TXHDR_L4STUFF_SHIFT;
	}

	l3off = skb_network_offset(skb) -
		(pad_bytes + sizeof(struct tx_pkt_hdr));

	ret = (((pad_bytes / 2) << TXHDR_PAD_SHIFT) |
	       (len << TXHDR_LEN_SHIFT) |
	       ((l3off / 2) << TXHDR_L3START_SHIFT) |
	       (ihl << TXHDR_IHL_SHIFT) |
	       ((eth_proto_inner < 1536) ? TXHDR_LLC : 0) |
	       ((eth_proto == ETH_P_8021Q) ? TXHDR_VLAN : 0) |
	       (ipv6 ? TXHDR_IP_VER : 0) |
	       csum_bits);

	return ret;
}

static int niu_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);
	unsigned long align, headroom;
	struct netdev_queue *txq;
	struct tx_ring_info *rp;
	struct tx_pkt_hdr *tp;
	unsigned int len, nfg;
	struct ethhdr *ehdr;
	int prod, i, tlen;
	u64 mapping, mrk;

	i = skb_get_queue_mapping(skb);
	rp = &np->tx_rings[i];
	txq = netdev_get_tx_queue(dev, i);

	if (niu_tx_avail(rp) <= (skb_shinfo(skb)->nr_frags + 1)) {
		netif_tx_stop_queue(txq);
		dev_err(np->device, PFX "%s: BUG! Tx ring full when "
			"queue awake!\n", dev->name);
		rp->tx_errors++;
		return NETDEV_TX_BUSY;
	}

	if (skb->len < ETH_ZLEN) {
		unsigned int pad_bytes = ETH_ZLEN - skb->len;

		if (skb_pad(skb, pad_bytes))
			goto out;
		skb_put(skb, pad_bytes);
	}

	len = sizeof(struct tx_pkt_hdr) + 15;
	if (skb_headroom(skb) < len) {
		struct sk_buff *skb_new;

		skb_new = skb_realloc_headroom(skb, len);
		if (!skb_new) {
			rp->tx_errors++;
			goto out_drop;
		}
		kfree_skb(skb);
		skb = skb_new;
	} else
		skb_orphan(skb);

	align = ((unsigned long) skb->data & (16 - 1));
	headroom = align + sizeof(struct tx_pkt_hdr);

	ehdr = (struct ethhdr *) skb->data;
	tp = (struct tx_pkt_hdr *) skb_push(skb, headroom);

	len = skb->len - sizeof(struct tx_pkt_hdr);
	tp->flags = cpu_to_le64(niu_compute_tx_flags(skb, ehdr, align, len));
	tp->resv = 0;

	len = skb_headlen(skb);
	mapping = np->ops->map_single(np->device, skb->data,
				      len, DMA_TO_DEVICE);

	prod = rp->prod;

	rp->tx_buffs[prod].skb = skb;
	rp->tx_buffs[prod].mapping = mapping;

	mrk = TX_DESC_SOP;
	if (++rp->mark_counter == rp->mark_freq) {
		rp->mark_counter = 0;
		mrk |= TX_DESC_MARK;
		rp->mark_pending++;
	}

	tlen = len;
	nfg = skb_shinfo(skb)->nr_frags;
	while (tlen > 0) {
		tlen -= MAX_TX_DESC_LEN;
		nfg++;
	}

	while (len > 0) {
		unsigned int this_len = len;

		if (this_len > MAX_TX_DESC_LEN)
			this_len = MAX_TX_DESC_LEN;

		niu_set_txd(rp, prod, mapping, this_len, mrk, nfg);
		mrk = nfg = 0;

		prod = NEXT_TX(rp, prod);
		mapping += this_len;
		len -= this_len;
	}

	for (i = 0; i <  skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		len = frag->size;
		mapping = np->ops->map_page(np->device, frag->page,
					    frag->page_offset, len,
					    DMA_TO_DEVICE);

		rp->tx_buffs[prod].skb = NULL;
		rp->tx_buffs[prod].mapping = mapping;

		niu_set_txd(rp, prod, mapping, len, 0, 0);

		prod = NEXT_TX(rp, prod);
	}

	if (prod < rp->prod)
		rp->wrap_bit ^= TX_RING_KICK_WRAP;
	rp->prod = prod;

	nw64(TX_RING_KICK(rp->tx_channel), rp->wrap_bit | (prod << 3));

	if (unlikely(niu_tx_avail(rp) <= (MAX_SKB_FRAGS + 1))) {
		netif_tx_stop_queue(txq);
		if (niu_tx_avail(rp) > NIU_TX_WAKEUP_THRESH(rp))
			netif_tx_wake_queue(txq);
	}

	dev->trans_start = jiffies;

out:
	return NETDEV_TX_OK;

out_drop:
	rp->tx_errors++;
	kfree_skb(skb);
	goto out;
}

static int niu_change_mtu(struct net_device *dev, int new_mtu)
{
	struct niu *np = netdev_priv(dev);
	int err, orig_jumbo, new_jumbo;

	if (new_mtu < 68 || new_mtu > NIU_MAX_MTU)
		return -EINVAL;

	orig_jumbo = (dev->mtu > ETH_DATA_LEN);
	new_jumbo = (new_mtu > ETH_DATA_LEN);

	dev->mtu = new_mtu;

	if (!netif_running(dev) ||
	    (orig_jumbo == new_jumbo))
		return 0;

	niu_full_shutdown(np, dev);

	niu_free_channels(np);

	niu_enable_napi(np);

	err = niu_alloc_channels(np);
	if (err)
		return err;

	spin_lock_irq(&np->lock);

	err = niu_init_hw(np);
	if (!err) {
		init_timer(&np->timer);
		np->timer.expires = jiffies + HZ;
		np->timer.data = (unsigned long) np;
		np->timer.function = niu_timer;

		err = niu_enable_interrupts(np, 1);
		if (err)
			niu_stop_hw(np);
	}

	spin_unlock_irq(&np->lock);

	if (!err) {
		netif_tx_start_all_queues(dev);
		if (np->link_config.loopback_mode != LOOPBACK_DISABLED)
			netif_carrier_on(dev);

		add_timer(&np->timer);
	}

	return err;
}

static void niu_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	struct niu *np = netdev_priv(dev);
	struct niu_vpd *vpd = &np->vpd;

	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
	sprintf(info->fw_version, "%d.%d",
		vpd->fcode_major, vpd->fcode_minor);
	if (np->parent->plat_type != PLAT_TYPE_NIU)
		strcpy(info->bus_info, pci_name(np->pdev));
}

static int niu_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct niu *np = netdev_priv(dev);
	struct niu_link_config *lp;

	lp = &np->link_config;

	memset(cmd, 0, sizeof(*cmd));
	cmd->phy_address = np->phy_addr;
	cmd->supported = lp->supported;
	cmd->advertising = lp->active_advertising;
	cmd->autoneg = lp->active_autoneg;
	cmd->speed = lp->active_speed;
	cmd->duplex = lp->active_duplex;
	cmd->port = (np->flags & NIU_FLAGS_FIBER) ? PORT_FIBRE : PORT_TP;
	cmd->transceiver = (np->flags & NIU_FLAGS_XCVR_SERDES) ?
		XCVR_EXTERNAL : XCVR_INTERNAL;

	return 0;
}

static int niu_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct niu *np = netdev_priv(dev);
	struct niu_link_config *lp = &np->link_config;

	lp->advertising = cmd->advertising;
	lp->speed = cmd->speed;
	lp->duplex = cmd->duplex;
	lp->autoneg = cmd->autoneg;
	return niu_init_link(np);
}

static u32 niu_get_msglevel(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);
	return np->msg_enable;
}

static void niu_set_msglevel(struct net_device *dev, u32 value)
{
	struct niu *np = netdev_priv(dev);
	np->msg_enable = value;
}

static int niu_nway_reset(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);

	if (np->link_config.autoneg)
		return niu_init_link(np);

	return 0;
}

static int niu_get_eeprom_len(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);

	return np->eeprom_len;
}

static int niu_get_eeprom(struct net_device *dev,
			  struct ethtool_eeprom *eeprom, u8 *data)
{
	struct niu *np = netdev_priv(dev);
	u32 offset, len, val;

	offset = eeprom->offset;
	len = eeprom->len;

	if (offset + len < offset)
		return -EINVAL;
	if (offset >= np->eeprom_len)
		return -EINVAL;
	if (offset + len > np->eeprom_len)
		len = eeprom->len = np->eeprom_len - offset;

	if (offset & 3) {
		u32 b_offset, b_count;

		b_offset = offset & 3;
		b_count = 4 - b_offset;
		if (b_count > len)
			b_count = len;

		val = nr64(ESPC_NCR((offset - b_offset) / 4));
		memcpy(data, ((char *)&val) + b_offset, b_count);
		data += b_count;
		len -= b_count;
		offset += b_count;
	}
	while (len >= 4) {
		val = nr64(ESPC_NCR(offset / 4));
		memcpy(data, &val, 4);
		data += 4;
		len -= 4;
		offset += 4;
	}
	if (len) {
		val = nr64(ESPC_NCR(offset / 4));
		memcpy(data, &val, len);
	}
	return 0;
}

static void niu_ethflow_to_l3proto(int flow_type, u8 *pid)
{
	switch (flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		*pid = IPPROTO_TCP;
		break;
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		*pid = IPPROTO_UDP;
		break;
	case SCTP_V4_FLOW:
	case SCTP_V6_FLOW:
		*pid = IPPROTO_SCTP;
		break;
	case AH_V4_FLOW:
	case AH_V6_FLOW:
		*pid = IPPROTO_AH;
		break;
	case ESP_V4_FLOW:
	case ESP_V6_FLOW:
		*pid = IPPROTO_ESP;
		break;
	default:
		*pid = 0;
		break;
	}
}

static int niu_class_to_ethflow(u64 class, int *flow_type)
{
	switch (class) {
	case CLASS_CODE_TCP_IPV4:
		*flow_type = TCP_V4_FLOW;
		break;
	case CLASS_CODE_UDP_IPV4:
		*flow_type = UDP_V4_FLOW;
		break;
	case CLASS_CODE_AH_ESP_IPV4:
		*flow_type = AH_V4_FLOW;
		break;
	case CLASS_CODE_SCTP_IPV4:
		*flow_type = SCTP_V4_FLOW;
		break;
	case CLASS_CODE_TCP_IPV6:
		*flow_type = TCP_V6_FLOW;
		break;
	case CLASS_CODE_UDP_IPV6:
		*flow_type = UDP_V6_FLOW;
		break;
	case CLASS_CODE_AH_ESP_IPV6:
		*flow_type = AH_V6_FLOW;
		break;
	case CLASS_CODE_SCTP_IPV6:
		*flow_type = SCTP_V6_FLOW;
		break;
	case CLASS_CODE_USER_PROG1:
	case CLASS_CODE_USER_PROG2:
	case CLASS_CODE_USER_PROG3:
	case CLASS_CODE_USER_PROG4:
		*flow_type = IP_USER_FLOW;
		break;
	default:
		return 0;
	}

	return 1;
}

static int niu_ethflow_to_class(int flow_type, u64 *class)
{
	switch (flow_type) {
	case TCP_V4_FLOW:
		*class = CLASS_CODE_TCP_IPV4;
		break;
	case UDP_V4_FLOW:
		*class = CLASS_CODE_UDP_IPV4;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		*class = CLASS_CODE_AH_ESP_IPV4;
		break;
	case SCTP_V4_FLOW:
		*class = CLASS_CODE_SCTP_IPV4;
		break;
	case TCP_V6_FLOW:
		*class = CLASS_CODE_TCP_IPV6;
		break;
	case UDP_V6_FLOW:
		*class = CLASS_CODE_UDP_IPV6;
		break;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		*class = CLASS_CODE_AH_ESP_IPV6;
		break;
	case SCTP_V6_FLOW:
		*class = CLASS_CODE_SCTP_IPV6;
		break;
	default:
		return 0;
	}

	return 1;
}

static u64 niu_flowkey_to_ethflow(u64 flow_key)
{
	u64 ethflow = 0;

	if (flow_key & FLOW_KEY_L2DA)
		ethflow |= RXH_L2DA;
	if (flow_key & FLOW_KEY_VLAN)
		ethflow |= RXH_VLAN;
	if (flow_key & FLOW_KEY_IPSA)
		ethflow |= RXH_IP_SRC;
	if (flow_key & FLOW_KEY_IPDA)
		ethflow |= RXH_IP_DST;
	if (flow_key & FLOW_KEY_PROTO)
		ethflow |= RXH_L3_PROTO;
	if (flow_key & (FLOW_KEY_L4_BYTE12 << FLOW_KEY_L4_0_SHIFT))
		ethflow |= RXH_L4_B_0_1;
	if (flow_key & (FLOW_KEY_L4_BYTE12 << FLOW_KEY_L4_1_SHIFT))
		ethflow |= RXH_L4_B_2_3;

	return ethflow;

}

static int niu_ethflow_to_flowkey(u64 ethflow, u64 *flow_key)
{
	u64 key = 0;

	if (ethflow & RXH_L2DA)
		key |= FLOW_KEY_L2DA;
	if (ethflow & RXH_VLAN)
		key |= FLOW_KEY_VLAN;
	if (ethflow & RXH_IP_SRC)
		key |= FLOW_KEY_IPSA;
	if (ethflow & RXH_IP_DST)
		key |= FLOW_KEY_IPDA;
	if (ethflow & RXH_L3_PROTO)
		key |= FLOW_KEY_PROTO;
	if (ethflow & RXH_L4_B_0_1)
		key |= (FLOW_KEY_L4_BYTE12 << FLOW_KEY_L4_0_SHIFT);
	if (ethflow & RXH_L4_B_2_3)
		key |= (FLOW_KEY_L4_BYTE12 << FLOW_KEY_L4_1_SHIFT);

	*flow_key = key;

	return 1;

}

static int niu_get_hash_opts(struct niu *np, struct ethtool_rxnfc *nfc)
{
	u64 class;

	nfc->data = 0;

	if (!niu_ethflow_to_class(nfc->flow_type, &class))
		return -EINVAL;

	if (np->parent->tcam_key[class - CLASS_CODE_USER_PROG1] &
	    TCAM_KEY_DISC)
		nfc->data = RXH_DISCARD;
	else
		nfc->data = niu_flowkey_to_ethflow(np->parent->flow_key[class -
						      CLASS_CODE_USER_PROG1]);
	return 0;
}

static void niu_get_ip4fs_from_tcam_key(struct niu_tcam_entry *tp,
					struct ethtool_rx_flow_spec *fsp)
{

	fsp->h_u.tcp_ip4_spec.ip4src = (tp->key[3] & TCAM_V4KEY3_SADDR) >>
		TCAM_V4KEY3_SADDR_SHIFT;
	fsp->h_u.tcp_ip4_spec.ip4dst = (tp->key[3] & TCAM_V4KEY3_DADDR) >>
		TCAM_V4KEY3_DADDR_SHIFT;
	fsp->m_u.tcp_ip4_spec.ip4src = (tp->key_mask[3] & TCAM_V4KEY3_SADDR) >>
		TCAM_V4KEY3_SADDR_SHIFT;
	fsp->m_u.tcp_ip4_spec.ip4dst = (tp->key_mask[3] & TCAM_V4KEY3_DADDR) >>
		TCAM_V4KEY3_DADDR_SHIFT;

	fsp->h_u.tcp_ip4_spec.ip4src =
		cpu_to_be32(fsp->h_u.tcp_ip4_spec.ip4src);
	fsp->m_u.tcp_ip4_spec.ip4src =
		cpu_to_be32(fsp->m_u.tcp_ip4_spec.ip4src);
	fsp->h_u.tcp_ip4_spec.ip4dst =
		cpu_to_be32(fsp->h_u.tcp_ip4_spec.ip4dst);
	fsp->m_u.tcp_ip4_spec.ip4dst =
		cpu_to_be32(fsp->m_u.tcp_ip4_spec.ip4dst);

	fsp->h_u.tcp_ip4_spec.tos = (tp->key[2] & TCAM_V4KEY2_TOS) >>
		TCAM_V4KEY2_TOS_SHIFT;
	fsp->m_u.tcp_ip4_spec.tos = (tp->key_mask[2] & TCAM_V4KEY2_TOS) >>
		TCAM_V4KEY2_TOS_SHIFT;

	switch (fsp->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		fsp->h_u.tcp_ip4_spec.psrc =
			((tp->key[2] & TCAM_V4KEY2_PORT_SPI) >>
			 TCAM_V4KEY2_PORT_SPI_SHIFT) >> 16;
		fsp->h_u.tcp_ip4_spec.pdst =
			((tp->key[2] & TCAM_V4KEY2_PORT_SPI) >>
			 TCAM_V4KEY2_PORT_SPI_SHIFT) & 0xffff;
		fsp->m_u.tcp_ip4_spec.psrc =
			((tp->key_mask[2] & TCAM_V4KEY2_PORT_SPI) >>
			 TCAM_V4KEY2_PORT_SPI_SHIFT) >> 16;
		fsp->m_u.tcp_ip4_spec.pdst =
			((tp->key_mask[2] & TCAM_V4KEY2_PORT_SPI) >>
			 TCAM_V4KEY2_PORT_SPI_SHIFT) & 0xffff;

		fsp->h_u.tcp_ip4_spec.psrc =
			cpu_to_be16(fsp->h_u.tcp_ip4_spec.psrc);
		fsp->h_u.tcp_ip4_spec.pdst =
			cpu_to_be16(fsp->h_u.tcp_ip4_spec.pdst);
		fsp->m_u.tcp_ip4_spec.psrc =
			cpu_to_be16(fsp->m_u.tcp_ip4_spec.psrc);
		fsp->m_u.tcp_ip4_spec.pdst =
			cpu_to_be16(fsp->m_u.tcp_ip4_spec.pdst);
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		fsp->h_u.ah_ip4_spec.spi =
			(tp->key[2] & TCAM_V4KEY2_PORT_SPI) >>
			TCAM_V4KEY2_PORT_SPI_SHIFT;
		fsp->m_u.ah_ip4_spec.spi =
			(tp->key_mask[2] & TCAM_V4KEY2_PORT_SPI) >>
			TCAM_V4KEY2_PORT_SPI_SHIFT;

		fsp->h_u.ah_ip4_spec.spi =
			cpu_to_be32(fsp->h_u.ah_ip4_spec.spi);
		fsp->m_u.ah_ip4_spec.spi =
			cpu_to_be32(fsp->m_u.ah_ip4_spec.spi);
		break;
	case IP_USER_FLOW:
		fsp->h_u.usr_ip4_spec.l4_4_bytes =
			(tp->key[2] & TCAM_V4KEY2_PORT_SPI) >>
			TCAM_V4KEY2_PORT_SPI_SHIFT;
		fsp->m_u.usr_ip4_spec.l4_4_bytes =
			(tp->key_mask[2] & TCAM_V4KEY2_PORT_SPI) >>
			TCAM_V4KEY2_PORT_SPI_SHIFT;

		fsp->h_u.usr_ip4_spec.l4_4_bytes =
			cpu_to_be32(fsp->h_u.usr_ip4_spec.l4_4_bytes);
		fsp->m_u.usr_ip4_spec.l4_4_bytes =
			cpu_to_be32(fsp->m_u.usr_ip4_spec.l4_4_bytes);

		fsp->h_u.usr_ip4_spec.proto =
			(tp->key[2] & TCAM_V4KEY2_PROTO) >>
			TCAM_V4KEY2_PROTO_SHIFT;
		fsp->m_u.usr_ip4_spec.proto =
			(tp->key_mask[2] & TCAM_V4KEY2_PROTO) >>
			TCAM_V4KEY2_PROTO_SHIFT;

		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		break;
	default:
		break;
	}
}

static int niu_get_ethtool_tcam_entry(struct niu *np,
				      struct ethtool_rxnfc *nfc)
{
	struct niu_parent *parent = np->parent;
	struct niu_tcam_entry *tp;
	struct ethtool_rx_flow_spec *fsp = &nfc->fs;
	u16 idx;
	u64 class;
	int ret = 0;

	idx = tcam_get_index(np, (u16)nfc->fs.location);

	tp = &parent->tcam[idx];
	if (!tp->valid) {
		pr_info(PFX "niu%d: %s entry [%d] invalid for idx[%d]\n",
		parent->index, np->dev->name, (u16)nfc->fs.location, idx);
		return -EINVAL;
	}

	/* fill the flow spec entry */
	class = (tp->key[0] & TCAM_V4KEY0_CLASS_CODE) >>
		TCAM_V4KEY0_CLASS_CODE_SHIFT;
	ret = niu_class_to_ethflow(class, &fsp->flow_type);

	if (ret < 0) {
		pr_info(PFX "niu%d: %s niu_class_to_ethflow failed\n",
		parent->index, np->dev->name);
		ret = -EINVAL;
		goto out;
	}

	if (fsp->flow_type == AH_V4_FLOW || fsp->flow_type == AH_V6_FLOW) {
		u32 proto = (tp->key[2] & TCAM_V4KEY2_PROTO) >>
			TCAM_V4KEY2_PROTO_SHIFT;
		if (proto == IPPROTO_ESP) {
			if (fsp->flow_type == AH_V4_FLOW)
				fsp->flow_type = ESP_V4_FLOW;
			else
				fsp->flow_type = ESP_V6_FLOW;
		}
	}

	switch (fsp->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		niu_get_ip4fs_from_tcam_key(tp, fsp);
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		/* Not yet implemented */
		ret = -EINVAL;
		break;
	case IP_USER_FLOW:
		niu_get_ip4fs_from_tcam_key(tp, fsp);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret < 0)
		goto out;

	if (tp->assoc_data & TCAM_ASSOCDATA_DISC)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = (tp->assoc_data & TCAM_ASSOCDATA_OFFSET) >>
			TCAM_ASSOCDATA_OFFSET_SHIFT;

	/* put the tcam size here */
	nfc->data = tcam_get_size(np);
out:
	return ret;
}

static int niu_get_ethtool_tcam_all(struct niu *np,
				    struct ethtool_rxnfc *nfc,
				    u32 *rule_locs)
{
	struct niu_parent *parent = np->parent;
	struct niu_tcam_entry *tp;
	int i, idx, cnt;
	u16 n_entries;
	unsigned long flags;


	/* put the tcam size here */
	nfc->data = tcam_get_size(np);

	niu_lock_parent(np, flags);
	n_entries = nfc->rule_cnt;
	for (cnt = 0, i = 0; i < nfc->data; i++) {
		idx = tcam_get_index(np, i);
		tp = &parent->tcam[idx];
		if (!tp->valid)
			continue;
		rule_locs[cnt] = i;
		cnt++;
	}
	niu_unlock_parent(np, flags);

	if (n_entries != cnt) {
		/* print warning, this should not happen */
		pr_info(PFX "niu%d: %s In niu_get_ethtool_tcam_all, "
			"n_entries[%d] != cnt[%d]!!!\n\n",
			np->parent->index, np->dev->name, n_entries, cnt);
	}

	return 0;
}

static int niu_get_nfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
		       void *rule_locs)
{
	struct niu *np = netdev_priv(dev);
	int ret = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXFH:
		ret = niu_get_hash_opts(np, cmd);
		break;
	case ETHTOOL_GRXRINGS:
		cmd->data = np->num_rx_rings;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = tcam_get_valid_entry_cnt(np);
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = niu_get_ethtool_tcam_entry(np, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = niu_get_ethtool_tcam_all(np, cmd, (u32 *)rule_locs);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int niu_set_hash_opts(struct niu *np, struct ethtool_rxnfc *nfc)
{
	u64 class;
	u64 flow_key = 0;
	unsigned long flags;

	if (!niu_ethflow_to_class(nfc->flow_type, &class))
		return -EINVAL;

	if (class < CLASS_CODE_USER_PROG1 ||
	    class > CLASS_CODE_SCTP_IPV6)
		return -EINVAL;

	if (nfc->data & RXH_DISCARD) {
		niu_lock_parent(np, flags);
		flow_key = np->parent->tcam_key[class -
					       CLASS_CODE_USER_PROG1];
		flow_key |= TCAM_KEY_DISC;
		nw64(TCAM_KEY(class - CLASS_CODE_USER_PROG1), flow_key);
		np->parent->tcam_key[class - CLASS_CODE_USER_PROG1] = flow_key;
		niu_unlock_parent(np, flags);
		return 0;
	} else {
		/* Discard was set before, but is not set now */
		if (np->parent->tcam_key[class - CLASS_CODE_USER_PROG1] &
		    TCAM_KEY_DISC) {
			niu_lock_parent(np, flags);
			flow_key = np->parent->tcam_key[class -
					       CLASS_CODE_USER_PROG1];
			flow_key &= ~TCAM_KEY_DISC;
			nw64(TCAM_KEY(class - CLASS_CODE_USER_PROG1),
			     flow_key);
			np->parent->tcam_key[class - CLASS_CODE_USER_PROG1] =
				flow_key;
			niu_unlock_parent(np, flags);
		}
	}

	if (!niu_ethflow_to_flowkey(nfc->data, &flow_key))
		return -EINVAL;

	niu_lock_parent(np, flags);
	nw64(FLOW_KEY(class - CLASS_CODE_USER_PROG1), flow_key);
	np->parent->flow_key[class - CLASS_CODE_USER_PROG1] = flow_key;
	niu_unlock_parent(np, flags);

	return 0;
}

static void niu_get_tcamkey_from_ip4fs(struct ethtool_rx_flow_spec *fsp,
				       struct niu_tcam_entry *tp,
				       int l2_rdc_tab, u64 class)
{
	u8 pid = 0;
	u32 sip, dip, sipm, dipm, spi, spim;
	u16 sport, dport, spm, dpm;

	sip = be32_to_cpu(fsp->h_u.tcp_ip4_spec.ip4src);
	sipm = be32_to_cpu(fsp->m_u.tcp_ip4_spec.ip4src);
	dip = be32_to_cpu(fsp->h_u.tcp_ip4_spec.ip4dst);
	dipm = be32_to_cpu(fsp->m_u.tcp_ip4_spec.ip4dst);

	tp->key[0] = class << TCAM_V4KEY0_CLASS_CODE_SHIFT;
	tp->key_mask[0] = TCAM_V4KEY0_CLASS_CODE;
	tp->key[1] = (u64)l2_rdc_tab << TCAM_V4KEY1_L2RDCNUM_SHIFT;
	tp->key_mask[1] = TCAM_V4KEY1_L2RDCNUM;

	tp->key[3] = (u64)sip << TCAM_V4KEY3_SADDR_SHIFT;
	tp->key[3] |= dip;

	tp->key_mask[3] = (u64)sipm << TCAM_V4KEY3_SADDR_SHIFT;
	tp->key_mask[3] |= dipm;

	tp->key[2] |= ((u64)fsp->h_u.tcp_ip4_spec.tos <<
		       TCAM_V4KEY2_TOS_SHIFT);
	tp->key_mask[2] |= ((u64)fsp->m_u.tcp_ip4_spec.tos <<
			    TCAM_V4KEY2_TOS_SHIFT);
	switch (fsp->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		sport = be16_to_cpu(fsp->h_u.tcp_ip4_spec.psrc);
		spm = be16_to_cpu(fsp->m_u.tcp_ip4_spec.psrc);
		dport = be16_to_cpu(fsp->h_u.tcp_ip4_spec.pdst);
		dpm = be16_to_cpu(fsp->m_u.tcp_ip4_spec.pdst);

		tp->key[2] |= (((u64)sport << 16) | dport);
		tp->key_mask[2] |= (((u64)spm << 16) | dpm);
		niu_ethflow_to_l3proto(fsp->flow_type, &pid);
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		spi = be32_to_cpu(fsp->h_u.ah_ip4_spec.spi);
		spim = be32_to_cpu(fsp->m_u.ah_ip4_spec.spi);

		tp->key[2] |= spi;
		tp->key_mask[2] |= spim;
		niu_ethflow_to_l3proto(fsp->flow_type, &pid);
		break;
	case IP_USER_FLOW:
		spi = be32_to_cpu(fsp->h_u.usr_ip4_spec.l4_4_bytes);
		spim = be32_to_cpu(fsp->m_u.usr_ip4_spec.l4_4_bytes);

		tp->key[2] |= spi;
		tp->key_mask[2] |= spim;
		pid = fsp->h_u.usr_ip4_spec.proto;
		break;
	default:
		break;
	}

	tp->key[2] |= ((u64)pid << TCAM_V4KEY2_PROTO_SHIFT);
	if (pid) {
		tp->key_mask[2] |= TCAM_V4KEY2_PROTO;
	}
}

static int niu_add_ethtool_tcam_entry(struct niu *np,
				      struct ethtool_rxnfc *nfc)
{
	struct niu_parent *parent = np->parent;
	struct niu_tcam_entry *tp;
	struct ethtool_rx_flow_spec *fsp = &nfc->fs;
	struct niu_rdc_tables *rdc_table = &parent->rdc_group_cfg[np->port];
	int l2_rdc_table = rdc_table->first_table_num;
	u16 idx;
	u64 class;
	unsigned long flags;
	int err, ret;

	ret = 0;

	idx = nfc->fs.location;
	if (idx >= tcam_get_size(np))
		return -EINVAL;

	if (fsp->flow_type == IP_USER_FLOW) {
		int i;
		int add_usr_cls = 0;
		int ipv6 = 0;
		struct ethtool_usrip4_spec *uspec = &fsp->h_u.usr_ip4_spec;
		struct ethtool_usrip4_spec *umask = &fsp->m_u.usr_ip4_spec;

		niu_lock_parent(np, flags);

		for (i = 0; i < NIU_L3_PROG_CLS; i++) {
			if (parent->l3_cls[i]) {
				if (uspec->proto == parent->l3_cls_pid[i]) {
					class = parent->l3_cls[i];
					parent->l3_cls_refcnt[i]++;
					add_usr_cls = 1;
					break;
				}
			} else {
				/* Program new user IP class */
				switch (i) {
				case 0:
					class = CLASS_CODE_USER_PROG1;
					break;
				case 1:
					class = CLASS_CODE_USER_PROG2;
					break;
				case 2:
					class = CLASS_CODE_USER_PROG3;
					break;
				case 3:
					class = CLASS_CODE_USER_PROG4;
					break;
				default:
					break;
				}
				if (uspec->ip_ver == ETH_RX_NFC_IP6)
					ipv6 = 1;
				ret = tcam_user_ip_class_set(np, class, ipv6,
							     uspec->proto,
							     uspec->tos,
							     umask->tos);
				if (ret)
					goto out;

				ret = tcam_user_ip_class_enable(np, class, 1);
				if (ret)
					goto out;
				parent->l3_cls[i] = class;
				parent->l3_cls_pid[i] = uspec->proto;
				parent->l3_cls_refcnt[i]++;
				add_usr_cls = 1;
				break;
			}
		}
		if (!add_usr_cls) {
			pr_info(PFX "niu%d: %s niu_add_ethtool_tcam_entry: "
				"Could not find/insert class for pid %d\n",
				parent->index, np->dev->name, uspec->proto);
			ret = -EINVAL;
			goto out;
		}
		niu_unlock_parent(np, flags);
	} else {
		if (!niu_ethflow_to_class(fsp->flow_type, &class)) {
			return -EINVAL;
		}
	}

	niu_lock_parent(np, flags);

	idx = tcam_get_index(np, idx);
	tp = &parent->tcam[idx];

	memset(tp, 0, sizeof(*tp));

	/* fill in the tcam key and mask */
	switch (fsp->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		niu_get_tcamkey_from_ip4fs(fsp, tp, l2_rdc_table, class);
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		/* Not yet implemented */
		pr_info(PFX "niu%d: %s In niu_add_ethtool_tcam_entry: "
			"flow %d for IPv6 not implemented\n\n",
			parent->index, np->dev->name, fsp->flow_type);
		ret = -EINVAL;
		goto out;
	case IP_USER_FLOW:
		if (fsp->h_u.usr_ip4_spec.ip_ver == ETH_RX_NFC_IP4) {
			niu_get_tcamkey_from_ip4fs(fsp, tp, l2_rdc_table,
						   class);
		} else {
			/* Not yet implemented */
			pr_info(PFX "niu%d: %s In niu_add_ethtool_tcam_entry: "
			"usr flow for IPv6 not implemented\n\n",
			parent->index, np->dev->name);
			ret = -EINVAL;
			goto out;
		}
		break;
	default:
		pr_info(PFX "niu%d: %s In niu_add_ethtool_tcam_entry: "
			"Unknown flow type %d\n\n",
			parent->index, np->dev->name, fsp->flow_type);
		ret = -EINVAL;
		goto out;
	}

	/* fill in the assoc data */
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC) {
		tp->assoc_data = TCAM_ASSOCDATA_DISC;
	} else {
		if (fsp->ring_cookie >= np->num_rx_rings) {
			pr_info(PFX "niu%d: %s In niu_add_ethtool_tcam_entry: "
				"Invalid RX ring %lld\n\n",
				parent->index, np->dev->name,
				(long long) fsp->ring_cookie);
			ret = -EINVAL;
			goto out;
		}
		tp->assoc_data = (TCAM_ASSOCDATA_TRES_USE_OFFSET |
				  (fsp->ring_cookie <<
				   TCAM_ASSOCDATA_OFFSET_SHIFT));
	}

	err = tcam_write(np, idx, tp->key, tp->key_mask);
	if (err) {
		ret = -EINVAL;
		goto out;
	}
	err = tcam_assoc_write(np, idx, tp->assoc_data);
	if (err) {
		ret = -EINVAL;
		goto out;
	}

	/* validate the entry */
	tp->valid = 1;
	np->clas.tcam_valid_entries++;
out:
	niu_unlock_parent(np, flags);

	return ret;
}

static int niu_del_ethtool_tcam_entry(struct niu *np, u32 loc)
{
	struct niu_parent *parent = np->parent;
	struct niu_tcam_entry *tp;
	u16 idx;
	unsigned long flags;
	u64 class;
	int ret = 0;

	if (loc >= tcam_get_size(np))
		return -EINVAL;

	niu_lock_parent(np, flags);

	idx = tcam_get_index(np, loc);
	tp = &parent->tcam[idx];

	/* if the entry is of a user defined class, then update*/
	class = (tp->key[0] & TCAM_V4KEY0_CLASS_CODE) >>
		TCAM_V4KEY0_CLASS_CODE_SHIFT;

	if (class >= CLASS_CODE_USER_PROG1 && class <= CLASS_CODE_USER_PROG4) {
		int i;
		for (i = 0; i < NIU_L3_PROG_CLS; i++) {
			if (parent->l3_cls[i] == class) {
				parent->l3_cls_refcnt[i]--;
				if (!parent->l3_cls_refcnt[i]) {
					/* disable class */
					ret = tcam_user_ip_class_enable(np,
									class,
									0);
					if (ret)
						goto out;
					parent->l3_cls[i] = 0;
					parent->l3_cls_pid[i] = 0;
				}
				break;
			}
		}
		if (i == NIU_L3_PROG_CLS) {
			pr_info(PFX "niu%d: %s In niu_del_ethtool_tcam_entry,"
				"Usr class 0x%llx not found \n",
				parent->index, np->dev->name,
				(unsigned long long) class);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = tcam_flush(np, idx);
	if (ret)
		goto out;

	/* invalidate the entry */
	tp->valid = 0;
	np->clas.tcam_valid_entries--;
out:
	niu_unlock_parent(np, flags);

	return ret;
}

static int niu_set_nfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct niu *np = netdev_priv(dev);
	int ret = 0;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = niu_set_hash_opts(np, cmd);
		break;
	case ETHTOOL_SRXCLSRLINS:
		ret = niu_add_ethtool_tcam_entry(np, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = niu_del_ethtool_tcam_entry(np, cmd->fs.location);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct {
	const char string[ETH_GSTRING_LEN];
} niu_xmac_stat_keys[] = {
	{ "tx_frames" },
	{ "tx_bytes" },
	{ "tx_fifo_errors" },
	{ "tx_overflow_errors" },
	{ "tx_max_pkt_size_errors" },
	{ "tx_underflow_errors" },
	{ "rx_local_faults" },
	{ "rx_remote_faults" },
	{ "rx_link_faults" },
	{ "rx_align_errors" },
	{ "rx_frags" },
	{ "rx_mcasts" },
	{ "rx_bcasts" },
	{ "rx_hist_cnt1" },
	{ "rx_hist_cnt2" },
	{ "rx_hist_cnt3" },
	{ "rx_hist_cnt4" },
	{ "rx_hist_cnt5" },
	{ "rx_hist_cnt6" },
	{ "rx_hist_cnt7" },
	{ "rx_octets" },
	{ "rx_code_violations" },
	{ "rx_len_errors" },
	{ "rx_crc_errors" },
	{ "rx_underflows" },
	{ "rx_overflows" },
	{ "pause_off_state" },
	{ "pause_on_state" },
	{ "pause_received" },
};

#define NUM_XMAC_STAT_KEYS	ARRAY_SIZE(niu_xmac_stat_keys)

static const struct {
	const char string[ETH_GSTRING_LEN];
} niu_bmac_stat_keys[] = {
	{ "tx_underflow_errors" },
	{ "tx_max_pkt_size_errors" },
	{ "tx_bytes" },
	{ "tx_frames" },
	{ "rx_overflows" },
	{ "rx_frames" },
	{ "rx_align_errors" },
	{ "rx_crc_errors" },
	{ "rx_len_errors" },
	{ "pause_off_state" },
	{ "pause_on_state" },
	{ "pause_received" },
};

#define NUM_BMAC_STAT_KEYS	ARRAY_SIZE(niu_bmac_stat_keys)

static const struct {
	const char string[ETH_GSTRING_LEN];
} niu_rxchan_stat_keys[] = {
	{ "rx_channel" },
	{ "rx_packets" },
	{ "rx_bytes" },
	{ "rx_dropped" },
	{ "rx_errors" },
};

#define NUM_RXCHAN_STAT_KEYS	ARRAY_SIZE(niu_rxchan_stat_keys)

static const struct {
	const char string[ETH_GSTRING_LEN];
} niu_txchan_stat_keys[] = {
	{ "tx_channel" },
	{ "tx_packets" },
	{ "tx_bytes" },
	{ "tx_errors" },
};

#define NUM_TXCHAN_STAT_KEYS	ARRAY_SIZE(niu_txchan_stat_keys)

static void niu_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct niu *np = netdev_priv(dev);
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	if (np->flags & NIU_FLAGS_XMAC) {
		memcpy(data, niu_xmac_stat_keys,
		       sizeof(niu_xmac_stat_keys));
		data += sizeof(niu_xmac_stat_keys);
	} else {
		memcpy(data, niu_bmac_stat_keys,
		       sizeof(niu_bmac_stat_keys));
		data += sizeof(niu_bmac_stat_keys);
	}
	for (i = 0; i < np->num_rx_rings; i++) {
		memcpy(data, niu_rxchan_stat_keys,
		       sizeof(niu_rxchan_stat_keys));
		data += sizeof(niu_rxchan_stat_keys);
	}
	for (i = 0; i < np->num_tx_rings; i++) {
		memcpy(data, niu_txchan_stat_keys,
		       sizeof(niu_txchan_stat_keys));
		data += sizeof(niu_txchan_stat_keys);
	}
}

static int niu_get_stats_count(struct net_device *dev)
{
	struct niu *np = netdev_priv(dev);

	return ((np->flags & NIU_FLAGS_XMAC ?
		 NUM_XMAC_STAT_KEYS :
		 NUM_BMAC_STAT_KEYS) +
		(np->num_rx_rings * NUM_RXCHAN_STAT_KEYS) +
		(np->num_tx_rings * NUM_TXCHAN_STAT_KEYS));
}

static void niu_get_ethtool_stats(struct net_device *dev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct niu *np = netdev_priv(dev);
	int i;

	niu_sync_mac_stats(np);
	if (np->flags & NIU_FLAGS_XMAC) {
		memcpy(data, &np->mac_stats.xmac,
		       sizeof(struct niu_xmac_stats));
		data += (sizeof(struct niu_xmac_stats) / sizeof(u64));
	} else {
		memcpy(data, &np->mac_stats.bmac,
		       sizeof(struct niu_bmac_stats));
		data += (sizeof(struct niu_bmac_stats) / sizeof(u64));
	}
	for (i = 0; i < np->num_rx_rings; i++) {
		struct rx_ring_info *rp = &np->rx_rings[i];

		niu_sync_rx_discard_stats(np, rp, 0);

		data[0] = rp->rx_channel;
		data[1] = rp->rx_packets;
		data[2] = rp->rx_bytes;
		data[3] = rp->rx_dropped;
		data[4] = rp->rx_errors;
		data += 5;
	}
	for (i = 0; i < np->num_tx_rings; i++) {
		struct tx_ring_info *rp = &np->tx_rings[i];

		data[0] = rp->tx_channel;
		data[1] = rp->tx_packets;
		data[2] = rp->tx_bytes;
		data[3] = rp->tx_errors;
		data += 4;
	}
}

static u64 niu_led_state_save(struct niu *np)
{
	if (np->flags & NIU_FLAGS_XMAC)
		return nr64_mac(XMAC_CONFIG);
	else
		return nr64_mac(BMAC_XIF_CONFIG);
}

static void niu_led_state_restore(struct niu *np, u64 val)
{
	if (np->flags & NIU_FLAGS_XMAC)
		nw64_mac(XMAC_CONFIG, val);
	else
		nw64_mac(BMAC_XIF_CONFIG, val);
}

static void niu_force_led(struct niu *np, int on)
{
	u64 val, reg, bit;

	if (np->flags & NIU_FLAGS_XMAC) {
		reg = XMAC_CONFIG;
		bit = XMAC_CONFIG_FORCE_LED_ON;
	} else {
		reg = BMAC_XIF_CONFIG;
		bit = BMAC_XIF_CONFIG_LINK_LED;
	}

	val = nr64_mac(reg);
	if (on)
		val |= bit;
	else
		val &= ~bit;
	nw64_mac(reg, val);
}

static int niu_phys_id(struct net_device *dev, u32 data)
{
	struct niu *np = netdev_priv(dev);
	u64 orig_led_state;
	int i;

	if (!netif_running(dev))
		return -EAGAIN;

	if (data == 0)
		data = 2;

	orig_led_state = niu_led_state_save(np);
	for (i = 0; i < (data * 2); i++) {
		int on = ((i % 2) == 0);

		niu_force_led(np, on);

		if (msleep_interruptible(500))
			break;
	}
	niu_led_state_restore(np, orig_led_state);

	return 0;
}

static const struct ethtool_ops niu_ethtool_ops = {
	.get_drvinfo		= niu_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= niu_get_msglevel,
	.set_msglevel		= niu_set_msglevel,
	.nway_reset		= niu_nway_reset,
	.get_eeprom_len		= niu_get_eeprom_len,
	.get_eeprom		= niu_get_eeprom,
	.get_settings		= niu_get_settings,
	.set_settings		= niu_set_settings,
	.get_strings		= niu_get_strings,
	.get_stats_count	= niu_get_stats_count,
	.get_ethtool_stats	= niu_get_ethtool_stats,
	.phys_id		= niu_phys_id,
	.get_rxnfc		= niu_get_nfc,
	.set_rxnfc		= niu_set_nfc,
};

static int niu_ldg_assign_ldn(struct niu *np, struct niu_parent *parent,
			      int ldg, int ldn)
{
	if (ldg < NIU_LDG_MIN || ldg > NIU_LDG_MAX)
		return -EINVAL;
	if (ldn < 0 || ldn > LDN_MAX)
		return -EINVAL;

	parent->ldg_map[ldn] = ldg;

	if (np->parent->plat_type == PLAT_TYPE_NIU) {
		/* On N2 NIU, the ldn-->ldg assignments are setup and fixed by
		 * the firmware, and we're not supposed to change them.
		 * Validate the mapping, because if it's wrong we probably
		 * won't get any interrupts and that's painful to debug.
		 */
		if (nr64(LDG_NUM(ldn)) != ldg) {
			dev_err(np->device, PFX "Port %u, mis-matched "
				"LDG assignment "
				"for ldn %d, should be %d is %llu\n",
				np->port, ldn, ldg,
				(unsigned long long) nr64(LDG_NUM(ldn)));
			return -EINVAL;
		}
	} else
		nw64(LDG_NUM(ldn), ldg);

	return 0;
}

static int niu_set_ldg_timer_res(struct niu *np, int res)
{
	if (res < 0 || res > LDG_TIMER_RES_VAL)
		return -EINVAL;


	nw64(LDG_TIMER_RES, res);

	return 0;
}

static int niu_set_ldg_sid(struct niu *np, int ldg, int func, int vector)
{
	if ((ldg < NIU_LDG_MIN || ldg > NIU_LDG_MAX) ||
	    (func < 0 || func > 3) ||
	    (vector < 0 || vector > 0x1f))
		return -EINVAL;

	nw64(SID(ldg), (func << SID_FUNC_SHIFT) | vector);

	return 0;
}

static int __devinit niu_pci_eeprom_read(struct niu *np, u32 addr)
{
	u64 frame, frame_base = (ESPC_PIO_STAT_READ_START |
				 (addr << ESPC_PIO_STAT_ADDR_SHIFT));
	int limit;

	if (addr > (ESPC_PIO_STAT_ADDR >> ESPC_PIO_STAT_ADDR_SHIFT))
		return -EINVAL;

	frame = frame_base;
	nw64(ESPC_PIO_STAT, frame);
	limit = 64;
	do {
		udelay(5);
		frame = nr64(ESPC_PIO_STAT);
		if (frame & ESPC_PIO_STAT_READ_END)
			break;
	} while (limit--);
	if (!(frame & ESPC_PIO_STAT_READ_END)) {
		dev_err(np->device, PFX "EEPROM read timeout frame[%llx]\n",
			(unsigned long long) frame);
		return -ENODEV;
	}

	frame = frame_base;
	nw64(ESPC_PIO_STAT, frame);
	limit = 64;
	do {
		udelay(5);
		frame = nr64(ESPC_PIO_STAT);
		if (frame & ESPC_PIO_STAT_READ_END)
			break;
	} while (limit--);
	if (!(frame & ESPC_PIO_STAT_READ_END)) {
		dev_err(np->device, PFX "EEPROM read timeout frame[%llx]\n",
			(unsigned long long) frame);
		return -ENODEV;
	}

	frame = nr64(ESPC_PIO_STAT);
	return (frame & ESPC_PIO_STAT_DATA) >> ESPC_PIO_STAT_DATA_SHIFT;
}

static int __devinit niu_pci_eeprom_read16(struct niu *np, u32 off)
{
	int err = niu_pci_eeprom_read(np, off);
	u16 val;

	if (err < 0)
		return err;
	val = (err << 8);
	err = niu_pci_eeprom_read(np, off + 1);
	if (err < 0)
		return err;
	val |= (err & 0xff);

	return val;
}

static int __devinit niu_pci_eeprom_read16_swp(struct niu *np, u32 off)
{
	int err = niu_pci_eeprom_read(np, off);
	u16 val;

	if (err < 0)
		return err;

	val = (err & 0xff);
	err = niu_pci_eeprom_read(np, off + 1);
	if (err < 0)
		return err;

	val |= (err & 0xff) << 8;

	return val;
}

static int __devinit niu_pci_vpd_get_propname(struct niu *np,
					      u32 off,
					      char *namebuf,
					      int namebuf_len)
{
	int i;

	for (i = 0; i < namebuf_len; i++) {
		int err = niu_pci_eeprom_read(np, off + i);
		if (err < 0)
			return err;
		*namebuf++ = err;
		if (!err)
			break;
	}
	if (i >= namebuf_len)
		return -EINVAL;

	return i + 1;
}

static void __devinit niu_vpd_parse_version(struct niu *np)
{
	struct niu_vpd *vpd = &np->vpd;
	int len = strlen(vpd->version) + 1;
	const char *s = vpd->version;
	int i;

	for (i = 0; i < len - 5; i++) {
		if (!strncmp(s + i, "FCode ", 5))
			break;
	}
	if (i >= len - 5)
		return;

	s += i + 5;
	sscanf(s, "%d.%d", &vpd->fcode_major, &vpd->fcode_minor);

	niudbg(PROBE, "VPD_SCAN: FCODE major(%d) minor(%d)\n",
	       vpd->fcode_major, vpd->fcode_minor);
	if (vpd->fcode_major > NIU_VPD_MIN_MAJOR ||
	    (vpd->fcode_major == NIU_VPD_MIN_MAJOR &&
	     vpd->fcode_minor >= NIU_VPD_MIN_MINOR))
		np->flags |= NIU_FLAGS_VPD_VALID;
}

/* ESPC_PIO_EN_ENABLE must be set */
static int __devinit niu_pci_vpd_scan_props(struct niu *np,
					    u32 start, u32 end)
{
	unsigned int found_mask = 0;
#define FOUND_MASK_MODEL	0x00000001
#define FOUND_MASK_BMODEL	0x00000002
#define FOUND_MASK_VERS		0x00000004
#define FOUND_MASK_MAC		0x00000008
#define FOUND_MASK_NMAC		0x00000010
#define FOUND_MASK_PHY		0x00000020
#define FOUND_MASK_ALL		0x0000003f

	niudbg(PROBE, "VPD_SCAN: start[%x] end[%x]\n",
	       start, end);
	while (start < end) {
		int len, err, instance, type, prop_len;
		char namebuf[64];
		u8 *prop_buf;
		int max_len;

		if (found_mask == FOUND_MASK_ALL) {
			niu_vpd_parse_version(np);
			return 1;
		}

		err = niu_pci_eeprom_read(np, start + 2);
		if (err < 0)
			return err;
		len = err;
		start += 3;

		instance = niu_pci_eeprom_read(np, start);
		type = niu_pci_eeprom_read(np, start + 3);
		prop_len = niu_pci_eeprom_read(np, start + 4);
		err = niu_pci_vpd_get_propname(np, start + 5, namebuf, 64);
		if (err < 0)
			return err;

		prop_buf = NULL;
		max_len = 0;
		if (!strcmp(namebuf, "model")) {
			prop_buf = np->vpd.model;
			max_len = NIU_VPD_MODEL_MAX;
			found_mask |= FOUND_MASK_MODEL;
		} else if (!strcmp(namebuf, "board-model")) {
			prop_buf = np->vpd.board_model;
			max_len = NIU_VPD_BD_MODEL_MAX;
			found_mask |= FOUND_MASK_BMODEL;
		} else if (!strcmp(namebuf, "version")) {
			prop_buf = np->vpd.version;
			max_len = NIU_VPD_VERSION_MAX;
			found_mask |= FOUND_MASK_VERS;
		} else if (!strcmp(namebuf, "local-mac-address")) {
			prop_buf = np->vpd.local_mac;
			max_len = ETH_ALEN;
			found_mask |= FOUND_MASK_MAC;
		} else if (!strcmp(namebuf, "num-mac-addresses")) {
			prop_buf = &np->vpd.mac_num;
			max_len = 1;
			found_mask |= FOUND_MASK_NMAC;
		} else if (!strcmp(namebuf, "phy-type")) {
			prop_buf = np->vpd.phy_type;
			max_len = NIU_VPD_PHY_TYPE_MAX;
			found_mask |= FOUND_MASK_PHY;
		}

		if (max_len && prop_len > max_len) {
			dev_err(np->device, PFX "Property '%s' length (%d) is "
				"too long.\n", namebuf, prop_len);
			return -EINVAL;
		}

		if (prop_buf) {
			u32 off = start + 5 + err;
			int i;

			niudbg(PROBE, "VPD_SCAN: Reading in property [%s] "
			       "len[%d]\n", namebuf, prop_len);
			for (i = 0; i < prop_len; i++)
				*prop_buf++ = niu_pci_eeprom_read(np, off + i);
		}

		start += len;
	}

	return 0;
}

/* ESPC_PIO_EN_ENABLE must be set */
static void __devinit niu_pci_vpd_fetch(struct niu *np, u32 start)
{
	u32 offset;
	int err;

	err = niu_pci_eeprom_read16_swp(np, start + 1);
	if (err < 0)
		return;

	offset = err + 3;

	while (start + offset < ESPC_EEPROM_SIZE) {
		u32 here = start + offset;
		u32 end;

		err = niu_pci_eeprom_read(np, here);
		if (err != 0x90)
			return;

		err = niu_pci_eeprom_read16_swp(np, here + 1);
		if (err < 0)
			return;

		here = start + offset + 3;
		end = start + offset + err;

		offset += err;

		err = niu_pci_vpd_scan_props(np, here, end);
		if (err < 0 || err == 1)
			return;
	}
}

/* ESPC_PIO_EN_ENABLE must be set */
static u32 __devinit niu_pci_vpd_offset(struct niu *np)
{
	u32 start = 0, end = ESPC_EEPROM_SIZE, ret;
	int err;

	while (start < end) {
		ret = start;

		/* ROM header signature?  */
		err = niu_pci_eeprom_read16(np, start +  0);
		if (err != 0x55aa)
			return 0;

		/* Apply offset to PCI data structure.  */
		err = niu_pci_eeprom_read16(np, start + 23);
		if (err < 0)
			return 0;
		start += err;

		/* Check for "PCIR" signature.  */
		err = niu_pci_eeprom_read16(np, start +  0);
		if (err != 0x5043)
			return 0;
		err = niu_pci_eeprom_read16(np, start +  2);
		if (err != 0x4952)
			return 0;

		/* Check for OBP image type.  */
		err = niu_pci_eeprom_read(np, start + 20);
		if (err < 0)
			return 0;
		if (err != 0x01) {
			err = niu_pci_eeprom_read(np, ret + 2);
			if (err < 0)
				return 0;

			start = ret + (err * 512);
			continue;
		}

		err = niu_pci_eeprom_read16_swp(np, start + 8);
		if (err < 0)
			return err;
		ret += err;

		err = niu_pci_eeprom_read(np, ret + 0);
		if (err != 0x82)
			return 0;

		return ret;
	}

	return 0;
}

static int __devinit niu_phy_type_prop_decode(struct niu *np,
					      const char *phy_prop)
{
	if (!strcmp(phy_prop, "mif")) {
		/* 1G copper, MII */
		np->flags &= ~(NIU_FLAGS_FIBER |
			       NIU_FLAGS_10G);
		np->mac_xcvr = MAC_XCVR_MII;
	} else if (!strcmp(phy_prop, "xgf")) {
		/* 10G fiber, XPCS */
		np->flags |= (NIU_FLAGS_10G |
			      NIU_FLAGS_FIBER);
		np->mac_xcvr = MAC_XCVR_XPCS;
	} else if (!strcmp(phy_prop, "pcs")) {
		/* 1G fiber, PCS */
		np->flags &= ~NIU_FLAGS_10G;
		np->flags |= NIU_FLAGS_FIBER;
		np->mac_xcvr = MAC_XCVR_PCS;
	} else if (!strcmp(phy_prop, "xgc")) {
		/* 10G copper, XPCS */
		np->flags |= NIU_FLAGS_10G;
		np->flags &= ~NIU_FLAGS_FIBER;
		np->mac_xcvr = MAC_XCVR_XPCS;
	} else if (!strcmp(phy_prop, "xgsd") || !strcmp(phy_prop, "gsd")) {
		/* 10G Serdes or 1G Serdes, default to 10G */
		np->flags |= NIU_FLAGS_10G;
		np->flags &= ~NIU_FLAGS_FIBER;
		np->flags |= NIU_FLAGS_XCVR_SERDES;
		np->mac_xcvr = MAC_XCVR_XPCS;
	} else {
		return -EINVAL;
	}
	return 0;
}

static int niu_pci_vpd_get_nports(struct niu *np)
{
	int ports = 0;

	if ((!strcmp(np->vpd.model, NIU_QGC_LP_MDL_STR)) ||
	    (!strcmp(np->vpd.model, NIU_QGC_PEM_MDL_STR)) ||
	    (!strcmp(np->vpd.model, NIU_MARAMBA_MDL_STR)) ||
	    (!strcmp(np->vpd.model, NIU_KIMI_MDL_STR)) ||
	    (!strcmp(np->vpd.model, NIU_ALONSO_MDL_STR))) {
		ports = 4;
	} else if ((!strcmp(np->vpd.model, NIU_2XGF_LP_MDL_STR)) ||
		   (!strcmp(np->vpd.model, NIU_2XGF_PEM_MDL_STR)) ||
		   (!strcmp(np->vpd.model, NIU_FOXXY_MDL_STR)) ||
		   (!strcmp(np->vpd.model, NIU_2XGF_MRVL_MDL_STR))) {
		ports = 2;
	}

	return ports;
}

static void __devinit niu_pci_vpd_validate(struct niu *np)
{
	struct net_device *dev = np->dev;
	struct niu_vpd *vpd = &np->vpd;
	u8 val8;

	if (!is_valid_ether_addr(&vpd->local_mac[0])) {
		dev_err(np->device, PFX "VPD MAC invalid, "
			"falling back to SPROM.\n");

		np->flags &= ~NIU_FLAGS_VPD_VALID;
		return;
	}

	if (!strcmp(np->vpd.model, NIU_ALONSO_MDL_STR) ||
	    !strcmp(np->vpd.model, NIU_KIMI_MDL_STR)) {
		np->flags |= NIU_FLAGS_10G;
		np->flags &= ~NIU_FLAGS_FIBER;
		np->flags |= NIU_FLAGS_XCVR_SERDES;
		np->mac_xcvr = MAC_XCVR_PCS;
		if (np->port > 1) {
			np->flags |= NIU_FLAGS_FIBER;
			np->flags &= ~NIU_FLAGS_10G;
		}
		if (np->flags & NIU_FLAGS_10G)
			 np->mac_xcvr = MAC_XCVR_XPCS;
	} else if (!strcmp(np->vpd.model, NIU_FOXXY_MDL_STR)) {
		np->flags |= (NIU_FLAGS_10G | NIU_FLAGS_FIBER |
			      NIU_FLAGS_HOTPLUG_PHY);
	} else if (niu_phy_type_prop_decode(np, np->vpd.phy_type)) {
		dev_err(np->device, PFX "Illegal phy string [%s].\n",
			np->vpd.phy_type);
		dev_err(np->device, PFX "Falling back to SPROM.\n");
		np->flags &= ~NIU_FLAGS_VPD_VALID;
		return;
	}

	memcpy(dev->perm_addr, vpd->local_mac, ETH_ALEN);

	val8 = dev->perm_addr[5];
	dev->perm_addr[5] += np->port;
	if (dev->perm_addr[5] < val8)
		dev->perm_addr[4]++;

	memcpy(dev->dev_addr, dev->perm_addr, dev->addr_len);
}

static int __devinit niu_pci_probe_sprom(struct niu *np)
{
	struct net_device *dev = np->dev;
	int len, i;
	u64 val, sum;
	u8 val8;

	val = (nr64(ESPC_VER_IMGSZ) & ESPC_VER_IMGSZ_IMGSZ);
	val >>= ESPC_VER_IMGSZ_IMGSZ_SHIFT;
	len = val / 4;

	np->eeprom_len = len;

	niudbg(PROBE, "SPROM: Image size %llu\n", (unsigned long long) val);

	sum = 0;
	for (i = 0; i < len; i++) {
		val = nr64(ESPC_NCR(i));
		sum += (val >>  0) & 0xff;
		sum += (val >>  8) & 0xff;
		sum += (val >> 16) & 0xff;
		sum += (val >> 24) & 0xff;
	}
	niudbg(PROBE, "SPROM: Checksum %x\n", (int)(sum & 0xff));
	if ((sum & 0xff) != 0xab) {
		dev_err(np->device, PFX "Bad SPROM checksum "
			"(%x, should be 0xab)\n", (int) (sum & 0xff));
		return -EINVAL;
	}

	val = nr64(ESPC_PHY_TYPE);
	switch (np->port) {
	case 0:
		val8 = (val & ESPC_PHY_TYPE_PORT0) >>
			ESPC_PHY_TYPE_PORT0_SHIFT;
		break;
	case 1:
		val8 = (val & ESPC_PHY_TYPE_PORT1) >>
			ESPC_PHY_TYPE_PORT1_SHIFT;
		break;
	case 2:
		val8 = (val & ESPC_PHY_TYPE_PORT2) >>
			ESPC_PHY_TYPE_PORT2_SHIFT;
		break;
	case 3:
		val8 = (val & ESPC_PHY_TYPE_PORT3) >>
			ESPC_PHY_TYPE_PORT3_SHIFT;
		break;
	default:
		dev_err(np->device, PFX "Bogus port number %u\n",
			np->port);
		return -EINVAL;
	}
	niudbg(PROBE, "SPROM: PHY type %x\n", val8);

	switch (val8) {
	case ESPC_PHY_TYPE_1G_COPPER:
		/* 1G copper, MII */
		np->flags &= ~(NIU_FLAGS_FIBER |
			       NIU_FLAGS_10G);
		np->mac_xcvr = MAC_XCVR_MII;
		break;

	case ESPC_PHY_TYPE_1G_FIBER:
		/* 1G fiber, PCS */
		np->flags &= ~NIU_FLAGS_10G;
		np->flags |= NIU_FLAGS_FIBER;
		np->mac_xcvr = MAC_XCVR_PCS;
		break;

	case ESPC_PHY_TYPE_10G_COPPER:
		/* 10G copper, XPCS */
		np->flags |= NIU_FLAGS_10G;
		np->flags &= ~NIU_FLAGS_FIBER;
		np->mac_xcvr = MAC_XCVR_XPCS;
		break;

	case ESPC_PHY_TYPE_10G_FIBER:
		/* 10G fiber, XPCS */
		np->flags |= (NIU_FLAGS_10G |
			      NIU_FLAGS_FIBER);
		np->mac_xcvr = MAC_XCVR_XPCS;
		break;

	default:
		dev_err(np->device, PFX "Bogus SPROM phy type %u\n", val8);
		return -EINVAL;
	}

	val = nr64(ESPC_MAC_ADDR0);
	niudbg(PROBE, "SPROM: MAC_ADDR0[%08llx]\n",
	       (unsigned long long) val);
	dev->perm_addr[0] = (val >>  0) & 0xff;
	dev->perm_addr[1] = (val >>  8) & 0xff;
	dev->perm_addr[2] = (val >> 16) & 0xff;
	dev->perm_addr[3] = (val >> 24) & 0xff;

	val = nr64(ESPC_MAC_ADDR1);
	niudbg(PROBE, "SPROM: MAC_ADDR1[%08llx]\n",
	       (unsigned long long) val);
	dev->perm_addr[4] = (val >>  0) & 0xff;
	dev->perm_addr[5] = (val >>  8) & 0xff;

	if (!is_valid_ether_addr(&dev->perm_addr[0])) {
		dev_err(np->device, PFX "SPROM MAC address invalid\n");
		dev_err(np->device, PFX "[ \n");
		for (i = 0; i < 6; i++)
			printk("%02x ", dev->perm_addr[i]);
		printk("]\n");
		return -EINVAL;
	}

	val8 = dev->perm_addr[5];
	dev->perm_addr[5] += np->port;
	if (dev->perm_addr[5] < val8)
		dev->perm_addr[4]++;

	memcpy(dev->dev_addr, dev->perm_addr, dev->addr_len);

	val = nr64(ESPC_MOD_STR_LEN);
	niudbg(PROBE, "SPROM: MOD_STR_LEN[%llu]\n",
	       (unsigned long long) val);
	if (val >= 8 * 4)
		return -EINVAL;

	for (i = 0; i < val; i += 4) {
		u64 tmp = nr64(ESPC_NCR(5 + (i / 4)));

		np->vpd.model[i + 3] = (tmp >>  0) & 0xff;
		np->vpd.model[i + 2] = (tmp >>  8) & 0xff;
		np->vpd.model[i + 1] = (tmp >> 16) & 0xff;
		np->vpd.model[i + 0] = (tmp >> 24) & 0xff;
	}
	np->vpd.model[val] = '\0';

	val = nr64(ESPC_BD_MOD_STR_LEN);
	niudbg(PROBE, "SPROM: BD_MOD_STR_LEN[%llu]\n",
	       (unsigned long long) val);
	if (val >= 4 * 4)
		return -EINVAL;

	for (i = 0; i < val; i += 4) {
		u64 tmp = nr64(ESPC_NCR(14 + (i / 4)));

		np->vpd.board_model[i + 3] = (tmp >>  0) & 0xff;
		np->vpd.board_model[i + 2] = (tmp >>  8) & 0xff;
		np->vpd.board_model[i + 1] = (tmp >> 16) & 0xff;
		np->vpd.board_model[i + 0] = (tmp >> 24) & 0xff;
	}
	np->vpd.board_model[val] = '\0';

	np->vpd.mac_num =
		nr64(ESPC_NUM_PORTS_MACS) & ESPC_NUM_PORTS_MACS_VAL;
	niudbg(PROBE, "SPROM: NUM_PORTS_MACS[%d]\n",
	       np->vpd.mac_num);

	return 0;
}

static int __devinit niu_get_and_validate_port(struct niu *np)
{
	struct niu_parent *parent = np->parent;

	if (np->port <= 1)
		np->flags |= NIU_FLAGS_XMAC;

	if (!parent->num_ports) {
		if (parent->plat_type == PLAT_TYPE_NIU) {
			parent->num_ports = 2;
		} else {
			parent->num_ports = niu_pci_vpd_get_nports(np);
			if (!parent->num_ports) {
				/* Fall back to SPROM as last resort.
				 * This will fail on most cards.
				 */
				parent->num_ports = nr64(ESPC_NUM_PORTS_MACS) &
					ESPC_NUM_PORTS_MACS_VAL;

				/* All of the current probing methods fail on
				 * Maramba on-board parts.
				 */
				if (!parent->num_ports)
					parent->num_ports = 4;
			}
		}
	}

	niudbg(PROBE, "niu_get_and_validate_port: port[%d] num_ports[%d]\n",
	       np->port, parent->num_ports);
	if (np->port >= parent->num_ports)
		return -ENODEV;

	return 0;
}

static int __devinit phy_record(struct niu_parent *parent,
				struct phy_probe_info *p,
				int dev_id_1, int dev_id_2, u8 phy_port,
				int type)
{
	u32 id = (dev_id_1 << 16) | dev_id_2;
	u8 idx;

	if (dev_id_1 < 0 || dev_id_2 < 0)
		return 0;
	if (type == PHY_TYPE_PMA_PMD || type == PHY_TYPE_PCS) {
		if (((id & NIU_PHY_ID_MASK) != NIU_PHY_ID_BCM8704) &&
		    ((id & NIU_PHY_ID_MASK) != NIU_PHY_ID_MRVL88X2011) &&
		    ((id & NIU_PHY_ID_MASK) != NIU_PHY_ID_BCM8706))
			return 0;
	} else {
		if ((id & NIU_PHY_ID_MASK) != NIU_PHY_ID_BCM5464R)
			return 0;
	}

	pr_info("niu%d: Found PHY %08x type %s at phy_port %u\n",
		parent->index, id,
		(type == PHY_TYPE_PMA_PMD ?
		 "PMA/PMD" :
		 (type == PHY_TYPE_PCS ?
		  "PCS" : "MII")),
		phy_port);

	if (p->cur[type] >= NIU_MAX_PORTS) {
		printk(KERN_ERR PFX "Too many PHY ports.\n");
		return -EINVAL;
	}
	idx = p->cur[type];
	p->phy_id[type][idx] = id;
	p->phy_port[type][idx] = phy_port;
	p->cur[type] = idx + 1;
	return 0;
}

static int __devinit port_has_10g(struct phy_probe_info *p, int port)
{
	int i;

	for (i = 0; i < p->cur[PHY_TYPE_PMA_PMD]; i++) {
		if (p->phy_port[PHY_TYPE_PMA_PMD][i] == port)
			return 1;
	}
	for (i = 0; i < p->cur[PHY_TYPE_PCS]; i++) {
		if (p->phy_port[PHY_TYPE_PCS][i] == port)
			return 1;
	}

	return 0;
}

static int __devinit count_10g_ports(struct phy_probe_info *p, int *lowest)
{
	int port, cnt;

	cnt = 0;
	*lowest = 32;
	for (port = 8; port < 32; port++) {
		if (port_has_10g(p, port)) {
			if (!cnt)
				*lowest = port;
			cnt++;
		}
	}

	return cnt;
}

static int __devinit count_1g_ports(struct phy_probe_info *p, int *lowest)
{
	*lowest = 32;
	if (p->cur[PHY_TYPE_MII])
		*lowest = p->phy_port[PHY_TYPE_MII][0];

	return p->cur[PHY_TYPE_MII];
}

static void __devinit niu_n2_divide_channels(struct niu_parent *parent)
{
	int num_ports = parent->num_ports;
	int i;

	for (i = 0; i < num_ports; i++) {
		parent->rxchan_per_port[i] = (16 / num_ports);
		parent->txchan_per_port[i] = (16 / num_ports);

		pr_info(PFX "niu%d: Port %u [%u RX chans] "
			"[%u TX chans]\n",
			parent->index, i,
			parent->rxchan_per_port[i],
			parent->txchan_per_port[i]);
	}
}

static void __devinit niu_divide_channels(struct niu_parent *parent,
					  int num_10g, int num_1g)
{
	int num_ports = parent->num_ports;
	int rx_chans_per_10g, rx_chans_per_1g;
	int tx_chans_per_10g, tx_chans_per_1g;
	int i, tot_rx, tot_tx;

	if (!num_10g || !num_1g) {
		rx_chans_per_10g = rx_chans_per_1g =
			(NIU_NUM_RXCHAN / num_ports);
		tx_chans_per_10g = tx_chans_per_1g =
			(NIU_NUM_TXCHAN / num_ports);
	} else {
		rx_chans_per_1g = NIU_NUM_RXCHAN / 8;
		rx_chans_per_10g = (NIU_NUM_RXCHAN -
				    (rx_chans_per_1g * num_1g)) /
			num_10g;

		tx_chans_per_1g = NIU_NUM_TXCHAN / 6;
		tx_chans_per_10g = (NIU_NUM_TXCHAN -
				    (tx_chans_per_1g * num_1g)) /
			num_10g;
	}

	tot_rx = tot_tx = 0;
	for (i = 0; i < num_ports; i++) {
		int type = phy_decode(parent->port_phy, i);

		if (type == PORT_TYPE_10G) {
			parent->rxchan_per_port[i] = rx_chans_per_10g;
			parent->txchan_per_port[i] = tx_chans_per_10g;
		} else {
			parent->rxchan_per_port[i] = rx_chans_per_1g;
			parent->txchan_per_port[i] = tx_chans_per_1g;
		}
		pr_info(PFX "niu%d: Port %u [%u RX chans] "
			"[%u TX chans]\n",
			parent->index, i,
			parent->rxchan_per_port[i],
			parent->txchan_per_port[i]);
		tot_rx += parent->rxchan_per_port[i];
		tot_tx += parent->txchan_per_port[i];
	}

	if (tot_rx > NIU_NUM_RXCHAN) {
		printk(KERN_ERR PFX "niu%d: Too many RX channels (%d), "
		       "resetting to one per port.\n",
		       parent->index, tot_rx);
		for (i = 0; i < num_ports; i++)
			parent->rxchan_per_port[i] = 1;
	}
	if (tot_tx > NIU_NUM_TXCHAN) {
		printk(KERN_ERR PFX "niu%d: Too many TX channels (%d), "
		       "resetting to one per port.\n",
		       parent->index, tot_tx);
		for (i = 0; i < num_ports; i++)
			parent->txchan_per_port[i] = 1;
	}
	if (tot_rx < NIU_NUM_RXCHAN || tot_tx < NIU_NUM_TXCHAN) {
		printk(KERN_WARNING PFX "niu%d: Driver bug, wasted channels, "
		       "RX[%d] TX[%d]\n",
		       parent->index, tot_rx, tot_tx);
	}
}

static void __devinit niu_divide_rdc_groups(struct niu_parent *parent,
					    int num_10g, int num_1g)
{
	int i, num_ports = parent->num_ports;
	int rdc_group, rdc_groups_per_port;
	int rdc_channel_base;

	rdc_group = 0;
	rdc_groups_per_port = NIU_NUM_RDC_TABLES / num_ports;

	rdc_channel_base = 0;

	for (i = 0; i < num_ports; i++) {
		struct niu_rdc_tables *tp = &parent->rdc_group_cfg[i];
		int grp, num_channels = parent->rxchan_per_port[i];
		int this_channel_offset;

		tp->first_table_num = rdc_group;
		tp->num_tables = rdc_groups_per_port;
		this_channel_offset = 0;
		for (grp = 0; grp < tp->num_tables; grp++) {
			struct rdc_table *rt = &tp->tables[grp];
			int slot;

			pr_info(PFX "niu%d: Port %d RDC tbl(%d) [ ",
				parent->index, i, tp->first_table_num + grp);
			for (slot = 0; slot < NIU_RDC_TABLE_SLOTS; slot++) {
				rt->rxdma_channel[slot] =
					rdc_channel_base + this_channel_offset;

				printk("%d ", rt->rxdma_channel[slot]);

				if (++this_channel_offset == num_channels)
					this_channel_offset = 0;
			}
			printk("]\n");
		}

		parent->rdc_default[i] = rdc_channel_base;

		rdc_channel_base += num_channels;
		rdc_group += rdc_groups_per_port;
	}
}

static int __devinit fill_phy_probe_info(struct niu *np,
					 struct niu_parent *parent,
					 struct phy_probe_info *info)
{
	unsigned long flags;
	int port, err;

	memset(info, 0, sizeof(*info));

	/* Port 0 to 7 are reserved for onboard Serdes, probe the rest.  */
	niu_lock_parent(np, flags);
	err = 0;
	for (port = 8; port < 32; port++) {
		int dev_id_1, dev_id_2;

		dev_id_1 = mdio_read(np, port,
				     NIU_PMA_PMD_DEV_ADDR, MII_PHYSID1);
		dev_id_2 = mdio_read(np, port,
				     NIU_PMA_PMD_DEV_ADDR, MII_PHYSID2);
		err = phy_record(parent, info, dev_id_1, dev_id_2, port,
				 PHY_TYPE_PMA_PMD);
		if (err)
			break;
		dev_id_1 = mdio_read(np, port,
				     NIU_PCS_DEV_ADDR, MII_PHYSID1);
		dev_id_2 = mdio_read(np, port,
				     NIU_PCS_DEV_ADDR, MII_PHYSID2);
		err = phy_record(parent, info, dev_id_1, dev_id_2, port,
				 PHY_TYPE_PCS);
		if (err)
			break;
		dev_id_1 = mii_read(np, port, MII_PHYSID1);
		dev_id_2 = mii_read(np, port, MII_PHYSID2);
		err = phy_record(parent, info, dev_id_1, dev_id_2, port,
				 PHY_TYPE_MII);
		if (err)
			break;
	}
	niu_unlock_parent(np, flags);

	return err;
}

static int __devinit walk_phys(struct niu *np, struct niu_parent *parent)
{
	struct phy_probe_info *info = &parent->phy_probe_info;
	int lowest_10g, lowest_1g;
	int num_10g, num_1g;
	u32 val;
	int err;

	num_10g = num_1g = 0;

	if (!strcmp(np->vpd.model, NIU_ALONSO_MDL_STR) ||
	    !strcmp(np->vpd.model, NIU_KIMI_MDL_STR)) {
		num_10g = 0;
		num_1g = 2;
		parent->plat_type = PLAT_TYPE_ATCA_CP3220;
		parent->num_ports = 4;
		val = (phy_encode(PORT_TYPE_1G, 0) |
		       phy_encode(PORT_TYPE_1G, 1) |
		       phy_encode(PORT_TYPE_1G, 2) |
		       phy_encode(PORT_TYPE_1G, 3));
	} else if (!strcmp(np->vpd.model, NIU_FOXXY_MDL_STR)) {
		num_10g = 2;
		num_1g = 0;
		parent->num_ports = 2;
		val = (phy_encode(PORT_TYPE_10G, 0) |
		       phy_encode(PORT_TYPE_10G, 1));
	} else if ((np->flags & NIU_FLAGS_XCVR_SERDES) &&
		   (parent->plat_type == PLAT_TYPE_NIU)) {
		/* this is the Monza case */
		if (np->flags & NIU_FLAGS_10G) {
			val = (phy_encode(PORT_TYPE_10G, 0) |
			       phy_encode(PORT_TYPE_10G, 1));
		} else {
			val = (phy_encode(PORT_TYPE_1G, 0) |
			       phy_encode(PORT_TYPE_1G, 1));
		}
	} else {
		err = fill_phy_probe_info(np, parent, info);
		if (err)
			return err;

		num_10g = count_10g_ports(info, &lowest_10g);
		num_1g = count_1g_ports(info, &lowest_1g);

		switch ((num_10g << 4) | num_1g) {
		case 0x24:
			if (lowest_1g == 10)
				parent->plat_type = PLAT_TYPE_VF_P0;
			else if (lowest_1g == 26)
				parent->plat_type = PLAT_TYPE_VF_P1;
			else
				goto unknown_vg_1g_port;

			/* fallthru */
		case 0x22:
			val = (phy_encode(PORT_TYPE_10G, 0) |
			       phy_encode(PORT_TYPE_10G, 1) |
			       phy_encode(PORT_TYPE_1G, 2) |
			       phy_encode(PORT_TYPE_1G, 3));
			break;

		case 0x20:
			val = (phy_encode(PORT_TYPE_10G, 0) |
			       phy_encode(PORT_TYPE_10G, 1));
			break;

		case 0x10:
			val = phy_encode(PORT_TYPE_10G, np->port);
			break;

		case 0x14:
			if (lowest_1g == 10)
				parent->plat_type = PLAT_TYPE_VF_P0;
			else if (lowest_1g == 26)
				parent->plat_type = PLAT_TYPE_VF_P1;
			else
				goto unknown_vg_1g_port;

			/* fallthru */
		case 0x13:
			if ((lowest_10g & 0x7) == 0)
				val = (phy_encode(PORT_TYPE_10G, 0) |
				       phy_encode(PORT_TYPE_1G, 1) |
				       phy_encode(PORT_TYPE_1G, 2) |
				       phy_encode(PORT_TYPE_1G, 3));
			else
				val = (phy_encode(PORT_TYPE_1G, 0) |
				       phy_encode(PORT_TYPE_10G, 1) |
				       phy_encode(PORT_TYPE_1G, 2) |
				       phy_encode(PORT_TYPE_1G, 3));
			break;

		case 0x04:
			if (lowest_1g == 10)
				parent->plat_type = PLAT_TYPE_VF_P0;
			else if (lowest_1g == 26)
				parent->plat_type = PLAT_TYPE_VF_P1;
			else
				goto unknown_vg_1g_port;

			val = (phy_encode(PORT_TYPE_1G, 0) |
			       phy_encode(PORT_TYPE_1G, 1) |
			       phy_encode(PORT_TYPE_1G, 2) |
			       phy_encode(PORT_TYPE_1G, 3));
			break;

		default:
			printk(KERN_ERR PFX "Unsupported port config "
			       "10G[%d] 1G[%d]\n",
			       num_10g, num_1g);
			return -EINVAL;
		}
	}

	parent->port_phy = val;

	if (parent->plat_type == PLAT_TYPE_NIU)
		niu_n2_divide_channels(parent);
	else
		niu_divide_channels(parent, num_10g, num_1g);

	niu_divide_rdc_groups(parent, num_10g, num_1g);

	return 0;

unknown_vg_1g_port:
	printk(KERN_ERR PFX "Cannot identify platform type, 1gport=%d\n",
	       lowest_1g);
	return -EINVAL;
}

static int __devinit niu_probe_ports(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	int err, i;

	niudbg(PROBE, "niu_probe_ports(): port_phy[%08x]\n",
	       parent->port_phy);

	if (parent->port_phy == PORT_PHY_UNKNOWN) {
		err = walk_phys(np, parent);
		if (err)
			return err;

		niu_set_ldg_timer_res(np, 2);
		for (i = 0; i <= LDN_MAX; i++)
			niu_ldn_irq_enable(np, i, 0);
	}

	if (parent->port_phy == PORT_PHY_INVALID)
		return -EINVAL;

	return 0;
}

static int __devinit niu_classifier_swstate_init(struct niu *np)
{
	struct niu_classifier *cp = &np->clas;

	niudbg(PROBE, "niu_classifier_swstate_init: num_tcam(%d)\n",
	       np->parent->tcam_num_entries);

	cp->tcam_top = (u16) np->port;
	cp->tcam_sz = np->parent->tcam_num_entries / np->parent->num_ports;
	cp->h1_init = 0xffffffff;
	cp->h2_init = 0xffff;

	return fflp_early_init(np);
}

static void __devinit niu_link_config_init(struct niu *np)
{
	struct niu_link_config *lp = &np->link_config;

	lp->advertising = (ADVERTISED_10baseT_Half |
			   ADVERTISED_10baseT_Full |
			   ADVERTISED_100baseT_Half |
			   ADVERTISED_100baseT_Full |
			   ADVERTISED_1000baseT_Half |
			   ADVERTISED_1000baseT_Full |
			   ADVERTISED_10000baseT_Full |
			   ADVERTISED_Autoneg);
	lp->speed = lp->active_speed = SPEED_INVALID;
	lp->duplex = DUPLEX_FULL;
	lp->active_duplex = DUPLEX_INVALID;
	lp->autoneg = 1;
#if 0
	lp->loopback_mode = LOOPBACK_MAC;
	lp->active_speed = SPEED_10000;
	lp->active_duplex = DUPLEX_FULL;
#else
	lp->loopback_mode = LOOPBACK_DISABLED;
#endif
}

static int __devinit niu_init_mac_ipp_pcs_base(struct niu *np)
{
	switch (np->port) {
	case 0:
		np->mac_regs = np->regs + XMAC_PORT0_OFF;
		np->ipp_off  = 0x00000;
		np->pcs_off  = 0x04000;
		np->xpcs_off = 0x02000;
		break;

	case 1:
		np->mac_regs = np->regs + XMAC_PORT1_OFF;
		np->ipp_off  = 0x08000;
		np->pcs_off  = 0x0a000;
		np->xpcs_off = 0x08000;
		break;

	case 2:
		np->mac_regs = np->regs + BMAC_PORT2_OFF;
		np->ipp_off  = 0x04000;
		np->pcs_off  = 0x0e000;
		np->xpcs_off = ~0UL;
		break;

	case 3:
		np->mac_regs = np->regs + BMAC_PORT3_OFF;
		np->ipp_off  = 0x0c000;
		np->pcs_off  = 0x12000;
		np->xpcs_off = ~0UL;
		break;

	default:
		dev_err(np->device, PFX "Port %u is invalid, cannot "
			"compute MAC block offset.\n", np->port);
		return -EINVAL;
	}

	return 0;
}

static void __devinit niu_try_msix(struct niu *np, u8 *ldg_num_map)
{
	struct msix_entry msi_vec[NIU_NUM_LDG];
	struct niu_parent *parent = np->parent;
	struct pci_dev *pdev = np->pdev;
	int i, num_irqs, err;
	u8 first_ldg;

	first_ldg = (NIU_NUM_LDG / parent->num_ports) * np->port;
	for (i = 0; i < (NIU_NUM_LDG / parent->num_ports); i++)
		ldg_num_map[i] = first_ldg + i;

	num_irqs = (parent->rxchan_per_port[np->port] +
		    parent->txchan_per_port[np->port] +
		    (np->port == 0 ? 3 : 1));
	BUG_ON(num_irqs > (NIU_NUM_LDG / parent->num_ports));

retry:
	for (i = 0; i < num_irqs; i++) {
		msi_vec[i].vector = 0;
		msi_vec[i].entry = i;
	}

	err = pci_enable_msix(pdev, msi_vec, num_irqs);
	if (err < 0) {
		np->flags &= ~NIU_FLAGS_MSIX;
		return;
	}
	if (err > 0) {
		num_irqs = err;
		goto retry;
	}

	np->flags |= NIU_FLAGS_MSIX;
	for (i = 0; i < num_irqs; i++)
		np->ldg[i].irq = msi_vec[i].vector;
	np->num_ldg = num_irqs;
}

static int __devinit niu_n2_irq_init(struct niu *np, u8 *ldg_num_map)
{
#ifdef CONFIG_SPARC64
	struct of_device *op = np->op;
	const u32 *int_prop;
	int i;

	int_prop = of_get_property(op->node, "interrupts", NULL);
	if (!int_prop)
		return -ENODEV;

	for (i = 0; i < op->num_irqs; i++) {
		ldg_num_map[i] = int_prop[i];
		np->ldg[i].irq = op->irqs[i];
	}

	np->num_ldg = op->num_irqs;

	return 0;
#else
	return -EINVAL;
#endif
}

static int __devinit niu_ldg_init(struct niu *np)
{
	struct niu_parent *parent = np->parent;
	u8 ldg_num_map[NIU_NUM_LDG];
	int first_chan, num_chan;
	int i, err, ldg_rotor;
	u8 port;

	np->num_ldg = 1;
	np->ldg[0].irq = np->dev->irq;
	if (parent->plat_type == PLAT_TYPE_NIU) {
		err = niu_n2_irq_init(np, ldg_num_map);
		if (err)
			return err;
	} else
		niu_try_msix(np, ldg_num_map);

	port = np->port;
	for (i = 0; i < np->num_ldg; i++) {
		struct niu_ldg *lp = &np->ldg[i];

		netif_napi_add(np->dev, &lp->napi, niu_poll, 64);

		lp->np = np;
		lp->ldg_num = ldg_num_map[i];
		lp->timer = 2; /* XXX */

		/* On N2 NIU the firmware has setup the SID mappings so they go
		 * to the correct values that will route the LDG to the proper
		 * interrupt in the NCU interrupt table.
		 */
		if (np->parent->plat_type != PLAT_TYPE_NIU) {
			err = niu_set_ldg_sid(np, lp->ldg_num, port, i);
			if (err)
				return err;
		}
	}

	/* We adopt the LDG assignment ordering used by the N2 NIU
	 * 'interrupt' properties because that simplifies a lot of
	 * things.  This ordering is:
	 *
	 *	MAC
	 *	MIF	(if port zero)
	 *	SYSERR	(if port zero)
	 *	RX channels
	 *	TX channels
	 */

	ldg_rotor = 0;

	err = niu_ldg_assign_ldn(np, parent, ldg_num_map[ldg_rotor],
				  LDN_MAC(port));
	if (err)
		return err;

	ldg_rotor++;
	if (ldg_rotor == np->num_ldg)
		ldg_rotor = 0;

	if (port == 0) {
		err = niu_ldg_assign_ldn(np, parent,
					 ldg_num_map[ldg_rotor],
					 LDN_MIF);
		if (err)
			return err;

		ldg_rotor++;
		if (ldg_rotor == np->num_ldg)
			ldg_rotor = 0;

		err = niu_ldg_assign_ldn(np, parent,
					 ldg_num_map[ldg_rotor],
					 LDN_DEVICE_ERROR);
		if (err)
			return err;

		ldg_rotor++;
		if (ldg_rotor == np->num_ldg)
			ldg_rotor = 0;

	}

	first_chan = 0;
	for (i = 0; i < port; i++)
		first_chan += parent->rxchan_per_port[port];
	num_chan = parent->rxchan_per_port[port];

	for (i = first_chan; i < (first_chan + num_chan); i++) {
		err = niu_ldg_assign_ldn(np, parent,
					 ldg_num_map[ldg_rotor],
					 LDN_RXDMA(i));
		if (err)
			return err;
		ldg_rotor++;
		if (ldg_rotor == np->num_ldg)
			ldg_rotor = 0;
	}

	first_chan = 0;
	for (i = 0; i < port; i++)
		first_chan += parent->txchan_per_port[port];
	num_chan = parent->txchan_per_port[port];
	for (i = first_chan; i < (first_chan + num_chan); i++) {
		err = niu_ldg_assign_ldn(np, parent,
					 ldg_num_map[ldg_rotor],
					 LDN_TXDMA(i));
		if (err)
			return err;
		ldg_rotor++;
		if (ldg_rotor == np->num_ldg)
			ldg_rotor = 0;
	}

	return 0;
}

static void __devexit niu_ldg_free(struct niu *np)
{
	if (np->flags & NIU_FLAGS_MSIX)
		pci_disable_msix(np->pdev);
}

static int __devinit niu_get_of_props(struct niu *np)
{
#ifdef CONFIG_SPARC64
	struct net_device *dev = np->dev;
	struct device_node *dp;
	const char *phy_type;
	const u8 *mac_addr;
	const char *model;
	int prop_len;

	if (np->parent->plat_type == PLAT_TYPE_NIU)
		dp = np->op->node;
	else
		dp = pci_device_to_OF_node(np->pdev);

	phy_type = of_get_property(dp, "phy-type", &prop_len);
	if (!phy_type) {
		dev_err(np->device, PFX "%s: OF node lacks "
			"phy-type property\n",
			dp->full_name);
		return -EINVAL;
	}

	if (!strcmp(phy_type, "none"))
		return -ENODEV;

	strcpy(np->vpd.phy_type, phy_type);

	if (niu_phy_type_prop_decode(np, np->vpd.phy_type)) {
		dev_err(np->device, PFX "%s: Illegal phy string [%s].\n",
			dp->full_name, np->vpd.phy_type);
		return -EINVAL;
	}

	mac_addr = of_get_property(dp, "local-mac-address", &prop_len);
	if (!mac_addr) {
		dev_err(np->device, PFX "%s: OF node lacks "
			"local-mac-address property\n",
			dp->full_name);
		return -EINVAL;
	}
	if (prop_len != dev->addr_len) {
		dev_err(np->device, PFX "%s: OF MAC address prop len (%d) "
			"is wrong.\n",
			dp->full_name, prop_len);
	}
	memcpy(dev->perm_addr, mac_addr, dev->addr_len);
	if (!is_valid_ether_addr(&dev->perm_addr[0])) {
		int i;

		dev_err(np->device, PFX "%s: OF MAC address is invalid\n",
			dp->full_name);
		dev_err(np->device, PFX "%s: [ \n",
			dp->full_name);
		for (i = 0; i < 6; i++)
			printk("%02x ", dev->perm_addr[i]);
		printk("]\n");
		return -EINVAL;
	}

	memcpy(dev->dev_addr, dev->perm_addr, dev->addr_len);

	model = of_get_property(dp, "model", &prop_len);

	if (model)
		strcpy(np->vpd.model, model);

	return 0;
#else
	return -EINVAL;
#endif
}

static int __devinit niu_get_invariants(struct niu *np)
{
	int err, have_props;
	u32 offset;

	err = niu_get_of_props(np);
	if (err == -ENODEV)
		return err;

	have_props = !err;

	err = niu_init_mac_ipp_pcs_base(np);
	if (err)
		return err;

	if (have_props) {
		err = niu_get_and_validate_port(np);
		if (err)
			return err;

	} else  {
		if (np->parent->plat_type == PLAT_TYPE_NIU)
			return -EINVAL;

		nw64(ESPC_PIO_EN, ESPC_PIO_EN_ENABLE);
		offset = niu_pci_vpd_offset(np);
		niudbg(PROBE, "niu_get_invariants: VPD offset [%08x]\n",
		       offset);
		if (offset)
			niu_pci_vpd_fetch(np, offset);
		nw64(ESPC_PIO_EN, 0);

		if (np->flags & NIU_FLAGS_VPD_VALID) {
			niu_pci_vpd_validate(np);
			err = niu_get_and_validate_port(np);
			if (err)
				return err;
		}

		if (!(np->flags & NIU_FLAGS_VPD_VALID)) {
			err = niu_get_and_validate_port(np);
			if (err)
				return err;
			err = niu_pci_probe_sprom(np);
			if (err)
				return err;
		}
	}

	err = niu_probe_ports(np);
	if (err)
		return err;

	niu_ldg_init(np);

	niu_classifier_swstate_init(np);
	niu_link_config_init(np);

	err = niu_determine_phy_disposition(np);
	if (!err)
		err = niu_init_link(np);

	return err;
}

static LIST_HEAD(niu_parent_list);
static DEFINE_MUTEX(niu_parent_lock);
static int niu_parent_index;

static ssize_t show_port_phy(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct niu_parent *p = plat_dev->dev.platform_data;
	u32 port_phy = p->port_phy;
	char *orig_buf = buf;
	int i;

	if (port_phy == PORT_PHY_UNKNOWN ||
	    port_phy == PORT_PHY_INVALID)
		return 0;

	for (i = 0; i < p->num_ports; i++) {
		const char *type_str;
		int type;

		type = phy_decode(port_phy, i);
		if (type == PORT_TYPE_10G)
			type_str = "10G";
		else
			type_str = "1G";
		buf += sprintf(buf,
			       (i == 0) ? "%s" : " %s",
			       type_str);
	}
	buf += sprintf(buf, "\n");
	return buf - orig_buf;
}

static ssize_t show_plat_type(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct niu_parent *p = plat_dev->dev.platform_data;
	const char *type_str;

	switch (p->plat_type) {
	case PLAT_TYPE_ATLAS:
		type_str = "atlas";
		break;
	case PLAT_TYPE_NIU:
		type_str = "niu";
		break;
	case PLAT_TYPE_VF_P0:
		type_str = "vf_p0";
		break;
	case PLAT_TYPE_VF_P1:
		type_str = "vf_p1";
		break;
	default:
		type_str = "unknown";
		break;
	}

	return sprintf(buf, "%s\n", type_str);
}

static ssize_t __show_chan_per_port(struct device *dev,
				    struct device_attribute *attr, char *buf,
				    int rx)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct niu_parent *p = plat_dev->dev.platform_data;
	char *orig_buf = buf;
	u8 *arr;
	int i;

	arr = (rx ? p->rxchan_per_port : p->txchan_per_port);

	for (i = 0; i < p->num_ports; i++) {
		buf += sprintf(buf,
			       (i == 0) ? "%d" : " %d",
			       arr[i]);
	}
	buf += sprintf(buf, "\n");

	return buf - orig_buf;
}

static ssize_t show_rxchan_per_port(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return __show_chan_per_port(dev, attr, buf, 1);
}

static ssize_t show_txchan_per_port(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return __show_chan_per_port(dev, attr, buf, 1);
}

static ssize_t show_num_ports(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct niu_parent *p = plat_dev->dev.platform_data;

	return sprintf(buf, "%d\n", p->num_ports);
}

static struct device_attribute niu_parent_attributes[] = {
	__ATTR(port_phy, S_IRUGO, show_port_phy, NULL),
	__ATTR(plat_type, S_IRUGO, show_plat_type, NULL),
	__ATTR(rxchan_per_port, S_IRUGO, show_rxchan_per_port, NULL),
	__ATTR(txchan_per_port, S_IRUGO, show_txchan_per_port, NULL),
	__ATTR(num_ports, S_IRUGO, show_num_ports, NULL),
	{}
};

static struct niu_parent * __devinit niu_new_parent(struct niu *np,
						    union niu_parent_id *id,
						    u8 ptype)
{
	struct platform_device *plat_dev;
	struct niu_parent *p;
	int i;

	niudbg(PROBE, "niu_new_parent: Creating new parent.\n");

	plat_dev = platform_device_register_simple("niu", niu_parent_index,
						   NULL, 0);
	if (!plat_dev)
		return NULL;

	for (i = 0; attr_name(niu_parent_attributes[i]); i++) {
		int err = device_create_file(&plat_dev->dev,
					     &niu_parent_attributes[i]);
		if (err)
			goto fail_unregister;
	}

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		goto fail_unregister;

	p->index = niu_parent_index++;

	plat_dev->dev.platform_data = p;
	p->plat_dev = plat_dev;

	memcpy(&p->id, id, sizeof(*id));
	p->plat_type = ptype;
	INIT_LIST_HEAD(&p->list);
	atomic_set(&p->refcnt, 0);
	list_add(&p->list, &niu_parent_list);
	spin_lock_init(&p->lock);

	p->rxdma_clock_divider = 7500;

	p->tcam_num_entries = NIU_PCI_TCAM_ENTRIES;
	if (p->plat_type == PLAT_TYPE_NIU)
		p->tcam_num_entries = NIU_NONPCI_TCAM_ENTRIES;

	for (i = CLASS_CODE_USER_PROG1; i <= CLASS_CODE_SCTP_IPV6; i++) {
		int index = i - CLASS_CODE_USER_PROG1;

		p->tcam_key[index] = TCAM_KEY_TSEL;
		p->flow_key[index] = (FLOW_KEY_IPSA |
				      FLOW_KEY_IPDA |
				      FLOW_KEY_PROTO |
				      (FLOW_KEY_L4_BYTE12 <<
				       FLOW_KEY_L4_0_SHIFT) |
				      (FLOW_KEY_L4_BYTE12 <<
				       FLOW_KEY_L4_1_SHIFT));
	}

	for (i = 0; i < LDN_MAX + 1; i++)
		p->ldg_map[i] = LDG_INVALID;

	return p;

fail_unregister:
	platform_device_unregister(plat_dev);
	return NULL;
}

static struct niu_parent * __devinit niu_get_parent(struct niu *np,
						    union niu_parent_id *id,
						    u8 ptype)
{
	struct niu_parent *p, *tmp;
	int port = np->port;

	niudbg(PROBE, "niu_get_parent: platform_type[%u] port[%u]\n",
	       ptype, port);

	mutex_lock(&niu_parent_lock);
	p = NULL;
	list_for_each_entry(tmp, &niu_parent_list, list) {
		if (!memcmp(id, &tmp->id, sizeof(*id))) {
			p = tmp;
			break;
		}
	}
	if (!p)
		p = niu_new_parent(np, id, ptype);

	if (p) {
		char port_name[6];
		int err;

		sprintf(port_name, "port%d", port);
		err = sysfs_create_link(&p->plat_dev->dev.kobj,
					&np->device->kobj,
					port_name);
		if (!err) {
			p->ports[port] = np;
			atomic_inc(&p->refcnt);
		}
	}
	mutex_unlock(&niu_parent_lock);

	return p;
}

static void niu_put_parent(struct niu *np)
{
	struct niu_parent *p = np->parent;
	u8 port = np->port;
	char port_name[6];

	BUG_ON(!p || p->ports[port] != np);

	niudbg(PROBE, "niu_put_parent: port[%u]\n", port);

	sprintf(port_name, "port%d", port);

	mutex_lock(&niu_parent_lock);

	sysfs_remove_link(&p->plat_dev->dev.kobj, port_name);

	p->ports[port] = NULL;
	np->parent = NULL;

	if (atomic_dec_and_test(&p->refcnt)) {
		list_del(&p->list);
		platform_device_unregister(p->plat_dev);
	}

	mutex_unlock(&niu_parent_lock);
}

static void *niu_pci_alloc_coherent(struct device *dev, size_t size,
				    u64 *handle, gfp_t flag)
{
	dma_addr_t dh;
	void *ret;

	ret = dma_alloc_coherent(dev, size, &dh, flag);
	if (ret)
		*handle = dh;
	return ret;
}

static void niu_pci_free_coherent(struct device *dev, size_t size,
				  void *cpu_addr, u64 handle)
{
	dma_free_coherent(dev, size, cpu_addr, handle);
}

static u64 niu_pci_map_page(struct device *dev, struct page *page,
			    unsigned long offset, size_t size,
			    enum dma_data_direction direction)
{
	return dma_map_page(dev, page, offset, size, direction);
}

static void niu_pci_unmap_page(struct device *dev, u64 dma_address,
			       size_t size, enum dma_data_direction direction)
{
	dma_unmap_page(dev, dma_address, size, direction);
}

static u64 niu_pci_map_single(struct device *dev, void *cpu_addr,
			      size_t size,
			      enum dma_data_direction direction)
{
	return dma_map_single(dev, cpu_addr, size, direction);
}

static void niu_pci_unmap_single(struct device *dev, u64 dma_address,
				 size_t size,
				 enum dma_data_direction direction)
{
	dma_unmap_single(dev, dma_address, size, direction);
}

static const struct niu_ops niu_pci_ops = {
	.alloc_coherent	= niu_pci_alloc_coherent,
	.free_coherent	= niu_pci_free_coherent,
	.map_page	= niu_pci_map_page,
	.unmap_page	= niu_pci_unmap_page,
	.map_single	= niu_pci_map_single,
	.unmap_single	= niu_pci_unmap_single,
};

static void __devinit niu_driver_version(void)
{
	static int niu_version_printed;

	if (niu_version_printed++ == 0)
		pr_info("%s", version);
}

static struct net_device * __devinit niu_alloc_and_init(
	struct device *gen_dev, struct pci_dev *pdev,
	struct of_device *op, const struct niu_ops *ops,
	u8 port)
{
	struct net_device *dev;
	struct niu *np;

	dev = alloc_etherdev_mq(sizeof(struct niu), NIU_NUM_TXCHAN);
	if (!dev) {
		dev_err(gen_dev, PFX "Etherdev alloc failed, aborting.\n");
		return NULL;
	}

	SET_NETDEV_DEV(dev, gen_dev);

	np = netdev_priv(dev);
	np->dev = dev;
	np->pdev = pdev;
	np->op = op;
	np->device = gen_dev;
	np->ops = ops;

	np->msg_enable = niu_debug;

	spin_lock_init(&np->lock);
	INIT_WORK(&np->reset_task, niu_reset_task);

	np->port = port;

	return dev;
}

static const struct net_device_ops niu_netdev_ops = {
	.ndo_open		= niu_open,
	.ndo_stop		= niu_close,
	.ndo_start_xmit		= niu_start_xmit,
	.ndo_get_stats		= niu_get_stats,
	.ndo_set_multicast_list	= niu_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= niu_set_mac_addr,
	.ndo_do_ioctl		= niu_ioctl,
	.ndo_tx_timeout		= niu_tx_timeout,
	.ndo_change_mtu		= niu_change_mtu,
};

static void __devinit niu_assign_netdev_ops(struct net_device *dev)
{
	dev->netdev_ops = &niu_netdev_ops;
	dev->ethtool_ops = &niu_ethtool_ops;
	dev->watchdog_timeo = NIU_TX_TIMEOUT;
}

static void __devinit niu_device_announce(struct niu *np)
{
	struct net_device *dev = np->dev;

	pr_info("%s: NIU Ethernet %pM\n", dev->name, dev->dev_addr);

	if (np->parent->plat_type == PLAT_TYPE_ATCA_CP3220) {
		pr_info("%s: Port type[%s] mode[%s:%s] XCVR[%s] phy[%s]\n",
				dev->name,
				(np->flags & NIU_FLAGS_XMAC ? "XMAC" : "BMAC"),
				(np->flags & NIU_FLAGS_10G ? "10G" : "1G"),
				(np->flags & NIU_FLAGS_FIBER ? "RGMII FIBER" : "SERDES"),
				(np->mac_xcvr == MAC_XCVR_MII ? "MII" :
				 (np->mac_xcvr == MAC_XCVR_PCS ? "PCS" : "XPCS")),
				np->vpd.phy_type);
	} else {
		pr_info("%s: Port type[%s] mode[%s:%s] XCVR[%s] phy[%s]\n",
				dev->name,
				(np->flags & NIU_FLAGS_XMAC ? "XMAC" : "BMAC"),
				(np->flags & NIU_FLAGS_10G ? "10G" : "1G"),
				(np->flags & NIU_FLAGS_FIBER ? "FIBER" :
				 (np->flags & NIU_FLAGS_XCVR_SERDES ? "SERDES" :
				  "COPPER")),
				(np->mac_xcvr == MAC_XCVR_MII ? "MII" :
				 (np->mac_xcvr == MAC_XCVR_PCS ? "PCS" : "XPCS")),
				np->vpd.phy_type);
	}
}

static int __devinit niu_pci_init_one(struct pci_dev *pdev,
				      const struct pci_device_id *ent)
{
	union niu_parent_id parent_id;
	struct net_device *dev;
	struct niu *np;
	int err, pos;
	u64 dma_mask;
	u16 val16;

	niu_driver_version();

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, PFX "Cannot enable PCI device, "
			"aborting.\n");
		return err;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
	    !(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, PFX "Cannot find proper PCI device "
			"base addresses, aborting.\n");
		err = -ENODEV;
		goto err_out_disable_pdev;
	}

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		dev_err(&pdev->dev, PFX "Cannot obtain PCI resources, "
			"aborting.\n");
		goto err_out_disable_pdev;
	}

	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (pos <= 0) {
		dev_err(&pdev->dev, PFX "Cannot find PCI Express capability, "
			"aborting.\n");
		goto err_out_free_res;
	}

	dev = niu_alloc_and_init(&pdev->dev, pdev, NULL,
				 &niu_pci_ops, PCI_FUNC(pdev->devfn));
	if (!dev) {
		err = -ENOMEM;
		goto err_out_free_res;
	}
	np = netdev_priv(dev);

	memset(&parent_id, 0, sizeof(parent_id));
	parent_id.pci.domain = pci_domain_nr(pdev->bus);
	parent_id.pci.bus = pdev->bus->number;
	parent_id.pci.device = PCI_SLOT(pdev->devfn);

	np->parent = niu_get_parent(np, &parent_id,
				    PLAT_TYPE_ATLAS);
	if (!np->parent) {
		err = -ENOMEM;
		goto err_out_free_dev;
	}

	pci_read_config_word(pdev, pos + PCI_EXP_DEVCTL, &val16);
	val16 &= ~PCI_EXP_DEVCTL_NOSNOOP_EN;
	val16 |= (PCI_EXP_DEVCTL_CERE |
		  PCI_EXP_DEVCTL_NFERE |
		  PCI_EXP_DEVCTL_FERE |
		  PCI_EXP_DEVCTL_URRE |
		  PCI_EXP_DEVCTL_RELAX_EN);
	pci_write_config_word(pdev, pos + PCI_EXP_DEVCTL, val16);

	dma_mask = DMA_44BIT_MASK;
	err = pci_set_dma_mask(pdev, dma_mask);
	if (!err) {
		dev->features |= NETIF_F_HIGHDMA;
		err = pci_set_consistent_dma_mask(pdev, dma_mask);
		if (err) {
			dev_err(&pdev->dev, PFX "Unable to obtain 44 bit "
				"DMA for consistent allocations, "
				"aborting.\n");
			goto err_out_release_parent;
		}
	}
	if (err || dma_mask == DMA_BIT_MASK(32)) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, PFX "No usable DMA configuration, "
				"aborting.\n");
			goto err_out_release_parent;
		}
	}

	dev->features |= (NETIF_F_SG | NETIF_F_HW_CSUM);

	np->regs = pci_ioremap_bar(pdev, 0);
	if (!np->regs) {
		dev_err(&pdev->dev, PFX "Cannot map device registers, "
			"aborting.\n");
		err = -ENOMEM;
		goto err_out_release_parent;
	}

	pci_set_master(pdev);
	pci_save_state(pdev);

	dev->irq = pdev->irq;

	niu_assign_netdev_ops(dev);

	err = niu_get_invariants(np);
	if (err) {
		if (err != -ENODEV)
			dev_err(&pdev->dev, PFX "Problem fetching invariants "
				"of chip, aborting.\n");
		goto err_out_iounmap;
	}

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, PFX "Cannot register net device, "
			"aborting.\n");
		goto err_out_iounmap;
	}

	pci_set_drvdata(pdev, dev);

	niu_device_announce(np);

	return 0;

err_out_iounmap:
	if (np->regs) {
		iounmap(np->regs);
		np->regs = NULL;
	}

err_out_release_parent:
	niu_put_parent(np);

err_out_free_dev:
	free_netdev(dev);

err_out_free_res:
	pci_release_regions(pdev);

err_out_disable_pdev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	return err;
}

static void __devexit niu_pci_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct niu *np = netdev_priv(dev);

		unregister_netdev(dev);
		if (np->regs) {
			iounmap(np->regs);
			np->regs = NULL;
		}

		niu_ldg_free(np);

		niu_put_parent(np);

		free_netdev(dev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}

static int niu_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct niu *np = netdev_priv(dev);
	unsigned long flags;

	if (!netif_running(dev))
		return 0;

	flush_scheduled_work();
	niu_netif_stop(np);

	del_timer_sync(&np->timer);

	spin_lock_irqsave(&np->lock, flags);
	niu_enable_interrupts(np, 0);
	spin_unlock_irqrestore(&np->lock, flags);

	netif_device_detach(dev);

	spin_lock_irqsave(&np->lock, flags);
	niu_stop_hw(np);
	spin_unlock_irqrestore(&np->lock, flags);

	pci_save_state(pdev);

	return 0;
}

static int niu_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct niu *np = netdev_priv(dev);
	unsigned long flags;
	int err;

	if (!netif_running(dev))
		return 0;

	pci_restore_state(pdev);

	netif_device_attach(dev);

	spin_lock_irqsave(&np->lock, flags);

	err = niu_init_hw(np);
	if (!err) {
		np->timer.expires = jiffies + HZ;
		add_timer(&np->timer);
		niu_netif_start(np);
	}

	spin_unlock_irqrestore(&np->lock, flags);

	return err;
}

static struct pci_driver niu_pci_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= niu_pci_tbl,
	.probe		= niu_pci_init_one,
	.remove		= __devexit_p(niu_pci_remove_one),
	.suspend	= niu_suspend,
	.resume		= niu_resume,
};

#ifdef CONFIG_SPARC64
static void *niu_phys_alloc_coherent(struct device *dev, size_t size,
				     u64 *dma_addr, gfp_t flag)
{
	unsigned long order = get_order(size);
	unsigned long page = __get_free_pages(flag, order);

	if (page == 0UL)
		return NULL;
	memset((char *)page, 0, PAGE_SIZE << order);
	*dma_addr = __pa(page);

	return (void *) page;
}

static void niu_phys_free_coherent(struct device *dev, size_t size,
				   void *cpu_addr, u64 handle)
{
	unsigned long order = get_order(size);

	free_pages((unsigned long) cpu_addr, order);
}

static u64 niu_phys_map_page(struct device *dev, struct page *page,
			     unsigned long offset, size_t size,
			     enum dma_data_direction direction)
{
	return page_to_phys(page) + offset;
}

static void niu_phys_unmap_page(struct device *dev, u64 dma_address,
				size_t size, enum dma_data_direction direction)
{
	/* Nothing to do.  */
}

static u64 niu_phys_map_single(struct device *dev, void *cpu_addr,
			       size_t size,
			       enum dma_data_direction direction)
{
	return __pa(cpu_addr);
}

static void niu_phys_unmap_single(struct device *dev, u64 dma_address,
				  size_t size,
				  enum dma_data_direction direction)
{
	/* Nothing to do.  */
}

static const struct niu_ops niu_phys_ops = {
	.alloc_coherent	= niu_phys_alloc_coherent,
	.free_coherent	= niu_phys_free_coherent,
	.map_page	= niu_phys_map_page,
	.unmap_page	= niu_phys_unmap_page,
	.map_single	= niu_phys_map_single,
	.unmap_single	= niu_phys_unmap_single,
};

static unsigned long res_size(struct resource *r)
{
	return r->end - r->start + 1UL;
}

static int __devinit niu_of_probe(struct of_device *op,
				  const struct of_device_id *match)
{
	union niu_parent_id parent_id;
	struct net_device *dev;
	struct niu *np;
	const u32 *reg;
	int err;

	niu_driver_version();

	reg = of_get_property(op->node, "reg", NULL);
	if (!reg) {
		dev_err(&op->dev, PFX "%s: No 'reg' property, aborting.\n",
			op->node->full_name);
		return -ENODEV;
	}

	dev = niu_alloc_and_init(&op->dev, NULL, op,
				 &niu_phys_ops, reg[0] & 0x1);
	if (!dev) {
		err = -ENOMEM;
		goto err_out;
	}
	np = netdev_priv(dev);

	memset(&parent_id, 0, sizeof(parent_id));
	parent_id.of = of_get_parent(op->node);

	np->parent = niu_get_parent(np, &parent_id,
				    PLAT_TYPE_NIU);
	if (!np->parent) {
		err = -ENOMEM;
		goto err_out_free_dev;
	}

	dev->features |= (NETIF_F_SG | NETIF_F_HW_CSUM);

	np->regs = of_ioremap(&op->resource[1], 0,
			      res_size(&op->resource[1]),
			      "niu regs");
	if (!np->regs) {
		dev_err(&op->dev, PFX "Cannot map device registers, "
			"aborting.\n");
		err = -ENOMEM;
		goto err_out_release_parent;
	}

	np->vir_regs_1 = of_ioremap(&op->resource[2], 0,
				    res_size(&op->resource[2]),
				    "niu vregs-1");
	if (!np->vir_regs_1) {
		dev_err(&op->dev, PFX "Cannot map device vir registers 1, "
			"aborting.\n");
		err = -ENOMEM;
		goto err_out_iounmap;
	}

	np->vir_regs_2 = of_ioremap(&op->resource[3], 0,
				    res_size(&op->resource[3]),
				    "niu vregs-2");
	if (!np->vir_regs_2) {
		dev_err(&op->dev, PFX "Cannot map device vir registers 2, "
			"aborting.\n");
		err = -ENOMEM;
		goto err_out_iounmap;
	}

	niu_assign_netdev_ops(dev);

	err = niu_get_invariants(np);
	if (err) {
		if (err != -ENODEV)
			dev_err(&op->dev, PFX "Problem fetching invariants "
				"of chip, aborting.\n");
		goto err_out_iounmap;
	}

	err = register_netdev(dev);
	if (err) {
		dev_err(&op->dev, PFX "Cannot register net device, "
			"aborting.\n");
		goto err_out_iounmap;
	}

	dev_set_drvdata(&op->dev, dev);

	niu_device_announce(np);

	return 0;

err_out_iounmap:
	if (np->vir_regs_1) {
		of_iounmap(&op->resource[2], np->vir_regs_1,
			   res_size(&op->resource[2]));
		np->vir_regs_1 = NULL;
	}

	if (np->vir_regs_2) {
		of_iounmap(&op->resource[3], np->vir_regs_2,
			   res_size(&op->resource[3]));
		np->vir_regs_2 = NULL;
	}

	if (np->regs) {
		of_iounmap(&op->resource[1], np->regs,
			   res_size(&op->resource[1]));
		np->regs = NULL;
	}

err_out_release_parent:
	niu_put_parent(np);

err_out_free_dev:
	free_netdev(dev);

err_out:
	return err;
}

static int __devexit niu_of_remove(struct of_device *op)
{
	struct net_device *dev = dev_get_drvdata(&op->dev);

	if (dev) {
		struct niu *np = netdev_priv(dev);

		unregister_netdev(dev);

		if (np->vir_regs_1) {
			of_iounmap(&op->resource[2], np->vir_regs_1,
				   res_size(&op->resource[2]));
			np->vir_regs_1 = NULL;
		}

		if (np->vir_regs_2) {
			of_iounmap(&op->resource[3], np->vir_regs_2,
				   res_size(&op->resource[3]));
			np->vir_regs_2 = NULL;
		}

		if (np->regs) {
			of_iounmap(&op->resource[1], np->regs,
				   res_size(&op->resource[1]));
			np->regs = NULL;
		}

		niu_ldg_free(np);

		niu_put_parent(np);

		free_netdev(dev);
		dev_set_drvdata(&op->dev, NULL);
	}
	return 0;
}

static const struct of_device_id niu_match[] = {
	{
		.name = "network",
		.compatible = "SUNW,niusl",
	},
	{},
};
MODULE_DEVICE_TABLE(of, niu_match);

static struct of_platform_driver niu_of_driver = {
	.name		= "niu",
	.match_table	= niu_match,
	.probe		= niu_of_probe,
	.remove		= __devexit_p(niu_of_remove),
};

#endif /* CONFIG_SPARC64 */

static int __init niu_init(void)
{
	int err = 0;

	BUILD_BUG_ON(PAGE_SIZE < 4 * 1024);

	niu_debug = netif_msg_init(debug, NIU_MSG_DEFAULT);

#ifdef CONFIG_SPARC64
	err = of_register_driver(&niu_of_driver, &of_bus_type);
#endif

	if (!err) {
		err = pci_register_driver(&niu_pci_driver);
#ifdef CONFIG_SPARC64
		if (err)
			of_unregister_driver(&niu_of_driver);
#endif
	}

	return err;
}

static void __exit niu_exit(void)
{
	pci_unregister_driver(&niu_pci_driver);
#ifdef CONFIG_SPARC64
	of_unregister_driver(&niu_of_driver);
#endif
}

module_init(niu_init);
module_exit(niu_exit);
