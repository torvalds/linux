// SPDX-License-Identifier: GPL-2.0-or-later
/***************************************************************************
 *
 * Copyright (C) 2004-2008 SMSC
 * Copyright (C) 2005-2008 ARM
 *
 ***************************************************************************
 * Rewritten, heavily based on smsc911x simple driver by SMSC.
 * Partly uses io macros from smc91x.c by Nicolas Pitre
 *
 * Supported devices:
 *   LAN9115, LAN9116, LAN9117, LAN9118
 *   LAN9215, LAN9216, LAN9217, LAN9218
 *   LAN9210, LAN9211
 *   LAN9220, LAN9221
 *   LAN89218,LAN9250
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/crc32.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/bug.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/swab.h>
#include <linux/phy.h>
#include <linux/smsc911x.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_net.h>
#include <linux/acpi.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/gpio/consumer.h>

#include "smsc911x.h"

#define SMSC_CHIPNAME		"smsc911x"
#define SMSC_MDIONAME		"smsc911x-mdio"
#define SMSC_DRV_VERSION	"2008-10-21"

MODULE_DESCRIPTION("SMSC LAN911x/LAN921x Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(SMSC_DRV_VERSION);
MODULE_ALIAS("platform:smsc911x");

#if USE_DEBUG > 0
static int debug = 16;
#else
static int debug = 3;
#endif

module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

struct smsc911x_data;

struct smsc911x_ops {
	u32 (*reg_read)(struct smsc911x_data *pdata, u32 reg);
	void (*reg_write)(struct smsc911x_data *pdata, u32 reg, u32 val);
	void (*rx_readfifo)(struct smsc911x_data *pdata,
				unsigned int *buf, unsigned int wordcount);
	void (*tx_writefifo)(struct smsc911x_data *pdata,
				unsigned int *buf, unsigned int wordcount);
};

#define SMSC911X_NUM_SUPPLIES 2

struct smsc911x_data {
	void __iomem *ioaddr;

	unsigned int idrev;

	/* used to decide which workarounds apply */
	unsigned int generation;

	/* device configuration (copied from platform_data during probe) */
	struct smsc911x_platform_config config;

	/* This needs to be acquired before calling any of below:
	 * smsc911x_mac_read(), smsc911x_mac_write()
	 */
	spinlock_t mac_lock;

	/* spinlock to ensure register accesses are serialised */
	spinlock_t dev_lock;

	struct mii_bus *mii_bus;
	unsigned int using_extphy;
	int last_duplex;
	int last_carrier;

	u32 msg_enable;
	unsigned int gpio_setting;
	unsigned int gpio_orig_setting;
	struct net_device *dev;
	struct napi_struct napi;

	unsigned int software_irq_signal;

#ifdef USE_PHY_WORK_AROUND
#define MIN_PACKET_SIZE (64)
	char loopback_tx_pkt[MIN_PACKET_SIZE];
	char loopback_rx_pkt[MIN_PACKET_SIZE];
	unsigned int resetcount;
#endif

	/* Members for Multicast filter workaround */
	unsigned int multicast_update_pending;
	unsigned int set_bits_mask;
	unsigned int clear_bits_mask;
	unsigned int hashhi;
	unsigned int hashlo;

	/* register access functions */
	const struct smsc911x_ops *ops;

	/* regulators */
	struct regulator_bulk_data supplies[SMSC911X_NUM_SUPPLIES];

	/* Reset GPIO */
	struct gpio_desc *reset_gpiod;

	/* clock */
	struct clk *clk;
};

/* Easy access to information */
#define __smsc_shift(pdata, reg) ((reg) << ((pdata)->config.shift))

static inline u32 __smsc911x_reg_read(struct smsc911x_data *pdata, u32 reg)
{
	if (pdata->config.flags & SMSC911X_USE_32BIT)
		return readl(pdata->ioaddr + reg);

	if (pdata->config.flags & SMSC911X_USE_16BIT)
		return ((readw(pdata->ioaddr + reg) & 0xFFFF) |
			((readw(pdata->ioaddr + reg + 2) & 0xFFFF) << 16));

	BUG();
	return 0;
}

static inline u32
__smsc911x_reg_read_shift(struct smsc911x_data *pdata, u32 reg)
{
	if (pdata->config.flags & SMSC911X_USE_32BIT)
		return readl(pdata->ioaddr + __smsc_shift(pdata, reg));

	if (pdata->config.flags & SMSC911X_USE_16BIT)
		return (readw(pdata->ioaddr +
				__smsc_shift(pdata, reg)) & 0xFFFF) |
			((readw(pdata->ioaddr +
			__smsc_shift(pdata, reg + 2)) & 0xFFFF) << 16);

	BUG();
	return 0;
}

static inline u32 smsc911x_reg_read(struct smsc911x_data *pdata, u32 reg)
{
	u32 data;
	unsigned long flags;

	spin_lock_irqsave(&pdata->dev_lock, flags);
	data = pdata->ops->reg_read(pdata, reg);
	spin_unlock_irqrestore(&pdata->dev_lock, flags);

	return data;
}

static inline void __smsc911x_reg_write(struct smsc911x_data *pdata, u32 reg,
					u32 val)
{
	if (pdata->config.flags & SMSC911X_USE_32BIT) {
		writel(val, pdata->ioaddr + reg);
		return;
	}

	if (pdata->config.flags & SMSC911X_USE_16BIT) {
		writew(val & 0xFFFF, pdata->ioaddr + reg);
		writew((val >> 16) & 0xFFFF, pdata->ioaddr + reg + 2);
		return;
	}

	BUG();
}

static inline void
__smsc911x_reg_write_shift(struct smsc911x_data *pdata, u32 reg, u32 val)
{
	if (pdata->config.flags & SMSC911X_USE_32BIT) {
		writel(val, pdata->ioaddr + __smsc_shift(pdata, reg));
		return;
	}

	if (pdata->config.flags & SMSC911X_USE_16BIT) {
		writew(val & 0xFFFF,
			pdata->ioaddr + __smsc_shift(pdata, reg));
		writew((val >> 16) & 0xFFFF,
			pdata->ioaddr + __smsc_shift(pdata, reg + 2));
		return;
	}

	BUG();
}

static inline void smsc911x_reg_write(struct smsc911x_data *pdata, u32 reg,
				      u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&pdata->dev_lock, flags);
	pdata->ops->reg_write(pdata, reg, val);
	spin_unlock_irqrestore(&pdata->dev_lock, flags);
}

/* Writes a packet to the TX_DATA_FIFO */
static inline void
smsc911x_tx_writefifo(struct smsc911x_data *pdata, unsigned int *buf,
		      unsigned int wordcount)
{
	unsigned long flags;

	spin_lock_irqsave(&pdata->dev_lock, flags);

	if (pdata->config.flags & SMSC911X_SWAP_FIFO) {
		while (wordcount--)
			__smsc911x_reg_write(pdata, TX_DATA_FIFO,
					     swab32(*buf++));
		goto out;
	}

	if (pdata->config.flags & SMSC911X_USE_32BIT) {
		iowrite32_rep(pdata->ioaddr + TX_DATA_FIFO, buf, wordcount);
		goto out;
	}

	if (pdata->config.flags & SMSC911X_USE_16BIT) {
		while (wordcount--)
			__smsc911x_reg_write(pdata, TX_DATA_FIFO, *buf++);
		goto out;
	}

	BUG();
out:
	spin_unlock_irqrestore(&pdata->dev_lock, flags);
}

/* Writes a packet to the TX_DATA_FIFO - shifted version */
static inline void
smsc911x_tx_writefifo_shift(struct smsc911x_data *pdata, unsigned int *buf,
		      unsigned int wordcount)
{
	unsigned long flags;

	spin_lock_irqsave(&pdata->dev_lock, flags);

	if (pdata->config.flags & SMSC911X_SWAP_FIFO) {
		while (wordcount--)
			__smsc911x_reg_write_shift(pdata, TX_DATA_FIFO,
					     swab32(*buf++));
		goto out;
	}

	if (pdata->config.flags & SMSC911X_USE_32BIT) {
		iowrite32_rep(pdata->ioaddr + __smsc_shift(pdata,
						TX_DATA_FIFO), buf, wordcount);
		goto out;
	}

	if (pdata->config.flags & SMSC911X_USE_16BIT) {
		while (wordcount--)
			__smsc911x_reg_write_shift(pdata,
						 TX_DATA_FIFO, *buf++);
		goto out;
	}

	BUG();
out:
	spin_unlock_irqrestore(&pdata->dev_lock, flags);
}

/* Reads a packet out of the RX_DATA_FIFO */
static inline void
smsc911x_rx_readfifo(struct smsc911x_data *pdata, unsigned int *buf,
		     unsigned int wordcount)
{
	unsigned long flags;

	spin_lock_irqsave(&pdata->dev_lock, flags);

	if (pdata->config.flags & SMSC911X_SWAP_FIFO) {
		while (wordcount--)
			*buf++ = swab32(__smsc911x_reg_read(pdata,
							    RX_DATA_FIFO));
		goto out;
	}

	if (pdata->config.flags & SMSC911X_USE_32BIT) {
		ioread32_rep(pdata->ioaddr + RX_DATA_FIFO, buf, wordcount);
		goto out;
	}

	if (pdata->config.flags & SMSC911X_USE_16BIT) {
		while (wordcount--)
			*buf++ = __smsc911x_reg_read(pdata, RX_DATA_FIFO);
		goto out;
	}

	BUG();
out:
	spin_unlock_irqrestore(&pdata->dev_lock, flags);
}

/* Reads a packet out of the RX_DATA_FIFO - shifted version */
static inline void
smsc911x_rx_readfifo_shift(struct smsc911x_data *pdata, unsigned int *buf,
		     unsigned int wordcount)
{
	unsigned long flags;

	spin_lock_irqsave(&pdata->dev_lock, flags);

	if (pdata->config.flags & SMSC911X_SWAP_FIFO) {
		while (wordcount--)
			*buf++ = swab32(__smsc911x_reg_read_shift(pdata,
							    RX_DATA_FIFO));
		goto out;
	}

	if (pdata->config.flags & SMSC911X_USE_32BIT) {
		ioread32_rep(pdata->ioaddr + __smsc_shift(pdata,
						RX_DATA_FIFO), buf, wordcount);
		goto out;
	}

	if (pdata->config.flags & SMSC911X_USE_16BIT) {
		while (wordcount--)
			*buf++ = __smsc911x_reg_read_shift(pdata,
								RX_DATA_FIFO);
		goto out;
	}

	BUG();
out:
	spin_unlock_irqrestore(&pdata->dev_lock, flags);
}

/*
 * enable regulator and clock resources.
 */
static int smsc911x_enable_resources(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct smsc911x_data *pdata = netdev_priv(ndev);
	int ret = 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(pdata->supplies),
			pdata->supplies);
	if (ret)
		netdev_err(ndev, "failed to enable regulators %d\n",
				ret);

	if (!IS_ERR(pdata->clk)) {
		ret = clk_prepare_enable(pdata->clk);
		if (ret < 0)
			netdev_err(ndev, "failed to enable clock %d\n", ret);
	}

	return ret;
}

/*
 * disable resources, currently just regulators.
 */
