/* 
 * drivers/input/touchscreen/novatek_ts.c
 *
 * Novatek TouchScreen driver. 
 *
 * Copyright (c) 2010  Novatek tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/pm.h>
#include <linux/earlysuspend.h>
#endif

#include "linux/amlogic/input/common.h"
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>

#include "linux/amlogic/input/novatek.h"


struct tp_event {
	u16	x;
	u16	y;
    	s16 id;
	u16	pressure;
	u8  touch_point;
	u8  flag;
};

struct novatek_ts_data {
	uint16_t addr;
	uint8_t bad_data;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_reset;		//use RESET flag
	int use_irq;		//use EINT flag
	int read_mode;		//read moudle mode,20110221 by andrew
	struct hrtimer timer;
	struct work_struct  work;
	char phys[32];
	int retry;
	struct early_suspend early_suspend;
	int (*power)(struct novatek_ts_data * ts, int on);
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t max_touch_num;
	uint8_t int_trigger_type;
	uint8_t green_wake_mode;
};


#if 0
#define KFprintk(x...) printk(x)
#else
#define KFprintk(x...) do{} while(0)
#endif
//static struct early_suspend novatek_power; //for ealy suspend
static struct i2c_client *this_client;


//#include "ctp_platform_ops.h"
//#define TOUCH_KEY_LIGHT_SUPPORT
extern struct touch_pdata *ts_com;
static int SW_INT_IRQNO_PIO = 0;
static struct i2c_client *this_client;

//static int gpio_int_hdle = 0;
//static int gpio_wakeup_hdle = 0;
//static int gpio_reset_hdle = 0;
#ifdef TOUCH_KEY_LIGHT_SUPPORT
static int gpio_light_hdle = 0;
#endif
#ifdef TOUCH_KEY_SUPPORT
static int key_tp  = 0;
static int key_val = 0;
#endif
/* version list ¡êoPlease list the version when you modify
	Rocky@inet@20111022 :
	Rocky@inet@20120131	:
*/

#define VERSION "1.0"
#define PRINT_INT_INFO
//Rocky@20111019-
//static void* __iomem gpio_addr = NULL;

static int screen_max_x = 0;
static int screen_max_y = 0;
//static int revert_x_flag = 0;
//static int revert_y_flag = 0;
//static int exchange_x_y_flag = 0;
//static int ctp_reset_enable = 0;
//static int ctp_wakeup_enable = 0;
//static int ctp_havekey=0;

/* Addresses to scan */
//static union{
//	unsigned short dirty_addr_buf[2];
//	const unsigned short normal_i2c[2];
//}u_i2c_addr = {{0x00},};
	
//static __u32 twi_id = 0;

#define SCREEN_MAX_X    (screen_max_x)
#define SCREEN_MAX_Y    (screen_max_y)
#define PRESS_MAX       255
//static int key_down_status = 0;

//Rocky@20110923 +
#define ROTATION_90  1
#define SCREEN_YCOUNT screen_max_x
#define SCREEN_XCOUNT screen_max_y
//Rocky@20110923 -

//Rocky@20111019+
#define KEY_PRESS       1
#define KEY_RELEASE     0
#define CFG_NUMOFKEYS	4

//static const button[CFG_NUMOFKEYS] = {KEY_BACK,KEY_HOME,KEY_MENU,KEY_ENTER};
//static const int button[CFG_NUMOFKEYS] = {KEY_SEARCH,KEY_BACK,KEY_HOME,KEY_MENU};

//Rocky@20111019-

int m_inet_ctpState;

struct ntp_ts_data 
{	
	struct i2c_client *client;
	struct input_dev	*input_dev;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	u8  key_status;//Rocky@20111019+
	u8  old_status;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend	early_suspend;
#endif
};


/*******************************************************	
Description:
	Read data from the i2c slave device;
	This operation consisted of 2 i2c_msgs,the first msg used
	to write the operate address,the second msg used to read data.

Parameter:
	client:	i2c device.
	buf[0]:operate address.
	buf[1]~buf[len]:read data buffer.
	len:operate length.
	
return:
	numbers of i2c_msgs to transfer
*********************************************************/
static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	int retries = 0;

	msgs[0].flags=!I2C_M_RD;
	msgs[0].addr=client->addr;
	msgs[0].len=1;
	msgs[0].buf=&buf[0];

	msgs[1].flags=I2C_M_RD;
	msgs[1].addr=client->addr;
	msgs[1].len=len-1;
	msgs[1].buf=&buf[1];

	while(retries<5)
	{
		ret=i2c_transfer(client->adapter,msgs, 2);
		if(ret == 2)break;
		retries++;
	}
	return ret;
}

/*******************************************************	
Description:
	write data to the i2c slave device.

Parameter:
	client:	i2c device.
	buf[0]:operate address.
	buf[1]~buf[len]:write data buffer.
	len:operate length.
	
return:
	numbers of i2c_msgs to transfer.
*********************************************************/
static int i2c_write_bytes(struct i2c_client *client,uint8_t *data,int len)
{
	struct i2c_msg msg;
	int ret=-1;
	int retries = 0;

	msg.flags=!I2C_M_RD;
	msg.addr=client->addr;
	msg.len=len;
	msg.buf=data;		
	
	while(retries<5)
	{
		ret=i2c_transfer(client->adapter,&msg, 1);
		if(ret == 1)break;
		retries++;
	}
	return ret;
}


static void ntp_reset(void)
{
	//wake up
	pr_info("ntp_reset. \n");

	aml_gpio_direction_output(ts_com->gpio_reset, 1);
	mdelay(20);

	aml_gpio_direction_output(ts_com->gpio_reset, 0);
	mdelay(50);

	aml_gpio_direction_output(ts_com->gpio_reset, 1);
	mdelay(500);
}


#ifdef NTP_APK_DRIVER_FUNC_SUPPORT

#define NTP_DEVICE_NAME	"NVTflash"

struct ntp_flash_data
{
	rwlock_t lock;
	unsigned char bufferIndex;
	unsigned int length;
	struct i2c_client *client;
};

static int ntp_apk_mode = 0;
static struct proc_dir_entry *ntp_proc_entry;
static struct ntp_flash_data *ntp_flash_priv;

