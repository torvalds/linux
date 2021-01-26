// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/suspend.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include "ebc_pmic.h"

/* After waking up from sleep, Papyrus
   waits for VN to be discharged and all
   voltage ref to startup before loading
   the default EEPROM settings. So accessing
   registers too early after WAKEUP could
   cause the register to be overridden by
   default values */
#define PAPYRUS_EEPROM_DELAY_MS 50
/* Papyrus WAKEUP pin must stay low for
   a minimum time */
#define PAPYRUS_SLEEP_MINIMUM_MS 110
/* Temp sensor might take a little time to
   settle eventhough the status bit in TMST1
   state conversion is done - if read too early
   0C will be returned instead of the right temp */
#define PAPYRUS_TEMP_READ_TIME_MS 10
/* Powerup sequence takes at least 24 ms - no need to poll too frequently */
#define HW_GET_STATE_INTERVAL_MS 24

#define SEQ_VDD(index)		((index % 4) << 6)
#define SEQ_VPOS(index)	((index % 4) << 4)
#define SEQ_VEE(index)		((index % 4) << 2)
#define SEQ_VNEG(index)	((index % 4) << 0)

/* power up seq delay time */
#define UDLY_3ms(index)	(0x00 << ((index%4) * 2))
#define UDLY_6ms(index)	(0x01 << ((index%4) * 2))
#define UDLY_9ms(index)	(0x10 << ((index%4) * 2))
#define UDLY_12ms(index)	(0x11 << ((index%4) * 2))

/* power down seq delay time */
#define DDLY_6ms(index)	(0x00 << ((index%4) * 2))
#define DDLY_12ms(index)	(0x01 << ((index%4) * 2))
#define DDLY_24ms(index)	(0x10 << ((index%4) * 2))
#define DDLY_48ms(index)	(0x11 << ((index%4) * 2))

#define NUMBER_PMIC_REGS	10

#define PAPYRUS_ADDR_TMST_VALUE	0x00
#define PAPYRUS_ADDR_ENABLE			0x01
#define PAPYRUS_ADDR_VADJ			0x02
#define PAPYRUS_ADDR_VCOM1_ADJUST	0x03
#define PAPYRUS_ADDR_VCOM2_ADJUST	0x04
#define PAPYRUS_ADDR_INT_ENABLE1	0x05
#define PAPYRUS_ADDR_INT_ENABLE2	0x06
#define PAPYRUS_ADDR_INT_STATUS1	0x07
#define PAPYRUS_ADDR_INT_STATUS2	0x08
#define PAPYRUS_ADDR_UPSEQ0			0x09
#define PAPYRUS_ADDR_UPSEQ1			0x0a
#define PAPYRUS_ADDR_DWNSEQ0		0x0b
#define PAPYRUS_ADDR_DWNSEQ1		0x0c
#define PAPYRUS_ADDR_TMST1			0x0d
#define PAPYRUS_ADDR_TMST2			0x0e
#define PAPYRUS_ADDR_PG_STATUS		0x0f
#define PAPYRUS_ADDR_REVID			0x10

// INT_ENABLE1
#define PAPYRUS_INT_ENABLE1_ACQC_EN	1
#define PAPYRUS_INT_ENABLE1_PRGC_EN		0

// INT_STATUS1
#define PAPYRUS_INT_STATUS1_ACQC		1
#define PAPYRUS_INT_STATUS1_PRGC		0

// VCOM2_ADJUST
#define PAPYRUS_VCOM2_ACQ		7
#define PAPYRUS_VCOM2_PROG		6
#define PAPYRUS_VCOM2_HIZ		5

#define PAPYRUS_MV_TO_VCOMREG(MV)	((MV) / 10)

#define V3P3_EN_MASK		0x20
#define PAPYRUS_V3P3OFF_DELAY_MS	20//100

