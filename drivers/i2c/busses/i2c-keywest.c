/*
    i2c Support for Apple Keywest I2C Bus Controller

    Copyright (c) 2001 Benjamin Herrenschmidt <benh@kernel.crashing.org>

    Original work by
    
    Copyright (c) 2000 Philip Edelbrock <phil@stimpy.netroedge.com>

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Changes:

    2001/12/13 BenH	New implementation
    2001/12/15 BenH	Add support for "byte" and "quick"
                        transfers. Add i2c_xfer routine.
    2003/09/21 BenH	Rework state machine with Paulus help
    2004/01/21 BenH	Merge in Greg KH changes, polled mode is back
    2004/02/05 BenH	Merge 64 bits fixes from the g5 ppc64 tree

    My understanding of the various modes supported by keywest are:

     - Dumb mode : not implemented, probably direct tweaking of lines
     - Standard mode : simple i2c transaction of type
         S Addr R/W A Data A Data ... T
     - Standard sub mode : combined 8 bit subaddr write with data read
         S Addr R/W A SubAddr A Data A Data ... T
     - Combined mode : Subaddress and Data sequences appended with no stop
         S Addr R/W A SubAddr S Addr R/W A Data A Data ... T

    Currently, this driver uses only Standard mode for i2c xfer, and
    smbus byte & quick transfers ; and uses StandardSub mode for
    other smbus transfers instead of combined as we need that for the
    sound driver to be happy
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/pmac_low_i2c.h>

#include "i2c-keywest.h"

#undef POLLED_MODE

/* Some debug macros */
#define WRONG_STATE(name) do {\
		pr_debug("KW: wrong state. Got %s, state: %s (isr: %02x)\n", \
			 name, __kw_state_names[iface->state], isr);	\
	} while(0)

#ifdef DEBUG
static const char *__kw_state_names[] = {
	"state_idle",
	"state_addr",
	"state_read",
	"state_write",
	"state_stop",
	"state_dead"
};
#endif /* DEBUG */

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("I2C driver for Apple's Keywest");
MODULE_LICENSE("GPL");

#ifdef POLLED_MODE
/* Don't schedule, the g5 fan controller is too
 * timing sensitive
 */
static u8
wait_interrupt(struct keywest_iface* iface)
{
	int i;
	u8 isr;
	
	for (i = 0; i < 200000; i++) {
		isr = read_reg(reg_isr) & KW_I2C_IRQ_MASK;
		if (isr != 0)
			return isr;
		udelay(10);
	}
	return isr;
}
#endif /* POLLED_MODE */

static void
do_stop(struct keywest_iface* iface, int result)
{
	write_reg(reg_control, KW_I2C_CTL_STOP);
	iface->state = state_stop;
	iface->result = result;
}

