

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/30pin_con.h>
#include <linux/switch.h>
#include <linux/wakelock.h>
#include <plat/adc.h>
#include <linux/earlysuspend.h>
#include <linux/power_supply.h>

#include <asm/irq.h>
#include <linux/mfd/tps6586x.h>

#ifdef CONFIG_REGULATOR
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#endif

#ifdef CONFIG_MHL_SII9234
#include "sii9234.h"
#endif

#define SUBJECT "ACCESSORY"

#define ACC_CONDEV_DBG(format, ...) \
	pr_info("[ "SUBJECT " (%s,%d) ] " format "\n", \
		__func__, __LINE__, ## __VA_ARGS__);

#define DETECTION_INTR_DELAY	(get_jiffies_64() + (HZ*15)) /* 20s */

enum accessory_type {
	ACCESSORY_NONE = 0,
	ACCESSORY_OTG,
	ACCESSORY_LINEOUT,
	ACCESSORY_CARMOUNT,
	ACCESSORY_UNKNOWN,
};

enum dock_type {
	DOCK_NONE = 0,
	DOCK_DESK,
	DOCK_KEYBOARD,
};

enum uevent_dock_type {
	UEVENT_DOCK_NONE = 0,
	UEVENT_DOCK_DESK,
	UEVENT_DOCK_CAR,
	UEVENT_DOCK_KEYBOARD = 9,
};

struct acc_con_info {
	struct device *acc_dev;
	struct acc_con_platform_data *pdata;
	struct delayed_work acc_dwork;
	struct delayed_work acc_id_dwork;
	struct switch_dev dock_switch;
	struct switch_dev ear_jack_switch;
	struct wake_lock wake_lock;
	struct s3c_adc_client *padc;
	struct sec_30pin_callbacks callbacks;
	enum accessory_type current_accessory;
	enum accessory_type univ_kdb_accessory;
	enum dock_type current_dock;
	int accessory_irq;
	int dock_irq;
	int cable_type;
	int cable_sub_type;
	int cable_pwr_type;
#if defined(CONFIG_MHL_SII9234) || defined(CONFIG_SAMSUNG_MHL_9290)
	int mhl_irq;
	bool mhl_pwr_state;
#endif
	struct early_suspend early_suspend;
	struct delayed_work acc_con_work;
	struct mutex lock;
};

#if defined(CONFIG_STMPE811_ADC)
#ifdef CONFIG_MACH_P4NOTE
#define ACCESSORY_ID_ADC_CH 7
#else
#define ACCESSORY_ID_ADC_CH 0
#endif
#else
#define ACCESSORY_ID_ADC_CH 4
#endif

#ifdef CONFIG_SAMSUNG_MHL_9290
static BLOCKING_NOTIFIER_HEAD(acc_notifier);

int acc_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&acc_notifier, nb);
}

int acc_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&acc_notifier, nb);
}

static int acc_notify(int event)
{
	pr_info("notifier: mhl callback\n");
	return blocking_notifier_call_chain(&acc_notifier, event, NULL);
}
#endif
static int acc_get_adc_accessroy_id(struct s3c_adc_client *padc)
{
	int adc_data;

#if defined(CONFIG_STMPE811_ADC)
	adc_data = stmpe811_get_adc_data(ACCESSORY_ID_ADC_CH);
#else
	adc_data = s3c_adc_read(padc, ACCESSORY_ID_ADC_CH);
#endif
/*	ACC_CONDEV_DBG("[ACC] adc_data = %d..\n",adc_data); */
	return adc_data;
}

static int acc_get_accessory_id(struct acc_con_info *acc)
{
	int i;
	u32 adc = 0, adc_sum = 0;
	u32 adc_buff[5] = {0};
	u32 adc_val = 0;
	u32 adc_min = 0;
	u32 adc_max = 0;

	if (!acc) {
		pr_err("adc client is not registered!\n");
		return -1;
	}

	for (i = 0; i < 5; i++) {
		adc_val = acc_get_adc_accessroy_id(acc->padc);
		ACC_CONDEV_DBG("ACCESSORY_ID adc_val[%d] value = %d",
			i, adc_val);
		adc_buff[i] = adc_val;
		adc_sum += adc_buff[i];
		if (i == 0) {
			adc_min = adc_buff[0];
			adc_max = adc_buff[0];
		} else {
			if (adc_max < adc_buff[i])
				adc_max = adc_buff[i];
			else if (adc_min > adc_buff[i])
				adc_min = adc_buff[i];
		}
		msleep(20);
	}
	/* adc = (adc_sum - adc_max - adc_min)/3; */
	adc = adc_buff[4];
	ACC_CONDEV_DBG("ACCESSORY_ID ADC value = %d", adc);
	return (int)adc;
}

