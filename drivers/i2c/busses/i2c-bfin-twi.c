/*
 * drivers/i2c/busses/i2c-bfin-twi.c
 *
 * Description: Driver for Blackfin Two Wire Interface
 *
 * Author:      sonicz  <sonic.zhang@analog.com>
 *
 * Copyright (c) 2005-2007 Analog Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/blackfin.h>
#include <asm/irq.h>

#define POLL_TIMEOUT       (2 * HZ)

/* SMBus mode*/
#define TWI_I2C_MODE_STANDARD		0x01
#define TWI_I2C_MODE_STANDARDSUB	0x02
#define TWI_I2C_MODE_COMBINED		0x04

struct bfin_twi_iface {
	struct mutex		twi_lock;
	int			irq;
	spinlock_t		lock;
	char			read_write;
	u8			command;
	u8			*transPtr;
	int			readNum;
	int			writeNum;
	int			cur_mode;
	int			manual_stop;
	int			result;
	int			timeout_count;
	struct timer_list	timeout_timer;
	struct i2c_adapter	adap;
	struct completion	complete;
};

static struct bfin_twi_iface twi_iface;

static void bfin_twi_handle_interrupt(struct bfin_twi_iface *iface)
{
	unsigned short twi_int_status = bfin_read_TWI_INT_STAT();
	unsigned short mast_stat = bfin_read_TWI_MASTER_STAT();

	if (twi_int_status & XMTSERV) {
		/* Transmit next data */
		if (iface->writeNum > 0) {
			bfin_write_TWI_XMT_DATA8(*(iface->transPtr++));
			iface->writeNum--;
		}
		/* start receive immediately after complete sending in
		 * combine mode.
		 */
		else if (iface->cur_mode == TWI_I2C_MODE_COMBINED) {
			bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL()
				| MDIR | RSTART);
		} else if (iface->manual_stop)
			bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL()
				| STOP);
		SSYNC();
		/* Clear status */
		bfin_write_TWI_INT_STAT(XMTSERV);
		SSYNC();
	}
	if (twi_int_status & RCVSERV) {
		if (iface->readNum > 0) {
			/* Receive next data */
			*(iface->transPtr) = bfin_read_TWI_RCV_DATA8();
			if (iface->cur_mode == TWI_I2C_MODE_COMBINED) {
				/* Change combine mode into sub mode after
				 * read first data.
				 */
				iface->cur_mode = TWI_I2C_MODE_STANDARDSUB;
				/* Get read number from first byte in block
				 * combine mode.
				 */
				if (iface->readNum == 1 && iface->manual_stop)
					iface->readNum = *iface->transPtr + 1;
			}
			iface->transPtr++;
			iface->readNum--;
		} else if (iface->manual_stop) {
			bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL()
				| STOP);
			SSYNC();
		}
		/* Clear interrupt source */
		bfin_write_TWI_INT_STAT(RCVSERV);
		SSYNC();
	}
	if (twi_int_status & MERR) {
		bfin_write_TWI_INT_STAT(MERR);
		bfin_write_TWI_INT_MASK(0);
		bfin_write_TWI_MASTER_STAT(0x3e);
		bfin_write_TWI_MASTER_CTL(0);
		SSYNC();
		iface->result = -1;
		/* if both err and complete int stats are set, return proper
		 * results.
		 */
		if (twi_int_status & MCOMP) {
			bfin_write_TWI_INT_STAT(MCOMP);
			bfin_write_TWI_INT_MASK(0);
			bfin_write_TWI_MASTER_CTL(0);
			SSYNC();
			/* If it is a quick transfer, only address bug no data,
			 * not an err, return 1.
			 */
			if (iface->writeNum == 0 && (mast_stat & BUFRDERR))
				iface->result = 1;
			/* If address not acknowledged return -1,
			 * else return 0.
			 */
			else if (!(mast_stat & ANAK))
				iface->result = 0;
		}
		complete(&iface->complete);
		return;
	}
	if (twi_int_status & MCOMP) {
		bfin_write_TWI_INT_STAT(MCOMP);
		SSYNC();
		if (iface->cur_mode == TWI_I2C_MODE_COMBINED) {
			if (iface->readNum == 0) {
				/* set the read number to 1 and ask for manual
				 * stop in block combine mode
				 */
				iface->readNum = 1;
				iface->manual_stop = 1;
				bfin_write_TWI_MASTER_CTL(
					bfin_read_TWI_MASTER_CTL()
					| (0xff << 6));
			} else {
				/* set the readd number in other
				 * combine mode.
				 */
				bfin_write_TWI_MASTER_CTL(
					(bfin_read_TWI_MASTER_CTL() &
					(~(0xff << 6))) |
					( iface->readNum << 6));
			}
			/* remove restart bit and enable master receive */
			bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL() &
				~RSTART);
			bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL() |
				MEN | MDIR);
			SSYNC();
		} else {
			iface->result = 1;
			bfin_write_TWI_INT_MASK(0);
			bfin_write_TWI_MASTER_CTL(0);
			SSYNC();
			complete(&iface->complete);
		}
	}
}

