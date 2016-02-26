/*
 * BQ27320 battery driver
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h> 
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/vmalloc.h>

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define DRIVER_VERSION			"1.0.0"
#define BQ27x00_REG_TEMP		0x06
#define BQ27x00_REG_VOLT		0x08
#define BQ27x00_REG_AI			0x14
#define BQ27x00_REG_BATTERYSTATUS		0x0A
#define BQ27x00_REG_TTE			0x16
#define BQ27x00_REG_TTF			0x18
#define BQ27320_REG_SOC			0x2c

#define BQ27320_BATTERYSTATUS_DSC		BIT(0)
#define BQ27320_BATTERYSTATUS_SYSDOWN		BIT(1)
//#define BQ27320_BATTERYSTATUS_CHGS		BIT(8)
#define BQ27320_BATTERYSTATUS_FC			BIT(9)
#define BQ27320_BATTERYSTATUS_OTD		BIT(10)
#define BQ27320_BATTERYSTATUS_OTC		BIT(11)
#define BQ27320_CURRENT		BIT(15)

#define BQ27320_SPEED 			100 * 1000

/*define for firmware update*/
#define BSP_I2C_MAX_TRANSFER_LEN			128
#define BSP_MAX_ASC_PER_LINE				400
#define BSP_ENTER_ROM_MODE_CMD				0x00
#define BSP_ENTER_ROM_MODE_DATA 			0x0F00
#define BSP_ROM_MODE_I2C_ADDR				0x0B
#define BSP_NORMAL_MODE_I2C_ADDR			0x55
#define BSP_FIRMWARE_FILE_SIZE				(3301*400)

/*define for power detect*/
#define BATTERY_LOW_CAPACITY 2
#define BATTERY_LOW_VOLTAGE 3500000
#define BATTERY_RECHARGER_CAPACITY 97
#define BATTERY_LOW_TEMPRETURE 0
#define BATTERY_HIGH_TEMPRETURE 650

struct bq27320_device_info {
	struct device 		*dev;
	struct power_supply	bat;
	struct power_supply	usb;
	struct power_supply	ac;
	struct delayed_work work;
	struct i2c_client	*client;
	unsigned int interval;
	unsigned int dc_check_pin;
	unsigned int bat_num;
	unsigned		ac_charging;
	unsigned		usb_charging;
	unsigned		online;
	unsigned int irq_pin;
	int soc_full;
	int rsoc;
	int bat_tempreture;
	int bat_status;
	int bat_present;
	struct workqueue_struct *workqueue;	
	struct delayed_work	chg_down_work;
	struct mutex	battery_mutex;
	
};
struct bq27320_board {
	unsigned int irq_pin;
	struct device_node *of_node;
};

struct i2c_client* g_bq27320_i2c_client = NULL;
static struct i2c_driver bq27320_battery_driver;

int  virtual_battery_enable = 0;
extern int dwc_vbus_status(void);
//extern int get_gadget_connect_flag(void);
extern int dwc_otg_check_dpdm(bool wait);

#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

/* If the system has several batteries we need a different name for each
 * of them...
 */
static DEFINE_MUTEX(battery_mutex);

static struct bq27320_device_info *bq27320_di;
static enum power_supply_property bq27320_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
//	POWER_SUPPLY_PROP_TECHNOLOGY,
//	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	//POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

static enum power_supply_property rk3190_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property rk3190_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static ssize_t battery_proc_write(struct file *file,const char __user *buffer,
			 size_t count, loff_t *ppos)
{
	char c;
	int rc;
	printk("USER:\n");
	printk("echo x >/proc/driver/power\n");
	printk("x=1,means just print log ||x=2,means log and data ||x= other,means close log\n");

	rc = get_user(c,buffer);
	if(rc)
		return rc;
		
	if(c == '1')
		virtual_battery_enable = 1;
	else if(c == '2')
		virtual_battery_enable = 2;
	else if(c == '3')
		virtual_battery_enable = 3;
	else 
		virtual_battery_enable = 0;
	printk("%s,count(%d),virtual_battery_enable(%d)\n",__FUNCTION__,(int)count,virtual_battery_enable);
	return count;
}

static const struct file_operations battery_proc_fops = {
	.owner		= THIS_MODULE, 
	.write		= battery_proc_write,
}; 

/*
 * Common code for BQ27320 devices read
 */

 int bq27320_i2c_master_reg8_read(const struct i2c_client *client, const char reg, char *buf, int count, int scl_rate)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf = reg;
	
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;
	msgs[0].scl_rate = scl_rate;
//	msgs[0].udelay = client->udelay;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = count;
	msgs[1].buf = (char *)buf;
	msgs[1].scl_rate = scl_rate;
//	msgs[1].udelay = client->udelay;

	ret = i2c_transfer(adap, msgs, 2);
	return (ret == 2)? count : ret;
}
EXPORT_SYMBOL(bq27320_i2c_master_reg8_read);

int bq27320_i2c_master_reg8_write(const struct i2c_client *client, const char reg, const char *buf, int count, int scl_rate)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;
	char *tx_buf = (char *)kmalloc(count + 1, GFP_KERNEL);
	if(!tx_buf)
		return -ENOMEM;
	tx_buf[0] = reg;
	memcpy(tx_buf+1, buf, count); 

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = count + 1;
	msg.buf = (char *)tx_buf;
	msg.scl_rate = scl_rate;
