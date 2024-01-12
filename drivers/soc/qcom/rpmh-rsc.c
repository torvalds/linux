// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s " fmt, KBUILD_MODNAME

#include <linux/atomic.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/tcs.h>
#include <soc/qcom/crm.h>
#include <dt-bindings/soc/qcom,rpmh-rsc.h>

#include "rpmh-internal.h"

#define CREATE_TRACE_POINTS
#include "trace-rpmh.h"

#include <linux/ipc_logging.h>

#define RSC_DRV_IPC_LOG_SIZE		2

/* DRV ID Register */
#define RSC_DRV_ID			0
#define MAJOR_VER_MASK			0xFF
#define MAJOR_VER_SHIFT			16
#define MINOR_VER_MASK			0xFF
#define MINOR_VER_SHIFT			8

/* DRV HW Solver Configuration Register Mask */
#define DRV_HW_SOLVER_MASK		1
#define DRV_HW_SOLVER_SHIFT		24

/* DRV TCS Configuration Information Register Mask */
#define DRV_NUM_TCS_MASK		0x3F
#define DRV_NUM_TCS_SHIFT		6
#define DRV_NCPT_MASK			0x1F
#define DRV_NCPT_SHIFT			27

#define TCS_AMC_MODE_ENABLE		BIT(16)
#define TCS_AMC_MODE_TRIGGER		BIT(24)

/* TCS CMD register bit mask */
#define CMD_MSGID_LEN			8
#define CMD_MSGID_RESP_REQ		BIT(8)
#define CMD_MSGID_WRITE			BIT(16)
#define CMD_STATUS_ISSUED		BIT(8)
#define CMD_STATUS_COMPL		BIT(16)

/* Offsets for DRV channel status register */
#define CH0_CHN_BUSY			BIT(0)
#define CH1_CHN_BUSY			BIT(1)
#define CH0_WAKE_TCS_STATUS		BIT(0)
#define CH0_SLEEP_TCS_STATUS		BIT(1)
#define CH1_WAKE_TCS_STATUS		BIT(2)
#define CH1_SLEEP_TCS_STATUS		BIT(3)
#define CH_CLEAR_STATUS			BIT(31)

#define ACCL_TYPE(addr)			((addr >> 16) & 0xF)
#define VREG_ADDR(addr)			(addr & ~0xF)

#define MAX_RSC_COUNT			5

enum {
	HW_ACCL_CLK = 0x3,
	HW_ACCL_VREG,
	HW_ACCL_BUS,
};

static const char * const accl_str[] = {
	"", "", "", "CLK", "VREG", "BUS",
};

static LIST_HEAD(rpmh_rsc_dev_list);
static struct rsc_drv *__rsc_drv[MAX_RSC_COUNT];
static int __rsc_count;
bool rpmh_standalone;

/*
 * Here's a high level overview of how all the registers in RPMH work
 * together:
 *
 * - The main rpmh-rsc address is the base of a register space that can
 *   be used to find overall configuration of the hardware
 *   (DRV_PRNT_CHLD_CONFIG). Also found within the rpmh-rsc register
 *   space are all the TCS blocks. The offset of the TCS blocks is
 *   specified in the device tree by "qcom,tcs-offset" and used to
 *   compute tcs_base.
 * - TCS blocks come one after another. Type, count, and order are
 *   specified by the device tree as "qcom,tcs-config".
 * - Each TCS block has some registers, then space for up to 16 commands.
 *   Note that though address space is reserved for 16 commands, fewer
 *   might be present. See ncpt (num cmds per TCS).
 *
 * Here's a picture:
 *
 *  +---------------------------------------------------+
 *  |RSC                                                |
 *  | ctrl                                              |
 *  |                                                   |
 *  | Drvs:                                             |
 *  | +-----------------------------------------------+ |
 *  | |DRV0                                           | |
 *  | | ctrl/config                                   | |
 *  | | IRQ                                           | |
 *  | |                                               | |
 *  | | TCSes:                                        | |
 *  | | +------------------------------------------+  | |
 *  | | |TCS0  |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | | ctrl | 0| 1| 2| 3| 4| 5| .| .| .| .|14|15|  | |
 *  | | |      |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | +------------------------------------------+  | |
 *  | | +------------------------------------------+  | |
 *  | | |TCS1  |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | | ctrl | 0| 1| 2| 3| 4| 5| .| .| .| .|14|15|  | |
 *  | | |      |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | +------------------------------------------+  | |
 *  | | +------------------------------------------+  | |
 *  | | |TCS2  |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | | ctrl | 0| 1| 2| 3| 4| 5| .| .| .| .|14|15|  | |
 *  | | |      |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | +------------------------------------------+  | |
 *  | |                    ......                     | |
 *  | +-----------------------------------------------+ |
 *  | +-----------------------------------------------+ |
 *  | |DRV1                                           | |
 *  | | (same as DRV0)                                | |
 *  | +-----------------------------------------------+ |
 *  |                      ......                       |
 *  +---------------------------------------------------+
 */

enum {
	RSC_DRV_TCS_OFFSET,
	RSC_DRV_CMD_OFFSET,
/* DRV HW Solver Configuration Information Register */
	DRV_SOLVER_CONFIG,
/* DRV TCS Configuration Information Register */
	DRV_PRNT_CHLD_CONFIG,
/* Offsets for common TCS Registers, one bit per TCS */
	RSC_DRV_IRQ_ENABLE,
	RSC_DRV_IRQ_STATUS,
	RSC_DRV_IRQ_CLEAR,	/* w/o; write 1 to clear */
/*
 * Offsets for per TCS Registers.
 *
 * TCSes start at 0x10 from tcs_base and are stored one after another.
 * Multiply tcs_id by RSC_DRV_TCS_OFFSET to find a given TCS and add one
 * of the below to find a register.
 */
	RSC_DRV_CMD_WAIT_FOR_CMPL,	/* 1 bit per command */
	RSC_DRV_CONTROL,
	RSC_DRV_STATUS,	/* zero if tcs is busy */
	RSC_DRV_CMD_ENABLE,	/* 1 bit per command */
/*
 * Offsets for per command in a TCS.
 *
 * Commands (up to 16) start at 0x30 in a TCS; multiply command index
 * by RSC_DRV_CMD_OFFSET and add one of the below to find a register.
 */
	RSC_DRV_CMD_MSGID,
	RSC_DRV_CMD_ADDR,
	RSC_DRV_CMD_DATA,
	RSC_DRV_CMD_STATUS,
	RSC_DRV_CMD_RESP_DATA,
/* DRV channel Registers */
	RSC_DRV_CHN_SEQ_BUSY,
	RSC_DRV_CHN_SEQ_PC,
	RSC_DRV_CHN_TCS_TRIGGER,
	RSC_DRV_CHN_TCS_COMPLETE,
	RSC_DRV_CHN_UPDATE,
	RSC_DRV_CHN_BUSY,
	RSC_DRV_CHN_EN,
};

static u32 rpmh_rsc_reg_offsets_ver_2_7[] = {
	[RSC_DRV_TCS_OFFSET]		=	672,
	[RSC_DRV_CMD_OFFSET]		=	20,
	[DRV_SOLVER_CONFIG]		=	0x04,
	[DRV_PRNT_CHLD_CONFIG]		=	0x0C,
	[RSC_DRV_IRQ_ENABLE]		=	0x00,
	[RSC_DRV_IRQ_STATUS]		=	0x04,
	[RSC_DRV_IRQ_CLEAR]		=	0x08,
	[RSC_DRV_CMD_WAIT_FOR_CMPL]	=	0x10,
	[RSC_DRV_CONTROL]		=	0x14,
	[RSC_DRV_STATUS]		=	0x18,
	[RSC_DRV_CMD_ENABLE]		=	0x1C,
	[RSC_DRV_CMD_MSGID]		=	0x30,
	[RSC_DRV_CMD_ADDR]		=	0x34,
	[RSC_DRV_CMD_DATA]		=	0x38,
	[RSC_DRV_CMD_STATUS]		=	0x3C,
	[RSC_DRV_CMD_RESP_DATA]		=	0x40,
	[RSC_DRV_CHN_SEQ_BUSY]		=	0x0,
	[RSC_DRV_CHN_SEQ_PC]		=	0x0,
	[RSC_DRV_CHN_TCS_TRIGGER]	=	0x0,
	[RSC_DRV_CHN_TCS_COMPLETE]	=	0x0,
	[RSC_DRV_CHN_UPDATE]		=	0x0,
	[RSC_DRV_CHN_BUSY]		=	0x0,
	[RSC_DRV_CHN_EN]		=	0x0,
};

