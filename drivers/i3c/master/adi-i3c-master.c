// SPDX-License-Identifier: GPL-2.0-only
/*
 * I3C Controller driver
 * Copyright 2025 Analog Devices Inc.
 * Author: Jorge Marques <jorge.marques@analog.com>
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/adi-axi-common.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "../internals.h"

#define ADI_MAX_DEVS			16
#define ADI_HAS_MDB_FROM_BCR(x)		(FIELD_GET(BIT(2), (x)))

#define REG_ENABLE			0x040

#define REG_PID_L			0x054
#define REG_PID_H			0x058
#define REG_DCR_BCR_DA			0x05c
#define   REG_DCR_BCR_DA_GET_DA(x)	FIELD_GET(GENMASK(22, 16), (x))
#define   REG_DCR_BCR_DA_GET_BCR(x)	FIELD_GET(GENMASK(15, 8), (x))
#define   REG_DCR_BCR_DA_GET_DCR(x)	FIELD_GET(GENMASK(7, 0), (x))

#define REG_IRQ_MASK			0x080
#define REG_IRQ_PENDING			0x084
#define   REG_IRQ_PENDING_DAA		BIT(7)
#define   REG_IRQ_PENDING_IBI		BIT(6)
#define   REG_IRQ_PENDING_CMDR		BIT(5)

#define REG_CMD_FIFO			0x0d4
#define   REG_CMD_FIFO_0_IS_CCC		BIT(22)
#define   REG_CMD_FIFO_0_BCAST		BIT(21)
#define   REG_CMD_FIFO_0_SR		BIT(20)
#define   REG_CMD_FIFO_0_LEN(l)		FIELD_PREP(GENMASK(19, 8), (l))
#define   REG_CMD_FIFO_0_DEV_ADDR(a)	FIELD_PREP(GENMASK(7, 1), (a))
#define   REG_CMD_FIFO_0_RNW		BIT(0)
#define   REG_CMD_FIFO_1_CCC(id)	FIELD_PREP(GENMASK(7, 0), (id))

#define REG_CMD_FIFO_ROOM		0x0c0
#define REG_CMDR_FIFO			0x0d8
#define   REG_CMDR_FIFO_UDA_ERROR	8
#define   REG_CMDR_FIFO_NACK_RESP	6
#define   REG_CMDR_FIFO_CE2_ERROR	4
#define   REG_CMDR_FIFO_CE0_ERROR	1
#define   REG_CMDR_FIFO_NO_ERROR	0
#define   REG_CMDR_FIFO_ERROR(x)	FIELD_GET(GENMASK(23, 20), (x))
#define   REG_CMDR_FIFO_XFER_BYTES(x)	FIELD_GET(GENMASK(19, 8), (x))

#define REG_SDO_FIFO			0x0dc
#define REG_SDO_FIFO_ROOM		0x0c8
#define REG_SDI_FIFO			0x0e0
#define REG_IBI_FIFO			0x0e4
#define REG_FIFO_STATUS			0x0e8
#define   REG_FIFO_STATUS_CMDR_EMPTY	BIT(0)
#define   REG_FIFO_STATUS_IBI_EMPTY	BIT(1)

#define REG_OPS				0x100
#define   REG_OPS_PP_SG_MASK		GENMASK(6, 5)
#define   REG_OPS_SET_SG(x)		FIELD_PREP(REG_OPS_PP_SG_MASK, (x))

#define REG_IBI_CONFIG			0x140
#define   REG_IBI_CONFIG_ENABLE		BIT(0)
#define   REG_IBI_CONFIG_LISTEN		BIT(1)

#define REG_DEV_CHAR			0x180
#define   REG_DEV_CHAR_IS_I2C		BIT(0)
#define   REG_DEV_CHAR_IS_ATTACHED	BIT(1)
#define   REG_DEV_CHAR_BCR_IBI(x)	FIELD_PREP(GENMASK(3, 2), (x))
#define   REG_DEV_CHAR_WEN		BIT(8)
#define   REG_DEV_CHAR_ADDR(x)		FIELD_PREP(GENMASK(15, 9), (x))

enum speed_grade {PP_SG_UNSET, PP_SG_1MHZ, PP_SG_3MHZ, PP_SG_6MHZ, PP_SG_12MHZ};

struct adi_i3c_cmd {
	u32 cmd0;
	u32 cmd1;
	u32 tx_len;
	const void *tx_buf;
	u32 rx_len;
	void *rx_buf;
	u32 error;
};

struct adi_i3c_xfer {
	struct list_head node;
	struct completion comp;
	int ret;
	unsigned int ncmds;
	unsigned int ncmds_comp;
	struct adi_i3c_cmd cmds[] __counted_by(ncmds);
};

struct adi_i3c_master {
	struct i3c_master_controller base;
	u32 free_rr_slots;
	struct {
		unsigned int num_slots;
		struct i3c_dev_desc **slots;
		spinlock_t lock; /* Protect IBI slot access */
	} ibi;
	struct {
		struct list_head list;
		struct adi_i3c_xfer *cur;
		spinlock_t lock; /* Protect transfer */
	} xferqueue;
	void __iomem *regs;
	struct clk *clk;
	unsigned long i3c_scl_lim;
	struct {
		u8 addrs[ADI_MAX_DEVS];
		u8 index;
	} daa;
};

