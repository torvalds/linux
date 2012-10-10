/*
 * This file is part of the AP3212B, AP3212C and AP3216C sensor driver.
 * AP3212B is combined proximity and ambient light sensor.
 * AP3216C is combined proximity, ambient light sensor and IRLED.
 *
 * Contact: YC Hou <yc.hou@liteonsemi.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 *
 * Filename: ap321XX.c
 *
 * Summary:
 *	AP3212B v1.5-1.8 and AP3212C/AP3216C v2.0-v2.3 sensor dirver.
 *
 * Modification History:
 * Date     By       Summary
 * -------- -------- -------------------------------------------------------
 * 06/28/11 YC		 Original Creation (Test version:1.0)
 * 06/28/11 YC       Change dev name to dyna for demo purpose (ver 1.5).
 * 08/29/11 YC       Add engineer mode. Change version to 1.6.
 * 09/26/11 YC       Add calibration compensation function and add not power up 
 *                   prompt. Change version to 1.7.
 * 02/02/12 YC       1. Modify irq function to seperate two interrupt routine. 
 *					 2. Fix the index of reg array error in em write. 
 * 02/22/12 YC       3. Merge AP3212B and AP3216C into the same driver. (ver 1.8)
 * 03/01/12 YC       Add AP3212C into the driver. (ver 1.8)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/string.h>
#include <mach/gpio.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <mach/board.h>

#define AP3212B_DRV_NAME		"ap321xx"
#define DRIVER_VERSION		"1.8"

#define AP3212B_NUM_CACHABLE_REGS	23
#define AP3216C_NUM_CACHABLE_REGS	26

#define AP3212B_RAN_COMMAND	0x10
#define AP3212B_RAN_MASK		0x30
#define AP3212B_RAN_SHIFT	(4)

#define AP3212B_MODE_COMMAND	0x00
#define AP3212B_MODE_SHIFT	(0)
#define AP3212B_MODE_MASK	0x07

#define	AL3212_ADC_LSB		0x0c
#define	AL3212_ADC_MSB		0x0d

#define	AL3212_PX_LSB		0x0e
#define	AL3212_PX_MSB		0x0f
#define	AL3212_PX_LSB_MASK	0x0f
#define	AL3212_PX_MSB_MASK	0x3f

#define AP3212B_OBJ_COMMAND	0x0f
#define AP3212B_OBJ_MASK		0x80
#define AP3212B_OBJ_SHIFT	(7)

#define AP3212B_INT_COMMAND	0x01
#define AP3212B_INT_SHIFT	(0)
#define AP3212B_INT_MASK		0x03
#define AP3212B_INT_PMASK		0x02
#define AP3212B_INT_AMASK		0x01

#define AP3212B_ALS_LTHL			0x1a
#define AP3212B_ALS_LTHL_SHIFT	(0)
#define AP3212B_ALS_LTHL_MASK	0xff

#define AP3212B_ALS_LTHH			0x1b
#define AP3212B_ALS_LTHH_SHIFT	(0)
#define AP3212B_ALS_LTHH_MASK	0xff

#define AP3212B_ALS_HTHL			0x1c
#define AP3212B_ALS_HTHL_SHIFT	(0)
#define AP3212B_ALS_HTHL_MASK	0xff

#define AP3212B_ALS_HTHH			0x1d
#define AP3212B_ALS_HTHH_SHIFT	(0)
#define AP3212B_ALS_HTHH_MASK	0xff

#define AP3212B_PX_LTHL			0x2a
#define AP3212B_PX_LTHL_SHIFT	(0)
#define AP3212B_PX_LTHL_MASK		0x03

#define AP3212B_PX_LTHH			0x2b
#define AP3212B_PX_LTHH_SHIFT	(0)
#define AP3212B_PX_LTHH_MASK		0xff

#define AP3212B_PX_HTHL			0x2c
#define AP3212B_PX_HTHL_SHIFT	(0)
#define AP3212B_PX_HTHL_MASK		0x03

#define AP3212B_PX_HTHH			0x2d
#define AP3212B_PX_HTHH_SHIFT	(0)
#define AP3212B_PX_HTHH_MASK		0xff

#define AP3212B_PX_CONFIGURE	0x20

#define PSENSOR_IOCTL_MAGIC 'c'
#define PSENSOR_IOCTL_GET_ENABLED _IOR(PSENSOR_IOCTL_MAGIC, 1, int *)
#define PSENSOR_IOCTL_ENABLE _IOW(PSENSOR_IOCTL_MAGIC, 2, int *)

#define LIGHTSENSOR_IOCTL_MAGIC 'l'
#define LIGHTSENSOR_IOCTL_GET_ENABLED _IOR(LIGHTSENSOR_IOCTL_MAGIC, 1, int *)
#define LIGHTSENSOR_IOCTL_ENABLE _IOW(LIGHTSENSOR_IOCTL_MAGIC, 2, int *)


#define LSC_DBG
#ifdef LSC_DBG
#define LDBG(s,args...)	{printk("LDBG: func [%s], line [%d], ",__func__,__LINE__); printk(s,## args);}
#else
#define LDBG(s,args...) {}
#endif

struct ap321xx_data {
	struct i2c_client *client;
	u8 reg_cache[AP3216C_NUM_CACHABLE_REGS];
	u8 power_state_before_suspend;
	int irq;
	struct input_dev	*psensor_input_dev;
	struct input_dev	*lsensor_input_dev;
};

// AP3216C / AP3212C register
static u8 ap3216c_reg[AP3216C_NUM_CACHABLE_REGS] = 
	{0x00,0x01,0x02,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	 0x10,0x19,0x1a,0x1b,0x1c,0x1d,
	 0x20,0x21,0x22,0x23,0x24,0x28,0x29,0x2a,0x2b,0x2c,0x2d};

// AP3216C / AP3212C range
static int ap3216c_range[4] = {23360,5840,1460,265};

// AP3212B register
static u8 ap3212b_reg[AP3212B_NUM_CACHABLE_REGS] = 
	{0x00,0x01,0x02,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	 0x10,0x11,0x1a,0x1b,0x1c,0x1d,
	 0x20,0x21,0x22,0x23,0x2a,0x2b,0x2c,0x2d};

// AP3212B range
static int ap3212b_range[4] = {65535,16383,4095,1023};

static u16 ap321xx_threshole[8] = {28,444,625,888,1778,3555,7222,0xffff};

static u8 *reg_array;
static int *range;
static int reg_num = 0;

static int cali = 100;

#define ADD_TO_IDX(addr,idx)	{														\
									int i;												\
									for(i = 0; i < reg_num; i++)						\
									{													\
										if (addr == reg_array[i])						\
										{												\
											idx = i;									\
											break;										\
										}												\
									}													\
								}


/*
 * register access helpers
 */

