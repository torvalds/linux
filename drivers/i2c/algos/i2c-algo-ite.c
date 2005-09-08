/*
   -------------------------------------------------------------------------
   i2c-algo-ite.c i2c driver algorithms for ITE adapters	    
   
   Hai-Pao Fan, MontaVista Software, Inc.
   hpfan@mvista.com or source@mvista.com

   Copyright 2000 MontaVista Software Inc.

   ---------------------------------------------------------------------------
   This file was highly leveraged from i2c-algo-pcf.c, which was created
   by Simon G. Vogl and Hans Berglund:


     Copyright (C) 1995-1997 Simon G. Vogl
                   1998-2000 Hans Berglund

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and 
   Frodo Looijaard <frodol@dds.nl> ,and also from Martin Bailey
   <mbailey@littlefeet-inc.com> */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/sched.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-ite.h>
#include "i2c-algo-ite.h"

#define	PM_DSR		IT8172_PCI_IO_BASE + IT_PM_DSR
#define	PM_IBSR		IT8172_PCI_IO_BASE + IT_PM_DSR + 0x04 
#define GPIO_CCR	IT8172_PCI_IO_BASE + IT_GPCCR

#define DEB2(x) if (i2c_debug>=2) x
#define DEB3(x) if (i2c_debug>=3) x /* print several statistical values*/
#define DEF_TIMEOUT 16


/* module parameters:
 */
static int i2c_debug;
static int iic_test;	/* see if the line-setting functions work	*/

/* --- setting states on the bus with the right timing: ---------------	*/

#define get_clock(adap) adap->getclock(adap->data)
#define iic_outw(adap, reg, val) adap->setiic(adap->data, reg, val)
#define iic_inw(adap, reg) adap->getiic(adap->data, reg)


/* --- other auxiliary functions --------------------------------------	*/

static void iic_start(struct i2c_algo_iic_data *adap)
{
	iic_outw(adap,ITE_I2CHCR,ITE_CMD);
}

static void iic_stop(struct i2c_algo_iic_data *adap)
{
	iic_outw(adap,ITE_I2CHCR,0);
	iic_outw(adap,ITE_I2CHSR,ITE_I2CHSR_TDI);
}

static void iic_reset(struct i2c_algo_iic_data *adap)
{
	iic_outw(adap, PM_IBSR, iic_inw(adap, PM_IBSR) | 0x80);
}


static int wait_for_bb(struct i2c_algo_iic_data *adap)
{
	int timeout = DEF_TIMEOUT;
	short status;

	status = iic_inw(adap, ITE_I2CHSR);
#ifndef STUB_I2C
	while (timeout-- && (status & ITE_I2CHSR_HB)) {
		udelay(1000); /* How much is this? */
		status = iic_inw(adap, ITE_I2CHSR);
	}
#endif
	if (timeout<=0) {
		printk(KERN_ERR "Timeout, host is busy\n");
		iic_reset(adap);
	}
	return(timeout<=0);
}

/* After we issue a transaction on the IIC bus, this function
 * is called.  It puts this process to sleep until we get an interrupt from
 * from the controller telling us that the transaction we requested in complete.
 */
static int wait_for_pin(struct i2c_algo_iic_data *adap, short *status) {

	int timeout = DEF_TIMEOUT;
	
	timeout = wait_for_bb(adap);
	if (timeout) {
  		DEB2(printk("Timeout waiting for host not busy\n");)
  		return -EIO;
	}                           
	timeout = DEF_TIMEOUT;

	*status = iic_inw(adap, ITE_I2CHSR);
#ifndef STUB_I2C
	while (timeout-- && !(*status & ITE_I2CHSR_TDI)) {
	   adap->waitforpin();
	   *status = iic_inw(adap, ITE_I2CHSR);
	}
#endif
	if (timeout <= 0)
		return(-1);
	else
		return(0);
}

static int wait_for_fe(struct i2c_algo_iic_data *adap, short *status)
{
	int timeout = DEF_TIMEOUT;

	*status = iic_inw(adap, ITE_I2CFSR);
#ifndef STUB_I2C 
	while (timeout-- && (*status & ITE_I2CFSR_FE)) {
		udelay(1000);
		iic_inw(adap, ITE_I2CFSR);
	}
#endif
	if (timeout <= 0) 
		return(-1);
	else
		return(0);
}

