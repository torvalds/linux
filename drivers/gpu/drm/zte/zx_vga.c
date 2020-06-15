// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Sanechips Technology Co., Ltd.
 * Copyright 2017 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "zx_drm_drv.h"
#include "zx_vga_regs.h"
#include "zx_vou.h"

struct zx_vga_pwrctrl {
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};

struct zx_vga_i2c {
	struct i2c_adapter adap;
	struct mutex lock;
};

struct zx_vga {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct zx_vga_i2c *ddc;
	struct device *dev;
	void __iomem *mmio;
	struct clk *i2c_wclk;
	struct zx_vga_pwrctrl pwrctrl;
	struct completion complete;
	bool connected;
};

#define to_zx_vga(x) container_of(x, struct zx_vga, x)

static void zx_vga_encoder_enable(struct drm_encoder *encoder)
{
	struct zx_vga *vga = to_zx_vga(encoder);
	struct zx_vga_pwrctrl *pwrctrl = &vga->pwrctrl;

	/* Set bit to power up VGA DACs */
	regmap_update_bits(pwrctrl->regmap, pwrctrl->reg, pwrctrl->mask,
			   pwrctrl->mask);

	vou_inf_enable(VOU_VGA, encoder->crtc);
}

static void zx_vga_encoder_disable(struct drm_encoder *encoder)
{
	struct zx_vga *vga = to_zx_vga(encoder);
	struct zx_vga_pwrctrl *pwrctrl = &vga->pwrctrl;

	vou_inf_disable(VOU_VGA, encoder->crtc);

	/* Clear bit to power down VGA DACs */
	regmap_update_bits(pwrctrl->regmap, pwrctrl->reg, pwrctrl->mask, 0);
}

static const struct drm_encoder_helper_funcs zx_vga_encoder_helper_funcs = {
	.enable	= zx_vga_encoder_enable,
	.disable = zx_vga_encoder_disable,
};

static int zx_vga_connector_get_modes(struct drm_connector *connector)
{
	struct zx_vga *vga = to_zx_vga(connector);
	struct edid *edid;
	int ret;

	/*
	 * Clear both detection bits to switch I2C bus from device
	 * detecting to EDID reading.
	 */
	zx_writel(vga->mmio + VGA_AUTO_DETECT_SEL, 0);

	edid = drm_get_edid(connector, &vga->ddc->adap);
	if (!edid) {
		/*
		 * If EDID reading fails, we set the device state into
		 * disconnected.  Locking is not required here, since the
		 * VGA_AUTO_DETECT_SEL register write in irq handler cannot
		 * be triggered when both detection bits are cleared as above.
		 */
		zx_writel(vga->mmio + VGA_AUTO_DETECT_SEL,
			  VGA_DETECT_SEL_NO_DEVICE);
		vga->connected = false;
		return 0;
	}

	/*
	 * As edid reading succeeds, device must be connected, so we set
	 * up detection bit for unplug interrupt here.
	 */
	zx_writel(vga->mmio + VGA_AUTO_DETECT_SEL, VGA_DETECT_SEL_HAS_DEVICE);

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return ret;
}

static enum drm_mode_status
zx_vga_connector_mode_valid(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_connector_helper_funcs zx_vga_connector_helper_funcs = {
	.get_modes = zx_vga_connector_get_modes,
	.mode_valid = zx_vga_connector_mode_valid,
};

static enum drm_connector_status
zx_vga_connector_detect(struct drm_connector *connector, bool force)
{
	struct zx_vga *vga = to_zx_vga(connector);

	return vga->connected ? connector_status_connected :
				connector_status_disconnected;
}

static const struct drm_connector_funcs zx_vga_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = zx_vga_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int zx_vga_register(struct drm_device *drm, struct zx_vga *vga)
{
	struct drm_encoder *encoder = &vga->encoder;
	struct drm_connector *connector = &vga->connector;
	struct device *dev = vga->dev;
	int ret;

	encoder->possible_crtcs = VOU_CRTC_MASK;

	ret = drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_DAC);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init encoder: %d\n", ret);
		return ret;
	};

	drm_encoder_helper_add(encoder, &zx_vga_encoder_helper_funcs);

	vga->connector.polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init_with_ddc(drm, connector,
					  &zx_vga_connector_funcs,
					  DRM_MODE_CONNECTOR_VGA,
					  &vga->ddc->adap);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init connector: %d\n", ret);
		goto clean_encoder;
	};

	drm_connector_helper_add(connector, &zx_vga_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to attach encoder: %d\n", ret);
		goto clean_connector;
	};

	return 0;

clean_connector:
	drm_connector_cleanup(connector);