static inline struct adi_i3c_master *to_adi_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct adi_i3c_master, base);
}

static void adi_i3c_master_wr_to_tx_fifo(struct adi_i3c_master *master,
					 const u8 *buf, unsigned int nbytes)
{
	unsigned int n, m;

	n = readl(master->regs + REG_SDO_FIFO_ROOM);
	m = min(n, nbytes);
	i3c_writel_fifo(master->regs + REG_SDO_FIFO, buf, m);
}

static void adi_i3c_master_rd_from_rx_fifo(struct adi_i3c_master *master,
					   u8 *buf, unsigned int nbytes)
{
	i3c_readl_fifo(master->regs + REG_SDI_FIFO, buf, nbytes);
}

static bool adi_i3c_master_supports_ccc_cmd(struct i3c_master_controller *m,
					    const struct i3c_ccc_cmd *cmd)
{
	if (cmd->ndests > 1)
		return false;

	switch (cmd->id) {
	case I3C_CCC_ENEC(true):
	case I3C_CCC_ENEC(false):
	case I3C_CCC_DISEC(true):
	case I3C_CCC_DISEC(false):
	case I3C_CCC_RSTDAA(true):
	case I3C_CCC_RSTDAA(false):
	case I3C_CCC_ENTDAA:
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETHDRCAP:
		return true;
	default:
		break;
	}

	return false;
}

static int adi_i3c_master_disable(struct adi_i3c_master *master)
{
	writel(0, master->regs + REG_IBI_CONFIG);

	return 0;
}

static struct adi_i3c_xfer *adi_i3c_master_alloc_xfer(struct adi_i3c_master *master,
						      unsigned int ncmds)
{
	struct adi_i3c_xfer *xfer;

	xfer = kzalloc(struct_size(xfer, cmds, ncmds), GFP_KERNEL);
	if (!xfer)
		return NULL;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ncmds = ncmds;
	xfer->ret = -ETIMEDOUT;

	return xfer;
}

static void adi_i3c_master_start_xfer_locked(struct adi_i3c_master *master)
{
	struct adi_i3c_xfer *xfer = master->xferqueue.cur;
	unsigned int i, n, m;

	if (!xfer)
		return;

	for (i = 0; i < xfer->ncmds; i++) {
		struct adi_i3c_cmd *cmd = &xfer->cmds[i];

		if (!(cmd->cmd0 & REG_CMD_FIFO_0_RNW))
			adi_i3c_master_wr_to_tx_fifo(master, cmd->tx_buf, cmd->tx_len);
	}

	n = readl(master->regs + REG_CMD_FIFO_ROOM);
	for (i = 0; i < xfer->ncmds; i++) {
		struct adi_i3c_cmd *cmd = &xfer->cmds[i];

		m = cmd->cmd0 & REG_CMD_FIFO_0_IS_CCC ? 2 : 1;
		if (m > n)
			break;
		writel(cmd->cmd0, master->regs + REG_CMD_FIFO);
		if (cmd->cmd0 & REG_CMD_FIFO_0_IS_CCC)
			writel(cmd->cmd1, master->regs + REG_CMD_FIFO);
		n -= m;
	}
}

