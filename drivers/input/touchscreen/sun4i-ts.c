/* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
*
* Copyright (c) 2009
*
* ChangeLog
*
*
*/
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/tick.h>
#include <asm-generic/cputime.h>
#include <mach/irqs.h>
#include <mach/system.h>
#include <mach/hardware.h>
#include <plat/sys_config.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
    #include <linux/pm.h>
    #include <linux/earlysuspend.h>
#endif

#define IRQ_TP                 (29)
#define TP_BASSADDRESS         (0xf1c25000)
#define TP_CTRL0               (0x00)
#define TP_CTRL1               (0x04)
#define TP_CTRL2               (0x08)
#define TP_CTRL3               (0x0c)
#define TP_INT_FIFOC           (0x10)
#define TP_INT_FIFOS           (0x14)
#define TP_TPR                 (0x18)
#define TP_CDAT                (0x1c)
#define TEMP_DATA              (0x20)
#define TP_DATA                (0x24)


#define ADC_FIRST_DLY          (0x1<<24)
#define ADC_FIRST_DLY_MODE     (0x1<<23)
#define ADC_CLK_SELECT         (0x0<<22)
#define ADC_CLK_DIVIDER        (0x2<<20)
//#define CLK                    (6)
#define CLK                    (7)
#define FS_DIV                 (CLK<<16)
#define ACQ                    (0x3f)
#define T_ACQ                  (ACQ)

#define STYLUS_UP_DEBOUNCE     (5<<12)
#define STYLUS_UP_DEBOUCE_EN   (1<<9)
#define TOUCH_PAN_CALI_EN      (1<<6)
#define TP_DUAL_EN             (1<<5)
#define TP_MODE_EN             (1<<4)
#define TP_ADC_SELECT          (0<<3)
#define ADC_CHAN_SELECT        (0)

#define TP_SENSITIVE_ADJUST    (tp_sensitive_level<<28)       //mark by young for test angda 5" 0xc
#define TP_MODE_SELECT         (0x0<<26)
#define PRE_MEA_EN             (0x1<<24)
#define PRE_MEA_THRE_CNT       (tp_press_threshold<<0)         //0x1f40


#define FILTER_EN              (1<<2)
#define FILTER_TYPE            (0x01<<0)

#define TP_DATA_IRQ_EN         (1<<16)
#define TP_DATA_XY_CHANGE      (tp_exchange_x_y<<13)       //tp_exchange_x_y
#define TP_FIFO_TRIG_LEVEL     (1<<8)
#define TP_FIFO_FLUSH          (1<<4)
#define TP_UP_IRQ_EN           (1<<1)
#define TP_DOWN_IRQ_EN         (1<<0)

#define FIFO_DATA_PENDING      (1<<16)
#define TP_UP_PENDING          (1<<1)
#define TP_DOWN_PENDING        (1<<0)

struct sun4i_ts_data {
	struct resource *res;
	struct input_dev *input;
	void __iomem *base_addr;
	int irq;
	char phys[32];
	int ignore_fifo_data;
#ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend early_suspend;
#endif
};

//config from sysconfig files.
static int tp_press_threshold_enable = 0;
static int tp_press_threshold = 0; //usded to adjust sensitivity of touch
static int tp_sensitive_level = 0; //used to adjust sensitivity of pen down detection
static int tp_exchange_x_y = 0;

//停用设备
#ifdef CONFIG_HAS_EARLYSUSPEND
static void sun4i_ts_suspend(struct early_suspend *h)
{
	/*int ret;
	struct sun4i_ts_data *ts = container_of(h, struct sun4i_ts_data, early_suspend);
    */
    #ifdef PRINT_SUSPEND_INFO
        printk("enter earlysuspend: sun4i_ts_suspend. \n");
    #endif
    writel(0,TP_BASSADDRESS + TP_CTRL1);
	return ;
}

//重新唤醒
static void sun4i_ts_resume(struct early_suspend *h)
{
	/*int ret;
	struct sun4i_ts_data *ts = container_of(h, struct sun4i_ts_data, early_suspend);
    */
    #ifdef PRINT_SUSPEND_INFO
        printk("enter laterresume: sun4i_ts_resume. \n");
    #endif
    writel(STYLUS_UP_DEBOUNCE|STYLUS_UP_DEBOUCE_EN|TP_MODE_EN,TP_BASSADDRESS + TP_CTRL1);
	return ;
}
#else
//停用设备
#ifdef CONFIG_PM
static int sun4i_ts_suspend(struct platform_device *pdev, pm_message_t state)
{
    #ifdef PRINT_SUSPEND_INFO
        printk("enter: sun4i_ts_suspend. \n");
    #endif

    writel(0,TP_BASSADDRESS + TP_CTRL1);
	return 0;
}

