/* 
 * driver/mfd/ricoh619.c
 *
 * Core driver implementation to access RICOH RC5T619 power management chip.
 *
 * Copyright (C) 2012-2013 RICOH COMPANY,LTD
 *
 * Based on code
 *	Copyright (C) 2011 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
/*#define DEBUG			1*/
/*#define VERBOSE_DEBUG		1*/
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ricoh619.h>
#include <linux/power/ricoh619_battery.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/syscore_ops.h>

struct ricoh619 *g_ricoh619;
struct sleep_control_data {
	u8 reg_add;
};
static struct mfd_cell ricoh619s[] = {
	{
		.name = "ricoh619-regulator",
	},
	{
		.name = "ricoh619-battery",
	},
	{
		.name = "ricoh619-rtc",
	},
	{
		.name = "ricoh619-pwrkey",
	},
};


#define SLEEP_INIT(_id, _reg)		\
	[RICOH619_DS_##_id] = {.reg_add = _reg}
/*
static struct sleep_control_data sleep_data[] = {
	SLEEP_INIT(DC1, 0x16),
	SLEEP_INIT(DC2, 0x17),
	SLEEP_INIT(DC3, 0x18),
	SLEEP_INIT(DC4, 0x19),
	SLEEP_INIT(DC5, 0x1A),
	SLEEP_INIT(LDO1, 0x1B),
	SLEEP_INIT(LDO2, 0x1C),
	SLEEP_INIT(LDO3, 0x1D),
	SLEEP_INIT(LDO4, 0x1E),
	SLEEP_INIT(LDO5, 0x1F),
	SLEEP_INIT(LDO6, 0x20),
	SLEEP_INIT(LDO7, 0x21),
	SLEEP_INIT(LDO8, 0x22),
	SLEEP_INIT(LDO9, 0x23),
	SLEEP_INIT(LDO10, 0x24),
	SLEEP_INIT(PSO0, 0x25),
	SLEEP_INIT(PSO1, 0x26),
	SLEEP_INIT(PSO2, 0x27),
	SLEEP_INIT(PSO3, 0x28),
	SLEEP_INIT(PSO4, 0x29),
	SLEEP_INIT(LDORTC1, 0x2A),
};
*/
static inline int __ricoh619_read(struct i2c_client *client,
				  u8 reg, uint8_t *val)
{
	int ret =0;
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading at 0x%02x %d\n", reg,ret);
		return ret;
	}

	*val = (uint8_t)ret;
	dev_dbg(&client->dev, "ricoh619: reg read  reg=%x, val=%x\n",
				reg, *val);
	return 0;
}

static inline int __ricoh619_bulk_reads(struct i2c_client *client, u8 reg,
				int len, uint8_t *val)
{
	int ret;
	int i;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading from 0x%02x %dn", reg,ret);
		return ret;
	}
	for (i = 0; i < len; ++i) {
		dev_dbg(&client->dev, "ricoh619: reg read  reg=%x, val=%x\n",
				reg + i, *(val + i));
	}
	return 0;
}

static inline int __ricoh619_write(struct i2c_client *client,
				 u8 reg, uint8_t val)
{
	int ret=0;

	dev_dbg(&client->dev, "ricoh619: reg write  reg=%x, val=%x\n",
				reg, val);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writing 0x%02x to 0x%02x\n",
				val, reg);
		return ret;
	}
	return 0;
}

static inline int __ricoh619_bulk_writes(struct i2c_client *client, u8 reg,
				  int len, uint8_t *val)
{
	int ret=0;
	int i;

	for (i = 0; i < len; ++i) {
		dev_dbg(&client->dev, "ricoh619: reg write  reg=%x, val=%x\n",
				reg + i, *(val + i));
	}

	ret = i2c_smbus_write_i2c_block_data(client, reg, len, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writings to 0x%02x\n", reg);
		return ret;
	}
	return 0;
}

