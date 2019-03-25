// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 * Rockchip CAN driver
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/led.h>
#include <linux/clk.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

#define DRV_NAME		"rockchip_can"

#define CAN_MODE		0x00
#define RESET_MODE		0
#define WORK_MODE		BIT(0)
#define SELF_TEST_EN		BIT(2)
#define MODE_AUTO_RETX		BIT(10)

#define CAN_CMD			0x04
#define TX_REQ			BIT(0)

#define CAN_STATE		0x08
#define RX_BUF_FULL		BIT(0)
#define TX_BUF_FULL		BIT(1)
#define RX_PERIOD		BIT(2)
#define TX_PERIOD		BIT(3)
#define ERR_WARN		BIT(4)
#define BUS_OFF			BIT(5)

#define CAN_INT			0x0C

#define CAN_INT_MASK		0x10
#define RX_FINISH		BIT(0)
#define TX_FINISH		BIT(1)
#define ERR_WARN_INT		BIT(2)
#define RX_BUF_OV		BIT(3)
#define PASSIVE_ERR		BIT(4)
#define TX_LOSTARB		BIT(5)
#define BUS_ERR_INT		BIT(6)

/* Bit Timing Register */
#define CAN_BTT			0x18
#define MODE_3_SAMPLES		BIT(16)
#define BT_SJW_SHIFT		14
#define BT_SJW_MASK		GENMASK(15, 14)
#define BT_BRP_SHIFT		8
#define BT_BRP_MASK		GENMASK(13, 8)
#define BT_TSEG2_SHIFT		4
#define BT_TSEG2_MASK		GENMASK(6, 4)
#define BT_TSEG1_SHIFT		0
#define BT_TSEG1_MASK		GENMASK(3, 0)

#define CAN_LOSTARB_CODE	0x28

#define CAN_ERR_CODE		0x2c
#define ERR_TYPE_MASK		GENMASK(24, 22)
#define ERR_TYPE_SHIFT		22
#define BIT_ERR			0
#define STUFF_ERR		1
#define FORM_ERR		2
#define ACK_ERR			3
#define CRC_ERR			4
#define ERR_DIR_RX		BIT(21)
#define ERR_LOC_MASK		GENMASK(13, 0)

#define CAN_RX_ERR_CNT		0x34

#define CAN_TX_ERR_CNT		0x38

#define CAN_ID			0x3c

#define CAN_ID_MASK		0x40

#define CAN_TX_FRM_INFO		0x50
#define CAN_EFF			BIT(7)
#define CAN_RTR			BIT(6)
#define CAN_DLC_MASK		GENMASK(3, 0)
#define CAN_DLC(x)		((x) & GENMASK(3, 0))

#define CAN_TX_ID		0x54
#define CAN_TX_ID_MASK		0x1fffffff

#define CAN_TX_DATA1		0x58

#define CAN_TX_DATA2		0x5c

#define CAN_RX_FRM_INFO		0x60

#define CAN_RX_ID		0x64

#define CAN_RX_DATA1		0x68

#define CAN_RX_DATA2		0x6c

#define CAN_RX_FILTER_MASK	0x1fffffff

#define CAN_VERSION		0x70

struct rockchip_can {
	struct can_priv can;
	void __iomem *base;
	struct device *dev;
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *reset;
};

static const struct can_bittiming_const rockchip_can_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 128,
	.brp_inc = 2,
};

static void rockchip_can_write_cmdreg(struct rockchip_can *rcan, u8 val)
{
	writel(val, rcan->base + CAN_CMD);
}

static int set_reset_mode(struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);

	reset_control_assert(rcan->reset);
	udelay(2);
	reset_control_deassert(rcan->reset);

	writel(0, rcan->base + CAN_MODE);

	return 0;
}

static int set_normal_mode(struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);
	u32 val;

	val = readl(rcan->base + CAN_MODE);
	val |= WORK_MODE | MODE_AUTO_RETX;
	writel(val, rcan->base + CAN_MODE);

	return 0;
}