static int sun4i_ts_resume(struct platform_device *pdev)
{
    #ifdef PRINT_SUSPEND_INFO
        printk("enter: sun4i_ts_resume. \n");
    #endif

    writel(STYLUS_UP_DEBOUNCE|STYLUS_UP_DEBOUCE_EN|TP_MODE_EN,TP_BASSADDRESS + TP_CTRL1);
	return 0;
}
#endif

#endif


static int  tp_init(void)
{
    //      Hosc clk | clkin: clk/6 | adc sample freq: clkin / 8192 | t_acq clkin / (16 * 48)
    //      0x0<<22 | 0x2<<20 | 7<<16 | 0x3f
    writel(ADC_CLK_DIVIDER|FS_DIV|T_ACQ, TP_BASSADDRESS + TP_CTRL0);

    //TP_CTRL2: 0xc4000000
    if(1 == tp_press_threshold_enable){
        writel(TP_SENSITIVE_ADJUST |TP_MODE_SELECT | PRE_MEA_EN | PRE_MEA_THRE_CNT, TP_BASSADDRESS + TP_CTRL2);
    }
    else{
        writel(TP_SENSITIVE_ADJUST|TP_MODE_SELECT,TP_BASSADDRESS + TP_CTRL2);
    }


    //TP_CTRL3: 0x05
    writel(FILTER_EN|FILTER_TYPE,TP_BASSADDRESS + TP_CTRL3);

    #ifdef TP_TEMP_DEBUG
        //TP_INT_FIFOC: 0x00010313
        writel(TP_DATA_IRQ_EN|TP_FIFO_TRIG_LEVEL|TP_FIFO_FLUSH|TP_UP_IRQ_EN|0x40000, TP_BASSADDRESS + TP_INT_FIFOC);
        writel(0x10fff, TP_BASSADDRESS + TP_TPR);
    #else
        //TP_INT_FIFOC: 0x00010313
        writel(TP_DATA_IRQ_EN|TP_FIFO_TRIG_LEVEL|TP_FIFO_FLUSH|TP_UP_IRQ_EN, TP_BASSADDRESS + TP_INT_FIFOC);
    #endif
    //TP_CTRL1: 0x00000070 -> 0x00000030

    writel(TP_DATA_XY_CHANGE|STYLUS_UP_DEBOUNCE|STYLUS_UP_DEBOUCE_EN|TP_MODE_EN,TP_BASSADDRESS + TP_CTRL1);

    return (0);
}

static irqreturn_t sun4i_isr_tp(int irq, void *dev_id)
{
	struct sun4i_ts_data *ts_data = dev_id;
	u32 reg_val;
	u32 x, y;

	reg_val  = readl(TP_BASSADDRESS + TP_INT_FIFOS);

	if (reg_val & FIFO_DATA_PENDING) {
		x = readl(TP_BASSADDRESS + TP_DATA);
		y = readl(TP_BASSADDRESS + TP_DATA);
		/* The 1st location reported after an up event is unreliable */
		if (!ts_data->ignore_fifo_data) {
			/* pr_err("motion: %dx%d\n", x, y); */
			input_report_abs(ts_data->input, ABS_X, x);
			input_report_abs(ts_data->input, ABS_Y, y);
			/*
			 * The hardware has a separate down status bit, but
			 * that gets set before we get the first location,
			 * resulting in reporting a click on the old location.
			 */
			input_report_key(ts_data->input, BTN_TOUCH, 1);
			input_sync(ts_data->input);
		} else
			ts_data->ignore_fifo_data = 0;
	}

	if (reg_val & TP_UP_PENDING) {
		/* pr_err("up\n"); */
		ts_data->ignore_fifo_data = 1;
		input_report_key(ts_data->input, BTN_TOUCH, 0);
		input_sync(ts_data->input);
	}

        writel(reg_val, TP_BASSADDRESS + TP_INT_FIFOS);

	return IRQ_HANDLED;
}

static struct sun4i_ts_data *sun4i_ts_data_alloc(struct platform_device *pdev)
{

	struct sun4i_ts_data *ts_data = kzalloc(sizeof(*ts_data), GFP_KERNEL);

	if (!ts_data)
		return NULL;

	ts_data->input = input_allocate_device();
	if (!ts_data->input) {
		kfree(ts_data);
		return NULL;
	}


