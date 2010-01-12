/*
 *  Atmel Touch Screen Driver
 *
 *  Copyright (c) 2008 ATMEL
 *  Copyright (c) 2008 Dan Liang
 *  Copyright (c) 2008 TimeSys Corporation
 *  Copyright (c) 2008 Justin Waters
 *
 *  Based on touchscreen code from Atmel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <mach/board.h>
#include <mach/cpu.h>

/* Register definitions based on AT91SAM9RL64 preliminary draft datasheet */

#define ATMEL_TSADCC_CR		0x00	/* Control register */
#define   ATMEL_TSADCC_SWRST	(1 << 0)	/* Software Reset*/
#define	  ATMEL_TSADCC_START	(1 << 1)	/* Start conversion */

#define ATMEL_TSADCC_MR		0x04	/* Mode register */
#define	  ATMEL_TSADCC_TSAMOD	(3    <<  0)	/* ADC mode */
#define	    ATMEL_TSADCC_TSAMOD_ADC_ONLY_MODE	(0x0)	/* ADC Mode */
#define	    ATMEL_TSADCC_TSAMOD_TS_ONLY_MODE	(0x1)	/* Touch Screen Only Mode */
#define	  ATMEL_TSADCC_LOWRES	(1    <<  4)	/* Resolution selection */
#define	  ATMEL_TSADCC_SLEEP	(1    <<  5)	/* Sleep mode */
#define	  ATMEL_TSADCC_PENDET	(1    <<  6)	/* Pen Detect selection */
#define	  ATMEL_TSADCC_PRES	(1    <<  7)	/* Pressure Measurement Selection */
#define	  ATMEL_TSADCC_PRESCAL	(0x3f <<  8)	/* Prescalar Rate Selection */
#define	  ATMEL_TSADCC_EPRESCAL	(0xff <<  8)	/* Prescalar Rate Selection (Extended) */
#define	  ATMEL_TSADCC_STARTUP	(0x7f << 16)	/* Start Up time */
#define	  ATMEL_TSADCC_SHTIM	(0xf  << 24)	/* Sample & Hold time */
#define	  ATMEL_TSADCC_PENDBC	(0xf  << 28)	/* Pen Detect debouncing time */

#define ATMEL_TSADCC_TRGR	0x08	/* Trigger register */
#define	  ATMEL_TSADCC_TRGMOD	(7      <<  0)	/* Trigger mode */
#define	    ATMEL_TSADCC_TRGMOD_NONE		(0 << 0)
#define     ATMEL_TSADCC_TRGMOD_EXT_RISING	(1 << 0)
#define     ATMEL_TSADCC_TRGMOD_EXT_FALLING	(2 << 0)
#define     ATMEL_TSADCC_TRGMOD_EXT_ANY		(3 << 0)
#define     ATMEL_TSADCC_TRGMOD_PENDET		(4 << 0)
#define     ATMEL_TSADCC_TRGMOD_PERIOD		(5 << 0)
#define     ATMEL_TSADCC_TRGMOD_CONTINUOUS	(6 << 0)
#define   ATMEL_TSADCC_TRGPER	(0xffff << 16)	/* Trigger period */

#define ATMEL_TSADCC_TSR	0x0C	/* Touch Screen register */
#define	  ATMEL_TSADCC_TSFREQ	(0xf <<  0)	/* TS Frequency in Interleaved mode */
#define	  ATMEL_TSADCC_TSSHTIM	(0xf << 24)	/* Sample & Hold time */

#define ATMEL_TSADCC_CHER	0x10	/* Channel Enable register */
#define ATMEL_TSADCC_CHDR	0x14	/* Channel Disable register */
#define ATMEL_TSADCC_CHSR	0x18	/* Channel Status register */
#define	  ATMEL_TSADCC_CH(n)	(1 << (n))	/* Channel number */

