// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/slab.h>

#include "cmd.h"
#include "core.h"
#include "i2c.h"
#include "resources.h"

#define MLXSW_I2C_CIR2_BASE		0x72000
#define MLXSW_I2C_CIR_STATUS_OFF	0x18
#define MLXSW_I2C_CIR2_OFF_STATUS	(MLXSW_I2C_CIR2_BASE + \
					 MLXSW_I2C_CIR_STATUS_OFF)
#define MLXSW_I2C_OPMOD_SHIFT		12
#define MLXSW_I2C_EVENT_BIT_SHIFT	22
#define MLXSW_I2C_GO_BIT_SHIFT		23
#define MLXSW_I2C_CIR_CTRL_STATUS_SHIFT	24
#define MLXSW_I2C_EVENT_BIT		BIT(MLXSW_I2C_EVENT_BIT_SHIFT)
#define MLXSW_I2C_GO_BIT		BIT(MLXSW_I2C_GO_BIT_SHIFT)
#define MLXSW_I2C_GO_OPMODE		BIT(MLXSW_I2C_OPMOD_SHIFT)
#define MLXSW_I2C_SET_IMM_CMD		(MLXSW_I2C_GO_OPMODE | \
					 MLXSW_CMD_OPCODE_QUERY_FW)
#define MLXSW_I2C_PUSH_IMM_CMD		(MLXSW_I2C_GO_BIT | \
					 MLXSW_I2C_SET_IMM_CMD)
#define MLXSW_I2C_SET_CMD		(MLXSW_CMD_OPCODE_ACCESS_REG)
#define MLXSW_I2C_PUSH_CMD		(MLXSW_I2C_GO_BIT | MLXSW_I2C_SET_CMD)
#define MLXSW_I2C_TLV_HDR_SIZE		0x10
#define MLXSW_I2C_ADDR_WIDTH		4
#define MLXSW_I2C_PUSH_CMD_SIZE		(MLXSW_I2C_ADDR_WIDTH + 4)
#define MLXSW_I2C_SET_EVENT_CMD		(MLXSW_I2C_EVENT_BIT)
#define MLXSW_I2C_PUSH_EVENT_CMD	(MLXSW_I2C_GO_BIT | \
					 MLXSW_I2C_SET_EVENT_CMD)
#define MLXSW_I2C_READ_SEMA_SIZE	4
#define MLXSW_I2C_PREP_SIZE		(MLXSW_I2C_ADDR_WIDTH + 28)
#define MLXSW_I2C_MBOX_SIZE		20
#define MLXSW_I2C_MBOX_OUT_PARAM_OFF	12
#define MLXSW_I2C_MBOX_OFFSET_BITS	20
#define MLXSW_I2C_MBOX_SIZE_BITS	12
#define MLXSW_I2C_ADDR_BUF_SIZE		4
#define MLXSW_I2C_BLK_DEF		32
#define MLXSW_I2C_BLK_MAX		100
#define MLXSW_I2C_RETRY			5
#define MLXSW_I2C_TIMEOUT_MSECS		5000
#define MLXSW_I2C_MAX_DATA_SIZE		256

/* Driver can be initialized by kernel platform driver or from the user
 * space. In the first case IRQ line number is passed through the platform
 * data, otherwise default IRQ line is to be used. Default IRQ is relevant
 * only for specific I2C slave address, allowing 3.4 MHz I2C path to the chip
 * (special hardware feature for I2C acceleration).
 */
#define MLXSW_I2C_DEFAULT_IRQ		17
#define MLXSW_FAST_I2C_SLAVE		0x37

/**
 * struct mlxsw_i2c - device private data:
 * @cmd: command attributes;
 * @cmd.mb_size_in: input mailbox size;
 * @cmd.mb_off_in: input mailbox offset in register space;
 * @cmd.mb_size_out: output mailbox size;
 * @cmd.mb_off_out: output mailbox offset in register space;
 * @cmd.lock: command execution lock;
 * @dev: I2C device;
 * @core: switch core pointer;
 * @bus_info: bus info block;
 * @block_size: maximum block size allowed to pass to under layer;
 * @pdata: device platform data;
 * @irq_work: interrupts work item;
 * @irq: IRQ line number;
 */