clean_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static int zx_vga_pwrctrl_init(struct zx_vga *vga)
{
	struct zx_vga_pwrctrl *pwrctrl = &vga->pwrctrl;
	struct device *dev = vga->dev;
	struct of_phandle_args out_args;
	struct regmap *regmap;
	int ret;

	ret = of_parse_phandle_with_fixed_args(dev->of_node,
				"zte,vga-power-control", 2, 0, &out_args);
	if (ret)
		return ret;

	regmap = syscon_node_to_regmap(out_args.np);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto out;
	}

	pwrctrl->regmap = regmap;
	pwrctrl->reg = out_args.args[0];
	pwrctrl->mask = out_args.args[1];

out:
	of_node_put(out_args.np);
	return ret;
}

static int zx_vga_i2c_read(struct zx_vga *vga, struct i2c_msg *msg)
{
	int len = msg->len;
	u8 *buf = msg->buf;
	u32 offset = 0;
	int i;

	reinit_completion(&vga->complete);

	/* Select combo write */
	zx_writel_mask(vga->mmio + VGA_CMD_CFG, VGA_CMD_COMBO, VGA_CMD_COMBO);
	zx_writel_mask(vga->mmio + VGA_CMD_CFG, VGA_CMD_RW, 0);

	while (len > 0) {
		u32 cnt;

		/* Clear RX FIFO */
		zx_writel_mask(vga->mmio + VGA_RXF_CTRL, VGA_RX_FIFO_CLEAR,
			       VGA_RX_FIFO_CLEAR);

		/* Data offset to read from */
		zx_writel(vga->mmio + VGA_SUB_ADDR, offset);

		/* Kick off the transfer */
		zx_writel_mask(vga->mmio + VGA_CMD_CFG, VGA_CMD_TRANS,
			       VGA_CMD_TRANS);

		if (!wait_for_completion_timeout(&vga->complete,
						 msecs_to_jiffies(1000))) {
			DRM_DEV_ERROR(vga->dev, "transfer timeout\n");
			return -ETIMEDOUT;
		}

		cnt = zx_readl(vga->mmio + VGA_RXF_STATUS);
		cnt = (cnt & VGA_RXF_COUNT_MASK) >> VGA_RXF_COUNT_SHIFT;
		/* FIFO status may report more data than we need to read */
		cnt = min_t(u32, len, cnt);

		for (i = 0; i < cnt; i++)
			*buf++ = zx_readl(vga->mmio + VGA_DATA);

		len -= cnt;
		offset += cnt;
	}

	return 0;
}

static int zx_vga_i2c_write(struct zx_vga *vga, struct i2c_msg *msg)
{
	/*
	 * The DDC I2C adapter is only for reading EDID data, so we assume
	 * that the write to this adapter must be the EDID data offset.
	 */
	if ((msg->len != 1) || ((msg->addr != DDC_ADDR)))
		return -EINVAL;

	/* Hardware will take care of the slave address shifting */
	zx_writel(vga->mmio + VGA_DEVICE_ADDR, msg->addr);

	return 0;
}

static int zx_vga_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num)
{
	struct zx_vga *vga = i2c_get_adapdata(adap);
	struct zx_vga_i2c *ddc = vga->ddc;
	int ret = 0;
	int i;

	mutex_lock(&ddc->lock);

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD)
			ret = zx_vga_i2c_read(vga, &msgs[i]);
		else
			ret = zx_vga_i2c_write(vga, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	mutex_unlock(&ddc->lock);

	return ret;
}

static u32 zx_vga_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm zx_vga_algorithm = {
	.master_xfer	= zx_vga_i2c_xfer,
	.functionality	= zx_vga_i2c_func,
};

static int zx_vga_ddc_register(struct zx_vga *vga)
{
	struct device *dev = vga->dev;
	struct i2c_adapter *adap;
	struct zx_vga_i2c *ddc;
	int ret;

	ddc = devm_kzalloc(dev, sizeof(*ddc), GFP_KERNEL);
	if (!ddc)
		return -ENOMEM;

	vga->ddc = ddc;
	mutex_init(&ddc->lock);

	adap = &ddc->adap;
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_DDC;
	adap->dev.parent = dev;
	adap->algo = &zx_vga_algorithm;
	snprintf(adap->name, sizeof(adap->name), "zx vga i2c");

	ret = i2c_add_adapter(adap);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to add I2C adapter: %d\n", ret);
		return ret;
	}

	i2c_set_adapdata(adap, vga);

	return 0;
}

static irqreturn_t zx_vga_irq_thread(int irq, void *dev_id)
{
	struct zx_vga *vga = dev_id;

	drm_helper_hpd_irq_event(vga->connector.dev);

	return IRQ_HANDLED;
}

