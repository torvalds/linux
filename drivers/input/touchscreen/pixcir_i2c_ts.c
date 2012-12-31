/* drivers/input/touchscreen/pixcir_i2c_ts.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright 2010 Pixcir, Inc.
 * Copyright 2010 Bee <http://www.pixcir.com.cn>
 * Copyright 2011 Dongsu Ha <dsfine.ha@samsung.com>
 * Copyright 2011 Samsung Electronics <http://www.samsung.com>
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <plat/gpio-cfg.h>
#include <mach/gpio.h>
#include <mach/board_rev.h>

#define	PIXCIR_DEBUG		0

#define ATTB			samsung_board_rev_is_0_0() ? EXYNOS4_GPX1(7) : EXYNOS4_GPX2(6)
#define RESET			samsung_board_rev_is_0_0() ? EXYNOS4_GPX1(6) : EXYNOS4212_GPM3(4)
#define GPIO_NAME		samsung_board_rev_is_0_0() ? "GPX1" : "GPM3"
#define get_attb_value		gpio_get_value
#define	RESETPIN_CFG		s3c_gpio_cfgpin(RESET, S3C_GPIO_OUTPUT)
#define	RESETPIN_SET0		gpio_direction_output(RESET,0)
#define	RESETPIN_SET1		gpio_direction_output(RESET,1)

#define	SLAVE_ADDR		0x5c
#define	BOOTLOADER_ADDR		0x5d

#ifndef	I2C_MAJOR
#define	I2C_MAJOR		125
#endif

#define	I2C_MINORS		256

#define	CALIBRATION_FLAG	1
#define	NORMAL_MODE		8
#define	PIXCIR_DEBUG_MODE	3
#define	VERSION_FLAG		6
#define	BOOTLOADER_MODE		7
#define	RD_EEPROM		12
#define	WR_EEPROM		13

#define  ENABLE_IRQ		10
#define  DISABLE_IRQ		11

#define SPECOP			0x37

#define reset

#define MAXX			32
#define MAXY			32
#define TOUCHSCREEN_MINX	0
#define TOUCHSCREEN_MAXX	480
#define TOUCHSCREEN_MINY	0
#define TOUCHSCREEN_MAXY	800

int global_irq;

static unsigned char status_reg;
unsigned char read_XN_YN_flag;

unsigned char global_touching, global_oldtouching;
unsigned char global_posx1_low, global_posx1_high, global_posy1_low,
	      global_posy1_high, global_posx2_low, global_posx2_high,
	      global_posy2_low, global_posy2_high;

unsigned char Tango_number;

unsigned char interrupt_flag;

unsigned char x_nb_electrodes;
unsigned char y_nb_electrodes;
unsigned char x2_nb_electrodes;
unsigned char x1_x2_nb_electrodes;

signed char xy_raw1[(MAXX * 2 + 3)];
signed char xy_raw2[MAXX * 2];
signed char xy_raw12[(MAXX * 4 + 3)];

unsigned char data2eep[3], op2eep[2];

struct i2c_dev {
	struct list_head list;
	struct i2c_adapter *adap;
	struct device *dev;
};

static struct i2c_driver pixcir_i2c_ts_driver;
static struct class *i2c_dev_class;
static LIST_HEAD(i2c_dev_list);
static DEFINE_SPINLOCK(i2c_dev_list_lock);

static void return_i2c_dev(struct i2c_dev *i2c_dev)
{
	spin_lock(&i2c_dev_list_lock);
	list_del(&i2c_dev->list);
	spin_unlock(&i2c_dev_list_lock);
	kfree(i2c_dev);
}

static struct i2c_dev *i2c_dev_get_by_minor(unsigned index)
{
	struct i2c_dev *i2c_dev;
	i2c_dev = NULL;

	spin_lock(&i2c_dev_list_lock);
	list_for_each_entry(i2c_dev, &i2c_dev_list, list) {
		if (i2c_dev->adap->nr == index)
			goto found;
	}
	i2c_dev = NULL;
found:
	spin_unlock(&i2c_dev_list_lock);

	return i2c_dev;
}

static struct i2c_dev *get_free_i2c_dev(struct i2c_adapter *adap)
{
	struct i2c_dev *i2c_dev;

	if (adap->nr >= I2C_MINORS) {
		printk(KERN_ERR "i2c-dev: Out of device minors (%d)\n",
				adap->nr);
		return ERR_PTR(-ENODEV);
	}

	i2c_dev = kzalloc(sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return ERR_PTR(-ENOMEM);
	i2c_dev->adap = adap;

	spin_lock(&i2c_dev_list_lock);
	list_add_tail(&i2c_dev->list, &i2c_dev_list);
	spin_unlock(&i2c_dev_list_lock);

	return i2c_dev;
}

void read_XN_YN_value(struct i2c_client *client)
{
	char Wrbuf[4], Rdbuf[2];

	memset(Wrbuf, 0, sizeof(Wrbuf));
	memset(Rdbuf, 0, sizeof(Rdbuf));

	Wrbuf[0] = SPECOP;
	Wrbuf[1] = 1;

	Wrbuf[2] = 64;
	Wrbuf[3] = 0;

	i2c_master_send(client, Wrbuf, 4);
	mdelay(8);
	i2c_master_recv(client, Rdbuf, 2);
	x_nb_electrodes = Rdbuf[0];

	if (Tango_number == 1) {
		x2_nb_electrodes = 0;

		memset(Wrbuf, 0, sizeof(Wrbuf));
		memset(Rdbuf, 0, sizeof(Rdbuf));

		Wrbuf[0] = SPECOP;
		Wrbuf[1] = 1;
		Wrbuf[2] = 203;
		Wrbuf[3] = 0;

		i2c_master_send(client, Wrbuf, 4);
		mdelay(4);

		i2c_master_recv(client, Rdbuf, 2);
		y_nb_electrodes = Rdbuf[0];
	} else if (Tango_number == 2) {
		memset(Wrbuf, 0, sizeof(Wrbuf));
		memset(Rdbuf, 0, sizeof(Rdbuf));

		Wrbuf[0] = SPECOP;
		Wrbuf[1] = 1;

		i2c_master_send(client, Wrbuf, 4);
		mdelay(4);
		i2c_master_recv(client, Rdbuf, 2);
		x2_nb_electrodes = Rdbuf[0];

		memset(Wrbuf, 0, sizeof(Wrbuf));
		memset(Rdbuf, 0, sizeof(Rdbuf));

		Wrbuf[0] = SPECOP;
		Wrbuf[1] = 1;

		i2c_master_send(client, Wrbuf, 4);
		mdelay(4);

		i2c_master_recv(client, Rdbuf, 2);
		y_nb_electrodes = Rdbuf[0];
	}
	if (x2_nb_electrodes)
		x1_x2_nb_electrodes = x_nb_electrodes + x2_nb_electrodes - 1;
	else
		x1_x2_nb_electrodes = x_nb_electrodes;

	read_XN_YN_flag = 1;
}

void read_XY_tables(struct i2c_client *client, signed char *xy_raw1_buf,
		signed char *xy_raw2_buf)
{
	u_int8_t Wrbuf[1];

	memset(Wrbuf, 0, sizeof(Wrbuf));

	i2c_master_send(client, Wrbuf, 1);
	i2c_master_recv(client, xy_raw1_buf, (MAXX - 1) * 2);
	i2c_master_send(client, Wrbuf, 1);
	i2c_master_recv(client, xy_raw2_buf, (MAXX - 1) * 2);
}

static struct workqueue_struct *pixcir_wq;

struct pixcir_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct input_dev *input_key;
	struct delayed_work work;
	int irq;
};

static unsigned char pixcir_keycode[] = {KEY_D, KEY_A, KEY_B};

static void pixcir_ts_poscheck(struct work_struct *work)
{
	struct pixcir_i2c_ts_data *tsdata =
		container_of(work, struct pixcir_i2c_ts_data, work.work);
	unsigned char touching = 0;
	unsigned char oldtouching = 0;
	int posx1, posy1, posx2, posy2;
	unsigned char Rdbuf[10], Wrbuf[1];
	int z = 50;
	int w = 15;
	static int pressed_keycode = -1;

	interrupt_flag = 1;

	memset(Wrbuf, 0, sizeof(Wrbuf));
	memset(Rdbuf, 0, sizeof(Rdbuf));

	Wrbuf[0] = 0;

	i2c_master_send(tsdata->client, Wrbuf, 1);
	i2c_master_recv(tsdata->client, Rdbuf, sizeof(Rdbuf));

	posx1 = ((Rdbuf[5] << 8) | Rdbuf[4]);
	posy1 = ((Rdbuf[3] << 8) | Rdbuf[2]);
	posx2 = ((Rdbuf[9] << 8) | Rdbuf[8]);
	posy2 = ((Rdbuf[7] << 8) | Rdbuf[6]);

	posx1 = TOUCHSCREEN_MAXX - posx1;
	posx2 = TOUCHSCREEN_MAXX - posx2;

	touching = Rdbuf[0];
	oldtouching = Rdbuf[1];

	if (touching == 1 && posy1 > 800) {
		if (posx1 < 100) 		/* MENU KEY */
			pressed_keycode = 0;
		else if (posx1 > (240 - 50) && posx1 < (240 + 50)) /* HOME KEY */
			pressed_keycode = 1;
		else if (posx1 > (480 - 100))	/* BACK KEY */
			pressed_keycode = 2;
		else
			pressed_keycode = -1;

		if (pressed_keycode != -1) {
			input_event(tsdata->input_key, EV_MSC, MSC_SCAN,
					pressed_keycode);
			input_report_key(tsdata->input_key,
					pixcir_keycode[pressed_keycode], 1);
			input_sync(tsdata->input_key);
		}
	} else {
		if (touching) {
			input_report_abs(tsdata->input, ABS_X, posx1);
			input_report_abs(tsdata->input, ABS_Y, posy1);
			input_report_key(tsdata->input, BTN_TOUCH, 1);
			input_report_abs(tsdata->input, ABS_PRESSURE, 1);
		} else {
			input_report_key(tsdata->input, BTN_TOUCH, 0);
			input_report_abs(tsdata->input, ABS_PRESSURE, 0);
		}

		if (!(touching)) {
			z = 0;
			w = 0;
		}
		if (touching == 1) {
			input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, w);
			input_report_abs(tsdata->input, ABS_MT_POSITION_X, posx1);
			input_report_abs(tsdata->input, ABS_MT_POSITION_Y, posy1);
			input_mt_sync(tsdata->input);
		} else if (touching == 2) {
			input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, w);
			input_report_abs(tsdata->input, ABS_MT_POSITION_X, posx1);
			input_report_abs(tsdata->input, ABS_MT_POSITION_Y, posy1);
			input_mt_sync(tsdata->input);

			input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, w);
			input_report_abs(tsdata->input, ABS_MT_POSITION_X, posx2);
			input_report_abs(tsdata->input, ABS_MT_POSITION_Y, posy2);
			input_mt_sync(tsdata->input);
		}
		input_sync(tsdata->input);
	}

	if (touching == 0) {
		if (pressed_keycode != -1) {
			input_event (tsdata->input_key, EV_MSC, MSC_SCAN, pressed_keycode);
			input_report_key (tsdata->input_key, pixcir_keycode[pressed_keycode], 0);
			input_sync(tsdata->input_key);
			pressed_keycode = -1;
		}
		else {
			input_mt_sync(tsdata->input);
			input_sync(tsdata->input);
		}
	}

	if (status_reg == NORMAL_MODE) {
		global_touching =	touching;
		global_oldtouching =	oldtouching;
		global_posx1_low =	Rdbuf[2];
		global_posx1_high =	Rdbuf[3];
		global_posy1_low =	Rdbuf[4];
		global_posy1_high =	Rdbuf[5];
		global_posx2_low =	Rdbuf[6];
		global_posx2_high =	Rdbuf[7];
		global_posy2_low =	Rdbuf[8];
		global_posy2_high =	Rdbuf[9];
	}

	enable_irq(tsdata->irq);
}

