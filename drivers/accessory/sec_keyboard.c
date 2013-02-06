
#include "sec_keyboard.h"

static void sec_keyboard_tx(struct sec_keyboard_drvdata *data, u8 cmd)
{
	if (data->pre_connected && data->tx_ready)
		serio_write(data->serio, cmd);
}

static void sec_keyboard_power(struct work_struct *work)
{
	struct sec_keyboard_drvdata *data = container_of(work,
			struct sec_keyboard_drvdata, power_dwork.work);

	if (UNKOWN_KEYLAYOUT == data->kl) {
		data->acc_power(1, false);
		data->pre_connected = false;

		if (data->check_uart_path)
			data->check_uart_path(false);
	}
}

static void forced_wakeup(struct sec_keyboard_drvdata *data)
{
	input_report_key(data->input_dev,
		KEY_WAKEUP, 1);
	input_report_key(data->input_dev,
		KEY_WAKEUP, 0);
	input_sync(data->input_dev);
}

static void sec_keyboard_remapkey(struct work_struct *work)
{
	unsigned int keycode = 0;
	struct sec_keyboard_drvdata *data = container_of(work,
			struct sec_keyboard_drvdata, remap_dwork.work);

	if (data->pressed[0x45] || data->pressed[0x48]) {
		keycode = data->keycode[data->remap_key];
		input_report_key(data->input_dev, keycode, 1);
		input_sync(data->input_dev);
	}
	data->remap_key = 0;
}

static void sec_keyboard_ack(struct work_struct *work)
{
	unsigned int ackcode = 0;
	char *envp[3];
	struct sec_keyboard_drvdata *data = container_of(work,
			struct sec_keyboard_drvdata, ack_dwork.work);

	if (data->ack_code) {
		ackcode = data->ack_code;
		sec_keyboard_tx(data, ackcode);
	}

	if (ackcode == 0x68)
		data->univ_kbd_dock = true;

	printk(KERN_DEBUG "[Keyboard] Ack code to KBD 0x%x\n", ackcode);

	data->noti_univ_kbd_dock(data->ack_code);
}

static void release_all_keys(struct sec_keyboard_drvdata *data)
{
	int i;
	printk(KERN_DEBUG "[Keyboard] Release the pressed keys.\n");
	for (i = 0; i < KEYBOARD_MAX; i++) {
		if (data->pressed[i]) {
			input_report_key(data->input_dev, data->keycode[i], 0);
			data->pressed[i] = false;
		}
		input_sync(data->input_dev);
	}
}

static void sec_keyboard_process_data(
	struct sec_keyboard_drvdata *data, u8 scan_code)
{
	bool press;
	unsigned int keycode;

	/* keyboard driver need the contry code*/
	if (data->kl == UNKOWN_KEYLAYOUT) {
		switch (scan_code) {
		case US_KEYBOARD:
			data->kl = US_KEYLAYOUT;
			data->keycode[49] = KEY_BACKSLASH;
			/* for the wakeup state*/
			data->pre_kl = data->kl;
			printk(KERN_DEBUG "[Keyboard] US keyboard is attacted.\n");
			break;

		case UK_KEYBOARD:
			data->kl = UK_KEYLAYOUT;
			data->keycode[49] = KEY_NUMERIC_POUND;
			/* for the wakeup state*/
			data->pre_kl = data->kl;
			printk(KERN_DEBUG "[Keyboard] UK keyboard is attacted.\n");
			break;

		default:
			printk(KERN_DEBUG "[Keyboard] Unkown layout : %x\n",
				scan_code);
			break;
		}
	} else {
		switch (scan_code) {
		case 0x0:
			release_all_keys(data);
			break;

		case 0xca: /* Caps lock on */
		case 0xcb: /* Caps lock off */
		case 0xeb: /* US keyboard */
		case 0xec: /* UK keyboard */
			break; /* Ignore */

		case 0x45:
		case 0x48:
			data->remap_key = scan_code;
			data->pressed[scan_code] = true;
			schedule_delayed_work(&data->remap_dwork, HZ/3);
			break;
		case 0x68:
		case 0x69:
		case 0x6a:
		case 0x6b:
		case 0x6c:
			data->ack_code = scan_code;
			schedule_delayed_work(&data->ack_dwork, HZ/200);
			printk(KERN_DEBUG "[Keyboard] scan_code %d Received.\n",
				scan_code);
			break;
		case 0xc5:
		case 0xc8:
			keycode = (scan_code & 0x7f);
			data->pressed[keycode] = false;
			if (0 == data->remap_key) {
				input_report_key(data->input_dev,
					data->keycode[keycode], 0);
				input_sync(data->input_dev);
			} else {
				cancel_delayed_work_sync(&data->remap_dwork);
				if (0x48 == keycode)
					keycode = KEY_NEXTSONG;
				else
					keycode = KEY_PREVIOUSSONG;

				input_report_key(data->input_dev,
					keycode, 1);
				input_report_key(data->input_dev,
					keycode, 0);
				input_sync(data->input_dev);
			}
			break;

		default:
			keycode = (scan_code & 0x7f);
			press = ((scan_code & 0x80) != 0x80);

			if (keycode >= KEYBOARD_MIN
				|| keycode <= KEYBOARD_MAX) {
				data->pressed[keycode] = press;
				input_report_key(data->input_dev,
					data->keycode[keycode], press);
				input_sync(data->input_dev);
			}
			break;
		}
	}
}

