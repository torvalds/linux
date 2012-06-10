/*
 * Sun4i keypad driver
 *
 * Copyright (C) 2011 Allwinner Co.Ltd
 * Author: Aaron.maoye <leafy.myeh@allwinnertech.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/input/matrix_keypad.h>

#include <mach/clock.h>
#include <plat/sys_config.h>

//#define AW1623_FPGA
#define swkp_msg(...)       printk("[kpad]: "__VA_ARGS__);

/* register define */
#define SW_KP_PBASE         (0x01c23000)
#define SW_KP_CTL           (0x00)
#define SW_KP_TIMING       	(0x04)
#define SW_KP_INT_CFG      	(0x08)
#define SW_KP_INT_STA      	(0x0c)
#define SW_KP_IN0          	(0x10)
#define SW_KP_IN1          	(0x14)
#define SW_KP_DEB          	(0x18)

/* SW_KP_CTL */
#define SW_KPCTL_IFENB      (1)
#define SW_KPCTL_COLMASK    (0xff << 8)
#define SW_KPCTL_ROWMASK    (0xff << 16)

/* SW_KP_INT_CFG */
#define SW_KPINT_F_EN		(1 << 0)
#define SW_KPINT_R_EN		(1 << 1)

/* SW_KP_INT_STA */
#define SW_KP_PRESS         1
#define SW_KP_RELEASE       2
#define SW_KP_PRESS_RELEASE 3

#define SW_MAX_ROWS        8
#define SW_MAX_COLS        8

struct sw_keypad {
	struct input_dev        *input_dev;
	struct platform_device  *pdev;
	struct clk              *pclk;
	struct clk              *mclk;
	void __iomem            *base;
	u32                 mod_clk;
	u32                 pio_hdle;
	int                 irq;
	unsigned int        row_shift;
	unsigned int        rows;
	unsigned int        cols;
	unsigned int        row_state[SW_MAX_COLS];
	unsigned short      keycodes[];
};

struct sw_keypad_platdata {
        const struct matrix_keymap_data *keymap_data;
        unsigned int rows;
        unsigned int cols;
        bool no_autorepeat;
};

static const uint32_t sw_keymap[] = {
    KEY(0, 0, KEY_1),  KEY(0, 1, KEY_2),  KEY(0, 2, KEY_3),  KEY(0, 3, KEY_4),  KEY(0, 4, KEY_5),  KEY(0, 5, KEY_6),  KEY(0, 6, KEY_7),  KEY(0, 7, KEY_8),
    KEY(1, 0, KEY_A),  KEY(1, 1, KEY_B),  KEY(1, 2, KEY_C),  KEY(1, 3, KEY_D),  KEY(1, 4, KEY_E),  KEY(1, 5, KEY_F),  KEY(1, 6, KEY_G),  KEY(1, 7, KEY_H),
    KEY(2, 0, KEY_A),  KEY(2, 1, KEY_B),  KEY(2, 2, KEY_C),  KEY(2, 3, KEY_D),  KEY(2, 4, KEY_E),  KEY(2, 5, KEY_F),  KEY(2, 6, KEY_G),  KEY(2, 7, KEY_H),
    KEY(3, 0, KEY_A),  KEY(3, 1, KEY_B),  KEY(3, 2, KEY_C),  KEY(3, 3, KEY_D),  KEY(3, 4, KEY_E),  KEY(3, 5, KEY_F),  KEY(3, 6, KEY_G),  KEY(3, 7, KEY_H),
    KEY(4, 0, KEY_A),  KEY(4, 1, KEY_B),  KEY(4, 2, KEY_C),  KEY(4, 3, KEY_D),  KEY(4, 4, KEY_E),  KEY(4, 5, KEY_F),  KEY(4, 6, KEY_G),  KEY(4, 7, KEY_H),
    KEY(5, 0, KEY_A),  KEY(5, 1, KEY_B),  KEY(5, 2, KEY_C),  KEY(5, 3, KEY_D),  KEY(5, 4, KEY_E),  KEY(5, 5, KEY_F),  KEY(5, 6, KEY_G),  KEY(5, 7, KEY_H),
    KEY(6, 0, KEY_A),  KEY(6, 1, KEY_B),  KEY(6, 2, KEY_C),  KEY(6, 3, KEY_D),  KEY(6, 4, KEY_E),  KEY(6, 5, KEY_F),  KEY(6, 6, KEY_G),  KEY(6, 7, KEY_H),
    KEY(7, 0, KEY_F1), KEY(7, 1, KEY_F2), KEY(7, 2, KEY_F3), KEY(7, 3, KEY_F4), KEY(7, 3, KEY_F4), KEY(7, 5, KEY_F),  KEY(7, 6, KEY_G),  KEY(7, 7, KEY_H),
};

