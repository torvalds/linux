/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

#include <linux/cap_prox.h>

#define CAP_PROX_NAME "cap-prox"
#define CP_STATUS_NUM_KEYS_ENABLED              0x20

#define CP_STATUS_KEY3_IN_DETECT                0x24
#define CP_STATUS_KEY1_IN_DETECT                0x21
#define CP_STATUS_KEY1_KEY3_IN_DETECT           0x25
#define CP_STATUS_KEY1_KEY3_EN_FORCE_DETECT     0x75

struct cap_prox_msg {
	uint8_t         status;
	uint16_t        ref_key1;
	uint16_t        ref_key3;
	uint8_t         chip_id;
	uint8_t         sw_ver;
	uint8_t         reserved8;
	uint16_t        signal1;
	uint16_t        signal2;
	uint16_t        signal3;
	uint16_t        signal4;
	uint16_t        save_ref1;
	uint16_t        save_ref2;
	uint16_t        save_ref3;
	uint16_t        save_ref4;
} __attribute__ ((packed));

struct cap_prox_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct delayed_work input_work;
	struct work_struct irq_work;
	struct cap_prox_platform_data *pdata;

	atomic_t enabled;
	spinlock_t irq_lock;
	int irq_state;
};

static uint32_t cp_dbg;
module_param_named(cp_debug, cp_dbg, uint, 0664);

struct cap_prox_data *cap_prox_misc_data;
struct workqueue_struct *cp_irq_wq;

static void cap_prox_irq_enable(struct cap_prox_data *cp, int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&cp->irq_lock, flags);
	if (cp->irq_state != enable) {
		if (enable)
			enable_irq(cp->client->irq);
		else
			disable_irq_nosync(cp->client->irq);
		cp->irq_state = enable;
	}
	spin_unlock_irqrestore(&cp->irq_lock, flags);
}

static irqreturn_t cap_prox_irq_handler(int irq, void *dev_id)
{
	struct cap_prox_data *cp = dev_id;

	cap_prox_irq_enable(cp, 0);
	queue_work(cp_irq_wq, &cp->irq_work);

	return IRQ_HANDLED;
}

static int cap_prox_write(struct cap_prox_data *cp, void *buf, int buf_sz)
{
	int retries = 10;
	int ret;

	do {
		ret = i2c_master_send(cp->client, (char *)buf, buf_sz);
	} while ((ret < buf_sz) && (--retries > 0));

	if (ret < 0)
		pr_info("%s: Error while trying to write %d bytes\n", __func__,
			buf_sz);
	else if (ret != buf_sz) {
		pr_info("%s: Write %d bytes, expected %d\n", __func__,
			ret, buf_sz);
		ret = -EIO;
	}
	return ret;
}

static int cap_prox_read(struct cap_prox_data *cp, void *buf, int buf_sz)
{
	int retries = 10;
	int ret;

	do {
		memset(buf, 0, buf_sz);
		ret = i2c_master_recv(cp->client, (char *)buf, buf_sz);
	} while ((ret < 0) && (--retries > 0));

	if (ret < 0)
		pr_info("%s: Error while trying to read %d bytes\n", __func__,
			buf_sz);
	else if (ret != buf_sz) {
		pr_info("%s: Read %d bytes, expected %d\n", __func__,
			ret, buf_sz);
		ret = -EIO;
	}

	return ret >= 0 ? 0 : ret;
}

static void cap_prox_calibrate(struct cap_prox_data *cp) {
	cap_prox_write(cp, &cp->pdata->plat_cap_prox_cfg.calibrate,1);
	if (cp_dbg)
		pr_info("%s: Send Calibrate 0x%x\n", __func__,
			 cp->pdata->plat_cap_prox_cfg.calibrate);
}

