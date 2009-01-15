/*
 * Blackfin On-Chip Two Wire Interface Driver
 *
 * Copyright 2005-2007 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
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
#include <asm/portmux.h>
#include <asm/irq.h>

#define POLL_TIMEOUT       (2 * HZ)

/* SMBus mode*/
#define TWI_I2C_MODE_STANDARD		1
#define TWI_I2C_MODE_STANDARDSUB	2
#define TWI_I2C_MODE_COMBINED		3
#define TWI_I2C_MODE_REPEAT		4

struct bfin_twi_iface {
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
	struct i2c_msg 		*pmsg;
	int			msg_num;
	int			cur_msg;
	u16			saved_clkdiv;
	u16			saved_control;
	void __iomem		*regs_base;
};


#define DEFINE_TWI_REG(reg, off) \
static inline u16 read_##reg(struct bfin_twi_iface *iface) \
	{ return bfin_read16(iface->regs_base + (off)); } \
static inline void write_##reg(struct bfin_twi_iface *iface, u16 v) \
	{ bfin_write16(iface->regs_base + (off), v); }

DEFINE_TWI_REG(CLKDIV, 0x00)
DEFINE_TWI_REG(CONTROL, 0x04)
DEFINE_TWI_REG(SLAVE_CTL, 0x08)
DEFINE_TWI_REG(SLAVE_STAT, 0x0C)
DEFINE_TWI_REG(SLAVE_ADDR, 0x10)
DEFINE_TWI_REG(MASTER_CTL, 0x14)
DEFINE_TWI_REG(MASTER_STAT, 0x18)
DEFINE_TWI_REG(MASTER_ADDR, 0x1C)
DEFINE_TWI_REG(INT_STAT, 0x20)
DEFINE_TWI_REG(INT_MASK, 0x24)
DEFINE_TWI_REG(FIFO_CTL, 0x28)
DEFINE_TWI_REG(FIFO_STAT, 0x2C)
DEFINE_TWI_REG(XMT_DATA8, 0x80)
DEFINE_TWI_REG(XMT_DATA16, 0x84)
DEFINE_TWI_REG(RCV_DATA8, 0x88)
DEFINE_TWI_REG(RCV_DATA16, 0x8C)

static const u16 pin_req[2][3] = {
	{P_TWI0_SCL, P_TWI0_SDA, 0},
	{P_TWI1_SCL, P_TWI1_SDA, 0},
};