struct papyrus_sess {
	struct device *dev;
	struct i2c_client *client;
	uint8_t enable_reg_shadow;
	uint8_t vadj;
	uint8_t vcom1;
	uint8_t vcom2;
	uint8_t upseq0;
	uint8_t upseq1;
	uint8_t dwnseq0;
	uint8_t dwnseq1;
	int irq;
	struct gpio_desc *int_pin;
	struct gpio_desc *pwr_en_pin;
	struct gpio_desc *pwr_up_pin;
	struct gpio_desc *wake_up_pin;
	struct gpio_desc *vcom_ctl_pin;
	struct mutex power_lock;
	struct workqueue_struct *tmp_monitor_wq;
	struct delayed_work tmp_delay_work;
};

struct papyrus_hw_state {
	uint8_t tmst_value;
	uint8_t int_status1;
	uint8_t int_status2;
	uint8_t pg_status;
};
static bool papyrus_need_reconfig = true;

static int papyrus_hw_setreg(struct papyrus_sess *sess, uint8_t regaddr, uint8_t val)
{
	int stat;
	uint8_t txbuf[2] = { regaddr, val };
	struct i2c_msg msgs[] = {
		{
			.addr = sess->client->addr,
			.flags = 0,
			.len = 2,
			.buf = txbuf,
		}
	};

	stat = i2c_transfer(sess->client->adapter, msgs, ARRAY_SIZE(msgs));

	if (stat < 0) {
		dev_err(&sess->client->dev, "i2c send error: %d\n", stat);
	} else if (stat != ARRAY_SIZE(msgs)) {
		dev_err(&sess->client->dev, "i2c send N mismatch: %d\n", stat);
		stat = -EIO;
	} else {
		stat = 0;
	}

	return stat;
}

static int papyrus_hw_getreg(struct papyrus_sess *sess, uint8_t regaddr, uint8_t *val)
{
	int stat;
	struct i2c_msg msgs[] = {
		{
			.addr = sess->client->addr,
			.flags = 0,
			.len = 1,
			.buf = &regaddr,
		},
		{
			.addr = sess->client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = val,
		}
	};

	stat = i2c_transfer(sess->client->adapter, msgs, ARRAY_SIZE(msgs));
	if (stat < 0) {
		dev_err(&sess->client->dev, "i2c read error: %d\n", stat);
	} else if (stat != ARRAY_SIZE(msgs)) {
		dev_err(&sess->client->dev, "i2c read N mismatch: %d\n", stat);
		stat = -EIO;
	} else {
		stat = 0;
	}

	return stat;
}

static void papyrus_hw_get_int_state(struct papyrus_sess *sess, struct papyrus_hw_state *hwst)
{
	int stat;

	stat = papyrus_hw_getreg(sess, PAPYRUS_ADDR_INT_STATUS1, &hwst->int_status1);
	if (stat)
		dev_err(&sess->client->dev, "i2c error: %d\n", stat);

	stat = papyrus_hw_getreg(sess, PAPYRUS_ADDR_INT_STATUS2, &hwst->int_status2);
	if (stat)
		dev_err(&sess->client->dev, "i2c error: %d\n", stat);
}
#if 0
static bool papyrus_hw_power_ack(struct papyrus_sess *sess, int up)
{
	struct papyrus_hw_state hwst;
	int st;
	int retries_left = 10;

	if ((up & ~(1UL))) {
		dev_err(&sess->client->dev, "invalid power flag %d.\n", up);
		return false;
	}

	do {
		papyrus_hw_getreg(sess, PAPYRUS_ADDR_PG_STATUS, &hwst.pg_status);
		dev_dbg(&sess->client->dev, "hwst: tmst_val=%d, ist1=%02x, ist2=%02x, pg=%02x\n",
			hwst.tmst_value, hwst.int_status1, hwst.int_status2, hwst.pg_status);
		hwst.pg_status &= 0xfa;
		if ((hwst.pg_status == 0xfa) && (up == 1)) {
			st = 1;
		} else if ((hwst.pg_status == 0x00) && (up == 0)) {
			st = 0;
		} else {
			st = -1;	/* not settled yet */
			msleep(HW_GET_STATE_INTERVAL_MS);
		}
		retries_left--;
	} while ((st == -1) && retries_left);

	if (st == -1)
		dev_err(&sess->client->dev, "power %s settle error (PG = %02x)\n", up ? "up" : "down", hwst.pg_status);

	return (st == up);
}
#endif
static void papyrus_hw_send_powerup(struct papyrus_sess *sess)
{
	int stat = 0;

	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VADJ, sess->vadj);

	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_UPSEQ0, sess->upseq0);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_UPSEQ1, sess->upseq1);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_DWNSEQ0, sess->dwnseq0);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_DWNSEQ1, sess->dwnseq1);

	//stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VCOM1_ADJUST, sess->vcom1);
	//stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VCOM2_ADJUST, sess->vcom2);

	sess->enable_reg_shadow |= V3P3_EN_MASK;
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_ENABLE, sess->enable_reg_shadow);
	usleep_range(2 * 1000, 3 * 1000);

	sess->enable_reg_shadow = (0x80 | 0x30 | 0x0F);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_ENABLE, sess->enable_reg_shadow);
	if (stat)
		dev_err(&sess->client->dev, "i2c error: %d\n", stat);
	if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
		gpiod_direction_output(sess->pwr_up_pin, 1);

	return;
}

