// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "%s " fmt, KBUILD_MODNAME

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/tcs.h>
#include <dt-bindings/soc/qcom,rpmh-rsc.h>

#include "rpmh-internal.h"

#define CREATE_TRACE_POINTS
#include "trace-rpmh.h"

#define RSC_DRV_TCS_OFFSET		672
#define RSC_DRV_CMD_OFFSET		20

/* DRV Configuration Information Register */
#define DRV_PRNT_CHLD_CONFIG		0x0C
#define DRV_NUM_TCS_MASK		0x3F
#define DRV_NUM_TCS_SHIFT		6
#define DRV_NCPT_MASK			0x1F
#define DRV_NCPT_SHIFT			27

/* Register offsets */
#define RSC_DRV_IRQ_ENABLE		0x00
#define RSC_DRV_IRQ_STATUS		0x04
#define RSC_DRV_IRQ_CLEAR		0x08
#define RSC_DRV_CMD_WAIT_FOR_CMPL	0x10
#define RSC_DRV_CONTROL			0x14
#define RSC_DRV_STATUS			0x18
#define RSC_DRV_CMD_ENABLE		0x1C
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

static u32 read_tcs_reg(struct rsc_drv *drv, int reg, int tcs_id, int cmd_id)
{
	return readl_relaxed(drv->tcs_base + reg + RSC_DRV_TCS_OFFSET * tcs_id +
			     RSC_DRV_CMD_OFFSET * cmd_id);
}

static void write_tcs_cmd(struct rsc_drv *drv, int reg, int tcs_id, int cmd_id,
			  u32 data)
{
	writel_relaxed(data, drv->tcs_base + reg + RSC_DRV_TCS_OFFSET * tcs_id +
		       RSC_DRV_CMD_OFFSET * cmd_id);
}

static void write_tcs_reg(struct rsc_drv *drv, int reg, int tcs_id, u32 data)
{
	writel_relaxed(data, drv->tcs_base + reg + RSC_DRV_TCS_OFFSET * tcs_id);
}

static void write_tcs_reg_sync(struct rsc_drv *drv, int reg, int tcs_id,
			       u32 data)
{
	writel(data, drv->tcs_base + reg + RSC_DRV_TCS_OFFSET * tcs_id);
	for (;;) {
		if (data == readl(drv->tcs_base + reg +
				  RSC_DRV_TCS_OFFSET * tcs_id))
			break;
		udelay(1);
	}
}

static bool tcs_is_free(struct rsc_drv *drv, int tcs_id)
{
	return !test_bit(tcs_id, drv->tcs_in_use) &&
	       read_tcs_reg(drv, RSC_DRV_STATUS, tcs_id, 0);
}

static struct tcs_group *get_tcs_of_type(struct rsc_drv *drv, int type)
{
	return &drv->tcs[type];
}

static int tcs_invalidate(struct rsc_drv *drv, int type)
{
	int m;
	struct tcs_group *tcs;

	tcs = get_tcs_of_type(drv, type);

	spin_lock(&tcs->lock);
	if (bitmap_empty(tcs->slots, MAX_TCS_SLOTS)) {
		spin_unlock(&tcs->lock);
		return 0;
	}

	for (m = tcs->offset; m < tcs->offset + tcs->num_tcs; m++) {
		if (!tcs_is_free(drv, m)) {
			spin_unlock(&tcs->lock);
			return -EAGAIN;
		}
		write_tcs_reg_sync(drv, RSC_DRV_CMD_ENABLE, m, 0);
		write_tcs_reg_sync(drv, RSC_DRV_CMD_WAIT_FOR_CMPL, m, 0);
	}
	bitmap_zero(tcs->slots, MAX_TCS_SLOTS);
	spin_unlock(&tcs->lock);

	return 0;
}

/**
 * rpmh_rsc_invalidate - Invalidate sleep and wake TCSes
 *
 * @drv: the RSC controller
 */
int rpmh_rsc_invalidate(struct rsc_drv *drv)
{
	int ret;

	ret = tcs_invalidate(drv, SLEEP_TCS);
	if (!ret)
		ret = tcs_invalidate(drv, WAKE_TCS);

	return ret;
}

static struct tcs_group *get_tcs_for_msg(struct rsc_drv *drv,
					 const struct tcs_request *msg)
{
	int type, ret;
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
	 * send active votes.
	 * NOTE: The driver must be aware that this RSC does not have a
	 * dedicated AMC, and therefore would invalidate the sleep and wake
	 * TCSes before making an active state request.
	 */
	tcs = get_tcs_of_type(drv, type);
	if (msg->state == RPMH_ACTIVE_ONLY_STATE && !tcs->num_tcs) {
		tcs = get_tcs_of_type(drv, WAKE_TCS);
		if (tcs->num_tcs) {
			ret = rpmh_rsc_invalidate(drv);
			if (ret)
				return ERR_PTR(ret);
		}
	}

	return tcs;
}

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
 * tcs_tx_done: TX Done interrupt handler
 */