static irqreturn_t pixcir_ts_isr(int irq, void *dev_id)
{
	struct pixcir_i2c_ts_data *tsdata = dev_id;

	if ((status_reg == 0) || (status_reg == NORMAL_MODE)) {
		disable_irq_nosync(irq);
		queue_work(pixcir_wq, &tsdata->work.work);
	}

	return IRQ_HANDLED;
}

static int pixcir_ts_open(struct input_dev *dev)
{
	return 0;
}

static void pixcir_ts_close(struct input_dev *dev)
{
}

static int pixcir_i2c_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pixcir_i2c_ts_data *tsdata;
	struct input_dev *input;
	struct input_dev *input_key;
	struct device *dev;
	struct i2c_dev *i2c_dev;
	int error = 0;

	tsdata = kzalloc(sizeof(*tsdata), GFP_KERNEL);
	if (!tsdata) {
		dev_err(&client->dev, "failed to allocate driver data!\n");
		error = -ENOMEM;
		dev_set_drvdata(&client->dev, NULL);
		return error;
	}

	dev_set_drvdata(&client->dev, tsdata);

	input = input_allocate_device();
	if (!input) {
		dev_err(&client->dev, "failed to allocate input device!\n");
		error = -ENOMEM;
		goto err_free_tsdata;
	}

	set_bit(EV_SYN, input->evbit);
	set_bit(EV_KEY, input->evbit);
	set_bit(EV_ABS, input->evbit);
	set_bit(BTN_TOUCH, input->keybit);
	input_set_abs_params(input, ABS_X, TOUCHSCREEN_MINX,
			TOUCHSCREEN_MAXX, 0, 0);
	input_set_abs_params(input, ABS_Y, TOUCHSCREEN_MINY,
			TOUCHSCREEN_MAXY, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X,
			TOUCHSCREEN_MINX, TOUCHSCREEN_MAXX, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y,
			TOUCHSCREEN_MINY, TOUCHSCREEN_MAXY, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 25, 0, 0);

	input->name = "pixcir-i2c-ts";
	input->phys = "pixcir_ts/input1";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;
	input->open = pixcir_ts_open;
	input->close = pixcir_ts_close;

	input_set_drvdata(input, tsdata);

	tsdata->client = client;
	tsdata->input = input;

	INIT_WORK(&tsdata->work.work, pixcir_ts_poscheck);

	tsdata->irq = client->irq;
	global_irq = client->irq;

	if (input_register_device(input)) {
		error = -EIO;
		goto err_free_input;
	}

	/* for keypad */
	input_key = input_allocate_device();
	if (!input_key) {
		dev_err(&client->dev, "failed to allocate input device!\n");
		error = -ENOMEM;
		goto err_unregister_input;
	}

	input_key->evbit[0] = BIT_MASK(EV_KEY);
	input_set_capability(input_key, EV_MSC, MSC_SCAN);

	input_key->keycode = pixcir_keycode;
	input_key->keycodesize = sizeof(unsigned char);
	input_key->keycodemax = ARRAY_SIZE(pixcir_keycode);

	__set_bit(pixcir_keycode[0], input_key->keybit);
	__set_bit(pixcir_keycode[1], input_key->keybit);
	__set_bit(pixcir_keycode[2], input_key->keybit);
	__clear_bit(KEY_RESERVED, input_key->keybit);

	input_key->name = "pixcir-i2c-ts_key";
	input_key->phys = "pixcir_ts/input2";
	input_key->id.bustype = BUS_I2C;
	input_key->dev.parent = &client->dev;
	input_key->open = pixcir_ts_open;
	input_key->close = pixcir_ts_close;

	tsdata->input_key = input_key;

	if (input_register_device(input_key)) {
		error = -EIO;
		goto err_free_input_key;
	}

	if (gpio_request(RESET, GPIO_NAME)) {
		error = -EIO;
		goto err_unregister_input_key;
	}
	RESETPIN_CFG;
	RESETPIN_SET0;
	mdelay(20);
	RESETPIN_SET1;

	mdelay(30);

	error = request_irq(tsdata->irq, pixcir_ts_isr, IRQF_TRIGGER_FALLING,
				"pixcir_ts_irq", tsdata);
	if (error) {
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		goto err_unregister_input_key;
	}

	s3c_gpio_setpull(ATTB, S3C_GPIO_PULL_NONE);

	device_init_wakeup(&client->dev, 0);

	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		goto err_free_irq;
	}

	dev = device_create(i2c_dev_class, &client->adapter->dev,
			MKDEV(I2C_MAJOR, client->adapter->nr), NULL,
			"pixcir_i2c_ts%d", 0);
	if (IS_ERR(dev)) {
		error = PTR_ERR(dev);
		goto err_free_irq;
	}

	dev_err(&tsdata->client->dev, "insmod successfully!\n");

	return 0;

 err_free_irq:
	free_irq(tsdata->irq, input);
 err_unregister_input_key:
	input_unregister_device(input_key);
 err_free_input_key:
	input_free_device(input_key);
 err_unregister_input:
	input_unregister_device(input);
 err_free_input:
	input_free_device(input);
 err_free_tsdata:
	kfree(tsdata);

	return error;
}

