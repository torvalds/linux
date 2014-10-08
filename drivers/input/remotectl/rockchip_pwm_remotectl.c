#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include "rockchip_pwm_remotectl.h"



/*sys/module/rk_pwm_remotectl/parameters,
modify code_print to change the value*/

static int rk_remote_print_code;
module_param_named(code_print, rk_remote_print_code, int, 0644);
#define DBG_CODE(args...) \
	do { \
		if (rk_remote_print_code) { \
			pr_info(args); \
		} \
	} while (0)

static int rk_remote_pwm_dbg_level;
module_param_named(dbg_level, rk_remote_pwm_dbg_level, int, 0644);
#define DBG(args...) \
	do { \
		if (rk_remote_pwm_dbg_level) { \
			pr_info(args); \
		} \
	} while (0)


struct rkxx_remote_key_table {
	int scancode;
	int keycode;
};

struct rkxx_remotectl_button {
	int usercode;
	int nbuttons;
	struct rkxx_remote_key_table key_table[MAX_NUM_KEYS];
};

struct rkxx_remotectl_drvdata {
	void __iomem *base;
	int state;
	int nbuttons;
	int result;
	int scandata;
	int count;
	int keynum;
	int maxkeybdnum;
	int keycode;
	int press;
	int pre_press;
	int period;
	int irq;
	int remote_pwm_id;
	int handle_cpu_id;
	int wakeup;
	struct input_dev *input;
	struct timer_list timer;
	struct tasklet_struct remote_tasklet;
	struct wake_lock remotectl_wake_lock;
};

static struct rkxx_remotectl_button *remotectl_button; 


static int remotectl_keybd_num_lookup(struct rkxx_remotectl_drvdata *ddata)
{
	int i;
	int num;

	num = ddata->maxkeybdnum;
	for (i = 0; i < num; i++) {
		if (remotectl_button[i].usercode == (ddata->scandata&0xFFFF)) {
			ddata->keynum = i;
			return 1;
		}
	}
	return 0;
}


static int remotectl_keycode_lookup(struct rkxx_remotectl_drvdata *ddata)
{
	int i;
	unsigned char keydata = (unsigned char)((ddata->scandata >> 8) & 0xff);

	for (i = 0; i < remotectl_button[ddata->keynum].nbuttons; i++) {
		if (remotectl_button[ddata->keynum].key_table[i].scancode ==
		    keydata) {
			ddata->keycode =
			remotectl_button[ddata->keynum].key_table[i].keycode;
			return 1;
		}
	}
	return 0;
}


static int rk_remotectl_parse_ir_keys(struct platform_device *pdev)
{
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct device_node *child_node;
	u32 loop;
	u32 ret;
	u32 len;
	u32 boardnum;
	
	boardnum = 0;
	for_each_child_of_node(node, child_node) {
		if(of_property_read_u32(child_node, "rockchip,usercode",
			 &remotectl_button[boardnum].usercode)) {
			dev_err(&pdev->dev, "Missing usercode property in the DTS.\n");
			ret = -1;
			return ret;
		}
		DBG("remotectl_button[0].usercode=0x%x\n",
				remotectl_button[boardnum].usercode);
		of_get_property(child_node, "rockchip,key_table", &len);
		len /= sizeof(u32);
		DBG("len=0x%x\n",len);
		remotectl_button[boardnum].nbuttons = len/2;
		if(of_property_read_u32_array(child_node, "rockchip,key_table", 
			 (u32 *)remotectl_button[boardnum].key_table, len)) {
			dev_err(&pdev->dev, "Missing key_table property in the DTS.\n");
			ret = -1;
			return ret;
		}
		for (loop=0; loop<(len/2); loop++) {
			DBG("board[%d].scanCode[%d]=0x%x\n", boardnum, loop, 
					remotectl_button[boardnum].key_table[loop].scancode);
			DBG("board[%d].keyCode[%d]=%d\n", boardnum, loop, 
					remotectl_button[boardnum].key_table[loop].keycode);
		}
		boardnum++;
		if (boardnum > ddata->maxkeybdnum)
			break;
	}
	DBG("keybdNum=0x%x\n",boardnum);
	return 0;
}



