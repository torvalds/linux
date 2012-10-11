#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/input/ili210x.h>

#define MAX_TOUCHES		2
#define DEFAULT_POLL_PERIOD	20

/* Touchscreen commands */
#define REG_TOUCHDATA		0x10
#define REG_PANEL_INFO		0x20
#define REG_FIRMWARE_VERSION	0x40
#define REG_CALIBRATE		0xcc

struct finger {
	u8 x_low;
	u8 x_high;
	u8 y_low;
	u8 y_high;
} __packed;

struct touchdata {
	u8 status;
	struct finger finger[MAX_TOUCHES];
} __packed;

struct panel_info {
	struct finger finger_max;
	u8 xchannel_num;
	u8 ychannel_num;
} __packed;

struct firmware_version {
	u8 id;
	u8 major;
	u8 minor;
} __packed;

struct ili210x {
	struct i2c_client *client;
	struct input_dev *input;
	bool (*get_pendown_state)(void);
	unsigned int poll_period;
	struct delayed_work dwork;
};

static int ili210x_read_reg(struct i2c_client *client, u8 reg, void *buf,
			    size_t len)
{
	struct i2c_msg msg[2] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &reg,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
		}
	};

	if (i2c_transfer(client->adapter, msg, 2) != 2) {
		dev_err(&client->dev, "i2c transfer failed\n");
		return -EIO;
	}

	return 0;
}

static void ili210x_report_events(struct input_dev *input,
				  const struct touchdata *touchdata)
{
	int i;
	bool touch;
	unsigned int x, y;
	const struct finger *finger;

	for (i = 0; i < MAX_TOUCHES; i++) {
		input_mt_slot(input, i);

		finger = &touchdata->finger[i];

		touch = touchdata->status & (1 << i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);
		if (touch) {
			x = finger->x_low | (finger->x_high << 8);
			y = finger->y_low | (finger->y_high << 8);

			input_report_abs(input, ABS_MT_POSITION_X, x);
			input_report_abs(input, ABS_MT_POSITION_Y, y);
		}
	}

	input_mt_report_pointer_emulation(input, false);
	input_sync(input);
}

static bool get_pendown_state(const struct ili210x *priv)
{
	bool state = false;

	if (priv->get_pendown_state)
		state = priv->get_pendown_state();

	return state;
}

static void ili210x_work(struct work_struct *work)
{
	struct ili210x *priv = container_of(work, struct ili210x,
					    dwork.work);
	struct i2c_client *client = priv->client;
	struct touchdata touchdata;
	int error;

	error = ili210x_read_reg(client, REG_TOUCHDATA,
				 &touchdata, sizeof(touchdata));
	if (error) {
		dev_err(&client->dev,
			"Unable to get touchdata, err = %d\n", error);
		return;
	}

	ili210x_report_events(priv->input, &touchdata);

	if ((touchdata.status & 0xf3) || get_pendown_state(priv))
		schedule_delayed_work(&priv->dwork,
				      msecs_to_jiffies(priv->poll_period));
}

static irqreturn_t ili210x_irq(int irq, void *irq_data)
{
	struct ili210x *priv = irq_data;

	schedule_delayed_work(&priv->dwork, 0);

	return IRQ_HANDLED;
}

static ssize_t ili210x_calibrate(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	unsigned long calibrate;
	int rc;
	u8 cmd = REG_CALIBRATE;

	if (kstrtoul(buf, 10, &calibrate))
		return -EINVAL;

	if (calibrate > 1)
		return -EINVAL;

	if (calibrate) {
		rc = i2c_master_send(priv->client, &cmd, sizeof(cmd));
		if (rc != sizeof(cmd))
			return -EIO;
	}

	return count;
}
static DEVICE_ATTR(calibrate, 0644, NULL, ili210x_calibrate);

static struct attribute *ili210x_attributes[] = {
	&dev_attr_calibrate.attr,
	NULL,
};

static const struct attribute_group ili210x_attr_group = {
	.attrs = ili210x_attributes,
};