//	msg.udelay = client->udelay;

	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	return (ret == 1) ? count : ret;

}
EXPORT_SYMBOL(bq27320_i2c_master_reg8_write);
static int bq27320_read(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret;
	mutex_lock(&battery_mutex);
	ret = bq27320_i2c_master_reg8_read(client, reg, buf, len, BQ27320_SPEED);
	mutex_unlock(&battery_mutex);
	return ret; 
}

static int bq27320_write(struct i2c_client *client, u8 reg, u8 const buf[], unsigned len)
{
	int ret; 
	mutex_lock(&battery_mutex);
	ret = bq27320_i2c_master_reg8_write(client, reg, buf, (int)len, BQ27320_SPEED);
	mutex_unlock(&battery_mutex);
	return ret;
}
#if 1
static int bq27320_read_and_compare(struct i2c_client *client, u8 reg, u8 *pSrcBuf, u8 *pDstBuf, u16 len)
{
	int i2c_ret;

	i2c_ret = bq27320_read(client, reg, pSrcBuf, len);
	if(i2c_ret < 0)
	{
		printk(KERN_ERR "[%s,%d,%08x] bq27320_read failed\n",__FUNCTION__,__LINE__,reg);
		return i2c_ret;
	}

	i2c_ret = strncmp(pDstBuf, pSrcBuf, len);

	return i2c_ret;
}

static int bq27320_atoi(const char *s)
{
	int k = 0;

	k = 0;
	while (*s != '\0' && *s >= '0' && *s <= '9') {
		k = 10 * k + (*s - '0');
		s++;
	}
	return k;
}

static unsigned long bq27320_strtoul(const char *cp, unsigned int base)
{
	unsigned long result = 0,value;

	while (isxdigit(*cp) && (value = isdigit(*cp) ? *cp-'0' : (islower(*cp)
		? toupper(*cp) : *cp)-'A'+10) < base) 
	{
		result = result*base + value;
		cp++;
	}

	return result;
}

static int bq27320_firmware_program(struct i2c_client *client, const unsigned char *pgm_data, unsigned int filelen)
{
	unsigned int i = 0, j = 0, ulDelay = 0, ulReadNum = 0;
	unsigned int ulCounter = 0, ulLineLen = 0;
	unsigned char temp = 0; 
	unsigned char *p_cur;
	unsigned char pBuf[BSP_MAX_ASC_PER_LINE] = { 0 };
	unsigned char p_src[BSP_I2C_MAX_TRANSFER_LEN] = { 0 };
	unsigned char p_dst[BSP_I2C_MAX_TRANSFER_LEN] = { 0 };
	unsigned char ucTmpBuf[16] = { 0 };

bq275x0_firmware_program_begin:
	if(ulCounter > 10)
	{
		return -1;
	}
	
	p_cur = (unsigned char *)pgm_data;		 

	while(1)
	{
		if((p_cur - pgm_data) >= filelen)
		{
			printk("Download success\n");
			break;
		}
			
		while (*p_cur == '\r' || *p_cur == '\n')
		{
			p_cur++;
		}
		
		i = 0;
		ulLineLen = 0;

		memset(p_src, 0x00, sizeof(p_src));
		memset(p_dst, 0x00, sizeof(p_dst));
		memset(pBuf, 0x00, sizeof(pBuf));

		/*获取一行数据，去除空格*/
		while(i < BSP_MAX_ASC_PER_LINE)
		{
			temp = *p_cur++;	  
			i++;
			if(('\r' == temp) || ('\n' == temp))
			{
				break;	
			}
			if(' ' != temp)
			{
				pBuf[ulLineLen++] = temp;
			}
		}

		
		p_src[0] = pBuf[0];
		p_src[1] = pBuf[1];

		if(('W' == p_src[0]) || ('C' == p_src[0]))
		{
			for(i=2,j=0; i<ulLineLen; i+=2,j++)
			{
				memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
				memcpy(ucTmpBuf, pBuf+i, 2);
				p_src[2+j] = bq27320_strtoul(ucTmpBuf, 16);
			}

			temp = (ulLineLen -2)/2;
			ulLineLen = temp + 2;
		}
		else if('X' == p_src[0])
		{
			memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
			memcpy(ucTmpBuf, pBuf+2, ulLineLen-2);
			ulDelay = bq27320_atoi(ucTmpBuf);
		}
		else if('R' == p_src[0])
		{
			memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
			memcpy(ucTmpBuf, pBuf+2, 2);
			p_src[2] = bq27320_strtoul(ucTmpBuf, 16);
			memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
			memcpy(ucTmpBuf, pBuf+4, 2);
			p_src[3] = bq27320_strtoul(ucTmpBuf, 16);
			memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
			memcpy(ucTmpBuf, pBuf+6, ulLineLen-6);
			ulReadNum = bq27320_atoi(ucTmpBuf);
		}

		if(':' == p_src[1])
		{
			switch(p_src[0])
			{
				case 'W' :

					#if 0
					printk("W: ");
					for(i=0; i<ulLineLen-4; i++)
					{
						printk("%x ", p_src[4+i]);
					}
					printk(KERN_ERR "\n");
					#endif					  

					if(bq27320_write(client, p_src[3], &p_src[4], ulLineLen-4) < 0)
					{
						 printk(KERN_ERR "[%s,%d] bq27320_write failed len=%d reg+ %08x\n",__FUNCTION__,__LINE__,ulLineLen-4,p_src[3]);						  
					}

					break;
				
				case 'R' :
					if(bq27320_read(client, p_src[3], p_dst, ulReadNum) < 0)
					{
						printk(KERN_ERR "[%s,%d] bq275x0_i2c_bytes_read failed\n",__FUNCTION__,__LINE__);
					}
					break;
					
				case 'C' :
					if(bq27320_read_and_compare(client, p_src[3], p_dst, &p_src[4], ulLineLen-4))
					{
						ulCounter++;
						printk(KERN_ERR "[%s,%d] bq275x0_i2c_bytes_read_and_compare failed\n",__FUNCTION__,__LINE__);
						goto bq275x0_firmware_program_begin;
					}
					break;
					
				case 'X' :					  
					mdelay(ulDelay);
					break;
				  
				default:
					return 0;
			}
		}
	  
	}

	return 0;
	
}