static int iic_init (struct i2c_algo_iic_data *adap)
{
	short i;

	/* Clear bit 7 to set I2C to normal operation mode */
	i=iic_inw(adap, PM_DSR)& 0xff7f;
	iic_outw(adap, PM_DSR, i);

	/* set IT_GPCCR port C bit 2&3 as function 2 */
	i = iic_inw(adap, GPIO_CCR) & 0xfc0f;
	iic_outw(adap,GPIO_CCR,i);

	/* Clear slave address/sub-address */
	iic_outw(adap,ITE_I2CSAR, 0);
	iic_outw(adap,ITE_I2CSSAR, 0);

	/* Set clock counter register */
	iic_outw(adap,ITE_I2CCKCNT, get_clock(adap));

	/* Set START/reSTART/STOP time registers */
	iic_outw(adap,ITE_I2CSHDR, 0x0a);
	iic_outw(adap,ITE_I2CRSUR, 0x0a);
	iic_outw(adap,ITE_I2CPSUR, 0x0a);

	/* Enable interrupts on completing the current transaction */
	iic_outw(adap,ITE_I2CHCR, ITE_I2CHCR_IE | ITE_I2CHCR_HCE);

	/* Clear transfer count */
	iic_outw(adap,ITE_I2CFBCR, 0x0);

	DEB2(printk("iic_init: Initialized IIC on ITE 0x%x\n",
		iic_inw(adap, ITE_I2CHSR)));
	return 0;
}


/*
 * Sanity check for the adapter hardware - check the reaction of
 * the bus lines only if it seems to be idle.
 */
static int test_bus(struct i2c_algo_iic_data *adap, char *name) {
#if 0
	int scl,sda;
	sda=getsda(adap);
	if (adap->getscl==NULL) {
		printk("test_bus: Warning: Adapter can't read from clock line - skipping test.\n");
		return 0;		
	}
	scl=getscl(adap);
	printk("test_bus: Adapter: %s scl: %d  sda: %d -- testing...\n",
	name,getscl(adap),getsda(adap));
	if (!scl || !sda ) {
		printk("test_bus: %s seems to be busy.\n",adap->name);
		goto bailout;
	}
	sdalo(adap);
	printk("test_bus:1 scl: %d  sda: %d\n", getscl(adap),
	       getsda(adap));
	if ( 0 != getsda(adap) ) {
		printk("test_bus: %s SDA stuck high!\n",name);
		sdahi(adap);
		goto bailout;
	}
	if ( 0 == getscl(adap) ) {
		printk("test_bus: %s SCL unexpected low while pulling SDA low!\n",
			name);
		goto bailout;
	}		
	sdahi(adap);
	printk("test_bus:2 scl: %d  sda: %d\n", getscl(adap),
	       getsda(adap));
	if ( 0 == getsda(adap) ) {
		printk("test_bus: %s SDA stuck low!\n",name);
		sdahi(adap);
		goto bailout;
	}
	if ( 0 == getscl(adap) ) {
		printk("test_bus: %s SCL unexpected low while SDA high!\n",
		       adap->name);
	goto bailout;
	}
	scllo(adap);
	printk("test_bus:3 scl: %d  sda: %d\n", getscl(adap),
	       getsda(adap));
	if ( 0 != getscl(adap) ) {

		sclhi(adap);
		goto bailout;
	}
	if ( 0 == getsda(adap) ) {
		printk("test_bus: %s SDA unexpected low while pulling SCL low!\n",
			name);
		goto bailout;
	}
	sclhi(adap);
	printk("test_bus:4 scl: %d  sda: %d\n", getscl(adap),
	       getsda(adap));
	if ( 0 == getscl(adap) ) {
		printk("test_bus: %s SCL stuck low!\n",name);
		sclhi(adap);
		goto bailout;
	}
	if ( 0 == getsda(adap) ) {
		printk("test_bus: %s SDA unexpected low while SCL high!\n",
			name);
		goto bailout;
	}
	printk("test_bus: %s passed test.\n",name);
	return 0;
bailout:
	sdahi(adap);
	sclhi(adap);
	return -ENODEV;
#endif
	return (0);
}

/* ----- Utility functions
 */


