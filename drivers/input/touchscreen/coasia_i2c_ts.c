/*
 *  Coasia Microelectronics Corp.
 *
 * pixcir_i2c_ts.c V1.0  support multi touch
 * pixcir_i2c_ts.c V2.0  add tuning function including follows function:
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/pixcir_ts.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/irq.h>

#include <plat/gpio-cfg.h>

#include <mach/gpio.h>
#include <mach/irqs.h>


/* Touch Finger Numbers */
#define ONE_FINGER_TOUCH	0x1
#define TWO_FINGER_TOUCH	0x2

#define I2C_MAJOR		125
#define I2C_MINORS		256

struct finger_info {
	int id;
	int status;
	int pos_x;
	int pos_y;
};

static struct i2c_driver pixcir_i2c_ts_driver;
static struct class *i2c_dev_class;

static struct workqueue_struct *pixcir_wq;

struct pixcir_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work work;
	int irq;
};

static void pixcir_ts_poscheck(struct work_struct *work)
{
	struct pixcir_i2c_ts_data *tsdata =
		container_of(work, struct pixcir_i2c_ts_data, work.work);

	unsigned char touch_count;
	static unsigned char old_touch_count = 0;
	struct finger_info finger[2];
	static struct finger_info old_finger[2];
	unsigned char Rdbuf[17], Wrbuf[1];
	int ret;
	int i;
	int z = 50;
	int w = 15;
	static int ignore_count=0;

	Wrbuf[0] = 0;
	ret = i2c_master_send(tsdata->client, Wrbuf, 1);
	if (ret != 1) {
		dev_err(&tsdata->client->dev,
			"Unable to write to i2c, ret =%d\n", ret);
		goto out;
	}

	/* Read data from 0 to 16 */
	ret = i2c_master_recv(tsdata->client, Rdbuf, sizeof(Rdbuf));
	if (ret != sizeof(Rdbuf)) {
		dev_err(&tsdata->client->dev,
			"Unable to read i2c page, ret = %d\n", ret);
		goto out;
	}

	/* Number of fingers touching (up/down) */
	touch_count = Rdbuf[0];
	finger[0].id = ((Rdbuf[1] & 0xF0) >> 4);
	finger[0].status = (Rdbuf[1] & 0x0F);
	finger[0].pos_x = ((Rdbuf[3] << 8) | Rdbuf[2]);
	finger[0].pos_y = ((Rdbuf[5] << 8) | Rdbuf[4]);
	finger[1].id = ((Rdbuf[10] & 0xF0) >> 4);
	finger[1].status = (Rdbuf[10] & 0x0F);
	finger[1].pos_x = ((Rdbuf[12] << 8) | Rdbuf[11]);
	finger[1].pos_y = ((Rdbuf[14] << 8) | Rdbuf[13]);

	if (touch_count) {
		if (old_touch_count == touch_count) {
			for (i = 0; i < touch_count; i++) {
				if (finger[i].id != old_finger[i].id ||
				    finger[i].status != old_finger[i].status ||
				    finger[i].pos_x != old_finger[i].pos_x ||
				    finger[i].pos_y != old_finger[i].pos_y)
					goto report;
			}
			ignore_count++;
			goto out;
		}
report:
		ignore_count = 0;

		memcpy(old_finger, finger, sizeof(finger));
		old_touch_count = touch_count;

		input_report_abs(tsdata->input, ABS_X, finger[0].pos_x);
		input_report_abs(tsdata->input, ABS_Y, finger[0].pos_y);
		input_report_key(tsdata->input, BTN_TOUCH, 1);
		input_report_key(tsdata->input, ABS_PRESSURE, 1);
	} else {
		input_report_key(tsdata->input, BTN_TOUCH, 0);
		input_report_abs(tsdata->input, ABS_PRESSURE, 0);
	}

	switch (touch_count) {
	case TWO_FINGER_TOUCH:
		if ((finger[0].status == 0x1) &&
		    (finger[1].status == 0x1)) {
			input_mt_sync(tsdata->input);
			pr_debug("NO fingers touch now!!!!!\n");
			goto fun_end;
		} else {
			input_report_abs(tsdata->input,
					ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(tsdata->input,
					ABS_MT_WIDTH_MAJOR, w);
			input_report_abs(tsdata->input,
					ABS_MT_POSITION_X, finger[1].pos_x);
			input_report_abs(tsdata->input,
					ABS_MT_POSITION_Y, finger[1].pos_y);
			input_mt_sync(tsdata->input);
		}
		break;
	case ONE_FINGER_TOUCH:
		if (finger[0].status == 0x1) {
			input_mt_sync(tsdata->input);
			pr_debug("NO fingers touch now!!!!!\n");
			goto fun_end;
		} else {
			input_report_abs(tsdata->input,
					ABS_MT_TOUCH_MAJOR, z);
			input_report_abs(tsdata->input,
					ABS_MT_WIDTH_MAJOR, w);
			input_report_abs(tsdata->input,
					ABS_MT_POSITION_X, finger[0].pos_x);
			input_report_abs(tsdata->input,
					ABS_MT_POSITION_Y, finger[0].pos_y);
			input_mt_sync(tsdata->input);
		}
		break;
	default:
		pr_err("touch_count > 2, NOT report\n");
		break;
	}
	/* sync after groups of events */
fun_end:
	input_sync(tsdata->input);
out:
	enable_irq(tsdata->irq);
}

static irqreturn_t pixcir_ts_isr(int irq, void *dev_id)
{
	struct pixcir_i2c_ts_data *tsdata = dev_id;

	disable_irq_nosync(irq);
	queue_work(pixcir_wq, &tsdata->work.work);

	return IRQ_HANDLED;
}

static int pixcir_i2c_ts_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	const struct pixcir_ts_platform_data *pdata = client->dev.platform_data;
	struct pixcir_i2c_ts_data *tsdata;
	struct input_dev *input;
	struct device *dev;
	int error;

	if (!pdata) {
		dev_err(&client->dev, "platform data not defined\n");
		return -EINVAL;
	}

	tsdata = kzalloc(sizeof(*tsdata), GFP_KERNEL);
	if (!tsdata) {
		dev_err(&client->dev, "failed to allocate driver data!\n");
		error = -ENOMEM;
		return error;
	}

	dev_set_drvdata(&client->dev, tsdata);

	input = input_allocate_device();
	if (!input) {
		dev_err(&client->dev, "failed to allocate input device!\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	set_bit(EV_SYN, input->evbit);
	set_bit(EV_KEY, input->evbit);		/* Support Key func */
	set_bit(EV_ABS, input->evbit);		/* Support Touch */
	set_bit(BTN_TOUCH, input->keybit);	/* Single Touch */
	input_set_abs_params(input, ABS_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, pdata->y_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, pdata->y_max, 0, 0);

	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 25, 0, 0);
	input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, 4, 0, 0);

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;

	input_set_drvdata(input, tsdata);

	tsdata->client = client;
	tsdata->input = input;

	INIT_WORK(&tsdata->work.work, pixcir_ts_poscheck);

	tsdata->irq = client->irq;

	error = request_irq(tsdata->irq, pixcir_ts_isr,
		IRQF_TRIGGER_FALLING, client->name, tsdata);
	if(error){
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		goto err_free_dev;
	}

	error = input_register_device(input);
	if (error)
		goto err_free_irq;

	device_init_wakeup(&client->dev, 1);

	dev = device_create(i2c_dev_class, &client->adapter->dev,
		MKDEV(I2C_MAJOR, client->adapter->nr), NULL,
		"pixcir_i2c_ts%d", client->adapter->nr);

	if (IS_ERR(dev)) {
		error = PTR_ERR(dev);
		goto err_unregister_dev;
	}

	return 0;

err_unregister_dev:
	input_unregister_device(input);
err_free_irq:
	free_irq(client->irq, tsdata);
err_free_dev:
	input_free_device(input);
err_free_mem:
	kfree(tsdata);
	return error;
}

static int pixcir_i2c_ts_remove(struct i2c_client *client)
{
	struct pixcir_i2c_ts_data *tsdata = dev_get_drvdata(&client->dev);

	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, client->adapter->nr));
	input_unregister_device(tsdata->input);
	free_irq(tsdata->irq, tsdata);
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

static const struct i2c_device_id pixcir_i2c_ts_id[] = {
	{ "pixcir_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pixcir_i2c_ts_id);

static struct i2c_driver pixcir_i2c_ts_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "pixcir_ts",
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

MODULE_DESCRIPTION("Pixcir I2C Touchscreen Driver with tune fuction");
MODULE_LICENSE("GPL");
