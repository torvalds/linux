/*
 * Copyright (C) 2007-2010 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * Low-level core for exclusive access to the AB3100 IC on the I2C bus
 * and some basic chip-configuration.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/mfd/abx500.h>

/* These are the only registers inside AB3100 used in this main file */

/* Interrupt event registers */
#define AB3100_EVENTA1		0x21
#define AB3100_EVENTA2		0x22
#define AB3100_EVENTA3		0x23

/* AB3100 DAC converter registers */
#define AB3100_DIS		0x00
#define AB3100_D0C		0x01
#define AB3100_D1C		0x02
#define AB3100_D2C		0x03
#define AB3100_D3C		0x04

/* Chip ID register */
#define AB3100_CID		0x20

/* AB3100 interrupt registers */
#define AB3100_IMRA1		0x24
#define AB3100_IMRA2		0x25
#define AB3100_IMRA3		0x26
#define AB3100_IMRB1		0x2B
#define AB3100_IMRB2		0x2C
#define AB3100_IMRB3		0x2D

/* System Power Monitoring and control registers */
#define AB3100_MCA		0x2E
#define AB3100_MCB		0x2F

/* SIM power up */
#define AB3100_SUP		0x50

/*
 * I2C communication
 *
 * The AB3100 is usually assigned address 0x48 (7-bit)
 * The chip is defined in the platform i2c_board_data section.
 */
static int ab3100_get_chip_id(struct device *dev)
{
	struct ab3100 *ab3100 = dev_get_drvdata(dev->parent);

	return (int)ab3100->chip_id;
}

static int ab3100_set_register_interruptible(struct ab3100 *ab3100,
	u8 reg, u8 regval)
{
	u8 regandval[2] = {reg, regval};
	int err;

	err = mutex_lock_interruptible(&ab3100->access_mutex);
	if (err)
		return err;

	/*
	 * A two-byte write message with the first byte containing the register
	 * number and the second byte containing the value to be written
	 * effectively sets a register in the AB3100.
	 */
	err = i2c_master_send(ab3100->i2c_client, regandval, 2);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (write register): %d\n",
			err);
	} else if (err != 2) {
		dev_err(ab3100->dev,
			"write error (write register) "
			"%d bytes transferred (expected 2)\n",
			err);
		err = -EIO;
	} else {
		/* All is well */
		err = 0;
	}
	mutex_unlock(&ab3100->access_mutex);
	return err;
}

static int set_register_interruptible(struct device *dev,
	u8 bank, u8 reg, u8 value)
{
	struct ab3100 *ab3100 = dev_get_drvdata(dev->parent);

	return ab3100_set_register_interruptible(ab3100, reg, value);
}

/*
 * The test registers exist at an I2C bus address up one
 * from the ordinary base. They are not supposed to be used
 * in production code, but sometimes you have to do that
 * anyway. It's currently only used from this file so declare
 * it static and do not export.
 */
static int ab3100_set_test_register_interruptible(struct ab3100 *ab3100,
				    u8 reg, u8 regval)
{
	u8 regandval[2] = {reg, regval};
	int err;

	err = mutex_lock_interruptible(&ab3100->access_mutex);
	if (err)
		return err;

	err = i2c_master_send(ab3100->testreg_client, regandval, 2);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (write test register): %d\n",
			err);
	} else if (err != 2) {
		dev_err(ab3100->dev,
			"write error (write test register) "
			"%d bytes transferred (expected 2)\n",
			err);
		err = -EIO;
	} else {
		/* All is well */
		err = 0;
	}
	mutex_unlock(&ab3100->access_mutex);

	return err;
}

