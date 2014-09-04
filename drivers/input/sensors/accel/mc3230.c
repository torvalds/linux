/*
 *  MCube mc3230 acceleration sensor driver
 *
 *  Copyright (C) 2011 MCube Inc.,
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * *****************************************************************************/

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
//add by cyrus.0117 start
#include <linux/sensor-dev.h> //add by cyrus.0117
#include <linux/mc3230.h>


#define MITECH_SENSOR_DBG                  

//#define MITECH_SENSOR_DBG(x...) printk(x);
#define MC32X0_XOUT_REG						0x00
#define MC32X0_YOUT_REG						0x01
#define MC32X0_ZOUT_REG						0x02
#define MC32X0_Tilt_Status_REG				0x03
#define MC32X0_Sampling_Rate_Status_REG		0x04
#define MC32X0_Sleep_Count_REG				0x05
#define MC32X0_Interrupt_Enable_REG			0x06
#define MC32X0_Mode_Feature_REG				0x07
#define MC32X0_Sample_Rate_REG				0x08
#define MC32X0_Tap_Detection_Enable_REG		0x09
#define MC32X0_TAP_Dwell_Reject_REG			0x0a
#define MC32X0_DROP_Control_Register_REG	0x0b
#define MC32X0_SHAKE_Debounce_REG			0x0c
#define MC32X0_XOUT_EX_L_REG				0x0d
#define MC32X0_XOUT_EX_H_REG				0x0e
#define MC32X0_YOUT_EX_L_REG				0x0f
#define MC32X0_YOUT_EX_H_REG				0x10
#define MC32X0_ZOUT_EX_L_REG				0x11
#define MC32X0_ZOUT_EX_H_REG				0x12
#define MC32X0_CHIP_ID_REG					0x18
#define MC32X0_RANGE_Control_REG			0x20
#define MC32X0_SHAKE_Threshold_REG			0x2B
#define MC32X0_UD_Z_TH_REG					0x2C
#define MC32X0_UD_X_TH_REG					0x2D
#define MC32X0_RL_Z_TH_REG					0x2E
#define MC32X0_RL_Y_TH_REG					0x2F
#define MC32X0_FB_Z_TH_REG					0x30
#define MC32X0_DROP_Threshold_REG			0x31
#define MC32X0_TAP_Threshold_REG			0x32
#define MC32X0_MODE_SLEEP				0x03
#define MC32X0_MODE_WAKEUP				0x01
#define MODE_CHANGE_DELAY_MS 100
#define MC3230_MODE_MITECH				0X58

#define MC3230_MODE_BITS		0x03

#define MC3230_PRECISION       8
#define MC3230_RANGE						1500000
#define MC3230_BOUNDARY        (0x1 << (MC3230_PRECISION - 1))
#define MC3230_GRAVITY_STEP    MC3230_RANGE/MC3230_BOUNDARY

/*rate*/
#define MC3230_RATE_1          0x07
#define MC3230_RATE_2          0x06
#define MC3230_RATE_4          0x05
#define MC3230_RATE_8          0x04
#define MC3230_RATE_16         0x03
#define MC3230_RATE_32         0x02
#define	MC3230_RATE_64         0x01
#define MC3230_RATE_120        0x00
//add by cyrus.0117 end
#define MC32X0_AXIS_X		   0
#define MC32X0_AXIS_Y		   1
#define MC32X0_AXIS_Z		   2
#define MC32X0_AXES_NUM 	   3
#define MC32X0_DATA_LEN 	   6
#define MC32X0_DEV_NAME 	   "MC32X0"
#define GRAVITY_EARTH_1000 		9807
#define IS_MC3230 1
#define IS_MC3210 2

#define SUPPORT_VIRTUAL_Z_SENSOR
#define LOW_RESOLUTION 1
#define HIGH_RESOLUTION 1
#define RBM_RESOLUTION 1

#define G_0		ABS_Y
#define G_1		ABS_X
#define G_2		ABS_Z
#define G_0_REVERSE	1
#define G_1_REVERSE	1
#define G_2_REVERSE	1

#ifdef SUPPORT_VIRTUAL_Z_SENSOR
#define Low_Pos_Max 127
#define Low_Neg_Max -128
#define High_Pos_Max 8191
#define High_Neg_Max -8192
#define VIRTUAL_Z	1
static int Railed = 0;
#else
#define VIRTUAL_Z	0
#endif
/*----------------------------------------------------------------------------*/

//#define CALIB_PATH				"/data/data/com.mcube.acc/files/mcube-calib.txt"
//#define DATA_PATH			    "/sdcard/mcube-register-map.txt"
//MCUBE_BACKUP_FILE
//#define BACKUP_CALIB_PATH		"/data/misc/mcube-calib.txt"
static char backup_buf[64];
//MCUBE_BACKUP_FILE

static char calib_path[] = "/data/data/com.mcube.acc/files/mcube-calib.txt";
//char *calib_path = "/data/data/com.mcube.acc/files/mcube-calib.txt";
//char data_path[] = "/sdcard/mcube-register-map.txt";
static char backup_calib_path[] = "/data/misc/mcube-calib.txt";

static GSENSOR_VECTOR3D gsensor_gain;

struct file *fd_file;
static int load_cali_flg = 0;
//MCUBE_BACKUP_FILE
static bool READ_FROM_BACKUP = false;
//MCUBE_BACKUP_FILE
static mm_segment_t oldfs;
//add by Liang for storage offset data
static unsigned char offset_buf[9]; 
static signed int offset_data[3];
s16 G_RAW_DATA[3];
static signed int gain_data[3];
static signed int enable_RBM_calibration = 0;
static unsigned char mc32x0_type;


#if 0
#define mcprintkreg(x...) printk(x)
#else
#define mcprintkreg(x...)
#endif

#if 0
#define mcprintkfunc(x...) printk(x)
#else
#define mcprintkfunc(x...)
#endif

#if 0
#define GSE_ERR(x...) 	printk(x)
#define GSE_LOG(x...) 	printk(x)

#endif


#define GSE_TAG 				 "[Gsensor] "
#define GSE_FUN(f)				 printk(KERN_INFO GSE_TAG"%s\n", __FUNCTION__)
#define GSE_ERR(fmt, args...)	 printk(KERN_INFO GSE_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
#define GSE_LOG(fmt, args...)	 printk(KERN_INFO GSE_TAG fmt, ##args)

//static int  mc3230_probe(struct i2c_client *client, const struct i2c_device_id *id);

#define MC3230_SPEED		200 * 1000
#define MC3230_DEVID		0x01
/* Addresses to scan -- protected by sense_data_mutex */
//static char sense_data[RBUFF_SIZE + 1];
static struct i2c_client *this_client;
//static struct miscdevice mc3230_device;

static DECLARE_WAIT_QUEUE_HEAD(data_ready_wq);

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend mc3230_early_suspend;
#endif
//static int revision = -1;
//static const char* vendor = "Mcube";


typedef char status_t;
/*status*/
#define MC3230_OPEN           1
#define MC3230_CLOSE          0

//by zwx
struct hwmsen_convert {
	s8 sign[3];
	u8 map[3];
};

struct mc3230_data {
	struct sensor_private_data *g_sensor_private_data;

    status_t status;
	char  curr_rate;