struct mlxsw_i2c {
	struct {
		u32 mb_size_in;
		u32 mb_off_in;
		u32 mb_size_out;
		u32 mb_off_out;
		struct mutex lock;
	} cmd;
	struct device *dev;
	struct mlxsw_core *core;
	struct mlxsw_bus_info bus_info;
	u16 block_size;
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct work_struct irq_work;
	int irq;
};

#define MLXSW_I2C_READ_MSG(_client, _addr_buf, _buf, _len) {	\
	{ .addr = (_client)->addr,				\
	  .buf = (_addr_buf),					\
	  .len = MLXSW_I2C_ADDR_BUF_SIZE,			\
	  .flags = 0 },						\
	{ .addr = (_client)->addr,				\
	  .buf = (_buf),					\
	  .len = (_len),					\
	  .flags = I2C_M_RD } }

#define MLXSW_I2C_WRITE_MSG(_client, _buf, _len)		\
	{ .addr = (_client)->addr,				\
	  .buf = (u8 *)(_buf),					\
	  .len = (_len),					\
	  .flags = 0 }

/* Routine converts in and out mail boxes offset and size. */
static inline void
mlxsw_i2c_convert_mbox(struct mlxsw_i2c *mlxsw_i2c, u8 *buf)
{
	u32 tmp;

	/* Local in/out mailboxes: 20 bits for offset, 12 for size */
	tmp = be32_to_cpup((__be32 *) buf);
	mlxsw_i2c->cmd.mb_off_in = tmp &
				   GENMASK(MLXSW_I2C_MBOX_OFFSET_BITS - 1, 0);
	mlxsw_i2c->cmd.mb_size_in = (tmp & GENMASK(31,
					MLXSW_I2C_MBOX_OFFSET_BITS)) >>
					MLXSW_I2C_MBOX_OFFSET_BITS;

	tmp = be32_to_cpup((__be32 *) (buf + MLXSW_I2C_ADDR_WIDTH));
	mlxsw_i2c->cmd.mb_off_out = tmp &
				    GENMASK(MLXSW_I2C_MBOX_OFFSET_BITS - 1, 0);
	mlxsw_i2c->cmd.mb_size_out = (tmp & GENMASK(31,
					MLXSW_I2C_MBOX_OFFSET_BITS)) >>
					MLXSW_I2C_MBOX_OFFSET_BITS;
}

/* Routine obtains register size from mail box buffer. */
static inline int mlxsw_i2c_get_reg_size(u8 *in_mbox)
{
	u16  tmp = be16_to_cpup((__be16 *) (in_mbox + MLXSW_I2C_TLV_HDR_SIZE));

	return (tmp & 0x7ff) * 4 + MLXSW_I2C_TLV_HDR_SIZE;
}

/* Routine sets I2C device internal offset in the transaction buffer. */
static inline void mlxsw_i2c_set_slave_addr(u8 *buf, u32 off)
{
	__be32 *val = (__be32 *) buf;

	*val = htonl(off);
}

/* Routine waits until go bit is cleared. */
static int mlxsw_i2c_wait_go_bit(struct i2c_client *client,
				 struct mlxsw_i2c *mlxsw_i2c, u8 *p_status)
{
	u8 addr_buf[MLXSW_I2C_ADDR_BUF_SIZE];
	u8 buf[MLXSW_I2C_READ_SEMA_SIZE];
	int len = MLXSW_I2C_READ_SEMA_SIZE;
	struct i2c_msg read_sema[] =
		MLXSW_I2C_READ_MSG(client, addr_buf, buf, len);
	bool wait_done = false;
	unsigned long end;
	int i = 0, err;

