

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/rockchip_io_vol_domain.h>

struct io_domain_port *g_uap;

int io_domain_regulator_set_voltage(struct regulator *regulator, int min_uV, int max_uV)
{
	int old_voltage,ret =0;

	old_voltage = io_domain_regulator_get_voltage(regulator);
	if (IS_ERR(g_uap->pins_3v3) ||IS_ERR(g_uap->pins_1v8) ){
			printk("IO_DOMAIN:ERROR:The io vol domin is not exit. %s\n",__func__);
	}

	if (min_uV  == old_voltage){
		IO_DOMAIN_DBG("IO_DOMAIN:the vol is not modify,not need to set io vol domain. %s \n",__func__);
		return 0;
	}
	else if (min_uV  > old_voltage){
		if( min_uV > IO_VOL_DOMAIN_3V3){
			pinctrl_select_state(g_uap->pctl, g_uap->pins_3v3);
			IO_DOMAIN_DBG("IO_DOMAIN:set io domain 3.3v. %s \n",__func__);
		}
		else if (min_uV > IO_VOL_DOMAIN_1V8){
			pinctrl_select_state(g_uap->pctl, g_uap->pins_1v8);
			IO_DOMAIN_DBG("IO_DOMAIN:set io domain 1.8v. %s \n",__func__);
		}
		ret = regulator_set_voltage(regulator,min_uV,max_uV);
	}
	else if (min_uV  < old_voltage){
		ret = regulator_set_voltage(regulator,min_uV,max_uV);
		if( min_uV > IO_VOL_DOMAIN_3V3){
			pinctrl_select_state(g_uap->pctl, g_uap->pins_3v3);
			IO_DOMAIN_DBG("IO_DOMAIN:set io domain 3.3v. %s \n",__func__);
		}
		else if (min_uV > IO_VOL_DOMAIN_1V8){
			pinctrl_select_state(g_uap->pctl, g_uap->pins_1v8);
			IO_DOMAIN_DBG("IO_DOMAIN:set io domain 1.8v. %s \n",__func__);
		}
	}
	return ret;

}


#if defined(CONFIG_PM)
static int rk_io_vol_domain_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int rk_io_vol_domain_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define rk_io_vol_domain_suspend NULL
#define rk_io_vol_domain_resume  NULL
#endif

static int rk_io_vol_domain_probe(struct platform_device *pdev)
{
	struct io_domain_port *uap;
	struct device_node *io_domain_node ;
	struct regulator *vol_regulator;
	struct io_domain_device *io_vol_dev = NULL;
	int vol=0,ret =0;

	io_vol_dev = devm_kzalloc(&pdev->dev, sizeof(struct io_domain_device), GFP_KERNEL);
	if (!io_vol_dev) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, io_vol_dev);
	io_vol_dev->dev = &pdev->dev;

	uap = devm_kzalloc(&pdev->dev, sizeof(struct io_domain_port),
			   GFP_KERNEL);
	if (uap == NULL) {
		printk("uap is not set %s,line=%d\n", __func__,__LINE__);
		
	}
	uap->pctl = devm_pinctrl_get(io_vol_dev->dev);
	uap->pins_default = pinctrl_lookup_state(uap->pctl,"default");
	uap->pins_1v8 = pinctrl_lookup_state(uap->pctl, "1.8V");
	uap->pins_3v3 = pinctrl_lookup_state(uap->pctl,"3.3V");
	g_uap = uap;

	io_domain_node = of_node_get(pdev->dev.of_node);
	if (!io_domain_node) {
		printk("could not find rk_io_vol_domain-node\n");
		return -ENODEV ;
	}

	io_domain_node->name = of_get_property(io_domain_node, "regulator-name", NULL);

	if (io_domain_node->name== NULL) {
		printk("%s io_domain_node->name(%s) get regulator_name err, ret:%d\n", __func__, io_domain_node->name,ret);
		return -ENODEV ;
	}
	vol_regulator = regulator_get(NULL,io_domain_node->name);
	if (IS_ERR(vol_regulator)){
		pinctrl_select_state(uap->pctl, uap->pins_default);
		IO_DOMAIN_DBG("IO_DOMAIN:ERROR:The io vol domin regulator name is not exit,set it by defult. %s %s\n",__func__,io_domain_node->name);
		return 0 ;
	}
	else{
		vol = regulator_get_voltage(vol_regulator);
		if (vol > IO_VOL_DOMAIN_3V3){
			pinctrl_select_state(uap->pctl, uap->pins_3v3);
			IO_DOMAIN_DBG("IO_DOMAIN:set io domain 3.3v. %s %s = %d\n",__func__,io_domain_node->name,vol);
			}
		else if (vol > IO_VOL_DOMAIN_1V8){
			pinctrl_select_state(uap->pctl, uap->pins_1v8);
			IO_DOMAIN_DBG("IO_DOMAIN:set io domain 1.8v. %s %s = %d\n",__func__,io_domain_node->name,vol);
			}
		else{
			pinctrl_select_state(uap->pctl, uap->pins_default);
			IO_DOMAIN_DBG("IO_DOMAIN:ERROR:The io vol domin is not exit,set it by defult. %s %s\n",__func__,io_domain_node->name);
			}
	}
	regulator_put(vol_regulator);
	return 0;
}

static int rk_io_vol_domain_remove(struct platform_device *pdev)
{

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id rk_io_vol_domain_dt_ids[] = {
	{.compatible = "rockchip,io_vol_domain", .data = NULL, },
	{}
};
#endif

static struct platform_driver rk_io_vol_domain_driver = {
	.probe = rk_io_vol_domain_probe,
	.remove = rk_io_vol_domain_remove,
	.driver = {
		   .name = "rk_io_vol_domain",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk_io_vol_domain_dt_ids),
		   },
	.suspend = rk_io_vol_domain_suspend,
	.resume = rk_io_vol_domain_resume,
};

static int __init rk_io_vol_domain_module_init(void)
{
	return platform_driver_register(&rk_io_vol_domain_driver);
}

static void __exit rk_io_vol_domain_module_exit(void)
{
	platform_driver_unregister(&rk_io_vol_domain_driver);
}

fs_initcall(rk_io_vol_domain_module_init);
module_exit(rk_io_vol_domain_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("rk io domain setting");


