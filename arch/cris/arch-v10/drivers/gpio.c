/*
 * Etrax general port I/O device
 *
 * Copyright (c) 1999-2007 Axis Communications AB
 *
 * Authors:    Bjorn Wesen      (initial version)
 *             Ola Knutsson     (LED handling)
 *             Johan Adolfsson  (read/set directions, write, port G)
 */


#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/string.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/etraxgpio.h>
#include <arch/svinto.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <arch/io_interface_mux.h>

#define GPIO_MAJOR 120  /* experimental MAJOR number */

#define D(x)

#if 0
static int dp_cnt;
#define DP(x) do { dp_cnt++; if (dp_cnt % 1000 == 0) x; }while(0)
#else
#define DP(x)
#endif

static char gpio_name[] = "etrax gpio";

#if 0
static wait_queue_head_t *gpio_wq;
#endif

static int gpio_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg);
static ssize_t gpio_write(struct file *file, const char __user *buf,
	size_t count, loff_t *off);
static int gpio_open(struct inode *inode, struct file *filp);
static int gpio_release(struct inode *inode, struct file *filp);
static unsigned int gpio_poll(struct file *filp, struct poll_table_struct *wait);

/* private data per open() of this driver */

struct gpio_private {
	struct gpio_private *next;
	/* These fields are for PA and PB only */
	volatile unsigned char *port, *shadow;
	volatile unsigned char *dir, *dir_shadow;
	unsigned char changeable_dir;
	unsigned char changeable_bits;
	unsigned char clk_mask;
	unsigned char data_mask;
	unsigned char write_msb;
	unsigned char pad1, pad2, pad3;
	/* These fields are generic */
	unsigned long highalarm, lowalarm;
	wait_queue_head_t alarm_wq;
	int minor;
};

/* linked list of alarms to check for */

static struct gpio_private *alarmlist;

static int gpio_some_alarms; /* Set if someone uses alarm */
static unsigned long gpio_pa_irq_enabled_mask;

static DEFINE_SPINLOCK(gpio_lock); /* Protect directions etc */

/* Port A and B use 8 bit access, but Port G is 32 bit */
#define NUM_PORTS (GPIO_MINOR_B+1)

static volatile unsigned char *ports[NUM_PORTS] = {
	R_PORT_PA_DATA,
	R_PORT_PB_DATA,
};
static volatile unsigned char *shads[NUM_PORTS] = {
	&port_pa_data_shadow,
	&port_pb_data_shadow
};

/* What direction bits that are user changeable 1=changeable*/
#ifndef CONFIG_ETRAX_PA_CHANGEABLE_DIR
#define CONFIG_ETRAX_PA_CHANGEABLE_DIR 0x00
#endif
#ifndef CONFIG_ETRAX_PB_CHANGEABLE_DIR
#define CONFIG_ETRAX_PB_CHANGEABLE_DIR 0x00
#endif

#ifndef CONFIG_ETRAX_PA_CHANGEABLE_BITS
#define CONFIG_ETRAX_PA_CHANGEABLE_BITS 0xFF
#endif
#ifndef CONFIG_ETRAX_PB_CHANGEABLE_BITS
#define CONFIG_ETRAX_PB_CHANGEABLE_BITS 0xFF
#endif


static unsigned char changeable_dir[NUM_PORTS] = {
	CONFIG_ETRAX_PA_CHANGEABLE_DIR,
	CONFIG_ETRAX_PB_CHANGEABLE_DIR
};
static unsigned char changeable_bits[NUM_PORTS] = {
	CONFIG_ETRAX_PA_CHANGEABLE_BITS,
	CONFIG_ETRAX_PB_CHANGEABLE_BITS
};

static volatile unsigned char *dir[NUM_PORTS] = {
	R_PORT_PA_DIR,
	R_PORT_PB_DIR
};

static volatile unsigned char *dir_shadow[NUM_PORTS] = {
	&port_pa_dir_shadow,
	&port_pb_dir_shadow
};

/* All bits in port g that can change dir. */
static const unsigned long int changeable_dir_g_mask = 0x01FFFF01;

/* Port G is 32 bit, handle it special, some bits are both inputs
   and outputs at the same time, only some of the bits can change direction
   and some of them in groups of 8 bit. */
static unsigned long changeable_dir_g;
static unsigned long dir_g_in_bits;
static unsigned long dir_g_out_bits;
static unsigned long dir_g_shadow; /* 1=output */

#define USE_PORTS(priv) ((priv)->minor <= GPIO_MINOR_B)