/*******************************************************
Description:
	Novatek touchscreen control driver initialize function.

Parameter:
	priv:	i2c client private struct.
	
return:
	Executive outcomes.0---succeed.
*******************************************************/
int ntp_flash_write(struct file *file, const char __user *buff, size_t count, loff_t *offp)
{
	struct i2c_msg msgs[2];	
	char *str;
	int ret=-1;
	int retries = 0;
	
	//file->private_data = (uint8_t *)kmalloc(64, GFP_KERNEL);
	//str = file->private_data;
	str = (uint8_t *)kmalloc(64, GFP_KERNEL);
	
	ret = copy_from_user(str, buff, count);

	//set addr
	if((str[0] == 0x7F)||(str[0] == (0x7F<<1)))
	{
		msgs[0].addr = NOVATEK_HW_ADDR;
	}
	else
	{
		msgs[0].addr = NOVATEK_TS_ADDR;
	}
	
	msgs[0].flags = !I2C_M_RD;
	//msgs[0].addr  = str[0];
	msgs[0].len   = str[1];
	msgs[0].buf   = &str[2];

	while(retries < 20)
	{
		ret = i2c_transfer(ntp_flash_priv->client->adapter, msgs, 1);
		if(ret == 1)
		{	
			break;
		}
		else
		{
			pr_info("write error %d\n", retries);
		}
		
		retries++;
	}

	kfree(str);
	
	return ret;
}

int ntp_flash_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	struct i2c_msg msgs[2];	 
	char *str;
	int ret = -1;
	int retries = 0;
	
	//file->private_data = (uint8_t *)kmalloc(64, GFP_KERNEL);
	//str = file->private_data;
	str = (uint8_t *)kmalloc(64, GFP_KERNEL);

	if(copy_from_user(str, buff, count))
	{
		return -EFAULT;
	}

	//set addr
	if((str[0] == 0x7F)||(str[0] == (0x7F<<1)))
	{
		msgs[0].addr = NOVATEK_HW_ADDR;
		msgs[1].addr = NOVATEK_HW_ADDR;
	}
	else
	{
		msgs[0].addr = NOVATEK_TS_ADDR;
		msgs[1].addr = NOVATEK_TS_ADDR;
	}
	
	msgs[0].flags = !I2C_M_RD;
	//msgs[0].addr  = str[0];
	msgs[0].len   = 1;
	msgs[0].buf   = &str[2];

	msgs[1].flags = I2C_M_RD;
	//msgs[1].addr  = str[0];
	msgs[1].len   = str[1]-1;
	msgs[1].buf   = &str[3];

	while(retries < 20)
	{
		ret = i2c_transfer(ntp_flash_priv->client->adapter, msgs, 2);
		if(ret == 2)
		{
			break;
		}
		else
		{
			pr_info("read error %d\n", retries);
		}
		
		retries++;
	}
	
	ret = copy_to_user(buff, str, count);

	kfree(str);
	
	return ret;
}

int ntp_flash_open(struct inode *inode, struct file *file)
{
	struct ntp_flash_data *dev;
    //pr_info("ntp_flash_open\n");
	dev = kmalloc(sizeof(struct ntp_flash_data), GFP_KERNEL);
	
	if (dev == NULL) 
	{
		return -ENOMEM;
	}

	rwlock_init(&dev->lock);
	file->private_data = dev;

	ntp_apk_mode = 1;

	return 0;
}

int ntp_flash_close(struct inode *inode, struct file *file)
{
	struct ntp_flash_data *dev = file->private_data;
	
    //pr_info("ntp_flash_close\n");

	ntp_apk_mode = 0;
	
	if (dev) 
	{
		kfree(dev);
	}
	
	return 0;   
}

struct file_operations ntp_flash_fops = 
{
	.owner = THIS_MODULE,
	.open = ntp_flash_open,
	.release = ntp_flash_close,
	.write = ntp_flash_write,
	.read = ntp_flash_read,
};

static int ntp_flash_init(void)
{		
	int ret = 0;
	
	ntp_proc_entry = proc_create(NTP_DEVICE_NAME, 0666, NULL, &ntp_flash_fops);
	if(ntp_proc_entry == NULL)
	{
		pr_info("Couldn't create proc entry!\n");
		ret = -ENOMEM;
		return ret;
	}

	ntp_flash_priv = kzalloc(sizeof(*ntp_flash_priv), GFP_KERNEL);	
	
	ntp_flash_priv->client = this_client;
	
	pr_info("NVT_flash driver loaded\n");
	
	return 0;
}

#endif // NTP_APK_DRIVER_FUNC_SUPPORT


#ifdef NVT_BOOTLOADER_FUNC_SUPPORT

enum
{
	RS_OK		= 0,
	RS_INIT_ER	= 8,
	RS_VERI_ER	= 9,
	RS_ERAS_ER	= 10,
	RS_WRDA_ER	= 11,
	RS_UPFW_ER	= 12
} ;

#if defined(NT11002)

#include "nt11002_firmware.h"

#define NTP_FLASH_SIZE		(0x4000)	// 16k bytes
#define NTP_UPDATE_SIZE		(0x3800)	// 14k bytes
#define NTP_SECTOR_SIZE		(0x800)		// 2k bytes
#define NTP_PAGE_SIZE		(0x80)		// 128 bytes
#define NTP_SECTOR_NUM		(NTP_FLASH_SIZE/NTP_SECTOR_SIZE)

#endif

#if defined(NT11003)

#include "linux/amlogic/input/RTR7_31x16_nabi2C_N327_V1.h"
#include "linux/amlogic/input/EDT7_29x19_nabi2C_N327_V1.h"

#define NTP_FLASH_SIZE		(0x8000)	// 32K bytes
#define NTP_UPDATE_SIZE		(0x8000)	// 32K bytes
#define NTP_SECTOR_SIZE		(0x80)		// 128 bytes
#define NTP_PAGE_SIZE		(0x80)		// 128 bytes
#define NTP_SECTOR_NUM		(NTP_FLASH_SIZE/NTP_SECTOR_SIZE)

#endif

unsigned char *ntp_fw_data_ptr;