static int ab3100_get_register_interruptible(struct ab3100 *ab3100,
	u8 reg, u8 *regval)
{
	int err;

	err = mutex_lock_interruptible(&ab3100->access_mutex);
	if (err)
		return err;

	/*
	 * AB3100 require an I2C "stop" command between each message, else
	 * it will not work. The only way of achieveing this with the
	 * message transport layer is to send the read and write messages
	 * separately.
	 */
	err = i2c_master_send(ab3100->i2c_client, &reg, 1);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (send register address): %d\n",
			err);
		goto get_reg_out_unlock;
	} else if (err != 1) {
		dev_err(ab3100->dev,
			"write error (send register address) "
			"%d bytes transferred (expected 1)\n",
			err);
		err = -EIO;
		goto get_reg_out_unlock;
	} else {
		/* All is well */
		err = 0;
	}

	err = i2c_master_recv(ab3100->i2c_client, regval, 1);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (read register): %d\n",
			err);
		goto get_reg_out_unlock;
	} else if (err != 1) {
		dev_err(ab3100->dev,
			"write error (read register) "
			"%d bytes transferred (expected 1)\n",
			err);
		err = -EIO;
		goto get_reg_out_unlock;
	} else {
		/* All is well */
		err = 0;
	}

 get_reg_out_unlock:
	mutex_unlock(&ab3100->access_mutex);
	return err;
}

static int get_register_interruptible(struct device *dev, u8 bank, u8 reg,
	u8 *value)
{
	struct ab3100 *ab3100 = dev_get_drvdata(dev->parent);

	return ab3100_get_register_interruptible(ab3100, reg, value);
}

static int ab3100_get_register_page_interruptible(struct ab3100 *ab3100,
			     u8 first_reg, u8 *regvals, u8 numregs)
{
	int err;

	if (ab3100->chip_id == 0xa0 ||
	    ab3100->chip_id == 0xa1)
		/* These don't support paged reads */
		return -EIO;

	err = mutex_lock_interruptible(&ab3100->access_mutex);
	if (err)
		return err;

	/*
	 * Paged read also require an I2C "stop" command.
	 */
	err = i2c_master_send(ab3100->i2c_client, &first_reg, 1);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (send first register address): %d\n",
			err);
		goto get_reg_page_out_unlock;
	} else if (err != 1) {
		dev_err(ab3100->dev,
			"write error (send first register address) "
			"%d bytes transferred (expected 1)\n",
			err);
		err = -EIO;
		goto get_reg_page_out_unlock;
	}

	err = i2c_master_recv(ab3100->i2c_client, regvals, numregs);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (read register page): %d\n",
			err);
		goto get_reg_page_out_unlock;
	} else if (err != numregs) {
		dev_err(ab3100->dev,
			"write error (read register page) "
			"%d bytes transferred (expected %d)\n",
			err, numregs);
		err = -EIO;
		goto get_reg_page_out_unlock;
	}

	/* All is well */
	err = 0;

 get_reg_page_out_unlock:
	mutex_unlock(&ab3100->access_mutex);
	return err;
}

static int get_register_page_interruptible(struct device *dev, u8 bank,
	u8 first_reg, u8 *regvals, u8 numregs)
{
	struct ab3100 *ab3100 = dev_get_drvdata(dev->parent);

	return ab3100_get_register_page_interruptible(ab3100,
			first_reg, regvals, numregs);
}

static int ab3100_mask_and_set_register_interruptible(struct ab3100 *ab3100,
				 u8 reg, u8 andmask, u8 ormask)
{
	u8 regandval[2] = {reg, 0};
	int err;

	err = mutex_lock_interruptible(&ab3100->access_mutex);
	if (err)
		return err;

	/* First read out the target register */
	err = i2c_master_send(ab3100->i2c_client, &reg, 1);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (maskset send address): %d\n",
			err);
		goto get_maskset_unlock;
	} else if (err != 1) {
		dev_err(ab3100->dev,
			"write error (maskset send address) "
			"%d bytes transferred (expected 1)\n",
			err);
		err = -EIO;
		goto get_maskset_unlock;
	}

	err = i2c_master_recv(ab3100->i2c_client, &regandval[1], 1);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (maskset read register): %d\n",
			err);
		goto get_maskset_unlock;
	} else if (err != 1) {
		dev_err(ab3100->dev,
			"write error (maskset read register) "
			"%d bytes transferred (expected 1)\n",
			err);
		err = -EIO;
		goto get_maskset_unlock;
	}

	/* Modify the register */
	regandval[1] &= andmask;
	regandval[1] |= ormask;

	/* Write the register */
	err = i2c_master_send(ab3100->i2c_client, regandval, 2);
	if (err < 0) {
		dev_err(ab3100->dev,
			"write error (write register): %d\n",
			err);
		goto get_maskset_unlock;
	} else if (err != 2) {
		dev_err(ab3100->dev,
			"write error (write register) "
			"%d bytes transferred (expected 2)\n",
			err);
		err = -EIO;
		goto get_maskset_unlock;
	}

	/* All is well */
	err = 0;

 get_maskset_unlock:
	mutex_unlock(&ab3100->access_mutex);
	return err;
}