#ifdef CONFIG_MHL_SII9234
static ssize_t MHD_check_read(struct class *class,
	struct class_attribute *attr, char *buf)
{
	int count;
	int res;
	/*TVout_LDO_ctrl(true);*/
	if (!MHD_HW_IsOn()) {
		sii9234_tpi_init();
		res = MHD_Read_deviceID();
		MHD_HW_Off();
	} else {
		sii9234_tpi_init();
		res = MHD_Read_deviceID();
	}

	count = sprintf(buf, "%d\n", res);
	/*TVout_LDO_ctrl(false);*/
	return count;
}

static ssize_t MHD_check_write(struct class *class,
		struct class_attribute *attr, const char *buf, size_t size)
{
	printk(KERN_INFO"input data --> %s\n", buf);

	return size;
}

static CLASS_ATTR(test_result, S_IRUGO, MHD_check_read, MHD_check_write);
#endif /* CONFIG_MHL_SII9234 */

static ssize_t acc_read_acc_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct acc_con_info *acc  = dev_get_drvdata(dev);
	int adc_val = 0 ;
	int jig_uart_off = 0 ;
	int count = 0 ;
	adc_val = acc_get_accessory_id(acc);

	if ((3540 < adc_val) && (adc_val < 3920))
		jig_uart_off = 28 ;
	else
		jig_uart_off = 0 ;

	ACC_CONDEV_DBG("jig_uart_off : %d", jig_uart_off);

	count = sprintf(buf, "%x\n", jig_uart_off);

	return count;
}

static DEVICE_ATTR(adc, S_IRUGO, acc_read_acc_id, NULL);


void acc_accessory_uevent(struct acc_con_info *acc, int acc_adc)
{
	char *env_ptr;
	char *stat_ptr;
	char *envp[3];

	/* value is changed for PxLTE
	3 pole earjack  1.00 V ( 0.90~1.10V)   adc: 797~1002
	Car mount        1.38 V (1.24~1.45V)   adc: 1134~1352
	4 pole earjack   just bundles is supported . adc :1360~1449
	OTG                 2.2 V  (2.00~2.35V)    adc: 1903~2248 */

	if (acc_adc != false) {
		if ((1100 < acc_adc) && (1400 > acc_adc)) {
			/* 3 pole earjack 1220 */
			env_ptr = "ACCESSORY=lineout";
			acc->current_accessory = ACCESSORY_LINEOUT;
			switch_set_state(&acc->ear_jack_switch, 1);
#if 0
		} else if ((1134 < acc_adc) && (1352 > acc_adc)) {
			/* car mount */
			env_ptr = "ACCESSORY=carmount";
			acc->current_accessory = ACCESSORY_CARMOUNT;
#endif
		} else if ((1800 < acc_adc) && (2350 > acc_adc)) {
			/* 4 pole earjack, No warranty 2000 */
			env_ptr = "ACCESSORY=lineout";
			acc->current_accessory = ACCESSORY_LINEOUT;
			switch_set_state(&acc->ear_jack_switch, 1);
		} else if ((2450 < acc_adc) && (2850 > acc_adc)) {
			/* otg 2730 */
			env_ptr = "ACCESSORY=OTG";
			acc->current_accessory = ACCESSORY_OTG;
		} else {
			env_ptr = "ACCESSORY=unknown";
			acc->current_accessory = ACCESSORY_UNKNOWN;
		}

		stat_ptr = "STATE=online";
		envp[0] = env_ptr;
		envp[1] = stat_ptr;
		envp[2] = NULL;
		if (acc->current_accessory == ACCESSORY_OTG) {
			if (acc->pdata->usb_ldo_en)
				acc->pdata->usb_ldo_en(1);
			if (acc->pdata->otg_en)
				acc->pdata->otg_en(1);
		}
		kobject_uevent_env(&acc->acc_dev->kobj, KOBJ_CHANGE, envp);
		ACC_CONDEV_DBG("%s : %s", env_ptr, stat_ptr);
	} else {
		if (acc->current_accessory == ACCESSORY_OTG)
			env_ptr = "ACCESSORY=OTG";
		else if (acc->current_accessory == ACCESSORY_LINEOUT) {
			env_ptr = "ACCESSORY=lineout";
			switch_set_state(&acc->ear_jack_switch,
				UEVENT_DOCK_NONE);
		} else if (acc->current_accessory == ACCESSORY_CARMOUNT)
			env_ptr = "ACCESSORY=carmount";
		else
			env_ptr = "ACCESSORY=unknown";

		stat_ptr = "STATE=offline";
		envp[0] = env_ptr;
		envp[1] = stat_ptr;
		envp[2] = NULL;
		kobject_uevent_env(&acc->acc_dev->kobj, KOBJ_CHANGE, envp);
		if ((acc->current_accessory == ACCESSORY_OTG) &&
			acc->pdata->otg_en)
			acc->pdata->otg_en(0);

		acc->current_accessory = ACCESSORY_NONE;
		ACC_CONDEV_DBG("%s : %s", env_ptr, stat_ptr);
	}
}