static int ntp_bl_read_bytes(struct i2c_client *client, unsigned char addr, unsigned char *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret = -1;
	int retries = 0;

	msgs[0].flags= !I2C_M_RD;
	msgs[0].addr = addr;
	msgs[0].len  = 1;
	msgs[0].buf  = &buf[0];

	msgs[1].flags= I2C_M_RD;
	msgs[1].addr = addr;
	msgs[1].len  = len-1;
	msgs[1].buf  = &buf[1];

	while(retries < 3)
	{
		ret = i2c_transfer(client->adapter,msgs, 2);
		if(ret == 2)
		{
			break;
		}
		retries++;
	}
	
	return ret;
}

static int ntp_bl_write_bytes(struct i2c_client *client, unsigned char addr, unsigned char *buf, int len)
{
	struct i2c_msg msg;
	int ret = -1;
	int retries = 0;

	msg.flags = !I2C_M_RD;
	msg.addr  = addr;
	msg.len   = len;
	msg.buf   = buf;		
	
	while(retries < 3)
	{
		ret = i2c_transfer(client->adapter,&msg, 1);
		if(ret == 1)
		{
			break;
		}
		retries++;
	}
	
	return ret;
}


void ntp_bl_delay_ms(unsigned long ms)
{
	msleep(ms);
}


void ntp_bl_reset(void)
{
	ntp_reset();
}


#if defined(NT11002)

static int ntp_bl_erase_flash_sector(struct i2c_client *client, int sec)
{
	unsigned char cmd[4] = {0};
	int ret = RS_ERAS_ER;
	int count = 0;

	while(count < 5)
	{
	    cmd[0] = 0x00;
	    cmd[1] = 0x33;
	    cmd[2] = (unsigned char)((sec*NTP_SECTOR_SIZE)>>8);
	    cmd[3] = (unsigned char)((sec*NTP_SECTOR_SIZE)&0xFF);

		ret = ntp_bl_write_bytes(client, NOVATEK_HW_ADDR, cmd, 4);
		if (ret <= 0)
		{
			pr_info("I2C transfer error. Number:%d\n ", ret);
		}
		ntp_bl_delay_ms(40);

		/*Read status*/
		ret = ntp_bl_read_bytes(client, NOVATEK_HW_ADDR, cmd, 2);
		if (ret <= 0)
		{
			pr_info("I2C transfer error. Number:%d\n ", ret);
		}

		pr_info("ntp_bl_erase_flash_sector: sec = %x, cmd[1] = %x\n ", sec, cmd[1]);
		
		if(cmd[1] == 0xAA)
		{
			return 1;
		}

		count++;
	}
	return -1;
}

static int ntp_bl_erase_flash(struct i2c_client *client)
{
	int i;
	int ret= RS_ERAS_ER;
	
	for(i = 0; i < NTP_SECTOR_NUM; i++)
	{
		ret = ntp_bl_erase_flash_sector(client, i);
		if (ret <= 0)
    	{
			pr_info("ntp_bl_erase_flash error\n ", ret);
			return RS_ERAS_ER;
    	}
	}
	
	return RS_OK;
}


static int ntp_bl_verify_fw(struct i2c_client *client)
{
	unsigned int checksum1, checksum2;
	int i=0;

	// get ic code checksum
	unsigned char cmd1[3] = {0xFF, 0x0F, 0xFF};
	unsigned char cmd2[2] = {0x00, 0xE1};
	unsigned char cmd3[3] = {0xFF, 0x0A, 0x0D};
	unsigned char buf[8];

	ntp_bl_delay_ms(250);

	ntp_bl_write_bytes(client, NOVATEK_TS_ADDR, cmd1, 3);
	ntp_bl_write_bytes(client, NOVATEK_TS_ADDR, cmd2, 2);
	ntp_bl_delay_ms(1000);
	ntp_bl_write_bytes(client, NOVATEK_TS_ADDR, cmd3, 3);
	
	buf[0] = 0;
	ntp_bl_read_bytes(client, NOVATEK_TS_ADDR, buf, 3);
	
	checksum1 = (((unsigned int)buf[1])<<8)|((unsigned int)buf[2]);
	checksum1 = (checksum1&0xFFFF);


	// get bin file checksum
	checksum2 = 0;
	while(i < NTP_UPDATE_SIZE)
	{
		checksum2 += ntp_fw_data_ptr[i];
		i++;
	}

	checksum2 = (checksum2&0xFFFF);

	pr_info("ic checksum = %x, bin checksum = %x \n", checksum1, checksum2);

	if(checksum1 != checksum2)
	{
		return RS_VERI_ER;
	}
	else
	{
		return RS_OK;
	}
}

#endif

#if defined(NT11003)

static int ntp_bl_erase_flash_mass(struct i2c_client *client)
{
	unsigned char i;
	unsigned char buf[8] = {0};
	int ret = RS_ERAS_ER;
	
    buf[0] = 0x00;
    buf[1] = 0x33;
	
	for(i = 5; i > 0; i--)
	{
		buf[2] = 0x00;
		
		ret = ntp_bl_write_bytes(client, NOVATEK_HW_ADDR, buf, 3);
		if (ret <= 0)
    	{
			pr_info("I2C transfer error!\n ");
    	}
		ntp_bl_delay_ms(25);

		// Read status
   		ret = ntp_bl_read_bytes(client, NOVATEK_HW_ADDR, buf, 2);
		if (ret <= 0)
    	{
			pr_info("I2C transfer error!\n ");
    	}
   		if(buf[1] == 0xAA)
   		{
	   		ret = RS_OK;
			break;
   		}
		
		ntp_bl_delay_ms(1);
	}
	
	return ret;
}


int ntp_bl_erase_flash(struct i2c_client *client)
{
	pr_info("ntp_bl_erase_flash \n");

	return ntp_bl_erase_flash_mass(client);
}