	mlxsw_i2c_set_slave_addr(addr_buf, MLXSW_I2C_CIR2_OFF_STATUS);

	end = jiffies + msecs_to_jiffies(MLXSW_I2C_TIMEOUT_MSECS);
	do {
		u32 ctrl;

		err = i2c_transfer(client->adapter, read_sema,
				   ARRAY_SIZE(read_sema));

		ctrl = be32_to_cpu(*(__be32 *) buf);
		if (err == ARRAY_SIZE(read_sema)) {
			if (!(ctrl & MLXSW_I2C_GO_BIT)) {
				wait_done = true;
				*p_status = ctrl >>
					    MLXSW_I2C_CIR_CTRL_STATUS_SHIFT;
				break;
			}
		}
		cond_resched();
	} while ((time_before(jiffies, end)) || (i++ < MLXSW_I2C_RETRY));

	if (wait_done) {
		if (*p_status)
			err = -EIO;
	} else {
		return -ETIMEDOUT;
	}

	return err > 0 ? 0 : err;
}

/* Routine posts a command to ASIC through mail box. */
static int mlxsw_i2c_write_cmd(struct i2c_client *client,
			       struct mlxsw_i2c *mlxsw_i2c,
			       int immediate)
{
	__be32 push_cmd_buf[MLXSW_I2C_PUSH_CMD_SIZE / 4] = {
		0, cpu_to_be32(MLXSW_I2C_PUSH_IMM_CMD)
	};
	__be32 prep_cmd_buf[MLXSW_I2C_PREP_SIZE / 4] = {
		0, 0, 0, 0, 0, 0,
		cpu_to_be32(client->adapter->nr & 0xffff),
		cpu_to_be32(MLXSW_I2C_SET_IMM_CMD)
	};
	struct i2c_msg push_cmd =
		MLXSW_I2C_WRITE_MSG(client, push_cmd_buf,
				    MLXSW_I2C_PUSH_CMD_SIZE);
	struct i2c_msg prep_cmd =
		MLXSW_I2C_WRITE_MSG(client, prep_cmd_buf, MLXSW_I2C_PREP_SIZE);
	int err;

	if (!immediate) {
		push_cmd_buf[1] = cpu_to_be32(MLXSW_I2C_PUSH_CMD);
		prep_cmd_buf[7] = cpu_to_be32(MLXSW_I2C_SET_CMD);
	}
	mlxsw_i2c_set_slave_addr((u8 *)prep_cmd_buf,
				 MLXSW_I2C_CIR2_BASE);
	mlxsw_i2c_set_slave_addr((u8 *)push_cmd_buf,
				 MLXSW_I2C_CIR2_OFF_STATUS);

	/* Prepare Command Interface Register for transaction */
	err = i2c_transfer(client->adapter, &prep_cmd, 1);
	if (err < 0)
		return err;
	else if (err != 1)
		return -EIO;

	/* Write out Command Interface Register GO bit to push transaction */
	err = i2c_transfer(client->adapter, &push_cmd, 1);
	if (err < 0)
		return err;
	else if (err != 1)
		return -EIO;

	return 0;
}

/* Routine posts initialization command to ASIC through mail box. */
static int
mlxsw_i2c_write_init_cmd(struct i2c_client *client,
			 struct mlxsw_i2c *mlxsw_i2c, u16 opcode, u32 in_mod)
{
	__be32 push_cmd_buf[MLXSW_I2C_PUSH_CMD_SIZE / 4] = {
		0, cpu_to_be32(MLXSW_I2C_PUSH_EVENT_CMD)
	};
	__be32 prep_cmd_buf[MLXSW_I2C_PREP_SIZE / 4] = {
		0, 0, 0, 0, 0, 0,
		cpu_to_be32(client->adapter->nr & 0xffff),
		cpu_to_be32(MLXSW_I2C_SET_EVENT_CMD)
	};
	struct i2c_msg push_cmd =
		MLXSW_I2C_WRITE_MSG(client, push_cmd_buf,
				    MLXSW_I2C_PUSH_CMD_SIZE);
	struct i2c_msg prep_cmd =
		MLXSW_I2C_WRITE_MSG(client, prep_cmd_buf, MLXSW_I2C_PREP_SIZE);
	u8 status;
	int err;

