/*
 * Copyright (C) ST-Ericsson 2010 - 2013
 * Author: Martin Persson <martin.persson@stericsson.com>
 *         Hongbo Zhang <hongbo.zhang@linaro.org>
 * License Terms: GNU General Public License v2
 *
 * When the AB8500 thermal warning temperature is reached (threshold cannot
 * be changed by SW), an interrupt is set, and if no further action is taken
 * within a certain time frame, pm_power off will be called.
 *
 * When AB8500 thermal shutdown temperature is reached a hardware shutdown of
 * the AB8500 will occur.
 */

 #include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/ioport.h>

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include <linux/timer.h>
#include <linux/completion.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include "hwmon-rockchip.h"


#define DEFAULT_POWER_OFF_DELAY	(HZ * 10)
/* Number of monitored sensors should not greater than NUM_SENSORS */
#define NUM_MONITORED_SENSORS	4

#define TSADC_USER_CON  0x00
#define TSADC_AUTO_CON  0x04

#define TSADC_CTRL_CH(ch)	((ch) << 0)
#define TSADC_CTRL_POWER_UP	(1 << 3)
#define TSADC_CTRL_START	(1 << 4)

#define TSADC_STAS_BUSY		(1 << 12)
#define TSADC_STAS_BUSY_MASK	(1 << 12)
#define TSADC_AUTO_STAS_BUSY		(1 << 16)
#define TSADC_AUTO_STAS_BUSY_MASK	(1 << 16)
#define TSADC_SAMPLE_DLY_SEL  (1 << 17)
#define TSADC_SAMPLE_DLY_SEL_MASK  (1 << 17)

#define TSADC_INT_EN  0x08
#define TSADC_INT_PD  0x0c

#define TSADC_DATA0  0x20
#define TSADC_DATA1  0x24
#define TSADC_DATA2  0x28
#define TSADC_DATA3  0x2c
#define TSADC_DATA_MASK		0xfff

#define TSADC_COMP0_INT  0x30
#define TSADC_COMP1_INT  0x34
#define TSADC_COMP2_INT  0x38
#define TSADC_COMP3_INT  0x3c

#define TSADC_COMP0_SHUT  0x40
#define TSADC_COMP1_SHUT  0x44
#define TSADC_COMP2_SHUT  0x48
#define TSADC_COMP3_SHUT  0x4c

#define TSADC_HIGHT_INT_DEBOUNCE  0x60
#define TSADC_HIGHT_TSHUT_DEBOUNCE  0x64
#define TSADC_HIGHT_INT_DEBOUNCE_TIME 0x0a
#define TSADC_HIGHT_TSHUT_DEBOUNCE_TIME 0x0a

#define TSADC_AUTO_PERIOD  0x68
#define TSADC_AUTO_PERIOD_HT  0x6c
#define TSADC_AUTO_PERIOD_TIME	0x03e8
#define TSADC_AUTO_PERIOD_HT_TIME  0x64

#define TSADC_AUTO_EVENT_NAME		"tsadc"

#define TSADC_COMP_INT_DATA		80
#define TSADC_COMP_INT_DATA_MASK		0xfff
#define TSADC_COMP_SHUT_DATA_MASK		0xfff
#define TSADC_TEMP_INT_EN 0
#define TSADC_TEMP_SHUT_EN 1
static int tsadc_ht_temp;
static int tsadc_ht_reset_cru;
static int tsadc_ht_pull_gpio;

struct tsadc_port {
	struct pinctrl		*pctl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_tsadc_int;
};

struct rockchip_tsadc_temp {
	struct delayed_work power_off_work;
	struct rockchip_temp *rockchip_data;
	void __iomem		*regs;
	struct clk		*clk;
	struct clk		*pclk;
	int irq;
	struct resource		*ioarea;
	struct tsadc_host		*tsadc;
	struct work_struct 	auto_ht_irq_work;
	struct workqueue_struct  *workqueue;
	struct workqueue_struct  *tsadc_workqueue;
};
struct tsadc_table
{
	int code;
	int temp;
};

static const struct tsadc_table table[] =
{
	{TSADC_DATA_MASK, -40},

	{3800, -40},
	{3792, -35},
	{3783, -30},
	{3774, -25},
	{3765, -20},
	{3756, -15},
	{3747, -10},
	{3737, -5},
	{3728, 0},
	{3718, 5},

	{3708, 10},
	{3698, 15},
	{3688, 20},
	{3678, 25},
	{3667, 30},
	{3656, 35},
	{3645, 40},
	{3634, 45},
	{3623, 50},
	{3611, 55},

	{3600, 60},
	{3588, 65},
	{3575, 70},
	{3563, 75},
	{3550, 80},
	{3537, 85},
	{3524, 90},
	{3510, 95},
	{3496, 100},
	{3482, 105},

	{3467, 110},
	{3452, 115},
	{3437, 120},
	{3421, 125},

	{0, 125},
};