static inline int set_bank_ricoh619(struct device *dev, int bank)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret;

	if (bank != (bank & 1))
		return -EINVAL;
	if (bank == ricoh619->bank_num)
		return 0;
	ret = __ricoh619_write(to_i2c_client(dev), RICOH619_REG_BANKSEL, bank);
	if (!ret)
		ricoh619->bank_num = bank;

	return ret;
}

int ricoh619_write(struct device *dev, u8 reg, uint8_t val)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 0);
	if( !ret )
		ret = __ricoh619_write(to_i2c_client(dev), reg, val);
	mutex_unlock(&ricoh619->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_write);

int ricoh619_write_bank1(struct device *dev, u8 reg, uint8_t val)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 1);
	if( !ret ) 
		ret = __ricoh619_write(to_i2c_client(dev), reg, val);
	mutex_unlock(&ricoh619->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_write_bank1);

int ricoh619_bulk_writes(struct device *dev, u8 reg, u8 len, uint8_t *val)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 0);
	if( !ret )
		ret = __ricoh619_bulk_writes(to_i2c_client(dev), reg, len, val);
	mutex_unlock(&ricoh619->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_bulk_writes);

int ricoh619_bulk_writes_bank1(struct device *dev, u8 reg, u8 len, uint8_t *val)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 1);
	if( !ret ) 
		ret = __ricoh619_bulk_writes(to_i2c_client(dev), reg, len, val);
	mutex_unlock(&ricoh619->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_bulk_writes_bank1);

int ricoh619_read(struct device *dev, u8 reg, uint8_t *val)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 0);
	if( !ret )
		ret = __ricoh619_read(to_i2c_client(dev), reg, val);
	mutex_unlock(&ricoh619->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_read);

int ricoh619_read_bank1(struct device *dev, u8 reg, uint8_t *val)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 1);
	if( !ret )
		ret =  __ricoh619_read(to_i2c_client(dev), reg, val);
	mutex_unlock(&ricoh619->io_lock);

	return ret;
}

EXPORT_SYMBOL_GPL(ricoh619_read_bank1);

int ricoh619_bulk_reads(struct device *dev, u8 reg, u8 len, uint8_t *val)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 0);
	if( !ret ) 
		ret = __ricoh619_bulk_reads(to_i2c_client(dev), reg, len, val);
	mutex_unlock(&ricoh619->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_bulk_reads);

int ricoh619_bulk_reads_bank1(struct device *dev, u8 reg, u8 len, uint8_t *val)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 1);
	if( !ret ) 
		ret = __ricoh619_bulk_reads(to_i2c_client(dev), reg, len, val);
	mutex_unlock(&ricoh619->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_bulk_reads_bank1);

int ricoh619_set_bits(struct device *dev, u8 reg, uint8_t bit_mask)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 0);
	if (!ret) {
		ret = __ricoh619_read(to_i2c_client(dev), reg, &reg_val);
		if (ret<0)
			goto out;

		if ((reg_val & bit_mask) != bit_mask) {
			reg_val |= bit_mask;
			ret = __ricoh619_write(to_i2c_client(dev), reg,
								 reg_val);
		}
	}
out:
	mutex_unlock(&ricoh619->io_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_set_bits);

int ricoh619_clr_bits(struct device *dev, u8 reg, uint8_t bit_mask)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 0);
	if( !ret ){
		ret = __ricoh619_read(to_i2c_client(dev), reg, &reg_val);
		if (ret<0)
			goto out;

		if (reg_val & bit_mask) {
			reg_val &= ~bit_mask;
			ret = __ricoh619_write(to_i2c_client(dev), reg,
								 reg_val);
		}
	}
out:
	mutex_unlock(&ricoh619->io_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_clr_bits);

int ricoh619_update(struct device *dev, u8 reg, uint8_t val, uint8_t mask)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 0);
	if( !ret ){
		ret = __ricoh619_read(ricoh619->client, reg, &reg_val);
		if (ret<0)
			goto out;

		if ((reg_val & mask) != val) {
			reg_val = (reg_val & ~mask) | (val & mask);
			ret = __ricoh619_write(ricoh619->client, reg, reg_val);
		}
	}
out:
	mutex_unlock(&ricoh619->io_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_update);