	push_cmd_buf[1] = cpu_to_be32(MLXSW_I2C_PUSH_EVENT_CMD | opcode);
	prep_cmd_buf[3] = cpu_to_be32(in_mod);
	prep_cmd_buf[7] = cpu_to_be32(MLXSW_I2C_GO_BIT | opcode);
	mlxsw_i2c_set_slave_addr((u8 *)prep_cmd_buf,
				 MLXSW_I2C_CIR2_BASE);
	mlxsw_i2c_set_slave_addr((u8 *)push_cmd_buf,
				 MLXSW_I2C_CIR2_OFF_STATUS);

	/* Prepare Command Interface Register for transaction */
	err = i2c_transfer(client->adapter, &prep_cmd, 1);
	if (err < 0)
		return err;
	else if (err != 1)
		return -EIO;

	/* Write out Command Interface Register GO bit to push transaction */
	err = i2c_transfer(client->adapter, &push_cmd, 1);
	if (err < 0)
		return err;
	else if (err != 1)
		return -EIO;

	/* Wait until go bit is cleared. */
	err = mlxsw_i2c_wait_go_bit(client, mlxsw_i2c, &status);
	if (err) {
		dev_err(&client->dev, "HW semaphore is not released");
		return err;
	}

	/* Validate transaction completion status. */
	if (status) {
		dev_err(&client->dev, "Bad transaction completion status %x\n",
			status);
		return -EIO;
	}

	return 0;
}

/* Routine obtains mail box offsets from ASIC register space. */
static int mlxsw_i2c_get_mbox(struct i2c_client *client,
			      struct mlxsw_i2c *mlxsw_i2c)
{
	u8 addr_buf[MLXSW_I2C_ADDR_BUF_SIZE];
	u8 buf[MLXSW_I2C_MBOX_SIZE];
	struct i2c_msg mbox_cmd[] =
		MLXSW_I2C_READ_MSG(client, addr_buf, buf, MLXSW_I2C_MBOX_SIZE);
	int err;

	/* Read mail boxes offsets. */
	mlxsw_i2c_set_slave_addr(addr_buf, MLXSW_I2C_CIR2_BASE);
	err = i2c_transfer(client->adapter, mbox_cmd, 2);
	if (err != 2) {
		dev_err(&client->dev, "Could not obtain mail boxes\n");
		if (!err)
			return -EIO;
		else
			return err;
	}

	/* Convert mail boxes. */
	mlxsw_i2c_convert_mbox(mlxsw_i2c, &buf[MLXSW_I2C_MBOX_OUT_PARAM_OFF]);

	return err;
}

