/*
 * tps65910-core.c -- Multifunction core driver for  TPS65910x chips
 *
 * Copyright (C) 2010 Mistral solutions Pvt Ltd <www.mistralsolutions.com>
 *
 * Based on twl-core.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <linux/regulator/machine.h>

#include <linux/i2c.h>
#include <linux/i2c/tps65910.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif
#define TPS65910_SPEED 	400 * 1000


#define DRIVER_NAME			"tps659102"

#if defined(CONFIG_GPIO_TPS65910)
#define tps65910_has_gpio()  		true
#else
#define tps65910_has_gpio()  		false
#endif

#if defined(CONFIG_REGULATOR_TPS65910)
#define tps65910_has_regulator()     	true
#else
#define tps65910_has_regulator()     	false
#endif

#if defined(CONFIG_RTC_DRV_TPS65910)
#define tps65910_has_rtc()   		true
#else
#define tps65910_has_rtc()   		false
#endif

#define TPS65910_GENERAL			0
#define TPS65910_SMARTREFLEX		1


struct tps65910_platform_data *gtps65910_platform = NULL;

enum tps65910x_model {
	TPS65910,   	/* TI processors OMAP3 family */
	TPS659101,  	/* Samsung - S5PV210, S5PC1xx */
	TPS659102,	/* Samsung - S3C64xx */
	TPS659103,	/* Reserved */
	TPS659104,	/* Reserved */
	TPS659105,	/* TI processors - DM643x, DM644x */
	TPS659106,	/* Reserved */
	TPS659107,	/* Reserved */
	TPS659108,	/* Reserved */
	TPS659109,	/* Freescale - i.MX51 */

};

static bool inuse;
static struct work_struct core_work;
static struct mutex work_lock;

/* Structure for each TPS65910 Slave */
struct tps65910_client {
	struct i2c_client *client;
	u8 address;
	/* max numb of i2c_msg required for read = 2 */
	struct i2c_msg xfer_msg[2];
	/* To lock access to xfer_msg */
	struct mutex xfer_lock;
};
static struct tps65910_client tps65910_modules[TPS65910_NUM_SLAVES];

/* bbch = Back-up battery charger control register */
int tps65910_enable_bbch(u8 voltage)
{
	u8 val = 0;
	int err;

	if (voltage == TPS65910_BBSEL_3P0 || voltage == TPS65910_BBSEL_2P52 ||
			voltage == TPS65910_BBSEL_3P15 ||
			voltage == TPS65910_BBSEL_VBAT) {
		val = (voltage | TPS65910_BBCHEN);
		err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val,
				TPS65910_REG_BBCH);
		if (err) {
			printk(KERN_ERR "Unable write TPS65910_REG_BBCH reg\n");
			return -EIO;
		}
	} else {
		printk(KERN_ERR"Invalid argumnet for %s \n", __func__);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(tps65910_enable_bbch);

int tps65910_disable_bbch(void)
{
	u8 val = 0;
	int err;

	err = tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_BBCH);

	if (!err) {
		val &= ~TPS65910_BBCHEN;

		err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val,
				TPS65910_REG_BBCH);
		if (err) {
			printk(KERN_ERR "Unable write TPS65910_REG_BBCH \
					reg\n");
			return -EIO;
		}
	} else {
		printk(KERN_ERR "Unable to read TPS65910_REG_BBCH reg\n");
		return -EIO;
	}
	return 0;
}
EXPORT_SYMBOL(tps65910_disable_bbch);

int tps65910_i2c_read_u8(u8 mod_no, u8 *value, u8 reg)
{
	struct tps65910_client *tps65910;
	int ret;
	
	switch (mod_no) {
	case TPS65910_I2C_ID0:
		tps65910 = &tps65910_modules[0];
		tps65910->address = TPS65910_I2C_ID0;
		break;
	case TPS65910_I2C_ID1:
		tps65910 = &tps65910_modules[1];
		tps65910->address = TPS65910_I2C_ID1;
		break;
	default:
		printk(KERN_ERR "Invalid Slave address for TPS65910\n");
		return -ENODEV;
	}
	
	ret = i2c_master_reg8_recv(tps65910->client, reg, (char *)value, 1, TPS65910_SPEED);
	DBG("%s: ret=%d, slave_addr=0x%x, reg=0x%x, value=0x%x\n", __FUNCTION__, (ret > 0 ? 0: -EINVAL), mod_no, reg, *value);

	return (ret > 0 ? 0: -EINVAL);
}
EXPORT_SYMBOL(tps65910_i2c_read_u8);

