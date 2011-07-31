/*
 * drivers/media/video/tegra/tegra_camera.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <mach/iomap.h>
#include <mach/clk.h>

#include <media/tegra_camera.h>

/* Eventually this should handle all clock and reset calls for the isp, vi,
 * vi_sensor, and csi modules, replacing nvrm and nvos completely for camera
 */
#define TEGRA_CAMERA_NAME "tegra_camera"
DEFINE_MUTEX(tegra_camera_lock);

struct tegra_camera_block {
	int (*enable) (void);
	int (*disable) (void);
	bool is_enabled;
};


static struct clk *isp_clk;
static struct clk *vi_clk;
static struct clk *vi_sensor_clk;
static struct clk *csus_clk;
static struct clk *csi_clk;
static struct regulator *tegra_camera_regulator_csi;

static int tegra_camera_enable_isp(void)
{
	return clk_enable(isp_clk);
}

static int tegra_camera_disable_isp(void)
{
	clk_disable(isp_clk);
	return 0;
}

static int tegra_camera_enable_vi(void)
{
	clk_enable(vi_clk);
	clk_enable(vi_sensor_clk);
	clk_enable(csus_clk);
	return 0;
}

static int tegra_camera_disable_vi(void)
{
	clk_disable(vi_clk);
	clk_disable(vi_sensor_clk);
	clk_disable(csus_clk);
	return 0;
}

static int tegra_camera_enable_csi(void)
{
	int ret;

	ret = regulator_enable(tegra_camera_regulator_csi);
	if (ret)
		return ret;
	clk_enable(csi_clk);
	return 0;
}

static int tegra_camera_disable_csi(void)
{
	int ret;

	ret = regulator_disable(tegra_camera_regulator_csi);
	if (ret)
		return ret;
	clk_disable(csi_clk);
	return 0;
}

struct tegra_camera_block tegra_camera_block[] = {
	[TEGRA_CAMERA_MODULE_ISP] = {tegra_camera_enable_isp,
		tegra_camera_disable_isp, false},
	[TEGRA_CAMERA_MODULE_VI] = {tegra_camera_enable_vi,
		tegra_camera_disable_vi, false},
	[TEGRA_CAMERA_MODULE_CSI] = {tegra_camera_enable_csi,
		tegra_camera_disable_csi, false},
};

#define TEGRA_CAMERA_VI_CLK_SEL_INTERNAL 0
#define TEGRA_CAMERA_VI_CLK_SEL_EXTERNAL (1<<24)
#define TEGRA_CAMERA_PD2VI_CLK_SEL_VI_SENSOR_CLK (1<<25)
#define TEGRA_CAMERA_PD2VI_CLK_SEL_PD2VI_CLK 0

static int tegra_camera_clk_set_rate(struct tegra_camera_clk_info *info)
{
	u32 offset;
	struct clk *clk;

	if (info->id != TEGRA_CAMERA_MODULE_VI) {
		pr_err("%s: Set rate only aplies to vi module %d\n", __func__,
		       info->id);
		return -EINVAL;
	}

	switch (info->clk_id) {
	case TEGRA_CAMERA_VI_CLK:
		clk = vi_clk;
		offset = 0x148;
		break;
	case TEGRA_CAMERA_VI_SENSOR_CLK:
		clk = vi_sensor_clk;
		offset = 0x1a8;
		break;
	default:
		pr_err("%s: invalid clk id for set rate %d\n", __func__,
		       info->clk_id);
		return -EINVAL;
	}

	clk_set_rate(clk, info->rate);

	if (info->clk_id == TEGRA_CAMERA_VI_CLK) {
		u32 val;
		void __iomem *car = IO_ADDRESS(TEGRA_CLK_RESET_BASE);
		void __iomem *apb_misc = IO_ADDRESS(TEGRA_APB_MISC_BASE);

		writel(0x2, car + offset);

		val = readl(apb_misc + 0x42c);
		writel(val | 0x1, apb_misc + 0x42c);
	}

	info->rate = clk_get_rate(clk);
	return 0;

}
static int tegra_camera_reset(uint id)
{
	struct clk *clk;

	switch (id) {
	case TEGRA_CAMERA_MODULE_VI:
		clk = vi_clk;
		break;
	case TEGRA_CAMERA_MODULE_ISP:
		clk = isp_clk;
		break;
	case TEGRA_CAMERA_MODULE_CSI:
		clk = csi_clk;
		break;
	default:
		return -EINVAL;
	}
	tegra_periph_reset_assert(clk);
	udelay(10);
	tegra_periph_reset_deassert(clk);

	return 0;
}