/* bittiming is called in reset_mode only */
static int rockchip_can_set_bittiming(struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);
	struct can_bittiming *bt = &rcan->can.bittiming;
	u32 cfg;

	cfg = ((bt->sjw - 1) << BT_SJW_SHIFT) |
	      (((bt->brp >> 1) - 1) << BT_BRP_SHIFT) |
	      ((bt->phase_seg2 - 1) << BT_TSEG2_SHIFT) |
	      ((bt->prop_seg + bt->phase_seg1 - 1));
	if (rcan->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		cfg |= MODE_3_SAMPLES;

	writel(cfg, rcan->base + CAN_BTT);

	netdev_dbg(ndev, "setting BITTIMING=0x%08x  brp: %d bitrate:%d\n",
		   cfg, bt->brp, bt->bitrate);

	return 0;
}

static int rockchip_can_get_berr_counter(const struct net_device *ndev,
					 struct can_berr_counter *bec)
{
	struct rockchip_can *rcan = netdev_priv(ndev);
	int err;

	err = pm_runtime_get_sync(rcan->dev);
	if (err < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, err);
		return err;
	}

	bec->rxerr = readl(rcan->base + CAN_RX_ERR_CNT);
	bec->txerr = readl(rcan->base + CAN_TX_ERR_CNT);

	pm_runtime_put(rcan->dev);

	netdev_dbg(ndev, "%s\n", __func__);
	return 0;
}

static int rockchip_can_start(struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);

	/* we need to enter the reset mode */
	set_reset_mode(ndev);

	writel(0, rcan->base + CAN_INT_MASK);

	/* RECEIVING FILTER, accept all */
	writel(0, rcan->base + CAN_ID);
	writel(CAN_RX_FILTER_MASK, rcan->base + CAN_ID_MASK);

	rockchip_can_set_bittiming(ndev);

	set_normal_mode(ndev);

	rcan->can.state = CAN_STATE_ERROR_ACTIVE;

	netdev_dbg(ndev, "%s\n", __func__);
	return 0;
}

static int rockchip_can_stop(struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);
	u32 val;

	rcan->can.state = CAN_STATE_STOPPED;
	/* we need to enter reset mode */
	set_reset_mode(ndev);

	/* disable all interrupts */
	val = RX_FINISH | TX_FINISH | ERR_WARN_INT |
	      RX_BUF_OV | PASSIVE_ERR | TX_LOSTARB |
	      BUS_ERR_INT;

	writel(val, rcan->base + CAN_INT_MASK);
	netdev_dbg(ndev, "%s\n", __func__);
	return 0;
}

static int rockchip_can_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int err;

	netdev_dbg(ndev, "can set mode: 0x%x\n", mode);

	switch (mode) {
	case CAN_MODE_START:
		err = rockchip_can_start(ndev);
		if (err) {
			netdev_err(ndev, "starting CAN controller failed!\n");
			return err;
		}
		if (netif_queue_stopped(ndev))
			netif_wake_queue(ndev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/* transmit a CAN message
 * message layout in the sk_buff should be like this:
 * xx xx xx xx         ff         ll 00 11 22 33 44 55 66 77
 * [ can_id ] [flags] [len] [can data (up to 8 bytes]
 */
static int rockchip_can_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	canid_t id;
	u8 dlc;
	u32 fi;
	u32 data1 = 0, data2 = 0;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(ndev);

	id = cf->can_id;
	dlc = cf->can_dlc;
	fi = dlc;

	if (id & CAN_RTR_FLAG) {
		fi |= CAN_RTR;
		fi &= ~CAN_DLC_MASK;
	}

	if (id & CAN_EFF_FLAG)
		fi |= CAN_EFF;

	rockchip_can_write_cmdreg(rcan, 0);

	writel(id & CAN_TX_ID_MASK, rcan->base + CAN_TX_ID);
	if (!(id & CAN_RTR_FLAG)) {
		data1 = le32_to_cpup((__le32 *)&cf->data[0]);
		data2 = le32_to_cpup((__le32 *)&cf->data[4]);
		writel(data1, rcan->base + CAN_TX_DATA1);
		writel(data2, rcan->base + CAN_TX_DATA2);
	}

	writel(fi, rcan->base + CAN_TX_FRM_INFO);
	can_put_echo_skb(skb, ndev, 0);

	rockchip_can_write_cmdreg(rcan, TX_REQ);
	netdev_dbg(ndev, "TX: can_id:0x%08x dlc: %d mode: 0x%08x data: 0x%08x 0x%08x\n",
		   cf->can_id, cf->can_dlc, rcan->can.ctrlmode, data1, data2);

	return NETDEV_TX_OK;
}

static void rockchip_can_rx(struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	canid_t id;
	u8 fi;
	u32 data1 = 0, data2 = 0;

	/* create zero'ed CAN frame buffer */
	skb = alloc_can_skb(ndev, &cf);
	if (!skb)
		return;

	fi = readl(rcan->base + CAN_RX_FRM_INFO);
	cf->can_dlc = get_can_dlc(fi & CAN_DLC_MASK);
	id = readl(rcan->base + CAN_RX_ID);
	if (fi & CAN_EFF)
		id |= CAN_EFF_FLAG;

	/* remote frame ? */
	if (fi & CAN_RTR) {
		id |= CAN_RTR_FLAG;
	} else {
		data1 = readl(rcan->base + CAN_RX_DATA1);
		data2 = readl(rcan->base + CAN_RX_DATA2);
	}

	cf->can_id = id;
	*(__le32 *)(cf->data + 0) = cpu_to_le32(data1);
	*(__le32 *)(cf->data + 4) = cpu_to_le32(data2);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);

	can_led_event(ndev, CAN_LED_EVENT_RX);

	netdev_dbg(ndev, "%s can_id:0x%08x fi: 0x%08x dlc: %d data: 0x%08x 0x%08x\n",
		   __func__, cf->can_id, fi, cf->can_dlc,
		   data1, data2);
}

