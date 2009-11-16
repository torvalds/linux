/*
 * ETRAX CRISv32 general port I/O device
 *
 * Copyright (c) 1999-2006 Axis Communications AB
 *
 * Authors:    Bjorn Wesen      (initial version)
 *             Ola Knutsson     (LED handling)
 *             Johan Adolfsson  (read/set directions, write, port G,
 *                               port to ETRAX FS.
 *
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>

#include <asm/etraxgpio.h>
#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/gio_defs.h>
#include <hwregs/intr_vect_defs.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/irq.h>

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
#include "../i2c.h"

#define VIRT_I2C_ADDR 0x40
#endif

/* The following gio ports on ETRAX FS is available:
 * pa  8 bits, supports interrupts off, hi, low, set, posedge, negedge anyedge
 * pb 18 bits
 * pc 18 bits
 * pd 18 bits
 * pe 18 bits
 * each port has a rw_px_dout, r_px_din and rw_px_oe register.
 */

#define GPIO_MAJOR 120  /* experimental MAJOR number */

#define D(x)

#if 0
static int dp_cnt;
#define DP(x) \
	do { \
		dp_cnt++; \
		if (dp_cnt % 1000 == 0) \
			x; \
	} while (0)
#else
#define DP(x)
#endif

static char gpio_name[] = "etrax gpio";

#if 0
static wait_queue_head_t *gpio_wq;
#endif

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
static int virtual_gpio_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg);
#endif
static int gpio_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg);
static ssize_t gpio_write(struct file *file, const char *buf, size_t count,
	loff_t *off);
static int gpio_open(struct inode *inode, struct file *filp);
static int gpio_release(struct inode *inode, struct file *filp);
static unsigned int gpio_poll(struct file *filp,
	struct poll_table_struct *wait);

/* private data per open() of this driver */

struct gpio_private {
	struct gpio_private *next;
	/* The IO_CFG_WRITE_MODE_VALUE only support 8 bits: */
	unsigned char clk_mask;
	unsigned char data_mask;
	unsigned char write_msb;
	unsigned char pad1;
	/* These fields are generic */
	unsigned long highalarm, lowalarm;
	wait_queue_head_t alarm_wq;
	int minor;
};

/* linked list of alarms to check for */

static struct gpio_private *alarmlist;

static int gpio_some_alarms; /* Set if someone uses alarm */
static unsigned long gpio_pa_high_alarms;
static unsigned long gpio_pa_low_alarms;

static DEFINE_SPINLOCK(alarm_lock);

