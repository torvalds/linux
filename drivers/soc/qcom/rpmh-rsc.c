// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
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
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/tcs.h>
#include <dt-bindings/soc/qcom,rpmh-rsc.h>

#include "rpmh-internal.h"

#define CREATE_TRACE_POINTS
#include "trace-rpmh.h"

#define RSC_DRV_TCS_OFFSET		672
#define RSC_DRV_CMD_OFFSET		20

/* DRV HW Solver Configuration Information Register */
#define DRV_SOLVER_CONFIG		0x04
#define DRV_HW_SOLVER_MASK		1
#define DRV_HW_SOLVER_SHIFT		24

/* DRV TCS Configuration Information Register */
#define DRV_PRNT_CHLD_CONFIG		0x0C
#define DRV_NUM_TCS_MASK		0x3F
#define DRV_NUM_TCS_SHIFT		6
#define DRV_NCPT_MASK			0x1F
#define DRV_NCPT_SHIFT			27

/* Offsets for common TCS Registers, one bit per TCS */
#define RSC_DRV_IRQ_ENABLE		0x00
#define RSC_DRV_IRQ_STATUS		0x04
#define RSC_DRV_IRQ_CLEAR		0x08	/* w/o; write 1 to clear */

/*
 * Offsets for per TCS Registers.
 *
 * TCSes start at 0x10 from tcs_base and are stored one after another.
 * Multiply tcs_id by RSC_DRV_TCS_OFFSET to find a given TCS and add one
 * of the below to find a register.
 */
#define RSC_DRV_CMD_WAIT_FOR_CMPL	0x10	/* 1 bit per command */
#define RSC_DRV_CONTROL			0x14
#define RSC_DRV_STATUS			0x18	/* zero if tcs is busy */
#define RSC_DRV_CMD_ENABLE		0x1C	/* 1 bit per command */

/*
 * Offsets for per command in a TCS.
 *
 * Commands (up to 16) start at 0x30 in a TCS; multiply command index
 * by RSC_DRV_CMD_OFFSET and add one of the below to find a register.
 */
#define RSC_DRV_CMD_MSGID		0x30
#define RSC_DRV_CMD_ADDR		0x34
#define RSC_DRV_CMD_DATA		0x38
#define RSC_DRV_CMD_STATUS		0x3C
#define RSC_DRV_CMD_RESP_DATA		0x40

#define TCS_AMC_MODE_ENABLE		BIT(16)
#define TCS_AMC_MODE_TRIGGER		BIT(24)

/* TCS CMD register bit mask */
#define CMD_MSGID_LEN			8
#define CMD_MSGID_RESP_REQ		BIT(8)
#define CMD_MSGID_WRITE			BIT(16)
#define CMD_STATUS_ISSUED		BIT(8)
#define CMD_STATUS_COMPL		BIT(16)

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

static inline void __iomem *
tcs_reg_addr(const struct rsc_drv *drv, int reg, int tcs_id)
{
	return drv->tcs_base + RSC_DRV_TCS_OFFSET * tcs_id + reg;
}

