/*
 * Platform CAN bus driver for Bosch C_CAN controller
 *
 * Copyright (C) 2010 ST Microelectronics
 * Bhupesh Sharma <bhupesh.sharma@st.com>
 *
 * Borrowed heavily from the C_CAN driver originally written by:
 * Copyright (C) 2007
 * - Sascha Hauer, Marc Kleine-Budde, Pengutronix <s.hauer@pengutronix.de>
 * - Simon Kallweit, intefo AG <simon.kallweit@intefo.ch>
 *
 * Bosch C_CAN controller is compliant to CAN protocol version 2.0 part A and B.
 * Bosch C_CAN user manual can be obtained from:
 * http://www.semiconductors.bosch.de/media/en/pdf/ipmodules_1/c_can/
 * users_manual_c_can.pdf
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>

#include <linux/can/dev.h>

#include "c_can.h"

#define CAN_RAMINIT_START_MASK(i)	(1 << (i))

/*
 * 16-bit c_can registers can be arranged differently in the memory
 * architecture of different implementations. For example: 16-bit
 * registers can be aligned to a 16-bit boundary or 32-bit boundary etc.
 * Handle the same by providing a common read/write interface.
 */
static u16 c_can_plat_read_reg_aligned_to_16bit(struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + priv->regs[index]);
}

static void c_can_plat_write_reg_aligned_to_16bit(struct c_can_priv *priv,
						enum reg index, u16 val)
{
	writew(val, priv->base + priv->regs[index]);
}

static u16 c_can_plat_read_reg_aligned_to_32bit(struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + 2 * priv->regs[index]);
}

static void c_can_plat_write_reg_aligned_to_32bit(struct c_can_priv *priv,
						enum reg index, u16 val)
{
	writew(val, priv->base + 2 * priv->regs[index]);
}

static void c_can_hw_raminit(const struct c_can_priv *priv, bool enable)
{
	u32 val;

	val = readl(priv->raminit_ctrlreg);
	if (enable)
		val |= CAN_RAMINIT_START_MASK(priv->instance);
	else
		val &= ~CAN_RAMINIT_START_MASK(priv->instance);
	writel(val, priv->raminit_ctrlreg);
}

static struct platform_device_id c_can_id_table[] = {
	[BOSCH_C_CAN_PLATFORM] = {
		.name = KBUILD_MODNAME,
		.driver_data = BOSCH_C_CAN,
	},
	[BOSCH_C_CAN] = {
		.name = "c_can",
		.driver_data = BOSCH_C_CAN,
	},
	[BOSCH_D_CAN] = {
		.name = "d_can",
		.driver_data = BOSCH_D_CAN,
	}, {
	}
};
MODULE_DEVICE_TABLE(platform, c_can_id_table);

static const struct of_device_id c_can_of_table[] = {
	{ .compatible = "bosch,c_can", .data = &c_can_id_table[BOSCH_C_CAN] },
	{ .compatible = "bosch,d_can", .data = &c_can_id_table[BOSCH_D_CAN] },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, c_can_of_table);

