#include <media/saa7146_vv.h>

static u32 saa7146_i2c_func(struct i2c_adapter *adapter)
{
//fm	DEB_I2C(("'%s'.\n", adapter->name));

	return	  I2C_FUNC_I2C
		| I2C_FUNC_SMBUS_QUICK
		| I2C_FUNC_SMBUS_READ_BYTE	| I2C_FUNC_SMBUS_WRITE_BYTE
		| I2C_FUNC_SMBUS_READ_BYTE_DATA | I2C_FUNC_SMBUS_WRITE_BYTE_DATA;
}

/* this function returns the status-register of our i2c-device */
static inline u32 saa7146_i2c_status(struct saa7146_dev *dev)
{
	u32 iicsta = saa7146_read(dev, I2C_STATUS);
/*
	DEB_I2C(("status: 0x%08x\n",iicsta));
*/
	return iicsta;
}

/* this function runs through the i2c-messages and prepares the data to be
   sent through the saa7146. have a look at the specifications p. 122 ff
   to understand this. it returns the number of u32s to send, or -1
   in case of an error. */
static int saa7146_i2c_msg_prepare(const struct i2c_msg *m, int num, __le32 *op)
{
	int h1, h2;
	int i, j, addr;
	int mem = 0, op_count = 0;

	/* first determine size of needed memory */
	for(i = 0; i < num; i++) {
		mem += m[i].len + 1;
	}

	/* worst case: we need one u32 for three bytes to be send
	   plus one extra byte to address the device */
	mem = 1 + ((mem-1) / 3);

	/* we assume that op points to a memory of at least SAA7146_I2C_MEM bytes
	   size. if we exceed this limit... */
	if ( (4*mem) > SAA7146_I2C_MEM ) {
//fm		DEB_I2C(("cannot prepare i2c-message.\n"));
		return -ENOMEM;
	}

	/* be careful: clear out the i2c-mem first */
	memset(op,0,sizeof(__le32)*mem);

	/* loop through all messages */
	for(i = 0; i < num; i++) {

		/* insert the address of the i2c-slave.
		   note: we get 7 bit i2c-addresses,
		   so we have to perform a translation */
		addr = (m[i].addr*2) + ( (0 != (m[i].flags & I2C_M_RD)) ? 1 : 0);
		h1 = op_count/3; h2 = op_count%3;
		op[h1] |= cpu_to_le32(	    (u8)addr << ((3-h2)*8));
		op[h1] |= cpu_to_le32(SAA7146_I2C_START << ((3-h2)*2));
		op_count++;

		/* loop through all bytes of message i */
		for(j = 0; j < m[i].len; j++) {
			/* insert the data bytes */
			h1 = op_count/3; h2 = op_count%3;
			op[h1] |= cpu_to_le32( (u32)((u8)m[i].buf[j]) << ((3-h2)*8));
			op[h1] |= cpu_to_le32(       SAA7146_I2C_CONT << ((3-h2)*2));
			op_count++;
		}

	}

	/* have a look at the last byte inserted:
	  if it was: ...CONT change it to ...STOP */
	h1 = (op_count-1)/3; h2 = (op_count-1)%3;
	if ( SAA7146_I2C_CONT == (0x3 & (le32_to_cpu(op[h1]) >> ((3-h2)*2))) ) {
		op[h1] &= ~cpu_to_le32(0x2 << ((3-h2)*2));
		op[h1] |= cpu_to_le32(SAA7146_I2C_STOP << ((3-h2)*2));
	}

	/* return the number of u32s to send */
	return mem;
}

/* this functions loops through all i2c-messages. normally, it should determine
   which bytes were read through the adapter and write them back to the corresponding
   i2c-message. but instead, we simply write back all bytes.
   fixme: this could be improved. */
static int saa7146_i2c_msg_cleanup(const struct i2c_msg *m, int num, __le32 *op)
{
	int i, j;
	int op_count = 0;

	/* loop through all messages */
	for(i = 0; i < num; i++) {

		op_count++;

		/* loop throgh all bytes of message i */
		for(j = 0; j < m[i].len; j++) {
			/* write back all bytes that could have been read */
			m[i].buf[j] = (le32_to_cpu(op[op_count/3]) >> ((3-(op_count%3))*8));
			op_count++;
		}
	}

	return 0;
}