static void rockchip_can_clean_rx_info(struct rockchip_can *rcan)
{
	readl(rcan->base + CAN_RX_FRM_INFO);
	readl(rcan->base + CAN_RX_ID);
	readl(rcan->base + CAN_RX_DATA1);
	readl(rcan->base + CAN_RX_DATA2);
}

static int rockchip_can_err(struct net_device *ndev, u8 isr)
{
	struct rockchip_can *rcan = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	enum can_state state = rcan->can.state;
	enum can_state rx_state, tx_state;
	struct can_frame *cf;
	struct sk_buff *skb;
	unsigned int rxerr, txerr;
	u32 ecc, alc;
	u32 sta_reg;

	skb = alloc_can_err_skb(ndev, &cf);

	rxerr = readl(rcan->base + CAN_RX_ERR_CNT);
	txerr = readl(rcan->base + CAN_TX_ERR_CNT);
	sta_reg = readl(rcan->base + CAN_STATE);

	if (skb) {
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}

	if (isr & RX_BUF_OV) {
		/* data overrun interrupt */
		netdev_dbg(ndev, "data overrun interrupt\n");
		if (likely(skb)) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		}
		stats->rx_over_errors++;
		stats->rx_errors++;

		/* reset the CAN IP by entering reset mode
		 * ignoring timeout error
		 */
		set_reset_mode(ndev);
		set_normal_mode(ndev);
	}

	if (isr & ERR_WARN_INT) {
		/* error warning interrupt */
		netdev_dbg(ndev, "error warning interrupt\n");

		if (sta_reg & BUS_OFF)
			state = CAN_STATE_BUS_OFF;
		else if (sta_reg & ERR_WARN)
			state = CAN_STATE_ERROR_WARNING;
		else
			state = CAN_STATE_ERROR_ACTIVE;
	}

	if (isr & BUS_ERR_INT) {
		/* bus error interrupt */
		netdev_dbg(ndev, "bus error interrupt\n");
		rcan->can.can_stats.bus_error++;
		stats->rx_errors++;

		if (likely(skb)) {
			ecc = readl(rcan->base + CAN_ERR_CODE);

			cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

			switch ((ecc & ERR_TYPE_MASK) >> ERR_TYPE_SHIFT) {
			case BIT_ERR:
				cf->data[2] |= CAN_ERR_PROT_BIT;
				break;
			case FORM_ERR:
				cf->data[2] |= CAN_ERR_PROT_FORM;
				break;
			case STUFF_ERR:
				cf->data[2] |= CAN_ERR_PROT_STUFF;
				break;
			default:
				cf->data[3] = ecc & ERR_LOC_MASK;
				break;
			}
			/* error occurred during transmission? */
			if ((ecc & ERR_DIR_RX) == 0)
				cf->data[2] |= CAN_ERR_PROT_TX;
		}
	}

	if (isr & PASSIVE_ERR) {
		/* error passive interrupt */
		netdev_dbg(ndev, "error passive interrupt\n");
		if (state == CAN_STATE_ERROR_PASSIVE)
			state = CAN_STATE_ERROR_WARNING;
		else
			state = CAN_STATE_ERROR_PASSIVE;
	}
	if (isr & TX_LOSTARB) {
		/* arbitration lost interrupt */
		netdev_dbg(ndev, "arbitration lost interrupt\n");
		alc = readl(rcan->base + CAN_LOSTARB_CODE);
		rcan->can.can_stats.arbitration_lost++;
		stats->tx_errors++;
		if (likely(skb)) {
			cf->can_id |= CAN_ERR_LOSTARB;
			cf->data[0] = alc;
		}
	}

	if (state != rcan->can.state) {
		tx_state = txerr >= rxerr ? state : 0;
		rx_state = txerr <= rxerr ? state : 0;

		if (likely(skb))
			can_change_state(ndev, cf, tx_state, rx_state);
		else
			rcan->can.state = state;
		if (state == CAN_STATE_BUS_OFF)
			can_bus_off(ndev);
	}

	if (likely(skb)) {
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	} else {
		return -ENOMEM;
	}

	return 0;
}