#define NUM_PORTS (GPIO_MINOR_LAST+1)
#define GIO_REG_RD_ADDR(reg) \
	(volatile unsigned long *)(regi_gio + REG_RD_ADDR_gio_##reg)
#define GIO_REG_WR_ADDR(reg) \
	(volatile unsigned long *)(regi_gio + REG_RD_ADDR_gio_##reg)
unsigned long led_dummy;
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
static unsigned long virtual_dummy;
static unsigned long virtual_rw_pv_oe = CONFIG_ETRAX_DEF_GIO_PV_OE;
static unsigned short cached_virtual_gpio_read;
#endif

static volatile unsigned long *data_out[NUM_PORTS] = {
	GIO_REG_WR_ADDR(rw_pa_dout),
	GIO_REG_WR_ADDR(rw_pb_dout),
	&led_dummy,
	GIO_REG_WR_ADDR(rw_pc_dout),
	GIO_REG_WR_ADDR(rw_pd_dout),
	GIO_REG_WR_ADDR(rw_pe_dout),
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	&virtual_dummy,
#endif
};

static volatile unsigned long *data_in[NUM_PORTS] = {
	GIO_REG_RD_ADDR(r_pa_din),
	GIO_REG_RD_ADDR(r_pb_din),
	&led_dummy,
	GIO_REG_RD_ADDR(r_pc_din),
	GIO_REG_RD_ADDR(r_pd_din),
	GIO_REG_RD_ADDR(r_pe_din),
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	&virtual_dummy,
#endif
};

static unsigned long changeable_dir[NUM_PORTS] = {
	CONFIG_ETRAX_PA_CHANGEABLE_DIR,
	CONFIG_ETRAX_PB_CHANGEABLE_DIR,
	0,
	CONFIG_ETRAX_PC_CHANGEABLE_DIR,
	CONFIG_ETRAX_PD_CHANGEABLE_DIR,
	CONFIG_ETRAX_PE_CHANGEABLE_DIR,
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	CONFIG_ETRAX_PV_CHANGEABLE_DIR,
#endif
};

static unsigned long changeable_bits[NUM_PORTS] = {
	CONFIG_ETRAX_PA_CHANGEABLE_BITS,
	CONFIG_ETRAX_PB_CHANGEABLE_BITS,
	0,
	CONFIG_ETRAX_PC_CHANGEABLE_BITS,
	CONFIG_ETRAX_PD_CHANGEABLE_BITS,
	CONFIG_ETRAX_PE_CHANGEABLE_BITS,
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	CONFIG_ETRAX_PV_CHANGEABLE_BITS,
#endif
};

static volatile unsigned long *dir_oe[NUM_PORTS] = {
	GIO_REG_WR_ADDR(rw_pa_oe),
	GIO_REG_WR_ADDR(rw_pb_oe),
	&led_dummy,
	GIO_REG_WR_ADDR(rw_pc_oe),
	GIO_REG_WR_ADDR(rw_pd_oe),
	GIO_REG_WR_ADDR(rw_pe_oe),
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	&virtual_rw_pv_oe,
#endif
};



static unsigned int gpio_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int mask = 0;
	struct gpio_private *priv = (struct gpio_private *)file->private_data;
	unsigned long data;
	poll_wait(file, &priv->alarm_wq, wait);
	if (priv->minor == GPIO_MINOR_A) {
		reg_gio_rw_intr_cfg intr_cfg;
		unsigned long tmp;
		unsigned long flags;

		local_irq_save(flags);
		data = REG_TYPE_CONV(unsigned long, reg_gio_r_pa_din,
			REG_RD(gio, regi_gio, r_pa_din));
		/* PA has support for interrupt
		 * lets activate high for those low and with highalarm set
		 */
		intr_cfg = REG_RD(gio, regi_gio, rw_intr_cfg);

		tmp = ~data & priv->highalarm & 0xFF;
		if (tmp & (1 << 0))
			intr_cfg.pa0 = regk_gio_hi;
		if (tmp & (1 << 1))
			intr_cfg.pa1 = regk_gio_hi;
		if (tmp & (1 << 2))
			intr_cfg.pa2 = regk_gio_hi;
		if (tmp & (1 << 3))
			intr_cfg.pa3 = regk_gio_hi;
		if (tmp & (1 << 4))
			intr_cfg.pa4 = regk_gio_hi;
		if (tmp & (1 << 5))
			intr_cfg.pa5 = regk_gio_hi;
		if (tmp & (1 << 6))
			intr_cfg.pa6 = regk_gio_hi;
		if (tmp & (1 << 7))
			intr_cfg.pa7 = regk_gio_hi;
		/*
		 * lets activate low for those high and with lowalarm set
		 */
		tmp = data & priv->lowalarm & 0xFF;
		if (tmp & (1 << 0))
			intr_cfg.pa0 = regk_gio_lo;
		if (tmp & (1 << 1))
			intr_cfg.pa1 = regk_gio_lo;
		if (tmp & (1 << 2))
			intr_cfg.pa2 = regk_gio_lo;
		if (tmp & (1 << 3))
			intr_cfg.pa3 = regk_gio_lo;
		if (tmp & (1 << 4))
			intr_cfg.pa4 = regk_gio_lo;
		if (tmp & (1 << 5))
			intr_cfg.pa5 = regk_gio_lo;
		if (tmp & (1 << 6))
			intr_cfg.pa6 = regk_gio_lo;
		if (tmp & (1 << 7))
			intr_cfg.pa7 = regk_gio_lo;

		REG_WR(gio, regi_gio, rw_intr_cfg, intr_cfg);
		local_irq_restore(flags);
	} else if (priv->minor <= GPIO_MINOR_E)
		data = *data_in[priv->minor];
	else
		return 0;

	if ((data & priv->highalarm) || (~data & priv->lowalarm))
		mask = POLLIN|POLLRDNORM;

	DP(printk(KERN_DEBUG "gpio_poll ready: mask 0x%08X\n", mask));
	return mask;
}

int etrax_gpio_wake_up_check(void)
{
	struct gpio_private *priv;
	unsigned long data = 0;
	unsigned long flags;
	int ret = 0;
	spin_lock_irqsave(&alarm_lock, flags);
	priv = alarmlist;
	while (priv) {
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
		if (priv->minor == GPIO_MINOR_V)
			data = (unsigned long)cached_virtual_gpio_read;
		else {
			data = *data_in[priv->minor];
			if (priv->minor == GPIO_MINOR_A)
				priv->lowalarm |= (1 << CONFIG_ETRAX_VIRTUAL_GPIO_INTERRUPT_PA_PIN);
		}
#else
		data = *data_in[priv->minor];
#endif
		if ((data & priv->highalarm) ||
		    (~data & priv->lowalarm)) {
			DP(printk(KERN_DEBUG
				"etrax_gpio_wake_up_check %i\n", priv->minor));
			wake_up_interruptible(&priv->alarm_wq);
			ret = 1;
		}
		priv = priv->next;
	}
	spin_unlock_irqrestore(&alarm_lock, flags);
	return ret;
}

static irqreturn_t
gpio_poll_timer_interrupt(int irq, void *dev_id)
{
	if (gpio_some_alarms)
		return IRQ_RETVAL(etrax_gpio_wake_up_check());
	return IRQ_NONE;
}

static irqreturn_t
gpio_pa_interrupt(int irq, void *dev_id)
{
	reg_gio_rw_intr_mask intr_mask;
	reg_gio_r_masked_intr masked_intr;
	reg_gio_rw_ack_intr ack_intr;
	unsigned long tmp;
	unsigned long tmp2;
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	unsigned char enable_gpiov_ack = 0;
#endif

	/* Find what PA interrupts are active */
	masked_intr = REG_RD(gio, regi_gio, r_masked_intr);
	tmp = REG_TYPE_CONV(unsigned long, reg_gio_r_masked_intr, masked_intr);

	/* Find those that we have enabled */
	spin_lock(&alarm_lock);
	tmp &= (gpio_pa_high_alarms | gpio_pa_low_alarms);
	spin_unlock(&alarm_lock);

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	/* Something changed on virtual GPIO. Interrupt is acked by
	 * reading the device.
	 */
	if (tmp & (1 << CONFIG_ETRAX_VIRTUAL_GPIO_INTERRUPT_PA_PIN)) {
		i2c_read(VIRT_I2C_ADDR, (void *)&cached_virtual_gpio_read,
			sizeof(cached_virtual_gpio_read));
		enable_gpiov_ack = 1;
	}
#endif

	/* Ack them */
	ack_intr = REG_TYPE_CONV(reg_gio_rw_ack_intr, unsigned long, tmp);
	REG_WR(gio, regi_gio, rw_ack_intr, ack_intr);

	/* Disable those interrupts.. */
	intr_mask = REG_RD(gio, regi_gio, rw_intr_mask);
	tmp2 = REG_TYPE_CONV(unsigned long, reg_gio_rw_intr_mask, intr_mask);
	tmp2 &= ~tmp;
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	/* Do not disable interrupt on virtual GPIO. Changes on virtual
	 * pins are only noticed by an interrupt.
	 */
	if (enable_gpiov_ack)
		tmp2 |= (1 << CONFIG_ETRAX_VIRTUAL_GPIO_INTERRUPT_PA_PIN);
#endif
	intr_mask = REG_TYPE_CONV(reg_gio_rw_intr_mask, unsigned long, tmp2);
	REG_WR(gio, regi_gio, rw_intr_mask, intr_mask);

	if (gpio_some_alarms)
		return IRQ_RETVAL(etrax_gpio_wake_up_check());
	return IRQ_NONE;
}


static ssize_t gpio_write(struct file *file, const char *buf, size_t count,
	loff_t *off)
{
	struct gpio_private *priv = (struct gpio_private *)file->private_data;
	unsigned char data, clk_mask, data_mask, write_msb;
	unsigned long flags;
	unsigned long shadow;
	volatile unsigned long *port;
	ssize_t retval = count;
	/* Only bits 0-7 may be used for write operations but allow all
	   devices except leds... */
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	if (priv->minor == GPIO_MINOR_V)
		return -EFAULT;
#endif
	if (priv->minor == GPIO_MINOR_LEDS)
		return -EFAULT;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;
	clk_mask = priv->clk_mask;
	data_mask = priv->data_mask;
	/* It must have been configured using the IO_CFG_WRITE_MODE */
	/* Perhaps a better error code? */
	if (clk_mask == 0 || data_mask == 0)
		return -EPERM;
	write_msb = priv->write_msb;
	D(printk(KERN_DEBUG "gpio_write: %lu to data 0x%02X clk 0x%02X "
		"msb: %i\n", count, data_mask, clk_mask, write_msb));
	port = data_out[priv->minor];

	while (count--) {
		int i;
		data = *buf++;
		if (priv->write_msb) {
			for (i = 7; i >= 0; i--) {
				local_irq_save(flags);
				shadow = *port;
				*port = shadow &= ~clk_mask;
				if (data & 1<<i)
					*port = shadow |= data_mask;
				else
					*port = shadow &= ~data_mask;
			/* For FPGA: min 5.0ns (DCC) before CCLK high */
				*port = shadow |= clk_mask;
				local_irq_restore(flags);
			}
		} else {
			for (i = 0; i <= 7; i++) {
				local_irq_save(flags);
				shadow = *port;
				*port = shadow &= ~clk_mask;
				if (data & 1<<i)
					*port = shadow |= data_mask;
				else
					*port = shadow &= ~data_mask;
			/* For FPGA: min 5.0ns (DCC) before CCLK high */
				*port = shadow |= clk_mask;
				local_irq_restore(flags);
			}
		}
	}
	return retval;
}



static int
gpio_open(struct inode *inode, struct file *filp)
{
	struct gpio_private *priv;
	int p = iminor(inode);

	if (p > GPIO_MINOR_LAST)
		return -EINVAL;

	priv = kmalloc(sizeof(struct gpio_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	lock_kernel();
	memset(priv, 0, sizeof(*priv));

	priv->minor = p;

	/* initialize the io/alarm struct */

	priv->clk_mask = 0;
	priv->data_mask = 0;
	priv->highalarm = 0;
	priv->lowalarm = 0;
	init_waitqueue_head(&priv->alarm_wq);

	filp->private_data = (void *)priv;

	/* link it into our alarmlist */
	spin_lock_irq(&alarm_lock);
	priv->next = alarmlist;
	alarmlist = priv;
	spin_unlock_irq(&alarm_lock);

	unlock_kernel();
	return 0;
}

static int
gpio_release(struct inode *inode, struct file *filp)
{
	struct gpio_private *p;
	struct gpio_private *todel;
	/* local copies while updating them: */
	unsigned long a_high, a_low;
	unsigned long some_alarms;

	/* unlink from alarmlist and free the private structure */

	spin_lock_irq(&alarm_lock);
	p = alarmlist;
	todel = (struct gpio_private *)filp->private_data;

	if (p == todel) {
		alarmlist = todel->next;
	} else {
		while (p->next != todel)
			p = p->next;
		p->next = todel->next;
	}

	kfree(todel);
	/* Check if there are still any alarms set */
	p = alarmlist;
	some_alarms = 0;
	a_high = 0;
	a_low = 0;
	while (p) {
		if (p->minor == GPIO_MINOR_A) {
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
			p->lowalarm |= (1 << CONFIG_ETRAX_VIRTUAL_GPIO_INTERRUPT_PA_PIN);
#endif
			a_high |= p->highalarm;
			a_low |= p->lowalarm;
		}

		if (p->highalarm | p->lowalarm)
			some_alarms = 1;
		p = p->next;
	}

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	/* Variables 'some_alarms' and 'a_low' needs to be set here again
	 * to ensure that interrupt for virtual GPIO is handled.
	 */
	some_alarms = 1;
	a_low |= (1 << CONFIG_ETRAX_VIRTUAL_GPIO_INTERRUPT_PA_PIN);
#endif

	gpio_some_alarms = some_alarms;
	gpio_pa_high_alarms = a_high;
	gpio_pa_low_alarms = a_low;
	spin_unlock_irq(&alarm_lock);

	return 0;
}

/* Main device API. ioctl's to read/set/clear bits, as well as to
 * set alarms to wait for using a subsequent select().
 */

inline unsigned long setget_input(struct gpio_private *priv, unsigned long arg)
{
	/* Set direction 0=unchanged 1=input,
	 * return mask with 1=input
	 */
	unsigned long flags;
	unsigned long dir_shadow;

	local_irq_save(flags);
	dir_shadow = *dir_oe[priv->minor];
	dir_shadow &= ~(arg & changeable_dir[priv->minor]);
	*dir_oe[priv->minor] = dir_shadow;
	local_irq_restore(flags);

	if (priv->minor == GPIO_MINOR_A)
		dir_shadow ^= 0xFF;    /* Only 8 bits */
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	else if (priv->minor == GPIO_MINOR_V)
		dir_shadow ^= 0xFFFF;  /* Only 16 bits */
#endif
	else
		dir_shadow ^= 0x3FFFF; /* Only 18 bits */
	return dir_shadow;

} /* setget_input */

inline unsigned long setget_output(struct gpio_private *priv, unsigned long arg)
{
	unsigned long flags;
	unsigned long dir_shadow;

	local_irq_save(flags);
	dir_shadow = *dir_oe[priv->minor];
	dir_shadow |=  (arg & changeable_dir[priv->minor]);
	*dir_oe[priv->minor] = dir_shadow;
	local_irq_restore(flags);
	return dir_shadow;
} /* setget_output */

static int
gpio_leds_ioctl(unsigned int cmd, unsigned long arg);

static int
gpio_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	unsigned long val;
	unsigned long shadow;
	struct gpio_private *priv = (struct gpio_private *)file->private_data;
	if (_IOC_TYPE(cmd) != ETRAXGPIO_IOCTYPE)
		return -EINVAL;

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	if (priv->minor == GPIO_MINOR_V)
		return virtual_gpio_ioctl(file, cmd, arg);
#endif

	switch (_IOC_NR(cmd)) {
	case IO_READBITS: /* Use IO_READ_INBITS and IO_READ_OUTBITS instead */
		/* Read the port. */
		return *data_in[priv->minor];
		break;
	case IO_SETBITS:
		local_irq_save(flags);
		/* Set changeable bits with a 1 in arg. */
		shadow = *data_out[priv->minor];
		shadow |=  (arg & changeable_bits[priv->minor]);
		*data_out[priv->minor] = shadow;
		local_irq_restore(flags);
		break;
	case IO_CLRBITS:
		local_irq_save(flags);
		/* Clear changeable bits with a 1 in arg. */
		shadow = *data_out[priv->minor];
		shadow &=  ~(arg & changeable_bits[priv->minor]);
		*data_out[priv->minor] = shadow;
		local_irq_restore(flags);
		break;
	case IO_HIGHALARM:
		/* Set alarm when bits with 1 in arg go high. */
		priv->highalarm |= arg;
		spin_lock_irqsave(&alarm_lock, flags);
		gpio_some_alarms = 1;
		if (priv->minor == GPIO_MINOR_A)
			gpio_pa_high_alarms |= arg;
		spin_unlock_irqrestore(&alarm_lock, flags);
		break;
	case IO_LOWALARM:
		/* Set alarm when bits with 1 in arg go low. */
		priv->lowalarm |= arg;
		spin_lock_irqsave(&alarm_lock, flags);
		gpio_some_alarms = 1;
		if (priv->minor == GPIO_MINOR_A)
			gpio_pa_low_alarms |= arg;
		spin_unlock_irqrestore(&alarm_lock, flags);
		break;
	case IO_CLRALARM:
		/* Clear alarm for bits with 1 in arg. */
		priv->highalarm &= ~arg;
		priv->lowalarm  &= ~arg;
		spin_lock_irqsave(&alarm_lock, flags);
		if (priv->minor == GPIO_MINOR_A) {
			if (gpio_pa_high_alarms & arg ||
			    gpio_pa_low_alarms & arg)
				/* Must update the gpio_pa_*alarms masks */
				;
		}
		spin_unlock_irqrestore(&alarm_lock, flags);
		break;
	case IO_READDIR: /* Use IO_SETGET_INPUT/OUTPUT instead! */
		/* Read direction 0=input 1=output */
		return *dir_oe[priv->minor];
	case IO_SETINPUT: /* Use IO_SETGET_INPUT instead! */
		/* Set direction 0=unchanged 1=input,
		 * return mask with 1=input
		 */
		return setget_input(priv, arg);
		break;
	case IO_SETOUTPUT: /* Use IO_SETGET_OUTPUT instead! */
		/* Set direction 0=unchanged 1=output,
		 * return mask with 1=output
		 */
		return setget_output(priv, arg);

	case IO_CFG_WRITE_MODE:
	{
		unsigned long dir_shadow;
		dir_shadow = *dir_oe[priv->minor];

		priv->clk_mask = arg & 0xFF;
		priv->data_mask = (arg >> 8) & 0xFF;
		priv->write_msb = (arg >> 16) & 0x01;
		/* Check if we're allowed to change the bits and
		 * the direction is correct
		 */
		if (!((priv->clk_mask & changeable_bits[priv->minor]) &&
		      (priv->data_mask & changeable_bits[priv->minor]) &&
		      (priv->clk_mask & dir_shadow) &&
		      (priv->data_mask & dir_shadow))) {
			priv->clk_mask = 0;
			priv->data_mask = 0;
			return -EPERM;
		}
		break;
	}
	case IO_READ_INBITS:
		/* *arg is result of reading the input pins */
		val = *data_in[priv->minor];
		if (copy_to_user((unsigned long *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;
		break;
	case IO_READ_OUTBITS:
		 /* *arg is result of reading the output shadow */
		val = *data_out[priv->minor];
		if (copy_to_user((unsigned long *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	case IO_SETGET_INPUT:
		/* bits set in *arg is set to input,
		 * *arg updated with current input pins.
		 */
		if (copy_from_user(&val, (unsigned long *)arg, sizeof(val)))
			return -EFAULT;
		val = setget_input(priv, val);
		if (copy_to_user((unsigned long *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	case IO_SETGET_OUTPUT:
		/* bits set in *arg is set to output,
		 * *arg updated with current output pins.
		 */
		if (copy_from_user(&val, (unsigned long *)arg, sizeof(val)))
			return -EFAULT;
		val = setget_output(priv, val);
		if (copy_to_user((unsigned long *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	default:
		if (priv->minor == GPIO_MINOR_LEDS)
			return gpio_leds_ioctl(cmd, arg);
		else
			return -EINVAL;
	} /* switch */

	return 0;
}

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
static int
virtual_gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	unsigned short val;
	unsigned short shadow;
	struct gpio_private *priv = (struct gpio_private *)file->private_data;

	switch (_IOC_NR(cmd)) {
	case IO_SETBITS:
		local_irq_save(flags);
		/* Set changeable bits with a 1 in arg. */
		i2c_read(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));
		shadow |= ~*dir_oe[priv->minor];
		shadow |= (arg & changeable_bits[priv->minor]);
		i2c_write(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));
		local_irq_restore(flags);
		break;
	case IO_CLRBITS:
		local_irq_save(flags);
		/* Clear changeable bits with a 1 in arg. */
		i2c_read(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));
		shadow |= ~*dir_oe[priv->minor];
		shadow &= ~(arg & changeable_bits[priv->minor]);
		i2c_write(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));
		local_irq_restore(flags);
		break;
	case IO_HIGHALARM:
		/* Set alarm when bits with 1 in arg go high. */
		priv->highalarm |= arg;
		spin_lock(&alarm_lock);
		gpio_some_alarms = 1;
		spin_unlock(&alarm_lock);
		break;
	case IO_LOWALARM:
		/* Set alarm when bits with 1 in arg go low. */
		priv->lowalarm |= arg;
		spin_lock(&alarm_lock);
		gpio_some_alarms = 1;
		spin_unlock(&alarm_lock);
		break;
	case IO_CLRALARM:
		/* Clear alarm for bits with 1 in arg. */
		priv->highalarm &= ~arg;
		priv->lowalarm  &= ~arg;
		spin_lock(&alarm_lock);
		spin_unlock(&alarm_lock);
		break;
	case IO_CFG_WRITE_MODE:
	{
		unsigned long dir_shadow;
		dir_shadow = *dir_oe[priv->minor];

		priv->clk_mask = arg & 0xFF;
		priv->data_mask = (arg >> 8) & 0xFF;
		priv->write_msb = (arg >> 16) & 0x01;
		/* Check if we're allowed to change the bits and
		 * the direction is correct
		 */
		if (!((priv->clk_mask & changeable_bits[priv->minor]) &&
		      (priv->data_mask & changeable_bits[priv->minor]) &&
		      (priv->clk_mask & dir_shadow) &&
		      (priv->data_mask & dir_shadow))) {
			priv->clk_mask = 0;
			priv->data_mask = 0;
			return -EPERM;
		}
		break;
	}
	case IO_READ_INBITS:
		/* *arg is result of reading the input pins */
		val = cached_virtual_gpio_read;
		val &= ~*dir_oe[priv->minor];
		if (copy_to_user((unsigned long *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;
		break;
	case IO_READ_OUTBITS:
		 /* *arg is result of reading the output shadow */
		i2c_read(VIRT_I2C_ADDR, (void *)&val, sizeof(val));
		val &= *dir_oe[priv->minor];
		if (copy_to_user((unsigned long *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	case IO_SETGET_INPUT:
	{
		/* bits set in *arg is set to input,
		 * *arg updated with current input pins.
		 */
		unsigned short input_mask = ~*dir_oe[priv->minor];
		if (copy_from_user(&val, (unsigned long *)arg, sizeof(val)))
			return -EFAULT;
		val = setget_input(priv, val);
		if (copy_to_user((unsigned long *)arg, &val, sizeof(val)))
			return -EFAULT;
		if ((input_mask & val) != input_mask) {
			/* Input pins changed. All ports desired as input
			 * should be set to logic 1.
			 */
			unsigned short change = input_mask ^ val;
			i2c_read(VIRT_I2C_ADDR, (void *)&shadow,
				sizeof(shadow));
			shadow &= ~change;
			shadow |= val;
			i2c_write(VIRT_I2C_ADDR, (void *)&shadow,
				sizeof(shadow));
		}
		break;
	}
	case IO_SETGET_OUTPUT:
		/* bits set in *arg is set to output,
		 * *arg updated with current output pins.
		 */
		if (copy_from_user(&val, (unsigned long *)arg, sizeof(val)))
			return -EFAULT;
		val = setget_output(priv, val);
		if (copy_to_user((unsigned long *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	default:
		return -EINVAL;
	} /* switch */
  return 0;
}
#endif /* CONFIG_ETRAX_VIRTUAL_GPIO */

static int
gpio_leds_ioctl(unsigned int cmd, unsigned long arg)
{
	unsigned char green;
	unsigned char red;

	switch (_IOC_NR(cmd)) {
	case IO_LEDACTIVE_SET:
		green = ((unsigned char) arg) & 1;
		red   = (((unsigned char) arg) >> 1) & 1;
		CRIS_LED_ACTIVE_SET_G(green);
		CRIS_LED_ACTIVE_SET_R(red);
		break;

	default:
		return -EINVAL;
	} /* switch */

	return 0;
}

static const struct file_operations gpio_fops = {
	.owner       = THIS_MODULE,
	.poll        = gpio_poll,
	.ioctl       = gpio_ioctl,
	.write       = gpio_write,
	.open        = gpio_open,
	.release     = gpio_release,
};

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
static void
virtual_gpio_init(void)
{
	reg_gio_rw_intr_cfg intr_cfg;
	reg_gio_rw_intr_mask intr_mask;
	unsigned short shadow;

	shadow = ~virtual_rw_pv_oe; /* Input ports should be set to logic 1 */
	shadow |= CONFIG_ETRAX_DEF_GIO_PV_OUT;
	i2c_write(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));

	/* Set interrupt mask and on what state the interrupt shall trigger.
	 * For virtual gpio the interrupt shall trigger on logic '0'.
	 */
	intr_cfg = REG_RD(gio, regi_gio, rw_intr_cfg);
	intr_mask = REG_RD(gio, regi_gio, rw_intr_mask);

	switch (CONFIG_ETRAX_VIRTUAL_GPIO_INTERRUPT_PA_PIN) {
	case 0:
		intr_cfg.pa0 = regk_gio_lo;
		intr_mask.pa0 = regk_gio_yes;
		break;
	case 1:
		intr_cfg.pa1 = regk_gio_lo;
		intr_mask.pa1 = regk_gio_yes;
		break;
	case 2:
		intr_cfg.pa2 = regk_gio_lo;
		intr_mask.pa2 = regk_gio_yes;
		break;
	case 3:
		intr_cfg.pa3 = regk_gio_lo;
		intr_mask.pa3 = regk_gio_yes;
		break;
	case 4:
		intr_cfg.pa4 = regk_gio_lo;
		intr_mask.pa4 = regk_gio_yes;
		break;
	case 5:
		intr_cfg.pa5 = regk_gio_lo;
		intr_mask.pa5 = regk_gio_yes;
		break;
	case 6:
		intr_cfg.pa6 = regk_gio_lo;
		intr_mask.pa6 = regk_gio_yes;
		break;
	case 7:
		intr_cfg.pa7 = regk_gio_lo;
		intr_mask.pa7 = regk_gio_yes;
	break;
	}

	REG_WR(gio, regi_gio, rw_intr_cfg, intr_cfg);
	REG_WR(gio, regi_gio, rw_intr_mask, intr_mask);

	gpio_pa_low_alarms |= (1 << CONFIG_ETRAX_VIRTUAL_GPIO_INTERRUPT_PA_PIN);
	gpio_some_alarms = 1;
}
#endif

/* main driver initialization routine, called from mem.c */

static __init int
gpio_init(void)
{
	int res;

	/* do the formalities */

	res = register_chrdev(GPIO_MAJOR, gpio_name, &gpio_fops);
	if (res < 0) {
		printk(KERN_ERR "gpio: couldn't get a major number.\n");
		return res;
	}

	/* Clear all leds */
	CRIS_LED_NETWORK_GRP0_SET(0);
	CRIS_LED_NETWORK_GRP1_SET(0);
	CRIS_LED_ACTIVE_SET(0);
	CRIS_LED_DISK_READ(0);
	CRIS_LED_DISK_WRITE(0);

	printk(KERN_INFO "ETRAX FS GPIO driver v2.5, (c) 2003-2007 "
		"Axis Communications AB\n");
	/* We call etrax_gpio_wake_up_check() from timer interrupt and
	 * from cpu_idle() in kernel/process.c
	 * The check in cpu_idle() reduces latency from ~15 ms to ~6 ms
	 * in some tests.
	 */
	if (request_irq(TIMER0_INTR_VECT, gpio_poll_timer_interrupt,
			IRQF_SHARED | IRQF_DISABLED, "gpio poll", &alarmlist))
		printk(KERN_ERR "timer0 irq for gpio\n");

	if (request_irq(GIO_INTR_VECT, gpio_pa_interrupt,
			IRQF_SHARED | IRQF_DISABLED, "gpio PA", &alarmlist))
		printk(KERN_ERR "PA irq for gpio\n");

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	virtual_gpio_init();
#endif

	return res;
}

/* this makes sure that gpio_init is called during kernel boot */

module_init(gpio_init);