static struct matrix_keymap_data sw_keymap_data = {
        .keymap         = sw_keymap,
        .keymap_size    = ARRAY_SIZE(sw_keymap),
};

static struct sw_keypad_platdata sw_keypad_data = {
        .keymap_data    = &sw_keymap_data,
        .rows           = 8,
        .cols           = 8,
};

static int kp_used = 0;

static int sw_keypad_gpio_request(struct sw_keypad *keypad)
{
    #ifndef AW1623_FPGA
	keypad->pio_hdle = gpio_request_ex("keypad_para", NULL);
    if (!keypad->pio_hdle)
    {
        swkp_msg("request pio parameter failed\n");
        return -1;
    }
    #else
    {
        #include <mach/platform.h>
        void __iomem* pi_cfg0 = (void __iomem*)(SW_VA_PORTC_IO_BASE+0x120);
        void __iomem* pi_cfg1 = (void __iomem*)(SW_VA_PORTC_IO_BASE+0x124);

        writel(0x22222222, pi_cfg0);
        writel(0x22222222, pi_cfg1);
    }
    #endif
    return 0;
}

static void sw_keypad_gpio_release(struct sw_keypad *keypad)
{
    gpio_release(keypad->pio_hdle, 1);
    keypad->pio_hdle = 0;
}

static void sw_keypad_scan(struct sw_keypad *keypad, unsigned int *row_state)
{
	unsigned int col;
	unsigned int val;
    u32 kp_iflag0 = readl(keypad->base + SW_KP_IN0);
    u32 kp_iflag1 = readl(keypad->base + SW_KP_IN1);

    //swkp_msg("scan key status, st0 %08x, st1 %08x\n", kp_iflag0, kp_iflag1);
    //recode row information
    for (col = 0; col < keypad->cols; col++) {
		if (col < 4)
		    val = kp_iflag0 >> (col << 3);
		else
		    val = kp_iflag1 >> ((col - 4) << 3);;
		row_state[col] = (~val) & ((1 << keypad->rows) - 1);
		//swkp_msg("rowstat[%d] %02x, keypad->row_state[%d] %02x, change %d\n", col, row_state[col], col, keypad->row_state[col], row_state[col] ^ keypad->row_state[col]);
	}
}

static bool sw_keypad_report(struct sw_keypad *keypad, unsigned int *row_state, unsigned action)
{
	struct input_dev *input_dev = keypad->input_dev;
	unsigned int changed;
	unsigned int key_down = 0;
	unsigned int val;
	unsigned int col, row;

    //swkp_msg("action %d\n", action);
    for (col = 0; col < keypad->cols; col++) {
        if (action & SW_KP_RELEASE) {
            for (row = 0; row < keypad->rows; row++) {
                if (row_state[col] & (1 << row)) {
                    swkp_msg("key %dx%d, up(all up)\n", row, col);
                    val = MATRIX_SCAN_CODE(row, col, keypad->row_shift);
                    input_event(input_dev, EV_MSC, MSC_SCAN, val);
        			input_report_key(input_dev, keypad->keycodes[val], 0);
                }
            }
            keypad->row_state[col] = 0;
            //swkp_msg("clr old sta keypad->row_state[%d] %02x\n", col, keypad->row_state[col]);
        } else if (action == SW_KP_PRESS) {
            changed = row_state[col] ^ keypad->row_state[col];
            if (changed) {
                //swkp_msg("col %d changed\n", col);
                for (row = 0; row < keypad->rows; row++) {
                    u32 cur = (row_state[col] >> row) & 1;
                    u32 last = (keypad->row_state[col] >> row) & 1;
                    u32 press = 0;

                    //swkp_msg("%d x %d last %d cur %d\n", row, col, last, cur);

                    if (last && !cur) {                 //1 --> 0
                        press = 0;
                    } else if (!last && cur) {          //0 --> 1
                        press = 1;
                    } else {
                        continue;
                    }

                    swkp_msg("key %dx%d, %s\n", row, col, press ? "down" : "up");
                    val = MATRIX_SCAN_CODE(row, col, keypad->row_shift);
                    input_event(input_dev, EV_MSC, MSC_SCAN, val);
        			input_report_key(input_dev, keypad->keycodes[val], press);
                }
            }
            keypad->row_state[col] = row_state[col];
        }
        input_sync(keypad->input_dev);
    }

	return key_down;
}