static int bq27320_firmware_download(struct i2c_client *client, const unsigned char *pgm_data, unsigned int len)
{
	int iRet;
	unsigned char ucTmpBuf[2] = { 0 };

	ucTmpBuf[0] = BSP_ENTER_ROM_MODE_DATA & 0x00ff;
	ucTmpBuf[1] = (BSP_ENTER_ROM_MODE_DATA>>8) & 0x00ff;
	
	/*Enter Rom Mode */
	iRet = bq27320_write(client, BSP_ENTER_ROM_MODE_CMD, &ucTmpBuf[0], 2);
	if(0 > iRet)
	{
		printk(KERN_ERR "[%s,%d] bq27320_write failed\n",__FUNCTION__,__LINE__);
	}
	mdelay(10);

	/*change i2c addr*/
	g_bq27320_i2c_client->addr = BSP_ROM_MODE_I2C_ADDR;

	/*program bqfs*/
	iRet = bq27320_firmware_program(g_bq27320_i2c_client, pgm_data, len);
	if(0 != iRet)
	{
		printk(KERN_ERR "[%s,%d] bq275x0_firmware_program failed\n",__FUNCTION__,__LINE__);
	}

	/*change i2c addr*/
	g_bq27320_i2c_client->addr = BSP_NORMAL_MODE_I2C_ADDR;

	return iRet;
	
}

static int bq27320_update_firmware(struct i2c_client *client, const char *pFilePath) 
{
	char *buf;
	struct file *filp;
	struct inode *inode = NULL;
	mm_segment_t oldfs;
	unsigned int length;
	int ret = 0;

	/* open file */
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	filp = filp_open(pFilePath, O_RDONLY, S_IRUSR);
	if (IS_ERR(filp)) 
	{
		printk(KERN_ERR "[%s,%d] filp_open failed\n",__FUNCTION__,__LINE__);
		set_fs(oldfs);
		return -1;
	}

	if (!filp->f_op) 
	{
		printk(KERN_ERR "[%s,%d] File Operation Method Error\n",__FUNCTION__,__LINE__); 	   
		filp_close(filp, NULL);
		set_fs(oldfs);
		return -1;
	}

	inode = filp->f_path.dentry->d_inode;
	if (!inode) 
	{
		printk(KERN_ERR "[%s,%d] Get inode from filp failed\n",__FUNCTION__,__LINE__);			
		filp_close(filp, NULL);
		set_fs(oldfs);
		return -1;
	}

	/* file's size */
	length = i_size_read(inode->i_mapping->host);
	printk("bq27320 firmware image size is %d \n",length);
	if (!( length > 0 && length < BSP_FIRMWARE_FILE_SIZE))
	{
		printk(KERN_ERR "[%s,%d] Get file size error\n",__FUNCTION__,__LINE__);
		filp_close(filp, NULL);
		set_fs(oldfs);
		return -1;
	}

	/* allocation buff size */
	buf = vmalloc(length+(length%2));		/* buf size if even */
	if (!buf) 
	{
		printk(KERN_ERR "[%s,%d] Alloctation memory failed\n",__FUNCTION__,__LINE__);
		filp_close(filp, NULL);
		set_fs(oldfs);
		return -1;
	}

	/* read data */
	if (filp->f_op->read(filp, buf, length, &filp->f_pos) != length) 
	{
		printk(KERN_ERR "[%s,%d] File read error\n",__FUNCTION__,__LINE__);
		filp_close(filp, NULL);
		filp_close(filp, NULL);
		set_fs(oldfs);
		vfree(buf);
		return -1;
	}

	ret = bq27320_firmware_download(client, (const char*)buf, length);

	//if(0 == ret)
		//ret = 1;

	filp_close(filp, NULL);
	set_fs(oldfs);
	vfree(buf);
	
	return ret;
}

static u8 get_child_version(void)
{
	u8 data[32];
	
	data[0] = 0x40;
	if(bq27320_write(g_bq27320_i2c_client, 0x3e, data, 1) < 0)
		return -1;
	mdelay(2);

	data[0] = 0x40;
	if(bq27320_write(g_bq27320_i2c_client, 0x3f, data, 1) < 0)
		return -1;
	mdelay(2);

	bq27320_read(g_bq27320_i2c_client, 0x40, data, 8);
	
	return data[2];
}