static int pixcir_i2c_ts_remove(struct i2c_client *client)
{
	int error;
	struct i2c_dev *i2c_dev;
	struct pixcir_i2c_ts_data *tsdata = dev_get_drvdata(&client->dev);

	free_irq(tsdata->irq, tsdata);
	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}
	return_i2c_dev(i2c_dev);
	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, client->adapter->nr));
	input_unregister_device(tsdata->input);
	input_free_device(tsdata->input);
	input_unregister_device(tsdata->input_key);
	input_free_device(tsdata->input_key);
	kfree(tsdata);
	dev_set_drvdata(&client->dev, NULL);

	return 0;
}

static int pixcir_i2c_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct pixcir_i2c_ts_data *tsdata = dev_get_drvdata(&client->dev);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(tsdata->irq);

	return 0;
}

static int pixcir_i2c_ts_resume(struct i2c_client *client)
{
	struct pixcir_i2c_ts_data *tsdata = dev_get_drvdata(&client->dev);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(tsdata->irq);

	return 0;
}

static int pixcir_open(struct inode *inode, struct file *file)
{
	int subminor;
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	struct i2c_dev *i2c_dev;

	subminor = iminor(inode);

	i2c_dev = i2c_dev_get_by_minor(subminor);
	if (!i2c_dev) {
		printk(KERN_ERR "error i2c_dev\n");
		return -ENODEV;
	}

	adapter = i2c_get_adapter(i2c_dev->adap->nr);
	if (!adapter)
		return -ENODEV;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		i2c_put_adapter(adapter);
		return -ENOMEM;
	}
	snprintf(client->name, I2C_NAME_SIZE, "pixcir_i2c_ts%d", adapter->nr);
	client->driver = &pixcir_i2c_ts_driver;
	client->adapter = adapter;
	file->private_data = client;

	return 0;
}

