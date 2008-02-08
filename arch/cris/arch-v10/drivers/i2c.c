/*!***************************************************************************
*!
*! FILE NAME  : i2c.c
*!
*! DESCRIPTION: implements an interface for IIC/I2C, both directly from other
*!              kernel modules (i2c_writereg/readreg) and from userspace using
*!              ioctl()'s
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

#include <asm/etraxi2c.h>

#include <asm/system.h>
#include <asm/arch/svinto.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/arch/io_interface_mux.h>

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

#ifdef CONFIG_ETRAX_I2C_USES_PB_NOT_PB_I2C
/* Use PB and not PB_I2C */
#ifndef CONFIG_ETRAX_I2C_DATA_PORT
#define CONFIG_ETRAX_I2C_DATA_PORT 0
#endif
#ifndef CONFIG_ETRAX_I2C_CLK_PORT
#define CONFIG_ETRAX_I2C_CLK_PORT 1
#endif

#define SDABIT CONFIG_ETRAX_I2C_DATA_PORT
#define SCLBIT CONFIG_ETRAX_I2C_CLK_PORT
#define i2c_enable() 
#define i2c_disable() 

/* enable or disable output-enable, to select output or input on the i2c bus */

#define i2c_dir_out() \
  REG_SHADOW_SET(R_PORT_PB_DIR, port_pb_dir_shadow, SDABIT, 1)
#define i2c_dir_in()  \
  REG_SHADOW_SET(R_PORT_PB_DIR, port_pb_dir_shadow, SDABIT, 0)

/* control the i2c clock and data signals */

#define i2c_clk(x) \
  REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, SCLBIT, x)
#define i2c_data(x) \
  REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, SDABIT, x)

/* read a bit from the i2c interface */

#define i2c_getbit() (((*R_PORT_PB_READ & (1 << SDABIT))) >> SDABIT)

#else
/* enable or disable the i2c interface */

#define i2c_enable() *R_PORT_PB_I2C = (port_pb_i2c_shadow |= IO_MASK(R_PORT_PB_I2C, i2c_en))
#define i2c_disable() *R_PORT_PB_I2C = (port_pb_i2c_shadow &= ~IO_MASK(R_PORT_PB_I2C, i2c_en))

/* enable or disable output-enable, to select output or input on the i2c bus */

#define i2c_dir_out() \
	*R_PORT_PB_I2C = (port_pb_i2c_shadow &= ~IO_MASK(R_PORT_PB_I2C, i2c_oe_)); \
	REG_SHADOW_SET(R_PORT_PB_DIR, port_pb_dir_shadow, 0, 1); 
#define i2c_dir_in() \
	*R_PORT_PB_I2C = (port_pb_i2c_shadow |= IO_MASK(R_PORT_PB_I2C, i2c_oe_)); \
	REG_SHADOW_SET(R_PORT_PB_DIR, port_pb_dir_shadow, 0, 0);

/* control the i2c clock and data signals */

#define i2c_clk(x) \
	*R_PORT_PB_I2C = (port_pb_i2c_shadow = (port_pb_i2c_shadow & \
       ~IO_MASK(R_PORT_PB_I2C, i2c_clk)) | IO_FIELD(R_PORT_PB_I2C, i2c_clk, (x))); \
       REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, 1, x);

#define i2c_data(x) \
	*R_PORT_PB_I2C = (port_pb_i2c_shadow = (port_pb_i2c_shadow & \
	   ~IO_MASK(R_PORT_PB_I2C, i2c_d)) | IO_FIELD(R_PORT_PB_I2C, i2c_d, (x))); \
	REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, 0, x);

/* read a bit from the i2c interface */

#define i2c_getbit() (*R_PORT_PB_READ & 0x1)
#endif

/* use the kernels delay routine */

#define i2c_delay(usecs) udelay(usecs)

