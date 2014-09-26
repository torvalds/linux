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
	struct rkxx_remote_key_table *key_table;
};

struct rkxx_remotectl_drvdata {
	void __iomem *base;
	int state;
	int nbuttons;
	int result;
	int scandata;
	int count;
	int keynum;
	int keycode;
	int press;
	int pre_press;
	int period;
	int irq;
	int wakeup;
	struct input_dev *input;
	struct timer_list timer;
	struct tasklet_struct remote_tasklet;
	struct wake_lock remotectl_wake_lock;
};


static struct rkxx_remote_key_table remote_key_table_meiyu_4040[] = {
	{0xf2, KEY_REPLY},
	{0xba, KEY_BACK},
	{0xf4, KEY_UP},
	{0xf1, KEY_DOWN},
	{0xef, KEY_LEFT},
	{0xee, KEY_RIGHT},
	{0xbd, KEY_HOME},
	{0xea, KEY_VOLUMEUP},
	{0xe3, KEY_VOLUMEDOWN},
	{0xe2, KEY_SEARCH},
	{0xb2, KEY_POWER},
	{0xbc, KEY_MUTE},
	{0xec, KEY_MENU},
/*lay pause*/
	{0xbf, 0x190},
/*pre*/
	{0xe0, 0x191},
/*next*/
	{0xe1, 0x192},
/*pic,rorate left*/
	{0xe9, 183},
/*rorate right*/
	{0xe6, 248},
/*zoom out*/
	{0xe8, 185},
/*zoom in*/
	{0xe7, 186},
/*mouse switch*/
	{0xb8, 388},
/*zoom outdisplay switch*/
	{0xbe, 0x175},
};


static struct rkxx_remote_key_table remote_key_table_sunchip_ff00[] = {
	{0xf9, KEY_HOME},
	{0xbf, KEY_BACK},
	{0xfb, KEY_MENU},
	{0xaa, KEY_REPLY},
	{0xb9, KEY_UP},
	{0xe9, KEY_DOWN},
	{0xb8, KEY_LEFT},
	{0xea, KEY_RIGHT},
	{0xeb, KEY_VOLUMEDOWN},
	{0xef, KEY_VOLUMEUP},
	{0xf7, KEY_MUTE},
	{0xe7, KEY_POWER},
	{0xfc, KEY_POWER},
	{0xa9, KEY_VOLUMEDOWN},
	{0xa8, KEY_VOLUMEDOWN},
	{0xe0, KEY_VOLUMEDOWN},
	{0xa5, KEY_VOLUMEDOWN},
	{0xab, 183},
	{0xb7, 388},
	{0xf8, 184},
	{0xaf, 185},
	{0xed, KEY_VOLUMEDOWN},
	{0xee, 186},
	{0xb3, KEY_VOLUMEDOWN},
	{0xf1, KEY_VOLUMEDOWN},
	{0xf2, KEY_VOLUMEDOWN},
	{0xf3, KEY_SEARCH},
	{0xb4, KEY_VOLUMEDOWN},
	{0xbe, KEY_SEARCH},
};



static struct rkxx_remote_key_table remote_key_table_0x1dcc[] = {
	{0xee, KEY_REPLY},
	{0xf0, KEY_BACK},
	{0xf8, KEY_UP},
	{0xbb, KEY_DOWN},
	{0xef, KEY_LEFT},
	{0xed, KEY_RIGHT},
	{0xfc, KEY_HOME},
	{0xf1, KEY_VOLUMEUP},
	{0xfd, KEY_VOLUMEDOWN},
	{0xb7, KEY_SEARCH},
	{0xff, KEY_POWER},
	{0xf3, KEY_MUTE},
	{0xbf, KEY_MENU},
	{0xf9, 0x191},
	{0xf5, 0x192},
	{0xb3, 388},
	{0xbe, KEY_1},
	{0xba, KEY_2},
	{0xb2, KEY_3},
	{0xbd, KEY_4},
	{0xf9, KEY_5},
	{0xb1, KEY_6},
	{0xfc, KEY_7},
	{0xf8, KEY_8},
	{0xb0, KEY_9},
	{0xb6, KEY_0},
	{0xb5, KEY_BACKSPACE},
};


static struct rkxx_remotectl_button remotectl_button[] = {
	{
		.usercode = 0xff00,
		.nbuttons =  29,
		.key_table = &remote_key_table_sunchip_ff00[0],
	},
	{
		.usercode = 0x4040,
		.nbuttons =  22,
		.key_table = &remote_key_table_meiyu_4040[0],
	},
	{
		.usercode = 0x1dcc,
		.nbuttons =  27,
		.key_table = &remote_key_table_0x1dcc[0],
	},
};


static int remotectl_keybd_num_lookup(struct rkxx_remotectl_drvdata *ddata)
{
	int i;
	int num;

	num =  sizeof(remotectl_button)/sizeof(struct rkxx_remotectl_button);
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
	val = readl_relaxed(ddata->base + PWM_REG_INTSTS);
	if (val&PWM_CH3_INT) {
		if ((val & PWM_CH3_POL) == 0) {
			val = readl_relaxed(ddata->base + PWM_REG_HPR);
			ddata->period = val;
			tasklet_hi_schedule(&ddata->remote_tasklet);
			DBG("hpr=0x%x\n", val);
		} else {
			val = readl_relaxed(ddata->base + PWM_REG_LPR);
			DBG("lpr=0x%x\n", val);
		}
		writel_relaxed(PWM_CH3_INT, ddata->base + PWM_REG_INTSTS);
		if (ddata->state == RMC_PRELOAD)
			wake_lock_timeout(&ddata->remotectl_wake_lock, HZ);
		return IRQ_HANDLED;
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
	val = readl_relaxed(ddata->base + PWM_REG_INT_EN);
	val = (val & 0xFFFFFFF7) | PWM_CH3_INT_ENABLE;
	writel_relaxed(val, ddata->base + PWM_REG_INT_EN);
	val = readl_relaxed(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_ENABLE;
	writel_relaxed(val, ddata->base + PWM_REG_CTRL);
	return 0;
}



static int rk_pwm_probe(struct platform_device *pdev)
{
	struct rkxx_remotectl_drvdata *ddata;
	struct resource *r;
	struct input_dev *input;
	struct clk *clk;
	int num;
	int irq;
	int ret;
	int i, j;

	DBG(".. rk pwm remotectl v1.1 init\n");
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
	tasklet_init(&ddata->remote_tasklet, rk_pwm_remotectl_do_something,
		     (unsigned long)ddata);
	num =  sizeof(remotectl_button)/sizeof(struct rkxx_remotectl_button);
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
	rk_pwm_remotectl_hw_init(ddata);
	return ret;
}

static int rk_pwm_remove(struct platform_device *pdev)
{
	return 0;
}


static const struct of_device_id rk_pwm_of_match[] = {
	{ .compatible =  "rockchip,remotectl-pwm"},
	{ }
};

MODULE_DEVICE_TABLE(of, rk_pwm_of_match);

static struct platform_driver rk_pwm_driver = {
	.driver = {
		.name = "remotectl-pwm",
		.of_match_table = rk_pwm_of_match,
	},
	.probe = rk_pwm_probe,
	.remove = rk_pwm_remove,
};

module_platform_driver(rk_pwm_driver);

MODULE_LICENSE("GPL");