#define ATMEL_TSADCC_SR		0x1C	/* Status register */
#define	  ATMEL_TSADCC_EOC(n)	(1 << ((n)+0))	/* End of conversion for channel N */
#define	  ATMEL_TSADCC_OVRE(n)	(1 << ((n)+8))	/* Overrun error for channel N */
#define	  ATMEL_TSADCC_DRDY	(1 << 16)	/* Data Ready */
#define	  ATMEL_TSADCC_GOVRE	(1 << 17)	/* General Overrun Error */
#define	  ATMEL_TSADCC_ENDRX	(1 << 18)	/* End of RX Buffer */
#define	  ATMEL_TSADCC_RXBUFF	(1 << 19)	/* TX Buffer full */
#define	  ATMEL_TSADCC_PENCNT	(1 << 20)	/* Pen contact */
#define	  ATMEL_TSADCC_NOCNT	(1 << 21)	/* No contact */

#define ATMEL_TSADCC_LCDR	0x20	/* Last Converted Data register */
#define	  ATMEL_TSADCC_DATA	(0x3ff << 0)	/* Channel data */

#define ATMEL_TSADCC_IER	0x24	/* Interrupt Enable register */
#define ATMEL_TSADCC_IDR	0x28	/* Interrupt Disable register */
#define ATMEL_TSADCC_IMR	0x2C	/* Interrupt Mask register */
#define ATMEL_TSADCC_CDR0	0x30	/* Channel Data 0 */
#define ATMEL_TSADCC_CDR1	0x34	/* Channel Data 1 */
#define ATMEL_TSADCC_CDR2	0x38	/* Channel Data 2 */
#define ATMEL_TSADCC_CDR3	0x3C	/* Channel Data 3 */
#define ATMEL_TSADCC_CDR4	0x40	/* Channel Data 4 */
#define ATMEL_TSADCC_CDR5	0x44	/* Channel Data 5 */

#define ATMEL_TSADCC_XPOS	0x50
#define ATMEL_TSADCC_Z1DAT	0x54
#define ATMEL_TSADCC_Z2DAT	0x58

#define PRESCALER_VAL(x)	((x) >> 8)

#define ADC_DEFAULT_CLOCK	100000

struct atmel_tsadcc {
	struct input_dev	*input;
	char			phys[32];
	struct clk		*clk;
	int			irq;
	unsigned int		prev_absx;
	unsigned int		prev_absy;
	unsigned char		bufferedmeasure;
};

static void __iomem		*tsc_base;

#define atmel_tsadcc_read(reg)		__raw_readl(tsc_base + (reg))
#define atmel_tsadcc_write(reg, val)	__raw_writel((val), tsc_base + (reg))

static irqreturn_t atmel_tsadcc_interrupt(int irq, void *dev)
{
	struct atmel_tsadcc	*ts_dev = (struct atmel_tsadcc *)dev;
	struct input_dev	*input_dev = ts_dev->input;

	unsigned int status;
	unsigned int reg;

	status = atmel_tsadcc_read(ATMEL_TSADCC_SR);
	status &= atmel_tsadcc_read(ATMEL_TSADCC_IMR);

	if (status & ATMEL_TSADCC_NOCNT) {
		/* Contact lost */
		reg = atmel_tsadcc_read(ATMEL_TSADCC_MR) | ATMEL_TSADCC_PENDBC;

		atmel_tsadcc_write(ATMEL_TSADCC_MR, reg);
		atmel_tsadcc_write(ATMEL_TSADCC_TRGR, ATMEL_TSADCC_TRGMOD_NONE);
		atmel_tsadcc_write(ATMEL_TSADCC_IDR,
				   ATMEL_TSADCC_EOC(3) | ATMEL_TSADCC_NOCNT);
		atmel_tsadcc_write(ATMEL_TSADCC_IER, ATMEL_TSADCC_PENCNT);

		input_report_key(input_dev, BTN_TOUCH, 0);
		ts_dev->bufferedmeasure = 0;
		input_sync(input_dev);

	} else if (status & ATMEL_TSADCC_PENCNT) {
		/* Pen detected */
		reg = atmel_tsadcc_read(ATMEL_TSADCC_MR);
		reg &= ~ATMEL_TSADCC_PENDBC;

		atmel_tsadcc_write(ATMEL_TSADCC_IDR, ATMEL_TSADCC_PENCNT);
		atmel_tsadcc_write(ATMEL_TSADCC_MR, reg);
		atmel_tsadcc_write(ATMEL_TSADCC_IER,
				   ATMEL_TSADCC_EOC(3) | ATMEL_TSADCC_NOCNT);
		atmel_tsadcc_write(ATMEL_TSADCC_TRGR,
				   ATMEL_TSADCC_TRGMOD_PERIOD | (0x0FFF << 16));

	} else if (status & ATMEL_TSADCC_EOC(3)) {
		/* Conversion finished */

		if (ts_dev->bufferedmeasure) {
			/* Last measurement is always discarded, since it can
			 * be erroneous.
			 * Always report previous measurement */
			input_report_abs(input_dev, ABS_X, ts_dev->prev_absx);
			input_report_abs(input_dev, ABS_Y, ts_dev->prev_absy);
			input_report_key(input_dev, BTN_TOUCH, 1);
			input_sync(input_dev);
		} else
			ts_dev->bufferedmeasure = 1;

		/* Now make new measurement */
		ts_dev->prev_absx = atmel_tsadcc_read(ATMEL_TSADCC_CDR3) << 10;
		ts_dev->prev_absx /= atmel_tsadcc_read(ATMEL_TSADCC_CDR2);

		ts_dev->prev_absy = atmel_tsadcc_read(ATMEL_TSADCC_CDR1) << 10;
		ts_dev->prev_absy /= atmel_tsadcc_read(ATMEL_TSADCC_CDR0);
	}

	return IRQ_HANDLED;
}