static int mask_and_set_register_interruptible(struct device *dev, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues)
{
	struct ab3100 *ab3100 = dev_get_drvdata(dev->parent);

	return ab3100_mask_and_set_register_interruptible(ab3100,
			reg, bitmask, (bitmask & bitvalues));
}

/*
 * Register a simple callback for handling any AB3100 events.
 */
int ab3100_event_register(struct ab3100 *ab3100,
			  struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ab3100->event_subscribers,
					       nb);
}
EXPORT_SYMBOL(ab3100_event_register);

/*
 * Remove a previously registered callback.
 */
int ab3100_event_unregister(struct ab3100 *ab3100,
			    struct notifier_block *nb)
{
  return blocking_notifier_chain_unregister(&ab3100->event_subscribers,
					    nb);
}
EXPORT_SYMBOL(ab3100_event_unregister);


static int ab3100_event_registers_startup_state_get(struct device *dev,
					     u8 *event)
{
	struct ab3100 *ab3100 = dev_get_drvdata(dev->parent);
	if (!ab3100->startup_events_read)
		return -EAGAIN; /* Try again later */
	memcpy(event, ab3100->startup_events, 3);
	return 0;
}

static struct abx500_ops ab3100_ops = {
	.get_chip_id = ab3100_get_chip_id,
	.set_register = set_register_interruptible,
	.get_register = get_register_interruptible,
	.get_register_page = get_register_page_interruptible,
	.set_register_page = NULL,
	.mask_and_set_register = mask_and_set_register_interruptible,
	.event_registers_startup_state_get =
		ab3100_event_registers_startup_state_get,
	.startup_irq_enabled = NULL,
};

/*
 * This is a threaded interrupt handler so we can make some
 * I2C calls etc.
 */
static irqreturn_t ab3100_irq_handler(int irq, void *data)
{
	struct ab3100 *ab3100 = data;
	u8 event_regs[3];
	u32 fatevent;
	int err;

	add_interrupt_randomness(irq);

	err = ab3100_get_register_page_interruptible(ab3100, AB3100_EVENTA1,
				       event_regs, 3);
	if (err)
		goto err_event;

	fatevent = (event_regs[0] << 16) |
		(event_regs[1] << 8) |
		event_regs[2];

	if (!ab3100->startup_events_read) {
		ab3100->startup_events[0] = event_regs[0];
		ab3100->startup_events[1] = event_regs[1];
		ab3100->startup_events[2] = event_regs[2];
		ab3100->startup_events_read = true;
	}
	/*
	 * The notified parties will have to mask out the events
	 * they're interested in and react to them. They will be
	 * notified on all events, then they use the fatevent value
	 * to determine if they're interested.
	 */
	blocking_notifier_call_chain(&ab3100->event_subscribers,
				     fatevent, NULL);

	dev_dbg(ab3100->dev,
		"IRQ Event: 0x%08x\n", fatevent);

	return IRQ_HANDLED;

 err_event:
	dev_dbg(ab3100->dev,
		"error reading event status\n");
	return IRQ_HANDLED;
}

#ifdef CONFIG_DEBUG_FS
/*
 * Some debugfs entries only exposed if we're using debug
 */
