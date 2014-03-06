/*
 * drivers/video/rockchip/hdmi/chips/rk3288/rk3188_hdmi.c
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *Author:zwl<zwl@rock-chips.com>
 *This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#if defined(CONFIG_OF)
#include <linux/of.h>
#include <linux/of_device.h>
#endif
#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "rk3288_hdmi_hw.h"
#include "rk3288_hdmi.h"

extern irqreturn_t hdmi_irq(int irq, void *priv);

static struct rk3288_hdmi_device *hdmi_dev = NULL;

#if defined(CONFIG_DEBUG_FS)
static int rk3288_hdmi_reg_show(struct seq_file *s, void *v)
{
	int i = 0;
	u32 val = 0;
	seq_printf(s, "\n>>>hdmi_ctl reg");
	for (i = 0; i < 16; i++) {
		seq_printf(s, " %2x", i);
	}
	seq_printf(s, "\n-----------------------------------------------------------------");

	for(i=0; i<= I2CM_SCDC_UPDATE1; i++) {
                hdmi_readl(hdmi_dev, i, &val);
		if(i%16==0)
			seq_printf(s,"\n>>>hdmi_ctl %2x:", i);
		seq_printf(s," %02x",val);

	}
	seq_printf(s, "\n-----------------------------------------------------------------\n");

	return 0;
}

static ssize_t rk3288_hdmi_reg_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	u32 reg;
	u32 val;
	char kbuf[25];
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	sscanf(kbuf, "%x%x", &reg, &val);
        if ((reg < 0) || (reg > I2CM_SCDC_UPDATE1)) {
                dev_info(hdmi_dev->dev, "it is no hdmi reg\n");
                return count;
        }
	dev_info(hdmi_dev->dev, "/**********rk3288 hdmi reg config******/");
	dev_info(hdmi_dev->dev, "\n reg=%x val=%x\n", reg, val);
        hdmi_writel(hdmi_dev, reg, val);

	return count;
}

static int rk3288_hdmi_reg_open(struct inode *inode, struct file *file)
{
	struct rk3288_hdmi_device *hdmi_dev = inode->i_private;
	return single_open(file,rk3288_hdmi_reg_show,hdmi_dev);
}

static const struct file_operations rk3288_hdmi_reg_fops = {
	.owner		= THIS_MODULE,
	.open		= rk3288_hdmi_reg_open,
	.read		= seq_read,
	.write          = rk3288_hdmi_reg_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int rk3288_hdmi_drv_init(struct hdmi *hdmi_drv)
{
	int ret = 0;
	//struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk_hdmi_device, driver);

        //grf_writel(HDMI_SEL_LCDC(hdmi_dev->lcdc_id),GRF_SOC_CON6);	//lcdc source select-->have config at lcdc driver so delete it
	if(hdmi_dev->lcdc_id == 0)
		hdmi_drv->lcdc = rk_get_lcdc_drv("lcdc0");
	else
		hdmi_drv->lcdc = rk_get_lcdc_drv("lcdc1");
	if(IS_ERR(hdmi_drv->lcdc))
	{
		dev_err(hdmi_drv->dev, "can not connect to video source lcdc\n");
		ret = -ENXIO;
		return ret;
	}

	hdmi_drv->xscale = 100;
	hdmi_drv->yscale = 100;

	spin_lock_init(&hdmi_drv->irq_lock);
	mutex_init(&hdmi_drv->enable_mutex);

	rk3288_hdmi_initial(hdmi_drv);
	hdmi_sys_init(hdmi_drv);
	hdmi_drv_register(hdmi_drv);

	return ret;
}

#if defined(CONFIG_OF)
static int rk3288_hdmi_parse_dt(struct rk3288_hdmi_device *hdmi_dev)
{
	int val = 0;
	struct device_node *np = hdmi_dev->dev->of_node;

	if(!of_property_read_u32(np, "rockchips,hdmi_lcdc_source", &val))
		hdmi_dev->lcdc_id = val;

	return 0;
}

static const struct of_device_id rk3288_hdmi_dt_ids[] = {
	{.compatible = "rockchips,rk3288-hdmi",},
	{}
};
MODULE_DEVICE_TABLE(of, rk3288_hdmi_dt_ids);
#endif

static int rk3288_hdmi_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct hdmi *dev_drv = NULL;

	hdmi_dev = kzalloc(sizeof(struct rk3288_hdmi_device), GFP_KERNEL);
	if(!hdmi_dev) {
		dev_err(&pdev->dev, ">>rk3288_hdmi_device kzalloc fail!");
		return -ENOMEM;
	}

	hdmi_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, hdmi_dev);

	rk3288_hdmi_parse_dt(hdmi_dev);
	//TODO Daisen wait to add cec iomux

	/*enable hclk*/
	hdmi_dev->hclk = devm_clk_get(hdmi_dev->dev,"hclk_hdmi");	//TODO Daisen wait to modify
	if(IS_ERR(hdmi_dev->hclk)) {
		dev_err(hdmi_dev->dev, "Unable to get hdmi hclk\n");
		ret = -ENXIO;
		goto err0;
	}
	clk_prepare_enable(hdmi_dev->hclk);

	/*request and remap iomem*/
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(hdmi_dev->dev, "Unable to get register resource\n");
		ret = -ENXIO;
		goto err0;
	}
	hdmi_dev->regbase_phy = res->start;
	hdmi_dev->regsize_phy = resource_size(res);
	hdmi_dev->regbase = devm_ioremap_resource(hdmi_dev->dev, res);
	if (IS_ERR(hdmi_dev->regbase)) {
		ret = PTR_ERR(hdmi_dev->regbase);
		dev_err(hdmi_dev->dev, "cannot ioremap registers,err=%d\n",ret);
		goto err1;
	}

	/*init hdmi driver*/
	dev_drv = &hdmi_dev->driver;
	dev_drv->dev = &pdev->dev;
	if(rk3288_hdmi_drv_init(dev_drv)) {
		goto err1;
	}

	dev_drv->workqueue = create_singlethread_workqueue("hdmi");
	INIT_DELAYED_WORK(&(dev_drv->delay_work), hdmi_work);

	hdmi_register_display_sysfs(dev_drv, NULL);