static long pixcir_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client =
		(struct i2c_client *) file->private_data;

	switch (cmd) {
	case CALIBRATION_FLAG:
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		status_reg = CALIBRATION_FLAG;
		break;

	case NORMAL_MODE:
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		status_reg = NORMAL_MODE;
		break;

	case PIXCIR_DEBUG_MODE:
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		status_reg = PIXCIR_DEBUG_MODE;

		Tango_number = arg;
		break;

	case BOOTLOADER_MODE:
		status_reg = 0;
		status_reg = BOOTLOADER_MODE;
		disable_irq_nosync(global_irq);

#ifdef reset
		client->addr = BOOTLOADER_ADDR;

		RESETPIN_CFG;
		RESETPIN_SET0;
		mdelay(20);
		RESETPIN_SET1;

		mdelay(30);
#else
		client->addr = SLAVE_ADDR;
		tmp[0] = SPECOP;
		tmp[1] = 5;
		i2c_master_send(client, tmp, 2);

		client->addr = BOOTLOADER_ADDR;
#endif
		break;

	case ENABLE_IRQ:
		enable_irq(global_irq);
		status_reg = 0;
		break;

	case DISABLE_IRQ:
		disable_irq_nosync(global_irq);
		break;

	case RD_EEPROM:
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		status_reg = RD_EEPROM;
		break;

	case WR_EEPROM:
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		status_reg = WR_EEPROM;
break;

	case VERSION_FLAG:
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		status_reg = VERSION_FLAG;

		Tango_number = arg;
		break;

	default:
		break;
	}
	return 0;
}