static void acc_dock_uevent(struct acc_con_info *acc, bool connected)
{
	char *env_ptr;
	char *stat_ptr;
	char *envp[3];

	if (acc->current_dock == DOCK_KEYBOARD)
		env_ptr = "DOCK=keyboard";
	else if (acc->current_dock == DOCK_DESK)
		env_ptr = "DOCK=desk";
	else
		env_ptr = "DOCK=unknown";

	if (!connected) {
		stat_ptr = "STATE=offline";
		acc->current_dock = DOCK_NONE;
	} else {
		stat_ptr = "STATE=online";
	}

	envp[0] = env_ptr;
	envp[1] = stat_ptr;
	envp[2] = NULL;
	kobject_uevent_env(&acc->acc_dev->kobj, KOBJ_CHANGE, envp);
	ACC_CONDEV_DBG("%s : %s", env_ptr, stat_ptr);
}

/* power supply name for set state */
#define PSY_NAME	"battery"
static void acc_dock_psy(struct acc_con_info *acc)
{
	struct power_supply *psy = power_supply_get_by_name(PSY_NAME);
	union power_supply_propval value;

/* only support p4note(high current charging) */
#ifndef CONFIG_MACH_P4NOTE
	return;
#endif

	if (!psy || !psy->set_property) {
		pr_err("%s: fail to get %s psy\n", __func__, PSY_NAME);
		return;
	}

	value.intval = 0;
	value.intval = (acc->cable_type << 16) + (acc->cable_sub_type << 8) +
			(acc->cable_pwr_type << 0);
	pr_info("[BATT]30 cx(%d), sub(%d), pwr(%d)\n",
		acc->cable_type, acc->cable_sub_type, acc->cable_pwr_type);

	psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &value);
}

void acc_otg_enable_by_univkbd(struct acc_con_info *acc, bool val)
{
	char *env_ptr;
	char *stat_ptr;
	char *envp[3];

	if (val == true) {
		if (acc->univ_kdb_accessory == ACCESSORY_NONE) {
			env_ptr = "ACCESSORY=OTG";
			stat_ptr = "STATE=online";
			acc->univ_kdb_accessory = ACCESSORY_OTG;

			if (acc->pdata->usb_ldo_en)
				acc->pdata->usb_ldo_en(1);
			if (acc->pdata->otg_en)
				acc->pdata->otg_en(1);

			envp[0] = env_ptr;
			envp[1] = stat_ptr;
			envp[2] = NULL;
			kobject_uevent_env(&acc->acc_dev->kobj,
					KOBJ_CHANGE, envp);
			ACC_CONDEV_DBG("%s : %s", env_ptr, stat_ptr);
		}
	} else {
		if (acc->univ_kdb_accessory == ACCESSORY_OTG) {
			env_ptr = "ACCESSORY=OTG";
			stat_ptr = "STATE=offline";

			envp[0] = env_ptr;
			envp[1] = stat_ptr;
			envp[2] = NULL;
			kobject_uevent_env(&acc->acc_dev->kobj,
					KOBJ_CHANGE, envp);
			ACC_CONDEV_DBG("%s : %s", env_ptr, stat_ptr);

			if (acc->pdata->otg_en)
				acc->pdata->otg_en(0);

			acc->univ_kdb_accessory = ACCESSORY_NONE;
		}
	}
}