static int ab3100_registers_print(struct seq_file *s, void *p)
{
	struct ab3100 *ab3100 = s->private;
	u8 value;
	u8 reg;

	seq_printf(s, "AB3100 registers:\n");

	for (reg = 0; reg < 0xff; reg++) {
		ab3100_get_register_interruptible(ab3100, reg, &value);
		seq_printf(s, "[0x%x]:  0x%x\n", reg, value);
	}
	return 0;
}

static int ab3100_registers_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab3100_registers_print, inode->i_private);
}

static const struct file_operations ab3100_registers_fops = {
	.open = ab3100_registers_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

struct ab3100_get_set_reg_priv {
	struct ab3100 *ab3100;
	bool mode;
};

static int ab3100_get_set_reg_open_file(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t ab3100_get_set_reg(struct file *file,
				  const char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct ab3100_get_set_reg_priv *priv = file->private_data;
	struct ab3100 *ab3100 = priv->ab3100;
	char buf[32];
	ssize_t buf_size;
	int regp;
	unsigned long user_reg;
	int err;
	int i = 0;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	/*
	 * The idea is here to parse a string which is either
	 * "0xnn" for reading a register, or "0xaa 0xbb" for
	 * writing 0xbb to the register 0xaa. First move past
	 * whitespace and then begin to parse the register.
	 */
	while ((i < buf_size) && (buf[i] == ' '))
		i++;
	regp = i;

	/*
	 * Advance pointer to end of string then terminate
	 * the register string. This is needed to satisfy
	 * the strict_strtoul() function.
	 */
	while ((i < buf_size) && (buf[i] != ' '))
		i++;
	buf[i] = '\0';

	err = strict_strtoul(&buf[regp], 16, &user_reg);
	if (err)
		return err;
	if (user_reg > 0xff)
		return -EINVAL;

	/* Either we read or we write a register here */
	if (!priv->mode) {
		/* Reading */
		u8 reg = (u8) user_reg;
		u8 regvalue;

		ab3100_get_register_interruptible(ab3100, reg, &regvalue);

		dev_info(ab3100->dev,
			 "debug read AB3100 reg[0x%02x]: 0x%02x\n",
			 reg, regvalue);
	} else {
		int valp;
		unsigned long user_value;
		u8 reg = (u8) user_reg;
		u8 value;
		u8 regvalue;

		/*
		 * Writing, we need some value to write to
		 * the register so keep parsing the string
		 * from userspace.
		 */
		i++;
		while ((i < buf_size) && (buf[i] == ' '))
			i++;
		valp = i;
		while ((i < buf_size) && (buf[i] != ' '))
			i++;
		buf[i] = '\0';

		err = strict_strtoul(&buf[valp], 16, &user_value);
		if (err)
			return err;
		if (user_reg > 0xff)
			return -EINVAL;

		value = (u8) user_value;
		ab3100_set_register_interruptible(ab3100, reg, value);
		ab3100_get_register_interruptible(ab3100, reg, &regvalue);

		dev_info(ab3100->dev,
			 "debug write reg[0x%02x] with 0x%02x, "
			 "after readback: 0x%02x\n",
			 reg, value, regvalue);
	}
	return buf_size;
}

static const struct file_operations ab3100_get_set_reg_fops = {
	.open = ab3100_get_set_reg_open_file,
	.write = ab3100_get_set_reg,
};

static struct dentry *ab3100_dir;
static struct dentry *ab3100_reg_file;
static struct ab3100_get_set_reg_priv ab3100_get_priv;
static struct dentry *ab3100_get_reg_file;
static struct ab3100_get_set_reg_priv ab3100_set_priv;
static struct dentry *ab3100_set_reg_file;

static void ab3100_setup_debugfs(struct ab3100 *ab3100)
{
	int err;

	ab3100_dir = debugfs_create_dir("ab3100", NULL);
	if (!ab3100_dir)
		goto exit_no_debugfs;

	ab3100_reg_file = debugfs_create_file("registers",
				S_IRUGO, ab3100_dir, ab3100,
				&ab3100_registers_fops);
	if (!ab3100_reg_file) {
		err = -ENOMEM;
		goto exit_destroy_dir;
	}

	ab3100_get_priv.ab3100 = ab3100;
	ab3100_get_priv.mode = false;
	ab3100_get_reg_file = debugfs_create_file("get_reg",
				S_IWUGO, ab3100_dir, &ab3100_get_priv,
				&ab3100_get_set_reg_fops);
	if (!ab3100_get_reg_file) {
		err = -ENOMEM;
		goto exit_destroy_reg;
	}

	ab3100_set_priv.ab3100 = ab3100;
	ab3100_set_priv.mode = true;
	ab3100_set_reg_file = debugfs_create_file("set_reg",
				S_IWUGO, ab3100_dir, &ab3100_set_priv,
				&ab3100_get_set_reg_fops);
	if (!ab3100_set_reg_file) {
		err = -ENOMEM;
		goto exit_destroy_get_reg;
	}
	return;

 exit_destroy_get_reg:
	debugfs_remove(ab3100_get_reg_file);
 exit_destroy_reg:
	debugfs_remove(ab3100_reg_file);
 exit_destroy_dir:
	debugfs_remove(ab3100_dir);
 exit_no_debugfs:
	return;
}
static inline void ab3100_remove_debugfs(void)
{
	debugfs_remove(ab3100_set_reg_file);
	debugfs_remove(ab3100_get_reg_file);
	debugfs_remove(ab3100_reg_file);
	debugfs_remove(ab3100_dir);
}
#else
static inline void ab3100_setup_debugfs(struct ab3100 *ab3100)
{
}
static inline void ab3100_remove_debugfs(void)
{
}
#endif

/*
 * Basic set-up, datastructure creation/destruction and I2C interface.
 * This sets up a default config in the AB3100 chip so that it
 * will work as expected.
 */

struct ab3100_init_setting {
	u8 abreg;
	u8 setting;
};

static const struct ab3100_init_setting __initconst
ab3100_init_settings[] = {
	{
		.abreg = AB3100_MCA,
		.setting = 0x01
	}, {
		.abreg = AB3100_MCB,
		.setting = 0x30
	}, {
		.abreg = AB3100_IMRA1,
		.setting = 0x00
	}, {
		.abreg = AB3100_IMRA2,
		.setting = 0xFF
	}, {
		.abreg = AB3100_IMRA3,
		.setting = 0x01
	}, {
		.abreg = AB3100_IMRB1,
		.setting = 0xBF
	}, {
		.abreg = AB3100_IMRB2,
		.setting = 0xFF
	}, {
		.abreg = AB3100_IMRB3,
		.setting = 0xFF
	}, {
		.abreg = AB3100_SUP,
		.setting = 0x00
	}, {
		.abreg = AB3100_DIS,
		.setting = 0xF0
	}, {
		.abreg = AB3100_D0C,
		.setting = 0x00
	}, {
		.abreg = AB3100_D1C,
		.setting = 0x00
	}, {
		.abreg = AB3100_D2C,
		.setting = 0x00
	}, {
		.abreg = AB3100_D3C,
		.setting = 0x00
	},
};

static int __init ab3100_setup(struct ab3100 *ab3100)
{
	int err = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(ab3100_init_settings); i++) {
		err = ab3100_set_register_interruptible(ab3100,
					  ab3100_init_settings[i].abreg,
					  ab3100_init_settings[i].setting);
		if (err)
			goto exit_no_setup;
	}

	/*
	 * Special trick to make the AB3100 use the 32kHz clock (RTC)
	 * bit 3 in test register 0x02 is a special, undocumented test
	 * register bit that only exist in AB3100 P1E
	 */
	if (ab3100->chip_id == 0xc4) {
		dev_warn(ab3100->dev,
			 "AB3100 P1E variant detected, "
			 "forcing chip to 32KHz\n");
		err = ab3100_set_test_register_interruptible(ab3100,
			0x02, 0x08);
	}

 exit_no_setup:
	return err;
}

/*
 * Here we define all the platform devices that appear
 * as children of the AB3100. These are regular platform
 * devices with the IORESOURCE_IO .start and .end set
 * to correspond to the internal AB3100 register range
 * mapping to the corresponding subdevice.
 */

#define AB3100_DEVICE(devname, devid)				\
static struct platform_device ab3100_##devname##_device = {	\
	.name		= devid,				\
	.id		= -1,					\
}