static void rk_pwm_remotectl_do_something(unsigned long  data)
{
	struct rkxx_remotectl_drvdata *ddata;

	ddata = (struct rkxx_remotectl_drvdata *)data;
	switch (ddata->state) {
	case RMC_IDLE: {
		;
		break;
	}
	case RMC_PRELOAD: {
		mod_timer(&ddata->timer, jiffies + msecs_to_jiffies(130));
		if ((RK_PWM_TIME_PRE_MIN < ddata->period) &&
		    (ddata->period < RK_PWM_TIME_PRE_MAX)) {
			ddata->scandata = 0;
			ddata->count = 0;
			ddata->state = RMC_USERCODE;
		} else {
			ddata->state = RMC_PRELOAD;
		}
		break;
	}
	case RMC_USERCODE: {
		if ((RK_PWM_TIME_BIT1_MIN < ddata->period) &&
		    (ddata->period < RK_PWM_TIME_BIT1_MAX))
			ddata->scandata |= (0x01 << ddata->count);
		ddata->count++;
		if (ddata->count == 0x10) {
			DBG_CODE("USERCODE=0x%x\n", ddata->scandata);
			if (remotectl_keybd_num_lookup(ddata)) {
				ddata->state = RMC_GETDATA;
				ddata->scandata = 0;
				ddata->count = 0;
			} else {
				ddata->state = RMC_PRELOAD;
			}
		}
	}
	break;
	case RMC_GETDATA: {
		if ((RK_PWM_TIME_BIT1_MIN < ddata->period) &&
		    (ddata->period < RK_PWM_TIME_BIT1_MAX))
			ddata->scandata |= (0x01<<ddata->count);
		ddata->count++;
		if (ddata->count < 0x10)
			return;
		DBG_CODE("RMC_GETDATA=%x\n", (ddata->scandata>>8));
		if ((ddata->scandata&0x0ff) ==
		    ((~ddata->scandata >> 8) & 0x0ff)) {
			if (remotectl_keycode_lookup(ddata)) {
				ddata->press = 1;
				input_event(ddata->input, EV_KEY,
					    ddata->keycode, 1);
				input_sync(ddata->input);
				ddata->state = RMC_SEQUENCE;
			} else {
				ddata->state = RMC_PRELOAD;
			}
		} else {
			ddata->state = RMC_PRELOAD;
		}
	}
	break;
	case RMC_SEQUENCE:{
		DBG("S=%d\n", ddata->period);
		if ((RK_PWM_TIME_RPT_MIN < ddata->period) &&
		    (ddata->period < RK_PWM_TIME_RPT_MAX)) {
			DBG("S1\n");
			mod_timer(&ddata->timer, jiffies
				  + msecs_to_jiffies(130));
		} else if ((RK_PWM_TIME_SEQ1_MIN < ddata->period) &&
			   (ddata->period < RK_PWM_TIME_SEQ1_MAX)) {
			DBG("S2\n");
			mod_timer(&ddata->timer, jiffies
				  + msecs_to_jiffies(130));
		} else if ((RK_PWM_TIME_SEQ2_MIN < ddata->period) &&
			  (ddata->period < RK_PWM_TIME_SEQ2_MAX)) {
			DBG("S3\n");
			mod_timer(&ddata->timer, jiffies
				  + msecs_to_jiffies(130));
		} else {
			DBG("S4\n");
			input_event(ddata->input, EV_KEY,
				    ddata->keycode, 0);
			input_sync(ddata->input);
			ddata->state = RMC_PRELOAD;
			ddata->press = 0;
		}
	}
	break;
	default:
	break;
	}
}

static void rk_pwm_remotectl_timer(unsigned long _data)
{
	struct rkxx_remotectl_drvdata *ddata;

	ddata =  (struct rkxx_remotectl_drvdata *)_data;
	if (ddata->press != ddata->pre_press) {
		ddata->pre_press = 0;
		ddata->press = 0;
		input_event(ddata->input, EV_KEY, ddata->keycode, 0);
		input_sync(ddata->input);
	}
	ddata->state = RMC_PRELOAD;
}


