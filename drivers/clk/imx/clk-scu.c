// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 *   Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/arm-smccc.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/slab.h>

#include "clk-scu.h"

#define IMX_SIP_CPUFREQ			0xC2000001
#define IMX_SIP_SET_CPUFREQ		0x00

static struct imx_sc_ipc *ccm_ipc_handle;

/*
 * struct clk_scu - Description of one SCU clock
 * @hw: the common clk_hw
 * @rsrc_id: resource ID of this SCU clock
 * @clk_type: type of this clock resource
 */
struct clk_scu {
	struct clk_hw hw;
	u16 rsrc_id;
	u8 clk_type;
};

/*
 * struct imx_sc_msg_req_set_clock_rate - clock set rate protocol
 * @hdr: SCU protocol header
 * @rate: rate to set
 * @resource: clock resource to set rate
 * @clk: clk type of this resource
 *
 * This structure describes the SCU protocol of clock rate set
 */
struct imx_sc_msg_req_set_clock_rate {
	struct imx_sc_rpc_msg hdr;
	__le32 rate;
	__le16 resource;
	u8 clk;
} __packed;

struct req_get_clock_rate {
	__le16 resource;
	u8 clk;
} __packed;

struct resp_get_clock_rate {
	__le32 rate;
};

/*
 * struct imx_sc_msg_get_clock_rate - clock get rate protocol
 * @hdr: SCU protocol header
 * @req: get rate request protocol
 * @resp: get rate response protocol
 *
 * This structure describes the SCU protocol of clock rate get
 */
struct imx_sc_msg_get_clock_rate {
	struct imx_sc_rpc_msg hdr;
	union {
		struct req_get_clock_rate req;
		struct resp_get_clock_rate resp;
	} data;
};

/*
 * struct imx_sc_msg_get_clock_parent - clock get parent protocol
 * @hdr: SCU protocol header
 * @req: get parent request protocol
 * @resp: get parent response protocol
 *
 * This structure describes the SCU protocol of clock get parent
 */
struct imx_sc_msg_get_clock_parent {
	struct imx_sc_rpc_msg hdr;
	union {
		struct req_get_clock_parent {
			__le16 resource;
			u8 clk;
		} __packed req;
		struct resp_get_clock_parent {
			u8 parent;
		} resp;
	} data;
};

/*
 * struct imx_sc_msg_set_clock_parent - clock set parent protocol
 * @hdr: SCU protocol header
 * @req: set parent request protocol
 *
 * This structure describes the SCU protocol of clock set parent
 */
struct imx_sc_msg_set_clock_parent {
	struct imx_sc_rpc_msg hdr;
	__le16 resource;
	u8 clk;
	u8 parent;
} __packed;

/*
 * struct imx_sc_msg_req_clock_enable - clock gate protocol
 * @hdr: SCU protocol header
 * @resource: clock resource to gate
 * @clk: clk type of this resource
 * @enable: whether gate off the clock
 * @autog: HW auto gate enable
 *
 * This structure describes the SCU protocol of clock gate
 */
struct imx_sc_msg_req_clock_enable {
	struct imx_sc_rpc_msg hdr;
	__le16 resource;
	u8 clk;
	u8 enable;
	u8 autog;
} __packed;

static inline struct clk_scu *to_clk_scu(struct clk_hw *hw)
{
	return container_of(hw, struct clk_scu, hw);
}

int imx_clk_scu_init(void)
{
	return imx_scu_get_handle(&ccm_ipc_handle);
}

/*
 * clk_scu_recalc_rate - Get clock rate for a SCU clock
 * @hw: clock to get rate for
 * @parent_rate: parent rate provided by common clock framework, not used
 *
 * Gets the current clock rate of a SCU clock. Returns the current
 * clock rate, or zero in failure.
 */
static unsigned long clk_scu_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_scu *clk = to_clk_scu(hw);
	struct imx_sc_msg_get_clock_rate msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_PM;
	hdr->func = IMX_SC_PM_FUNC_GET_CLOCK_RATE;
	hdr->size = 2;

	msg.data.req.resource = cpu_to_le16(clk->rsrc_id);
	msg.data.req.clk = clk->clk_type;

	ret = imx_scu_call_rpc(ccm_ipc_handle, &msg, true);
	if (ret) {
		pr_err("%s: failed to get clock rate %d\n",
		       clk_hw_get_name(hw), ret);
		return 0;
	}

	return le32_to_cpu(msg.data.resp.rate);
}

/*
 * clk_scu_round_rate - Round clock rate for a SCU clock
 * @hw: clock to round rate for
 * @rate: rate to round
 * @parent_rate: parent rate provided by common clock framework, not used
 *
 * Returns the current clock rate, or zero in failure.
 */
static long clk_scu_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	/*
	 * Assume we support all the requested rate and let the SCU firmware
	 * to handle the left work
	 */
	return rate;
}

static int clk_scu_atf_set_cpu_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct clk_scu *clk = to_clk_scu(hw);
	struct arm_smccc_res res;
	unsigned long cluster_id;

	if (clk->rsrc_id == IMX_SC_R_A35)
		cluster_id = 0;
	else
		return -EINVAL;

	/* CPU frequency scaling can ONLY be done by ARM-Trusted-Firmware */
	arm_smccc_smc(IMX_SIP_CPUFREQ, IMX_SIP_SET_CPUFREQ,
		      cluster_id, rate, 0, 0, 0, 0, &res);

	return 0;
}