static u32 rpmh_rsc_reg_offsets_ver_3_0[] = {
	[RSC_DRV_TCS_OFFSET]		=	672,
	[RSC_DRV_CMD_OFFSET]		=	24,
	[DRV_SOLVER_CONFIG]		=	0x04,
	[DRV_PRNT_CHLD_CONFIG]		=	0x0C,
	[RSC_DRV_IRQ_ENABLE]		=	0x00,
	[RSC_DRV_IRQ_STATUS]		=	0x04,
	[RSC_DRV_IRQ_CLEAR]		=	0x08,
	[RSC_DRV_CMD_WAIT_FOR_CMPL]	=	0x20,
	[RSC_DRV_CONTROL]		=	0x24,
	[RSC_DRV_STATUS]		=	0x28,
	[RSC_DRV_CMD_ENABLE]		=	0x2C,
	[RSC_DRV_CMD_MSGID]		=	0x34,
	[RSC_DRV_CMD_ADDR]		=	0x38,
	[RSC_DRV_CMD_DATA]		=	0x3C,
	[RSC_DRV_CMD_STATUS]		=	0x40,
	[RSC_DRV_CMD_RESP_DATA]		=	0x44,
	[RSC_DRV_CHN_SEQ_BUSY]		=	0x464,
	[RSC_DRV_CHN_SEQ_PC]		=	0x468,
	[RSC_DRV_CHN_TCS_TRIGGER]	=	0x490,
	[RSC_DRV_CHN_TCS_COMPLETE]	=	0x494,
	[RSC_DRV_CHN_UPDATE]		=	0x498,
	[RSC_DRV_CHN_BUSY]		=	0x49C,
	[RSC_DRV_CHN_EN]		=	0x4A0,
};

static u32 rpmh_rsc_reg_offsets_ver_3_0_hw_channel[] = {
	[RSC_DRV_TCS_OFFSET]		=	336,
	[RSC_DRV_CMD_OFFSET]		=	24,
	[DRV_SOLVER_CONFIG]		=	0x04,
	[DRV_PRNT_CHLD_CONFIG]		=	0x0C,
	[RSC_DRV_IRQ_ENABLE]		=	0x00,
	[RSC_DRV_IRQ_STATUS]		=	0x04,
	[RSC_DRV_IRQ_CLEAR]		=	0x08,
	[RSC_DRV_CMD_WAIT_FOR_CMPL]	=	0x20,
	[RSC_DRV_CONTROL]		=	0x24,
	[RSC_DRV_STATUS]		=	0x28,
	[RSC_DRV_CMD_ENABLE]		=	0x2C,
	[RSC_DRV_CMD_MSGID]		=	0x34,
	[RSC_DRV_CMD_ADDR]		=	0x38,
	[RSC_DRV_CMD_DATA]		=	0x3C,
	[RSC_DRV_CMD_STATUS]		=	0x40,
	[RSC_DRV_CMD_RESP_DATA]		=	0x44,
	[RSC_DRV_CHN_SEQ_BUSY]		=	0x464,
	[RSC_DRV_CHN_SEQ_PC]		=	0x468,
	[RSC_DRV_CHN_TCS_TRIGGER]	=	0x490,
	[RSC_DRV_CHN_TCS_COMPLETE]	=	0x494,
	[RSC_DRV_CHN_UPDATE]		=	0x498,
	[RSC_DRV_CHN_BUSY]		=	0x49C,
	[RSC_DRV_CHN_EN]		=	0x4A0,
};

static inline void __iomem *
tcs_reg_addr(const struct rsc_drv *drv, int reg, int tcs_id)
{
	return drv->tcs_base + drv->regs[RSC_DRV_TCS_OFFSET] * tcs_id + reg;
}

static inline void __iomem *
tcs_cmd_addr(const struct rsc_drv *drv, int reg, int tcs_id, int cmd_id)
{
	return tcs_reg_addr(drv, reg, tcs_id) + drv->regs[RSC_DRV_CMD_OFFSET] * cmd_id;
}

static u32 read_tcs_cmd(const struct rsc_drv *drv, int reg, int tcs_id,
			int cmd_id)
{
	return readl_relaxed(tcs_cmd_addr(drv, reg, tcs_id, cmd_id));
}

static u32 read_tcs_reg(const struct rsc_drv *drv, int reg, int tcs_id)
{
	return readl_relaxed(tcs_reg_addr(drv, reg, tcs_id));
}

static void write_tcs_cmd(const struct rsc_drv *drv, int reg, int tcs_id,
			  int cmd_id, u32 data)
{
	writel_relaxed(data, tcs_cmd_addr(drv, reg, tcs_id, cmd_id));
}

static void write_tcs_reg(const struct rsc_drv *drv, int reg, int tcs_id,
			  u32 data)
{
	writel_relaxed(data, tcs_reg_addr(drv, reg, tcs_id));
}

static void write_tcs_reg_sync(const struct rsc_drv *drv, int reg, int tcs_id,
			       u32 data)
{
	int i;

	writel(data, tcs_reg_addr(drv, reg, tcs_id));

	/*
	 * Wait until we read back the same value.  Use a counter rather than
	 * ktime for timeout since this may be called after timekeeping stops.
	 */
	for (i = 0; i < USEC_PER_SEC; i++) {
		if (readl(tcs_reg_addr(drv, reg, tcs_id)) == data)
			return;
		udelay(1);
	}
	pr_err("%s: error writing %#x to %d:%#x\n", drv->name,
	       data, tcs_id, reg);
}

/**
 * tcs_invalidate() - Invalidate all TCSes of the given type (sleep or wake).
 * @drv:  The RSC controller.
 * @type: SLEEP_TCS or WAKE_TCS
 * @ch:   Channel number
 *
 * This will clear the "slots" variable of the given tcs_group and also
 * tell the hardware to forget about all entries.
 *
 * The caller must ensure that no other RPMH actions are happening when this
 * function is called, since otherwise the device may immediately become
 * used again even before this function exits.
 */
static void tcs_invalidate(struct rsc_drv *drv, int type, int ch)
{
	int m;
	struct tcs_group *tcs = &drv->ch[ch].tcs[type];

	/* Caller ensures nobody else is running so no lock */
	if (bitmap_empty(tcs->slots, tcs->ncpt * tcs->num_tcs))
		return;

	for (m = tcs->offset; m < tcs->offset + tcs->num_tcs; m++) {
		write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CMD_ENABLE], m, 0);
		write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CMD_WAIT_FOR_CMPL], m, 0);
	}

	bitmap_zero(tcs->slots, tcs->ncpt * tcs->num_tcs);
}

/**
 * rpmh_rsc_get_channel() - Get the Unused channel to send data on.
 * @drv: The RSC controller.
 *
 * Return: 0 on success, else -error.
 */
int rpmh_rsc_get_channel(struct rsc_drv *drv)
{
	int chn_update, chn_busy;

	if (drv->num_channels == 1)
		return CH0;

	/* Select Unused channel */
	do {
		chn_update = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_UPDATE]);
		chn_busy = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_BUSY]);
	} while (chn_busy != chn_update);

	if (chn_busy & CH0_CHN_BUSY)
		return CH1;
	else if (chn_busy & CH1_CHN_BUSY)
		return CH0;
	else
		return -EBUSY;
}

/**
 * rpmh_rsc_invalidate() - Invalidate sleep and wake TCSes.
 * @drv: The RSC controller.
 * @ch:  Channel number
 *
 * The caller must ensure that no other RPMH actions are happening when thi]s
 * function is called, since otherwise the device may immediately become
 * used again even before this function exits.
 */
void rpmh_rsc_invalidate(struct rsc_drv *drv, int ch)
{
	tcs_invalidate(drv, SLEEP_TCS, ch);
	tcs_invalidate(drv, WAKE_TCS, ch);
}

/**
 * get_tcs_for_msg() - Get the tcs_group used to send the given message.
 * @drv: The RSC controller.
 * @msg: The message we want to send.
 *
 * This is normally pretty straightforward except if we are trying to send
 * an ACTIVE_ONLY message but don't have any active_only TCSes.
 *
 * Return: A pointer to a tcs_group or an ERR_PTR.
 */
