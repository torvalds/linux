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
#include <plat/gpio-cfg.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_HAS_EARLYSUSPEND)
	#include <linux/wakelock.h>
	#include <linux/earlysuspend.h>
	#include <linux/suspend.h>
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/input/touch-pdata.h>
#include <linux/input/odroidxu-touch.h>
#include "touch.h"
#include "odroidxu-config.h"

//[*]--------------------------------------------------------------------------------------------------[*]
//#define	DEBUG_TOUCH
#define	DEBUG_TOUCH_KEY

//[*]--------------------------------------------------------------------------------------------------[*]
// function prototype define
//[*]--------------------------------------------------------------------------------------------------[*]
// Touch data processing function
//[*]--------------------------------------------------------------------------------------------------[*]
static	int 	odroidxu_sw_config		(struct touch *ts);
static	int 	odroidxu_fifo_clear		(struct touch *ts);
		void 	odroidxu_work			(struct touch *ts);
		int 	odroidxu_calibration	(struct touch *ts);
		int 	odroidxu_i2c_read		(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len);
		void	odroidxu_enable			(struct touch *ts);
		void	odroidxu_disable		(struct touch *ts);
		int		odroidxu_early_probe	(struct touch *ts);
		int		odroidxu_probe			(struct touch *ts);
#ifdef	CONFIG_HAS_EARLYSUSPEND
		void	odroidxu_suspend		(struct early_suspend *h);
		void	odroidxu_resume			(struct early_suspend *h);
#endif		
//[*]--------------------------------------------------------------------------------------------------[*]
//
// i2c control function
//
//[*]--------------------------------------------------------------------------------------------------[*]
int 	odroidxu_i2c_read(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len)
{
	struct i2c_msg	msg[2];
	int 			ret;
	unsigned char	temp[10];
	unsigned char	i;

	if((len == 0) || (data == NULL))	{
		dev_err(&client->dev, "I2C read error: Null pointer or length == 0\n");	
		return 	-1;
	}

	memset(msg, 0x00, sizeof(msg));

	msg[0].addr 	= client->addr;
	msg[0].flags 	= 0;
	msg[0].len 		= cmd_len;
	msg[0].buf 		= cmd;

	msg[1].addr 	= client->addr;
	msg[1].flags    = I2C_M_RD;
	msg[1].len 		= len;
	msg[1].buf 		= temp;
	
	if ((ret = i2c_transfer(client->adapter, msg, 2)) != 2) {
		dev_err(&client->dev, "I2C read error: (%d) reg: 0x%X len: %d\n", ret, cmd[0], len);
		return -EIO;
	}

	if(len)	{
		for(i = 0; i < len; i++)	data[i] = temp[len - i -1];
	}

	return 	len;
}