int ricoh619_update_bank1(struct device *dev, u8 reg, uint8_t val, uint8_t mask)
{
	struct ricoh619 *ricoh619 = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&ricoh619->io_lock);
	ret = set_bank_ricoh619(dev, 1);
	if( !ret ){
		ret = __ricoh619_read(ricoh619->client, reg, &reg_val);
		if (ret<0)
			goto out;

		if ((reg_val & mask) != val) {
			reg_val = (reg_val & ~mask) | (val & mask);
			ret = __ricoh619_write(ricoh619->client, reg, reg_val);
		}
	}
out:
	mutex_unlock(&ricoh619->io_lock);
	return ret;
}

static struct i2c_client *ricoh619_i2c_client;
static void ricoh619_device_shutdown(void)
{
	int ret;
	uint8_t val;
	struct ricoh619 *ricoh619 = g_ricoh619;
	printk("%s,line=%d\n", __func__,__LINE__);

#ifdef CONFIG_BATTERY_RICOH619
	val = g_soc;
	val &= 0x7f;
	ret = ricoh619_write(ricoh619->dev, RICOH619_PSWR, val);
	if (ret < 0)
		dev_err(ricoh619->dev, "Error in writing PSWR_REG\n");

	if (g_fg_on_mode == 0) {
		ret = ricoh619_clr_bits(ricoh619->dev,
					 RICOH619_FG_CTRL, 0x01);
		if (ret < 0)
			dev_err(ricoh619->dev, "Error in writing FG_CTRL\n");
	}
	
	/* set rapid timer 300 min */
	ret = ricoh619_set_bits(ricoh619->dev, TIMSET_REG, 0x03);
	if (ret < 0)
		dev_err(ricoh619->dev, "Error in writing the TIMSET_Reg\n");
#endif  
	ret = ricoh619_clr_bits(ricoh619->dev, 0xae, (0x1 <<6)); //disable alam_d
       ret = ricoh619_write(ricoh619->dev, RICOH619_INTC_INTEN, 0); 
	ret = ricoh619_clr_bits(ricoh619->dev,RICOH619_PWR_REP_CNT,(0x1<<0));//Not repeat power ON after power off(Power Off/N_OE)
	mutex_lock(&ricoh619->io_lock);
	mdelay(100);
}
EXPORT_SYMBOL_GPL(ricoh619_device_shutdown);
static void ricoh619_power_off(void)
{
	int ret,i=0;
	uint8_t val,charge_state;
	struct i2c_client *client = ricoh619_i2c_client;

	for(i=0;i < 10;i++){
		printk("%s,line=%d\n", __func__,__LINE__);
		#ifdef CONFIG_BATTERY_RICOH619
			ret = __ricoh619_read(client, 0xBD, &val);
			if(ret < 0)
				continue;
			charge_state = (val & 0x1F);
			if(( charge_state == CHG_STATE_CHG_TRICKLE)||( charge_state == CHG_STATE_CHG_RAPID) ||(charge_state == CHG_STATE_CHG_COMPLETE)){
			 ret = __ricoh619_read(client, RICOH619_PWR_REP_CNT,&val);//Power OFF
			 if(ret < 0)
				continue;
		 	ret = __ricoh619_write(client, RICOH619_PWR_REP_CNT,(val |(0x1<<0)));//Power OFF
		 	if(ret < 0)
				continue;
		}
		#endif  
		ret = __ricoh619_read(client, RICOH619_PWR_SLP_CNT,&val);//Power OFF
		if(ret < 0)
			continue;
		ret = __ricoh619_write(client, RICOH619_PWR_SLP_CNT,(val |(0x1<<0)));//Power OFF
		if (ret < 0) {
			printk("ricoh619 power off error!\n");
			continue;
		}
	}
	while(1)wfi();
}
EXPORT_SYMBOL_GPL(ricoh619_power_off);

#if 0
static int ricoh619_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct ricoh619 *ricoh619 = container_of(gc, struct ricoh619, gpio_chip);
	uint8_t val;
	int ret;

	ret = ricoh619_read(ricoh619->dev, RICOH619_GPIO_MON_IOIN, &val);
	if (ret < 0)
		return ret;

	return ((val & (0x1 << offset)) != 0);
}

