

#include <linux/i2c.h>
#include <linux/input.h>

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input/mt.h>		//use slot B protocol, Android 4.0 system
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#ifdef CONFIG_RK_CONFIG
#include <mach/config.h>
#endif
#include <mach/irqs.h>
//#include <mach/system.h>
//#include <mach/hardware.h>
//#include <mach/sys_config.h>
#include <mach/board.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
    #include <linux/pm.h>
    #include <linux/earlysuspend.h>
#endif

//#define	CONFIG_TS_FUNCTION_CALLED_DEBUG			//Display the debug information whitch function is called
//#define CONFIG_TS_PROBE_DEBUG		//Display the debug information in byd693x_ts_probe function
//#define CONFIG_TS_I2C_TRANSFER_DEBUG		//Display the debug information of IIC transfer
//#define CONFIG_TPKEY_STATUS_DEBUG			//Display the debug information of Touch Key status
//#define CONFIG_TS_WORKQUEUE_DEBUG		//Display the debug ihnformation of creating work queue
//#define CONFIG_TS_COORDIATE_DEBUG		//
//#define CONFIG_TS_CUTEDGE_DEBUG			//

//----------------------------------------//
#define	TOUCH_INT_NO	SW_INT_IRQNO_PIO    //GPIO :set the interrupt 
#define byd693x_I2C_NAME	"byd693x-ts"

//----------------------------------------//
struct ChipSetting {
	char No;
	char Reg;
	char Data1;
	char Data2;
};

#include "byd693x_ts.h"

#define VERSION 	"byd693x_20120731_16:52_V1.2_Charles@Raysens@Zed"
#define CTP_NAME	"byd693x-ts"

#define TP_MODULE_NAME  CTP_NAME
#ifdef CONFIG_RK_CONFIG

enum {
#if defined(RK2928_PHONEPAD_DEFAULT_CONFIG)
        DEF_EN = 1,
#else
        DEF_EN = 0,
#endif
        DEF_IRQ = 0x002003c7,
        DEF_RST = 0X000003d5,
        DEF_I2C = 2, 
        DEF_ADDR = 0x52,
        DEF_X_MAX = 800,
        DEF_Y_MAX = 480,
};
static int en = DEF_EN;
module_param(en, int, 0644);

static int irq = DEF_IRQ;
module_param(irq, int, 0644);
static int rst =DEF_RST;
module_param(rst, int, 0644);

static int i2c = DEF_I2C;            // i2c channel
module_param(i2c, int, 0644);
static int addr = DEF_ADDR;           // i2c addr
module_param(addr, int, 0644);
static int x_max = DEF_X_MAX;
module_param(x_max, int, 0644);
static int y_max = DEF_Y_MAX;
module_param(y_max, int, 0644);

#include "rk_tp.c"
#endif



struct byd_platform_data *byd6932_pdata; 


#define FINGER_NO_MAX		10		//Define the max finger number, but the really finger number: fetch from .fex file
#define BYD_COORD_READ_ADDR 0x5c

static int SCREEN_MAX_X = 1024;
static int SCREEN_MAX_Y = 600;
static int Get_Finger_Num = 5;

#define	RESO_X_NO		0
#define	RESO_Y_NO		1

struct ChipSetting byd693xcfg_Resolution[]={							
//{ 2,0x08,	200/256,	200%256},	//	1	FTHD_H;FTHD_L	//¨º???¡ã¡ä?¨¹?D?¦Ì
//{ 2,0x0A,	120/256,	120%256},	//	2	NTHD_H;NTHD_L	//??¨¦¨´?D?¦Ì
{ 2,0x0C,	800/256,	800%256},	//	3 RESX_H;RESX_L	//X¡¤?¡À??¨º
{ 2,0x0E,	480/256,	480%256},	//	4	RESY_H;RESY_L	//Y¡¤?¡À??¨º
};

static void deviceResume(struct i2c_client *client);
static void deviceSuspend(struct i2c_client *client);
void byd693xdeviceInit(struct i2c_client *client); 

//static int byd693x_ts_open(struct input_dev *dev);
//static void byd693x_ts_close(struct input_dev *dev);
static int byd693x_ts_suspend(struct i2c_client *client, pm_message_t mesg);
static int byd693x_ts_resume(struct i2c_client *client);
#ifdef CONFIG_HAS_EARLYSUSPEND
static void byd693x_ts_early_suspend(struct early_suspend *h);
static void byd693x_ts_late_resume(struct early_suspend *h);
#endif /* CONFIG_HAS_EARLYSUSPEND */