/* this functions resets the i2c-device and returns 0 if everything was fine, otherwise -1 */
static int saa7146_i2c_reset(struct saa7146_dev *dev)
{
	/* get current status */
	u32 status = saa7146_i2c_status(dev);

	/* clear registers for sure */
	saa7146_write(dev, I2C_STATUS, dev->i2c_bitrate);
	saa7146_write(dev, I2C_TRANSFER, 0);

	/* check if any operation is still in progress */
	if ( 0 != ( status & SAA7146_I2C_BUSY) ) {

		/* yes, kill ongoing operation */
		DEB_I2C(("busy_state detected.\n"));

		/* set "ABORT-OPERATION"-bit (bit 7)*/
		saa7146_write(dev, I2C_STATUS, (dev->i2c_bitrate | MASK_07));
		saa7146_write(dev, MC2, (MASK_00 | MASK_16));
		msleep(SAA7146_I2C_DELAY);

		/* clear all error-bits pending; this is needed because p.123, note 1 */
		saa7146_write(dev, I2C_STATUS, dev->i2c_bitrate);
		saa7146_write(dev, MC2, (MASK_00 | MASK_16));
		msleep(SAA7146_I2C_DELAY);
	}

	/* check if any error is (still) present. (this can be necessary because p.123, note 1) */
	status = saa7146_i2c_status(dev);

	if ( dev->i2c_bitrate != status ) {

		DEB_I2C(("error_state detected. status:0x%08x\n",status));

		/* Repeat the abort operation. This seems to be necessary
		   after serious protocol errors caused by e.g. the SAA7740 */
		saa7146_write(dev, I2C_STATUS, (dev->i2c_bitrate | MASK_07));
		saa7146_write(dev, MC2, (MASK_00 | MASK_16));
		msleep(SAA7146_I2C_DELAY);

		/* clear all error-bits pending */
		saa7146_write(dev, I2C_STATUS, dev->i2c_bitrate);
		saa7146_write(dev, MC2, (MASK_00 | MASK_16));
		msleep(SAA7146_I2C_DELAY);

		/* the data sheet says it might be necessary to clear the status
		   twice after an abort */
		saa7146_write(dev, I2C_STATUS, dev->i2c_bitrate);
		saa7146_write(dev, MC2, (MASK_00 | MASK_16));
		msleep(SAA7146_I2C_DELAY);
	}

	/* if any error is still present, a fatal error has occured ... */
	status = saa7146_i2c_status(dev);
	if ( dev->i2c_bitrate != status ) {
		DEB_I2C(("fatal error. status:0x%08x\n",status));
		return -1;
	}

	return 0;
}

/* this functions writes out the data-byte 'dword' to the i2c-device.
   it returns 0 if ok, -1 if the transfer failed, -2 if the transfer
   failed badly (e.g. address error) */