static int __ap321xx_read_reg(struct i2c_client *client,
			       u32 reg, u8 mask, u8 shift)
{
	struct ap321xx_data *data = i2c_get_clientdata(client);
	u8 idx = 0xff;

	ADD_TO_IDX(reg,idx)
	return (data->reg_cache[idx] & mask) >> shift;
}

static int __ap321xx_write_reg(struct i2c_client *client,
				u32 reg, u8 mask, u8 shift, u8 val)
{
	struct ap321xx_data *data = i2c_get_clientdata(client);
	int ret = 0;
	u8 tmp;
	u8 idx = 0xff;

	ADD_TO_IDX(reg,idx)
	if (idx >= reg_num)
		return -EINVAL;

	tmp = data->reg_cache[idx];
	tmp &= ~mask;
	tmp |= val << shift;

	ret = i2c_smbus_write_byte_data(client, reg, tmp);
	if (!ret)
		data->reg_cache[idx] = tmp;

	return ret;
}

/*
 * internally used functions
 */

/* range */
static int ap321xx_get_range(struct i2c_client *client)
{
	u8 idx = __ap321xx_read_reg(client, AP3212B_RAN_COMMAND,
		AP3212B_RAN_MASK, AP3212B_RAN_SHIFT); 
	return range[idx];
}

static int ap321xx_set_range(struct i2c_client *client, int range)
{
	return __ap321xx_write_reg(client, AP3212B_RAN_COMMAND,
		AP3212B_RAN_MASK, AP3212B_RAN_SHIFT, range);;
}


/* mode */
static int ap321xx_get_mode(struct i2c_client *client)
{
	int ret;

	ret = __ap321xx_read_reg(client, AP3212B_MODE_COMMAND,
			AP3212B_MODE_MASK, AP3212B_MODE_SHIFT);
	return ret;
}

static int ap321xx_set_mode(struct i2c_client *client, int mode)
{
	int ret;

	ret = __ap321xx_write_reg(client, AP3212B_MODE_COMMAND,
				AP3212B_MODE_MASK, AP3212B_MODE_SHIFT, mode);
	return ret;
}

/* ALS low threshold */
static int ap321xx_get_althres(struct i2c_client *client)
{
	int lsb, msb;
	lsb = __ap321xx_read_reg(client, AP3212B_ALS_LTHL,
				AP3212B_ALS_LTHL_MASK, AP3212B_ALS_LTHL_SHIFT);
	msb = __ap321xx_read_reg(client, AP3212B_ALS_LTHH,
				AP3212B_ALS_LTHH_MASK, AP3212B_ALS_LTHH_SHIFT);
	return ((msb << 8) | lsb);
}

