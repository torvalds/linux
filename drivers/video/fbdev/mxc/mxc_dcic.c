/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/module.h>
#include <linux/mxc_dcic.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <video/videomode.h>
#include <video/of_videomode.h>

#define DRIVER_NAME	"mxc_dcic"

#define  DCIC_IPU1_DI0		"dcic-ipu1-di0"
#define  DCIC_IPU1_DI1		"dcic-ipu1-di1"
#define  DCIC_IPU2_DI0		"dcic-ipu2-di0"
#define  DCIC_IPU2_DI1		"dcic-ipu2-di1"
#define  DCIC_LCDIF			"dcic-lcdif"
#define  DCIC_LCDIF1		"dcic-lcdif1"
#define  DCIC_LCDIF2		"dcic-lcdif2"
#define  DCIC_LVDS			"dcic-lvds"
#define  DCIC_LVDS0			"dcic-lvds0"
#define  DCIC_LVDS1			"dcic-lvds1"
#define  DCIC_HDMI			"dcic-hdmi"

#define DCIC0_DEV_NAME "mxc_dcic0"
#define DCIC1_DEV_NAME "mxc_dcic1"

#define FB_SYNC_OE_LOW_ACT		0x80000000
#define FB_SYNC_CLK_LAT_FALL	0x40000000

static const struct dcic_mux imx6q_dcic0_mux[] = {
	{
		.dcic = DCIC_IPU1_DI0,
		.val = IMX6Q_GPR10_DCIC1_MUX_CTL_IPU1_DI0,
	}, {
		.dcic = DCIC_LVDS0,
		.val = IMX6Q_GPR10_DCIC1_MUX_CTL_LVDS0,
	}, {
		.dcic = DCIC_LVDS1,
		.val = IMX6Q_GPR10_DCIC1_MUX_CTL_LVDS1,
	}, {
		.dcic = DCIC_HDMI,
		.val = IMX6Q_GPR10_DCIC1_MUX_CTL_HDMI,
	}
};

static const struct dcic_mux imx6q_dcic1_mux[] = {
	{
		.dcic = DCIC_IPU1_DI1,
		.val = IMX6Q_GPR10_DCIC2_MUX_CTL_IPU1_DI1,
	}, {
		.dcic = DCIC_LVDS0,
		.val = IMX6Q_GPR10_DCIC2_MUX_CTL_LVDS0,
	}, {
		.dcic = DCIC_LVDS1,
		.val = IMX6Q_GPR10_DCIC2_MUX_CTL_LVDS1,
	}, {
		.dcic = DCIC_HDMI,
		.val = IMX6Q_GPR10_DCIC2_MUX_CTL_MIPI,
	}
};

static const struct bus_mux imx6q_dcic_buses[] = {
	{
		.name = DCIC0_DEV_NAME,
		.reg = IOMUXC_GPR10,
		.shift = 0,
		.mask = IMX6Q_GPR10_DCIC1_MUX_CTL_MASK,
		.dcic_mux_num = ARRAY_SIZE(imx6q_dcic0_mux),
		.dcics = imx6q_dcic0_mux,
	}, {
		.name = DCIC1_DEV_NAME,
		.reg = IOMUXC_GPR10,
		.shift = 2,
		.mask = IMX6Q_GPR10_DCIC2_MUX_CTL_MASK,
		.dcic_mux_num = ARRAY_SIZE(imx6q_dcic1_mux),
		.dcics = imx6q_dcic1_mux,
	}
};

static const struct dcic_info imx6q_dcic_info = {
	.bus_mux_num = ARRAY_SIZE(imx6q_dcic_buses),
	.buses = imx6q_dcic_buses,
};

static const struct dcic_mux imx6sx_dcic0_mux[] = {
	{
		.dcic = DCIC_LCDIF1,
		.val = IMX6SX_GPR5_DISP_MUX_DCIC1_LCDIF1,
	}, {
		.dcic = DCIC_LVDS,
		.val = IMX6SX_GPR5_DISP_MUX_DCIC1_LVDS,
	}
};

static const struct dcic_mux imx6sx_dcic1_mux[] = {
	{
		.dcic = DCIC_LCDIF2,
		.val = IMX6SX_GPR5_DISP_MUX_DCIC2_LCDIF2,
	}, {
		.dcic = DCIC_LVDS,
		.val = IMX6SX_GPR5_DISP_MUX_DCIC2_LVDS,
	}
};