//[*]--------------------------------------------------------------------------------------------------[*]
int 	odroidxu_calibration	(struct touch *ts)
{
	if(ts->pdata->reset_gpio)	gpio_set_value(ts->pdata->reset_gpio, ts->pdata->reset_level);			mdelay(10);
	if(ts->pdata->reset_gpio)	gpio_set_value(ts->pdata->reset_gpio, ts->pdata->reset_level ? 0 : 1);	mdelay(10);
		
	if(odroidxu_sw_config(ts) < 0)      return  -1;		
	if(odroidxu_fifo_clear(ts) < 0)     return  -1;

	return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//
// Touch data processing function
//
//[*]--------------------------------------------------------------------------------------------------[*]
static	int     odroidxu_sw_config	(struct touch *ts)
{
	unsigned char	i, wdata[2], cmd;

	for(i = 0; i < sizeof(Config) / sizeof(Config[0]); i++)
	{
		wdata[0] = Config[i].Data1;	wdata[1] = Config[i].Data2;		
		cmd = Config[i].Reg;
		
		if(Config[i].No == 0xFF)	mdelay(cmd);
		else	{
			if(ts->pdata->i2c_write(ts->client, (unsigned char *)&cmd, sizeof(cmd), &wdata[0], Config[i].No) < 0)
			    return  -1;
		}
	}
	mdelay(10);
	return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int 	odroidxu_fifo_clear	(struct touch *ts)
{
	unsigned char	wdata = 0x01, cmd = EVENT_FIFO_SCLR;
	
	if(ts->pdata->i2c_write(ts->client, (unsigned char *)&cmd, sizeof(cmd), (unsigned char *)&wdata, sizeof(wdata)) < 0)
	    return  -1; // clear Event FiFo
	    
	return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	unsigned char	odroidxu_id_tracking	(struct	touch *ts, unsigned char find_id)
{
	unsigned char	i, find_slot = 0xFF;
	
	for(i = 0; i < ts->pdata->max_fingers; i++)	{
		
		if(ts->finger[i].id == find_id)		find_slot = i;
			
		if((ts->finger[i].event == TS_EVENT_UNKNOWN) && (find_slot == 0xFF))	find_slot = i;
	}
	return	find_slot;
}

//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(SOFT_AVR_FILTER_ENABLE)

typedef struct  soft_filter__t  {
    unsigned int    x[SOFT_AVR_COUNT];
    unsigned int    y[SOFT_AVR_COUNT];
    unsigned int    pos, cnt;
}   __attribute__ ((packed))    soft_filter_t;

static  soft_filter_t   *pSoftFilter;

static  unsigned char   odroidxu_soft_avr_filter(struct touch *ts, event_stack_u *event_stack, unsigned char find_slot)
{
    unsigned int   i, x, y;

    if(ts->finger[find_slot].event != TS_EVENT_MOVE)    {
        pSoftFilter[find_slot].pos = 0;    pSoftFilter[find_slot].cnt = 0;  
        return  true;
    }

    x = (((event_stack->bits.msb_x << 8) & 0xF00) | event_stack->bits.lsb_x);
    y = (((event_stack->bits.msb_y << 8) & 0xF00) | event_stack->bits.lsb_y);

    if(y > 15)  y -= 15;
    else        y  = 1;

	ts->finger[find_slot].id        = event_stack->bits.number + 1;
    ts->finger[find_slot].pressure	=
    	(unsigned int)(event_stack->bits.pressure);
    
    if(pSoftFilter[find_slot].cnt == 1) {
        if((abs(pSoftFilter[find_slot].x[0] - x) < SOFT_AVR_MOVE_TOL_X) &&
           (abs(pSoftFilter[find_slot].y[0] - y) < SOFT_AVR_MOVE_TOL_Y)) return false;
    }

    if(event_stack->bits.speed < SOFT_AVR_ENABLE_SPEED)  {
        pSoftFilter[find_slot].x[pSoftFilter[find_slot].pos] = x;
        pSoftFilter[find_slot].y[pSoftFilter[find_slot].pos] = y;
        
        pSoftFilter[find_slot].pos++;  pSoftFilter[find_slot].pos %= SOFT_AVR_COUNT;
        
        if(pSoftFilter[find_slot].cnt < SOFT_AVR_COUNT)    pSoftFilter[find_slot].cnt++;
    
        for(i = 0, x = 0, y = 0; i < pSoftFilter[find_slot].cnt; i++) {
            x += pSoftFilter[find_slot].x[i];  y += pSoftFilter[find_slot].y[i];
        }
        
        ts->finger[find_slot].x = x / pSoftFilter[find_slot].cnt;
        ts->finger[find_slot].y = y / pSoftFilter[find_slot].cnt;
    }
    else    {
        pSoftFilter[find_slot].pos = 0;    pSoftFilter[find_slot].cnt = 0;  

		ts->finger[find_slot].x	= x;       ts->finger[find_slot].y	= y;
    }

    return  true;
}

#endif  // #if defined(SOFT_AVR_FILTER_ENABLE)

//[*]--------------------------------------------------------------------------------------------------[*]
void 	odroidxu_work		(struct touch *ts)
{
	status_u		status;
	button_u		button;
	event_stack_u	event_stack;
	unsigned char	cmd, find_slot;
	
	cmd = TOUCH_STATUS;
	if(ts->pdata->i2c_read(ts->client, (unsigned char *)&cmd, sizeof(cmd), (unsigned char *)&status.byte[0], sizeof(status_u)) < 0)	return;

	if(ts->pdata->keycode)	{
		cmd = BUTTON_STATUS;
		if(ts->pdata->i2c_read(ts->client, (unsigned char *)&cmd, sizeof(cmd), (unsigned char *)&button.ubyte, sizeof(button_u)) < 0)	return;
		ts->pdata->key_report(ts, button.ubyte);
	}

	if(status.bits.fifo_overflow || status.bits.large_object || status.bits.abnomal_status)	{
		printk("[Error Status] fifo_overflow(%d), large_object(%d), abnomal_status(%d)\n"	, status.bits.fifo_overflow
																							, status.bits.large_object
																							, status.bits.abnomal_status);
		// Error reconfig
		ts->pdata->disable(ts);		ts->pdata->enable(ts);
	
		return;
	}

	do	{
		cmd = EVENT_STACK;
		ts->pdata->i2c_read(ts->client, (unsigned char *)&cmd,  sizeof(cmd), (unsigned char *)&event_stack.byte[0],	sizeof(event_stack_u));

		if(status.bits.fifo_valid)	{

			if((find_slot = odroidxu_id_tracking(ts, event_stack.bits.number + 1)) == 0xFF)	{
				printk("ERROR(%s) : Empty slot not found", __func__);	continue;
			}
			if((event_stack.bits.event != EVENT_UNKNOWN) && (event_stack.bits.number < ts->pdata->max_fingers))	{
				if((event_stack.bits.event == EVENT_PRESS) || (event_stack.bits.event == EVENT_MOVE))	{

					ts->finger[find_slot].status	= true;
					ts->finger[find_slot].event		= TS_EVENT_MOVE;

                    #if defined(SOFT_AVR_FILTER_ENABLE)
                        ts->finger[find_slot].status	= odroidxu_soft_avr_filter(ts, &event_stack, find_slot);
                    #else
    					ts->finger[find_slot].id		= event_stack.bits.number + 1;
    					ts->finger[find_slot].x	=
    						(unsigned int)(((event_stack.bits.msb_x << 8) & 0xF00) | event_stack.bits.lsb_x);
    					ts->finger[find_slot].y	=
    						(unsigned int)(((event_stack.bits.msb_y << 8) & 0xF00) | event_stack.bits.lsb_y);
    						
                        if(ts->finger[find_slot].y > 15)    ts->finger[find_slot].y	-= 15;
                        else                                ts->finger[find_slot].y	 = 1;

    					ts->finger[find_slot].pressure	=
    						(unsigned int)(event_stack.bits.pressure);
                    #endif						
				}
				else	{
					if(ts->finger[find_slot].event == TS_EVENT_MOVE)	{
						ts->finger[find_slot].status	= true;
						ts->finger[find_slot].event 	= TS_EVENT_RELEASE;
					}
					else	{
						ts->finger[find_slot].status	= false;
						ts->finger[find_slot].event 	= TS_EVENT_UNKNOWN;
					}
                    #if defined(SOFT_AVR_FILTER_ENABLE)
                        odroidxu_soft_avr_filter(ts, &event_stack, find_slot);
                    #endif
				}
	            ts->pdata->report(ts);
			}
		}		
	}	while(!gpio_get_value(ts->pdata->irq_gpio));
}

//[*]--------------------------------------------------------------------------------------------------[*]
void	odroidxu_enable	(struct touch *ts)
{
	if(ts->disabled)		{
		odroidxu_calibration(ts);	
		
		enable_irq(ts->irq);		ts->disabled = false;
	}
}

//[*]--------------------------------------------------------------------------------------------------[*]
void	odroidxu_disable	(struct touch *ts)
{
	if(!ts->disabled)	{
		disable_irq(ts->irq);		ts->disabled = true;

		if(ts->pdata->event_clear)	ts->pdata->event_clear(ts);

		if(ts->pdata->reset_gpio)	gpio_set_value(ts->pdata->reset_gpio, ts->pdata->reset_level);
	}
}

//[*]--------------------------------------------------------------------------------------------------[*]
int		odroidxu_early_probe	(struct touch *ts)
{
#if defined(SOFT_AVR_FILTER_ENABLE)
	if(ts->pdata->max_fingers)	{
		if(!(pSoftFilter = kzalloc(sizeof(soft_filter_t) * ts->pdata->max_fingers, GFP_KERNEL)))	{
			printk("touch soft-filter struct malloc error!\n");	return	-ENOMEM;
		}
	}
#endif

	return  odroidxu_calibration(ts);	
}

//[*]--------------------------------------------------------------------------------------------------[*]
int		odroidxu_probe		(struct touch *ts)
{
	int				ret;
	unsigned short	rd;
	unsigned char	cmd;
	
	// Get Touch screen information
	cmd = DEVICE_ID;	rd = 0;
	ret = ts->pdata->i2c_read(ts->client, (unsigned char *)&cmd, sizeof(cmd), (unsigned char *)&rd, sizeof(rd));

#if defined(DEBUG_TOUCH)	// 0x2533
	printk("DEVICE ID       = 0x%04X\n", rd);
#endif

	cmd = VERSION_ID;	rd = 0;
	ret = ts->pdata->i2c_read(ts->client, (unsigned char *)&cmd, sizeof(cmd), (unsigned char *)&rd, sizeof(rd));

#if defined(DEBUG_TOUCH)
	printk("VERSION         = 0x%04X\n", rd);
#endif

	ts->fw_version = ((rd >> 8) & 0xFF) * 100 + (rd & 0xFF);

	return 	ret;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