static int smsc911x_disable_resources(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct smsc911x_data *pdata = netdev_priv(ndev);
	int ret = 0;

	ret = regulator_bulk_disable(ARRAY_SIZE(pdata->supplies),
			pdata->supplies);

	if (!IS_ERR(pdata->clk))
		clk_disable_unprepare(pdata->clk);

	return ret;
}

/*
 * Request resources, currently just regulators.
 *
 * The SMSC911x has two power pins: vddvario and vdd33a, in designs where
 * these are not always-on we need to request regulators to be turned on
 * before we can try to access the device registers.
 */
static int smsc911x_request_resources(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct smsc911x_data *pdata = netdev_priv(ndev);
	int ret = 0;

	/* Request regulators */
	pdata->supplies[0].supply = "vdd33a";
	pdata->supplies[1].supply = "vddvario";
	ret = regulator_bulk_get(&pdev->dev,
			ARRAY_SIZE(pdata->supplies),
			pdata->supplies);
	if (ret) {
		/*
		 * Retry on deferrals, else just report the error
		 * and try to continue.
		 */
		if (ret == -EPROBE_DEFER)
			return ret;
		netdev_err(ndev, "couldn't get regulators %d\n",
				ret);
	}

	/* Request optional RESET GPIO */
	pdata->reset_gpiod = devm_gpiod_get_optional(&pdev->dev,
						     "reset",
						     GPIOD_OUT_LOW);

	/* Request clock */
	pdata->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pdata->clk))
		dev_dbg(&pdev->dev, "couldn't get clock %li\n",
			PTR_ERR(pdata->clk));

	return ret;
}

/*
 * Free resources, currently just regulators.
 *
 */
static void smsc911x_free_resources(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct smsc911x_data *pdata = netdev_priv(ndev);

	/* Free regulators */
	regulator_bulk_free(ARRAY_SIZE(pdata->supplies),
			pdata->supplies);

	/* Free clock */
	if (!IS_ERR(pdata->clk)) {
		clk_put(pdata->clk);
		pdata->clk = NULL;
	}
}

/* waits for MAC not busy, with timeout.  Only called by smsc911x_mac_read
 * and smsc911x_mac_write, so assumes mac_lock is held */
static int smsc911x_mac_complete(struct smsc911x_data *pdata)
{
	int i;
	u32 val;

	SMSC_ASSERT_MAC_LOCK(pdata);

	for (i = 0; i < 40; i++) {
		val = smsc911x_reg_read(pdata, MAC_CSR_CMD);
		if (!(val & MAC_CSR_CMD_CSR_BUSY_))
			return 0;
	}
	SMSC_WARN(pdata, hw, "Timed out waiting for MAC not BUSY. "
		  "MAC_CSR_CMD: 0x%08X", val);
	return -EIO;
}

/* Fetches a MAC register value. Assumes mac_lock is acquired */
static u32 smsc911x_mac_read(struct smsc911x_data *pdata, unsigned int offset)
{
	unsigned int temp;

	SMSC_ASSERT_MAC_LOCK(pdata);

	temp = smsc911x_reg_read(pdata, MAC_CSR_CMD);
	if (unlikely(temp & MAC_CSR_CMD_CSR_BUSY_)) {
		SMSC_WARN(pdata, hw, "MAC busy at entry");
		return 0xFFFFFFFF;
	}

	/* Send the MAC cmd */
	smsc911x_reg_write(pdata, MAC_CSR_CMD, ((offset & 0xFF) |
		MAC_CSR_CMD_CSR_BUSY_ | MAC_CSR_CMD_R_NOT_W_));

	/* Workaround for hardware read-after-write restriction */
	temp = smsc911x_reg_read(pdata, BYTE_TEST);

	/* Wait for the read to complete */
	if (likely(smsc911x_mac_complete(pdata) == 0))
		return smsc911x_reg_read(pdata, MAC_CSR_DATA);

	SMSC_WARN(pdata, hw, "MAC busy after read");
	return 0xFFFFFFFF;
}

/* Set a mac register, mac_lock must be acquired before calling */
static void smsc911x_mac_write(struct smsc911x_data *pdata,
			       unsigned int offset, u32 val)
{
	unsigned int temp;

	SMSC_ASSERT_MAC_LOCK(pdata);

	temp = smsc911x_reg_read(pdata, MAC_CSR_CMD);
	if (unlikely(temp & MAC_CSR_CMD_CSR_BUSY_)) {
		SMSC_WARN(pdata, hw,
			  "smsc911x_mac_write failed, MAC busy at entry");
		return;
	}

	/* Send data to write */
	smsc911x_reg_write(pdata, MAC_CSR_DATA, val);

	/* Write the actual data */
	smsc911x_reg_write(pdata, MAC_CSR_CMD, ((offset & 0xFF) |
		MAC_CSR_CMD_CSR_BUSY_));

	/* Workaround for hardware read-after-write restriction */
	temp = smsc911x_reg_read(pdata, BYTE_TEST);

	/* Wait for the write to complete */
	if (likely(smsc911x_mac_complete(pdata) == 0))
		return;

	SMSC_WARN(pdata, hw, "smsc911x_mac_write failed, MAC busy after write");
}

/* Get a phy register */
static int smsc911x_mii_read(struct mii_bus *bus, int phyaddr, int regidx)
{
	struct smsc911x_data *pdata = bus->priv;
	unsigned long flags;
	unsigned int addr;
	int i, reg;

	pm_runtime_get_sync(bus->parent);
	spin_lock_irqsave(&pdata->mac_lock, flags);

	/* Confirm MII not busy */
	if (unlikely(smsc911x_mac_read(pdata, MII_ACC) & MII_ACC_MII_BUSY_)) {
		SMSC_WARN(pdata, hw, "MII is busy in smsc911x_mii_read???");
		reg = -EIO;
		goto out;
	}

	/* Set the address, index & direction (read from PHY) */
	addr = ((phyaddr & 0x1F) << 11) | ((regidx & 0x1F) << 6);
	smsc911x_mac_write(pdata, MII_ACC, addr);

	/* Wait for read to complete w/ timeout */
	for (i = 0; i < 100; i++)
		if (!(smsc911x_mac_read(pdata, MII_ACC) & MII_ACC_MII_BUSY_)) {
			reg = smsc911x_mac_read(pdata, MII_DATA);
			goto out;
		}

	SMSC_WARN(pdata, hw, "Timed out waiting for MII read to finish");
	reg = -EIO;

out:
	spin_unlock_irqrestore(&pdata->mac_lock, flags);
	pm_runtime_put(bus->parent);
	return reg;
}

/* Set a phy register */
static int smsc911x_mii_write(struct mii_bus *bus, int phyaddr, int regidx,
			   u16 val)
{
	struct smsc911x_data *pdata = bus->priv;
	unsigned long flags;
	unsigned int addr;
	int i, reg;

	pm_runtime_get_sync(bus->parent);
	spin_lock_irqsave(&pdata->mac_lock, flags);

	/* Confirm MII not busy */
	if (unlikely(smsc911x_mac_read(pdata, MII_ACC) & MII_ACC_MII_BUSY_)) {
		SMSC_WARN(pdata, hw, "MII is busy in smsc911x_mii_write???");
		reg = -EIO;
		goto out;
	}

	/* Put the data to write in the MAC */
	smsc911x_mac_write(pdata, MII_DATA, val);

	/* Set the address, index & direction (write to PHY) */
	addr = ((phyaddr & 0x1F) << 11) | ((regidx & 0x1F) << 6) |
		MII_ACC_MII_WRITE_;
	smsc911x_mac_write(pdata, MII_ACC, addr);

	/* Wait for write to complete w/ timeout */
	for (i = 0; i < 100; i++)
		if (!(smsc911x_mac_read(pdata, MII_ACC) & MII_ACC_MII_BUSY_)) {
			reg = 0;
			goto out;
		}

	SMSC_WARN(pdata, hw, "Timed out waiting for MII write to finish");
	reg = -EIO;

out:
	spin_unlock_irqrestore(&pdata->mac_lock, flags);
	pm_runtime_put(bus->parent);
	return reg;
}

/* Switch to external phy. Assumes tx and rx are stopped. */
static void smsc911x_phy_enable_external(struct smsc911x_data *pdata)
{
	unsigned int hwcfg = smsc911x_reg_read(pdata, HW_CFG);

	/* Disable phy clocks to the MAC */
	hwcfg &= (~HW_CFG_PHY_CLK_SEL_);
	hwcfg |= HW_CFG_PHY_CLK_SEL_CLK_DIS_;
	smsc911x_reg_write(pdata, HW_CFG, hwcfg);
	udelay(10);	/* Enough time for clocks to stop */

	/* Switch to external phy */
	hwcfg |= HW_CFG_EXT_PHY_EN_;
	smsc911x_reg_write(pdata, HW_CFG, hwcfg);

	/* Enable phy clocks to the MAC */
	hwcfg &= (~HW_CFG_PHY_CLK_SEL_);
	hwcfg |= HW_CFG_PHY_CLK_SEL_EXT_PHY_;
	smsc911x_reg_write(pdata, HW_CFG, hwcfg);
	udelay(10);	/* Enough time for clocks to restart */

	hwcfg |= HW_CFG_SMI_SEL_;
	smsc911x_reg_write(pdata, HW_CFG, hwcfg);
}

/* Autodetects and enables external phy if present on supported chips.
 * autodetection can be overridden by specifying SMSC911X_FORCE_INTERNAL_PHY
 * or SMSC911X_FORCE_EXTERNAL_PHY in the platform_data flags. */
static void smsc911x_phy_initialise_external(struct smsc911x_data *pdata)
{
	unsigned int hwcfg = smsc911x_reg_read(pdata, HW_CFG);

	if (pdata->config.flags & SMSC911X_FORCE_INTERNAL_PHY) {
		SMSC_TRACE(pdata, hw, "Forcing internal PHY");
		pdata->using_extphy = 0;
	} else if (pdata->config.flags & SMSC911X_FORCE_EXTERNAL_PHY) {
		SMSC_TRACE(pdata, hw, "Forcing external PHY");
		smsc911x_phy_enable_external(pdata);
		pdata->using_extphy = 1;
	} else if (hwcfg & HW_CFG_EXT_PHY_DET_) {
		SMSC_TRACE(pdata, hw,
			   "HW_CFG EXT_PHY_DET set, using external PHY");
		smsc911x_phy_enable_external(pdata);
		pdata->using_extphy = 1;
	} else {
		SMSC_TRACE(pdata, hw,
			   "HW_CFG EXT_PHY_DET clear, using internal PHY");
		pdata->using_extphy = 0;
	}
}

/* Fetches a tx status out of the status fifo */
static unsigned int smsc911x_tx_get_txstatus(struct smsc911x_data *pdata)
{
	unsigned int result =
	    smsc911x_reg_read(pdata, TX_FIFO_INF) & TX_FIFO_INF_TSUSED_;

	if (result != 0)
		result = smsc911x_reg_read(pdata, TX_STATUS_FIFO);

	return result;
}

/* Fetches the next rx status */
static unsigned int smsc911x_rx_get_rxstatus(struct smsc911x_data *pdata)
{
	unsigned int result =
	    smsc911x_reg_read(pdata, RX_FIFO_INF) & RX_FIFO_INF_RXSUSED_;

	if (result != 0)
		result = smsc911x_reg_read(pdata, RX_STATUS_FIFO);

	return result;
}