static void bfin_twi_handle_interrupt(struct bfin_twi_iface *iface)
{
	unsigned short twi_int_status = read_INT_STAT(iface);
	unsigned short mast_stat = read_MASTER_STAT(iface);

	if (twi_int_status & XMTSERV) {
		/* Transmit next data */
		if (iface->writeNum > 0) {
			write_XMT_DATA8(iface, *(iface->transPtr++));
			iface->writeNum--;
		}
		/* start receive immediately after complete sending in
		 * combine mode.
		 */
		else if (iface->cur_mode == TWI_I2C_MODE_COMBINED)
			write_MASTER_CTL(iface,
				read_MASTER_CTL(iface) | MDIR | RSTART);
		else if (iface->manual_stop)
			write_MASTER_CTL(iface,
				read_MASTER_CTL(iface) | STOP);
		else if (iface->cur_mode == TWI_I2C_MODE_REPEAT &&
				iface->cur_msg+1 < iface->msg_num)
			write_MASTER_CTL(iface,
				read_MASTER_CTL(iface) | RSTART);
		SSYNC();
		/* Clear status */
		write_INT_STAT(iface, XMTSERV);
		SSYNC();
	}
	if (twi_int_status & RCVSERV) {
		if (iface->readNum > 0) {
			/* Receive next data */
			*(iface->transPtr) = read_RCV_DATA8(iface);
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
			write_MASTER_CTL(iface,
				read_MASTER_CTL(iface) | STOP);
			SSYNC();
		} else if (iface->cur_mode == TWI_I2C_MODE_REPEAT &&
				iface->cur_msg+1 < iface->msg_num) {
			write_MASTER_CTL(iface,
				read_MASTER_CTL(iface) | RSTART);
			SSYNC();
		}
		/* Clear interrupt source */
		write_INT_STAT(iface, RCVSERV);
		SSYNC();
	}
	if (twi_int_status & MERR) {
		write_INT_STAT(iface, MERR);
		write_INT_MASK(iface, 0);
		write_MASTER_STAT(iface, 0x3e);
		write_MASTER_CTL(iface, 0);
		SSYNC();
		iface->result = -EIO;
		/* if both err and complete int stats are set, return proper
		 * results.
		 */
		if (twi_int_status & MCOMP) {
			write_INT_STAT(iface, MCOMP);
			write_INT_MASK(iface, 0);
			write_MASTER_CTL(iface, 0);
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
		write_INT_STAT(iface, MCOMP);
		SSYNC();
		if (iface->cur_mode == TWI_I2C_MODE_COMBINED) {
			if (iface->readNum == 0) {
				/* set the read number to 1 and ask for manual
				 * stop in block combine mode
				 */
				iface->readNum = 1;
				iface->manual_stop = 1;
				write_MASTER_CTL(iface,
					read_MASTER_CTL(iface) | (0xff << 6));
			} else {
				/* set the readd number in other
				 * combine mode.
				 */
				write_MASTER_CTL(iface,
					(read_MASTER_CTL(iface) &
					(~(0xff << 6))) |
					(iface->readNum << 6));
			}
			/* remove restart bit and enable master receive */
			write_MASTER_CTL(iface,
				read_MASTER_CTL(iface) & ~RSTART);
			write_MASTER_CTL(iface,
				read_MASTER_CTL(iface) | MEN | MDIR);
			SSYNC();
		} else if (iface->cur_mode == TWI_I2C_MODE_REPEAT &&
				iface->cur_msg+1 < iface->msg_num) {
			iface->cur_msg++;
			iface->transPtr = iface->pmsg[iface->cur_msg].buf;
			iface->writeNum = iface->readNum =
				iface->pmsg[iface->cur_msg].len;
			/* Set Transmit device address */
			write_MASTER_ADDR(iface,
				iface->pmsg[iface->cur_msg].addr);
			if (iface->pmsg[iface->cur_msg].flags & I2C_M_RD)
				iface->read_write = I2C_SMBUS_READ;
			else {
				iface->read_write = I2C_SMBUS_WRITE;
				/* Transmit first data */
				if (iface->writeNum > 0) {
					write_XMT_DATA8(iface,
						*(iface->transPtr++));
					iface->writeNum--;
					SSYNC();
				}
			}

			if (iface->pmsg[iface->cur_msg].len <= 255)
				write_MASTER_CTL(iface,
				iface->pmsg[iface->cur_msg].len << 6);
			else {
				write_MASTER_CTL(iface, 0xff << 6);
				iface->manual_stop = 1;
			}
			/* remove restart bit and enable master receive */
			write_MASTER_CTL(iface,
				read_MASTER_CTL(iface) & ~RSTART);
			write_MASTER_CTL(iface, read_MASTER_CTL(iface) |
				MEN | ((iface->read_write == I2C_SMBUS_READ) ?
				MDIR : 0));
			SSYNC();
		} else {
			iface->result = 1;
			write_INT_MASK(iface, 0);
			write_MASTER_CTL(iface, 0);
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
	int rc = 0;

	if (!(read_CONTROL(iface) & TWI_ENA))
		return -ENXIO;

	while (read_MASTER_STAT(iface) & BUSBUSY)
		yield();

	iface->pmsg = msgs;
	iface->msg_num = num;
	iface->cur_msg = 0;

	pmsg = &msgs[0];
	if (pmsg->flags & I2C_M_TEN) {
		dev_err(&adap->dev, "10 bits addr not supported!\n");
		return -EINVAL;
	}

	iface->cur_mode = TWI_I2C_MODE_REPEAT;
	iface->manual_stop = 0;
	iface->transPtr = pmsg->buf;
	iface->writeNum = iface->readNum = pmsg->len;
	iface->result = 0;
	iface->timeout_count = 10;
	init_completion(&(iface->complete));
	/* Set Transmit device address */
	write_MASTER_ADDR(iface, pmsg->addr);

	/* FIFO Initiation. Data in FIFO should be
	 *  discarded before start a new operation.
	 */
	write_FIFO_CTL(iface, 0x3);
	SSYNC();
	write_FIFO_CTL(iface, 0);
	SSYNC();

	if (pmsg->flags & I2C_M_RD)
		iface->read_write = I2C_SMBUS_READ;
	else {
		iface->read_write = I2C_SMBUS_WRITE;
		/* Transmit first data */
		if (iface->writeNum > 0) {
			write_XMT_DATA8(iface, *(iface->transPtr++));
			iface->writeNum--;
			SSYNC();
		}
	}

	/* clear int stat */
	write_INT_STAT(iface, MERR | MCOMP | XMTSERV | RCVSERV);

	/* Interrupt mask . Enable XMT, RCV interrupt */
	write_INT_MASK(iface, MCOMP | MERR | RCVSERV | XMTSERV);
	SSYNC();

	if (pmsg->len <= 255)
		write_MASTER_CTL(iface, pmsg->len << 6);
	else {
		write_MASTER_CTL(iface, 0xff << 6);
		iface->manual_stop = 1;
	}

	iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
	add_timer(&iface->timeout_timer);

	/* Master enable */
	write_MASTER_CTL(iface, read_MASTER_CTL(iface) | MEN |
		((iface->read_write == I2C_SMBUS_READ) ? MDIR : 0) |
		((CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ > 100) ? FAST : 0));
	SSYNC();

	wait_for_completion(&iface->complete);

	rc = iface->result;

	if (rc == 1)
		return num;
	else
		return rc;
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

	if (!(read_CONTROL(iface) & TWI_ENA))
		return -ENXIO;

	while (read_MASTER_STAT(iface) & BUSBUSY)
		yield();

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
	init_completion(&(iface->complete));

	/* FIFO Initiation. Data in FIFO should be discarded before
	 * start a new operation.
	 */
	write_FIFO_CTL(iface, 0x3);
	SSYNC();
	write_FIFO_CTL(iface, 0);

	/* clear int stat */
	write_INT_STAT(iface, MERR | MCOMP | XMTSERV | RCVSERV);

	/* Set Transmit device address */
	write_MASTER_ADDR(iface, addr);
	SSYNC();

	iface->timeout_timer.expires = jiffies + POLL_TIMEOUT;
	add_timer(&iface->timeout_timer);

	switch (iface->cur_mode) {
	case TWI_I2C_MODE_STANDARDSUB:
		write_XMT_DATA8(iface, iface->command);
		write_INT_MASK(iface, MCOMP | MERR |
			((iface->read_write == I2C_SMBUS_READ) ?
			RCVSERV : XMTSERV));
		SSYNC();

		if (iface->writeNum + 1 <= 255)
			write_MASTER_CTL(iface, (iface->writeNum + 1) << 6);
		else {
			write_MASTER_CTL(iface, 0xff << 6);
			iface->manual_stop = 1;
		}
		/* Master enable */
		write_MASTER_CTL(iface, read_MASTER_CTL(iface) | MEN |
			((CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ>100) ? FAST : 0));
		break;
	case TWI_I2C_MODE_COMBINED:
		write_XMT_DATA8(iface, iface->command);
		write_INT_MASK(iface, MCOMP | MERR | RCVSERV | XMTSERV);
		SSYNC();

		if (iface->writeNum > 0)
			write_MASTER_CTL(iface, (iface->writeNum + 1) << 6);
		else
			write_MASTER_CTL(iface, 0x1 << 6);
		/* Master enable */
		write_MASTER_CTL(iface, read_MASTER_CTL(iface) | MEN |
			((CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ>100) ? FAST : 0));
		break;
	default:
		write_MASTER_CTL(iface, 0);
		if (size != I2C_SMBUS_QUICK) {
			/* Don't access xmit data register when this is a
			 * read operation.
			 */
			if (iface->read_write != I2C_SMBUS_READ) {
				if (iface->writeNum > 0) {
					write_XMT_DATA8(iface,
						*(iface->transPtr++));
					if (iface->writeNum <= 255)
						write_MASTER_CTL(iface,
							iface->writeNum << 6);
					else {
						write_MASTER_CTL(iface,
							0xff << 6);
						iface->manual_stop = 1;
					}
					iface->writeNum--;
				} else {
					write_XMT_DATA8(iface, iface->command);
					write_MASTER_CTL(iface, 1 << 6);
				}
			} else {
				if (iface->readNum > 0 && iface->readNum <= 255)
					write_MASTER_CTL(iface,
						iface->readNum << 6);
				else if (iface->readNum > 255) {
					write_MASTER_CTL(iface, 0xff << 6);
					iface->manual_stop = 1;
				} else {
					del_timer(&iface->timeout_timer);
					break;
				}
			}
		}
		write_INT_MASK(iface, MCOMP | MERR |
			((iface->read_write == I2C_SMBUS_READ) ?
			RCVSERV : XMTSERV));
		SSYNC();

		/* Master enable */
		write_MASTER_CTL(iface, read_MASTER_CTL(iface) | MEN |
			((iface->read_write == I2C_SMBUS_READ) ? MDIR : 0) |
			((CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ > 100) ? FAST : 0));
		break;
	}
	SSYNC();

	wait_for_completion(&iface->complete);

	rc = (iface->result >= 0) ? 0 : -1;

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

static int i2c_bfin_twi_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct bfin_twi_iface *iface = platform_get_drvdata(pdev);

	iface->saved_clkdiv = read_CLKDIV(iface);
	iface->saved_control = read_CONTROL(iface);

	free_irq(iface->irq, iface);

	/* Disable TWI */
	write_CONTROL(iface, iface->saved_control & ~TWI_ENA);

	return 0;
}

static int i2c_bfin_twi_resume(struct platform_device *pdev)
{
	struct bfin_twi_iface *iface = platform_get_drvdata(pdev);

	int rc = request_irq(iface->irq, bfin_twi_interrupt_entry,
		IRQF_DISABLED, pdev->name, iface);
	if (rc) {
		dev_err(&pdev->dev, "Can't get IRQ %d !\n", iface->irq);
		return -ENODEV;
	}

	/* Resume TWI interface clock as specified */
	write_CLKDIV(iface, iface->saved_clkdiv);

	/* Resume TWI */
	write_CONTROL(iface, iface->saved_control);

	return 0;
}

static int i2c_bfin_twi_probe(struct platform_device *pdev)
{
	struct bfin_twi_iface *iface;
	struct i2c_adapter *p_adap;
	struct resource *res;
	int rc;

	iface = kzalloc(sizeof(struct bfin_twi_iface), GFP_KERNEL);
	if (!iface) {
		dev_err(&pdev->dev, "Cannot allocate memory\n");
		rc = -ENOMEM;
		goto out_error_nomem;
	}

	spin_lock_init(&(iface->lock));

	/* Find and map our resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Cannot get IORESOURCE_MEM\n");
		rc = -ENOENT;
		goto out_error_get_res;
	}

	iface->regs_base = ioremap(res->start, res->end - res->start + 1);
	if (iface->regs_base == NULL) {
		dev_err(&pdev->dev, "Cannot map IO\n");
		rc = -ENXIO;
		goto out_error_ioremap;
	}

	iface->irq = platform_get_irq(pdev, 0);
	if (iface->irq < 0) {
		dev_err(&pdev->dev, "No IRQ specified\n");
		rc = -ENOENT;
		goto out_error_no_irq;
	}

	init_timer(&(iface->timeout_timer));
	iface->timeout_timer.function = bfin_twi_timeout;
	iface->timeout_timer.data = (unsigned long)iface;

	p_adap = &iface->adap;
	p_adap->id = I2C_HW_BLACKFIN;
	p_adap->nr = pdev->id;
	strlcpy(p_adap->name, pdev->name, sizeof(p_adap->name));
	p_adap->algo = &bfin_twi_algorithm;
	p_adap->algo_data = iface;
	p_adap->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	p_adap->dev.parent = &pdev->dev;

	rc = peripheral_request_list(pin_req[pdev->id], "i2c-bfin-twi");
	if (rc) {
		dev_err(&pdev->dev, "Can't setup pin mux!\n");
		goto out_error_pin_mux;
	}

	rc = request_irq(iface->irq, bfin_twi_interrupt_entry,
		IRQF_DISABLED, pdev->name, iface);
	if (rc) {
		dev_err(&pdev->dev, "Can't get IRQ %d !\n", iface->irq);
		rc = -ENODEV;
		goto out_error_req_irq;
	}

	/* Set TWI internal clock as 10MHz */
	write_CONTROL(iface, ((get_sclk() / 1024 / 1024 + 5) / 10) & 0x7F);

	/* Set Twi interface clock as specified */
	write_CLKDIV(iface, ((5*1024 / CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ)
			<< 8) | ((5*1024 / CONFIG_I2C_BLACKFIN_TWI_CLK_KHZ)
			& 0xFF));

	/* Enable TWI */
	write_CONTROL(iface, read_CONTROL(iface) | TWI_ENA);
	SSYNC();

	rc = i2c_add_numbered_adapter(p_adap);
	if (rc < 0) {
		dev_err(&pdev->dev, "Can't add i2c adapter!\n");
		goto out_error_add_adapter;
	}

	platform_set_drvdata(pdev, iface);

	dev_info(&pdev->dev, "Blackfin BF5xx on-chip I2C TWI Contoller, "
		"regs_base@%p\n", iface->regs_base);

	return 0;

out_error_add_adapter:
	free_irq(iface->irq, iface);
out_error_req_irq:
out_error_no_irq:
	peripheral_free_list(pin_req[pdev->id]);
out_error_pin_mux:
	iounmap(iface->regs_base);
out_error_ioremap:
out_error_get_res:
	kfree(iface);
out_error_nomem:
	return rc;
}

static int i2c_bfin_twi_remove(struct platform_device *pdev)
{
	struct bfin_twi_iface *iface = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	i2c_del_adapter(&(iface->adap));
	free_irq(iface->irq, iface);
	peripheral_free_list(pin_req[pdev->id]);
	iounmap(iface->regs_base);
	kfree(iface);

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
	return platform_driver_register(&i2c_bfin_twi_driver);
}

static void __exit i2c_bfin_twi_exit(void)
{
	platform_driver_unregister(&i2c_bfin_twi_driver);
}

module_init(i2c_bfin_twi_init);
module_exit(i2c_bfin_twi_exit);

MODULE_AUTHOR("Bryan Wu, Sonic Zhang");
MODULE_DESCRIPTION("Blackfin BF5xx on-chip I2C TWI Contoller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:i2c-bfin-twi");
