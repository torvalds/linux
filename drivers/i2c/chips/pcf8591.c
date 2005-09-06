/*
    pcf8591.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (C) 2001-2004 Aurelien Jarno <aurelien@aurel32.net>
    Ported to Linux 2.6 by Aurelien Jarno <aurelien@aurel32.net> with 
    the help of Jean Delvare <khali@linux-fr.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b, 0x4c,
					0x4d, 0x4e, 0x4f, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(pcf8591);

static int input_mode;
module_param(input_mode, int, 0);
MODULE_PARM_DESC(input_mode,
	"Analog input mode:\n"
	" 0 = four single ended inputs\n"
	" 1 = three differential inputs\n"
	" 2 = single ended and differential mixed\n"
	" 3 = two differential inputs\n");

/* The PCF8591 control byte
      7    6    5    4    3    2    1    0  
   |  0 |AOEF|   AIP   |  0 |AINC|  AICH   | */

/* Analog Output Enable Flag (analog output active if 1) */
#define PCF8591_CONTROL_AOEF		0x40
					
/* Analog Input Programming 
   0x00 = four single ended inputs
   0x10 = three differential inputs
   0x20 = single ended and differential mixed
   0x30 = two differential inputs */
#define PCF8591_CONTROL_AIP_MASK	0x30

/* Autoincrement Flag (switch on if 1) */
#define PCF8591_CONTROL_AINC		0x04

/* Channel selection
   0x00 = channel 0 
   0x01 = channel 1
   0x02 = channel 2
   0x03 = channel 3 */
#define PCF8591_CONTROL_AICH_MASK	0x03

/* Initial values */
#define PCF8591_INIT_CONTROL	((input_mode << 4) | PCF8591_CONTROL_AOEF)
#define PCF8591_INIT_AOUT	0	/* DAC out = 0 */

/* Conversions */
#define REG_TO_SIGNED(reg)	(((reg) & 0x80)?((reg) - 256):(reg))

struct pcf8591_data {
	struct i2c_client client;
	struct semaphore update_lock;

	u8 control;
	u8 aout;
};

static int pcf8591_attach_adapter(struct i2c_adapter *adapter);
static int pcf8591_detect(struct i2c_adapter *adapter, int address, int kind);
static int pcf8591_detach_client(struct i2c_client *client);
static void pcf8591_init_client(struct i2c_client *client);
static int pcf8591_read_channel(struct device *dev, int channel);

/* This is the driver that will be inserted */
static struct i2c_driver pcf8591_driver = {
	.owner		= THIS_MODULE,
	.name		= "pcf8591",
	.id		= I2C_DRIVERID_PCF8591,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= pcf8591_attach_adapter,
	.detach_client	= pcf8591_detach_client,
};

/* following are the sysfs callback functions */
#define show_in_channel(channel)					\
static ssize_t show_in##channel##_input(struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return sprintf(buf, "%d\n", pcf8591_read_channel(dev, channel));\
}									\
static DEVICE_ATTR(in##channel##_input, S_IRUGO,			\
		   show_in##channel##_input, NULL);

show_in_channel(0);
show_in_channel(1);
show_in_channel(2);
show_in_channel(3);

static ssize_t show_out0_ouput(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pcf8591_data *data = i2c_get_clientdata(to_i2c_client(dev));
	return sprintf(buf, "%d\n", data->aout * 10);
}

static ssize_t set_out0_output(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int value;
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8591_data *data = i2c_get_clientdata(client);
	if ((value = (simple_strtoul(buf, NULL, 10) + 5) / 10) <= 255) {
		data->aout = value;
		i2c_smbus_write_byte_data(client, data->control, data->aout);
		return count;
	}
	return -EINVAL;
}

static DEVICE_ATTR(out0_output, S_IWUSR | S_IRUGO, 
		   show_out0_ouput, set_out0_output);

static ssize_t show_out0_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pcf8591_data *data = i2c_get_clientdata(to_i2c_client(dev));
	return sprintf(buf, "%u\n", !(!(data->control & PCF8591_CONTROL_AOEF)));
}