static int ntp_bl_verify_fw(struct i2c_client *client)
{
	unsigned char buf[8];
	unsigned int checksum1, checksum2;
//	unsigned int i = 0;
	
	// get dynamic checksum from ic
	
	unsigned char cmd1[3] = {0xFF, 0x8F, 0xFF};
	unsigned char cmd2[2] = {0x00, 0xE1};
	unsigned char cmd3[3] = {0xFF, 0x8E, 0x0E};
	ntp_bl_delay_ms(250);

	ntp_bl_write_bytes(client, NOVATEK_TS_ADDR, cmd1, 3);	
	ntp_bl_write_bytes(client, NOVATEK_TS_ADDR, cmd2, 2);
	ntp_bl_delay_ms(1000);
	ntp_bl_write_bytes(client, NOVATEK_TS_ADDR, cmd3, 3);
		
	buf[0] = 0;
	ntp_bl_read_bytes(client, NOVATEK_TS_ADDR, buf, 3);
	
	
	checksum1 = (((unsigned int)buf[1])<<8)|((unsigned int)buf[2]);
	checksum1 = checksum1&0xFFFF;

	// get checksum from data file
	#if 0
	while( i< NTP_FLASH_SIZE)
	{
		checksum2 += ntp_fw_data_ptr[i];
		i++;
	}
	#endif
	checksum2 = ((unsigned short)(ntp_fw_data_ptr[NTP_FLASH_SIZE-2]<<8))|((unsigned short)ntp_fw_data_ptr[NTP_FLASH_SIZE-1]);
	checksum2 = checksum2&0xFFFF;

	pr_info("ic checksum = %x, bin checksum = %x \n", checksum1, checksum2);

	// Compare checksum
	if(checksum1 != checksum2)
	{
		return RS_VERI_ER;
	}
	else
	{
		return RS_OK;
	}
}

#endif


static int ntp_bl_init_bootloader(struct i2c_client *client)
{
	int ret = RS_OK;
	unsigned char buf[4];

	buf[0] = 0x00;
	buf[1] = 0xA5;
	ntp_bl_write_bytes(client, NOVATEK_HW_ADDR, buf, 2);
	ntp_bl_delay_ms(10);

	buf[0] = 0x00;
	buf[1] = 0x00;
	ntp_bl_write_bytes(client, NOVATEK_HW_ADDR, buf, 2);
	ntp_bl_delay_ms(2);


	ntp_bl_read_bytes(client, NOVATEK_HW_ADDR, buf, 2);
	
	pr_info("buf[1] = %x\n", buf[1]);
	
	if(buf[1] != 0xAA)
	{
		pr_info("ntp_bl_init_bootloader: Error\n");
		ret = RS_INIT_ER;
	}
	
	return ret;
}


static int ntp_bl_write_data_to_flash(struct i2c_client *client)
{
	unsigned char ret = RS_OK;
	unsigned char buf[16];
	unsigned char checksum = 0;
	int i;
//	int count = 0;
	unsigned int flash_addr;
	unsigned int page;

	pr_info("--%s--\n", __func__);
	
	
	page = 0;
	flash_addr = 0;

	do{
		pr_info("data writing ....... %d \n", page);
  
		for (i = 0; i < 16; i++)
		{
			/* Write Data to flash*/
			buf[0] = 0x00;
			buf[1] = 0x55;
			buf[2] = (unsigned char)(flash_addr >> 8);
			buf[3] = (unsigned char)flash_addr;
			buf[4] = 8;

			buf[6] = ntp_fw_data_ptr[flash_addr + 0];
			buf[7] = ntp_fw_data_ptr[flash_addr + 1];
			buf[8] = ntp_fw_data_ptr[flash_addr + 2];
			buf[9] = ntp_fw_data_ptr[flash_addr + 3];
			buf[10]= ntp_fw_data_ptr[flash_addr + 4];
			buf[11]= ntp_fw_data_ptr[flash_addr + 5];
			buf[12]= ntp_fw_data_ptr[flash_addr + 6];
			buf[13]= ntp_fw_data_ptr[flash_addr + 7];

			checksum = ~(buf[2]+buf[3]+buf[4]+buf[6]+buf[7]+buf[8]+buf[9]+buf[10]+buf[11]+buf[12]+buf[13])+1;
			buf[5] = checksum;

			ntp_bl_write_bytes(client, NOVATEK_HW_ADDR, buf, 14);
			
		//	ntp_bl_delay_ms(1);

			flash_addr += 8;	
		}
		
		ntp_bl_delay_ms(10);

		page++;
	}
	while(flash_addr < NTP_UPDATE_SIZE);
	
	return ret;		        
}


static int ntp_bl_update_fw(struct i2c_client *client)
{
//	int i;
	int ret = RS_OK;
	int count = 0;
	
	//pr_info("--%s--\n",__func__);

START_UPDATE:

	// Init bootloader
	ret = ntp_bl_init_bootloader(client);

	if(ret != RS_OK)
	{
		pr_info("Init bootloader error \n");
		return ret;
	}
	
	// Erase flash
	ret = ntp_bl_erase_flash(client);

	if(ret != RS_OK)
	{
		pr_info("Erase flash error \n");
		return ret;
	}
	
	//Write data to flash
	ret = ntp_bl_write_data_to_flash(client);
	if(ret != RS_OK)
	{
		pr_info("Write data to flash error \n");
		return ret;
	}

	// Verify update result
	ntp_bl_reset();
	if(ntp_bl_verify_fw(client) != RS_OK)
	{
		if(count < 3)
		{
			count++;
			goto START_UPDATE;
		}
		else
		{
			printk("Update firmare fail!\n");
			return RS_UPFW_ER;
		}
	}

    pr_info("--%s-- ret = %d\n", __func__, ret);
	
	return ret;
}


static int	ntp_bl_bootloader(struct i2c_client *client)
{
	int gpio_fw_value, i;
	char fw_index[2] = {0xd,0xe};
	u8 *fw_buf[] = {RTR7_31x16_nabi2C_N327_V1, EDT7_29x19_nabi2C_N327_V1};
	pr_info("ntp_bl_bootloader\n");
	ntp_fw_data_ptr = NULL;
/*
#ifdef NT11002
	ntp_fw_data_ptr = nvctp_BinaryFile;
#endif
#ifdef NT11003
	ntp_fw_data_ptr = nt11003_firmware;
#endif
*/
	gpio_fw_value = get_gpio_fw(ts_com);
	if (gpio_fw_value < 0) 
	{
		printk("faild gpio_fw_value\n");
		
		return -1;
	}
	
	for (i=0; i<sizeof(fw_index); i++) 
	{
		if (gpio_fw_value == fw_index[i]) 
		{
			ntp_fw_data_ptr = fw_buf[i];
			
			printk("use fw_buf[%d]\n", i);
			
			break;
		}
	}

	if(ntp_fw_data_ptr == NULL)
	{
		return -1;
	}
	
	if(ntp_bl_verify_fw(client) != RS_OK)	// check if need update
	{
		pr_info("ntp_bl_bootloader: Start to update firmware\n");
		
		ntp_bl_update_fw(client);
	}
	else
	{
		unsigned char buf[2] = {0};
			
		pr_info("ntp_bl_bootloader: Firmware does not need to update\n");
		
		buf[0] = 0x00;
		buf[1] = 0xA5;
		ntp_bl_write_bytes(client, NOVATEK_HW_ADDR, buf, 2);										
	}
	
	ntp_bl_reset();
	
	return 0;
}