static int saa7146_i2c_writeout(struct saa7146_dev *dev, __le32 *dword, int short_delay)
{
	u32 status = 0, mc2 = 0;
	int trial = 0;
	unsigned long timeout;

	/* write out i2c-command */
	DEB_I2C(("before: 0x%08x (status: 0x%08x), %d\n",*dword,saa7146_read(dev, I2C_STATUS), dev->i2c_op));

	if( 0 != (SAA7146_USE_I2C_IRQ & dev->ext->flags)) {

		saa7146_write(dev, I2C_STATUS,	 dev->i2c_bitrate);
		saa7146_write(dev, I2C_TRANSFER, le32_to_cpu(*dword));

		dev->i2c_op = 1;
		SAA7146_ISR_CLEAR(dev, MASK_16|MASK_17);
		SAA7146_IER_ENABLE(dev, MASK_16|MASK_17);
		saa7146_write(dev, MC2, (MASK_00 | MASK_16));

		timeout = HZ/100 + 1; /* 10ms */
		timeout = wait_event_interruptible_timeout(dev->i2c_wq, dev->i2c_op == 0, timeout);
		if (timeout == -ERESTARTSYS || dev->i2c_op) {
			SAA7146_IER_DISABLE(dev, MASK_16|MASK_17);
			SAA7146_ISR_CLEAR(dev, MASK_16|MASK_17);
			if (timeout == -ERESTARTSYS)
				/* a signal arrived */
				return -ERESTARTSYS;

			printk(KERN_WARNING "%s %s [irq]: timed out waiting for end of xfer\n",
				dev->name, __func__);
			return -EIO;
		}
		status = saa7146_read(dev, I2C_STATUS);
	} else {
		saa7146_write(dev, I2C_STATUS,	 dev->i2c_bitrate);
		saa7146_write(dev, I2C_TRANSFER, le32_to_cpu(*dword));
		saa7146_write(dev, MC2, (MASK_00 | MASK_16));

		/* do not poll for i2c-status before upload is complete */
		timeout = jiffies + HZ/100 + 1; /* 10ms */
		while(1) {
			mc2 = (saa7146_read(dev, MC2) & 0x1);
			if( 0 != mc2 ) {
				break;
			}
			if (time_after(jiffies,timeout)) {
				printk(KERN_WARNING "%s %s: timed out waiting for MC2\n",
					dev->name, __func__);
				return -EIO;
			}
		}
		/* wait until we get a transfer done or error */
		timeout = jiffies + HZ/100 + 1; /* 10ms */
		/* first read usually delivers bogus results... */
		saa7146_i2c_status(dev);
		while(1) {
			status = saa7146_i2c_status(dev);
			if ((status & 0x3) != 1)
				break;
			if (time_after(jiffies,timeout)) {
				/* this is normal when probing the bus
				 * (no answer from nonexisistant device...)
				 */
				printk(KERN_WARNING "%s %s [poll]: timed out waiting for end of xfer\n",
					dev->name, __func__);
				return -EIO;
			}
			if (++trial < 50 && short_delay)
				udelay(10);
			else
				msleep(1);
		}
	}

	/* give a detailed status report */
	if ( 0 != (status & (SAA7146_I2C_SPERR | SAA7146_I2C_APERR |
			     SAA7146_I2C_DTERR | SAA7146_I2C_DRERR |
			     SAA7146_I2C_AL    | SAA7146_I2C_ERR   |
			     SAA7146_I2C_BUSY)) ) {

		if ( 0 == (status & SAA7146_I2C_ERR) ||
		     0 == (status & SAA7146_I2C_BUSY) ) {
			/* it may take some time until ERR goes high - ignore */
			DEB_I2C(("unexpected i2c status %04x\n", status));
		}
		if( 0 != (status & SAA7146_I2C_SPERR) ) {
			DEB_I2C(("error due to invalid start/stop condition.\n"));
		}
		if( 0 != (status & SAA7146_I2C_DTERR) ) {
			DEB_I2C(("error in data transmission.\n"));
		}
		if( 0 != (status & SAA7146_I2C_DRERR) ) {
			DEB_I2C(("error when receiving data.\n"));
		}
		if( 0 != (status & SAA7146_I2C_AL) ) {
			DEB_I2C(("error because arbitration lost.\n"));
		}

		/* we handle address-errors here */
		if( 0 != (status & SAA7146_I2C_APERR) ) {
			DEB_I2C(("error in address phase.\n"));
			return -EREMOTEIO;
		}

		return -EIO;
	}

	/* read back data, just in case we were reading ... */
	*dword = cpu_to_le32(saa7146_read(dev, I2C_TRANSFER));

	DEB_I2C(("after: 0x%08x\n",*dword));
	return 0;
}