#ifdef USE_PHY_WORK_AROUND
static int smsc911x_phy_check_loopbackpkt(struct smsc911x_data *pdata)
{
	unsigned int tries;
	u32 wrsz;
	u32 rdsz;
	ulong bufp;

	for (tries = 0; tries < 10; tries++) {
		unsigned int txcmd_a;
		unsigned int txcmd_b;
		unsigned int status;
		unsigned int pktlength;
		unsigned int i;

		/* Zero-out rx packet memory */
		memset(pdata->loopback_rx_pkt, 0, MIN_PACKET_SIZE);

		/* Write tx packet to 118 */
		txcmd_a = (u32)((ulong)pdata->loopback_tx_pkt & 0x03) << 16;
		txcmd_a |= TX_CMD_A_FIRST_SEG_ | TX_CMD_A_LAST_SEG_;
		txcmd_a |= MIN_PACKET_SIZE;

		txcmd_b = MIN_PACKET_SIZE << 16 | MIN_PACKET_SIZE;

		smsc911x_reg_write(pdata, TX_DATA_FIFO, txcmd_a);
		smsc911x_reg_write(pdata, TX_DATA_FIFO, txcmd_b);

		bufp = (ulong)pdata->loopback_tx_pkt & (~0x3);
		wrsz = MIN_PACKET_SIZE + 3;
		wrsz += (u32)((ulong)pdata->loopback_tx_pkt & 0x3);
		wrsz >>= 2;

		pdata->ops->tx_writefifo(pdata, (unsigned int *)bufp, wrsz);

		/* Wait till transmit is done */
		i = 60;
		do {
			udelay(5);
			status = smsc911x_tx_get_txstatus(pdata);
		} while ((i--) && (!status));

		if (!status) {
			SMSC_WARN(pdata, hw,
				  "Failed to transmit during loopback test");
			continue;
		}
		if (status & TX_STS_ES_) {
			SMSC_WARN(pdata, hw,
				  "Transmit encountered errors during loopback test");
			continue;
		}

		/* Wait till receive is done */
		i = 60;
		do {
			udelay(5);
			status = smsc911x_rx_get_rxstatus(pdata);
		} while ((i--) && (!status));

		if (!status) {
			SMSC_WARN(pdata, hw,
				  "Failed to receive during loopback test");
			continue;
		}
		if (status & RX_STS_ES_) {
			SMSC_WARN(pdata, hw,
				  "Receive encountered errors during loopback test");
			continue;
		}

		pktlength = ((status & 0x3FFF0000UL) >> 16);
		bufp = (ulong)pdata->loopback_rx_pkt;
		rdsz = pktlength + 3;
		rdsz += (u32)((ulong)pdata->loopback_rx_pkt & 0x3);
		rdsz >>= 2;

		pdata->ops->rx_readfifo(pdata, (unsigned int *)bufp, rdsz);

		if (pktlength != (MIN_PACKET_SIZE + 4)) {
			SMSC_WARN(pdata, hw, "Unexpected packet size "
				  "during loop back test, size=%d, will retry",
				  pktlength);
		} else {
			unsigned int j;
			int mismatch = 0;
			for (j = 0; j < MIN_PACKET_SIZE; j++) {
				if (pdata->loopback_tx_pkt[j]
				    != pdata->loopback_rx_pkt[j]) {
					mismatch = 1;
					break;
				}
			}
			if (!mismatch) {
				SMSC_TRACE(pdata, hw, "Successfully verified "
					   "loopback packet");
				return 0;
			} else {
				SMSC_WARN(pdata, hw, "Data mismatch "
					  "during loop back test, will retry");
			}
		}
	}

	return -EIO;
}

static int smsc911x_phy_reset(struct smsc911x_data *pdata)
{
	unsigned int temp;
	unsigned int i = 100000;

	temp = smsc911x_reg_read(pdata, PMT_CTRL);
	smsc911x_reg_write(pdata, PMT_CTRL, temp | PMT_CTRL_PHY_RST_);
	do {
		msleep(1);
		temp = smsc911x_reg_read(pdata, PMT_CTRL);
	} while ((i--) && (temp & PMT_CTRL_PHY_RST_));

	if (unlikely(temp & PMT_CTRL_PHY_RST_)) {
		SMSC_WARN(pdata, hw, "PHY reset failed to complete");
		return -EIO;
	}
	/* Extra delay required because the phy may not be completed with
	* its reset when BMCR_RESET is cleared. Specs say 256 uS is
	* enough delay but using 1ms here to be safe */
	msleep(1);

	return 0;
}

static int smsc911x_phy_loopbacktest(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	struct phy_device *phy_dev = dev->phydev;
	int result = -EIO;
	unsigned int i, val;
	unsigned long flags;

	/* Initialise tx packet using broadcast destination address */
	eth_broadcast_addr(pdata->loopback_tx_pkt);

	/* Use incrementing source address */
	for (i = 6; i < 12; i++)
		pdata->loopback_tx_pkt[i] = (char)i;

	/* Set length type field */
	pdata->loopback_tx_pkt[12] = 0x00;
	pdata->loopback_tx_pkt[13] = 0x00;

	for (i = 14; i < MIN_PACKET_SIZE; i++)
		pdata->loopback_tx_pkt[i] = (char)i;

	val = smsc911x_reg_read(pdata, HW_CFG);
	val &= HW_CFG_TX_FIF_SZ_;
	val |= HW_CFG_SF_;
	smsc911x_reg_write(pdata, HW_CFG, val);

	smsc911x_reg_write(pdata, TX_CFG, TX_CFG_TX_ON_);
	smsc911x_reg_write(pdata, RX_CFG,
		(u32)((ulong)pdata->loopback_rx_pkt & 0x03) << 8);

	for (i = 0; i < 10; i++) {
		/* Set PHY to 10/FD, no ANEG, and loopback mode */
		smsc911x_mii_write(phy_dev->mdio.bus, phy_dev->mdio.addr,
				   MII_BMCR, BMCR_LOOPBACK | BMCR_FULLDPLX);

		/* Enable MAC tx/rx, FD */
		spin_lock_irqsave(&pdata->mac_lock, flags);
		smsc911x_mac_write(pdata, MAC_CR, MAC_CR_FDPX_
				   | MAC_CR_TXEN_ | MAC_CR_RXEN_);
		spin_unlock_irqrestore(&pdata->mac_lock, flags);

		if (smsc911x_phy_check_loopbackpkt(pdata) == 0) {
			result = 0;
			break;
		}
		pdata->resetcount++;

		/* Disable MAC rx */
		spin_lock_irqsave(&pdata->mac_lock, flags);
		smsc911x_mac_write(pdata, MAC_CR, 0);
		spin_unlock_irqrestore(&pdata->mac_lock, flags);

		smsc911x_phy_reset(pdata);
	}

	/* Disable MAC */
	spin_lock_irqsave(&pdata->mac_lock, flags);
	smsc911x_mac_write(pdata, MAC_CR, 0);
	spin_unlock_irqrestore(&pdata->mac_lock, flags);

	/* Cancel PHY loopback mode */
	smsc911x_mii_write(phy_dev->mdio.bus, phy_dev->mdio.addr, MII_BMCR, 0);

	smsc911x_reg_write(pdata, TX_CFG, 0);
	smsc911x_reg_write(pdata, RX_CFG, 0);

	return result;
}
#endif				/* USE_PHY_WORK_AROUND */

static void smsc911x_phy_update_flowcontrol(struct smsc911x_data *pdata)
{
	struct net_device *ndev = pdata->dev;
	struct phy_device *phy_dev = ndev->phydev;
	u32 afc = smsc911x_reg_read(pdata, AFC_CFG);
	u32 flow;
	unsigned long flags;

	if (phy_dev->duplex == DUPLEX_FULL) {
		u16 lcladv = phy_read(phy_dev, MII_ADVERTISE);
		u16 rmtadv = phy_read(phy_dev, MII_LPA);
		u8 cap = mii_resolve_flowctrl_fdx(lcladv, rmtadv);

		if (cap & FLOW_CTRL_RX)
			flow = 0xFFFF0002;
		else
			flow = 0;

		if (cap & FLOW_CTRL_TX)
			afc |= 0xF;
		else
			afc &= ~0xF;

		SMSC_TRACE(pdata, hw, "rx pause %s, tx pause %s",
			   (cap & FLOW_CTRL_RX ? "enabled" : "disabled"),
			   (cap & FLOW_CTRL_TX ? "enabled" : "disabled"));
	} else {
		SMSC_TRACE(pdata, hw, "half duplex");
		flow = 0;
		afc |= 0xF;
	}

	spin_lock_irqsave(&pdata->mac_lock, flags);
	smsc911x_mac_write(pdata, FLOW, flow);
	spin_unlock_irqrestore(&pdata->mac_lock, flags);

	smsc911x_reg_write(pdata, AFC_CFG, afc);
}

/* Update link mode if anything has changed.  Called periodically when the
 * PHY is in polling mode, even if nothing has changed. */
static void smsc911x_phy_adjust_link(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	struct phy_device *phy_dev = dev->phydev;
	unsigned long flags;
	int carrier;

	if (phy_dev->duplex != pdata->last_duplex) {
		unsigned int mac_cr;
		SMSC_TRACE(pdata, hw, "duplex state has changed");

		spin_lock_irqsave(&pdata->mac_lock, flags);
		mac_cr = smsc911x_mac_read(pdata, MAC_CR);
		if (phy_dev->duplex) {
			SMSC_TRACE(pdata, hw,
				   "configuring for full duplex mode");
			mac_cr |= MAC_CR_FDPX_;
		} else {
			SMSC_TRACE(pdata, hw,
				   "configuring for half duplex mode");
			mac_cr &= ~MAC_CR_FDPX_;
		}
		smsc911x_mac_write(pdata, MAC_CR, mac_cr);
		spin_unlock_irqrestore(&pdata->mac_lock, flags);

		smsc911x_phy_update_flowcontrol(pdata);
		pdata->last_duplex = phy_dev->duplex;
	}

	carrier = netif_carrier_ok(dev);
	if (carrier != pdata->last_carrier) {
		SMSC_TRACE(pdata, hw, "carrier state has changed");
		if (carrier) {
			SMSC_TRACE(pdata, hw, "configuring for carrier OK");
			if ((pdata->gpio_orig_setting & GPIO_CFG_LED1_EN_) &&
			    (!pdata->using_extphy)) {
				/* Restore original GPIO configuration */
				pdata->gpio_setting = pdata->gpio_orig_setting;
				smsc911x_reg_write(pdata, GPIO_CFG,
					pdata->gpio_setting);
			}
		} else {
			SMSC_TRACE(pdata, hw, "configuring for no carrier");
			/* Check global setting that LED1
			 * usage is 10/100 indicator */
			pdata->gpio_setting = smsc911x_reg_read(pdata,
				GPIO_CFG);
			if ((pdata->gpio_setting & GPIO_CFG_LED1_EN_) &&
			    (!pdata->using_extphy)) {
				/* Force 10/100 LED off, after saving
				 * original GPIO configuration */
				pdata->gpio_orig_setting = pdata->gpio_setting;

				pdata->gpio_setting &= ~GPIO_CFG_LED1_EN_;
				pdata->gpio_setting |= (GPIO_CFG_GPIOBUF0_
							| GPIO_CFG_GPIODIR0_
							| GPIO_CFG_GPIOD0_);
				smsc911x_reg_write(pdata, GPIO_CFG,
					pdata->gpio_setting);
			}
		}
		pdata->last_carrier = carrier;
	}
}