static int c_can_plat_probe(struct platform_device *pdev)
{
	int ret;
	void __iomem *addr;
	struct net_device *dev;
	struct c_can_priv *priv;
	const struct of_device_id *match;
	const struct platform_device_id *id;
	struct pinctrl *pinctrl;
	struct resource *mem, *res;
	int irq;
	struct clk *clk;

	if (pdev->dev.of_node) {
		match = of_match_device(c_can_of_table, &pdev->dev);
		if (!match) {
			dev_err(&pdev->dev, "Failed to find matching dt id\n");
			ret = -EINVAL;
			goto exit;
		}
		id = match->data;
	} else {
		id = platform_get_device_id(pdev);
	}

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl))
		dev_warn(&pdev->dev,
			"failed to configure pins from driver\n");

	/* get the appropriate clk */
	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		ret = -ENODEV;
		goto exit;
	}

	/* get the platform data */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!mem || irq <= 0) {
		ret = -ENODEV;
		goto exit_free_clk;
	}

	if (!request_mem_region(mem->start, resource_size(mem),
				KBUILD_MODNAME)) {
		dev_err(&pdev->dev, "resource unavailable\n");
		ret = -ENODEV;
		goto exit_free_clk;
	}

	addr = ioremap(mem->start, resource_size(mem));
	if (!addr) {
		dev_err(&pdev->dev, "failed to map can port\n");
		ret = -ENOMEM;
		goto exit_release_mem;
	}

	/* allocate the c_can device */
	dev = alloc_c_can_dev();
	if (!dev) {
		ret = -ENOMEM;
		goto exit_iounmap;
	}

	priv = netdev_priv(dev);
	switch (id->driver_data) {
	case BOSCH_C_CAN:
		priv->regs = reg_map_c_can;
		switch (mem->flags & IORESOURCE_MEM_TYPE_MASK) {
		case IORESOURCE_MEM_32BIT:
			priv->read_reg = c_can_plat_read_reg_aligned_to_32bit;
			priv->write_reg = c_can_plat_write_reg_aligned_to_32bit;
			break;
		case IORESOURCE_MEM_16BIT:
		default:
			priv->read_reg = c_can_plat_read_reg_aligned_to_16bit;
			priv->write_reg = c_can_plat_write_reg_aligned_to_16bit;
			break;
		}
		break;
	case BOSCH_D_CAN:
		priv->regs = reg_map_d_can;
		priv->can.ctrlmode_supported |= CAN_CTRLMODE_3_SAMPLES;
		priv->read_reg = c_can_plat_read_reg_aligned_to_16bit;
		priv->write_reg = c_can_plat_write_reg_aligned_to_16bit;

		if (pdev->dev.of_node)
			priv->instance = of_alias_get_id(pdev->dev.of_node, "d_can");
		else
			priv->instance = pdev->id;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		priv->raminit_ctrlreg =	devm_request_and_ioremap(&pdev->dev, res);
		if (!priv->raminit_ctrlreg || priv->instance < 0)
			dev_info(&pdev->dev, "control memory is not used for raminit\n");
		else
			priv->raminit = c_can_hw_raminit;
		break;
	default:
		ret = -EINVAL;
		goto exit_free_device;
	}

	dev->irq = irq;
	priv->base = addr;
	priv->device = &pdev->dev;
	priv->can.clock.freq = clk_get_rate(clk);
	priv->priv = clk;
	priv->type = id->driver_data;

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	ret = register_c_can_dev(dev);
	if (ret) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			KBUILD_MODNAME, ret);
		goto exit_free_device;
	}

	dev_info(&pdev->dev, "%s device registered (regs=%p, irq=%d)\n",
		 KBUILD_MODNAME, priv->base, dev->irq);
	return 0;

exit_free_device:
	platform_set_drvdata(pdev, NULL);
	free_c_can_dev(dev);
exit_iounmap:
	iounmap(addr);
exit_release_mem:
	release_mem_region(mem->start, resource_size(mem));
exit_free_clk:
	clk_put(clk);
exit:
	dev_err(&pdev->dev, "probe failed\n");

	return ret;
}

static int c_can_plat_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct c_can_priv *priv = netdev_priv(dev);
	struct resource *mem;

	unregister_c_can_dev(dev);
	platform_set_drvdata(pdev, NULL);

	free_c_can_dev(dev);
	iounmap(priv->base);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));

	clk_put(priv->priv);

	return 0;
}

#ifdef CONFIG_PM
static int c_can_suspend(struct platform_device *pdev, pm_message_t state)
{
	int ret;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct c_can_priv *priv = netdev_priv(ndev);

	if (priv->type != BOSCH_D_CAN) {
		dev_warn(&pdev->dev, "Not supported\n");
		return 0;
	}

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
	}

	ret = c_can_power_down(ndev);
	if (ret) {
		netdev_err(ndev, "failed to enter power down mode\n");
		return ret;
	}

	priv->can.state = CAN_STATE_SLEEPING;

	return 0;
}

static int c_can_resume(struct platform_device *pdev)
{
	int ret;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct c_can_priv *priv = netdev_priv(ndev);

	if (priv->type != BOSCH_D_CAN) {
		dev_warn(&pdev->dev, "Not supported\n");
		return 0;
	}

	ret = c_can_power_up(ndev);
	if (ret) {
		netdev_err(ndev, "Still in power down mode\n");
		return ret;
	}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	if (netif_running(ndev)) {
		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}
#else
#define c_can_suspend NULL
#define c_can_resume NULL
#endif

static struct platform_driver c_can_plat_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(c_can_of_table),
	},
	.probe = c_can_plat_probe,
	.remove = c_can_plat_remove,
	.suspend = c_can_suspend,
	.resume = c_can_resume,
	.id_table = c_can_id_table,
};

module_platform_driver(c_can_plat_driver);

MODULE_AUTHOR("Bhupesh Sharma <bhupesh.sharma@st.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Platform CAN bus driver for Bosch C_CAN controller");
