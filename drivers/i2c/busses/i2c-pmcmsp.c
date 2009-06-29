/*
 * Specific bus support for PMC-TWI compliant implementation on MSP71xx.
 *
 * Copyright 2005-2007 PMC-Sierra, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <asm/io.h>

#define DRV_NAME	"pmcmsptwi"

#define MSP_TWI_SF_CLK_REG_OFFSET	0x00
#define MSP_TWI_HS_CLK_REG_OFFSET	0x04
#define MSP_TWI_CFG_REG_OFFSET		0x08
#define MSP_TWI_CMD_REG_OFFSET		0x0c
#define MSP_TWI_ADD_REG_OFFSET		0x10
#define MSP_TWI_DAT_0_REG_OFFSET	0x14
#define MSP_TWI_DAT_1_REG_OFFSET	0x18
#define MSP_TWI_INT_STS_REG_OFFSET	0x1c
#define MSP_TWI_INT_MSK_REG_OFFSET	0x20
#define MSP_TWI_BUSY_REG_OFFSET		0x24

#define MSP_TWI_INT_STS_DONE			(1 << 0)
#define MSP_TWI_INT_STS_LOST_ARBITRATION	(1 << 1)
#define MSP_TWI_INT_STS_NO_RESPONSE		(1 << 2)
#define MSP_TWI_INT_STS_DATA_COLLISION		(1 << 3)
#define MSP_TWI_INT_STS_BUSY			(1 << 4)
#define MSP_TWI_INT_STS_ALL			0x1f

#define MSP_MAX_BYTES_PER_RW		8
#define MSP_MAX_POLL			5
#define MSP_POLL_DELAY			10
#define MSP_IRQ_TIMEOUT			(MSP_MAX_POLL * MSP_POLL_DELAY)

/* IO Operation macros */
#define pmcmsptwi_readl		__raw_readl
#define pmcmsptwi_writel	__raw_writel

/* TWI command type */
enum pmcmsptwi_cmd_type {
	MSP_TWI_CMD_WRITE	= 0,	/* Write only */
	MSP_TWI_CMD_READ	= 1,	/* Read only */
	MSP_TWI_CMD_WRITE_READ	= 2,	/* Write then Read */
};

/* The possible results of the xferCmd */
enum pmcmsptwi_xfer_result {
	MSP_TWI_XFER_OK	= 0,
	MSP_TWI_XFER_TIMEOUT,
	MSP_TWI_XFER_BUSY,
	MSP_TWI_XFER_DATA_COLLISION,
	MSP_TWI_XFER_NO_RESPONSE,
	MSP_TWI_XFER_LOST_ARBITRATION,
};

/* Corresponds to a PMCTWI clock configuration register */
struct pmcmsptwi_clock {
	u8 filter;	/* Bits 15:12,	default = 0x03 */
	u16 clock;	/* Bits 9:0,	default = 0x001f */
};

struct pmcmsptwi_clockcfg {
	struct pmcmsptwi_clock standard;  /* The standard/fast clock config */
	struct pmcmsptwi_clock highspeed; /* The highspeed clock config */
};

/* Corresponds to the main TWI configuration register */
struct pmcmsptwi_cfg {
	u8 arbf;	/* Bits 15:12,	default=0x03 */
	u8 nak;		/* Bits 11:8,	default=0x03 */
	u8 add10;	/* Bit 7,	default=0x00 */
	u8 mst_code;	/* Bits 6:4,	default=0x00 */
	u8 arb;		/* Bit 1,	default=0x01 */
	u8 highspeed;	/* Bit 0,	default=0x00 */
};

/* A single pmctwi command to issue */
struct pmcmsptwi_cmd {
	u16 addr;	/* The slave address (7 or 10 bits) */
	enum pmcmsptwi_cmd_type type;	/* The command type */
	u8 write_len;	/* Number of bytes in the write buffer */
	u8 read_len;	/* Number of bytes in the read buffer */
	u8 *write_data;	/* Buffer of characters to send */
	u8 *read_data;	/* Buffer to fill with incoming data */
};