static void acc_check_dock_detection(struct acc_con_info *acc)
{
	if (NULL == acc->pdata->get_dock_state) {
		ACC_CONDEV_DBG("[30PIN] failed to get acc state!!!");
		return;
	}
	if (!acc->pdata->get_dock_state()) {
#ifdef CONFIG_SEC_KEYBOARD_DOCK
		if (acc->pdata->check_keyboard &&
			acc->pdata->check_keyboard(true)) {
			if (DOCK_KEYBOARD == acc->current_dock) {
				switch_set_state(&acc->dock_switch,
					UEVENT_DOCK_NONE);
				acc_dock_uevent(acc, false);
			}

			acc->current_dock = DOCK_KEYBOARD;
			ACC_CONDEV_DBG
			("The dock proves to be a keyboard dock..!");
			switch_set_state(&acc->dock_switch,
				UEVENT_DOCK_KEYBOARD);
		} else
#endif
		{
			ACC_CONDEV_DBG
			("The dock proves to be a desktop dock..!");
			switch_set_state(&acc->dock_switch, UEVENT_DOCK_DESK);
			acc->current_dock = DOCK_DESK;
			acc->cable_type = POWER_SUPPLY_TYPE_DOCK;
			acc->cable_sub_type = ONLINE_SUB_TYPE_DESK;

#if defined(CONFIG_MHL_SII9234) || defined(CONFIG_SAMSUNG_MHL_9290)
			mutex_lock(&acc->lock);
			if (!acc->mhl_pwr_state) {
#if defined(CONFIG_MHL_SII9234)
				sii9234_tpi_init();
#elif defined(CONFIG_SAMSUNG_MHL_9290)
				acc_notify(1);
#endif
				acc->mhl_pwr_state = true;
			}
			mutex_unlock(&acc->lock);
#endif
		}
		acc_dock_uevent(acc, true);
	} else {

		ACC_CONDEV_DBG("dock station detached.. !");

		switch_set_state(&acc->dock_switch, UEVENT_DOCK_NONE);
		acc->current_dock = DOCK_NONE;
		acc->cable_type = POWER_SUPPLY_TYPE_BATTERY;
		acc->cable_sub_type = ONLINE_SUB_TYPE_UNKNOWN;
#ifdef CONFIG_SEC_KEYBOARD_DOCK
		if (acc->pdata->check_keyboard)
			acc->pdata->check_keyboard(false);
		if (acc->univ_kdb_accessory == ACCESSORY_OTG) {
			acc_otg_enable_by_univkbd(acc, false);
			acc->current_dock = DOCK_NONE;
			acc->cable_type = POWER_SUPPLY_TYPE_BATTERY;
			acc->cable_sub_type = ONLINE_SUB_TYPE_UNKNOWN;
		}
#endif
#if defined(CONFIG_MHL_SII9234) || defined(CONFIG_SAMSUNG_MHL_9290)
		/*call MHL deinit */
		if (acc->mhl_pwr_state) {
#if defined(CONFIG_MHL_SII9234)
			MHD_HW_Off();
#elif defined(CONFIG_SAMSUNG_MHL_9290)
			acc_notify(0);
#endif
			acc->mhl_pwr_state = false;
		}
#endif
		acc_dock_uevent(acc, false);

	}
	acc_dock_psy(acc);
}

static irqreturn_t acc_dock_isr(int irq, void *ptr)
{
	struct acc_con_info *acc = ptr;
	wake_lock(&acc->wake_lock);
	ACC_CONDEV_DBG
		("A dock station attached or detached..");
	acc_check_dock_detection(acc);
	wake_unlock(&acc->wake_lock);
	return IRQ_HANDLED;
}

#define DETECTION_DELAY_MS	200

static irqreturn_t acc_accessory_isr(int irq, void *dev_id)
{
	struct acc_con_info *acc = (struct acc_con_info *)dev_id;
	ACC_CONDEV_DBG("");
	cancel_delayed_work_sync(&acc->acc_id_dwork);
	schedule_delayed_work(&acc->acc_id_dwork,
			msecs_to_jiffies(DETECTION_DELAY_MS));
	return IRQ_HANDLED;
}