static struct tcs_group *get_tcs_for_msg(struct rsc_drv *drv,
					 enum rpmh_state state,
					 int ch)
{
	int type;
	struct tcs_group *tcs;

	switch (state) {
	case RPMH_ACTIVE_ONLY_STATE:
		type = ACTIVE_TCS;
		break;
	case RPMH_WAKE_ONLY_STATE:
		type = WAKE_TCS;
		break;
	case RPMH_SLEEP_STATE:
		type = SLEEP_TCS;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	/*
	 * If we are making an active request on a RSC that does not have a
	 * dedicated TCS for active state use, then re-purpose a wake TCS to
	 * send active votes. This is safe because we ensure any active-only
	 * transfers have finished before we use it (maybe by running from
	 * the last CPU in PM code).
	 */
	tcs = &drv->ch[ch].tcs[type];
	if (state == RPMH_ACTIVE_ONLY_STATE && !tcs->num_tcs)
		tcs = &drv->ch[ch].tcs[WAKE_TCS];

	return tcs;
}

/**
 * get_req_from_tcs() - Get a stashed request that was xfering on the given TCS.
 * @drv:    The RSC controller.
 * @tcs_id: The global ID of this TCS.
 *
 * For ACTIVE_ONLY transfers we want to call back into the client when the
 * transfer finishes. To do this we need the "request" that the client
 * originally provided us. This function grabs the request that we stashed
 * when we started the transfer.
 *
 * This only makes sense for ACTIVE_ONLY transfers since those are the only
 * ones we track sending (the only ones we enable interrupts for and the only
 * ones we call back to the client for).
 *
 * Return: The stashed request.
 */
static const struct tcs_request *get_req_from_tcs(struct rsc_drv *drv,
						  int tcs_id,
						  int *ch)
{
	struct tcs_group *tcs;
	int i;

	for (i = 0; i < MAX_CHANNEL; i++) {
		if (!drv->ch[i].initialized)
			continue;

		tcs = get_tcs_for_msg(drv, RPMH_ACTIVE_ONLY_STATE, i);
		if (tcs->mask & BIT(tcs_id)) {
			*ch = i;
			return tcs->req[tcs_id - tcs->offset];
		}
	}

	return NULL;
}

/**
 * __tcs_set_trigger() - Start xfer on a TCS or unset trigger on a borrowed TCS
 * @drv:     The controller.
 * @tcs_id:  The global ID of this TCS.
 * @trigger: If true then untrigger/retrigger. If false then just untrigger.
 *
 * In the normal case we only ever call with "trigger=true" to start a
 * transfer. That will un-trigger/disable the TCS from the last transfer
 * then trigger/enable for this transfer.
 *
 * If we borrowed a wake TCS for an active-only transfer we'll also call
 * this function with "trigger=false" to just do the un-trigger/disable
 * before using the TCS for wake purposes again.
 *
 * Note that the AP is only in charge of triggering active-only transfers.
 * The AP never triggers sleep/wake values using this function.
 */
static void __tcs_set_trigger(struct rsc_drv *drv, int tcs_id, bool trigger)
{
	u32 enable;

	/*
	 * HW req: Clear the DRV_CONTROL and enable TCS again
	 * While clearing ensure that the AMC mode trigger is cleared
	 * and then the mode enable is cleared.
	 */
	enable = read_tcs_reg(drv, drv->regs[RSC_DRV_CONTROL], tcs_id);
	enable &= ~TCS_AMC_MODE_TRIGGER;
	write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CONTROL], tcs_id, enable);
	enable &= ~TCS_AMC_MODE_ENABLE;
	write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CONTROL], tcs_id, enable);

	if (trigger) {
		/* Enable the AMC mode on the TCS and then trigger the TCS */
		enable = TCS_AMC_MODE_ENABLE;
		write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CONTROL], tcs_id, enable);
		enable |= TCS_AMC_MODE_TRIGGER;
		write_tcs_reg(drv, drv->regs[RSC_DRV_CONTROL], tcs_id, enable);
	}
}

/**
 * enable_tcs_irq() - Enable or disable interrupts on the given TCS.
 * @drv:     The controller.
 * @tcs_id:  The global ID of this TCS.
 * @enable:  If true then enable; if false then disable
 *
 * We only ever call this when we borrow a wake TCS for an active-only
 * transfer. For active-only TCSes interrupts are always left enabled.
 */
static void enable_tcs_irq(struct rsc_drv *drv, int tcs_id, bool enable)
{
	u32 data;

	data = readl_relaxed(drv->tcs_base + drv->regs[RSC_DRV_IRQ_ENABLE]);
	if (enable)
		data |= BIT(tcs_id);
	else
		data &= ~BIT(tcs_id);
	writel_relaxed(data, drv->tcs_base + drv->regs[RSC_DRV_IRQ_ENABLE]);
}

/**
 * tcs_tx_done() - TX Done interrupt handler.
 * @irq: The IRQ number (ignored).
 * @p:   Pointer to "struct rsc_drv".
 *
 * Called for ACTIVE_ONLY transfers (those are the only ones we enable the
 * IRQ for) when a transfer is done.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t tcs_tx_done(int irq, void *p)
{
	struct rsc_drv *drv = p;
	int i, ch;
	unsigned long irq_status;
	const struct tcs_request *req;

	irq_status = readl_relaxed(drv->tcs_base + drv->regs[RSC_DRV_IRQ_STATUS]);

	for_each_set_bit(i, &irq_status, BITS_PER_TYPE(u32)) {
		req = get_req_from_tcs(drv, i, &ch);
		if (WARN_ON(!req))
			goto skip;

		trace_rpmh_tx_done(drv, i, req);
		ipc_log_string(drv->ipc_log_ctx, "IRQ response: m=%d", i);

		/*
		 * If wake tcs was re-purposed for sending active
		 * votes, clear AMC trigger & enable modes and
		 * disable interrupt for this TCS
		 */
		if (!drv->ch[ch].tcs[ACTIVE_TCS].num_tcs)
			__tcs_set_trigger(drv, i, false);
skip:
		/* Reclaim the TCS */

		write_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], i, 0);
		write_tcs_reg(drv, drv->regs[RSC_DRV_CMD_WAIT_FOR_CMPL], i, 0);
		writel_relaxed(BIT(i), drv->tcs_base + drv->regs[RSC_DRV_IRQ_CLEAR]);
		spin_lock(&drv->lock);
		clear_bit(i, drv->tcs_in_use);
		/*
		 * Disable interrupt for WAKE TCS to avoid being
		 * spammed with interrupts coming when the solver
		 * sends its wake votes.
		 */
		if (!drv->ch[ch].tcs[ACTIVE_TCS].num_tcs)
			enable_tcs_irq(drv, i, false);
		spin_unlock(&drv->lock);
		wake_up(&drv->tcs_wait);
		if (req)
			rpmh_tx_done(req);
	}

	return IRQ_HANDLED;
}

/**
 * __tcs_buffer_write() - Write to TCS hardware from a request; don't trigger.
 * @drv:    The controller.
 * @tcs_id: The global ID of this TCS.
 * @cmd_id: The index within the TCS to start writing.
 * @msg:    The message we want to send, which will contain several addr/data
 *          pairs to program (but few enough that they all fit in one TCS).
 *
 * This is used for all types of transfers (active, sleep, and wake).
 */
static void __tcs_buffer_write(struct rsc_drv *drv, int tcs_id, int cmd_id,
			       const struct tcs_request *msg)
{
	u32 msgid;
	u32 cmd_msgid = CMD_MSGID_LEN | CMD_MSGID_WRITE;
	u32 cmd_enable = 0;
	u32 cmd_complete;
	struct tcs_cmd *cmd;
	int i, j;

	cmd_msgid |= msg->wait_for_compl ? CMD_MSGID_RESP_REQ : 0;
	cmd_complete = read_tcs_reg(drv, drv->regs[RSC_DRV_CMD_WAIT_FOR_CMPL], tcs_id);

	for (i = 0, j = cmd_id; i < msg->num_cmds; i++, j++) {
		cmd = &msg->cmds[i];
		cmd_enable |= BIT(j);
		cmd_complete |= cmd->wait << j;
		msgid = cmd_msgid;
		msgid |= cmd->wait ? CMD_MSGID_RESP_REQ : 0;

		write_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_MSGID], tcs_id, j, msgid);
		write_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_ADDR], tcs_id, j, cmd->addr);
		write_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_DATA], tcs_id, j, cmd->data);
		trace_rpmh_send_msg(drv, tcs_id, j, msgid, cmd);
		ipc_log_string(drv->ipc_log_ctx,
			       "TCS write: m=%d n=%d msgid=%#x addr=%#x data=%#x wait=%d",
			       tcs_id, j, msgid, cmd->addr,
			       cmd->data, cmd->wait);
	}

	write_tcs_reg(drv, drv->regs[RSC_DRV_CMD_WAIT_FOR_CMPL], tcs_id, cmd_complete);
	cmd_enable |= read_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], tcs_id);
	write_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], tcs_id, cmd_enable);
}