/* Verify the device we want to talk to on the IIC bus really exists. */
static inline int try_address(struct i2c_algo_iic_data *adap,
		       unsigned int addr, int retries)
{
	int i, ret = -1;
	short status;

	for (i=0;i<retries;i++) {
		iic_outw(adap, ITE_I2CSAR, addr);
		iic_start(adap);
		if (wait_for_pin(adap, &status) == 0) {
			if ((status & ITE_I2CHSR_DNE) == 0) { 
				iic_stop(adap);
				iic_outw(adap, ITE_I2CFCR, ITE_I2CFCR_FLUSH);
				ret=1;
				break;	/* success! */
			}
		}
		iic_stop(adap);
		udelay(adap->udelay);
	}
	DEB2(if (i) printk("try_address: needed %d retries for 0x%x\n",i,
	                   addr));
	return ret;
}


static int iic_sendbytes(struct i2c_adapter *i2c_adap,const char *buf,
                         int count)
{
	struct i2c_algo_iic_data *adap = i2c_adap->algo_data;
	int wrcount=0, timeout;
	short status;
	int loops, remainder, i, j;
	union {
		char byte[2];
		unsigned short word;
	} tmp;
   
	iic_outw(adap, ITE_I2CSSAR, (unsigned short)buf[wrcount++]);
	count--;
	if (count == 0)
		return -EIO;

	loops =  count / 32;		/* 32-byte FIFO */
	remainder = count % 32;

	if(loops) {
		for(i=0; i<loops; i++) {

			iic_outw(adap, ITE_I2CFBCR, 32);
			for(j=0; j<32/2; j++) {
				tmp.byte[1] = buf[wrcount++];
				tmp.byte[0] = buf[wrcount++];
				iic_outw(adap, ITE_I2CFDR, tmp.word); 
			}

			/* status FIFO overrun */
			iic_inw(adap, ITE_I2CFSR);
			iic_inw(adap, ITE_I2CFBCR);

			iic_outw(adap, ITE_I2CHCR, ITE_WRITE);	/* Issue WRITE command */

			/* Wait for transmission to complete */
			timeout = wait_for_pin(adap, &status);
			if(timeout) {
				iic_stop(adap);
				printk("iic_sendbytes: %s write timeout.\n", i2c_adap->name);
				return -EREMOTEIO; /* got a better one ?? */
     	}
			if (status & ITE_I2CHSR_DB) {
				iic_stop(adap);
				printk("iic_sendbytes: %s write error - no ack.\n", i2c_adap->name);
				return -EREMOTEIO; /* got a better one ?? */
			}
		}
	}
	if(remainder) {
		iic_outw(adap, ITE_I2CFBCR, remainder);
		for(i=0; i<remainder/2; i++) {
			tmp.byte[1] = buf[wrcount++];
			tmp.byte[0] = buf[wrcount++];
			iic_outw(adap, ITE_I2CFDR, tmp.word);
		}

		/* status FIFO overrun */
		iic_inw(adap, ITE_I2CFSR);
		iic_inw(adap, ITE_I2CFBCR);

		iic_outw(adap, ITE_I2CHCR, ITE_WRITE);  /* Issue WRITE command */

		timeout = wait_for_pin(adap, &status);
		if(timeout) {
			iic_stop(adap);
			printk("iic_sendbytes: %s write timeout.\n", i2c_adap->name);
			return -EREMOTEIO; /* got a better one ?? */
		}
#ifndef STUB_I2C
		if (status & ITE_I2CHSR_DB) { 
			iic_stop(adap);
			printk("iic_sendbytes: %s write error - no ack.\n", i2c_adap->name);
			return -EREMOTEIO; /* got a better one ?? */
		}
#endif
	}
	iic_stop(adap);
	return wrcount;
}