/*
 * The functions for inserting/removing us as a module.
 */

static int __devinit atmel_tsadcc_probe(struct platform_device *pdev)
{
	struct atmel_tsadcc	*ts_dev;
	struct input_dev	*input_dev;
	struct resource		*res;
	struct at91_tsadcc_data *pdata = pdev->dev.platform_data;
	int		err = 0;
	unsigned int	prsc;
	unsigned int	reg;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no mmio resource defined.\n");
		return -ENXIO;
	}

	/* Allocate memory for device */
	ts_dev = kzalloc(sizeof(struct atmel_tsadcc), GFP_KERNEL);
	if (!ts_dev) {
		dev_err(&pdev->dev, "failed to allocate memory.\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, ts_dev);

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device.\n");
		err = -EBUSY;
		goto err_free_mem;
	}

	ts_dev->irq = platform_get_irq(pdev, 0);
	if (ts_dev->irq < 0) {
		dev_err(&pdev->dev, "no irq ID is designated.\n");
		err = -ENODEV;
		goto err_free_dev;
	}

	if (!request_mem_region(res->start, resource_size(res),
				"atmel tsadcc regs")) {
		dev_err(&pdev->dev, "resources is unavailable.\n");
		err = -EBUSY;
		goto err_free_dev;
	}

	tsc_base = ioremap(res->start, resource_size(res));
	if (!tsc_base) {
		dev_err(&pdev->dev, "failed to map registers.\n");
		err = -ENOMEM;
		goto err_release_mem;
	}

	err = request_irq(ts_dev->irq, atmel_tsadcc_interrupt, IRQF_DISABLED,
			pdev->dev.driver->name, ts_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to allocate irq.\n");
		goto err_unmap_regs;
	}

	ts_dev->clk = clk_get(&pdev->dev, "tsc_clk");
	if (IS_ERR(ts_dev->clk)) {
		dev_err(&pdev->dev, "failed to get ts_clk\n");
		err = PTR_ERR(ts_dev->clk);
		goto err_free_irq;
	}

	ts_dev->input = input_dev;
	ts_dev->bufferedmeasure = 0;

	snprintf(ts_dev->phys, sizeof(ts_dev->phys),
		 "%s/input0", dev_name(&pdev->dev));

	input_dev->name = "atmel touch screen controller";
	input_dev->phys = ts_dev->phys;
	input_dev->dev.parent = &pdev->dev;

	__set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_X, 0, 0x3FF, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0x3FF, 0, 0);

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);

	/* clk_enable() always returns 0, no need to check it */
	clk_enable(ts_dev->clk);

	prsc = clk_get_rate(ts_dev->clk);
	dev_info(&pdev->dev, "Master clock is set at: %d Hz\n", prsc);

	if (!pdata)
		goto err_fail;

	if (!pdata->adc_clock)
		pdata->adc_clock = ADC_DEFAULT_CLOCK;

	prsc = (prsc / (2 * pdata->adc_clock)) - 1;

	/* saturate if this value is too high */
	if (cpu_is_at91sam9rl()) {
		if (prsc > PRESCALER_VAL(ATMEL_TSADCC_PRESCAL))
			prsc = PRESCALER_VAL(ATMEL_TSADCC_PRESCAL);
	} else {
		if (prsc > PRESCALER_VAL(ATMEL_TSADCC_EPRESCAL))
			prsc = PRESCALER_VAL(ATMEL_TSADCC_EPRESCAL);
	}

	dev_info(&pdev->dev, "Prescaler is set at: %d\n", prsc);

	reg = ATMEL_TSADCC_TSAMOD_TS_ONLY_MODE		|
		((0x00 << 5) & ATMEL_TSADCC_SLEEP)	|	/* Normal Mode */
		((0x01 << 6) & ATMEL_TSADCC_PENDET)	|	/* Enable Pen Detect */
		(prsc << 8)				|
		((0x26 << 16) & ATMEL_TSADCC_STARTUP)	|
		((pdata->pendet_debounce << 28) & ATMEL_TSADCC_PENDBC);

	atmel_tsadcc_write(ATMEL_TSADCC_CR, ATMEL_TSADCC_SWRST);
	atmel_tsadcc_write(ATMEL_TSADCC_MR, reg);
	atmel_tsadcc_write(ATMEL_TSADCC_TRGR, ATMEL_TSADCC_TRGMOD_NONE);
	atmel_tsadcc_write(ATMEL_TSADCC_TSR,
		(pdata->ts_sample_hold_time << 24) & ATMEL_TSADCC_TSSHTIM);

	atmel_tsadcc_read(ATMEL_TSADCC_SR);
	atmel_tsadcc_write(ATMEL_TSADCC_IER, ATMEL_TSADCC_PENCNT);

	/* All went ok, so register to the input system */
	err = input_register_device(input_dev);
	if (err)
		goto err_fail;

	return 0;

