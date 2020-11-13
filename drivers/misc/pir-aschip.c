// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

struct aschip_pir_drv_data {
	struct device		*dev;
	struct gpio_desc	*pulse_gpio;
	struct proc_dir_entry	*procfs;
};

static int pir_aschip_set_sensibility(struct aschip_pir_drv_data *drv_data,
				      uint32_t sensibility)
{
	if (sensibility > 100 || sensibility < 4) {
		dev_err(drv_data->dev, "sensibilit should be [4, 100]\n");
		return -EINVAL;
	}

	gpiod_set_value(drv_data->pulse_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value(drv_data->pulse_gpio, 0);
	usleep_range(50000, 51000);
	gpiod_set_value(drv_data->pulse_gpio, 1);
	usleep_range(sensibility * 1000, sensibility * 1000 + 1);
	gpiod_set_value(drv_data->pulse_gpio, 0);
	usleep_range(50000, 51000);
	gpiod_set_value(drv_data->pulse_gpio, 1);

	return 0;
}

#ifdef CONFIG_PROC_FS
static int pir_aschip_sensibility_show(struct seq_file *s, void *v)
{
	seq_puts(s, "set sensibility [4, 100] for PIR :\n");
	seq_puts(s, "	echo [nr] > /proc/pir/sensibility\n");

	return 0;
}

static int pir_aschip_sensibility_open(struct inode *inode, struct file *file)
{
	return single_open(file, pir_aschip_sensibility_show, PDE_DATA(inode));
}

static ssize_t pir_aschip_sensibility_write(struct file *filp,
					    const char __user *buf,
					    size_t cnt, loff_t *ppos)
{
	struct aschip_pir_drv_data *drv_data = ((struct seq_file *)filp->private_data)->private;
	unsigned int val;
	int ret;

	ret = kstrtouint_from_user(buf, cnt, 0, &val);
	if (ret)
		return ret;

	ret = pir_aschip_set_sensibility(drv_data, val);
	return ret ? ret : cnt;
}

static const struct file_operations pir_aschip_sensibility_fops = {
	.open		= pir_aschip_sensibility_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= pir_aschip_sensibility_write,
};

static int pir_aschip_create_procfs(struct aschip_pir_drv_data *drv_data)
{
	struct proc_dir_entry *ent;

	drv_data->procfs = proc_mkdir("pir", NULL);
	if (!drv_data->procfs)
		return -ENOMEM;

	ent = proc_create_data("sensibility", 0644, drv_data->procfs,
			       &pir_aschip_sensibility_fops, drv_data);
	if (!ent)
		goto fail;

	return 0;
fail:
	proc_remove(drv_data->procfs);
	return -ENOMEM;
}

static void pir_aschip_proc_release(struct aschip_pir_drv_data *drv_data)
{
	remove_proc_entry("sensibility", NULL);
	proc_remove(drv_data->procfs);
}
#endif

static int pir_aschip_probe(struct platform_device *pdev)
{
	struct aschip_pir_drv_data *drv_data;
	struct device *dev = &pdev->dev;

	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->pulse_gpio = devm_gpiod_get(dev, "pulse", GPIOD_OUT_HIGH);

	if (IS_ERR(drv_data->pulse_gpio)) {
		dev_err(dev, "Failed to get pwdn-gpios\n");
		return PTR_ERR(drv_data->pulse_gpio);
	}

#ifdef CONFIG_PROC_FS
	pir_aschip_create_procfs(drv_data);
#endif
	drv_data->dev = dev;
	dev_set_drvdata(dev, drv_data);
	return 0;
}

static int pir_aschip_remove(struct platform_device *pdev)
{
#ifdef CONFIG_PROC_FS
	struct aschip_pir_drv_data *drv_data;

	drv_data = dev_get_drvdata(&pdev->dev);
	pir_aschip_proc_release(drv_data);
#endif
	return 0;
}

static const struct of_device_id pir_aschip_rockchip_match[] = {
	{ .compatible = "aschip,pir" },
	{},
};

MODULE_DEVICE_TABLE(of, pir_aschip_rockchip_match);

static struct platform_driver pir_aschip_pltfm_driver = {
	.probe		= pir_aschip_probe,
	.remove		= pir_aschip_remove,
	.driver		= {
		.name		= "pir_aschip_aschip",
		.of_match_table	= pir_aschip_rockchip_match,
	},
};

module_platform_driver(pir_aschip_pltfm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ziyuan Xu");
MODULE_DESCRIPTION("Aschip PIR sensibility control driver");