static unsigned int gpio_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	struct gpio_private *priv = file->private_data;
	unsigned long data;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	poll_wait(file, &priv->alarm_wq, wait);
	if (priv->minor == GPIO_MINOR_A) {
		unsigned long tmp;
		data = *R_PORT_PA_DATA;
		/* PA has support for high level interrupt -
		 * lets activate for those low and with highalarm set
		 */
		tmp = ~data & priv->highalarm & 0xFF;
		tmp = (tmp << R_IRQ_MASK1_SET__pa0__BITNR);

		gpio_pa_irq_enabled_mask |= tmp;
		*R_IRQ_MASK1_SET = tmp;
	} else if (priv->minor == GPIO_MINOR_B)
		data = *R_PORT_PB_DATA;
	else if (priv->minor == GPIO_MINOR_G)
		data = *R_PORT_G_DATA;
	else {
		mask = 0;
		goto out;
	}

	if ((data & priv->highalarm) ||
	    (~data & priv->lowalarm)) {
		mask = POLLIN|POLLRDNORM;
	}

out:
	spin_unlock_irqrestore(&gpio_lock, flags);
	DP(printk("gpio_poll ready: mask 0x%08X\n", mask));

	return mask;
}

int etrax_gpio_wake_up_check(void)
{
	struct gpio_private *priv;
	unsigned long data = 0;
        int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	priv = alarmlist;
	while (priv) {
		if (USE_PORTS(priv))
			data = *priv->port;
		else if (priv->minor == GPIO_MINOR_G)
			data = *R_PORT_G_DATA;

		if ((data & priv->highalarm) ||
		    (~data & priv->lowalarm)) {
			DP(printk("etrax_gpio_wake_up_check %i\n",priv->minor));
			wake_up_interruptible(&priv->alarm_wq);
                        ret = 1;
		}
		priv = priv->next;
	}
	spin_unlock_irqrestore(&gpio_lock, flags);
        return ret;
}

static irqreturn_t
gpio_poll_timer_interrupt(int irq, void *dev_id)
{
	if (gpio_some_alarms) {
		etrax_gpio_wake_up_check();
                return IRQ_HANDLED;
	}
        return IRQ_NONE;
}

static irqreturn_t
gpio_interrupt(int irq, void *dev_id)
{
	unsigned long tmp;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	/* Find what PA interrupts are active */
	tmp = (*R_IRQ_READ1);

	/* Find those that we have enabled */
	tmp &= gpio_pa_irq_enabled_mask;

	/* Clear them.. */
	*R_IRQ_MASK1_CLR = tmp;
	gpio_pa_irq_enabled_mask &= ~tmp;

	spin_unlock_irqrestore(&gpio_lock, flags);

	if (gpio_some_alarms)
		return IRQ_RETVAL(etrax_gpio_wake_up_check());

        return IRQ_NONE;
}

static void gpio_write_bit(struct gpio_private *priv,
	unsigned char data, int bit)
{
	*priv->port = *priv->shadow &= ~(priv->clk_mask);
	if (data & 1 << bit)
		*priv->port = *priv->shadow |= priv->data_mask;
	else
		*priv->port = *priv->shadow &= ~(priv->data_mask);

	/* For FPGA: min 5.0ns (DCC) before CCLK high */
	*priv->port = *priv->shadow |= priv->clk_mask;
}

static void gpio_write_byte(struct gpio_private *priv, unsigned char data)
{
	int i;

	if (priv->write_msb)
		for (i = 7; i >= 0; i--)
			gpio_write_bit(priv, data, i);
	else
		for (i = 0; i <= 7; i++)
			gpio_write_bit(priv, data, i);
}

static ssize_t gpio_write(struct file *file, const char __user *buf,
	size_t count, loff_t *off)
{
	struct gpio_private *priv = file->private_data;
	unsigned long flags;
	ssize_t retval = count;

	if (priv->minor != GPIO_MINOR_A && priv->minor != GPIO_MINOR_B)
		return -EFAULT;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	spin_lock_irqsave(&gpio_lock, flags);

	/* It must have been configured using the IO_CFG_WRITE_MODE */
	/* Perhaps a better error code? */
	if (priv->clk_mask == 0 || priv->data_mask == 0) {
		retval = -EPERM;
		goto out;
	}

	D(printk(KERN_DEBUG "gpio_write: %02X to data 0x%02X "
		"clk 0x%02X msb: %i\n",
		count, priv->data_mask, priv->clk_mask, priv->write_msb));

	while (count--)
		gpio_write_byte(priv, *buf++);

out:
	spin_unlock_irqrestore(&gpio_lock, flags);
	return retval;
}