static void adi_i3c_master_end_xfer_locked(struct adi_i3c_master *master,
					   u32 pending)
{
	struct adi_i3c_xfer *xfer = master->xferqueue.cur;
	int i, ret = 0;

	if (!xfer)
		return;

	while (!(readl(master->regs + REG_FIFO_STATUS) & REG_FIFO_STATUS_CMDR_EMPTY)) {
		struct adi_i3c_cmd *cmd;
		u32 cmdr, rx_len;

		cmdr = readl(master->regs + REG_CMDR_FIFO);

		cmd = &xfer->cmds[xfer->ncmds_comp++];
		if (cmd->cmd0 & REG_CMD_FIFO_0_RNW) {
			rx_len = min_t(u32, REG_CMDR_FIFO_XFER_BYTES(cmdr), cmd->rx_len);
			adi_i3c_master_rd_from_rx_fifo(master, cmd->rx_buf, rx_len);
		}
		cmd->error = REG_CMDR_FIFO_ERROR(cmdr);
	}

	for (i = 0; i < xfer->ncmds_comp; i++) {
		switch (xfer->cmds[i].error) {
		case REG_CMDR_FIFO_NO_ERROR:
			break;

		case REG_CMDR_FIFO_CE0_ERROR:
		case REG_CMDR_FIFO_CE2_ERROR:
		case REG_CMDR_FIFO_NACK_RESP:
		case REG_CMDR_FIFO_UDA_ERROR:
			ret = -EIO;
			break;

		default:
			ret = -EINVAL;
			break;
		}
	}

	xfer->ret = ret;

	if (xfer->ncmds_comp != xfer->ncmds)
		return;

	complete(&xfer->comp);

	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct adi_i3c_xfer, node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;
	adi_i3c_master_start_xfer_locked(master);
}

static void adi_i3c_master_queue_xfer(struct adi_i3c_master *master,
				      struct adi_i3c_xfer *xfer)
{
	init_completion(&xfer->comp);
	guard(spinlock_irqsave)(&master->xferqueue.lock);
	if (master->xferqueue.cur) {
		list_add_tail(&xfer->node, &master->xferqueue.list);
	} else {
		master->xferqueue.cur = xfer;
		adi_i3c_master_start_xfer_locked(master);
	}
}

static void adi_i3c_master_unqueue_xfer(struct adi_i3c_master *master,
					struct adi_i3c_xfer *xfer)
{
	guard(spinlock_irqsave)(&master->xferqueue.lock);
	if (master->xferqueue.cur == xfer)
		master->xferqueue.cur = NULL;
	else
		list_del_init(&xfer->node);

	writel(0x01, master->regs + REG_ENABLE);
	writel(0x00, master->regs + REG_ENABLE);
	writel(REG_IRQ_PENDING_CMDR, master->regs + REG_IRQ_MASK);
}

static enum i3c_error_code adi_i3c_cmd_get_err(struct adi_i3c_cmd *cmd)
{
	switch (cmd->error) {
	case REG_CMDR_FIFO_CE0_ERROR:
		return I3C_ERROR_M0;

	case REG_CMDR_FIFO_CE2_ERROR:
	case REG_CMDR_FIFO_NACK_RESP:
		return I3C_ERROR_M2;

	default:
		break;
	}

	return I3C_ERROR_UNKNOWN;
}

static int adi_i3c_master_send_ccc_cmd(struct i3c_master_controller *m,
				       struct i3c_ccc_cmd *cmd)
{
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_xfer *xfer __free(kfree) = NULL;
	struct adi_i3c_cmd *ccmd;

	xfer = adi_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	ccmd = xfer->cmds;
	ccmd->cmd1 = REG_CMD_FIFO_1_CCC(cmd->id);
	ccmd->cmd0 = REG_CMD_FIFO_0_IS_CCC |
		     REG_CMD_FIFO_0_LEN(cmd->dests[0].payload.len);

	if (cmd->id & I3C_CCC_DIRECT)
		ccmd->cmd0 |= REG_CMD_FIFO_0_DEV_ADDR(cmd->dests[0].addr);

