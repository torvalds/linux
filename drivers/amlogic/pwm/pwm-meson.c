/*
 * pwm-meson.c
 *
 * Support for Meson current source PWM
 *
 * Copyright (C) 2012 Elvis Yu <elvis.yu@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <mach/am_regs.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pwm.h>

static int npwm=1;
module_param(npwm,int,0644);
MODULE_PARM_DESC(npwm,"\n odroid-c1 The number of available pwm (max 2-port)\n");

#define PWM_B   0
#define PWM_E   1
#define FIN_FREQ		(24 * 1000)

struct meson_pwm_device {
	unsigned int		freq;
	unsigned int		duty;
	unsigned char		pwm_id;
	struct pwm_device	*pwm;
};

struct meson_chip {
	struct platform_device	*pdev;
	struct pwm_chip		    chip;
	struct meson_pwm_device *meson_pwm[2];
	struct pinctrl *pinctrl;
};

#define to_meson_chip(chip)	container_of(chip, struct meson_chip, chip)
#define pwm_dbg(_pwm, msg...) dev_dbg(&(_pwm)->pdev->dev, msg)

static void meson_pwm_init(struct device *dev, int pwmn)
{
	unsigned long flags;
	if(pwmn == 1) {
	    local_irq_save(flags);
        aml_write_reg32(P_PWM_MISC_REG_AB, (aml_read_reg32(P_PWM_MISC_REG_AB) & ~(0x7f << 16)) | ((1 << 23)));    
    	local_irq_restore(flags);
    }
    else {
    	local_irq_save(flags);
        aml_write_reg32(P_PWM_MISC_REG_AB, (aml_read_reg32(P_PWM_MISC_REG_AB) & ~(0x7f << 16)) | ((1 << 23)));
        aml_write_reg32(P_PWM_MISC_REG_EF, (aml_read_reg32(P_PWM_MISC_REG_EF) & ~(0x7f <<  8)) | ((1 << 15)));
    	local_irq_restore(flags);
    }
    return;
}

static int meson_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
   	struct meson_chip *meson = to_meson_chip(chip);
   	struct meson_pwm_device *meson_pwm;
	unsigned int id = pwm->pwm;
	unsigned long flags;

    meson_pwm = meson->meson_pwm[id];
    local_irq_save(flags);
    switch (id) {
        case PWM_B:
            aml_set_reg32_bits(P_PWM_MISC_REG_AB, 1, 1, 1);  //enable pwm_b
            break;
        case PWM_E:
            aml_set_reg32_bits(P_PWM_MISC_REG_EF, 1, 0, 1);  //enable pwm_e
            break;
    }
    local_irq_restore(flags);
    
	return 0;
}

static void meson_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
   	struct meson_chip *meson = to_meson_chip(chip);
   	struct meson_pwm_device *meson_pwm;
	unsigned int id = pwm->pwm;
    unsigned long flags;

    meson_pwm = meson->meson_pwm[id];
    local_irq_save(flags);
    switch (id) {
        case PWM_B:
            aml_set_reg32_bits(P_PWM_MISC_REG_AB, 0, 1, 1);  //disable pwm_b
            break;
        case PWM_E:
            aml_set_reg32_bits(P_PWM_MISC_REG_EF, 0, 0, 1);  //disable pwm_e
            break;
    }
    local_irq_restore(flags);
    return;
}

static int meson_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_percent, int pwm_freq)
{
   	struct meson_chip *meson = to_meson_chip(chip);
   	struct meson_pwm_device *meson_pwm;
	unsigned int id = pwm->pwm;
    struct device *dev = chip->dev;
    unsigned pwm_hi = 0, pwm_lo = 0;
    unsigned fout_freq=0, pwm_cnt, pwm_pre_div;
    unsigned long flags=0;
    int i=0;

	if((duty_percent<0)||(duty_percent>100)){
	    dev_err(dev, "Not available duty_percent... error!!!\n");
	    return -EINVAL;
	}
    meson_pwm = meson->meson_pwm[id];
    
    fout_freq = ((pwm_freq >= (FIN_FREQ * 500)) ? (FIN_FREQ * 500) : pwm_freq);
    for (i=0; i<0x7f; i++) {
        pwm_pre_div = i;
        pwm_cnt = FIN_FREQ * 1000 / (pwm_freq * (pwm_pre_div + 1)) - 2;
        if (pwm_cnt <= 0xffff)
            break;
    }
    if(duty_percent==0) {
        pwm_hi=0;
        pwm_lo=pwm_cnt;
        goto div_set;
    }
    if(duty_percent==100) {
        pwm_hi=pwm_cnt;
        pwm_lo=0;
        goto div_set;
    }
    pwm_hi = (pwm_cnt*duty_percent)/100;
    pwm_lo = (pwm_cnt*(100-duty_percent))/100;

div_set :
    local_irq_save(flags);
    switch(id){
        case PWM_B:
            aml_set_reg32_bits(P_PWM_MISC_REG_AB, pwm_pre_div, 16, 7);  //pwm_b_clk_div
            aml_write_reg32(P_PWM_PWM_B, (pwm_hi << 16) | (pwm_lo));
            break;
        case PWM_E:
            aml_set_reg32_bits(P_PWM_MISC_REG_EF, pwm_pre_div, 8, 7);   //pwm_e_clk_div
            aml_write_reg32(P_PWM_PWM_E, (pwm_hi << 16) | (pwm_lo));
            break;
        default:
            break;
    }
    local_irq_restore(flags);

	return 0;
}

static int meson_pwm_request(struct pwm_chip *chip,
				 struct pwm_device *pwm)
{
	struct meson_chip *meson = to_meson_chip(chip);
	unsigned int id = pwm->pwm;

	meson->meson_pwm[id] = devm_kzalloc(chip->dev, sizeof(struct meson_pwm_device), GFP_KERNEL);
	if (!meson->meson_pwm[id])
		return -ENOMEM;

	meson->meson_pwm[id]->pwm_id = id;

	meson->meson_pwm[id]->pwm = pwm;
	pwm_set_chip_data(pwm, meson->meson_pwm[id]);

	return 0;
}

static void meson_pwm_free(struct pwm_chip *chip,
				struct pwm_device *pwm)
{
	struct meson_chip *meson = to_meson_chip(chip);
	unsigned int id = pwm->pwm;

    if(meson->pinctrl){
        devm_pinctrl_put(meson->pinctrl);
        meson->pinctrl=NULL;
    }
    devm_kfree(chip->dev, meson->meson_pwm[id]);
}

static struct pwm_ops meson_pwm_ops = {
	.request = meson_pwm_request,
	.free = meson_pwm_free,
	.enable = meson_pwm_enable,
	.disable = meson_pwm_disable,
	.config = meson_pwm_config,
	.owner = THIS_MODULE,
};

static int meson_pwm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct meson_chip *meson;
	char prop_name[20];
	int ret=0;

    if (!np) {
        return -ENODEV;
    }

	if((npwm<=0)||(npwm>2)){
	    dev_err(dev, "Available pwm_device number error.\n");
	    return -EINVAL;
	}
    
	meson = devm_kzalloc(&pdev->dev, sizeof(*meson), GFP_KERNEL);
	if (meson == NULL) {
		dev_err(dev, "failed to allocate pwm_device\n");
		return -ENOMEM;
	}

	meson->pdev = pdev;
	meson->chip.dev = &pdev->dev;
	meson->chip.ops = &meson_pwm_ops;
	meson->chip.base = -1;
	meson->chip.npwm = npwm;

	ret = pwmchip_add(&meson->chip);
	if (ret < 0) {
		dev_err(dev, "failed to register pwm\n");
		return ret;
	}
	platform_set_drvdata(pdev, meson);

    if(npwm == 2)   strcpy(prop_name,"odroid_pwm1");
    else            strcpy(prop_name,"odroid_pwm0");

	meson->pinctrl = devm_pinctrl_get_select(&pdev->dev, prop_name);
	if(IS_ERR(meson->pinctrl)) {
		dev_err(&pdev->dev, "pinmux error\n");
		return -ENODEV;
	}
	dev_info(&pdev->dev, "pinctrl_name = %s\n", prop_name);

    meson_pwm_init(dev, npwm);

    dev_info(dev, "register pwm device.. %s\n",__func__);
	return 0;
}

static int meson_pwm_remove(struct platform_device *pdev)
{
	struct meson_chip *meson = platform_get_drvdata(pdev);
	int err;


	err = pwmchip_remove(&meson->chip);
	if (err < 0)
		return err;

    devm_kfree(meson->chip.dev,meson);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int meson_pwm_suspend(struct device *dev)
{
	return 0;
}

static int meson_pwm_resume(struct device *dev)
{
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(meson_pwm_pm_ops, meson_pwm_suspend,
			meson_pwm_resume);

#ifdef CONFIG_OF
static const struct of_device_id meson_pwm_of_match[]={
	{	.compatible = "amlogic, odroid-pwm", },
	{},
};
#else
#define meson_pwm_of_match NULL
#endif

static struct platform_driver meson_pwm_driver = {
	.driver		= {
		.name	= "meson_pwm",
		.owner	= THIS_MODULE,
		.of_match_table = meson_pwm_of_match,
		.pm	= &meson_pwm_pm_ops,
	},
	.probe		= meson_pwm_probe,
	.remove		= meson_pwm_remove,
};

static int __init module_pwm_init(void)
{
	int ret;

	ret = platform_driver_register(&meson_pwm_driver);
	if (ret)
		pr_err("failed to add pwm driver\n");

	return ret;
}
static void __exit module_pwm_exit(void)
{
    platform_driver_unregister(&meson_pwm_driver);
}
module_init(module_pwm_init);
module_exit(module_pwm_exit);

MODULE_DESCRIPTION("AMLogic meson8b PWM driver");
MODULE_AUTHOR("HardKernel");
MODULE_LICENSE("GPL");