static ssize_t pixcir_read(struct file *file,
		char __user *buf, size_t count, loff_t *offset)
{
	struct i2c_client *client =
		(struct i2c_client *)file->private_data;
	int ret = 0;
	unsigned char normal_tmp[10];

	switch (status_reg) {
	case NORMAL_MODE:
		memset(normal_tmp, 0, sizeof(normal_tmp));
		if (interrupt_flag) {
			normal_tmp[0] = global_touching;
			normal_tmp[1] = global_oldtouching;
			normal_tmp[2] = global_posx1_low;
			normal_tmp[3] = global_posx1_high;
			normal_tmp[4] = global_posy1_low;
			normal_tmp[5] = global_posy1_high;
			normal_tmp[6] = global_posx2_low;
			normal_tmp[7] = global_posx2_high;
			normal_tmp[8] = global_posy2_low;
			normal_tmp[9] = global_posy2_high;
			if (copy_to_user(buf, normal_tmp, 10)) {
				dev_err(&client->dev, "error : copy_to_user\n");
				return -EFAULT;
			}

		}
		interrupt_flag = 0;
		break;

	case PIXCIR_DEBUG_MODE:
		if (read_XN_YN_flag == 0) {
			unsigned char buf[2];
			memset(buf, 0, sizeof(buf));

			read_XN_YN_value(client);

			buf[0] = 194;
			buf[1] = 0;
			i2c_master_send(client, buf, 2);
		} else {
			memset(xy_raw1, 0, sizeof(xy_raw1));
			memset(xy_raw2, 0, sizeof(xy_raw2));
			read_XY_tables(client, xy_raw1, xy_raw2);
		}

		if (Tango_number == 1) {
			xy_raw1[MAXX * 2] = x_nb_electrodes;
			xy_raw1[MAXX * 2 + 1] = y_nb_electrodes;

			if (copy_to_user(buf, xy_raw1, MAXX * 2 + 2)) {
				dev_err(&client->dev, "error : copy_to_user\n");
				return -EFAULT;
			}

		} else if (Tango_number == 2) {
			xy_raw1[MAXX * 2] = x_nb_electrodes;
			xy_raw1[MAXX * 2 + 1] = y_nb_electrodes;
			xy_raw1[MAXX * 2 + 2] = x2_nb_electrodes;

			for (ret = 0; ret < (MAXX * 2 + 3); ret++)
				xy_raw12[ret] = xy_raw1[ret];

			for (ret = 0; ret < (MAXX * 2 - 1); ret++)
				xy_raw12[(MAXX * 2 + 3) + ret] = xy_raw2[ret];

			if (copy_to_user(buf, xy_raw12, MAXX * 4 + 3)) {
				dev_err(&client->dev, "error : copy_to_user\n");
				return -EFAULT;
			}
		}
		break;

	case RD_EEPROM: {
		unsigned char epmbytbuf[512];

		memset(epmbytbuf, 0, sizeof(epmbytbuf));
		i2c_master_recv(client, epmbytbuf, count);

		if (copy_to_user(buf, epmbytbuf, count)) {
			dev_err(&client->dev, "error : copy_to_user\n");
			return -EFAULT;
		}

		break;
	}

	case VERSION_FLAG: {
		unsigned char vaddbuf[1], verbuf[5];

		memset(vaddbuf, 0, sizeof(vaddbuf));
		memset(verbuf, 0, sizeof(verbuf));
		vaddbuf[0] = 48;
		i2c_master_send(client, vaddbuf, 1);
		i2c_master_recv(client, verbuf, 5);

		if (copy_to_user(buf, verbuf, 5)) {
			dev_err(&client->dev, "error : copy_to_user\n");
			return -EFAULT;
		}

		break;
	}

	default:
		break;
	}

	return ret;
}