static int saa7146_i2c_transfer(struct saa7146_dev *dev, const struct i2c_msg *msgs, int num, int retries)
{
	int i = 0, count = 0;
	__le32 *buffer = dev->d_i2c.cpu_addr;
	int err = 0;
	int short_delay = 0;

	if (mutex_lock_interruptible(&dev->i2c_lock))
		return -ERESTARTSYS;

	for(i=0;i<num;i++) {
		DEB_I2C(("msg:%d/%d\n",i+1,num));
	}

	/* prepare the message(s), get number of u32s to transfer */
	count = saa7146_i2c_msg_prepare(msgs, num, buffer);
	if ( 0 > count ) {
		err = -1;
		goto out;
	}

	if ( count > 3 || 0 != (SAA7146_I2C_SHORT_DELAY & dev->ext->flags) )
		short_delay = 1;

	do {
		/* reset the i2c-device if necessary */
		err = saa7146_i2c_reset(dev);
		if ( 0 > err ) {
			DEB_I2C(("could not reset i2c-device.\n"));
			goto out;
		}

		/* write out the u32s one after another */
		for(i = 0; i < count; i++) {
			err = saa7146_i2c_writeout(dev, &buffer[i], short_delay);
			if ( 0 != err) {
				/* this one is unsatisfying: some i2c slaves on some
				   dvb cards don't acknowledge correctly, so the saa7146
				   thinks that an address error occured. in that case, the
				   transaction should be retrying, even if an address error
				   occured. analog saa7146 based cards extensively rely on
				   i2c address probing, however, and address errors indicate that a
				   device is really *not* there. retrying in that case
				   increases the time the device needs to probe greatly, so
				   it should be avoided. So we bail out in irq mode after an
				   address error and trust the saa7146 address error detection. */
				if (-EREMOTEIO == err && 0 != (SAA7146_USE_I2C_IRQ & dev->ext->flags))
					goto out;
				DEB_I2C(("error while sending message(s). starting again.\n"));
				break;
			}
		}
		if( 0 == err ) {
			err = num;
			break;
		}

		/* delay a bit before retrying */
		msleep(10);

	} while (err != num && retries--);

	/* quit if any error occurred */
	if (err != num)
		goto out;

	/* if any things had to be read, get the results */
	if ( 0 != saa7146_i2c_msg_cleanup(msgs, num, buffer)) {
		DEB_I2C(("could not cleanup i2c-message.\n"));
		err = -1;
		goto out;
	}

	/* return the number of delivered messages */
	DEB_I2C(("transmission successful. (msg:%d).\n",err));
out:
	/* another bug in revision 0: the i2c-registers get uploaded randomly by other
	   uploads, so we better clear them out before continueing */
	if( 0 == dev->revision ) {
		__le32 zero = 0;
		saa7146_i2c_reset(dev);
		if( 0 != saa7146_i2c_writeout(dev, &zero, short_delay)) {
			INFO(("revision 0 error. this should never happen.\n"));
		}
	}

	mutex_unlock(&dev->i2c_lock);
	return err;
}

/* utility functions */
static int saa7146_i2c_xfer(struct i2c_adapter* adapter, struct i2c_msg *msg, int num)
{
	struct v4l2_device *v4l2_dev = i2c_get_adapdata(adapter);
	struct saa7146_dev *dev = to_saa7146_dev(v4l2_dev);

	/* use helper function to transfer data */
	return saa7146_i2c_transfer(dev, msg, num, adapter->retries);
}


/*****************************************************************************/
/* i2c-adapter helper functions                                              */
#include <linux/i2c-id.h>

/* exported algorithm data */
static struct i2c_algorithm saa7146_algo = {
	.master_xfer	= saa7146_i2c_xfer,
	.functionality	= saa7146_i2c_func,
};

int saa7146_i2c_adapter_prepare(struct saa7146_dev *dev, struct i2c_adapter *i2c_adapter, u32 bitrate)
{
	DEB_EE(("bitrate: 0x%08x\n",bitrate));

	/* enable i2c-port pins */
	saa7146_write(dev, MC1, (MASK_08 | MASK_24));

	dev->i2c_bitrate = bitrate;
	saa7146_i2c_reset(dev);

	if (i2c_adapter) {
		i2c_set_adapdata(i2c_adapter, &dev->v4l2_dev);
		i2c_adapter->dev.parent    = &dev->pci->dev;
		i2c_adapter->algo	   = &saa7146_algo;
		i2c_adapter->algo_data     = NULL;
		i2c_adapter->id		   = I2C_HW_SAA7146;
		i2c_adapter->timeout = SAA7146_I2C_TIMEOUT;
		i2c_adapter->retries = SAA7146_I2C_RETRIES;
	}

	return 0;
}