#endif //NVT_BOOTLOADER_FUNC_SUPPORT


#ifdef NTP_CHARGER_DETECT_SUPPORT
static int ntp_charger_enable(struct i2c_client *client)
{
	u8 cmd1[3] = {0xFF, 0x0F, 0xFF};
  	u8 cmd2[2] = {0x00, 0xDE};

	i2c_write_bytes(client, cmd1, 3);
	i2c_write_bytes(client, cmd2, 2);
}

static int ntp_charger_disable(struct i2c_client *client)
{
	u8 cmd1[3] = {0xFF,0x0F,0xFF};
  	u8 cmd2[2] = {0x00,0xDF};

	i2c_write_bytes(client, cmd1, 3);
	i2c_write_bytes(client, cmd2, 2);
}

#endif // NTP_CHARGER_DETECT_SUPPORT


#ifdef CONFIG_HAS_EARLYSUSPEND
static void ntp_suspend(struct early_suspend *handler)
{
	struct ntp_ts_data *ts = i2c_get_clientdata(this_client);
#if defined(NT11002)
	uint8_t cmd1[] ={0xff, 0x0F, 0x00};
	uint8_t cmd2[]= {0x00, 0x2B};
#elif defined(NT11003)
	uint8_t cmd1[] ={0xff, 0x8F, 0xFF};
	uint8_t cmd2[]= {0x00, 0xAF};
#endif
	int ret;

	pr_info("==ntp_suspend=\n");
		
	ret = i2c_write_bytes(ts->client, cmd1, (sizeof(cmd1)/sizeof(cmd1[0])));
	if (ret <= 0)
	{
		dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
	}
	
	ret = i2c_write_bytes(ts->client, cmd2, (sizeof(cmd2)/sizeof(cmd2[0])));
	if (ret <= 0)
	{
		dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
	}

	disable_irq(SW_INT_IRQNO_PIO);
}

static void ntp_resume(struct early_suspend *handler)
{
	struct ntp_ts_data *ts = i2c_get_clientdata(this_client);
	uint8_t cmd[] = {0x00, 0x00};
	int ret;

	pr_info("==ntp_resume== \n");
	
	ret = i2c_write_bytes(ts->client, cmd, (sizeof(cmd)/sizeof(cmd[0])));
	if (ret <= 0)
	{
		dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
	}

	ntp_reset();

	enable_irq(SW_INT_IRQNO_PIO);
}
#endif  //CONFIG_HAS_EARLYSUSPEND


