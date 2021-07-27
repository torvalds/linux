// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018-2021 NXP
 *   Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/arm-smccc.h>
#include <linux/bsearch.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "clk-scu.h"

#define IMX_SIP_CPUFREQ			0xC2000001
#define IMX_SIP_SET_CPUFREQ		0x00

static struct imx_sc_ipc *ccm_ipc_handle;
static struct device_node *pd_np;
static struct platform_driver imx_clk_scu_driver;
static const struct imx_clk_scu_rsrc_table *rsrc_table;

struct imx_scu_clk_node {
	const char *name;
	u32 rsrc;
	u8 clk_type;
	const char * const *parents;
	int num_parents;

	struct clk_hw *hw;
	struct list_head node;
};

struct list_head imx_scu_clks[IMX_SC_R_LAST];

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

	/* for state save&restore */
	struct clk_hw *parent;
	u8 parent_index;
	bool is_enabled;
	u32 rate;
};

/*
 * struct clk_gpr_scu - Description of one SCU GPR clock
 * @hw: the common clk_hw
 * @rsrc_id: resource ID of this SCU clock
 * @gpr_id: GPR ID index to control the divider
 */
struct clk_gpr_scu {
	struct clk_hw hw;
	u16 rsrc_id;
	u8 gpr_id;
	u8 flags;
	bool gate_invert;
};

#define to_clk_gpr_scu(_hw) container_of(_hw, struct clk_gpr_scu, hw)

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
} __packed __aligned(4);

struct req_get_clock_rate {
	__le16 resource;
	u8 clk;
} __packed __aligned(4);

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
		} __packed __aligned(4) req;
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
} __packed __aligned(4);

static inline struct clk_scu *to_clk_scu(struct clk_hw *hw)
{
	return container_of(hw, struct clk_scu, hw);
}

static inline int imx_scu_clk_search_cmp(const void *rsrc, const void *rsrc_p)
{
	return *(u32 *)rsrc - *(u32 *)rsrc_p;
}

static bool imx_scu_clk_is_valid(u32 rsrc_id)
{
	void *p;

	if (!rsrc_table)
		return true;

	p = bsearch(&rsrc_id, rsrc_table->rsrc, rsrc_table->num,
		    sizeof(rsrc_table->rsrc[0]), imx_scu_clk_search_cmp);

	return p != NULL;
}

int imx_clk_scu_init(struct device_node *np,
		     const struct imx_clk_scu_rsrc_table *data)
{
	u32 clk_cells;
	int ret, i;

	ret = imx_scu_get_handle(&ccm_ipc_handle);
	if (ret)
		return ret;

	of_property_read_u32(np, "#clock-cells", &clk_cells);

	if (clk_cells == 2) {
		for (i = 0; i < IMX_SC_R_LAST; i++)
			INIT_LIST_HEAD(&imx_scu_clks[i]);

		/* pd_np will be used to attach power domains later */
		pd_np = of_find_compatible_node(NULL, NULL, "fsl,scu-pd");
		if (!pd_np)
			return -EINVAL;

		rsrc_table = data;
	}

	return platform_driver_register(&imx_clk_scu_driver);
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

	if (clk->rsrc_id == IMX_SC_R_A35 || clk->rsrc_id == IMX_SC_R_A53)
		cluster_id = 0;
	else if (clk->rsrc_id == IMX_SC_R_A72)
		cluster_id = 1;
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

	clk->parent_index = msg.data.resp.parent;

	return msg.data.resp.parent;
}

static int clk_scu_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_scu *clk = to_clk_scu(hw);
	struct imx_sc_msg_set_clock_parent msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_PM;
	hdr->func = IMX_SC_PM_FUNC_SET_CLOCK_PARENT;
	hdr->size = 2;

	msg.resource = cpu_to_le16(clk->rsrc_id);
	msg.clk = clk->clk_type;
	msg.parent = index;

	ret = imx_scu_call_rpc(ccm_ipc_handle, &msg, true);
	if (ret) {
		pr_err("%s: failed to set clock parent %d\n",
		       clk_hw_get_name(hw), ret);
		return ret;
	}

	clk->parent_index = index;

	return 0;
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

static const struct clk_ops clk_scu_pi_ops = {
	.recalc_rate = clk_scu_recalc_rate,
	.round_rate  = clk_scu_round_rate,
	.set_rate    = clk_scu_set_rate,
};

