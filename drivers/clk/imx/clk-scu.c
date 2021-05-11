// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 *   Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/arm-smccc.h>
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
	bool is_enabled;
	u32 rate;
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

int imx_clk_scu_init(struct device_node *np)
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

	pm_runtime_set_suspended(dev);
	pm_runtime_set_autosuspend_delay(dev, 50);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret) {
		pm_runtime_disable(dev);
		return ret;
	}

	hw = __imx_clk_scu(dev, clk->name, clk->parents, clk->num_parents,
			   clk->rsrc, clk->clk_type);
	if (IS_ERR(hw)) {
		pm_runtime_disable(dev);
		return PTR_ERR(hw);
	}

	clk->hw = hw;
	list_add_tail(&clk->node, &imx_scu_clks[clk->rsrc]);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	dev_dbg(dev, "register SCU clock rsrc:%d type:%d\n", clk->rsrc,
		clk->clk_type);

	return 0;
}

static int __maybe_unused imx_clk_scu_suspend(struct device *dev)
{
	struct clk_scu *clk = dev_get_drvdata(dev);

	clk->rate = clk_hw_get_rate(&clk->hw);
	clk->is_enabled = clk_hw_is_enabled(&clk->hw);

	if (clk->rate)
		dev_dbg(dev, "save rate %d\n", clk->rate);

	if (clk->is_enabled)
		dev_dbg(dev, "save enabled state\n");

	return 0;
}

static int __maybe_unused imx_clk_scu_resume(struct device *dev)
{
	struct clk_scu *clk = dev_get_drvdata(dev);
	int ret = 0;

	if (clk->rate) {
		ret = clk_scu_set_rate(&clk->hw, clk->rate, 0);
		dev_dbg(dev, "restore rate %d %s\n", clk->rate,
			!ret ? "success" : "failed");
	}

	if (clk->is_enabled) {
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