static int ntp_get_chipid(struct i2c_client *client)
{
	uint8_t test_data[7] = {0x00,};
	int retry = 0;
	int ret = -1;

	pr_info( "I2C communication client->addr=%d\n",client->addr);

	for(retry=0; retry < 30; retry++)
	{		
		msleep(5);
		ret = i2c_read_bytes(client, test_data, 5); 
		if (ret > 0)
			break;
		pr_info("novatek i2c test failed ,ret =%d\n",ret);

	}
	if(ret <= 0)
	{
		pr_info( "I2C communication ERROR!novatek touchscreen driver become invalid\n");	
		m_inet_ctpState=0;
		return 0;
	}
	else
	{
		pr_info( "I2C communication ok\n");
		m_inet_ctpState=1;	
		return 1;	
	}
}
#if 0
static int ntp_button_event(u8 buf1, u8 buf2)
{

		if( ((point_data[1]>> 3) > 20)&& ((point_data[1] >> 3)<25) )
		//if(point_data[1] > 20)
		{
			//report key down
			//p = &pointer[0];
			//int yd1 = input_y, xd1 = input_x;
			//pr_err(" NOVATEK KEY YD1:%d  \n ",yd1);
	
			if(point_data[1]&0x01)
			{
				//pr_info("enter key model\n");
				if(point_data[1] == 169)
				{
					key_down_status = 1;
					input_report_key(ts->input_dev,KEY_MENU,1);
					input_sync(ts->input_dev);
					pr_info("KEY_MENU is press down %d\n", KEY_MENU);
				}
				else if(point_data[1] == 185)
				{
					key_down_status = 2;
					input_report_key(ts->input_dev,KEY_BACK,1);
					input_sync(ts->input_dev);
					pr_info("KEY_BACK is press down %d\n", KEY_BACK);
				}
				else if(point_data[1] == 193)
				{
					key_down_status = 3;
					input_report_key(ts->input_dev,KEY_SEARCH,1);
					input_sync(ts->input_dev);
					pr_info("KEY_SEARCH is press down %d\n", KEY_SEARCH);
				}
				else if(point_data[1] == 177)
				{
					key_down_status = 4;
					input_report_key(ts->input_dev,KEY_HOME,1);
					input_sync(ts->input_dev);
					pr_info("KEY_HOME is press down %d\n", KEY_HOME);
				}
			} 
			else
			//report key up
			{
				if(key_down_status == 1)
				{
					key_down_status = 0;
					input_report_key(ts->input_dev,KEY_MENU,0);
					input_sync(ts->input_dev);
					pr_info("KEY_MENU is press up %d\n", KEY_MENU);
				}
				else if(key_down_status == 2)
				{
					key_down_status = 0;
					input_report_key(ts->input_dev,KEY_BACK,0);
					input_sync(ts->input_dev);
					pr_info("KEY_BACK is press up %d\n", KEY_BACK);
				}
				else if(key_down_status == 3)
				{
					key_down_status = 0;
					input_report_key(ts->input_dev,KEY_SEARCH,0);
					input_sync(ts->input_dev);
					pr_info("KEY_SEARCH is press up %d\n", KEY_SEARCH);
				}
				else if(key_down_status == 4)
				{
					key_down_status = 0;
					input_report_key(ts->input_dev,KEY_HOME,0);
					input_sync(ts->input_dev);
					pr_info("KEY_HOME is press up %d\n", KEY_HOME);
				}
			}
		}
		else

	return 0;
}
#endif
/*******************************************************
Description:
	novatek touchscreen work function.

Parameter:
	ts:	i2c client private struct.
	
return:
	Executive outcomes.0---succeed.
*******************************************************/
static void ntp_work_func(struct work_struct *work)
{
	uint8_t  buf[IIC_BYTENUM*MAX_FINGER_NUM+3]={0}; 
	int pos = 0;	
	int track_id;
	int x,y;//,p;
	int temp;
	int index = 0;
	int touch_num = 0;
	int ret = -1;
	struct ntp_ts_data *ts = i2c_get_clientdata(this_client);
	
	buf[0] = 0;
	ret = i2c_read_bytes(ts->client, buf, sizeof(buf)/sizeof(buf[0]));

	//pr_info("buf,1-4:%x,%x,%x,%x\n",buf[1],buf[2],buf[3],buf[4]);
	//pr_info("buf,5-8:%x,%x,%x,%x\n",buf[5],buf[6],buf[7],buf[8]);

	//if(ntp_button_event(buf[IIC_BYTENUM*MAX_FINGER_NUM], buf[IIC_BYTENUM*MAX_FINGER_NUM+1]))
	{
		touch_num = 0;
		for(index = 0; index < MAX_FINGER_NUM; index++)
		{
			pos = 1 + IIC_BYTENUM*index;
			
			if(((buf[pos]&0x03) == 0x01)||((buf[pos]&0x03) == 0x02))
			{
				#if defined(NT11002)
				track_id = (unsigned int)(buf[pos]>>4)-1;
				#elif defined(NT11003)
				track_id = (unsigned int)(buf[pos]>>3)-1;
				#endif

				if((track_id>=0)&&(track_id<MAX_FINGER_NUM))
				{
					touch_num++;

					x = (unsigned int)(buf[pos+1]<<4) + (unsigned int)(buf[pos+3]>>4);
					y = (unsigned int)(buf[pos+2]<<4) + (unsigned int)(buf[pos+3]&0x0f);

					#ifdef TP_COORDINATE_XY_CHANGE
					temp = x;
					x = y;
					y = temp;
					#endif

					x = x * LCD_MAX_WIDTH / TP_MAX_WIDTH;
					y = y * LCD_MAX_HEIGHT / TP_MAX_HEIGHT;
					
					#ifdef TP_COORDINATE_X_REVERSE
					x = LCD_MAX_WIDTH - x;
					#endif

					#ifdef TP_COORDINATE_Y_REVERSE
					y = LCD_MAX_HEIGHT - y;
					#endif

					//pr_info("tp down x = %d, y = %d\n", x, y);
					
					input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, track_id);
					input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
					input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
					input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
					input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 1);
					input_mt_sync(ts->input_dev);
				}
			}
		}

	//	pr_info("touch_num = %d\n", touch_num);
		
		if(touch_num == 0)
		{
			//pr_info("tp up\n");

			//input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			//input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(ts->input_dev);
		}
	}

	input_sync(ts->input_dev);

	enable_irq(SW_INT_IRQNO_PIO);
}


static irqreturn_t ntp_interrupt(int irq, void *dev_id)
{
	struct ntp_ts_data *ntp_ts = dev_id;
//	int reg_val;
	static int irq_count = 0;
	touch_dbg("irq count: %d\n", irq_count++);

#if 0//def PRINT_INT_INFO		
	pr_err("==========------NT1100x_ts TS Interrupt-----============\n"); 
#endif

	//clear the IRQ_EINT21 interrupt pending
	//reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);
	 
//	if(reg_val&(1<<(IRQ_EINT21)))
//	{
//		#if 0 
//		pr_info("==IRQ_EINT21=\n");
//		#endif
        
		disable_irq_nosync(SW_INT_IRQNO_PIO);
		if (!work_pending(&ntp_ts->pen_event_work)) 
		{
			queue_work(ntp_ts->ts_workqueue, &ntp_ts->pen_event_work);
			//writel(reg_val&(1<<(IRQ_EINT21)),gpio_addr + PIO_INT_STAT_OFFSET);
		}
		//else
		//{
		//	pr_info("work_pending *******************************\n");
		//}
		// enable_irq(SW_INT_IRQNO_PIO);
//	}
//	else
//	{
//		#ifdef PRINT_INT_INFO
//		pr_info("Other Interrupt\n");
//		#endif
		//For Debug 
		//writel(reg_val,gpio_addr + PIO_INT_STAT_OFFSET);
		//enable_irq(IRQ_EINT);
//		return IRQ_NONE;
//	}

	return IRQ_HANDLED;
}


static int ntp_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ntp_ts_data *ntp_ts;
	struct input_dev *input_dev;
	int err = 0;
//	int ret = -1;
	
	printk("ntp_probe: novatek tp probe start\n");
	
#ifdef CONFIG_OF
	if (ts_com->owner != NULL) 
	{	
		return -ENODEV;
	}
	
	memset(ts_com, 0 ,sizeof(struct touch_pdata));
	ts_com = (struct touch_pdata *)client->dev.platform_data;
	
	pr_info("ts_com->owner = %s\n", ts_com->owner);
	
	if (request_touch_gpio(ts_com) != ERR_NO)
	{
		goto exit_get_dt_failed;
	}
	
//	aml_gpio_direction_input(ts_com->gpio_interrupt);
//	aml_gpio_to_irq(ts_com->gpio_interrupt, ts_com->irq, ts_com->irq_edge);
	screen_max_x = ts_com->xres;
	screen_max_y = ts_com->yres;
#endif
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}


	ntp_ts = kzalloc(sizeof(*ntp_ts), GFP_KERNEL);
	if (!ntp_ts)
	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