static int ap321xx_set_althres(struct i2c_client *client, int val)
{
	int lsb, msb, err;
	
	msb = val >> 8;
	lsb = val & AP3212B_ALS_LTHL_MASK;

	err = __ap321xx_write_reg(client, AP3212B_ALS_LTHL,
		AP3212B_ALS_LTHL_MASK, AP3212B_ALS_LTHL_SHIFT, lsb);
	if (err)
		return err;

	err = __ap321xx_write_reg(client, AP3212B_ALS_LTHH,
		AP3212B_ALS_LTHH_MASK, AP3212B_ALS_LTHH_SHIFT, msb);

	return err;
}

/* ALS high threshold */
static int ap321xx_get_ahthres(struct i2c_client *client)
{
	int lsb, msb;
	lsb = __ap321xx_read_reg(client, AP3212B_ALS_HTHL,
				AP3212B_ALS_HTHL_MASK, AP3212B_ALS_HTHL_SHIFT);
	msb = __ap321xx_read_reg(client, AP3212B_ALS_HTHH,
				AP3212B_ALS_HTHH_MASK, AP3212B_ALS_HTHH_SHIFT);
	return ((msb << 8) | lsb);
}

static int ap321xx_set_ahthres(struct i2c_client *client, int val)
{
	int lsb, msb, err;
	
	msb = val >> 8;
	lsb = val & AP3212B_ALS_HTHL_MASK;
	
	err = __ap321xx_write_reg(client, AP3212B_ALS_HTHL,
		AP3212B_ALS_HTHL_MASK, AP3212B_ALS_HTHL_SHIFT, lsb);
	if (err)
		return err;

	err = __ap321xx_write_reg(client, AP3212B_ALS_HTHH,
		AP3212B_ALS_HTHH_MASK, AP3212B_ALS_HTHH_SHIFT, msb);

	return err;
}

/* PX low threshold */
static int ap321xx_get_plthres(struct i2c_client *client)
{
	int lsb, msb;
	lsb = __ap321xx_read_reg(client, AP3212B_PX_LTHL,
				AP3212B_PX_LTHL_MASK, AP3212B_PX_LTHL_SHIFT);
	msb = __ap321xx_read_reg(client, AP3212B_PX_LTHH,
				AP3212B_PX_LTHH_MASK, AP3212B_PX_LTHH_SHIFT);
	return ((msb << 2) | lsb);
}

static int ap321xx_set_plthres(struct i2c_client *client, int val)
{
	int lsb, msb, err;
	
	msb = val >> 2;
	lsb = val & AP3212B_PX_LTHL_MASK;
	
	err = __ap321xx_write_reg(client, AP3212B_PX_LTHL,
		AP3212B_PX_LTHL_MASK, AP3212B_PX_LTHL_SHIFT, lsb);
	if (err)
		return err;

	err = __ap321xx_write_reg(client, AP3212B_PX_LTHH,
		AP3212B_PX_LTHH_MASK, AP3212B_PX_LTHH_SHIFT, msb);

	return err;
}

/* PX high threshold */
static int ap321xx_get_phthres(struct i2c_client *client)
{
	int lsb, msb;
	lsb = __ap321xx_read_reg(client, AP3212B_PX_HTHL,
				AP3212B_PX_HTHL_MASK, AP3212B_PX_HTHL_SHIFT);
	msb = __ap321xx_read_reg(client, AP3212B_PX_HTHH,
				AP3212B_PX_HTHH_MASK, AP3212B_PX_HTHH_SHIFT);
	return ((msb << 2) | lsb);
}

static int ap321xx_set_phthres(struct i2c_client *client, int val)
{
	int lsb, msb, err;
	
	msb = val >> 2;
	lsb = val & AP3212B_ALS_HTHL_MASK;
	
	err = __ap321xx_write_reg(client, AP3212B_PX_HTHL,
		AP3212B_PX_HTHL_MASK, AP3212B_PX_HTHL_SHIFT, lsb);
	if (err)
		return err;

	err = __ap321xx_write_reg(client, AP3212B_PX_HTHH,
		AP3212B_PX_HTHH_MASK, AP3212B_PX_HTHH_SHIFT, msb);

	return err;
}

static int ap321xx_get_adc_value(struct i2c_client *client)
{
	unsigned int lsb, msb, val;
#ifdef LSC_DBG
	unsigned int tmp,range;
#endif
	unsigned char index=0;

	lsb = i2c_smbus_read_byte_data(client, AL3212_ADC_LSB);

	if (lsb < 0) {
		return lsb;
	}

	msb = i2c_smbus_read_byte_data(client, AL3212_ADC_MSB);

	if (msb < 0)
		return msb;

#ifdef LSC_DBG
	range = ap321xx_get_range(client);
	tmp = (((msb << 8) | lsb) * range) >> 16;
	tmp = tmp * cali / 100;
	LDBG("ALS val=%d lux\n",tmp);
#endif
	val = msb << 8 | lsb;
	for(index = 0; index < 7 && val > ap321xx_threshole[index];index++)
		;

	return index;
}