/* This lists all the subdevices */
AB3100_DEVICE(dac, "ab3100-dac");
AB3100_DEVICE(leds, "ab3100-leds");
AB3100_DEVICE(power, "ab3100-power");
AB3100_DEVICE(regulators, "ab3100-regulators");
AB3100_DEVICE(sim, "ab3100-sim");
AB3100_DEVICE(uart, "ab3100-uart");
AB3100_DEVICE(rtc, "ab3100-rtc");
AB3100_DEVICE(charger, "ab3100-charger");
AB3100_DEVICE(boost, "ab3100-boost");
AB3100_DEVICE(adc, "ab3100-adc");
AB3100_DEVICE(fuelgauge, "ab3100-fuelgauge");
AB3100_DEVICE(vibrator, "ab3100-vibrator");
AB3100_DEVICE(otp, "ab3100-otp");
AB3100_DEVICE(codec, "ab3100-codec");

static struct platform_device *
ab3100_platform_devs[] = {
	&ab3100_dac_device,
	&ab3100_leds_device,
	&ab3100_power_device,
	&ab3100_regulators_device,
	&ab3100_sim_device,
	&ab3100_uart_device,
	&ab3100_rtc_device,
	&ab3100_charger_device,
	&ab3100_boost_device,
	&ab3100_adc_device,
	&ab3100_fuelgauge_device,
	&ab3100_vibrator_device,
	&ab3100_otp_device,
	&ab3100_codec_device,
};