static irqreturn_t byd693x_ts_isr(int irq, void *dev_id);
static struct workqueue_struct *byd693x_wq;


struct byd_ts_priv {
	struct i2c_client *client;
	struct input_dev *input;
	struct hrtimer timer;
	struct work_struct  byd_work;
#ifdef	CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif 

	int irq;
	int FingerNo;
	int FingerDetect;
	u8 btn_pre_TPKey;
	int suspend_opend;
};

/***********************************************************
Read Data from TP through IIC
***********************************************************/
static int ReadRegister(struct i2c_client *client,uint8_t reg,unsigned char *buf, int ByteLen)
{
//	unsigned char buf[4];
	struct i2c_msg msg[2];
	int ret;

//	memset(buf, 0xFF, sizeof(buf));
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg;
	msg[0].scl_rate=byd693x_I2C_RATE;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = ByteLen;
	msg[1].buf = buf;
	msg[1].scl_rate=byd693x_I2C_RATE;

	ret = i2c_transfer(client->adapter, msg, 2);

	#ifdef CONFIG_TS_I2C_TRANSFER_DEBUG
	if(ret<0)	printk("		ReadRegister: i2c_transfer Error !\n");
	else		printk("		ReadRegister: i2c_transfer OK !\n");
	#endif
	if(ret<0)		{	return 0;	}
		else		{	return 1;	}
}

/***********************************************************
Write Data to TP through IIC
***********************************************************/
static void WriteRegister(struct i2c_client *client,uint8_t Reg,unsigned char Data1,unsigned char Data2,int ByteNo)
{	
	struct i2c_msg msg;
	unsigned char buf[4];
	int ret;

	buf[0]=Reg;
	buf[1]=Data1;
	buf[2]=Data2;
	buf[3]=0;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = ByteNo+1;
	msg.buf = (char *)buf;
	msg.scl_rate=byd693x_I2C_RATE;
	ret = i2c_transfer(client->adapter, &msg, 1);

	#ifdef CONFIG_TS_I2C_TRANSFER_DEBUG
	if(ret<0)	printk("		WriteRegister: i2c_master_send Error !\n");
	else		printk("		WriteRegister: i2c_master_send OK !\n");
	#endif
}

void byd693xdeviceInit(struct i2c_client *client)
{	
	deviceSuspend(client);
	deviceResume(client);
mdelay(30);
}

static void deviceResume(struct i2c_client *client)
{	
	int i;

	for(i=0;i<sizeof(Resume)/sizeof(Resume[0]);i++)
	{
		WriteRegister(	client,Resume[i].Reg,
				Resume[i].Data1,Resume[i].Data2,
				Resume[i].No);
	}
	mdelay(20);
	//Config the resolution of CTP
	for(i=0;i<sizeof(byd693xcfg_Resolution)/sizeof(byd693xcfg_Resolution[0]);i++)
	{
		WriteRegister(	client,byd693xcfg_Resolution[i].Reg,
				byd693xcfg_Resolution[i].Data1,byd693xcfg_Resolution[i].Data2,
				byd693xcfg_Resolution[i].No);
	}
	mdelay(20);
}

static void deviceSuspend(struct i2c_client *client)
{	
	int i;
	
	for(i=0;i<sizeof(Suspend)/sizeof(Suspend[0]);i++)
	{
		WriteRegister(	client,Suspend[i].Reg,
				Suspend[i].Data1,Suspend[i].Data2,
				Suspend[i].No);
	}
	mdelay(50);
}

static void bf693x_ts_send_keyevent(struct byd_ts_priv *byd_priv,u8 btn_status)
{
	
	switch(btn_status & 0xf0)
	{
		case 0x90:
			byd_priv->btn_pre_TPKey = TPKey_code[0];
			break;
		case 0xa0:
			byd_priv->btn_pre_TPKey = TPKey_code[1];
			break;
		case 0xb0:
			byd_priv->btn_pre_TPKey = TPKey_code[2];
			break;
		case 0xc0:
			byd_priv->btn_pre_TPKey = TPKey_code[3];
			break;
		case 0xf0:
			input_report_key(byd_priv->input, byd_priv->btn_pre_TPKey, REPORT_TPKEY_UP);
			input_sync(byd_priv->input);
			return;
		default:
			return;
	}
	input_report_key(byd_priv->input, byd_priv->btn_pre_TPKey, REPORT_TPKEY_DOWN);
	input_sync(byd_priv->input);
}
	
