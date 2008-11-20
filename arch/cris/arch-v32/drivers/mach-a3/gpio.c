/*
 * Artec-3 general port I/O device
 *
 * Copyright (c) 2007 Axis Communications AB
 *
 * Authors:    Bjorn Wesen      (initial version)
 *             Ola Knutsson     (LED handling)
 *             Johan Adolfsson  (read/set directions, write, port G,
 *                               port to ETRAX FS.
 *             Ricard Wanderlof (PWM for Artpec-3)
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
#include <mach/pinmux.h>

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
#include "../i2c.h"

#define VIRT_I2C_ADDR 0x40
#endif

/* The following gio ports on ARTPEC-3 is available:
 * pa 32 bits
 * pb 32 bits
 * pc 16 bits
 * each port has a rw_px_dout, r_px_din and rw_px_oe register.
 */

#define GPIO_MAJOR 120  /* experimental MAJOR number */

#define I2C_INTERRUPT_BITS 0x300 /* i2c0_done and i2c1_done bits */

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

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
static int virtual_gpio_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg);
#endif
static int gpio_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg);
static ssize_t gpio_write(struct file *file, const char __user *buf,
	size_t count, loff_t *off);
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

static void gpio_set_alarm(struct gpio_private *priv);
static int gpio_leds_ioctl(unsigned int cmd, unsigned long arg);
static int gpio_pwm_ioctl(struct gpio_private *priv, unsigned int cmd,
	unsigned long arg);


/* linked list of alarms to check for */

static struct gpio_private *alarmlist;

static int wanted_interrupts;

static DEFINE_SPINLOCK(gpio_lock);