static ssize_t set_out0_enable(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8591_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	if (val)
		data->control |= PCF8591_CONTROL_AOEF;
	else
		data->control &= ~PCF8591_CONTROL_AOEF;
	i2c_smbus_write_byte(client, data->control);
	up(&data->update_lock);
	return count;
}

static DEVICE_ATTR(out0_enable, S_IWUSR | S_IRUGO, 
		   show_out0_enable, set_out0_enable);

/*
 * Real code
 */
static int pcf8591_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, pcf8591_detect);
}

/* This function is called by i2c_probe */
int pcf8591_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct pcf8591_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE
				     | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		goto exit;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet. */
	if (!(data = kmalloc(sizeof(struct pcf8591_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	memset(data, 0, sizeof(struct pcf8591_data));
	
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &pcf8591_driver;
	new_client->flags = 0;

	/* Now, we would do the remaining detection. But the PCF8591 is plainly
	   impossible to detect! Stupid chip. */

	/* Determine the chip type - only one kind supported! */
	if (kind <= 0)
		kind = pcf8591;

	/* Fill in the remaining client fields and put it into the global 
	   list */
	strlcpy(new_client->name, "pcf8591", I2C_NAME_SIZE);
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_kfree;

	/* Initialize the PCF8591 chip */
	pcf8591_init_client(new_client);

	/* Register sysfs hooks */
	device_create_file(&new_client->dev, &dev_attr_out0_enable);
	device_create_file(&new_client->dev, &dev_attr_out0_output);
	device_create_file(&new_client->dev, &dev_attr_in0_input);
	device_create_file(&new_client->dev, &dev_attr_in1_input);

	/* Register input2 if not in "two differential inputs" mode */
	if (input_mode != 3 )
		device_create_file(&new_client->dev, &dev_attr_in2_input);
		
	/* Register input3 only in "four single ended inputs" mode */
	if (input_mode == 0)
		device_create_file(&new_client->dev, &dev_attr_in3_input);
	
	return 0;
	
	/* OK, this is not exactly good programming practice, usually. But it is
	   very code-efficient in this case. */

exit_kfree:
	kfree(data);
exit:
	return err;
}

static int pcf8591_detach_client(struct i2c_client *client)
{
	int err;

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(i2c_get_clientdata(client));
	return 0;
}

/* Called when we have found a new PCF8591. */
static void pcf8591_init_client(struct i2c_client *client)
{
	struct pcf8591_data *data = i2c_get_clientdata(client);
	data->control = PCF8591_INIT_CONTROL;
	data->aout = PCF8591_INIT_AOUT;

	i2c_smbus_write_byte_data(client, data->control, data->aout);
	
	/* The first byte transmitted contains the conversion code of the 
	   previous read cycle. FLUSH IT! */
	i2c_smbus_read_byte(client);
}

static int pcf8591_read_channel(struct device *dev, int channel)
{
	u8 value;
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8591_data *data = i2c_get_clientdata(client);

	down(&data->update_lock);

	if ((data->control & PCF8591_CONTROL_AICH_MASK) != channel) {
		data->control = (data->control & ~PCF8591_CONTROL_AICH_MASK)
			      | channel;
		i2c_smbus_write_byte(client, data->control);
	
		/* The first byte transmitted contains the conversion code of 
		   the previous read cycle. FLUSH IT! */
		i2c_smbus_read_byte(client);
	}
	value = i2c_smbus_read_byte(client);

	up(&data->update_lock);

	if ((channel == 2 && input_mode == 2) ||
	    (channel != 3 && (input_mode == 1 || input_mode == 3)))
		return (10 * REG_TO_SIGNED(value));
	else
		return (10 * value);
}

static int __init pcf8591_init(void)
{
	if (input_mode < 0 || input_mode > 3) {
		printk(KERN_WARNING "pcf8591: invalid input_mode (%d)\n",
		       input_mode);
		input_mode = 0;
	}
	return i2c_add_driver(&pcf8591_driver);
}

static void __exit pcf8591_exit(void)
{
	i2c_del_driver(&pcf8591_driver);
}

MODULE_AUTHOR("Aurelien Jarno <aurelien@aurel32.net>");
MODULE_DESCRIPTION("PCF8591 driver");
MODULE_LICENSE("GPL");

module_init(pcf8591_init);
module_exit(pcf8591_exit);