static void byd693x_ts_work(struct work_struct *work)
{
	int i;
	unsigned short xpos=0, ypos=0;
	unsigned char Coord_Buf[4*FINGER_NO_MAX +1];		//Define the max finger data
	u8 btn_status;
	u8 Finger_ID,Finger_Status,Report_Status;

	struct byd_ts_priv *byd_priv = container_of(work,struct byd_ts_priv,byd_work);

	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_work!                  |\n");
	printk("+-----------------------------------------+\n");
	#endif
	if (byd_priv->suspend_opend == 1)
		return ;
		
	ReadRegister(byd_priv->client,BYD_COORD_READ_ADDR,Coord_Buf,(4 * Get_Finger_Num +1));		//read only the used finger number data
		
	btn_status = Coord_Buf[0];
#ifdef CONFIG_TS_COORDIATE_DEBUG
	printk("btn_status is: 0x%x\n",btn_status);
#endif
	
	if ( 0x00 == (btn_status & 0x80))
	{
		return;
	}

	bf693x_ts_send_keyevent(byd_priv,btn_status);

	byd_priv->FingerDetect = 0;
	Report_Status = 0;
	if ((btn_status & 0x0f))
		{
			for(i=0;i< (btn_status & 0x0f);i++)
			{
				Finger_ID = (Coord_Buf[i*4 + 1]>>4)-1;
				Finger_Status = Coord_Buf[i*4 + 3] & 0xf0;
				xpos = Coord_Buf[i*4 + 1] & 0x0f;
				xpos = (xpos <<8) | Coord_Buf[i*4 + 2];
				
				ypos = Coord_Buf[i*4 + 3] & 0x0f;
				ypos = (ypos <<8) | Coord_Buf[i*4 + 4];
			
			if (byd6932_pdata ->swap_xy)
				swap(xpos, ypos);
			if (byd6932_pdata ->xpol)
				xpos = byd6932_pdata ->screen_max_x -xpos;
			if (byd6932_pdata ->ypol)
				ypos = byd6932_pdata ->screen_max_y -ypos;

				if((0xa0 == Finger_Status) || (0x90 == Finger_Status))		//0xa0:The first Touch;  0x90: Hold Finger Touch
				{
					byd_priv->FingerDetect++;
					Report_Status = 1;
//					printk("Finger_ID = 0x%x, DOWN\n", Finger_ID);
					input_mt_slot(byd_priv->input, Finger_ID);		//Slot B protocol
					input_report_abs(byd_priv->input, ABS_MT_TRACKING_ID, Finger_ID);
					input_report_abs(byd_priv->input, ABS_MT_TOUCH_MAJOR, REPORT_TOUCH_MAJOR); //Finger Size
					input_report_abs(byd_priv->input, ABS_MT_POSITION_X, xpos);
					input_report_abs(byd_priv->input, ABS_MT_POSITION_Y, ypos);
					input_report_abs(byd_priv->input, ABS_MT_WIDTH_MAJOR, REPORT_WIDTH_MAJOR); //Touch Size
		
					#ifdef CONFIG_TS_COORDIATE_DEBUG
						printk("  Finger Touch X = %d , Y = %d, State = 0x%x,Finger_ID=0x%x\n\n",xpos,ypos,Finger_Status,Finger_ID);
					#endif
				}
				
				if (Finger_Status == 0xc0)
				{
					Report_Status = 1;
					input_mt_slot(byd_priv->input, Finger_ID);
					input_report_abs(byd_priv->input, ABS_MT_TRACKING_ID, -1);
				#ifdef CONFIG_TS_COORDIATE_DEBUG
					printk("	Touch release  X = %d , Y = %d, State = 0x%x,Finger_ID=0x%x\n\n",xpos,ypos,Finger_Status,Finger_ID);
				#endif
				}
			}
		}
	if (Report_Status)
	{
			input_sync(byd_priv->input);
	}
}

static int byd693x_init_platform_hw(void)
{
    if(gpio_request(byd6932_pdata->rst_pin,NULL) != 0){
      gpio_free(byd6932_pdata->rst_pin);
      printk("byd693x_init_platform_hw gpio_request error\n");
      return -EIO;
    }

    if(gpio_request(byd6932_pdata->int_pin, NULL) != 0){
      gpio_free(byd6932_pdata->int_pin);
      printk("byd693x_init_platform_hw gpio_request error\n");
      return -EIO;
    }
    gpio_pull_updown(byd6932_pdata->int_pin, 1);
    gpio_direction_output(byd6932_pdata->rst_pin, 1);
    return 0;
}