/* Interrupt handler */
static irqreturn_t bfin_twi_interrupt_entry(int irq, void *dev_id)
{
	struct bfin_twi_iface *iface = dev_id;
	unsigned long flags;

	spin_lock_irqsave(&iface->lock, flags);
	del_timer(&iface->timeout_timer);
	bfin_twi_handle_interrupt(iface);
	spin_unlock_irqrestore(&iface->lock, flags);
	return IRQ_HANDLED;
}

static void bfin_twi_timeout(unsigned long data)
{
	struct bfin_twi_iface *iface = (struct bfin_twi_iface *)data;
	unsigned long flags;

	spin_lock_irqsave(&iface->lock, flags);
	bfin_twi_handle_interrupt(iface);
	if (iface->result == 0) {
		iface->timeout_count--;
		if (iface->timeout_count > 0) {
			iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
			add_timer(&iface->timeout_timer);
		} else {
			iface->result = -1;
			complete(&iface->complete);
		}
	}
	spin_unlock_irqrestore(&iface->lock, flags);
}

/*
 * Generic i2c master transfer entrypoint
 */
static int bfin_twi_master_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msgs, int num)
{
	struct bfin_twi_iface *iface = adap->algo_data;
	struct i2c_msg *pmsg;
	int i, ret;
	int rc = 0;

	if (!(bfin_read_TWI_CONTROL() & TWI_ENA))
		return -ENXIO;

	mutex_lock(&iface->twi_lock);

	while (bfin_read_TWI_MASTER_STAT() & BUSBUSY) {
		mutex_unlock(&iface->twi_lock);
		yield();
		mutex_lock(&iface->twi_lock);
	}

	ret = 0;
	for (i = 0; rc >= 0 && i < num; i++) {
		pmsg = &msgs[i];
		if (pmsg->flags & I2C_M_TEN) {
			dev_err(&(adap->dev), "i2c-bfin-twi: 10 bits addr "
				"not supported !\n");
			rc = -EINVAL;
			break;
		}

		iface->cur_mode = TWI_I2C_MODE_STANDARD;
		iface->manual_stop = 0;
		iface->transPtr = pmsg->buf;
		iface->writeNum = iface->readNum = pmsg->len;
		iface->result = 0;
		iface->timeout_count = 10;
		/* Set Transmit device address */
		bfin_write_TWI_MASTER_ADDR(pmsg->addr);

		/* FIFO Initiation. Data in FIFO should be
		 *  discarded before start a new operation.
		 */
		bfin_write_TWI_FIFO_CTL(0x3);
		SSYNC();
		bfin_write_TWI_FIFO_CTL(0);
		SSYNC();

		if (pmsg->flags & I2C_M_RD)
			iface->read_write = I2C_SMBUS_READ;
		else {
			iface->read_write = I2C_SMBUS_WRITE;
			/* Transmit first data */
			if (iface->writeNum > 0) {
				bfin_write_TWI_XMT_DATA8(*(iface->transPtr++));
				iface->writeNum--;
				SSYNC();
			}
		}

		/* clear int stat */
		bfin_write_TWI_INT_STAT(MERR|MCOMP|XMTSERV|RCVSERV);

		/* Interrupt mask . Enable XMT, RCV interrupt */
		bfin_write_TWI_INT_MASK(MCOMP | MERR |
			((iface->read_write == I2C_SMBUS_READ)?
			RCVSERV : XMTSERV));
		SSYNC();

		if (pmsg->len > 0 && pmsg->len <= 255)
			bfin_write_TWI_MASTER_CTL(pmsg->len << 6);
		else if (pmsg->len > 255) {
			bfin_write_TWI_MASTER_CTL(0xff << 6);
			iface->manual_stop = 1;
		} else
			break;

		iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
		add_timer(&iface->timeout_timer);

		/* Master enable */
		bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL() | MEN |
			((iface->read_write == I2C_SMBUS_READ) ? MDIR : 0) |
			((CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ>100) ? FAST : 0));
		SSYNC();

		wait_for_completion(&iface->complete);

		rc = iface->result;
		if (rc == 1)
			ret++;
		else if (rc == -1)
			break;
	}

	/* Release mutex */
	mutex_unlock(&iface->twi_lock);

	return ret;
}

