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
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <linux/can/dev.h>

#include "c_can.h"

#define DCAN_RAM_INIT_BIT BIT(3)

static DEFINE_SPINLOCK(raminit_lock);

/* 16-bit c_can registers can be arranged differently in the memory
 * architecture of different implementations. For example: 16-bit
 * registers can be aligned to a 16-bit boundary or 32-bit boundary etc.
 * Handle the same by providing a common read/write interface.
 */
static u16 c_can_plat_read_reg_aligned_to_16bit(const struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + priv->regs[index]);
}

static void c_can_plat_write_reg_aligned_to_16bit(const struct c_can_priv *priv,
						  enum reg index, u16 val)
{
	writew(val, priv->base + priv->regs[index]);
}

static u16 c_can_plat_read_reg_aligned_to_32bit(const struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + 2 * priv->regs[index]);
}

static void c_can_plat_write_reg_aligned_to_32bit(const struct c_can_priv *priv,
						  enum reg index, u16 val)
{
	writew(val, priv->base + 2 * priv->regs[index]);
}

static void c_can_hw_raminit_wait_syscon(const struct c_can_priv *priv,
					 u32 mask, u32 val)
{
	const struct c_can_raminit *raminit = &priv->raminit_sys;
	int timeout = 0;
	u32 ctrl = 0;

	/* We look only at the bits of our instance. */
	val &= mask;
	do {
		udelay(1);
		timeout++;

		regmap_read(raminit->syscon, raminit->reg, &ctrl);
		if (timeout == 1000) {
			dev_err(&priv->dev->dev, "%s: time out\n", __func__);
			break;
		}
	} while ((ctrl & mask) != val);
}

static void c_can_hw_raminit_syscon(const struct c_can_priv *priv, bool enable)
{
	const struct c_can_raminit *raminit = &priv->raminit_sys;
	u32 ctrl = 0;
	u32 mask;

	spin_lock(&raminit_lock);

	mask = 1 << raminit->bits.start | 1 << raminit->bits.done;
	regmap_read(raminit->syscon, raminit->reg, &ctrl);

	/* We clear the start bit first. The start bit is
	 * looking at the 0 -> transition, but is not self clearing;
	 * NOTE: DONE must be written with 1 to clear it.
	 * We can't clear the DONE bit here using regmap_update_bits()
	 * as it will bypass the write if initial condition is START:0 DONE:1
	 * e.g. on DRA7 which needs START pulse.
	 */
	ctrl &= ~mask;	/* START = 0, DONE = 0 */
	regmap_update_bits(raminit->syscon, raminit->reg, mask, ctrl);

	/* check if START bit is 0. Ignore DONE bit for now
	 * as it can be either 0 or 1.
	 */
	c_can_hw_raminit_wait_syscon(priv, 1 << raminit->bits.start, ctrl);

	if (enable) {
		/* Clear DONE bit & set START bit. */
		ctrl |= 1 << raminit->bits.start;
		/* DONE must be written with 1 to clear it */
		ctrl |= 1 << raminit->bits.done;
		regmap_update_bits(raminit->syscon, raminit->reg, mask, ctrl);
		/* prevent further clearing of DONE bit */
		ctrl &= ~(1 << raminit->bits.done);
		/* clear START bit if start pulse is needed */
		if (raminit->needs_pulse) {
			ctrl &= ~(1 << raminit->bits.start);
			regmap_update_bits(raminit->syscon, raminit->reg,
					   mask, ctrl);
		}

		ctrl |= 1 << raminit->bits.done;
		c_can_hw_raminit_wait_syscon(priv, mask, ctrl);
	}
	spin_unlock(&raminit_lock);
}

static u32 c_can_plat_read_reg32(const struct c_can_priv *priv, enum reg index)
{
	u32 val;

	val = priv->read_reg(priv, index);
	val |= ((u32)priv->read_reg(priv, index + 1)) << 16;

	return val;
}

static void c_can_plat_write_reg32(const struct c_can_priv *priv,
				   enum reg index, u32 val)
{
	priv->write_reg(priv, index + 1, val >> 16);
	priv->write_reg(priv, index, val);
}