	if (cmd->rnw) {
		ccmd->cmd0 |= REG_CMD_FIFO_0_RNW;
		ccmd->rx_buf = cmd->dests[0].payload.data;
		ccmd->rx_len = cmd->dests[0].payload.len;
	} else {
		ccmd->tx_buf = cmd->dests[0].payload.data;
		ccmd->tx_len = cmd->dests[0].payload.len;
	}

	adi_i3c_master_queue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		adi_i3c_master_unqueue_xfer(master, xfer);

	cmd->err = adi_i3c_cmd_get_err(&xfer->cmds[0]);

	return 0;
}

static int adi_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				     struct i3c_priv_xfer *xfers,
				     int nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_xfer *xfer __free(kfree) = NULL;
	int i, ret;

	if (!nxfers)
		return 0;

	xfer = adi_i3c_master_alloc_xfer(master, nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		struct adi_i3c_cmd *ccmd = &xfer->cmds[i];

		ccmd->cmd0 = REG_CMD_FIFO_0_DEV_ADDR(dev->info.dyn_addr);

		if (xfers[i].rnw) {
			ccmd->cmd0 |= REG_CMD_FIFO_0_RNW;
			ccmd->rx_buf = xfers[i].data.in;
			ccmd->rx_len = xfers[i].len;
		} else {
			ccmd->tx_buf = xfers[i].data.out;
			ccmd->tx_len = xfers[i].len;
		}

		ccmd->cmd0 |= REG_CMD_FIFO_0_LEN(xfers[i].len);

		if (i < nxfers - 1)
			ccmd->cmd0 |= REG_CMD_FIFO_0_SR;

		if (!i)
			ccmd->cmd0 |= REG_CMD_FIFO_0_BCAST;
	}

	adi_i3c_master_queue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp,
					 msecs_to_jiffies(1000)))
		adi_i3c_master_unqueue_xfer(master, xfer);

	ret = xfer->ret;

	for (i = 0; i < nxfers; i++)
		xfers[i].err = adi_i3c_cmd_get_err(&xfer->cmds[i]);

	return ret;
}

struct adi_i3c_i2c_dev_data {
	struct i3c_generic_ibi_pool *ibi_pool;
	u16 id;
	s16 ibi;
};

static int adi_i3c_master_get_rr_slot(struct adi_i3c_master *master,
				      u8 dyn_addr)
{
	if (!master->free_rr_slots)
		return -ENOSPC;

	return ffs(master->free_rr_slots) - 1;
}

static int adi_i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev, u8 dyn_addr)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	u8 addr;

	addr = dev->info.dyn_addr ? dev->info.dyn_addr : dev->info.static_addr;

	writel(REG_DEV_CHAR_ADDR(dyn_addr), master->regs + REG_DEV_CHAR);
	writel((readl(master->regs + REG_DEV_CHAR) &
		~REG_DEV_CHAR_IS_ATTACHED) | REG_DEV_CHAR_WEN,
	       master->regs + REG_DEV_CHAR);

	writel(REG_DEV_CHAR_ADDR(addr), master->regs + REG_DEV_CHAR);
	writel(readl(master->regs + REG_DEV_CHAR) |
	       REG_DEV_CHAR_IS_ATTACHED | REG_DEV_CHAR_WEN,
	       master->regs + REG_DEV_CHAR);

	return 0;
}

static int adi_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_i2c_dev_data *data;
	int slot;
	u8 addr;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	slot = adi_i3c_master_get_rr_slot(master, dev->info.dyn_addr);
	if (slot < 0) {
		kfree(data);
		return slot;
	}

	data->id = slot;
	i3c_dev_set_master_data(dev, data);
	master->free_rr_slots &= ~BIT(slot);

	addr = dev->info.dyn_addr ? dev->info.dyn_addr : dev->info.static_addr;

	writel(REG_DEV_CHAR_ADDR(addr), master->regs + REG_DEV_CHAR);
	writel(readl(master->regs + REG_DEV_CHAR) |
	       REG_DEV_CHAR_IS_ATTACHED | REG_DEV_CHAR_WEN,
	       master->regs + REG_DEV_CHAR);

	return 0;
}