static int smsc911x_mii_probe(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	struct phy_device *phydev;
	int ret;

	/* find the first phy */
	phydev = phy_find_first(pdata->mii_bus);
	if (!phydev) {
		netdev_err(dev, "no PHY found\n");
		return -ENODEV;
	}

	SMSC_TRACE(pdata, probe, "PHY: addr %d, phy_id 0x%08X",
		   phydev->mdio.addr, phydev->phy_id);

	ret = phy_connect_direct(dev, phydev, &smsc911x_phy_adjust_link,
				 pdata->config.phy_interface);

	if (ret) {
		netdev_err(dev, "Could not attach to PHY\n");
		return ret;
	}

	phy_attached_info(phydev);

	phy_set_max_speed(phydev, SPEED_100);

	/* mask with MAC supported features */
	phy_support_asym_pause(phydev);

	pdata->last_duplex = -1;
	pdata->last_carrier = -1;

#ifdef USE_PHY_WORK_AROUND
	if (smsc911x_phy_loopbacktest(dev) < 0) {
		SMSC_WARN(pdata, hw, "Failed Loop Back Test");
		phy_disconnect(phydev);
		return -ENODEV;
	}
	SMSC_TRACE(pdata, hw, "Passed Loop Back Test");
#endif				/* USE_PHY_WORK_AROUND */

	SMSC_TRACE(pdata, hw, "phy initialised successfully");
	return 0;
}

static int smsc911x_mii_init(struct platform_device *pdev,
			     struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	struct phy_device *phydev;
	int err = -ENXIO;

	pdata->mii_bus = mdiobus_alloc();
	if (!pdata->mii_bus) {
		err = -ENOMEM;
		goto err_out_1;
	}

	pdata->mii_bus->name = SMSC_MDIONAME;
	snprintf(pdata->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		pdev->name, pdev->id);
	pdata->mii_bus->priv = pdata;
	pdata->mii_bus->read = smsc911x_mii_read;
	pdata->mii_bus->write = smsc911x_mii_write;

	pdata->mii_bus->parent = &pdev->dev;

	switch (pdata->idrev & 0xFFFF0000) {
	case 0x01170000:
	case 0x01150000:
	case 0x117A0000:
	case 0x115A0000:
		/* External PHY supported, try to autodetect */
		smsc911x_phy_initialise_external(pdata);
		break;
	default:
		SMSC_TRACE(pdata, hw, "External PHY is not supported, "
			   "using internal PHY");
		pdata->using_extphy = 0;
		break;
	}

	if (!pdata->using_extphy) {
		/* Mask all PHYs except ID 1 (internal) */
		pdata->mii_bus->phy_mask = ~(1 << 1);
	}

	if (mdiobus_register(pdata->mii_bus)) {
		SMSC_WARN(pdata, probe, "Error registering mii bus");
		goto err_out_free_bus_2;
	}

	phydev = phy_find_first(pdata->mii_bus);
	if (phydev)
		phydev->mac_managed_pm = true;

	return 0;

err_out_free_bus_2:
	mdiobus_free(pdata->mii_bus);
err_out_1:
	return err;
}

/* Gets the number of tx statuses in the fifo */
static unsigned int smsc911x_tx_get_txstatcount(struct smsc911x_data *pdata)
{
	return (smsc911x_reg_read(pdata, TX_FIFO_INF)
		& TX_FIFO_INF_TSUSED_) >> 16;
}

/* Reads tx statuses and increments counters where necessary */
static void smsc911x_tx_update_txcounters(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	unsigned int tx_stat;

	while ((tx_stat = smsc911x_tx_get_txstatus(pdata)) != 0) {
		if (unlikely(tx_stat & 0x80000000)) {
			/* In this driver the packet tag is used as the packet
			 * length. Since a packet length can never reach the
			 * size of 0x8000, this bit is reserved. It is worth
			 * noting that the "reserved bit" in the warning above
			 * does not reference a hardware defined reserved bit
			 * but rather a driver defined one.
			 */
			SMSC_WARN(pdata, hw, "Packet tag reserved bit is high");
		} else {
			if (unlikely(tx_stat & TX_STS_ES_)) {
				dev->stats.tx_errors++;
			} else {
				dev->stats.tx_packets++;
				dev->stats.tx_bytes += (tx_stat >> 16);
			}
			if (unlikely(tx_stat & TX_STS_EXCESS_COL_)) {
				dev->stats.collisions += 16;
				dev->stats.tx_aborted_errors += 1;
			} else {
				dev->stats.collisions +=
				    ((tx_stat >> 3) & 0xF);
			}
			if (unlikely(tx_stat & TX_STS_LOST_CARRIER_))
				dev->stats.tx_carrier_errors += 1;
			if (unlikely(tx_stat & TX_STS_LATE_COL_)) {
				dev->stats.collisions++;
				dev->stats.tx_aborted_errors++;
			}
		}
	}
}

/* Increments the Rx error counters */
static void
smsc911x_rx_counterrors(struct net_device *dev, unsigned int rxstat)
{
	int crc_err = 0;

	if (unlikely(rxstat & RX_STS_ES_)) {
		dev->stats.rx_errors++;
		if (unlikely(rxstat & RX_STS_CRC_ERR_)) {
			dev->stats.rx_crc_errors++;
			crc_err = 1;
		}
	}
	if (likely(!crc_err)) {
		if (unlikely((rxstat & RX_STS_FRAME_TYPE_) &&
			     (rxstat & RX_STS_LENGTH_ERR_)))
			dev->stats.rx_length_errors++;
		if (rxstat & RX_STS_MCAST_)
			dev->stats.multicast++;
	}
}

/* Quickly dumps bad packets */
static void
smsc911x_rx_fastforward(struct smsc911x_data *pdata, unsigned int pktwords)
{
	if (likely(pktwords >= 4)) {
		unsigned int timeout = 500;
		unsigned int val;
		smsc911x_reg_write(pdata, RX_DP_CTRL, RX_DP_CTRL_RX_FFWD_);
		do {
			udelay(1);
			val = smsc911x_reg_read(pdata, RX_DP_CTRL);
		} while ((val & RX_DP_CTRL_RX_FFWD_) && --timeout);

		if (unlikely(timeout == 0))
			SMSC_WARN(pdata, hw, "Timed out waiting for "
				  "RX FFWD to finish, RX_DP_CTRL: 0x%08X", val);
	} else {
		while (pktwords--)
			smsc911x_reg_read(pdata, RX_DATA_FIFO);
	}
}

/* NAPI poll function */
static int smsc911x_poll(struct napi_struct *napi, int budget)
{
	struct smsc911x_data *pdata =
		container_of(napi, struct smsc911x_data, napi);
	struct net_device *dev = pdata->dev;
	int npackets = 0;

	while (npackets < budget) {
		unsigned int pktlength;
		unsigned int pktwords;
		struct sk_buff *skb;
		unsigned int rxstat = smsc911x_rx_get_rxstatus(pdata);

		if (!rxstat) {
			unsigned int temp;
			/* We processed all packets available.  Tell NAPI it can
			 * stop polling then re-enable rx interrupts */
			smsc911x_reg_write(pdata, INT_STS, INT_STS_RSFL_);
			napi_complete(napi);
			temp = smsc911x_reg_read(pdata, INT_EN);
			temp |= INT_EN_RSFL_EN_;
			smsc911x_reg_write(pdata, INT_EN, temp);
			break;
		}

		/* Count packet for NAPI scheduling, even if it has an error.
		 * Error packets still require cycles to discard */
		npackets++;

		pktlength = ((rxstat & 0x3FFF0000) >> 16);
		pktwords = (pktlength + NET_IP_ALIGN + 3) >> 2;
		smsc911x_rx_counterrors(dev, rxstat);

		if (unlikely(rxstat & RX_STS_ES_)) {
			SMSC_WARN(pdata, rx_err,
				  "Discarding packet with error bit set");
			/* Packet has an error, discard it and continue with
			 * the next */
			smsc911x_rx_fastforward(pdata, pktwords);
			dev->stats.rx_dropped++;
			continue;
		}

		skb = netdev_alloc_skb(dev, pktwords << 2);
		if (unlikely(!skb)) {
			SMSC_WARN(pdata, rx_err,
				  "Unable to allocate skb for rx packet");
			/* Drop the packet and stop this polling iteration */
			smsc911x_rx_fastforward(pdata, pktwords);
			dev->stats.rx_dropped++;
			break;
		}

		pdata->ops->rx_readfifo(pdata,
				 (unsigned int *)skb->data, pktwords);

		/* Align IP on 16B boundary */
		skb_reserve(skb, NET_IP_ALIGN);
		skb_put(skb, pktlength - 4);
		skb->protocol = eth_type_trans(skb, dev);
		skb_checksum_none_assert(skb);
		netif_receive_skb(skb);

		/* Update counters */
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += (pktlength - 4);
	}

	/* Return total received packets */
	return npackets;
}

/* Returns hash bit number for given MAC address
 * Example:
 * 01 00 5E 00 00 01 -> returns bit number 31 */
static unsigned int smsc911x_hash(char addr[ETH_ALEN])
{
	return (ether_crc(ETH_ALEN, addr) >> 26) & 0x3f;
}

static void smsc911x_rx_multicast_update(struct smsc911x_data *pdata)
{
	/* Performs the multicast & mac_cr update.  This is called when
	 * safe on the current hardware, and with the mac_lock held */
	unsigned int mac_cr;

	SMSC_ASSERT_MAC_LOCK(pdata);

	mac_cr = smsc911x_mac_read(pdata, MAC_CR);
	mac_cr |= pdata->set_bits_mask;
	mac_cr &= ~(pdata->clear_bits_mask);
	smsc911x_mac_write(pdata, MAC_CR, mac_cr);
	smsc911x_mac_write(pdata, HASHH, pdata->hashhi);
	smsc911x_mac_write(pdata, HASHL, pdata->hashlo);
	SMSC_TRACE(pdata, hw, "maccr 0x%08X, HASHH 0x%08X, HASHL 0x%08X",
		   mac_cr, pdata->hashhi, pdata->hashlo);
}

static void smsc911x_rx_multicast_update_workaround(struct smsc911x_data *pdata)
{
	unsigned int mac_cr;

	/* This function is only called for older LAN911x devices
	 * (revA or revB), where MAC_CR, HASHH and HASHL should not
	 * be modified during Rx - newer devices immediately update the
	 * registers.
	 *
	 * This is called from interrupt context */

	spin_lock(&pdata->mac_lock);

	/* Check Rx has stopped */
	if (smsc911x_mac_read(pdata, MAC_CR) & MAC_CR_RXEN_)
		SMSC_WARN(pdata, drv, "Rx not stopped");

	/* Perform the update - safe to do now Rx has stopped */
	smsc911x_rx_multicast_update(pdata);

	/* Re-enable Rx */
	mac_cr = smsc911x_mac_read(pdata, MAC_CR);
	mac_cr |= MAC_CR_RXEN_;
	smsc911x_mac_write(pdata, MAC_CR, mac_cr);

	pdata->multicast_update_pending = 0;

	spin_unlock(&pdata->mac_lock);
}