static irqreturn_t sw_keypad_irq(int irq, void *dev_id)
{
	struct sw_keypad *keypad = dev_id;
	unsigned int row_state[SW_MAX_COLS];
	unsigned int val;
	bool key_down;


	val = readl(keypad->base + SW_KP_INT_STA);

	sw_keypad_scan(keypad, row_state);

	key_down = sw_keypad_report(keypad, row_state, val);

	writel(val, keypad->base + SW_KP_INT_STA);

	return IRQ_HANDLED;
}

static int sw_keypad_set_mclk(struct sw_keypad *keypad, u32 mod_clk)
{
    struct clk* sclk = NULL;
    int ret;

    sclk = clk_get(&keypad->pdev->dev, "hosc");
    if (IS_ERR(sclk))
    {
        ret = PTR_ERR(sclk);
        swkp_msg("Error to get source clock hosc\n");
        return ret;
    }

    clk_set_parent(keypad->mclk, sclk);
    clk_set_rate(keypad->mclk, mod_clk);
    clk_enable(keypad->mclk);

    #ifdef AW1623_FPGA
    keypad->mod_clk = 24000000;//fpga
    #else
    keypad->mod_clk = clk_get_rate(keypad->mclk);
    #endif

    clk_put(sclk);

    return 0;
}

static void sw_keypad_start(struct sw_keypad *keypad)
{
    swkp_msg("sw keypad start\n");

    sw_keypad_set_mclk(keypad, 1000000);
	clk_enable(keypad->pclk);

	/* Enable interrupt bits. */
    writel(SW_KPINT_F_EN|SW_KPINT_R_EN, keypad->base + SW_KP_INT_CFG);
    writel(SW_KPCTL_IFENB, keypad->base + SW_KP_CTL);

    enable_irq(keypad->irq);
}

static void sw_keypad_stop(struct sw_keypad *keypad)
{
    swkp_msg("sw keypad stop\n");

	disable_irq(keypad->irq);
	clk_disable(keypad->mclk);
	clk_disable(keypad->pclk);
}


static int sw_keypad_open(struct input_dev *input_dev)
{
	struct sw_keypad *keypad = input_get_drvdata(input_dev);

    swkp_msg("sw keypad open\n");
	sw_keypad_start(keypad);

	return 0;
}

static void sw_keypad_close(struct input_dev *input_dev)
{
	struct sw_keypad *keypad = input_get_drvdata(input_dev);

    swkp_msg("sw keypad close\n");
	sw_keypad_stop(keypad);
}

static int __devinit sw_keypad_probe(struct platform_device *pdev)
{
	const struct sw_keypad_platdata *pdata;
	const struct matrix_keymap_data *keymap_data;
	struct sw_keypad *keypad;
	struct resource *res;
	struct input_dev *input_dev;
	unsigned int row_shift;
	unsigned int keymap_size;
	int error;

    swkp_msg("sw keypad probe\n");

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		swkp_msg("no platform data defined\n");
		return -EINVAL;
	}

	keymap_data = pdata->keymap_data;

	if (!pdata->rows || pdata->rows > SW_MAX_ROWS)
		return -EINVAL;

	if (!pdata->cols || pdata->cols > SW_MAX_COLS)
		return -EINVAL;

	row_shift = get_count_order(pdata->cols);
	keymap_size = (pdata->rows << row_shift) * sizeof(keypad->keycodes[0]);

	keypad = kzalloc(sizeof(*keypad) + keymap_size, GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!keypad || !input_dev) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	/* initialize the gpio */
	if (sw_keypad_gpio_request(keypad))
	{
	    error = -ENODEV;
		goto err_free_mem;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		error = -ENODEV;
		goto err_free_gpio;
	}

	keypad->base = ioremap(res->start, resource_size(res));
	if (!keypad->base) {
		error = -EBUSY;
		goto err_free_gpio;
	}
	keypad->pclk = clk_get(&pdev->dev, "apb_key_pad");
	if (IS_ERR(keypad->pclk)) {
		swkp_msg("failed to get keypad hclk\n");
		error = PTR_ERR(keypad->pclk);
		goto err_unmap_base;
	}

	keypad->mclk = clk_get(&pdev->dev, "key_pad");
	if (IS_ERR(keypad->mclk)) {
		swkp_msg("failed to get keypad mclk\n");
		error = PTR_ERR(keypad->mclk);
		goto err_put_pclk;
	}

	keypad->input_dev = input_dev;
	keypad->row_shift = row_shift;
	keypad->rows = pdata->rows;
	keypad->cols = pdata->cols;

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->dev.parent = &pdev->dev;
	input_set_drvdata(input_dev, keypad);

	input_dev->open = sw_keypad_open;
	input_dev->close = sw_keypad_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	if (!pdata->no_autorepeat)
		input_dev->evbit[0] |= BIT_MASK(EV_REP);

	input_set_capability(input_dev, EV_MSC, MSC_SCAN);

	input_dev->keycode = keypad->keycodes;
	input_dev->keycodesize = sizeof(keypad->keycodes[0]);
	input_dev->keycodemax = pdata->rows << row_shift;

	matrix_keypad_build_keymap(keymap_data, row_shift, input_dev->keycode, input_dev->keybit);

	keypad->irq = platform_get_irq(pdev, 0);
	if (keypad->irq < 0) {
		error = keypad->irq;
		goto err_put_mclk;
	}

	error = request_irq(keypad->irq, sw_keypad_irq, 0, dev_name(&pdev->dev), keypad);
	if (error) {
		swkp_msg("failed to register keypad interrupt\n");
		goto err_put_mclk;
	}
    disable_irq(keypad->irq);

	error = input_register_device(keypad->input_dev);
	if (error)
		goto err_free_irq;

	platform_set_drvdata(pdev, keypad);
	keypad->pdev = pdev;

	swkp_msg("sw keypad probe done, base %p, irq %d\n", keypad->base, keypad->irq);
	return 0;

