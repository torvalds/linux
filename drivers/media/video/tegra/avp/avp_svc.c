/*
 * Copyright (C) 2010 Google, Inc.
 * Author: Dima Zavin <dima@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/tegra_rpc.h>
#include <linux/types.h>

#include <mach/clk.h>
#include <mach/nvmap.h>

#include "../../../../video/tegra/nvmap/nvmap.h"

#include "avp_msg.h"
#include "trpc.h"
#include "avp.h"

enum {
	AVP_DBG_TRACE_SVC		= 1U << 0,
};

static u32 debug_mask = 0;
module_param_named(debug_mask, debug_mask, uint, S_IWUSR | S_IRUGO);

#define DBG(flag, args...) \
	do { if (unlikely(debug_mask & (flag))) pr_info(args); } while (0)

enum {
	CLK_REQUEST_VCP		= 0,
	CLK_REQUEST_BSEA	= 1,
	CLK_REQUEST_VDE		= 2,
	NUM_CLK_REQUESTS,
};

struct avp_module {
	const char		*name;
	u32			clk_req;
};

static struct avp_module avp_modules[] = {
	[AVP_MODULE_ID_VCP] = {
		.name		= "vcp",
		.clk_req	= CLK_REQUEST_VCP,
	},
	[AVP_MODULE_ID_BSEA]	= {
		.name		= "bsea",
		.clk_req	= CLK_REQUEST_BSEA,
	},
	[AVP_MODULE_ID_VDE]	= {
		.name		= "vde",
		.clk_req	= CLK_REQUEST_VDE,
	},
};
#define NUM_AVP_MODULES		ARRAY_SIZE(avp_modules)

struct avp_clk {
	struct clk		*clk;
	int			refcnt;
	struct avp_module	*mod;
};

struct avp_svc_info {
	struct avp_clk			clks[NUM_CLK_REQUESTS];
	/* used for dvfs */
	struct clk			*sclk;
	struct clk			*emcclk;

	struct mutex			clk_lock;

	struct trpc_endpoint		*cpu_ep;
	struct task_struct		*svc_thread;

	/* client for remote allocations, for easy tear down */
	struct nvmap_client		*nvmap_remote;
	struct trpc_node		*rpc_node;
};

static void do_svc_nvmap_create(struct avp_svc_info *avp_svc,
				struct svc_msg *_msg,
				size_t len)
{
	struct svc_nvmap_create *msg = (struct svc_nvmap_create *)_msg;
	struct svc_nvmap_create_resp resp;
	struct nvmap_handle_ref *handle;
	u32 handle_id = 0;
	u32 err = 0;

	handle = nvmap_create_handle(avp_svc->nvmap_remote, msg->size);
	if (unlikely(IS_ERR(handle))) {
		pr_err("avp_svc: error creating handle (%d bytes) for remote\n",
		       msg->size);
		err = AVP_ERR_ENOMEM;
	} else
		handle_id = (u32)nvmap_ref_to_id(handle);

	resp.svc_id = SVC_NVMAP_CREATE_RESPONSE;
	resp.err = err;
	resp.handle_id = handle_id;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
	/* TODO: do we need to put the handle if send_msg failed? */
}