static int smsc911x_phy_general_power_up(struct smsc911x_data *pdata)
{
	struct net_device *ndev = pdata->dev;
	struct phy_device *phy_dev = ndev->phydev;
	int rc = 0;

	if (!phy_dev)
		return rc;

	/* If the internal PHY is in General Power-Down mode, all, except the
	 * management interface, is powered-down and stays in that condition as
	 * long as Phy register bit 0.11 is HIGH.
	 *
	 * In that case, clear the bit 0.11, so the PHY powers up and we can
	 * access to the phy registers.
	 */
	rc = phy_read(phy_dev, MII_BMCR);
	if (rc < 0) {
		SMSC_WARN(pdata, drv, "Failed reading PHY control reg");
		return rc;
	}

	/* If the PHY general power-down bit is not set is not necessary to
	 * disable the general power down-mode.
	 */
	if (rc & BMCR_PDOWN) {
		rc = phy_write(phy_dev, MII_BMCR, rc & ~BMCR_PDOWN);
		if (rc < 0) {
			SMSC_WARN(pdata, drv, "Failed writing PHY control reg");
			return rc;
		}

		usleep_range(1000, 1500);
	}

	return 0;
}

static int smsc911x_phy_disable_energy_detect(struct smsc911x_data *pdata)
{
	struct net_device *ndev = pdata->dev;
	struct phy_device *phy_dev = ndev->phydev;
	int rc = 0;

	if (!phy_dev)
		return rc;

	rc = phy_read(phy_dev, MII_LAN83C185_CTRL_STATUS);

	if (rc < 0) {
		SMSC_WARN(pdata, drv, "Failed reading PHY control reg");
		return rc;
	}

	/* Only disable if energy detect mode is already enabled */
	if (rc & MII_LAN83C185_EDPWRDOWN) {
		/* Disable energy detect mode for this SMSC Transceivers */
		rc = phy_write(phy_dev, MII_LAN83C185_CTRL_STATUS,
			       rc & (~MII_LAN83C185_EDPWRDOWN));

		if (rc < 0) {
			SMSC_WARN(pdata, drv, "Failed writing PHY control reg");
			return rc;
		}
		/* Allow PHY to wakeup */
		mdelay(2);
	}

	return 0;
}

static int smsc911x_phy_enable_energy_detect(struct smsc911x_data *pdata)
{
	struct net_device *ndev = pdata->dev;
	struct phy_device *phy_dev = ndev->phydev;
	int rc = 0;

	if (!phy_dev)
		return rc;

	rc = phy_read(phy_dev, MII_LAN83C185_CTRL_STATUS);

	if (rc < 0) {
		SMSC_WARN(pdata, drv, "Failed reading PHY control reg");
		return rc;
	}

	/* Only enable if energy detect mode is already disabled */
	if (!(rc & MII_LAN83C185_EDPWRDOWN)) {
		/* Enable energy detect mode for this SMSC Transceivers */
		rc = phy_write(phy_dev, MII_LAN83C185_CTRL_STATUS,
			       rc | MII_LAN83C185_EDPWRDOWN);

		if (rc < 0) {
			SMSC_WARN(pdata, drv, "Failed writing PHY control reg");
			return rc;
		}
	}
	return 0;
}

static int smsc911x_soft_reset(struct smsc911x_data *pdata)
{
	unsigned int timeout;
	unsigned int temp;
	int ret;
	unsigned int reset_offset = HW_CFG;
	unsigned int reset_mask = HW_CFG_SRST_;

	/*
	 * Make sure to power-up the PHY chip before doing a reset, otherwise
	 * the reset fails.
	 */
	ret = smsc911x_phy_general_power_up(pdata);
	if (ret) {
		SMSC_WARN(pdata, drv, "Failed to power-up the PHY chip");
		return ret;
	}

	/*
	 * LAN9210/LAN9211/LAN9220/LAN9221 chips have an internal PHY that
	 * are initialized in a Energy Detect Power-Down mode that prevents
	 * the MAC chip to be software reseted. So we have to wakeup the PHY
	 * before.
	 */
	if (pdata->generation == 4) {
		ret = smsc911x_phy_disable_energy_detect(pdata);

		if (ret) {
			SMSC_WARN(pdata, drv, "Failed to wakeup the PHY chip");
			return ret;
		}
	}

	if ((pdata->idrev & 0xFFFF0000) == LAN9250) {
		/* special reset for  LAN9250 */
		reset_offset = RESET_CTL;
		reset_mask = RESET_CTL_DIGITAL_RST_;
	}

	/* Reset the LAN911x */
	smsc911x_reg_write(pdata, reset_offset, reset_mask);

	/* verify reset bit is cleared */
	timeout = 10;
	do {
		udelay(10);
		temp = smsc911x_reg_read(pdata, reset_offset);
	} while ((--timeout) && (temp & reset_mask));

	if (unlikely(temp & reset_mask)) {
		SMSC_WARN(pdata, drv, "Failed to complete reset");
		return -EIO;
	}

	if (pdata->generation == 4) {
		ret = smsc911x_phy_enable_energy_detect(pdata);

		if (ret) {
			SMSC_WARN(pdata, drv, "Failed to wakeup the PHY chip");
			return ret;
		}
	}

	return 0;
}

/* Sets the device MAC address to dev_addr, called with mac_lock held */
static void
smsc911x_set_hw_mac_address(struct smsc911x_data *pdata, const u8 dev_addr[6])
{
	u32 mac_high16 = (dev_addr[5] << 8) | dev_addr[4];
	u32 mac_low32 = (dev_addr[3] << 24) | (dev_addr[2] << 16) |
	    (dev_addr[1] << 8) | dev_addr[0];

	SMSC_ASSERT_MAC_LOCK(pdata);

	smsc911x_mac_write(pdata, ADDRH, mac_high16);
	smsc911x_mac_write(pdata, ADDRL, mac_low32);
}

static void smsc911x_disable_irq_chip(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);

	smsc911x_reg_write(pdata, INT_EN, 0);
	smsc911x_reg_write(pdata, INT_STS, 0xFFFFFFFF);
}

static irqreturn_t smsc911x_irqhandler(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct smsc911x_data *pdata = netdev_priv(dev);
	u32 intsts = smsc911x_reg_read(pdata, INT_STS);
	u32 inten = smsc911x_reg_read(pdata, INT_EN);
	int serviced = IRQ_NONE;
	u32 temp;

	if (unlikely(intsts & inten & INT_STS_SW_INT_)) {
		temp = smsc911x_reg_read(pdata, INT_EN);
		temp &= (~INT_EN_SW_INT_EN_);
		smsc911x_reg_write(pdata, INT_EN, temp);
		smsc911x_reg_write(pdata, INT_STS, INT_STS_SW_INT_);
		pdata->software_irq_signal = 1;
		smp_wmb();
		serviced = IRQ_HANDLED;
	}

	if (unlikely(intsts & inten & INT_STS_RXSTOP_INT_)) {
		/* Called when there is a multicast update scheduled and
		 * it is now safe to complete the update */
		SMSC_TRACE(pdata, intr, "RX Stop interrupt");
		smsc911x_reg_write(pdata, INT_STS, INT_STS_RXSTOP_INT_);
		if (pdata->multicast_update_pending)
			smsc911x_rx_multicast_update_workaround(pdata);
		serviced = IRQ_HANDLED;
	}

	if (intsts & inten & INT_STS_TDFA_) {
		temp = smsc911x_reg_read(pdata, FIFO_INT);
		temp |= FIFO_INT_TX_AVAIL_LEVEL_;
		smsc911x_reg_write(pdata, FIFO_INT, temp);
		smsc911x_reg_write(pdata, INT_STS, INT_STS_TDFA_);
		netif_wake_queue(dev);
		serviced = IRQ_HANDLED;
	}

	if (unlikely(intsts & inten & INT_STS_RXE_)) {
		SMSC_TRACE(pdata, intr, "RX Error interrupt");
		smsc911x_reg_write(pdata, INT_STS, INT_STS_RXE_);
		serviced = IRQ_HANDLED;
	}

	if (likely(intsts & inten & INT_STS_RSFL_)) {
		if (likely(napi_schedule_prep(&pdata->napi))) {
			/* Disable Rx interrupts */
			temp = smsc911x_reg_read(pdata, INT_EN);
			temp &= (~INT_EN_RSFL_EN_);
			smsc911x_reg_write(pdata, INT_EN, temp);
			/* Schedule a NAPI poll */
			__napi_schedule(&pdata->napi);
		} else {
			SMSC_WARN(pdata, rx_err, "napi_schedule_prep failed");
		}
		serviced = IRQ_HANDLED;
	}

	return serviced;
}