/* The private data */
struct pmcmsptwi_data {
	void __iomem *iobase;			/* iomapped base for IO */
	int irq;				/* IRQ to use (0 disables) */
	struct completion wait;			/* Completion for xfer */
	struct mutex lock;			/* Used for threadsafeness */
	enum pmcmsptwi_xfer_result last_result;	/* result of last xfer */
};

/* The default settings */
static const struct pmcmsptwi_clockcfg pmcmsptwi_defclockcfg = {
	.standard = {
		.filter	= 0x3,
		.clock	= 0x1f,
	},
	.highspeed = {
		.filter	= 0x3,
		.clock	= 0x1f,
	},
};

static const struct pmcmsptwi_cfg pmcmsptwi_defcfg = {
	.arbf		= 0x03,
	.nak		= 0x03,
	.add10		= 0x00,
	.mst_code	= 0x00,
	.arb		= 0x01,
	.highspeed	= 0x00,
};

static struct pmcmsptwi_data pmcmsptwi_data;

static struct i2c_adapter pmcmsptwi_adapter;

/* inline helper functions */
static inline u32 pmcmsptwi_clock_to_reg(
			const struct pmcmsptwi_clock *clock)
{
	return ((clock->filter & 0xf) << 12) | (clock->clock & 0x03ff);
}

static inline void pmcmsptwi_reg_to_clock(
			u32 reg, struct pmcmsptwi_clock *clock)
{
	clock->filter = (reg >> 12) & 0xf;
	clock->clock = reg & 0x03ff;
}

static inline u32 pmcmsptwi_cfg_to_reg(const struct pmcmsptwi_cfg *cfg)
{
	return ((cfg->arbf & 0xf) << 12) |
		((cfg->nak & 0xf) << 8) |
		((cfg->add10 & 0x1) << 7) |
		((cfg->mst_code & 0x7) << 4) |
		((cfg->arb & 0x1) << 1) |
		(cfg->highspeed & 0x1);
}

static inline void pmcmsptwi_reg_to_cfg(u32 reg, struct pmcmsptwi_cfg *cfg)
{
	cfg->arbf = (reg >> 12) & 0xf;
	cfg->nak = (reg >> 8) & 0xf;
	cfg->add10 = (reg >> 7) & 0x1;
	cfg->mst_code = (reg >> 4) & 0x7;
	cfg->arb = (reg >> 1) & 0x1;
	cfg->highspeed = reg & 0x1;
}

/*
 * Sets the current clock configuration
 */
static void pmcmsptwi_set_clock_config(const struct pmcmsptwi_clockcfg *cfg,
					struct pmcmsptwi_data *data)
{
	mutex_lock(&data->lock);
	pmcmsptwi_writel(pmcmsptwi_clock_to_reg(&cfg->standard),
				data->iobase + MSP_TWI_SF_CLK_REG_OFFSET);
	pmcmsptwi_writel(pmcmsptwi_clock_to_reg(&cfg->highspeed),
				data->iobase + MSP_TWI_HS_CLK_REG_OFFSET);
	mutex_unlock(&data->lock);
}

/*
 * Gets the current TWI bus configuration
 */
static void pmcmsptwi_get_twi_config(struct pmcmsptwi_cfg *cfg,
					struct pmcmsptwi_data *data)
{
	mutex_lock(&data->lock);
	pmcmsptwi_reg_to_cfg(pmcmsptwi_readl(
				data->iobase + MSP_TWI_CFG_REG_OFFSET), cfg);
	mutex_unlock(&data->lock);
}

/*
 * Sets the current TWI bus configuration
 */
static void pmcmsptwi_set_twi_config(const struct pmcmsptwi_cfg *cfg,
					struct pmcmsptwi_data *data)
{
	mutex_lock(&data->lock);
	pmcmsptwi_writel(pmcmsptwi_cfg_to_reg(cfg),
				data->iobase + MSP_TWI_CFG_REG_OFFSET);
	mutex_unlock(&data->lock);
}