static int iic_readbytes(struct i2c_adapter *i2c_adap, char *buf, int count,
	int sread)
{
	int rdcount=0, i, timeout;
	short status;
	struct i2c_algo_iic_data *adap = i2c_adap->algo_data;
	int loops, remainder, j;
	union {
		char byte[2];
		unsigned short word;
	} tmp;
		
	loops = count / 32;				/* 32-byte FIFO */
	remainder = count % 32;

	if(loops) {
		for(i=0; i<loops; i++) {
			iic_outw(adap, ITE_I2CFBCR, 32);
			if (sread)
				iic_outw(adap, ITE_I2CHCR, ITE_SREAD);
			else
				iic_outw(adap, ITE_I2CHCR, ITE_READ);		/* Issue READ command */

			timeout = wait_for_pin(adap, &status);
			if(timeout) {
				iic_stop(adap);
				printk("iic_readbytes:  %s read timeout.\n", i2c_adap->name);
				return (-1);
			}
#ifndef STUB_I2C
			if (status & ITE_I2CHSR_DB) {
				iic_stop(adap);
				printk("iic_readbytes: %s read error - no ack.\n", i2c_adap->name);
				return (-1);
			}
#endif

			timeout = wait_for_fe(adap, &status);
			if(timeout) {
				iic_stop(adap);
				printk("iic_readbytes:  %s FIFO is empty\n", i2c_adap->name);
				return (-1); 
			}

			for(j=0; j<32/2; j++) {
				tmp.word = iic_inw(adap, ITE_I2CFDR);
				buf[rdcount++] = tmp.byte[1];
				buf[rdcount++] = tmp.byte[0];
			}

			/* status FIFO underrun */
			iic_inw(adap, ITE_I2CFSR);

		}
	}


	if(remainder) {
		remainder=(remainder+1)/2 * 2;
		iic_outw(adap, ITE_I2CFBCR, remainder);
		if (sread)
			iic_outw(adap, ITE_I2CHCR, ITE_SREAD);
		else
		iic_outw(adap, ITE_I2CHCR, ITE_READ);		/* Issue READ command */

		timeout = wait_for_pin(adap, &status);
		if(timeout) {
			iic_stop(adap);
			printk("iic_readbytes:  %s read timeout.\n", i2c_adap->name);
			return (-1);
		}
#ifndef STUB_I2C
		if (status & ITE_I2CHSR_DB) {
			iic_stop(adap);
			printk("iic_readbytes: %s read error - no ack.\n", i2c_adap->name);
			return (-1);
		}
#endif
		timeout = wait_for_fe(adap, &status);
		if(timeout) {
			iic_stop(adap);
			printk("iic_readbytes:  %s FIFO is empty\n", i2c_adap->name);
			return (-1);
		}         

		for(i=0; i<(remainder+1)/2; i++) {
			tmp.word = iic_inw(adap, ITE_I2CFDR);
			buf[rdcount++] = tmp.byte[1];
			buf[rdcount++] = tmp.byte[0];
		}

		/* status FIFO underrun */
		iic_inw(adap, ITE_I2CFSR);

	}

	iic_stop(adap);
	return rdcount;
}


/* This function implements combined transactions.  Combined
 * transactions consist of combinations of reading and writing blocks of data.
 * Each transfer (i.e. a read or a write) is separated by a repeated start
 * condition.
 */
#if 0
static int iic_combined_transaction(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs, int num) 
{
   int i;
   struct i2c_msg *pmsg;
   int ret;

   DEB2(printk("Beginning combined transaction\n"));

   for(i=0; i<(num-1); i++) {
      pmsg = &msgs[i];
      if(pmsg->flags & I2C_M_RD) {
         DEB2(printk("  This one is a read\n"));
         ret = iic_readbytes(i2c_adap, pmsg->buf, pmsg->len, IIC_COMBINED_XFER);
      }
      else if(!(pmsg->flags & I2C_M_RD)) {
         DEB2(printk("This one is a write\n"));
         ret = iic_sendbytes(i2c_adap, pmsg->buf, pmsg->len, IIC_COMBINED_XFER);
      }
   }
   /* Last read or write segment needs to be terminated with a stop */
   pmsg = &msgs[i];

   if(pmsg->flags & I2C_M_RD) {
      DEB2(printk("Doing the last read\n"));
      ret = iic_readbytes(i2c_adap, pmsg->buf, pmsg->len, IIC_SINGLE_XFER);
   }
   else if(!(pmsg->flags & I2C_M_RD)) {
      DEB2(printk("Doing the last write\n"));
      ret = iic_sendbytes(i2c_adap, pmsg->buf, pmsg->len, IIC_SINGLE_XFER);
   }

   return ret;
}
#endif


/* Whenever we initiate a transaction, the first byte clocked
 * onto the bus after the start condition is the address (7 bit) of the
 * device we want to talk to.  This function manipulates the address specified
 * so that it makes sense to the hardware when written to the IIC peripheral.
 *
 * Note: 10 bit addresses are not supported in this driver, although they are
 * supported by the hardware.  This functionality needs to be implemented.
 */