struct ab_family_id {
	u8	id;
	char	*name;
};

static const struct ab_family_id ids[] __initdata = {
	/* AB3100 */
	{
		.id = 0xc0,
		.name = "P1A"
	}, {
		.id = 0xc1,
		.name = "P1B"
	}, {
		.id = 0xc2,
		.name = "P1C"
	}, {
		.id = 0xc3,
		.name = "P1D"
	}, {
		.id = 0xc4,
		.name = "P1E"
	}, {
		.id = 0xc5,
		.name = "P1F/R1A"
	}, {
		.id = 0xc6,
		.name = "P1G/R1A"
	}, {
		.id = 0xc7,
		.name = "P2A/R2A"
	}, {
		.id = 0xc8,
		.name = "P2B/R2B"
	},
	/* AB3000 variants, not supported */
	{
		.id = 0xa0
	}, {
		.id = 0xa1
	}, {
		.id = 0xa2
	}, {
		.id = 0xa3
	}, {
		.id = 0xa4
	}, {
		.id = 0xa5
	}, {
		.id = 0xa6
	}, {
		.id = 0xa7
	},
	/* Terminator */
	{
		.id = 0x00,
	},
};

static int __init ab3100_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ab3100 *ab3100;
	struct ab3100_platform_data *ab3100_plf_data =
		client->dev.platform_data;
	int err;
	int i;

	ab3100 = kzalloc(sizeof(struct ab3100), GFP_KERNEL);
	if (!ab3100) {
		dev_err(&client->dev, "could not allocate AB3100 device\n");
		return -ENOMEM;
	}

	/* Initialize data structure */
	mutex_init(&ab3100->access_mutex);
	BLOCKING_INIT_NOTIFIER_HEAD(&ab3100->event_subscribers);

	ab3100->i2c_client = client;
	ab3100->dev = &ab3100->i2c_client->dev;

	i2c_set_clientdata(client, ab3100);

	/* Read chip ID register */
	err = ab3100_get_register_interruptible(ab3100, AB3100_CID,
						&ab3100->chip_id);
	if (err) {
		dev_err(&client->dev,
			"could not communicate with the AB3100 analog "
			"baseband chip\n");
		goto exit_no_detect;
	}

	for (i = 0; ids[i].id != 0x0; i++) {
		if (ids[i].id == ab3100->chip_id) {
			if (ids[i].name != NULL) {
				snprintf(&ab3100->chip_name[0],
					 sizeof(ab3100->chip_name) - 1,
					 "AB3100 %s",
					 ids[i].name);
				break;
			} else {
				dev_err(&client->dev,
					"AB3000 is not supported\n");
				goto exit_no_detect;
			}
		}
	}

	if (ids[i].id == 0x0) {
		dev_err(&client->dev, "unknown analog baseband chip id: 0x%x\n",
			ab3100->chip_id);
		dev_err(&client->dev, "accepting it anyway. Please update "
			"the driver.\n");
		goto exit_no_detect;
	}

	dev_info(&client->dev, "Detected chip: %s\n",
		 &ab3100->chip_name[0]);

	/* Attach a second dummy i2c_client to the test register address */
	ab3100->testreg_client = i2c_new_dummy(client->adapter,
						     client->addr + 1);
	if (!ab3100->testreg_client) {
		err = -ENOMEM;
		goto exit_no_testreg_client;
	}

	err = ab3100_setup(ab3100);
	if (err)
		goto exit_no_setup;

	err = request_threaded_irq(client->irq, NULL, ab3100_irq_handler,
				IRQF_ONESHOT, "ab3100-core", ab3100);
	/* This real unpredictable IRQ is of course sampled for entropy */
	rand_initialize_irq(client->irq);

	if (err)
		goto exit_no_irq;

	err = abx500_register_ops(&client->dev, &ab3100_ops);
	if (err)
		goto exit_no_ops;

	/* Set parent and a pointer back to the container in device data */
	for (i = 0; i < ARRAY_SIZE(ab3100_platform_devs); i++) {
		ab3100_platform_devs[i]->dev.parent =
			&client->dev;
		ab3100_platform_devs[i]->dev.platform_data =
			ab3100_plf_data;
		platform_set_drvdata(ab3100_platform_devs[i], ab3100);
	}

	/* Register the platform devices */
	platform_add_devices(ab3100_platform_devs,
			     ARRAY_SIZE(ab3100_platform_devs));

	ab3100_setup_debugfs(ab3100);

	return 0;

 exit_no_ops:
 exit_no_irq:
 exit_no_setup:
	i2c_unregister_device(ab3100->testreg_client);
 exit_no_testreg_client:
 exit_no_detect:
	kfree(ab3100);
	return err;
}