static const struct bus_mux imx6sx_dcic_buses[] = {
	{
		.name = DCIC0_DEV_NAME,
		.reg = IOMUXC_GPR5,
		.shift = 1,
		.mask = IMX6SX_GPR5_DISP_MUX_DCIC1_MASK,
		.dcic_mux_num = ARRAY_SIZE(imx6sx_dcic0_mux),
		.dcics = imx6sx_dcic0_mux,
	}, {
		.name = DCIC1_DEV_NAME,
		.reg = IOMUXC_GPR5,
		.shift = 2,
		.mask = IMX6SX_GPR5_DISP_MUX_DCIC2_MASK,
		.dcic_mux_num = ARRAY_SIZE(imx6sx_dcic1_mux),
		.dcics = imx6sx_dcic1_mux,
	}
};

static const struct dcic_info imx6sx_dcic_info = {
	.bus_mux_num = ARRAY_SIZE(imx6sx_dcic_buses),
	.buses = imx6sx_dcic_buses,
};

static const struct of_device_id dcic_dt_ids[] = {
	{ .compatible = "fsl,imx6q-dcic", .data = &imx6q_dcic_info, },
	{ .compatible = "fsl,imx6sx-dcic", .data = &imx6sx_dcic_info, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dcic_dt_ids);

static int of_get_dcic_val(struct device_node *np, struct dcic_data *dcic)
{
	const char *mux;
	int ret;
	u32 i, dcic_id;

	ret = of_property_read_string(np, "dcic_mux", &mux);
	if (ret < 0) {
		dev_err(dcic->dev, "Can not get dcic_mux\n");
		return ret;
	}
	ret = of_property_read_u32(np, "dcic_id", &dcic_id);
	if (ret < 0) {
		dev_err(dcic->dev, "Can not get dcic_id\n");
		return ret;
	}

	dcic->bus_n = dcic_id;

	for (i = 0; i < dcic->buses[dcic_id].dcic_mux_num; i++)
		if (!strcmp(mux, dcic->buses[dcic_id].dcics[i].dcic)) {
			dcic->mux_n = i;
			return dcic->buses[dcic_id].dcics[i].val;
		}

	return -EINVAL;
}

static void dcic_enable(struct dcic_data *dcic)
{
	u32 val;

	val = readl(&dcic->regs->dcicc);
	val |= DCICC_IC_ENABLE;
	writel(val, &dcic->regs->dcicc);
}

void dcic_disable(struct dcic_data *dcic)
{
	u32 val;

	val = readl(&dcic->regs->dcicc);
	val &= ~DCICC_IC_MASK;
	val |= DCICC_IC_DISABLE;
	writel(val, &dcic->regs->dcicc);
}

static void roi_enable(struct dcic_data *dcic, struct roi_params *roi_param)
{
	u32 val;
	u32 roi_n = roi_param->roi_n;

	val = readl(&dcic->regs->ROI[roi_n].dcicrc);
	val |= DCICRC_ROI_ENABLE;
	if (roi_param->freeze)
		val |= DCICRC_ROI_FROZEN;
	writel(val, &dcic->regs->ROI[roi_n].dcicrc);
}

static void roi_disable(struct dcic_data *dcic, u32 roi_n)
{
	u32 val;

	val = readl(&dcic->regs->ROI[roi_n].dcicrc);
	val &= ~DCICRC_ROI_ENABLE;
	writel(val, &dcic->regs->ROI[roi_n].dcicrc);
}

static bool roi_configure(struct dcic_data *dcic, struct roi_params *roi_param)
{
	struct roi_regs *roi_reg;
	u32 val;

	if (roi_param->roi_n < 0 || roi_param->roi_n >= 16) {
		printk(KERN_ERR "Error, Wrong ROI number %d\n", roi_param->roi_n);
		return false;
	}

	if (roi_param->end_x <= roi_param->start_x ||
			roi_param->end_y <= roi_param->start_y) {
		printk(KERN_ERR "Error, Wrong ROI\n");
		return false;
	}

	roi_reg = (struct roi_regs *) &dcic->regs->ROI[roi_param->roi_n];

	/* init roi block size  */
	val = roi_param->start_y << 16 | roi_param->start_x;
	writel(val, &roi_reg->dcicrc);

	val = roi_param->end_y << 16 | roi_param->end_x;
	writel(val, &roi_reg->dcicrs);

	writel(roi_param->ref_sig, &roi_reg->dcicrrs);

	roi_enable(dcic, roi_param);
	return true;
}

static void dcic_int_enable(struct dcic_data *dcic)
{
	u32 val;

	/* Clean pending interrupt before enable int */
	writel(DCICS_FI_STAT_PENDING, &dcic->regs->dcics);
	writel(0xffffffff, &dcic->regs->dcics);

	/* Enable function interrupt */
	val = readl(&dcic->regs->dcicic);
	val &= ~DCICIC_FUN_INT_MASK;
	val |= DCICIC_FUN_INT_ENABLE;
	writel(val, &dcic->regs->dcicic);
}

static void dcic_int_disable(struct dcic_data *dcic)
{
	u32 val;

	/* Disable both function and error interrupt */
	val = readl(&dcic->regs->dcicic);
	val = DCICIC_ERROR_INT_DISABLE | DCICIC_FUN_INT_DISABLE;
	writel(val, &dcic->regs->dcicic);
}

static irqreturn_t dcic_irq_handler(int irq, void *data)
{
	u32 i;

	struct dcic_data *dcic = data;
	u32 dcics = readl(&dcic->regs->dcics);

	dcic->result = dcics & 0xffff;

	dcic_int_disable(dcic);

	/* clean dcic interrupt state */
	writel(DCICS_FI_STAT_PENDING, &dcic->regs->dcics);
	writel(dcics, &dcic->regs->dcics);

	for (i = 0; i < 16; i++) {
		printk(KERN_INFO "ROI=%d,crcRS=0x%x, crcCS=0x%x\n", i,
				readl(&dcic->regs->ROI[i].dcicrrs),
				readl(&dcic->regs->ROI[i].dcicrcs));
	}
	complete(&dcic->roi_crc_comp);

	return 0;
}

static int dcic_configure(struct dcic_data *dcic, unsigned int sync)
{
	u32 val;
	val = 0;

	/* vsync, hsync,  DE, clk_pol  */
	if (!(sync & FB_SYNC_HOR_HIGH_ACT))
		val |= DCICC_HSYNC_POL_ACTIVE_LOW;
	if (!(sync & FB_SYNC_VERT_HIGH_ACT))
		val |= DCICC_VSYNC_POL_ACTIVE_LOW;
	if (sync & FB_SYNC_OE_LOW_ACT)
		val |= DCICC_DE_ACTIVE_LOW;
	if (sync & FB_SYNC_CLK_LAT_FALL)
		val |= DCICC_CLK_POL_INVERTED;

	writel(val, &dcic->regs->dcicc);
	return 0;
}

static int dcic_open(struct inode *inode, struct file *file)
{
	struct dcic_data *dcic;

	dcic = container_of(inode->i_cdev, struct dcic_data, cdev);

	mutex_lock(&dcic->lock);

	clk_prepare_enable(dcic->disp_axi_clk);
	clk_prepare_enable(dcic->dcic_clk);

	file->private_data = dcic;
	mutex_unlock(&dcic->lock);
	return 0;
}

static int dcic_release(struct inode *inode, struct file *file)
{
	struct dcic_data *dcic = file->private_data;
	u32 i;

	mutex_lock(&dcic->lock);

	for (i = 0; i < 16; i++)
		roi_disable(dcic, i);

	clk_disable_unprepare(dcic->dcic_clk);
	clk_disable_unprepare(dcic->disp_axi_clk);

	mutex_unlock(&dcic->lock);
	return 0;
}

static int dcic_init(struct device_node *np, struct dcic_data *dcic)
{
	u32 val, bus;

	val = of_get_dcic_val(np, dcic);
	if (val < 0) {
		printk(KERN_ERR "Error incorrect\n");
		return -1;
	}

	bus = dcic->bus_n;

	regmap_update_bits(dcic->regmap, dcic->buses[bus].reg ,
			   dcic->buses[bus].mask, val);

	return 0;
}

static long dcic_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int __user *argp = (void __user *)arg;
	struct dcic_data *dcic = file->private_data;
	struct roi_params roi_param;
	unsigned int sync;
	int ret = 0;

	switch (cmd) {
	case DCIC_IOC_CONFIG_DCIC:
		if (!copy_from_user(&sync, argp, sizeof(unsigned int)))
			dcic_configure(dcic, sync);
		break;
	case DCIC_IOC_CONFIG_ROI:
		if (copy_from_user(&roi_param, argp, sizeof(roi_param)))
			return -EFAULT;
		else
			if (!roi_configure(dcic, &roi_param))
				return -EINVAL;
		break;
	case DCIC_IOC_GET_RESULT:
		init_completion(&dcic->roi_crc_comp);

		dcic_enable(dcic);

		dcic->result = 0;
		msleep(25);

		dcic_int_enable(dcic);

		ret = wait_for_completion_interruptible_timeout(
			&dcic->roi_crc_comp, 1 * HZ);
		if (ret == 0) {
			dev_err(dcic->dev,
			"dcic wait for roi crc cal timeout\n");
			ret = -ETIME;
		} else if (ret > 0) {
			if (copy_to_user(argp, &dcic->result, sizeof(dcic->result)))
				return -EFAULT;
			ret = 0;
		}
		dcic_disable(dcic);
		break;
	default:
		printk(KERN_ERR "%s, Unsupport cmd %d\n", __func__, cmd);
		break;
     }
     return ret;
}