static int
gpio_open(struct inode *inode, struct file *filp)
{
	struct gpio_private *priv;
	int p = iminor(inode);
	unsigned long flags;

	if (p > GPIO_MINOR_LAST)
		return -EINVAL;

	priv = kzalloc(sizeof(struct gpio_private), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	lock_kernel();
	priv->minor = p;

	/* initialize the io/alarm struct */

	if (USE_PORTS(priv)) { /* A and B */
		priv->port = ports[p];
		priv->shadow = shads[p];
		priv->dir = dir[p];
		priv->dir_shadow = dir_shadow[p];
		priv->changeable_dir = changeable_dir[p];
		priv->changeable_bits = changeable_bits[p];
	} else {
		priv->port = NULL;
		priv->shadow = NULL;
		priv->dir = NULL;
		priv->dir_shadow = NULL;
		priv->changeable_dir = 0;
		priv->changeable_bits = 0;
	}

	priv->highalarm = 0;
	priv->lowalarm = 0;
	priv->clk_mask = 0;
	priv->data_mask = 0;
	init_waitqueue_head(&priv->alarm_wq);

	filp->private_data = priv;

	/* link it into our alarmlist */
	spin_lock_irqsave(&gpio_lock, flags);
	priv->next = alarmlist;
	alarmlist = priv;
	spin_unlock_irqrestore(&gpio_lock, flags);

	unlock_kernel();
	return 0;
}

static int
gpio_release(struct inode *inode, struct file *filp)
{
	struct gpio_private *p;
	struct gpio_private *todel;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	p = alarmlist;
	todel = filp->private_data;

	/* unlink from alarmlist and free the private structure */

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
	while (p) {
		if (p->highalarm | p->lowalarm) {
			gpio_some_alarms = 1;
			goto out;
		}
		p = p->next;
	}
	gpio_some_alarms = 0;
out:
	spin_unlock_irqrestore(&gpio_lock, flags);
	return 0;
}

/* Main device API. ioctl's to read/set/clear bits, as well as to
 * set alarms to wait for using a subsequent select().
 */
unsigned long inline setget_input(struct gpio_private *priv, unsigned long arg)
{
	/* Set direction 0=unchanged 1=input,
	 * return mask with 1=input */
	if (USE_PORTS(priv)) {
		*priv->dir = *priv->dir_shadow &=
		~((unsigned char)arg & priv->changeable_dir);
		return ~(*priv->dir_shadow) & 0xFF; /* Only 8 bits */
	}

	if (priv->minor != GPIO_MINOR_G)
		return 0;

	/* We must fiddle with R_GEN_CONFIG to change dir */
	if (((arg & dir_g_in_bits) != arg) &&
	    (arg & changeable_dir_g)) {
		arg &= changeable_dir_g;
		/* Clear bits in genconfig to set to input */
		if (arg & (1<<0)) {
			genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, g0dir);
			dir_g_in_bits |= (1<<0);
			dir_g_out_bits &= ~(1<<0);
		}
		if ((arg & 0x0000FF00) == 0x0000FF00) {
			genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, g8_15dir);
			dir_g_in_bits |= 0x0000FF00;
			dir_g_out_bits &= ~0x0000FF00;
		}
		if ((arg & 0x00FF0000) == 0x00FF0000) {
			genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, g16_23dir);
			dir_g_in_bits |= 0x00FF0000;
			dir_g_out_bits &= ~0x00FF0000;
		}
		if (arg & (1<<24)) {
			genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, g24dir);
			dir_g_in_bits |= (1<<24);
			dir_g_out_bits &= ~(1<<24);
		}
		D(printk(KERN_DEBUG "gpio: SETINPUT on port G set "
			 "genconfig to 0x%08lX "
			 "in_bits: 0x%08lX "
			 "out_bits: 0x%08lX\n",
			 (unsigned long)genconfig_shadow,
			 dir_g_in_bits, dir_g_out_bits));
		*R_GEN_CONFIG = genconfig_shadow;
		/* Must be a >120 ns delay before writing this again */

	}
	return dir_g_in_bits;
} /* setget_input */