static irqreturn_t rockchip_can_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct rockchip_can *rcan = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	u8 err_int = ERR_WARN_INT | RX_BUF_OV | PASSIVE_ERR |
		     TX_LOSTARB | BUS_ERR_INT;
	u8 isr;

	isr = readl(rcan->base + CAN_INT);
	if (isr & TX_FINISH) {
		/* transmission complete interrupt */
		stats->tx_bytes += readl(rcan->base + CAN_TX_FRM_INFO) &
				   CAN_DLC_MASK;
		stats->tx_packets++;
		rockchip_can_write_cmdreg(rcan, 0);
		can_get_echo_skb(ndev, 0);
		netif_wake_queue(ndev);
		can_led_event(ndev, CAN_LED_EVENT_TX);
	}

	if (isr & RX_FINISH)
		rockchip_can_rx(ndev);

	if (isr & err_int) {
		rockchip_can_clean_rx_info(rcan);
		if (rockchip_can_err(ndev, isr))
			netdev_err(ndev, "can't allocate buffer - clearing pending interrupts\n");
	}

	writel(isr, rcan->base + CAN_INT);
	rockchip_can_clean_rx_info(rcan);
	netdev_dbg(ndev, "isr: 0x%x\n", isr);
	return	IRQ_HANDLED;
}

static int rockchip_can_open(struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);
	int err;

	/* common open */
	err = open_candev(ndev);
	if (err)
		return err;

	err = pm_runtime_get_sync(rcan->dev);
	if (err < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, err);
		goto exit;
	}

	err = rockchip_can_start(ndev);
	if (err) {
		netdev_err(ndev, "could not start CAN peripheral\n");
		goto exit_can_start;
	}

	can_led_event(ndev, CAN_LED_EVENT_OPEN);
	netif_start_queue(ndev);

	netdev_dbg(ndev, "%s\n", __func__);
	return 0;

exit_can_start:
	pm_runtime_put(rcan->dev);
exit:
	close_candev(ndev);
	return err;
}

static int rockchip_can_close(struct net_device *ndev)
{
	struct rockchip_can *rcan = netdev_priv(ndev);

	netif_stop_queue(ndev);
	rockchip_can_stop(ndev);
	close_candev(ndev);
	can_led_event(ndev, CAN_LED_EVENT_STOP);
	pm_runtime_put(rcan->dev);

	netdev_dbg(ndev, "%s\n", __func__);
	return 0;
}

static const struct net_device_ops rockchip_can_netdev_ops = {
	.ndo_open = rockchip_can_open,
	.ndo_stop = rockchip_can_close,
	.ndo_start_xmit = rockchip_can_start_xmit,
};