static long tegra_camera_ioctl(struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	uint id;

	/* first element of arg must be u32 with id of module to talk to */
	if (copy_from_user(&id, (const void __user *)arg, sizeof(uint))) {
		pr_err("%s: Failed to copy arg from user", __func__);
		return -EFAULT;
	}

	if (id >= ARRAY_SIZE(tegra_camera_block)) {
		pr_err("%s: Invalid id to tegra isp ioctl%d\n", __func__, id);
		return -EINVAL;
	}

	switch (cmd) {
	case TEGRA_CAMERA_IOCTL_ENABLE:
	{
		int ret = 0;

		mutex_lock(&tegra_camera_lock);
		if (!tegra_camera_block[id].is_enabled) {
			ret = tegra_camera_block[id].enable();
			tegra_camera_block[id].is_enabled = true;
		}
		mutex_unlock(&tegra_camera_lock);
		return ret;
	}
	case TEGRA_CAMERA_IOCTL_DISABLE:
	{
		int ret = 0;

		mutex_lock(&tegra_camera_lock);
		if (tegra_camera_block[id].is_enabled) {
			ret = tegra_camera_block[id].disable();
			tegra_camera_block[id].is_enabled = false;
		}
		mutex_unlock(&tegra_camera_lock);
		return ret;
	}
	case TEGRA_CAMERA_IOCTL_CLK_SET_RATE:
	{
		struct tegra_camera_clk_info info;
		int ret;

		if (copy_from_user(&info, (const void __user *)arg,
				   sizeof(struct tegra_camera_clk_info))) {
			pr_err("%s: Failed to copy arg from user\n", __func__);
			return -EFAULT;
		}
		ret = tegra_camera_clk_set_rate(&info);
		if (ret)
			return ret;
		if (copy_to_user((void __user *)arg, &info,
				 sizeof(struct tegra_camera_clk_info))) {
			pr_err("%s: Failed to copy arg to user\n", __func__);
			return -EFAULT;
		}
		return 0;
	}
	case TEGRA_CAMERA_IOCTL_RESET:
		return tegra_camera_reset(id);
	default:
		pr_err("%s: Unknown tegra_camera ioctl.\n", TEGRA_CAMERA_NAME);
		return -EINVAL;
	}
	return 0;
}

static int tegra_camera_release(struct inode *inode, struct file *file)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tegra_camera_block); i++)
		if (tegra_camera_block[i].is_enabled) {
			tegra_camera_block[i].disable();
			tegra_camera_block[i].is_enabled = false;
		}

	return 0;
}

static const struct file_operations tegra_camera_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tegra_camera_ioctl,
	.release = tegra_camera_release,
};

static struct miscdevice tegra_camera_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = TEGRA_CAMERA_NAME,
	.fops = &tegra_camera_fops,
};

static int tegra_camera_clk_get(struct platform_device *pdev, const char *name,
				struct clk **clk)
{
	*clk = clk_get(&pdev->dev, name);
	if (IS_ERR_OR_NULL(*clk)) {
		pr_err("%s: unable to get clock for %s\n", __func__, name);
		*clk = NULL;
		return PTR_ERR(*clk);
	}
	return 0;
}

static int tegra_camera_probe(struct platform_device *pdev)
{
	int err;

	pr_info("%s: probe\n", TEGRA_CAMERA_NAME);
	tegra_camera_regulator_csi = regulator_get(&pdev->dev, "vcsi");
	if (IS_ERR_OR_NULL(tegra_camera_regulator_csi)) {
		pr_err("%s: Couldn't get regulator vcsi\n", TEGRA_CAMERA_NAME);
		return PTR_ERR(tegra_camera_regulator_csi);
	}

	err = misc_register(&tegra_camera_device);
	if (err) {
		pr_err("%s: Unable to register misc device!\n",
		       TEGRA_CAMERA_NAME);
		goto misc_register_err;
	}

	err = tegra_camera_clk_get(pdev, "isp", &isp_clk);
	if (err)
		goto misc_register_err;
	err = tegra_camera_clk_get(pdev, "vi", &vi_clk);
	if (err)
		goto vi_clk_get_err;
	err = tegra_camera_clk_get(pdev, "vi_sensor", &vi_sensor_clk);
	if (err)
		goto vi_sensor_clk_get_err;
	err = tegra_camera_clk_get(pdev, "csus", &csus_clk);
	if (err)
		goto csus_clk_get_err;
	err = tegra_camera_clk_get(pdev, "csi", &csi_clk);
	if (err)
		goto csi_clk_get_err;

	return 0;

csi_clk_get_err:
	clk_put(csus_clk);
csus_clk_get_err:
	clk_put(vi_sensor_clk);
vi_sensor_clk_get_err:
	clk_put(vi_clk);
vi_clk_get_err:
	clk_put(isp_clk);
misc_register_err:
	regulator_put(tegra_camera_regulator_csi);
	return err;
}

static int tegra_camera_remove(struct platform_device *pdev)
{
	clk_put(isp_clk);
	clk_put(vi_clk);
	clk_put(vi_sensor_clk);
	clk_put(csus_clk);
	clk_put(csi_clk);

	regulator_put(tegra_camera_regulator_csi);
	misc_deregister(&tegra_camera_device);
	return 0;
}

static struct platform_driver tegra_camera_driver = {
	.probe = tegra_camera_probe,
	.remove = tegra_camera_remove,
	.driver = { .name = TEGRA_CAMERA_NAME }
};

static int __init tegra_camera_init(void)
{
	return platform_driver_register(&tegra_camera_driver);
}

static void __exit tegra_camera_exit(void)
{
	platform_driver_unregister(&tegra_camera_driver);
}

module_init(tegra_camera_init);
module_exit(tegra_camera_exit);