/**
 * check_for_req_inflight() - Look to see if conflicting cmds are in flight.
 * @drv: The controller.
 * @tcs: A pointer to the tcs_group used for ACTIVE_ONLY transfers.
 * @msg: The message we want to send, which will contain several addr/data
 *       pairs to program (but few enough that they all fit in one TCS).
 *
 * This will walk through the TCSes in the group and check if any of them
 * appear to be sending to addresses referenced in the message. If it finds
 * one it'll return -EBUSY.
 *
 * Only for use for active-only transfers.
 *
 * Must be called with the drv->lock held since that protects tcs_in_use.
 *
 * Return: 0 if nothing in flight or -EBUSY if we should try again later.
 *         The caller must re-enable interrupts between tries since that's
 *         the only way tcs_in_use will ever be updated and the only way
 *         RSC_DRV_CMD_ENABLE will ever be cleared.
 */
static int check_for_req_inflight(struct rsc_drv *drv, struct tcs_group *tcs,
				  const struct tcs_request *msg)
{
	unsigned long curr_enabled;
	u32 addr;
	int j, k;
	int i = tcs->offset;
	unsigned long accl;

	for_each_set_bit_from(i, drv->tcs_in_use, tcs->offset + tcs->num_tcs) {
		curr_enabled = read_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], i);

		for_each_set_bit(j, &curr_enabled, tcs->ncpt) {
			addr = read_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_ADDR], i, j);
			for (k = 0; k < msg->num_cmds; k++) {
			/*
			 * Each RPMh VREG accelerator resource has 3 or 4 contiguous 4-byte
			 * aligned addresses associated with it. Ignore the offset to check
			 * for in-flight VREG requests.
			 */
				accl = ACCL_TYPE(msg->cmds[k].addr);
				if (accl == HW_ACCL_VREG &&
				    VREG_ADDR(addr) == VREG_ADDR(msg->cmds[k].addr))
					return -EBUSY;
				else if (addr == msg->cmds[k].addr)
					return -EBUSY;
			}
		}
	}

	return 0;
}

/**
 * find_free_tcs() - Find free tcs in the given tcs_group; only for active.
 * @tcs: A pointer to the active-only tcs_group (or the wake tcs_group if
 *       we borrowed it because there are zero active-only ones).
 *
 * Must be called with the drv->lock held since that protects tcs_in_use.
 *
 * Return: The first tcs that's free or -EBUSY if all in use.
 */
static int find_free_tcs(struct tcs_group *tcs)
{
	const struct rsc_drv *drv = tcs->drv;
	unsigned long i;
	unsigned long max = tcs->offset + tcs->num_tcs;
	int timeout = 100;

	i = find_next_zero_bit(drv->tcs_in_use, max, tcs->offset);
	if (i >= max)
		return -EBUSY;

	while (timeout) {
		if (read_tcs_reg(drv, drv->regs[RSC_DRV_STATUS], i))
			break;
		timeout--;
		udelay(1);
	}

	if (!timeout)
		return -EBUSY;

	return i;
}

/**
 * claim_tcs_for_req() - Claim a tcs in the given tcs_group; only for active.
 * @drv: The controller.
 * @tcs: The tcs_group used for ACTIVE_ONLY transfers.
 * @msg: The data to be sent.
 *
 * Claims a tcs in the given tcs_group while making sure that no existing cmd
 * is in flight that would conflict with the one in @msg.
 *
 * Context: Must be called with the drv->lock held since that protects
 * tcs_in_use.
 *
 * Return: The id of the claimed tcs or -EBUSY if a matching msg is in flight
 * or the tcs_group is full.
 */
static int claim_tcs_for_req(struct rsc_drv *drv, struct tcs_group *tcs,
			     const struct tcs_request *msg)
{
	int ret;

	/*
	 * The h/w does not like if we send a request to the same address,
	 * when one is already in-flight or being processed.
	 */
	ret = check_for_req_inflight(drv, tcs, msg);
	if (ret)
		return ret;

	return find_free_tcs(tcs);
}

/**
 * rpmh_rsc_send_data() - Write / trigger active-only message.
 * @drv: The controller.
 * @msg: The data to be sent.
 * @ch:  Channel number
 *
 * NOTES:
 * - This is only used for "ACTIVE_ONLY" since the limitations of this
 *   function don't make sense for sleep/wake cases.
 * - To do the transfer, we will grab a whole TCS for ourselves--we don't
 *   try to share. If there are none available we'll wait indefinitely
 *   for a free one.
 * - This function will not wait for the commands to be finished, only for
 *   data to be programmed into the RPMh. See rpmh_tx_done() which will
 *   be called when the transfer is fully complete.
 * - This function must be called with interrupts enabled. If the hardware
 *   is busy doing someone else's transfer we need that transfer to fully
 *   finish so that we can have the hardware, and to fully finish it needs
 *   the interrupt handler to run. If the interrupts is set to run on the
 *   active CPU this can never happen if interrupts are disabled.
 *
 * Return: 0 on success, -EINVAL on error.
 */
int rpmh_rsc_send_data(struct rsc_drv *drv, const struct tcs_request *msg, int ch)
{
	struct tcs_group *tcs;
	int tcs_id;
	unsigned long flags;

	tcs = get_tcs_for_msg(drv, msg->state, ch);
	if (IS_ERR(tcs))
		return PTR_ERR(tcs);

	spin_lock_irqsave(&drv->lock, flags);

	/* Controller is busy in 'solver' mode */
	if (drv->in_solver_mode) {
		spin_unlock_irqrestore(&drv->lock, flags);
		return -EBUSY;
	}

	/* Wait forever for a free tcs. It better be there eventually! */
	wait_event_lock_irq(drv->tcs_wait,
			    (tcs_id = claim_tcs_for_req(drv, tcs, msg)) >= 0,
			    drv->lock);

	tcs->req[tcs_id - tcs->offset] = msg;
	set_bit(tcs_id, drv->tcs_in_use);

	/*
	 * Clear previously programmed ACTIVE/WAKE commands in selected
	 * repurposed TCS to avoid triggering them. tcs->slots will be
	 * cleaned from rpmh_flush() by invoking rpmh_rsc_invalidate()
	 */
	write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CMD_ENABLE], tcs_id, 0);
	write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CMD_WAIT_FOR_CMPL], tcs_id, 0);

	if (msg->wait_for_compl || (msg->state == RPMH_ACTIVE_ONLY_STATE &&
	    tcs->type != ACTIVE_TCS))
		enable_tcs_irq(drv, tcs_id, true);
	else
		enable_tcs_irq(drv, tcs_id, false);

	/*
	 * These two can be done after the lock is released because:
	 * - We marked "tcs_in_use" under lock.
	 * - Once "tcs_in_use" has been marked nobody else could be writing
	 *   to these registers until the interrupt goes off.
	 * - The interrupt can't go off until we trigger w/ the last line
	 *   of __tcs_set_trigger() below.
	 */
	__tcs_buffer_write(drv, tcs_id, 0, msg);
	__tcs_set_trigger(drv, tcs_id, true);
	ipc_log_string(drv->ipc_log_ctx, "TCS trigger: m=%d wait_for_compl=%u",
		       tcs_id, msg->wait_for_compl);

	if (!msg->wait_for_compl)
		clear_bit(tcs_id, drv->tcs_in_use);

	spin_unlock_irqrestore(&drv->lock, flags);

	if (!msg->wait_for_compl)
		wake_up(&drv->tcs_wait);

	return 0;
}

/**
 * find_slots() - Find a place to write the given message.
 * @tcs:    The tcs group to search.
 * @msg:    The message we want to find room for.
 * @tcs_id: If we return 0 from the function, we return the global ID of the
 *          TCS to write to here.
 * @cmd_id: If we return 0 from the function, we return the index of
 *          the command array of the returned TCS where the client should
 *          start writing the message.
 *
 * Only for use on sleep/wake TCSes since those are the only ones we maintain
 * tcs->slots for.
 *
 * Return: -ENOMEM if there was no room, else 0.
 */