static void papyrus_hw_send_powerdown(struct papyrus_sess *sess)
{
	int stat = 0;

	sess->enable_reg_shadow = (0x40 | 0x20 | 0x0F);
	stat = papyrus_hw_setreg(sess, PAPYRUS_ADDR_ENABLE, sess->enable_reg_shadow);
	usleep_range(2 * 1000, 3 * 1000);
	sess->enable_reg_shadow &= ~V3P3_EN_MASK;
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_ENABLE, sess->enable_reg_shadow);
	if (stat)
		dev_err(&sess->client->dev, "i2c error: %d\n", stat);


	return;
}

static irqreturn_t papyrus_irq(int irq, void *dev_id)
{
	struct papyrus_sess *sess = dev_id;
	struct papyrus_hw_state hwst;

	papyrus_hw_get_int_state(sess, &hwst);
	dev_info(&sess->client->dev, "%s: (INT1 = %02x, INT2 = %02x)\n", __func__,
						hwst.int_status1, hwst.int_status2);
	//reset pmic
	if ((hwst.int_status2 & 0xfa) || (hwst.int_status1 & 0x04)) {
		if (sess->enable_reg_shadow | V3P3_EN_MASK)
			papyrus_hw_setreg(sess, PAPYRUS_ADDR_ENABLE, sess->enable_reg_shadow);
	}

	return IRQ_HANDLED;
}

static int papyrus_hw_get_revid(struct papyrus_sess *sess)
{
	int stat;
	uint8_t revid;

	stat = papyrus_hw_getreg(sess, PAPYRUS_ADDR_REVID, &revid);
	if (stat) {
		dev_err(&sess->client->dev, "i2c error: %d\n", stat);
		return stat;
	} else {
		return revid;
	}
}

static void papyrus_hw_arg_init(struct papyrus_sess *sess)
{
	sess->vadj = 0x03;
	sess->upseq0 = SEQ_VEE(0) | SEQ_VNEG(1) | SEQ_VPOS(2) | SEQ_VDD(3);
	sess->upseq1 = UDLY_3ms(0) | UDLY_3ms(1) | UDLY_3ms(2) | UDLY_3ms(3);
	sess->dwnseq0 = SEQ_VDD(0) | SEQ_VPOS(1) | SEQ_VNEG(2) | SEQ_VEE(3);
	sess->dwnseq1 = DDLY_6ms(0) | DDLY_6ms(1) | DDLY_6ms(2) | DDLY_6ms(3);
	sess->vcom1 = (PAPYRUS_MV_TO_VCOMREG(1560) & 0x00FF);
	sess->vcom2 = ((PAPYRUS_MV_TO_VCOMREG(1560) & 0x0100) >> 8);
}