static int ap321xx_get_object(struct i2c_client *client)
{
	int val;

	val = i2c_smbus_read_byte_data(client, AP3212B_OBJ_COMMAND);
	val &= AP3212B_OBJ_MASK;

	return val >> AP3212B_OBJ_SHIFT;
}

static int ap321xx_get_intstat(struct i2c_client *client)
{
	int val;
	
	val = i2c_smbus_read_byte_data(client, AP3212B_INT_COMMAND);
	val &= AP3212B_INT_MASK;

	return val >> AP3212B_INT_SHIFT;
}


static int ap321xx_get_px_value(struct i2c_client *client)
{
	int lsb, msb;

	lsb = i2c_smbus_read_byte_data(client, AL3212_PX_LSB);

	if (lsb < 0) {
		return lsb;
	}

	msb = i2c_smbus_read_byte_data(client, AL3212_PX_MSB);

	if (msb < 0)
		return msb;

	return (u32)(((msb & AL3212_PX_MSB_MASK) << 4) | (lsb & AL3212_PX_LSB_MASK));
}

#if 0
/*
 * sysfs layer
 */
static int ap321xx_input_init(struct ap321xx_data *data)
{
    struct input_dev *dev;
    int err;

    dev = input_allocate_device();
    if (!dev) {
        return -ENOMEM;
    }
    dev->name = "lightsensor-level";
    dev->id.bustype = BUS_I2C;

    input_set_capability(dev, EV_ABS, ABS_MISC);
    input_set_capability(dev, EV_ABS, ABS_RUDDER);
    input_set_drvdata(dev, data);

    err = input_register_device(dev);
    if (err < 0) {
        input_free_device(dev);
        return err;
    }
    data->input = dev;

    return 0;
}

static void ap321xx_input_fini(struct ap321xx_data *data)
{
    struct input_dev *dev = data->input;

    input_unregister_device(dev);
    input_free_device(dev);
}
#else
static int ap321xx_lsensor_open(struct inode *inode, struct file *file);
static int ap321xx_lsensor_release(struct inode *inode, struct file *file);
static long ap321xx_lsensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static void ap321xx_change_ls_threshold(struct i2c_client *client);

static int misc_ls_opened = 0;
static struct file_operations ap321xx_lsensor_fops = {
	.owner = THIS_MODULE,
	.open = ap321xx_lsensor_open,
	.release = ap321xx_lsensor_release,
	.unlocked_ioctl = ap321xx_lsensor_ioctl
};

static struct miscdevice ap321xx_lsensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &ap321xx_lsensor_fops
};

static int ap321xx_lsensor_open(struct inode *inode, struct file *file)
{
	LDBG("\n");
	if (misc_ls_opened)
		return -EBUSY;
	misc_ls_opened = 1;
	return 0;
}

static int ap321xx_lsensor_release(struct inode *inode, struct file *file)
{
	LDBG("\n");
	misc_ls_opened = 0;
	return 0;
}

static int ap321xx_lsensor_enable(struct i2c_client *client)
{
	int ret = 0,mode;
	
	mode = ap321xx_get_mode(client);
	if((mode & 0x01) == 0){
		mode |= 0x01;
		ret = ap321xx_set_mode(client,mode);
	}
	
	return ret;
}

static int ap321xx_lsensor_disable(struct i2c_client *client)
{
	int ret = 0,mode;
	
	mode = ap321xx_get_mode(client);
	if(mode & 0x01){
		mode &= ~0x01;
		if(mode == 0x04)
			mode = 0;
		ret = ap321xx_set_mode(client,mode);
	}
	
	return ret;
}

static long ap321xx_lsensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	char val;
	int ret;
	struct i2c_client *client = container_of(ap321xx_lsensor_misc.parent, struct i2c_client, dev);

	LDBG("%s cmd %d\n", __FUNCTION__, _IOC_NR(cmd));
	
	switch (cmd) {
	case LIGHTSENSOR_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		if (val){
			ret = ap321xx_lsensor_enable(client);
			if(!ret){
				msleep(200);
				ap321xx_change_ls_threshold(client);
			}
			return ret;
		}
		else
			return ap321xx_lsensor_disable(client);
		break;
	case LIGHTSENSOR_IOCTL_GET_ENABLED:
		val = ap321xx_get_mode(client);
		val &= 0x01;
		return put_user(val, (unsigned long __user *)arg);
		break;
	default:
		pr_err("%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
		return -EINVAL;
	}
}