static void ricoh619_gpio_set(struct gpio_chip *gc, unsigned offset,
			int value)
{
	struct ricoh619 *ricoh619 = container_of(gc, struct ricoh619, gpio_chip);
	if (value)
		ricoh619_set_bits(ricoh619->dev, RICOH619_GPIO_IOOUT,
						1 << offset);
	else
		ricoh619_clr_bits(ricoh619->dev, RICOH619_GPIO_IOOUT,
						1 << offset);
}

static int ricoh619_gpio_input(struct gpio_chip *gc, unsigned offset)
{
	struct ricoh619 *ricoh619 = container_of(gc, struct ricoh619, gpio_chip);

	return ricoh619_clr_bits(ricoh619->dev, RICOH619_GPIO_IOSEL,
						1 << offset);
}

static int ricoh619_gpio_output(struct gpio_chip *gc, unsigned offset,
				int value)
{
	struct ricoh619 *ricoh619 = container_of(gc, struct ricoh619, gpio_chip);

	ricoh619_gpio_set(gc, offset, value);
	return ricoh619_set_bits(ricoh619->dev, RICOH619_GPIO_IOSEL,
						1 << offset);
}

static int ricoh619_gpio_to_irq(struct gpio_chip *gc, unsigned off)
{
	struct ricoh619 *ricoh619 = container_of(gc, struct ricoh619, gpio_chip);

	if ((off >= 0) && (off < 8))
		return ricoh619->irq_base + RICOH619_IRQ_GPIO0 + off;

	return -EIO;
}

static void ricoh619_gpio_init(struct ricoh619 *ricoh619,
	struct ricoh619_platform_data *pdata)
{
	int ret;
	int i;
	struct ricoh619_gpio_init_data *ginit;

	if (pdata->gpio_base  <= 0)
		return;

	for (i = 0; i < pdata->num_gpioinit_data; ++i) {
		ginit = &pdata->gpio_init_data[i];

		if (!ginit->init_apply)
			continue;

		if (ginit->output_mode_en) {
			/* GPIO output mode */
			if (ginit->output_val)
				/* output H */
				ret = ricoh619_set_bits(ricoh619->dev,
					RICOH619_GPIO_IOOUT, 1 << i);
			else
				/* output L */
				ret = ricoh619_clr_bits(ricoh619->dev,
					RICOH619_GPIO_IOOUT, 1 << i);
			if (!ret)
				ret = ricoh619_set_bits(ricoh619->dev,
					RICOH619_GPIO_IOSEL, 1 << i);
		} else
			/* GPIO input mode */
			ret = ricoh619_clr_bits(ricoh619->dev,
					RICOH619_GPIO_IOSEL, 1 << i);

		/* if LED function enabled in OTP */
		if (ginit->led_mode) {
			/* LED Mode 1 */
			if (i == 0)	/* GP0 */
				ret = ricoh619_set_bits(ricoh619->dev,
					 RICOH619_GPIO_LED_FUNC,
					 0x04 | (ginit->led_func & 0x03));
			if (i == 1)	/* GP1 */
				ret = ricoh619_set_bits(ricoh619->dev,
					 RICOH619_GPIO_LED_FUNC,
					 0x40 | (ginit->led_func & 0x03) << 4);

		}


		if (ret < 0)
			dev_err(ricoh619->dev, "Gpio %d init "
				"dir configuration failed: %d\n", i, ret);

	}

	ricoh619->gpio_chip.owner		= THIS_MODULE;
	ricoh619->gpio_chip.label		= ricoh619->client->name;
	ricoh619->gpio_chip.dev			= ricoh619->dev;
	ricoh619->gpio_chip.base		= pdata->gpio_base;
	ricoh619->gpio_chip.ngpio		= RICOH619_NR_GPIO;
	ricoh619->gpio_chip.can_sleep	= 1;