	ts_data->input->evbit[0] =  BIT(EV_SYN) | BIT(EV_KEY) | BIT(EV_ABS);
	set_bit(BTN_TOUCH, ts_data->input->keybit);

    input_set_abs_params(ts_data->input, ABS_X, 0, 4095, 0, 0);
    input_set_abs_params(ts_data->input, ABS_Y, 0, 4095, 0, 0);

	ts_data->input->name = pdev->name;
	ts_data->input->phys = "sun4i_ts/input0";
	ts_data->input->id.bustype = BUS_HOST ;
	ts_data->input->id.vendor = 0x0001;
	ts_data->input->id.product = 0x0001;
	ts_data->input->id.version = 0x0100;
	ts_data->input->dev.parent = &pdev->dev;

	return ts_data;
}




static void sun4i_ts_data_free(struct sun4i_ts_data *ts_data)
{
	if (!ts_data)
		return;
	if (ts_data->input)
		input_free_device(ts_data->input);
	kfree(ts_data);
}


static int __devinit sun4i_ts_probe(struct platform_device *pdev)
{
	int err =0;
	int irq = platform_get_irq(pdev, 0);
	struct sun4i_ts_data *ts_data;

    #ifdef CONFIG_TOUCHSCREEN_SUN4I_DEBUG
	    printk( "sun4i-ts.c: sun4i_ts_probe: start...\n");
	#endif

	ts_data = sun4i_ts_data_alloc(pdev);
	if (!ts_data) {
		dev_err(&pdev->dev, "Cannot allocate driver structures\n");
		err = -ENOMEM;
		goto err_out;
	}

	ts_data->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ts_data->res) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Can't get the MEMORY\n");
		goto err_out1;
	}

    ts_data->base_addr = (void __iomem *)TP_BASSADDRESS;

	ts_data->irq = irq;
	err = request_irq(irq, sun4i_isr_tp,
		IRQF_DISABLED, pdev->name, ts_data);
	if (err) {
		dev_err(&pdev->dev, "Cannot request keypad IRQ\n");
		goto err_out2;
	}

	ts_data->ignore_fifo_data = 1;

	platform_set_drvdata(pdev, ts_data);

	//printk("Input request \n");
	/* All went ok, so register to the input system */
	err = input_register_device(ts_data->input);
	if (err)
		goto err_out3;

	#ifdef CONFIG_TOUCHSCREEN_SUN4I_DEBUG
        printk("tp init\n");
    #endif

    tp_init();

    #ifdef CONFIG_TOUCHSCREEN_SUN4I_DEBUG
	    printk( "sun4i-ts.c: sun4i_ts_probe: end\n");
    #endif

#ifdef CONFIG_HAS_EARLYSUSPEND
    printk("==register_early_suspend =\n");
    ts_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    ts_data->early_suspend.suspend = sun4i_ts_suspend;
    ts_data->early_suspend.resume	= sun4i_ts_resume;
    register_early_suspend(&ts_data->early_suspend);
#endif

    return 0;

 err_out3:
	if (ts_data->irq)
		free_irq(ts_data->irq, pdev);
err_out2:
err_out1:
	sun4i_ts_data_free(ts_data);
err_out:
    #ifdef CONFIG_TOUCHSCREEN_SUN4I_DEBUG
	    printk( "sun4i-ts.c: sun4i_ts_probe: failed!\n");
	#endif

	return err;
}

static int __devexit sun4i_ts_remove(struct platform_device *pdev)
{

	struct sun4i_ts_data *ts_data = platform_get_drvdata(pdev);
	#ifdef CONFIG_HAS_EARLYSUSPEND
	    unregister_early_suspend(&ts_data->early_suspend);
	#endif
	input_unregister_device(ts_data->input);
	free_irq(ts_data->irq, pdev);
	sun4i_ts_data_free(ts_data);
	platform_set_drvdata(pdev, NULL);
        //cancle tasklet?
	return 0;
}


static struct platform_driver sun4i_ts_driver = {
	.probe		= sun4i_ts_probe,
	.remove		= __devexit_p(sun4i_ts_remove),
#ifdef CONFIG_HAS_EARLYSUSPEND

#else
#ifdef CONFIG_PM
	.suspend	= sun4i_ts_suspend,
	.resume		= sun4i_ts_resume,
#endif
#endif
	.driver		= {
		.name	= "sun4i-ts",
	},
};


static void sun4i_ts_nop_release(struct device *dev)
{
	/* Nothing */
}

static struct resource sun4i_ts_resource[] = {
	{
	.flags  = IORESOURCE_IRQ,
	.start  = SW_INT_IRQNO_TOUCH_PANEL ,
	.end    = SW_INT_IRQNO_TOUCH_PANEL ,
	},