static int __devinit ili210x_i2c_probe(struct i2c_client *client,
				       const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	const struct ili210x_platform_data *pdata = dev->platform_data;
	struct ili210x *priv;
	struct input_dev *input;
	struct panel_info panel;
	struct firmware_version firmware;
	int xmax, ymax;
	int error;

	dev_dbg(dev, "Probing for ILI210X I2C Touschreen driver");

	if (!pdata) {
		dev_err(dev, "No platform data!\n");
		return -EINVAL;
	}

	if (client->irq <= 0) {
		dev_err(dev, "No IRQ!\n");
		return -EINVAL;
	}

	/* Get firmware version */
	error = ili210x_read_reg(client, REG_FIRMWARE_VERSION,
				 &firmware, sizeof(firmware));
	if (error) {
		dev_err(dev, "Failed to get firmware version, err: %d\n",
			error);
		return error;
	}

	/* get panel info */
	error = ili210x_read_reg(client, REG_PANEL_INFO, &panel, sizeof(panel));
	if (error) {
		dev_err(dev, "Failed to get panel informations, err: %d\n",
			error);
		return error;
	}

	xmax = panel.finger_max.x_low | (panel.finger_max.x_high << 8);
	ymax = panel.finger_max.y_low | (panel.finger_max.y_high << 8);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	input = input_allocate_device();
	if (!priv || !input) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	priv->client = client;
	priv->input = input;
	priv->get_pendown_state = pdata->get_pendown_state;
	priv->poll_period = pdata->poll_period ? : DEFAULT_POLL_PERIOD;
	INIT_DELAYED_WORK(&priv->dwork, ili210x_work);

	/* Setup input device */
	input->name = "ILI210x Touchscreen";
	input->id.bustype = BUS_I2C;
	input->dev.parent = dev;

	__set_bit(EV_SYN, input->evbit);
	__set_bit(EV_KEY, input->evbit);
	__set_bit(EV_ABS, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);

	/* Single touch */
	input_set_abs_params(input, ABS_X, 0, xmax, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, ymax, 0, 0);

	/* Multi touch */
	input_mt_init_slots(input, MAX_TOUCHES, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, xmax, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ymax, 0, 0);

	input_set_drvdata(input, priv);
	i2c_set_clientdata(client, priv);

	error = request_irq(client->irq, ili210x_irq, pdata->irq_flags,
			    client->name, priv);
	if (error) {
		dev_err(dev, "Unable to request touchscreen IRQ, err: %d\n",
			error);
		goto err_free_mem;
	}

	error = sysfs_create_group(&dev->kobj, &ili210x_attr_group);
	if (error) {
		dev_err(dev, "Unable to create sysfs attributes, err: %d\n",
			error);
		goto err_free_irq;
	}

	error = input_register_device(priv->input);
	if (error) {
		dev_err(dev, "Cannot regiser input device, err: %d\n", error);
		goto err_remove_sysfs;
	}

	device_init_wakeup(&client->dev, 1);

	dev_dbg(dev,
		"ILI210x initialized (IRQ: %d), firmware version %d.%d.%d",
		client->irq, firmware.id, firmware.major, firmware.minor);

	return 0;

err_remove_sysfs:
	sysfs_remove_group(&dev->kobj, &ili210x_attr_group);
err_free_irq:
	free_irq(client->irq, priv);
err_free_mem:
	input_free_device(input);
	kfree(priv);
	return error;
}

static int __devexit ili210x_i2c_remove(struct i2c_client *client)
{
	struct ili210x *priv = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &ili210x_attr_group);
	free_irq(priv->client->irq, priv);
	cancel_delayed_work_sync(&priv->dwork);
	input_unregister_device(priv->input);
	kfree(priv);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ili210x_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int ili210x_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ili210x_i2c_pm,
			 ili210x_i2c_suspend, ili210x_i2c_resume);

static const struct i2c_device_id ili210x_i2c_id[] = {
	{ "ili210x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ili210x_i2c_id);

static struct i2c_driver ili210x_ts_driver = {
	.driver = {
		.name = "ili210x_i2c",
		.owner = THIS_MODULE,
		.pm = &ili210x_i2c_pm,
	},
	.id_table = ili210x_i2c_id,
	.probe = ili210x_i2c_probe,
	.remove = __devexit_p(ili210x_i2c_remove),
};

module_i2c_driver(ili210x_ts_driver);

MODULE_AUTHOR("Olivier Sobrie <olivier@sobrie.be>");
MODULE_DESCRIPTION("ILI210X I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