static int find_slots(struct tcs_group *tcs, const struct tcs_request *msg,
		      int *tcs_id, int *cmd_id)
{
	int slot, offset;
	int i = 0;

	/* Do over, until we can fit the full payload in a single TCS */
	do {
		slot = bitmap_find_next_zero_area(tcs->slots,
						  tcs->ncpt * tcs->num_tcs,
						  i, msg->num_cmds, 0);
		if (slot >= tcs->num_tcs * tcs->ncpt)
			return -ENOMEM;
		i += tcs->ncpt;
	} while (slot + msg->num_cmds - 1 >= i);

	bitmap_set(tcs->slots, slot, msg->num_cmds);

	offset = slot / tcs->ncpt;
	*tcs_id = offset + tcs->offset;
	*cmd_id = slot % tcs->ncpt;

	return 0;
}

/**
 * rpmh_rsc_write_ctrl_data() - Write request to controller but don't trigger.
 * @drv: The controller.
 * @msg: The data to be written to the controller.
 * @ch:  Channel number
 *
 * This should only be called for sleep/wake state, never active-only
 * state.
 *
 * The caller must ensure that no other RPMH actions are happening and the
 * controller is idle when this function is called since it runs lockless.
 *
 * Return: 0 if no error; else -error.
 */
int rpmh_rsc_write_ctrl_data(struct rsc_drv *drv, const struct tcs_request *msg, int ch)
{
	struct tcs_group *tcs;
	int tcs_id = 0, cmd_id = 0;
	int ret;

	if (!msg->num_cmds) {
		ipc_log_string(drv->ipc_log_ctx, "Empty num_cmds, returning");
		return 0;
	}

	tcs = get_tcs_for_msg(drv, msg->state, ch);
	if (IS_ERR(tcs))
		return PTR_ERR(tcs);

	/* find the TCS id and the command in the TCS to write to */
	ret = find_slots(tcs, msg, &tcs_id, &cmd_id);
	if (!ret)
		__tcs_buffer_write(drv, tcs_id, cmd_id, msg);

	return ret;
}

static struct tcs_group *get_tcs_from_index(struct rsc_drv *drv, int tcs_id)
{
	unsigned int i, j;

	for (i = 0; i < TCS_TYPE_NR; i++) {
		for (j = 0; j < MAX_CHANNEL; j++) {
			if (!drv->ch[j].initialized)
				continue;

			if (drv->ch[j].tcs[i].mask & BIT(tcs_id))
				return &drv->ch[j].tcs[i];
		}
	}

	return NULL;
}

static void print_tcs_info(struct rsc_drv *drv, int tcs_id, unsigned long *accl,
			   bool *aoss_irq_sts)
{
	int ch = 0;
	struct tcs_group *tcs_grp = get_tcs_from_index(drv, tcs_id);
	const struct tcs_request *req = get_req_from_tcs(drv, tcs_id, &ch);
	unsigned long cmds_enabled;
	u32 addr, data, msgid, sts, irq_sts;
	bool in_use = test_bit(tcs_id, drv->tcs_in_use);
	int i;

	sts = read_tcs_reg(drv, drv->regs[RSC_DRV_STATUS], tcs_id);
	cmds_enabled = read_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], tcs_id);
	if (!cmds_enabled || !tcs_grp)
		return;

	if (!req)
		goto print_tcs_data;

	data = read_tcs_reg(drv, drv->regs[RSC_DRV_CONTROL], tcs_id);
	irq_sts = readl_relaxed(drv->tcs_base + drv->regs[RSC_DRV_IRQ_STATUS]);
	pr_warn("Request: tcs-in-use:%s active_tcs=%s(%d) state=%d wait_for_compl=%u]\n",
		(in_use ? "YES" : "NO"),
		((tcs_grp->type == ACTIVE_TCS) ? "YES" : "NO"),
		tcs_grp->type, req->state, req->wait_for_compl);
	pr_warn("TCS=%d [ctrlr-sts:%s amc-mode:0x%x irq-sts:%s]\n",
		tcs_id, sts ? "IDLE" : "BUSY", data,
		(irq_sts & BIT(tcs_id)) ? "COMPLETED" : "PENDING");

	*aoss_irq_sts = (irq_sts & BIT(tcs_id)) ? true : false;

print_tcs_data:
	for_each_set_bit(i, &cmds_enabled, tcs_grp->ncpt) {
		addr = read_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_ADDR], tcs_id, i);
		data = read_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_DATA], tcs_id, i);
		msgid = read_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_MSGID], tcs_id, i);
		sts = read_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_STATUS], tcs_id, i);
		pr_warn("\tCMD=%d [addr=0x%x data=0x%x hdr=0x%x sts=0x%x enabled=1]\n",
			i, addr, data, msgid, sts);
		if (!(sts & CMD_STATUS_ISSUED))
			continue;
		if (!(sts & CMD_STATUS_COMPL))
			*accl |= BIT(ACCL_TYPE(addr));
	}
}

void rpmh_rsc_debug_channel_busy(struct rsc_drv *drv)
{
	u32 event_sts, ctrl_sts;
	u32 chn_update, chn_busy, chn_en;
	u32 seq_busy, seq_pc;

	pr_err("RSC:%s\n", drv->name);

	event_sts = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_TCS_COMPLETE]);
	ctrl_sts = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_TCS_TRIGGER]);
	chn_update = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_UPDATE]);
	chn_busy = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_BUSY]);
	chn_en = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_EN]);
	seq_busy = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_SEQ_BUSY]);
	seq_pc = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_SEQ_PC]);

	pr_err("event sts: 0x%x ctrl_sts: 0x%x\n", event_sts, ctrl_sts);
	pr_err("chn_update: 0x%x chn_busy: 0x%x chn_en: 0x%x\n", chn_update, chn_busy, chn_en);
	pr_err("seq_busy: 0x%x seq_pc: 0x%x\n", seq_busy, seq_pc);

	crm_dump_regs("cam_crm");
	crm_dump_drv_regs("cam_crm", drv->id);
}

void rpmh_rsc_debug(struct rsc_drv *drv, struct completion *compl)
{
	struct irq_data *rsc_irq_data = irq_get_irq_data(drv->irq);
	bool gic_irq_sts, aoss_irq_sts = false;
	int i;
	int busy = 0;
	unsigned long accl = 0;
	char str[20] = "";

	pr_warn("RSC:%s\n", drv->name);

	for (i = 0; i < drv->num_tcs; i++) {
		if (!test_bit(i, drv->tcs_in_use))
			continue;
		busy++;
		print_tcs_info(drv, i, &accl, &aoss_irq_sts);
	}

	if (!rsc_irq_data) {
		pr_err("No IRQ data for RSC:%s\n", drv->name);
		return;
	}

	irq_get_irqchip_state(drv->irq, IRQCHIP_STATE_PENDING, &gic_irq_sts);
	pr_warn("HW IRQ %lu is %s at GIC\n", rsc_irq_data->hwirq,
		gic_irq_sts ? "PENDING" : "NOT PENDING");
	pr_warn("Completion is %s to finish\n",
		completion_done(compl) ? "PENDING" : "NOT PENDING");

	for_each_set_bit(i, &accl, ARRAY_SIZE(accl_str)) {
		strlcat(str, accl_str[i], sizeof(str));
		strlcat(str, " ", sizeof(str));
	}

	if ((busy && !gic_irq_sts) || !aoss_irq_sts)
		pr_warn("ERROR:Accelerator(s) { %s } at AOSS did not respond\n",
			str);
	else if (gic_irq_sts)
		pr_warn("ERROR:Possible lockup in Linux\n");

	/* Show fast path status, if the TCS is busy */
	for (i = 0; i < MAX_CHANNEL; i++) {
		if (!drv->ch[i].initialized)
			continue;

		/* Show fast path status, if the TCS is busy */
		if (drv->ch[i].tcs[FAST_PATH_TCS].num_tcs) {
			int tcs_id = drv->ch[i].tcs[FAST_PATH_TCS].offset;
			bool sts = read_tcs_reg(drv,
						drv->regs[RSC_DRV_STATUS],
						tcs_id);

			if (!sts) {
				pr_err("Fast-path TCS information:\n");
				print_tcs_info(drv, tcs_id, &accl, &aoss_irq_sts);
			}
		}
	}

	/*
	 * The TCS(s) are busy waiting, we have no way to recover from this.
	 * If this debug function is called, we assume it's because timeout
	 * has happened.
	 * Crash and report.
	 */
	BUG_ON(busy);
}

/**
 * rpmh_rsc_ctrlr_is_busy() - Check if any of the AMCs are busy.
 * @drv: The controller
 *
 * Checks if any of the AMCs are busy in handling ACTIVE sets.
 * This is called from the last cpu powering down before flushing
 * SLEEP and WAKE sets. If AMCs are busy, controller can not enter
 * power collapse, so deny from the last cpu's pm notification.
 *
 * Context: Must be called with the drv->lock held.
 *
 * Return:
 * * False		- AMCs are idle
 * * True		- AMCs are busy
 */