struct clk_hw *__imx_clk_scu(struct device *dev, const char *name,
			     const char * const *parents, int num_parents,
			     u32 rsrc_id, u8 clk_type)
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
	if (rsrc_id == IMX_SC_R_A35 || rsrc_id == IMX_SC_R_A53 || rsrc_id == IMX_SC_R_A72)
		init.ops = &clk_scu_cpu_ops;
	else if (rsrc_id == IMX_SC_R_PI_0_PLL)
		init.ops = &clk_scu_pi_ops;
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
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(clk);
		hw = ERR_PTR(ret);
		return hw;
	}

	if (dev)
		dev_set_drvdata(dev, clk);

	return hw;
}

struct clk_hw *imx_scu_of_clk_src_get(struct of_phandle_args *clkspec,
				      void *data)
{
	unsigned int rsrc = clkspec->args[0];
	unsigned int idx = clkspec->args[1];
	struct list_head *scu_clks = data;
	struct imx_scu_clk_node *clk;

	list_for_each_entry(clk, &scu_clks[rsrc], node) {
		if (clk->clk_type == idx)
			return clk->hw;
	}

	return ERR_PTR(-ENODEV);
}

static int imx_clk_scu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx_scu_clk_node *clk = dev_get_platdata(dev);
	struct clk_hw *hw;
	int ret;

	if (!((clk->rsrc == IMX_SC_R_A35) || (clk->rsrc == IMX_SC_R_A53) ||
	    (clk->rsrc == IMX_SC_R_A72))) {
		pm_runtime_set_suspended(dev);
		pm_runtime_set_autosuspend_delay(dev, 50);
		pm_runtime_use_autosuspend(&pdev->dev);
		pm_runtime_enable(dev);

		ret = pm_runtime_get_sync(dev);
		if (ret) {
			pm_genpd_remove_device(dev);
			pm_runtime_disable(dev);
			return ret;
		}
	}

	hw = __imx_clk_scu(dev, clk->name, clk->parents, clk->num_parents,
			   clk->rsrc, clk->clk_type);
	if (IS_ERR(hw)) {
		pm_runtime_disable(dev);
		return PTR_ERR(hw);
	}

	clk->hw = hw;
	list_add_tail(&clk->node, &imx_scu_clks[clk->rsrc]);

	if (!((clk->rsrc == IMX_SC_R_A35) || (clk->rsrc == IMX_SC_R_A53) ||
	    (clk->rsrc == IMX_SC_R_A72))) {
		pm_runtime_mark_last_busy(&pdev->dev);
		pm_runtime_put_autosuspend(&pdev->dev);
	}

	dev_dbg(dev, "register SCU clock rsrc:%d type:%d\n", clk->rsrc,
		clk->clk_type);

	return 0;
}

static int __maybe_unused imx_clk_scu_suspend(struct device *dev)
{
	struct clk_scu *clk = dev_get_drvdata(dev);
	u32 rsrc_id = clk->rsrc_id;

	if ((rsrc_id == IMX_SC_R_A35) || (rsrc_id == IMX_SC_R_A53) ||
	    (rsrc_id == IMX_SC_R_A72))
		return 0;

	clk->parent = clk_hw_get_parent(&clk->hw);

	/* DC SS needs to handle bypass clock using non-cached clock rate */
	if (clk->rsrc_id == IMX_SC_R_DC_0_VIDEO0 ||
		clk->rsrc_id == IMX_SC_R_DC_0_VIDEO1 ||
		clk->rsrc_id == IMX_SC_R_DC_1_VIDEO0 ||
		clk->rsrc_id == IMX_SC_R_DC_1_VIDEO1)
		clk->rate = clk_scu_recalc_rate(&clk->hw, 0);
	else
		clk->rate = clk_hw_get_rate(&clk->hw);
	clk->is_enabled = clk_hw_is_enabled(&clk->hw);

	if (clk->parent)
		dev_dbg(dev, "save parent %s idx %u\n", clk_hw_get_name(clk->parent),
			clk->parent_index);

	if (clk->rate)
		dev_dbg(dev, "save rate %d\n", clk->rate);

	if (clk->is_enabled)
		dev_dbg(dev, "save enabled state\n");

	return 0;
}