static int acc_init_dock_int(struct acc_con_info *acc)
{
	int ret = 0;
	acc->accessory_irq = gpio_to_irq(acc->pdata->accessory_irq_gpio);
	ret = request_threaded_irq(acc->accessory_irq, NULL, acc_dock_isr,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_NO_SUSPEND,
			"accessory_detect", acc);
	if (ret)
		ACC_CONDEV_DBG("request_irq(accessory_irq) return : %d\n", ret);

	ret = enable_irq_wake(acc->accessory_irq);
	if (ret)
		ACC_CONDEV_DBG("enable_irq_wake(accessory_irq) return : %d\n",
			ret);

	return ret;
}

static int acc_init_accessory_int(struct acc_con_info *acc)
{
	int ret = 0;
	acc->dock_irq = gpio_to_irq(acc->pdata->dock_irq_gpio);
	ret = request_threaded_irq(acc->dock_irq, NULL, acc_accessory_isr,
			IRQF_ONESHOT |
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"dock_detect", acc);
	if (ret)
		ACC_CONDEV_DBG("request_irq(dock_irq) return : %d\n", ret);

	ret = enable_irq_wake(acc->dock_irq);
	if (ret)
		ACC_CONDEV_DBG("enable_irq_wake(dock_irq) return : %d\n", ret);

	return ret;
}

static void acc_dwork_int_init(struct work_struct *work)
{
	struct acc_con_info *acc = container_of(work,
		struct acc_con_info, acc_dwork.work);
	int retval;

	ACC_CONDEV_DBG("");

	retval = acc_init_dock_int(acc);
	if (retval) {
		ACC_CONDEV_DBG("failed to initialize dock_int irq");
		goto err_irq_dock;
	}

	retval = acc_init_accessory_int(acc);
	if (retval) {
		ACC_CONDEV_DBG(" failed to initialize accessory_int irq");
		goto err_irq_acc;
	}

	if (acc->pdata->get_dock_state) {
		if (!acc->pdata->get_dock_state())
			acc_check_dock_detection(acc);
	}

	if (acc->pdata->get_acc_state) {
		if (!acc->pdata->get_acc_state())
			schedule_delayed_work(&acc->acc_id_dwork,
				msecs_to_jiffies(DETECTION_DELAY_MS));
	}

	return ;

err_irq_acc:
	free_irq(acc->accessory_irq, acc);
err_irq_dock:
	switch_dev_unregister(&acc->ear_jack_switch);
	return ;
}

static int acc_noti_univkbd_dock(struct sec_30pin_callbacks *cb,
	unsigned int code)
{
	struct acc_con_info *acc =
		container_of(cb, struct acc_con_info, callbacks);

	char *env_ptr;
	char *stat_ptr;
	char *envp[3];

	ACC_CONDEV_DBG("universal keyboard noti. callback 0x%x", code);

	switch (code) {
	case 0x68: /*dock is con*/
		acc_otg_enable_by_univkbd(acc, true);
		acc->cable_type = POWER_SUPPLY_TYPE_DOCK;
		acc->cable_sub_type = ONLINE_SUB_TYPE_KBD;
		acc_dock_psy(acc);
		break;
	case 0x69: /*usb charging*/
		acc->cable_pwr_type = ONLINE_POWER_TYPE_USB;
		acc_dock_psy(acc);
		break;
	case 0x6a: /*what is this. same as usb charging*/
		acc_otg_enable_by_univkbd(acc, false);
		acc->cable_pwr_type = ONLINE_POWER_TYPE_USB;
		acc_dock_psy(acc);
		break;
	case 0x6b: /*TA connection*/
		acc->cable_pwr_type = ONLINE_POWER_TYPE_TA;
		acc_dock_psy(acc);
		break;
	case 0x6c: /* current is not support*/
		acc->cable_pwr_type = ONLINE_POWER_TYPE_BATTERY;
		acc_dock_psy(acc);
		break;
	}

	return 0;
}

static void acc_dwork_accessory_detect(struct work_struct *work)
{
	struct acc_con_info *acc = container_of(work,
			struct acc_con_info, acc_id_dwork.work);

	int adc_val = 0;
	int acc_state = 0;
	int acc_state2 = 0;

	acc_state = acc->pdata->get_acc_state();

	if (acc_state) {
		ACC_CONDEV_DBG("Accessory detached");
		acc_accessory_uevent(acc, false);
	} else {
		adc_val = acc_get_accessory_id(acc);

		acc_state2 = acc->pdata->get_acc_state();
		if (acc_state2) {
			ACC_CONDEV_DBG("Accessory detached2.");
			acc_accessory_uevent(acc, false);
		} else {
		ACC_CONDEV_DBG("Accessory attached");
		acc_accessory_uevent(acc, adc_val);
		}
	}
}