/*
 * Parses the 'int_sts' register and returns a well-defined error code
 */
static enum pmcmsptwi_xfer_result pmcmsptwi_get_result(u32 reg)
{
	if (reg & MSP_TWI_INT_STS_LOST_ARBITRATION) {
		dev_dbg(&pmcmsptwi_adapter.dev,
			"Result: Lost arbitration\n");
		return MSP_TWI_XFER_LOST_ARBITRATION;
	} else if (reg & MSP_TWI_INT_STS_NO_RESPONSE) {
		dev_dbg(&pmcmsptwi_adapter.dev,
			"Result: No response\n");
		return MSP_TWI_XFER_NO_RESPONSE;
	} else if (reg & MSP_TWI_INT_STS_DATA_COLLISION) {
		dev_dbg(&pmcmsptwi_adapter.dev,
			"Result: Data collision\n");
		return MSP_TWI_XFER_DATA_COLLISION;
	} else if (reg & MSP_TWI_INT_STS_BUSY) {
		dev_dbg(&pmcmsptwi_adapter.dev,
			"Result: Bus busy\n");
		return MSP_TWI_XFER_BUSY;
	}

	dev_dbg(&pmcmsptwi_adapter.dev, "Result: Operation succeeded\n");
	return MSP_TWI_XFER_OK;
}

/*
 * In interrupt mode, handle the interrupt.
 * NOTE: Assumes data->lock is held.
 */
static irqreturn_t pmcmsptwi_interrupt(int irq, void *ptr)
{
	struct pmcmsptwi_data *data = ptr;

	u32 reason = pmcmsptwi_readl(data->iobase +
					MSP_TWI_INT_STS_REG_OFFSET);
	pmcmsptwi_writel(reason, data->iobase + MSP_TWI_INT_STS_REG_OFFSET);

	dev_dbg(&pmcmsptwi_adapter.dev, "Got interrupt 0x%08x\n", reason);
	if (!(reason & MSP_TWI_INT_STS_DONE))
		return IRQ_NONE;

	data->last_result = pmcmsptwi_get_result(reason);
	complete(&data->wait);

	return IRQ_HANDLED;
}

/*
 * Probe for and register the device and return 0 if there is one.
 */
static int __devinit pmcmsptwi_probe(struct platform_device *pldev)
{
	struct resource *res;
	int rc = -ENODEV;

	/* get the static platform resources */
	res = platform_get_resource(pldev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pldev->dev, "IOMEM resource not found\n");
		goto ret_err;
	}

	/* reserve the memory region */
	if (!request_mem_region(res->start, resource_size(res),
				pldev->name)) {
		dev_err(&pldev->dev,
			"Unable to get memory/io address region 0x%08x\n",
			res->start);
		rc = -EBUSY;
		goto ret_err;
	}

	/* remap the memory */
	pmcmsptwi_data.iobase = ioremap_nocache(res->start,
						resource_size(res));
	if (!pmcmsptwi_data.iobase) {
		dev_err(&pldev->dev,
			"Unable to ioremap address 0x%08x\n", res->start);
		rc = -EIO;
		goto ret_unreserve;
	}

	/* request the irq */
	pmcmsptwi_data.irq = platform_get_irq(pldev, 0);
	if (pmcmsptwi_data.irq) {
		rc = request_irq(pmcmsptwi_data.irq, &pmcmsptwi_interrupt,
			IRQF_SHARED | IRQF_DISABLED | IRQF_SAMPLE_RANDOM,
			pldev->name, &pmcmsptwi_data);
		if (rc == 0) {
			/*
			 * Enable 'DONE' interrupt only.
			 *
			 * If you enable all interrupts, you will get one on
			 * error and another when the operation completes.
			 * This way you only have to handle one interrupt,
			 * but you can still check all result flags.
			 */
			pmcmsptwi_writel(MSP_TWI_INT_STS_DONE,
					pmcmsptwi_data.iobase +
					MSP_TWI_INT_MSK_REG_OFFSET);
		} else {
			dev_warn(&pldev->dev,
				"Could not assign TWI IRQ handler "
				"to irq %d (continuing with poll)\n",
				pmcmsptwi_data.irq);
			pmcmsptwi_data.irq = 0;
		}
	}

	init_completion(&pmcmsptwi_data.wait);
	mutex_init(&pmcmsptwi_data.lock);

	pmcmsptwi_set_clock_config(&pmcmsptwi_defclockcfg, &pmcmsptwi_data);
	pmcmsptwi_set_twi_config(&pmcmsptwi_defcfg, &pmcmsptwi_data);

	printk(KERN_INFO DRV_NAME ": Registering MSP71xx I2C adapter\n");

	pmcmsptwi_adapter.dev.parent = &pldev->dev;
	platform_set_drvdata(pldev, &pmcmsptwi_adapter);
	i2c_set_adapdata(&pmcmsptwi_adapter, &pmcmsptwi_data);

	rc = i2c_add_adapter(&pmcmsptwi_adapter);
	if (rc) {
		dev_err(&pldev->dev, "Unable to register I2C adapter\n");
		goto ret_unmap;
	}

	return 0;