/*
 * SMBus type transfer entrypoint
 */

int bfin_twi_smbus_xfer(struct i2c_adapter *adap, u16 addr,
			unsigned short flags, char read_write,
			u8 command, int size, union i2c_smbus_data *data)
{
	struct bfin_twi_iface *iface = adap->algo_data;
	int rc = 0;

	if (!(bfin_read_TWI_CONTROL() & TWI_ENA))
		return -ENXIO;

	mutex_lock(&iface->twi_lock);

	while (bfin_read_TWI_MASTER_STAT() & BUSBUSY) {
		mutex_unlock(&iface->twi_lock);
		yield();
		mutex_lock(&iface->twi_lock);
	}

	iface->writeNum = 0;
	iface->readNum = 0;

	/* Prepare datas & select mode */
	switch (size) {
	case I2C_SMBUS_QUICK:
		iface->transPtr = NULL;
		iface->cur_mode = TWI_I2C_MODE_STANDARD;
		break;
	case I2C_SMBUS_BYTE:
		if (data == NULL)
			iface->transPtr = NULL;
		else {
			if (read_write == I2C_SMBUS_READ)
				iface->readNum = 1;
			else
				iface->writeNum = 1;
			iface->transPtr = &data->byte;
		}
		iface->cur_mode = TWI_I2C_MODE_STANDARD;
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ) {
			iface->readNum = 1;
			iface->cur_mode = TWI_I2C_MODE_COMBINED;
		} else {
			iface->writeNum = 1;
			iface->cur_mode = TWI_I2C_MODE_STANDARDSUB;
		}
		iface->transPtr = &data->byte;
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ) {
			iface->readNum = 2;
			iface->cur_mode = TWI_I2C_MODE_COMBINED;
		} else {
			iface->writeNum = 2;
			iface->cur_mode = TWI_I2C_MODE_STANDARDSUB;
		}
		iface->transPtr = (u8 *)&data->word;
		break;
	case I2C_SMBUS_PROC_CALL:
		iface->writeNum = 2;
		iface->readNum = 2;
		iface->cur_mode = TWI_I2C_MODE_COMBINED;
		iface->transPtr = (u8 *)&data->word;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			iface->readNum = 0;
			iface->cur_mode = TWI_I2C_MODE_COMBINED;
		} else {
			iface->writeNum = data->block[0] + 1;
			iface->cur_mode = TWI_I2C_MODE_STANDARDSUB;
		}
		iface->transPtr = data->block;
		break;
	default:
		return -1;
	}

	iface->result = 0;
	iface->manual_stop = 0;
	iface->read_write = read_write;
	iface->command = command;
	iface->timeout_count = 10;

	/* FIFO Initiation. Data in FIFO should be discarded before
	 * start a new operation.
	 */
	bfin_write_TWI_FIFO_CTL(0x3);
	SSYNC();
	bfin_write_TWI_FIFO_CTL(0);

	/* clear int stat */
	bfin_write_TWI_INT_STAT(MERR|MCOMP|XMTSERV|RCVSERV);

	/* Set Transmit device address */
	bfin_write_TWI_MASTER_ADDR(addr);
	SSYNC();

	iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
	add_timer(&iface->timeout_timer);

	switch (iface->cur_mode) {
	case TWI_I2C_MODE_STANDARDSUB:
		bfin_write_TWI_XMT_DATA8(iface->command);
		bfin_write_TWI_INT_MASK(MCOMP | MERR |
			((iface->read_write == I2C_SMBUS_READ) ?
			RCVSERV : XMTSERV));
		SSYNC();

		if (iface->writeNum + 1 <= 255)
			bfin_write_TWI_MASTER_CTL((iface->writeNum + 1) << 6);
		else {
			bfin_write_TWI_MASTER_CTL(0xff << 6);
			iface->manual_stop = 1;
		}
		/* Master enable */
		bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL() | MEN |
			((CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ>100) ? FAST : 0));
		break;
	case TWI_I2C_MODE_COMBINED:
		bfin_write_TWI_XMT_DATA8(iface->command);
		bfin_write_TWI_INT_MASK(MCOMP | MERR | RCVSERV | XMTSERV);
		SSYNC();

		if (iface->writeNum > 0)
			bfin_write_TWI_MASTER_CTL((iface->writeNum + 1) << 6);
		else
			bfin_write_TWI_MASTER_CTL(0x1 << 6);
		/* Master enable */
		bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL() | MEN |
			((CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ>100) ? FAST : 0));
		break;
	default:
		bfin_write_TWI_MASTER_CTL(0);
		if (size != I2C_SMBUS_QUICK) {
			/* Don't access xmit data register when this is a
			 * read operation.
			 */
			if (iface->read_write != I2C_SMBUS_READ) {
				if (iface->writeNum > 0) {
					bfin_write_TWI_XMT_DATA8(*(iface->transPtr++));
					if (iface->writeNum <= 255)
						bfin_write_TWI_MASTER_CTL(iface->writeNum << 6);
					else {
						bfin_write_TWI_MASTER_CTL(0xff << 6);
						iface->manual_stop = 1;
					}
					iface->writeNum--;
				} else {
					bfin_write_TWI_XMT_DATA8(iface->command);
					bfin_write_TWI_MASTER_CTL(1 << 6);
				}
			} else {
				if (iface->readNum > 0 && iface->readNum <= 255)
					bfin_write_TWI_MASTER_CTL(iface->readNum << 6);
				else if (iface->readNum > 255) {
					bfin_write_TWI_MASTER_CTL(0xff << 6);
					iface->manual_stop = 1;
				} else {
					del_timer(&iface->timeout_timer);
					break;
				}
			}
		}
		bfin_write_TWI_INT_MASK(MCOMP | MERR |
			((iface->read_write == I2C_SMBUS_READ) ?
			RCVSERV : XMTSERV));
		SSYNC();

		/* Master enable */
		bfin_write_TWI_MASTER_CTL(bfin_read_TWI_MASTER_CTL() | MEN |
			((iface->read_write == I2C_SMBUS_READ) ? MDIR : 0) |
			((CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ > 100) ? FAST : 0));
		break;
	}
	SSYNC();

	wait_for_completion(&iface->complete);

	rc = (iface->result >= 0) ? 0 : -1;

	/* Release mutex */
	mutex_unlock(&iface->twi_lock);

	return rc;
}