static ssize_t bq27320_attr_store(struct device_driver *driver,const char *buf, size_t count)
{
	int iRet = 0;
	unsigned char path_image[255];

	if(NULL == buf || count >255 || count == 0 || strnchr(buf, count, 0x20))
		return -1;
	memcpy (path_image, buf,  count);
	/* replace '\n' with  '\0'	*/ 
	if((path_image[count-1]) == '\n')
		path_image[count-1] = '\0'; 
	else
		path_image[count] = '\0';		

	/*enter firmware bqfs download*/
	virtual_battery_enable = 1;
	iRet = bq27320_update_firmware(g_bq27320_i2c_client, path_image);		
	msleep(3000);
	virtual_battery_enable = 0;

	if (iRet == 0) {
		pr_err("Update firemware finish, then update battery status...");		
		return count;
	}

	return iRet;
}

static ssize_t bq27320_attr_show(struct device_driver *driver, char *buf)
{
	u8 ver;
	
	if(NULL == buf)
	{
		return -1;
	}

	ver = get_child_version();

	if(ver < 0)
	{
		return sprintf(buf, "%s", "Coulometer Damaged or Firmware Error");
	}
	else
	{	 
	
		return sprintf(buf, "download firemware is %x", ver);
	}

}

static DRIVER_ATTR(state, 0664, bq27320_attr_show, bq27320_attr_store);


#endif
/*
 * Return the battery temperature in tenths of degree Celsius
 * Or < 0 if something fails.
 */
static int bq27320_battery_temperature(struct bq27320_device_info *di)
{
	int ret;
	int temp = 0;
	u8 buf[2];

	#if defined (CONFIG_NO_BATTERY_IC)
	return 258;
	#endif

	if(virtual_battery_enable == 1)
		return 125/*258*/;
	ret = bq27320_read(di->client,BQ27x00_REG_TEMP,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading temperature\n");
		return ret;
	}
	temp = get_unaligned_le16(buf);
	temp = temp - 2731;
	DBG("Enter:%s--temp = %d\n",__FUNCTION__,temp);
	di ->bat_tempreture = temp;
	return temp;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */
static int bq27320_battery_voltage(struct bq27320_device_info *di)
{
	int ret;
	u8 buf[2];
	int volt = 0;

	#if defined (CONFIG_NO_BATTERY_IC)
		return 4000000;
	#endif
	if(virtual_battery_enable == 1)
		return 2000000/*4000000*/;

	ret = bq27320_read(di->client,BQ27x00_REG_VOLT,buf,2); 
	if (ret<0) {
		dev_err(di->dev, "error reading voltage\n");
		return ret;
	}
	volt = get_unaligned_le16(buf);

	//bp27510 can only measure one li-lion bat
	if(di->bat_num == 2){
		volt = volt * 1000 * 2;
	}else{
		volt = volt * 1000;
	}

	DBG("Enter:%s--volt = %d\n",__FUNCTION__,volt);
	return volt;
}

/*
 * Return the battery average current
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int bq27320_battery_current(struct bq27320_device_info *di)
{
	int ret;
	int curr = 0;
	u8 buf[2];

	#if defined (CONFIG_NO_BATTERY_IC)
		return 22000;
	#endif
	if(virtual_battery_enable == 1)
		return 11000/*22000*/;
	ret = bq27320_read(di->client,BQ27x00_REG_AI,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading current\n");
		return 0;
	}

	curr = get_unaligned_le16(buf);
	DBG("curr = %x \n",curr);
	if(curr>0x8000){
		curr = 0xFFFF^(curr-1);
		DBG("curr = -%d \n",curr*1000);
	}
	else
		DBG("curr = %d \n",curr*1000);
	curr = curr * 1000;
	return curr;
}

/*
 * Return the battery Relative State-of-Charge
 * Or < 0 if something fails.
 
 */