static int __maybe_unused imx_clk_scu_resume(struct device *dev)
{
	struct clk_scu *clk = dev_get_drvdata(dev);
	u32 rsrc_id = clk->rsrc_id;
	int ret = 0;

	if ((rsrc_id == IMX_SC_R_A35) || (rsrc_id == IMX_SC_R_A53) ||
	    (rsrc_id == IMX_SC_R_A72))
		return 0;

	if (clk->parent) {
		ret = clk_scu_set_parent(&clk->hw, clk->parent_index);
		dev_dbg(dev, "restore parent %s idx %u %s\n",
			clk_hw_get_name(clk->parent),
			clk->parent_index, !ret ? "success" : "failed");
	}

	if (clk->rate) {
		ret = clk_scu_set_rate(&clk->hw, clk->rate, 0);
		dev_dbg(dev, "restore rate %d %s\n", clk->rate,
			!ret ? "success" : "failed");
	}

	if (clk->is_enabled && rsrc_id != IMX_SC_R_PI_0_PLL) {
		ret = clk_scu_prepare(&clk->hw);
		dev_dbg(dev, "restore enabled state %s\n",
			!ret ? "success" : "failed");
	}

	return ret;
}

static const struct dev_pm_ops imx_clk_scu_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(imx_clk_scu_suspend,
				      imx_clk_scu_resume)
};

static struct platform_driver imx_clk_scu_driver = {
	.driver = {
		.name = "imx-scu-clk",
		.suppress_bind_attrs = true,
		.pm = &imx_clk_scu_pm_ops,
	},
	.probe = imx_clk_scu_probe,
};

static int imx_clk_scu_attach_pd(struct device *dev, u32 rsrc_id)
{
	struct of_phandle_args genpdspec = {
		.np = pd_np,
		.args_count = 1,
		.args[0] = rsrc_id,
	};

	if (rsrc_id == IMX_SC_R_A35 || rsrc_id == IMX_SC_R_A53 ||
	    rsrc_id == IMX_SC_R_A72)
		return 0;

	return of_genpd_add_device(&genpdspec, dev);
}

struct clk_hw *imx_clk_scu_alloc_dev(const char *name,
				     const char * const *parents,
				     int num_parents, u32 rsrc_id, u8 clk_type)
{
	struct imx_scu_clk_node clk = {
		.name = name,
		.rsrc = rsrc_id,
		.clk_type = clk_type,
		.parents = parents,
		.num_parents = num_parents,
	};
	struct platform_device *pdev;
	int ret;

	if (!imx_scu_clk_is_valid(rsrc_id))
		return ERR_PTR(-EINVAL);

	pdev = platform_device_alloc(name, PLATFORM_DEVID_NONE);
	if (!pdev) {
		pr_err("%s: failed to allocate scu clk dev rsrc %d type %d\n",
		       name, rsrc_id, clk_type);
		return ERR_PTR(-ENOMEM);
	}

	ret = platform_device_add_data(pdev, &clk, sizeof(clk));
	if (ret) {
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	pdev->driver_override = "imx-scu-clk";

	ret = imx_clk_scu_attach_pd(&pdev->dev, rsrc_id);
	if (ret)
		pr_warn("%s: failed to attached the power domain %d\n",
			name, ret);

	platform_device_add(pdev);

	/* For API backwards compatiblilty, simply return NULL for success */
	return NULL;
}

void imx_clk_scu_unregister(void)
{
	struct imx_scu_clk_node *clk;
	int i;

	for (i = 0; i < IMX_SC_R_LAST; i++) {
		list_for_each_entry(clk, &imx_scu_clks[i], node) {
			clk_hw_unregister(clk->hw);
			kfree(clk);
		}
	}
}

static unsigned long clk_gpr_div_scu_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_gpr_scu *clk = to_clk_gpr_scu(hw);
	unsigned long rate = 0;
	u32 val;
	int err;

	err = imx_sc_misc_get_control(ccm_ipc_handle, clk->rsrc_id,
				      clk->gpr_id, &val);

	rate  = val ? parent_rate / 2 : parent_rate;

	return err ? 0 : rate;
}

static long clk_gpr_div_scu_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	if (rate < *prate)
		rate = *prate / 2;
	else
		rate = *prate;

	return rate;
}

static int clk_gpr_div_scu_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct clk_gpr_scu *clk = to_clk_gpr_scu(hw);
	uint32_t val;
	int err;

	val = (rate < parent_rate) ? 1 : 0;
	err = imx_sc_misc_set_control(ccm_ipc_handle, clk->rsrc_id,
				      clk->gpr_id, val);

	return err ? -EINVAL : 0;
}

