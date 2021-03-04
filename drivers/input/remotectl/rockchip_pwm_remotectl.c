/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
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
#include <linux/rockchip/rockchip_sip.h>
#include "rockchip_pwm_remotectl.h"



/*
 * sys/module/rk_pwm_remotectl/parameters,
 * modify code_print to change the value
 */

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
	int scandata;
	int count;
	int keynum;
	int maxkeybdnum;
	int keycode;
	int press;
	int pre_press;
	int irq;
	int remote_pwm_id;
	int handle_cpu_id;
	int wakeup;
	int support_psci;
	int pwm_pwrkey_capture;
	unsigned long period;
	unsigned long temp_period;
	int pwm_freq_nstime;
	int pwrkey_wakeup;
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

static int rk_remotectl_get_irkeybd_count(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *child_node;
	int boardnum;
	int temp_usercode;

	boardnum = 0;
	for_each_child_of_node(node, child_node) {
		if (of_property_read_u32(child_node, "rockchip,usercode",
			&temp_usercode)) {
			DBG("get keybd num error.\n");
		} else {
			boardnum++;
		}
	}
	DBG("get keybd num = 0x%x.\n", boardnum);
	return boardnum;
}


static int rk_remotectl_parse_ir_keys(struct platform_device *pdev)
{
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct device_node *child_node;
	int loop;
	int ret;
	int len;
	int boardnum;

	boardnum = 0;
	for_each_child_of_node(node, child_node) {
		if (of_property_read_u32(child_node, "rockchip,usercode",
			 &remotectl_button[boardnum].usercode)) {
			dev_err(&pdev->dev, "Missing usercode in the DTS.\n");
			ret = -1;
			return ret;
		}
		DBG("remotectl_button[0].usercode=0x%x\n",
				remotectl_button[boardnum].usercode);
		of_get_property(child_node, "rockchip,key_table", &len);
		len /= sizeof(u32);
		DBG("len=0x%x\n", len);
		remotectl_button[boardnum].nbuttons = len/2;
		if (of_property_read_u32_array(child_node, "rockchip,key_table",
			 (u32 *)remotectl_button[boardnum].key_table, len)) {
			dev_err(&pdev->dev, "Missing key_table in the DTS.\n");
			ret = -1;
			return ret;
		}
		for (loop = 0; loop < (len/2); loop++) {
			DBG("board[%d].scanCode[%d]=0x%x\n", boardnum, loop,
			     remotectl_button[boardnum].key_table[loop].scancode);
			DBG("board[%d].keyCode[%d]=%d\n", boardnum, loop,
			     remotectl_button[boardnum].key_table[loop].keycode);
		}
		boardnum++;
		if (boardnum > ddata->maxkeybdnum)
			break;
	}
	DBG("keybdNum=0x%x\n", boardnum);
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
		mod_timer(&ddata->timer, jiffies + msecs_to_jiffies(140));
		if ((ddata->period > RK_PWM_TIME_PRE_MIN) &&
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
		if ((ddata->period > RK_PWM_TIME_BIT1_MIN) &&
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
				if (rk_remote_print_code) {
					ddata->state = RMC_GETDATA;
					ddata->scandata = 0;
					ddata->count = 0;
				} else
					ddata->state = RMC_PRELOAD;
			}
		}
	}
	break;
	case RMC_GETDATA: {
		if ((ddata->period > RK_PWM_TIME_BIT1_MIN) &&
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
		DBG("S=%ld\n", ddata->period);
		if ((ddata->period > RK_PWM_TIME_RPT_MIN) &&
		    (ddata->period < RK_PWM_TIME_RPT_MAX)) {
			DBG("S1\n");
			mod_timer(&ddata->timer, jiffies
				  + msecs_to_jiffies(130));
		} else if ((ddata->period > RK_PWM_TIME_SEQ1_MIN) &&
			   (ddata->period < RK_PWM_TIME_SEQ1_MAX)) {
			DBG("S2\n");
			mod_timer(&ddata->timer, jiffies
				  + msecs_to_jiffies(130));
		} else if ((ddata->period > RK_PWM_TIME_SEQ2_MIN) &&
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

static void rk_pwm_remotectl_timer(struct timer_list *t)
{
	struct rkxx_remotectl_drvdata *ddata = from_timer(ddata, t, timer);

	if (ddata->press != ddata->pre_press) {
		ddata->pre_press = 0;
		ddata->press = 0;
		input_event(ddata->input, EV_KEY, ddata->keycode, 0);
		input_sync(ddata->input);
	}
	ddata->state = RMC_PRELOAD;
}

static irqreturn_t rockchip_pwm_pwrirq(int irq, void *dev_id)
{
	struct rkxx_remotectl_drvdata *ddata = dev_id;
	int val;
	unsigned int id = ddata->remote_pwm_id;

	if (id > 3)
		return IRQ_NONE;

	val = readl_relaxed(ddata->base + PWM_REG_INTSTS(id));

	if (val & PWM_PWR_KEY_INT) {
		DBG("pwr=0x%x\n", readl_relaxed(ddata->base + PWM_PWRCAPTURE_VALUE(id)));
		writel_relaxed(PWM_PWR_KEY_INT, ddata->base + PWM_REG_INTSTS(id));
		ddata->pwrkey_wakeup = 1;
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t rockchip_pwm_irq(int irq, void *dev_id)
{
	struct rkxx_remotectl_drvdata *ddata = dev_id;
	int val;
	int temp_hpr;
	int temp_lpr;
	int temp_period;
	unsigned int id = ddata->remote_pwm_id;

	if (id > 3)
		return IRQ_NONE;
	val = readl_relaxed(ddata->base + PWM_REG_INTSTS(id));

	if ((val & PWM_CH_INT(id)) == 0)
		return IRQ_NONE;
	if ((val & PWM_CH_POL(id)) == 0) {
		temp_hpr = readl_relaxed(ddata->base + PWM_REG_HPR);
		DBG("hpr=%d\n", temp_hpr);
		temp_lpr = readl_relaxed(ddata->base + PWM_REG_LPR);
		DBG("lpr=%d\n", temp_lpr);
		temp_period = ddata->pwm_freq_nstime * temp_lpr / 1000;
		if (temp_period > RK_PWM_TIME_BIT0_MIN) {
			ddata->period = ddata->temp_period
			    + ddata->pwm_freq_nstime * temp_hpr / 1000;
			tasklet_hi_schedule(&ddata->remote_tasklet);
			ddata->temp_period = 0;
			DBG("period+ =%ld\n", ddata->period);
		} else {
			ddata->temp_period += ddata->pwm_freq_nstime
			    * (temp_hpr + temp_lpr) / 1000;
		}
	}
	writel_relaxed(PWM_CH_INT(id), ddata->base + PWM_REG_INTSTS(id));
	if (ddata->state == RMC_PRELOAD)
		wake_lock_timeout(&ddata->remotectl_wake_lock, HZ);
	return IRQ_HANDLED;
}

static int rk_pwm_pwrkey_wakeup_init(struct platform_device *pdev)
{
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
	int val, min_temp, max_temp;
	unsigned int pwm_id = ddata->remote_pwm_id;
	int version;
	int i, j;
	int num = 0;
	int ret = -1;
	int pwr_irq;

	ddata->pwm_pwrkey_capture = 0;
	version = readl_relaxed(ddata->base + RK_PWM_VERSION_ID(pwm_id));
	dev_info(&pdev->dev, "pwm version is 0x%x\n", version & 0xffff0000);
	if (((version >> 24) & 0xFF) < 2) {
		dev_info(&pdev->dev, "pwm version is less v2.0\n");
		goto end;
	}
	pwr_irq = platform_get_irq(pdev, 1);
	if (pwr_irq < 0) {
		dev_err(&pdev->dev, "cannot find PWR IRQ\n");
		goto end;
	}
	ret = devm_request_irq(&pdev->dev, pwr_irq, rockchip_pwm_pwrirq,
			       IRQF_NO_SUSPEND, "rk_pwm_pwr_irq", ddata);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim PWR_IRQ!!!\n");
		goto end;
	}
	val = readl_relaxed(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_DISABLE;
	writel_relaxed(val, ddata->base + PWM_REG_CTRL);

	//preloader low min:8000us, max:10000us
	min_temp = RK_PWM_TIME_PRE_MIN_LOW * 1000 / ddata->pwm_freq_nstime;
	max_temp = RK_PWM_TIME_PRE_MAX_LOW * 1000 / ddata->pwm_freq_nstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, ddata->base + PWM_REG_PWRMATCH_LPRE(pwm_id));

	//preloader higt min:4000us, max:5000us
	min_temp = RK_PWM_TIME_PRE_MIN * 1000 / ddata->pwm_freq_nstime;
	max_temp = RK_PWM_TIME_PRE_MAX * 1000 / ddata->pwm_freq_nstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, ddata->base + PWM_REG_PWRMATCH_HPRE(pwm_id));

	//logic 0/1 low min:480us, max 700us
	min_temp = RK_PWM_TIME_BIT_MIN_LOW * 1000 / ddata->pwm_freq_nstime;
	max_temp = RK_PWM_TIME_BIT_MAX_LOW * 1000 / ddata->pwm_freq_nstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, ddata->base + PWM_REG_PWRMATCH_LD(pwm_id));

	//logic 0 higt min:480us, max 700us
	min_temp = RK_PWM_TIME_BIT0_MIN * 1000 / ddata->pwm_freq_nstime;
	max_temp = RK_PWM_TIME_BIT0_MAX * 1000 / ddata->pwm_freq_nstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, ddata->base + PWM_REG_PWRMATCH_HD_ZERO(pwm_id));

	//logic 1 higt min:1300us, max 2000us
	min_temp = RK_PWM_TIME_BIT1_MIN * 1000 / ddata->pwm_freq_nstime;
	max_temp = RK_PWM_TIME_BIT1_MAX * 1000 / ddata->pwm_freq_nstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, ddata->base + PWM_REG_PWRMATCH_HD_ONE(pwm_id));

	for (j = 0; j < ddata->maxkeybdnum; j++) {
		for (i = 0; i < remotectl_button[j].nbuttons; i++) {
			int scancode, usercode, pwrkey;

			if (remotectl_button[j].key_table[i].keycode != KEY_POWER)
				continue;
			usercode = remotectl_button[j].usercode & 0xffff;
			scancode = remotectl_button[j].key_table[i].scancode & 0xff;
			DBG("usercode=%x, key=%x\n", usercode, scancode);
			pwrkey  = usercode;
			pwrkey |= (scancode << 24) | ((~scancode & 0xff) << 16);
			DBG("pwrkey = %x\n", pwrkey);
			writel_relaxed(pwrkey, ddata->base
					+ PWM_PWRMATCH_VALUE(pwm_id) + num * 4);
			num++;
			if (num >= PWM_PWR_KEY_CAPURURE_MAX)
				break;
		}
	}

	val = readl_relaxed(ddata->base + PWM_REG_INT_EN(pwm_id));
	val = (val & 0xFFFFFF7F) | PWM_PWR_INT_ENABLE;
	writel_relaxed(val, ddata->base + PWM_REG_INT_EN(pwm_id));

	val = CH3_PWRKEY_ENABLE;
	writel_relaxed(val, ddata->base + PWM_REG_PWRMATCH_CTRL(pwm_id));

	val = readl_relaxed(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_ENABLE;
	writel_relaxed(val, ddata->base + PWM_REG_CTRL);
	ddata->pwm_pwrkey_capture = 1;
end:
	return ret;
}

static void rk_pwm_int_ctrl(void __iomem *pwm_base, uint pwm_id, int ctrl)
{
	int val;

	if (pwm_id > 3)
		return;
	val = readl_relaxed(pwm_base + PWM_REG_INT_EN(pwm_id));
	if (ctrl) {
		val |= PWM_CH_INT_ENABLE(pwm_id);
		DBG("pwm int enabled, value is 0x%x\n", val);
		writel_relaxed(val, pwm_base + PWM_REG_INT_EN(pwm_id));
	} else {
		val &= ~PWM_CH_INT_ENABLE(pwm_id);
		DBG("pwm int disabled, value is 0x%x\n", val);
	}
	writel_relaxed(val, pwm_base + PWM_REG_INT_EN(pwm_id));
}

static int rk_pwm_remotectl_hw_init(void __iomem *pwm_base, uint pwm_id)
{
	int val;

	if (pwm_id > 3)
		return -1;
	//1. disabled pwm
	val = readl_relaxed(pwm_base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_DISABLE;
	writel_relaxed(val, pwm_base + PWM_REG_CTRL);
	//2. capture mode
	val = readl_relaxed(pwm_base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFF9) | PWM_MODE_CAPTURE;
	writel_relaxed(val, pwm_base + PWM_REG_CTRL);
	//set clk div, clk div to 64
	val = readl_relaxed(pwm_base + PWM_REG_CTRL);
	val = (val & 0xFF0001FF) | PWM_DIV64;
	writel_relaxed(val, pwm_base + PWM_REG_CTRL);
	//4. enabled pwm int
	rk_pwm_int_ctrl(pwm_base, pwm_id, PWM_INT_ENABLE);
	//5. enabled pwm
	val = readl_relaxed(pwm_base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_ENABLE;
	writel_relaxed(val, pwm_base + PWM_REG_CTRL);
	return 0;
}

static int rk_pwm_sip_wakeup_init(struct platform_device *pdev)
{
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	struct irq_desc *desc;
	int support_psci = 0;
	int irq;
	int hwirq;
	int i, j;
	int num;
	int pwm_id;
	int ret = -1;

	if (of_property_read_u32(np, "remote_support_psci", &support_psci)) {
		dev_info(&pdev->dev, "PWM Missing psci property in the DT.\n");
		goto end;
	}
	DBG("support_psci=0x%x\n", support_psci);
	if (!support_psci)
		goto end;
	irq = ddata->irq;
	desc = irq_to_desc(irq);
	if (!desc || !desc->irq_data.domain)
		goto end;
	hwirq = desc->irq_data.hwirq;
	ret = sip_smc_remotectl_config(REMOTECTL_SET_IRQ, hwirq);
	if (ret) {
		dev_err(&pdev->dev, "set irq err, set support_psci to 0 !!\n");
		/*
		 * if atf doesn't support, return probe success to abandon atf
		 * function and still use kernel pwm parse function
		 */
		goto end;
	}
	pwm_id = ddata->remote_pwm_id;
	num = ddata->maxkeybdnum;
	sip_smc_remotectl_config(REMOTECTL_SET_PWM_CH, pwm_id);
	for (j = 0; j < num; j++) {
		for (i = 0; i < remotectl_button[j].nbuttons; i++) {
			int scancode, pwrkey;

			if (remotectl_button[j].key_table[i].keycode
			    != KEY_POWER)
				continue;
			scancode = remotectl_button[j].key_table[i].scancode;
			DBG("usercode=%x, key=%x\n",
			    remotectl_button[j].usercode, scancode);
			pwrkey = (remotectl_button[j].usercode & 0xffff) << 16;
			pwrkey |= (scancode & 0xff) << 8;
			DBG("deliver: key=%x\n", pwrkey);
			sip_smc_remotectl_config(REMOTECTL_SET_PWRKEY,
							pwrkey);
		}
	}
	sip_smc_remotectl_config(REMOTECTL_ENABLE, 1);
	ddata->support_psci = support_psci;
	DBG("rk pwm sip init end!\n");
	return 0;
end:
	dev_info(&pdev->dev, "pwm sip wakeup config error!!\n");
	ddata->support_psci = 0;
	return ret;
}

static inline void rk_pwm_wakeup(struct input_dev *input)
{
	input_event(input, EV_KEY, KEY_POWER, 1);
	input_event(input, EV_KEY, KEY_POWER, 0);
	input_sync(input);
}

static int rk_pwm_probe(struct platform_device *pdev)
{
	struct rkxx_remotectl_drvdata *ddata;
	struct device_node *np = pdev->dev.of_node;
	struct resource *r;
	struct input_dev *input;
	struct clk *clk;
	struct clk *p_clk;
	struct cpumask cpumask;
	int num;
	int irq;
	int ret;
	int i, j;
	int cpu_id;
	int pwm_id;
	int pwm_freq;
	int count;

	pr_err(".. rk pwm remotectl v2.0 init\n");
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
	ddata->temp_period = 0;
	ddata->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(ddata->base))
		return PTR_ERR(ddata->base);
	count = of_property_count_strings(np, "clock-names");
	if (count == 2) {
		clk = devm_clk_get(&pdev->dev, "pwm");
		p_clk = devm_clk_get(&pdev->dev, "pclk");
	} else {
		clk = devm_clk_get(&pdev->dev, NULL);
		p_clk = clk;
	}
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Can't get bus clk: %d\n", ret);
		return ret;
	}
	if (IS_ERR(p_clk)) {
		ret = PTR_ERR(p_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Can't get periph clk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable bus clk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(p_clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable bus periph clk: %d\n", ret);
		goto error_clk;
	}
	platform_set_drvdata(pdev, ddata);
	num = rk_remotectl_get_irkeybd_count(pdev);
	if (num == 0) {
		pr_err("remotectl: no ir keyboard add in dts!!\n");
		ret = -EINVAL;
		goto error_pclk;
	}
	ddata->maxkeybdnum = num;
	remotectl_button = devm_kzalloc(&pdev->dev,
					num * sizeof(struct rkxx_remotectl_button),
					GFP_KERNEL);
	if (!remotectl_button) {
		pr_err("failed to malloc remote button memory\n");
		ret = -ENOMEM;
		goto error_pclk;
	}
	input = devm_input_allocate_device(&pdev->dev);
	if (!input) {
		pr_err("failed to allocate input device\n");
		ret = -ENOMEM;
		goto error_pclk;
	}
	input->name = pdev->name;
	input->phys = "gpio-keys/remotectl";
	input->dev.parent = &pdev->dev;
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x524b;
	input->id.product = 0x0006;
	input->id.version = 0x0100;
	ddata->input = input;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		goto error_pclk;
	}
	ddata->irq = irq;
	ddata->wakeup = 1;
	of_property_read_u32(np, "remote_pwm_id", &pwm_id);
	pwm_id %= 4;
	ddata->remote_pwm_id = pwm_id;
	if (pwm_id > 3) {
		dev_err(&pdev->dev, "pwm id error\n");
		goto error_pclk;
	}
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
			int keycode;

			keycode = remotectl_button[j].key_table[i].keycode;
			input_set_capability(input, EV_KEY, keycode);
		}
	}
	ret = input_register_device(input);
	if (ret)
		dev_err(&pdev->dev, "register input device err, ret=%d\n", ret);
	input_set_capability(input, EV_KEY, KEY_WAKEUP);
	device_init_wakeup(&pdev->dev, 1);
	enable_irq_wake(irq);
	timer_setup(&ddata->timer, rk_pwm_remotectl_timer, 0);
	wake_lock_init(&ddata->remotectl_wake_lock,
		       WAKE_LOCK_SUSPEND, "rockchip_pwm_remote");
	cpumask_clear(&cpumask);
	cpumask_set_cpu(cpu_id, &cpumask);
	irq_set_affinity(irq, &cpumask);
	ret = devm_request_irq(&pdev->dev, irq, rockchip_pwm_irq,
			       IRQF_NO_SUSPEND, "rk_pwm_irq", ddata);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", irq);
		goto error_irq;
	}

	pwm_freq = clk_get_rate(clk) / 64;
	ddata->pwm_freq_nstime = 1000000000 / pwm_freq;
	rk_pwm_remotectl_hw_init(ddata->base, pwm_id);

	ret = rk_pwm_pwrkey_wakeup_init(pdev);
	if (!ret) {
		dev_info(&pdev->dev, "Controller support pwrkey capture\n");
		goto end;
	}

	ret = rk_pwm_sip_wakeup_init(pdev);
	if (ret)
		dev_info(&pdev->dev, "Donot support ATF Wakeup\n");
	else
		dev_info(&pdev->dev, "Support ATF Wakeup\n");

	DBG("rk pwm remotectl init end!\n");