static void adi_i3c_master_sync_dev_char(struct i3c_master_controller *m)
{
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct i3c_dev_desc *i3cdev;
	u32 bcr_ibi;
	u8 addr;

	i3c_bus_for_each_i3cdev(&m->bus, i3cdev) {
		addr = i3cdev->info.dyn_addr ?
		       i3cdev->info.dyn_addr : i3cdev->info.static_addr;
		writel(REG_DEV_CHAR_ADDR(addr), master->regs + REG_DEV_CHAR);
		bcr_ibi = FIELD_GET(I3C_BCR_IBI_PAYLOAD | I3C_BCR_IBI_REQ_CAP, (i3cdev->info.bcr));
		writel(readl(master->regs + REG_DEV_CHAR) |
		       REG_DEV_CHAR_BCR_IBI(bcr_ibi) | REG_DEV_CHAR_WEN,
		       master->regs + REG_DEV_CHAR);
	}
}

static void adi_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	u8 addr;

	addr = dev->info.dyn_addr ? dev->info.dyn_addr : dev->info.static_addr;

	writel(REG_DEV_CHAR_ADDR(addr), master->regs + REG_DEV_CHAR);
	writel((readl(master->regs + REG_DEV_CHAR) &
		~REG_DEV_CHAR_IS_ATTACHED) | REG_DEV_CHAR_WEN,
	       master->regs + REG_DEV_CHAR);

	i3c_dev_set_master_data(dev, NULL);
	master->free_rr_slots |= BIT(data->id);
	kfree(data);
}

static int adi_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_i2c_dev_data *data;
	int slot;

	slot = adi_i3c_master_get_rr_slot(master, 0);
	if (slot < 0)
		return slot;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->id = slot;
	master->free_rr_slots &= ~BIT(slot);
	i2c_dev_set_master_data(dev, data);

	writel(REG_DEV_CHAR_ADDR(dev->addr) |
	       REG_DEV_CHAR_IS_I2C | REG_DEV_CHAR_IS_ATTACHED | REG_DEV_CHAR_WEN,
	       master->regs + REG_DEV_CHAR);

	return 0;
}

static void adi_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);

	writel(REG_DEV_CHAR_ADDR(dev->addr) |
	       REG_DEV_CHAR_IS_I2C | REG_DEV_CHAR_WEN,
	       master->regs + REG_DEV_CHAR);

	i2c_dev_set_master_data(dev, NULL);
	master->free_rr_slots |= BIT(data->id);
	kfree(data);
}

static void adi_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct adi_i3c_master *master = to_adi_i3c_master(m);

	adi_i3c_master_disable(master);
}

static void adi_i3c_master_upd_i3c_scl_lim(struct adi_i3c_master *master)
{
	struct i3c_master_controller *m = &master->base;
	struct i3c_bus *bus = i3c_master_get_bus(m);
	u8 i3c_scl_lim = 0;
	struct i3c_dev_desc *dev;
	u8 pp_sg;

	i3c_bus_for_each_i3cdev(bus, dev) {
		u8 max_fscl;

		max_fscl = max(I3C_CCC_MAX_SDR_FSCL(dev->info.max_read_ds),
			       I3C_CCC_MAX_SDR_FSCL(dev->info.max_write_ds));

		switch (max_fscl) {
		case I3C_SDR1_FSCL_8MHZ:
			max_fscl = PP_SG_6MHZ;
			break;
		case I3C_SDR2_FSCL_6MHZ:
			max_fscl = PP_SG_3MHZ;
			break;
		case I3C_SDR3_FSCL_4MHZ:
			max_fscl = PP_SG_3MHZ;
			break;
		case I3C_SDR4_FSCL_2MHZ:
			max_fscl = PP_SG_1MHZ;
			break;
		case I3C_SDR0_FSCL_MAX:
		default:
			max_fscl = PP_SG_12MHZ;
			break;
		}

		if (max_fscl &&
		    (i3c_scl_lim > max_fscl || !i3c_scl_lim))
			i3c_scl_lim = max_fscl;
	}

	if (!i3c_scl_lim)
		return;

	master->i3c_scl_lim = i3c_scl_lim - 1;

	pp_sg = readl(master->regs + REG_OPS) & ~REG_OPS_PP_SG_MASK;
	pp_sg |= REG_OPS_SET_SG(master->i3c_scl_lim);

	writel(pp_sg, master->regs + REG_OPS);
}

static void adi_i3c_master_get_features(struct adi_i3c_master *master,
					unsigned int slot,
					struct i3c_device_info *info)
{
	u32 buf;