static int ap321xx_register_lsensor_device(struct i2c_client *client, struct ap321xx_data *data)
{
	struct input_dev *input_dev;
	int rc;

	LDBG("allocating input device lsensor\n");
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev,"%s: could not allocate input device for lsensor\n", __FUNCTION__);
		rc = -ENOMEM;
		goto done;
	}
	data->lsensor_input_dev = input_dev;
	input_set_drvdata(input_dev, data);
	input_dev->name = "lightsensor-level";
	input_dev->dev.parent = &client->dev;
	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_MISC, 0, 8, 0, 0);

	rc = input_register_device(input_dev);
	if (rc < 0) {
		pr_err("%s: could not register input device for lsensor\n", __FUNCTION__);
		goto done;
	}
	rc = misc_register(&ap321xx_lsensor_misc);
	if (rc < 0) {
		pr_err("%s: could not register misc device lsensor\n", __FUNCTION__);
		goto err_unregister_input_device;
	}

	ap321xx_lsensor_misc.parent = &client->dev;
	return 0;

err_unregister_input_device:
	input_unregister_device(input_dev);
done:
	return rc;
}

static void ap321xx_unregister_lsensor_device(struct i2c_client *client, struct ap321xx_data *data)
{
	misc_deregister(&ap321xx_lsensor_misc);
	input_unregister_device(data->lsensor_input_dev);
}

static void ap321xx_change_ls_threshold(struct i2c_client *client)
{
	struct ap321xx_data *data = i2c_get_clientdata(client);
	int value;

	value = ap321xx_get_adc_value(client);
	LDBG("ALS lux index: %u\n", value);
	if(value > 0){
		ap321xx_set_althres(client,ap321xx_threshole[value-1]);
		ap321xx_set_ahthres(client,ap321xx_threshole[value]);
	}
	else{
		ap321xx_set_althres(client,0);
		ap321xx_set_ahthres(client,ap321xx_threshole[value]);
	}
	
	input_report_abs(data->lsensor_input_dev, ABS_MISC, value);
	input_sync(data->lsensor_input_dev);
	
}


static int misc_ps_opened = 0;
static int ap321xx_psensor_open(struct inode *inode, struct file *file);
static int ap321xx_psensor_release(struct inode *inode, struct file *file);
static long ap321xx_psensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static struct file_operations ap321xx_psensor_fops = {
	.owner = THIS_MODULE,
	.open = ap321xx_psensor_open,
	.release = ap321xx_psensor_release,
	.unlocked_ioctl = ap321xx_psensor_ioctl
};

static struct miscdevice ap321xx_psensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "psensor",
	.fops = &ap321xx_psensor_fops
};

static int ap321xx_psensor_open(struct inode *inode, struct file *file)
{
	LDBG("\n");
	if (misc_ps_opened)
		return -EBUSY;
	misc_ps_opened = 1;
	return 0;
}

static int ap321xx_psensor_release(struct inode *inode, struct file *file)
{
	LDBG("\n");
	misc_ps_opened = 0;
	return 0;
}

static int ap321xx_psensor_enable(struct i2c_client *client)
{
	int ret = 0,mode;
	
	mode = ap321xx_get_mode(client);
	if((mode & 0x02) == 0){
		mode |= 0x02;
		ret = ap321xx_set_mode(client,mode);
	}
	
	return ret;
}

static int ap321xx_psensor_disable(struct i2c_client *client)
{
	int ret = 0,mode;
	
	mode = ap321xx_get_mode(client);
	if(mode & 0x02){
		mode &= ~0x02;
		if(mode == 0x04)
			mode = 0x00;
		ret = ap321xx_set_mode(client,mode);
	}
	return ret;
}


static long ap321xx_psensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	char val;
	struct i2c_client *client = container_of(ap321xx_psensor_misc.parent,struct i2c_client,dev);

	LDBG("%s cmd %d\n", __func__, _IOC_NR(cmd));
	
	switch (cmd) {
	case PSENSOR_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		if (val)
			return ap321xx_psensor_enable(client);
		else
			return ap321xx_psensor_disable(client);
		break;
	case PSENSOR_IOCTL_GET_ENABLED:
		val = ap321xx_get_mode(client);
		val = (val >> 1) & 0x01;
		return put_user(val, (unsigned long __user *)arg);
		break;
	default:
		pr_err("%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
		return -EINVAL;
	}
}

static int ap321xx_register_psensor_device(struct i2c_client *client, struct ap321xx_data *data)
{
	struct input_dev *input_dev;
	int rc;

	LDBG("allocating input device psensor\n");
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev,"%s: could not allocate input device for psensor\n", __FUNCTION__);
		rc = -ENOMEM;
		goto done;
	}
	data->psensor_input_dev = input_dev;
	input_set_drvdata(input_dev, data);
	input_dev->name = "proximity";
	input_dev->dev.parent = &client->dev;
	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	rc = input_register_device(input_dev);
	if (rc < 0) {
		pr_err("%s: could not register input device for psensor\n", __FUNCTION__);
		goto done;
	}

	rc = misc_register(&ap321xx_psensor_misc);
	if (rc < 0) {
		pr_err("%s: could not register misc device psensor\n", __FUNCTION__);
		goto err_unregister_input_device;
	}
	ap321xx_psensor_misc.parent = &client->dev;
	return 0;

