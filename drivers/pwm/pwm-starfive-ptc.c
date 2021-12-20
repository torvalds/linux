/*
 * Copyright (C) 2018 Starfive, Inc
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2, as published by
 * the Free Software Foundation.
 */

#include <dt-bindings/pwm/pwm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>

/* max channel of pwm */
#define MAX_PWM							8

/* PTC Register offsets */
#define REG_RPTC_CNTR 						0x0
#define REG_RPTC_HRC						0x4
#define REG_RPTC_LRC						0x8
#define REG_RPTC_CTRL						0xC

/* Bit for PWM clock */
#define BIT_PWM_CLOCK_EN					31

/* Bit for clock gen soft reset */
#define BIT_CLK_GEN_SOFT_RESET					13

#define NS_1							1000000000

#define PTC_DEBUG						0

/* Access PTC register (cntr hrc lrc and ctrl) ,need to replace PWM_BASE_ADDR */
#define REG_PTC_BASE_ADDR_SUB(base, N)		((base) + ((N>3)?((N%4)*0x10+(1<<15)):(N*0x10))) 
#define REG_PTC_RPTC_CNTR(base,N)		(REG_PTC_BASE_ADDR_SUB(base,N))
#define REG_PTC_RPTC_HRC(base,N)		(REG_PTC_BASE_ADDR_SUB(base,N) + 0x4)
#define REG_PTC_RPTC_LRC(base,N)		(REG_PTC_BASE_ADDR_SUB(base,N) + 0x8)
#define REG_PTC_RPTC_CTRL(base,N)		(REG_PTC_BASE_ADDR_SUB(base,N) + 0xC)

///PTC_RPTC_CTRL
#define PTC_EN      (1<<0)
#define PTC_ECLK    (1<<1)  /* 1:ptc_ecgt signal increment RPTC_CNTR. 0:system clock increment RPTC_CNTR. */
#define PTC_NEC     (1<<2)  /* gate:system clock or ptc_ecgt input signal to increment RPTC_CNTR. If gate function is enabled, PWM periods can be automatically adjusted with the capture input. */
#define PTC_OE      (1<<3)  /* enbale PWM output */
#define PTC_SIGNLE  (1<<4)  /* 1:single operation; 0:continue operation */
#define PTC_INTE    (1<<5)  /* Timer/Counter interrput enable */
#define PTC_INT     (1<<6)  /* interrupt status, write 1 to clear */
#define PTC_CNTRRST (1<<7)  /* 0:clear reset */
#define PTC_CAPTE   (1<<8)  /* ptc_capt to increment RPTC_CNTR.*/


/* pwm ptc device */
struct starfive_pwm_ptc_device {
	struct pwm_chip		chip;
	struct clk		*clk;
	void __iomem		*regs;
	int 			irq;
	/* apb clock frequency , from dts */
	unsigned int		approx_period;
};

static inline struct starfive_pwm_ptc_device *chip_to_starfive_ptc(struct pwm_chip *c)
{
	return container_of(c, struct starfive_pwm_ptc_device, chip);
}


static void starfive_pwm_ptc_get_state(struct pwm_chip *chip, struct pwm_device *dev, struct pwm_state *state)
{
	struct starfive_pwm_ptc_device *pwm = chip_to_starfive_ptc(chip);
	uint32_t data_lrc;
	uint32_t data_hrc;
	uint32_t period;	
	uint32_t pwm_clk_ns = 0;

	/* get lrc and hrc data from registe*/
	data_lrc = ioread32(REG_PTC_RPTC_LRC(pwm->regs,dev->hwpwm));
	data_hrc = ioread32(REG_PTC_RPTC_HRC(pwm->regs,dev->hwpwm));
	//period = data_lrc + data_hrc;

	/* how many ns does apb clock elapse */
	pwm_clk_ns = NS_1 / pwm->approx_period;

	/* pwm period(ns) */
	state->period     = data_lrc*pwm_clk_ns;

	/* duty cycle(ns) ,means high level eclapse ns if it is normal polarity */
	state->duty_cycle = data_hrc*pwm_clk_ns;

	/* polarity,we don't use it now because it is not in dts */
	state->polarity   = PWM_POLARITY_NORMAL;

	/* enabled or not */
	state->enabled    = 1;
#if PTC_DEBUG   
	printk("starfive_pwm_ptc_get_state in,no:%d....\r\n",dev->hwpwm);
	printk("data_hrc:0x%x 0x%x \n", data_hrc, data_lrc);
	printk("period:%d\r\n",state->period);
	printk("duty_cycle:%d\r\n",state->duty_cycle);
	printk("polarity:%d\r\n",state->polarity);
	printk("enabled:%d\r\n",state->enabled);
#endif

}