static struct rockchip_tsadc_temp *g_dev;

static DEFINE_MUTEX(tsadc_mutex);

static u32 tsadc_readl(u32 offset)
{
	return readl_relaxed(g_dev->regs + offset);
}

static void tsadc_writel(u32 val, u32 offset)
{
	writel_relaxed(val, g_dev->regs + offset);
}

void rockchip_tsadc_auto_ht_work(struct work_struct *work)
{
        int ret,val;

//	printk("%s,line=%d\n", __func__,__LINE__);

	mutex_lock(&tsadc_mutex);

	val = tsadc_readl(TSADC_INT_PD);
	tsadc_writel(val &(~ (1 <<8) ), TSADC_INT_PD);
	ret = tsadc_readl(TSADC_INT_PD);
	tsadc_writel(ret | 0xff, TSADC_INT_PD);       //clr irq status
	if ((val & 0x0f) != 0){
		printk("rockchip tsadc is over temp . %s,line=%d\n", __func__,__LINE__);
		pm_power_off();					//power_off
	}
	mutex_unlock(&tsadc_mutex);
}

static irqreturn_t rockchip_tsadc_auto_ht_interrupt(int irq, void *data)
{
	struct rockchip_tsadc_temp *dev = data;

	printk("%s,line=%d\n", __func__,__LINE__);
	
	queue_work(dev->workqueue, &dev->auto_ht_irq_work);
	
	return IRQ_HANDLED;
}

static void rockchip_tsadc_set_cmpn_int_vale( int chn, int temp)
{
	u32 code = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(table) - 1; i++) {
		if (temp <= table[i].temp && temp > table[i -1].temp) {
			code = table[i].code;
		}
	}
	tsadc_writel((code & TSADC_COMP_INT_DATA_MASK), (TSADC_COMP0_INT + chn*4));

}

static void rockchip_tsadc_set_cmpn_shut_vale( int chn, int temp)
{
	u32 code=0;
	int i;

	for (i = 0; i < ARRAY_SIZE(table) - 1; i++) {
		if (temp <= table[i].temp && temp > table[i -1].temp) {
			code = table[i].code;
		}
	}

	tsadc_writel((code & TSADC_COMP_SHUT_DATA_MASK), (TSADC_COMP0_SHUT + chn*4));
}

static void rockchip_tsadc_set_auto_int_en( int chn, int ht_int_en,int tshut_en)
{
	u32 ret;
	tsadc_writel(0, TSADC_INT_EN);
	if (ht_int_en){
		ret = tsadc_readl(TSADC_INT_EN);
		tsadc_writel( ret | (1 << chn), TSADC_INT_EN);
	}
	if (tshut_en){
		ret = tsadc_readl(TSADC_INT_EN);
		if (tsadc_ht_pull_gpio)
			tsadc_writel(ret | (0xf << (chn + 4)), TSADC_INT_EN);
		else if (tsadc_ht_reset_cru)
			tsadc_writel(ret | (0xf << (chn + 8)), TSADC_INT_EN);
	}	

}
static void rockchip_tsadc_auto_mode_set(int chn, int int_temp,
	int shut_temp, int int_en, int shut_en)
{
	u32 ret;
	
	if (!g_dev || chn > 4)
		return;
	
	mutex_lock(&tsadc_mutex);
	
	clk_enable(g_dev->pclk);
	clk_enable(g_dev->clk);
	
	msleep(10);
	tsadc_writel(0, TSADC_AUTO_CON);
	tsadc_writel(1 << (4+chn), TSADC_AUTO_CON);
	msleep(10);
	if ((tsadc_readl(TSADC_AUTO_CON) & TSADC_AUTO_STAS_BUSY_MASK) != TSADC_AUTO_STAS_BUSY) {
		rockchip_tsadc_set_cmpn_int_vale(chn,int_temp);
		rockchip_tsadc_set_cmpn_shut_vale(chn,shut_temp),

		tsadc_writel(TSADC_AUTO_PERIOD_TIME, TSADC_AUTO_PERIOD);
		tsadc_writel(TSADC_AUTO_PERIOD_HT_TIME, TSADC_AUTO_PERIOD_HT);

		tsadc_writel(TSADC_HIGHT_INT_DEBOUNCE_TIME,
			TSADC_HIGHT_INT_DEBOUNCE);
		tsadc_writel(TSADC_HIGHT_TSHUT_DEBOUNCE_TIME,
			TSADC_HIGHT_TSHUT_DEBOUNCE);
		
		rockchip_tsadc_set_auto_int_en(chn,int_en,shut_en);	
	}

	msleep(10);

	ret = tsadc_readl(TSADC_AUTO_CON);
	tsadc_writel(ret | (1 <<0) , TSADC_AUTO_CON);
	
	mutex_unlock(&tsadc_mutex);
		
}