/* Main state machine for standard & standard sub mode */
static void
handle_interrupt(struct keywest_iface *iface, u8 isr)
{
	int ack;
	
	if (isr == 0) {
		if (iface->state != state_stop) {
			pr_debug("KW: Timeout !\n");
			do_stop(iface, -EIO);
		}
		if (iface->state == state_stop) {
			ack = read_reg(reg_status);
			if (!(ack & KW_I2C_STAT_BUSY)) {
				iface->state = state_idle;
				write_reg(reg_ier, 0x00);
#ifndef POLLED_MODE
				complete(&iface->complete);
#endif /* POLLED_MODE */
			}
		}
		return;
	}

	if (isr & KW_I2C_IRQ_ADDR) {
		ack = read_reg(reg_status);
		if (iface->state != state_addr) {
			write_reg(reg_isr, KW_I2C_IRQ_ADDR);
			WRONG_STATE("KW_I2C_IRQ_ADDR"); 
			do_stop(iface, -EIO);
			return;
		}
		if ((ack & KW_I2C_STAT_LAST_AAK) == 0) {
			iface->state = state_stop;		     
			iface->result = -ENODEV;
			pr_debug("KW: NAK on address\n");
		} else {
			/* Handle rw "quick" mode */
			if (iface->datalen == 0) {
				do_stop(iface, 0);
			} else if (iface->read_write == I2C_SMBUS_READ) {
				iface->state = state_read;
				if (iface->datalen > 1)
					write_reg(reg_control, KW_I2C_CTL_AAK);
			} else {
				iface->state = state_write;
				write_reg(reg_data, *(iface->data++));
				iface->datalen--;
			}
		}
		write_reg(reg_isr, KW_I2C_IRQ_ADDR);
	}

	if (isr & KW_I2C_IRQ_DATA) {
		if (iface->state == state_read) {
			*(iface->data++) = read_reg(reg_data);
			write_reg(reg_isr, KW_I2C_IRQ_DATA);
			iface->datalen--;
			if (iface->datalen == 0)
				iface->state = state_stop;
			else if (iface->datalen == 1)
				write_reg(reg_control, 0);
		} else if (iface->state == state_write) {
			/* Check ack status */
			ack = read_reg(reg_status);
			if ((ack & KW_I2C_STAT_LAST_AAK) == 0) {
				pr_debug("KW: nack on data write (%x): %x\n",
				    iface->data[-1], ack);
				do_stop(iface, -EIO);
			} else if (iface->datalen) {
				write_reg(reg_data, *(iface->data++));
				iface->datalen--;
			} else {
				write_reg(reg_control, KW_I2C_CTL_STOP);
				iface->state = state_stop;
				iface->result = 0;
			}
			write_reg(reg_isr, KW_I2C_IRQ_DATA);
		} else {
			write_reg(reg_isr, KW_I2C_IRQ_DATA);
			WRONG_STATE("KW_I2C_IRQ_DATA"); 
			if (iface->state != state_stop)
				do_stop(iface, -EIO);
		}
	}

	if (isr & KW_I2C_IRQ_STOP) {
		write_reg(reg_isr, KW_I2C_IRQ_STOP);
		if (iface->state != state_stop) {
			WRONG_STATE("KW_I2C_IRQ_STOP");
			iface->result = -EIO;
		}
		iface->state = state_idle;
		write_reg(reg_ier, 0x00);
#ifndef POLLED_MODE
		complete(&iface->complete);
#endif /* POLLED_MODE */			
	}

	if (isr & KW_I2C_IRQ_START)
		write_reg(reg_isr, KW_I2C_IRQ_START);
}

#ifndef POLLED_MODE

/* Interrupt handler */
static irqreturn_t
keywest_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct keywest_iface *iface = (struct keywest_iface *)dev_id;
	unsigned long flags;

	spin_lock_irqsave(&iface->lock, flags);
	del_timer(&iface->timeout_timer);
	handle_interrupt(iface, read_reg(reg_isr));
	if (iface->state != state_idle) {
		iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
		add_timer(&iface->timeout_timer);
	}
	spin_unlock_irqrestore(&iface->lock, flags);
	return IRQ_HANDLED;
}

static void
keywest_timeout(unsigned long data)
{
	struct keywest_iface *iface = (struct keywest_iface *)data;
	unsigned long flags;

	pr_debug("timeout !\n");
	spin_lock_irqsave(&iface->lock, flags);
	handle_interrupt(iface, read_reg(reg_isr));
	if (iface->state != state_idle) {
		iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
		add_timer(&iface->timeout_timer);
	}
	spin_unlock_irqrestore(&iface->lock, flags);
}

#endif /* POLLED_MODE */

/*
 * SMBUS-type transfer entrypoint
 */
static s32
keywest_smbus_xfer(	struct i2c_adapter*	adap,
			u16			addr,
			unsigned short		flags,
			char			read_write,
			u8			command,
			int			size,
			union i2c_smbus_data*	data)
{
	struct keywest_chan* chan = i2c_get_adapdata(adap);
	struct keywest_iface* iface = chan->iface;
	int len;
	u8* buffer;
	u16 cur_word;
	int rc = 0;