static int bq27320_battery_rsoc(struct bq27320_device_info *di)
{
	int ret;
	int rsoc = 0;
	#if 0
	int nvcap = 0,facap = 0,remcap=0,fccap=0,full=0,cnt=0;
	int art = 0, artte = 0, ai = 0, tte = 0, ttf = 0, si = 0;
	int stte = 0, mli = 0, mltte = 0, ae = 0, ap = 0, ttecp = 0, cc = 0;
	#endif
	u8 buf[2];

	#if defined (CONFIG_NO_BATTERY_IC)
		return 50;
	#endif
	if(virtual_battery_enable == 1)
		return 50/*100*/;
	
	ret = bq27320_read(di->client,BQ27320_REG_SOC,buf,2); 
	if (ret<0) {
		dev_err(di->dev, "error reading relative State-of-Charge\n");
		return ret;
	}
	rsoc = get_unaligned_le16(buf);
	DBG("Enter:%s --rsoc = %d\n",__FUNCTION__,rsoc);

	#if defined (CONFIG_NO_BATTERY_IC)
	rsoc = 100;
	#endif
	#if 0     //other register information, for debug use
	ret = bq27320_read(di->client,0x0c,buf,2);		//NominalAvailableCapacity
	nvcap = get_unaligned_le16(buf);
	DBG("\nEnter:%s %d--nvcap = %d\n",__FUNCTION__,__LINE__,nvcap);
	ret = bq27320_read(di->client,0x0e,buf,2);		//FullAvailableCapacity
	facap = get_unaligned_le16(buf);
	DBG("Enter:%s %d--facap = %d\n",__FUNCTION__,__LINE__,facap);
	ret = bq27320_read(di->client,0x10,buf,2);		//RemainingCapacity
	remcap = get_unaligned_le16(buf);
	DBG("Enter:%s %d--remcap = %d\n",__FUNCTION__,__LINE__,remcap);
	ret = bq27320_read(di->client,0x12,buf,2);		//FullChargeCapacity
	fccap = get_unaligned_le16(buf);
	DBG("Enter:%s %d--fccap = %d\n",__FUNCTION__,__LINE__,fccap);
	ret = bq27320_read(di->client,0x3c,buf,2);		//DesignCapacity
	full = get_unaligned_le16(buf);
	DBG("Enter:%s %d--DesignCapacity = %d\n",__FUNCTION__,__LINE__,full);
	
	buf[0] = 0x00;						//CONTROL_STATUS
	buf[1] = 0x00;
	bq27320_write(di->client,0x00,buf,2);
	ret = bq27320_read(di->client,0x00,buf,2);
	cnt = get_unaligned_le16(buf);
	DBG("Enter:%s %d--Control status = %x\n",__FUNCTION__,__LINE__,cnt);

	ret = bq27320_read(di->client,0x02,buf,2);		//AtRate
	art = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AtRate = %d\n",__FUNCTION__,__LINE__,art);
	ret = bq27320_read(di->client,0x04,buf,2);		//AtRateTimeToEmpty
	artte = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AtRateTimeToEmpty = %d\n",__FUNCTION__,__LINE__,artte);
	ret = bq27320_read(di->client,0x14,buf,2);		//AverageCurrent
	ai = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AverageCurrent = %d\n",__FUNCTION__,__LINE__,ai);
	ret = bq27320_read(di->client,0x16,buf,2);		//TimeToEmpty
	tte = get_unaligned_le16(buf);
	DBG("Enter:%s %d--TimeToEmpty = %d\n",__FUNCTION__,__LINE__,tte);
	ret = bq27320_read(di->client,0x18,buf,2);		//TimeToFull
	ttf = get_unaligned_le16(buf);
	DBG("Enter:%s %d--TimeToFull = %d\n",__FUNCTION__,__LINE__,ttf);
	ret = bq27320_read(di->client,0x1a,buf,2);		//StandbyCurrent
	si = get_unaligned_le16(buf);
	DBG("Enter:%s %d--StandbyCurrent = %d\n",__FUNCTION__,__LINE__,si);
	ret = bq27320_read(di->client,0x1c,buf,2);		//StandbyTimeToEmpty
	stte = get_unaligned_le16(buf);
	DBG("Enter:%s %d--StandbyTimeToEmpty = %d\n",__FUNCTION__,__LINE__,stte);
	ret = bq27320_read(di->client,0x1e,buf,2);		//MaxLoadCurrent
	mli = get_unaligned_le16(buf);
	DBG("Enter:%s %d--MaxLoadCurrent = %d\n",__FUNCTION__,__LINE__,mli);
	ret = bq27320_read(di->client,0x20,buf,2);		//MaxLoadTimeToEmpty
	mltte = get_unaligned_le16(buf);
	DBG("Enter:%s %d--MaxLoadTimeToEmpty = %d\n",__FUNCTION__,__LINE__,mltte);
	ret = bq27320_read(di->client,0x22,buf,2);		//AvailableEnergy
	ae = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AvailableEnergy = %d\n",__FUNCTION__,__LINE__,ae);
	ret = bq27320_read(di->client,0x24,buf,2);		//AveragePower
	ap = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AveragePower = %d\n",__FUNCTION__,__LINE__,ap);
	ret = bq27320_read(di->client,0x26,buf,2);		//TTEatConstantPower
	ttecp = get_unaligned_le16(buf);
	DBG("Enter:%s %d--TTEatConstantPower = %d\n",__FUNCTION__,__LINE__,ttecp);
	ret = bq27320_read(di->client,0x2a,buf,2);		//CycleCount
	cc = get_unaligned_le16(buf);
	DBG("Enter:%s %d--CycleCount = %d\n",__FUNCTION__,__LINE__,cc);
	#endif
	return rsoc;
}

static int bq27320_battery_status(struct bq27320_device_info *di,
				  union power_supply_propval *val)
{
	u8 buf[2];
	int flags = 0;
	int status;
	int ret;

	#if defined (CONFIG_NO_BATTERY_IC)
		val->intval = POWER_SUPPLY_STATUS_FULL;
	return 0;
	#endif

	if(virtual_battery_enable == 1)
	{
		val->intval = POWER_SUPPLY_STATUS_FULL;
		return 0;
	}
	
	ret = bq27320_read(di->client,BQ27x00_REG_BATTERYSTATUS, buf, 2);
	if (ret < 0) {
		dev_err(di->dev, "error reading flags\n");
		return ret;
	}
	
	flags = get_unaligned_le16(buf);
	DBG("Enter:%s %d--status = %x\n",__FUNCTION__,__LINE__,flags);
	