static irqreturn_t tcs_tx_done(int irq, void *p)
{
	struct rsc_drv *drv = p;
	int i, j, err = 0;
	unsigned long irq_status;
	const struct tcs_request *req;
	struct tcs_cmd *cmd;

	irq_status = read_tcs_reg(drv, RSC_DRV_IRQ_STATUS, 0, 0);

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
			sts = read_tcs_reg(drv, RSC_DRV_CMD_STATUS, i, j);
			if (!(sts & CMD_STATUS_ISSUED) ||
			   ((req->wait_for_compl || cmd->wait) &&
			   !(sts & CMD_STATUS_COMPL))) {
				pr_err("Incomplete request: %s: addr=%#x data=%#x",
				       drv->name, cmd->addr, cmd->data);
				err = -EIO;
			}
		}

		trace_rpmh_tx_done(drv, i, req, err);
skip:
		/* Reclaim the TCS */
		write_tcs_reg(drv, RSC_DRV_CMD_ENABLE, i, 0);
		write_tcs_reg(drv, RSC_DRV_CMD_WAIT_FOR_CMPL, i, 0);
		write_tcs_reg(drv, RSC_DRV_IRQ_CLEAR, 0, BIT(i));
		spin_lock(&drv->lock);
		clear_bit(i, drv->tcs_in_use);
		spin_unlock(&drv->lock);
		if (req)
			rpmh_tx_done(req, err);
	}

	return IRQ_HANDLED;
}

static void __tcs_buffer_write(struct rsc_drv *drv, int tcs_id, int cmd_id,
			       const struct tcs_request *msg)
{
	u32 msgid, cmd_msgid;
	u32 cmd_enable = 0;
	u32 cmd_complete;
	struct tcs_cmd *cmd;
	int i, j;

	cmd_msgid = CMD_MSGID_LEN;
	cmd_msgid |= msg->wait_for_compl ? CMD_MSGID_RESP_REQ : 0;
	cmd_msgid |= CMD_MSGID_WRITE;

	cmd_complete = read_tcs_reg(drv, RSC_DRV_CMD_WAIT_FOR_CMPL, tcs_id, 0);

	for (i = 0, j = cmd_id; i < msg->num_cmds; i++, j++) {
		cmd = &msg->cmds[i];
		cmd_enable |= BIT(j);
		cmd_complete |= cmd->wait << j;
		msgid = cmd_msgid;
		msgid |= cmd->wait ? CMD_MSGID_RESP_REQ : 0;

		write_tcs_cmd(drv, RSC_DRV_CMD_MSGID, tcs_id, j, msgid);
		write_tcs_cmd(drv, RSC_DRV_CMD_ADDR, tcs_id, j, cmd->addr);
		write_tcs_cmd(drv, RSC_DRV_CMD_DATA, tcs_id, j, cmd->data);
		trace_rpmh_send_msg(drv, tcs_id, j, msgid, cmd);
	}

	write_tcs_reg(drv, RSC_DRV_CMD_WAIT_FOR_CMPL, tcs_id, cmd_complete);
	cmd_enable |= read_tcs_reg(drv, RSC_DRV_CMD_ENABLE, tcs_id, 0);
	write_tcs_reg(drv, RSC_DRV_CMD_ENABLE, tcs_id, cmd_enable);
}

static void __tcs_trigger(struct rsc_drv *drv, int tcs_id)
{
	u32 enable;

	/*
	 * HW req: Clear the DRV_CONTROL and enable TCS again
	 * While clearing ensure that the AMC mode trigger is cleared
	 * and then the mode enable is cleared.
	 */
	enable = read_tcs_reg(drv, RSC_DRV_CONTROL, tcs_id, 0);
	enable &= ~TCS_AMC_MODE_TRIGGER;
	write_tcs_reg_sync(drv, RSC_DRV_CONTROL, tcs_id, enable);
	enable &= ~TCS_AMC_MODE_ENABLE;
	write_tcs_reg_sync(drv, RSC_DRV_CONTROL, tcs_id, enable);

	/* Enable the AMC mode on the TCS and then trigger the TCS */
	enable = TCS_AMC_MODE_ENABLE;
	write_tcs_reg_sync(drv, RSC_DRV_CONTROL, tcs_id, enable);
	enable |= TCS_AMC_MODE_TRIGGER;
	write_tcs_reg_sync(drv, RSC_DRV_CONTROL, tcs_id, enable);
}