static int smsc911x_open(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	unsigned int timeout;
	unsigned int temp;
	unsigned int intcfg;
	int retval;
	int irq_flags;

	pm_runtime_get_sync(dev->dev.parent);

	/* find and start the given phy */
	if (!dev->phydev) {
		retval = smsc911x_mii_probe(dev);
		if (retval < 0) {
			SMSC_WARN(pdata, probe, "Error starting phy");
			goto out;
		}
	}

	/* Reset the LAN911x */
	retval = smsc911x_soft_reset(pdata);
	if (retval) {
		SMSC_WARN(pdata, hw, "soft reset failed");
		goto mii_free_out;
	}

	smsc911x_reg_write(pdata, HW_CFG, 0x00050000);
	smsc911x_reg_write(pdata, AFC_CFG, 0x006E3740);

	/* Increase the legal frame size of VLAN tagged frames to 1522 bytes */
	spin_lock_irq(&pdata->mac_lock);
	smsc911x_mac_write(pdata, VLAN1, ETH_P_8021Q);
	spin_unlock_irq(&pdata->mac_lock);

	/* Make sure EEPROM has finished loading before setting GPIO_CFG */
	timeout = 50;
	while ((smsc911x_reg_read(pdata, E2P_CMD) & E2P_CMD_EPC_BUSY_) &&
	       --timeout) {
		udelay(10);
	}

	if (unlikely(timeout == 0))
		SMSC_WARN(pdata, ifup,
			  "Timed out waiting for EEPROM busy bit to clear");

	smsc911x_reg_write(pdata, GPIO_CFG, 0x70070000);

	/* The soft reset above cleared the device's MAC address,
	 * restore it from local copy (set in probe) */
	spin_lock_irq(&pdata->mac_lock);
	smsc911x_set_hw_mac_address(pdata, dev->dev_addr);
	spin_unlock_irq(&pdata->mac_lock);

	/* Initialise irqs, but leave all sources disabled */
	smsc911x_disable_irq_chip(dev);

	/* Set interrupt deassertion to 100uS */
	intcfg = ((10 << 24) | INT_CFG_IRQ_EN_);

	if (pdata->config.irq_polarity) {
		SMSC_TRACE(pdata, ifup, "irq polarity: active high");
		intcfg |= INT_CFG_IRQ_POL_;
	} else {
		SMSC_TRACE(pdata, ifup, "irq polarity: active low");
	}

	if (pdata->config.irq_type) {
		SMSC_TRACE(pdata, ifup, "irq type: push-pull");
		intcfg |= INT_CFG_IRQ_TYPE_;
	} else {
		SMSC_TRACE(pdata, ifup, "irq type: open drain");
	}

	smsc911x_reg_write(pdata, INT_CFG, intcfg);

	SMSC_TRACE(pdata, ifup, "Testing irq handler using IRQ %d", dev->irq);
	pdata->software_irq_signal = 0;
	smp_wmb();

	irq_flags = irq_get_trigger_type(dev->irq);
	retval = request_irq(dev->irq, smsc911x_irqhandler,
			     irq_flags | IRQF_SHARED, dev->name, dev);
	if (retval) {
		SMSC_WARN(pdata, probe,
			  "Unable to claim requested irq: %d", dev->irq);
		goto mii_free_out;
	}

	temp = smsc911x_reg_read(pdata, INT_EN);
	temp |= INT_EN_SW_INT_EN_;
	smsc911x_reg_write(pdata, INT_EN, temp);

	timeout = 1000;
	while (timeout--) {
		if (pdata->software_irq_signal)
			break;
		msleep(1);
	}

	if (!pdata->software_irq_signal) {
		netdev_warn(dev, "ISR failed signaling test (IRQ %d)\n",
			    dev->irq);
		retval = -ENODEV;
		goto irq_stop_out;
	}
	SMSC_TRACE(pdata, ifup, "IRQ handler passed test using IRQ %d",
		   dev->irq);

	netdev_info(dev, "SMSC911x/921x identified at %#08lx, IRQ: %d\n",
		    (unsigned long)pdata->ioaddr, dev->irq);

	/* Reset the last known duplex and carrier */
	pdata->last_duplex = -1;
	pdata->last_carrier = -1;

	/* Bring the PHY up */
	phy_start(dev->phydev);

	temp = smsc911x_reg_read(pdata, HW_CFG);
	/* Preserve TX FIFO size and external PHY configuration */
	temp &= (HW_CFG_TX_FIF_SZ_|0x00000FFF);
	temp |= HW_CFG_SF_;
	smsc911x_reg_write(pdata, HW_CFG, temp);

	temp = smsc911x_reg_read(pdata, FIFO_INT);
	temp |= FIFO_INT_TX_AVAIL_LEVEL_;
	temp &= ~(FIFO_INT_RX_STS_LEVEL_);
	smsc911x_reg_write(pdata, FIFO_INT, temp);

	/* set RX Data offset to 2 bytes for alignment */
	smsc911x_reg_write(pdata, RX_CFG, (NET_IP_ALIGN << 8));

	/* enable NAPI polling before enabling RX interrupts */
	napi_enable(&pdata->napi);

	temp = smsc911x_reg_read(pdata, INT_EN);
	temp |= (INT_EN_TDFA_EN_ | INT_EN_RSFL_EN_ | INT_EN_RXSTOP_INT_EN_);
	smsc911x_reg_write(pdata, INT_EN, temp);

	spin_lock_irq(&pdata->mac_lock);
	temp = smsc911x_mac_read(pdata, MAC_CR);
	temp |= (MAC_CR_TXEN_ | MAC_CR_RXEN_ | MAC_CR_HBDIS_);
	smsc911x_mac_write(pdata, MAC_CR, temp);
	spin_unlock_irq(&pdata->mac_lock);

	smsc911x_reg_write(pdata, TX_CFG, TX_CFG_TX_ON_);

	netif_start_queue(dev);
	return 0;

irq_stop_out:
	free_irq(dev->irq, dev);
mii_free_out:
	phy_disconnect(dev->phydev);
out:
	pm_runtime_put(dev->dev.parent);
	return retval;
}

/* Entry point for stopping the interface */
static int smsc911x_stop(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	unsigned int temp;

	/* Disable all device interrupts */
	temp = smsc911x_reg_read(pdata, INT_CFG);
	temp &= ~INT_CFG_IRQ_EN_;
	smsc911x_reg_write(pdata, INT_CFG, temp);

	/* Stop Tx and Rx polling */
	netif_stop_queue(dev);
	napi_disable(&pdata->napi);

	/* At this point all Rx and Tx activity is stopped */
	dev->stats.rx_dropped += smsc911x_reg_read(pdata, RX_DROP);
	smsc911x_tx_update_txcounters(dev);

	free_irq(dev->irq, dev);

	/* Bring the PHY down */
	if (dev->phydev) {
		phy_stop(dev->phydev);
		phy_disconnect(dev->phydev);
	}
	netif_carrier_off(dev);
	pm_runtime_put(dev->dev.parent);

	SMSC_TRACE(pdata, ifdown, "Interface stopped");
	return 0;
}

/* Entry point for transmitting a packet */
static netdev_tx_t
smsc911x_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	unsigned int freespace;
	unsigned int tx_cmd_a;
	unsigned int tx_cmd_b;
	unsigned int temp;
	u32 wrsz;
	ulong bufp;

	freespace = smsc911x_reg_read(pdata, TX_FIFO_INF) & TX_FIFO_INF_TDFREE_;

	if (unlikely(freespace < TX_FIFO_LOW_THRESHOLD))
		SMSC_WARN(pdata, tx_err,
			  "Tx data fifo low, space available: %d", freespace);

	/* Word alignment adjustment */
	tx_cmd_a = (u32)((ulong)skb->data & 0x03) << 16;
	tx_cmd_a |= TX_CMD_A_FIRST_SEG_ | TX_CMD_A_LAST_SEG_;
	tx_cmd_a |= (unsigned int)skb->len;

	tx_cmd_b = ((unsigned int)skb->len) << 16;
	tx_cmd_b |= (unsigned int)skb->len;

	smsc911x_reg_write(pdata, TX_DATA_FIFO, tx_cmd_a);
	smsc911x_reg_write(pdata, TX_DATA_FIFO, tx_cmd_b);

	bufp = (ulong)skb->data & (~0x3);
	wrsz = (u32)skb->len + 3;
	wrsz += (u32)((ulong)skb->data & 0x3);
	wrsz >>= 2;

	pdata->ops->tx_writefifo(pdata, (unsigned int *)bufp, wrsz);
	freespace -= (skb->len + 32);
	skb_tx_timestamp(skb);
	dev_consume_skb_any(skb);

	if (unlikely(smsc911x_tx_get_txstatcount(pdata) >= 30))
		smsc911x_tx_update_txcounters(dev);

	if (freespace < TX_FIFO_LOW_THRESHOLD) {
		netif_stop_queue(dev);
		temp = smsc911x_reg_read(pdata, FIFO_INT);
		temp &= 0x00FFFFFF;
		temp |= 0x32000000;
		smsc911x_reg_write(pdata, FIFO_INT, temp);
	}

	return NETDEV_TX_OK;
}

/* Entry point for getting status counters */
static struct net_device_stats *smsc911x_get_stats(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	smsc911x_tx_update_txcounters(dev);
	dev->stats.rx_dropped += smsc911x_reg_read(pdata, RX_DROP);
	return &dev->stats;
}

/* Entry point for setting addressing modes */
static void smsc911x_set_multicast_list(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	unsigned long flags;

	if (dev->flags & IFF_PROMISC) {
		/* Enabling promiscuous mode */
		pdata->set_bits_mask = MAC_CR_PRMS_;
		pdata->clear_bits_mask = (MAC_CR_MCPAS_ | MAC_CR_HPFILT_);
		pdata->hashhi = 0;
		pdata->hashlo = 0;
	} else if (dev->flags & IFF_ALLMULTI) {
		/* Enabling all multicast mode */
		pdata->set_bits_mask = MAC_CR_MCPAS_;
		pdata->clear_bits_mask = (MAC_CR_PRMS_ | MAC_CR_HPFILT_);
		pdata->hashhi = 0;
		pdata->hashlo = 0;
	} else if (!netdev_mc_empty(dev)) {
		/* Enabling specific multicast addresses */
		unsigned int hash_high = 0;
		unsigned int hash_low = 0;
		struct netdev_hw_addr *ha;

		pdata->set_bits_mask = MAC_CR_HPFILT_;
		pdata->clear_bits_mask = (MAC_CR_PRMS_ | MAC_CR_MCPAS_);

		netdev_for_each_mc_addr(ha, dev) {
			unsigned int bitnum = smsc911x_hash(ha->addr);
			unsigned int mask = 0x01 << (bitnum & 0x1F);

			if (bitnum & 0x20)
				hash_high |= mask;
			else
				hash_low |= mask;
		}

		pdata->hashhi = hash_high;
		pdata->hashlo = hash_low;
	} else {
		/* Enabling local MAC address only */
		pdata->set_bits_mask = 0;
		pdata->clear_bits_mask =
		    (MAC_CR_PRMS_ | MAC_CR_MCPAS_ | MAC_CR_HPFILT_);
		pdata->hashhi = 0;
		pdata->hashlo = 0;
	}

	spin_lock_irqsave(&pdata->mac_lock, flags);

	if (pdata->generation <= 1) {
		/* Older hardware revision - cannot change these flags while
		 * receiving data */
		if (!pdata->multicast_update_pending) {
			unsigned int temp;
			SMSC_TRACE(pdata, hw, "scheduling mcast update");
			pdata->multicast_update_pending = 1;

			/* Request the hardware to stop, then perform the
			 * update when we get an RX_STOP interrupt */
			temp = smsc911x_mac_read(pdata, MAC_CR);
			temp &= ~(MAC_CR_RXEN_);
			smsc911x_mac_write(pdata, MAC_CR, temp);
		} else {
			/* There is another update pending, this should now
			 * use the newer values */
		}
	} else {
		/* Newer hardware revision - can write immediately */
		smsc911x_rx_multicast_update(pdata);
	}

	spin_unlock_irqrestore(&pdata->mac_lock, flags);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void smsc911x_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	smsc911x_irqhandler(0, dev);
	enable_irq(dev->irq);
}
#endif				/* CONFIG_NET_POLL_CONTROLLER */

static int smsc911x_set_mac_address(struct net_device *dev, void *p)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	struct sockaddr *addr = p;

	/* On older hardware revisions we cannot change the mac address
	 * registers while receiving data.  Newer devices can safely change
	 * this at any time. */
	if (pdata->generation <= 1 && netif_running(dev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(dev, addr->sa_data);

	spin_lock_irq(&pdata->mac_lock);
	smsc911x_set_hw_mac_address(pdata, dev->dev_addr);
	spin_unlock_irq(&pdata->mac_lock);

	netdev_info(dev, "MAC Address: %pM\n", dev->dev_addr);

	return 0;
}

static void smsc911x_ethtool_getdrvinfo(struct net_device *dev,
					struct ethtool_drvinfo *info)
{
	strscpy(info->driver, SMSC_CHIPNAME, sizeof(info->driver));
	strscpy(info->version, SMSC_DRV_VERSION, sizeof(info->version));
	strscpy(info->bus_info, dev_name(dev->dev.parent),
		sizeof(info->bus_info));
}

static u32 smsc911x_ethtool_getmsglevel(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	return pdata->msg_enable;
}

static void smsc911x_ethtool_setmsglevel(struct net_device *dev, u32 level)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	pdata->msg_enable = level;
}

static int smsc911x_ethtool_getregslen(struct net_device *dev)
{
	return (((E2P_DATA - ID_REV) / 4 + 1) + (WUCSR - MAC_CR) + 1 + 32) *
	    sizeof(u32);
}