static DEFINE_SPINLOCK(i2c_lock); /* Protect directions etc */

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
	/*
	 * now wait for ack
	 */
	i2c_delay(CLOCK_HIGH_TIME/2);
	/*
	 * check for ack
	 */
	if(i2c_getbit())
		ack = 0;
	i2c_delay(CLOCK_HIGH_TIME/2);
	if(!ack){
		if(!i2c_getbit()) /* receiver pulld SDA low */
			ack = 1;
		i2c_delay(CLOCK_HIGH_TIME/2);
	}

	/*
	 * our clock is high now, make sure data is low
	 * before we enable our output. If we keep data high
	 * and enable output, we would generate a stop condition.
	 */
	i2c_data(I2C_DATA_LOW);

	/*
	 * end clock pulse
	 */
	i2c_enable();
	i2c_dir_out();
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

	spin_lock(&i2c_lock);

	do {
		error = 0;
		/*
		 * we don't like to be interrupted
		 */
		local_irq_save(flags);

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
		/*
		 * enable interrupt again
		 */
		local_irq_restore(flags);
		
	} while(error && cntr--);

	i2c_delay(CLOCK_LOW_TIME);

	spin_unlock(&i2c_lock);

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

	spin_lock(&i2c_lock);

	do {
		error = 0;
		/*
		 * we don't like to be interrupted
		 */
		local_irq_save(flags);
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
			error = 1;
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
			error = 1;
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
		/*
		 * enable interrupt again
		 */
		local_irq_restore(flags);
		
	} while(error && cntr--);

	spin_unlock(&i2c_lock);

	return b;
}

static int
i2c_open(struct inode *inode, struct file *filp)
{
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
		return -EINVAL;
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
	.owner    = THIS_MODULE,
	.ioctl    = i2c_ioctl,
	.open     = i2c_open,
	.release  = i2c_release,
};

int __init
i2c_init(void)
{
	static int res = 0;
	static int first = 1;

	if (!first) {
		return res;
	}
	first = 0;

	/* Setup and enable the Port B I2C interface */

#ifndef CONFIG_ETRAX_I2C_USES_PB_NOT_PB_I2C
	if ((res = cris_request_io_interface(if_i2c, "I2C"))) {
		printk(KERN_CRIT "i2c_init: Failed to get IO interface\n");
		return res;
	}

	*R_PORT_PB_I2C = port_pb_i2c_shadow |= 
		IO_STATE(R_PORT_PB_I2C, i2c_en,  on) |
		IO_FIELD(R_PORT_PB_I2C, i2c_d,   1)  |
		IO_FIELD(R_PORT_PB_I2C, i2c_clk, 1)  |
		IO_STATE(R_PORT_PB_I2C, i2c_oe_, enable);

	port_pb_dir_shadow &= ~IO_MASK(R_PORT_PB_DIR, dir0);
	port_pb_dir_shadow &= ~IO_MASK(R_PORT_PB_DIR, dir1);

	*R_PORT_PB_DIR = (port_pb_dir_shadow |=
			  IO_STATE(R_PORT_PB_DIR, dir0, input)  |
			  IO_STATE(R_PORT_PB_DIR, dir1, output));
#else
        if ((res = cris_io_interface_allocate_pins(if_i2c,
						   'b',
                                                   CONFIG_ETRAX_I2C_DATA_PORT,
						   CONFIG_ETRAX_I2C_DATA_PORT))) {
		printk(KERN_WARNING "i2c_init: Failed to get IO pin for I2C data port\n");
		return res;
	} else if ((res = cris_io_interface_allocate_pins(if_i2c,
							  'b',
							  CONFIG_ETRAX_I2C_CLK_PORT,
							  CONFIG_ETRAX_I2C_CLK_PORT))) {
		cris_io_interface_free_pins(if_i2c,
					    'b',
					    CONFIG_ETRAX_I2C_DATA_PORT,
					    CONFIG_ETRAX_I2C_DATA_PORT);
		printk(KERN_WARNING "i2c_init: Failed to get IO pin for I2C clk port\n");
	}
#endif

	return res;
}

static int __init
i2c_register(void)
{
	int res;

	res = i2c_init();
	if (res < 0)
		return res;
  	res = register_chrdev(I2C_MAJOR, i2c_name, &i2c_fops);
	if(res < 0) {
		printk(KERN_ERR "i2c: couldn't get a major number.\n");
		return res;
	}

	printk(KERN_INFO "I2C driver v2.2, (c) 1999-2004 Axis Communications AB\n");
	
	return 0;
}

/* this makes sure that i2c_register is called during boot */

module_init(i2c_register);

/****************** END OF FILE i2c.c ********************************/