static int check_for_req_inflight(struct rsc_drv *drv, struct tcs_group *tcs,
				  const struct tcs_request *msg)
{
	unsigned long curr_enabled;
	u32 addr;
	int i, j, k;
	int tcs_id = tcs->offset;

	for (i = 0; i < tcs->num_tcs; i++, tcs_id++) {
		if (tcs_is_free(drv, tcs_id))
			continue;

		curr_enabled = read_tcs_reg(drv, RSC_DRV_CMD_ENABLE, tcs_id, 0);

		for_each_set_bit(j, &curr_enabled, MAX_CMDS_PER_TCS) {
			addr = read_tcs_reg(drv, RSC_DRV_CMD_ADDR, tcs_id, j);
			for (k = 0; k < msg->num_cmds; k++) {
				if (addr == msg->cmds[k].addr)
					return -EBUSY;
			}
		}
	}

	return 0;
}

static int find_free_tcs(struct tcs_group *tcs)
{
	int i;

	for (i = 0; i < tcs->num_tcs; i++) {
		if (tcs_is_free(tcs->drv, tcs->offset + i))
			return tcs->offset + i;
	}

	return -EBUSY;
}

static int tcs_write(struct rsc_drv *drv, const struct tcs_request *msg)
{
	struct tcs_group *tcs;
	int tcs_id;
	unsigned long flags;
	int ret;

	tcs = get_tcs_for_msg(drv, msg);
	if (IS_ERR(tcs))
		return PTR_ERR(tcs);

	spin_lock_irqsave(&tcs->lock, flags);
	spin_lock(&drv->lock);
	/*
	 * The h/w does not like if we send a request to the same address,
	 * when one is already in-flight or being processed.
	 */
	ret = check_for_req_inflight(drv, tcs, msg);
	if (ret) {
		spin_unlock(&drv->lock);
		goto done_write;
	}

	tcs_id = find_free_tcs(tcs);
	if (tcs_id < 0) {
		ret = tcs_id;
		spin_unlock(&drv->lock);
		goto done_write;
	}

	tcs->req[tcs_id - tcs->offset] = msg;
	set_bit(tcs_id, drv->tcs_in_use);
	spin_unlock(&drv->lock);

	__tcs_buffer_write(drv, tcs_id, 0, msg);
	__tcs_trigger(drv, tcs_id);

done_write:
	spin_unlock_irqrestore(&tcs->lock, flags);
	return ret;
}

/**
 * rpmh_rsc_send_data: Validate the incoming message and write to the
 * appropriate TCS block.
 *
 * @drv: the controller
 * @msg: the data to be sent
 *
 * Return: 0 on success, -EINVAL on error.
 * Note: This call blocks until a valid data is written to the TCS.
 */
int rpmh_rsc_send_data(struct rsc_drv *drv, const struct tcs_request *msg)
{
	int ret;

	if (!msg || !msg->cmds || !msg->num_cmds ||
	    msg->num_cmds > MAX_RPMH_PAYLOAD) {
		WARN_ON(1);
		return -EINVAL;
	}

	do {
		ret = tcs_write(drv, msg);
		if (ret == -EBUSY) {
			pr_info_ratelimited("TCS Busy, retrying RPMH message send: addr=%#x\n",
					    msg->cmds[0].addr);
			udelay(10);
		}
	} while (ret == -EBUSY);

	return ret;
}

static int find_match(const struct tcs_group *tcs, const struct tcs_cmd *cmd,
		      int len)
{
	int i, j;

	/* Check for already cached commands */
	for_each_set_bit(i, tcs->slots, MAX_TCS_SLOTS) {
		if (tcs->cmd_cache[i] != cmd[0].addr)
			continue;
		if (i + len >= tcs->num_tcs * tcs->ncpt)
			goto seq_err;
		for (j = 0; j < len; j++) {
			if (tcs->cmd_cache[i + j] != cmd[j].addr)
				goto seq_err;
		}
		return i;
	}

	return -ENODATA;

seq_err:
	WARN(1, "Message does not match previous sequence.\n");
	return -EINVAL;
}

static int find_slots(struct tcs_group *tcs, const struct tcs_request *msg,
		      int *tcs_id, int *cmd_id)
{
	int slot, offset;
	int i = 0;

	/* Find if we already have the msg in our TCS */
	slot = find_match(tcs, msg->cmds, msg->num_cmds);
	if (slot >= 0)
		goto copy_data;

	/* Do over, until we can fit the full payload in a TCS */
	do {
		slot = bitmap_find_next_zero_area(tcs->slots, MAX_TCS_SLOTS,
						  i, msg->num_cmds, 0);
		if (slot == tcs->num_tcs * tcs->ncpt)
			return -ENOMEM;
		i += tcs->ncpt;
	} while (slot + msg->num_cmds - 1 >= i);

copy_data:
	bitmap_set(tcs->slots, slot, msg->num_cmds);
	/* Copy the addresses of the resources over to the slots */
	for (i = 0; i < msg->num_cmds; i++)
		tcs->cmd_cache[slot + i] = msg->cmds[i].addr;

	offset = slot / tcs->ncpt;
	*tcs_id = offset + tcs->offset;
	*cmd_id = slot % tcs->ncpt;

	return 0;
}