	if ((flags & BQ27320_BATTERYSTATUS_FC) ||(bq27320_di ->rsoc ==100)){
		status = POWER_SUPPLY_STATUS_FULL;
		di->soc_full = 1;
		DBG("status =POWER_SUPPLY_STATUS_FULL \n");
	}
	else if (flags & BQ27320_BATTERYSTATUS_DSC){
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		DBG("status =POWER_SUPPLY_STATUS_DISCHARGING \n");
	}
	else {
		status = POWER_SUPPLY_STATUS_CHARGING;
		DBG("status =POWER_SUPPLY_STATUS_CHARGING \n");
	}	

	if (((status==POWER_SUPPLY_STATUS_FULL)||(status==POWER_SUPPLY_STATUS_CHARGING)) 
		&& ((bq27320_di->ac_charging ==0) && (bq27320_di->usb_charging ==0) ))
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else if ((status==POWER_SUPPLY_STATUS_DISCHARGING) && ((bq27320_di->ac_charging ==1) || (bq27320_di->usb_charging ==1) ))
		status = POWER_SUPPLY_STATUS_CHARGING;

	di ->bat_status = status;
	val->intval = status;
	return 0;
}

static int bq27320_health_status(struct bq27320_device_info *di,
				  union power_supply_propval *val)
{
	u8 buf[2];
	int flags = 0;
	int status;
	int ret;
	
	#if defined (CONFIG_NO_BATTERY_IC)
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
	return 0;
	#endif

	if(virtual_battery_enable == 1)
	{
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	}
	ret = bq27320_read(di->client,BQ27x00_REG_BATTERYSTATUS, buf, 2);
	if (ret < 0) {
		dev_err(di->dev, "error reading flags\n");
		return ret;
	}
	flags = get_unaligned_le16(buf);
	DBG("Enter:%s--health status = %x\n",__FUNCTION__,flags);
	if ((flags & BQ27320_BATTERYSTATUS_OTD)||(flags & BQ27320_BATTERYSTATUS_OTC)){
		status = POWER_SUPPLY_HEALTH_OVERHEAT;
		DBG("health =POWER_SUPPLY_HEALTH_OVERHEAT \n");
	}
	else{
		status = POWER_SUPPLY_HEALTH_GOOD;
		DBG("health =POWER_SUPPLY_HEALTH_GOOD \n");
	}

	val->intval = status;
	return 0;
}


/*
 * Read a time register.
 * Return < 0 if something fails.
 */
static int bq27320_battery_time(struct bq27320_device_info *di, int reg,
				union power_supply_propval *val)
{
	u8 buf[2];
	int tval = 0;
	int ret;

	ret = bq27320_read(di->client,reg,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading register %02x\n", reg);
		return ret;
	}
	tval = get_unaligned_le16(buf);
	DBG("Enter:%s--tval=%d\n",__FUNCTION__,tval);
	if (tval == 65535)
		return -ENODATA;

	val->intval = tval * 60;
	DBG("Enter:%s val->intval = %d\n",__FUNCTION__,val->intval);
	return 0;
}

#define to_bq27320_device_info(x) container_of((x), \
				struct bq27320_device_info, bat);

static int bq27320_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret = 0;
	struct bq27320_device_info *di = to_bq27320_device_info(psy);
	DBG("Enter:%s %d psp= %d\n",__FUNCTION__,__LINE__,psp);
	
	switch (psp) {
	
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq27320_battery_status(di, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
			val->intval = bq27320_di ->ac_charging;	
		else if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = bq27320_di ->usb_charging;	
      		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = bq27320_battery_voltage(di);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
			val->intval = bq27320_battery_voltage(di);
			val->intval = val->intval <= 0 ? 0 : 1;
			di->bat_present =val->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = bq27320_battery_current(di);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = bq27320_battery_rsoc(di);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bq27320_battery_temperature(di);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;	
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq27320_health_status(di, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		ret = bq27320_battery_time(di, BQ27x00_REG_TTE, val);
		break;
//	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
//		ret = bq27320_battery_time(di, BQ27x00_REG_TTECP, val);
//		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		ret = bq27320_battery_time(di, BQ27x00_REG_TTF, val);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int  bq27320_get_usb_status(void){
	int usb_status = 0; // 0--dischage ,1 ---usb charge, 2 ---ac charge
	int vbus_status =  dwc_vbus_status();
	
	if (1 == vbus_status) {
	//	if (0 == get_gadget_connect_flag()){ 
//			usb_status = 2; // non-standard AC charger
//		}else
			usb_status = 1;	// connect to pc	
	}else{
		if (2 == vbus_status) 
			usb_status = 2; //standard AC charger
		else
			usb_status = 0; 
	}
	return usb_status;
}
static int bq27320_battery_get_status(void)
{
	int charge_on = 0;
	int usb_ac_charging = 0;
	
	if(dwc_otg_check_dpdm(0) == 0){
		bq27320_di->usb_charging = 0;
		bq27320_di->ac_charging = 0;
	}else if(dwc_otg_check_dpdm(0) == 1){
		bq27320_di->usb_charging = 1;
		bq27320_di->ac_charging = 0;
	}else if(dwc_otg_check_dpdm(0) == 2 || dwc_otg_check_dpdm(0) == 3){
		bq27320_di->usb_charging = 0;
		bq27320_di->ac_charging = 1;
	}
	if(( 1 == bq27320_di->usb_charging)||(1 == bq27320_di ->ac_charging))
		charge_on =1;
	
	if (charge_on == 0){
		usb_ac_charging = bq27320_get_usb_status(); //0 --discharge, 1---usb charging,2----AC charging;
		if(1 == usb_ac_charging){
			bq27320_di->usb_charging = 1;
			bq27320_di->ac_charging = 0;
		}
		else if(2 == usb_ac_charging){
			bq27320_di->usb_charging = 0;
			bq27320_di->ac_charging = 1;	
		}
		else{
			bq27320_di->usb_charging = 0;
			bq27320_di->ac_charging = 0;	
		}
	}
	return 0;

}
static int rk3190_ac_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	int ret = 0;
	
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
			val->intval = bq27320_di ->ac_charging;	
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	bq27320_di->online =val->intval;
	DBG("%s:rk3190_ac_get_property %d val->intval = %d\n",__FUNCTION__,__LINE__,val->intval);

	return ret;
}

static int rk3190_usb_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	int ret = 0;
	
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	 if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = bq27320_di ->usb_charging;	
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	bq27320_di->online =val->intval;
	DBG("%s:%d rk3190_usb_get_property  val->intval = %d\n",__FUNCTION__,__LINE__,val->intval);

	return ret;
}

