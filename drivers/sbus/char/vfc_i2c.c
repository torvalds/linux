/*
 * drivers/sbus/char/vfc_i2c.c
 *
 * Driver for the Videopix Frame Grabber.
 * 
 * Functions that support the Phillips i2c(I squared C) bus on the vfc
 *  Documentation for the Phillips I2C bus can be found on the 
 *  phillips home page
 *
 * Copyright (C) 1996 Manish Vachharajani (mvachhar@noc.rutgers.edu)
 *
 */

/* NOTE: It seems to me that the documentation regarding the
pcd8584t/pcf8584 does not show the correct way to address the i2c bus.
Based on the information on the I2C bus itself and the remainder of
the Phillips docs the following algorithims apper to be correct.  I am
fairly certain that the flowcharts in the phillips docs are wrong. */


#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sbus.h>

#if 0 
#define VFC_I2C_DEBUG
#endif

#include "vfc.h"
#include "vfc_i2c.h"

#define WRITE_S1(__val) \
	sbus_writel(__val, &dev->regs->i2c_s1)
#define WRITE_REG(__val) \
	sbus_writel(__val, &dev->regs->i2c_reg)

#define VFC_I2C_READ (0x1)
#define VFC_I2C_WRITE (0x0)
     
/****** 
  The i2c bus controller chip on the VFC is a pcd8584t, but
  phillips claims it doesn't exist.  As far as I can tell it is
  identical to the PCF8584 so I treat it like it is the pcf8584.
  
  NOTE: The pcf8584 only cares
  about the msb of the word you feed it 
*****/

int vfc_pcf8584_init(struct vfc_dev *dev) 
{
	/* This will also choose register S0_OWN so we can set it. */
	WRITE_S1(RESET);

	/* The pcf8584 shifts this value left one bit and uses
	 * it as its i2c bus address.
	 */
	WRITE_REG(0x55000000);

	/* This will set the i2c bus at the same speed sun uses,
	 * and set another magic bit.
	 */
	WRITE_S1(SELECT(S2));
	WRITE_REG(0x14000000);
	
	/* Enable the serial port, idle the i2c bus and set
	 * the data reg to s0.
	 */
	WRITE_S1(CLEAR_I2C_BUS);
	udelay(100);
	return 0;
}

void vfc_i2c_delay_no_busy(struct vfc_dev *dev, unsigned long usecs) 
{
	schedule_timeout_uninterruptible(usecs_to_jiffies(usecs));
}

void inline vfc_i2c_delay(struct vfc_dev *dev) 
{ 
	vfc_i2c_delay_no_busy(dev, 100);
}

int vfc_init_i2c_bus(struct vfc_dev *dev)
{
	WRITE_S1(ENABLE_SERIAL | SELECT(S0) | ACK);
	vfc_i2c_reset_bus(dev);
	return 0;
}

int vfc_i2c_reset_bus(struct vfc_dev *dev) 
{
	VFC_I2C_DEBUG_PRINTK((KERN_DEBUG "vfc%d: Resetting the i2c bus\n",
			      dev->instance));
	if(dev == NULL)
		return -EINVAL;
	if(dev->regs == NULL)
		return -EINVAL;
	WRITE_S1(SEND_I2C_STOP);
	WRITE_S1(SEND_I2C_STOP | ACK);
	vfc_i2c_delay(dev);
	WRITE_S1(CLEAR_I2C_BUS);
	VFC_I2C_DEBUG_PRINTK((KERN_DEBUG "vfc%d: I2C status %x\n",
			      dev->instance,
			      sbus_readl(&dev->regs->i2c_s1)));
	return 0;
}

int vfc_i2c_wait_for_bus(struct vfc_dev *dev) 
{
	int timeout = 1000; 

	while(!(sbus_readl(&dev->regs->i2c_s1) & BB)) {
		if(!(timeout--))
			return -ETIMEDOUT;
		vfc_i2c_delay(dev);
	}
	return 0;
}

int vfc_i2c_wait_for_pin(struct vfc_dev *dev, int ack)
{
	int timeout = 1000; 
	int s1;

	while ((s1 = sbus_readl(&dev->regs->i2c_s1)) & PIN) {
		if (!(timeout--))
			return -ETIMEDOUT;
		vfc_i2c_delay(dev);
	}
	if (ack == VFC_I2C_ACK_CHECK) {
		if(s1 & LRB)
			return -EIO; 
	}
	return 0;
}