ret_unmap:
	platform_set_drvdata(pldev, NULL);
	if (pmcmsptwi_data.irq) {
		pmcmsptwi_writel(0,
			pmcmsptwi_data.iobase + MSP_TWI_INT_MSK_REG_OFFSET);
		free_irq(pmcmsptwi_data.irq, &pmcmsptwi_data);
	}

	iounmap(pmcmsptwi_data.iobase);

ret_unreserve:
	release_mem_region(res->start, resource_size(res));

ret_err:
	return rc;
}

/*
 * Release the device and return 0 if there is one.
 */
static int __devexit pmcmsptwi_remove(struct platform_device *pldev)
{
	struct resource *res;

	i2c_del_adapter(&pmcmsptwi_adapter);

	platform_set_drvdata(pldev, NULL);
	if (pmcmsptwi_data.irq) {
		pmcmsptwi_writel(0,
			pmcmsptwi_data.iobase + MSP_TWI_INT_MSK_REG_OFFSET);
		free_irq(pmcmsptwi_data.irq, &pmcmsptwi_data);
	}

	iounmap(pmcmsptwi_data.iobase);

	res = platform_get_resource(pldev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	return 0;
}

/*
 * Polls the 'busy' register until the command is complete.
 * NOTE: Assumes data->lock is held.
 */
static void pmcmsptwi_poll_complete(struct pmcmsptwi_data *data)
{
	int i;

	for (i = 0; i < MSP_MAX_POLL; i++) {
		u32 val = pmcmsptwi_readl(data->iobase +
						MSP_TWI_BUSY_REG_OFFSET);
		if (val == 0) {
			u32 reason = pmcmsptwi_readl(data->iobase +
						MSP_TWI_INT_STS_REG_OFFSET);
			pmcmsptwi_writel(reason, data->iobase +
						MSP_TWI_INT_STS_REG_OFFSET);
			data->last_result = pmcmsptwi_get_result(reason);
			return;
		}
		udelay(MSP_POLL_DELAY);
	}

	dev_dbg(&pmcmsptwi_adapter.dev, "Result: Poll timeout\n");
	data->last_result = MSP_TWI_XFER_TIMEOUT;
}

/*
 * Do the transfer (low level):
 *   May use interrupt-driven or polling, depending on if an IRQ is
 *   presently registered.
 * NOTE: Assumes data->lock is held.
 */
static enum pmcmsptwi_xfer_result pmcmsptwi_do_xfer(
			u32 reg, struct pmcmsptwi_data *data)
{
	dev_dbg(&pmcmsptwi_adapter.dev, "Writing cmd reg 0x%08x\n", reg);
	pmcmsptwi_writel(reg, data->iobase + MSP_TWI_CMD_REG_OFFSET);
	if (data->irq) {
		unsigned long timeleft = wait_for_completion_timeout(
						&data->wait, MSP_IRQ_TIMEOUT);
		if (timeleft == 0) {
			dev_dbg(&pmcmsptwi_adapter.dev,
				"Result: IRQ timeout\n");
			complete(&data->wait);
			data->last_result = MSP_TWI_XFER_TIMEOUT;
		}
	} else
		pmcmsptwi_poll_complete(data);

	return data->last_result;
}

/*
 * Helper routine, converts 'pmctwi_cmd' struct to register format
 */
static inline u32 pmcmsptwi_cmd_to_reg(const struct pmcmsptwi_cmd *cmd)
{
	return ((cmd->type & 0x3) << 8) |
		(((cmd->write_len - 1) & 0x7) << 4) |
		((cmd->read_len - 1) & 0x7);
}

/*
 * Do the transfer (high level)
 */
static enum pmcmsptwi_xfer_result pmcmsptwi_xfer_cmd(
			struct pmcmsptwi_cmd *cmd,
			struct pmcmsptwi_data *data)
{
	enum pmcmsptwi_xfer_result retval;

	if ((cmd->type == MSP_TWI_CMD_WRITE && cmd->write_len == 0) ||
	    (cmd->type == MSP_TWI_CMD_READ && cmd->read_len == 0) ||
	    (cmd->type == MSP_TWI_CMD_WRITE_READ &&
	    (cmd->read_len == 0 || cmd->write_len == 0))) {
		dev_err(&pmcmsptwi_adapter.dev,
			"%s: Cannot transfer less than 1 byte\n",
			__func__);
		return -EINVAL;
	}

	if (cmd->read_len > MSP_MAX_BYTES_PER_RW ||
	    cmd->write_len > MSP_MAX_BYTES_PER_RW) {
		dev_err(&pmcmsptwi_adapter.dev,
			"%s: Cannot transfer more than %d bytes\n",
			__func__, MSP_MAX_BYTES_PER_RW);
		return -EINVAL;
	}

	mutex_lock(&data->lock);
	dev_dbg(&pmcmsptwi_adapter.dev,
		"Setting address to 0x%04x\n", cmd->addr);
	pmcmsptwi_writel(cmd->addr, data->iobase + MSP_TWI_ADD_REG_OFFSET);

	if (cmd->type == MSP_TWI_CMD_WRITE ||
	    cmd->type == MSP_TWI_CMD_WRITE_READ) {
		u64 tmp = be64_to_cpup((__be64 *)cmd->write_data);
		tmp >>= (MSP_MAX_BYTES_PER_RW - cmd->write_len) * 8;
		dev_dbg(&pmcmsptwi_adapter.dev, "Writing 0x%016llx\n", tmp);
		pmcmsptwi_writel(tmp & 0x00000000ffffffffLL,
				data->iobase + MSP_TWI_DAT_0_REG_OFFSET);
		if (cmd->write_len > 4)
			pmcmsptwi_writel(tmp >> 32,
				data->iobase + MSP_TWI_DAT_1_REG_OFFSET);
	}

	retval = pmcmsptwi_do_xfer(pmcmsptwi_cmd_to_reg(cmd), data);
	if (retval != MSP_TWI_XFER_OK)
		goto xfer_err;

	if (cmd->type == MSP_TWI_CMD_READ ||
	    cmd->type == MSP_TWI_CMD_WRITE_READ) {
		int i;
		u64 rmsk = ~(0xffffffffffffffffLL << (cmd->read_len * 8));
		u64 tmp = (u64)pmcmsptwi_readl(data->iobase +
					MSP_TWI_DAT_0_REG_OFFSET);
		if (cmd->read_len > 4)
			tmp |= (u64)pmcmsptwi_readl(data->iobase +
					MSP_TWI_DAT_1_REG_OFFSET) << 32;
		tmp &= rmsk;
		dev_dbg(&pmcmsptwi_adapter.dev, "Read 0x%016llx\n", tmp);

		for (i = 0; i < cmd->read_len; i++)
			cmd->read_data[i] = tmp >> i;
	}

xfer_err:
	mutex_unlock(&data->lock);

	return retval;
}

/* -- Algorithm functions -- */

/*
 * Sends an i2c command out on the adapter
 */
static int pmcmsptwi_master_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msg, int num)
{
	struct pmcmsptwi_data *data = i2c_get_adapdata(adap);
	struct pmcmsptwi_cmd cmd;
	struct pmcmsptwi_cfg oldcfg, newcfg;
	int ret;

	if (num > 2) {
		dev_dbg(&adap->dev, "%d messages unsupported\n", num);
		return -EINVAL;
	} else if (num == 2) {
		/* Check for a dual write-then-read command */
		struct i2c_msg *nextmsg = msg + 1;
		if (!(msg->flags & I2C_M_RD) &&
		    (nextmsg->flags & I2C_M_RD) &&
		    msg->addr == nextmsg->addr) {
			cmd.type = MSP_TWI_CMD_WRITE_READ;
			cmd.write_len = msg->len;
			cmd.write_data = msg->buf;
			cmd.read_len = nextmsg->len;
			cmd.read_data = nextmsg->buf;
		} else {
			dev_dbg(&adap->dev,
				"Non write-read dual messages unsupported\n");
			return -EINVAL;
		}
	} else if (msg->flags & I2C_M_RD) {
		cmd.type = MSP_TWI_CMD_READ;
		cmd.read_len = msg->len;
		cmd.read_data = msg->buf;
		cmd.write_len = 0;
		cmd.write_data = NULL;
	} else {
		cmd.type = MSP_TWI_CMD_WRITE;
		cmd.read_len = 0;
		cmd.read_data = NULL;
		cmd.write_len = msg->len;
		cmd.write_data = msg->buf;
	}

	if (msg->len == 0) {
		dev_err(&adap->dev, "Zero-byte messages unsupported\n");
		return -EINVAL;
	}

	cmd.addr = msg->addr;

	if (msg->flags & I2C_M_TEN) {
		pmcmsptwi_get_twi_config(&newcfg, data);
		memcpy(&oldcfg, &newcfg, sizeof(oldcfg));

		/* Set the special 10-bit address flag */
		newcfg.add10 = 1;

		pmcmsptwi_set_twi_config(&newcfg, data);
	}

	/* Execute the command */
	ret = pmcmsptwi_xfer_cmd(&cmd, data);

	if (msg->flags & I2C_M_TEN)
		pmcmsptwi_set_twi_config(&oldcfg, data);

	dev_dbg(&adap->dev, "I2C %s of %d bytes %s\n",
		(msg->flags & I2C_M_RD) ? "read" : "write", msg->len,
		(ret == MSP_TWI_XFER_OK) ? "succeeded" : "failed");

	if (ret != MSP_TWI_XFER_OK) {
		/*
		 * TODO: We could potentially loop and retry in the case
		 * of MSP_TWI_XFER_TIMEOUT.
		 */
		return -1;
	}

	return 0;
}

static u32 pmcmsptwi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR |
		I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_PROC_CALL;
}

/* -- Initialization -- */

static struct i2c_algorithm pmcmsptwi_algo = {
	.master_xfer	= pmcmsptwi_master_xfer,
	.functionality	= pmcmsptwi_i2c_func,
};

static struct i2c_adapter pmcmsptwi_adapter = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON | I2C_CLASS_SPD,
	.algo		= &pmcmsptwi_algo,
	.name		= DRV_NAME,
};

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:" DRV_NAME);

static struct platform_driver pmcmsptwi_driver = {
	.probe  = pmcmsptwi_probe,
	.remove	= __devexit_p(pmcmsptwi_remove),
	.driver = {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init pmcmsptwi_init(void)
{
	return platform_driver_register(&pmcmsptwi_driver);
}

static void __exit pmcmsptwi_exit(void)
{
	platform_driver_unregister(&pmcmsptwi_driver);
}

MODULE_DESCRIPTION("PMC MSP TWI/SMBus/I2C driver");
MODULE_LICENSE("GPL");

module_init(pmcmsptwi_init);
module_exit(pmcmsptwi_exit);