/* Routine sends I2C write transaction to ASIC device. */
static int
mlxsw_i2c_write(struct device *dev, size_t in_mbox_size, u8 *in_mbox, int num,
		u8 *p_status)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mlxsw_i2c *mlxsw_i2c = i2c_get_clientdata(client);
	unsigned long timeout = msecs_to_jiffies(MLXSW_I2C_TIMEOUT_MSECS);
	int off = mlxsw_i2c->cmd.mb_off_in, chunk_size, i, j;
	unsigned long end;
	u8 *tran_buf;
	struct i2c_msg write_tran =
		MLXSW_I2C_WRITE_MSG(client, NULL, MLXSW_I2C_PUSH_CMD_SIZE);
	int err;

	tran_buf = kmalloc(mlxsw_i2c->block_size + MLXSW_I2C_ADDR_BUF_SIZE,
			   GFP_KERNEL);
	if (!tran_buf)
		return -ENOMEM;

	write_tran.buf = tran_buf;
	for (i = 0; i < num; i++) {
		chunk_size = (in_mbox_size > mlxsw_i2c->block_size) ?
			     mlxsw_i2c->block_size : in_mbox_size;
		write_tran.len = MLXSW_I2C_ADDR_WIDTH + chunk_size;
		mlxsw_i2c_set_slave_addr(tran_buf, off);
		memcpy(&tran_buf[MLXSW_I2C_ADDR_BUF_SIZE], in_mbox +
		       mlxsw_i2c->block_size * i, chunk_size);

		j = 0;
		end = jiffies + timeout;
		do {
			err = i2c_transfer(client->adapter, &write_tran, 1);
			if (err == 1)
				break;

			cond_resched();
		} while ((time_before(jiffies, end)) ||
			 (j++ < MLXSW_I2C_RETRY));

		if (err != 1) {
			if (!err) {
				err = -EIO;
				goto mlxsw_i2c_write_exit;
			}
		}

		off += chunk_size;
		in_mbox_size -= chunk_size;
	}

	/* Prepare and write out Command Interface Register for transaction. */
	err = mlxsw_i2c_write_cmd(client, mlxsw_i2c, 0);
	if (err) {
		dev_err(&client->dev, "Could not start transaction");
		err = -EIO;
		goto mlxsw_i2c_write_exit;
	}

	/* Wait until go bit is cleared. */
	err = mlxsw_i2c_wait_go_bit(client, mlxsw_i2c, p_status);
	if (err) {
		dev_err(&client->dev, "HW semaphore is not released");
		goto mlxsw_i2c_write_exit;
	}

	/* Validate transaction completion status. */
	if (*p_status) {
		dev_err(&client->dev, "Bad transaction completion status %x\n",
			*p_status);
		err = -EIO;
	}

mlxsw_i2c_write_exit:
	kfree(tran_buf);
	return err;
}

/* Routine executes I2C command. */
static int
mlxsw_i2c_cmd(struct device *dev, u16 opcode, u32 in_mod, size_t in_mbox_size,
	      u8 *in_mbox, size_t out_mbox_size, u8 *out_mbox, u8 *status)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mlxsw_i2c *mlxsw_i2c = i2c_get_clientdata(client);
	unsigned long timeout = msecs_to_jiffies(MLXSW_I2C_TIMEOUT_MSECS);
	u8 tran_buf[MLXSW_I2C_ADDR_BUF_SIZE];
	int num, chunk_size, reg_size, i, j;
	int off = mlxsw_i2c->cmd.mb_off_out;
	unsigned long end;
	struct i2c_msg read_tran[] =
		MLXSW_I2C_READ_MSG(client, tran_buf, NULL, 0);
	int err;

	WARN_ON(in_mbox_size % sizeof(u32) || out_mbox_size % sizeof(u32));

	if (in_mbox) {
		reg_size = mlxsw_i2c_get_reg_size(in_mbox);
		num = DIV_ROUND_UP(reg_size, mlxsw_i2c->block_size);

		if (mutex_lock_interruptible(&mlxsw_i2c->cmd.lock) < 0) {
			dev_err(&client->dev, "Could not acquire lock");
			return -EINVAL;
		}

		err = mlxsw_i2c_write(dev, reg_size, in_mbox, num, status);
		if (err)
			goto cmd_fail;

		/* No out mailbox is case of write transaction. */
		if (!out_mbox) {
			mutex_unlock(&mlxsw_i2c->cmd.lock);
			return 0;
		}
	} else {
		/* No input mailbox is case of initialization query command. */
		reg_size = MLXSW_I2C_MAX_DATA_SIZE;
		num = DIV_ROUND_UP(reg_size, mlxsw_i2c->block_size);

		if (mutex_lock_interruptible(&mlxsw_i2c->cmd.lock) < 0) {
			dev_err(&client->dev, "Could not acquire lock");
			return -EINVAL;
		}

		err = mlxsw_i2c_write_init_cmd(client, mlxsw_i2c, opcode,
					       in_mod);
		if (err)
			goto cmd_fail;
	}

	/* Send read transaction to get output mailbox content. */
	read_tran[1].buf = out_mbox;
	for (i = 0; i < num; i++) {
		chunk_size = (reg_size > mlxsw_i2c->block_size) ?
			     mlxsw_i2c->block_size : reg_size;
		read_tran[1].len = chunk_size;
		mlxsw_i2c_set_slave_addr(tran_buf, off);

		j = 0;
		end = jiffies + timeout;
		do {
			err = i2c_transfer(client->adapter, read_tran,
					   ARRAY_SIZE(read_tran));
			if (err == ARRAY_SIZE(read_tran))
				break;

			cond_resched();
		} while ((time_before(jiffies, end)) ||
			 (j++ < MLXSW_I2C_RETRY));

		if (err != ARRAY_SIZE(read_tran)) {
			if (!err)
				err = -EIO;

			goto cmd_fail;
		}

		off += chunk_size;
		reg_size -= chunk_size;
		read_tran[1].buf += chunk_size;
	}

	mutex_unlock(&mlxsw_i2c->cmd.lock);

	return 0;