static int check_keyboard_dock(struct sec_keyboard_callbacks *cb, bool val)
{
	struct sec_keyboard_drvdata *data =
		container_of(cb, struct sec_keyboard_drvdata, callbacks);
	int try_cnt = 0;
	int max_cnt = 14;

	if (NULL == data->serio) {
		for (try_cnt = 0; try_cnt < max_cnt; try_cnt++) {
			msleep(50);
			if (data->tx_ready)
				break;

			if (gpio_get_value(data->acc_int_gpio)) {
				printk(KERN_DEBUG "[Keyboard] acc is disconnected.\n");
				return 0;
			}
		}
		/* To block acc_power enable in LPM mode */
		if ((data->tx_ready != true) && (val == true))
			return 0 ;
	}

	if (!val) {
		data->dockconnected = false;
		data->univ_kbd_dock = false;
	} else {
		cancel_delayed_work_sync(&data->power_dwork);
		/* wakeup by keyboard dock */
		if (data->pre_connected) {
			if (UNKOWN_KEYLAYOUT != data->pre_kl) {
				data->kl = data->pre_kl;
				data->acc_power(1, true);
				printk(KERN_DEBUG "[Keyboard] kl : %d\n",
					data->pre_kl);
				return 1;
			}
		} else
			data->pre_kl = UNKOWN_KEYLAYOUT;

		data->pre_connected = true;

		/* to prevent the over current issue */
		data->acc_power(0, false);

		if (data->check_uart_path)
			data->check_uart_path(true);

		msleep(200);
		data->acc_power(1, true);

		/* try to get handshake data */
		for (try_cnt = 0; try_cnt < max_cnt; try_cnt++) {
			msleep(50);
			if (data->kl != UNKOWN_KEYLAYOUT) {
				data->dockconnected = true;
				break;
			}
			if (gpio_get_value(data->acc_int_gpio)) {
				printk(KERN_DEBUG "[Keyboard] acc is disconnected.\n");
				break;
			}
		}
	}

	if (data->dockconnected) {
		return 1;
	} else {
		if (data->pre_connected) {
			data->dockconnected = false;
			schedule_delayed_work(&data->power_dwork, HZ/2);

			data->kl = UNKOWN_KEYLAYOUT;
			release_all_keys(data);
		}
		return 0;
	}
}

static int sec_keyboard_event(struct input_dev *dev,
			unsigned int type, unsigned int code, int value)
{
	struct sec_keyboard_drvdata *data = input_get_drvdata(dev);

	switch (type) {
	case EV_LED:
		if (value)
			sec_keyboard_tx(data, 0xca);
		else
			sec_keyboard_tx(data, 0xcb);

	printk(KERN_DEBUG "[Keyboard] %s, capslock on led value=%d\n",\
		 __func__, value);
		return 0;
	}
	return -1;
}

static ssize_t check_keyboard_connection(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct sec_keyboard_drvdata *ddata = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ddata->dockconnected);
}

static DEVICE_ATTR(attached, S_IRUGO, check_keyboard_connection, NULL);

static struct attribute *sec_keyboard_attributes[] = {
	&dev_attr_attached.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = sec_keyboard_attributes,
};