#ifdef CONFIG_SWITCH
	dev_drv->switch_hdmi.name = "hdmi";
	switch_dev_register(&(dev_drv->switch_hdmi));
#endif

	/* get and request the IRQ */
	dev_drv->irq = platform_get_irq(pdev, 0);
	if(dev_drv->irq <= 0) {
		dev_err(hdmi_dev->dev, "failed to get hdmi irq resource (%d).\n", hdmi_dev->irq);
		ret = -ENXIO;
		goto err2;
	}

	ret = request_irq(dev_drv->irq, hdmi_irq, 0, dev_name(hdmi_dev->dev), dev_drv);
	if (ret) {
		dev_err(hdmi_dev->dev, "hdmi request_irq failed (%d).\n", ret);
		goto err2;
	}

#if defined(CONFIG_DEBUG_FS)
        hdmi_dev->debugfs_dir = debugfs_create_dir("rk3288-hdmi", NULL);
	if (IS_ERR(hdmi_dev->debugfs_dir)) {
		dev_err(hdmi_dev->dev,"failed to create debugfs dir for rk3288 hdmi!\n");
	} else {
		debugfs_create_file("hdmi", S_IRUSR, hdmi_dev->debugfs_dir, hdmi_dev, &rk3288_hdmi_reg_fops);
	}
#endif

	dev_info(hdmi_dev->dev, "rk3288 hdmi probe sucess.\n");
	return 0;

err2:
#ifdef CONFIG_SWITCH
	switch_dev_unregister(&(dev_drv->switch_hdmi));
#endif
	hdmi_unregister_display_sysfs(dev_drv);

	//iounmap((void*)hdmi_dev->regbase);
err1:
	//release_mem_region(res->start,hdmi_dev->regsize_phy);
	clk_disable_unprepare(hdmi_dev->hclk);
err0:
	dev_info(hdmi_dev->dev, "rk3288 hdmi probe error.\n");
	kfree(hdmi_dev);
	hdmi_dev = NULL;
	return ret;
}

static int rk3288_hdmi_remove(struct platform_device *pdev)
{
	struct rk3288_hdmi_device *hdmi_dev = platform_get_drvdata(pdev);
	struct hdmi *hdmi_drv = NULL;

	if(hdmi_dev) {
		hdmi_drv = &hdmi_dev->driver;
		mutex_lock(&hdmi_drv->enable_mutex);
		if(!hdmi_drv->suspend && hdmi_drv->enable)
			disable_irq(hdmi_drv->irq);
		mutex_unlock(&hdmi_drv->enable_mutex);
		free_irq(hdmi_drv->irq, NULL);

		flush_workqueue(hdmi_drv->workqueue);
		destroy_workqueue(hdmi_drv->workqueue);

		#ifdef CONFIG_SWITCH
		switch_dev_unregister(&(hdmi_drv->switch_hdmi));
		#endif
		hdmi_unregister_display_sysfs(hdmi_drv);

		//iounmap((void*)hdmi_drv->regbase);
		//release_mem_region(hdmi_drv->regbase_phy, hdmi_drv->regsize_phy);
		clk_disable_unprepare(hdmi_dev->hclk);
		fb_destroy_modelist(&hdmi_drv->edid.modelist);
		if(hdmi_drv->edid.audio)
			kfree(hdmi_drv->edid.audio);
		if(hdmi_drv->edid.specs)
		{
			if(hdmi_drv->edid.specs->modedb)
				kfree(hdmi_drv->edid.specs->modedb);
			kfree(hdmi_drv->edid.specs);
		}

		kfree(hdmi_dev);
		hdmi_dev = NULL;
	}
	printk(KERN_INFO "rk3288 hdmi removed.\n");
	return 0;
}

static void rk3288_hdmi_shutdown(struct platform_device *pdev)
{

}

static struct platform_driver rk3288_hdmi_driver = {
	.probe		= rk3288_hdmi_probe,
	.remove		= rk3288_hdmi_remove,
	.driver		= {
		.name	= "rk3288-hdmi",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rk3288_hdmi_dt_ids),
	},
	.shutdown   = rk3288_hdmi_shutdown,
};

static int __init rk3288_hdmi_init(void)
{
	return platform_driver_register(&rk3288_hdmi_driver);
}

static void __exit rk3288_hdmi_exit(void)
{
	platform_driver_unregister(&rk3288_hdmi_driver);
}

device_initcall_sync(rk3288_hdmi_init);
module_exit(rk3288_hdmi_exit);