unsigned long inline setget_output(struct gpio_private *priv, unsigned long arg)
{
	if (USE_PORTS(priv)) {
		*priv->dir = *priv->dir_shadow |=
			((unsigned char)arg & priv->changeable_dir);
		return *priv->dir_shadow;
	}
	if (priv->minor != GPIO_MINOR_G)
		return 0;

	/* We must fiddle with R_GEN_CONFIG to change dir */
	if (((arg & dir_g_out_bits) != arg) &&
	    (arg & changeable_dir_g)) {
		/* Set bits in genconfig to set to output */
		if (arg & (1<<0)) {
			genconfig_shadow |= IO_MASK(R_GEN_CONFIG, g0dir);
			dir_g_out_bits |= (1<<0);
			dir_g_in_bits &= ~(1<<0);
		}
		if ((arg & 0x0000FF00) == 0x0000FF00) {
			genconfig_shadow |= IO_MASK(R_GEN_CONFIG, g8_15dir);
			dir_g_out_bits |= 0x0000FF00;
			dir_g_in_bits &= ~0x0000FF00;
		}
		if ((arg & 0x00FF0000) == 0x00FF0000) {
			genconfig_shadow |= IO_MASK(R_GEN_CONFIG, g16_23dir);
			dir_g_out_bits |= 0x00FF0000;
			dir_g_in_bits &= ~0x00FF0000;
		}
		if (arg & (1<<24)) {
			genconfig_shadow |= IO_MASK(R_GEN_CONFIG, g24dir);
			dir_g_out_bits |= (1<<24);
			dir_g_in_bits &= ~(1<<24);
		}
		D(printk(KERN_INFO "gpio: SETOUTPUT on port G set "
			 "genconfig to 0x%08lX "
			 "in_bits: 0x%08lX "
			 "out_bits: 0x%08lX\n",
			 (unsigned long)genconfig_shadow,
			 dir_g_in_bits, dir_g_out_bits));
		*R_GEN_CONFIG = genconfig_shadow;
		/* Must be a >120 ns delay before writing this again */
	}
	return dir_g_out_bits & 0x7FFFFFFF;
} /* setget_output */

static int
gpio_leds_ioctl(unsigned int cmd, unsigned long arg);