err_free_irq:
	free_irq(keypad->irq, keypad);
err_put_mclk:
	clk_put(keypad->mclk);
err_put_pclk:
	clk_put(keypad->pclk);
err_unmap_base:
	iounmap(keypad->base);
err_free_gpio:
    gpio_release(keypad->pio_hdle, 1);
    keypad->pio_hdle = 0;
err_free_mem:
	input_free_device(input_dev);
	kfree(keypad);

	return error;
}

static int __devexit sw_keypad_remove(struct platform_device *pdev)
{
	struct sw_keypad *keypad = platform_get_drvdata(pdev);

    swkp_msg("sw keypad remove\n");

	platform_set_drvdata(pdev, NULL);

	input_unregister_device(keypad->input_dev);

	free_irq(keypad->irq, keypad);
	clk_put(keypad->pclk);
	clk_put(keypad->mclk);
	iounmap(keypad->base);

    sw_keypad_gpio_release(keypad);
	kfree(keypad);
	return 0;
}


#ifdef CONFIG_PM

static int sw_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sw_keypad *keypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = keypad->input_dev;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		sw_keypad_stop(keypad);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int sw_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sw_keypad *keypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = keypad->input_dev;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		sw_keypad_start(keypad);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static const struct dev_pm_ops sw_keypad_pm_ops = {
	.suspend	= sw_keypad_suspend,
	.resume		= sw_keypad_resume,
};
#endif

static struct resource sw_keypad_resources[] = {
        [0] = {
                .start  = SW_KP_PBASE,
                .end    = SW_KP_PBASE + 0x400 - 1,
                .flags  = IORESOURCE_MEM,
        },
        [1] = {
                .start  = SW_INT_IRQNO_KEYPAD,
                .end    = SW_INT_IRQNO_KEYPAD,
                .flags  = IORESOURCE_IRQ,
        },
};

static struct platform_device sw_device_keypad = {
        .name           = "sw-keypad",
        .id             = -1,
        .num_resources  = ARRAY_SIZE(sw_keypad_resources),
        .resource       = sw_keypad_resources,
        .dev.platform_data = &sw_keypad_data,
};

static struct platform_driver sw_keypad_driver = {
	.probe		= sw_keypad_probe,
	.remove		= __devexit_p(sw_keypad_remove),
	.driver		= {
		.name	= "sw-keypad",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &sw_keypad_pm_ops,
#endif
	},
};

static int __init sw_keypad_init(void)
{
    int ret;

    swkp_msg("sw keypad init\n");
    kp_used  = 0;
    ret = script_parser_fetch("keypad_para", "ke_used", &kp_used, sizeof(int));
    if (ret)
    {
        printk("sw keypad fetch keypad uning configuration failed\n");
    }
    if (kp_used)
    {
        platform_device_register(&sw_device_keypad);
        return platform_driver_register(&sw_keypad_driver);
    }
    else
    {
        pr_warning("keypad: cannot find using configuration, return without doing anything!\n");
        return 0;
    }
}
module_init(sw_keypad_init);

static void __exit sw_keypad_exit(void)
{
    swkp_msg("sw keypad exit\n");
    if (kp_used)
    {
        kp_used = 0;
        platform_driver_unregister(&sw_keypad_driver);
    }
}
module_exit(sw_keypad_exit);

MODULE_DESCRIPTION("SW keypad driver");
MODULE_AUTHOR("Aaron.maoye<leafy.myeh@allwinnertech.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sw-keypad");
