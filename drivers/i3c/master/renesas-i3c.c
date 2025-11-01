// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas I3C Controller driver
 * Copyright (C) 2023-25 Renesas Electronics Corp.
 *
 * TODO: IBI support, HotJoin support, Target support
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include "../internals.h"

#define PRTS			0x00
#define  PRTS_PRTMD		BIT(0)

#define BCTL			0x14
#define  BCTL_INCBA		BIT(0)
#define  BCTL_HJACKCTL		BIT(8)
#define  BCTL_ABT		BIT(29)
#define  BCTL_BUSE		BIT(31)

#define MSDVAD			0x18
#define  MSDVAD_MDYAD(x)	FIELD_PREP(GENMASK(21, 16), x)
#define  MSDVAD_MDYADV		BIT(31)

#define RSTCTL			0x20
#define  RSTCTL_RI3CRST		BIT(0)
#define  RSTCTL_INTLRST		BIT(16)

#define INST			0x30

#define IBINCTL			0x58
#define  IBINCTL_NRHJCTL	BIT(0)
#define  IBINCTL_NRMRCTL	BIT(1)
#define  IBINCTL_NRSIRCTL	BIT(3)

#define SVCTL			0x64

#define REFCKCTL		0x70
#define  REFCKCTL_IREFCKS(x)	FIELD_PREP(GENMASK(2, 0), x)

#define STDBR			0x74
#define  STDBR_SBRLO(cond, x)	FIELD_PREP(GENMASK(7, 0), (x) >> (cond))
#define  STDBR_SBRHO(cond, x)	FIELD_PREP(GENMASK(15, 8), (x) >> (cond))
#define  STDBR_SBRLP(x)		FIELD_PREP(GENMASK(21, 16), x)
#define  STDBR_SBRHP(x)		FIELD_PREP(GENMASK(29, 24), x)
#define  STDBR_DSBRPO		BIT(31)

#define EXTBR			0x78
#define  EXTBR_EBRLO(x)		FIELD_PREP(GENMASK(7, 0), x)
#define  EXTBR_EBRHO(x)		FIELD_PREP(GENMASK(15, 8), x)
#define  EXTBR_EBRLP(x)		FIELD_PREP(GENMASK(21, 16), x)
#define  EXTBR_EBRHP(x)		FIELD_PREP(GENMASK(29, 24), x)

#define BFRECDT			0x7c
#define  BFRECDT_FRECYC(x)	FIELD_PREP(GENMASK(8, 0), x)

#define BAVLCDT			0x80
#define  BAVLCDT_AVLCYC(x)	FIELD_PREP(GENMASK(8, 0), x)

#define BIDLCDT			0x84
#define  BIDLCDT_IDLCYC(x)	FIELD_PREP(GENMASK(17, 0), x)

#define ACKCTL			0xa0
#define  ACKCTL_ACKT		BIT(1)
#define  ACKCTL_ACKTWP		BIT(2)

#define SCSTRCTL		0xa4
#define  SCSTRCTL_ACKTWE	BIT(0)
#define  SCSTRCTL_RWE		BIT(1)

#define SCSTLCTL		0xb0

#define CNDCTL			0x140
#define  CNDCTL_STCND		BIT(0)
#define  CNDCTL_SRCND		BIT(1)
#define  CNDCTL_SPCND		BIT(2)

#define NCMDQP			0x150 /* Normal Command Queue */
#define  NCMDQP_CMD_ATTR(x)	FIELD_PREP(GENMASK(2, 0), x)
#define  NCMDQP_IMMED_XFER	0x01
#define  NCMDQP_ADDR_ASSGN	0x02
#define  NCMDQP_TID(x)		FIELD_PREP(GENMASK(6, 3), x)
#define  NCMDQP_CMD(x)		FIELD_PREP(GENMASK(14, 7), x)
#define  NCMDQP_CP		BIT(15)
#define  NCMDQP_DEV_INDEX(x)	FIELD_PREP(GENMASK(20, 16), x)
#define  NCMDQP_BYTE_CNT(x)	FIELD_PREP(GENMASK(25, 23), x)
#define  NCMDQP_DEV_COUNT(x)	FIELD_PREP(GENMASK(29, 26), x)
#define  NCMDQP_MODE(x)		FIELD_PREP(GENMASK(28, 26), x)
#define  NCMDQP_RNW(x)		FIELD_PREP(GENMASK(29, 29), x)
#define  NCMDQP_ROC		BIT(30)
#define  NCMDQP_TOC		BIT(31)
#define  NCMDQP_DATA_LENGTH(x)	FIELD_PREP(GENMASK(31, 16), x)

#define NRSPQP			0x154 /* Normal Respone Queue */
#define  NRSPQP_NO_ERROR			0
#define  NRSPQP_ERROR_CRC		1
#define  NRSPQP_ERROR_PARITY		2
#define  NRSPQP_ERROR_FRAME		3
#define  NRSPQP_ERROR_IBA_NACK		4
#define  NRSPQP_ERROR_ADDRESS_NACK	5
#define  NRSPQP_ERROR_OVER_UNDER_FLOW	6
#define  NRSPQP_ERROR_TRANSF_ABORT	8
#define  NRSPQP_ERROR_I2C_W_NACK_ERR	9
#define  NRSPQP_ERROR_UNSUPPORTED	10
#define  NRSPQP_DATA_LEN(x)	FIELD_GET(GENMASK(15, 0), x)
#define  NRSPQP_ERR_STATUS(x)	FIELD_GET(GENMASK(31, 28), x)

#define NTDTBP0			0x158 /* Normal Transfer Data Buffer */
#define  NTDTBP0_DEPTH		16

#define NQTHCTL			0x190
#define  NQTHCTL_CMDQTH(x)	FIELD_PREP(GENMASK(1, 0), x)
#define  NQTHCTL_IBIDSSZ(x)	FIELD_PREP(GENMASK(23, 16), x)

#define NTBTHCTL0		0x194

#define NRQTHCTL		0x1c0

#define BST			0x1d0
#define  BST_STCNDDF		BIT(0)
#define  BST_SPCNDDF		BIT(1)
#define  BST_NACKDF		BIT(4)
#define  BST_TENDF		BIT(8)

#define BSTE			0x1d4
#define  BSTE_STCNDDE		BIT(0)
#define  BSTE_SPCNDDE		BIT(1)
#define  BSTE_NACKDE		BIT(4)
#define  BSTE_TENDE		BIT(8)
#define  BSTE_ALE		BIT(16)
#define  BSTE_TODE		BIT(20)
#define  BSTE_WUCNDDE		BIT(24)
#define  BSTE_ALL_FLAG		(BSTE_STCNDDE | BSTE_SPCNDDE |\
				BSTE_NACKDE | BSTE_TENDE |\
				BSTE_ALE | BSTE_TODE | BSTE_WUCNDDE)