static ssize_t pixcir_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct i2c_client *client;
	char *tmp, bootload_data[143], Rdbuf[1];
	int ret = 0, stu;
	int re_value = 0;

	client = file->private_data;

	switch (status_reg) {
	case CALIBRATION_FLAG:
		tmp = kmalloc(count, GFP_KERNEL);
		if (tmp == NULL)
			return -ENOMEM;

		if (copy_from_user(tmp, buf, count)) {
			dev_err(&client->dev, "error : copy_from_user\n");
			kfree(tmp);
			return -EFAULT;
		}
		i2c_master_send(client, tmp, count);
		mdelay(100);

		kfree(tmp);

		status_reg = 0;
		break;

	case BOOTLOADER_MODE:
		memset(bootload_data, 0, sizeof(bootload_data));
		memset(Rdbuf, 0, sizeof(Rdbuf));

		if (copy_from_user(bootload_data, buf, count)) {
			dev_err(&client->dev, "error : copy_from_user\n");
			return -EFAULT;
		}

		stu = bootload_data[0];

		i2c_master_send(client, bootload_data, count);

		if (stu != 0x01) {
			mdelay(1);
			while (get_attb_value(ATTB))
				;
			mdelay(1);

			i2c_master_recv(client, Rdbuf, 1);
			re_value = Rdbuf[0];
		} else {
			mdelay(100);
			status_reg = 0;
			enable_irq(global_irq);
		}

		if ((re_value & 0x80) && (stu != 0x01)) {
			printk(KERN_ERR "Failed : (re_value & 0x80) && (stu != 0x01) = 1\n");
			ret = 0;
		}
		break;

	case RD_EEPROM: {
		unsigned char epmdatabuf[2], wr2eep[4];

		memset(epmdatabuf, 0, sizeof(epmdatabuf));
		memset(wr2eep, 0, sizeof(wr2eep));

		if (copy_from_user(epmdatabuf, buf, count)) {
			dev_err(&client->dev, "error : copy_from_user\n");
			return -EFAULT;
		}

		wr2eep[0] = SPECOP;
		wr2eep[1] = 1;
		wr2eep[2] = epmdatabuf[0];
		wr2eep[3] = epmdatabuf[1];
		i2c_master_send(client, wr2eep, 4);

		break;
	}

	case WR_EEPROM: {
		unsigned char epmdatabuf[2];

		memset(epmdatabuf, 0, sizeof(epmdatabuf));

		if (copy_from_user(epmdatabuf, buf, count)) {
			dev_err(&client->dev, "error : copy_from_user\n");
			return -EFAULT;
		}

		if (2 == count) {
			op2eep[0] = SPECOP;
			op2eep[1] = 2;
			data2eep[0] = epmdatabuf[0];
			data2eep[1] = epmdatabuf[1];
		} else if (1 == count) {
			data2eep[2] = epmdatabuf[0];
			i2c_master_send(client, op2eep, 2);
			i2c_master_send(client, data2eep, 3);
			mdelay(4);
			i2c_master_recv(client, data2eep, 1);
			mdelay(100);
		}
		break;
	}

	default:
		break;
	}
	return ret;
}