static bool rpmh_rsc_ctrlr_is_busy(struct rsc_drv *drv)
{
	int i;
	struct tcs_group *tcs;
	unsigned long set;
	unsigned long max;

	for (i = 0; i < MAX_CHANNEL; i++) {
		if (!drv->ch[i].initialized)
			continue;

		tcs = &drv->ch[i].tcs[ACTIVE_TCS];
		/*
		 * If we made an active request on a RSC that does not have a
		 * dedicated TCS for active state use, then re-purposed wake TCSes
		 * should be checked for not busy, because we used wake TCSes for
		 * active requests in this case.
		 */
		if (!tcs->num_tcs)
			tcs = &drv->ch[i].tcs[WAKE_TCS];

		max = tcs->offset + tcs->num_tcs;
		set = find_next_bit(drv->tcs_in_use, max, tcs->offset);
		if (set < max)
			return true;

		/* Check if there is pending fastpath transaction */
		tcs = &drv->ch[i].tcs[FAST_PATH_TCS];
		if (tcs->num_tcs &&
		    !read_tcs_reg(drv, drv->regs[RSC_DRV_STATUS], tcs->offset))
			return true;
	}

	return false;
}

/**
 * rpmh_rsc_cpu_pm_callback() - Check if any of the AMCs are busy.
 * @nfb:    Pointer to the notifier block in struct rsc_drv.
 * @action: CPU_PM_ENTER, CPU_PM_ENTER_FAILED, or CPU_PM_EXIT.
 * @v:      Unused
 *
 * This function is given to cpu_pm_register_notifier so we can be informed
 * about when CPUs go down. When all CPUs go down we know no more active
 * transfers will be started so we write sleep/wake sets. This function gets
 * called from cpuidle code paths and also at system suspend time.
 *
 * If its last CPU going down and AMCs are not busy then writes cached sleep
 * and wake messages to TCSes. The firmware then takes care of triggering
 * them when entering deepest low power modes.
 *
 * Return: See cpu_pm_register_notifier()
 */
static int rpmh_rsc_cpu_pm_callback(struct notifier_block *nfb,
				    unsigned long action, void *v)
{
	struct rsc_drv *drv = container_of(nfb, struct rsc_drv, rsc_pm);
	int ret = NOTIFY_OK;
	int cpus_in_pm, ch;

	switch (action) {
	case CPU_PM_ENTER:
		cpus_in_pm = atomic_inc_return(&drv->cpus_in_pm);
		/*
		 * NOTE: comments for num_online_cpus() point out that it's
		 * only a snapshot so we need to be careful. It should be OK
		 * for us to use, though.  It's important for us not to miss
		 * if we're the last CPU going down so it would only be a
		 * problem if a CPU went offline right after we did the check
		 * AND that CPU was not idle AND that CPU was the last non-idle
		 * CPU. That can't happen. CPUs would have to come out of idle
		 * before the CPU could go offline.
		 */
		if (cpus_in_pm < num_online_cpus())
			return NOTIFY_OK;
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		atomic_dec(&drv->cpus_in_pm);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}

	/*
	 * It's likely we're on the last CPU. Grab the drv->lock and write
	 * out the sleep/wake commands to RPMH hardware. Grabbing the lock
	 * means that if we race with another CPU coming up we are still
	 * guaranteed to be safe. If another CPU came up just after we checked
	 * and has grabbed the lock or started an active transfer then we'll
	 * notice we're busy and abort. If another CPU comes up after we start
	 * flushing it will be blocked from starting an active transfer until
	 * we're done flushing. If another CPU starts an active transfer after
	 * we release the lock we're still OK because we're no longer the last
	 * CPU.
	 */
	if (spin_trylock(&drv->lock)) {
		ch = rpmh_rsc_get_channel(drv);
		if (ch < 0 || rpmh_rsc_ctrlr_is_busy(drv) || rpmh_flush(&drv->client, ch))
			ret = NOTIFY_BAD;
		spin_unlock(&drv->lock);
	} else {
		/* Another CPU must be up */
		return NOTIFY_OK;
	}

	if (ret == NOTIFY_BAD) {
		/* Double-check if we're here because someone else is up */
		if (cpus_in_pm < num_online_cpus())
			ret = NOTIFY_OK;
		else
			/* We won't be called w/ CPU_PM_ENTER_FAILED */
			atomic_dec(&drv->cpus_in_pm);
	}

	return ret;
}

/**
 * rpmh_rsc_mode_solver_set() - Enable/disable solver mode.
 * @drv:     The controller.
 * @enable:  Boolean state to be set - true/false
 *
 * Return:
 * * 0			- success
 * * -EBUSY		- AMCs are busy
 */
int rpmh_rsc_mode_solver_set(struct rsc_drv *drv, bool enable)
{
	int ret = -EBUSY;

	if (spin_trylock(&drv->lock)) {
		if (!enable || !rpmh_rsc_ctrlr_is_busy(drv)) {
			drv->in_solver_mode = enable;
			trace_rpmh_solver_set(drv, enable);
			ipc_log_string(drv->ipc_log_ctx,
				       "solver mode set: %d", enable);
			ret = 0;
		}
		spin_unlock(&drv->lock);
	}

	return ret;
}

int rpmh_rsc_is_tcs_completed(struct rsc_drv *drv, int ch)
{
	u32 sts;
	int retry = 10, ret = 0;

	do {
		sts = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_TCS_COMPLETE]);

		if (ch == 0)
			sts &= CH0_WAKE_TCS_STATUS;
		else
			sts &= CH1_WAKE_TCS_STATUS;

		retry--;
		/*
		 * Wait till all the WAKE votes of the new channel are
		 * applied during channel switch.
		 * Maximum delay of 100 usec.
		 */
		if (!sts)
			udelay(10);
	} while (!sts && retry);

	if (!retry) {
		ret = -EBUSY;
		goto exit;
	}

	writel_relaxed(CH_CLEAR_STATUS,
		       drv->base + drv->regs[RSC_DRV_CHN_TCS_COMPLETE]);

exit:
	trace_rpmh_switch_channel(drv, ch, ret);
	ipc_log_string(drv->ipc_log_ctx, "channel switched to: %d ret: %d", ch, ret);
	return 0;
}

/**
 * rpmh_rsc_switch_channel() - Switch to the channel
 * @drv:     The controller.
 * @ch:      The channel number to switch to.
 *
 * NOTE: Caller should ensure serialization before making this call.
 * Return:
 * * 0			- success
 * * -Error             - Error code
 */
int rpmh_rsc_switch_channel(struct rsc_drv *drv, int ch)
{
	writel_relaxed(BIT(ch), drv->base + drv->regs[RSC_DRV_CHN_UPDATE]);
	return rpmh_rsc_is_tcs_completed(drv, ch);
}

/**
 * rpmh_rsc_drv_enable() - Enable the DRV and trigger Wake vote
 * @drv:     The controller.
 *
 * NOTE: Caller should ensure serialization before making this call.
 * Return:
 * * 0			- success
 * * -Error             - Error code
 */
int rpmh_rsc_drv_enable(struct rsc_drv *drv, bool enable)
{
	int ret = 0, ch;
	u32 chn_en;

	spin_lock(&drv->lock);

	chn_en = readl_relaxed(drv->base + drv->regs[RSC_DRV_CHN_EN]);
	if (chn_en == enable) {
		ret = -EINVAL;
		goto exit;
	}

	if (enable) {
		/* Start with channel 0 */
		ch = 0;

		ret = rpmh_flush(&drv->client, ch);
		if (ret)
			goto exit;

		writel_relaxed(enable, drv->base + drv->regs[RSC_DRV_CHN_EN]);

		ret = rpmh_rsc_switch_channel(drv, ch);
		if (ret)
			goto exit;
	} else {
		/* Select unused channel */
		ch = rpmh_rsc_get_channel(drv);
		if (ch < 0)
			goto exit;

		ret = rpmh_flush(&drv->client, ch);
		if (ret)
			goto exit;

		ret = rpmh_rsc_switch_channel(drv, ch);
		if (ret)
			goto exit;

		writel_relaxed(0, drv->base + drv->regs[RSC_DRV_CHN_UPDATE]);
		writel_relaxed(enable, drv->base + drv->regs[RSC_DRV_CHN_EN]);
	}
exit:
	spin_unlock(&drv->lock);
	trace_rpmh_drv_enable(drv, enable, ret);
	ipc_log_string(drv->ipc_log_ctx, "drv enable: %d ret: %d", enable, ret);
	return ret;
}

