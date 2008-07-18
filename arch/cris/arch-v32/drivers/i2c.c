/*!***************************************************************************
*!
*! FILE NAME  : i2c.c
*!
*! DESCRIPTION: implements an interface for IIC/I2C, both directly from other
*!              kernel modules (i2c_writereg/readreg) and from userspace using
*!              ioctl()'s
*!
*! Nov 30 1998  Torbjorn Eliasson  Initial version.
*!              Bjorn Wesen        Elinux kernel version.
*! Jan 14 2000  Johan Adolfsson    Fixed PB shadow register stuff -
*!                                 don't use PB_I2C if DS1302 uses same bits,
*!                                 use PB.
*| June 23 2003 Pieter Grimmerink  Added 'i2c_sendnack'. i2c_readreg now
*|                                 generates nack on last received byte,
*|                                 instead of ack.
*|                                 i2c_getack changed data level while clock
*|                                 was high, causing DS75 to see  a stop condition
*!
*! ---------------------------------------------------------------------------
*!
*! (C) Copyright 1999-2007 Axis Communications AB, LUND, SWEDEN
*!
*!***************************************************************************/

/****************** INCLUDE FILES SECTION ***********************************/

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/smp_lock.h>

#include <asm/etraxi2c.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/delay.h>

#include "i2c.h"

/****************** I2C DEFINITION SECTION *************************/

#define D(x)

#define I2C_MAJOR 123  /* LOCAL/EXPERIMENTAL */
static const char i2c_name[] = "i2c";

#define CLOCK_LOW_TIME            8
#define CLOCK_HIGH_TIME           8
#define START_CONDITION_HOLD_TIME 8
#define STOP_CONDITION_HOLD_TIME  8
#define ENABLE_OUTPUT 0x01
#define ENABLE_INPUT 0x00
#define I2C_CLOCK_HIGH 1
#define I2C_CLOCK_LOW 0
#define I2C_DATA_HIGH 1
#define I2C_DATA_LOW 0

#define i2c_enable()
#define i2c_disable()

/* enable or disable output-enable, to select output or input on the i2c bus */

#define i2c_dir_out() crisv32_io_set_dir(&cris_i2c_data, crisv32_io_dir_out)
#define i2c_dir_in() crisv32_io_set_dir(&cris_i2c_data, crisv32_io_dir_in)

/* control the i2c clock and data signals */

#define i2c_clk(x) crisv32_io_set(&cris_i2c_clk, x)
#define i2c_data(x) crisv32_io_set(&cris_i2c_data, x)

/* read a bit from the i2c interface */

#define i2c_getbit() crisv32_io_rd(&cris_i2c_data)

#define i2c_delay(usecs) udelay(usecs)

static DEFINE_SPINLOCK(i2c_lock); /* Protect directions etc */

/****************** VARIABLE SECTION ************************************/

static struct crisv32_iopin cris_i2c_clk;
static struct crisv32_iopin cris_i2c_data;

/****************** FUNCTION DEFINITION SECTION *************************/


/* generate i2c start condition */

void
i2c_start(void)
{
	/*
	 * SCL=1 SDA=1
	 */
	i2c_dir_out();
	i2c_delay(CLOCK_HIGH_TIME/6);
	i2c_data(I2C_DATA_HIGH);
	i2c_clk(I2C_CLOCK_HIGH);
	i2c_delay(CLOCK_HIGH_TIME);
	/*
	 * SCL=1 SDA=0
	 */
	i2c_data(I2C_DATA_LOW);
	i2c_delay(START_CONDITION_HOLD_TIME);
	/*
	 * SCL=0 SDA=0
	 */
	i2c_clk(I2C_CLOCK_LOW);
	i2c_delay(CLOCK_LOW_TIME);
}

/* generate i2c stop condition */

void
i2c_stop(void)
{
	i2c_dir_out();

	/*
	 * SCL=0 SDA=0
	 */
	i2c_clk(I2C_CLOCK_LOW);
	i2c_data(I2C_DATA_LOW);
	i2c_delay(CLOCK_LOW_TIME*2);
	/*
	 * SCL=1 SDA=0
	 */
	i2c_clk(I2C_CLOCK_HIGH);
	i2c_delay(CLOCK_HIGH_TIME*2);
	/*
	 * SCL=1 SDA=1
	 */
	i2c_data(I2C_DATA_HIGH);
	i2c_delay(STOP_CONDITION_HOLD_TIME);

	i2c_dir_in();
}

/* write a byte to the i2c interface */