static irqreturn_t rockchip_pwm_irq(int irq, void *dev_id)
{
	struct rkxx_remotectl_drvdata *ddata;
	int val;

	ddata = (struct rkxx_remotectl_drvdata *)dev_id;
	switch (ddata->remote_pwm_id) {
	case 0: {
		val = readl_relaxed(ddata->base + PWM0_REG_INTSTS);
		if (val & PWM_CH0_INT) {
			if ((val & PWM_CH0_POL) == 0) {
				val = readl_relaxed(ddata->base + PWM_REG_HPR);
				ddata->period = val;
				tasklet_hi_schedule(&ddata->remote_tasklet);
				DBG("hpr=0x%x\n", val);
			} else {
				val = readl_relaxed(ddata->base + PWM_REG_LPR);
				DBG("lpr=0x%x\n", val);
			}
			writel_relaxed(PWM_CH0_INT, ddata->base + PWM0_REG_INTSTS);
			if (ddata->state == RMC_PRELOAD)
				wake_lock_timeout(&ddata->remotectl_wake_lock, HZ);
			return IRQ_HANDLED;
		}
	}
	break;
	case 1:	{
		val = readl_relaxed(ddata->base + PWM1_REG_INTSTS);
		if (val & PWM_CH1_INT) {
			if ((val & PWM_CH1_POL) == 0) {
				val = readl_relaxed(ddata->base + PWM_REG_HPR);
				ddata->period = val;
				tasklet_hi_schedule(&ddata->remote_tasklet);
				DBG("hpr=0x%x\n", val);
			} else {
				val = readl_relaxed(ddata->base + PWM_REG_LPR);
				DBG("lpr=0x%x\n", val);
			}
			writel_relaxed(PWM_CH1_INT, ddata->base + PWM1_REG_INTSTS);
			if (ddata->state == RMC_PRELOAD)
				wake_lock_timeout(&ddata->remotectl_wake_lock, HZ);
			return IRQ_HANDLED;
		}
	}
	break;
	case 2:	{
		val = readl_relaxed(ddata->base + PWM2_REG_INTSTS);
		if (val & PWM_CH2_INT) {
			if ((val & PWM_CH2_POL) == 0) {
				val = readl_relaxed(ddata->base + PWM_REG_HPR);
				ddata->period = val;
				tasklet_hi_schedule(&ddata->remote_tasklet);
				DBG("hpr=0x%x\n", val);
			} else {
				val = readl_relaxed(ddata->base + PWM_REG_LPR);
				DBG("lpr=0x%x\n", val);
			}
			writel_relaxed(PWM_CH2_INT, ddata->base + PWM2_REG_INTSTS);
			if (ddata->state == RMC_PRELOAD)
				wake_lock_timeout(&ddata->remotectl_wake_lock, HZ);
			return IRQ_HANDLED;
		}
	}
	break;
	case 3:	{
		val = readl_relaxed(ddata->base + PWM3_REG_INTSTS);
		if (val & PWM_CH3_INT) {
			if ((val & PWM_CH3_POL) == 0) {
				val = readl_relaxed(ddata->base + PWM_REG_HPR);
				ddata->period = val;
				tasklet_hi_schedule(&ddata->remote_tasklet);
				DBG("hpr=0x%x\n", val);
			} else {
				val = readl_relaxed(ddata->base + PWM_REG_LPR);
				DBG("lpr=0x%x\n", val);
			}
			writel_relaxed(PWM_CH3_INT, ddata->base + PWM3_REG_INTSTS);
			if (ddata->state == RMC_PRELOAD)
				wake_lock_timeout(&ddata->remotectl_wake_lock, HZ);
			return IRQ_HANDLED;
		}
	}
	break;
	default:	
	break;
	}		
	return IRQ_NONE;
}