/**
 * rpmh_rsc_init_fast_path() - Initialize the fast-path TCS contents
 * @drv:    The controller.
 * @msg:    The TCS request to populate.
 * @ch:     Channel number
 *
 * Return:
 * * 0			- success
 * * -ENODEV            - no fast-path TCS available
 */
int rpmh_rsc_init_fast_path(struct rsc_drv *drv, const struct tcs_request *msg, int ch)
{
	int tcs_id;

	if (!drv->ch[ch].tcs[FAST_PATH_TCS].num_tcs)
		return -ENODEV;

	tcs_id = drv->ch[ch].tcs[FAST_PATH_TCS].offset;

	/* We won't use the AMC IRQ to confirm if the TCS is free */
	enable_tcs_irq(drv, tcs_id, false);

	__tcs_buffer_write(drv, tcs_id, 0, msg);

	return 0;
}

/**
 * rpmh_rsc_update_fast_path() - Update the fast-path TCS data and trigger
 * @drv:     The controller.
 * @msg:     The TCS request data to be updated.
 * @mask:    The update mask for elements in @msg to be sent
 * @ch:      Channel number
 *
 * NOTE: Caller should ensure serialization before making this call.
 * Return:
 * * 0			- success
 * * -ENODEV            - no fast-path TCS available
 */
int rpmh_rsc_update_fast_path(struct rsc_drv *drv,
			      const struct tcs_request *msg,
			      u32 mask, int ch)
{
	int i;
	u32 sts;
	int tcs_id;
	struct tcs_cmd *cmd;
	int retry = 5;

	if (!drv->ch[ch].tcs[FAST_PATH_TCS].num_tcs)
		return -ENODEV;

	tcs_id = drv->ch[ch].tcs[FAST_PATH_TCS].offset;

	/* Ensure the TCS is free before writing to the TCS */
	do {
		sts = read_tcs_reg(drv, drv->regs[RSC_DRV_STATUS], tcs_id);
		if (!sts) {
			retry--;
			/* Report and bail, if it took too many attempts */
			if (!retry) {
				pr_err("Fast-path TCS is too busy\n");
				return -EBUSY;
			}
			udelay(1);
		}
	} while (!sts);

	/*
	 * We only update the data, everything else remains the same.
	 * The number of commands and the addresses do not change with
	 * updates.
	 */
	for (i = 0; i < msg->num_cmds; i++) {
		if (!(mask & BIT(i)))
			continue;
		cmd = &msg->cmds[i];
		write_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_DATA], tcs_id, i, cmd->data);
	}

	/* Trigger the TCS to send the request */
	__tcs_set_trigger(drv, tcs_id, true);

	return 0;
}

static int rpmh_rsc_poweroff_noirq(struct device *dev)
{
	return 0;
}

static void rpmh_rsc_tcs_irq_enable(struct rsc_drv *drv)
{
	u32 tcs_mask;
	int ch;

	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		if (!drv->ch[ch].initialized)
			continue;

		tcs_mask = readl_relaxed(drv->tcs_base + drv->regs[RSC_DRV_IRQ_ENABLE]);
		tcs_mask |= drv->ch[ch].tcs[ACTIVE_TCS].mask;
		writel_relaxed(tcs_mask, drv->tcs_base + drv->regs[RSC_DRV_IRQ_ENABLE]);
	}
}

static int rpmh_rsc_restore_noirq(struct device *dev)
{
	struct rsc_drv_top *rsc_top = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < rsc_top->drv_count; i++) {
		if (rsc_top->drv[i].initialized)
			rpmh_rsc_tcs_irq_enable(&rsc_top->drv[i]);
	}

	return 0;
}

static struct rsc_drv_top *rpmh_rsc_get_top_device(const char *name)
{
	struct rsc_drv_top *rsc_top;
	bool rsc_dev_present = false;

	list_for_each_entry(rsc_top, &rpmh_rsc_dev_list, list) {
		if (!strcmp(name, rsc_top->name)) {
			rsc_dev_present = true;
			break;
		}
	}

	if (!rsc_dev_present)
		return ERR_PTR(-ENODEV);

	return rsc_top;
}

const struct device *rpmh_rsc_get_device(const char *name, u32 drv_id)
{
	struct rsc_drv_top *rsc_top = rpmh_rsc_get_top_device(name);
	int i;

	if (IS_ERR(rsc_top))
		return ERR_PTR(-ENODEV);

	for (i = 0; i < rsc_top->drv_count; i++) {
		if (i == drv_id && rsc_top->drv[i].initialized)
			return rsc_top->drv[i].dev;
	}

	return ERR_PTR(-ENODEV);
}

static int rpmh_probe_channel_tcs_config(struct device_node *np,
					 struct rsc_drv *drv,
					 u32 max_tcs, u32 ncpt, int ch)
{
	struct tcs_type_config {
		u32 type;
		u32 n;
	} tcs_cfg[TCS_TYPE_NR] = { { 0 } };
	struct tcs_group *tcs;
	struct drv_channel *channel = &drv->ch[ch];
	int i, ret, n, st = 0;
	u32 tcs_mask;

	n = of_property_count_u32_elems(np, "qcom,tcs-config");
	if (n != 2 * TCS_TYPE_NR)
		return -EINVAL;

	for (i = 0; i < TCS_TYPE_NR; i++) {
		ret = of_property_read_u32_index(np, "qcom,tcs-config",
						 i * 2, &tcs_cfg[i].type);
		if (ret)
			return ret;
		if (tcs_cfg[i].type >= TCS_TYPE_NR)
			return -EINVAL;

		ret = of_property_read_u32_index(np, "qcom,tcs-config",
						 i * 2 + 1, &tcs_cfg[i].n);
		if (ret)
			return ret;
		if (tcs_cfg[i].n > MAX_TCS_PER_TYPE)
			return -EINVAL;
	}

	for (i = 0; i < TCS_TYPE_NR; i++) {
		tcs = &channel->tcs[tcs_cfg[i].type];
		if (tcs->drv)
			return -EINVAL;
		tcs->drv = drv;
		tcs->type = tcs_cfg[i].type;
		tcs->num_tcs = tcs_cfg[i].n;
		tcs->ncpt = ncpt;

		if (!tcs->num_tcs || tcs->type == CONTROL_TCS)
			continue;

		if (st + tcs->num_tcs > max_tcs ||
		    st + tcs->num_tcs >= BITS_PER_BYTE * sizeof(tcs->mask))
			return -EINVAL;

		tcs->mask = ((1 << tcs->num_tcs) - 1) << (st + drv->num_tcs);
		tcs->offset = st + drv->num_tcs;
		st += tcs->num_tcs;
	}

	/* Enable the active TCS to send requests immediately */
	tcs_mask = readl_relaxed(drv->tcs_base + drv->regs[RSC_DRV_IRQ_ENABLE]);
	tcs_mask |= drv->ch[ch].tcs[ACTIVE_TCS].mask;
	writel_relaxed(tcs_mask, drv->tcs_base + drv->regs[RSC_DRV_IRQ_ENABLE]);

	channel->drv = drv;
	channel->initialized = true;
	drv->num_tcs += st;

	return 0;
}

static int rpmh_probe_tcs_config(struct rsc_drv *drv)
{
	struct device_node *cn, *np = drv->dev->of_node;
	int ch = 0, ret;
	u32 offset, config;
	u32 max_tcs, ncpt;

	ret = of_property_read_u32(np, "qcom,tcs-offset", &offset);
	if (ret)
		return ret;
	drv->tcs_base = drv->base + offset;

	config = readl_relaxed(drv->base + drv->regs[DRV_PRNT_CHLD_CONFIG]);

	max_tcs = config;
	max_tcs &= DRV_NUM_TCS_MASK << (DRV_NUM_TCS_SHIFT * drv->id);
	max_tcs = max_tcs >> (DRV_NUM_TCS_SHIFT * drv->id);

	ncpt = config & (DRV_NCPT_MASK << DRV_NCPT_SHIFT);
	ncpt = ncpt >> DRV_NCPT_SHIFT;
	for_each_child_of_node(np, cn) {

		if (!of_node_name_eq(cn, "channel"))
			continue;

		ret = rpmh_probe_channel_tcs_config(cn, drv, max_tcs, ncpt, ch);
		if (ret)
			return ret;
		ch++;
	}
	drv->num_channels = ch;

	return 0;
}