	s16 					offset[MC32X0_AXES_NUM+1];	/*+1: for 4-byte alignment*/
	s16 					data[MC32X0_AXES_NUM+1]; 
	s16                     cali_sw[MC32X0_AXES_NUM+1];

	struct hwmsen_convert   cvt;
};
static int MC32X0_WriteCalibration(struct i2c_client *client, int dat[MC32X0_AXES_NUM]);

static int mc3230_write_reg(struct i2c_client *client,int addr,int value);
//static char mc3230_read_reg(struct i2c_client *client,int addr);
//static int mc3230_rx_data(struct i2c_client *client, char *rxData, int length);
//static int mc3230_tx_data(struct i2c_client *client, char *txData, int length);
static int mc3230_read_block(struct i2c_client *client, char reg, char *rxData, int length);
//static int mc3230_write_block(struct i2c_client *client, char reg, char *txData, int length);
static int mc3230_active(struct i2c_client *client,int enable);
static void MC32X0_rbm(struct i2c_client *client, int enable);
static int init_3230_ctl_data(struct i2c_client *client);
#ifdef SUPPORT_VIRTUAL_Z_SENSOR
int Verify_Z_Railed(int AccData, int resolution)
{
	//printk("%s: ------------zhoukl--1--------\n",__func__);
	int status = 0;
	//printk("%s: AccData = %d   resolution=%d \n",__func__, AccData , resolution);
	if(resolution == 1) // Low resolution
	{
		if((AccData >= Low_Pos_Max && AccData >=0)|| (AccData <= Low_Neg_Max && AccData < 0))
		{
			status = 1;
			printk("%s: Railed at Low Resolution",__func__);
		}
	}
	else if (resolution == 2)	//High resolution
	{
		if((AccData >= High_Pos_Max && AccData >=0) || (AccData <= High_Neg_Max && AccData < 0))
		{
			status = 1;
			printk("%s: Railed at High Resolution",__func__);
		}
	}
	else if (resolution == 3)	//High resolution
	{
		if((AccData >= Low_Pos_Max*3 && AccData >=0) || (AccData <= Low_Neg_Max*3 && AccData < 0))
		{
			status = 1;
			printk("%s: Railed at High Resolution",__func__);
		}
	}
	else
		printk("%s, Wrong resolution",__func__);

	return status;
}

int SquareRoot(int x) 
{


    int lowerbound = 1;
    int upperbound = x;
    int root = lowerbound + (upperbound - lowerbound)/2;

	if(x < 0) return -1;
    if(x == 0 || x == 1) return x;

    while(root > x/root || root+1 <= x/(root+1))
    {
        if(root > x/root)
        {
            upperbound = root;
        } 
        else 
        {
            lowerbound = root;
        }
        root = lowerbound + (upperbound - lowerbound)/2;
    }
    printk("%s: Sqrt root is %d",__func__, root);
    return root;
}
#endif

struct file *openFile(char *path,int flag,int mode) 
{ 
	struct file *fp; 
	 
	fp=filp_open(path, flag, mode); 
	if (IS_ERR(fp) || !fp->f_op) 
	{
		//GSE_LOG("Calibration File filp_open return NULL\n");
		return NULL; 
	}
	else 
	{

		return fp; 
	}
} 
 
int readFile(struct file *fp,char *buf,int readlen) 
{ 
	if (fp->f_op && fp->f_op->read) 
		return fp->f_op->read(fp,buf,readlen, &fp->f_pos); 
	else 
		return -1; 
} 

int writeFile(struct file *fp,char *buf,int writelen) 
{ 
	if (fp->f_op && fp->f_op->write) 
		return fp->f_op->write(fp,buf,writelen, &fp->f_pos); 
	else 
		return -1; 
}
 
int closeFile(struct file *fp) 
{ 
	filp_close(fp,NULL); 
	return 0; 
} 

void initKernelEnv(void) 
{ 
	oldfs = get_fs(); 
	set_fs(KERNEL_DS);
	//printk(KERN_INFO "initKernelEnv\n");
} 
struct mc3230_data g_mc3230_data = {0};
static struct mc3230_data *get_3230_ctl_data(void)
{
	return &g_mc3230_data;
}

static int mcube_read_cali_file(struct i2c_client *client)
{
	int cali_data[3];
	int err =0;

	//printk("%s %d\n",__func__,__LINE__);
			//MCUBE_BACKUP_FILE
	READ_FROM_BACKUP = false;
	//MCUBE_BACKUP_FILE
	initKernelEnv();
	
	fd_file = openFile("/data/data/com.mcube.acc/files/mcube-calib.txt",0,0); 
	//MCUBE_BACKUP_FILE
	if (fd_file == NULL) 
	{
		fd_file = openFile(backup_calib_path, O_RDONLY, 0); 
		if(fd_file != NULL)
		{
				READ_FROM_BACKUP = true;
		}
	}
	//MCUBE_BACKUP_FILE
	if (fd_file == NULL) 
	{
		//printk("fail to open\n");
		cali_data[0] = 0;
		cali_data[1] = 0;
		cali_data[2] = 0;
		return 1;
	}
	else
	{
		printk("%s %d\n",__func__,__LINE__);
		memset(backup_buf,0,64); 
		if ((err = readFile(fd_file,backup_buf,128))>0) 
			GSE_LOG("buf:%s\n",backup_buf); 
		else 
			GSE_LOG("read file error %d\n",err); 
		printk("%s %d\n",__func__,__LINE__);

		set_fs(oldfs); 
		closeFile(fd_file); 

		sscanf(backup_buf, "%d %d %d",&cali_data[MC32X0_AXIS_X], &cali_data[MC32X0_AXIS_Y], &cali_data[MC32X0_AXIS_Z]);
		GSE_LOG("cali_data: %d %d %d\n", cali_data[MC32X0_AXIS_X], cali_data[MC32X0_AXIS_Y], cali_data[MC32X0_AXIS_Z]); 	
				
		//GSE_LOG("cali_data1: %d %d %d\n", cali_data1[MC32X0_AXIS_X], cali_data1[MC32X0_AXIS_Y], cali_data1[MC32X0_AXIS_Z]); 	
		//printk("%s %d\n",__func__,__LINE__);	  
		MC32X0_WriteCalibration(client, cali_data);
	}
	return 0;
}


static void MC32X0_rbm(struct i2c_client *client, int enable)
{
	int err; 

	if(enable == 1 )
	{

		err = mc3230_write_reg(client,0x07,0x43);
		err = mc3230_write_reg(client,0x14,0x02);
		err = mc3230_write_reg(client,0x07,0x41);

		enable_RBM_calibration =1;
		
		GSE_LOG("set rbm!!\n");

		msleep(220);
	}
	else if(enable == 0 )  
	{

		err = mc3230_write_reg(client,0x07,0x43);
		err = mc3230_write_reg(client,0x14,0x00);
		err = mc3230_write_reg(client,0x07,0x41);
		enable_RBM_calibration =0;

		GSE_LOG("clear rbm!!\n");

		msleep(220);
	}
}

/*----------------------------------------------------------------------------*/