static int
gpio_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	unsigned long val;
        int ret = 0;

	struct gpio_private *priv = file->private_data;
	if (_IOC_TYPE(cmd) != ETRAXGPIO_IOCTYPE)
		return -EINVAL;

	spin_lock_irqsave(&gpio_lock, flags);

	switch (_IOC_NR(cmd)) {
	case IO_READBITS: /* Use IO_READ_INBITS and IO_READ_OUTBITS instead */
		// read the port
		if (USE_PORTS(priv)) {
			ret =  *priv->port;
		} else if (priv->minor == GPIO_MINOR_G) {
			ret =  (*R_PORT_G_DATA) & 0x7FFFFFFF;
		}
		break;
	case IO_SETBITS:
		// set changeable bits with a 1 in arg
		if (USE_PORTS(priv)) {
			*priv->port = *priv->shadow |= 
			  ((unsigned char)arg & priv->changeable_bits);
		} else if (priv->minor == GPIO_MINOR_G) {
			*R_PORT_G_DATA = port_g_data_shadow |= (arg & dir_g_out_bits);
		}
		break;
	case IO_CLRBITS:
		// clear changeable bits with a 1 in arg
		if (USE_PORTS(priv)) {
			*priv->port = *priv->shadow &= 
			 ~((unsigned char)arg & priv->changeable_bits);
		} else if (priv->minor == GPIO_MINOR_G) {
			*R_PORT_G_DATA = port_g_data_shadow &= ~((unsigned long)arg & dir_g_out_bits);
		}
		break;
	case IO_HIGHALARM:
		// set alarm when bits with 1 in arg go high
		priv->highalarm |= arg;
		gpio_some_alarms = 1;
		break;
	case IO_LOWALARM:
		// set alarm when bits with 1 in arg go low
		priv->lowalarm |= arg;
		gpio_some_alarms = 1;
		break;
	case IO_CLRALARM:
		// clear alarm for bits with 1 in arg
		priv->highalarm &= ~arg;
		priv->lowalarm  &= ~arg;
		{
			/* Must update gpio_some_alarms */
			struct gpio_private *p = alarmlist;
			int some_alarms;
			spin_lock_irq(&gpio_lock);
			p = alarmlist;
			some_alarms = 0;
			while (p) {
				if (p->highalarm | p->lowalarm) {
					some_alarms = 1;
					break;
				}
				p = p->next;
			}
			gpio_some_alarms = some_alarms;
			spin_unlock_irq(&gpio_lock);
		}
		break;
	case IO_READDIR: /* Use IO_SETGET_INPUT/OUTPUT instead! */
		/* Read direction 0=input 1=output */
		if (USE_PORTS(priv)) {
			ret = *priv->dir_shadow;
		} else if (priv->minor == GPIO_MINOR_G) {
			/* Note: Some bits are both in and out,
			 * Those that are dual is set here as well.
			 */
			ret = (dir_g_shadow | dir_g_out_bits) & 0x7FFFFFFF;
		}
		break;
	case IO_SETINPUT: /* Use IO_SETGET_INPUT instead! */
		/* Set direction 0=unchanged 1=input, 
		 * return mask with 1=input 
		 */
		ret = setget_input(priv, arg) & 0x7FFFFFFF;
		break;
	case IO_SETOUTPUT: /* Use IO_SETGET_OUTPUT instead! */
		/* Set direction 0=unchanged 1=output, 
		 * return mask with 1=output 
		 */
		ret =  setget_output(priv, arg) & 0x7FFFFFFF;
		break;
	case IO_SHUTDOWN:
		SOFT_SHUTDOWN();
		break;
	case IO_GET_PWR_BT:
#if defined (CONFIG_ETRAX_SOFT_SHUTDOWN)
		ret = (*R_PORT_G_DATA & ( 1 << CONFIG_ETRAX_POWERBUTTON_BIT));
#else
		ret = 0;
#endif
		break;
	case IO_CFG_WRITE_MODE:
		priv->clk_mask = arg & 0xFF;
		priv->data_mask = (arg >> 8) & 0xFF;
		priv->write_msb = (arg >> 16) & 0x01;
		/* Check if we're allowed to change the bits and
		 * the direction is correct
		 */
		if (!((priv->clk_mask & priv->changeable_bits) &&
		      (priv->data_mask & priv->changeable_bits) &&
		      (priv->clk_mask & *priv->dir_shadow) &&
		      (priv->data_mask & *priv->dir_shadow)))
		{
			priv->clk_mask = 0;
			priv->data_mask = 0;
			ret = -EPERM;
		}
		break;
	case IO_READ_INBITS: 
		/* *arg is result of reading the input pins */
		if (USE_PORTS(priv)) {
			val = *priv->port;
		} else if (priv->minor == GPIO_MINOR_G) {
			val = *R_PORT_G_DATA;
		}
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			ret = -EFAULT;
		break;
	case IO_READ_OUTBITS:
		 /* *arg is result of reading the output shadow */
		if (USE_PORTS(priv)) {
			val = *priv->shadow;
		} else if (priv->minor == GPIO_MINOR_G) {
			val = port_g_data_shadow;
		}
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			ret = -EFAULT;
		break;
	case IO_SETGET_INPUT: 
		/* bits set in *arg is set to input,
		 * *arg updated with current input pins.
		 */
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
		{
			ret = -EFAULT;
			break;
		}
		val = setget_input(priv, val);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			ret = -EFAULT;
		break;
	case IO_SETGET_OUTPUT:
		/* bits set in *arg is set to output,
		 * *arg updated with current output pins.
		 */
		if (copy_from_user(&val, (void __user *)arg, sizeof(val))) {
			ret = -EFAULT;
			break;
		}
		val = setget_output(priv, val);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			ret = -EFAULT;
		break;
	default:
		if (priv->minor == GPIO_MINOR_LEDS)
			ret = gpio_leds_ioctl(cmd, arg);
		else
			ret = -EINVAL;
	} /* switch */

	spin_unlock_irqrestore(&gpio_lock, flags);
	return ret;
}