static int byd693x_ts_probe(struct i2c_client *client,const struct i2c_device_id *idp)
{
	struct byd_ts_priv *byd_priv;
	struct input_dev *byd_input = NULL;
	struct byd_platform_data *pdata = client->dev.platform_data;
	unsigned char tp_buf[1];
	int error = -1;
#ifdef CONFIG_RK_CONFIG
        struct port_config rst_cfg = get_port_config(rst);
        struct port_config irq_cfg = get_port_config(irq);

        if(!pdata){
                pdata = kzalloc(sizeof(struct byd_platform_data), GFP_KERNEL);
                if(!pdata){
			printk("byd693x_ts_probe: kzalloc Error!\n");
                        return -ENOMEM;
                };
        }
        pdata->rst_pin = rst_cfg.gpio;
        pdata->int_pin = irq_cfg.gpio;
        pdata->screen_max_x = x_max;
        pdata->screen_max_y = y_max;
#endif


	byd6932_pdata = pdata;
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
		printk("+-----------------------------------------+\n");
		printk("|	byd693x_ts_probe!                 |\n");
		printk("+-----------------------------------------+\n");
	#endif
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: need I2C_FUNC_I2C\n");
		#endif
		return -ENODEV;
	}
	else
	{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: i2c Check OK!\n");
			printk("		byd693x_ts_probe: i2c_client name : %s\n",client->name);
		#endif
	}

	byd_priv = kzalloc(sizeof(*byd_priv), GFP_KERNEL);
	if (!byd_priv)
	{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: kzalloc Error!\n");
		#endif
		error=-ENODEV;
		goto	err0;
	}
	else
	{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: kzalloc OK!\n");
		#endif
	}



	dev_set_drvdata(&client->dev, byd_priv);
	byd_input = input_allocate_device();
	if (!byd_input)
	{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: input_allocate_device Error\n");
		#endif
		error=-ENODEV;
		goto	err1;
	}
	else
	{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: input_allocate_device OK\n");
		#endif
	}

	//only check BYD BF6932
	error = ReadRegister(client, BYD_COORD_READ_ADDR, tp_buf, sizeof(tp_buf));
	if (error <= 0)
	{
		printk(KERN_ALERT "BYD BF6932 Touchscreen not found \n");
		gpio_free(pdata->rst_pin);
		gpio_free(pdata->int_pin);
		goto err1;
	}

//	byd_input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) | BIT_MASK(EV_SYN)|BIT_MASK(EV_REP) ;
//	byd_input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) | BIT_MASK(BTN_2);
	byd_input->name = client->name;
	byd_input->id.bustype = BUS_I2C;
	byd_input->id.vendor  = 0x2878; // Modify for Vendor ID
	byd_input->dev.parent = &client->dev;
//	byd_input->open = byd693x_ts_open;
//	byd_input->close = byd693x_ts_close;
	input_set_drvdata(byd_input, byd_priv);
	byd_priv->client = client;
	byd_priv->input = byd_input;
	byd_priv->irq = pdata->int_pin;
	byd_priv->FingerNo=FINGER_NO_MAX;

	byd_input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	byd_input->absbit[0] = BIT(ABS_X) | BIT(ABS_Y); // for android

	__set_bit(EV_ABS, byd_input->evbit);
	__set_bit(INPUT_PROP_DIRECT, byd_input->propbit);
	set_bit(ABS_MT_POSITION_X, byd_input->absbit);
	set_bit(ABS_MT_POSITION_Y, byd_input->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, byd_input->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, byd_input->absbit);