static int MC32X0_ReadData_RBM(struct i2c_client *client, int data[MC32X0_AXES_NUM])
{   
	//u8 uData;
	u8 addr = 0x0d;
	u8 rbm_buf[MC32X0_DATA_LEN] = {0};
	int err = 0;
	if(NULL == client)
	{
		err = -EINVAL;
		return err;
	}

	err = mc3230_read_block(client, addr, rbm_buf, 0x06);

	data[MC32X0_AXIS_X] = (s16)((rbm_buf[0]) | (rbm_buf[1] << 8));
	data[MC32X0_AXIS_Y] = (s16)((rbm_buf[2]) | (rbm_buf[3] << 8));
	data[MC32X0_AXIS_Z] = (s16)((rbm_buf[4]) | (rbm_buf[5] << 8));

	GSE_LOG("rbm_buf<<<<<[%02x %02x %02x %02x %02x %02x]\n",rbm_buf[0], rbm_buf[2], rbm_buf[2], rbm_buf[3], rbm_buf[4], rbm_buf[5]);
	GSE_LOG("RBM<<<<<[%04x %04x %04x]\n", data[MC32X0_AXIS_X], data[MC32X0_AXIS_Y], data[MC32X0_AXIS_Z]);
	GSE_LOG("RBM<<<<<[%04d %04d %04d]\n", data[MC32X0_AXIS_X], data[MC32X0_AXIS_Y], data[MC32X0_AXIS_Z]);		
	return err;
}

/* AKM HW info */
#if 0
static ssize_t gsensor_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	// sprintf(buf, "%#x\n", revision);
	sprintf(buf, "%s.\n", vendor);
	ret = strlen(buf) + 1;

	return ret;
}

//static DEVICE_ATTR(vendor, 0444, gsensor_vendor_show, NULL);

//static struct kobject *android_gsensor_kobj;