#define BIE			0x1d8
#define  BIE_STCNDDIE		BIT(0)
#define  BIE_SPCNDDIE		BIT(1)
#define  BIE_NACKDIE		BIT(4)
#define  BIE_TENDIE		BIT(8)

#define NTST			0x1e0
#define  NTST_TDBEF0		BIT(0)
#define  NTST_RDBFF0		BIT(1)
#define  NTST_CMDQEF		BIT(3)
#define  NTST_RSPQFF		BIT(4)
#define  NTST_TABTF		BIT(5)
#define  NTST_TEF		BIT(9)

#define NTSTE			0x1e4
#define  NTSTE_TDBEE0		BIT(0)
#define  NTSTE_RDBFE0		BIT(1)
#define  NTSTE_IBIQEFE		BIT(2)
#define  NTSTE_CMDQEE		BIT(3)
#define  NTSTE_RSPQFE		BIT(4)
#define  NTSTE_TABTE		BIT(5)
#define  NTSTE_TEE		BIT(9)
#define  NTSTE_RSQFE		BIT(20)
#define  NTSTE_ALL_FLAG		(NTSTE_TDBEE0 | NTSTE_RDBFE0 |\
				NTSTE_IBIQEFE | NTSTE_CMDQEE |\
				NTSTE_RSPQFE | NTSTE_TABTE |\
				NTSTE_TEE | NTSTE_RSQFE)

#define NTIE			0x1e8
#define  NTIE_TDBEIE0		BIT(0)
#define  NTIE_RDBFIE0		BIT(1)
#define  NTIE_IBIQEFIE		BIT(2)
#define  NTIE_RSPQFIE		BIT(4)
#define  NTIE_RSQFIE		BIT(20)

#define BCST			0x210
#define  BCST_BFREF		BIT(0)

#define DATBAS(x)		(0x224 + 0x8 * (x))
#define  DATBAS_DVSTAD(x)	FIELD_PREP(GENMASK(6, 0), x)
#define  DATBAS_DVDYAD(x)	FIELD_PREP(GENMASK(23, 16), x)

#define NDBSTLV0		0x398
#define  NDBSTLV0_RDBLV(x)	FIELD_GET(GENMASK(15, 8), x)

#define RENESAS_I3C_MAX_DEVS	8
#define I2C_INIT_MSG		-1

enum i3c_internal_state {
	I3C_INTERNAL_STATE_DISABLED,
	I3C_INTERNAL_STATE_CONTROLLER_IDLE,
	I3C_INTERNAL_STATE_CONTROLLER_ENTDAA,
	I3C_INTERNAL_STATE_CONTROLLER_SETDASA,
	I3C_INTERNAL_STATE_CONTROLLER_WRITE,
	I3C_INTERNAL_STATE_CONTROLLER_READ,
	I3C_INTERNAL_STATE_CONTROLLER_COMMAND_WRITE,
	I3C_INTERNAL_STATE_CONTROLLER_COMMAND_READ,
};

enum renesas_i3c_event {
	I3C_COMMAND_ADDRESS_ASSIGNMENT,
	I3C_WRITE,
	I3C_READ,
	I3C_COMMAND_WRITE,
	I3C_COMMAND_READ,
};

struct renesas_i3c_cmd {
	u32 cmd0;
	u32 len;
	const void *tx_buf;
	u32 tx_count;
	void *rx_buf;
	u32 rx_count;
	u32 err;
	u8 rnw;
	/* i2c xfer */
	int i2c_bytes_left;
	int i2c_is_last;
	u8 *i2c_buf;
	const struct i2c_msg *msg;
};

struct renesas_i3c_xfer {
	struct list_head node;
	struct completion comp;
	int ret;
	bool is_i2c_xfer;
	unsigned int ncmds;
	struct renesas_i3c_cmd cmds[] __counted_by(ncmds);
};

struct renesas_i3c_xferqueue {
	struct list_head list;
	struct renesas_i3c_xfer *cur;
	/* Lock for accessing the xfer queue */
	spinlock_t lock;
};

struct renesas_i3c {
	struct i3c_master_controller base;
	enum i3c_internal_state internal_state;
	u16 maxdevs;
	u32 free_pos;
	u32 i2c_STDBR;
	u32 i3c_STDBR;
	u8 addrs[RENESAS_I3C_MAX_DEVS];
	struct renesas_i3c_xferqueue xferqueue;
	void __iomem *regs;
	struct clk *tclk;
};

struct renesas_i3c_i2c_dev_data {
	u8 index;
};

struct renesas_i3c_irq_desc {
	const char *name;
	irq_handler_t isr;
	const char *desc;
};

struct renesas_i3c_config {
	unsigned int has_pclkrw:1;
};

static inline void renesas_i3c_reg_update(void __iomem *reg, u32 mask, u32 val)
{
	u32 data = readl(reg);

	data &= ~mask;
	data |= (val & mask);
	writel(data, reg);
}