static const struct file_operations mxc_dcic_fops = {
	.owner = THIS_MODULE,
	.open = dcic_open,
	.release = dcic_release,
	.unlocked_ioctl = dcic_ioctl,
};

static int dcic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id =
			of_match_device(dcic_dt_ids, dev);
	const struct dcic_info *dcic_info =
			(const struct dcic_info *)of_id->data;
	struct device_node *np = dev->of_node;
	struct dcic_data *dcic;
	struct resource *res;
	const char *name;
	dev_t devt;
	int ret = 0;
	int irq;

	dcic = devm_kzalloc(&pdev->dev,
				sizeof(struct dcic_data),
				GFP_KERNEL);
	if (!dcic) {
		dev_err(&pdev->dev, "Cannot allocate device data\n");
		ret = -ENOMEM;
		goto ealloc;
	}

	platform_set_drvdata(pdev, dcic);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No dcic base address found.\n");
		ret = -ENODEV;
		goto ealloc;
	}

	dcic->regs = (struct dcic_regs *) devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!dcic->regs) {
		dev_err(&pdev->dev, "ioremap failed with dcic base\n");
		ret = -ENOMEM;
		goto ealloc;
	}

	dcic->dev = dev;
	dcic->buses = dcic_info->buses;

	dcic->regmap = syscon_regmap_lookup_by_phandle(np, "gpr");
	if (IS_ERR(dcic->regmap)) {
		dev_err(dev, "failed to get parent regmap\n");
		ret = PTR_ERR(dcic->regmap);
		goto ealloc;
	}

	/* clock */
	dcic->disp_axi_clk = devm_clk_get(&pdev->dev, "disp-axi");
	if (IS_ERR(dcic->disp_axi_clk)) {
		dev_err(&pdev->dev, "get disp-axi clock failed\n");
		ret = PTR_ERR(dcic->disp_axi_clk);
		goto ealloc;
	}

	dcic->dcic_clk = devm_clk_get(&pdev->dev, "dcic");
	if (IS_ERR(dcic->dcic_clk)) {
		dev_err(&pdev->dev, "get dcic clk failed\n");
		ret = PTR_ERR(dcic->dcic_clk);
		goto ealloc;
	}

	mutex_init(&dcic->lock);
	ret = dcic_init(np, dcic);
	if (ret < 0) {
		printk(KERN_ERR "Failed init dcic\n");
		goto ealloc;
	}

	/* register device */
	name = dcic->buses[dcic->bus_n].name;
	dcic->major = register_chrdev(0, name, &mxc_dcic_fops);
	if (dcic->major < 0) {
		printk(KERN_ERR "DCIC: unable to get a major for dcic\n");
		ret = -EBUSY;
		goto ealloc;
	}

	dcic->class = class_create(THIS_MODULE, name);
	if (IS_ERR(dcic->class)) {
		ret = PTR_ERR(dcic->class);
		goto err_out_chrdev;
	}

	/* create char device */
	devt = MKDEV(dcic->major, 0);
	dcic->devt = devt;

	cdev_init(&dcic->cdev, &mxc_dcic_fops);
	dcic->cdev.owner = THIS_MODULE;
	ret = cdev_add(&dcic->cdev, devt, 1);
	if (ret)
		goto err_out_class;

	device_create(dcic->class, NULL, devt,
				   NULL, name);

	/* IRQ */
	irq = platform_get_irq(pdev, 0);

	ret = devm_request_irq(&pdev->dev, irq, dcic_irq_handler, 0,
			  dev_name(&pdev->dev), dcic);
	if (ret) {
		dev_err(&pdev->dev, "request_irq (%d) failed with error %d\n",
				irq, ret);
		goto err_out_cdev;
	}

	return 0;

err_out_cdev:
	cdev_del(&dcic->cdev);
err_out_class:
	device_destroy(dcic->class, devt);
	class_destroy(dcic->class);
err_out_chrdev:
	unregister_chrdev(dcic->major, name);
ealloc:
	return ret;
}

static int dcic_remove(struct platform_device *pdev)
{
	struct dcic_data *dcic = platform_get_drvdata(pdev);
	const char *name;

	name = dcic->buses[dcic->bus_n].name;

	device_destroy(dcic->class, dcic->devt);
	cdev_del(&dcic->cdev);
	class_destroy(dcic->class);
	unregister_chrdev(dcic->major, name);
	mutex_destroy(&dcic->lock);

	return 0;
}

static struct platform_driver dcic_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table	= dcic_dt_ids,
	},
	.probe = dcic_probe,
	.remove = dcic_remove,
};

module_platform_driver(dcic_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXC DCIC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