static inline int iic_doAddress(struct i2c_algo_iic_data *adap,
                                struct i2c_msg *msg, int retries) 
{
	unsigned short flags = msg->flags;
	unsigned int addr;
	int ret;

/* Ten bit addresses not supported right now */
	if ( (flags & I2C_M_TEN)  ) { 
#if 0
		addr = 0xf0 | (( msg->addr >> 7) & 0x03);
		DEB2(printk("addr0: %d\n",addr));
		ret = try_address(adap, addr, retries);
		if (ret!=1) {
			printk("iic_doAddress: died at extended address code.\n");
			return -EREMOTEIO;
		}
		iic_outw(adap,msg->addr & 0x7f);
		if (ret != 1) {
			printk("iic_doAddress: died at 2nd address code.\n");
			return -EREMOTEIO;
		}
		if ( flags & I2C_M_RD ) {
			i2c_repstart(adap);
			addr |= 0x01;
			ret = try_address(adap, addr, retries);
			if (ret!=1) {
				printk("iic_doAddress: died at extended address code.\n");
				return -EREMOTEIO;
			}
		}
#endif
	} else {

		addr = ( msg->addr << 1 );

#if 0
		if (flags & I2C_M_RD )
			addr |= 1;
		if (flags & I2C_M_REV_DIR_ADDR )
			addr ^= 1;
#endif

		if (iic_inw(adap, ITE_I2CSAR) != addr) {
			iic_outw(adap, ITE_I2CSAR, addr);
			ret = try_address(adap, addr, retries);
			if (ret!=1) {
				printk("iic_doAddress: died at address code.\n");
				return -EREMOTEIO;
			}
		}

  }

	return 0;
}


/* Description: Prepares the controller for a transaction (clearing status
 * registers, data buffers, etc), and then calls either iic_readbytes or
 * iic_sendbytes to do the actual transaction.
 *
 * still to be done: Before we issue a transaction, we should
 * verify that the bus is not busy or in some unknown state.
 */
static int iic_xfer(struct i2c_adapter *i2c_adap,
		    struct i2c_msg *msgs, 
		    int num)
{
	struct i2c_algo_iic_data *adap = i2c_adap->algo_data;
	struct i2c_msg *pmsg;
	int i = 0;
	int ret, timeout;
    
	pmsg = &msgs[i];

	if(!pmsg->len) {
		DEB2(printk("iic_xfer: read/write length is 0\n");)
		return -EIO;
	}
	if(!(pmsg->flags & I2C_M_RD) && (!(pmsg->len)%2) ) {
		DEB2(printk("iic_xfer: write buffer length is not odd\n");)
		return -EIO; 
	}

	/* Wait for any pending transfers to complete */
	timeout = wait_for_bb(adap);
	if (timeout) {
		DEB2(printk("iic_xfer: Timeout waiting for host not busy\n");)
		return -EIO;
	}

	/* Flush FIFO */
	iic_outw(adap, ITE_I2CFCR, ITE_I2CFCR_FLUSH);

	/* Load address */
	ret = iic_doAddress(adap, pmsg, i2c_adap->retries);
	if (ret)
		return -EIO;

#if 0
	/* Combined transaction (read and write) */
	if(num > 1) {
           DEB2(printk("iic_xfer: Call combined transaction\n"));
           ret = iic_combined_transaction(i2c_adap, msgs, num);
  }
#endif

	DEB3(printk("iic_xfer: Msg %d, addr=0x%x, flags=0x%x, len=%d\n",
		i, msgs[i].addr, msgs[i].flags, msgs[i].len);)

	if(pmsg->flags & I2C_M_RD) 		/* Read */
		ret = iic_readbytes(i2c_adap, pmsg->buf, pmsg->len, 0);
	else {													/* Write */ 
		udelay(1000);
		ret = iic_sendbytes(i2c_adap, pmsg->buf, pmsg->len);
	}

	if (ret != pmsg->len)
		DEB3(printk("iic_xfer: error or fail on read/write %d bytes.\n",ret)); 
	else
		DEB3(printk("iic_xfer: read/write %d bytes.\n",ret));

	return ret;
}