static void
smsc911x_ethtool_getregs(struct net_device *dev, struct ethtool_regs *regs,
			 void *buf)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	struct phy_device *phy_dev = dev->phydev;
	unsigned long flags;
	unsigned int i;
	unsigned int j = 0;
	u32 *data = buf;

	regs->version = pdata->idrev;
	for (i = ID_REV; i <= E2P_DATA; i += (sizeof(u32)))
		data[j++] = smsc911x_reg_read(pdata, i);

	for (i = MAC_CR; i <= WUCSR; i++) {
		spin_lock_irqsave(&pdata->mac_lock, flags);
		data[j++] = smsc911x_mac_read(pdata, i);
		spin_unlock_irqrestore(&pdata->mac_lock, flags);
	}

	for (i = 0; i <= 31; i++)
		data[j++] = smsc911x_mii_read(phy_dev->mdio.bus,
					      phy_dev->mdio.addr, i);
}

static void smsc911x_eeprom_enable_access(struct smsc911x_data *pdata)
{
	unsigned int temp = smsc911x_reg_read(pdata, GPIO_CFG);
	temp &= ~GPIO_CFG_EEPR_EN_;
	smsc911x_reg_write(pdata, GPIO_CFG, temp);
	msleep(1);
}

static int smsc911x_eeprom_send_cmd(struct smsc911x_data *pdata, u32 op)
{
	int timeout = 100;
	u32 e2cmd;

	SMSC_TRACE(pdata, drv, "op 0x%08x", op);
	if (smsc911x_reg_read(pdata, E2P_CMD) & E2P_CMD_EPC_BUSY_) {
		SMSC_WARN(pdata, drv, "Busy at start");
		return -EBUSY;
	}

	e2cmd = op | E2P_CMD_EPC_BUSY_;
	smsc911x_reg_write(pdata, E2P_CMD, e2cmd);

	do {
		msleep(1);
		e2cmd = smsc911x_reg_read(pdata, E2P_CMD);
	} while ((e2cmd & E2P_CMD_EPC_BUSY_) && (--timeout));

	if (!timeout) {
		SMSC_TRACE(pdata, drv, "TIMED OUT");
		return -EAGAIN;
	}

	if (e2cmd & E2P_CMD_EPC_TIMEOUT_) {
		SMSC_TRACE(pdata, drv, "Error occurred during eeprom operation");
		return -EINVAL;
	}

	return 0;
}

static int smsc911x_eeprom_read_location(struct smsc911x_data *pdata,
					 u8 address, u8 *data)
{
	u32 op = E2P_CMD_EPC_CMD_READ_ | address;
	int ret;

	SMSC_TRACE(pdata, drv, "address 0x%x", address);
	ret = smsc911x_eeprom_send_cmd(pdata, op);

	if (!ret)
		data[address] = smsc911x_reg_read(pdata, E2P_DATA);

	return ret;
}

static int smsc911x_eeprom_write_location(struct smsc911x_data *pdata,
					  u8 address, u8 data)
{
	u32 op = E2P_CMD_EPC_CMD_ERASE_ | address;
	int ret;

	SMSC_TRACE(pdata, drv, "address 0x%x, data 0x%x", address, data);
	ret = smsc911x_eeprom_send_cmd(pdata, op);

	if (!ret) {
		op = E2P_CMD_EPC_CMD_WRITE_ | address;
		smsc911x_reg_write(pdata, E2P_DATA, (u32)data);

		/* Workaround for hardware read-after-write restriction */
		smsc911x_reg_read(pdata, BYTE_TEST);

		ret = smsc911x_eeprom_send_cmd(pdata, op);
	}

	return ret;
}

static int smsc911x_ethtool_get_eeprom_len(struct net_device *dev)
{
	return SMSC911X_EEPROM_SIZE;
}

static int smsc911x_ethtool_get_eeprom(struct net_device *dev,
				       struct ethtool_eeprom *eeprom, u8 *data)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	u8 eeprom_data[SMSC911X_EEPROM_SIZE];
	int len;
	int i;

	smsc911x_eeprom_enable_access(pdata);

	len = min(eeprom->len, SMSC911X_EEPROM_SIZE);
	for (i = 0; i < len; i++) {
		int ret = smsc911x_eeprom_read_location(pdata, i, eeprom_data);
		if (ret < 0) {
			eeprom->len = 0;
			return ret;
		}
	}

	memcpy(data, &eeprom_data[eeprom->offset], len);
	eeprom->len = len;
	return 0;
}

static int smsc911x_ethtool_set_eeprom(struct net_device *dev,
				       struct ethtool_eeprom *eeprom, u8 *data)
{
	int ret;
	struct smsc911x_data *pdata = netdev_priv(dev);

	smsc911x_eeprom_enable_access(pdata);
	smsc911x_eeprom_send_cmd(pdata, E2P_CMD_EPC_CMD_EWEN_);
	ret = smsc911x_eeprom_write_location(pdata, eeprom->offset, *data);
	smsc911x_eeprom_send_cmd(pdata, E2P_CMD_EPC_CMD_EWDS_);

	/* Single byte write, according to man page */
	eeprom->len = 1;

	return ret;
}

static const struct ethtool_ops smsc911x_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_drvinfo = smsc911x_ethtool_getdrvinfo,
	.nway_reset = phy_ethtool_nway_reset,
	.get_msglevel = smsc911x_ethtool_getmsglevel,
	.set_msglevel = smsc911x_ethtool_setmsglevel,
	.get_regs_len = smsc911x_ethtool_getregslen,
	.get_regs = smsc911x_ethtool_getregs,
	.get_eeprom_len = smsc911x_ethtool_get_eeprom_len,
	.get_eeprom = smsc911x_ethtool_get_eeprom,
	.set_eeprom = smsc911x_ethtool_set_eeprom,
	.get_ts_info = ethtool_op_get_ts_info,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops smsc911x_netdev_ops = {
	.ndo_open		= smsc911x_open,
	.ndo_stop		= smsc911x_stop,
	.ndo_start_xmit		= smsc911x_hard_start_xmit,
	.ndo_get_stats		= smsc911x_get_stats,
	.ndo_set_rx_mode	= smsc911x_set_multicast_list,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= smsc911x_set_mac_address,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= smsc911x_poll_controller,
#endif
};

/* copies the current mac address from hardware to dev->dev_addr */
static void smsc911x_read_mac_address(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	u32 mac_high16 = smsc911x_mac_read(pdata, ADDRH);
	u32 mac_low32 = smsc911x_mac_read(pdata, ADDRL);
	u8 addr[ETH_ALEN];

	addr[0] = (u8)(mac_low32);
	addr[1] = (u8)(mac_low32 >> 8);
	addr[2] = (u8)(mac_low32 >> 16);
	addr[3] = (u8)(mac_low32 >> 24);
	addr[4] = (u8)(mac_high16);
	addr[5] = (u8)(mac_high16 >> 8);
	eth_hw_addr_set(dev, addr);
}

/* Initializing private device structures, only called from probe */
static int smsc911x_init(struct net_device *dev)
{
	struct smsc911x_data *pdata = netdev_priv(dev);
	unsigned int byte_test, mask;
	unsigned int to = 100;

	SMSC_TRACE(pdata, probe, "Driver Parameters:");
	SMSC_TRACE(pdata, probe, "LAN base: 0x%08lX",
		   (unsigned long)pdata->ioaddr);
	SMSC_TRACE(pdata, probe, "IRQ: %d", dev->irq);
	SMSC_TRACE(pdata, probe, "PHY will be autodetected.");

	spin_lock_init(&pdata->dev_lock);
	spin_lock_init(&pdata->mac_lock);

	if (pdata->ioaddr == NULL) {
		SMSC_WARN(pdata, probe, "pdata->ioaddr: 0x00000000");
		return -ENODEV;
	}

	/*
	 * poll the READY bit in PMT_CTRL. Any other access to the device is
	 * forbidden while this bit isn't set. Try for 100ms
	 *
	 * Note that this test is done before the WORD_SWAP register is
	 * programmed. So in some configurations the READY bit is at 16 before
	 * WORD_SWAP is written to. This issue is worked around by waiting
	 * until either bit 0 or bit 16 gets set in PMT_CTRL.
	 *
	 * SMSC has confirmed that checking bit 16 (marked as reserved in
	 * the datasheet) is fine since these bits "will either never be set
	 * or can only go high after READY does (so also indicate the device
	 * is ready)".
	 */

	mask = PMT_CTRL_READY_ | swahw32(PMT_CTRL_READY_);
	while (!(smsc911x_reg_read(pdata, PMT_CTRL) & mask) && --to)
		udelay(1000);

	if (to == 0) {
		netdev_err(dev, "Device not READY in 100ms aborting\n");
		return -ENODEV;
	}

	/* Check byte ordering */
	byte_test = smsc911x_reg_read(pdata, BYTE_TEST);
	SMSC_TRACE(pdata, probe, "BYTE_TEST: 0x%08X", byte_test);
	if (byte_test == 0x43218765) {
		SMSC_TRACE(pdata, probe, "BYTE_TEST looks swapped, "
			   "applying WORD_SWAP");
		smsc911x_reg_write(pdata, WORD_SWAP, 0xffffffff);

		/* 1 dummy read of BYTE_TEST is needed after a write to
		 * WORD_SWAP before its contents are valid */
		byte_test = smsc911x_reg_read(pdata, BYTE_TEST);

		byte_test = smsc911x_reg_read(pdata, BYTE_TEST);
	}

	if (byte_test != 0x87654321) {
		SMSC_WARN(pdata, drv, "BYTE_TEST: 0x%08X", byte_test);
		if (((byte_test >> 16) & 0xFFFF) == (byte_test & 0xFFFF)) {
			SMSC_WARN(pdata, probe,
				  "top 16 bits equal to bottom 16 bits");
			SMSC_TRACE(pdata, probe,
				   "This may mean the chip is set "
				   "for 32 bit while the bus is reading 16 bit");
		}
		return -ENODEV;
	}

	/* Default generation to zero (all workarounds apply) */
	pdata->generation = 0;

	pdata->idrev = smsc911x_reg_read(pdata, ID_REV);
	switch (pdata->idrev & 0xFFFF0000) {
	case LAN9118:
	case LAN9117:
	case LAN9116:
	case LAN9115:
	case LAN89218:
		/* LAN911[5678] family */
		pdata->generation = pdata->idrev & 0x0000FFFF;
		break;

	case LAN9218:
	case LAN9217:
	case LAN9216:
	case LAN9215:
		/* LAN921[5678] family */
		pdata->generation = 3;
		break;

	case LAN9210:
	case LAN9211:
	case LAN9220:
	case LAN9221:
	case LAN9250:
		/* LAN9210/LAN9211/LAN9220/LAN9221/LAN9250 */
		pdata->generation = 4;
		break;

	default:
		SMSC_WARN(pdata, probe, "LAN911x not identified, idrev: 0x%08X",
			  pdata->idrev);
		return -ENODEV;
	}

	SMSC_TRACE(pdata, probe,
		   "LAN911x identified, idrev: 0x%08X, generation: %d",
		   pdata->idrev, pdata->generation);

	if (pdata->generation == 0)
		SMSC_WARN(pdata, probe,
			  "This driver is not intended for this chip revision");

	/* workaround for platforms without an eeprom, where the mac address
	 * is stored elsewhere and set by the bootloader.  This saves the
	 * mac address before resetting the device */
	if (pdata->config.flags & SMSC911X_SAVE_MAC_ADDRESS) {
		spin_lock_irq(&pdata->mac_lock);
		smsc911x_read_mac_address(dev);
		spin_unlock_irq(&pdata->mac_lock);
	}

	/* Reset the LAN911x */
	if (smsc911x_phy_reset(pdata) || smsc911x_soft_reset(pdata))
		return -ENODEV;

	dev->flags |= IFF_MULTICAST;
	netif_napi_add_weight(dev, &pdata->napi, smsc911x_poll,
			      SMSC_NAPI_WEIGHT);
	dev->netdev_ops = &smsc911x_netdev_ops;
	dev->ethtool_ops = &smsc911x_ethtool_ops;

	return 0;
}