static u32 d_can_plat_read_reg32(const struct c_can_priv *priv, enum reg index)
{
	return readl(priv->base + priv->regs[index]);
}

static void d_can_plat_write_reg32(const struct c_can_priv *priv,
				   enum reg index, u32 val)
{
	writel(val, priv->base + priv->regs[index]);
}

static void c_can_hw_raminit_wait(const struct c_can_priv *priv, u32 mask)
{
	while (priv->read_reg32(priv, C_CAN_FUNCTION_REG) & mask)
		udelay(1);
}

static void c_can_hw_raminit(const struct c_can_priv *priv, bool enable)
{
	u32 ctrl;

	ctrl = priv->read_reg32(priv, C_CAN_FUNCTION_REG);
	ctrl &= ~DCAN_RAM_INIT_BIT;
	priv->write_reg32(priv, C_CAN_FUNCTION_REG, ctrl);
	c_can_hw_raminit_wait(priv, ctrl);

	if (enable) {
		ctrl |= DCAN_RAM_INIT_BIT;
		priv->write_reg32(priv, C_CAN_FUNCTION_REG, ctrl);
		c_can_hw_raminit_wait(priv, ctrl);
	}
}

static const struct c_can_driver_data c_can_drvdata = {
	.id = BOSCH_C_CAN,
	.msg_obj_num = 32,
};

static const struct c_can_driver_data d_can_drvdata = {
	.id = BOSCH_D_CAN,
	.msg_obj_num = 32,
};

static const struct raminit_bits dra7_raminit_bits[] = {
	[0] = { .start = 3, .done = 1, },
	[1] = { .start = 5, .done = 2, },
};

static const struct c_can_driver_data dra7_dcan_drvdata = {
	.id = BOSCH_D_CAN,
	.msg_obj_num = 64,
	.raminit_num = ARRAY_SIZE(dra7_raminit_bits),
	.raminit_bits = dra7_raminit_bits,
	.raminit_pulse = true,
};

static const struct raminit_bits am3352_raminit_bits[] = {
	[0] = { .start = 0, .done = 8, },
	[1] = { .start = 1, .done = 9, },
};

static const struct c_can_driver_data am3352_dcan_drvdata = {
	.id = BOSCH_D_CAN,
	.msg_obj_num = 64,
	.raminit_num = ARRAY_SIZE(am3352_raminit_bits),
	.raminit_bits = am3352_raminit_bits,
};