static void bq27320_powersupply_init(struct bq27320_device_info *di)
{
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = bq27320_battery_props;
	di->bat.num_properties = ARRAY_SIZE(bq27320_battery_props);
	di->bat.get_property = bq27320_battery_get_property;
	
	di->usb.name = "bq27320-usb";
	di->usb.type = POWER_SUPPLY_TYPE_USB;
	di->usb.properties = rk3190_usb_props;
	di->usb.num_properties = ARRAY_SIZE(rk3190_usb_props);
	di->usb.get_property = rk3190_usb_get_property;

	di->ac.name = "bq27320-ac";
	di->ac.type = POWER_SUPPLY_TYPE_MAINS;
	di->ac.properties = rk3190_ac_props;
	di->ac.num_properties = ARRAY_SIZE(rk3190_ac_props);
	di->ac.get_property =rk3190_ac_get_property;
}


static void bq27320_battery_update_status(struct bq27320_device_info *di)
{

	bq27320_battery_get_status();	
	power_supply_changed(&di->bat);
	power_supply_changed(&di->usb);
	power_supply_changed(&di->ac);	
}
#ifdef CONFIG_CHARGER_BQ24161
#include <linux/power/bq24296_charger.h>
static void bq27320_for_charging(struct bq27320_device_info *di)
{			
	if ((bq27320_di->usb_charging  || bq27320_di->ac_charging) && (di->bat_present == 1)) {//有充电器接入，且电池存在
		//tempreture out of safe range, do not charge
		if ((di->bat_tempreture < BATTERY_LOW_TEMPRETURE) 
			|| (di->bat_tempreture > BATTERY_HIGH_TEMPRETURE)) {
			printk(KERN_INFO "battery tempreture is out of safe range\n");
			di->soc_full = 0;
			bq24296_charge_otg_en(0, 0);//disable charging
			return ;
		}
/*
		if (otg_is_host_mode() || mhl_vbus_power_on()) {
			printk("**********usb is otg mode, otg lock***********\n");
			bq24296_charge_otg_en(1, 1, 1);
		}
		
		else 
			bq24296_charge_otg_en(1, 1, 0);
*/
		if (di->bat_status==POWER_SUPPLY_STATUS_FULL) {//充满
			di->soc_full = 1;
			printk(KERN_INFO "**********charger ok*********\n");
		}
		else if ((di->soc_full==1) && (di->rsoc<=BATTERY_RECHARGER_CAPACITY)) {//已充满过，且电量小于95%，需要续充
			bq24296_charge_otg_en(0, 0);
			msleep(1000);
			bq24296_charge_otg_en(1, 0);
			di->soc_full = 0;
			printk(KERN_INFO "**********recharger*********\n");
		}
	}
	/*
	else if (otg_is_host_mode() || mhl_vbus_power_on()) {
		bq24296_charge_otg_en(1, 0, 1);
	}
	else {
		di->bat_full = 0;
		bq24296_charge_otg_en();
	}
	*/
}

#endif


static void bq27320_battery_work(struct work_struct *work)
{
	struct bq27320_device_info *di = container_of(work, struct bq27320_device_info, work.work); 
	bq27320_battery_update_status(di);
	/* reschedule for the next time */
	#ifdef CONFIG_CHARGER_BQ24296
	bq27320_for_charging(di);
	#endif
	schedule_delayed_work(&di->work, 1*HZ);
}
#if 0
static void bq27320_set(void)
{
	struct bq27320_device_info *di;
        int i = 0;
	u8 buf[2];

	di = bq27320_di;
        printk("enter 0x41\n");
	buf[0] = 0x41;
	buf[1] = 0x00;
	bq27320_write(di->client,0x00,buf,2);
	
        msleep(1500);
		
        printk("enter 0x21\n");
	buf[0] = 0x21;
	buf[1] = 0x00;
	bq27320_write(di->client,0x00,buf,2);

	buf[0] = 0;
	buf[1] = 0;
	bq27320_read(di->client,0x00,buf,2);

      	// printk("%s: Enter:BUF[0]= 0X%x   BUF[1] = 0X%x\n",__FUNCTION__,buf[0],buf[1]);

      	while((buf[0] & 0x04)&&(i<5))	
       	{
        	printk("enter more 0x21 times i = %d\n",i);
              	mdelay(1000);
       		buf[0] = 0x21;
		buf[1] = 0x00;
		bq27320_write(di->client,0x00,buf,2);

		buf[0] = 0;
		buf[1] = 0;
		bq27320_read(di->client,0x00,buf,2);
		i++;
       	}

      	if(i>5)
	   	printk("write 0x21 error\n");
	else
		printk("bq27320 write 0x21 success\n");
}
#endif