#define SHIFT(a) ((a) << 24)
int vfc_i2c_xmit_addr(struct vfc_dev *dev, unsigned char addr, char mode) 
{ 
	int ret, raddr;
#if 1
	WRITE_S1(SEND_I2C_STOP | ACK);
	WRITE_S1(SELECT(S0) | ENABLE_SERIAL);
	vfc_i2c_delay(dev);
#endif

	switch(mode) {
	case VFC_I2C_READ:
		raddr = SHIFT(((unsigned int)addr | 0x1));
		WRITE_REG(raddr);
		VFC_I2C_DEBUG_PRINTK(("vfc%d: receiving from i2c addr 0x%x\n",
				      dev->instance, addr | 0x1));
		break;
	case VFC_I2C_WRITE:
		raddr = SHIFT((unsigned int)addr & ~0x1);
		WRITE_REG(raddr);
		VFC_I2C_DEBUG_PRINTK(("vfc%d: sending to i2c addr 0x%x\n",
				      dev->instance, addr & ~0x1));
		break;
	default:
		return -EINVAL;
	};

	WRITE_S1(SEND_I2C_START);
	vfc_i2c_delay(dev);
	ret = vfc_i2c_wait_for_pin(dev,VFC_I2C_ACK_CHECK); /* We wait
							      for the
							      i2c send
							      to finish
							      here but
							      Sun
							      doesn't,
							      hmm */
	if (ret) {
		printk(KERN_ERR "vfc%d: VFC xmit addr timed out or no ack\n",
		       dev->instance);
		return ret;
	} else if (mode == VFC_I2C_READ) {
		if ((ret = sbus_readl(&dev->regs->i2c_reg) & 0xff000000) != raddr) {
			printk(KERN_WARNING 
			       "vfc%d: returned slave address "
			       "mismatch(%x,%x)\n",
			       dev->instance, raddr, ret);
		}
	}	
	return 0;
}

int vfc_i2c_xmit_byte(struct vfc_dev *dev,unsigned char *byte) 
{
	int ret;
	u32 val = SHIFT((unsigned int)*byte);

	WRITE_REG(val);

	ret = vfc_i2c_wait_for_pin(dev, VFC_I2C_ACK_CHECK); 
	switch(ret) {
	case -ETIMEDOUT: 
		printk(KERN_ERR "vfc%d: VFC xmit byte timed out or no ack\n",
		       dev->instance);
		break;
	case -EIO:
		ret = XMIT_LAST_BYTE;
		break;
	default:
		break;
	};

	return ret;
}

int vfc_i2c_recv_byte(struct vfc_dev *dev, unsigned char *byte, int last) 
{
	int ret;

	if (last) {
		WRITE_REG(NEGATIVE_ACK);
		VFC_I2C_DEBUG_PRINTK(("vfc%d: sending negative ack\n",
				      dev->instance));
	} else {
		WRITE_S1(ACK);
	}

	ret = vfc_i2c_wait_for_pin(dev, VFC_I2C_NO_ACK_CHECK);
	if(ret) {
		printk(KERN_ERR "vfc%d: "
		       "VFC recv byte timed out\n",
		       dev->instance);
	}
	*byte = (sbus_readl(&dev->regs->i2c_reg)) >> 24;
	return ret;
}

int vfc_i2c_recvbuf(struct vfc_dev *dev, unsigned char addr,
		    char *buf, int count)
{
	int ret, last;

	if(!(count && buf && dev && dev->regs) )
		return -EINVAL;

	if ((ret = vfc_i2c_wait_for_bus(dev))) {
		printk(KERN_ERR "vfc%d: VFC I2C bus busy\n", dev->instance);
		return ret;
	}

	if ((ret = vfc_i2c_xmit_addr(dev, addr, VFC_I2C_READ))) {
		WRITE_S1(SEND_I2C_STOP);
		vfc_i2c_delay(dev);
		return ret;
	}
	
	last = 0;
	while (count--) {
		if (!count)
			last = 1;
		if ((ret = vfc_i2c_recv_byte(dev, buf, last))) {
			printk(KERN_ERR "vfc%d: "
			       "VFC error while receiving byte\n",
			       dev->instance);
			WRITE_S1(SEND_I2C_STOP);
			ret = -EINVAL;
		}
		buf++;
	}
	WRITE_S1(SEND_I2C_STOP | ACK);
	vfc_i2c_delay(dev);
	return ret;
}

int vfc_i2c_sendbuf(struct vfc_dev *dev, unsigned char addr, 
		    char *buf, int count) 
{
	int ret;
	
	if (!(buf && dev && dev->regs))
		return -EINVAL;
	
	if ((ret = vfc_i2c_wait_for_bus(dev))) {
		printk(KERN_ERR "vfc%d: VFC I2C bus busy\n", dev->instance);
		return ret;
	}
	
	if ((ret = vfc_i2c_xmit_addr(dev, addr, VFC_I2C_WRITE))) {
		WRITE_S1(SEND_I2C_STOP);
		vfc_i2c_delay(dev);
		return ret;
	}
	
	while(count--) {
		ret = vfc_i2c_xmit_byte(dev, buf);
		switch(ret) {
		case XMIT_LAST_BYTE:
			VFC_I2C_DEBUG_PRINTK(("vfc%d: "
					      "Receiver ended transmission with "
					      " %d bytes remaining\n",
					      dev->instance, count));
			ret = 0;
			goto done;
			break;
		case 0:
			break;
		default:
			printk(KERN_ERR "vfc%d: "
			       "VFC error while sending byte\n", dev->instance);
			break;
		};

		buf++;
	}
done:
	WRITE_S1(SEND_I2C_STOP | ACK);
	vfc_i2c_delay(dev);
	return ret;
}