static int cap_prox_read_data(struct cap_prox_data *cp)
{
	struct cap_prox_msg *msg;
	uint8_t status;
	int ret = -1;
	int key1_ref_drift = 0;
	int key3_ref_drift = 0;
	int ref_drift_diff = 0;
	int key1_save_drift = 0;
	int key3_save_drift = 0;
	int save_drift_diff = 0;
	int key1_key2_signal_drift = 0;
	int key3_key4_signal_drift = 0;
	uint8_t mesg_buf[sizeof(struct cap_prox_msg)];


	ret = cap_prox_read(cp, mesg_buf, sizeof(struct cap_prox_msg));
	if (ret) {
		status = CP_STATUS_NUM_KEYS_ENABLED;
		pr_info("%s: read failed \n",  __func__);
		goto read_fail_ret;
	}
	msg = (struct cap_prox_msg *)mesg_buf;

	if (cp_dbg & 0x02) {
		pr_info("%s: Cap-Prox data \n", __func__);
		pr_info(" msg->status 0x%2x \n", msg->status);
		pr_info(" msg->ref_key1 %d \n", msg->ref_key1);
		pr_info(" msg->ref_key3 %d \n", msg->ref_key3);
		pr_info(" msg->chip_id 0x%2x \n", msg->chip_id);
		pr_info(" msg->sw_ver 0x%2x \n", msg->sw_ver);
		pr_info(" msg->signal1 %d \n", msg->signal1);
		pr_info(" msg->signal2 %d \n", msg->signal2);
		pr_info(" msg->signal3 %d \n", msg->signal3);
		pr_info(" msg->signal4 %d \n", msg->signal4);
		pr_info(" msg->save_ref1 %d \n", msg->save_ref1);
		pr_info(" msg->save_ref2 %d \n", msg->save_ref2);
		pr_info(" msg->save_ref3 %d \n", msg->save_ref3);
		pr_info(" msg->save_ref4 %d \n\n", msg->save_ref4);
	}

	key1_ref_drift = abs(msg->ref_key1 - msg->signal1);
	key3_ref_drift = abs(msg->ref_key3 - msg->signal3);
	ref_drift_diff = abs(key3_ref_drift - key1_ref_drift);
	key1_save_drift = abs(msg->save_ref1 - msg->signal1);
	key3_save_drift = abs(msg->save_ref3 - msg->signal3);
	save_drift_diff = abs(key3_save_drift - key1_save_drift);
	key1_key2_signal_drift = abs(msg->signal1 - msg->signal2);
	key3_key4_signal_drift = abs(msg->signal3 - msg->signal4);

	if (cp_dbg) {
		pr_info("%s: Key1 ref drift %d \n", __func__, key1_ref_drift);
		pr_info("%s: key3 ref drift %d \n", __func__, key3_ref_drift);
		pr_info("%s: Key1 Key3 ref drift diff %d \n\n",
			 __func__, ref_drift_diff);
		pr_info("%s: Key1 save drift %d \n", __func__, key1_save_drift);
		pr_info("%s: key3 save drift %d \n", __func__, key3_save_drift);
		pr_info("%s: Key1 Key3 saved drift diff %d \n\n",
			 __func__, save_drift_diff);
		pr_info("%s: Key1 Key2 signal/sheild drift diff %d \n\n",
			 __func__, key1_key2_signal_drift);
		pr_info("%s: Key3 Key4 signal/sheild drift diff %d \n\n",
			 __func__, key3_key4_signal_drift);
	}

	switch (msg->status) {

	case CP_STATUS_KEY1_IN_DETECT:
		if (key1_ref_drift < cp->pdata->key1_ref_drift_thres_l)
			cap_prox_calibrate(cp);
		break;
	case CP_STATUS_KEY3_IN_DETECT:
		if (key3_ref_drift < cp->pdata->key3_ref_drift_thres_l)
			cap_prox_calibrate(cp);
		break;
	case CP_STATUS_KEY1_KEY3_EN_FORCE_DETECT:
		if ((save_drift_diff < cp->pdata->save_drift_diff_thres) &&
			 (key1_save_drift < cp->pdata->key1_save_drift_thres) &&
			 (key3_save_drift < cp->pdata->key3_save_drift_thres)) {

			/* Key1 sensor has failed, keep in force detect */
			if ((key1_key2_signal_drift >
				 cp->pdata->key1_failsafe_thres) &&
				 (msg->signal2 > cp->pdata->key2_signal_thres))
				break;

			/* Key3 sensor has failed, keep in force detect */
			if ((key3_key4_signal_drift >
				 cp->pdata->key3_failsafe_thres) &&
				 (msg->signal4 > cp->pdata->key4_signal_thres))
				break;

			status = msg->status & 0xF0;
			cap_prox_write(cp,&status,1);
		}
		break;
	case CP_STATUS_KEY1_KEY3_IN_DETECT:
		if ((key3_ref_drift < cp->pdata->key1_ref_drift_thres_h) &&
			 (key1_ref_drift < cp->pdata->key3_ref_drift_thres_h) &&
			 (ref_drift_diff < cp->pdata->ref_drift_diff_thres))
			cap_prox_calibrate(cp);
		break;
	default:
		if (cp_dbg) {
			pr_info("%s: Cap-prox message 0x%x\n", __func__,
				msg->status);
		}
		break;
	}
	status = msg->status;

read_fail_ret:
	return status;
}