int rockchip_tsadc_set_auto_temp(int chn)
{
	rockchip_tsadc_auto_mode_set(chn, TSADC_COMP_INT_DATA,
		tsadc_ht_temp, TSADC_TEMP_INT_EN, TSADC_TEMP_SHUT_EN);
	return 0;
}
EXPORT_SYMBOL(rockchip_tsadc_set_auto_temp);

static void rockchip_tsadc_get(int chn, int *temp, int *code)
{
	int i;
	*temp = 0;
	*code = 0;

	if (!g_dev || chn > 4){
		*temp = 150;
		return ;
	}
#if 0
	mutex_lock(&tsadc_mutex);

	clk_enable(g_dev->pclk);
	clk_enable(g_dev->clk);

	msleep(10);
	tsadc_writel(0, TSADC_USER_CON);
	tsadc_writel(TSADC_CTRL_POWER_UP | TSADC_CTRL_CH(chn), TSADC_USER_CON);
	msleep(20);
	if ((tsadc_readl(TSADC_USER_CON) & TSADC_STAS_BUSY_MASK) != TSADC_STAS_BUSY) {
		*code = tsadc_readl((TSADC_DATA0 + chn*4)) & TSADC_DATA_MASK;
		for (i = 0; i < ARRAY_SIZE(table) - 1; i++) {
			if ((*code) <= table[i].code && (*code) > table[i + 1].code) {
				*temp = table[i].temp + (table[i + 1].temp - table[i].temp) * (table[i].code - (*code)) / (table[i].code - table[i + 1].code);
			}
		}
	}
	
	tsadc_writel(0, TSADC_USER_CON);

	clk_disable(g_dev->clk);
	clk_disable(g_dev->pclk);

	mutex_unlock(&tsadc_mutex);
#else
	*code = tsadc_readl((TSADC_DATA0 + chn*4)) & TSADC_DATA_MASK;
	for (i = 0; i < ARRAY_SIZE(table) - 1; i++) {
		if ((*code) <= table[i].code && (*code) > table[i + 1].code)
			*temp = table[i].temp + (table[i + 1].temp
			- table[i].temp) * (table[i].code - (*code))
			/ (table[i].code - table[i + 1].code);
	}
#endif
}

 int rockchip_tsadc_get_temp(int chn)
{
	int temp, code;
	
	rockchip_tsadc_get(chn, &temp, &code);

	return temp;
}
EXPORT_SYMBOL(rockchip_tsadc_get_temp);

static ssize_t rockchip_show_name(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "rockchip-tsadc\n");
}

static ssize_t rockchip_show_label(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	char *label;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int index = attr->index;

	switch (index) {
	case 0:
		label = "tsadc0";
		break;
	case 1:
		label = "tsadc1";
		break;
	case 2:
		label = "tsadc2";
		break;
	case 3:
		label = "tsadc3";
		break;
	default:
		return -EINVAL;
	}

	return sprintf(buf, "%s\n", label);
}