static int tcs_ctrl_write(struct rsc_drv *drv, const struct tcs_request *msg)
{
	struct tcs_group *tcs;
	int tcs_id = 0, cmd_id = 0;
	unsigned long flags;
	int ret;

	tcs = get_tcs_for_msg(drv, msg);
	if (IS_ERR(tcs))
		return PTR_ERR(tcs);

	spin_lock_irqsave(&tcs->lock, flags);
	/* find the TCS id and the command in the TCS to write to */
	ret = find_slots(tcs, msg, &tcs_id, &cmd_id);
	if (!ret)
		__tcs_buffer_write(drv, tcs_id, cmd_id, msg);
	spin_unlock_irqrestore(&tcs->lock, flags);

	return ret;
}

/**
 * rpmh_rsc_write_ctrl_data: Write request to the controller
 *
 * @drv: the controller
 * @msg: the data to be written to the controller
 *
 * There is no response returned for writing the request to the controller.
 */
int rpmh_rsc_write_ctrl_data(struct rsc_drv *drv, const struct tcs_request *msg)
{
	if (!msg || !msg->cmds || !msg->num_cmds ||
	    msg->num_cmds > MAX_RPMH_PAYLOAD) {
		pr_err("Payload error\n");
		return -EINVAL;
	}

	/* Data sent to this API will not be sent immediately */
	if (msg->state == RPMH_ACTIVE_ONLY_STATE)
		return -EINVAL;

	return tcs_ctrl_write(drv, msg);
}

static int rpmh_probe_tcs_config(struct platform_device *pdev,
				 struct rsc_drv *drv)
{
	struct tcs_type_config {
		u32 type;
		u32 n;
	} tcs_cfg[TCS_TYPE_NR] = { { 0 } };
	struct device_node *dn = pdev->dev.of_node;
	u32 config, max_tcs, ncpt, offset;
	int i, ret, n, st = 0;
	struct tcs_group *tcs;
	struct resource *res;
	void __iomem *base;
	char drv_id[10] = {0};

	snprintf(drv_id, ARRAY_SIZE(drv_id), "drv-%d", drv->id);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, drv_id);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

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
		spin_lock_init(&tcs->lock);

		if (!tcs->num_tcs || tcs->type == CONTROL_TCS)
			continue;

		if (st + tcs->num_tcs > max_tcs ||
		    st + tcs->num_tcs >= BITS_PER_BYTE * sizeof(tcs->mask))
			return -EINVAL;

		tcs->mask = ((1 << tcs->num_tcs) - 1) << st;
		tcs->offset = st;
		st += tcs->num_tcs;

		/*
		 * Allocate memory to cache sleep and wake requests to
		 * avoid reading TCS register memory.
		 */
		if (tcs->type == ACTIVE_TCS)
			continue;

		tcs->cmd_cache = devm_kcalloc(&pdev->dev,
					      tcs->num_tcs * ncpt, sizeof(u32),
					      GFP_KERNEL);
		if (!tcs->cmd_cache)
			return -ENOMEM;
	}

	drv->num_tcs = st;

	return 0;
}

static int rpmh_rsc_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct rsc_drv *drv;
	int ret, irq;

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

	ret = rpmh_probe_tcs_config(pdev, drv);
	if (ret)
		return ret;

	spin_lock_init(&drv->lock);
	bitmap_zero(drv->tcs_in_use, MAX_TCS_NR);

	irq = platform_get_irq(pdev, drv->id);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, tcs_tx_done,
			       IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND,
			       drv->name, drv);
	if (ret)
		return ret;

	/* Enable the active TCS to send requests immediately */
	write_tcs_reg(drv, RSC_DRV_IRQ_ENABLE, 0, drv->tcs[ACTIVE_TCS].mask);

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

static struct platform_driver rpmh_driver = {
	.probe = rpmh_rsc_probe,
	.driver = {
		  .name = "rpmh",
		  .of_match_table = rpmh_drv_match,
	},
};

static int __init rpmh_driver_init(void)
{
	return platform_driver_register(&rpmh_driver);
}
arch_initcall(rpmh_driver_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm RPM-Hardened (RPMH) Communication driver");
