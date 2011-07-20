/*
 * Copyright (C) 2010 MEMSIC, Inc.
 *
 * Initial Code:
 *	Robbie Cao
 * 	Dale Hou
 *
 * This program is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

#include "fm580x.h"

#if 1
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif


#define FM580X_RETRY_COUNT	3

#define FM580X_DEV_NAME	"fm580x"

static struct i2c_client *this_client;
static uint8_t RDA5807P_REG[8];

static int fm580x_i2c_rx_data(uint8_t *buf, int len)
{
	uint8_t i;
	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
			.scl_rate = 200*1000,
			.udelay = 100,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
			.scl_rate = 200*1000,
			.udelay = 100,
		}
	};

	for (i = 0; i < FM580X_RETRY_COUNT; i++) {
	if (i2c_transfer(this_client->adapter, msgs, 2) >= 0) {
		break;
	}
	mdelay(10);
	}

	if (i >= FM580X_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, FM580X_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

static int fm580x_i2c_tx_data(uint8_t *buf, int len)
{
	uint8_t i;
	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= len,
			.buf	= buf,
		}
	};
	
	for (i = 0; i < FM580X_RETRY_COUNT; i++) {
	if (i2c_transfer(this_client->adapter, msg, 1) >= 0) {
		break;
	}
	mdelay(10);
	}

	if (i >= FM580X_RETRY_COUNT) {
	pr_err("%s: retry over %d\n", __FUNCTION__, FM580X_RETRY_COUNT);
	return -EIO;
	}
	return 0;
}


bool fm580x_tuner_init (void)
{
    int error_ind = 0;
    uint8_t i = 0;
    uint16_t gChipID;

    RDA5807P_REG[0] = 0x00;
    RDA5807P_REG[1] = 0x02;

	error_ind = fm580x_i2c_tx_data( (uint8_t *)&RDA5807P_REG[0], 2);
	mdelay(50);
    
    error_ind = fm580x_i2c_rx_data( (uint8_t *)&RDA5807P_REG[0], 8);      
    gChipID = RDA5807P_REG[6];
    gChipID = ((gChipID<<8) | RDA5807P_REG[7]);

    if (gChipID == 0x5804)
    {
		for (i=0;i<8;i++)
		RDA5807P_REG[i] = RDA5807PE_initialization_reg[i];
        error_ind = fm580x_i2c_tx_data( (uint8_t *)&RDA5807PE_initialization_reg[0], 2);
        mdelay(50); 
	    error_ind = fm580x_i2c_tx_data( (uint8_t *)&RDA5807PE_initialization_reg[0],sizeof(RDA5807PE_initialization_reg));
    }	
	
    DBG("%s:RDA5807P_REG[0]=0x%x,RDA5807P_REG[1]=0x%x,ID=0x%x\n",__FUNCTION__,RDA5807P_REG[0],RDA5807P_REG[1],gChipID);
	
    return true;
}


unsigned short fm580x_tuner_exit (void)
{   
   	RDA5807P_REG[1] &= (~1);
	fm580x_i2c_tx_data( &(RDA5807P_REG[0]), 2);
}

void fm580x_tuner_mute (unsigned short flag)
{
   if(flag)
     RDA5807P_REG[0] &=  ~(1<<7);
    else      
     RDA5807P_REG[0] |=  1<<7;  
    DBG("fm580x_tuner_mute \n");
    fm580x_i2c_tx_data( &(RDA5807P_REG[0]), 2);
}

void fm580x_tuner_setStereo(unsigned short flag)
{
    if(flag)
            RDA5807P_REG[0] &=  ~(1<<6);
        else
            RDA5807P_REG[0] |=  1<<6;
    
    fm580x_i2c_tx_data( &(RDA5807P_REG[0]), 2);
    mdelay(50);    
}

bool fm580x_tuner_getStereo()
{
    bool state;
    if(RDA5807P_REG[0] &(1<<6))
        state = 1;
    else
        state = 0;

     return state;
}

uint16_t fm580x_FreqToChan(uint16_t frequency) 
{
    uint8_t channelSpacing;
    uint16_t bottomOfBand;
    uint16_t channel;

    if ((RDA5807P_REG[3] & 0x0c) == 0x00) 
        bottomOfBand = 870;
    else if ((RDA5807P_REG[3] & 0x0c) == 0x04)  
        bottomOfBand = 760;
    else if ((RDA5807P_REG[3] & 0x0c) == 0x08)  
        bottomOfBand = 760; 
	
    if ((RDA5807P_REG[3] & 0x03) == 0x00) 
        channelSpacing = 1;
    else if ((RDA5807P_REG[3] & 0x03) == 0x01) 
        channelSpacing = 2;
   
    channel = (frequency - bottomOfBand) / channelSpacing;
    DBG("%s: RDA5807P_REG[3]=0x%x,channel=%d,bottomOfBand=%d,channelSpacing=%d\n",__FUNCTION__,RDA5807P_REG[3],channel,bottomOfBand,channelSpacing);
    return channel;
}


void fm580x_set_frequency(unsigned short curFreq)
{
    uint16_t curChan;
    curChan = fm580x_FreqToChan(curFreq/10);

    //SetNoMute
    //RDA5807P_REG[0] |=  1<<7;
    RDA5807P_REG[2]=curChan>>2;
    RDA5807P_REG[3]=(((curChan&0x0003)<<6)|0x10) | (RDA5807P_REG[3]&0x0f);  //set tune bit
    DBG("fm580x_set_frequency %x,%x,%x \n",RDA5807P_REG[0],RDA5807P_REG[2],RDA5807P_REG[3]);
    fm580x_i2c_tx_data( &(RDA5807P_REG[0]), 4);
    mdelay(10);     //Delay five ms
    DBG("%s:curchan=0x%x\n",__FUNCTION__,curChan);
}

bool  fm580x_tuner_CheckStation()
{
 
    uint8_t RDA5807P_reg_data[4]={0};	
	int i;
    //fm580x_i2c_rx_data(&(RDA5807P_reg_data[0]), 4);

    do
	{
		i++;
		if(i>5) return 0; 
		mdelay(30);
		fm580x_i2c_rx_data(&(RDA5807P_reg_data[0]), 4);	
	}while((RDA5807P_reg_data[3]&0x80)==0);
	DBG("fm580x_tuner_CheckStation %x ,%x\n",RDA5807P_reg_data[2],RDA5807P_reg_data[3]);
    return  (RDA5807P_reg_data[2]&(1<<0));  
}

static int fm580x_init_device(struct i2c_client *client)    
{   
    fm580x_tuner_init();
    fm580x_tuner_exit();
    return 0;
}

static ssize_t fm580x_proc_write(struct file *file, const char __user *buffer,
			   unsigned long count, void *data)
{
	char c;
	int rc;
	unsigned short freq;
	
	rc = get_user(c, buffer);
	if (rc)
		return rc; 
	
	freq = 10000*(c - '0');
	
	fm580x_tuner_init();
	fm580x_tuner_setStereo(1);
	fm580x_set_frequency(freq);
	fm580x_tuner_CheckStation();	

	printk("%s:freq=%d\n",__FUNCTION__,freq);
		
	return count; 
}

static const struct file_operations fm580x_proc_fops = {
	.owner		= THIS_MODULE, 
	.write		= fm580x_proc_write,
}; 

static int fm580x_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int fm580x_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int fm580x_ioctl(struct inode *inode, struct file *file, 
	unsigned int cmd, unsigned long arg)
{
	bool status;
	int ret = 0;
   	unsigned short freq;
	
	switch (cmd){	
	case FM_SET_AREA:
		DBG("set fm580x area \n");		
		break;

	case FM_MUTE://mute fm
		DBG("set fm580x mute \n");
		fm580x_tuner_mute(arg);
		break;	
		
	case FM_SET_ENABLE:
	    DBG("enable fm580x chip \n");
	    fm580x_tuner_init();	
	    //rk1000_codec_all_line_input_open(&rk1000_codec_dai,&codecdata1,&codecdata2);
		break;
		
	case FM_SET_DISABLE:
	    DBG("disable fm580x chip \n");
	    //rk1000_codec_all_line_input_close(&rk1000_codec_dai,codecdata1,codecdata2);
	    fm580x_tuner_exit();		
		break;	
		
	case FM_SET_STEREO:
	    DBG("set fm580x stereo \n");
		fm580x_tuner_setStereo(arg);
		break;	
		
	case FM_GET_STEREO:
	    DBG("get fm580x stereo \n");
	    status = fm580x_tuner_getStereo();
        ret = put_user(status,(int *)arg);
		if(ret < 0){
		    DBG("put_user err!\n");
		}
    	break;	
    	
    case FM_SET_FREQ:	 
    	ret=copy_from_user(&freq,(void __user *) arg, sizeof(int));
		if(ret < 0){
		DBG("put_user err!\n");
		return ret;
		} 
		fm580x_set_frequency(freq);  
   		break;    	
    	
    case FM_STATION_ISAVAILABLE: 
        status = fm580x_tuner_CheckStation();
        ret = put_user(status,(int *)arg);
		if(ret < 0){
		DBG("put_user err!\n");
		}
		DBG(" fm580x station's state is %d \n",status);
        break;
        
   case FM_TR_FUN:
        DBG("set fm580x tr fun \n"); 
        //musicTran(arg);
        break;
		
   case FM_TR_FUN_STOP:
        DBG("stop fm580x tr fun \n");
        //if(arg)
        //    musicCloseTran();
        break;
		
   default:
		//E("unknown ioctl cmd!\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static ssize_t fm580x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	sprintf(buf, "fm580x");
	ret = strlen(buf) + 1;
	printk("%s:ret=%d\n",__FUNCTION__,ret);
	return ret;
}

static DEVICE_ATTR(fm580x, S_IRUGO, fm580x_show, NULL);

static struct file_operations fm580x_fops = {
	.owner		= THIS_MODULE,
	.open		= fm580x_open,
	.release	= fm580x_release,
	.ioctl		= fm580x_ioctl,
};

static struct miscdevice fm580x_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = FM580X_DEV_NAME,
	.fops = &fm580x_fops,
};

static int fm580x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	unsigned char data[16] = {0};
	int res = 0;
	struct proc_dir_entry *fm580x_proc_entry;	

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		pr_err("%s: functionality check failed\n", __FUNCTION__);
		res = -ENODEV;
		goto out;
	}
	
	this_client = client;

	res = misc_register(&fm580x_device);
	if (res) {
	pr_err("%s: fm580x_device register failed\n", __FUNCTION__);
	goto out;
	}

	res = device_create_file(&client->dev, &dev_attr_fm580x);
	if (res) {
	pr_err("%s: device_create_file failed\n", __FUNCTION__);
	goto out_deregister;
	}
	
	//fm580x_tuner_init();

	fm580x_proc_entry = proc_create("driver/fm580x", 0777, NULL, &fm580x_proc_fops); 
	
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
	
	return 0;
	
out_deregister:
	misc_deregister(&fm580x_device);
out:
	return res;
}

static int fm580x_remove(struct i2c_client *client)
{
	device_remove_file(&client->dev, &dev_attr_fm580x);
	misc_deregister(&fm580x_device);
	return 0;
}

static const struct i2c_device_id fm580x_id[] = {
	{ FM580X_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver fm580x_driver = {
	.probe 		= fm580x_probe,
	.remove 	= fm580x_remove,
	.id_table	= fm580x_id,
	.driver 	= {
		.owner	= THIS_MODULE,
		.name	= FM580X_I2C_NAME,
	},
};


static int __init fm580x_init(void)
{
	pr_info("fm580x driver: init\n");
	return i2c_add_driver(&fm580x_driver);
}

static void __exit fm580x_exit(void)
{
	pr_info("fm580x driver: exit\n");
	i2c_del_driver(&fm580x_driver);
}

module_init(fm580x_init);
module_exit(fm580x_exit);

MODULE_AUTHOR("luo wei<lw@rock-chips.com>");
MODULE_DESCRIPTION("FM rda580x Driver");
MODULE_LICENSE("GPL");