static void do_svc_nvmap_alloc(struct avp_svc_info *avp_svc,
			       struct svc_msg *_msg,
			       size_t len)
{
	struct svc_nvmap_alloc *msg = (struct svc_nvmap_alloc *)_msg;
	struct svc_common_resp resp;
	struct nvmap_handle *handle;
	u32 err = 0;
	u32 heap_mask = 0;
	int i;
	size_t align;

	handle = nvmap_get_handle_id(avp_svc->nvmap_remote, msg->handle_id);
	if (IS_ERR(handle)) {
		pr_err("avp_svc: unknown remote handle 0x%x\n", msg->handle_id);
		err = AVP_ERR_EACCES;
		goto out;
	}

	if (msg->num_heaps > 4) {
		pr_err("avp_svc: invalid remote alloc request (%d heaps?!)\n",
		       msg->num_heaps);
		/* TODO: should we error out instead ? */
		msg->num_heaps = 0;
	}
	if (msg->num_heaps == 0)
		heap_mask = NVMAP_HEAP_CARVEOUT_GENERIC | NVMAP_HEAP_SYSMEM;

	for (i = 0; i < msg->num_heaps; i++) {
		switch (msg->heaps[i]) {
		case AVP_NVMAP_HEAP_EXTERNAL:
			heap_mask |= NVMAP_HEAP_SYSMEM;
			break;
		case AVP_NVMAP_HEAP_GART:
			heap_mask |= NVMAP_HEAP_IOVMM;
			break;
		case AVP_NVMAP_HEAP_EXTERNAL_CARVEOUT:
			heap_mask |= NVMAP_HEAP_CARVEOUT_GENERIC;
			break;
		case AVP_NVMAP_HEAP_IRAM:
			heap_mask |= NVMAP_HEAP_CARVEOUT_IRAM;
			break;
		default:
			break;
		}
	}

	align = max_t(size_t, L1_CACHE_BYTES, msg->align);
	err = nvmap_alloc_handle_id(avp_svc->nvmap_remote, msg->handle_id,
				    heap_mask, align, 0);
	nvmap_handle_put(handle);
	if (err) {
		pr_err("avp_svc: can't allocate for handle 0x%x (%d)\n",
		       msg->handle_id, err);
		err = AVP_ERR_ENOMEM;
	}

out:
	resp.svc_id = SVC_NVMAP_ALLOC_RESPONSE;
	resp.err = err;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_nvmap_free(struct avp_svc_info *avp_svc,
			      struct svc_msg *_msg,
			      size_t len)
{
	struct svc_nvmap_free *msg = (struct svc_nvmap_free *)_msg;

	nvmap_free_handle_id(avp_svc->nvmap_remote, msg->handle_id);
}

static void do_svc_nvmap_pin(struct avp_svc_info *avp_svc,
			     struct svc_msg *_msg,
			     size_t len)
{
	struct svc_nvmap_pin *msg = (struct svc_nvmap_pin *)_msg;
	struct svc_nvmap_pin_resp resp;
	struct nvmap_handle_ref *handle;
	unsigned long addr = ~0UL;
	unsigned long id = msg->handle_id;
	int err;

	handle = nvmap_duplicate_handle_id(avp_svc->nvmap_remote, id);
	if (IS_ERR(handle)) {
		pr_err("avp_svc: can't dup handle %lx\n", id);
		goto out;
	}
	err = nvmap_pin_ids(avp_svc->nvmap_remote, 1, &id);
	if (err) {
		pr_err("avp_svc: can't pin for handle %lx (%d)\n", id, err);
		goto out;
	}
	addr = nvmap_handle_address(avp_svc->nvmap_remote, id);

out:
	resp.svc_id = SVC_NVMAP_PIN_RESPONSE;
	resp.addr = addr;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_nvmap_unpin(struct avp_svc_info *avp_svc,
			       struct svc_msg *_msg,
			       size_t len)
{
	struct svc_nvmap_unpin *msg = (struct svc_nvmap_unpin *)_msg;
	struct svc_common_resp resp;
	unsigned long id = msg->handle_id;

	nvmap_unpin_ids(avp_svc->nvmap_remote, 1, &id);
	nvmap_free_handle_id(avp_svc->nvmap_remote, id);

	resp.svc_id = SVC_NVMAP_UNPIN_RESPONSE;
	resp.err = 0;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_nvmap_from_id(struct avp_svc_info *avp_svc,
				 struct svc_msg *_msg,
				 size_t len)
{
	struct svc_nvmap_from_id *msg = (struct svc_nvmap_from_id *)_msg;
	struct svc_common_resp resp;
	struct nvmap_handle_ref *handle;
	int err = 0;

	handle = nvmap_duplicate_handle_id(avp_svc->nvmap_remote,
					   msg->handle_id);
	if (IS_ERR(handle)) {
		pr_err("avp_svc: can't duplicate handle for id 0x%x (%d)\n",
		       msg->handle_id, (int)PTR_ERR(handle));
		err = AVP_ERR_ENOMEM;
	}

	resp.svc_id = SVC_NVMAP_FROM_ID_RESPONSE;
	resp.err = err;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_nvmap_get_addr(struct avp_svc_info *avp_svc,
				  struct svc_msg *_msg,
				  size_t len)
{
	struct svc_nvmap_get_addr *msg = (struct svc_nvmap_get_addr *)_msg;
	struct svc_nvmap_get_addr_resp resp;

	resp.svc_id = SVC_NVMAP_GET_ADDRESS_RESPONSE;
	resp.addr = nvmap_handle_address(avp_svc->nvmap_remote, msg->handle_id);
	resp.addr += msg->offs;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_pwr_register(struct avp_svc_info *avp_svc,
				struct svc_msg *_msg,
				size_t len)
{
	struct svc_pwr_register *msg = (struct svc_pwr_register *)_msg;
	struct svc_pwr_register_resp resp;

	resp.svc_id = SVC_POWER_RESPONSE;
	resp.err = 0;
	resp.client_id = msg->client_id;

	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static struct avp_module *find_avp_module(struct avp_svc_info *avp_svc, u32 id)
{
	if (id < NUM_AVP_MODULES && avp_modules[id].name)
		return &avp_modules[id];
	return NULL;
}

static void do_svc_module_reset(struct avp_svc_info *avp_svc,
				struct svc_msg *_msg,
				size_t len)
{
	struct svc_module_ctrl *msg = (struct svc_module_ctrl *)_msg;
	struct svc_common_resp resp;
	struct avp_module *mod;
	struct avp_clk *aclk;

	mod = find_avp_module(avp_svc, msg->module_id);
	if (!mod) {
		if (msg->module_id == AVP_MODULE_ID_AVP)
			pr_err("avp_svc: AVP suicidal?!?!\n");
		else
			pr_err("avp_svc: Unknown module reset requested: %d\n",
			       msg->module_id);
		/* other side doesn't handle errors for reset */
		resp.err = 0;
		goto send_response;
	}

	aclk = &avp_svc->clks[mod->clk_req];
	tegra_periph_reset_assert(aclk->clk);
	udelay(10);
	tegra_periph_reset_deassert(aclk->clk);
	resp.err = 0;

send_response:
	resp.svc_id = SVC_MODULE_RESET_RESPONSE;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_module_clock(struct avp_svc_info *avp_svc,
				struct svc_msg *_msg,
				size_t len)
{
	struct svc_module_ctrl *msg = (struct svc_module_ctrl *)_msg;
	struct svc_common_resp resp;
	struct avp_module *mod;
	struct avp_clk *aclk;

	mod = find_avp_module(avp_svc, msg->module_id);
	if (!mod) {
		pr_err("avp_svc: unknown module clock requested: %d\n",
		       msg->module_id);
		resp.err = AVP_ERR_EINVAL;
		goto send_response;
	}

	mutex_lock(&avp_svc->clk_lock);
	aclk = &avp_svc->clks[mod->clk_req];
	if (msg->enable) {
		if (aclk->refcnt++ == 0) {
			clk_enable(avp_svc->emcclk);
			clk_enable(avp_svc->sclk);
			clk_enable(aclk->clk);
		}
	} else {
		if (unlikely(aclk->refcnt == 0)) {
			pr_err("avp_svc: unbalanced clock disable for '%s'\n",
			       aclk->mod->name);
		} else if (--aclk->refcnt == 0) {
			clk_disable(aclk->clk);
			clk_disable(avp_svc->sclk);
			clk_disable(avp_svc->emcclk);
		}
	}
	mutex_unlock(&avp_svc->clk_lock);
	resp.err = 0;

send_response:
	resp.svc_id = SVC_MODULE_CLOCK_RESPONSE;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_null_response(struct avp_svc_info *avp_svc,
				 struct svc_msg *_msg,
				 size_t len, u32 resp_svc_id)
{
	struct svc_common_resp resp;
	resp.svc_id = resp_svc_id;
	resp.err = 0;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_dfs_get_state(struct avp_svc_info *avp_svc,
				 struct svc_msg *_msg,
				 size_t len)
{
	struct svc_dfs_get_state_resp resp;
	resp.svc_id = SVC_DFS_GETSTATE_RESPONSE;
	resp.state = AVP_DFS_STATE_STOPPED;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_dfs_get_clk_util(struct avp_svc_info *avp_svc,
				    struct svc_msg *_msg,
				    size_t len)
{
	struct svc_dfs_get_clk_util_resp resp;

	resp.svc_id = SVC_DFS_GET_CLK_UTIL_RESPONSE;
	resp.err = 0;
	memset(&resp.usage, 0, sizeof(struct avp_clk_usage));
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_pwr_max_freq(struct avp_svc_info *avp_svc,
				struct svc_msg *_msg,
				size_t len)
{
	struct svc_pwr_max_freq_resp resp;

	resp.svc_id = SVC_POWER_MAXFREQ;
	resp.freq = 0;
	trpc_send_msg(avp_svc->rpc_node, avp_svc->cpu_ep, &resp,
		      sizeof(resp), GFP_KERNEL);
}

static void do_svc_printf(struct avp_svc_info *avp_svc, struct svc_msg *_msg,
			  size_t len)
{
	struct svc_printf *msg = (struct svc_printf *)_msg;
	char tmp_str[SVC_MAX_STRING_LEN];

	/* ensure we null terminate the source */
	strlcpy(tmp_str, msg->str, SVC_MAX_STRING_LEN);
	pr_info("[AVP]: %s", tmp_str);
}

static int dispatch_svc_message(struct avp_svc_info *avp_svc,
				struct svc_msg *msg,
				size_t len)
{
	int ret = 0;

	switch (msg->svc_id) {
	case SVC_NVMAP_CREATE:
		DBG(AVP_DBG_TRACE_SVC, "%s: got nvmap_create\n", __func__);
		do_svc_nvmap_create(avp_svc, msg, len);
		break;
	case SVC_NVMAP_ALLOC:
		DBG(AVP_DBG_TRACE_SVC, "%s: got nvmap_alloc\n", __func__);
		do_svc_nvmap_alloc(avp_svc, msg, len);
		break;
	case SVC_NVMAP_FREE:
		DBG(AVP_DBG_TRACE_SVC, "%s: got nvmap_free\n", __func__);
		do_svc_nvmap_free(avp_svc, msg, len);
		break;
	case SVC_NVMAP_PIN:
		DBG(AVP_DBG_TRACE_SVC, "%s: got nvmap_pin\n", __func__);
		do_svc_nvmap_pin(avp_svc, msg, len);
		break;
	case SVC_NVMAP_UNPIN:
		DBG(AVP_DBG_TRACE_SVC, "%s: got nvmap_unpin\n", __func__);
		do_svc_nvmap_unpin(avp_svc, msg, len);
		break;
	case SVC_NVMAP_FROM_ID:
		DBG(AVP_DBG_TRACE_SVC, "%s: got nvmap_from_id\n", __func__);
		do_svc_nvmap_from_id(avp_svc, msg, len);
		break;
	case SVC_NVMAP_GET_ADDRESS:
		DBG(AVP_DBG_TRACE_SVC, "%s: got nvmap_get_addr\n", __func__);
		do_svc_nvmap_get_addr(avp_svc, msg, len);
		break;
	case SVC_POWER_REGISTER:
		DBG(AVP_DBG_TRACE_SVC, "%s: got power_register\n", __func__);
		do_svc_pwr_register(avp_svc, msg, len);
		break;
	case SVC_POWER_UNREGISTER:
		DBG(AVP_DBG_TRACE_SVC, "%s: got power_unregister\n", __func__);
		/* nothing to do */
		break;
	case SVC_POWER_BUSY_HINT_MULTI:
		DBG(AVP_DBG_TRACE_SVC, "%s: got power_busy_hint_multi\n",
		    __func__);
		/* nothing to do */
		break;
	case SVC_POWER_BUSY_HINT:
	case SVC_POWER_STARVATION:
		DBG(AVP_DBG_TRACE_SVC, "%s: got power busy/starve hint\n",
		    __func__);
		do_svc_null_response(avp_svc, msg, len, SVC_POWER_RESPONSE);
		break;
	case SVC_POWER_MAXFREQ:
		DBG(AVP_DBG_TRACE_SVC, "%s: got power get_max_freq\n",
		    __func__);
		do_svc_pwr_max_freq(avp_svc, msg, len);
		break;
	case SVC_DFS_GETSTATE:
		DBG(AVP_DBG_TRACE_SVC, "%s: got dfs_get_state\n", __func__);
		do_svc_dfs_get_state(avp_svc, msg, len);
		break;
	case SVC_MODULE_RESET:
		DBG(AVP_DBG_TRACE_SVC, "%s: got module_reset\n", __func__);
		do_svc_module_reset(avp_svc, msg, len);
		break;
	case SVC_MODULE_CLOCK:
		DBG(AVP_DBG_TRACE_SVC, "%s: got module_clock\n", __func__);
		do_svc_module_clock(avp_svc, msg, len);
		break;
	case SVC_DFS_GET_CLK_UTIL:
		DBG(AVP_DBG_TRACE_SVC, "%s: got get_clk_util\n", __func__);
		do_svc_dfs_get_clk_util(avp_svc, msg, len);
		break;
	case SVC_PRINTF:
		DBG(AVP_DBG_TRACE_SVC, "%s: got remote printf\n", __func__);
		do_svc_printf(avp_svc, msg, len);
		break;
	case SVC_AVP_WDT_RESET:
		pr_err("avp_svc: AVP has been reset by watchdog\n");
		break;
	default:
		pr_err("avp_svc: invalid SVC call 0x%x\n", msg->svc_id);
		ret = -ENOMSG;
		break;
	}

	return ret;
}

static int avp_svc_thread(void *data)
{
	struct avp_svc_info *avp_svc = data;
	u8 buf[TEGRA_RPC_MAX_MSG_LEN];
	struct svc_msg *msg = (struct svc_msg *)buf;
	int ret;

	BUG_ON(!avp_svc->cpu_ep);

	ret = trpc_wait_peer(avp_svc->cpu_ep, -1);
	if (ret) {
		/* XXX: teardown?! */
		pr_err("%s: no connection from AVP (%d)\n", __func__, ret);
		goto err;
	}

	pr_info("%s: got remote peer\n", __func__);

	while (!kthread_should_stop()) {
		DBG(AVP_DBG_TRACE_SVC, "%s: waiting for message\n", __func__);
		ret = trpc_recv_msg(avp_svc->rpc_node, avp_svc->cpu_ep, buf,
				    TEGRA_RPC_MAX_MSG_LEN, -1);
		DBG(AVP_DBG_TRACE_SVC, "%s: got message\n", __func__);
		if (ret < 0) {
			pr_err("%s: couldn't receive msg\n", __func__);
			/* XXX: port got closed? we should exit? */
			goto err;
		} else if (!ret) {
			pr_err("%s: received msg of len 0?!\n", __func__);
			continue;
		}
		dispatch_svc_message(avp_svc, msg, ret);
	}

err:
	trpc_put(avp_svc->cpu_ep);
	pr_info("%s: done\n", __func__);
	return ret;
}

int avp_svc_start(struct avp_svc_info *avp_svc)
{
	struct trpc_endpoint *ep;
	int ret;

	avp_svc->nvmap_remote = nvmap_create_client(nvmap_dev, "avp_remote");
	if (IS_ERR(avp_svc->nvmap_remote)) {
		pr_err("%s: cannot create remote nvmap client\n", __func__);
		ret = PTR_ERR(avp_svc->nvmap_remote);
		goto err_nvmap_create_remote_client;
	}

	ep = trpc_create(avp_svc->rpc_node, "RPC_CPU_PORT", NULL, NULL);
	if (IS_ERR(ep)) {
		pr_err("%s: can't create RPC_CPU_PORT\n", __func__);
		ret = PTR_ERR(ep);
		goto err_cpu_port_create;
	}

	/* TODO: protect this */
	avp_svc->cpu_ep = ep;

	/* the service thread should get an extra reference for the port */
	trpc_get(avp_svc->cpu_ep);
	avp_svc->svc_thread = kthread_run(avp_svc_thread, avp_svc,
					  "avp_svc_thread");
	if (IS_ERR_OR_NULL(avp_svc->svc_thread)) {
		avp_svc->svc_thread = NULL;
		pr_err("%s: can't create svc thread\n", __func__);
		ret = -ENOMEM;
		goto err_kthread;
	}
	return 0;

err_kthread:
	trpc_close(avp_svc->cpu_ep);
	trpc_put(avp_svc->cpu_ep);
	avp_svc->cpu_ep = NULL;
err_cpu_port_create:
	nvmap_client_put(avp_svc->nvmap_remote);
err_nvmap_create_remote_client:
	avp_svc->nvmap_remote = NULL;
	return ret;
}

void avp_svc_stop(struct avp_svc_info *avp_svc)
{
	int ret;
	int i;

	trpc_close(avp_svc->cpu_ep);
	ret = kthread_stop(avp_svc->svc_thread);
	if (ret == -EINTR) {
		/* the thread never started, drop it's extra reference */
		trpc_put(avp_svc->cpu_ep);
	}
	avp_svc->cpu_ep = NULL;

	nvmap_client_put(avp_svc->nvmap_remote);
	avp_svc->nvmap_remote = NULL;

	mutex_lock(&avp_svc->clk_lock);
	for (i = 0; i < NUM_CLK_REQUESTS; i++) {
		struct avp_clk *aclk = &avp_svc->clks[i];
		BUG_ON(aclk->refcnt < 0);
		if (aclk->refcnt > 0) {
			pr_info("%s: remote left clock '%s' on\n", __func__,
				aclk->mod->name);
			clk_disable(aclk->clk);
			/* sclk/emcclk was enabled once for every clock */
			clk_disable(avp_svc->sclk);
			clk_disable(avp_svc->emcclk);
		}
		aclk->refcnt = 0;
	}
	mutex_unlock(&avp_svc->clk_lock);
}

struct avp_svc_info *avp_svc_init(struct platform_device *pdev,
				  struct trpc_node *rpc_node)
{
	struct avp_svc_info *avp_svc;
	int ret;
	int i;
	int cnt = 0;

	BUG_ON(!rpc_node);

	avp_svc = kzalloc(sizeof(struct avp_svc_info), GFP_KERNEL);
	if (!avp_svc) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	BUILD_BUG_ON(NUM_CLK_REQUESTS > BITS_PER_LONG);

	for (i = 0; i < NUM_AVP_MODULES; i++) {
		struct avp_module *mod = &avp_modules[i];
		struct clk *clk;
		if (!mod->name)
			continue;
		BUG_ON(mod->clk_req >= NUM_CLK_REQUESTS ||
		       cnt++ >= NUM_CLK_REQUESTS);

		clk = clk_get(&pdev->dev, mod->name);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			pr_err("avp_svc: Couldn't get required clocks\n");
			goto err_get_clks;
		}
		avp_svc->clks[mod->clk_req].clk = clk;
		avp_svc->clks[mod->clk_req].mod = mod;
		avp_svc->clks[mod->clk_req].refcnt = 0;
	}

	avp_svc->sclk = clk_get(&pdev->dev, "sclk");
	if (IS_ERR(avp_svc->sclk)) {
		pr_err("avp_svc: Couldn't get sclk for dvfs\n");
		ret = -ENOENT;
		goto err_get_clks;
	}

	avp_svc->emcclk = clk_get(&pdev->dev, "emc");
	if (IS_ERR(avp_svc->emcclk)) {
		pr_err("avp_svc: Couldn't get emcclk for dvfs\n");
		ret = -ENOENT;
		goto err_get_clks;
	}

	/*
	 * The emc is a shared clock, it will be set to the highest
	 * requested rate from any user.  Set the rate to ULONG_MAX to
	 * always request the max rate whenever this request is enabled
	 */
	clk_set_rate(avp_svc->emcclk, ULONG_MAX);

	avp_svc->rpc_node = rpc_node;

	mutex_init(&avp_svc->clk_lock);

	return avp_svc;

err_get_clks:
	for (i = 0; i < NUM_CLK_REQUESTS; i++)
		if (avp_svc->clks[i].clk)
			clk_put(avp_svc->clks[i].clk);
	if (!IS_ERR_OR_NULL(avp_svc->sclk))
		clk_put(avp_svc->sclk);
	if (!IS_ERR_OR_NULL(avp_svc->emcclk))
		clk_put(avp_svc->emcclk);
err_alloc:
	return ERR_PTR(ret);
}

void avp_svc_destroy(struct avp_svc_info *avp_svc)
{
	int i;

	for (i = 0; i < NUM_CLK_REQUESTS; i++)
		clk_put(avp_svc->clks[i].clk);
	clk_put(avp_svc->sclk);
	clk_put(avp_svc->emcclk);

	kfree(avp_svc);
}
