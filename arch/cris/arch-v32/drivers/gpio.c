/* $Id: gpio.c,v 1.16 2005/06/19 17:06:49 starvik Exp $
 *
 * ETRAX CRISv32 general port I/O device
 *
 * Copyright (c) 1999, 2000, 2001, 2002, 2003 Axis Communications AB
 *
 * Authors:    Bjorn Wesen      (initial version)
 *             Ola Knutsson     (LED handling)
 *             Johan Adolfsson  (read/set directions, write, port G,
 *                               port to ETRAX FS.
 *
 * $Log: gpio.c,v $
 * Revision 1.16  2005/06/19 17:06:49  starvik
 * Merge of Linux 2.6.12.
 *
 * Revision 1.15  2005/05/25 08:22:20  starvik
 * Changed GPIO port order to fit packages/devices/axis-2.4.
 *
 * Revision 1.14  2005/04/24 18:35:08  starvik
 * Updated with final register headers.
 *
 * Revision 1.13  2005/03/15 15:43:00  starvik
 * dev_id needs to be supplied for shared IRQs.
 *
 * Revision 1.12  2005/03/10 17:12:00  starvik
 * Protect alarm list with spinlock.
 *
 * Revision 1.11  2005/01/05 06:08:59  starvik
 * No need to do local_irq_disable after local_irq_save.
 *
 * Revision 1.10  2004/11/19 08:38:31  starvik
 * Removed old crap.
 *
 * Revision 1.9  2004/05/14 07:58:02  starvik
 * Merge of changes from 2.4
 *
 * Revision 1.8  2003/09/11 07:29:50  starvik
 * Merge of Linux 2.6.0-test5
 *
 * Revision 1.7  2003/07/10 13:25:46  starvik
 * Compiles for 2.5.74
 * Lindented ethernet.c
 *
 * Revision 1.6  2003/07/04 08:27:46  starvik
 * Merge of Linux 2.5.74
 *
 * Revision 1.5  2003/06/10 08:26:37  johana
 * Etrax -> ETRAX CRISv32
 *
 * Revision 1.4  2003/06/05 14:22:48  johana
 * Initialise some_alarms.
 *
 * Revision 1.3  2003/06/05 10:15:46  johana
 * New INTR_VECT macros.
 * Enable interrupts in global config.
 *
 * Revision 1.2  2003/06/03 15:52:50  johana
 * Initial CRIS v32 version.
 *
 * Revision 1.1  2003/06/03 08:53:15  johana
 * Copy of os/lx25/arch/cris/arch-v10/drivers/gpio.c version 1.7.
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

#include <asm/etraxgpio.h>
#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/gio_defs.h>
#include <asm/arch/hwregs/intr_vect_defs.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/irq.h>

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
static ssize_t gpio_write(struct file * file, const char * buf, size_t count,
                          loff_t *off);
static int gpio_open(struct inode *inode, struct file *filp);
static int gpio_release(struct inode *inode, struct file *filp);
static unsigned int gpio_poll(struct file *filp, struct poll_table_struct *wait);

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

static struct gpio_private *alarmlist = 0;

static int gpio_some_alarms = 0; /* Set if someone uses alarm */
static unsigned long gpio_pa_high_alarms = 0;
static unsigned long gpio_pa_low_alarms = 0;

static DEFINE_SPINLOCK(alarm_lock);

#define NUM_PORTS (GPIO_MINOR_LAST+1)
#define GIO_REG_RD_ADDR(reg) (volatile unsigned long*) (regi_gio + REG_RD_ADDR_gio_##reg )
#define GIO_REG_WR_ADDR(reg) (volatile unsigned long*) (regi_gio + REG_RD_ADDR_gio_##reg )
unsigned long led_dummy;

static volatile unsigned long *data_out[NUM_PORTS] = {
	GIO_REG_WR_ADDR(rw_pa_dout),
	GIO_REG_WR_ADDR(rw_pb_dout),
	&led_dummy,
	GIO_REG_WR_ADDR(rw_pc_dout),
	GIO_REG_WR_ADDR(rw_pd_dout),
	GIO_REG_WR_ADDR(rw_pe_dout),
};