//	deviceReset(client);
//	printk("BYD Touchscreen I2C Address: 0x%02X\n",client->addr);
//	printk("BYD Touchscreen Device ID  : BF6932\n");
	
	//config the resolution of CTP
	byd693xcfg_Resolution[RESO_X_NO].Data1 = (char)(pdata->screen_max_x >>8);
	byd693xcfg_Resolution[RESO_X_NO].Data2 = (char)(pdata->screen_max_x & 0xff);
	
	byd693xcfg_Resolution[RESO_Y_NO].Data1 = (char)(pdata->screen_max_y >>8);
	byd693xcfg_Resolution[RESO_Y_NO].Data2 = (char)(pdata->screen_max_y & 0xff);
	
	byd693xdeviceInit(client);
 
	input_mt_init_slots(byd_input, MAX_TRACKID_ITEM);


	input_set_abs_params(byd_input, ABS_MT_TOUCH_MAJOR, 0, MAX_TOUCH_MAJOR, 0, 0);
	input_set_abs_params(byd_input, ABS_MT_WIDTH_MAJOR, 0, MAX_WIDTH_MAJOR, 0, 0);
	input_set_abs_params(byd_input, ABS_MT_POSITION_X,  0,pdata->screen_max_x + 1, 0, 0);
	input_set_abs_params(byd_input, ABS_MT_POSITION_Y,  0,pdata->screen_max_y + 1, 0, 0);

#ifdef USE_TOUCH_KEY
	set_bit(KEY_MENU, byd_input->keybit);
	set_bit(KEY_HOME, byd_input->keybit);
	set_bit(KEY_BACK, byd_input->keybit);
	set_bit(KEY_SEARCH, byd_input->keybit);
#endif
	
	INIT_WORK(&byd_priv->byd_work, byd693x_ts_work);

	error = input_register_device(byd_input);

	if(error)
	{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: input_register_device input Error!\n");
		#endif
		error=-ENODEV;
		goto	err1;
	}
	else
	{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: input_register_device input OK!\n");
		#endif
	}

		error = byd693x_init_platform_hw();		//Init RK29 GPIO
		if(0 != error)
		{
			printk("%s:Init_INT set_irq_mode err. \n", __func__);
			goto exit_set_irq_mode;
		}
		// Options for different interrupt system 
//		error = request_irq(byd_priv->irq, byd693x_ts_isr, IRQF_DISABLED|IRQF_TRIGGER_FALLING, client->name,byd_priv);
//		error = request_irq(byd_priv->irq, byd693x_ts_isr, IRQF_TRIGGER_FALLING, client->name,byd_priv);
		error = request_irq(byd_priv->irq, byd693x_ts_isr, IRQF_TRIGGER_FALLING, client->name,byd_priv);
		if(error)
		{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: request_irq Error!\n");
		#endif
			error=-ENODEV;
			goto err2;
		}
		else
		{
		#ifdef CONFIG_TS_PROBE_DEBUG
			printk("		byd693x_ts_probe: request_irq OK!\n");
			#endif
		}	

	printk("Install BYD BF6932 Touchscreen driver successfully\n");
	
#ifdef	CONFIG_HAS_EARLYSUSPEND
	byd_priv->early_suspend.suspend = byd693x_ts_early_suspend;
	byd_priv->early_suspend.resume  = byd693x_ts_late_resume;
	byd_priv->early_suspend.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB+1;
	register_early_suspend(&byd_priv->early_suspend);
#endif 
	return 0;
exit_set_irq_mode:
err2:	input_unregister_device(byd_input);	
err1:	input_free_device(byd_input);
	kfree(byd_priv);
//exit_gpio_wakeup_request_failed:	
err0:	dev_set_drvdata(&client->dev, NULL);
	return error;
}

/*
static int byd693x_ts_open(struct input_dev *dev)
{
	struct byd_ts_priv *byd_priv = input_get_drvdata(dev);
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_open!                  |\n");
	printk("+-----------------------------------------+\n");
	#endif	
	deviceResume(byd_priv->client);
	enable_irq(byd_priv->irq);
	byd_priv->suspend_opend = 0;
	return 0;
}

static void byd693x_ts_close(struct input_dev *dev)
{
	struct byd_ts_priv *byd_priv = input_get_drvdata(dev);
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_close!                 |\n");
	printk("+-----------------------------------------+\n");
	#endif
	deviceSuspend(byd_priv->client);	
	byd_priv->suspend_opend = 1;
	disable_irq(byd_priv->irq);
}
*/

static int byd693x_ts_resume(struct i2c_client *client)
{
	struct byd_ts_priv *byd_priv = dev_get_drvdata(&client->dev);
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_resume!                |\n");
	printk("+-----------------------------------------+\n");
	#endif

	deviceResume(client);
	byd_priv->suspend_opend = 0;		
	enable_irq(byd_priv->irq);
	return 0;
}

static int byd693x_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct byd_ts_priv *byd_priv = dev_get_drvdata(&client->dev);
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_suspend!               |\n");
	printk("+-----------------------------------------+\n");
	#endif
	byd_priv->suspend_opend = 1;

	disable_irq(byd_priv->irq);
	deviceSuspend(client);
	return 0;
}