#define NUM_PORTS (GPIO_MINOR_LAST+1)
#define GIO_REG_RD_ADDR(reg) \
	(unsigned long *)(regi_gio + REG_RD_ADDR_gio_##reg)
#define GIO_REG_WR_ADDR(reg) \
	(unsigned long *)(regi_gio + REG_WR_ADDR_gio_##reg)
static unsigned long led_dummy;
static unsigned long port_d_dummy;	/* Only input on Artpec-3 */
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
static unsigned long port_e_dummy;	/* Non existent on Artpec-3 */
static unsigned long virtual_dummy;
static unsigned long virtual_rw_pv_oe = CONFIG_ETRAX_DEF_GIO_PV_OE;
static unsigned short cached_virtual_gpio_read;
#endif

static unsigned long *data_out[NUM_PORTS] = {
	GIO_REG_WR_ADDR(rw_pa_dout),
	GIO_REG_WR_ADDR(rw_pb_dout),
	&led_dummy,
	GIO_REG_WR_ADDR(rw_pc_dout),
	&port_d_dummy,
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	&port_e_dummy,
	&virtual_dummy,
#endif
};

static unsigned long *data_in[NUM_PORTS] = {
	GIO_REG_RD_ADDR(r_pa_din),
	GIO_REG_RD_ADDR(r_pb_din),
	&led_dummy,
	GIO_REG_RD_ADDR(r_pc_din),
	GIO_REG_RD_ADDR(r_pd_din),
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	&port_e_dummy,
	&virtual_dummy,
#endif
};

static unsigned long changeable_dir[NUM_PORTS] = {
	CONFIG_ETRAX_PA_CHANGEABLE_DIR,
	CONFIG_ETRAX_PB_CHANGEABLE_DIR,
	0,
	CONFIG_ETRAX_PC_CHANGEABLE_DIR,
	0,
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	0,
	CONFIG_ETRAX_PV_CHANGEABLE_DIR,
#endif
};

static unsigned long changeable_bits[NUM_PORTS] = {
	CONFIG_ETRAX_PA_CHANGEABLE_BITS,
	CONFIG_ETRAX_PB_CHANGEABLE_BITS,
	0,
	CONFIG_ETRAX_PC_CHANGEABLE_BITS,
	0,
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	0,
	CONFIG_ETRAX_PV_CHANGEABLE_BITS,
#endif
};

static unsigned long *dir_oe[NUM_PORTS] = {
	GIO_REG_WR_ADDR(rw_pa_oe),
	GIO_REG_WR_ADDR(rw_pb_oe),
	&led_dummy,
	GIO_REG_WR_ADDR(rw_pc_oe),
	&port_d_dummy,
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	&port_e_dummy,
	&virtual_rw_pv_oe,
#endif
};

static void gpio_set_alarm(struct gpio_private *priv)
{
	int bit;
	int intr_cfg;
	int mask;
	int pins;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	intr_cfg = REG_RD_INT(gio, regi_gio, rw_intr_cfg);
	pins = REG_RD_INT(gio, regi_gio, rw_intr_pins);
	mask = REG_RD_INT(gio, regi_gio, rw_intr_mask) & I2C_INTERRUPT_BITS;

	for (bit = 0; bit < 32; bit++) {
		int intr = bit % 8;
		int pin = bit / 8;
		if (priv->minor < GPIO_MINOR_LEDS)
			pin += priv->minor * 4;
		else
			pin += (priv->minor - 1) * 4;

		if (priv->highalarm & (1<<bit)) {
			intr_cfg |= (regk_gio_hi << (intr * 3));
			mask |= 1 << intr;
			wanted_interrupts = mask & 0xff;
			pins |= pin << (intr * 4);
		} else if (priv->lowalarm & (1<<bit)) {
			intr_cfg |= (regk_gio_lo << (intr * 3));
			mask |= 1 << intr;
			wanted_interrupts = mask & 0xff;
			pins |= pin << (intr * 4);
		}
	}

	REG_WR_INT(gio, regi_gio, rw_intr_cfg, intr_cfg);
	REG_WR_INT(gio, regi_gio, rw_intr_pins, pins);
	REG_WR_INT(gio, regi_gio, rw_intr_mask, mask);

	spin_unlock_irqrestore(&gpio_lock, flags);
}

static unsigned int gpio_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int mask = 0;
	struct gpio_private *priv = file->private_data;
	unsigned long data;
	unsigned long tmp;

	if (priv->minor >= GPIO_MINOR_PWM0 &&
	    priv->minor <= GPIO_MINOR_LAST_PWM)
		return 0;

	poll_wait(file, &priv->alarm_wq, wait);
	if (priv->minor <= GPIO_MINOR_D) {
		data = readl(data_in[priv->minor]);
		REG_WR_INT(gio, regi_gio, rw_ack_intr, wanted_interrupts);
		tmp = REG_RD_INT(gio, regi_gio, rw_intr_mask);
		tmp &= I2C_INTERRUPT_BITS;
		tmp |= wanted_interrupts;
		REG_WR_INT(gio, regi_gio, rw_intr_mask, tmp);
	} else
		return 0;

	if ((data & priv->highalarm) || (~data & priv->lowalarm))
		mask = POLLIN|POLLRDNORM;

	DP(printk(KERN_DEBUG "gpio_poll ready: mask 0x%08X\n", mask));
	return mask;
}

static irqreturn_t gpio_interrupt(int irq, void *dev_id)
{
	reg_gio_rw_intr_mask intr_mask;
	reg_gio_r_masked_intr masked_intr;
	reg_gio_rw_ack_intr ack_intr;
	unsigned long flags;
	unsigned long tmp;
	unsigned long tmp2;
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	unsigned char enable_gpiov_ack = 0;
#endif

	/* Find what PA interrupts are active */
	masked_intr = REG_RD(gio, regi_gio, r_masked_intr);
	tmp = REG_TYPE_CONV(unsigned long, reg_gio_r_masked_intr, masked_intr);

	/* Find those that we have enabled */
	spin_lock_irqsave(&gpio_lock, flags);
	tmp &= wanted_interrupts;
	spin_unlock_irqrestore(&gpio_lock, flags);

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

	return IRQ_RETVAL(tmp);
}

static void gpio_write_bit(unsigned long *port, unsigned char data, int bit,
	unsigned char clk_mask, unsigned char data_mask)
{
	unsigned long shadow = readl(port) & ~clk_mask;
	writel(shadow, port);
	if (data & 1 << bit)
		shadow |= data_mask;
	else
		shadow &= ~data_mask;
	writel(shadow, port);
	/* For FPGA: min 5.0ns (DCC) before CCLK high */
	shadow |= clk_mask;
	writel(shadow, port);
}

static void gpio_write_byte(struct gpio_private *priv, unsigned long *port,
		unsigned char data)
{
	int i;

	if (priv->write_msb)
		for (i = 7; i >= 0; i--)
			gpio_write_bit(port, data, i, priv->clk_mask,
				priv->data_mask);
	else
		for (i = 0; i <= 7; i++)
			gpio_write_bit(port, data, i, priv->clk_mask,
				priv->data_mask);
}


static ssize_t gpio_write(struct file *file, const char __user *buf,
	size_t count, loff_t *off)
{
	struct gpio_private *priv = file->private_data;
	unsigned long flags;
	ssize_t retval = count;
	/* Only bits 0-7 may be used for write operations but allow all
	   devices except leds... */
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	if (priv->minor == GPIO_MINOR_V)
		return -EFAULT;
#endif
	if (priv->minor == GPIO_MINOR_LEDS)
		return -EFAULT;

	if (priv->minor >= GPIO_MINOR_PWM0 &&
	    priv->minor <= GPIO_MINOR_LAST_PWM)
		return -EFAULT;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	/* It must have been configured using the IO_CFG_WRITE_MODE */
	/* Perhaps a better error code? */
	if (priv->clk_mask == 0 || priv->data_mask == 0)
		return -EPERM;

	D(printk(KERN_DEBUG "gpio_write: %lu to data 0x%02X clk 0x%02X "
		"msb: %i\n",
		count, priv->data_mask, priv->clk_mask, priv->write_msb));

	spin_lock_irqsave(&gpio_lock, flags);

	while (count--)
		gpio_write_byte(priv, data_out[priv->minor], *buf++);

	spin_unlock_irqrestore(&gpio_lock, flags);
	return retval;
}

static int gpio_open(struct inode *inode, struct file *filp)
{
	struct gpio_private *priv;
	int p = iminor(inode);

	if (p > GPIO_MINOR_LAST_PWM ||
	    (p > GPIO_MINOR_LAST && p < GPIO_MINOR_PWM0))
		return -EINVAL;

	priv = kmalloc(sizeof(struct gpio_private), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	lock_kernel();
	memset(priv, 0, sizeof(*priv));

	priv->minor = p;
	filp->private_data = priv;

	/* initialize the io/alarm struct, not for PWM ports though  */
	if (p <= GPIO_MINOR_LAST) {

		priv->clk_mask = 0;
		priv->data_mask = 0;
		priv->highalarm = 0;
		priv->lowalarm = 0;

		init_waitqueue_head(&priv->alarm_wq);

		/* link it into our alarmlist */
		spin_lock_irq(&gpio_lock);
		priv->next = alarmlist;
		alarmlist = priv;
		spin_unlock_irq(&gpio_lock);
	}

	unlock_kernel();
	return 0;
}

static int gpio_release(struct inode *inode, struct file *filp)
{
	struct gpio_private *p;
	struct gpio_private *todel;
	/* local copies while updating them: */
	unsigned long a_high, a_low;

	/* prepare to free private structure */
	todel = filp->private_data;

	/* unlink from alarmlist - only for non-PWM ports though */
	if (todel->minor <= GPIO_MINOR_LAST) {
		spin_lock_irq(&gpio_lock);
		p = alarmlist;

		if (p == todel)
			alarmlist = todel->next;
		 else {
			while (p->next != todel)
				p = p->next;
			p->next = todel->next;
		}

		/* Check if there are still any alarms set */
		p = alarmlist;
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

			p = p->next;
		}

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	/* Variable 'a_low' needs to be set here again
	 * to ensure that interrupt for virtual GPIO is handled.
	 */
		a_low |= (1 << CONFIG_ETRAX_VIRTUAL_GPIO_INTERRUPT_PA_PIN);
#endif

		spin_unlock_irq(&gpio_lock);
	}
	kfree(todel);

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

	spin_lock_irqsave(&gpio_lock, flags);

	dir_shadow = readl(dir_oe[priv->minor]) &
		~(arg & changeable_dir[priv->minor]);
	writel(dir_shadow, dir_oe[priv->minor]);

	spin_unlock_irqrestore(&gpio_lock, flags);

	if (priv->minor == GPIO_MINOR_C)
		dir_shadow ^= 0xFFFF;		/* Only 16 bits */
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	else if (priv->minor == GPIO_MINOR_V)
		dir_shadow ^= 0xFFFF;		/* Only 16 bits */
#endif
	else
		dir_shadow ^= 0xFFFFFFFF;	/* PA, PB and PD 32 bits */

	return dir_shadow;

} /* setget_input */

static inline unsigned long setget_output(struct gpio_private *priv,
	unsigned long arg)
{
	unsigned long flags;
	unsigned long dir_shadow;

	spin_lock_irqsave(&gpio_lock, flags);

	dir_shadow = readl(dir_oe[priv->minor]) |
		(arg & changeable_dir[priv->minor]);
	writel(dir_shadow, dir_oe[priv->minor]);

	spin_unlock_irqrestore(&gpio_lock, flags);
	return dir_shadow;
} /* setget_output */

static int gpio_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	unsigned long val;
	unsigned long shadow;
	struct gpio_private *priv = file->private_data;

	if (_IOC_TYPE(cmd) != ETRAXGPIO_IOCTYPE)
		return -ENOTTY;

	/* Check for special ioctl handlers first */

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	if (priv->minor == GPIO_MINOR_V)
		return virtual_gpio_ioctl(file, cmd, arg);
#endif

	if (priv->minor == GPIO_MINOR_LEDS)
		return gpio_leds_ioctl(cmd, arg);

	if (priv->minor >= GPIO_MINOR_PWM0 &&
	    priv->minor <= GPIO_MINOR_LAST_PWM)
		return gpio_pwm_ioctl(priv, cmd, arg);

	switch (_IOC_NR(cmd)) {
	case IO_READBITS: /* Use IO_READ_INBITS and IO_READ_OUTBITS instead */
		/* Read the port. */
		return readl(data_in[priv->minor]);
	case IO_SETBITS:
		spin_lock_irqsave(&gpio_lock, flags);
		/* Set changeable bits with a 1 in arg. */
		shadow = readl(data_out[priv->minor]) |
			(arg & changeable_bits[priv->minor]);
		writel(shadow, data_out[priv->minor]);
		spin_unlock_irqrestore(&gpio_lock, flags);
		break;
	case IO_CLRBITS:
		spin_lock_irqsave(&gpio_lock, flags);
		/* Clear changeable bits with a 1 in arg. */
		shadow = readl(data_out[priv->minor]) &
			~(arg & changeable_bits[priv->minor]);
		writel(shadow, data_out[priv->minor]);
		spin_unlock_irqrestore(&gpio_lock, flags);
		break;
	case IO_HIGHALARM:
		/* Set alarm when bits with 1 in arg go high. */
		priv->highalarm |= arg;
		gpio_set_alarm(priv);
		break;
	case IO_LOWALARM:
		/* Set alarm when bits with 1 in arg go low. */
		priv->lowalarm |= arg;
		gpio_set_alarm(priv);
		break;
	case IO_CLRALARM:
		/* Clear alarm for bits with 1 in arg. */
		priv->highalarm &= ~arg;
		priv->lowalarm  &= ~arg;
		gpio_set_alarm(priv);
		break;
	case IO_READDIR: /* Use IO_SETGET_INPUT/OUTPUT instead! */
		/* Read direction 0=input 1=output */
		return readl(dir_oe[priv->minor]);

	case IO_SETINPUT: /* Use IO_SETGET_INPUT instead! */
		/* Set direction 0=unchanged 1=input,
		 * return mask with 1=input
		 */
		return setget_input(priv, arg);

	case IO_SETOUTPUT: /* Use IO_SETGET_OUTPUT instead! */
		/* Set direction 0=unchanged 1=output,
		 * return mask with 1=output
		 */
		return setget_output(priv, arg);

	case IO_CFG_WRITE_MODE:
	{
		int res = -EPERM;
		unsigned long dir_shadow, clk_mask, data_mask, write_msb;

		clk_mask = arg & 0xFF;
		data_mask = (arg >> 8) & 0xFF;
		write_msb = (arg >> 16) & 0x01;

		/* Check if we're allowed to change the bits and
		 * the direction is correct
		 */
		spin_lock_irqsave(&gpio_lock, flags);
		dir_shadow = readl(dir_oe[priv->minor]);
		if ((clk_mask & changeable_bits[priv->minor]) &&
		    (data_mask & changeable_bits[priv->minor]) &&
		    (clk_mask & dir_shadow) &&
		    (data_mask & dir_shadow)) {
			priv->clk_mask = clk_mask;
			priv->data_mask = data_mask;
			priv->write_msb = write_msb;
			res = 0;
		}
		spin_unlock_irqrestore(&gpio_lock, flags);

		return res;
	}
	case IO_READ_INBITS:
		/* *arg is result of reading the input pins */
		val = readl(data_in[priv->minor]);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	case IO_READ_OUTBITS:
		 /* *arg is result of reading the output shadow */
		val = *data_out[priv->minor];
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	case IO_SETGET_INPUT:
		/* bits set in *arg is set to input,
		 * *arg updated with current input pins.
		 */
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		val = setget_input(priv, val);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	case IO_SETGET_OUTPUT:
		/* bits set in *arg is set to output,
		 * *arg updated with current output pins.
		 */
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		val = setget_output(priv, val);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	default:
		return -EINVAL;
	} /* switch */

	return 0;
}

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
static int virtual_gpio_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	unsigned long flags;
	unsigned short val;
	unsigned short shadow;
	struct gpio_private *priv = file->private_data;

	switch (_IOC_NR(cmd)) {
	case IO_SETBITS:
		spin_lock_irqsave(&gpio_lock, flags);
		/* Set changeable bits with a 1 in arg. */
		i2c_read(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));
		shadow |= ~readl(dir_oe[priv->minor]) |
			(arg & changeable_bits[priv->minor]);
		i2c_write(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));
		spin_lock_irqrestore(&gpio_lock, flags);
		break;
	case IO_CLRBITS:
		spin_lock_irqsave(&gpio_lock, flags);
		/* Clear changeable bits with a 1 in arg. */
		i2c_read(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));
		shadow |= ~readl(dir_oe[priv->minor]) &
			~(arg & changeable_bits[priv->minor]);
		i2c_write(VIRT_I2C_ADDR, (void *)&shadow, sizeof(shadow));
		spin_lock_irqrestore(&gpio_lock, flags);
		break;
	case IO_HIGHALARM:
		/* Set alarm when bits with 1 in arg go high. */
		priv->highalarm |= arg;
		break;
	case IO_LOWALARM:
		/* Set alarm when bits with 1 in arg go low. */
		priv->lowalarm |= arg;
		break;
	case IO_CLRALARM:
		/* Clear alarm for bits with 1 in arg. */
		priv->highalarm &= ~arg;
		priv->lowalarm  &= ~arg;
		break;
	case IO_CFG_WRITE_MODE:
	{
		unsigned long dir_shadow;
		dir_shadow = readl(dir_oe[priv->minor]);

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
		val = cached_virtual_gpio_read & ~readl(dir_oe[priv->minor]);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;

	case IO_READ_OUTBITS:
		 /* *arg is result of reading the output shadow */
		i2c_read(VIRT_I2C_ADDR, (void *)&val, sizeof(val));
		val &= readl(dir_oe[priv->minor]);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	case IO_SETGET_INPUT:
	{
		/* bits set in *arg is set to input,
		 * *arg updated with current input pins.
		 */
		unsigned short input_mask = ~readl(dir_oe[priv->minor]);
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		val = setget_input(priv, val);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
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
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		val = setget_output(priv, val);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	default:
		return -EINVAL;
	} /* switch */
	return 0;
}
#endif /* CONFIG_ETRAX_VIRTUAL_GPIO */