static const struct platform_device_id c_can_id_table[] = {
	{
		.name = KBUILD_MODNAME,
		.driver_data = (kernel_ulong_t)&c_can_drvdata,
	},
	{
		.name = "c_can",
		.driver_data = (kernel_ulong_t)&c_can_drvdata,
	},
	{
		.name = "d_can",
		.driver_data = (kernel_ulong_t)&d_can_drvdata,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, c_can_id_table);

static const struct of_device_id c_can_of_table[] = {
	{ .compatible = "bosch,c_can", .data = &c_can_drvdata },
	{ .compatible = "bosch,d_can", .data = &d_can_drvdata },
	{ .compatible = "ti,dra7-d_can", .data = &dra7_dcan_drvdata },
	{ .compatible = "ti,am3352-d_can", .data = &am3352_dcan_drvdata },
	{ .compatible = "ti,am4372-d_can", .data = &am3352_dcan_drvdata },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, c_can_of_table);

static int c_can_plat_probe(struct platform_device *pdev)
{
	int ret;
	void __iomem *addr;
	struct net_device *dev;
	struct c_can_priv *priv;
	struct resource *mem;
	int irq;
	struct clk *clk;
	const struct c_can_driver_data *drvdata;
	struct device_node *np = pdev->dev.of_node;

	drvdata = device_get_match_data(&pdev->dev);

	/* get the appropriate clk */
	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto exit;
	}

	/* get the platform data */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto exit;
	}

	addr = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(addr)) {
		ret =  PTR_ERR(addr);
		goto exit;
	}

	/* allocate the c_can device */
	dev = alloc_c_can_dev(drvdata->msg_obj_num);
	if (!dev) {
		ret = -ENOMEM;
		goto exit;
	}

	priv = netdev_priv(dev);
	switch (drvdata->id) {
	case BOSCH_C_CAN:
		priv->regs = reg_map_c_can;
		switch (mem->flags & IORESOURCE_MEM_TYPE_MASK) {
		case IORESOURCE_MEM_32BIT:
			priv->read_reg = c_can_plat_read_reg_aligned_to_32bit;
			priv->write_reg = c_can_plat_write_reg_aligned_to_32bit;
			priv->read_reg32 = c_can_plat_read_reg32;
			priv->write_reg32 = c_can_plat_write_reg32;
			break;
		case IORESOURCE_MEM_16BIT:
		default:
			priv->read_reg = c_can_plat_read_reg_aligned_to_16bit;
			priv->write_reg = c_can_plat_write_reg_aligned_to_16bit;
			priv->read_reg32 = c_can_plat_read_reg32;
			priv->write_reg32 = c_can_plat_write_reg32;
			break;
		}
		break;
	case BOSCH_D_CAN:
		priv->regs = reg_map_d_can;
		priv->read_reg = c_can_plat_read_reg_aligned_to_16bit;
		priv->write_reg = c_can_plat_write_reg_aligned_to_16bit;
		priv->read_reg32 = d_can_plat_read_reg32;
		priv->write_reg32 = d_can_plat_write_reg32;

		/* Check if we need custom RAMINIT via syscon. Mostly for TI
		 * platforms. Only supported with DT boot.
		 */
		if (np && of_property_read_bool(np, "syscon-raminit")) {
			u32 id;
			struct c_can_raminit *raminit = &priv->raminit_sys;

			ret = -EINVAL;
			raminit->syscon = syscon_regmap_lookup_by_phandle(np,
									  "syscon-raminit");
			if (IS_ERR(raminit->syscon)) {
				/* can fail with -EPROBE_DEFER */
				ret = PTR_ERR(raminit->syscon);
				free_c_can_dev(dev);
				return ret;
			}

			if (of_property_read_u32_index(np, "syscon-raminit", 1,
						       &raminit->reg)) {
				dev_err(&pdev->dev,
					"couldn't get the RAMINIT reg. offset!\n");
				goto exit_free_device;
			}

			if (of_property_read_u32_index(np, "syscon-raminit", 2,
						       &id)) {
				dev_err(&pdev->dev,
					"couldn't get the CAN instance ID\n");
				goto exit_free_device;
			}

			if (id >= drvdata->raminit_num) {
				dev_err(&pdev->dev,
					"Invalid CAN instance ID\n");
				goto exit_free_device;
			}

			raminit->bits = drvdata->raminit_bits[id];
			raminit->needs_pulse = drvdata->raminit_pulse;

			priv->raminit = c_can_hw_raminit_syscon;
		} else {
			priv->raminit = c_can_hw_raminit;
		}
		break;
	default:
		ret = -EINVAL;
		goto exit_free_device;
	}

	dev->irq = irq;
	priv->base = addr;
	priv->device = &pdev->dev;
	priv->can.clock.freq = clk_get_rate(clk);
	priv->type = drvdata->id;

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	pm_runtime_enable(priv->device);
	ret = register_c_can_dev(dev);
	if (ret) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			KBUILD_MODNAME, ret);
		goto exit_pm_runtime;
	}

	dev_info(&pdev->dev, "%s device registered (regs=%p, irq=%d)\n",
		 KBUILD_MODNAME, priv->base, dev->irq);
	return 0;

exit_pm_runtime:
	pm_runtime_disable(priv->device);
exit_free_device:
	free_c_can_dev(dev);
exit:
	dev_err(&pdev->dev, "probe failed\n");

	return ret;
}

static void c_can_plat_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct c_can_priv *priv = netdev_priv(dev);

	unregister_c_can_dev(dev);
	pm_runtime_disable(priv->device);
	free_c_can_dev(dev);
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
		.of_match_table = c_can_of_table,
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
