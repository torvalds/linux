/*
 * ISA1200 linear virbrator driver
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/earlysuspend.h>
#include <linux/input/isa1200.h>
#include <linux/pwm.h>

#include <plat/gpio-cfg.h>
#include "../../staging/android/timed_output.h"

#define ISA1200_VERSION  "1.0.0"
#define ISA1200_NAME "isa1200"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
/*
 * driver private data
 */
struct isa1200_chip {
	struct i2c_client *i2c;
	struct isa1200_platform_data *pdata;

	struct pwm_device *pwm;
	struct timed_output_dev timed_output;

	int period;
	int duty;
	int pwm_id;
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
enum
{
	HCTRL_0 = 0x30,
	HCTRL_1,
	HCTRL_2,
	HCTRL_3,
	HCTRL_4,
	HCTRL_5,
	HCTRL_6,
	HCTRL_7,
	HCTRL_8,
	HCTRL_9,
	HCTRL_A,
	HCTRL_B,
	HCTRL_C,
	HCTRL_D,
	HCTRL_E,
	HCTRL_F,
	HCTRL_MAX = 0x40
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int isa1200_hw_init(struct isa1200_chip *chip)
{
	struct i2c_client *client = chip->i2c;
	int ret =0;
	unsigned val=0;

	// gpio_hen enable
	ret = gpio_is_valid(chip->pdata->gpio_hen);
	if (ret) {
		ret = gpio_request(chip->pdata->gpio_hen, "gpio_hen");
		if (ret) {
			dev_err(&client->dev, "gpio %d request failed\n",
					chip->pdata->gpio_hen);
			return -EIO;
		}
		ret = gpio_direction_output(chip->pdata->gpio_hen, 1);
		if (ret) {
			dev_err(&client->dev, "gpio %d set direction failed\n",
						chip->pdata->gpio_hen);
			return -EIO;
		}
		gpio_free(chip->pdata->gpio_hen);
	}
	else return -EIO;

	mdelay(1);

	// gpio_len enable
	ret = gpio_is_valid(chip->pdata->gpio_len);
	if (ret) {
		ret = gpio_request(chip->pdata->gpio_len, "gpio_len");
		if (ret) {
			dev_err(&client->dev, "gpio %d request failed\n",
					chip->pdata->gpio_len);
			return -EIO;
		}
		ret = gpio_direction_output(chip->pdata->gpio_len, 1);
		if (ret) {
			dev_err(&client->dev, "gpio %d set direction failed\n",
						chip->pdata->gpio_len);
			return -EIO;
		}
		gpio_free(chip->pdata->gpio_len);
	}
	else return -EIO;
	
	//pwm port pin_func init
	if (gpio_is_valid(chip->pdata->pwm_gpio)) {
		ret = gpio_request(chip->pdata->pwm_gpio, "pwm_gpio");
		if (ret)
			printk(KERN_ERR "failed to get GPIO for PWM0\n");
		s3c_gpio_cfgpin(chip->pdata->pwm_gpio, chip->pdata->pwm_func);
		gpio_free(chip->pdata->pwm_gpio);
    }
    
    mdelay (1);
	i2c_smbus_write_byte_data(client, HCTRL_2, 0x80); //Software reset enable
	mdelay (1);
	i2c_smbus_write_byte_data(client, HCTRL_2, 0x00); //Software reset disable
	mdelay (1);
    i2c_smbus_write_byte_data(client, HCTRL_0, 0x88); // PWM_INPUT Mode

	val = i2c_smbus_read_byte_data(client, 0x00);
	printk("%s : read val = 0x%02x\n",__func__,val);
	
	return 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void isa1200_vibrator_enable (struct timed_output_dev *dev, int value)
{
	struct isa1200_chip *chip = container_of(dev, struct isa1200_chip, timed_output);

	if(10<value) {

		if(5<value)	value -=5;
		pwm_disable(chip->pwm);
		pwm_config(chip->pwm, chip->duty * chip->period / 255, chip->period);
		pwm_enable(chip->pwm);
		msleep_interruptible(value);
		pwm_disable(chip->pwm);
	}
	return;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int isa1200_vibrator_get_time (struct timed_output_dev *dev)
{
	return 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static struct timed_output_dev isa1200_vibrator = {
  .name = "vibrator",
  .get_time = isa1200_vibrator_get_time,
  .enable = isa1200_vibrator_enable,
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int isa1200_i2c_probe (struct i2c_client *client, const struct i2c_device_id *id)
{
	struct isa1200_chip *chip;
	struct device 	*dev = &client->dev;
	int err;
	
	/* setup i2c client */
	if (!i2c_check_functionality (client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "i2c byte data not supported\n");
		return -EIO;
	}
	if (!client->dev.platform_data) {
		dev_err(&client->dev, "pdata is not available\n");
		return -EINVAL;
	}

	chip = kzalloc (sizeof (struct isa1200_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->pdata = client->dev.platform_data;
	chip->i2c = client;
	i2c_set_clientdata (client, chip);

	chip->pwm = pwm_request(chip->pdata->pwm_id, id->name);
	chip->period = chip->pdata->pwm_periode_ns;
	chip->duty = chip->pdata->pwm_duty;

	pwm_disable(chip->pwm);

	isa1200_hw_init(chip);

	chip->timed_output = isa1200_vibrator;
	err = timed_output_dev_register (&chip->timed_output);
	if (err < 0)
		goto error;

	dev_set_drvdata(dev, chip);

	return 0;
	
error:
	kfree (chip);
	return -1;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int isa1200_i2c_remove (struct i2c_client *client)
{
	struct isa1200_chip *isa1200 = i2c_get_clientdata (client);
	
	timed_output_dev_unregister (&isa1200->timed_output);
	i2c_set_clientdata (client, NULL);
	kfree (isa1200);
	
	return 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
static void isa1200_early_suspend(struct early_suspend *h)
{
	printk("\t%s [%d]\n",__FUNCTION__,__LINE__);
	return;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void isa1200_late_resume(struct early_suspend *h)
{
	printk("%s\n",__FUNCTION__);
	return;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static struct early_suspend isa1200_early_suspend_desc = {
     .level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
     .suspend = isa1200_early_suspend,
     .resume = isa1200_late_resume
};
#endif //CONFIG_HAS_EARLYSUSPEND
#endif //CONFIG_PM

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static const struct i2c_device_id isa1200_id[] = {
  {ISA1200_NAME, 0},
  {},
};
MODULE_DEVICE_TABLE (i2c, isa1200_id);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
struct i2c_driver isa1200_driver = {
	.driver = {
	     .name = "isa1200",
	     .owner = THIS_MODULE,
     },
	.probe =    isa1200_i2c_probe,
	.remove =   isa1200_i2c_remove,
	.id_table = isa1200_id,
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int __init isa1200_init (void)
{
	int ret;

	ret = i2c_add_driver (&isa1200_driver);
	if (ret) {
		printk(KERN_ERR "i2c add driver Failed %d\n", ret);
		return -1;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&isa1200_early_suspend_desc);
#endif
	return 0;
}
module_init (isa1200_init);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void __exit isa1200_exit (void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&isa1200_early_suspend_desc);
#endif
	i2c_del_driver (&isa1200_driver);
}
module_exit (isa1200_exit);


MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION ("ISA1200 linear virbrator driver");
MODULE_VERSION (ISA1200_VERSION);