cmd_fail:
	mutex_unlock(&mlxsw_i2c->cmd.lock);
	return err;
}

static int mlxsw_i2c_cmd_exec(void *bus_priv, u16 opcode, u8 opcode_mod,
			      u32 in_mod, bool out_mbox_direct,
			      char *in_mbox, size_t in_mbox_size,
			      char *out_mbox, size_t out_mbox_size,
			      u8 *status)
{
	struct mlxsw_i2c *mlxsw_i2c = bus_priv;

	return mlxsw_i2c_cmd(mlxsw_i2c->dev, opcode, in_mod, in_mbox_size,
			     in_mbox, out_mbox_size, out_mbox, status);
}

static bool mlxsw_i2c_skb_transmit_busy(void *bus_priv,
					const struct mlxsw_tx_info *tx_info)
{
	return false;
}

static int mlxsw_i2c_skb_transmit(void *bus_priv, struct sk_buff *skb,
				  const struct mlxsw_txhdr_info *txhdr_info)
{
	return 0;
}

static int
mlxsw_i2c_init(void *bus_priv, struct mlxsw_core *mlxsw_core,
	       const struct mlxsw_config_profile *profile,
	       struct mlxsw_res *res)
{
	struct mlxsw_i2c *mlxsw_i2c = bus_priv;
	char *mbox;
	int err;

	mlxsw_i2c->core = mlxsw_core;

	mbox = mlxsw_cmd_mbox_alloc();
	if (!mbox)
		return -ENOMEM;

	err = mlxsw_cmd_query_fw(mlxsw_core, mbox);
	if (err)
		goto mbox_put;

	mlxsw_i2c->bus_info.fw_rev.major =
		mlxsw_cmd_mbox_query_fw_fw_rev_major_get(mbox);
	mlxsw_i2c->bus_info.fw_rev.minor =
		mlxsw_cmd_mbox_query_fw_fw_rev_minor_get(mbox);
	mlxsw_i2c->bus_info.fw_rev.subminor =
		mlxsw_cmd_mbox_query_fw_fw_rev_subminor_get(mbox);

	err = mlxsw_core_resources_query(mlxsw_core, mbox, res);

mbox_put:
	mlxsw_cmd_mbox_free(mbox);
	return err;
}

static void mlxsw_i2c_fini(void *bus_priv)
{
	struct mlxsw_i2c *mlxsw_i2c = bus_priv;

	mlxsw_i2c->core = NULL;
}

static void mlxsw_i2c_work_handler(struct work_struct *work)
{
	struct mlxsw_i2c *mlxsw_i2c;

	mlxsw_i2c = container_of(work, struct mlxsw_i2c, irq_work);
	mlxsw_core_irq_event_handlers_call(mlxsw_i2c->core);
}