int tps65910_i2c_write_u8(u8 slave_addr, u8 value, u8 reg)
{
	struct tps65910_client *tps65910;
	int ret;

	switch (slave_addr) {
	case TPS65910_I2C_ID0:
		tps65910 = &tps65910_modules[0];
		tps65910->address = TPS65910_I2C_ID0;
		break;
	case TPS65910_I2C_ID1:
		tps65910 = &tps65910_modules[1];
		tps65910->address = TPS65910_I2C_ID1;
		break;
	default:
		printk(KERN_ERR "Invalid Slave address for TPS65910\n");
		return -ENODEV;
	}

	ret = i2c_master_reg8_send(tps65910->client, reg, (char *)&value, 1, TPS65910_SPEED);
	DBG("%s: ret=%d, slave_addr=0x%x, reg=0x%x, value=0x%x\n", __FUNCTION__, ret, slave_addr, reg, value);

	if (ret < 0)
		return -EIO;
	else
		return 0;
}
EXPORT_SYMBOL(tps65910_i2c_write_u8);


int tps65910_enable_irq(int irq)
{
	u8  mask = 0x00;

	if (irq > 7) {
		irq -= 8;
		tps65910_i2c_read_u8(TPS65910_I2C_ID0,
				&mask, TPS65910_REG_INT_MSK2);
		mask &= ~(1 << irq);
		return tps65910_i2c_write_u8(TPS65910_I2C_ID0,
				mask, TPS65910_REG_INT_MSK2);
	} else {
		tps65910_i2c_read_u8(TPS65910_I2C_ID0,
				&mask, TPS65910_REG_INT_MSK);
		mask &= ~(1 << irq);
		return tps65910_i2c_write_u8(TPS65910_I2C_ID0,
				mask, TPS65910_REG_INT_MSK);
	}
}
EXPORT_SYMBOL(tps65910_enable_irq);

int tps65910_disable_irq(int irq)
{
	u8  mask = 0x00;

	if (irq > 7) {
		irq -= 8;
		tps65910_i2c_read_u8(TPS65910_I2C_ID0,
				&mask, TPS65910_REG_INT_MSK2);
		mask |= (1 << irq);
		return tps65910_i2c_write_u8(TPS65910_I2C_ID0,
				mask, TPS65910_REG_INT_MSK2);
	} else {
		tps65910_i2c_read_u8(TPS65910_I2C_ID0,
				&mask, TPS65910_REG_INT_MSK);
		mask = (1 << irq);
		return tps65910_i2c_write_u8(TPS65910_I2C_ID0,
				mask, TPS65910_REG_INT_MSK);
	}
}
EXPORT_SYMBOL(tps65910_disable_irq);

int tps65910_add_irq_work(int irq,
		void (*handler)(void *data))
{
	int ret = 0;
	gtps65910_platform->handlers[irq] = handler;
	ret = tps65910_enable_irq(irq);

	return ret;
}
EXPORT_SYMBOL(tps65910_add_irq_work);

int tps65910_remove_irq_work(int irq)
{
	int ret = 0;
	ret = tps65910_disable_irq(irq);
	gtps65910_platform->handlers[irq] = NULL;
	return ret;
}
EXPORT_SYMBOL(tps65910_remove_irq_work);

static void tps65910_core_work(struct work_struct *work)
{
	/* Read the status register and take action  */
	u8	status = 0x00;
	u8	status2 = 0x00;
	u8 	mask = 0x00;
	u8 	mask2 = 0x00;
	u16 isr = 0x00;
	u16 irq = 0;
	void	(*handler)(void *data) = NULL;

	DBG("Enter::%s %d\n",__FUNCTION__,__LINE__);
	mutex_lock(&work_lock);
	while (1) {
		tps65910_i2c_read_u8(TPS65910_I2C_ID0, &status2,
				TPS65910_REG_INT_STS2);
		tps65910_i2c_read_u8(TPS65910_I2C_ID0, &mask2,
				TPS65910_REG_INT_MSK2);
		status2 &= (~mask2);
		isr = (status2 << 8);
		tps65910_i2c_read_u8(TPS65910_I2C_ID0, &status,
				TPS65910_REG_INT_STS);
		tps65910_i2c_read_u8(TPS65910_I2C_ID0, &mask,
				TPS65910_REG_INT_MSK);
		status &= ~(mask);
		isr |= status;
		if (!isr)
			break;

		while (isr) {
			irq = fls(isr) - 1;
			isr &= ~(1 << irq);
			handler = gtps65910_platform->handlers[irq];
			if (handler)
				handler(gtps65910_platform);
		}
	}
	enable_irq(gtps65910_platform->irq_num);
	mutex_unlock(&work_lock);
}


