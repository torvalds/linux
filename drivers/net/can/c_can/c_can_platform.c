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

#include <linux/can/dev.h>

#include "c_can.h"

#define CAN_RAMINIT_START_MASK(i)	(0x001 << (i))
#define CAN_RAMINIT_DONE_MASK(i)	(0x100 << (i))
#define CAN_RAMINIT_ALL_MASK(i)		(0x101 << (i))
#define DCAN_RAM_INIT_BIT		(1 << 3)
static DEFINE_SPINLOCK(raminit_lock);
/*
 * 16-bit c_can registers can be arranged differently in the memory
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

static void c_can_hw_raminit_wait_ti(const struct c_can_priv *priv, u32 mask,
				  u32 val)
{
	/* We look only at the bits of our instance. */
	val &= mask;
	while ((readl(priv->raminit_ctrlreg) & mask) != val)
		udelay(1);
}

static void c_can_hw_raminit_ti(const struct c_can_priv *priv, bool enable)
{
	u32 mask = CAN_RAMINIT_ALL_MASK(priv->instance);
	u32 ctrl;

	spin_lock(&raminit_lock);

	ctrl = readl(priv->raminit_ctrlreg);
	/* We clear the done and start bit first. The start bit is
	 * looking at the 0 -> transition, but is not self clearing;
	 * And we clear the init done bit as well.
	 */
	ctrl &= ~CAN_RAMINIT_START_MASK(priv->instance);
	ctrl |= CAN_RAMINIT_DONE_MASK(priv->instance);
	writel(ctrl, priv->raminit_ctrlreg);
	ctrl &= ~CAN_RAMINIT_DONE_MASK(priv->instance);
	c_can_hw_raminit_wait_ti(priv, mask, ctrl);

	if (enable) {
		/* Set start bit and wait for the done bit. */
		ctrl |= CAN_RAMINIT_START_MASK(priv->instance);
		writel(ctrl, priv->raminit_ctrlreg);
		ctrl |= CAN_RAMINIT_DONE_MASK(priv->instance);
		c_can_hw_raminit_wait_ti(priv, mask, ctrl);
	}
	spin_unlock(&raminit_lock);
}

static u32 c_can_plat_read_reg32(const struct c_can_priv *priv, enum reg index)
{
	u32 val;

	val = priv->read_reg(priv, index);
	val |= ((u32) priv->read_reg(priv, index + 1)) << 16;

	return val;
}

static void c_can_plat_write_reg32(const struct c_can_priv *priv, enum reg index,
		u32 val)
{
	priv->write_reg(priv, index + 1, val >> 16);
	priv->write_reg(priv, index, val);
}

static u32 d_can_plat_read_reg32(const struct c_can_priv *priv, enum reg index)
{
	return readl(priv->base + priv->regs[index]);
}

static void d_can_plat_write_reg32(const struct c_can_priv *priv, enum reg index,
		u32 val)
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

	/* get the appropriate clk */
	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto exit;
	}

	/* get the platform data */
	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		ret = -ENODEV;
		goto exit;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	addr = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(addr)) {
		ret =  PTR_ERR(addr);
		goto exit;
	}

	/* allocate the c_can device */
	dev = alloc_c_can_dev();
	if (!dev) {
		ret = -ENOMEM;
		goto exit;
	}

	priv = netdev_priv(dev);
	switch (id->driver_data) {
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
		priv->can.ctrlmode_supported |= CAN_CTRLMODE_3_SAMPLES;
		priv->read_reg = c_can_plat_read_reg_aligned_to_16bit;
		priv->write_reg = c_can_plat_write_reg_aligned_to_16bit;
		priv->read_reg32 = d_can_plat_read_reg32;
		priv->write_reg32 = d_can_plat_write_reg32;

		if (pdev->dev.of_node)
			priv->instance = of_alias_get_id(pdev->dev.of_node, "d_can");
		else
			priv->instance = pdev->id;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		/* Not all D_CAN modules have a separate register for the D_CAN
		 * RAM initialization. Use default RAM init bit in D_CAN module
		 * if not specified in DT.
		 */
		if (!res) {
			priv->raminit = c_can_hw_raminit;
			break;
		}

		priv->raminit_ctrlreg = devm_ioremap(&pdev->dev, res->start,
						     resource_size(res));
		if (!priv->raminit_ctrlreg || priv->instance < 0)
			dev_info(&pdev->dev, "control memory is not used for raminit\n");
		else
			priv->raminit = c_can_hw_raminit_ti;
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
	free_c_can_dev(dev);
exit:
	dev_err(&pdev->dev, "probe failed\n");

	return ret;
}

static int c_can_plat_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_c_can_dev(dev);

	free_c_can_dev(dev);

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