static void smsc911x_drv_remove(struct platform_device *pdev)
{
	struct net_device *dev;
	struct smsc911x_data *pdata;
	struct resource *res;

	dev = platform_get_drvdata(pdev);
	BUG_ON(!dev);
	pdata = netdev_priv(dev);
	BUG_ON(!pdata);
	BUG_ON(!pdata->ioaddr);

	SMSC_TRACE(pdata, ifdown, "Stopping driver");

	unregister_netdev(dev);

	mdiobus_unregister(pdata->mii_bus);
	mdiobus_free(pdata->mii_bus);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "smsc911x-memory");
	if (!res)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	release_mem_region(res->start, resource_size(res));

	iounmap(pdata->ioaddr);

	(void)smsc911x_disable_resources(pdev);
	smsc911x_free_resources(pdev);

	free_netdev(dev);

	pm_runtime_disable(&pdev->dev);
}

/* standard register acces */
static const struct smsc911x_ops standard_smsc911x_ops = {
	.reg_read = __smsc911x_reg_read,
	.reg_write = __smsc911x_reg_write,
	.rx_readfifo = smsc911x_rx_readfifo,
	.tx_writefifo = smsc911x_tx_writefifo,
};

/* shifted register access */
static const struct smsc911x_ops shifted_smsc911x_ops = {
	.reg_read = __smsc911x_reg_read_shift,
	.reg_write = __smsc911x_reg_write_shift,
	.rx_readfifo = smsc911x_rx_readfifo_shift,
	.tx_writefifo = smsc911x_tx_writefifo_shift,
};

static int smsc911x_probe_config(struct smsc911x_platform_config *config,
				 struct device *dev)
{
	int phy_interface;
	u32 width = 0;
	int err;

	phy_interface = device_get_phy_mode(dev);
	if (phy_interface < 0)
		phy_interface = PHY_INTERFACE_MODE_NA;
	config->phy_interface = phy_interface;

	device_get_mac_address(dev, config->mac);

	err = device_property_read_u32(dev, "reg-io-width", &width);
	if (err == -ENXIO)
		return err;
	if (!err && width == 4)
		config->flags |= SMSC911X_USE_32BIT;
	else
		config->flags |= SMSC911X_USE_16BIT;

	device_property_read_u32(dev, "reg-shift", &config->shift);

	if (device_property_present(dev, "smsc,irq-active-high"))
		config->irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_HIGH;

	if (device_property_present(dev, "smsc,irq-push-pull"))
		config->irq_type = SMSC911X_IRQ_TYPE_PUSH_PULL;

	if (device_property_present(dev, "smsc,force-internal-phy"))
		config->flags |= SMSC911X_FORCE_INTERNAL_PHY;

	if (device_property_present(dev, "smsc,force-external-phy"))
		config->flags |= SMSC911X_FORCE_EXTERNAL_PHY;

	if (device_property_present(dev, "smsc,save-mac-address"))
		config->flags |= SMSC911X_SAVE_MAC_ADDRESS;

	return 0;
}

static int smsc911x_drv_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct smsc911x_data *pdata;
	struct smsc911x_platform_config *config = dev_get_platdata(&pdev->dev);
	struct resource *res;
	int res_size, irq;
	int retval;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "smsc911x-memory");
	if (!res)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_warn("Could not allocate resource\n");
		retval = -ENODEV;
		goto out_0;
	}
	res_size = resource_size(res);

	irq = platform_get_irq(pdev, 0);
	if (irq == -EPROBE_DEFER) {
		retval = -EPROBE_DEFER;
		goto out_0;
	} else if (irq < 0) {
		pr_warn("Could not allocate irq resource\n");
		retval = -ENODEV;
		goto out_0;
	}

	if (!request_mem_region(res->start, res_size, SMSC_CHIPNAME)) {
		retval = -EBUSY;
		goto out_0;
	}

	dev = alloc_etherdev(sizeof(struct smsc911x_data));
	if (!dev) {
		retval = -ENOMEM;
		goto out_release_io_1;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);

	pdata = netdev_priv(dev);
	dev->irq = irq;
	pdata->ioaddr = ioremap(res->start, res_size);
	if (!pdata->ioaddr) {
		retval = -ENOMEM;
		goto out_ioremap_fail;
	}

	pdata->dev = dev;
	pdata->msg_enable = ((1 << debug) - 1);

	platform_set_drvdata(pdev, dev);

	retval = smsc911x_request_resources(pdev);
	if (retval)
		goto out_request_resources_fail;

	retval = smsc911x_enable_resources(pdev);
	if (retval)
		goto out_enable_resources_fail;

	if (pdata->ioaddr == NULL) {
		SMSC_WARN(pdata, probe, "Error smsc911x base address invalid");
		retval = -ENOMEM;
		goto out_disable_resources;
	}

	retval = smsc911x_probe_config(&pdata->config, &pdev->dev);
	if (retval && config) {
		/* copy config parameters across to pdata */
		memcpy(&pdata->config, config, sizeof(pdata->config));
		retval = 0;
	}

	if (retval) {
		SMSC_WARN(pdata, probe, "Error smsc911x config not found");
		goto out_disable_resources;
	}

	/* assume standard, non-shifted, access to HW registers */
	pdata->ops = &standard_smsc911x_ops;
	/* apply the right access if shifting is needed */
	if (pdata->config.shift)
		pdata->ops = &shifted_smsc911x_ops;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	retval = smsc911x_init(dev);
	if (retval < 0)
		goto out_init_fail;

	netif_carrier_off(dev);

	retval = smsc911x_mii_init(pdev, dev);
	if (retval) {
		SMSC_WARN(pdata, probe, "Error %i initialising mii", retval);
		goto out_init_fail;
	}

	retval = register_netdev(dev);
	if (retval) {
		SMSC_WARN(pdata, probe, "Error %i registering device", retval);
		goto out_init_fail;
	} else {
		SMSC_TRACE(pdata, probe,
			   "Network interface: \"%s\"", dev->name);
	}

	spin_lock_irq(&pdata->mac_lock);

	/* Check if mac address has been specified when bringing interface up */
	if (is_valid_ether_addr(dev->dev_addr)) {
		smsc911x_set_hw_mac_address(pdata, dev->dev_addr);
		SMSC_TRACE(pdata, probe,
			   "MAC Address is specified by configuration");
	} else if (is_valid_ether_addr(pdata->config.mac)) {
		eth_hw_addr_set(dev, pdata->config.mac);
		SMSC_TRACE(pdata, probe,
			   "MAC Address specified by platform data");
	} else {
		/* Try reading mac address from device. if EEPROM is present
		 * it will already have been set */
		smsc_get_mac(dev);

		if (is_valid_ether_addr(dev->dev_addr)) {
			/* eeprom values are valid  so use them */
			SMSC_TRACE(pdata, probe,
				   "Mac Address is read from LAN911x EEPROM");
		} else {
			/* eeprom values are invalid, generate random MAC */
			eth_hw_addr_random(dev);
			smsc911x_set_hw_mac_address(pdata, dev->dev_addr);
			SMSC_TRACE(pdata, probe,
				   "MAC Address is set to eth_random_addr");
		}
	}

	spin_unlock_irq(&pdata->mac_lock);
	pm_runtime_put(&pdev->dev);

	netdev_info(dev, "MAC Address: %pM\n", dev->dev_addr);

	return 0;

out_init_fail:
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
out_disable_resources:
	(void)smsc911x_disable_resources(pdev);
out_enable_resources_fail:
	smsc911x_free_resources(pdev);
out_request_resources_fail:
	iounmap(pdata->ioaddr);
out_ioremap_fail:
	free_netdev(dev);
out_release_io_1:
	release_mem_region(res->start, resource_size(res));
out_0:
	return retval;
}

#ifdef CONFIG_PM
/* This implementation assumes the devices remains powered on its VDDVARIO
 * pins during suspend. */

/* TODO: implement freeze/thaw callbacks for hibernation.*/

static int smsc911x_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct smsc911x_data *pdata = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
		if (!device_may_wakeup(dev))
			phy_stop(ndev->phydev);
	}

	/* enable wake on LAN, energy detection and the external PME
	 * signal. */
	smsc911x_reg_write(pdata, PMT_CTRL,
		PMT_CTRL_PM_MODE_D1_ | PMT_CTRL_WOL_EN_ |
		PMT_CTRL_ED_EN_ | PMT_CTRL_PME_EN_);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);

	return 0;
}

static int smsc911x_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct smsc911x_data *pdata = netdev_priv(ndev);
	unsigned int to = 100;

	pm_runtime_enable(dev);
	pm_runtime_resume(dev);

	/* Note 3.11 from the datasheet:
	 * 	"When the LAN9220 is in a power saving state, a write of any
	 * 	 data to the BYTE_TEST register will wake-up the device."
	 */
	smsc911x_reg_write(pdata, BYTE_TEST, 0);

	/* poll the READY bit in PMT_CTRL. Any other access to the device is
	 * forbidden while this bit isn't set. Try for 100ms and return -EIO
	 * if it failed. */
	while (!(smsc911x_reg_read(pdata, PMT_CTRL) & PMT_CTRL_READY_) && --to)
		udelay(1000);

	if (to == 0)
		return -EIO;

	if (netif_running(ndev)) {
		netif_device_attach(ndev);
		netif_start_queue(ndev);
		if (!device_may_wakeup(dev))
			phy_start(ndev->phydev);
	}

	return 0;
}

static const struct dev_pm_ops smsc911x_pm_ops = {
	.suspend	= smsc911x_suspend,
	.resume		= smsc911x_resume,
};

#define SMSC911X_PM_OPS (&smsc911x_pm_ops)

#else
#define SMSC911X_PM_OPS NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id smsc911x_dt_ids[] = {
	{ .compatible = "smsc,lan9115", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, smsc911x_dt_ids);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id smsc911x_acpi_match[] = {
	{ "ARMH9118", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, smsc911x_acpi_match);
#endif

static struct platform_driver smsc911x_driver = {
	.probe = smsc911x_drv_probe,
	.remove_new = smsc911x_drv_remove,
	.driver = {
		.name	= SMSC_CHIPNAME,
		.pm	= SMSC911X_PM_OPS,
		.of_match_table = of_match_ptr(smsc911x_dt_ids),
		.acpi_match_table = ACPI_PTR(smsc911x_acpi_match),
	},
};

/* Entry point for loading the module */
static int __init smsc911x_init_module(void)
{
	SMSC_INITIALIZE();
	return platform_driver_register(&smsc911x_driver);
}

/* entry point for unloading the module */
static void __exit smsc911x_cleanup_module(void)
{
	platform_driver_unregister(&smsc911x_driver);
}

module_init(smsc911x_init_module);
module_exit(smsc911x_cleanup_module);