static int gpio_leds_ioctl(unsigned int cmd, unsigned long arg)
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

static int gpio_pwm_set_mode(unsigned long arg, int pwm_port)
{
	int pinmux_pwm = pinmux_pwm0 + pwm_port;
	int mode;
	reg_gio_rw_pwm0_ctrl rw_pwm_ctrl = {
		.ccd_val = 0,
		.ccd_override = regk_gio_no,
		.mode = regk_gio_no
	};
	int allocstatus;

	if (get_user(mode, &((struct io_pwm_set_mode *) arg)->mode))
		return -EFAULT;
	rw_pwm_ctrl.mode = mode;
	if (mode != PWM_OFF)
		allocstatus = crisv32_pinmux_alloc_fixed(pinmux_pwm);
	else
		allocstatus = crisv32_pinmux_dealloc_fixed(pinmux_pwm);
	if (allocstatus)
		return allocstatus;
	REG_WRITE(reg_gio_rw_pwm0_ctrl, REG_ADDR(gio, regi_gio, rw_pwm0_ctrl) +
		12 * pwm_port, rw_pwm_ctrl);
	return 0;
}

static int gpio_pwm_set_period(unsigned long arg, int pwm_port)
{
	struct io_pwm_set_period periods;
	reg_gio_rw_pwm0_var rw_pwm_widths;

	if (copy_from_user(&periods, (void __user *)arg, sizeof(periods)))
		return -EFAULT;
	if (periods.lo > 8191 || periods.hi > 8191)
		return -EINVAL;
	rw_pwm_widths.lo = periods.lo;
	rw_pwm_widths.hi = periods.hi;
	REG_WRITE(reg_gio_rw_pwm0_var, REG_ADDR(gio, regi_gio, rw_pwm0_var) +
		12 * pwm_port, rw_pwm_widths);
	return 0;
}