static irqreturn_t sec_keyboard_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct sec_keyboard_drvdata *ddata = serio_get_drvdata(serio);
	if (ddata->pre_connected)
		sec_keyboard_process_data(ddata, data);
	return IRQ_HANDLED;
}

static int sec_keyboard_connect(struct serio *serio, struct serio_driver *drv)
{
	struct sec_keyboard_drvdata *data = container_of(drv,
			struct sec_keyboard_drvdata, serio_driver);
	printk(KERN_DEBUG "[Keyboard] %s", __func__);
	data->serio = serio;
	serio_set_drvdata(serio, data);
	if (serio_open(serio, drv))
		printk(KERN_ERR "[Keyboard] failed to open serial port\n");
	else
		data->tx_ready = true;
	return 0;
}

static void sec_keyboard_disconnect(struct serio *serio)
{
	struct sec_keyboard_drvdata *data = serio_get_drvdata(serio);
	printk(KERN_DEBUG "[Keyboard] %s", __func__);
	data->tx_ready = false;
	serio_close(serio);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void keyboard_early_suspend(struct early_suspend *early_sus)
{
	struct sec_keyboard_drvdata *data = container_of(early_sus,
		struct sec_keyboard_drvdata, early_suspend);

	if (data->kl != UNKOWN_KEYLAYOUT) {
		/*
		if the command of the caps lock off is needed,
		this command should be sent.
		sec_keyboard_tx(0xcb);
		msleep(20);
		*/
		release_all_keys(data);
		if (data->univ_kbd_dock == false)
			sec_keyboard_tx(data, 0x10);	/* the idle mode */
	}
}

static void keyboard_late_resume(struct early_suspend *early_sus)
{
	struct sec_keyboard_drvdata *data = container_of(early_sus,
		struct sec_keyboard_drvdata, early_suspend);

	if (data->kl != UNKOWN_KEYLAYOUT)
		printk(KERN_DEBUG "[Keyboard] %s\n", __func__);

}
#endif
static int __devinit sec_keyboard_probe(struct platform_device *pdev)
{
	struct sec_keyboard_platform_data *pdata = pdev->dev.platform_data;
	struct sec_keyboard_drvdata *ddata;
	struct input_dev *input;
	int i, error;

	if (pdata == NULL) {
		printk(KERN_ERR "%s: no pdata\n", __func__);
		return -ENODEV;
	}

	ddata = kzalloc(sizeof(struct sec_keyboard_drvdata), GFP_KERNEL);
	if (NULL == ddata) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	input = input_allocate_device();
	if (NULL == input) {
		printk(KERN_ERR "[Keyboard] failed to allocate input device.\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	ddata->input_dev = input;
	ddata->acc_power = pdata->acc_power;
	ddata->check_uart_path = pdata->check_uart_path;
	ddata->acc_int_gpio = pdata->accessory_irq_gpio;
	ddata->led_on = false;
	ddata->dockconnected = false;
	ddata->pre_connected = false;
	ddata->univ_kbd_dock = false;
	ddata->remap_key = 0;
	ddata->kl = UNKOWN_KEYLAYOUT;
	ddata->callbacks.check_keyboard_dock = check_keyboard_dock;
	if (pdata->register_cb)
		pdata->register_cb(&ddata->callbacks);
	ddata->noti_univ_kbd_dock = pdata->noti_univ_kbd_dock;

	memcpy(ddata->keycode, sec_keycodes, sizeof(sec_keycodes));

	INIT_DELAYED_WORK(&ddata->remap_dwork, sec_keyboard_remapkey);
	INIT_DELAYED_WORK(&ddata->power_dwork, sec_keyboard_power);
	INIT_DELAYED_WORK(&ddata->ack_dwork, sec_keyboard_ack);

	platform_set_drvdata(pdev, ddata);
	input_set_drvdata(input, ddata);

	input->name = pdev->name;
	input->dev.parent = &pdev->dev;
	input->id.bustype = BUS_RS232;
	input->event = sec_keyboard_event;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(EV_LED, input->evbit);
	__set_bit(LED_CAPSL, input->ledbit);
	/* framework doesn't use repeat event */
	/* __set_bit(EV_REP, input->evbit); */

	for (i = 0; i < KEYBOARD_SIZE; i++) {
		if (KEY_RESERVED != ddata->keycode[i])
			input_set_capability(input, EV_KEY, ddata->keycode[i]);
	}

	/* for the UK keyboard */
	input_set_capability(input, EV_KEY, KEY_NUMERIC_POUND);

	/* for the remaped keys */
	input_set_capability(input, EV_KEY, KEY_NEXTSONG);
	input_set_capability(input, EV_KEY, KEY_PREVIOUSSONG);

	/* for the wakeup key */
	input_set_capability(input, EV_KEY, KEY_WAKEUP);

	error = input_register_device(input);
	if (error < 0) {
		printk(KERN_ERR "[Keyboard] failed to register input device.\n");
		error = -ENOMEM;
		goto err_input_allocate_device;
	}

	ddata->serio_driver.driver.name = pdev->name;
	ddata->serio_driver.id_table = sec_serio_ids;
	ddata->serio_driver.interrupt = sec_keyboard_interrupt,
	ddata->serio_driver.connect = sec_keyboard_connect,
	ddata->serio_driver.disconnect = sec_keyboard_disconnect,

	error = serio_register_driver(&ddata->serio_driver);
	if (error < 0) {
		printk(KERN_ERR "[Keyboard] failed to register serio\n");
		error = -ENOMEM;
		goto err_reg_serio;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ddata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ddata->early_suspend.suspend = keyboard_early_suspend;
	ddata->early_suspend.resume = keyboard_late_resume;
	register_early_suspend(&ddata->early_suspend);
#endif	/* CONFIG_HAS_EARLYSUSPEND */

	ddata->keyboard_dev = device_create(sec_class, NULL, 0,
		ddata, "sec_keyboard");
	if (IS_ERR(ddata->keyboard_dev)) {
		printk(KERN_ERR "[Keyboard] failed to create device for the sysfs\n");
		error = -ENODEV;
		goto err_sysfs_create_group;
	}

	error = sysfs_create_group(&ddata->keyboard_dev->kobj, &attr_group);
	if (error) {
		printk(KERN_ERR "[Keyboard] failed to create sysfs group\n");
		goto err_sysfs_create_group;
	}

	return 0;

err_sysfs_create_group:
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ddata->early_suspend);
#endif
	serio_unregister_driver(&ddata->serio_driver);
err_reg_serio:
err_input_allocate_device:
	input_free_device(input);
	del_timer_sync(&ddata->remap_dwork.timer);
	del_timer_sync(&ddata->power_dwork.timer);
	del_timer_sync(&ddata->ack_dwork.timer);
err_free_mem:
	kfree(ddata);
	return error;

}

static int __devexit sec_keyboard_remove(struct platform_device *pdev)
{
	struct sec_keyboard_drvdata *data = platform_get_drvdata(pdev);
	input_unregister_device(data->input_dev);
	serio_unregister_driver(&data->serio_driver);
	return 0;
}

#ifndef CONFIG_HAS_EARLYSUSPEND
static int sec_keyboard_suspend(struct platform_device *pdev,
			pm_message_t state)
{
	struct sec_keyboard_drvdata *data = platform_get_drvdata(pdev);

	if (data->kl != UNKOWN_KEYLAYOUT)
		sec_keyboard_tx(data, 0x10);

	return 0;
}

static int sec_keyboard_resume(struct platform_device *pdev)
{
	struct sec_keyboard_platform_data *pdata = pdev->dev.platform_data;
	struct sec_keyboard_drvdata *data = platform_get_drvdata(pdev);
	if (pdata->wakeup_key) {
		if (KEY_WAKEUP == pdata->wakeup_key())
			forced_wakeup(data);
	}

	return 0;
}
#endif

static struct platform_driver sec_keyboard_driver = {
	.probe = sec_keyboard_probe,
	.remove = __devexit_p(sec_keyboard_remove),
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = sec_keyboard_suspend,
	.resume	= sec_keyboard_resume,
#endif
	.driver = {
		.name	= "sec_keyboard",
		.owner	= THIS_MODULE,
	}
};

static int __init sec_keyboard_init(void)
{
	return platform_driver_register(&sec_keyboard_driver);
}

static void __exit sec_keyboard_exit(void)
{
	platform_driver_unregister(&sec_keyboard_driver);
}

module_init(sec_keyboard_init);
module_exit(sec_keyboard_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SEC Keyboard Dock driver");