	if (iface->state == state_dead)
		return -ENXIO;
		
	/* Prepare datas & select mode */
	iface->cur_mode &= ~KW_I2C_MODE_MODE_MASK;
	switch (size) {
        case I2C_SMBUS_QUICK:
	    	len = 0;
	    	buffer = NULL;
	    	iface->cur_mode |= KW_I2C_MODE_STANDARD;
	    	break;
        case I2C_SMBUS_BYTE:
	    	len = 1;
	    	buffer = &data->byte;
	    	iface->cur_mode |= KW_I2C_MODE_STANDARD;
	    	break;
        case I2C_SMBUS_BYTE_DATA:
	    	len = 1;
	    	buffer = &data->byte;
	    	iface->cur_mode |= KW_I2C_MODE_STANDARDSUB;
	    	break;
        case I2C_SMBUS_WORD_DATA:
	    	len = 2;
	    	cur_word = cpu_to_le16(data->word);
	    	buffer = (u8 *)&cur_word;
	    	iface->cur_mode |= KW_I2C_MODE_STANDARDSUB;
		break;
        case I2C_SMBUS_BLOCK_DATA:
	    	len = data->block[0];
	    	buffer = &data->block[1];
	    	iface->cur_mode |= KW_I2C_MODE_STANDARDSUB;
		break;
        default:
	    	return -1;
	}

	/* Turn a standardsub read into a combined mode access */
 	if (read_write == I2C_SMBUS_READ
 	    && (iface->cur_mode & KW_I2C_MODE_MODE_MASK) == KW_I2C_MODE_STANDARDSUB) {
 		iface->cur_mode &= ~KW_I2C_MODE_MODE_MASK;
 		iface->cur_mode |= KW_I2C_MODE_COMBINED;
 	}

	/* Original driver had this limitation */
	if (len > 32)
		len = 32;

	if (pmac_low_i2c_lock(iface->node))
		return -ENXIO;

	pr_debug("chan: %d, addr: 0x%x, transfer len: %d, read: %d\n",
		chan->chan_no, addr, len, read_write == I2C_SMBUS_READ);

	iface->data = buffer;
	iface->datalen = len;
	iface->state = state_addr;
	iface->result = 0;
	iface->read_write = read_write;
	
	/* Setup channel & clear pending irqs */
	write_reg(reg_isr, read_reg(reg_isr));
	write_reg(reg_mode, iface->cur_mode | (chan->chan_no << 4));
	write_reg(reg_status, 0);

	/* Set up address and r/w bit */
	write_reg(reg_addr,
		(addr << 1) | ((read_write == I2C_SMBUS_READ) ? 0x01 : 0x00));

	/* Set up the sub address */
	if ((iface->cur_mode & KW_I2C_MODE_MODE_MASK) == KW_I2C_MODE_STANDARDSUB
	    || (iface->cur_mode & KW_I2C_MODE_MODE_MASK) == KW_I2C_MODE_COMBINED)
		write_reg(reg_subaddr, command);

#ifndef POLLED_MODE
	/* Arm timeout */
	iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
	add_timer(&iface->timeout_timer);
#endif

	/* Start sending address & enable interrupt*/
	write_reg(reg_control, KW_I2C_CTL_XADDR);
	write_reg(reg_ier, KW_I2C_IRQ_MASK);

#ifdef POLLED_MODE
	pr_debug("using polled mode...\n");
	/* State machine, to turn into an interrupt handler */
	while(iface->state != state_idle) {
		unsigned long flags;

		u8 isr = wait_interrupt(iface);
		spin_lock_irqsave(&iface->lock, flags);
		handle_interrupt(iface, isr);
		spin_unlock_irqrestore(&iface->lock, flags);
	}
#else /* POLLED_MODE */
	pr_debug("using interrupt mode...\n");
	wait_for_completion(&iface->complete);	
#endif /* POLLED_MODE */	

	rc = iface->result;	
	pr_debug("transfer done, result: %d\n", rc);