/**
 * rockchip_can_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the driver into low power mode.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused rockchip_can_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
		rockchip_can_stop(ndev);
	}

	return pm_runtime_force_suspend(dev);
}

/**
 * rockchip_can_resume - Resume from suspend
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused rockchip_can_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "pm_runtime_force_resume failed on resume\n");
		return ret;
	}

	if (netif_running(ndev)) {
		ret = rockchip_can_start(ndev);
		if (ret) {
			dev_err(dev, "rockchip_can_chip_start failed on resume\n");
			return ret;
		}

		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}

/**
 * rockchip_can_runtime_suspend - Runtime suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the driver into low power mode.
 * Return: 0 always
 */
static int __maybe_unused rockchip_can_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rockchip_can *rcan = netdev_priv(ndev);

	clk_bulk_disable_unprepare(rcan->num_clks, rcan->clks);

	return 0;
}

/**
 * rockchip_can_runtime_resume - Runtime resume from suspend
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused rockchip_can_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rockchip_can *rcan = netdev_priv(ndev);
	int ret;

	ret = clk_bulk_prepare_enable(rcan->num_clks, rcan->clks);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops rockchip_can_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_can_suspend, rockchip_can_resume)
	SET_RUNTIME_PM_OPS(rockchip_can_runtime_suspend,
			   rockchip_can_runtime_resume, NULL)
};

static const struct of_device_id rockchip_can_of_match[] = {
	{.compatible = "rockchip,can-1.0"},
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_can_of_match);

static int rockchip_can_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct rockchip_can *rcan;
	struct resource *res;
	void __iomem *addr;
	int err, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "could not get a valid irq\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr))
		return -EBUSY;

	ndev = alloc_candev(sizeof(struct rockchip_can), 1);
	if (!ndev) {
		dev_err(&pdev->dev, "could not allocate memory for CAN device\n");
		return -ENOMEM;
	}

	ndev->netdev_ops = &rockchip_can_netdev_ops;
	ndev->irq = irq;
	ndev->flags |= IFF_ECHO;

	rcan = netdev_priv(ndev);

	/* register interrupt handler */
	err = devm_request_irq(&pdev->dev, ndev->irq, rockchip_can_interrupt,
			       0, ndev->name, ndev);
	if (err) {
		dev_err(&pdev->dev, "request_irq err: %d\n", err);
		return err;
	}

	rcan->reset = devm_reset_control_array_get(&pdev->dev, false, false);
	if (IS_ERR(rcan->reset)) {
		if (PTR_ERR(rcan->reset) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get rcan reset lines\n");
		return PTR_ERR(rcan->reset);
	}

	rcan->num_clks = devm_clk_bulk_get_all(&pdev->dev, &rcan->clks);
	if (rcan->num_clks < 1) {
		dev_err(&pdev->dev, "bus clock not found\n");
		return -ENODEV;
	}

	rcan->dev = &pdev->dev;
	rcan->can.clock.freq = clk_get_rate(rcan->clks[0].clk);
	rcan->can.bittiming_const = &rockchip_can_bittiming_const;
	rcan->can.do_set_mode = rockchip_can_set_mode;
	rcan->can.do_get_berr_counter = rockchip_can_get_berr_counter;
	rcan->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING |
				       CAN_CTRLMODE_LISTENONLY |
				       CAN_CTRLMODE_LOOPBACK |
				       CAN_CTRLMODE_3_SAMPLES;
	rcan->base = addr;
	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	pm_runtime_enable(&pdev->dev);
	err = pm_runtime_get_sync(&pdev->dev);
	if (err < 0) {
		dev_err(&pdev->dev, "%s: pm_runtime_get failed(%d)\n",
			__func__, err);
		goto err_pmdisable;
	}

	err = register_candev(ndev);
	if (err) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			DRV_NAME, err);
		goto err_disableclks;
	}

	devm_can_led_init(ndev);

	pm_runtime_put(&pdev->dev);

	return 0;

err_disableclks:
	pm_runtime_put(&pdev->dev);
err_pmdisable:
	pm_runtime_disable(&pdev->dev);
	free_candev(ndev);

	return err;
}

static int rockchip_can_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_netdev(ndev);
	pm_runtime_disable(&pdev->dev);
	free_candev(ndev);

	return 0;
}

static struct platform_driver rockchip_can_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &rockchip_can_dev_pm_ops,
		.of_match_table = rockchip_can_of_match,
	},
	.probe = rockchip_can_probe,
	.remove = rockchip_can_remove,
};
module_platform_driver(rockchip_can_driver);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rockchip CAN Drivers");