/*
 * clk_scu_set_rate - Set rate for a SCU clock
 * @hw: clock to change rate for
 * @rate: target rate for the clock
 * @parent_rate: rate of the clock parent, not used for SCU clocks
 *
 * Sets a clock frequency for a SCU clock. Returns the SCU
 * protocol status.
 */
static int clk_scu_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_scu *clk = to_clk_scu(hw);
	struct imx_sc_msg_req_set_clock_rate msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_PM;
	hdr->func = IMX_SC_PM_FUNC_SET_CLOCK_RATE;
	hdr->size = 3;

	msg.rate = cpu_to_le32(rate);
	msg.resource = cpu_to_le16(clk->rsrc_id);
	msg.clk = clk->clk_type;

	return imx_scu_call_rpc(ccm_ipc_handle, &msg, true);
}

static u8 clk_scu_get_parent(struct clk_hw *hw)
{
	struct clk_scu *clk = to_clk_scu(hw);
	struct imx_sc_msg_get_clock_parent msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_PM;
	hdr->func = IMX_SC_PM_FUNC_GET_CLOCK_PARENT;
	hdr->size = 2;

	msg.data.req.resource = cpu_to_le16(clk->rsrc_id);
	msg.data.req.clk = clk->clk_type;

	ret = imx_scu_call_rpc(ccm_ipc_handle, &msg, true);
	if (ret) {
		pr_err("%s: failed to get clock parent %d\n",
		       clk_hw_get_name(hw), ret);
		return 0;
	}

	return msg.data.resp.parent;
}

static int clk_scu_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_scu *clk = to_clk_scu(hw);
	struct imx_sc_msg_set_clock_parent msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_PM;
	hdr->func = IMX_SC_PM_FUNC_SET_CLOCK_PARENT;
	hdr->size = 2;

	msg.resource = cpu_to_le16(clk->rsrc_id);
	msg.clk = clk->clk_type;
	msg.parent = index;

	return imx_scu_call_rpc(ccm_ipc_handle, &msg, true);
}

static int sc_pm_clock_enable(struct imx_sc_ipc *ipc, u16 resource,
			      u8 clk, bool enable, bool autog)
{
	struct imx_sc_msg_req_clock_enable msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_PM;
	hdr->func = IMX_SC_PM_FUNC_CLOCK_ENABLE;
	hdr->size = 3;

	msg.resource = cpu_to_le16(resource);
	msg.clk = clk;
	msg.enable = enable;
	msg.autog = autog;

	return imx_scu_call_rpc(ccm_ipc_handle, &msg, true);
}

/*
 * clk_scu_prepare - Enable a SCU clock
 * @hw: clock to enable
 *
 * Enable the clock at the DSC slice level
 */
static int clk_scu_prepare(struct clk_hw *hw)
{
	struct clk_scu *clk = to_clk_scu(hw);

	return sc_pm_clock_enable(ccm_ipc_handle, clk->rsrc_id,
				  clk->clk_type, true, false);
}

/*
 * clk_scu_unprepare - Disable a SCU clock
 * @hw: clock to enable
 *
 * Disable the clock at the DSC slice level
 */
static void clk_scu_unprepare(struct clk_hw *hw)
{
	struct clk_scu *clk = to_clk_scu(hw);
	int ret;

	ret = sc_pm_clock_enable(ccm_ipc_handle, clk->rsrc_id,
				 clk->clk_type, false, false);
	if (ret)
		pr_warn("%s: clk unprepare failed %d\n", clk_hw_get_name(hw),
			ret);
}

static const struct clk_ops clk_scu_ops = {
	.recalc_rate = clk_scu_recalc_rate,
	.round_rate = clk_scu_round_rate,
	.set_rate = clk_scu_set_rate,
	.get_parent = clk_scu_get_parent,
	.set_parent = clk_scu_set_parent,
	.prepare = clk_scu_prepare,
	.unprepare = clk_scu_unprepare,
};

static const struct clk_ops clk_scu_cpu_ops = {
	.recalc_rate = clk_scu_recalc_rate,
	.round_rate = clk_scu_round_rate,
	.set_rate = clk_scu_atf_set_cpu_rate,
	.prepare = clk_scu_prepare,
	.unprepare = clk_scu_unprepare,
};

struct clk_hw *__imx_clk_scu(const char *name, const char * const *parents,
			     int num_parents, u32 rsrc_id, u8 clk_type)
{
	struct clk_init_data init;
	struct clk_scu *clk;
	struct clk_hw *hw;
	int ret;

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return ERR_PTR(-ENOMEM);

	clk->rsrc_id = rsrc_id;
	clk->clk_type = clk_type;

	init.name = name;
	init.ops = &clk_scu_ops;
	if (rsrc_id == IMX_SC_R_A35)
		init.ops = &clk_scu_cpu_ops;
	else
		init.ops = &clk_scu_ops;
	init.parent_names = parents;
	init.num_parents = num_parents;

	/*
	 * Note on MX8, the clocks are tightly coupled with power domain
	 * that once the power domain is off, the clock status may be
	 * lost. So we make it NOCACHE to let user to retrieve the real
	 * clock status from HW instead of using the possible invalid
	 * cached rate.
	 */
	init.flags = CLK_GET_RATE_NOCACHE;
	clk->hw.init = &init;

	hw = &clk->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(clk);
		hw = ERR_PTR(ret);
	}

	return hw;
}