static inline u32 renesas_readl(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static inline void renesas_writel(void __iomem *base, u32 reg, u32 val)
{
	writel(val, base + reg);
}

static void renesas_set_bit(void __iomem *base, u32 reg, u32 val)
{
	renesas_i3c_reg_update(base + reg, val, val);
}

static void renesas_clear_bit(void __iomem *base, u32 reg, u32 val)
{
	renesas_i3c_reg_update(base + reg, val, 0);
}

static inline struct renesas_i3c *to_renesas_i3c(struct i3c_master_controller *m)
{
	return container_of(m, struct renesas_i3c, base);
}

static inline u32 datbas_dvdyad_with_parity(u8 addr)
{
	return DATBAS_DVDYAD(addr | (parity8(addr) ? 0 : BIT(7)));
}

static int renesas_i3c_get_free_pos(struct renesas_i3c *i3c)
{
	if (!(i3c->free_pos & GENMASK(i3c->maxdevs - 1, 0)))
		return -ENOSPC;

	return ffs(i3c->free_pos) - 1;
}

static int renesas_i3c_get_addr_pos(struct renesas_i3c *i3c, u8 addr)
{
	int pos;

	for (pos = 0; pos < i3c->maxdevs; pos++) {
		if (addr == i3c->addrs[pos])
			return pos;
	}

	return -EINVAL;
}

static struct renesas_i3c_xfer *renesas_i3c_alloc_xfer(struct renesas_i3c *i3c,
						       unsigned int ncmds)
{
	struct renesas_i3c_xfer *xfer;

	xfer = kzalloc(struct_size(xfer, cmds, ncmds), GFP_KERNEL);
	if (!xfer)
		return NULL;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ncmds = ncmds;
	xfer->ret = -ETIMEDOUT;

	return xfer;
}

static void renesas_i3c_start_xfer_locked(struct renesas_i3c *i3c)
{
	struct renesas_i3c_xfer *xfer = i3c->xferqueue.cur;
	struct renesas_i3c_cmd *cmd;
	u32 cmd1;

	if (!xfer)
		return;

	cmd = xfer->cmds;

	switch (i3c->internal_state) {
	case I3C_INTERNAL_STATE_CONTROLLER_ENTDAA:
	case I3C_INTERNAL_STATE_CONTROLLER_SETDASA:
		renesas_set_bit(i3c->regs, NTIE, NTIE_RSPQFIE);
		renesas_writel(i3c->regs, NCMDQP, cmd->cmd0);
		renesas_writel(i3c->regs, NCMDQP, 0);
		break;
	case I3C_INTERNAL_STATE_CONTROLLER_WRITE:
	case I3C_INTERNAL_STATE_CONTROLLER_COMMAND_WRITE:
		renesas_set_bit(i3c->regs, NTIE, NTIE_RSPQFIE);
		if (cmd->len <= 4) {
			cmd->cmd0 |= NCMDQP_CMD_ATTR(NCMDQP_IMMED_XFER);
			cmd->cmd0 |= NCMDQP_BYTE_CNT(cmd->len);
			cmd->tx_count = cmd->len;
			cmd1 = cmd->len == 0 ? 0 : *(u32 *)cmd->tx_buf;
		} else {
			cmd1 = NCMDQP_DATA_LENGTH(cmd->len);
		}
		renesas_writel(i3c->regs, NCMDQP, cmd->cmd0);
		renesas_writel(i3c->regs, NCMDQP, cmd1);
		break;
	case I3C_INTERNAL_STATE_CONTROLLER_READ:
	case I3C_INTERNAL_STATE_CONTROLLER_COMMAND_READ:
		renesas_set_bit(i3c->regs, NTIE, NTIE_RDBFIE0);
		cmd1 = NCMDQP_DATA_LENGTH(cmd->len);
		renesas_writel(i3c->regs, NCMDQP, cmd->cmd0);
		renesas_writel(i3c->regs, NCMDQP, cmd1);
		break;
	default:
		break;
	}

	/* Clear the command queue empty flag */
	renesas_clear_bit(i3c->regs, NTST, NTST_CMDQEF);
}

static void renesas_i3c_dequeue_xfer_locked(struct renesas_i3c *i3c,
					    struct renesas_i3c_xfer *xfer)
{
	if (i3c->xferqueue.cur == xfer)
		i3c->xferqueue.cur = NULL;
	else
		list_del_init(&xfer->node);
}

static void renesas_i3c_dequeue_xfer(struct renesas_i3c *i3c, struct renesas_i3c_xfer *xfer)
{
	scoped_guard(spinlock_irqsave, &i3c->xferqueue.lock)
		renesas_i3c_dequeue_xfer_locked(i3c, xfer);
}

static void renesas_i3c_enqueue_xfer(struct renesas_i3c *i3c, struct renesas_i3c_xfer *xfer)
{
	reinit_completion(&xfer->comp);
	scoped_guard(spinlock_irqsave, &i3c->xferqueue.lock) {
		if (i3c->xferqueue.cur) {
			list_add_tail(&xfer->node, &i3c->xferqueue.list);
		} else {
			i3c->xferqueue.cur = xfer;
			if (!xfer->is_i2c_xfer)
				renesas_i3c_start_xfer_locked(i3c);
		}
	}
}

static void renesas_i3c_wait_xfer(struct renesas_i3c *i3c, struct renesas_i3c_xfer *xfer)
{
	unsigned long time_left;

	renesas_i3c_enqueue_xfer(i3c, xfer);

	time_left = wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000));
	if (!time_left)
		renesas_i3c_dequeue_xfer(i3c, xfer);
}

static void renesas_i3c_set_prts(struct renesas_i3c *i3c, u32 val)
{
	/* Required sequence according to tnrza0140ae */
	renesas_set_bit(i3c->regs, RSTCTL, RSTCTL_INTLRST);
	renesas_writel(i3c->regs, PRTS, val);
	renesas_clear_bit(i3c->regs, RSTCTL, RSTCTL_INTLRST);
}

static void renesas_i3c_bus_enable(struct i3c_master_controller *m, bool i3c_mode)
{
	struct renesas_i3c *i3c = to_renesas_i3c(m);

	/* Setup either I3C or I2C protocol */
	if (i3c_mode) {
		renesas_i3c_set_prts(i3c, 0);
		/* Revisit: INCBA handling, especially after I2C transfers */
		renesas_set_bit(i3c->regs, BCTL, BCTL_HJACKCTL | BCTL_INCBA);
		renesas_set_bit(i3c->regs, MSDVAD, MSDVAD_MDYADV);
		renesas_writel(i3c->regs, STDBR, i3c->i3c_STDBR);
	} else {
		renesas_i3c_set_prts(i3c, PRTS_PRTMD);
		renesas_writel(i3c->regs, STDBR, i3c->i2c_STDBR);
	}

	/* Enable I3C bus */
	renesas_set_bit(i3c->regs, BCTL, BCTL_BUSE);
}

static int renesas_i3c_reset(struct renesas_i3c *i3c)
{
	u32 val;

	renesas_writel(i3c->regs, BCTL, 0);
	renesas_set_bit(i3c->regs, RSTCTL, RSTCTL_RI3CRST);

	return read_poll_timeout(renesas_readl, val, !(val & RSTCTL_RI3CRST),
				 0, 1000, false, i3c->regs, RSTCTL);
}