/* Implements device specific ioctls.  Higher level ioctls can
 * be found in i2c-core.c and are typical of any i2c controller (specifying
 * slave address, timeouts, etc).  These ioctls take advantage of any hardware
 * features built into the controller for which this algorithm-adapter set
 * was written.  These ioctls allow you to take control of the data and clock
 * lines and set the either high or low,
 * similar to a GPIO pin.
 */
static int algo_control(struct i2c_adapter *adapter, 
	unsigned int cmd, unsigned long arg)
{

  struct i2c_algo_iic_data *adap = adapter->algo_data;
  struct i2c_iic_msg s_msg;
  char *buf;
	int ret;

  if (cmd == I2C_SREAD) {
		if(copy_from_user(&s_msg, (struct i2c_iic_msg *)arg, 
				sizeof(struct i2c_iic_msg))) 
			return -EFAULT;
		buf = kmalloc(s_msg.len, GFP_KERNEL);
		if (buf== NULL)
			return -ENOMEM;

		/* Flush FIFO */
		iic_outw(adap, ITE_I2CFCR, ITE_I2CFCR_FLUSH);

		/* Load address */
		iic_outw(adap, ITE_I2CSAR,s_msg.addr<<1);
		iic_outw(adap, ITE_I2CSSAR,s_msg.waddr & 0xff);

		ret = iic_readbytes(adapter, buf, s_msg.len, 1);
		if (ret>=0) {
			if(copy_to_user( s_msg.buf, buf, s_msg.len) ) 
				ret = -EFAULT;
		}
		kfree(buf);
	}
	return 0;
}


static u32 iic_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR | 
	       I2C_FUNC_PROTOCOL_MANGLING; 
}

/* -----exported algorithm data: -------------------------------------	*/

static struct i2c_algorithm iic_algo = {
	.master_xfer	= iic_xfer,
	.algo_control	= algo_control, /* ioctl */
	.functionality	= iic_func,
};


/* 
 * registering functions to load algorithms at runtime 
 */
int i2c_iic_add_bus(struct i2c_adapter *adap)
{
	struct i2c_algo_iic_data *iic_adap = adap->algo_data;

	if (iic_test) {
		int ret = test_bus(iic_adap, adap->name);
		if (ret<0)
			return -ENODEV;
	}

	DEB2(printk("i2c-algo-ite: hw routines for %s registered.\n",
	            adap->name));

	/* register new adapter to i2c module... */
	adap->algo = &iic_algo;

	adap->timeout = 100;	/* default values, should	*/
	adap->retries = 3;		/* be replaced by defines	*/
	adap->flags = 0;

	i2c_add_adapter(adap);
	iic_init(iic_adap);

	return 0;
}


int i2c_iic_del_bus(struct i2c_adapter *adap)
{
	int res;
	if ((res = i2c_del_adapter(adap)) < 0)
		return res;
	DEB2(printk("i2c-algo-ite: adapter unregistered: %s\n",adap->name));

	return 0;
}


int __init i2c_algo_iic_init (void)
{
	printk(KERN_INFO "ITE iic (i2c) algorithm module\n");
	return 0;
}


void i2c_algo_iic_exit(void)
{
	return;
}


EXPORT_SYMBOL(i2c_iic_add_bus);
EXPORT_SYMBOL(i2c_iic_del_bus);

/* The MODULE_* macros resolve to nothing if MODULES is not defined
 * when this file is compiled.
 */
MODULE_AUTHOR("MontaVista Software <www.mvista.com>");
MODULE_DESCRIPTION("ITE iic algorithm");
MODULE_LICENSE("GPL");

module_param(iic_test, bool, 0);
module_param(i2c_debug, int, S_IRUGO | S_IWUSR);

MODULE_PARM_DESC(iic_test, "Test if the I2C bus is available");
MODULE_PARM_DESC(i2c_debug,
        "debug level - 0 off; 1 normal; 2,3 more verbose; 9 iic-protocol");


/* This function resolves to init_module (the function invoked when a module
 * is loaded via insmod) when this file is compiled with MODULES defined.
 * Otherwise (i.e. if you want this driver statically linked to the kernel),
 * a pointer to this function is stored in a table and called
 * during the initialization of the kernel (in do_basic_setup in /init/main.c) 
 *
 * All this functionality is complements of the macros defined in linux/init.h
 */
module_init(i2c_algo_iic_init);


/* If MODULES is defined when this file is compiled, then this function will
 * resolved to cleanup_module.
 */
module_exit(i2c_algo_iic_exit);