static int
gpio_leds_ioctl(unsigned int cmd, unsigned long arg)
{
	unsigned char green;
	unsigned char red;

	switch (_IOC_NR(cmd)) {
	case IO_LEDACTIVE_SET:
		green = ((unsigned char)arg) & 1;
		red   = (((unsigned char)arg) >> 1) & 1;
		CRIS_LED_ACTIVE_SET_G(green);
		CRIS_LED_ACTIVE_SET_R(red);
		break;

	case IO_LED_SETBIT:
		CRIS_LED_BIT_SET(arg);
		break;

	case IO_LED_CLRBIT:
		CRIS_LED_BIT_CLR(arg);
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

static void ioif_watcher(const unsigned int gpio_in_available,
	const unsigned int gpio_out_available,
	const unsigned char pa_available,
	const unsigned char pb_available)
{
	unsigned long int flags;

	D(printk(KERN_DEBUG "gpio.c: ioif_watcher called\n"));
	D(printk(KERN_DEBUG "gpio.c: G in: 0x%08x G out: 0x%08x "
		"PA: 0x%02x PB: 0x%02x\n",
		gpio_in_available, gpio_out_available,
		pa_available, pb_available));

	spin_lock_irqsave(&gpio_lock, flags);

	dir_g_in_bits = gpio_in_available;
	dir_g_out_bits = gpio_out_available;

	/* Initialise the dir_g_shadow etc. depending on genconfig */
	/* 0=input 1=output */
	if (genconfig_shadow & IO_STATE(R_GEN_CONFIG, g0dir, out))
		dir_g_shadow |= (1 << 0);
	if (genconfig_shadow & IO_STATE(R_GEN_CONFIG, g8_15dir, out))
		dir_g_shadow |= 0x0000FF00;
	if (genconfig_shadow & IO_STATE(R_GEN_CONFIG, g16_23dir, out))
		dir_g_shadow |= 0x00FF0000;
	if (genconfig_shadow & IO_STATE(R_GEN_CONFIG, g24dir, out))
		dir_g_shadow |= (1 << 24);

	changeable_dir_g = changeable_dir_g_mask;
	changeable_dir_g &= dir_g_out_bits;
	changeable_dir_g &= dir_g_in_bits;

	/* Correct the bits that can change direction */
	dir_g_out_bits &= ~changeable_dir_g;
	dir_g_out_bits |= dir_g_shadow;
	dir_g_in_bits &= ~changeable_dir_g;
	dir_g_in_bits |= (~dir_g_shadow & changeable_dir_g);

	spin_unlock_irqrestore(&gpio_lock, flags);

	printk(KERN_INFO "GPIO port G: in_bits: 0x%08lX out_bits: 0x%08lX "
		"val: %08lX\n",
	       dir_g_in_bits, dir_g_out_bits, (unsigned long)*R_PORT_G_DATA);
	printk(KERN_INFO "GPIO port G: dir: %08lX changeable: %08lX\n",
	       dir_g_shadow, changeable_dir_g);
}

/* main driver initialization routine, called from mem.c */

static int __init gpio_init(void)
{
	int res;
#if defined (CONFIG_ETRAX_CSP0_LEDS)
	int i;
#endif

	res = register_chrdev(GPIO_MAJOR, gpio_name, &gpio_fops);
	if (res < 0) {
		printk(KERN_ERR "gpio: couldn't get a major number.\n");
		return res;
	}

	/* Clear all leds */
#if defined (CONFIG_ETRAX_CSP0_LEDS) ||  defined (CONFIG_ETRAX_PA_LEDS) || defined (CONFIG_ETRAX_PB_LEDS)
	CRIS_LED_NETWORK_SET(0);
	CRIS_LED_ACTIVE_SET(0);
	CRIS_LED_DISK_READ(0);
	CRIS_LED_DISK_WRITE(0);

#if defined (CONFIG_ETRAX_CSP0_LEDS)
	for (i = 0; i < 32; i++)
		CRIS_LED_BIT_SET(i);
#endif

#endif
	/* The I/O interface allocation watcher will be called when
	 * registering it. */
	if (cris_io_interface_register_watcher(ioif_watcher)){
		printk(KERN_WARNING "gpio_init: Failed to install IO "
			"if allocator watcher\n");
	}

	printk(KERN_INFO "ETRAX 100LX GPIO driver v2.5, (c) 2001-2008 "
		"Axis Communications AB\n");
	/* We call etrax_gpio_wake_up_check() from timer interrupt and
	 * from cpu_idle() in kernel/process.c
	 * The check in cpu_idle() reduces latency from ~15 ms to ~6 ms
	 * in some tests.
	 */
	res = request_irq(TIMER0_IRQ_NBR, gpio_poll_timer_interrupt,
		IRQF_SHARED | IRQF_DISABLED, "gpio poll", gpio_name);
	if (res) {
		printk(KERN_CRIT "err: timer0 irq for gpio\n");
		return res;
	}
	res = request_irq(PA_IRQ_NBR, gpio_interrupt,
		IRQF_SHARED | IRQF_DISABLED, "gpio PA", gpio_name);
	if (res)
		printk(KERN_CRIT "err: PA irq for gpio\n");

	return res;
}

/* this makes sure that gpio_init is called during kernel boot */
module_init(gpio_init);

