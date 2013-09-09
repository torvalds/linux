//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C Touchscreen driver
//  2012.01.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/device.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <asm/unaligned.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_HAS_EARLYSUSPEND)
	#include <linux/wakelock.h>
	#include <linux/earlysuspend.h>
	#include <linux/suspend.h>
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/input/mt.h>
#include <linux/input/touch-pdata.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#include "touch.h"
#include "touch-i2c.h"
#include "touch-sysfs.h"

//[*]--------------------------------------------------------------------------------------------------[*]
//#define	DEBUG_TOUCH

//[*]--------------------------------------------------------------------------------------------------[*]
// function prototype define
//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef	CONFIG_HAS_EARLYSUSPEND
	static	void	touch_suspend			(struct early_suspend *h);
	static	void	touch_resume			(struct early_suspend *h);
#endif

irqreturn_t 	touch_irq					(int irq, void *handle);
static 	void	touch_work					(struct touch *ts);
static 	void 	touch_work_q				(struct work_struct *work);

static 	void	touch_key_report			(struct touch *ts, unsigned char button_data);
static	void 	touch_report_protocol_a		(struct touch *ts);
static	void 	touch_report_protocol_b		(struct touch *ts);

static	void	touch_event_clear			(struct	touch *ts);
static	void	touch_enable				(struct touch *ts);
static	void	touch_disable				(struct touch *ts);
static	void 	touch_input_close			(struct input_dev *input);
static	int		touch_input_open			(struct input_dev *input);
static	int		touch_check_functionality	(struct touch_pdata *pdata);
static  void    touch_dummy_mode            (struct touch *ts);

		void 	touch_hw_reset				(struct touch *ts);
		int		touch_info_display			(struct touch *ts);
		int		touch_probe					(struct i2c_client *client);
		int 	touch_remove				(struct device *dev);
		