err_unregister_input_device:
	input_unregister_device(input_dev);
done:
	return rc;
}

static void ap321xx_unregister_psensor_device(struct i2c_client *client, struct ap321xx_data *data)
{
	misc_deregister(&ap321xx_psensor_misc);
	input_unregister_device(data->psensor_input_dev);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend ap321xx_early_suspend;
static void ap321xx_suspend(struct early_suspend *h)
{
	struct i2c_client *client = container_of(ap321xx_lsensor_misc.parent, struct i2c_client, dev);

	if (misc_ps_opened)
		ap321xx_psensor_disable(client);
	if (misc_ls_opened)
		ap321xx_lsensor_disable(client);
}

static void ap321xx_resume(struct early_suspend *h)
{
	struct i2c_client *client = container_of(ap321xx_lsensor_misc.parent, struct i2c_client, dev);

	if (misc_ls_opened)
		ap321xx_lsensor_enable(client);
	if (misc_ps_opened)
		ap321xx_psensor_enable(client);
}
#endif

#endif

/* range */
static ssize_t ap321xx_show_range(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	return sprintf(buf, "%i\n", ap321xx_get_range(data->client));
}

static ssize_t ap321xx_store_range(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	unsigned long val;
	int ret;

	if ((strict_strtoul(buf, 10, &val) < 0) || (val > 3))
		return -EINVAL;

	ret = ap321xx_set_range(data->client, val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(range, S_IWUSR | S_IRUGO,
		   ap321xx_show_range, ap321xx_store_range);


/* mode */
static ssize_t ap321xx_show_mode(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	return sprintf(buf, "%d\n", ap321xx_get_mode(data->client));
}

static ssize_t ap321xx_store_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	unsigned long val;
	int ret;

	if ((strict_strtoul(buf, 10, &val) < 0) || (val > 7))
		return -EINVAL;

	ret = ap321xx_set_mode(data->client, val);
	
	if (ret < 0)
		return ret;
	return count;
}

static DEVICE_ATTR(mode, S_IWUSR | S_IRUGO,
		   ap321xx_show_mode, ap321xx_store_mode);


/* lux */
static ssize_t ap321xx_show_lux(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);

	/* No LUX data if power down */
	if (ap321xx_get_mode(data->client) == 0x00)
		return sprintf((char*) buf, "%s\n", "Please power up first!");

	return sprintf(buf, "%d\n", ap321xx_get_adc_value(data->client));
}

static DEVICE_ATTR(lux, S_IRUGO, ap321xx_show_lux, NULL);


/* Px data */
static ssize_t ap321xx_show_pxvalue(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);

	/* No Px data if power down */
	if (ap321xx_get_mode(data->client) == 0x00)
		return -EBUSY;

	return sprintf(buf, "%d\n", ap321xx_get_px_value(data->client));
}

static DEVICE_ATTR(pxvalue, S_IRUGO, ap321xx_show_pxvalue, NULL);


/* proximity object detect */
static ssize_t ap321xx_show_object(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	return sprintf(buf, "%d\n", ap321xx_get_object(data->client));
}

static DEVICE_ATTR(object, S_IRUGO, ap321xx_show_object, NULL);


/* ALS low threshold */
static ssize_t ap321xx_show_althres(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	return sprintf(buf, "%d\n", ap321xx_get_althres(data->client));
}