static int starfive_pwm_ptc_apply(struct pwm_chip *chip, struct pwm_device *dev, struct pwm_state *state)
{
	struct starfive_pwm_ptc_device *pwm = chip_to_starfive_ptc(chip);
	uint32_t pwm_clk_ns = 0;
	uint32_t data_hrc = 0;
	uint32_t data_lrc = 0;
	uint32_t period_data = 0;
	uint32_t duty_data = 0;
	void __iomem* reg_addr;

#if PTC_DEBUG	
	printk("starfive_pwm_ptc_apply in,no:%d....\r\n",dev->hwpwm);
	printk("set parameter......\r\n");
	printk("period:%d\r\n",state->period);
	printk("duty_cycle:%d\r\n",state->duty_cycle);	
	printk("polarity:%d\r\n",state->polarity);
	printk("enabled:%d\r\n",state->enabled);
#endif
	/* duty_cycle should be less or equal than period */
	if(state->duty_cycle > state->period)
	{
		state->duty_cycle = state->period;
	}	

	/* calculate pwm real period (ns) */
	pwm_clk_ns = NS_1 / pwm->approx_period;
	
#if PTC_DEBUG
	printk("approx_period,:%d,pwm_clk_ns:%d\r\n",pwm->approx_period,pwm_clk_ns);
#endif

	/* calculate period count */
	period_data = state->period / pwm_clk_ns;

	if (!state->enabled) 
	{
		/* if is unenable,just set duty_dat to 0 , means low level always */
		duty_data = 0;
	}
	else
	{
		/* calculate duty count*/
		duty_data = state->duty_cycle / pwm_clk_ns;
	}

#if PTC_DEBUG
	printk("period_data:%d,duty_data:%d\r\n",period_data,duty_data);
#endif

	if(state->polarity == PWM_POLARITY_NORMAL)
	{
		/* calculate data_hrc */	
		data_hrc = period_data - duty_data;		
	}
	else
	{
		/* calculate data_hrc */
		data_hrc = duty_data;	
	}
	data_lrc = period_data;

	/* set hrc */
	reg_addr = REG_PTC_RPTC_HRC(pwm->regs,dev->hwpwm);
#if PTC_DEBUG
	printk("[starfive_pwm_ptc_config]reg_addr:0x%lx,HRC data:0x%x....\n",reg_addr,data_hrc);
#endif
	iowrite32(data_hrc, reg_addr);


	/* set lrc */
	reg_addr = REG_PTC_RPTC_LRC(pwm->regs,dev->hwpwm);
#if PTC_DEBUG
	printk("[starfive_pwm_ptc_config]reg_addr:0x%lx,LRC data:0x%x....\n",reg_addr,data_lrc);
#endif	
	iowrite32(data_lrc, reg_addr);	

	/* set REG_RPTC_CNTR*/
	reg_addr = REG_PTC_RPTC_CNTR(pwm->regs, dev->hwpwm);
	iowrite32(0, reg_addr);
	
	/* set REG_PTC_RPTC_CTRL*/
	reg_addr = REG_PTC_RPTC_CTRL(pwm->regs, dev->hwpwm);
	iowrite32(PTC_EN|PTC_OE, reg_addr);
	
	return 0;
}



static const struct pwm_ops starfive_pwm_ptc_ops = {
	.get_state	= starfive_pwm_ptc_get_state,
	.apply		= (void*)starfive_pwm_ptc_apply,
	.owner		= THIS_MODULE,
};




static int starfive_pwm_ptc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct starfive_pwm_ptc_device *pwm;
	struct pwm_chip *chip;
	struct resource *res;
	int ret;
	
#if PTC_DEBUG
	printk("starfive_pwm_ptc_probe in....\r\n");
#endif
	pwm = devm_kzalloc(dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm) {
		dev_err(dev, "Out of memory\n");
		return -ENOMEM;
	}

	chip = &pwm->chip;
	chip->dev = dev;
	chip->ops = &starfive_pwm_ptc_ops;

	/* how many parameters can be transfered to ptc,need to fix */
	chip->of_pwm_n_cells = 3;
	chip->base = -1;

	/* get pwm channels count, max value is 8 */
	ret = of_property_read_u32(node, "starfive,npwm", &chip->npwm);
	if (ret < 0 || chip->npwm > MAX_PWM) 
	{
		chip->npwm = MAX_PWM;
	}
#if PTC_DEBUG	
	printk("[starfive_pwm_ptc_probe] npwm:0x%lx....\r\n",chip->npwm);
#endif
	/* get apb clock frequency */
	ret = of_property_read_u32(node, "starfive,approx-period", &pwm->approx_period);

#if PTC_DEBUG
	printk("[starfive_pwm_ptc_probe] approx_period:%d....\r\n",pwm->approx_period);
#endif
	/* get IO base address*/
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

#if PTC_DEBUG
	printk("[starfive_pwm_ptc_probe] res start:0x%lx,end:0x%lx....\r\n",res->start,res->end);
#endif	
	pwm->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pwm->regs)) 
	{
		dev_err(dev, "Unable to map IO resources\n");
		return PTR_ERR(pwm->regs);
	}

#if PTC_DEBUG
	printk("[starfive_pwm_ptc_probe] regs:0x%lx....\r\n",pwm->regs);
#endif

	pwm->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pwm->clk)) {
		dev_err(dev, "Unable to find controller clock\n");
		return PTR_ERR(pwm->clk);
	}

	/* after add,it will display as /sys/class/pwm/pwmchip0,0 is chip->base 
	 * after execute echo 0 > export in  , pwm0 can be seen */
	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err(dev, "cannot register PTC: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pwm);
	
#if PTC_DEBUG
	printk("starfive PWM PTC chip registered %d PWMs\n", chip->npwm);
#endif

	return 0;
}

static int starfive_pwm_ptc_remove(struct platform_device *dev)
{
	struct starfive_pwm_ptc_device *pwm = platform_get_drvdata(dev);
	struct pwm_chip *chip = &pwm->chip;

	pwmchip_remove(chip);

	return 0;
}

static const struct of_device_id starfive_pwm_ptc_of_match[] = {
	{ .compatible = "starfive,pwm0" },
	{},
};
MODULE_DEVICE_TABLE(of, starfive_pwm_ptc_of_match);

static struct platform_driver starfive_pwm_ptc_driver = {
	.probe = starfive_pwm_ptc_probe,
	.remove = starfive_pwm_ptc_remove,
	.driver = {
		.name = "pwm-starfive-ptc",
		.of_match_table = of_match_ptr(starfive_pwm_ptc_of_match),
	},
};
module_platform_driver(starfive_pwm_ptc_driver);

MODULE_DESCRIPTION("starfive PWM PTC driver");
MODULE_LICENSE("GPL v2");