static int renesas_i3c_bus_init(struct i3c_master_controller *m)
{
	struct renesas_i3c *i3c = to_renesas_i3c(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = {};
	struct i2c_timings t;
	unsigned long rate;
	u32 double_SBR, val;
	int cks, pp_high_ticks, pp_low_ticks, i3c_total_ticks;
	int od_high_ticks, od_low_ticks, i2c_total_ticks;
	int ret;

	rate = clk_get_rate(i3c->tclk);
	if (!rate)
		return -EINVAL;

	ret = renesas_i3c_reset(i3c);
	if (ret)
		return ret;

	i2c_total_ticks = DIV_ROUND_UP(rate, bus->scl_rate.i2c);
	i3c_total_ticks = DIV_ROUND_UP(rate, bus->scl_rate.i3c);

	i2c_parse_fw_timings(&m->dev, &t, true);

	for (cks = 0; cks < 7; cks++) {
		/* SCL low-period calculation in Open-drain mode */
		od_low_ticks = ((i2c_total_ticks * 6) / 10);

		/* SCL clock calculation in Push-Pull mode */
		if (bus->mode == I3C_BUS_MODE_PURE)
			pp_high_ticks = ((i3c_total_ticks * 5) / 10);
		else
			pp_high_ticks = DIV_ROUND_UP(I3C_BUS_THIGH_MIXED_MAX_NS,
						     NSEC_PER_SEC / rate);
		pp_low_ticks = i3c_total_ticks - pp_high_ticks;

		if ((od_low_ticks / 2) <= 0xFF && pp_low_ticks < 0x3F)
			break;

		i2c_total_ticks /= 2;
		i3c_total_ticks /= 2;
		rate /= 2;
	}

	/* SCL clock period calculation in Open-drain mode */
	if ((od_low_ticks / 2) > 0xFF || pp_low_ticks > 0x3F) {
		dev_err(&m->dev, "invalid speed (i2c-scl = %lu Hz, i3c-scl = %lu Hz). Too slow.\n",
			(unsigned long)bus->scl_rate.i2c, (unsigned long)bus->scl_rate.i3c);
		return -EINVAL;
	}

	/* SCL high-period calculation in Open-drain mode */
	od_high_ticks = i2c_total_ticks - od_low_ticks;

	/* Standard Bit Rate setting */
	double_SBR = od_low_ticks > 0xFF ? 1 : 0;
	i3c->i3c_STDBR = (double_SBR ? STDBR_DSBRPO : 0) |
			STDBR_SBRLO(double_SBR, od_low_ticks) |
			STDBR_SBRHO(double_SBR, od_high_ticks) |
			STDBR_SBRLP(pp_low_ticks) |
			STDBR_SBRHP(pp_high_ticks);

	od_low_ticks -= t.scl_fall_ns / (NSEC_PER_SEC / rate) + 1;
	od_high_ticks -= t.scl_rise_ns / (NSEC_PER_SEC / rate) + 1;
	i3c->i2c_STDBR = (double_SBR ? STDBR_DSBRPO : 0) |
			STDBR_SBRLO(double_SBR, od_low_ticks) |
			STDBR_SBRHO(double_SBR, od_high_ticks) |
			STDBR_SBRLP(pp_low_ticks) |
			STDBR_SBRHP(pp_high_ticks);
	renesas_writel(i3c->regs, STDBR, i3c->i3c_STDBR);

	/* Extended Bit Rate setting */
	renesas_writel(i3c->regs, EXTBR, EXTBR_EBRLO(od_low_ticks) |
					   EXTBR_EBRHO(od_high_ticks) |
					   EXTBR_EBRLP(pp_low_ticks) |
					   EXTBR_EBRHP(pp_high_ticks));

	renesas_writel(i3c->regs, REFCKCTL, REFCKCTL_IREFCKS(cks));

	/* Disable Slave Mode */
	renesas_writel(i3c->regs, SVCTL, 0);

	/* Initialize Queue/Buffer threshold */
	renesas_writel(i3c->regs, NQTHCTL, NQTHCTL_IBIDSSZ(6) |
					     NQTHCTL_CMDQTH(1));

	/* The only supported configuration is two entries*/
	renesas_writel(i3c->regs, NTBTHCTL0, 0);
	/* Interrupt when there is one entry in the queue */
	renesas_writel(i3c->regs, NRQTHCTL, 0);

	/* Enable all Bus/Transfer Status Flags */
	renesas_writel(i3c->regs, BSTE, BSTE_ALL_FLAG);
	renesas_writel(i3c->regs, NTSTE, NTSTE_ALL_FLAG);

	/* Interrupt enable settings */
	renesas_writel(i3c->regs, BIE, BIE_NACKDIE | BIE_TENDIE);
	renesas_writel(i3c->regs, NTIE, 0);

	/* Clear Status register */
	renesas_writel(i3c->regs, NTST, 0);
	renesas_writel(i3c->regs, INST, 0);
	renesas_writel(i3c->regs, BST, 0);

	/* Hot-Join Acknowlege setting. */
	renesas_set_bit(i3c->regs, BCTL, BCTL_HJACKCTL);

	renesas_writel(i3c->regs, IBINCTL, IBINCTL_NRHJCTL | IBINCTL_NRMRCTL |
					     IBINCTL_NRSIRCTL);

	renesas_writel(i3c->regs, SCSTLCTL, 0);
	renesas_set_bit(i3c->regs, SCSTRCTL, SCSTRCTL_ACKTWE);

	/* Bus condition timing */
	val = DIV_ROUND_UP(I3C_BUS_TBUF_MIXED_FM_MIN_NS, NSEC_PER_SEC / rate);
	renesas_writel(i3c->regs, BFRECDT, BFRECDT_FRECYC(val));

	val = DIV_ROUND_UP(I3C_BUS_TAVAL_MIN_NS, NSEC_PER_SEC / rate);
	renesas_writel(i3c->regs, BAVLCDT, BAVLCDT_AVLCYC(val));

	val = DIV_ROUND_UP(I3C_BUS_TIDLE_MIN_NS, NSEC_PER_SEC / rate);
	renesas_writel(i3c->regs, BIDLCDT, BIDLCDT_IDLCYC(val));

	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	renesas_writel(i3c->regs, MSDVAD, MSDVAD_MDYAD(ret) | MSDVAD_MDYADV);

	memset(&info, 0, sizeof(info));
	info.dyn_addr = ret;
	return i3c_master_set_info(&i3c->base, &info);
}

static void renesas_i3c_bus_cleanup(struct i3c_master_controller *m)
{
	struct renesas_i3c *i3c = to_renesas_i3c(m);

	renesas_i3c_reset(i3c);
}

static int renesas_i3c_daa(struct i3c_master_controller *m)
{
	struct renesas_i3c *i3c = to_renesas_i3c(m);
	struct renesas_i3c_cmd *cmd;
	u32 olddevs, newdevs;
	u8 last_addr = 0, pos;
	int ret;

	struct renesas_i3c_xfer *xfer __free(kfree) = renesas_i3c_alloc_xfer(i3c, 1);
	if (!xfer)
		return -ENOMEM;

	/* Enable I3C bus. */
	renesas_i3c_bus_enable(m, true);

	olddevs = ~(i3c->free_pos);
	i3c->internal_state = I3C_INTERNAL_STATE_CONTROLLER_ENTDAA;

	/* Setting DATBASn registers for target devices. */
	for (pos = 0; pos < i3c->maxdevs; pos++) {
		if (olddevs & BIT(pos))
			continue;

		ret = i3c_master_get_free_addr(m, last_addr + 1);
		if (ret < 0)
			return -ENOSPC;

		i3c->addrs[pos] = ret;
		last_addr = ret;

		renesas_writel(i3c->regs, DATBAS(pos), datbas_dvdyad_with_parity(ret));
	}

	init_completion(&xfer->comp);
	cmd = xfer->cmds;
	cmd->rx_count = 0;

	ret = renesas_i3c_get_free_pos(i3c);
	if (ret < 0)
		return ret;

	/*
	 * Setup the command descriptor to start the ENTDAA command
	 * and starting at the selected device index.
	 */
	cmd->cmd0 = NCMDQP_CMD_ATTR(NCMDQP_ADDR_ASSGN) | NCMDQP_ROC |
		    NCMDQP_TID(I3C_COMMAND_ADDRESS_ASSIGNMENT) |
		    NCMDQP_CMD(I3C_CCC_ENTDAA) | NCMDQP_DEV_INDEX(ret) |
		    NCMDQP_DEV_COUNT(i3c->maxdevs - ret) | NCMDQP_TOC;

	renesas_i3c_wait_xfer(i3c, xfer);

	newdevs = GENMASK(i3c->maxdevs - cmd->rx_count - 1, 0);
	newdevs &= ~olddevs;

	for (pos = 0; pos < i3c->maxdevs; pos++) {
		if (newdevs & BIT(pos))
			i3c_master_add_i3c_dev_locked(m, i3c->addrs[pos]);
	}

	return 0;
}

static bool renesas_i3c_supports_ccc_cmd(struct i3c_master_controller *m,
						const struct i3c_ccc_cmd *cmd)
{
	if (cmd->ndests > 1)
		return false;

	switch (cmd->id) {
	case I3C_CCC_ENEC(true):
	case I3C_CCC_ENEC(false):
	case I3C_CCC_DISEC(true):
	case I3C_CCC_DISEC(false):
	case I3C_CCC_ENTAS(0, true):
	case I3C_CCC_ENTAS(1, true):
	case I3C_CCC_ENTAS(2, true):
	case I3C_CCC_ENTAS(3, true):
	case I3C_CCC_ENTAS(0, false):
	case I3C_CCC_ENTAS(1, false):
	case I3C_CCC_ENTAS(2, false):
	case I3C_CCC_ENTAS(3, false):
	case I3C_CCC_RSTDAA(true):
	case I3C_CCC_RSTDAA(false):
	case I3C_CCC_ENTDAA:
	case I3C_CCC_DEFSLVS:
	case I3C_CCC_SETMWL(true):
	case I3C_CCC_SETMWL(false):
	case I3C_CCC_SETMRL(true):
	case I3C_CCC_SETMRL(false):
	case I3C_CCC_ENTTM:
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETACCMST:
	case I3C_CCC_GETMXDS:
		return true;
	default:
		return false;
	}
}

static int renesas_i3c_send_ccc_cmd(struct i3c_master_controller *m,
					   struct i3c_ccc_cmd *ccc)
{
	struct renesas_i3c *i3c = to_renesas_i3c(m);
	struct renesas_i3c_xfer *xfer;
	struct renesas_i3c_cmd *cmd;
	int ret, pos = 0;

	if (ccc->id & I3C_CCC_DIRECT) {
		pos = renesas_i3c_get_addr_pos(i3c, ccc->dests[0].addr);
		if (pos < 0)
			return pos;
	}

	xfer = renesas_i3c_alloc_xfer(i3c, 1);
	if (!xfer)
		return -ENOMEM;

	renesas_i3c_bus_enable(m, true);

	init_completion(&xfer->comp);
	cmd = xfer->cmds;
	cmd->rnw = ccc->rnw;
	cmd->cmd0 = 0;

	/* Calculate the command descriptor. */
	switch (ccc->id) {
	case I3C_CCC_SETDASA:
		renesas_writel(i3c->regs, DATBAS(pos),
			DATBAS_DVSTAD(ccc->dests[0].addr) |
			DATBAS_DVDYAD(*(u8 *)ccc->dests[0].payload.data >> 1));
		cmd->cmd0 = NCMDQP_CMD_ATTR(NCMDQP_ADDR_ASSGN) | NCMDQP_ROC |
			NCMDQP_TID(I3C_COMMAND_ADDRESS_ASSIGNMENT) |
			NCMDQP_CMD(I3C_CCC_SETDASA) | NCMDQP_DEV_INDEX(pos) |
			NCMDQP_DEV_COUNT(0) | NCMDQP_TOC;
		i3c->internal_state = I3C_INTERNAL_STATE_CONTROLLER_SETDASA;
		break;
	default:
		/* Calculate the command descriptor. */
		cmd->cmd0 = NCMDQP_TID(I3C_COMMAND_WRITE) | NCMDQP_MODE(0) |
				NCMDQP_RNW(ccc->rnw) | NCMDQP_CMD(ccc->id) |
				NCMDQP_ROC | NCMDQP_TOC | NCMDQP_CP |
				NCMDQP_DEV_INDEX(pos);

		if (ccc->rnw) {
			cmd->rx_buf = ccc->dests[0].payload.data;
			cmd->len = ccc->dests[0].payload.len;
			cmd->rx_count = 0;
			i3c->internal_state = I3C_INTERNAL_STATE_CONTROLLER_COMMAND_READ;
		} else {
			cmd->tx_buf = ccc->dests[0].payload.data;
			cmd->len = ccc->dests[0].payload.len;
			cmd->tx_count = 0;
			i3c->internal_state = I3C_INTERNAL_STATE_CONTROLLER_COMMAND_WRITE;
		}
	}

	renesas_i3c_wait_xfer(i3c, xfer);

	ret = xfer->ret;
	if (ret)
		ccc->err = I3C_ERROR_M2;

	kfree(xfer);

	return ret;
}

static int renesas_i3c_priv_xfers(struct i3c_dev_desc *dev, struct i3c_priv_xfer *i3c_xfers,
					 int i3c_nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct renesas_i3c *i3c = to_renesas_i3c(m);
	struct renesas_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct renesas_i3c_xfer *xfer;
	int i;

	/* Enable I3C bus. */
	renesas_i3c_bus_enable(m, true);

	xfer = renesas_i3c_alloc_xfer(i3c, 1);
	if (!xfer)
		return -ENOMEM;

	init_completion(&xfer->comp);

	for (i = 0; i < i3c_nxfers; i++) {
		struct renesas_i3c_cmd *cmd = xfer->cmds;

		/* Calculate the Transfer Command Descriptor */
		cmd->rnw = i3c_xfers[i].rnw;
		cmd->cmd0 = NCMDQP_DEV_INDEX(data->index) | NCMDQP_MODE(0) |
			    NCMDQP_RNW(cmd->rnw) | NCMDQP_ROC | NCMDQP_TOC;

		if (i3c_xfers[i].rnw) {
			cmd->rx_count = 0;
			cmd->cmd0 |= NCMDQP_TID(I3C_READ);
			cmd->rx_buf = i3c_xfers[i].data.in;
			cmd->len = i3c_xfers[i].len;
			i3c->internal_state = I3C_INTERNAL_STATE_CONTROLLER_READ;
		} else {
			cmd->tx_count = 0;
			cmd->cmd0 |= NCMDQP_TID(I3C_WRITE);
			cmd->tx_buf = i3c_xfers[i].data.out;
			cmd->len = i3c_xfers[i].len;
			i3c->internal_state = I3C_INTERNAL_STATE_CONTROLLER_WRITE;
		}

		if (!i3c_xfers[i].rnw && i3c_xfers[i].len > 4) {
			i3c_writel_fifo(i3c->regs + NTDTBP0, cmd->tx_buf, cmd->len);
			if (cmd->len > NTDTBP0_DEPTH * sizeof(u32))
				renesas_set_bit(i3c->regs, NTIE, NTIE_TDBEIE0);
		}

		renesas_i3c_wait_xfer(i3c, xfer);
	}

	return 0;
}

static int renesas_i3c_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct renesas_i3c *i3c = to_renesas_i3c(m);
	struct renesas_i3c_i2c_dev_data *data;
	int pos;

	pos = renesas_i3c_get_free_pos(i3c);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	i3c->addrs[pos] = dev->info.dyn_addr ? : dev->info.static_addr;
	i3c->free_pos &= ~BIT(pos);

	renesas_writel(i3c->regs, DATBAS(pos), DATBAS_DVSTAD(dev->info.static_addr) |
				    datbas_dvdyad_with_parity(i3c->addrs[pos]));
	i3c_dev_set_master_data(dev, data);

	return 0;
}