static ssize_t ap321xx_store_althres(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	unsigned long val;
	int ret;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	ret = ap321xx_set_althres(data->client, val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(althres, S_IWUSR | S_IRUGO,
		   ap321xx_show_althres, ap321xx_store_althres);


/* ALS high threshold */
static ssize_t ap321xx_show_ahthres(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	return sprintf(buf, "%d\n", ap321xx_get_ahthres(data->client));
}

static ssize_t ap321xx_store_ahthres(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	unsigned long val;
	int ret;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	ret = ap321xx_set_ahthres(data->client, val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(ahthres, S_IWUSR | S_IRUGO,
		   ap321xx_show_ahthres, ap321xx_store_ahthres);

/* Px low threshold */
static ssize_t ap321xx_show_plthres(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	return sprintf(buf, "%d\n", ap321xx_get_plthres(data->client));
}

static ssize_t ap321xx_store_plthres(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	unsigned long val;
	int ret;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	ret = ap321xx_set_plthres(data->client, val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(plthres, S_IWUSR | S_IRUGO,
		   ap321xx_show_plthres, ap321xx_store_plthres);

/* Px high threshold */
static ssize_t ap321xx_show_phthres(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	return sprintf(buf, "%d\n", ap321xx_get_phthres(data->client));
}

static ssize_t ap321xx_store_phthres(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	unsigned long val;
	int ret;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	ret = ap321xx_set_phthres(data->client, val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(phthres, S_IWUSR | S_IRUGO,
		   ap321xx_show_phthres, ap321xx_store_phthres);


/* calibration */
static ssize_t ap321xx_show_calibration_state(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sprintf(buf, "%d\n", cali);
}

static ssize_t ap321xx_store_calibration_state(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct ap321xx_data *data = input_get_drvdata(input);
	int stdls, lux; 
	char tmp[10];

	/* No LUX data if not operational */
	if (ap321xx_get_mode(data->client) == 0x00)
	{
		printk("Please power up first!");
		return -EINVAL;
	}

	cali = 100;
	sscanf(buf, "%d %s", &stdls, tmp);

	if (!strncmp(tmp, "-setcv", 6))
	{
		cali = stdls;
		return -EBUSY;
	}

	if (stdls < 0)
	{
		printk("Std light source: [%d] < 0 !!!\nCheck again, please.\n\
		Set calibration factor to 100.\n", stdls);
		return -EBUSY;
	}

	lux = ap321xx_get_adc_value(data->client);
	cali = stdls * 100 / lux;

	return -EBUSY;
}

static DEVICE_ATTR(calibration, S_IWUSR | S_IRUGO,
		   ap321xx_show_calibration_state, ap321xx_store_calibration_state);

#ifdef LSC_DBG
/* engineer mode */
static ssize_t ap321xx_em_read(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ap321xx_data *data = i2c_get_clientdata(client);
	int i;
	u8 tmp;
	
	for (i = 0; i < reg_num; i++)
	{
		tmp = i2c_smbus_read_byte_data(data->client, reg_array[i]);

		printk("Reg[0x%x] Val[0x%x]\n", reg_array[i], tmp);
	}

	return 0;
}

static ssize_t ap321xx_em_write(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ap321xx_data *data = i2c_get_clientdata(client);
	u32 addr,val,idx=0;
	int ret = 0;

	sscanf(buf, "%x%x", &addr, &val);

	printk("Write [%x] to Reg[%x]...\n",val,addr);

	ret = i2c_smbus_write_byte_data(data->client, addr, val);
	ADD_TO_IDX(addr,idx)
	if (!ret)
		data->reg_cache[idx] = val;

	return count;
}
static DEVICE_ATTR(em, S_IWUSR |S_IRUGO,
				   ap321xx_em_read, ap321xx_em_write);
#endif

static struct attribute *ap321xx_attributes[] = {
	&dev_attr_range.attr,
	&dev_attr_mode.attr,
	&dev_attr_lux.attr,
	&dev_attr_object.attr,
	&dev_attr_pxvalue.attr,
	&dev_attr_althres.attr,
	&dev_attr_ahthres.attr,
	&dev_attr_plthres.attr,
	&dev_attr_phthres.attr,
	&dev_attr_calibration.attr,
#ifdef LSC_DBG
	&dev_attr_em.attr,
#endif
	NULL
};

static const struct attribute_group ap321xx_attr_group = {
	.attrs = ap321xx_attributes,
};

static int Product_Detect(struct i2c_client *client)
{
	int mid = i2c_smbus_read_byte_data(client, 0x03);
	int pid = i2c_smbus_read_byte_data(client, 0x04);
	int rid = i2c_smbus_read_byte_data(client, 0x05);

	if ( mid == 0x01 && pid == 0x01 && 
	    (rid == 0x03 || rid == 0x04) )
	{
		LDBG("RevID [%d], ==> DA3212 v1.5~1.8 ...... AP3212B detected\n", rid)
		reg_array = ap3212b_reg;
		range = ap3212b_range;
		reg_num = AP3212B_NUM_CACHABLE_REGS;
	}
	else if ( (mid == 0x01 && pid == 0x02 && rid == 0x00) || 
		      (mid == 0x02 && pid == 0x02 && rid == 0x01))
	{
		LDBG("RevID [%d], ==> DA3212 v2.0 ...... AP3212C/AP3216C detected\n", rid)
		reg_array = ap3216c_reg;
		range = ap3216c_range;
		reg_num = AP3216C_NUM_CACHABLE_REGS;
	}
	else
	{
		LDBG("MakeID[%d] ProductID[%d] RevID[%d] .... can't detect ... bad reversion!!!\n", mid, pid, rid)
		return -EIO;
	}
		

	return 0;
}

static int ap321xx_init_client(struct i2c_client *client)
{
	struct ap321xx_data *data = i2c_get_clientdata(client);
	int i;

	/* read all the registers once to fill the cache.
	 * if one of the reads fails, we consider the init failed */
	for (i = 0; i < reg_num; i++) {
		int v = i2c_smbus_read_byte_data(client, reg_array[i]);
		if (v < 0)
			return -ENODEV;

		data->reg_cache[i] = v;
	}

	/* set defaults */
	ap321xx_set_range(client, 0);
	ap321xx_set_mode(client, 0);

	return 0;
}

/*
 * I2C layer
 */

static irqreturn_t ap321xx_irq(int irq, void *data_)
{
	struct ap321xx_data *data = data_;
	u8 int_stat;
	int Pval;
 
	int_stat = ap321xx_get_intstat(data->client);

	// ALS int
	if (int_stat & AP3212B_INT_AMASK)
	{
		ap321xx_change_ls_threshold(data->client);
	}
	
	// PX int
	if (int_stat & AP3212B_INT_PMASK)
	{
		Pval = ap321xx_get_object(data->client);
		LDBG("%s\n", Pval ? "obj near":"obj far");
		input_report_abs(data->psensor_input_dev, ABS_DISTANCE, Pval);
		input_sync(data->psensor_input_dev);
	}

    return IRQ_HANDLED;
}

static int __devinit ap321xx_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	const struct ap321xx_platform_data *pdata = client->dev.platform_data;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct ap321xx_data *data;
	int err = 0;

	LDBG("ap321xx_probe\n");
	
	if (pdata->init_platform_hw) {
		err = pdata->init_platform_hw();
		if (err < 0)
			goto exit_free_gpio;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)){
		err = -EIO;
		goto exit_free_gpio;
	}
	err = Product_Detect(client);
	if (err)
	{
		dev_err(&client->dev, "ret: %d, product version detect failed.\n",err);
		err = -EIO;
		goto exit_free_gpio;
	}

	data = kzalloc(sizeof(struct ap321xx_data), GFP_KERNEL);
	if (!data){
		err = -ENOMEM;
		goto exit_free_gpio;
	}
	
	data->client = client;
	i2c_set_clientdata(client, data);
	data->irq = client->irq;

	/* initialize the AP3212B chip */
	err = ap321xx_init_client(client);
	if (err)
		goto exit_kfree;

	err = ap321xx_register_lsensor_device(client,data);
	if (err){
		dev_err(&client->dev, "failed to register_lsensor_device\n");
		goto exit_kfree;
	}
		
	err = ap321xx_register_psensor_device(client, data);
	if (err) {
		dev_err(&client->dev, "failed to register_psensor_device\n");
		goto exit_free_ls_device;
	}

#if 0
	/* register sysfs hooks */
	err = sysfs_create_group(&data->input->dev.kobj, &ap321xx_attr_group);
	if (err)
		goto exit_free_ps_device;
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	ap321xx_early_suspend.suspend = ap321xx_suspend;
	ap321xx_early_suspend.resume  = ap321xx_resume;
	ap321xx_early_suspend.level   = 0x02;
	register_early_suspend(&ap321xx_early_suspend);
#endif


	err = request_threaded_irq(client->irq, NULL, ap321xx_irq,
                               IRQF_TRIGGER_FALLING,
                               "ap321xx", data);
    if (err) {
		dev_err(&client->dev, "ret: %d, could not get IRQ %d\n",err,client->irq);
            goto exit_free_ps_device;
    }

	dev_info(&client->dev, "Driver version %s enabled\n", DRIVER_VERSION);
	return 0;

exit_free_ps_device:
	ap321xx_unregister_psensor_device(client,data);

exit_free_ls_device:
	ap321xx_unregister_lsensor_device(client,data);

exit_kfree:
	kfree(data);
exit_free_gpio:
	if (pdata->exit_platform_hw) {
		pdata->exit_platform_hw();
	}
	return err;
}

static int __devexit ap321xx_remove(struct i2c_client *client)
{
	const struct ap321xx_platform_data *pdata = client->dev.platform_data;
	struct ap321xx_data *data = i2c_get_clientdata(client);
	free_irq(data->irq, data);

//	sysfs_remove_group(&data->input->dev.kobj, &ap321xx_attr_group);
	ap321xx_unregister_psensor_device(client,data);
	ap321xx_unregister_lsensor_device(client,data);
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ap321xx_early_suspend);
#endif

	ap321xx_set_mode(client, 0);
	kfree(i2c_get_clientdata(client));
	if (pdata->exit_platform_hw) {
		pdata->exit_platform_hw();
	}
	return 0;
}

static const struct i2c_device_id ap321xx_id[] = {
	{ AP3212B_DRV_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ap321xx_id);

static struct i2c_driver ap321xx_driver = {
	.driver = {
		.name	= AP3212B_DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	= ap321xx_probe,
	.remove	= __devexit_p(ap321xx_remove),
	.id_table = ap321xx_id,
};

static int __init ap321xx_init(void)
{
	LDBG("ap321xx_init\n");
	return i2c_add_driver(&ap321xx_driver);
}

static void __exit ap321xx_exit(void)
{
	i2c_del_driver(&ap321xx_driver);
}

MODULE_AUTHOR("YC Hou, LiteOn-semi corporation.");
MODULE_DESCRIPTION("Test AP3212B, AP3212C and AP3216C driver on mini6410.");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);

module_init(ap321xx_init);
module_exit(ap321xx_exit);