static int papyrus_hw_init(struct papyrus_sess *sess)
{
	int stat = 0;

	if (!IS_ERR_OR_NULL(sess->pwr_en_pin)) {
		gpiod_direction_output(sess->pwr_en_pin, 1);
		usleep_range(2 * 1000, 3 * 1000);
	}
	gpiod_direction_output(sess->wake_up_pin, 0);
	/* wait to reset papyrus */
	msleep(PAPYRUS_SLEEP_MINIMUM_MS);
	gpiod_direction_output(sess->wake_up_pin, 1);
	/* power up pin no need to control, use i2c control */
	if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
		gpiod_direction_output(sess->pwr_up_pin, 0);
	gpiod_direction_output(sess->vcom_ctl_pin, 1);
	msleep(PAPYRUS_EEPROM_DELAY_MS);

	stat = papyrus_hw_get_revid(sess);
	if (stat < 0) {
		dev_err(&sess->client->dev, "get id failed");
		return stat;
	}

	dev_info(&sess->client->dev, "detected device with ID=%02x (TPS6518%dr%dp%d)\n",
		 stat, stat & 0xF, (stat & 0xC0) >> 6, (stat & 0x30) >> 4);

	return 0;
}

static void papyrus_set_vcom_voltage(struct papyrus_sess *sess, int vcom_mv)
{
	sess->vcom1 = (PAPYRUS_MV_TO_VCOMREG(vcom_mv) & 0x00FF);
	sess->vcom2 = ((PAPYRUS_MV_TO_VCOMREG(vcom_mv) & 0x0100) >> 8);
}

static int papyrus_hw_read_temperature(struct ebc_pmic *pmic, int *t)
{
	struct papyrus_sess *sess = (struct papyrus_sess *)pmic->drvpar;
	int stat;
	uint8_t tb;
#if 0
	int ntries = 50;

	stat = papyrus_hw_setreg(sess, PAPYRUS_ADDR_TMST1, 0x80);
	if (stat)
		return stat;
	do {
		stat = papyrus_hw_getreg(sess, PAPYRUS_ADDR_TMST1, &tb);
	} while (!stat && ntries-- && (((tb & 0x20) == 0) || (tb & 0x80)));

	if (stat)
		return stat;

	msleep(PAPYRUS_TEMP_READ_TIME_MS);
#endif
	stat = papyrus_hw_getreg(sess, PAPYRUS_ADDR_TMST_VALUE, &tb);
	*t = (int)(int8_t)tb;

	return stat;
}

static void papyrus_hw_power_req(struct ebc_pmic *pmic, bool up)
{
	struct papyrus_sess *sess = (struct papyrus_sess *)pmic->drvpar;

	if (up)
		mutex_lock(&sess->power_lock);
	if (papyrus_need_reconfig) {
		if (up) {
			papyrus_hw_send_powerup(sess);
			//papyrus_hw_power_ack(sess, up);
			enable_irq(sess->irq);
		} else {
			disable_irq(sess->irq);
			papyrus_hw_send_powerdown(sess);
			//papyrus_hw_power_ack(sess, up);
		}
		papyrus_need_reconfig = false;
	} else {
		if (up) {
			if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
				gpiod_direction_output(sess->pwr_up_pin, 1);
			enable_irq(sess->irq);
		} else {
			disable_irq(sess->irq);
			if (!IS_ERR_OR_NULL(sess->pwr_up_pin))
				gpiod_direction_output(sess->pwr_up_pin, 0);
		}
	}
	if (!up)
		mutex_unlock(&sess->power_lock);
	return;
}

static int papyrus_hw_vcom_get(struct ebc_pmic *pmic)
{
	struct papyrus_sess *sess = (struct papyrus_sess *)pmic->drvpar;
	uint8_t rev_val = 0;
	int stat = 0;
	int read_vcom_mv = 0;

	mutex_lock(&sess->power_lock);
	// VERIFICATION
	gpiod_direction_output(sess->wake_up_pin, 0);
	msleep(10);
	gpiod_direction_output(sess->wake_up_pin, 1);
	msleep(10);
	read_vcom_mv = 0;
	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_VCOM1_ADJUST, &rev_val);
	read_vcom_mv += rev_val;
	stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_VCOM2_ADJUST, &rev_val);
	read_vcom_mv += ((rev_val & 0x0001) << 8);

	if (stat)
		dev_err(&sess->client->dev, "papyrus: I2C error: %d\n", stat);
	mutex_unlock(&sess->power_lock);

	return read_vcom_mv * 10;
}