static int renesas_i3c_reattach_i3c_dev(struct i3c_dev_desc *dev,
					       u8 old_dyn_addr)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct renesas_i3c *i3c = to_renesas_i3c(m);
	struct renesas_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c->addrs[data->index] = dev->info.dyn_addr ? dev->info.dyn_addr :
							dev->info.static_addr;

	return 0;
}

static void renesas_i3c_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct renesas_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct renesas_i3c *i3c = to_renesas_i3c(m);

	i3c_dev_set_master_data(dev, NULL);
	i3c->addrs[data->index] = 0;
	i3c->free_pos |= BIT(data->index);
	kfree(data);
}

static int renesas_i3c_i2c_xfers(struct i2c_dev_desc *dev,
					struct i2c_msg *i2c_xfers,
					int i2c_nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct renesas_i3c *i3c = to_renesas_i3c(m);
	struct renesas_i3c_cmd *cmd;
	u8 start_bit = CNDCTL_STCND;
	int i;

	struct renesas_i3c_xfer *xfer __free(kfree) = renesas_i3c_alloc_xfer(i3c, 1);
	if (!xfer)
		return -ENOMEM;

	if (!i2c_nxfers)
		return 0;

	renesas_i3c_bus_enable(m, false);

	init_completion(&xfer->comp);
	xfer->is_i2c_xfer = true;
	cmd = xfer->cmds;

	if (!(renesas_readl(i3c->regs, BCST) & BCST_BFREF)) {
		cmd->err = -EBUSY;
		return cmd->err;
	}

	renesas_writel(i3c->regs, BST, 0);

	renesas_i3c_enqueue_xfer(i3c, xfer);

	for (i = 0; i < i2c_nxfers; i++) {
		cmd->i2c_bytes_left = I2C_INIT_MSG;
		cmd->i2c_buf = i2c_xfers[i].buf;
		cmd->msg = &i2c_xfers[i];
		cmd->i2c_is_last = (i == i2c_nxfers - 1);

		renesas_set_bit(i3c->regs, BIE, BIE_NACKDIE);
		renesas_set_bit(i3c->regs, NTIE, NTIE_TDBEIE0);
		renesas_set_bit(i3c->regs, BIE, BIE_STCNDDIE);

		/* Issue Start condition */
		renesas_set_bit(i3c->regs, CNDCTL, start_bit);

		renesas_set_bit(i3c->regs, NTSTE, NTSTE_TDBEE0);

		wait_for_completion_timeout(&xfer->comp, m->i2c.timeout);

		if (cmd->err)
			break;

		start_bit = CNDCTL_SRCND;
	}

	renesas_i3c_dequeue_xfer(i3c, xfer);
	return cmd->err;
}