end:
	return 0;
error_irq:
	wake_lock_destroy(&ddata->remotectl_wake_lock);
error_pclk:
	clk_unprepare(p_clk);
error_clk:
	clk_unprepare(clk);
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
	int pwm_id;
	struct cpumask cpumask;
	struct platform_device *pdev = to_platform_device(dev);
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);

	if (ddata->pwm_pwrkey_capture) {
		pwm_id = ddata->remote_pwm_id;
		ddata->pwrkey_wakeup = 0;
		rk_pwm_int_ctrl(ddata->base, pwm_id, PWM_INT_DISABLE);
	}
	cpumask_clear(&cpumask);
	cpumask_set_cpu(cpu, &cpumask);
	irq_set_affinity(ddata->irq, &cpumask);
	return 0;
}


static int remotectl_resume(struct device *dev)
{
	struct cpumask cpumask;
	int pwm_id;
	struct platform_device *pdev = to_platform_device(dev);
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
	int state;

	cpumask_clear(&cpumask);
	cpumask_set_cpu(ddata->handle_cpu_id, &cpumask);
	irq_set_affinity(ddata->irq, &cpumask);
	if (ddata->support_psci) {
		/*
		 * loop wakeup state
		 */
		state = sip_smc_remotectl_config(
					REMOTECTL_GET_WAKEUP_STATE, 0);
		if (state == REMOTECTL_PWRKEY_WAKEUP)
			rk_pwm_wakeup(ddata->input);
	}  else if (ddata->pwm_pwrkey_capture) {
		pwm_id = ddata->remote_pwm_id;
		rk_pwm_int_ctrl(ddata->base, pwm_id, PWM_INT_ENABLE);
		if (ddata->pwrkey_wakeup == 0)
			return 0;
		ddata->pwrkey_wakeup = 0;
		rk_pwm_wakeup(ddata->input);
	}

	return 0;
}

static const struct dev_pm_ops remotectl_pm_ops = {
	.suspend_late = remotectl_suspend,
	.resume_early = remotectl_resume,
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
		.pm = &remotectl_pm_ops,
#endif
	},
	.remove = rk_pwm_remove,
};

module_platform_driver_probe(rk_pwm_driver, rk_pwm_probe);

MODULE_LICENSE("GPL");