static volatile unsigned long *data_in[NUM_PORTS] = {
	GIO_REG_RD_ADDR(r_pa_din),
	GIO_REG_RD_ADDR(r_pb_din),
	&led_dummy,
	GIO_REG_RD_ADDR(r_pc_din),
	GIO_REG_RD_ADDR(r_pd_din),
	GIO_REG_RD_ADDR(r_pe_din),
};

static unsigned long changeable_dir[NUM_PORTS] = {
	CONFIG_ETRAX_PA_CHANGEABLE_DIR,
	CONFIG_ETRAX_PB_CHANGEABLE_DIR,
	0,
	CONFIG_ETRAX_PC_CHANGEABLE_DIR,
	CONFIG_ETRAX_PD_CHANGEABLE_DIR,
	CONFIG_ETRAX_PE_CHANGEABLE_DIR,
};

static unsigned long changeable_bits[NUM_PORTS] = {
	CONFIG_ETRAX_PA_CHANGEABLE_BITS,
	CONFIG_ETRAX_PB_CHANGEABLE_BITS,
	0,
	CONFIG_ETRAX_PC_CHANGEABLE_BITS,
	CONFIG_ETRAX_PD_CHANGEABLE_BITS,
	CONFIG_ETRAX_PE_CHANGEABLE_BITS,
};

static volatile unsigned long *dir_oe[NUM_PORTS] = {
	GIO_REG_WR_ADDR(rw_pa_oe),
	GIO_REG_WR_ADDR(rw_pb_oe),
	&led_dummy,
	GIO_REG_WR_ADDR(rw_pc_oe),
	GIO_REG_WR_ADDR(rw_pd_oe),
	GIO_REG_WR_ADDR(rw_pe_oe),
};



static unsigned int
gpio_poll(struct file *file,
	  poll_table *wait)
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
		data = REG_TYPE_CONV(unsigned long, reg_gio_r_pa_din, REG_RD(gio, regi_gio, r_pa_din));
		/* PA has support for interrupt
		 * lets activate high for those low and with highalarm set
		 */
		intr_cfg = REG_RD(gio, regi_gio, rw_intr_cfg);

		tmp = ~data & priv->highalarm & 0xFF;
                if (tmp & (1 << 0)) {
			intr_cfg.pa0 = regk_gio_hi;
		}
                if (tmp & (1 << 1)) {
			intr_cfg.pa1 = regk_gio_hi;
		}
                if (tmp & (1 << 2)) {
			intr_cfg.pa2 = regk_gio_hi;
		}
                if (tmp & (1 << 3)) {
			intr_cfg.pa3 = regk_gio_hi;
		}
                if (tmp & (1 << 4)) {
			intr_cfg.pa4 = regk_gio_hi;
		}
                if (tmp & (1 << 5)) {
			intr_cfg.pa5 = regk_gio_hi;
		}
                if (tmp & (1 << 6)) {
			intr_cfg.pa6 = regk_gio_hi;
		}
                if (tmp & (1 << 7)) {
			intr_cfg.pa7 = regk_gio_hi;
		}
		/*
		 * lets activate low for those high and with lowalarm set
		 */
		tmp = data & priv->lowalarm & 0xFF;
                if (tmp & (1 << 0)) {
			intr_cfg.pa0 = regk_gio_lo;
		}
                if (tmp & (1 << 1)) {
			intr_cfg.pa1 = regk_gio_lo;
		}
                if (tmp & (1 << 2)) {
			intr_cfg.pa2 = regk_gio_lo;
		}
                if (tmp & (1 << 3)) {
			intr_cfg.pa3 = regk_gio_lo;
		}
                if (tmp & (1 << 4)) {
			intr_cfg.pa4 = regk_gio_lo;
		}
                if (tmp & (1 << 5)) {
			intr_cfg.pa5 = regk_gio_lo;
		}
                if (tmp & (1 << 6)) {
			intr_cfg.pa6 = regk_gio_lo;
		}
                if (tmp & (1 << 7)) {
			intr_cfg.pa7 = regk_gio_lo;
		}

		REG_WR(gio, regi_gio, rw_intr_cfg, intr_cfg);
		local_irq_restore(flags);
	} else if (priv->minor <= GPIO_MINOR_E)
		data = *data_in[priv->minor];
	else
		return 0;

	if ((data & priv->highalarm) ||
	    (~data & priv->lowalarm)) {
		mask = POLLIN|POLLRDNORM;
	}

	DP(printk("gpio_poll ready: mask 0x%08X\n", mask));
	return mask;
}