	/* Dynamic address and PID are for identification only */
	memset(info, 0, sizeof(*info));
	buf = readl(master->regs + REG_DCR_BCR_DA);
	info->dyn_addr = REG_DCR_BCR_DA_GET_DA(buf);
	info->dcr = REG_DCR_BCR_DA_GET_DCR(buf);
	info->bcr = REG_DCR_BCR_DA_GET_BCR(buf);
	info->pid = readl(master->regs + REG_PID_L);
	info->pid |= (u64)readl(master->regs + REG_PID_H) << 32;
}

static int adi_i3c_master_do_daa(struct i3c_master_controller *m)
{
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	int ret, addr = 0;
	u32 irq_mask;

	for (u8 i = 0; i < ADI_MAX_DEVS; i++) {
		addr = i3c_master_get_free_addr(m, addr);
		if (addr < 0)
			return addr;
		master->daa.addrs[i] = addr;
	}

	irq_mask = readl(master->regs + REG_IRQ_MASK);
	writel(irq_mask | REG_IRQ_PENDING_DAA,
	       master->regs + REG_IRQ_MASK);

	master->daa.index = 0;
	ret = i3c_master_entdaa_locked(&master->base);

	writel(irq_mask, master->regs + REG_IRQ_MASK);

	/* DAA always finishes with CE2_ERROR or NACK_RESP */
	if (ret && ret != I3C_ERROR_M2)
		return ret;

	/* Add I3C devices discovered */
	for (u8 i = 0; i < master->daa.index; i++)
		i3c_master_add_i3c_dev_locked(m, master->daa.addrs[i]);
	/* Sync retrieved devs info with the IP */
	adi_i3c_master_sync_dev_char(m);

	i3c_master_defslvs_locked(&master->base);

	adi_i3c_master_upd_i3c_scl_lim(master);

	return 0;
}

static int adi_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct i3c_device_info info = { };
	int ret;

	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	adi_i3c_master_get_features(master, 0, &info);
	ret = i3c_master_set_info(&master->base, &info);
	if (ret)
		return ret;

	writel(REG_IBI_CONFIG_LISTEN,
	       master->regs + REG_IBI_CONFIG);

	return 0;
}

static void adi_i3c_master_handle_ibi(struct adi_i3c_master *master,
				      u32 raw)
{
	struct adi_i3c_i2c_dev_data *data;
	struct i3c_ibi_slot *slot;
	struct i3c_dev_desc *dev;
	u8 da, id, mdb, len;
	u8 *buf;

	da = FIELD_GET(GENMASK(23, 17), raw);
	mdb = FIELD_GET(GENMASK(15, 8), raw);
	for (id = 0; id < master->ibi.num_slots; id++) {
		if (master->ibi.slots[id] &&
		    master->ibi.slots[id]->info.dyn_addr == da)
			break;
	}

	if (id == master->ibi.num_slots)
		return;

	dev = master->ibi.slots[id];
	len = ADI_HAS_MDB_FROM_BCR(dev->info.bcr);
	data = i3c_dev_get_master_data(dev);

	guard(spinlock)(&master->ibi.lock);
	slot = i3c_generic_ibi_get_free_slot(data->ibi_pool);
	if (!slot)
		return;

	slot->len = len;
	buf = slot->data;
	buf[0] = mdb;
	i3c_master_queue_ibi(dev, slot);
}

static void adi_i3c_master_demux_ibis(struct adi_i3c_master *master)
{
	while (!(readl(master->regs + REG_FIFO_STATUS) & REG_FIFO_STATUS_IBI_EMPTY)) {
		u32 raw = readl(master->regs + REG_IBI_FIFO);

		adi_i3c_master_handle_ibi(master, raw);
	}
}

static void adi_i3c_master_handle_da_req(struct adi_i3c_master *master)
{
	u8 payload0[8];
	u32 addr;

	adi_i3c_master_rd_from_rx_fifo(master, payload0, 6);
	addr = master->daa.addrs[master->daa.index++];
	addr = (addr << 1) | (parity8(addr) ? 0 : 1);

	writel(addr, master->regs + REG_SDO_FIFO);
}