	if (rc == 0 && size == I2C_SMBUS_WORD_DATA && read_write == I2C_SMBUS_READ)
	    	data->word = le16_to_cpu(cur_word);
	
	/* Release sem */
	pmac_low_i2c_unlock(iface->node);
	
	return rc;
}

/*
 * Generic i2c master transfer entrypoint
 */
static int
keywest_xfer(	struct i2c_adapter *adap,
		struct i2c_msg *msgs, 
		int num)
{
	struct keywest_chan* chan = i2c_get_adapdata(adap);
	struct keywest_iface* iface = chan->iface;
	struct i2c_msg *pmsg;
	int i, completed;
	int rc = 0;

	if (iface->state == state_dead)
		return -ENXIO;

	if (pmac_low_i2c_lock(iface->node))
		return -ENXIO;

	/* Set adapter to standard mode */
	iface->cur_mode &= ~KW_I2C_MODE_MODE_MASK;
	iface->cur_mode |= KW_I2C_MODE_STANDARD;

	completed = 0;
	for (i = 0; rc >= 0 && i < num;) {
		u8 addr;
		
		pmsg = &msgs[i++];
		addr = pmsg->addr;
		if (pmsg->flags & I2C_M_TEN) {
			printk(KERN_ERR "i2c-keywest: 10 bits addr not supported !\n");
			rc = -EINVAL;
			break;
		}
		pr_debug("xfer: chan: %d, doing %s %d bytes to 0x%02x - %d of %d messages\n",
		     chan->chan_no,
		     pmsg->flags & I2C_M_RD ? "read" : "write",
                     pmsg->len, addr, i, num);
    
		/* Setup channel & clear pending irqs */
		write_reg(reg_mode, iface->cur_mode | (chan->chan_no << 4));
		write_reg(reg_isr, read_reg(reg_isr));
		write_reg(reg_status, 0);
		
		iface->data = pmsg->buf;
		iface->datalen = pmsg->len;
		iface->state = state_addr;
		iface->result = 0;
		if (pmsg->flags & I2C_M_RD)
			iface->read_write = I2C_SMBUS_READ;
		else
			iface->read_write = I2C_SMBUS_WRITE;

		/* Set up address and r/w bit */
		if (pmsg->flags & I2C_M_REV_DIR_ADDR)
			addr ^= 1;		
		write_reg(reg_addr,
			(addr << 1) |
			((iface->read_write == I2C_SMBUS_READ) ? 0x01 : 0x00));

#ifndef POLLED_MODE
		/* Arm timeout */
		iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
		add_timer(&iface->timeout_timer);
#endif

		/* Start sending address & enable interrupt*/
		write_reg(reg_ier, KW_I2C_IRQ_MASK);
		write_reg(reg_control, KW_I2C_CTL_XADDR);

#ifdef POLLED_MODE
		pr_debug("using polled mode...\n");
		/* State machine, to turn into an interrupt handler */
		while(iface->state != state_idle) {
			u8 isr = wait_interrupt(iface);
			handle_interrupt(iface, isr);
		}
#else /* POLLED_MODE */
		pr_debug("using interrupt mode...\n");
		wait_for_completion(&iface->complete);	
#endif /* POLLED_MODE */	

		rc = iface->result;
		if (rc == 0)
			completed++;
		pr_debug("transfer done, result: %d\n", rc);
	}

	/* Release sem */
	pmac_low_i2c_unlock(iface->node);

	return completed;
}

static u32
keywest_func(struct i2c_adapter * adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA;
}

/* For now, we only handle combined mode (smbus) */
static struct i2c_algorithm keywest_algorithm = {
	.smbus_xfer	= keywest_smbus_xfer,
	.master_xfer	= keywest_xfer,
	.functionality	= keywest_func,
};