static irqreturn_t mlxsw_i2c_irq_handler(int irq, void *dev)
{
	struct mlxsw_i2c *mlxsw_i2c = dev;

	mlxsw_core_schedule_work(&mlxsw_i2c->irq_work);

	/* Interrupt handler shares IRQ line with 'main' interrupt handler.
	 * Return here IRQ_NONE, while main handler will return IRQ_HANDLED.
	 */
	return IRQ_NONE;
}

static int mlxsw_i2c_irq_init(struct mlxsw_i2c *mlxsw_i2c, u8 addr)
{
	int err;

	/* Initialize interrupt handler if system hotplug driver is reachable,
	 * otherwise interrupt line is not enabled and interrupts will not be
	 * raised to CPU. Also request_irq() call will be not valid.
	 */
	if (!IS_REACHABLE(CONFIG_MLXREG_HOTPLUG))
		return 0;

	/* Set default interrupt line. */
	if (mlxsw_i2c->pdata && mlxsw_i2c->pdata->irq)
		mlxsw_i2c->irq = mlxsw_i2c->pdata->irq;
	else if (addr == MLXSW_FAST_I2C_SLAVE)
		mlxsw_i2c->irq = MLXSW_I2C_DEFAULT_IRQ;

	if (!mlxsw_i2c->irq)
		return 0;

	INIT_WORK(&mlxsw_i2c->irq_work, mlxsw_i2c_work_handler);
	err = request_irq(mlxsw_i2c->irq, mlxsw_i2c_irq_handler,
			  IRQF_TRIGGER_FALLING | IRQF_SHARED, "mlxsw-i2c",
			  mlxsw_i2c);
	if (err) {
		dev_err(mlxsw_i2c->bus_info.dev, "Failed to request irq: %d\n",
			err);
		return err;
	}

	return 0;
}

static void mlxsw_i2c_irq_fini(struct mlxsw_i2c *mlxsw_i2c)
{
	if (!IS_REACHABLE(CONFIG_MLXREG_HOTPLUG) || !mlxsw_i2c->irq)
		return;
	cancel_work_sync(&mlxsw_i2c->irq_work);
	free_irq(mlxsw_i2c->irq, mlxsw_i2c);
}

static const struct mlxsw_bus mlxsw_i2c_bus = {
	.kind			= "i2c",
	.init			= mlxsw_i2c_init,
	.fini			= mlxsw_i2c_fini,
	.skb_transmit_busy	= mlxsw_i2c_skb_transmit_busy,
	.skb_transmit		= mlxsw_i2c_skb_transmit,
	.cmd_exec		= mlxsw_i2c_cmd_exec,
};