int etrax_gpio_wake_up_check(void)
{
	struct gpio_private *priv = alarmlist;
	unsigned long data = 0;
        int ret = 0;
	while (priv) {
		data = *data_in[priv->minor];
		if ((data & priv->highalarm) ||
		    (~data & priv->lowalarm)) {
			DP(printk("etrax_gpio_wake_up_check %i\n",priv->minor));
			wake_up_interruptible(&priv->alarm_wq);
                        ret = 1;
		}
		priv = priv->next;
	}
        return ret;
}

static irqreturn_t
gpio_poll_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	if (gpio_some_alarms) {
		return IRQ_RETVAL(etrax_gpio_wake_up_check());
	}
        return IRQ_NONE;
}

static irqreturn_t
gpio_pa_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	reg_gio_rw_intr_mask intr_mask;
	reg_gio_r_masked_intr masked_intr;
	reg_gio_rw_ack_intr ack_intr;
	unsigned long tmp;
	unsigned long tmp2;

	/* Find what PA interrupts are active */
	masked_intr = REG_RD(gio, regi_gio, r_masked_intr);
	tmp = REG_TYPE_CONV(unsigned long, reg_gio_r_masked_intr, masked_intr);

	/* Find those that we have enabled */
	spin_lock(&alarm_lock);
	tmp &= (gpio_pa_high_alarms | gpio_pa_low_alarms);
	spin_unlock(&alarm_lock);

	/* Ack them */
	ack_intr = REG_TYPE_CONV(reg_gio_rw_ack_intr, unsigned long, tmp);
	REG_WR(gio, regi_gio, rw_ack_intr, ack_intr);

	/* Disable those interrupts.. */
	intr_mask = REG_RD(gio, regi_gio, rw_intr_mask);
	tmp2 = REG_TYPE_CONV(unsigned long, reg_gio_rw_intr_mask, intr_mask);
	tmp2 &= ~tmp;
	intr_mask = REG_TYPE_CONV(reg_gio_rw_intr_mask, unsigned long, tmp2);
	REG_WR(gio, regi_gio, rw_intr_mask, intr_mask);

	if (gpio_some_alarms) {
		return IRQ_RETVAL(etrax_gpio_wake_up_check());
	}
        return IRQ_NONE;
}