/*
 * Return what the adapter supports
 */
static u32 bfin_twi_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_PROC_CALL |
	       I2C_FUNC_I2C;
}


static struct i2c_algorithm bfin_twi_algorithm = {
	.master_xfer   = bfin_twi_master_xfer,
	.smbus_xfer    = bfin_twi_smbus_xfer,
	.functionality = bfin_twi_functionality,
};


static int i2c_bfin_twi_suspend(struct platform_device *dev, pm_message_t state)
{
/*	struct bfin_twi_iface *iface = platform_get_drvdata(dev);*/

	/* Disable TWI */
	bfin_write_TWI_CONTROL(bfin_read_TWI_CONTROL() & ~TWI_ENA);
	SSYNC();

	return 0;
}

static int i2c_bfin_twi_resume(struct platform_device *dev)
{
/*	struct bfin_twi_iface *iface = platform_get_drvdata(dev);*/

	/* Enable TWI */
	bfin_write_TWI_CONTROL(bfin_read_TWI_CONTROL() | TWI_ENA);
	SSYNC();

	return 0;
}

static int i2c_bfin_twi_probe(struct platform_device *dev)
{
	struct bfin_twi_iface *iface = &twi_iface;
	struct i2c_adapter *p_adap;
	int rc;

	mutex_init(&(iface->twi_lock));
	spin_lock_init(&(iface->lock));
	init_completion(&(iface->complete));
	iface->irq = IRQ_TWI;

	init_timer(&(iface->timeout_timer));
	iface->timeout_timer.function = bfin_twi_timeout;
	iface->timeout_timer.data = (unsigned long)iface;

	p_adap = &iface->adap;
	p_adap->id = I2C_HW_BLACKFIN;
	strlcpy(p_adap->name, dev->name, sizeof(p_adap->name));
	p_adap->algo = &bfin_twi_algorithm;
	p_adap->algo_data = iface;
	p_adap->class = I2C_CLASS_ALL;
	p_adap->dev.parent = &dev->dev;

	rc = request_irq(iface->irq, bfin_twi_interrupt_entry,
		IRQF_DISABLED, dev->name, iface);
	if (rc) {
		dev_err(&(p_adap->dev), "i2c-bfin-twi: can't get IRQ %d !\n",
			iface->irq);
		return -ENODEV;
	}

	/* Set TWI internal clock as 10MHz */
	bfin_write_TWI_CONTROL(((get_sclk() / 1024 / 1024 + 5) / 10) & 0x7F);

	/* Set Twi interface clock as specified */
	bfin_write_TWI_CLKDIV((( 5*1024 / CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ )
			<< 8) | (( 5*1024 / CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ )
			& 0xFF));

	/* Enable TWI */
	bfin_write_TWI_CONTROL(bfin_read_TWI_CONTROL() | TWI_ENA);
	SSYNC();

	rc = i2c_add_adapter(p_adap);
	if (rc < 0)
		free_irq(iface->irq, iface);
	else
		platform_set_drvdata(dev, iface);

	return rc;
}

static int i2c_bfin_twi_remove(struct platform_device *pdev)
{
	struct bfin_twi_iface *iface = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	i2c_del_adapter(&(iface->adap));
	free_irq(iface->irq, iface);

	return 0;
}

static struct platform_driver i2c_bfin_twi_driver = {
	.probe		= i2c_bfin_twi_probe,
	.remove		= i2c_bfin_twi_remove,
	.suspend	= i2c_bfin_twi_suspend,
	.resume		= i2c_bfin_twi_resume,
	.driver		= {
		.name	= "i2c-bfin-twi",
		.owner	= THIS_MODULE,
	},
};

static int __init i2c_bfin_twi_init(void)
{
	pr_info("I2C: Blackfin I2C TWI driver\n");

	return platform_driver_register(&i2c_bfin_twi_driver);
}

static void __exit i2c_bfin_twi_exit(void)
{
	platform_driver_unregister(&i2c_bfin_twi_driver);
}

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for Blackfin TWI");
MODULE_LICENSE("GPL");

module_init(i2c_bfin_twi_init);
module_exit(i2c_bfin_twi_exit);