static irqreturn_t tps65910_isr(int irq,  void *data)
{
	disable_irq_nosync(irq);
	(void) schedule_work(&core_work);
	return IRQ_HANDLED;
}


static struct device *add_numbered_child(unsigned chip, const char *name,
	int num, void *pdata, unsigned pdata_len, bool can_wakeup, int irq)
{

	struct platform_device  *pdev;
	struct tps65910_client  *tps65910 = &tps65910_modules[chip];
	int  status;

	pdev = platform_device_alloc(name, num);
	if (!pdev) {
		dev_dbg(&tps65910->client->dev, "can't alloc dev\n");
		status = -ENOMEM;
		goto err;
	}
	device_init_wakeup(&pdev->dev, can_wakeup);
	pdev->dev.parent = &tps65910->client->dev;

	if (pdata) {
		status = platform_device_add_data(pdev, pdata, pdata_len);
		if (status < 0) {
			dev_dbg(&pdev->dev, "can't add platform_data\n");
			goto err;
		}
	}
	status = platform_device_add(pdev);

err:
	if (status < 0) {
		platform_device_put(pdev);
		dev_err(&tps65910->client->dev, "can't add %s dev\n", name);
		return ERR_PTR(status);
	}
	return &pdev->dev;

}

static inline struct device *add_child(unsigned chip, const char *name,
		void *pdata, unsigned pdata_len,
		bool can_wakeup, int irq)
{
	return add_numbered_child(chip, name, -1, pdata, pdata_len,
			can_wakeup, irq);
}

static
struct device *add_regulator_linked(int num, struct regulator_init_data *pdata,
		struct regulator_consumer_supply *consumers,
		unsigned num_consumers)
{
	/* regulator framework demands init_data */
	if (!pdata)
		return NULL;

	if (consumers) {
		pdata->consumer_supplies = consumers;
		pdata->num_consumer_supplies = num_consumers;
	}
	
	return add_numbered_child(TPS65910_GENERAL, "tps65910_regulator", num,
			pdata, sizeof(*pdata), false, TPS65910_HOST_IRQ);
}

	static struct device *
add_regulator(int num, struct regulator_init_data *pdata)
{
	return add_regulator_linked(num, pdata, NULL, 0);
}

static int
add_children(struct tps65910_platform_data *pdata, unsigned long features)
{
	int 		status;
	struct device   *child;

	struct platform_device  *pdev = NULL;

	DBG("cwz add_children: tps65910 add children.\n");

	if (tps65910_has_gpio() && (pdata->gpio != NULL)) {

		pdev = platform_device_alloc("tps65910_gpio", -1);
		if (!pdev) {
			status = -ENOMEM;
			goto err;
		}
		pdev->dev.parent = &tps65910_modules[0].client->dev;
		device_init_wakeup(&pdev->dev, 0);
		if (pdata) {
			status = platform_device_add_data(pdev, pdata,
							sizeof(*pdata));
			if (status < 0) {
				dev_dbg(&pdev->dev,
				"can't add platform_data\n");
				goto err;
			}
		}
	}
	if (tps65910_has_rtc()) {
		child = add_child(TPS65910_GENERAL, "tps65910_rtc",
				NULL, 0, true, pdata->irq_num);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (tps65910_has_regulator()) {
		child = add_regulator(TPS65910_VIO, pdata->vio);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VDD1, pdata->vdd1);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VDD2, pdata->vdd2);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VDD3, pdata->vdd3);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VDIG1, pdata->vdig1);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VDIG2, pdata->vdig2);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VAUX33, pdata->vaux33);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VMMC, pdata->vmmc);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VAUX1, pdata->vaux1);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VAUX2, pdata->vaux2);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VDAC, pdata->vdac);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TPS65910_VPLL, pdata->vpll);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}
	
	return 0;
err:
	return -1;
}

static int tps65910_remove(struct i2c_client *client)
{
	unsigned i;

	for (i = 0; i < TPS65910_NUM_SLAVES; i++) {

		struct tps65910_client *tps65910 = &tps65910_modules[i];

		if (tps65910->client && tps65910->client != client)
			i2c_unregister_device(tps65910->client);

		tps65910_modules[i].client = NULL;
	}
	inuse = false;
	return 0;
}