static int rpmh_rsc_pd_cb(struct notifier_block *nb,
			  unsigned long action, void *data)
{
	struct rsc_drv *drv = container_of(nb, struct rsc_drv, genpd_nb);
	int ch;

	if (action != GENPD_NOTIFY_PRE_OFF)
		return NOTIFY_OK;

	ch = rpmh_rsc_get_channel(drv);

	/* We don't need to lock as domin on/off are serialized */
	if (ch < 0 || rpmh_rsc_ctrlr_is_busy(drv) || _rpmh_flush(&drv->client, ch))
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static int rpmh_rsc_pd_attach(struct rsc_drv *drv)
{
	int ret;

	pm_runtime_enable(&drv->pdev->dev);
	ret = dev_pm_domain_attach(&drv->pdev->dev, false);
	if (ret)
		return ret;

	drv->genpd_nb.notifier_call = rpmh_rsc_pd_cb;
	return dev_pm_genpd_add_notifier(&drv->pdev->dev, &drv->genpd_nb);
}

static int rpmh_rsc_probe(struct platform_device *pdev)
{
	struct device_node *np, *dn = pdev->dev.of_node;
	struct rsc_drv *drv;
	struct rsc_drv_top *rsc_top;
	int ret, irq;
	u32 rsc_id, major_ver, minor_ver, solver_config;
	int i, j, drv_count;
	const char *name;

	/*
	 * Even though RPMh doesn't directly use cmd-db, all of its children
	 * do. To avoid adding this check to our children we'll do it now.
	 */
	ret = cmd_db_ready();
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Command DB not available (%d)\n",
									ret);
		return ret;
	}

	rpmh_standalone = cmd_db_is_standalone();
	if (rpmh_standalone)
		dev_info(&pdev->dev, "RPMH is running in standalone mode.\n");


	rsc_top = devm_kzalloc(&pdev->dev, sizeof(*rsc_top), GFP_KERNEL);
	if (!rsc_top)
		return -ENOMEM;

	ret = of_property_read_u32(dn, "qcom,drv-count", &drv_count);
	if (ret)
		return ret;

	drv = devm_kcalloc(&pdev->dev, drv_count, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	name = of_get_property(dn, "label", NULL);
	if (!name)
		name = dev_name(&pdev->dev);

	rsc_top->drv_count = drv_count;
	rsc_top->drv = drv;
	rsc_top->dev = &pdev->dev;
	scnprintf(rsc_top->name, sizeof(rsc_top->name), "%s", name);

	for_each_child_of_node(dn, np) {
		struct device *drv_dev;

		if (!of_node_name_eq(np, "drv"))
			continue;

		ret = of_property_read_u32(np, "qcom,drv-id", &i);
		if (ret)
			return ret;

		scnprintf(drv[i].name, sizeof(drv[i].name), "%s-drv-%d", name, i);

		drv[i].base = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(drv[i].base))
			return PTR_ERR(drv[i].base);

		drv_dev = kzalloc(sizeof(*drv_dev), GFP_KERNEL);
		if (!drv_dev)
			return -ENOMEM;

		drv[i].id = i;
		drv[i].pdev = pdev;
		drv[i].dev = drv_dev;
		drv_dev->parent = &pdev->dev;
		drv_dev->of_node = np;
		dev_set_name(drv_dev, "%s:%pOFn%d", dev_name(drv_dev->parent), np, i);
		ret = device_register(drv_dev);
		if (ret)
			return ret;

		rsc_id = readl_relaxed(drv[i].base + RSC_DRV_ID);
		major_ver = rsc_id & (MAJOR_VER_MASK << MAJOR_VER_SHIFT);
		major_ver >>= MAJOR_VER_SHIFT;
		minor_ver = rsc_id & (MINOR_VER_MASK << MINOR_VER_SHIFT);
		minor_ver >>= MINOR_VER_SHIFT;

		if (major_ver >= 3 && minor_ver >= 0)
			drv[i].regs = rpmh_rsc_reg_offsets_ver_3_0;
		else
			drv[i].regs = rpmh_rsc_reg_offsets_ver_2_7;

		ret = rpmh_probe_tcs_config(&drv[i]);
		if (ret)
			return ret;

		dev_set_drvdata(drv_dev, &drv[i]);
		drv[i].initialized = true;
	}

	for (i = 0; i < drv_count; i++) {
		if (!drv[i].initialized)
			continue;

		/*
		 * CPU PM notification are not required for controllers that support
		 * 'HW solver' mode where they can be in autonomous mode executing low
		 * power mode to power down.
		 */
		solver_config = readl_relaxed(drv[i].base +
					      drv[i].regs[DRV_SOLVER_CONFIG]);
		solver_config &= DRV_HW_SOLVER_MASK << DRV_HW_SOLVER_SHIFT;
		solver_config = solver_config >> DRV_HW_SOLVER_SHIFT;
		if (of_find_property(dn, "power-domains", NULL)) {
			ret = rpmh_rsc_pd_attach(&drv[i]);
			if (ret)
				return ret;
		} else if (!solver_config &&
			   !of_find_property(dn, "qcom,hw-channel", NULL)) {
			drv[i].rsc_pm.notifier_call = rpmh_rsc_cpu_pm_callback;
			cpu_pm_register_notifier(&drv[i].rsc_pm);
		} else if (solver_config) {
			drv[i].client.flags = SOLVER_PRESENT;
		} else {
		/*
		 * The requets for HW channel TCSes has to be either
		 * RPMH_SLEEP_STATE or RPMH_WAKE_ONLY_STATE.
		 *
		 * Assume 'solver' state which does nothing but to disallow
		 * RPMH_ACTIVE_ONLY_STATE requests.
		 */
			drv[i].client.flags = SOLVER_PRESENT | HW_CHANNEL_PRESENT;
			drv[i].client.in_solver_mode = true;
			drv[i].in_solver_mode = true;
			drv[i].regs = rpmh_rsc_reg_offsets_ver_3_0_hw_channel;
		}

		spin_lock_init(&drv[i].lock);
		init_waitqueue_head(&drv[i].tcs_wait);
		bitmap_zero(drv[i].tcs_in_use, MAX_TCS_NR);
		drv[i].client.non_batch_cache = devm_kcalloc(&pdev->dev, CMD_DB_MAX_RESOURCES,
							     sizeof(struct cache_req), GFP_KERNEL);
		if (!drv[i].client.non_batch_cache)
			return -ENOMEM;

		for (j = 0; j < CMD_DB_MAX_RESOURCES; j++)
			INIT_LIST_HEAD(&drv[i].client.non_batch_cache[j].list);

		irq = platform_get_irq(pdev, drv[i].id);
		if (irq < 0)
			return irq;

		drv[i].irq = irq;

		ret = devm_request_irq(&pdev->dev, irq, tcs_tx_done,
				       IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND,
				       drv[i].name, &drv[i]);
		if (ret)
			return ret;

		spin_lock_init(&drv[i].client.cache_lock);

		drv[i].ipc_log_ctx = ipc_log_context_create(
						RSC_DRV_IPC_LOG_SIZE,
						drv[i].name, 0);

		if (__rsc_count < MAX_RSC_COUNT)
			__rsc_drv[__rsc_count++] = &drv[i];

		ret = devm_of_platform_populate(drv[i].dev);
		if (ret)
			return ret;
	}

	INIT_LIST_HEAD(&rsc_top->list);
	list_add_tail(&rsc_top->list, &rpmh_rsc_dev_list);
	dev_set_drvdata(&pdev->dev, rsc_top);

	return devm_of_platform_populate(&pdev->dev);
}

static const struct dev_pm_ops rpmh_rsc_dev_pm_ops = {
	.poweroff_noirq = rpmh_rsc_poweroff_noirq,
	.restore_noirq = rpmh_rsc_restore_noirq,
};

static const struct of_device_id rpmh_drv_match[] = {
	{ .compatible = "qcom,rpmh-rsc", },
	{ }
};
MODULE_DEVICE_TABLE(of, rpmh_drv_match);

static struct platform_driver rpmh_driver = {
	.probe = rpmh_rsc_probe,
	.driver = {
		  .name = "rpmh",
		  .of_match_table = rpmh_drv_match,
		  .pm = &rpmh_rsc_dev_pm_ops,
		  .suppress_bind_attrs = true,
	},
};

static int __init rpmh_driver_init(void)
{
	return platform_driver_register(&rpmh_driver);
}
arch_initcall(rpmh_driver_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. RPMh Driver");
MODULE_LICENSE("GPL v2");