static int renesas_i3c_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct renesas_i3c *i3c = to_renesas_i3c(m);
	struct renesas_i3c_i2c_dev_data *data;
	int pos;

	pos = renesas_i3c_get_free_pos(i3c);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	i3c->addrs[pos] = dev->addr;
	i3c->free_pos &= ~BIT(pos);
	i2c_dev_set_master_data(dev, data);

	return 0;
}

static void renesas_i3c_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct renesas_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct renesas_i3c *i3c = to_renesas_i3c(m);

	i2c_dev_set_master_data(dev, NULL);
	i3c->addrs[data->index] = 0;
	i3c->free_pos |= BIT(data->index);
	kfree(data);
}

static irqreturn_t renesas_i3c_tx_isr(int irq, void *data)
{
	struct renesas_i3c *i3c = data;
	struct renesas_i3c_xfer *xfer;
	struct renesas_i3c_cmd *cmd;
	u8 val;

	scoped_guard(spinlock, &i3c->xferqueue.lock) {
		xfer = i3c->xferqueue.cur;
		cmd = xfer->cmds;

		if (xfer->is_i2c_xfer) {
			if (!cmd->i2c_bytes_left)
				return IRQ_NONE;

			if (cmd->i2c_bytes_left != I2C_INIT_MSG) {
				val = *cmd->i2c_buf;
				cmd->i2c_buf++;
				cmd->i2c_bytes_left--;
				renesas_writel(i3c->regs, NTDTBP0, val);
			}

			if (cmd->i2c_bytes_left == 0) {
				renesas_clear_bit(i3c->regs, NTIE, NTIE_TDBEIE0);
				renesas_set_bit(i3c->regs, BIE, BIE_TENDIE);
			}

			/* Clear the Transmit Buffer Empty status flag. */
			renesas_clear_bit(i3c->regs, NTST, NTST_TDBEF0);
		} else {
			i3c_writel_fifo(i3c->regs + NTDTBP0, cmd->tx_buf, cmd->len);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t renesas_i3c_resp_isr(int irq, void *data)
{
	struct renesas_i3c *i3c = data;
	struct renesas_i3c_xfer *xfer;
	struct renesas_i3c_cmd *cmd;
	u32 resp_descriptor = renesas_readl(i3c->regs, NRSPQP);
	u32 bytes_remaining = 0;
	u32 ntst, data_len;
	int ret = 0;

	scoped_guard(spinlock, &i3c->xferqueue.lock) {
		xfer = i3c->xferqueue.cur;
		cmd = xfer->cmds;

		/* Clear the Respone Queue Full status flag*/
		renesas_clear_bit(i3c->regs, NTST, NTST_RSPQFF);

		data_len = NRSPQP_DATA_LEN(resp_descriptor);

		switch (i3c->internal_state) {
		case I3C_INTERNAL_STATE_CONTROLLER_ENTDAA:
			cmd->rx_count = data_len;
			break;
		case I3C_INTERNAL_STATE_CONTROLLER_WRITE:
		case I3C_INTERNAL_STATE_CONTROLLER_COMMAND_WRITE:
			/* Disable the transmit IRQ if it hasn't been disabled already. */
			renesas_clear_bit(i3c->regs, NTIE, NTIE_TDBEIE0);
			break;
		case I3C_INTERNAL_STATE_CONTROLLER_READ:
		case I3C_INTERNAL_STATE_CONTROLLER_COMMAND_READ:
			if (NDBSTLV0_RDBLV(renesas_readl(i3c->regs, NDBSTLV0)) && !cmd->err)
				bytes_remaining = data_len - cmd->rx_count;

			i3c_readl_fifo(i3c->regs + NTDTBP0, cmd->rx_buf, bytes_remaining);
			renesas_clear_bit(i3c->regs, NTIE, NTIE_RDBFIE0);
			break;
		default:
			break;
		}

		switch (NRSPQP_ERR_STATUS(resp_descriptor)) {
		case NRSPQP_NO_ERROR:
			break;
		case NRSPQP_ERROR_PARITY:
		case NRSPQP_ERROR_IBA_NACK:
		case NRSPQP_ERROR_TRANSF_ABORT:
		case NRSPQP_ERROR_CRC:
		case NRSPQP_ERROR_FRAME:
			ret = -EIO;
			break;
		case NRSPQP_ERROR_OVER_UNDER_FLOW:
			ret = -ENOSPC;
			break;
		case NRSPQP_ERROR_UNSUPPORTED:
			ret = -EOPNOTSUPP;
			break;
		case NRSPQP_ERROR_I2C_W_NACK_ERR:
		case NRSPQP_ERROR_ADDRESS_NACK:
		default:
			ret = -EINVAL;
			break;
		}

		/*
		 * If the transfer was aborted, then the abort flag must be cleared
		 * before notifying the application that a transfer has completed.
		 */
		ntst = renesas_readl(i3c->regs, NTST);
		if (ntst & NTST_TABTF)
			renesas_clear_bit(i3c->regs, BCTL, BCTL_ABT);

		/* Clear error status flags. */
		renesas_clear_bit(i3c->regs, NTST, NTST_TEF | NTST_TABTF);

		xfer->ret = ret;
		complete(&xfer->comp);

		xfer = list_first_entry_or_null(&i3c->xferqueue.list,
						struct renesas_i3c_xfer, node);
		if (xfer)
			list_del_init(&xfer->node);

		i3c->xferqueue.cur = xfer;
	}

	return IRQ_HANDLED;
}

static irqreturn_t renesas_i3c_tend_isr(int irq, void *data)
{
	struct renesas_i3c *i3c = data;
	struct renesas_i3c_xfer *xfer;
	struct renesas_i3c_cmd *cmd;

	scoped_guard(spinlock, &i3c->xferqueue.lock) {
		xfer = i3c->xferqueue.cur;
		cmd = xfer->cmds;

		if (xfer->is_i2c_xfer) {
			if (renesas_readl(i3c->regs, BST) & BST_NACKDF) {
				/* We got a NACKIE */
				renesas_readl(i3c->regs, NTDTBP0); /* dummy read */
				renesas_clear_bit(i3c->regs, BST, BST_NACKDF);
				cmd->err = -ENXIO;
			} else if (cmd->i2c_bytes_left) {
				renesas_set_bit(i3c->regs, NTIE, NTIE_TDBEIE0);
				return IRQ_NONE;
			}

			if (cmd->i2c_is_last || cmd->err) {
				renesas_clear_bit(i3c->regs, BIE, BIE_TENDIE);
				renesas_set_bit(i3c->regs, BIE, BIE_SPCNDDIE);
				renesas_set_bit(i3c->regs, CNDCTL, CNDCTL_SPCND);
			} else {
				/* Transfer is complete, but do not send STOP */
				renesas_clear_bit(i3c->regs, NTSTE, NTSTE_TDBEE0);
				renesas_clear_bit(i3c->regs, BIE, BIE_TENDIE);
				xfer->ret = 0;
				complete(&xfer->comp);
			}
		}

		/* Clear the Transmit Buffer Empty status flag. */
		renesas_clear_bit(i3c->regs, BST, BST_TENDF);
	}

	return IRQ_HANDLED;
}

static irqreturn_t renesas_i3c_rx_isr(int irq, void *data)
{
	struct renesas_i3c *i3c = data;
	struct renesas_i3c_xfer *xfer;
	struct renesas_i3c_cmd *cmd;
	int read_bytes;

	/* If resp_isr already read the data and updated 'xfer', we can just leave */
	if (!(renesas_readl(i3c->regs, NTIE) & NTIE_RDBFIE0))
		return IRQ_NONE;

	scoped_guard(spinlock, &i3c->xferqueue.lock) {
		xfer = i3c->xferqueue.cur;
		cmd = xfer->cmds;

		if (xfer->is_i2c_xfer) {
			if (!cmd->i2c_bytes_left)
				return IRQ_NONE;

			if (cmd->i2c_bytes_left == I2C_INIT_MSG) {
				cmd->i2c_bytes_left = cmd->msg->len;
				renesas_set_bit(i3c->regs, SCSTRCTL, SCSTRCTL_RWE);
				renesas_readl(i3c->regs, NTDTBP0); /* dummy read */
				if (cmd->i2c_bytes_left == 1)
					renesas_writel(i3c->regs, ACKCTL, ACKCTL_ACKT | ACKCTL_ACKTWP);
				return IRQ_HANDLED;
			}

			if (cmd->i2c_bytes_left == 1) {
				/* STOP must come before we set ACKCTL! */
				if (cmd->i2c_is_last) {
					renesas_set_bit(i3c->regs, BIE, BIE_SPCNDDIE);
					renesas_clear_bit(i3c->regs, BST, BST_SPCNDDF);
					renesas_set_bit(i3c->regs, CNDCTL, CNDCTL_SPCND);
				}
				renesas_writel(i3c->regs, ACKCTL, ACKCTL_ACKT | ACKCTL_ACKTWP);
			} else {
				renesas_writel(i3c->regs, ACKCTL, ACKCTL_ACKTWP);
			}

			/* Reading acks the RIE interrupt */
			*cmd->i2c_buf = renesas_readl(i3c->regs, NTDTBP0);
			cmd->i2c_buf++;
			cmd->i2c_bytes_left--;
		} else {
			read_bytes = NDBSTLV0_RDBLV(renesas_readl(i3c->regs, NDBSTLV0)) * sizeof(u32);
			i3c_readl_fifo(i3c->regs + NTDTBP0, cmd->rx_buf, read_bytes);
			cmd->rx_count = read_bytes;
		}

		/* Clear the Read Buffer Full status flag. */
		renesas_clear_bit(i3c->regs, NTST, NTST_RDBFF0);
	}

	return IRQ_HANDLED;
}

static irqreturn_t renesas_i3c_stop_isr(int irq, void *data)
{
	struct renesas_i3c *i3c = data;
	struct renesas_i3c_xfer *xfer;

	scoped_guard(spinlock, &i3c->xferqueue.lock) {
		xfer = i3c->xferqueue.cur;

		/* read back registers to confirm writes have fully propagated */
		renesas_writel(i3c->regs, BST, 0);
		renesas_readl(i3c->regs, BST);
		renesas_writel(i3c->regs, BIE, 0);
		renesas_clear_bit(i3c->regs, NTST, NTST_TDBEF0 | NTST_RDBFF0);
		renesas_clear_bit(i3c->regs, SCSTRCTL, SCSTRCTL_RWE);

		xfer->ret = 0;
		complete(&xfer->comp);
	}

	return IRQ_HANDLED;
}

static irqreturn_t renesas_i3c_start_isr(int irq, void *data)
{
	struct renesas_i3c *i3c = data;
	struct renesas_i3c_xfer *xfer;
	struct renesas_i3c_cmd *cmd;
	u8 val;

	scoped_guard(spinlock, &i3c->xferqueue.lock) {
		xfer = i3c->xferqueue.cur;
		cmd = xfer->cmds;

		if (xfer->is_i2c_xfer) {
			if (!cmd->i2c_bytes_left)
				return IRQ_NONE;

			if (cmd->i2c_bytes_left == I2C_INIT_MSG) {
				if (cmd->msg->flags & I2C_M_RD) {
					/* On read, switch over to receive interrupt */
					renesas_clear_bit(i3c->regs, NTIE, NTIE_TDBEIE0);
					renesas_set_bit(i3c->regs, NTIE, NTIE_RDBFIE0);
				} else {
					/* On write, initialize length */
					cmd->i2c_bytes_left = cmd->msg->len;
				}

				val = i2c_8bit_addr_from_msg(cmd->msg);
				renesas_writel(i3c->regs, NTDTBP0, val);
			}
		}

		renesas_clear_bit(i3c->regs, BIE, BIE_STCNDDIE);
		renesas_clear_bit(i3c->regs, BST, BST_STCNDDF);
	}

	return IRQ_HANDLED;
}

static const struct i3c_master_controller_ops renesas_i3c_ops = {
	.bus_init = renesas_i3c_bus_init,
	.bus_cleanup = renesas_i3c_bus_cleanup,
	.attach_i3c_dev = renesas_i3c_attach_i3c_dev,
	.reattach_i3c_dev = renesas_i3c_reattach_i3c_dev,
	.detach_i3c_dev = renesas_i3c_detach_i3c_dev,
	.do_daa = renesas_i3c_daa,
	.supports_ccc_cmd = renesas_i3c_supports_ccc_cmd,
	.send_ccc_cmd = renesas_i3c_send_ccc_cmd,
	.priv_xfers = renesas_i3c_priv_xfers,
	.attach_i2c_dev = renesas_i3c_attach_i2c_dev,
	.detach_i2c_dev = renesas_i3c_detach_i2c_dev,
	.i2c_xfers = renesas_i3c_i2c_xfers,
};

static const struct renesas_i3c_irq_desc renesas_i3c_irqs[] = {
	{ .name = "resp", .isr = renesas_i3c_resp_isr, .desc = "i3c-resp" },
	{ .name = "rx", .isr = renesas_i3c_rx_isr, .desc = "i3c-rx" },
	{ .name = "tx", .isr = renesas_i3c_tx_isr, .desc = "i3c-tx" },
	{ .name = "st", .isr = renesas_i3c_start_isr, .desc = "i3c-start" },
	{ .name = "sp", .isr = renesas_i3c_stop_isr, .desc = "i3c-stop" },
	{ .name = "tend", .isr = renesas_i3c_tend_isr, .desc = "i3c-tend" },
	{ .name = "nack", .isr = renesas_i3c_tend_isr, .desc = "i3c-nack" },
};

static int renesas_i3c_probe(struct platform_device *pdev)
{
	struct renesas_i3c *i3c;
	struct reset_control *reset;
	struct clk *clk;
	const struct renesas_i3c_config *config = of_device_get_match_data(&pdev->dev);
	int ret, i;

	if (!config)
		return -ENODATA;

	i3c = devm_kzalloc(&pdev->dev, sizeof(*i3c), GFP_KERNEL);
	if (!i3c)
		return -ENOMEM;

	i3c->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i3c->regs))
		return PTR_ERR(i3c->regs);

	clk = devm_clk_get_enabled(&pdev->dev, "pclk");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (config->has_pclkrw) {
		clk = devm_clk_get_enabled(&pdev->dev, "pclkrw");
		if (IS_ERR(clk))
			return PTR_ERR(clk);
	}

	i3c->tclk = devm_clk_get_enabled(&pdev->dev, "tclk");
	if (IS_ERR(i3c->tclk))
		return PTR_ERR(i3c->tclk);

	reset = devm_reset_control_get_optional_exclusive_deasserted(&pdev->dev, "tresetn");
	if (IS_ERR(reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(reset),
				     "Error: missing tresetn ctrl\n");

	reset = devm_reset_control_get_optional_exclusive_deasserted(&pdev->dev, "presetn");
	if (IS_ERR(reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(reset),
				     "Error: missing presetn ctrl\n");

	spin_lock_init(&i3c->xferqueue.lock);
	INIT_LIST_HEAD(&i3c->xferqueue.list);

	ret = renesas_i3c_reset(i3c);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(renesas_i3c_irqs); i++) {
		ret = platform_get_irq_byname(pdev, renesas_i3c_irqs[i].name);
		if (ret < 0)
			return ret;

		ret = devm_request_irq(&pdev->dev, ret, renesas_i3c_irqs[i].isr,
				       0, renesas_i3c_irqs[i].desc, i3c);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, i3c);

	i3c->maxdevs = RENESAS_I3C_MAX_DEVS;
	i3c->free_pos = GENMASK(i3c->maxdevs - 1, 0);

	return i3c_master_register(&i3c->base, &pdev->dev, &renesas_i3c_ops, false);
}

static void renesas_i3c_remove(struct platform_device *pdev)
{
	struct renesas_i3c *i3c = platform_get_drvdata(pdev);

	i3c_master_unregister(&i3c->base);
}

static const struct renesas_i3c_config empty_i3c_config = {
};

static const struct renesas_i3c_config r9a09g047_i3c_config = {
	.has_pclkrw = 1,
};

static const struct of_device_id renesas_i3c_of_ids[] = {
	{ .compatible = "renesas,r9a08g045-i3c", .data = &empty_i3c_config },
	{ .compatible = "renesas,r9a09g047-i3c", .data = &r9a09g047_i3c_config },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, renesas_i3c_of_ids);

static struct platform_driver renesas_i3c = {
	.probe = renesas_i3c_probe,
	.remove = renesas_i3c_remove,
	.driver = {
		.name = "renesas-i3c",
		.of_match_table = renesas_i3c_of_ids,
	},
};
module_platform_driver(renesas_i3c);

MODULE_AUTHOR("Wolfram Sang <wsa+renesas@sang-engineering.com>");
MODULE_AUTHOR("Renesas BSP teams");
MODULE_DESCRIPTION("Renesas I3C controller driver");
MODULE_LICENSE("GPL");