static int __init
tps65910_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int	 status;
	unsigned i;
	struct tps65910_platform_data	*pdata;
 
	pdata = client->dev.platform_data;
	gtps65910_platform = pdata;

	DBG("cwz: tps65910_i2c_probe\n");
	
	if (!pdata) {
		dev_dbg(&client->dev, "no platform data?\n");
		return -EINVAL;
	}

 	if (!i2c_check_functionality(client->adapter,I2C_FUNC_I2C)) {
		dev_dbg(&client->dev, "can't talk I2C?\n");
		return -EIO;
	}

	if (inuse) {
		dev_dbg(&client->dev, "driver is already in use\n");
		return -EBUSY;
	} 
	for (i = 0; i < TPS65910_NUM_SLAVES; i++) {

		struct tps65910_client  *tps65910 = &tps65910_modules[i];

		tps65910->address = client->addr;

		if (i == 0)
			tps65910->client = client;
		else {
			tps65910->client = i2c_new_dummy(client->adapter,
					tps65910->address);

			if (!tps65910->client) {
				dev_err(&client->dev,
						"can't attach client %d\n", i);
				status = -ENOMEM;
				goto fail;
			}
		}
		mutex_init(&tps65910->xfer_lock);
	} 

	inuse = true;

	if (pdata->board_tps65910_config != NULL)
		pdata->board_tps65910_config(pdata);

	if (pdata->irq_num) {
		/* TPS65910 power ON interrupt(s) would have already been
		 * occurred, so immediately after request_irq the control will
		 * be transferred to tps65910_isr, if we do core_work
		 * initialization after requesting IRQ, the system crashes
		 * and does not boot; to avoid this we do core_work
		 * initialization before requesting IRQ
		 */
		mutex_init(&work_lock);

		if(gpio_request(client->irq, "tps65910 irq"))
		{
			dev_err(&client->dev, "gpio request fail\n");
			gpio_free(client->irq);
			goto fail;
		}
		
		pdata->irq_num = gpio_to_irq(client->irq);
		gpio_pull_updown(client->irq,GPIOPullUp);

		status = request_irq(pdata->irq_num, tps65910_isr,
					IRQF_TRIGGER_FALLING, client->dev.driver->name, pdata);
		if (status < 0) {
			pr_err("tps65910: could not claim irq%d: %d\n",
					pdata->irq_num,	status);
			goto fail;
		}
		enable_irq_wake(pdata->irq_num);
		INIT_WORK(&core_work, tps65910_core_work);
	}

	status = add_children(pdata, 0x00);
	if (status < 0)
		goto fail;

	return 0;

fail:
	if (status < 0)
		tps65910_remove(client);

	return status;
}


static int tps65910_i2c_remove(struct i2c_client *client)
{
	unsigned i;

	for (i = 0; i < TPS65910_NUM_SLAVES; i++) {

		struct tps65910_client	*tps65910 = &tps65910_modules[i];

		if (tps65910->client && tps65910->client != client)
			i2c_unregister_device(tps65910->client);

		tps65910_modules[i].client = NULL;
	}
	inuse = false;
	return 0;
}

/* chip-specific feature flags, for i2c_device_id.driver_data */
static const struct i2c_device_id tps65910_i2c_ids[] = {
	{ "tps65910", TPS65910 },
	{ "tps659101", TPS659101 },
	{ "tps659102", TPS659102 },
	{ "tps659103", TPS659103 },
	{ "tps659104", TPS659104 },
	{ "tps659105", TPS659105 },
	{ "tps659106", TPS659106 },
	{ "tps659107", TPS659107 },
	{ "tps659108", TPS659108 },
	{ "tps659109", TPS659109 },
	{/* end of list */ },
};
MODULE_DEVICE_TABLE(i2c, tps65910_i2c_ids);

/* One Client Driver ,3 Clients - Regulator, RTC , GPIO */
static struct i2c_driver tps65910_i2c_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.id_table       = tps65910_i2c_ids,
	.probe          = tps65910_i2c_probe,
	.remove         = __devexit_p(tps65910_i2c_remove),
};

static int __init tps65910_init(void)
{
	int res;

	res = i2c_add_driver(&tps65910_i2c_driver);
	if (res < 0) {
		pr_err(DRIVER_NAME ": driver registration failed\n");
		return res;
	}

	return 0;
}
subsys_initcall_sync(tps65910_init);

static void __exit tps65910_exit(void)
{
	i2c_del_driver(&tps65910_i2c_driver);
}
module_exit(tps65910_exit);


