#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>

#include "vtl_ts.h"
#include "chip.h"

#define		APK_ITOUCH			0x01

#define		INVALID				0x00
#define		HW_RESET_CMD			0x01
#define		CHIP_INFO_CMD			0x02
#define		DRIVER_INFO_CMD			0x03
#define		CHIP_ID_CMD			0x04
#define		WRITE_CHIP_CMD			0x05
#define		READ_CHIP_CMD			0x06
#define		RECOVERY_CMD			0x07
#define		INTERRUPT_CMD			0x08
#define		READ_CHECKSUM_CMD		0x09
#define		READ_FWCHECKSUM_CMD		0x0a
#define		XY_DEBUG_CMD			0x0b


#define		LINUX_SHELL			'c'
#define		SHELL_CHECKSUM_CMD		'1'
#define		SHELL_UPDATE_CMD		'2'
#define		SHELL_DEBUG_CMD			'3'



static struct ts_info * ts_object = NULL;

/*****************************************************************************
** Function define
*****************************************************************************/
static int apk_i2c_transfer(struct i2c_client *client,unsigned char i2c_addr,unsigned char len,unsigned char *buf,unsigned char rw)
{
	struct i2c_msg msgs[1];
	
	DEBUG();
	
	msgs[0].flags = rw;
	msgs[0].addr  = i2c_addr;
	msgs[0].len   = len;
	msgs[0].buf   = buf;
	//msgs[0].scl_rate = TS_I2C_SPEED;		//only for rockchip
	
	if(i2c_transfer(client->adapter, msgs, 1)!= 1)
	{
		return -1;
	}	
 	return 0;
}



static int apk_open(struct inode *inode, struct file *file)
{
	printk("___%s___\n",__func__);
	DEBUG();

	ts_object = vtl_ts_get_object();	
	if(ts_object == NULL)
	{
		return -1;
	}
	return 0;
}

static int apk_close(struct inode *inode, struct file *file)
{
	printk("___%s___\n",__func__);
	DEBUG();

	return 0;
}
static ssize_t apk_write(struct file *file, const char __user *buff, size_t count, loff_t *offp)
{
	struct i2c_client *client = ts_object->driver->client;	
	unsigned char frame_data[255] = {0};
	int bin_checksum = 0;
	int fw_checksum = 0;
	int ret = 0;

	DEBUG();
	
	if(copy_from_user(frame_data, buff, count)){
		return -EFAULT;
	}
	
	if(frame_data[0]==APK_ITOUCH){
		
		switch(frame_data[1]){
			
			case 	HW_RESET_CMD :{
					vtl_ts_hw_reset();							
				}break;

			case 	INTERRUPT_CMD :{
					if(frame_data[4]){
						enable_irq(ts_object->config_info.irq_number);
					}else{
						disable_irq(ts_object->config_info.irq_number);
					}						
				}break;

			case 	RECOVERY_CMD :{		
					ret = update(client);			
				}break;
									
			case	WRITE_CHIP_CMD:{					
					ret = apk_i2c_transfer(client,frame_data[2],frame_data[3],&frame_data[4],0);	
				}break;

			case 	XY_DEBUG_CMD :{	
					if(ts_object->debug){
						ts_object->debug = 0x00;
					}else{
						ts_object->debug = 0x01;
					}				
				}break;
			
			default :{
				
				}break;
		}
	}else if(frame_data[0]==LINUX_SHELL){
		printk("CMD: %s,count = %zu\n",frame_data,count);
		switch(frame_data[1]){
			case	SHELL_CHECKSUM_CMD :{
					chip_get_checksum(client,&bin_checksum,&fw_checksum);
					printk("bin_checksum = 0x%x,fw_checksum = 0x%x\n",bin_checksum,fw_checksum);
				}break;

			case 	SHELL_UPDATE_CMD :{		
					chip_update(client);			
				}break;

			case 	SHELL_DEBUG_CMD :{
					if(ts_object->debug){
						ts_object->debug = 0x00;
					}else{
						ts_object->debug = 0x01;
					}		
								
				}break;

			default :{
					
				}break;
		}
	}else{
		return -1;
	}
	if(ret<0){
		return -1;	
	}

 	return count;
}

static ssize_t apk_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	struct i2c_client *client = ts_object->driver->client;
	unsigned char frame_data[255];
	int bin_checksum = 0;
	int fw_checksum = 0;
	int len = 0;
	int ret = 0;

	DEBUG();
	if(copy_from_user(frame_data, buff, count))
	{
		return -EFAULT;
	}

	len = frame_data[3];
	if(frame_data[0]==APK_ITOUCH){
		
		switch(frame_data[1]){
			case	DRIVER_INFO_CMD :{
					frame_data[0] = client->addr;
				}break;
									
			case	READ_CHIP_CMD :{
					
					ret = apk_i2c_transfer(client,frame_data[2],frame_data[3],frame_data,1);
				}break;

			case	READ_CHECKSUM_CMD :{
					ret = chip_get_checksum(client,&bin_checksum,&fw_checksum);
					frame_data[0] = bin_checksum & 0x00ff;
					frame_data[1] = bin_checksum >> 8;
					frame_data[2] = fw_checksum & 0x00ff;
					frame_data[3] = fw_checksum >> 8;
				}break;
			case	READ_FWCHECKSUM_CMD :{
					ret = chip_get_fwchksum(client,&fw_checksum);
					frame_data[0] = fw_checksum & 0x00ff;
					frame_data[1] = fw_checksum >> 8;
				}break;
			
			default :{
					
				}break;
		}
	}

	if(copy_to_user(buff, frame_data,len)){
		return -EFAULT; 
	}
	if(ret<0){
		return -1;	
	}
	
	return count;
}

struct file_operations apk_fops = {
	.owner	 = THIS_MODULE,
	.open    = apk_open,
	.release = apk_close,
	.write   = apk_write,
	.read    = apk_read,
};