static int papyrus_hw_vcom_set(struct ebc_pmic *pmic, int vcom_mv)
{
	struct papyrus_sess *sess = (struct papyrus_sess *)pmic->drvpar;
	uint8_t rev_val = 0;
	int stat = 0;

	mutex_lock(&sess->power_lock);
	gpiod_direction_output(sess->wake_up_pin, 1);
	msleep(10);
	// Set vcom voltage
	papyrus_set_vcom_voltage(sess, vcom_mv);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VCOM1_ADJUST, sess->vcom1);
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VCOM2_ADJUST, sess->vcom2);

	// PROGRAMMING
	sess->vcom2 |= 1 << PAPYRUS_VCOM2_PROG;
	stat |= papyrus_hw_setreg(sess, PAPYRUS_ADDR_VCOM2_ADJUST, sess->vcom2);
	rev_val = 0;
	while (!(rev_val & (1 << PAPYRUS_INT_STATUS1_PRGC))) {
		stat |= papyrus_hw_getreg(sess, PAPYRUS_ADDR_INT_STATUS1, &rev_val);
		if (stat)
			break;
		msleep(50);
	}

	if (stat)
		dev_err(&sess->client->dev, "papyrus: I2C error: %d\n", stat);
	mutex_unlock(&sess->power_lock);

	return 0;
}

static void papyrus_pm_sleep(struct ebc_pmic *pmic)
{
	struct papyrus_sess *s = (struct papyrus_sess *)pmic->drvpar;

	cancel_delayed_work_sync(&s->tmp_delay_work);

	mutex_lock(&s->power_lock);
	gpiod_direction_output(s->vcom_ctl_pin, 0);
	gpiod_direction_output(s->wake_up_pin, 0);
	if (!IS_ERR_OR_NULL(s->pwr_en_pin))
		gpiod_direction_output(s->pwr_en_pin, 0);
	papyrus_need_reconfig = true;
	mutex_unlock(&s->power_lock);
}

static void papyrus_pm_resume(struct ebc_pmic *pmic)
{
	struct papyrus_sess *s = (struct papyrus_sess *)pmic->drvpar;

	mutex_lock(&s->power_lock);
	if (!IS_ERR_OR_NULL(s->pwr_en_pin)) {
		gpiod_direction_output(s->pwr_en_pin, 1);
		usleep_range(2 * 1000, 3 * 1000);
	}
	gpiod_direction_output(s->wake_up_pin, 1);
	gpiod_direction_output(s->vcom_ctl_pin, 1);
	usleep_range(2 * 1000, 3 * 1000);
	mutex_unlock(&s->power_lock);

	//trigger temperature measurement
	papyrus_hw_setreg(s, PAPYRUS_ADDR_TMST1, 0x80);
	queue_delayed_work(s->tmp_monitor_wq, &s->tmp_delay_work,
			   msecs_to_jiffies(10000));
}

static void papyrus_tmp_work(struct work_struct *work)
{
	struct papyrus_sess *s =
		container_of(work, struct papyrus_sess, tmp_delay_work.work);

	//trigger temperature measurement
	papyrus_hw_setreg(s, PAPYRUS_ADDR_TMST1, 0x80);

	queue_delayed_work(s->tmp_monitor_wq, &s->tmp_delay_work,
			   msecs_to_jiffies(10000));
}