#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int proc_tps65910_show(struct seq_file *s, void *v)
{
    u8 val = 0;
	
	seq_printf(s, "\n\nTPS65910 Registers is:\n");

	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_REF);
	seq_printf(s, "REF_REG=0x%x, Value=0x%x\n", TPS65910_REG_REF, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VRTC);
	seq_printf(s, "VRTC_REG=0x%x, Value=0x%x\n", TPS65910_REG_VRTC, val);
	
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDD1);
	seq_printf(s, "VDD1_REG=0x%x, Value=0x%x\n", TPS65910_REG_VDD1, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDD1_OP);
	seq_printf(s, "VDD1_OP_REG=0x%x, Value=0x%x\n", TPS65910_REG_VDD1_OP, val);

	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDD2);
	seq_printf(s, "VDD2_REG=0x%x, Value=0x%x\n", TPS65910_REG_VDD2, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDD2_OP);
	seq_printf(s, "VDD2_OP_REG=0x%x, Value=0x%x\n", TPS65910_REG_VDD2_OP, val);

	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VIO);
	seq_printf(s, "VIO_REG=0x%x, Value=0x%x\n", TPS65910_REG_VIO, val);

	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDIG1);
	seq_printf(s, "VDIG1_REG=0x%x, Value=0x%x\n", TPS65910_REG_VDIG1, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDIG2);
	seq_printf(s, "VDIG2_REG=0x%x, Value=0x%x\n", TPS65910_REG_VDIG2, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VAUX1);
	seq_printf(s, "VAUX1_REG=0x%x, Value=0x%x\n", TPS65910_REG_VAUX1, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VAUX2);
	seq_printf(s, "VAUX2_REG=0x%x, Value=0x%x\n", TPS65910_REG_VAUX2, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VAUX33);
	seq_printf(s, "VAUX33_REG=0x%x, Value=0x%x\n", TPS65910_REG_VAUX33, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VMMC);
	seq_printf(s, "VMMC_REG=0x%x, Value=0x%x\n", TPS65910_REG_VMMC, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VPLL);
	seq_printf(s, "VPLL_REG=0x%x, Value=0x%x\n", TPS65910_REG_VPLL, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDAC);
	seq_printf(s, "VDAC_REG=0x%x, Value=0x%x\n", TPS65910_REG_VDAC, val);

	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_DEVCTRL);
	seq_printf(s, "DEVCTRL_REG=0x%x, Value=0x%x\n", TPS65910_REG_DEVCTRL, val);
	tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_DEVCTRL2);
	seq_printf(s, "DEVCTRL2_REG=0x%x, Value=0x%x\n", TPS65910_REG_DEVCTRL2, val);

#if 0	//	cwz 1 test vcore
{
    struct regulator *vldo;
	
	vldo = regulator_get(NULL, "vcore");
	if (!IS_ERR(vldo))
	{
		int uV = 0;
#if 0		
		seq_printf(s, "Set VCORE.\n");
		regulator_set_voltage(vldo,1350000,1350000);
#endif
		uV = regulator_get_voltage(vldo);
		seq_printf(s, "Get VCORE=%d(uV).\n", uV);
	}
}
#endif

#if 0
{
    struct regulator *vldo;

#if 1
	vldo = regulator_get(NULL, "vaux1");
	if (!IS_ERR(vldo))
	{		
		seq_printf(s, "Disable VAUX1.\n");
		regulator_disable(vldo);
	}
#endif

#if 1
	vldo = regulator_get(NULL, "vdig1");
	if (!IS_ERR(vldo))
	{		
		seq_printf(s, "Disable VDIG1.\n");
		regulator_disable(vldo);
	}

	vldo = regulator_get(NULL, "vdig2");
	if (!IS_ERR(vldo))
	{		
		seq_printf(s, "Disable VDIG2.\n");
		regulator_disable(vldo);
	}
#endif

#if 0	//	fih board is for hdmi
	vldo = regulator_get(NULL, "vdac");
	if (!IS_ERR(vldo))
	{		
		seq_printf(s, "Disable VDAC.\n");
		regulator_disable(vldo);
	}
#endif

#if 1
	vldo = regulator_get(NULL, "vaux2");
	if (!IS_ERR(vldo))
	{		
		seq_printf(s, "Disable VAUX2.\n");
		regulator_disable(vldo);
	}
#endif
}
#endif

	return 0;
}

static int proc_tps65910_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_tps65910_show, NULL);
}

static const struct file_operations proc_tps65910_fops = {
	.open		= proc_tps65910_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_tps65910_init(void)
{
	proc_create("tps65910", 0, NULL, &proc_tps65910_fops);
	return 0;
}
late_initcall(proc_tps65910_init);
#endif /* CONFIG_PROC_FS */

MODULE_AUTHOR("cwz <cwz@rock-chips.com>");
MODULE_DESCRIPTION("I2C Core interface for TPS65910");
MODULE_LICENSE("GPL");