static int cap_prox_hw_init(struct cap_prox_data *cp)
{
	pr_info("%s: HW init\n", __func__);
	cap_prox_calibrate(cp);
	cap_prox_write(cp,&cp->pdata->plat_cap_prox_cfg.thres_key1,1);
	cap_prox_write(cp,&cp->pdata->plat_cap_prox_cfg.thres_key2,1);
	msleep(200);

	return 0;
}

static void cap_prox_irq_work_func(struct work_struct *work)
{
	int ret;
	u8 buf[2];
	struct cap_prox_data *cp =
		container_of(work, struct cap_prox_data, irq_work);

	cancel_delayed_work_sync(&cp->input_work);
	ret = cap_prox_read(cp, buf, 2);

	if (cp_dbg)
		pr_info("%s: Cap-Prox Status: [0x%x][0x%x] \n",
			 __func__, buf[0], buf[1]);

	if (buf[0] != CP_STATUS_NUM_KEYS_ENABLED)
		schedule_delayed_work(&cp->input_work,
			      msecs_to_jiffies(cp->pdata->poll_interval));

	cap_prox_irq_enable(cp, 1);

}

static struct miscdevice cap_prox_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = CAP_PROX_NAME,
};

static void cap_prox_input_work_func(struct work_struct *work)
{
	int ret = 0;
	struct cap_prox_data *cp = container_of((struct delayed_work *)work,
					struct cap_prox_data, input_work);
	ret = cap_prox_read_data(cp);

	if (ret != CP_STATUS_NUM_KEYS_ENABLED)
		schedule_delayed_work(&cp->input_work,
			      msecs_to_jiffies(cp->pdata->poll_interval));
}