static int rk_pwm_remotectl_hw_init(struct rkxx_remotectl_drvdata *ddata)
{
	int val;

	val = readl_relaxed(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_DISABLE;
	writel_relaxed(val, ddata->base + PWM_REG_CTRL);
	val = readl_relaxed(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFF9) | PWM_MODE_CAPTURE;
	writel_relaxed(val, ddata->base + PWM_REG_CTRL);
	val = readl_relaxed(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFF008DFF) | 0x00646200;
	writel_relaxed(val, ddata->base + PWM_REG_CTRL);
	switch (ddata->remote_pwm_id) {
	case 0: {
		val = readl_relaxed(ddata->base + PWM0_REG_INT_EN);
		val = (val & 0xFFFFFFFE) | PWM_CH0_INT_ENABLE;
		writel_relaxed(val, ddata->base + PWM0_REG_INT_EN);
	}
	break;
	case 1:	{
		val = readl_relaxed(ddata->base + PWM1_REG_INT_EN);
		val = (val & 0xFFFFFFFD) | PWM_CH1_INT_ENABLE;
		writel_relaxed(val, ddata->base + PWM1_REG_INT_EN);
	}
	break;
	case 2:	{
		val = readl_relaxed(ddata->base + PWM2_REG_INT_EN);
		val = (val & 0xFFFFFFFB) | PWM_CH2_INT_ENABLE;
		writel_relaxed(val, ddata->base + PWM2_REG_INT_EN);
	}
	break;
	case 3:	{
		val = readl_relaxed(ddata->base + PWM3_REG_INT_EN);
		val = (val & 0xFFFFFFF7) | PWM_CH3_INT_ENABLE;
		writel_relaxed(val, ddata->base + PWM3_REG_INT_EN);
	}
	break;
	default:	
	break;
	}		
	val = readl_relaxed(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_ENABLE;
	writel_relaxed(val, ddata->base + PWM_REG_CTRL);
	return 0;
}



static int rk_pwm_probe(struct platform_device *pdev)
{
	struct rkxx_remotectl_drvdata *ddata;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child_node;
	struct resource *r;
	struct input_dev *input;
	struct clk *clk;
	struct cpumask cpumask;
	int num;
	int irq;
	int ret;
	int i, j;
	int cpu_id;
	int pwm_id;
	
	pr_err(".. rk pwm remotectl v1.1 init\n");
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no memory resources defined\n");
		return -ENODEV;
	}
	ddata = devm_kzalloc(&pdev->dev, sizeof(struct rkxx_remotectl_drvdata),
			     GFP_KERNEL);
	if (!ddata) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	ddata->state = RMC_PRELOAD;
	ddata->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(ddata->base))
		return PTR_ERR(ddata->base);
	clk = devm_clk_get(&pdev->dev, "pclk_pwm");
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	platform_set_drvdata(pdev, ddata);
	num = 0;
	for_each_child_of_node(np, child_node){
		if(of_device_is_compatible(child_node, "rockchip,ir_key"))
			num++;
	}
	if (num == 0) {
		pr_err("remotectl: no ir keyboard add in dts!!\n");
		return -1;
	}
	ddata->maxkeybdnum = num;
	remotectl_button = kmalloc(
					num*sizeof(struct rkxx_remotectl_button),
					GFP_KERNEL);
	if (!remotectl_button) {
		pr_err("failed to malloc remote button memory\n");
		return -ENOMEM;
	}
	input = input_allocate_device();
	input->name = pdev->name;
	input->phys = "gpio-keys/remotectl";
	input->dev.parent = &pdev->dev;
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;
	ddata->input = input;
	ddata->input = input;
	wake_lock_init(&ddata->remotectl_wake_lock,
		       WAKE_LOCK_SUSPEND, "rk29_pwm_remote");
	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;
	irq = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		return ret;
	}
	ddata->irq = irq;
	ddata->wakeup = 1;
	of_property_read_u32(np, "remote_pwm_id", &pwm_id);
	ddata->remote_pwm_id = pwm_id;
	DBG("remotectl: remote pwm id=0x%x\n", pwm_id);
	of_property_read_u32(np, "handle_cpu_id", &cpu_id);
	ddata->handle_cpu_id = cpu_id;
	DBG("remotectl: handle cpu id=0x%x\n", cpu_id);
	rk_remotectl_parse_ir_keys(pdev);
	tasklet_init(&ddata->remote_tasklet, rk_pwm_remotectl_do_something,
		     (unsigned long)ddata);
	for (j = 0; j < num; j++) {
		DBG("remotectl probe j = 0x%x\n", j);
		for (i = 0; i < remotectl_button[j].nbuttons; i++) {
			unsigned int type = EV_KEY;

			input_set_capability(input, type, remotectl_button[j].
					     key_table[i].keycode);
		}
	}
	ret = input_register_device(input);
	if (ret)
		pr_err("remotectl: register input device err, ret: %d\n", ret);
	input_set_capability(input, EV_KEY, KEY_WAKEUP);
	device_init_wakeup(&pdev->dev, 1);
	ret = devm_request_irq(&pdev->dev, irq, rockchip_pwm_irq,
			       0, "rk_pwm_irq", ddata);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", irq);
		return ret;
	}
	enable_irq_wake(irq);
	setup_timer(&ddata->timer, rk_pwm_remotectl_timer,
		    (unsigned long)ddata);
	mod_timer(&ddata->timer, jiffies + msecs_to_jiffies(1000));
	cpumask_clear(&cpumask);
	cpumask_set_cpu(cpu_id, &cpumask); 
	irq_set_affinity(irq, &cpumask); 
	rk_pwm_remotectl_hw_init(ddata);
	return ret;
}

static int rk_pwm_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_PM
static int remotectl_suspend(struct device *dev)
{
	int cpu = 0;
	struct cpumask cpumask;
	struct platform_device *pdev = to_platform_device(dev);
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
	
	cpumask_clear(&cpumask);
	cpumask_set_cpu(cpu, &cpumask);
	irq_set_affinity(ddata->irq, &cpumask);
	return 0;
}


static int remotectl_resume(struct device *dev)
{
	struct cpumask cpumask;
	struct platform_device *pdev = to_platform_device(dev);
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
	
	cpumask_clear(&cpumask);
	cpumask_set_cpu(ddata->handle_cpu_id, &cpumask); 
	irq_set_affinity(ddata->irq, &cpumask); 
	return 0;
}

static const struct dev_pm_ops remotectl_pm_ops = {
	.suspend = remotectl_suspend,
	.resume	= remotectl_resume,
};
#endif

static const struct of_device_id rk_pwm_of_match[] = {
	{ .compatible =  "rockchip,remotectl-pwm"},
	{ }
};

MODULE_DEVICE_TABLE(of, rk_pwm_of_match);

static struct platform_driver rk_pwm_driver = {
	.driver = {
		.name = "remotectl-pwm",
		.of_match_table = rk_pwm_of_match,
#ifdef CONFIG_PM
        .pm	= &remotectl_pm_ops,
#endif	
	},
	.probe = rk_pwm_probe,
	.remove = rk_pwm_remove,
};

module_platform_driver(rk_pwm_driver);

MODULE_LICENSE("GPL");