static int
create_iface(struct device_node *np, struct device *dev)
{
	unsigned long steps;
	unsigned bsteps, tsize, i, nchan, addroffset;
	struct keywest_iface* iface;
	u32 *psteps, *prate;
	int rc;

	if (np->n_intrs < 1 || np->n_addrs < 1) {
		printk(KERN_ERR "%s: Missing interrupt or address !\n",
		       np->full_name);
		return -ENODEV;
	}
	if (pmac_low_i2c_lock(np))
		return -ENODEV;

	psteps = (u32 *)get_property(np, "AAPL,address-step", NULL);
	steps = psteps ? (*psteps) : 0x10;

	/* Hrm... maybe we can be smarter here */
	for (bsteps = 0; (steps & 0x01) == 0; bsteps++)
		steps >>= 1;

	if (np->parent->name[0] == 'u') {
		nchan = 2;
		addroffset = 3;
	} else {
		addroffset = 0;
		nchan = 1;
	}

	tsize = sizeof(struct keywest_iface) +
		(sizeof(struct keywest_chan) + 4) * nchan;
	iface = kzalloc(tsize, GFP_KERNEL);
	if (iface == NULL) {
		printk(KERN_ERR "i2c-keywest: can't allocate inteface !\n");
		pmac_low_i2c_unlock(np);
		return -ENOMEM;
	}
	spin_lock_init(&iface->lock);
	init_completion(&iface->complete);
	iface->node = of_node_get(np);
	iface->bsteps = bsteps;
	iface->chan_count = nchan;
	iface->state = state_idle;
	iface->irq = np->intrs[0].line;
	iface->channels = (struct keywest_chan *)
		(((unsigned long)(iface + 1) + 3UL) & ~3UL);
	iface->base = ioremap(np->addrs[0].address + addroffset,
						np->addrs[0].size);
	if (!iface->base) {
		printk(KERN_ERR "i2c-keywest: can't map inteface !\n");
		kfree(iface);
		pmac_low_i2c_unlock(np);
		return -ENOMEM;
	}

#ifndef POLLED_MODE
	init_timer(&iface->timeout_timer);
	iface->timeout_timer.function = keywest_timeout;
	iface->timeout_timer.data = (unsigned long)iface;
#endif

	/* Select interface rate */
	iface->cur_mode = KW_I2C_MODE_100KHZ;
	prate = (u32 *)get_property(np, "AAPL,i2c-rate", NULL);
	if (prate) switch(*prate) {
	case 100:
		iface->cur_mode = KW_I2C_MODE_100KHZ;
		break;
	case 50:
		iface->cur_mode = KW_I2C_MODE_50KHZ;
		break;
	case 25:
		iface->cur_mode = KW_I2C_MODE_25KHZ;
		break;
	default:
		printk(KERN_WARNING "i2c-keywest: unknown rate %ldKhz, using 100KHz\n",
		       (long)*prate);
	}
	
	/* Select standard mode by default */
	iface->cur_mode |= KW_I2C_MODE_STANDARD;
	
	/* Write mode */
	write_reg(reg_mode, iface->cur_mode);
	
	/* Switch interrupts off & clear them*/
	write_reg(reg_ier, 0x00);
	write_reg(reg_isr, KW_I2C_IRQ_MASK);

#ifndef POLLED_MODE
	/* Request chip interrupt */	
	rc = request_irq(iface->irq, keywest_irq, SA_INTERRUPT, "keywest i2c", iface);
	if (rc) {
		printk(KERN_ERR "i2c-keywest: can't get IRQ %d !\n", iface->irq);
		iounmap(iface->base);
		kfree(iface);
		pmac_low_i2c_unlock(np);
		return -ENODEV;
	}
#endif /* POLLED_MODE */

	pmac_low_i2c_unlock(np);
	dev_set_drvdata(dev, iface);
	
	for (i=0; i<nchan; i++) {
		struct keywest_chan* chan = &iface->channels[i];
		
		sprintf(chan->adapter.name, "%s %d", np->parent->name, i);
		chan->iface = iface;
		chan->chan_no = i;
		chan->adapter.algo = &keywest_algorithm;
		chan->adapter.algo_data = NULL;
		chan->adapter.client_register = NULL;
		chan->adapter.client_unregister = NULL;
		i2c_set_adapdata(&chan->adapter, chan);
		chan->adapter.dev.parent = dev;

		rc = i2c_add_adapter(&chan->adapter);
		if (rc) {
			printk("i2c-keywest.c: Adapter %s registration failed\n",
				chan->adapter.name);
			i2c_set_adapdata(&chan->adapter, NULL);
		}
	}

	printk(KERN_INFO "Found KeyWest i2c on \"%s\", %d channel%s, stepping: %d bits\n",
		np->parent->name, nchan, nchan > 1 ? "s" : "", bsteps);
		
	return 0;
}