static int acc_con_probe(struct platform_device *pdev)
{
	struct acc_con_info *acc;
	struct acc_con_platform_data *pdata = pdev->dev.platform_data;
	struct regulator *vadc_regulator;

	int	retval;
#ifdef CONFIG_MHL_SII9234
	struct class *sec_mhl;
#endif
	ACC_CONDEV_DBG("");

	if (pdata == NULL) {
		pr_err("%s: no pdata\n", __func__);
		return -ENODEV;
	}

#ifdef CONFIG_REGULATOR
#ifndef CONFIG_MACH_P4NOTE
		/* LDO1 regulator ON */
		vadc_regulator = regulator_get(&pdev->dev, "vadc_3.3v");
		if (IS_ERR(vadc_regulator)) {
			printk(KERN_ERR "failed to get resource %s\n",
				"vadc_3.3v");
			return PTR_ERR(vadc_regulator);
		}
		regulator_enable(vadc_regulator);
#endif
#endif

	acc = kzalloc(sizeof(*acc), GFP_KERNEL);
	if (!acc)
		return -ENOMEM;

	acc->pdata = pdata;
	acc->current_dock = DOCK_NONE;
	acc->current_accessory = ACCESSORY_NONE;
	acc->univ_kdb_accessory = ACCESSORY_NONE;
#if defined(CONFIG_MHL_SII9234) || defined(CONFIG_SAMSUNG_MHL_9290)
	acc->mhl_irq = gpio_to_irq(pdata->mhl_irq_gpio);
	acc->mhl_pwr_state = false;
#endif

	mutex_init(&acc->lock);
	dev_set_drvdata(&pdev->dev, acc);

	acc->acc_dev = &pdev->dev;

	/* Register adc client */
#if defined(CONFIG_STMPE811_ADC)
	/* Do nothing */
#elif defined(CONFIG_S3C_ADC)
	acc->padc = s3c_adc_register(pdev, NULL, NULL, 0);
#endif

	acc->callbacks.noti_univ_kdb_dock = acc_noti_univkbd_dock;
	if (pdata->register_cb)
		pdata->register_cb(&acc->callbacks);

#ifdef CONFIG_MHL_SII9234
	retval = i2c_add_driver(&SII9234A_i2c_driver);
	if (retval) {
		pr_err("[MHL SII9234A] can't add i2c driver\n");
		goto err_i2c_a;
	} else {
		pr_info("[MHL SII9234A] add i2c driver\n");
	}

	retval = i2c_add_driver(&SII9234B_i2c_driver);
	if (retval) {
		pr_err("[MHL SII9234B] can't add i2c driver\n");
		goto err_i2c_b;
	} else {
		pr_info("[MHL SII9234B] add i2c driver\n");
	}

	retval = i2c_add_driver(&SII9234C_i2c_driver);
	if (retval) {
		pr_err("[MHL SII9234C] can't add i2c driver\n");
		goto err_i2c_c;
	} else {
		pr_info("[MHL SII9234C] add i2c driver\n");
	}

	retval = i2c_add_driver(&SII9234_i2c_driver);
	if (retval) {
		pr_err("[MHL SII9234] can't add i2c driver\n");
		goto err_i2c;
	} else {
		pr_info("[MHL SII9234] add i2c driver\n");
	}
#endif
	acc->dock_switch.name = "dock";
	retval = switch_dev_register(&acc->dock_switch);
	if (retval < 0)
		goto err_sw_dock;

	acc->ear_jack_switch.name = "usb_audio";
	retval = switch_dev_register(&acc->ear_jack_switch);
	if (retval < 0)
		goto err_sw_jack;

	wake_lock_init(&acc->wake_lock, WAKE_LOCK_SUSPEND, "30pin_con");

	INIT_DELAYED_WORK(&acc->acc_dwork, acc_dwork_int_init);
	schedule_delayed_work(&acc->acc_dwork, msecs_to_jiffies(10000));
	INIT_DELAYED_WORK(&acc->acc_id_dwork, acc_dwork_accessory_detect);
#ifdef CONFIG_MHL_SII9234
	sec_mhl = class_create(THIS_MODULE, "mhl");
	if (IS_ERR(sec_mhl)) {
		pr_err("Failed to create class(sec_mhl)!\n");
		retval = -ENOMEM;
	}

	retval = class_create_file(sec_mhl, &class_attr_test_result);
	if (retval) {
		pr_err("Failed to create device file in sysfs entries!\n");
		retval = -ENOMEM;
	}
#endif

#ifndef CONFIG_MACH_P10
	if (device_create_file(sec_switch_dev, &dev_attr_adc) < 0)
		pr_err("Failed to create device file(%s)!\n",
			dev_attr_adc  .attr.name);
	dev_set_drvdata(sec_switch_dev, acc);
#endif

	return 0;

#ifndef CONFIG_SEC_KEYBOARD_DOCK
err_irq_acc:
	free_irq(acc->accessory_irq, acc);
err_irq_dock:
	switch_dev_unregister(&acc->ear_jack_switch);
#endif
err_sw_jack:
	switch_dev_unregister(&acc->dock_switch);
err_sw_dock:
#ifdef CONFIG_MHL_SII9234
	i2c_del_driver(&SII9234_i2c_driver);
err_i2c:
	i2c_del_driver(&SII9234C_i2c_driver);
err_i2c_c:
	i2c_del_driver(&SII9234B_i2c_driver);
err_i2c_b:
	i2c_del_driver(&SII9234A_i2c_driver);
err_i2c_a:
#endif

	kfree(acc);

	return retval;
}