err_fail:
	clk_disable(ts_dev->clk);
	clk_put(ts_dev->clk);
err_free_irq:
	free_irq(ts_dev->irq, ts_dev);
err_unmap_regs:
	iounmap(tsc_base);
err_release_mem:
	release_mem_region(res->start, resource_size(res));
err_free_dev:
	input_free_device(ts_dev->input);
err_free_mem:
	kfree(ts_dev);
	return err;
}

static int __devexit atmel_tsadcc_remove(struct platform_device *pdev)
{
	struct atmel_tsadcc *ts_dev = dev_get_drvdata(&pdev->dev);
	struct resource *res;

	free_irq(ts_dev->irq, ts_dev);

	input_unregister_device(ts_dev->input);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iounmap(tsc_base);
	release_mem_region(res->start, resource_size(res));

	clk_disable(ts_dev->clk);
	clk_put(ts_dev->clk);

	kfree(ts_dev);

	return 0;
}

static struct platform_driver atmel_tsadcc_driver = {
	.probe		= atmel_tsadcc_probe,
	.remove		= __devexit_p(atmel_tsadcc_remove),
	.driver		= {
		.name	= "atmel_tsadcc",
	},
};

static int __init atmel_tsadcc_init(void)
{
	return platform_driver_register(&atmel_tsadcc_driver);
}

static void __exit atmel_tsadcc_exit(void)
{
	platform_driver_unregister(&atmel_tsadcc_driver);
}

module_init(atmel_tsadcc_init);
module_exit(atmel_tsadcc_exit);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atmel TouchScreen Driver");
MODULE_AUTHOR("Dan Liang <dan.liang@atmel.com>");