static int bq27320_battery_suspend(struct i2c_client *client, pm_message_t mesg)
{
	cancel_delayed_work_sync(&bq27320_di->work);
	return 0;
}

static int bq27320_battery_resume(struct i2c_client *client)
{
	schedule_delayed_work(&bq27320_di->work, msecs_to_jiffies(50));
	return 0;
}
#if 0
static int bq27320_is_in_rom_mode(void)
{
	int ret = 0;
	unsigned char data = 0x0f;
	
	bq27320_di->client->addr = BSP_ROM_MODE_I2C_ADDR;
	ret = bq27320_write(bq27320_di->client, 0x00, &data, 1);
	bq27320_di->client->addr = BSP_NORMAL_MODE_I2C_ADDR;

	if (ret == 1)
		return 1;
	else 
		return 0;
}
#endif
#ifdef CONFIG_OF
static struct of_device_id bq27320_battery_of_match[] = {
	{ .compatible = "ti,bq27320"},
	{ },
};
MODULE_DEVICE_TABLE(of, bq27320_battery_of_match);
#endif

static int bq27320_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct bq27320_device_info *di;
	int retval = 0;
	struct device_node *bq27320_node;
	u8 buf[2];

	 DBG("%s,line=%d\n", __func__,__LINE__);
	 
	 bq27320_node = of_node_get(client->dev.of_node);
	if (!bq27320_node) {
		printk("could not find bq27320-node\n");
	}

	di = devm_kzalloc(&client->dev,sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		goto batt_failed_2;
	}
	i2c_set_clientdata(client, di);
	di->dev = &client->dev;
	di->bat.name = "bq27320-battery";
	di->client = client;
	/* 4 seconds between monotor runs interval */
	di->interval = msecs_to_jiffies(4 * 1000);
	di->ac_charging = 1;
	di->usb_charging =1;
	di->online =1;
	di->soc_full = 0;
	bq27320_di = di;
	
	mutex_init(&di->battery_mutex);
	
	retval = bq27320_read(di->client,0x00,buf,2);
	if (retval < 0){
		printk("The device is not bq27320 %d\n",retval);
		goto batt_failed_2;
	}
	
	bq27320_powersupply_init(di);
	retval = power_supply_register(&client->dev, &di->bat);
	if (retval)
		dev_err(&client->dev, "failed to register battery\n");

	retval = power_supply_register(&client->dev, &di->usb);
	if (retval)
		dev_err(&client->dev, "failed to register ac\n");

	retval = power_supply_register(&client->dev, &di->ac);
	if (retval)
		dev_err(&client->dev, "failed to register ac\n");

	 g_bq27320_i2c_client = client;
		
	retval = driver_create_file(&(bq27320_battery_driver.driver), &driver_attr_state);
	if (0 != retval)
	{
		printk("failed to create sysfs entry(state): %d\n", retval);
		goto batt_failed_3;
	}
	 
	INIT_DELAYED_WORK(&di->work, bq27320_battery_work);
//	schedule_delayed_work(&di->work, di->interval);
	schedule_delayed_work(&di->work, 1*HZ);
	dev_info(&client->dev, "support ver. %s enabled\n", DRIVER_VERSION);
	
	return 0;

batt_failed_3:
	driver_remove_file(&(bq27320_battery_driver.driver), &driver_attr_state);
batt_failed_2:
	return retval;

}

static int bq27320_battery_remove(struct i2c_client *client)
{
	struct bq27320_device_info *di = i2c_get_clientdata(client);

	driver_remove_file(&(bq27320_battery_driver.driver), &driver_attr_state);

	power_supply_unregister(&di->bat);
	power_supply_unregister(&di->usb);
	power_supply_unregister(&di->ac);
	kfree(di->bat.name);
	kfree(di->usb.name);
	kfree(di->ac.name);
	return 0;
}

static const struct i2c_device_id bq27320_id[] = {
	{ "bq27320", 0 },
};

static struct i2c_driver bq27320_battery_driver = {
	.driver = {
		.name = "bq27320",
		.of_match_table =of_match_ptr(bq27320_battery_of_match),
	},
	.probe = bq27320_battery_probe,
	.remove = bq27320_battery_remove,
	.suspend = bq27320_battery_suspend,
	.resume = bq27320_battery_resume,
	.id_table = bq27320_id,
};

static int __init bq27320_battery_init(void)
{
	int ret;
	struct proc_dir_entry * battery_proc_entry;
	
	ret = i2c_add_driver(&bq27320_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register BQ27320 driver\n");
	
	battery_proc_entry = proc_create("driver/power",0777,NULL,&battery_proc_fops);
	return ret;
}
module_init(bq27320_battery_init);

static void __exit bq27320_battery_exit(void)
{
	i2c_del_driver(&bq27320_battery_driver);
}
module_exit(bq27320_battery_exit);

MODULE_AUTHOR("Rockchip");
MODULE_DESCRIPTION("BQ27320 battery monitor driver");
MODULE_LICENSE("GPL");