//    gpio_addr = ioremap(PIO_BASE_ADDRESS, PIO_RANGE_SIZE);
//    if(!gpio_addr)
//	{
//	    err = -EIO;
//	    goto exit_ioremap_failed;	
//	}
	//printk("touch panel gpio addr: = 0x%x", gpio_addr);
	this_client = client;
	
	//printk("ntp_probe : client->addr = %d. \n", client->addr);
	this_client->addr = client->addr;
	printk("ntp_probe : client->addr = %d. \n", client->addr);
	i2c_set_clientdata(client, ntp_ts);
	
	    //config gpio:
//    gpio_int_hdle = gpio_request_ex("ctp_para", "ctp_int_port");
//    if(!gpio_int_hdle)
//	{
//        pr_warning("touch panel IRQ_EINT21_para request gpio fail!\n");
//        goto exit_gpio_int_request_failed;
//    }
//    
//    gpio_wakeup_hdle = gpio_request_ex("ctp_para", "ctp_wakeup");
//    if(!gpio_wakeup_hdle)
//	{
//        ctp_wakeup_enable = 0; 
//    }
//	else
//	{
//        ctp_wakeup_enable = 1; 
//    }
//    pr_info("ctp_wakeup_enable = %d. \n", ctp_wakeup_enable);
// 
//    gpio_reset_hdle = gpio_request_ex("ctp_para", "ctp_reset");
//    if(!gpio_reset_hdle) 
//	{
//        ctp_reset_enable = 0;
//    }
//	else
//	{
//        ctp_reset_enable = 1;
//    }
//    pr_info("ctp_reset_enable = %d. \n", ctp_reset_enable);
//
//    #ifdef TOUCH_KEY_LIGHT_SUPPORT
//    gpio_light_hdle = gpio_request_ex("ctp_para", "ctp_light");
//    #endif
       
		
	ntp_reset();
		
	if(ntp_get_chipid(client) == 0)
	{
		goto exit_gpio_int_request_failed;
	}

	//printk("==INIT_WORK=\n");
	INIT_WORK(&ntp_ts->pen_event_work, ntp_work_func);

	ntp_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!ntp_ts->ts_workqueue) 
	{
		err = -ESRCH;
		goto exit_create_singlethread;
	}

	input_dev = input_allocate_device();
	if (!input_dev) 
	{
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}
	
	ntp_ts->input_dev = input_dev;
	ntp_ts->client = client;
	
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);	
	set_bit(ABS_MT_TRACKING_ID, input_dev->absbit);
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	
	set_bit(KEY_MENU,ntp_ts->input_dev->keybit);
	set_bit(KEY_BACK,ntp_ts->input_dev->keybit);
	set_bit(KEY_SEARCH,ntp_ts->input_dev->keybit);
	set_bit(KEY_HOME,ntp_ts->input_dev->keybit);
	set_bit(KEY_VOLUMEDOWN,ntp_ts->input_dev->keybit);
	set_bit(KEY_VOLUMEUP,ntp_ts->input_dev->keybit);
	
	input_set_abs_params(input_dev,ABS_MT_POSITION_X,  0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev,ABS_MT_POSITION_Y,  0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev,ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev,ABS_MT_TRACKING_ID, 0, 4, 0, 0);

	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);

	input_dev->name = NOVATEK_TS_NAME;
	err = input_register_device(input_dev);
	if (err) 
	{
		dev_err(&client->dev, "ntp_probe: failed to register input device: %s\n", dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

#ifdef NVT_BOOTLOADER_FUNC_SUPPORT
	ntp_bl_bootloader(ntp_ts->client);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	printk("==register_early_suspend =\n");
	ntp_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ntp_ts->early_suspend.suspend = ntp_suspend;
	ntp_ts->early_suspend.resume  = ntp_resume;
	register_early_suspend(&ntp_ts->early_suspend);
#endif
		

	SW_INT_IRQNO_PIO = client->irq;
	err = request_irq(SW_INT_IRQNO_PIO, ntp_interrupt, IRQF_DISABLED, "novatek", ntp_ts);
	//err = request_irq(SW_INT_IRQNO_PIO, ntp_interrupt, IRQF_TRIGGER_LOW | IRQF_SHARED, "novatek", ntp_ts);
   
	if (err < 0)
	{
		dev_err(&client->dev, "ntp_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

#ifdef NTP_APK_DRIVER_FUNC_SUPPORT
	ntp_flash_init();
#endif

	printk("ntp_probe: novatek tp probe success\n");

	ntp_reset();
    return 0;

exit_irq_request_failed:
	cancel_work_sync(&ntp_ts->pen_event_work);
	destroy_workqueue(ntp_ts->ts_workqueue);
	enable_irq(SW_INT_IRQNO_PIO);
	
exit_input_register_device_failed:
	input_free_device(input_dev);
	
exit_input_dev_alloc_failed:
	free_irq(SW_INT_IRQNO_PIO, ntp_ts);
	
exit_gpio_int_request_failed:
exit_create_singlethread:
	printk("==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(ntp_ts);
	
//exit_ioremap_failed:
//    if(gpio_addr)
//	{
//        iounmap(gpio_addr);
//    }
	
exit_alloc_data_failed:
exit_check_functionality_failed:
exit_get_dt_failed:
	free_touch_gpio(ts_com);
	ts_com->owner = NULL;
	printk("%s: probe failed!\n", __FUNCTION__);	
	return err;
}

static int ntp_remove(struct i2c_client *client)
{
    struct ntp_ts_data *ntp_ts = i2c_get_clientdata(client);
    
    //NT1100x_set_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);	

    printk("==ntp_remove=\n");
	
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ntp_ts->early_suspend);
#endif

	free_irq(SW_INT_IRQNO_PIO, ntp_ts);
	free_touch_gpio(ts_com);
	ts_com->owner = NULL;
	input_unregister_device(ntp_ts->input_dev);
	kfree(ntp_ts);
	cancel_work_sync(&ntp_ts->pen_event_work);
	destroy_workqueue(ntp_ts->ts_workqueue);
    
    i2c_set_clientdata(client, NULL);
	
//    if(gpio_addr)
//    {
//        iounmap(gpio_addr);
//    }
//	
//    gpio_release(gpio_int_hdle, 2);
//    gpio_release(gpio_wakeup_hdle, 2);
	
    return 0;
}

/**
 * ctp_detect - Device detection callback for automatic device creation
 * return value:  
 *                    = 0; success;
 *                    < 0; err
 */
#if 0
static int ntp_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if(twi_id == adapter->nr)
	{
		pr_info("%s: Detected chip %s at adapter %d, address 0x%02x\n",
			 __func__, NOVATEK_TS_NAME, i2c_adapter_id(adapter), client->addr);
		
		strlcpy(info->type, NOVATEK_TS_NAME, I2C_NAME_SIZE);
		return 0;
	}
	else
	{
		return -ENODEV;
	}
}
#endif

static const struct i2c_device_id ntp_id[] = {
	{ NOVATEK_TS_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ntp_id);

static struct i2c_driver ntp_driver = 
{
	.class 		= I2C_CLASS_HWMON,
	.probe		= ntp_probe,
	.remove		= ntp_remove,
	.id_table	= ntp_id,
	.driver	= 
	{
		.name	= NOVATEK_TS_NAME,
		.owner	= THIS_MODULE,
	},
	//.address_list	= u_i2c_addr.normal_i2c,
};

static int __init ntp_init(void)
{ 
	int ret = -1;
//	int ctp_used = -1;
//	char name[I2C_NAME_SIZE];
//	__u32 twi_addr = 0;
//	script_parser_value_type_t type = SCIRPT_PARSER_VALUE_TYPE_STRING;
//
//	pr_err("=========Novatek_TouchDriver============\n");	
//	pr_err("VERSION =%s\n",VERSION);
//		
//	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_used", &ctp_used, 1))
//	{
//		pr_err("%s: script_parser_fetch err. \n", __func__);
//		goto script_parser_fetch_err;
//	}
//	
//	if(1 != ctp_used)
//	{
//		pr_err("%s: ctp_unused. \n",  __func__);
//		//ret = 1;
//		return ret;
//	}
//	
//	if(SCRIPT_PARSER_OK != script_parser_fetch_ex("ctp_para", "ctp_name", (int *)(&name), &type, sizeof(name)/sizeof(int)))
//	{
//		pr_err("%s: script_parser_fetch err. \n", __func__);
//		goto script_parser_fetch_err;
//	}
//
//	if(strcmp(NOVATEK_TS_NAME, name))
//	{
//		pr_err("%s: name %s does not match NOVATEK_TS_NAME. \n", __func__, name);
//		pr_err(NOVATEK_TS_NAME);
//		//ret = 1;
//		return ret;
//	}
//	
//	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_twi_addr", &twi_addr, sizeof(twi_addr)/sizeof(__u32)))
//	{
//		pr_err("%s: script_parser_fetch err. \n", name);
//		goto script_parser_fetch_err;
//	}
//		
//	//big-endian or small-endian?
//	//pr_info("%s: before: ctp_twi_addr is 0x%x, dirty_addr_buf: 0x%hx. dirty_addr_buf[1]: 0x%hx \n", __func__, twi_addr, u_i2c_addr.dirty_addr_buf[0], u_i2c_addr.dirty_addr_buf[1]);
//	u_i2c_addr.dirty_addr_buf[0] = twi_addr;
//	u_i2c_addr.dirty_addr_buf[1] = I2C_CLIENT_END;
//	//pr_info("%s: after: ctp_twi_addr is 0x%x, dirty_addr_buf: 0x%hx. dirty_addr_buf[1]: 0x%hx \n", __func__, twi_addr, u_i2c_addr.dirty_addr_buf[0], u_i2c_addr.dirty_addr_buf[1]);
//	//pr_info("%s: after: ctp_twi_addr is 0x%x, u32_dirty_addr_buf: 0x%hx. u32_dirty_addr_buf[1]: 0x%hx \n", __func__, twi_addr, u32_dirty_addr_buf[0],u32_dirty_addr_buf[1]);
//
//	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_screen_max_x", &screen_max_x, 1))
//	{
//		pr_err(" 1 ! ntp_ts: script_parser_fetch err. \n");
//		goto script_parser_fetch_err;
//	}
//	//pr_info("ntp_ts: screen_max_x = %d. \n", screen_max_x);
//
//	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_screen_max_y", &screen_max_y, 1))
//	{
//		pr_err(" 2 ! ntp_ts: script_parser_fetch err. \n");
//		goto script_parser_fetch_err;
//	}
//	//pr_info("ntp_ts: screen_max_y = %d. \n", screen_max_y);
//
//	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_revert_x_flag", &revert_x_flag, 1))
//	{
//		pr_err("3.! ntp_ts: script_parser_fetch err. \n");
//		goto script_parser_fetch_err;
//	}
//	//pr_info("ntp_ts: revert_x_flag = %d. \n", revert_x_flag);
// 
//	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_revert_y_flag", &revert_y_flag, 1))
//	{
//		pr_err("6 !ntp_ts: script_parser_fetch err. \n");
//		goto script_parser_fetch_err;
//	}
//	//pr_info("ntp_ts: revert_y_flag = %d. \n", revert_y_flag);
//
//	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_exchange_x_y_flag", &exchange_x_y_flag, 1))
//	{
//		pr_err("5 ! ntp_ts: script_parser_fetch err. \n");
//		goto script_parser_fetch_err;
//	}
//	//pr_info("ntp_ts: exchange_x_y_flag = %d. \n", exchange_x_y_flag);
//    
//    if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_twi_id", &twi_id, sizeof(twi_id)/sizeof(__u32)))
//	{
//		pr_err("%s: script_parser_fetch err. \n", name);
//		goto script_parser_fetch_err;
//	}
//	//pr_info("%s: ctp_twi_id is %d. \n", __func__, twi_id);
	
//	ntp_driver.detect = ntp_detect;

	ret = i2c_add_driver(&ntp_driver);

	pr_info("!!!ntp_ts: ret = %d. \n", ret);
	
//script_parser_fetch_err:
	return ret;
}

static void __exit ntp_exit(void)
{
	pr_info("==ntp_exit==\n");
	
	i2c_del_driver(&ntp_driver);
}

module_init(ntp_init);
module_exit(ntp_exit);

MODULE_AUTHOR("<x_j_chen@novatek.com.cn>");
MODULE_DESCRIPTION("Novatek TouchScreen driver");
MODULE_LICENSE("GPL");