static irqreturn_t zx_vga_irq_handler(int irq, void *dev_id)
{
	struct zx_vga *vga = dev_id;
	u32 status;

	status = zx_readl(vga->mmio + VGA_I2C_STATUS);

	/* Clear interrupt status */
	zx_writel_mask(vga->mmio + VGA_I2C_STATUS, VGA_CLEAR_IRQ,
		       VGA_CLEAR_IRQ);

	if (status & VGA_DEVICE_CONNECTED) {
		/*
		 * Since VGA_DETECT_SEL bits need to be reset for switching DDC
		 * bus from device detection to EDID read, rather than setting
		 * up HAS_DEVICE bit here, we need to do that in .get_modes
		 * hook for unplug detecting after EDID read succeeds.
		 */
		vga->connected = true;
		return IRQ_WAKE_THREAD;
	}

	if (status & VGA_DEVICE_DISCONNECTED) {
		zx_writel(vga->mmio + VGA_AUTO_DETECT_SEL,
			  VGA_DETECT_SEL_NO_DEVICE);
		vga->connected = false;
		return IRQ_WAKE_THREAD;
	}

	if (status & VGA_TRANS_DONE) {
		complete(&vga->complete);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void zx_vga_hw_init(struct zx_vga *vga)
{
	unsigned long ref = clk_get_rate(vga->i2c_wclk);
	int div;

	/*
	 * Set up I2C fast speed divider per formula below to get 400kHz.
	 *   scl = ref / ((div + 1) * 4)
	 */
	div = DIV_ROUND_UP(ref / 1000, 400 * 4) - 1;
	zx_writel(vga->mmio + VGA_CLK_DIV_FS, div);

	/* Set up device detection */
	zx_writel(vga->mmio + VGA_AUTO_DETECT_PARA, 0x80);
	zx_writel(vga->mmio + VGA_AUTO_DETECT_SEL, VGA_DETECT_SEL_NO_DEVICE);

	/*
	 * We need to poke monitor via DDC bus to get connection irq
	 * start working.
	 */
	zx_writel(vga->mmio + VGA_DEVICE_ADDR, DDC_ADDR);
	zx_writel_mask(vga->mmio + VGA_CMD_CFG, VGA_CMD_TRANS, VGA_CMD_TRANS);
}

static int zx_vga_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct resource *res;
	struct zx_vga *vga;
	int irq;
	int ret;

	vga = devm_kzalloc(dev, sizeof(*vga), GFP_KERNEL);
	if (!vga)
		return -ENOMEM;

	vga->dev = dev;
	dev_set_drvdata(dev, vga);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vga->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(vga->mmio))
		return PTR_ERR(vga->mmio);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	vga->i2c_wclk = devm_clk_get(dev, "i2c_wclk");
	if (IS_ERR(vga->i2c_wclk)) {
		ret = PTR_ERR(vga->i2c_wclk);
		DRM_DEV_ERROR(dev, "failed to get i2c_wclk: %d\n", ret);
		return ret;
	}

	ret = zx_vga_pwrctrl_init(vga);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init power control: %d\n", ret);
		return ret;
	}

	ret = zx_vga_ddc_register(vga);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register ddc: %d\n", ret);
		return ret;
	}

	ret = zx_vga_register(drm, vga);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register vga: %d\n", ret);
		return ret;
	}

	init_completion(&vga->complete);

	ret = devm_request_threaded_irq(dev, irq, zx_vga_irq_handler,
					zx_vga_irq_thread, IRQF_SHARED,
					dev_name(dev), vga);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to request threaded irq: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vga->i2c_wclk);
	if (ret)
		return ret;

	zx_vga_hw_init(vga);

	return 0;
}

static void zx_vga_unbind(struct device *dev, struct device *master,
			  void *data)
{
	struct zx_vga *vga = dev_get_drvdata(dev);

	clk_disable_unprepare(vga->i2c_wclk);
}

static const struct component_ops zx_vga_component_ops = {
	.bind = zx_vga_bind,
	.unbind = zx_vga_unbind,
};

static int zx_vga_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &zx_vga_component_ops);
}

static int zx_vga_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &zx_vga_component_ops);
	return 0;
}

static const struct of_device_id zx_vga_of_match[] = {
	{ .compatible = "zte,zx296718-vga", },
	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, zx_vga_of_match);

struct platform_driver zx_vga_driver = {
	.probe = zx_vga_probe,
	.remove = zx_vga_remove,
	.driver	= {
		.name = "zx-vga",
		.of_match_table	= zx_vga_of_match,
	},
};