static int gsensor_sysfs_init(void)
{
	int ret ;

	android_gsensor_kobj = kobject_create_and_add("android_gsensor", NULL);
	if (android_gsensor_kobj == NULL) {
		printk(KERN_ERR
		       "MC3230 gsensor_sysfs_init:"\
		       "subsystem_register failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = sysfs_create_file(android_gsensor_kobj, &dev_attr_vendor.attr);   // "vendor"
	if (ret) {
		printk(KERN_ERR
		       "MC3230 gsensor_sysfs_init:"\
		       "sysfs_create_group failed\n");
		goto err4;
	}

	return 0 ;
err4:
	kobject_del(android_gsensor_kobj);
err:
	return ret ;
}
#endif

static int mc3230_read_block(struct i2c_client *client, char reg, char *rxData, int length)
{
	int ret = 0;
		*rxData = reg;
		ret = sensor_rx_data(client, rxData, length);
		//if (ret < 0)
		return ret;
}

#if 0
static char mc3230_read_reg(struct i2c_client *client,int addr)
{
	char tmp;
	int ret = 0;

	tmp = addr;
	ret = sensor_rx_data(client, &tmp, 1);
	return tmp;
}
#endif
static int mc3230_write_reg(struct i2c_client *client,int addr,int value)
{
	char buffer[3];
	int ret = 0;

	buffer[0] = addr;
	buffer[1] = value;
	ret = sensor_tx_data(client, &buffer[0], 2);
	return ret;
}

#if 0
static char mc3230_get_devid(struct i2c_client *client)
{
	mcprintkreg("mc3230 devid:%x\n",mc3230_read_reg(client,MC3230_REG_CHIP_ID));
	return mc3230_read_reg(client,MC3230_REG_CHIP_ID);
}
#endif
static int mc3230_active(struct i2c_client *client,int enable)
{
	int tmp;
	int ret = 0;
	if(enable)
		tmp = 0x01;
	else
		tmp = 0x03;
	mcprintkreg("mc3230_active %s (0x%x)\n",enable?"active":"standby",tmp);	
	ret = mc3230_write_reg(client,MC3230_REG_SYSMOD,tmp);
	return ret;
}

static int mc3230_reg_init(struct i2c_client *client)
{
	int ret = 0;
	int pcode = 0;

	mcprintkfunc("-------------------------mc3230 init------------------------\n");	
	

	mc3230_active(client,0);  // 1:awake  0:standby   ??Oo?????	mcprintkreg("mc3230 MC3230_REG_SYSMOD:%x\n",mc3230_read_reg(client,MC3230_REG_SYSMOD));

#if 0//zwx	
	 ret = mc3230_read_block(client, 0x3b, databuf, 1);
#endif
	
	pcode = sensor_read_reg(client,MC3230_REG_PRODUCT_CODE);
    //printk("mc3230_reg_init pcode=%d\n", pcode);
	if( 0x19 == pcode)
	{
		mc32x0_type = IS_MC3230;
	}
	else if ( 0x90 ==pcode)
	{
		mc32x0_type = IS_MC3210;
	}



	if ( mc32x0_type == IS_MC3230 )
	{
		gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = 86;
	}
	else if ( mc32x0_type == IS_MC3210 )
	{
		gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = 1024;
	}

	//MC32X0_rbm(client,0);



//	mc3230_active(client,1); 
//	mcprintkreg("mc3230 0x07:%x\n",mc3230_read_reg(client,MC3230_REG_SYSMOD));
//	enable_irq(client->irq);
//	msleep(50);
	return ret;
}
static int init_3230_ctl_data(struct i2c_client *client)
{
	int err;
	//char devid;
	s16 tmp, x_gain, y_gain, z_gain ;
	s32 x_off, y_off, z_off;
	struct mc3230_data* mc3230 = get_3230_ctl_data();
	
	load_cali_flg = 30;
	
	mcprintkfunc("%s enter\n",__FUNCTION__);

	//mc3230->client = client;
	//i2c_set_clientdata(client, mc3230);

	this_client = client;

	mc3230->g_sensor_private_data = (struct sensor_private_data *) i2c_get_clientdata(client);
	mc3230->curr_rate = MC3230_RATE_16;
	mc3230->status = MC3230_CLOSE;
	mc3230->cvt.sign[MC32X0_AXIS_X] = 1;
	mc3230->cvt.sign[MC32X0_AXIS_Y] = 1;
	mc3230->cvt.sign[MC32X0_AXIS_Z] = 1;
	mc3230->cvt.map[MC32X0_AXIS_X]= 0;
	mc3230->cvt.map[MC32X0_AXIS_Y]= 1;
	mc3230->cvt.map[MC32X0_AXIS_Z]= 2;
	/*
	// add by Liang for reset sensor: Fix software system reset issue!!!!!!!!!
	unsigned char buf[2];
	buf[0]=0x43;
	mc3230_write_block(client, 0x07, buf, 1);	

	buf[0]=0x80;
	mc3230_write_block(client, 0x1C, buf, 1);	
	buf[0]=0x80;
	mc3230_write_block(client, 0x17, buf, 1);	
	msleep(5);
	
	buf[0]=0x00;
	mc3230_write_block(client, 0x1C, buf, 1);	
	buf[0]=0x00;
	mc3230_write_block(client, 0x17, buf, 1);	
	*/
	sensor_write_reg(client,0x1b,0x6d);
	sensor_write_reg(client,0x1b,0x43);
	msleep(5);
	
	sensor_write_reg(client,0x07,0x43);
	sensor_write_reg(client,0x1C,0x80);
	sensor_write_reg(client,0x17,0x80);
	msleep(5);
	sensor_write_reg(client,0x1C,0x00);
	sensor_write_reg(client,0x17,0x00);
	msleep(5);


/*
	if ((err = mc3230_read_block(new_client, 0x21, offset_buf, 6))) //add by Liang for storeage OTP offsef register value
	{
		GSE_ERR("error: %d\n", err);
		return err;
	}
*/
	memset(offset_buf, 0, 9);
	offset_buf[0] = 0x21;
	err = sensor_rx_data(client, offset_buf, 9);
	if(err)
	{
		GSE_ERR("error: %d\n", err);
		return err;
	}

	tmp = ((offset_buf[1] & 0x3f) << 8) + offset_buf[0];
		if (tmp & 0x2000)
			tmp |= 0xc000;
		x_off = tmp;
					
	tmp = ((offset_buf[3] & 0x3f) << 8) + offset_buf[2];
		if (tmp & 0x2000)
			tmp |= 0xc000;
		y_off = tmp;
					
	tmp = ((offset_buf[5] & 0x3f) << 8) + offset_buf[4];
		if (tmp & 0x2000)
			tmp |= 0xc000;
		z_off = tmp;
					
	// get x,y,z gain
	x_gain = ((offset_buf[1] >> 7) << 8) + offset_buf[6];
	y_gain = ((offset_buf[3] >> 7) << 8) + offset_buf[7];
	z_gain = ((offset_buf[5] >> 7) << 8) + offset_buf[8];
							

	//storege the cerrunt offset data with DOT format
	offset_data[0] = x_off;
	offset_data[1] = y_off;
	offset_data[2] = z_off;

	//storege the cerrunt Gain data with GOT format
	gain_data[0] = 256*8*128/3/(40+x_gain);
	gain_data[1] = 256*8*128/3/(40+y_gain);
	gain_data[2] = 256*8*128/3/(40+z_gain);
	//printk("offser gain = %d %d %d %d %d %d======================\n\n ",
		//gain_data[0],gain_data[1],gain_data[2],offset_data[0],offset_data[1],offset_data[2]);


	mc3230_reg_init(this_client);

//Louis, 2013.11.14, apply cali data
  //  mcube_read_cali_file(this_client);

	return 0;
	
}

static int mc3230_start_dev(struct i2c_client *client, char rate)
{
	int ret = 0;
	struct mc3230_data* mc3230= get_3230_ctl_data();
	//struct sensor_private_data *mc3230 = (struct sensor_private_data *)i2c_get_clientdata(client);   // mc3230_data ???? mc3230.h ?. 

	mcprintkfunc("-------------------------mc3230 start dev------------------------\n");	
	/* standby */
	mc3230_active(client,0);
	mcprintkreg("mc3230 MC3230_REG_SYSMOD:%x\n",mc3230_read_reg(client,MC3230_REG_SYSMOD));

	/*data rate*/
	ret = mc3230_write_reg(client,MC3230_REG_RATE_SAMP,rate);
	mc3230->curr_rate = rate;
	mcprintkreg("mc3230 MC3230_REG_RATE_SAMP:%x  rate=%d\n",mc3230_read_reg(client,MC3230_REG_RATE_SAMP),rate);
	/*wake*/
	mc3230_active(client,1);
	mcprintkreg("mc3230 MC3230_REG_SYSMOD:%x\n",mc3230_read_reg(client,MC3230_REG_SYSMOD));
	
	//enable_irq(client->irq);
	return ret;

}

static int mc3230_start(struct i2c_client *client, char rate)
{ 
	//struct sensor_private_data *mc3230 = (struct sensor_private_data *)i2c_get_clientdata(client);
	struct mc3230_data* mc3230= get_3230_ctl_data();
	mcprintkfunc("%s::enter\n",__FUNCTION__); 
	if (mc3230->status == MC3230_OPEN) {
		return 0;      
	}
	mc3230->status = MC3230_OPEN;
	rate = 0;
	return mc3230_start_dev(client, rate);
}
#if 0
static int mc3230_close_dev(struct i2c_client *client)
{    	
	disable_irq_nosync(client->irq);
	return mc3230_active(client,0);
}

static int mc3230_close(struct i2c_client *client)
{
	struct mc3230_data *mc3230 = (struct mc3230_data *)i2c_get_clientdata(client);
	mcprintkfunc("%s::enter\n",__FUNCTION__); 
	mc3230->status = MC3230_CLOSE;

	return mc3230_close_dev(client);
}

static int mc3230_reset_rate(struct i2c_client *client, char rate)
{
	int ret = 0;
	
	mcprintkfunc("\n----------------------------mc3230_reset_rate------------------------\n");
	rate = (rate & 0x07);
	disable_irq_nosync(client->irq);
    	ret = mc3230_start_dev(client, rate);
  
	return ret ;
}
#endif
static inline int mc3230_convert_to_int(s16 value)
{
    int result;


    if (value < MC3230_BOUNDARY) {
       result = value * MC3230_GRAVITY_STEP;
    } else {
       result = ~(((~value & 0x7f) + 1)* MC3230_GRAVITY_STEP) + 1;
    }
		

    return result;
}



static void mc3230_report_value(struct i2c_client *client, struct mc3230_axis *axis)
{
	struct sensor_private_data *mc3230 = i2c_get_clientdata(client);
    //struct mc3230_axis *axis = (struct mc3230_axis *)rbuf;

	//int x = 1;
	//int y = 1;
	//int z = -1;
	//int temp = 0;


	input_report_abs(mc3230->input_dev, ABS_X, -(axis->x));
	input_report_abs(mc3230->input_dev, ABS_Y, (axis->y));
	input_report_abs(mc3230->input_dev, ABS_Z, (axis->z));

    input_sync(mc3230->input_dev);
  //printk("xhh ========Gsensor x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);
}

static int MC32X0_ReadData(struct i2c_client *client, s16 buffer[MC32X0_AXES_NUM]);

/** ? ?????? ???? g sensor ??? */
static int mc3230_get_data(struct i2c_client *client)
{
    struct sensor_private_data* mc3230 = i2c_get_clientdata(client);
	s16 buffer[6];
	int ret;
	int x,y,z;
    struct mc3230_axis axis;
	
    struct sensor_platform_data *pdata = pdata = client->dev.platform_data;
	//printk("%d\n==========",load_cali_flg);
 	if( load_cali_flg > 0)
	{
		ret =mcube_read_cali_file(client);
		if(ret == 0)
			load_cali_flg = ret;
		else 
			load_cali_flg --;
		//printk("load_cali %d %d\n",ret, load_cali_flg); 
	}  	
		ret = MC32X0_ReadData(client, buffer);
	if(ret)
	{    
		
		GSE_ERR("%s I2C error: ret value=%d", __func__,ret);
		return EIO;
	}
mcprintkfunc("%s %d %d %d \n",__func__,buffer[0],buffer[1],buffer[2]);
	
	x = mc3230_convert_to_int(buffer[0]);
	y = mc3230_convert_to_int(buffer[1]);
	z = mc3230_convert_to_int(buffer[2])*2/5;


		axis.x = (pdata->orientation[0])*x + (pdata->orientation[1])*y + (pdata->orientation[2])*z;
		axis.y = (pdata->orientation[3])*x + (pdata->orientation[4])*y + (pdata->orientation[5])*z;	
		axis.z = (pdata->orientation[6])*x + (pdata->orientation[7])*y + (pdata->orientation[8])*z;


	axis.x = x;
	axis.y = y;	
	axis.z = z;
	
    //printk( "%s: ------------------mc3230_GetData axis = %d  %d  %d--------------\n",
            //__func__, axis.x, axis.y, axis.z); 
     
    //memcpy(sense_data, &axis, sizeof(axis));
    mc3230_report_value(client, &axis);
	//atomic_set(&data_ready, 0);
	//wake_up(&data_ready_wq);

    /* ?????????? */
    
	
	mutex_lock(&mc3230->data_mutex);
	memcpy(&axis, &mc3230->axis, sizeof(mc3230->axis));	//get data from buffer
	mutex_unlock(&mc3230->data_mutex);
	
    /* ?? data_ready */
    atomic_set(&(mc3230->data_ready), 1);
    /* ??? data_ready ????? */
	wake_up(&(mc3230->data_ready_wq) );

	return 0;
}
static int MC32X0_ReadRBMData(struct i2c_client *client, char *buf)
{
	struct mc3230_data *mc3230 = (struct mc3230_data*)i2c_get_clientdata(client);
	int res = 0;
	int data[3];

	if (!buf || !client)
	{
		return EINVAL;
	}

	if(mc3230->status == MC3230_CLOSE)
	{
		res = mc3230_start(client, 0);
		if(res)
		{
			GSE_ERR("Power on mc32x0 error %d!\n", res);
		}
	}
	res = MC32X0_ReadData_RBM(client, data);
	if(res)
	{        
		GSE_ERR("%s I2C error: ret value=%d",__func__, res);
		return EIO;
	}
	else
	{
		sprintf(buf, "%04x %04x %04x", data[MC32X0_AXIS_X], 
			data[MC32X0_AXIS_Y], data[MC32X0_AXIS_Z]);
	
	}
	
	return 0;
}
static int MC32X0_ReadOffset(struct i2c_client *client, s16 ofs[MC32X0_AXES_NUM])
{    
	int err;
	u8 off_data[6];
	
	off_data[0]=MC32X0_XOUT_EX_L_REG;
	if ( mc32x0_type == IS_MC3210 )
	{
		//if ((err = mc3230_read_block(client, MC32X0_XOUT_EX_L_REG, off_data, MC32X0_DATA_LEN))) 
		err = sensor_rx_data(client, off_data, MC32X0_DATA_LEN);
		if(err )
    		{
    			GSE_ERR("error: %d\n", err);
    			return err;
    		}
		ofs[MC32X0_AXIS_X] = ((s16)(off_data[0]))|((s16)(off_data[1])<<8);
		ofs[MC32X0_AXIS_Y] = ((s16)(off_data[2]))|((s16)(off_data[3])<<8);
		ofs[MC32X0_AXIS_Z] = ((s16)(off_data[4]))|((s16)(off_data[5])<<8);
	}
	else if (mc32x0_type == IS_MC3230) 
	{
		//if ((err = mc3230_read_block(client, 0, off_data, 3))) 
		err = sensor_rx_data(client, off_data, MC32X0_DATA_LEN);
		if(err )
    		{
    			GSE_ERR("error: %d\n", err);
    			return err;
    		}
		ofs[MC32X0_AXIS_X] = (s8)off_data[0];
		ofs[MC32X0_AXIS_Y] = (s8)off_data[1];
		ofs[MC32X0_AXIS_Z] = (s8)off_data[2];			
	}

	GSE_LOG("MC32X0_ReadOffset %d %d %d \n",ofs[MC32X0_AXIS_X] ,ofs[MC32X0_AXIS_Y],ofs[MC32X0_AXIS_Z]);

    return 0;  
}
/*----------------------------------------------------------------------------*/
static int MC32X0_ResetCalibration(struct i2c_client *client)
{
	struct mc3230_data *mc3230 = get_3230_ctl_data();
	s16 tmp,i;

		sensor_write_reg(client,0x07,0x43);

		for(i=0;i<6;i++)
		{	
		sensor_write_reg(client, 0x21+i, offset_buf[i]);
		msleep(10);
		}	
		//mc3230_write_block(client, 0x21, offset_buf, 6);
		
		sensor_write_reg(client,0x07,0x41);

		msleep(20);

		tmp = ((offset_buf[1] & 0x3f) << 8) + offset_buf[0];  // add by Liang for set offset_buf as OTP value 
		if (tmp & 0x2000)
			tmp |= 0xc000;
		offset_data[0] = tmp;
					
		tmp = ((offset_buf[3] & 0x3f) << 8) + offset_buf[2];  // add by Liang for set offset_buf as OTP value 
			if (tmp & 0x2000)
				tmp |= 0xc000;
		offset_data[1] = tmp;
					
		tmp = ((offset_buf[5] & 0x3f) << 8) + offset_buf[4];  // add by Liang for set offset_buf as OTP value 
		if (tmp & 0x2000)
			tmp |= 0xc000;
		offset_data[2] = tmp;	

	memset(mc3230->cali_sw, 0x00, sizeof(mc3230->cali_sw));
	return 0;  

}
/*----------------------------------------------------------------------------*/
static int MC32X0_ReadCalibration(struct i2c_client *client, int dat[MC32X0_AXES_NUM])
{
	
    struct mc3230_data *mc3230 = get_3230_ctl_data();
    int err;
	
    if ((err = MC32X0_ReadOffset(client, mc3230->offset))) {
        GSE_ERR("read offset fail, %d\n", err);
        return err;
    }    
    
    dat[MC32X0_AXIS_X] = mc3230->offset[MC32X0_AXIS_X];
    dat[MC32X0_AXIS_Y] = mc3230->offset[MC32X0_AXIS_Y];
    dat[MC32X0_AXIS_Z] = mc3230->offset[MC32X0_AXIS_Z];  
	//modify by zwx
	//GSE_LOG("MC32X0_ReadCalibration %d %d %d \n",dat[mc3230->cvt.map[MC32X0_AXIS_X]] ,dat[mc3230->cvt.map[MC32X0_AXIS_Y]],dat[mc3230->cvt.map[MC32X0_AXIS_Z]]);
                                      
    return 0;
}

/*----------------------------------------------------------------------------*/
static int MC32X0_WriteCalibration(struct i2c_client *client, int dat[MC32X0_AXES_NUM])
{
	int err;
	u8 buf[9],i;
	s16 tmp, x_gain, y_gain, z_gain ;
	s32 x_off, y_off, z_off;
#if 1  //modify by zwx

	GSE_LOG("UPDATE dat: (%+3d %+3d %+3d)\n", 
	dat[MC32X0_AXIS_X], dat[MC32X0_AXIS_Y], dat[MC32X0_AXIS_Z]);

	/*calculate the real offset expected by caller*/
	//cali_temp[MC32X0_AXIS_X] = dat[MC32X0_AXIS_X];
	//cali_temp[MC32X0_AXIS_Y] = dat[MC32X0_AXIS_Y];
	//cali_temp[MC32X0_AXIS_Z] = dat[MC32X0_AXIS_Z];
	//cali[MC32X0_AXIS_Z]= cali[MC32X0_AXIS_Z]-gsensor_gain.z;


#endif	
// read register 0x21~0x28
	
	buf[0] = 0x21;
	err = sensor_rx_data(client, &buf[0], 3);
	buf[3] = 0x24;
	err = sensor_rx_data(client, &buf[3], 3);
	buf[6] = 0x27;
	err = sensor_rx_data(client, &buf[6], 3);
#if 1
	// get x,y,z offset
	tmp = ((buf[1] & 0x3f) << 8) + buf[0];
		if (tmp & 0x2000)
			tmp |= 0xc000;
		x_off = tmp;
					
	tmp = ((buf[3] & 0x3f) << 8) + buf[2];
		if (tmp & 0x2000)
			tmp |= 0xc000;
		y_off = tmp;
					
	tmp = ((buf[5] & 0x3f) << 8) + buf[4];
		if (tmp & 0x2000)
			tmp |= 0xc000;
		z_off = tmp;
					
	// get x,y,z gain
	x_gain = ((buf[1] >> 7) << 8) + buf[6];
	y_gain = ((buf[3] >> 7) << 8) + buf[7];
	z_gain = ((buf[5] >> 7) << 8) + buf[8];
								
	// prepare new offset
	x_off = x_off + 16 * dat[MC32X0_AXIS_X] * 256 * 128 / 3 / gsensor_gain.x / (40 + x_gain);
	y_off = y_off + 16 * dat[MC32X0_AXIS_Y] * 256 * 128 / 3 / gsensor_gain.y / (40 + y_gain);
	z_off = z_off + 16 * dat[MC32X0_AXIS_Z] * 256 * 128 / 3 / gsensor_gain.z / (40 + z_gain);

	//storege the cerrunt offset data with DOT format
	offset_data[0] = x_off;
	offset_data[1] = y_off;
	offset_data[2] = z_off;

	//storege the cerrunt Gain data with GOT format
	gain_data[0] = 256*8*128/3/(40+x_gain);
	gain_data[1] = 256*8*128/3/(40+y_gain);
	gain_data[2] = 256*8*128/3/(40+z_gain);
//	printk("%d %d ======================\n\n ",gain_data[0],x_gain);
#endif
	//buf[0]=0x43;
	//mc3230_write_block(client, 0x07, buf, 1);
	sensor_write_reg(client,0x07,0x43);
					
	buf[0] = x_off & 0xff;
	buf[1] = ((x_off >> 8) & 0x3f) | (x_gain & 0x0100 ? 0x80 : 0);
	buf[2] = y_off & 0xff;
	buf[3] = ((y_off >> 8) & 0x3f) | (y_gain & 0x0100 ? 0x80 : 0);
	buf[4] = z_off & 0xff;
	buf[5] = ((z_off >> 8) & 0x3f) | (z_gain & 0x0100 ? 0x80 : 0);


	//mc3230_tx_data(client, 0x21, buf, 6);
	for(i=0;i<6;i++)
	{	
	sensor_write_reg(client, 0x21+i, buf[i]);
	msleep(10);
	}	
	//buf[0]=0x41;
	//mc3230_write_block(client, 0x07, buf, 1);	

	sensor_write_reg(client,0x07,0x41);

    return err;

}


static int MC32X0_ReadData(struct i2c_client *client, s16 buffer[MC32X0_AXES_NUM])
{
	s8 buf[3];
	char rbm_buf[6];
	int ret;
	int err = 0;

#ifdef SUPPORT_VIRTUAL_Z_SENSOR
	int tempX=0;
	int tempY=0;
	int tempZ=0;
#endif
	
	if(NULL == client)
	{
		err = -EINVAL;
		return err;
	}
mcprintkfunc("MC32X0_ReadData enable_RBM_calibration = %d\n", enable_RBM_calibration);
	if ( enable_RBM_calibration == 0)
	{
		//err = hwmsen_read_block(client, addr, buf, 0x06);
	}
	else if (enable_RBM_calibration == 1)
	{		
		memset(rbm_buf, 0, 3);
        	rbm_buf[0] = MC3230_REG_RBM_DATA;
        	ret = sensor_rx_data(client, &rbm_buf[0], 2);
        	rbm_buf[2] = MC3230_REG_RBM_DATA+2;
        	ret = sensor_rx_data(client, &rbm_buf[2], 2);
        	rbm_buf[4] = MC3230_REG_RBM_DATA+4;
        	ret = sensor_rx_data(client, &rbm_buf[4], 2);
	}

mcprintkfunc("MC32X0_ReadData %d %d %d %d %d %d\n", rbm_buf[0], rbm_buf[1], rbm_buf[2], rbm_buf[3], rbm_buf[4], rbm_buf[5]);
	if ( enable_RBM_calibration == 0)
	{
	    do {
	        memset(buf, 0, 3);
	        buf[0] = MC3230_REG_X_OUT;
	        ret = sensor_rx_data(client, &buf[0], 3);
	        if (ret < 0)
	            return ret;
	    	} while (0);
		
		buffer[0]=(s16)buf[0];
		buffer[1]=(s16)buf[1];
		buffer[2]=(s16)buf[2];
		
#ifdef SUPPORT_VIRTUAL_Z_SENSOR			
		//printk("%s: ------------zhoukl--2--------\n",__func__);
		if(1 == Verify_Z_Railed(buffer[MC32X0_AXIS_Z], LOW_RESOLUTION)) // z-railed
		{
			Railed = 1;
			//printk("%s: ------------zhoukl--2-------gsensor_gain.z=%d   tempX=%d  tempY=%d \n",__func__, gsensor_gain.z, tempX, tempY);
			if (G_2_REVERSE == 1)
				buffer[MC32X0_AXIS_Z] = (signed short) (  gsensor_gain.z - (abs(tempX) + abs(tempY)));
			else
				buffer[MC32X0_AXIS_Z] = (signed short) -(  gsensor_gain.z - (abs(tempX) + abs(tempY)));
			//printk("%s: ------------zhoukl--2-------buffer[MC32X0_AXIS_Z]=%d\n",__func__, buffer[MC32X0_AXIS_Z]);
		}
		else
		{
			Railed = 0;	
		}
#endif  
		mcprintkfunc("0x%02x 0x%02x 0x%02x \n",buffer[0],buffer[1],buffer[2]);
	}
	else if (enable_RBM_calibration == 1)
	{
		buffer[MC32X0_AXIS_X] = (s16)((rbm_buf[0]) | (rbm_buf[1] << 8));
		buffer[MC32X0_AXIS_Y] = (s16)((rbm_buf[2]) | (rbm_buf[3] << 8));
		buffer[MC32X0_AXIS_Z] = (s16)((rbm_buf[4]) | (rbm_buf[5] << 8));

		mcprintkfunc("%s RBM<<<<<[%08d %08d %08d]\n", __func__,buffer[MC32X0_AXIS_X], buffer[MC32X0_AXIS_Y], buffer[MC32X0_AXIS_Z]);
if(gain_data[0] == 0)
{
		buffer[MC32X0_AXIS_X] = 0;
		buffer[MC32X0_AXIS_Y] = 0;
		buffer[MC32X0_AXIS_Z] = 0;
	return 0;
}
		buffer[MC32X0_AXIS_X] = (buffer[MC32X0_AXIS_X] + offset_data[0]/2)*gsensor_gain.x/gain_data[0];
		buffer[MC32X0_AXIS_Y] = (buffer[MC32X0_AXIS_Y] + offset_data[1]/2)*gsensor_gain.y/gain_data[1];
		buffer[MC32X0_AXIS_Z] = (buffer[MC32X0_AXIS_Z] + offset_data[2]/2)*gsensor_gain.z/gain_data[2];
		
		#ifdef SUPPORT_VIRTUAL_Z_SENSOR
		//printk("%s: ------------zhoukl--4--------\n",__func__);
		
		tempX = buffer[MC32X0_AXIS_X];
		tempY = buffer[MC32X0_AXIS_Y];
		tempZ = buffer[MC32X0_AXIS_Z];
			
		printk("Original RBM<<<<<[%08d %08d %08d]\n", buffer[MC32X0_AXIS_X], buffer[MC32X0_AXIS_Y], buffer[MC32X0_AXIS_Z]);
		
		if(1 == Verify_Z_Railed(buffer[MC32X0_AXIS_Z], RBM_RESOLUTION))// z-railed
		{
			printk("%s: Z Railed in RBM mode",__func__);
			if (G_2_REVERSE == 1)
				buffer[MC32X0_AXIS_Z] = (s16) (  gsensor_gain.z - (abs(tempX) + abs(tempY)));
			else
				buffer[MC32X0_AXIS_Z] = (s16) -(  gsensor_gain.z - (abs(tempX) + abs(tempY)));
		}
		printk("RBM<<<<<[%08d %08d %08d]\n", buffer[MC32X0_AXIS_X], buffer[MC32X0_AXIS_Y], buffer[MC32X0_AXIS_Z]);
		#endif		
		mcprintkfunc("%s offset_data <<<<<[%d %d %d]\n", __func__,offset_data[0], offset_data[1], offset_data[2]);

		mcprintkfunc("%s gsensor_gain <<<<<[%d %d %d]\n", __func__,gsensor_gain.x, gsensor_gain.y, gsensor_gain.z);
		
		mcprintkfunc("%s gain_data <<<<<[%d %d %d]\n", __func__,gain_data[0], gain_data[1], gain_data[2]);

		mcprintkfunc("%s RBM->RAW <<<<<[%d %d %d]\n", __func__,buffer[MC32X0_AXIS_X], buffer[MC32X0_AXIS_Y], buffer[MC32X0_AXIS_Z]);
	}
	
	return 0;
}
static int MC32X0_ReadRawData(struct i2c_client *client, char * buf)
{
	struct mc3230_data *obj = get_3230_ctl_data();
	int res = 0;
	s16 raw_buf[3];

	if (!buf || !client)
	{
		return EINVAL;
	}

	if(obj->status == MC3230_CLOSE)
	{
		res = mc3230_start(client, 0);
		if(res)
		{
			GSE_ERR("Power on mc32x0 error %d!\n", res);
		}
	}
	res = MC32X0_ReadData(client, &raw_buf[0]);
	if(res)
	{     
	printk("%s %d\n",__FUNCTION__, __LINE__);
		GSE_ERR("I2C error: ret value=%d", res);
		return EIO;
	}
	else
	{
	
	GSE_LOG("UPDATE dat: (%+3d %+3d %+3d)\n", 
	raw_buf[MC32X0_AXIS_X], raw_buf[MC32X0_AXIS_Y], raw_buf[MC32X0_AXIS_Z]);

	G_RAW_DATA[MC32X0_AXIS_X] = raw_buf[0];
	G_RAW_DATA[MC32X0_AXIS_Y] = raw_buf[1];
	G_RAW_DATA[MC32X0_AXIS_Z] = raw_buf[2];
	G_RAW_DATA[MC32X0_AXIS_Z]= G_RAW_DATA[MC32X0_AXIS_Z]+gsensor_gain.z;
	
	//printk("%s %d\n",__FUNCTION__, __LINE__);
		sprintf(buf, "%04x %04x %04x", G_RAW_DATA[MC32X0_AXIS_X], 
			G_RAW_DATA[MC32X0_AXIS_Y], G_RAW_DATA[MC32X0_AXIS_Z]);
		GSE_LOG("G_RAW_DATA: (%+3d %+3d %+3d)\n", 
	G_RAW_DATA[MC32X0_AXIS_X], G_RAW_DATA[MC32X0_AXIS_Y], G_RAW_DATA[MC32X0_AXIS_Z]);
	}
	return 0;
}

//MCUBE_BACKUP_FILE
static void mcube_copy_file( char *dstFilePath)
{

	int err =0;
	initKernelEnv();


	fd_file = openFile(dstFilePath,O_RDWR,0); 
	if (fd_file == NULL) 
	{
		GSE_LOG("open %s fail\n",dstFilePath);  
		return;
	}

	

		if ((err = writeFile(fd_file,backup_buf,64))>0) 
			GSE_LOG("buf:%s\n",backup_buf); 
		else 
			GSE_LOG("write file error %d\n",err);

		set_fs(oldfs); ; 
		closeFile(fd_file);

}
//MCUBE_BACKUP_FILE

 long mc3230_ioctl( struct file *file, unsigned int cmd,unsigned long arg, struct i2c_client *client)
{

	void __user *argp = (void __user *)arg;
		
	char strbuf[256];
	void __user *data;
	SENSOR_DATA sensor_data;
	int err = 0;
	int cali[3];
	
	// char msg[RBUFF_SIZE + 1];
	struct mc3230_data*  p_mc3230_data= get_3230_ctl_data();
    struct mc3230_axis sense_data = {0};
	//int ret = -1;
	//char rate;
	//struct i2c_client *client = container_of(mc3230_device.parent, struct i2c_client, dev);
  //  struct sensor_private_data* this = (struct sensor_private_data *)i2c_get_clientdata(client);  /* ???????}?R??. */

	mcprintkreg("mc3230_ioctl cmd is %d.", cmd);

 	
        
	switch (cmd) {
	
	case GSENSOR_IOCTL_READ_SENSORDATA:	
	case GSENSOR_IOCTL_READ_RAW_DATA:
		GSE_LOG("fwq GSENSOR_IOCTL_READ_RAW_DATA\n");
		data = (void __user*)arg;
		MC32X0_ReadRawData(client, strbuf);
		if(copy_to_user(data, &strbuf, strlen(strbuf)+1))
			{
				err = -EFAULT;
				break;	  
			}
		break;
	case GSENSOR_IOCTL_SET_CALI:
		
			GSE_LOG("fwq GSENSOR_IOCTL_SET_CALI!!\n");

			break;

	case GSENSOR_MCUBE_IOCTL_SET_CALI:
			GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_SET_CALI!!\n");
			data = (void __user*)arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			if(copy_from_user(&sensor_data, data, sizeof(sensor_data)))
			{
				err = -EFAULT;
				break;	  
			}
			//if(atomic_read(&this->suspend))
			//{
			//	GSE_ERR("Perform calibration in suspend state!!\n");
			//	err = -EINVAL;
			//}
			else
			{
				//this->cali_sw[MC32X0_AXIS_X] += sensor_data.x;
				//this->cali_sw[MC32X0_AXIS_Y] += sensor_data.y;
				//this->cali_sw[MC32X0_AXIS_Z] += sensor_data.z;
				
				cali[MC32X0_AXIS_X] = sensor_data.x;
				cali[MC32X0_AXIS_Y] = sensor_data.y;
				cali[MC32X0_AXIS_Z] = sensor_data.z;	

			  	GSE_LOG("GSENSOR_MCUBE_IOCTL_SET_CALI %d  %d  %d  %d  %d  %d!!\n", cali[MC32X0_AXIS_X], cali[MC32X0_AXIS_Y],cali[MC32X0_AXIS_Z] ,sensor_data.x, sensor_data.y ,sensor_data.z);
				
				err = MC32X0_WriteCalibration(client, cali);			 
			}
			break;
		case GSENSOR_IOCTL_CLR_CALI:
			GSE_LOG("fwq GSENSOR_IOCTL_CLR_CALI!!\n");
			err = MC32X0_ResetCalibration(client);
			break;

		case GSENSOR_IOCTL_GET_CALI:
			GSE_LOG("fwq mc32x0 GSENSOR_IOCTL_GET_CALI\n");
			data = (void __user*)arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			if((err = MC32X0_ReadCalibration(client, cali)))
			{
				GSE_LOG("fwq mc32x0 MC32X0_ReadCalibration error!!!!\n");
				break;
			}
			
			sensor_data.x = p_mc3230_data->cali_sw[MC32X0_AXIS_X];
			sensor_data.y = p_mc3230_data->cali_sw[MC32X0_AXIS_Y];
			sensor_data.z = p_mc3230_data->cali_sw[MC32X0_AXIS_Z];
			if(copy_to_user(data, &sensor_data, sizeof(sensor_data)))
			{
				err = -EFAULT;
				break;
			}		
			break;	
		// add by liang ****
		//add in Sensors_io.h
		//#define GSENSOR_IOCTL_SET_CALI_MODE   _IOW(GSENSOR, 0x0e, int)
		case GSENSOR_IOCTL_SET_CALI_MODE:
			GSE_LOG("fwq mc32x0 GSENSOR_IOCTL_SET_CALI_MODE\n");
			break;

		case GSENSOR_MCUBE_IOCTL_READ_RBM_DATA:
			GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_READ_RBM_DATA\n");
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			MC32X0_ReadRBMData(client, (char *)&strbuf);
			if(copy_to_user(data, &strbuf, strlen(strbuf)+1))
			{
				err = -EFAULT;
				break;	  
			}
			break;

		case GSENSOR_MCUBE_IOCTL_SET_RBM_MODE:
			printk("fwq GSENSOR_MCUBE_IOCTL_SET_RBM_MODE\n");
			//MCUBE_BACKUP_FILE
			if (READ_FROM_BACKUP == true)
			{
				mcube_copy_file(calib_path);
				READ_FROM_BACKUP = false;
			}
			//MCUBE_BACKUP_FILE
			MC32X0_rbm(client,1);

			break;

		case GSENSOR_MCUBE_IOCTL_CLEAR_RBM_MODE:
			GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_SET_RBM_MODE\n");

			MC32X0_rbm(client,0);

			break;

		case GSENSOR_MCUBE_IOCTL_REGISTER_MAP:
			GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_REGISTER_MAP\n");

			//MC32X0_Read_Reg_Map(client);

			break;
			
	default:
		return -ENOTTY;
	}

	switch (cmd) {

	case MC_IOCTL_GETDATA:
        /*
		if (copy_to_user(argp, &msg, sizeof(msg)))
			return -EFAULT;
        */
        if ( copy_to_user(argp, &sense_data, sizeof(sense_data) ) ) {
            printk("failed to copy sense data to user space.");
			return -EFAULT;
        }
		break;
	case GSENSOR_IOCTL_READ_RAW_DATA:
	case GSENSOR_IOCTL_READ_SENSORDATA:
		if (copy_to_user(argp, &strbuf, strlen(strbuf)+1)) {
			printk("failed to copy sense data to user space.");
			return -EFAULT;
		}
		
		break;

	default:
		break;
	}

	return 0;
}


static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	int mc3230_rate = 0;

	//MITECH_SENSOR_DBG("Mitech_andy#SENSOR %s entry.\n", __FUNCTION__);

	mc3230_rate = 0xf8 | (0x07 & rate);

	result = sensor_write_reg(client, MC32X0_Sample_Rate_REG, mc3230_rate);	
 
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
		
	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	//MITECH_SENSOR_DBG("Mitech_yanghui#SENSOR %s sensor_ctldata_original[0x%x].\n", __FUNCTION__,sensor->ops->ctrl_data);
	
	//register setting according to chip datasheet		
	if(!enable)
	{	
		sensor->ops->ctrl_data &= ~MC3230_MODE_BITS;
		sensor->ops->ctrl_data |= MC32X0_MODE_SLEEP;
	}
	else
	{
		sensor->ops->ctrl_data &= ~MC3230_MODE_BITS;
		sensor->ops->ctrl_data |= MC32X0_MODE_WAKEUP;
	}

	//MITECH_SENSOR_DBG("Mitech_yanghui#SENSOR %s sensor_ctldata_current[0x%x].\n", __FUNCTION__,sensor->ops->ctrl_data);
	
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
		printk("%s:fail to active sensor\n",__func__);
	
	return result;

}

static int sensor_init(struct i2c_client *client)
{	
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	static int MC3230_is_init = 0;


	//MITECH_SENSOR_DBG("Mitech_yanghui#SENSOR %s entry.\n", __FUNCTION__);
	if(MC3230_is_init == 0)
	{
		init_3230_ctl_data(client);//add by cyrus.0117
	}	
	MC3230_is_init = 1;
	
	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
	
	sensor->status_cur = SENSOR_OFF;

	result = sensor_write_reg(client, MC32X0_Interrupt_Enable_REG, 0x10);	
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	result = sensor->ops->active(client,1,MC3230_RATE_32);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
	
	return result;
}


#if 0
static int gsensor_report_value(struct i2c_client *client, struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);	

	input_report_abs(sensor->input_dev, ABS_X, axis->x);
	input_report_abs(sensor->input_dev, ABS_Y, axis->z/*(axis->y*/);
	input_report_abs(sensor->input_dev, ABS_Z, -(axis->y)/*axis->z*/);
	input_sync(sensor->input_dev);
	printk("MITECH ....$$$$$$$$$$$$$$....Gsensor x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);

	return 0;
}
#endif
#define RawDataLength 4
int RawDataNum = 0;
int Xaverage = 0;
int Yaverage = 0;
int Zaverage = 0;
#define GSENSOR_MIN  10
static int sensor_report_value(struct i2c_client *client)
{

	int ret = 0;
	mc3230_get_data(client);

	return ret;
}

struct sensor_operate gsensor_ops = {
	.name				= "gs_mc3230",
	.type				= SENSOR_TYPE_ACCEL,	//sensor type and it should be correct
	.id_i2c				= ACCEL_ID_MC3230,		//i2c id number
	.read_reg			= MC32X0_XOUT_REG,	//read data
	.read_len			= 3,					//data length
	.id_reg				= SENSOR_UNKNOW_DATA,					//read device id from this register,but mc3230 has no id register
	.id_data 			= SENSOR_UNKNOW_DATA,					//device id
	.precision			= 6,					//6 bits
	.ctrl_reg 			= MC32X0_Mode_Feature_REG	,		//enable or disable 
	.int_status_reg 	= MC32X0_Interrupt_Enable_REG	,	//intterupt status register
	.range				= {-MC3230_RANGE, MC3230_RANGE},	//range
	.trig				= (IRQF_TRIGGER_HIGH|IRQF_ONESHOT),		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
struct sensor_operate *gsensor_get_ops(void)
{
	return &gsensor_ops;
}

EXPORT_SYMBOL(gsensor_get_ops);

static int __init gsensor_init(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, gsensor_get_ops);
	printk("%s\n",__func__);
	return result;
}

static void __exit gsensor_exit(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, gsensor_get_ops);
}


module_init(gsensor_init);
module_exit(gsensor_exit);