static int mlxsw_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	const struct i2c_adapter_quirks *quirks = client->adapter->quirks;
	struct mlxsw_i2c *mlxsw_i2c;
	u8 status;
	int err;

	mlxsw_i2c = devm_kzalloc(&client->dev, sizeof(*mlxsw_i2c), GFP_KERNEL);
	if (!mlxsw_i2c)
		return -ENOMEM;

	if (quirks) {
		if ((quirks->max_read_len &&
		     quirks->max_read_len < MLXSW_I2C_BLK_DEF) ||
		    (quirks->max_write_len &&
		     quirks->max_write_len < MLXSW_I2C_BLK_DEF)) {
			dev_err(&client->dev, "Insufficient transaction buffer length\n");
			return -EOPNOTSUPP;
		}

		mlxsw_i2c->block_size = min_t(u16, MLXSW_I2C_BLK_MAX,
					      min_t(u16, quirks->max_read_len,
						    quirks->max_write_len));
	} else {
		mlxsw_i2c->block_size = MLXSW_I2C_BLK_DEF;
	}

	i2c_set_clientdata(client, mlxsw_i2c);
	mutex_init(&mlxsw_i2c->cmd.lock);

	/* In order to use mailboxes through the i2c, special area is reserved
	 * on the i2c address space that can be used for input and output
	 * mailboxes. Such mailboxes are called local mailboxes. When using a
	 * local mailbox, software should specify 0 as the Input/Output
	 * parameters. The location of the Local Mailbox addresses on the i2c
	 * space can be retrieved through the QUERY_FW command.
	 * For this purpose QUERY_FW is to be issued with opcode modifier equal
	 * 0x01. For such command the output parameter is an immediate value.
	 * Here QUERY_FW command is invoked for ASIC probing and for getting
	 * local mailboxes addresses from immedate output parameters.
	 */

	/* Prepare and write out Command Interface Register for transaction */
	err = mlxsw_i2c_write_cmd(client, mlxsw_i2c, 1);
	if (err) {
		dev_err(&client->dev, "Could not start transaction");
		goto errout;
	}

	/* Wait until go bit is cleared. */
	err = mlxsw_i2c_wait_go_bit(client, mlxsw_i2c, &status);
	if (err) {
		dev_err(&client->dev, "HW semaphore is not released");
		goto errout;
	}

	/* Validate transaction completion status. */
	if (status) {
		dev_err(&client->dev, "Bad transaction completion status %x\n",
			status);
		err = -EIO;
		goto errout;
	}

	/* Get mailbox offsets. */
	err = mlxsw_i2c_get_mbox(client, mlxsw_i2c);
	if (err < 0) {
		dev_err(&client->dev, "Fail to get mailboxes\n");
		goto errout;
	}

	dev_info(&client->dev, "%s mb size=%x off=0x%08x out mb size=%x off=0x%08x\n",
		 id->name, mlxsw_i2c->cmd.mb_size_in,
		 mlxsw_i2c->cmd.mb_off_in, mlxsw_i2c->cmd.mb_size_out,
		 mlxsw_i2c->cmd.mb_off_out);

	/* Register device bus. */
	mlxsw_i2c->bus_info.device_kind = id->name;
	mlxsw_i2c->bus_info.device_name = client->name;
	mlxsw_i2c->bus_info.dev = &client->dev;
	mlxsw_i2c->bus_info.low_frequency = true;
	mlxsw_i2c->dev = &client->dev;
	mlxsw_i2c->pdata = client->dev.platform_data;

	err = mlxsw_i2c_irq_init(mlxsw_i2c, client->addr);
	if (err)
		goto errout;

	err = mlxsw_core_bus_device_register(&mlxsw_i2c->bus_info,
					     &mlxsw_i2c_bus, mlxsw_i2c, false,
					     NULL, NULL);
	if (err) {
		dev_err(&client->dev, "Fail to register core bus\n");
		goto err_bus_device_register;
	}

	return 0;

err_bus_device_register:
	mlxsw_i2c_irq_fini(mlxsw_i2c);
errout:
	mutex_destroy(&mlxsw_i2c->cmd.lock);
	i2c_set_clientdata(client, NULL);

	return err;
}

static void mlxsw_i2c_remove(struct i2c_client *client)
{
	struct mlxsw_i2c *mlxsw_i2c = i2c_get_clientdata(client);

	mlxsw_core_bus_device_unregister(mlxsw_i2c->core, false);
	mlxsw_i2c_irq_fini(mlxsw_i2c);
	mutex_destroy(&mlxsw_i2c->cmd.lock);
}

int mlxsw_i2c_driver_register(struct i2c_driver *i2c_driver)
{
	i2c_driver->probe = mlxsw_i2c_probe;
	i2c_driver->remove = mlxsw_i2c_remove;
	return i2c_add_driver(i2c_driver);
}
EXPORT_SYMBOL(mlxsw_i2c_driver_register);

void mlxsw_i2c_driver_unregister(struct i2c_driver *i2c_driver)
{
	i2c_del_driver(i2c_driver);
}
EXPORT_SYMBOL(mlxsw_i2c_driver_unregister);

MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("Mellanox switch I2C interface driver");
MODULE_LICENSE("Dual BSD/GPL");