int rockchip_hwmon_init(struct rockchip_temp *data)
{
	struct rockchip_tsadc_temp *rockchip_tsadc_data;
	struct resource *res;
	struct device_node *np = data->pdev->dev.of_node;
	int ret,irq;
	u32 rate;
	struct tsadc_port *uap;
	
	rockchip_tsadc_data = devm_kzalloc(&data->pdev->dev, sizeof(*rockchip_tsadc_data),
		GFP_KERNEL);
	if (!rockchip_tsadc_data)
		return -ENOMEM;

	res = platform_get_resource(data->pdev, IORESOURCE_MEM, 0);
	rockchip_tsadc_data->regs = devm_request_and_ioremap(&data->pdev->dev, res);
	if (!rockchip_tsadc_data->regs) {
		dev_err(&data->pdev->dev, "cannot map IO\n");
		return -ENXIO;
	} 

	//irq request	
	irq = platform_get_irq(data->pdev, 0);
	if (irq < 0) {
		dev_err(&data->pdev->dev, "no irq resource?\n");
		return -EPERM;
	}
	rockchip_tsadc_data->irq = irq;
	ret = request_threaded_irq(rockchip_tsadc_data->irq, NULL, rockchip_tsadc_auto_ht_interrupt, IRQF_ONESHOT, TSADC_AUTO_EVENT_NAME, rockchip_tsadc_data);
	if (ret < 0) {
		dev_err(&data->pdev->dev, "failed to attach tsadc irq\n");
		return -EPERM;
	}	

	rockchip_tsadc_data->workqueue = create_singlethread_workqueue("rockchip_tsadc");
	INIT_WORK(&rockchip_tsadc_data->auto_ht_irq_work, rockchip_tsadc_auto_ht_work);
	
	//clk enable
	rockchip_tsadc_data->clk = devm_clk_get(&data->pdev->dev, "tsadc");
	if (IS_ERR(rockchip_tsadc_data->clk)) {
	    dev_err(&data->pdev->dev, "failed to get tsadc clock\n");
	    ret = PTR_ERR(rockchip_tsadc_data->clk);
	    return -EPERM;
	}

	if(of_property_read_u32(np, "clock-frequency", &rate)) {
          dev_err(&data->pdev->dev, "Missing clock-frequency property in the DT.\n");
	  return -EPERM;
	}

	ret = clk_set_rate(rockchip_tsadc_data->clk, rate);
	    if(ret < 0) {
	    dev_err(&data->pdev->dev, "failed to set adc clk\n");
	    return -EPERM;
	}
	clk_prepare_enable(rockchip_tsadc_data->clk);

	rockchip_tsadc_data->pclk = devm_clk_get(&data->pdev->dev, "pclk_tsadc");
	if (IS_ERR(rockchip_tsadc_data->pclk)) {
	    dev_err(&data->pdev->dev, "failed to get tsadc pclk\n");
	    ret = PTR_ERR(rockchip_tsadc_data->pclk);
	    return -EPERM;
	}
	clk_prepare_enable(rockchip_tsadc_data->pclk);

	platform_set_drvdata(data->pdev, rockchip_tsadc_data);
	g_dev = rockchip_tsadc_data;
	data->plat_data = rockchip_tsadc_data;

	ret = tsadc_readl(TSADC_AUTO_CON);
	tsadc_writel(ret | (1 << 8) , TSADC_AUTO_CON);/*gpio0_b2 = 0 shutdown*/

	if (of_property_read_u32(np, "tsadc-ht-temp",
		&tsadc_ht_temp)) {
		dev_err(&data->pdev->dev, "Missing  tsadc_ht_temp in the DT.\n");
		return -EPERM;
	}
	if (of_property_read_u32(np, "tsadc-ht-reset-cru",
		&tsadc_ht_reset_cru)) {
		dev_err(&data->pdev->dev, "Missing tsadc_ht_reset_cru in the DT.\n");
		return -EPERM;
	}
	if (of_property_read_u32(np, "tsadc-ht-pull-gpio",
		&tsadc_ht_pull_gpio)) {
		dev_err(&data->pdev->dev, "Missing tsadc_ht_pull_gpio in the DT.\n");
		return -EPERM;
	}

	uap = devm_kzalloc(&data->pdev->dev, sizeof(struct tsadc_port),
			   GFP_KERNEL);
	if (uap == NULL)
		dev_err(&data->pdev->dev,
		"uap is not set %s,line=%d\n", __func__, __LINE__);
	uap->pctl = devm_pinctrl_get(&data->pdev->dev);
	uap->pins_default = pinctrl_lookup_state(uap->pctl, "default");
	uap->pins_tsadc_int = pinctrl_lookup_state(uap->pctl, "tsadc_int");
	pinctrl_select_state(uap->pctl, uap->pins_tsadc_int);

	rockchip_tsadc_set_auto_temp(1);

	data->monitored_sensors = NUM_MONITORED_SENSORS;
	data->ops.read_sensor = rockchip_tsadc_get_temp;
	data->ops.show_name = rockchip_show_name;
	data->ops.show_label = rockchip_show_label;
	data->ops.is_visible = NULL;

	dev_info(&data->pdev->dev, "initialized\n");
	return 0;
}
EXPORT_SYMBOL(rockchip_hwmon_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("Driver for TSADC");