static int papyrus_probe(struct ebc_pmic *pmic, struct i2c_client *client)
{
	struct papyrus_sess *sess;
	int stat;

	sess = devm_kzalloc(&client->dev, sizeof(*sess), GFP_KERNEL);
	if (!sess) {
		dev_err(&client->dev, "%s:%d: kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}
	sess->client = client;
	mutex_init(&sess->power_lock);
	papyrus_hw_arg_init(sess);

	sess->pwr_en_pin = devm_gpiod_get_optional(&client->dev, "poweren", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(sess->pwr_en_pin)) {
		dev_err(&client->dev, "tsp65185: failed to find poweren pin, no defined\n");
	}

	sess->wake_up_pin = devm_gpiod_get_optional(&client->dev, "wakeup", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(sess->wake_up_pin)) {
		dev_err(&client->dev, "tsp65185: failed to find wakeup pin\n");
		return -ENOMEM;
	}

	sess->vcom_ctl_pin = devm_gpiod_get_optional(&client->dev, "vcomctl", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(sess->vcom_ctl_pin)) {
		dev_err(&client->dev, "tsp65185: failed to find vcom_ctl pin\n");
		return -ENOMEM;
	}

	sess->pwr_up_pin = devm_gpiod_get_optional(&client->dev, "powerup", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(sess->pwr_up_pin))
		dev_err(&client->dev, "tsp65185: no pwr_up pin find\n");

	sess->int_pin = devm_gpiod_get(&client->dev, "int", GPIOD_IN);
	if (IS_ERR(sess->int_pin)) {
		dev_err(&client->dev, "tsp65185: failed to find int pin\n");
		return PTR_ERR(sess->int_pin);
	}
	sess->irq = gpiod_to_irq(sess->int_pin);
	if (sess->irq < 0) {
		dev_err(&client->dev, "Unable to get irq number for int pin\n");
		return sess->irq;
	}

	irq_set_status_flags(sess->irq, IRQ_NOAUTOEN);
	stat = devm_request_threaded_irq(&client->dev, sess->irq, NULL, papyrus_irq,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					"tps65185", sess);
	if (stat) {
		dev_err(&client->dev,
			"Failed to enable IRQ, error: %d\n", stat);
		return stat;
	}

	sess->tmp_monitor_wq = alloc_ordered_workqueue("%s",
			WQ_MEM_RECLAIM | WQ_FREEZABLE, "tps-tmp-monitor-wq");
	INIT_DELAYED_WORK(&sess->tmp_delay_work, papyrus_tmp_work);
	queue_delayed_work(sess->tmp_monitor_wq, &sess->tmp_delay_work,
			   msecs_to_jiffies(10000));

	stat = papyrus_hw_init(sess);
	if (stat)
		return stat;

	sess->enable_reg_shadow = 0;
	stat = papyrus_hw_setreg(sess, PAPYRUS_ADDR_ENABLE, sess->enable_reg_shadow);
	if (stat)
		return stat;

	//trigger temperature measurement
	papyrus_hw_setreg(sess, PAPYRUS_ADDR_TMST1, 0x80);

	pmic->drvpar = sess;

	pmic->pmic_get_vcom = papyrus_hw_vcom_get;
	pmic->pmic_set_vcom = papyrus_hw_vcom_set;
	pmic->pmic_pm_resume = papyrus_pm_resume;
	pmic->pmic_pm_suspend = papyrus_pm_sleep;
	pmic->pmic_power_req = papyrus_hw_power_req;
	pmic->pmic_read_temperature = papyrus_hw_read_temperature;

	return 0;
}

static int tps65185_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ebc_pmic *pmic;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c check functionality failed.");
		return -ENODEV;
	}

	pmic = devm_kzalloc(&client->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic) {
		dev_err(&client->dev, "%s:%d: kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	if (0 != papyrus_probe(pmic, client)) {
		dev_err(&client->dev, "tps65185 hw init failed.");
		return -ENODEV;
	}

	pmic->dev = &client->dev;
	sprintf(pmic->pmic_name, "tps65185");
	i2c_set_clientdata(client, pmic);

	dev_info(&client->dev, "tps65185 probe ok.\n");

	return 0;
}

static int tps65185_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id tps65185_id[] = {
	{ "tps65185", 0 },
	{ }
};

static const struct of_device_id tps65185_dt_ids[] = {
	{ .compatible = "ti,tps65185", },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, tps65185_dt_ids);
static struct i2c_driver tps65185_driver = {
	.probe	= tps65185_probe,
	.remove 	= tps65185_remove,
	.id_table	= tps65185_id,
	.driver = {
		.of_match_table = tps65185_dt_ids,
		.name	  = "tps65185",
		.owner	  = THIS_MODULE,
	},
};

module_i2c_driver(tps65185_driver);

MODULE_DESCRIPTION("ti tps65185 pmic");
MODULE_LICENSE("GPL");