#ifdef	CONFIG_HAS_EARLYSUSPEND
static void byd693x_ts_late_resume(struct early_suspend *h)
{
	struct byd_ts_priv *byd_priv = container_of(h, struct byd_ts_priv, early_suspend);
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_late_resume!           |\n");
	printk("+-----------------------------------------+\n");
	#endif
	byd693x_ts_resume(byd_priv->client);
}
static void byd693x_ts_early_suspend(struct early_suspend *h)
{
	struct byd_ts_priv *byd_priv = container_of(h, struct byd_ts_priv, early_suspend);
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_early_suspend!         |\n");
	printk("+-----------------------------------------+\n");
	#endif
	byd693x_ts_suspend(byd_priv->client, PMSG_SUSPEND);
}
#endif

static int byd693x_ts_remove(struct i2c_client *client)
{
	struct byd_ts_priv *byd_priv = dev_get_drvdata(&client->dev);
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_remove !               |\n");
	printk("+-----------------------------------------+\n");
	#endif
	free_irq(byd_priv->irq, byd_priv);
	input_unregister_device(byd_priv->input);
	input_free_device(byd_priv->input);
	kfree(byd_priv);
	dev_set_drvdata(&client->dev, NULL);
	return 0;
}

static irqreturn_t byd693x_ts_isr(int irq, void *dev_id)
{
	struct byd_ts_priv *byd_priv = dev_id;
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_isr!                   |\n");
	printk("+-----------------------------------------+\n");
	#endif	
	disable_irq_nosync(byd_priv->irq);
	queue_work(byd693x_wq, &byd_priv->byd_work);
	enable_irq(byd_priv->irq);
	return IRQ_HANDLED;
}

static const struct i2c_device_id byd693x_ts_id[] = {
	{ CTP_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, byd693x_ts_id);

static struct i2c_driver byd693x_ts_driver = {
	.driver = {
		.name = CTP_NAME,
	},
	.probe = byd693x_ts_probe,
	.remove = byd693x_ts_remove,
#ifndef	CONFIG_HAS_EARLYSUSPEND
	.suspend = byd693x_ts_suspend,
	.resume = byd693x_ts_resume,
#endif
	.id_table = byd693x_ts_id,
};

static char banner[] __initdata = KERN_INFO "BYD Touchscreen driver, (c) 2012 BYD Systech Ltd.\n";
static int __init byd693x_ts_init(void)
{
	int ret;

#ifdef CONFIG_RK_CONFIG
        ret = tp_board_init();

        if(ret < 0)
                return ret;
#endif
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd_ts_init!                      |\n");
	printk("+-----------------------------------------+\n");
	#endif
	printk(banner);
	printk("==================byd693x_ts_init===========================\n");
	printk("Version =%s\n",VERSION);
	byd693x_wq = create_singlethread_workqueue("byd693x_wq");
	if (!byd693x_wq)
	{
		#ifdef CONFIG_TS_WORKQUEUE_DEBUG
		printk("		byd693x_ts_init: create_singlethread_workqueue Error!\n");
		#endif
		return -ENOMEM;
	}
	else
	{
		#ifdef CONFIG_TS_WORKQUEUE_DEBUG
		printk("		byd693x_ts_init: create_singlethread_workqueue OK!\n");
		#endif
	}
	ret=i2c_add_driver(&byd693x_ts_driver);
	#ifdef CONFIG_TS_I2C_TRANSFER_DEBUG
	if(ret) printk("		byd693x_ts_init: i2c_add_driver Error! \n");
	else    printk("		byd693x_ts_init: i2c_add_driver OK! \n");
	#endif
	return ret;
}

static void __exit byd693x_ts_exit(void)
{
	#ifdef CONFIG_TS_FUNCTION_CALLED_DEBUG
	printk("+-----------------------------------------+\n");
	printk("|	byd693x_ts_exit!                  |\n");
	printk("+-----------------------------------------+\n");
	#endif
	i2c_del_driver(&byd693x_ts_driver);
	if (byd693x_wq) destroy_workqueue(byd693x_wq);
}

module_init(byd693x_ts_init);
module_exit(byd693x_ts_exit);

MODULE_AUTHOR("BYD Systech Ltd - Raysens Design Technology, Charles Chen.");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("byd693x Touchscreen Driver 1.2_Charles@Raysens@20120731");