static irqreturn_t adi_i3c_master_irq(int irq, void *data)
{
	struct adi_i3c_master *master = data;
	u32 pending;

	pending = readl(master->regs + REG_IRQ_PENDING);
	writel(pending, master->regs + REG_IRQ_PENDING);
	if (pending & REG_IRQ_PENDING_CMDR) {
		scoped_guard(spinlock_irqsave, &master->xferqueue.lock) {
			adi_i3c_master_end_xfer_locked(master, pending);
		}
	}
	if (pending & REG_IRQ_PENDING_IBI)
		adi_i3c_master_demux_ibis(master);
	if (pending & REG_IRQ_PENDING_DAA)
		adi_i3c_master_handle_da_req(master);

	return IRQ_HANDLED;
}

static int adi_i3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				    struct i2c_msg *xfers,
				    int nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_xfer *xfer __free(kfree) = NULL;
	int i;

	if (!nxfers)
		return 0;
	for (i = 0; i < nxfers; i++) {
		if (xfers[i].flags & I2C_M_TEN)
			return -EOPNOTSUPP;
	}
	xfer = adi_i3c_master_alloc_xfer(master, nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		struct adi_i3c_cmd *ccmd = &xfer->cmds[i];

		ccmd->cmd0 = REG_CMD_FIFO_0_DEV_ADDR(xfers[i].addr);

		if (xfers[i].flags & I2C_M_RD) {
			ccmd->cmd0 |= REG_CMD_FIFO_0_RNW;
			ccmd->rx_buf = xfers[i].buf;
			ccmd->rx_len = xfers[i].len;
		} else {
			ccmd->tx_buf = xfers[i].buf;
			ccmd->tx_len = xfers[i].len;
		}

		ccmd->cmd0 |= REG_CMD_FIFO_0_LEN(xfers[i].len);
	}

	adi_i3c_master_queue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp,
					 m->i2c.timeout))
		adi_i3c_master_unqueue_xfer(master, xfer);

	return xfer->ret;
}

static int adi_i3c_master_disable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct i3c_dev_desc *i3cdev;
	u32 enabled = 0;
	int ret;

	ret = i3c_master_disec_locked(m, dev->info.dyn_addr,
				      I3C_CCC_EVENT_SIR);

	i3c_bus_for_each_i3cdev(&m->bus, i3cdev) {
		if (dev != i3cdev && i3cdev->ibi)
			enabled |= i3cdev->ibi->enabled;
	}
	if (!enabled) {
		writel(REG_IBI_CONFIG_LISTEN,
		       master->regs + REG_IBI_CONFIG);
		writel(readl(master->regs + REG_IRQ_MASK) & ~REG_IRQ_PENDING_IBI,
		       master->regs + REG_IRQ_MASK);
	}

	return ret;
}

static int adi_i3c_master_enable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);

	writel(REG_IBI_CONFIG_LISTEN | REG_IBI_CONFIG_ENABLE,
	       master->regs + REG_IBI_CONFIG);

	writel(readl(master->regs + REG_IRQ_MASK) | REG_IRQ_PENDING_IBI,
	       master->regs + REG_IRQ_MASK);

	return i3c_master_enec_locked(m, dev->info.dyn_addr,
				      I3C_CCC_EVENT_SIR);
}

static int adi_i3c_master_request_ibi(struct i3c_dev_desc *dev,
				      const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_i2c_dev_data *data;
	unsigned int i;

	data = i3c_dev_get_master_data(dev);
	data->ibi_pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(data->ibi_pool))
		return PTR_ERR(data->ibi_pool);

	scoped_guard(spinlock_irqsave, &master->ibi.lock) {
		for (i = 0; i < master->ibi.num_slots; i++) {
			if (!master->ibi.slots[i]) {
				data->ibi = i;
				master->ibi.slots[i] = dev;
				break;
			}
		}
	}

	if (i < master->ibi.num_slots)
		return 0;

	i3c_generic_ibi_free_pool(data->ibi_pool);
	data->ibi_pool = NULL;

	return -ENOSPC;
}

static void adi_i3c_master_free_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct adi_i3c_master *master = to_adi_i3c_master(m);
	struct adi_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	scoped_guard(spinlock_irqsave, &master->ibi.lock) {
		master->ibi.slots[data->ibi] = NULL;
	}

	i3c_generic_ibi_free_pool(data->ibi_pool);
}