void
i2c_outbyte(unsigned char x)
{
	int i;

	i2c_dir_out();

	for (i = 0; i < 8; i++) {
		if (x & 0x80) {
			i2c_data(I2C_DATA_HIGH);
		} else {
			i2c_data(I2C_DATA_LOW);
		}

		i2c_delay(CLOCK_LOW_TIME/2);
		i2c_clk(I2C_CLOCK_HIGH);
		i2c_delay(CLOCK_HIGH_TIME);
		i2c_clk(I2C_CLOCK_LOW);
		i2c_delay(CLOCK_LOW_TIME/2);
		x <<= 1;
	}
	i2c_data(I2C_DATA_LOW);
	i2c_delay(CLOCK_LOW_TIME/2);

	/*
	 * enable input
	 */
	i2c_dir_in();
}

/* read a byte from the i2c interface */

unsigned char
i2c_inbyte(void)
{
	unsigned char aBitByte = 0;
	int i;

	/* Switch off I2C to get bit */
	i2c_disable();
	i2c_dir_in();
	i2c_delay(CLOCK_HIGH_TIME/2);

	/* Get bit */
	aBitByte |= i2c_getbit();

	/* Enable I2C */
	i2c_enable();
	i2c_delay(CLOCK_LOW_TIME/2);

	for (i = 1; i < 8; i++) {
		aBitByte <<= 1;
		/* Clock pulse */
		i2c_clk(I2C_CLOCK_HIGH);
		i2c_delay(CLOCK_HIGH_TIME);
		i2c_clk(I2C_CLOCK_LOW);
		i2c_delay(CLOCK_LOW_TIME);

		/* Switch off I2C to get bit */
		i2c_disable();
		i2c_dir_in();
		i2c_delay(CLOCK_HIGH_TIME/2);

		/* Get bit */
		aBitByte |= i2c_getbit();

		/* Enable I2C */
		i2c_enable();
		i2c_delay(CLOCK_LOW_TIME/2);
	}
	i2c_clk(I2C_CLOCK_HIGH);
	i2c_delay(CLOCK_HIGH_TIME);

	/*
	 * we leave the clock low, getbyte is usually followed
	 * by sendack/nack, they assume the clock to be low
	 */
	i2c_clk(I2C_CLOCK_LOW);
	return aBitByte;
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_getack
*#
*# DESCRIPTION  : checks if ack was received from ic2
*#
*#--------------------------------------------------------------------------*/

int
i2c_getack(void)
{
	int ack = 1;
	/*
	 * enable output
	 */
	i2c_dir_out();
	/*
	 * Release data bus by setting
	 * data high
	 */
	i2c_data(I2C_DATA_HIGH);
	/*
	 * enable input
	 */
	i2c_dir_in();
	i2c_delay(CLOCK_HIGH_TIME/4);
	/*
	 * generate ACK clock pulse
	 */
	i2c_clk(I2C_CLOCK_HIGH);
#if 0
	/*
	 * Use PORT PB instead of I2C
	 * for input. (I2C not working)
	 */
	i2c_clk(1);
	i2c_data(1);
	/*
	 * switch off I2C
	 */
	i2c_data(1);
	i2c_disable();
	i2c_dir_in();
#endif

	/*
	 * now wait for ack
	 */
	i2c_delay(CLOCK_HIGH_TIME/2);
	/*
	 * check for ack
	 */
	if (i2c_getbit())
		ack = 0;
	i2c_delay(CLOCK_HIGH_TIME/2);
	if (!ack) {
		if (!i2c_getbit()) /* receiver pulld SDA low */
			ack = 1;
		i2c_delay(CLOCK_HIGH_TIME/2);
	}

   /*
    * our clock is high now, make sure data is low
    * before we enable our output. If we keep data high
    * and enable output, we would generate a stop condition.
    */
#if 0
   i2c_data(I2C_DATA_LOW);

	/*
	 * end clock pulse
	 */
	i2c_enable();
	i2c_dir_out();
#endif
	i2c_clk(I2C_CLOCK_LOW);
	i2c_delay(CLOCK_HIGH_TIME/4);
	/*
	 * enable output
	 */
	i2c_dir_out();
	/*
	 * remove ACK clock pulse
	 */
	i2c_data(I2C_DATA_HIGH);
	i2c_delay(CLOCK_LOW_TIME/2);
	return ack;
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: I2C::sendAck
*#
*# DESCRIPTION  : Send ACK on received data
*#
*#--------------------------------------------------------------------------*/
void
i2c_sendack(void)
{
	/*
	 * enable output
	 */
	i2c_delay(CLOCK_LOW_TIME);
	i2c_dir_out();
	/*
	 * set ack pulse high
	 */
	i2c_data(I2C_DATA_LOW);
	/*
	 * generate clock pulse
	 */
	i2c_delay(CLOCK_HIGH_TIME/6);
	i2c_clk(I2C_CLOCK_HIGH);
	i2c_delay(CLOCK_HIGH_TIME);
	i2c_clk(I2C_CLOCK_LOW);
	i2c_delay(CLOCK_LOW_TIME/6);
	/*
	 * reset data out
	 */
	i2c_data(I2C_DATA_HIGH);
	i2c_delay(CLOCK_LOW_TIME);

	i2c_dir_in();
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_sendnack
*#
*# DESCRIPTION  : Sends NACK on received data
*#
*#--------------------------------------------------------------------------*/
void
i2c_sendnack(void)
{
	/*
	 * enable output
	 */
	i2c_delay(CLOCK_LOW_TIME);
	i2c_dir_out();
	/*
	 * set data high
	 */
	i2c_data(I2C_DATA_HIGH);
	/*
	 * generate clock pulse
	 */
	i2c_delay(CLOCK_HIGH_TIME/6);
	i2c_clk(I2C_CLOCK_HIGH);
	i2c_delay(CLOCK_HIGH_TIME);
	i2c_clk(I2C_CLOCK_LOW);
	i2c_delay(CLOCK_LOW_TIME);

	i2c_dir_in();
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_write
*#
*# DESCRIPTION  : Writes a value to an I2C device
*#
*#--------------------------------------------------------------------------*/
int
i2c_write(unsigned char theSlave, void *data, size_t nbytes)
{
	int error, cntr = 3;
	unsigned char bytes_wrote = 0;
	unsigned char value;
	unsigned long flags;

	spin_lock_irqsave(&i2c_lock, flags);

	do {
		error = 0;

		i2c_start();
		/*
		 * send slave address
		 */
		i2c_outbyte((theSlave & 0xfe));
		/*
		 * wait for ack
		 */
		if (!i2c_getack())
			error = 1;
		/*
		 * send data
		 */
		for (bytes_wrote = 0; bytes_wrote < nbytes; bytes_wrote++) {
			memcpy(&value, data + bytes_wrote, sizeof value);
			i2c_outbyte(value);
			/*
			 * now it's time to wait for ack
			 */
			if (!i2c_getack())
				error |= 4;
		}
		/*
		 * end byte stream
		 */
		i2c_stop();

	} while (error && cntr--);

	i2c_delay(CLOCK_LOW_TIME);

	spin_unlock_irqrestore(&i2c_lock, flags);

	return -error;
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_read
*#
*# DESCRIPTION  : Reads a value from an I2C device
*#
*#--------------------------------------------------------------------------*/
int
i2c_read(unsigned char theSlave, void *data, size_t nbytes)
{
	unsigned char b = 0;
	unsigned char bytes_read = 0;
	int error, cntr = 3;
	unsigned long flags;

	spin_lock_irqsave(&i2c_lock, flags);

	do {
		error = 0;
		memset(data, 0, nbytes);
		/*
		 * generate start condition
		 */
		i2c_start();
		/*
		 * send slave address
		 */
		i2c_outbyte((theSlave | 0x01));
		/*
		 * wait for ack
		 */
		if (!i2c_getack())
			error = 1;
		/*
		 * fetch data
		 */
		for (bytes_read = 0; bytes_read < nbytes; bytes_read++) {
			b = i2c_inbyte();
			memcpy(data + bytes_read, &b, sizeof b);

			if (bytes_read < (nbytes - 1))
				i2c_sendack();
		}
		/*
		 * last received byte needs to be nacked
		 * instead of acked
		 */
		i2c_sendnack();
		/*
		 * end sequence
		 */
		i2c_stop();
	} while (error && cntr--);

	spin_unlock_irqrestore(&i2c_lock, flags);

	return -error;
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_writereg
*#
*# DESCRIPTION  : Writes a value to an I2C device
*#
*#--------------------------------------------------------------------------*/
int
i2c_writereg(unsigned char theSlave, unsigned char theReg,
	     unsigned char theValue)
{
	int error, cntr = 3;
	unsigned long flags;

	spin_lock_irqsave(&i2c_lock, flags);

	do {
		error = 0;

		i2c_start();
		/*
		 * send slave address
		 */
		i2c_outbyte((theSlave & 0xfe));
		/*
		 * wait for ack
		 */
		if(!i2c_getack())
			error = 1;
		/*
		 * now select register
		 */
		i2c_dir_out();
		i2c_outbyte(theReg);
		/*
		 * now it's time to wait for ack
		 */
		if(!i2c_getack())
			error |= 2;
		/*
		 * send register register data
		 */
		i2c_outbyte(theValue);
		/*
		 * now it's time to wait for ack
		 */
		if(!i2c_getack())
			error |= 4;
		/*
		 * end byte stream
		 */
		i2c_stop();
	} while(error && cntr--);

	i2c_delay(CLOCK_LOW_TIME);

	spin_unlock_irqrestore(&i2c_lock, flags);

	return -error;
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_readreg
*#
*# DESCRIPTION  : Reads a value from the decoder registers.
*#
*#--------------------------------------------------------------------------*/
unsigned char
i2c_readreg(unsigned char theSlave, unsigned char theReg)
{
	unsigned char b = 0;
	int error, cntr = 3;
	unsigned long flags;

	spin_lock_irqsave(&i2c_lock, flags);

	do {
		error = 0;
		/*
		 * generate start condition
		 */
		i2c_start();

		/*
		 * send slave address
		 */
		i2c_outbyte((theSlave & 0xfe));
		/*
		 * wait for ack
		 */
		if(!i2c_getack())
			error = 1;
		/*
		 * now select register
		 */
		i2c_dir_out();
		i2c_outbyte(theReg);
		/*
		 * now it's time to wait for ack
		 */
		if(!i2c_getack())
			error |= 2;
		/*
		 * repeat start condition
		 */
		i2c_delay(CLOCK_LOW_TIME);
		i2c_start();
		/*
		 * send slave address
		 */
		i2c_outbyte(theSlave | 0x01);
		/*
		 * wait for ack
		 */
		if(!i2c_getack())
			error |= 4;
		/*
		 * fetch register
		 */
		b = i2c_inbyte();
		/*
		 * last received byte needs to be nacked
		 * instead of acked
		 */
		i2c_sendnack();
		/*
		 * end sequence
		 */
		i2c_stop();

	} while(error && cntr--);

	spin_unlock_irqrestore(&i2c_lock, flags);

	return b;
}

static int
i2c_open(struct inode *inode, struct file *filp)
{
	cycle_kernel_lock();
	return 0;
}

static int
i2c_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* Main device API. ioctl's to write or read to/from i2c registers.
 */

static int
i2c_ioctl(struct inode *inode, struct file *file,
	  unsigned int cmd, unsigned long arg)
{
	if(_IOC_TYPE(cmd) != ETRAXI2C_IOCTYPE) {
		return -ENOTTY;
	}

	switch (_IOC_NR(cmd)) {
		case I2C_WRITEREG:
			/* write to an i2c slave */
			D(printk("i2cw %d %d %d\n",
				 I2C_ARGSLAVE(arg),
				 I2C_ARGREG(arg),
				 I2C_ARGVALUE(arg)));

			return i2c_writereg(I2C_ARGSLAVE(arg),
					    I2C_ARGREG(arg),
					    I2C_ARGVALUE(arg));
		case I2C_READREG:
		{
			unsigned char val;
			/* read from an i2c slave */
			D(printk("i2cr %d %d ",
				I2C_ARGSLAVE(arg),
				I2C_ARGREG(arg)));
			val = i2c_readreg(I2C_ARGSLAVE(arg), I2C_ARGREG(arg));
			D(printk("= %d\n", val));
			return val;
		}
		default:
			return -EINVAL;

	}

	return 0;
}

static const struct file_operations i2c_fops = {
	.owner =    THIS_MODULE,
	.ioctl =    i2c_ioctl,
	.open =     i2c_open,
	.release =  i2c_release,
};

static int __init i2c_init(void)
{
	static int res;
	static int first = 1;

	if (!first)
		return res;

	first = 0;

	/* Setup and enable the DATA and CLK pins */

	res = crisv32_io_get_name(&cris_i2c_data,
		CONFIG_ETRAX_V32_I2C_DATA_PORT);
	if (res < 0)
		return res;

	res = crisv32_io_get_name(&cris_i2c_clk, CONFIG_ETRAX_V32_I2C_CLK_PORT);
	crisv32_io_set_dir(&cris_i2c_clk, crisv32_io_dir_out);

	return res;
}


static int __init i2c_register(void)
{
	int res;

	res = i2c_init();
	if (res < 0)
		return res;

	/* register char device */

	res = register_chrdev(I2C_MAJOR, i2c_name, &i2c_fops);
	if (res < 0) {
		printk(KERN_ERR "i2c: couldn't get a major number.\n");
		return res;
	}

	printk(KERN_INFO
		"I2C driver v2.2, (c) 1999-2007 Axis Communications AB\n");

	return 0;
}
/* this makes sure that i2c_init is called during boot */
module_init(i2c_register);

/****************** END OF FILE i2c.c ********************************/