static ssize_t gpio_write(struct file * file, const char * buf, size_t count,
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
	if (priv->minor == GPIO_MINOR_LEDS) {
		return -EFAULT;
	}

	if (!access_ok(VERIFY_READ, buf, count)) {
		return -EFAULT;
	}
	clk_mask = priv->clk_mask;
	data_mask = priv->data_mask;
	/* It must have been configured using the IO_CFG_WRITE_MODE */
	/* Perhaps a better error code? */
	if (clk_mask == 0 || data_mask == 0) {
		return -EPERM;
	}
	write_msb = priv->write_msb;
	D(printk("gpio_write: %lu to data 0x%02X clk 0x%02X msb: %i\n",count, data_mask, clk_mask, write_msb));
	port = data_out[priv->minor];

	while (count--) {
		int i;
		data = *buf++;
		if (priv->write_msb) {
			for (i = 7; i >= 0;i--) {
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
			for (i = 0; i <= 7;i++) {
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

	priv = kmalloc(sizeof(struct gpio_private),
					      GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	priv->minor = p;

	/* initialize the io/alarm struct and link it into our alarmlist */

	priv->next = alarmlist;
	alarmlist = priv;
	priv->clk_mask = 0;
	priv->data_mask = 0;
	priv->highalarm = 0;
	priv->lowalarm = 0;
	init_waitqueue_head(&priv->alarm_wq);

	filp->private_data = (void *)priv;

	return 0;
}

static int
gpio_release(struct inode *inode, struct file *filp)
{
	struct gpio_private *p = alarmlist;
	struct gpio_private *todel = (struct gpio_private *)filp->private_data;
	/* local copies while updating them: */
	unsigned long a_high, a_low;
	unsigned long some_alarms;

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
        some_alarms = 0;
	a_high = 0;
	a_low = 0;
	while (p) {
		if (p->minor == GPIO_MINOR_A) {
			a_high |= p->highalarm;
			a_low |= p->lowalarm;
		}

		if (p->highalarm | p->lowalarm) {
			some_alarms = 1;
		}
		p = p->next;
	}

	spin_lock(&alarm_lock);
	gpio_some_alarms = some_alarms;
	gpio_pa_high_alarms = a_high;
	gpio_pa_low_alarms = a_low;
	spin_unlock(&alarm_lock);

	return 0;
}

/* Main device API. ioctl's to read/set/clear bits, as well as to
 * set alarms to wait for using a subsequent select().
 */

unsigned long inline setget_input(struct gpio_private *priv, unsigned long arg)
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
	else
		dir_shadow ^= 0x3FFFF; /* Only 18 bits */
	return dir_shadow;

} /* setget_input */

unsigned long inline setget_output(struct gpio_private *priv, unsigned long arg)
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
	if (_IOC_TYPE(cmd) != ETRAXGPIO_IOCTYPE) {
		return -EINVAL;
	}

	switch (_IOC_NR(cmd)) {
	case IO_READBITS: /* Use IO_READ_INBITS and IO_READ_OUTBITS instead */
		// read the port
		return *data_in[priv->minor];
		break;
	case IO_SETBITS:
		local_irq_save(flags);
                if (arg & 0x04)
                  printk("GPIO SET 2\n");
		// set changeable bits with a 1 in arg
		shadow = *data_out[priv->minor];
		shadow |=  (arg & changeable_bits[priv->minor]);
		*data_out[priv->minor] = shadow;
		local_irq_restore(flags);
		break;
	case IO_CLRBITS:
		local_irq_save(flags);
                if (arg & 0x04)
                  printk("GPIO CLR 2\n");
		// clear changeable bits with a 1 in arg
		shadow = *data_out[priv->minor];
		shadow &=  ~(arg & changeable_bits[priv->minor]);
		*data_out[priv->minor] = shadow;
		local_irq_restore(flags);
		break;
	case IO_HIGHALARM:
		// set alarm when bits with 1 in arg go high
		priv->highalarm |= arg;
		spin_lock(&alarm_lock);
		gpio_some_alarms = 1;
		if (priv->minor == GPIO_MINOR_A) {
			gpio_pa_high_alarms |= arg;
		}
		spin_unlock(&alarm_lock);
		break;
	case IO_LOWALARM:
		// set alarm when bits with 1 in arg go low
		priv->lowalarm |= arg;
		spin_lock(&alarm_lock);
		gpio_some_alarms = 1;
		if (priv->minor == GPIO_MINOR_A) {
			gpio_pa_low_alarms |= arg;
		}
		spin_unlock(&alarm_lock);
		break;
	case IO_CLRALARM:
		// clear alarm for bits with 1 in arg
		priv->highalarm &= ~arg;
		priv->lowalarm  &= ~arg;
		spin_lock(&alarm_lock);
		if (priv->minor == GPIO_MINOR_A) {
			if (gpio_pa_high_alarms & arg ||
			    gpio_pa_low_alarms & arg) {
				/* Must update the gpio_pa_*alarms masks */
			}
		}
		spin_unlock(&alarm_lock);
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
		      (priv->data_mask & dir_shadow)))
		{
			priv->clk_mask = 0;
			priv->data_mask = 0;
			return -EPERM;
		}
		break;
	}
	case IO_READ_INBITS:
		/* *arg is result of reading the input pins */
		val = *data_in[priv->minor];
		if (copy_to_user((unsigned long*)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;
		break;
	case IO_READ_OUTBITS:
		 /* *arg is result of reading the output shadow */
		val = *data_out[priv->minor];
		if (copy_to_user((unsigned long*)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	case IO_SETGET_INPUT:
		/* bits set in *arg is set to input,
		 * *arg updated with current input pins.
		 */
		if (copy_from_user(&val, (unsigned long*)arg, sizeof(val)))
			return -EFAULT;
		val = setget_input(priv, val);
		if (copy_to_user((unsigned long*)arg, &val, sizeof(val)))
			return -EFAULT;
		break;
	case IO_SETGET_OUTPUT:
		/* bits set in *arg is set to output,
		 * *arg updated with current output pins.
		 */
		if (copy_from_user(&val, (unsigned long*)arg, sizeof(val)))
			return -EFAULT;
		val = setget_output(priv, val);
		if (copy_to_user((unsigned long*)arg, &val, sizeof(val)))
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

static int
gpio_leds_ioctl(unsigned int cmd, unsigned long arg)
{
	unsigned char green;
	unsigned char red;

	switch (_IOC_NR(cmd)) {
	case IO_LEDACTIVE_SET:
		green = ((unsigned char) arg) & 1;
		red   = (((unsigned char) arg) >> 1) & 1;
		LED_ACTIVE_SET_G(green);
		LED_ACTIVE_SET_R(red);
		break;

	default:
		return -EINVAL;
	} /* switch */

	return 0;
}

const struct file_operations gpio_fops = {
	.owner       = THIS_MODULE,
	.poll        = gpio_poll,
	.ioctl       = gpio_ioctl,
	.write       = gpio_write,
	.open        = gpio_open,
	.release     = gpio_release,
};


/* main driver initialization routine, called from mem.c */

static __init int
gpio_init(void)
{
	int res;
	reg_intr_vect_rw_mask intr_mask;

	/* do the formalities */

	res = register_chrdev(GPIO_MAJOR, gpio_name, &gpio_fops);
	if (res < 0) {
		printk(KERN_ERR "gpio: couldn't get a major number.\n");
		return res;
	}

	/* Clear all leds */
	LED_NETWORK_SET(0);
	LED_ACTIVE_SET(0);
	LED_DISK_READ(0);
	LED_DISK_WRITE(0);

	printk("ETRAX FS GPIO driver v2.5, (c) 2003-2005 Axis Communications AB\n");
	/* We call etrax_gpio_wake_up_check() from timer interrupt and
	 * from cpu_idle() in kernel/process.c
	 * The check in cpu_idle() reduces latency from ~15 ms to ~6 ms
	 * in some tests.
	 */
	if (request_irq(TIMER_INTR_VECT, gpio_poll_timer_interrupt,
			IRQF_SHARED | IRQF_DISABLED,"gpio poll", &alarmlist)) {
		printk("err: timer0 irq for gpio\n");
	}
	if (request_irq(GEN_IO_INTR_VECT, gpio_pa_interrupt,
			IRQF_SHARED | IRQF_DISABLED,"gpio PA", &alarmlist)) {
		printk("err: PA irq for gpio\n");
	}
	/* enable the gio and timer irq in global config */
	intr_mask = REG_RD(intr_vect, regi_irq, rw_mask);
	intr_mask.timer = 1;
	intr_mask.gen_io = 1;
	REG_WR(intr_vect, regi_irq, rw_mask, intr_mask);

	return res;
}

/* this makes sure that gpio_init is called during kernel boot */

module_init(gpio_init);