static int acc_con_remove(struct platform_device *pdev)
{
	struct acc_con_info *acc = platform_get_drvdata(pdev);
	ACC_CONDEV_DBG("");

	free_irq(acc->accessory_irq, acc);
	free_irq(acc->dock_irq, acc);
#ifdef CONFIG_MHL_SII9234
	i2c_del_driver(&SII9234A_i2c_driver);
	i2c_del_driver(&SII9234B_i2c_driver);
	i2c_del_driver(&SII9234C_i2c_driver);
	i2c_del_driver(&SII9234_i2c_driver);
#endif

	switch_dev_unregister(&acc->dock_switch);
	switch_dev_unregister(&acc->ear_jack_switch);
	kfree(acc);
	return 0;
}

static int acc_con_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct acc_con_info *acc = platform_get_drvdata(pdev);
	ACC_CONDEV_DBG("");
#if defined(CONFIG_MHL_SII9234) || defined(CONFIG_SAMSUNG_MHL_9290)
	if (acc->mhl_pwr_state) {
		pr_err("%s::MHL off\n", __func__);
#if defined(CONFIG_MHL_SII9234)
		MHD_HW_Off();
#elif defined(CONFIG_SAMSUNG_MHL_9290)
		acc_notify(0);
#endif
		acc->mhl_pwr_state = false;
	}
#endif
	return 0;
}

static int acc_con_resume(struct platform_device *pdev)
{
	struct acc_con_info *acc = platform_get_drvdata(pdev);
	ACC_CONDEV_DBG("");

	mutex_lock(&acc->lock);
#if defined(CONFIG_MHL_SII9234) || defined(CONFIG_SAMSUNG_MHL_9290)
	if (acc->current_dock == DOCK_DESK && !acc->mhl_pwr_state) {
		pr_err("%s::MHL init\n", __func__);
#if defined(CONFIG_MHL_SII9234)
		sii9234_tpi_init();  /* call MHL init */
#elif defined(CONFIG_SAMSUNG_MHL_9290)
		acc_notify(1);
#endif
		acc->mhl_pwr_state = true;
	}
#endif
	mutex_unlock(&acc->lock);

	return 0;
}

static struct platform_driver acc_con_driver = {
	.probe		= acc_con_probe,
	.remove		= acc_con_remove,
	.suspend	= acc_con_suspend,
	.resume		= acc_con_resume,
	.driver		= {
		.name		= "acc_con",
		.owner		= THIS_MODULE,
	},
};

static int __init acc_con_init(void)
{
	ACC_CONDEV_DBG("");

	return platform_driver_register(&acc_con_driver);
}

static void __exit acc_con_exit(void)
{
	platform_driver_unregister(&acc_con_driver);
}

late_initcall(acc_con_init);
module_exit(acc_con_exit);

MODULE_AUTHOR("Kyungrok Min <gyoungrok.min@samsung.com>");
MODULE_DESCRIPTION("acc connector driver");
MODULE_LICENSE("GPL");