//[*]--------------------------------------------------------------------------------------------------[*]
irqreturn_t 	touch_irq			(int irq, void *handle)
{
	struct touch 	*ts = handle;

	if(ts->pdata->irq_mode)		queue_work(ts->work_queue, &ts->work);	// normal mode (work q used)
	else						ts->pdata->touch_work(ts);				// thread mode

	return IRQ_HANDLED;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	void	touch_work			(struct touch *ts)
{
	printk("error : undefined touch work function!!\n");
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	void 	touch_work_q		(struct work_struct *work)
{
	struct touch 	*ts = container_of(work, struct touch, work);
	
	ts->pdata->touch_work(ts);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	void	touch_key_report	(struct touch *ts, unsigned char button_data)
{
	static	button_u	button_old; 
			button_u	button_new;

	button_new.ubyte = button_data;
	if(button_old.ubyte != button_new.ubyte)	{
		if((button_old.bits.bt0_press != button_new.bits.bt0_press) && (ts->pdata->keycnt > 0))	{
			if(button_new.bits.bt0_press)	input_report_key(ts->input, ts->pdata->keycode[0], true);
			else							input_report_key(ts->input, ts->pdata->keycode[0], false);
			#if defined(DEBUG_TOUCH_KEY)
				printk("keycode[0](0x%04X) %s\n", ts->pdata->keycode[0], button_new.bits.bt0_press ? "press":"release");
			#endif
		}
		if((button_old.bits.bt1_press != button_new.bits.bt1_press) && (ts->pdata->keycnt > 1))	{
			if(button_new.bits.bt1_press)	input_report_key(ts->input, ts->pdata->keycode[1], true);
			else							input_report_key(ts->input, ts->pdata->keycode[1], false);
			#if defined(DEBUG_TOUCH_KEY)
				printk("keycode[1](0x%04X) %s\n", ts->pdata->keycode[1], button_new.bits.bt1_press ? "press":"release");
			#endif
		}
		if((button_old.bits.bt2_press != button_new.bits.bt2_press) && (ts->pdata->keycnt > 2))	{
			if(button_new.bits.bt2_press)	input_report_key(ts->input, ts->pdata->keycode[2], true);
			else							input_report_key(ts->input, ts->pdata->keycode[2], false);
			#if defined(DEBUG_TOUCH_KEY)
				printk("keycode[2](0x%04X) %s\n", ts->pdata->keycode[2], button_new.bits.bt2_press ? "press":"release");
			#endif
		}
		if((button_old.bits.bt3_press != button_new.bits.bt3_press) && (ts->pdata->keycnt > 3))	{
			if(button_new.bits.bt3_press)	input_report_key(ts->input, ts->pdata->keycode[3], true);
			else							input_report_key(ts->input, ts->pdata->keycode[3], false);
			#if defined(DEBUG_TOUCH_KEY)
				printk("keycode[3](0x%04X) %s\n", ts->pdata->keycode[3], button_new.bits.bt3_press ? "press":"release");
			#endif
		}
		if((button_old.bits.bt4_press != button_new.bits.bt4_press) && (ts->pdata->keycnt > 4))	{
			if(button_new.bits.bt4_press)	input_report_key(ts->input, ts->pdata->keycode[4], true);
			else							input_report_key(ts->input, ts->pdata->keycode[4], false);
			#if defined(DEBUG_TOUCH_KEY)
				printk("keycode[4](0x%04X) %s\n", ts->pdata->keycode[4], button_new.bits.bt4_press ? "press":"release");
			#endif
		}
		if((button_old.bits.bt5_press != button_new.bits.bt5_press) && (ts->pdata->keycnt > 5))	{
			if(button_new.bits.bt5_press)	input_report_key(ts->input, ts->pdata->keycode[5], true);
			else							input_report_key(ts->input, ts->pdata->keycode[5], false);
			#if defined(DEBUG_TOUCH_KEY)
				printk("keycode[5](0x%04X) %s\n", ts->pdata->keycode[5], button_new.bits.bt5_press ? "press":"release");
			#endif
		}
		if((button_old.bits.bt6_press != button_new.bits.bt6_press) && (ts->pdata->keycnt > 6))	{
			if(button_new.bits.bt6_press)	input_report_key(ts->input, ts->pdata->keycode[6], true);
			else							input_report_key(ts->input, ts->pdata->keycode[6], false);
			#if defined(DEBUG_TOUCH_KEY)
				printk("keycode[6](0x%04X) %s\n", ts->pdata->keycode[6], button_new.bits.bt6_press ? "press":"release");
			#endif
		}
		if((button_old.bits.bt7_press != button_new.bits.bt7_press) && (ts->pdata->keycnt > 7))	{
			if(button_new.bits.bt7_press)	input_report_key(ts->input, ts->pdata->keycode[7], true);
			else							input_report_key(ts->input, ts->pdata->keycode[7], false);
			#if defined(DEBUG_TOUCH_KEY)
				printk("keycode[7](0x%04X) %s\n", ts->pdata->keycode[7], button_new.bits.bt7_press ? "press":"release");
			#endif
		}
		button_old.ubyte = button_new.ubyte;
	}
}

//[*]--------------------------------------------------------------------------------------------------[*]
static  void    touch_report_single     (struct touch *ts)
{
	if(ts->finger[0].event == TS_EVENT_UNKNOWN)		return;
		
	if(ts->finger[0].event != TS_EVENT_RELEASE)	{
        // for single touch
        input_report_key(ts->input, BTN_TOUCH, 1);
        input_report_abs(ts->input, ABS_X, ts->finger[0].x);
        input_report_abs(ts->input, ABS_Y, ts->finger[0].y);

#if defined(DEBUG_TOUCH)
	printk("%s : id = %d, x = %d, y = %d\n", __func__, ts->finger[0].id, ts->finger[0].x, ts->finger[0].y);
#endif
	}
	else	{
	    // for single touch
        input_report_key(ts->input, BTN_TOUCH, 0);

		ts->finger[0].event = TS_EVENT_UNKNOWN;

#if defined(DEBUG_TOUCH)
	printk("%s : release id = %d\n", __func__, ts->finger[0].id);
#endif
	}
	input_sync(ts->input);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	void 	touch_report_protocol_a	(struct touch *ts)
{
	int		id;
	
	for(id = 0; id < ts->pdata->max_fingers; id++)	{

		if(ts->finger[id].event == TS_EVENT_UNKNOWN)		continue;
		
		if(ts->finger[id].event != TS_EVENT_RELEASE)	{
			if(ts->pdata->id_max)		input_report_abs(ts->input, ABS_MT_TRACKING_ID, ts->finger[id].id);
			if(ts->pdata->area_max)		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, ts->finger[id].area ? ts->finger[id].area : 10);
			if(ts->pdata->press_max)	input_report_abs(ts->input, ABS_MT_PRESSURE, ts->finger[id].pressure);
	
			input_report_abs(ts->input, ABS_MT_POSITION_X, 	ts->finger[id].x);
			input_report_abs(ts->input, ABS_MT_POSITION_Y,	ts->finger[id].y);

#if defined(DEBUG_TOUCH)
	printk("%s : id = %d, x = %d, y = %d\n", __func__, ts->finger[id].id, ts->finger[id].x, ts->finger[id].y);
#endif
		}
		else	{
			ts->finger[id].event = TS_EVENT_UNKNOWN;

#if defined(DEBUG_TOUCH)
	printk("%s : release id = %d\n", __func__, ts->finger[id].id);
#endif
		}

		input_mt_sync(ts->input);
	}

	input_sync(ts->input);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	void 	touch_report_protocol_b	(struct touch *ts)
{
	int		id;
	
	for(id = 0; id < ts->pdata->max_fingers; id++)	{

		if((ts->finger[id].event == TS_EVENT_UNKNOWN) || (ts->finger[id].status == false))	continue;

		input_mt_slot(ts->input, id);	ts->finger[id].status = false;	

		if(ts->finger[id].event != TS_EVENT_RELEASE)	{
			input_report_abs(ts->input, ABS_MT_TRACKING_ID, ts->finger[id].id);
			input_report_abs(ts->input, ABS_MT_POSITION_X, 	ts->finger[id].x);
			input_report_abs(ts->input, ABS_MT_POSITION_Y,	ts->finger[id].y);

			if(ts->pdata->area_max)		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, ts->finger[id].area ? ts->finger[id].area : 10);
			if(ts->pdata->press_max)	input_report_abs(ts->input, ABS_MT_PRESSURE, ts->finger[id].pressure);
	
#if defined(DEBUG_TOUCH)
    printk("%s : slot = %d, id = %d, x = %d, y = %d\n", __func__, id, ts->finger[id].id, ts->finger[id].x, ts->finger[id].y);
#endif
		}
		else	{
			ts->finger[id].event = TS_EVENT_UNKNOWN;
			input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);

#if defined(DEBUG_TOUCH)
	printk("%s : release slot = %d, id = %d\n", __func__, id, ts->finger[id].id);
#endif
		}
	}
	input_sync(ts->input);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	void	touch_event_clear	(struct	touch *ts)
{
	unsigned char	id;
	
	for(id = 0; id < ts->pdata->max_fingers; id++)	{
		if(ts->finger[id].event == TS_EVENT_MOVE)	{
			ts->finger[id].status	= true;
			ts->finger[id].event 	= TS_EVENT_RELEASE;
		}
	}
	ts->pdata->report(ts);	
	if(ts->pdata->keycode)		ts->pdata->key_report(ts, 0x00);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	void	touch_enable		(struct touch *ts)
{
	if(ts->disabled)		{
		if(ts->irq)		enable_irq(ts->irq);
		ts->disabled = false;
	}
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	void	touch_disable		(struct touch *ts)
{
	if(!ts->disabled)	{
		if(ts->irq)		disable_irq(ts->irq);
		ts->disabled = true;
	}
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		touch_input_open	(struct input_dev *input)
{
	struct touch 	*ts = input_get_drvdata(input);

	ts->pdata->enable(ts);

	printk("%s\n", __func__);
	
	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	void 	touch_input_close	(struct input_dev *input)
{
	struct touch 	*ts = input_get_drvdata(input);

	ts->pdata->disable(ts);

	printk("%s\n", __func__);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		touch_check_functionality	(struct touch_pdata *pdata)
{
	if(!pdata)	{
		printk("Error : Platform data is NULL pointer!\n");		return	-1;
	}

	if(!pdata->i2c_read)		pdata->i2c_read			= touch_i2c_read;
	if(!pdata->i2c_write)		pdata->i2c_write 		= touch_i2c_write;
		
	if(!pdata->i2c_boot_read)	pdata->i2c_boot_read	= touch_i2c_read;
	if(!pdata->i2c_boot_write)	pdata->i2c_boot_write	= touch_i2c_write;
		
	if(!pdata->enable)			pdata->enable 			= touch_enable;
	if(!pdata->disable)			pdata->disable			= touch_disable;

	if(!pdata->report)	{
	    if(pdata->max_fingers == 1) pdata->report   = touch_report_single;	
	    else    {
    		if(pdata->id_max)		pdata->report   = touch_report_protocol_b;
    		else					pdata->report   = touch_report_protocol_a;
	    }
	}

	if(!pdata->key_report)		pdata->key_report		= touch_key_report;

	if(!pdata->touch_work)		pdata->touch_work		= touch_work;
	if(!pdata->irq_func)		pdata->irq_func			= touch_irq;

	if(!pdata->event_clear)		pdata->event_clear		= touch_event_clear;
		
#ifdef	CONFIG_HAS_EARLYSUSPEND
	if(!pdata->resume)			pdata->resume			= touch_resume;
	if(!pdata->suspend)			pdata->suspend			= touch_suspend;
#endif
	
	return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_LCD_MIPI_TC358764)
    extern  unsigned short FrameBufferSizeX;
    extern  unsigned short FrameBufferSizeY;
#endif

static  void    touch_dummy_mode    (struct touch *ts)
{
    ts->irq = 0;    
    
    ts->pdata->irq_gpio         = 0;
    ts->pdata->probe            = 0;
    ts->pdata->i2c_read			= touch_i2c_read;
    ts->pdata->i2c_write 		= touch_i2c_write;
    
    ts->pdata->i2c_boot_read	= touch_i2c_read;
    ts->pdata->i2c_boot_write	= touch_i2c_write;
    
    ts->pdata->enable 			= touch_enable;
    ts->pdata->disable			= touch_disable;

    if(ts->pdata->max_fingers == 1) ts->pdata->report   = touch_report_single;	
    else    {
    	if(ts->pdata->id_max)		ts->pdata->report   = touch_report_protocol_b;
    	else					    ts->pdata->report   = touch_report_protocol_a;
    }

	ts->pdata->key_report		= touch_key_report;

	ts->pdata->touch_work		= touch_work;
	ts->pdata->irq_func			= touch_irq;

	ts->pdata->event_clear		= touch_event_clear;
		
#ifdef	CONFIG_HAS_EARLYSUSPEND
	ts->pdata->resume			= touch_resume;
	ts->pdata->suspend			= touch_suspend;
#endif
	
#if defined(CONFIG_LCD_MIPI_TC358764)
    ts->pdata->abs_max_x = FrameBufferSizeX;
    ts->pdata->abs_max_y = FrameBufferSizeY;
#endif
}

//[*]--------------------------------------------------------------------------------------------------[*]
void 	touch_hw_reset		(struct touch *ts)
{
	if(ts->pdata->reset_gpio)	{
		if(gpio_request(ts->pdata->reset_gpio, "touch reset"))	{
			printk("--------------------------------------------------------\n");
			printk("%s : request port error!\n", "touch reset");
			printk("--------------------------------------------------------\n");
		}
		else	{
			gpio_direction_output(ts->pdata->reset_gpio, ts->pdata->reset_level ? 0 : 1);
			mdelay(10);
	
			gpio_direction_output(ts->pdata->reset_gpio, ts->pdata->reset_level ? 1 : 0);
			mdelay(10);
	
			gpio_direction_output(ts->pdata->reset_gpio, ts->pdata->reset_level ? 0 : 1);
			mdelay(10);
			
			gpio_free(ts->pdata->reset_gpio);
		}
	}	
}

//[*]--------------------------------------------------------------------------------------------------[*]
int		touch_info_display	(struct touch *ts)
{
	printk("--------------------------------------------------------\n");
	printk("           TOUCH SCREEN INFORMATION\n");
	printk("--------------------------------------------------------\n");
	if(ts->pdata->irq_gpio && ts->pdata->max_fingers)	{
		printk("TOUCH INPUT Name = %s\n", ts->pdata->name);
		
		switch(ts->pdata->irq_mode)	{
			default	:
			case	IRQ_MODE_THREAD:	printk("TOUCH IRQ Mode   = %s\n", "IRQ_MODE_THREAD");	break;
			case	IRQ_MODE_NORMAL:	printk("TOUCH IRQ Mode   = %s\n", "IRQ_MODE_NORMAL");	break;
			case	IRQ_MODE_POLLING:	printk("TOUCH IRQ Mode   = %s\n", "IRQ_MODE_POLLING");	break;
		}
		printk("TOUCH F/W Version = %d.%02d\n", ts->fw_version / 100, ts->fw_version % 100);
	
		printk("TOUCH FINGRES MAX = %d\n", ts->pdata->max_fingers);
		printk("TOUCH ABS X MAX = %d, TOUCH ABS X MIN = %d\n", ts->pdata->abs_max_x, ts->pdata->abs_min_x);
		printk("TOUCH ABS Y MAX = %d, TOUCH ABS Y MIN = %d\n", ts->pdata->abs_max_y, ts->pdata->abs_min_y);
	
		if(ts->pdata->area_max)
			printk("TOUCH MAJOR MAX = %d, TOUCH MAJOR MIN = %d\n", ts->pdata->area_max, ts->pdata->area_min);
		
		if(ts->pdata->press_max)
			printk("TOUCH PRESS MAX = %d, TOUCH PRESS MIN = %d\n", ts->pdata->press_max, ts->pdata->press_min);
	
	    if(ts->pdata->max_fingers == 1) {
			printk("Single Touch Protocol Used.\n");
	    }
	    else    {
    		if(ts->pdata->id_max)	{
    			printk("TOUCH ID MAX = %d, TOUCH ID MIN = %d\n", ts->pdata->id_max, ts->pdata->id_min);
    			printk("Mulit-Touch Protocol-B Used.\n");
    		}
    		else
    			printk("Mulit-Touch Protocol-A Used.\n");
	    }
	
		if(ts->pdata->gpio_init)	
			printk("GPIO early-init function implemented\n");
			
		if(ts->pdata->reset_gpio)	
			printk("H/W Reset function implemented\n");
	
	#ifdef	CONFIG_HAS_EARLYSUSPEND
			printk("Early-suspend function implemented\n");
	#endif
		if(ts->pdata->fw_control)
			printk("Firmware update function(sysfs control) implemented\n");
			
		if(ts->pdata->flash_firmware)
			printk("Firmware update function(udev control) implemented\n");
	
		if(ts->pdata->calibration)
			printk("Calibration function implemented\n");
	}
	else	{
		printk("TOUCH INPUT Name = %s\n", ts->pdata->name);
		printk("TOUCH ABS X MAX = %d, TOUCH ABS X MIN = %d\n", ts->pdata->abs_max_x, ts->pdata->abs_min_x);
		printk("TOUCH ABS Y MAX = %d, TOUCH ABS Y MIN = %d\n", ts->pdata->abs_max_y, ts->pdata->abs_min_y);
		printk("Dummy Touchscreen driver!\n");
	}

	printk("--------------------------------------------------------\n");
	
	return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
int		touch_probe		(struct i2c_client *client)
{
    int				rc = -1;
    struct device 	*dev = &client->dev;
	struct touch 	*ts;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) 	{
		dev_err(&client->dev, "i2c byte data not supported\n");
		return -EIO;
	}

	if (touch_check_functionality(client->dev.platform_data) < 0)	{
		dev_err(&client->dev, "Platform data is not available!\n");
		return -EINVAL;
	}

	if(!(ts = kzalloc(sizeof(struct touch), GFP_KERNEL)))	{
		printk("touch struct malloc error!\n");			return	-ENOMEM;
	}
	ts->client	= client;
	ts->pdata 	= client->dev.platform_data;
	
	if(ts->pdata->irq_gpio)		ts->irq	= gpio_to_irq(ts->pdata->irq_gpio);

	i2c_set_clientdata(client, ts);

	if(ts->pdata->max_fingers)	{
		if(!(ts->finger = kzalloc(sizeof(finger_t) * ts->pdata->max_fingers, GFP_KERNEL)))	{
			kfree(ts);
			printk("touch data struct malloc error!\n");	return	-ENOMEM;
		}
	}
	
	if(ts->pdata->gpio_init)	ts->pdata->gpio_init();
	if(ts->pdata->reset_gpio)	touch_hw_reset(ts);

	if(ts->pdata->early_probe)	{
		if((rc = ts->pdata->early_probe(ts)) < 0)   touch_dummy_mode(ts);
	}

	dev_set_drvdata(dev, ts);

	if(!(ts->input 	= input_allocate_device()))		goto err_free_mem;

	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", ts->pdata->name);

	if(!ts->pdata->input_open)		ts->input->open 	= touch_input_open;
	else							ts->input->open		= ts->pdata->input_open;
	if(!ts->pdata->input_close)		ts->input->close	= touch_input_close;
	else							ts->input->close	= ts->pdata->input_close;

	ts->input->name 		= ts->pdata->name;
	ts->input->phys 		= ts->phys;
	ts->input->dev.parent 	= dev;
	ts->input->id.bustype 	= BUS_I2C;
	
	ts->input->id.vendor 	= ts->pdata->vendor;
	ts->input->id.product 	= ts->pdata->product;
	ts->input->id.version 	= ts->pdata->version;

	set_bit(EV_SYN, ts->input->evbit);		
	set_bit(EV_ABS, ts->input->evbit);

	// Touch Key Event
	if(ts->pdata->keycode)	{
		int	key;

		set_bit(EV_KEY,	ts->input->evbit);

		for(key = 0; key < ts->pdata->keycnt; key++)	{
			if(ts->pdata->keycode[key] <= 0)	continue;
			set_bit(ts->pdata->keycode[key] & KEY_MAX, ts->input->keybit);
		}
	}

	input_set_drvdata(ts->input, ts);

    if(ts->pdata->max_fingers == 1) {
        /* For single touch */
        set_bit(EV_KEY,	ts->input->evbit);    set_bit(BTN_TOUCH, ts->input->keybit);
        input_set_abs_params(ts->input, ABS_X, ts->pdata->abs_min_x, ts->pdata->abs_max_x, 0, 0);
        input_set_abs_params(ts->input, ABS_Y, ts->pdata->abs_min_y, ts->pdata->abs_max_y, 0, 0);
    }
    else    {
    	/* multi touch */
    	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 	ts->pdata->abs_min_x, ts->pdata->abs_max_x,	0, 0);
    	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 	ts->pdata->abs_min_y, ts->pdata->abs_max_y,	0, 0);
    		
    	if(ts->pdata->area_max)
    		input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, ts->pdata->area_min, ts->pdata->area_max,	0, 0);
    		
    	if(ts->pdata->press_max)
    		input_set_abs_params(ts->input, ABS_MT_PRESSURE, ts->pdata->press_min, ts->pdata->press_max,	0, 0);
    }

	if(ts->pdata->id_max)	{
		input_set_abs_params(ts->input, ABS_MT_TRACKING_ID, ts->pdata->id_min, ts->pdata->id_max, 	0, 0);
		input_mt_init_slots(ts->input, ts->pdata->max_fingers);
	}

	if ((rc = input_register_device(ts->input)))	{
		dev_err(dev, "(%s) input register fail!\n", ts->input->name);
		goto err_free_input_mem;
	}

	if(ts->irq)	{

		switch(ts->pdata->irq_mode)	{
			default	:
			case	IRQ_MODE_THREAD:
				if((rc = request_threaded_irq(ts->irq, NULL, ts->pdata->irq_func, ts->pdata->irq_flags, ts->pdata->name, ts)))	{
					dev_err(dev, "irq %d request fail!\n", ts->irq);
					goto err_free_input_mem;
				}
				break;
			case	IRQ_MODE_NORMAL:
				INIT_WORK(&ts->work, touch_work_q);
				if((ts->work_queue = create_singlethread_workqueue("work_queue")) == NULL)	goto err_free_input_mem;

				if((rc = request_irq(ts->irq, ts->pdata->irq_func, ts->pdata->irq_flags, ts->pdata->name, ts)))	{
					printk("irq %d request fail!\n", ts->irq);
					goto err_free_input_mem;
				}
				break;
			case	IRQ_MODE_POLLING:
				printk("Error IRQ_MODE POLLING!! but defined irq_gpio\n");
				break;
		}

		disable_irq_nosync(ts->irq);	
	}

	ts->disabled = true;

#if defined(CONFIG_HAS_EARLYSUSPEND)
	if(ts->pdata->suspend)	ts->power.suspend	= ts->pdata->suspend;
	if(ts->pdata->resume)	ts->power.resume	= ts->pdata->resume;

	ts->power.level		= EARLY_SUSPEND_LEVEL_DISABLE_FB-1;
	
	//if, is in USER_SLEEP status and no active auto expiring wake lock
	//if (has_wake_lock(WAKE_LOCK_SUSPEND) == 0 && get_suspend_state() == PM_SUSPEND_ON)
	register_early_suspend(&ts->power);
#endif

	if((rc = touch_sysfs_create(dev)) < 0)		goto	err_free_irq;

	if(ts->pdata->probe)	{
		if((rc = ts->pdata->probe(ts)) < 0)		goto	err_free_all;
	}

	touch_info_display(ts);
	
	return 0;

err_free_all:
	touch_sysfs_remove(dev);
err_free_irq:
	free_irq(ts->irq, ts);
	input_unregister_device(ts->input);
err_free_input_mem:
	input_free_device(ts->input);
	ts->input = NULL;
err_free_mem:
	kfree(ts->finger);		ts->finger = NULL;
	kfree(ts);				ts = NULL;
	return rc;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//
// Power Management function
//
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef	CONFIG_HAS_EARLYSUSPEND
//[*]--------------------------------------------------------------------------------------------------[*]
static	void	touch_suspend	(struct early_suspend *h)
{
	struct touch *ts = container_of(h, struct touch, power);

	printk("%s\n", __func__);

	ts->pdata->disable(ts);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	void	touch_resume		(struct early_suspend *h)
{
	struct touch *ts = container_of(h, struct touch, power);

	printk("%s\n", __func__);

	ts->pdata->enable(ts);
}

//[*]--------------------------------------------------------------------------------------------------[*]
#endif
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
int 	touch_remove	(struct device *dev)
{
	struct touch *ts = dev_get_drvdata(dev);

	if(ts->irq)					free_irq(ts->irq, ts);

	if(ts->pdata->reset_gpio)	gpio_free(ts->pdata->reset_gpio);

	touch_sysfs_remove(dev);

	input_unregister_device(ts->input);
	
	dev_set_drvdata(dev, NULL);		

	kfree(ts);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