	{
	.flags	= IORESOURCE_MEM,
	.start	= TP_BASSADDRESS,
	.end	= TP_BASSADDRESS + 0x100-1,
	},
};

struct platform_device sun4i_ts_device = {
	.name		= "sun4i-ts",
	.id		    = -1,
	.dev = {
		.release = sun4i_ts_nop_release,
		},
	.resource	= sun4i_ts_resource,
	.num_resources	= ARRAY_SIZE(sun4i_ts_resource),
};


static int __init sun4i_ts_init(void)
{
  int device_used = 0;
  int ret = -1;

#ifdef CONFIG_TOUCHSCREEN_SUN4I_DEBUG
	    printk("sun4i-ts.c: sun4i_ts_init: start ...\n");
#endif

	//config rtp
	if(SCRIPT_PARSER_OK != script_parser_fetch("rtp_para", "rtp_used", &device_used, sizeof(device_used)/sizeof(int))){
	    pr_err("sun4i_ts_init: script_parser_fetch err. \n");
	    goto script_parser_fetch_err;
	}
	printk("rtp_used == %d. \n", device_used);
	if(1 == device_used){

            if(SCRIPT_PARSER_OK != script_parser_fetch("rtp_para", "rtp_press_threshold_enable", &tp_press_threshold_enable, 1)){
                pr_err("sun4i_ts_init: script_parser_fetch err rtp_press_threshold_enable. \n");
                goto script_parser_fetch_err;
            }
            printk("sun4i-ts: tp_press_threshold_enable is %d.\n", tp_press_threshold_enable);

            if(0 != tp_press_threshold_enable  && 1 != tp_press_threshold_enable){
                printk("sun4i-ts: only tp_press_threshold_enable  0 or 1  is supported. \n");
                goto script_parser_fetch_err;
            }

            if(1 == tp_press_threshold_enable){
                if(SCRIPT_PARSER_OK != script_parser_fetch("rtp_para", "rtp_press_threshold", &tp_press_threshold, 1)){
                    pr_err("sun4i_ts_init: script_parser_fetch err rtp_press_threshold. \n");
                    goto script_parser_fetch_err;
                }
                printk("sun4i-ts: rtp_press_threshold is %d.\n", tp_press_threshold);

                if(tp_press_threshold < 0 || tp_press_threshold > 0xFFFFFF){
                    printk("sun4i-ts: only tp_regidity_level between 0 and 0xFFFFFF  is supported. \n");
                    goto script_parser_fetch_err;
                }
            }

            if(SCRIPT_PARSER_OK != script_parser_fetch("rtp_para", "rtp_sensitive_level", &tp_sensitive_level, 1)){
                pr_err("sun4i_ts_init: script_parser_fetch err rtp_sensitive_level. \n");
                goto script_parser_fetch_err;
            }
            printk("sun4i-ts: rtp_sensitive_level is %d.\n", tp_sensitive_level);

            if(tp_sensitive_level < 0 || tp_sensitive_level > 0xf){
                printk("sun4i-ts: only tp_regidity_level between 0 and 0xf  is supported. \n");
                goto script_parser_fetch_err;
            }

            if(SCRIPT_PARSER_OK != script_parser_fetch("rtp_para", "rtp_exchange_x_y_flag", &tp_exchange_x_y, 1)){
                pr_err("sun4i_ts_init: script_parser_fetch err rtp_exchange_x_y_flag. \n");
                goto script_parser_fetch_err;
            }
            printk("sun4i-ts: rtp_exchange_x_y_flag is %d.\n", tp_exchange_x_y);

            if(0 != tp_exchange_x_y && 1 != tp_exchange_x_y){
                printk("sun4i-ts: only tp_exchange_x_y==1 or  tp_exchange_x_y==0 is supported. \n");
                goto script_parser_fetch_err;
            }

	}else{
		goto script_parser_fetch_err;
	}

	platform_device_register(&sun4i_ts_device);
	ret = platform_driver_register(&sun4i_ts_driver);

script_parser_fetch_err:
	return ret;
}

static void __exit sun4i_ts_exit(void)
{
	platform_driver_unregister(&sun4i_ts_driver);
	platform_device_unregister(&sun4i_ts_device);

}

module_init(sun4i_ts_init);
module_exit(sun4i_ts_exit);

MODULE_AUTHOR("zhengdixu <@>");
MODULE_DESCRIPTION("sun4i touchscreen driver");
MODULE_LICENSE("GPL");