static void adi_i3c_master_recycle_ibi_slot(struct i3c_dev_desc *dev,
					    struct i3c_ibi_slot *slot)
{
	struct adi_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c_generic_ibi_recycle_slot(data->ibi_pool, slot);
}

static const struct i3c_master_controller_ops adi_i3c_master_ops = {
	.bus_init = adi_i3c_master_bus_init,
	.bus_cleanup = adi_i3c_master_bus_cleanup,
	.attach_i3c_dev = adi_i3c_master_attach_i3c_dev,
	.reattach_i3c_dev = adi_i3c_master_reattach_i3c_dev,
	.detach_i3c_dev = adi_i3c_master_detach_i3c_dev,
	.attach_i2c_dev = adi_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = adi_i3c_master_detach_i2c_dev,
	.do_daa = adi_i3c_master_do_daa,
	.supports_ccc_cmd = adi_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = adi_i3c_master_send_ccc_cmd,
	.priv_xfers = adi_i3c_master_priv_xfers,
	.i2c_xfers = adi_i3c_master_i2c_xfers,
	.request_ibi = adi_i3c_master_request_ibi,
	.enable_ibi = adi_i3c_master_enable_ibi,
	.disable_ibi = adi_i3c_master_disable_ibi,
	.free_ibi = adi_i3c_master_free_ibi,
	.recycle_ibi_slot = adi_i3c_master_recycle_ibi_slot,
};

static const struct of_device_id adi_i3c_master_of_match[] = {
	{ .compatible = "adi,i3c-master-v1" },
	{}
};

static int adi_i3c_master_probe(struct platform_device *pdev)
{
	struct adi_i3c_master *master;
	struct clk_bulk_data *clk;
	unsigned int version;
	int ret, irq;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	ret = devm_clk_bulk_get_all_enabled(&pdev->dev, &clk);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to get clocks\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	version = readl(master->regs + ADI_AXI_REG_VERSION);
	if (ADI_AXI_PCORE_VER_MAJOR(version) != 1)
		dev_err_probe(&pdev->dev, -ENODEV, "Unsupported peripheral version %u.%u.%u\n",
			      ADI_AXI_PCORE_VER_MAJOR(version),
			      ADI_AXI_PCORE_VER_MINOR(version),
			      ADI_AXI_PCORE_VER_PATCH(version));

	writel(0x00, master->regs + REG_ENABLE);
	writel(0x00, master->regs + REG_IRQ_MASK);

	ret = devm_request_irq(&pdev->dev, irq, adi_i3c_master_irq, 0,
			       dev_name(&pdev->dev), master);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, master);

	master->free_rr_slots = GENMASK(ADI_MAX_DEVS, 1);

	writel(REG_IRQ_PENDING_CMDR, master->regs + REG_IRQ_MASK);

	spin_lock_init(&master->ibi.lock);
	master->ibi.num_slots = 15;
	master->ibi.slots = devm_kcalloc(&pdev->dev, master->ibi.num_slots,
					 sizeof(*master->ibi.slots),
					 GFP_KERNEL);
	if (!master->ibi.slots)
		return -ENOMEM;

	spin_lock_init(&master->xferqueue.lock);
	INIT_LIST_HEAD(&master->xferqueue.list);

	return i3c_master_register(&master->base, &pdev->dev,
				   &adi_i3c_master_ops, false);
}

static void adi_i3c_master_remove(struct platform_device *pdev)
{
	struct adi_i3c_master *master = platform_get_drvdata(pdev);

	writel(0xff, master->regs + REG_IRQ_PENDING);
	writel(0x00, master->regs + REG_IRQ_MASK);
	writel(0x01, master->regs + REG_ENABLE);

	i3c_master_unregister(&master->base);
}

static struct platform_driver adi_i3c_master = {
	.probe = adi_i3c_master_probe,
	.remove = adi_i3c_master_remove,
	.driver = {
		.name = "adi-i3c-master",
		.of_match_table = adi_i3c_master_of_match,
	},
};
module_platform_driver(adi_i3c_master);

MODULE_AUTHOR("Jorge Marques <jorge.marques@analog.com>");
MODULE_DESCRIPTION("Analog Devices I3C master driver");
MODULE_LICENSE("GPL");