static int pixcir_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client = file->private_data;

	i2c_put_adapter(client->adapter);
	kfree(client);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations pixcir_i2c_ts_fops = {
	.owner		= THIS_MODULE,
	.read		= pixcir_read,
	.write		= pixcir_write,
	.open		= pixcir_open,
	.unlocked_ioctl	= pixcir_ioctl,
	.release	= pixcir_release,
};

static const struct i2c_device_id pixcir_i2c_ts_id[] = {
	{"pixcir-ts", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, pixcir_i2c_ts_id);

static struct i2c_driver pixcir_i2c_ts_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "pixcir-i2c-ts",
	},
	.probe		= pixcir_i2c_ts_probe,
	.remove		= pixcir_i2c_ts_remove,
	.suspend	= pixcir_i2c_ts_suspend,
	.resume		= pixcir_i2c_ts_resume,
	.id_table	= pixcir_i2c_ts_id,
};

static int __init pixcir_i2c_ts_init(void)
{
	int ret;

	pixcir_wq = create_singlethread_workqueue("pixcir_wq");

	if (!pixcir_wq)
		return -ENOMEM;

	ret = register_chrdev(I2C_MAJOR, "pixcir_i2c_ts", &pixcir_i2c_ts_fops);
	if (ret) {
		printk(KERN_ERR "%s:register chrdev failed\n", __FILE__);
		return ret;
	}

	i2c_dev_class = class_create(THIS_MODULE, "pixcir_i2c_dev");

	if (IS_ERR(i2c_dev_class)) {
		ret = PTR_ERR(i2c_dev_class);
		class_destroy(i2c_dev_class);
	}

	return i2c_add_driver(&pixcir_i2c_ts_driver);
}

static void __exit pixcir_i2c_ts_exit(void)
{
	i2c_del_driver(&pixcir_i2c_ts_driver);
	class_destroy(i2c_dev_class);
	unregister_chrdev(I2C_MAJOR, "pixcir_i2c_ts");
	if (pixcir_wq)
		destroy_workqueue(pixcir_wq);
}

module_init(pixcir_i2c_ts_init);
module_exit(pixcir_i2c_ts_exit);

MODULE_AUTHOR("Dongsu Ha <dsfine.ha@samsung.com>, "
	      "Bee<http://www.pixcir.com.cn>, "
	      "Samsung Electronics <http://www.samsung.com>");

MODULE_DESCRIPTION("Pixcir Touchscreen driver");
MODULE_LICENSE("GPL");