	ricoh619->gpio_chip.direction_input	= ricoh619_gpio_input;
	ricoh619->gpio_chip.direction_output	= ricoh619_gpio_output;
	ricoh619->gpio_chip.set			= ricoh619_gpio_set;
	ricoh619->gpio_chip.get			= ricoh619_gpio_get;
	ricoh619->gpio_chip.to_irq	  	= ricoh619_gpio_to_irq;

	ret = gpiochip_add(&ricoh619->gpio_chip);
	if (ret)
		dev_warn(ricoh619->dev, "GPIO registration failed: %d\n", ret);
}
#endif
static int ricoh619_remove_subdev(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int ricoh619_remove_subdevs(struct ricoh619 *ricoh619)
{
	return device_for_each_child(ricoh619->dev, NULL,
				     ricoh619_remove_subdev);
}
#if 0
static int ricoh619_add_subdevs(struct ricoh619 *ricoh619,
				struct ricoh619_platform_data *pdata)
{
	struct ricoh619_subdev_info *subdev;
	struct platform_device *pdev;
	int i, ret = 0;

	for (i = 0; i < pdata->num_subdevs; i++) {
		subdev = &pdata->subdevs[i];

		pdev = platform_device_alloc(subdev->name, subdev->id);

		pdev->dev.parent = ricoh619->dev;
		pdev->dev.platform_data = subdev->platform_data;

		ret = platform_device_add(pdev);
		if (ret)
			goto failed;
	}
	return 0;

failed:
	ricoh619_remove_subdevs(ricoh619);
	return ret;
}
#endif
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
static void print_regs(const char *header, struct seq_file *s,
		struct i2c_client *client, int start_offset,
		int end_offset)
{
	uint8_t reg_val;
	int i;
	int ret;

	seq_printf(s, "%s\n", header);
	for (i = start_offset; i <= end_offset; ++i) {
		ret = __ricoh619_read(client, i, &reg_val);
		if (ret >= 0)
			seq_printf(s, "Reg 0x%02x Value 0x%02x\n", i, reg_val);
	}
	seq_printf(s, "------------------\n");
}

static int dbg_ricoh_show(struct seq_file *s, void *unused)
{
	struct ricoh619 *ricoh = s->private;
	struct i2c_client *client = ricoh->client;

	seq_printf(s, "RICOH619 Registers\n");
	seq_printf(s, "------------------\n");

	print_regs("System Regs",		s, client, 0x0, 0x05);
	print_regs("Power Control Regs",	s, client, 0x07, 0x2B);
	print_regs("DCDC  Regs",		s, client, 0x2C, 0x43);
	print_regs("LDO   Regs",		s, client, 0x44, 0x61);
	print_regs("ADC   Regs",		s, client, 0x64, 0x8F);
	print_regs("GPIO  Regs",		s, client, 0x90, 0x98);
	print_regs("INTC  Regs",		s, client, 0x9C, 0x9E);
	print_regs("RTC   Regs",		s, client, 0xA0, 0xAF);
	print_regs("OPT   Regs",		s, client, 0xB0, 0xB1);
	print_regs("CHG   Regs",		s, client, 0xB2, 0xDF);
	print_regs("FUEL  Regs",		s, client, 0xE0, 0xFC);
	return 0;
}

static int dbg_ricoh_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_ricoh_show, inode->i_private);
}