static inline void __iomem *
tcs_cmd_addr(const struct rsc_drv *drv, int reg, int tcs_id, int cmd_id)
{
	return tcs_reg_addr(drv, reg, tcs_id) + RSC_DRV_CMD_OFFSET * cmd_id;
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
 *
 * This will clear the "slots" variable of the given tcs_group and also
 * tell the hardware to forget about all entries.
 *
 * The caller must ensure that no other RPMH actions are happening when this
 * function is called, since otherwise the device may immediately become
 * used again even before this function exits.
 */
static void tcs_invalidate(struct rsc_drv *drv, int type)
{
	int m;
	struct tcs_group *tcs = &drv->tcs[type];

	/* Caller ensures nobody else is running so no lock */
	if (bitmap_empty(tcs->slots, MAX_TCS_SLOTS))
		return;

	for (m = tcs->offset; m < tcs->offset + tcs->num_tcs; m++)
		write_tcs_reg_sync(drv, RSC_DRV_CMD_ENABLE, m, 0);

	bitmap_zero(tcs->slots, MAX_TCS_SLOTS);
}

/**
 * rpmh_rsc_invalidate() - Invalidate sleep and wake TCSes.
 * @drv: The RSC controller.
 *
 * The caller must ensure that no other RPMH actions are happening when this
 * function is called, since otherwise the device may immediately become
 * used again even before this function exits.
 */
void rpmh_rsc_invalidate(struct rsc_drv *drv)
{
	tcs_invalidate(drv, SLEEP_TCS);
	tcs_invalidate(drv, WAKE_TCS);
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
					 const struct tcs_request *msg)
{
	int type;
	struct tcs_group *tcs;

	switch (msg->state) {
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
	tcs = &drv->tcs[type];
	if (msg->state == RPMH_ACTIVE_ONLY_STATE && !tcs->num_tcs)
		tcs = &drv->tcs[WAKE_TCS];

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
						  int tcs_id)
{
	struct tcs_group *tcs;
	int i;

	for (i = 0; i < TCS_TYPE_NR; i++) {
		tcs = &drv->tcs[i];
		if (tcs->mask & BIT(tcs_id))
			return tcs->req[tcs_id - tcs->offset];
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
	enable = read_tcs_reg(drv, RSC_DRV_CONTROL, tcs_id);
	enable &= ~TCS_AMC_MODE_TRIGGER;
	write_tcs_reg_sync(drv, RSC_DRV_CONTROL, tcs_id, enable);
	enable &= ~TCS_AMC_MODE_ENABLE;
	write_tcs_reg_sync(drv, RSC_DRV_CONTROL, tcs_id, enable);

	if (trigger) {
		/* Enable the AMC mode on the TCS and then trigger the TCS */
		enable = TCS_AMC_MODE_ENABLE;
		write_tcs_reg_sync(drv, RSC_DRV_CONTROL, tcs_id, enable);
		enable |= TCS_AMC_MODE_TRIGGER;
		write_tcs_reg(drv, RSC_DRV_CONTROL, tcs_id, enable);
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

	data = readl_relaxed(drv->tcs_base + RSC_DRV_IRQ_ENABLE);
	if (enable)
		data |= BIT(tcs_id);
	else
		data &= ~BIT(tcs_id);
	writel_relaxed(data, drv->tcs_base + RSC_DRV_IRQ_ENABLE);
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
	int i, j, err = 0;
	unsigned long irq_status;
	const struct tcs_request *req;
	struct tcs_cmd *cmd;

	irq_status = readl_relaxed(drv->tcs_base + RSC_DRV_IRQ_STATUS);

	for_each_set_bit(i, &irq_status, BITS_PER_LONG) {
		req = get_req_from_tcs(drv, i);
		if (!req) {
			WARN_ON(1);
			goto skip;
		}

		err = 0;
		for (j = 0; j < req->num_cmds; j++) {
			u32 sts;

			cmd = &req->cmds[j];
			sts = read_tcs_cmd(drv, RSC_DRV_CMD_STATUS, i, j);
			if (!(sts & CMD_STATUS_ISSUED) ||
			   ((req->wait_for_compl || cmd->wait) &&
			   !(sts & CMD_STATUS_COMPL))) {
				pr_err("Incomplete request: %s: addr=%#x data=%#x",
				       drv->name, cmd->addr, cmd->data);
				err = -EIO;
			}
		}

		trace_rpmh_tx_done(drv, i, req, err);

		/*
		 * If wake tcs was re-purposed for sending active
		 * votes, clear AMC trigger & enable modes and
		 * disable interrupt for this TCS
		 */
		if (!drv->tcs[ACTIVE_TCS].num_tcs)
			__tcs_set_trigger(drv, i, false);
skip:
		/* Reclaim the TCS */
		write_tcs_reg(drv, RSC_DRV_CMD_ENABLE, i, 0);
		writel_relaxed(BIT(i), drv->tcs_base + RSC_DRV_IRQ_CLEAR);
		spin_lock(&drv->lock);
		clear_bit(i, drv->tcs_in_use);
		/*
		 * Disable interrupt for WAKE TCS to avoid being
		 * spammed with interrupts coming when the solver
		 * sends its wake votes.
		 */
		if (!drv->tcs[ACTIVE_TCS].num_tcs)
			enable_tcs_irq(drv, i, false);
		spin_unlock(&drv->lock);
		wake_up(&drv->tcs_wait);
		if (req)
			rpmh_tx_done(req, err);
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
	struct tcs_cmd *cmd;
	int i, j;

	/* Convert all commands to RR when the request has wait_for_compl set */
	cmd_msgid |= msg->wait_for_compl ? CMD_MSGID_RESP_REQ : 0;

	for (i = 0, j = cmd_id; i < msg->num_cmds; i++, j++) {
		cmd = &msg->cmds[i];
		cmd_enable |= BIT(j);
		msgid = cmd_msgid;
		/*
		 * Additionally, if the cmd->wait is set, make the command
		 * response reqd even if the overall request was fire-n-forget.
		 */
		msgid |= cmd->wait ? CMD_MSGID_RESP_REQ : 0;

		write_tcs_cmd(drv, RSC_DRV_CMD_MSGID, tcs_id, j, msgid);
		write_tcs_cmd(drv, RSC_DRV_CMD_ADDR, tcs_id, j, cmd->addr);
		write_tcs_cmd(drv, RSC_DRV_CMD_DATA, tcs_id, j, cmd->data);
		trace_rpmh_send_msg(drv, tcs_id, j, msgid, cmd);
	}

	cmd_enable |= read_tcs_reg(drv, RSC_DRV_CMD_ENABLE, tcs_id);
	write_tcs_reg(drv, RSC_DRV_CMD_ENABLE, tcs_id, cmd_enable);
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

	for_each_set_bit_from(i, drv->tcs_in_use, tcs->offset + tcs->num_tcs) {
		curr_enabled = read_tcs_reg(drv, RSC_DRV_CMD_ENABLE, i);

		for_each_set_bit(j, &curr_enabled, MAX_CMDS_PER_TCS) {
			addr = read_tcs_cmd(drv, RSC_DRV_CMD_ADDR, i, j);
			for (k = 0; k < msg->num_cmds; k++) {
				if (addr == msg->cmds[k].addr)
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

	i = find_next_zero_bit(drv->tcs_in_use, max, tcs->offset);
	if (i >= max)
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
int rpmh_rsc_send_data(struct rsc_drv *drv, const struct tcs_request *msg)
{
	struct tcs_group *tcs;
	int tcs_id;
	unsigned long flags;

	tcs = get_tcs_for_msg(drv, msg);
	if (IS_ERR(tcs))
		return PTR_ERR(tcs);

	spin_lock_irqsave(&drv->lock, flags);

	/* Wait forever for a free tcs. It better be there eventually! */
	wait_event_lock_irq(drv->tcs_wait,
			    (tcs_id = claim_tcs_for_req(drv, tcs, msg)) >= 0,
			    drv->lock);

	tcs->req[tcs_id - tcs->offset] = msg;
	set_bit(tcs_id, drv->tcs_in_use);
	if (msg->state == RPMH_ACTIVE_ONLY_STATE && tcs->type != ACTIVE_TCS) {
		/*
		 * Clear previously programmed WAKE commands in selected
		 * repurposed TCS to avoid triggering them. tcs->slots will be
		 * cleaned from rpmh_flush() by invoking rpmh_rsc_invalidate()
		 */
		write_tcs_reg_sync(drv, RSC_DRV_CMD_ENABLE, tcs_id, 0);
		enable_tcs_irq(drv, tcs_id, true);
	}
	spin_unlock_irqrestore(&drv->lock, flags);

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
		slot = bitmap_find_next_zero_area(tcs->slots, MAX_TCS_SLOTS,
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
 *
 * This should only be called for for sleep/wake state, never active-only
 * state.
 *
 * The caller must ensure that no other RPMH actions are happening and the
 * controller is idle when this function is called since it runs lockless.
 *
 * Return: 0 if no error; else -error.
 */
int rpmh_rsc_write_ctrl_data(struct rsc_drv *drv, const struct tcs_request *msg)
{
	struct tcs_group *tcs;
	int tcs_id = 0, cmd_id = 0;
	int ret;

	tcs = get_tcs_for_msg(drv, msg);
	if (IS_ERR(tcs))
		return PTR_ERR(tcs);

	/* find the TCS id and the command in the TCS to write to */
	ret = find_slots(tcs, msg, &tcs_id, &cmd_id);
	if (!ret)
		__tcs_buffer_write(drv, tcs_id, cmd_id, msg);

	return ret;
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
	unsigned long set;
	const struct tcs_group *tcs = &drv->tcs[ACTIVE_TCS];
	unsigned long max;

	/*
	 * If we made an active request on a RSC that does not have a
	 * dedicated TCS for active state use, then re-purposed wake TCSes
	 * should be checked for not busy, because we used wake TCSes for
	 * active requests in this case.
	 */
	if (!tcs->num_tcs)
		tcs = &drv->tcs[WAKE_TCS];

	max = tcs->offset + tcs->num_tcs;
	set = find_next_bit(drv->tcs_in_use, max, tcs->offset);

	return set < max;
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
	int cpus_in_pm;

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
		if (rpmh_rsc_ctrlr_is_busy(drv) || rpmh_flush(&drv->client))
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

static int rpmh_probe_tcs_config(struct platform_device *pdev,
				 struct rsc_drv *drv, void __iomem *base)
{
	struct tcs_type_config {
		u32 type;
		u32 n;
	} tcs_cfg[TCS_TYPE_NR] = { { 0 } };
	struct device_node *dn = pdev->dev.of_node;
	u32 config, max_tcs, ncpt, offset;
	int i, ret, n, st = 0;
	struct tcs_group *tcs;

	ret = of_property_read_u32(dn, "qcom,tcs-offset", &offset);
	if (ret)
		return ret;
	drv->tcs_base = base + offset;

	config = readl_relaxed(base + DRV_PRNT_CHLD_CONFIG);

	max_tcs = config;
	max_tcs &= DRV_NUM_TCS_MASK << (DRV_NUM_TCS_SHIFT * drv->id);
	max_tcs = max_tcs >> (DRV_NUM_TCS_SHIFT * drv->id);

	ncpt = config & (DRV_NCPT_MASK << DRV_NCPT_SHIFT);
	ncpt = ncpt >> DRV_NCPT_SHIFT;

	n = of_property_count_u32_elems(dn, "qcom,tcs-config");
	if (n != 2 * TCS_TYPE_NR)
		return -EINVAL;

	for (i = 0; i < TCS_TYPE_NR; i++) {
		ret = of_property_read_u32_index(dn, "qcom,tcs-config",
						 i * 2, &tcs_cfg[i].type);
		if (ret)
			return ret;
		if (tcs_cfg[i].type >= TCS_TYPE_NR)
			return -EINVAL;

		ret = of_property_read_u32_index(dn, "qcom,tcs-config",
						 i * 2 + 1, &tcs_cfg[i].n);
		if (ret)
			return ret;
		if (tcs_cfg[i].n > MAX_TCS_PER_TYPE)
			return -EINVAL;
	}

	for (i = 0; i < TCS_TYPE_NR; i++) {
		tcs = &drv->tcs[tcs_cfg[i].type];
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

		tcs->mask = ((1 << tcs->num_tcs) - 1) << st;
		tcs->offset = st;
		st += tcs->num_tcs;
	}

	drv->num_tcs = st;

	return 0;
}

static int rpmh_rsc_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct rsc_drv *drv;
	struct resource *res;
	char drv_id[10] = {0};
	int ret, irq;
	u32 solver_config;
	void __iomem *base;

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

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	ret = of_property_read_u32(dn, "qcom,drv-id", &drv->id);
	if (ret)
		return ret;

	drv->name = of_get_property(dn, "label", NULL);
	if (!drv->name)
		drv->name = dev_name(&pdev->dev);

	snprintf(drv_id, ARRAY_SIZE(drv_id), "drv-%d", drv->id);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, drv_id);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = rpmh_probe_tcs_config(pdev, drv, base);
	if (ret)
		return ret;

	spin_lock_init(&drv->lock);
	init_waitqueue_head(&drv->tcs_wait);
	bitmap_zero(drv->tcs_in_use, MAX_TCS_NR);

	irq = platform_get_irq(pdev, drv->id);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, tcs_tx_done,
			       IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND,
			       drv->name, drv);
	if (ret)
		return ret;

	/*
	 * CPU PM notification are not required for controllers that support
	 * 'HW solver' mode where they can be in autonomous mode executing low
	 * power mode to power down.
	 */
	solver_config = readl_relaxed(base + DRV_SOLVER_CONFIG);
	solver_config &= DRV_HW_SOLVER_MASK << DRV_HW_SOLVER_SHIFT;
	solver_config = solver_config >> DRV_HW_SOLVER_SHIFT;
	if (!solver_config) {
		drv->rsc_pm.notifier_call = rpmh_rsc_cpu_pm_callback;
		cpu_pm_register_notifier(&drv->rsc_pm);
	}

	/* Enable the active TCS to send requests immediately */
	writel_relaxed(drv->tcs[ACTIVE_TCS].mask,
		       drv->tcs_base + RSC_DRV_IRQ_ENABLE);

	spin_lock_init(&drv->client.cache_lock);
	INIT_LIST_HEAD(&drv->client.cache);
	INIT_LIST_HEAD(&drv->client.batch_cache);

	dev_set_drvdata(&pdev->dev, drv);

	return devm_of_platform_populate(&pdev->dev);
}

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