static int gpio_pwm_set_duty(unsigned long arg, int pwm_port)
{
	unsigned int duty;
	reg_gio_rw_pwm0_data rw_pwm_duty;

	if (get_user(duty, &((struct io_pwm_set_duty *) arg)->duty))
		return -EFAULT;
	if (duty > 255)
		return -EINVAL;
	rw_pwm_duty.data = duty;
	REG_WRITE(reg_gio_rw_pwm0_data, REG_ADDR(gio, regi_gio, rw_pwm0_data) +
		12 * pwm_port, rw_pwm_duty);
	return 0;
}

static int gpio_pwm_ioctl(struct gpio_private *priv, unsigned int cmd,
	unsigned long arg)
{
	int pwm_port = priv->minor - GPIO_MINOR_PWM0;

	switch (_IOC_NR(cmd)) {
	case IO_PWM_SET_MODE:
		return gpio_pwm_set_mode(arg, pwm_port);
	case IO_PWM_SET_PERIOD:
		return gpio_pwm_set_period(arg, pwm_port);
	case IO_PWM_SET_DUTY:
		return gpio_pwm_set_duty(arg, pwm_port);
	default:
		return -EINVAL;
	}
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
static void __init virtual_gpio_init(void)
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
}
#endif

/* main driver initialization routine, called from mem.c */

static int __init gpio_init(void)
{
	int res;

	printk(KERN_INFO "ETRAX FS GPIO driver v2.7, (c) 2003-2008 "
		"Axis Communications AB\n");

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

	int res2 = request_irq(GIO_INTR_VECT, gpio_interrupt,
		IRQF_SHARED | IRQF_DISABLED, "gpio", &alarmlist);
	if (res2) {
		printk(KERN_ERR "err: irq for gpio\n");
		return res2;
	}

	/* No IRQs by default. */
	REG_WR_INT(gio, regi_gio, rw_intr_pins, 0);

#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
	virtual_gpio_init();
#endif

	return res;
}

/* this makes sure that gpio_init is called during kernel boot */

module_init(gpio_init);