static int
dispose_iface(struct device *dev)
{
	struct keywest_iface *iface = dev_get_drvdata(dev);
	int i, rc;
	
	/* Make sure we stop all activity */
	if (pmac_low_i2c_lock(iface->node))
		return -ENODEV;

#ifndef POLLED_MODE
	spin_lock_irq(&iface->lock);
	while (iface->state != state_idle) {
		spin_unlock_irq(&iface->lock);
		msleep(100);
		spin_lock_irq(&iface->lock);
	}
#endif /* POLLED_MODE */
	iface->state = state_dead;
#ifndef POLLED_MODE
	spin_unlock_irq(&iface->lock);
	free_irq(iface->irq, iface);
#endif /* POLLED_MODE */

	pmac_low_i2c_unlock(iface->node);

	/* Release all channels */
	for (i=0; i<iface->chan_count; i++) {
		struct keywest_chan* chan = &iface->channels[i];
		if (i2c_get_adapdata(&chan->adapter) == NULL)
			continue;
		rc = i2c_del_adapter(&chan->adapter);
		i2c_set_adapdata(&chan->adapter, NULL);
		/* We aren't that prepared to deal with this... */
		if (rc)
			printk("i2c-keywest.c: i2c_del_adapter failed, that's bad !\n");
	}
	iounmap(iface->base);
	dev_set_drvdata(dev, NULL);
	of_node_put(iface->node);
	kfree(iface);

	return 0;
}

static int
create_iface_macio(struct macio_dev* dev, const struct of_device_id *match)
{
	return create_iface(dev->ofdev.node, &dev->ofdev.dev);
}

static int
dispose_iface_macio(struct macio_dev* dev)
{
	return dispose_iface(&dev->ofdev.dev);
}

static int
create_iface_of_platform(struct of_device* dev, const struct of_device_id *match)
{
	return create_iface(dev->node, &dev->dev);
}

static int
dispose_iface_of_platform(struct of_device* dev)
{
	return dispose_iface(&dev->dev);
}

static struct of_device_id i2c_keywest_match[] = 
{
	{
	.type		= "i2c",
	.compatible	= "keywest"
	},
	{},
};

static struct macio_driver i2c_keywest_macio_driver = 
{
	.owner		= THIS_MODULE,
	.name 		= "i2c-keywest",
	.match_table	= i2c_keywest_match,
	.probe		= create_iface_macio,
	.remove		= dispose_iface_macio
};

static struct of_platform_driver i2c_keywest_of_platform_driver = 
{
	.owner		= THIS_MODULE,
	.name 		= "i2c-keywest",
	.match_table	= i2c_keywest_match,
	.probe		= create_iface_of_platform,
	.remove		= dispose_iface_of_platform
};

static int __init
i2c_keywest_init(void)
{
	of_register_driver(&i2c_keywest_of_platform_driver);
	macio_register_driver(&i2c_keywest_macio_driver);

	return 0;
}

static void __exit
i2c_keywest_cleanup(void)
{
	of_unregister_driver(&i2c_keywest_of_platform_driver);
	macio_unregister_driver(&i2c_keywest_macio_driver);
}

module_init(i2c_keywest_init);
module_exit(i2c_keywest_cleanup);