static int __exit ab3100_remove(struct i2c_client *client)
{
	struct ab3100 *ab3100 = i2c_get_clientdata(client);
	int i;

	/* Unregister subdevices */
	for (i = 0; i < ARRAY_SIZE(ab3100_platform_devs); i++)
		platform_device_unregister(ab3100_platform_devs[i]);

	ab3100_remove_debugfs();
	i2c_unregister_device(ab3100->testreg_client);

	/*
	 * At this point, all subscribers should have unregistered
	 * their notifiers so deactivate IRQ
	 */
	free_irq(client->irq, ab3100);
	kfree(ab3100);
	return 0;
}

static const struct i2c_device_id ab3100_id[] = {
	{ "ab3100", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ab3100_id);

static struct i2c_driver ab3100_driver = {
	.driver = {
		.name	= "ab3100",
		.owner	= THIS_MODULE,
	},
	.id_table	= ab3100_id,
	.probe		= ab3100_probe,
	.remove		= __exit_p(ab3100_remove),
};

static int __init ab3100_i2c_init(void)
{
	return i2c_add_driver(&ab3100_driver);
}

static void __exit ab3100_i2c_exit(void)
{
	i2c_del_driver(&ab3100_driver);
}

subsys_initcall(ab3100_i2c_init);
module_exit(ab3100_i2c_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");
MODULE_DESCRIPTION("AB3100 core driver");
MODULE_LICENSE("GPL");