static int cap_prox_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct cap_prox_platform_data *pdata = client->dev.platform_data;
	struct cap_prox_data *cp;
	struct cap_prox_msg *msg;
	uint8_t mesg_buf[sizeof(struct cap_prox_msg)];
	int err = -1;

	if (pdata == NULL) {
		pr_err("%s: platform data required\n", __func__);
		return -ENODEV;
	} else if (!client->irq) {
		pr_err("%s: polling mode currently not supported\n", __func__);
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: need I2C_FUNC_I2C\n", __func__);
		return -ENODEV;
	}

	cp = kzalloc(sizeof(struct cap_prox_data), GFP_KERNEL);
	if (cp == NULL) {
		err = -ENOMEM;
		goto err_out1;
	}

	cp->pdata = pdata;
	cp->client = client;
	i2c_set_clientdata(client, cp);
	spin_lock_init(&cp->irq_lock);
	cp->irq_state = 1;

	INIT_WORK(&cp->irq_work, cap_prox_irq_work_func);
	INIT_DELAYED_WORK(&cp->input_work, cap_prox_input_work_func);

	err = cap_prox_hw_init(cp);
	if (err < 0)
		goto err_out2;

	err = misc_register(&cap_prox_misc_device);
	if (err < 0) {
		dev_err(&client->dev, "misc register failed: %d\n", err);
		goto err_out2;
	}

	err = cap_prox_read(cp, mesg_buf, sizeof(struct cap_prox_msg));
	if (err)
		goto err_out3;
	msg = (struct cap_prox_msg *)mesg_buf;
	pr_info("%s: msg->status 0x%2x \n", __func__, msg->status);

	/* Could be booted with body proximity, so force detect */
	cap_prox_write(cp,&cp->pdata->plat_cap_prox_cfg.force_detect,1);

	err = request_irq(cp->client->irq, cap_prox_irq_handler,
		 IRQF_TRIGGER_FALLING, "cap_prox_irq", cp);
	if (err < 0) {
		dev_err(&client->dev, "request irq failed: %d\n", err);
		goto err_out3;
	}

	pr_info("%s: Request IRQ = %d\n", __func__, cp->client->irq);

	cap_prox_write(cp, &cp->pdata->plat_cap_prox_cfg.address_ptr, 1);

	dev_info(&client->dev, "cap-prox probed\n");

	return 0;

err_out3:
	misc_deregister(&cap_prox_misc_device);
err_out2:
	kfree(cp);
err_out1:
	return err;
}

static int __devexit cap_prox_remove(struct i2c_client *client)
{
	struct cap_prox_data *cp = i2c_get_clientdata(client);

	free_irq(cp->client->irq, cp);
	misc_deregister(&cap_prox_misc_device);
	i2c_set_clientdata(client, NULL);
	kfree(cp);

	return 0;
}

static int cap_prox_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct cap_prox_data *cp = i2c_get_clientdata(client);

	if (cp_dbg & 0x4)
		pr_info("%s: Suspending\n", __func__);

	cap_prox_irq_enable(cp, 0);
	cancel_delayed_work_sync(&cp->input_work);

	return 0;
}

static int cap_prox_resume(struct i2c_client *client)
{
	struct cap_prox_data *cp = i2c_get_clientdata(client);

	if (cp_dbg & 0x4)
		pr_info("%s: Resuming\n", __func__);

	schedule_delayed_work(&cp->input_work,
		 msecs_to_jiffies(cp->pdata->min_poll_interval));
	cap_prox_irq_enable(cp, 1);

	return 0;
}

static const struct i2c_device_id cap_prox_id[] = {
	{ CAP_PROX_NAME, 0 },
	{ }
};

static struct i2c_driver cap_prox_driver = {
	.probe		= cap_prox_probe,
	.remove		= cap_prox_remove,
	.suspend	= cap_prox_suspend,
	.resume		= cap_prox_resume,
	.id_table	= cap_prox_id,
	.driver = {
		.name	= CAP_PROX_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __devinit cap_prox_init(void)
{
	cp_irq_wq = create_singlethread_workqueue("cp_irq_wq");
	if (cp_irq_wq == NULL) {
		pr_err("%s: No memory for cp_irq_wq\n", __func__);
		return -ENOMEM;
	}
	return i2c_add_driver(&cap_prox_driver);
}

static void __exit cap_prox_exit(void)
{
	i2c_del_driver(&cap_prox_driver);
	if (cp_irq_wq)
		destroy_workqueue(cp_irq_wq);
}

module_init(cap_prox_init);
module_exit(cap_prox_exit);

MODULE_AUTHOR("Motorola");
MODULE_DESCRIPTION("Capacitive Proximity Sensor Driver");
MODULE_LICENSE("GPL");