static const struct file_operations debug_fops = {
	.open		= dbg_ricoh_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
static void __init ricoh619_debuginit(struct ricoh619 *ricoh)
{
	(void)debugfs_create_file("ricoh619", S_IRUGO, NULL,
			ricoh, &debug_fops);
}
#else
static void print_regs(const char *header, struct i2c_client *client,
		int start_offset, int end_offset)
{
	uint8_t reg_val;
	int i;
	int ret;

	printk(KERN_INFO "%s\n", header);
	for (i = start_offset; i <= end_offset; ++i) {
		ret = __ricoh619_read(client, i, &reg_val);
		if (ret >= 0)
			printk(KERN_INFO "Reg 0x%02x Value 0x%02x\n",
							 i, reg_val);
	}
	printk(KERN_INFO "------------------\n");
}
static void __init ricoh619_debuginit(struct ricoh619 *ricoh)
{
	struct i2c_client *client = ricoh->client;

	printk(KERN_INFO "RICOH619 Registers\n");
	printk(KERN_INFO "------------------\n");

	print_regs("System Regs",		client, 0x0, 0x05);
	print_regs("Power Control Regs",	client, 0x07, 0x2B);
	print_regs("DCDC  Regs",		client, 0x2C, 0x43);
	print_regs("LDO   Regs",		client, 0x44, 0x5C);
	print_regs("ADC   Regs",		client, 0x64, 0x8F);
	print_regs("GPIO  Regs",		client, 0x90, 0x9B);
	print_regs("INTC  Regs",		client, 0x9C, 0x9E);
	print_regs("OPT   Regs",		client, 0xB0, 0xB1);
	print_regs("CHG   Regs",		client, 0xB2, 0xDF);
	print_regs("FUEL  Regs",		client, 0xE0, 0xFC);

	return 0;
}
#endif

#ifdef CONFIG_OF
static struct ricoh619_platform_data *ricoh619_parse_dt(struct ricoh619 *ricoh619)
{
	struct ricoh619_platform_data *pdata;
	struct device_node *ricoh619_pmic_np;

	ricoh619_pmic_np = of_node_get(ricoh619->dev->of_node);
	if (!ricoh619_pmic_np) {
		printk("could not find pmic sub-node\n");
		return NULL;
	}
	pdata = devm_kzalloc(ricoh619->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->irq_gpio = of_get_named_gpio(ricoh619_pmic_np,"gpios",0);
		if (!gpio_is_valid(pdata->irq_gpio)) {
			printk("invalid gpio: %d\n",  pdata->irq_gpio);
			return NULL;
		}

	pdata->pmic_sleep_gpio = of_get_named_gpio(ricoh619_pmic_np,"gpios",1);
			if (!gpio_is_valid(pdata->pmic_sleep_gpio)) {
				printk("invalid gpio: %d\n",  pdata->pmic_sleep_gpio);
		}

	pdata->dc_det = of_get_named_gpio(ricoh619_pmic_np,"gpios",2);
			if (!gpio_is_valid(pdata->dc_det)) {
				printk("invalid gpio: %d\n",  pdata->dc_det);
		}
	pdata->pmic_sleep = true;
	
	pdata->pm_off = of_property_read_bool(ricoh619_pmic_np,"ricoh619,system-power-controller");
		
	return pdata;
}

#else
static struct ricoh619_platform_data *ricoh619_parse_dt(struct ricoh619 *ricoh619)
{
	return NULL;
}
#endif

static struct syscore_ops ricoh619_syscore_ops = {
	.shutdown = ricoh619_device_shutdown,
};

static void ricoh619_noe_init(struct ricoh619 *ricoh)
{
	int ret;
	
	/***************set noe time 128ms**************/
	ret = ricoh619_set_bits(ricoh->dev,0x11,(0x1 <<3));
	ret = ricoh619_clr_bits(ricoh->dev,0x11,(0x7 <<0));
	ret = ricoh619_clr_bits(ricoh->dev,0x11,(0x1 <<3));//N_OE timer setting to 128mS
 	/**********************************************/
	ret = ricoh619_clr_bits(ricoh->dev,RICOH619_PWR_REP_CNT,(1 << 0));  //Repeat power ON after reset (Power Off/N_OE) :1:reset 0:power off
}

static int ricoh619_pre_init(struct ricoh619 *ricoh619)
{
	int ret=0;
	 printk("%s,line=%d\n", __func__,__LINE__);
	 /*
	ret = ricoh619_read(ricoh619->dev,0x09,&val);
	printk("%s,line=%d ricoh619 power on his %08x\n", __func__,__LINE__,val);
	ret = ricoh619_read(ricoh619->dev,0x0a,&val);
	printk("%s,line=%d ricoh619 power off his %08x\n", __func__,__LINE__,val);
	*/
	ricoh619_set_bits(ricoh619->dev, 0xae, (0x1 <<6));//enable alam_d
	ricoh619_write(ricoh619->dev, 0x2f, 0x43);//slove ripple
	ricoh619_write(ricoh619->dev, 0x05, 0x07);//enable clkout2
	
	ricoh619_noe_init(ricoh619);
	/***************set PKEY long press time 0sec*******/
	ret = ricoh619_set_bits(ricoh619->dev,0x10,(0x1 <<7));
	ret = ricoh619_clr_bits(ricoh619->dev,0x10,(0x1 <<3));
	ret = ricoh619_clr_bits(ricoh619->dev,0x10,(0x1 <<7));
 	/**********************************************/
	ret = ricoh619_set_bits(ricoh619->dev,BATSET2_REG,(3 << 0)); 
	ret = ricoh619_clr_bits(ricoh619->dev,BATSET2_REG,(1 << 2)); //set vrchg 4v

	
	return ret;
}

static int ricoh619_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct ricoh619 *ricoh619;
	struct ricoh619_platform_data *pdata;
	int ret;
	uint8_t control;
	int i=0;
	 printk("%s,line=%d\n", __func__,__LINE__);

	ricoh619 = devm_kzalloc(&client->dev,sizeof(struct ricoh619), GFP_KERNEL);
	if (ricoh619 == NULL)
		return -ENOMEM;

	ricoh619->client = client;
	ricoh619->dev = &client->dev;
	i2c_set_clientdata(client, ricoh619);
	mutex_init(&ricoh619->io_lock);

	ret = ricoh619_read(ricoh619->dev, 0x36, &control);
	if ((ret <0) || (control < 0) || (control == 0xff) || (control == 0) ){
		if (ret <0){
			printk(KERN_INFO "The device is not ricoh619 %08x %d\n",control,ret);
			goto err;
		}
		else{
			do{
				ret = ricoh619_write(ricoh619->dev, 0xff, 0x00);
				ret = ricoh619_read(ricoh619->dev, 0x36, &control);
				i += 1;
				printk(KERN_INFO "##################:read ricoh619 0x36 error retry %08x %d\n",control,ret);
			}while( ((control == 0xff) || (control == 0) ) && (i < 10));
			if ((control == 0xff) || (control == 0) ){
				ret = -ENXIO;	
				printk(KERN_INFO "##################The device is not ricoh619 %08x %d\n",control,ret);
				goto err;
			}
		}
	}

	ret = ricoh619_pre_init(ricoh619);
	if (ret < 0){
		printk("The ricoh619_pre_init failed %d\n",ret);
		goto err;
	}

	ricoh619->bank_num = 0;
	
	if (ricoh619->dev->of_node)
		pdata = ricoh619_parse_dt(ricoh619);
	
	if (pdata->dc_det) 
		ricoh619->dc_det = pdata->dc_det;
	
	/******************************set sleep vol & dcdc mode******************/
	#ifdef CONFIG_OF
	if (pdata->pmic_sleep_gpio) {
			ret = gpio_request(pdata->pmic_sleep_gpio, "ricoh619_pmic_sleep");
			if (ret < 0) {
				dev_err(ricoh619->dev,"Failed to request gpio %d with ret:""%d\n",	pdata->pmic_sleep_gpio, ret);
				return IRQ_NONE;
			}
			gpio_direction_output(pdata->pmic_sleep_gpio,0);
			ret = gpio_get_value(pdata->pmic_sleep_gpio);
			gpio_free(pdata->pmic_sleep_gpio);
			pr_info("%s: ricoh619_pmic_sleep=%x\n", __func__, ret);
	}	
	#endif
	/**********************************************************/
	ret = ricoh619_irq_init(ricoh619, pdata->irq_gpio, pdata);
	if (ret < 0)
		goto err;
	
	ret = mfd_add_devices(ricoh619->dev, -1,
			     ricoh619s, ARRAY_SIZE(ricoh619s),
			      NULL, 0,NULL);
	g_ricoh619 = ricoh619;
	if (pdata->pm_off && !pm_power_off) {
		pm_power_off = ricoh619_power_off;
	}
	ricoh619_debuginit(ricoh619);

	ricoh619_i2c_client = client;
	register_syscore_ops(&ricoh619_syscore_ops);
	return 0;
err:
	mfd_remove_devices(ricoh619->dev);
	return ret;
}

static int ricoh619_i2c_remove(struct i2c_client *client)
{
	struct ricoh619 *ricoh619 = i2c_get_clientdata(client);

	unregister_syscore_ops(&ricoh619_syscore_ops);
	ricoh619_remove_subdevs(ricoh619);
	return 0;
}

#ifdef CONFIG_PM
extern u8 ricoh619_pwr_key_reg;
int ricoh619_pwrkey_wakeup = 0;
static int ricoh619_i2c_suspend(struct i2c_client *client, pm_message_t state)
{
//	printk("PMU: %s: \n",__func__);

	if (g_ricoh619->chip_irq)
		disable_irq(g_ricoh619->chip_irq);
	ricoh619_pwrkey_wakeup = 1;
	__ricoh619_write(client, RICOH619_INT_IR_SYS, 0x0); //Clear PWR_KEY IRQ
	 __ricoh619_read(client, RICOH619_INT_IR_SYS, &ricoh619_pwr_key_reg);
	return 0;
}
static int ricoh619_i2c_resume(struct i2c_client *client)
{
	/*
	uint8_t reg_val;
	int ret;
	ret = __ricoh619_read(client, RICOH619_INT_IR_SYS, &reg_val);
	if(ricoh619_pwr_key_reg & 0x01) { //If PWR_KEY wakeup
		//printk("PMU: %s: PWR_KEY Wakeup %08x\n",__func__,ricoh619_pwr_key_reg);
		rcoh619_pwrkey_wakeup = 1;
		__ricoh619_write(client, RICOH619_INT_IR_SYS, 0x0); //Clear PWR_KEY IRQ
	}
	*/
	
	if (g_ricoh619->chip_irq)
		enable_irq(g_ricoh619->chip_irq);
	return 0;
}

static int  ricoh619_i2c_late_suspend(struct device *dev)
{
	struct i2c_client *client = i2c_verify_client(dev);

        ricoh619_i2c_suspend(client,PMSG_SUSPEND);
	return 0;
}

static int rockchip_i2c_late_resume(struct device *dev)
{
	struct i2c_client *client = i2c_verify_client(dev);    
    
        ricoh619_i2c_resume(client);
	return 0;
}

static const struct dev_pm_ops ricoh619_i2c_dev_pm= {
	.suspend_late = ricoh619_i2c_late_suspend,
	.resume_early = rockchip_i2c_late_resume,
};

#endif

static const struct i2c_device_id ricoh619_i2c_id[] = {
	{"ricoh619", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ricoh619_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id ricoh619_dt_match[] = {
	{ .compatible = "ricoh,ricoh619", },
	{},
};
MODULE_DEVICE_TABLE(of, ricoh619_dt_match);
#endif

static struct i2c_driver ricoh619_i2c_driver = {
	.driver = {
		   .name = "ricoh619",
		   .owner = THIS_MODULE,
                  #ifdef CONFIG_PM
		    .pm	= (&ricoh619_i2c_dev_pm),
                  #endif		   
		   .of_match_table = of_match_ptr(ricoh619_dt_match),
		   },
	.probe = ricoh619_i2c_probe,
	.remove = ricoh619_i2c_remove,

	.id_table = ricoh619_i2c_id,
};


static int __init ricoh619_i2c_init(void)
{
	int ret = -ENODEV;
	ret = i2c_add_driver(&ricoh619_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);

	return ret;
}

subsys_initcall_sync(ricoh619_i2c_init);

static void __exit ricoh619_i2c_exit(void)
{
	i2c_del_driver(&ricoh619_i2c_driver);
}

module_exit(ricoh619_i2c_exit);

MODULE_DESCRIPTION("RICOH RC5T619 PMU multi-function core driver");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_LICENSE("GPL");