static const struct clk_ops clk_gpr_div_scu_ops = {
	.recalc_rate = clk_gpr_div_scu_recalc_rate,
	.round_rate = clk_gpr_div_scu_round_rate,
	.set_rate = clk_gpr_div_scu_set_rate,
};

static u8 clk_gpr_mux_scu_get_parent(struct clk_hw *hw)
{
	struct clk_gpr_scu *clk = to_clk_gpr_scu(hw);
	u32 val = 0;

	imx_sc_misc_get_control(ccm_ipc_handle, clk->rsrc_id,
				clk->gpr_id, &val);

	return (u8)val;
}

static int clk_gpr_mux_scu_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_gpr_scu *clk = to_clk_gpr_scu(hw);

	return imx_sc_misc_set_control(ccm_ipc_handle, clk->rsrc_id,
				       clk->gpr_id, index);
}

static const struct clk_ops clk_gpr_mux_scu_ops = {
	.get_parent = clk_gpr_mux_scu_get_parent,
	.set_parent = clk_gpr_mux_scu_set_parent,
};

static int clk_gpr_gate_scu_prepare(struct clk_hw *hw)
{
	struct clk_gpr_scu *clk = to_clk_gpr_scu(hw);

	return imx_sc_misc_set_control(ccm_ipc_handle, clk->rsrc_id,
				       clk->gpr_id, !clk->gate_invert);
}

static void clk_gpr_gate_scu_unprepare(struct clk_hw *hw)
{
	struct clk_gpr_scu *clk = to_clk_gpr_scu(hw);
	int ret;

	ret = imx_sc_misc_set_control(ccm_ipc_handle, clk->rsrc_id,
				      clk->gpr_id, clk->gate_invert);
	if (ret)
		pr_err("%s: clk unprepare failed %d\n", clk_hw_get_name(hw),
		       ret);
}

static int clk_gpr_gate_scu_is_prepared(struct clk_hw *hw)
{
	struct clk_gpr_scu *clk = to_clk_gpr_scu(hw);
	int ret;
	u32 val;

	ret = imx_sc_misc_get_control(ccm_ipc_handle, clk->rsrc_id,
				      clk->gpr_id, &val);
	if (ret)
		return ret;

	return clk->gate_invert ? !val : val;
}

static const struct clk_ops clk_gpr_gate_scu_ops = {
	.prepare = clk_gpr_gate_scu_prepare,
	.unprepare = clk_gpr_gate_scu_unprepare,
	.is_prepared = clk_gpr_gate_scu_is_prepared,
};

struct clk_hw *__imx_clk_gpr_scu(const char *name, const char * const *parent_name,
				 int num_parents, u32 rsrc_id, u8 gpr_id, u8 flags,
				 bool invert)
{
	struct imx_scu_clk_node *clk_node;
	struct clk_gpr_scu *clk;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	if (rsrc_id >= IMX_SC_R_LAST || gpr_id >= IMX_SC_C_LAST)
		return ERR_PTR(-EINVAL);

	clk_node = kzalloc(sizeof(*clk_node), GFP_KERNEL);
	if (!clk_node)
		return ERR_PTR(-ENOMEM);

	if (!imx_scu_clk_is_valid(rsrc_id))
		return ERR_PTR(-EINVAL);

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk) {
		kfree(clk_node);
		return ERR_PTR(-ENOMEM);
	}

	clk->rsrc_id = rsrc_id;
	clk->gpr_id = gpr_id;
	clk->flags = flags;
	clk->gate_invert = invert;

	if (flags & IMX_SCU_GPR_CLK_GATE)
		init.ops = &clk_gpr_gate_scu_ops;

	if (flags & IMX_SCU_GPR_CLK_DIV)
		init.ops = &clk_gpr_div_scu_ops;

	if (flags & IMX_SCU_GPR_CLK_MUX)
		init.ops = &clk_gpr_mux_scu_ops;

	init.flags = 0;
	init.name = name;
	init.parent_names = parent_name;
	init.num_parents = num_parents;

	clk->hw.init = &init;

	hw = &clk->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(clk);
		kfree(clk_node);
		hw = ERR_PTR(ret);
	} else {
		clk_node->hw = hw;
		clk_node->clk_type = gpr_id;
		list_add_tail(&clk_node->node, &imx_scu_clks[rsrc_id]);
	}

	return hw;
}
