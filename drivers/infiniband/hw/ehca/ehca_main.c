/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  module start stop, hca detection
 *
 *  Authors: Heiko J Schick <schickhj@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *           Joachim Fenkes <fenkes@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef CONFIG_PPC_64K_PAGES
#include <linux/slab.h>
#endif
#include "ehca_classes.h"
#include "ehca_iverbs.h"
#include "ehca_mrmw.h"
#include "ehca_tools.h"
#include "hcp_if.h"

#define HCAD_VERSION "0024"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Christoph Raisch <raisch@de.ibm.com>");
MODULE_DESCRIPTION("IBM eServer HCA InfiniBand Device Driver");
MODULE_VERSION(HCAD_VERSION);

int ehca_open_aqp1     = 0;
int ehca_debug_level   = 0;
int ehca_hw_level      = 0;
int ehca_nr_ports      = 2;
int ehca_use_hp_mr     = 0;
int ehca_port_act_time = 30;
int ehca_poll_all_eqs  = 1;
int ehca_static_rate   = -1;
int ehca_scaling_code  = 0;
int ehca_mr_largepage  = 1;

module_param_named(open_aqp1,     ehca_open_aqp1,     int, S_IRUGO);
module_param_named(debug_level,   ehca_debug_level,   int, S_IRUGO);
module_param_named(hw_level,      ehca_hw_level,      int, S_IRUGO);
module_param_named(nr_ports,      ehca_nr_ports,      int, S_IRUGO);
module_param_named(use_hp_mr,     ehca_use_hp_mr,     int, S_IRUGO);
module_param_named(port_act_time, ehca_port_act_time, int, S_IRUGO);
module_param_named(poll_all_eqs,  ehca_poll_all_eqs,  int, S_IRUGO);
module_param_named(static_rate,   ehca_static_rate,   int, S_IRUGO);
module_param_named(scaling_code,  ehca_scaling_code,  int, S_IRUGO);
module_param_named(mr_largepage,  ehca_mr_largepage,  int, S_IRUGO);

MODULE_PARM_DESC(open_aqp1,
		 "AQP1 on startup (0: no (default), 1: yes)");
MODULE_PARM_DESC(debug_level,
		 "debug level"
		 " (0: no debug traces (default), 1: with debug traces)");
MODULE_PARM_DESC(hw_level,
		 "hardware level"
		 " (0: autosensing (default), 1: v. 0.20, 2: v. 0.21)");
MODULE_PARM_DESC(nr_ports,
		 "number of connected ports (default: 2)");
MODULE_PARM_DESC(use_hp_mr,
		 "high performance MRs (0: no (default), 1: yes)");
MODULE_PARM_DESC(port_act_time,
		 "time to wait for port activation (default: 30 sec)");
MODULE_PARM_DESC(poll_all_eqs,
		 "polls all event queues periodically"
		 " (0: no, 1: yes (default))");
MODULE_PARM_DESC(static_rate,
		 "set permanent static rate (default: disabled)");
MODULE_PARM_DESC(scaling_code,
		 "set scaling code (0: disabled/default, 1: enabled)");
MODULE_PARM_DESC(mr_largepage,
		 "use large page for MR (0: use PAGE_SIZE (default), "
		 "1: use large page depending on MR size");

DEFINE_RWLOCK(ehca_qp_idr_lock);
DEFINE_RWLOCK(ehca_cq_idr_lock);
DEFINE_IDR(ehca_qp_idr);
DEFINE_IDR(ehca_cq_idr);

static LIST_HEAD(shca_list); /* list of all registered ehcas */
static DEFINE_SPINLOCK(shca_list_lock);

static struct timer_list poll_eqs_timer;

#ifdef CONFIG_PPC_64K_PAGES
static struct kmem_cache *ctblk_cache;

void *ehca_alloc_fw_ctrlblock(gfp_t flags)
{
	void *ret = kmem_cache_zalloc(ctblk_cache, flags);
	if (!ret)
		ehca_gen_err("Out of memory for ctblk");
	return ret;
}

void ehca_free_fw_ctrlblock(void *ptr)
{
	if (ptr)
		kmem_cache_free(ctblk_cache, ptr);

}
#endif

int ehca2ib_return_code(u64 ehca_rc)
{
	switch (ehca_rc) {
	case H_SUCCESS:
		return 0;
	case H_RESOURCE:             /* Resource in use */
	case H_BUSY:
		return -EBUSY;
	case H_NOT_ENOUGH_RESOURCES: /* insufficient resources */
	case H_CONSTRAINED:          /* resource constraint */
	case H_NO_MEM:
		return -ENOMEM;
	default:
		return -EINVAL;
	}
}

static int ehca_create_slab_caches(void)
{
	int ret;

	ret = ehca_init_pd_cache();
	if (ret) {
		ehca_gen_err("Cannot create PD SLAB cache.");
		return ret;
	}

	ret = ehca_init_cq_cache();
	if (ret) {
		ehca_gen_err("Cannot create CQ SLAB cache.");
		goto create_slab_caches2;
	}

	ret = ehca_init_qp_cache();
	if (ret) {
		ehca_gen_err("Cannot create QP SLAB cache.");
		goto create_slab_caches3;
	}

	ret = ehca_init_av_cache();
	if (ret) {
		ehca_gen_err("Cannot create AV SLAB cache.");
		goto create_slab_caches4;
	}

	ret = ehca_init_mrmw_cache();
	if (ret) {
		ehca_gen_err("Cannot create MR&MW SLAB cache.");
		goto create_slab_caches5;
	}

	ret = ehca_init_small_qp_cache();
	if (ret) {
		ehca_gen_err("Cannot create small queue SLAB cache.");
		goto create_slab_caches6;
	}

#ifdef CONFIG_PPC_64K_PAGES
	ctblk_cache = kmem_cache_create("ehca_cache_ctblk",
					EHCA_PAGESIZE, H_CB_ALIGNMENT,
					SLAB_HWCACHE_ALIGN,
					NULL);
	if (!ctblk_cache) {
		ehca_gen_err("Cannot create ctblk SLAB cache.");
		ehca_cleanup_small_qp_cache();
		goto create_slab_caches6;
	}
#endif
	return 0;

create_slab_caches6:
	ehca_cleanup_mrmw_cache();

create_slab_caches5:
	ehca_cleanup_av_cache();

create_slab_caches4:
	ehca_cleanup_qp_cache();

create_slab_caches3:
	ehca_cleanup_cq_cache();

create_slab_caches2:
	ehca_cleanup_pd_cache();

	return ret;
}

static void ehca_destroy_slab_caches(void)
{
	ehca_cleanup_small_qp_cache();
	ehca_cleanup_mrmw_cache();
	ehca_cleanup_av_cache();
	ehca_cleanup_qp_cache();
	ehca_cleanup_cq_cache();
	ehca_cleanup_pd_cache();
#ifdef CONFIG_PPC_64K_PAGES
	if (ctblk_cache)
		kmem_cache_destroy(ctblk_cache);
#endif
}

#define EHCA_HCAAVER  EHCA_BMASK_IBM(32, 39)
#define EHCA_REVID    EHCA_BMASK_IBM(40, 63)

static struct cap_descr {
	u64 mask;
	char *descr;
} hca_cap_descr[] = {
	{ HCA_CAP_AH_PORT_NR_CHECK, "HCA_CAP_AH_PORT_NR_CHECK" },
	{ HCA_CAP_ATOMIC, "HCA_CAP_ATOMIC" },
	{ HCA_CAP_AUTO_PATH_MIG, "HCA_CAP_AUTO_PATH_MIG" },
	{ HCA_CAP_BAD_P_KEY_CTR, "HCA_CAP_BAD_P_KEY_CTR" },
	{ HCA_CAP_SQD_RTS_PORT_CHANGE, "HCA_CAP_SQD_RTS_PORT_CHANGE" },
	{ HCA_CAP_CUR_QP_STATE_MOD, "HCA_CAP_CUR_QP_STATE_MOD" },
	{ HCA_CAP_INIT_TYPE, "HCA_CAP_INIT_TYPE" },
	{ HCA_CAP_PORT_ACTIVE_EVENT, "HCA_CAP_PORT_ACTIVE_EVENT" },
	{ HCA_CAP_Q_KEY_VIOL_CTR, "HCA_CAP_Q_KEY_VIOL_CTR" },
	{ HCA_CAP_WQE_RESIZE, "HCA_CAP_WQE_RESIZE" },
	{ HCA_CAP_RAW_PACKET_MCAST, "HCA_CAP_RAW_PACKET_MCAST" },
	{ HCA_CAP_SHUTDOWN_PORT, "HCA_CAP_SHUTDOWN_PORT" },
	{ HCA_CAP_RC_LL_QP, "HCA_CAP_RC_LL_QP" },
	{ HCA_CAP_SRQ, "HCA_CAP_SRQ" },
	{ HCA_CAP_UD_LL_QP, "HCA_CAP_UD_LL_QP" },
	{ HCA_CAP_RESIZE_MR, "HCA_CAP_RESIZE_MR" },
	{ HCA_CAP_MINI_QP, "HCA_CAP_MINI_QP" },
};

static int ehca_sense_attributes(struct ehca_shca *shca)
{
	int i, ret = 0;
	u64 h_ret;
	struct hipz_query_hca *rblock;
	struct hipz_query_port *port;

	static const u32 pgsize_map[] = {
		HCA_CAP_MR_PGSIZE_4K,  0x1000,
		HCA_CAP_MR_PGSIZE_64K, 0x10000,
		HCA_CAP_MR_PGSIZE_1M,  0x100000,
		HCA_CAP_MR_PGSIZE_16M, 0x1000000,
	};

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_gen_err("Cannot allocate rblock memory.");
		return -ENOMEM;
	}

	h_ret = hipz_h_query_hca(shca->ipz_hca_handle, rblock);
	if (h_ret != H_SUCCESS) {
		ehca_gen_err("Cannot query device properties. h_ret=%li",
			     h_ret);
		ret = -EPERM;
		goto sense_attributes1;
	}

	if (ehca_nr_ports == 1)
		shca->num_ports = 1;
	else
		shca->num_ports = (u8)rblock->num_ports;

	ehca_gen_dbg(" ... found %x ports", rblock->num_ports);

	if (ehca_hw_level == 0) {
		u32 hcaaver;
		u32 revid;

		hcaaver = EHCA_BMASK_GET(EHCA_HCAAVER, rblock->hw_ver);
		revid   = EHCA_BMASK_GET(EHCA_REVID, rblock->hw_ver);

		ehca_gen_dbg(" ... hardware version=%x:%x", hcaaver, revid);

		if (hcaaver == 1) {
			if (revid <= 3)
				shca->hw_level = 0x10 | (revid + 1);
			else
				shca->hw_level = 0x14;
		} else if (hcaaver == 2) {
			if (revid == 0)
				shca->hw_level = 0x21;
			else if (revid == 0x10)
				shca->hw_level = 0x22;
			else if (revid == 0x20 || revid == 0x21)
				shca->hw_level = 0x23;
		}

		if (!shca->hw_level) {
			ehca_gen_warn("unknown hardware version"
				      " - assuming default level");
			shca->hw_level = 0x22;
		}
	} else
		shca->hw_level = ehca_hw_level;
	ehca_gen_dbg(" ... hardware level=%x", shca->hw_level);

	shca->sport[0].rate = IB_RATE_30_GBPS;
	shca->sport[1].rate = IB_RATE_30_GBPS;

	shca->hca_cap = rblock->hca_cap_indicators;
	ehca_gen_dbg(" ... HCA capabilities:");
	for (i = 0; i < ARRAY_SIZE(hca_cap_descr); i++)
		if (EHCA_BMASK_GET(hca_cap_descr[i].mask, shca->hca_cap))
			ehca_gen_dbg("   %s", hca_cap_descr[i].descr);

	/* translate supported MR page sizes; always support 4K */
	shca->hca_cap_mr_pgsize = EHCA_PAGESIZE;
	if (ehca_mr_largepage) { /* support extra sizes only if enabled */
		for (i = 0; i < ARRAY_SIZE(pgsize_map); i += 2)
			if (rblock->memory_page_size_supported & pgsize_map[i])
				shca->hca_cap_mr_pgsize |= pgsize_map[i + 1];
	}

	/* query max MTU from first port -- it's the same for all ports */
	port = (struct hipz_query_port *)rblock;
	h_ret = hipz_h_query_port(shca->ipz_hca_handle, 1, port);
	if (h_ret != H_SUCCESS) {
		ehca_gen_err("Cannot query port properties. h_ret=%li",
			     h_ret);
		ret = -EPERM;
		goto sense_attributes1;
	}

	shca->max_mtu = port->max_mtu;

sense_attributes1:
	ehca_free_fw_ctrlblock(rblock);
	return ret;
}

static int init_node_guid(struct ehca_shca *shca)
{
	int ret = 0;
	struct hipz_query_hca *rblock;

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_err(&shca->ib_device, "Can't allocate rblock memory.");
		return -ENOMEM;
	}

	if (hipz_h_query_hca(shca->ipz_hca_handle, rblock) != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query device properties");
		ret = -EINVAL;
		goto init_node_guid1;
	}

	memcpy(&shca->ib_device.node_guid, &rblock->node_guid, sizeof(u64));

init_node_guid1:
	ehca_free_fw_ctrlblock(rblock);
	return ret;
}

int ehca_init_device(struct ehca_shca *shca)
{
	int ret;

	ret = init_node_guid(shca);
	if (ret)
		return ret;

	strlcpy(shca->ib_device.name, "ehca%d", IB_DEVICE_NAME_MAX);
	shca->ib_device.owner               = THIS_MODULE;

	shca->ib_device.uverbs_abi_ver	    = 8;
	shca->ib_device.uverbs_cmd_mask	    =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)	|
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)		|
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_REG_MR)		|
		(1ull << IB_USER_VERBS_CMD_DEREG_MR)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL)	|
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_QP)		|
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_QP)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP)		|
		(1ull << IB_USER_VERBS_CMD_ATTACH_MCAST)	|
		(1ull << IB_USER_VERBS_CMD_DETACH_MCAST);

	shca->ib_device.node_type           = RDMA_NODE_IB_CA;
	shca->ib_device.phys_port_cnt       = shca->num_ports;
	shca->ib_device.num_comp_vectors    = 1;
	shca->ib_device.dma_device          = &shca->ofdev->dev;
	shca->ib_device.query_device        = ehca_query_device;
	shca->ib_device.query_port          = ehca_query_port;
	shca->ib_device.query_gid           = ehca_query_gid;
	shca->ib_device.query_pkey          = ehca_query_pkey;
	/* shca->in_device.modify_device    = ehca_modify_device    */
	shca->ib_device.modify_port         = ehca_modify_port;
	shca->ib_device.alloc_ucontext      = ehca_alloc_ucontext;
	shca->ib_device.dealloc_ucontext    = ehca_dealloc_ucontext;
	shca->ib_device.alloc_pd            = ehca_alloc_pd;
	shca->ib_device.dealloc_pd          = ehca_dealloc_pd;
	shca->ib_device.create_ah	    = ehca_create_ah;
	/* shca->ib_device.modify_ah	    = ehca_modify_ah;	    */
	shca->ib_device.query_ah	    = ehca_query_ah;
	shca->ib_device.destroy_ah	    = ehca_destroy_ah;
	shca->ib_device.create_qp	    = ehca_create_qp;
	shca->ib_device.modify_qp	    = ehca_modify_qp;
	shca->ib_device.query_qp	    = ehca_query_qp;
	shca->ib_device.destroy_qp	    = ehca_destroy_qp;
	shca->ib_device.post_send	    = ehca_post_send;
	shca->ib_device.post_recv	    = ehca_post_recv;
	shca->ib_device.create_cq	    = ehca_create_cq;
	shca->ib_device.destroy_cq	    = ehca_destroy_cq;
	shca->ib_device.resize_cq	    = ehca_resize_cq;
	shca->ib_device.poll_cq		    = ehca_poll_cq;
	/* shca->ib_device.peek_cq	    = ehca_peek_cq;	    */
	shca->ib_device.req_notify_cq	    = ehca_req_notify_cq;
	/* shca->ib_device.req_ncomp_notif  = ehca_req_ncomp_notif; */
	shca->ib_device.get_dma_mr	    = ehca_get_dma_mr;
	shca->ib_device.reg_phys_mr	    = ehca_reg_phys_mr;
	shca->ib_device.reg_user_mr	    = ehca_reg_user_mr;
	shca->ib_device.query_mr	    = ehca_query_mr;
	shca->ib_device.dereg_mr	    = ehca_dereg_mr;
	shca->ib_device.rereg_phys_mr	    = ehca_rereg_phys_mr;
	shca->ib_device.alloc_mw	    = ehca_alloc_mw;
	shca->ib_device.bind_mw		    = ehca_bind_mw;
	shca->ib_device.dealloc_mw	    = ehca_dealloc_mw;
	shca->ib_device.alloc_fmr	    = ehca_alloc_fmr;
	shca->ib_device.map_phys_fmr	    = ehca_map_phys_fmr;
	shca->ib_device.unmap_fmr	    = ehca_unmap_fmr;
	shca->ib_device.dealloc_fmr	    = ehca_dealloc_fmr;
	shca->ib_device.attach_mcast	    = ehca_attach_mcast;
	shca->ib_device.detach_mcast	    = ehca_detach_mcast;
	/* shca->ib_device.process_mad	    = ehca_process_mad;	    */
	shca->ib_device.mmap		    = ehca_mmap;

	if (EHCA_BMASK_GET(HCA_CAP_SRQ, shca->hca_cap)) {
		shca->ib_device.uverbs_cmd_mask |=
			(1ull << IB_USER_VERBS_CMD_CREATE_SRQ) |
			(1ull << IB_USER_VERBS_CMD_MODIFY_SRQ) |
			(1ull << IB_USER_VERBS_CMD_QUERY_SRQ) |
			(1ull << IB_USER_VERBS_CMD_DESTROY_SRQ);

		shca->ib_device.create_srq          = ehca_create_srq;
		shca->ib_device.modify_srq          = ehca_modify_srq;
		shca->ib_device.query_srq           = ehca_query_srq;
		shca->ib_device.destroy_srq         = ehca_destroy_srq;
		shca->ib_device.post_srq_recv       = ehca_post_srq_recv;
	}

	return ret;
}

static int ehca_create_aqp1(struct ehca_shca *shca, u32 port)
{
	struct ehca_sport *sport = &shca->sport[port - 1];
	struct ib_cq *ibcq;
	struct ib_qp *ibqp;
	struct ib_qp_init_attr qp_init_attr;
	int ret;

	if (sport->ibcq_aqp1) {
		ehca_err(&shca->ib_device, "AQP1 CQ is already created.");
		return -EPERM;
	}

	ibcq = ib_create_cq(&shca->ib_device, NULL, NULL, (void *)(-1), 10, 0);
	if (IS_ERR(ibcq)) {
		ehca_err(&shca->ib_device, "Cannot create AQP1 CQ.");
		return PTR_ERR(ibcq);
	}
	sport->ibcq_aqp1 = ibcq;

	if (sport->ibqp_aqp1) {
		ehca_err(&shca->ib_device, "AQP1 QP is already created.");
		ret = -EPERM;
		goto create_aqp1;
	}

	memset(&qp_init_attr, 0, sizeof(struct ib_qp_init_attr));
	qp_init_attr.send_cq          = ibcq;
	qp_init_attr.recv_cq          = ibcq;
	qp_init_attr.sq_sig_type      = IB_SIGNAL_ALL_WR;
	qp_init_attr.cap.max_send_wr  = 100;
	qp_init_attr.cap.max_recv_wr  = 100;
	qp_init_attr.cap.max_send_sge = 2;
	qp_init_attr.cap.max_recv_sge = 1;
	qp_init_attr.qp_type          = IB_QPT_GSI;
	qp_init_attr.port_num         = port;
	qp_init_attr.qp_context       = NULL;
	qp_init_attr.event_handler    = NULL;
	qp_init_attr.srq              = NULL;

	ibqp = ib_create_qp(&shca->pd->ib_pd, &qp_init_attr);
	if (IS_ERR(ibqp)) {
		ehca_err(&shca->ib_device, "Cannot create AQP1 QP.");
		ret = PTR_ERR(ibqp);
		goto create_aqp1;
	}
	sport->ibqp_aqp1 = ibqp;

	return 0;

create_aqp1:
	ib_destroy_cq(sport->ibcq_aqp1);
	return ret;
}

static int ehca_destroy_aqp1(struct ehca_sport *sport)
{
	int ret;

	ret = ib_destroy_qp(sport->ibqp_aqp1);
	if (ret) {
		ehca_gen_err("Cannot destroy AQP1 QP. ret=%i", ret);
		return ret;
	}

	ret = ib_destroy_cq(sport->ibcq_aqp1);
	if (ret)
		ehca_gen_err("Cannot destroy AQP1 CQ. ret=%i", ret);

	return ret;
}

static ssize_t ehca_show_debug_level(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
			ehca_debug_level);
}

static ssize_t ehca_store_debug_level(struct device_driver *ddp,
				      const char *buf, size_t count)
{
	int value = (*buf) - '0';
	if (value >= 0 && value <= 9)
		ehca_debug_level = value;
	return 1;
}

DRIVER_ATTR(debug_level, S_IRUSR | S_IWUSR,
	    ehca_show_debug_level, ehca_store_debug_level);

static struct attribute *ehca_drv_attrs[] = {
	&driver_attr_debug_level.attr,
	NULL
};

static struct attribute_group ehca_drv_attr_grp = {
	.attrs = ehca_drv_attrs
};

#define EHCA_RESOURCE_ATTR(name)                                           \
static ssize_t  ehca_show_##name(struct device *dev,                       \
				 struct device_attribute *attr,            \
				 char *buf)                                \
{									   \
	struct ehca_shca *shca;						   \
	struct hipz_query_hca *rblock;				           \
	int data;                                                          \
									   \
	shca = dev->driver_data;					   \
									   \
	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);			   \
	if (!rblock) {						           \
		dev_err(dev, "Can't allocate rblock memory.\n");           \
		return 0;						   \
	}								   \
									   \
	if (hipz_h_query_hca(shca->ipz_hca_handle, rblock) != H_SUCCESS) { \
		dev_err(dev, "Can't query device properties\n");           \
		ehca_free_fw_ctrlblock(rblock);			   	   \
		return 0;					   	   \
	}								   \
									   \
	data = rblock->name;                                               \
	ehca_free_fw_ctrlblock(rblock);                                    \
									   \
	if ((strcmp(#name, "num_ports") == 0) && (ehca_nr_ports == 1))	   \
		return snprintf(buf, 256, "1\n");			   \
	else								   \
		return snprintf(buf, 256, "%d\n", data);		   \
									   \
}									   \
static DEVICE_ATTR(name, S_IRUGO, ehca_show_##name, NULL);

EHCA_RESOURCE_ATTR(num_ports);
EHCA_RESOURCE_ATTR(hw_ver);
EHCA_RESOURCE_ATTR(max_eq);
EHCA_RESOURCE_ATTR(cur_eq);
EHCA_RESOURCE_ATTR(max_cq);
EHCA_RESOURCE_ATTR(cur_cq);
EHCA_RESOURCE_ATTR(max_qp);
EHCA_RESOURCE_ATTR(cur_qp);
EHCA_RESOURCE_ATTR(max_mr);
EHCA_RESOURCE_ATTR(cur_mr);
EHCA_RESOURCE_ATTR(max_mw);
EHCA_RESOURCE_ATTR(cur_mw);
EHCA_RESOURCE_ATTR(max_pd);
EHCA_RESOURCE_ATTR(max_ah);

static ssize_t ehca_show_adapter_handle(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct ehca_shca *shca = dev->driver_data;

	return sprintf(buf, "%lx\n", shca->ipz_hca_handle.handle);

}
static DEVICE_ATTR(adapter_handle, S_IRUGO, ehca_show_adapter_handle, NULL);

static ssize_t ehca_show_mr_largepage(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "%d\n", ehca_mr_largepage);
}
static DEVICE_ATTR(mr_largepage, S_IRUGO, ehca_show_mr_largepage, NULL);

static struct attribute *ehca_dev_attrs[] = {
	&dev_attr_adapter_handle.attr,
	&dev_attr_num_ports.attr,
	&dev_attr_hw_ver.attr,
	&dev_attr_max_eq.attr,
	&dev_attr_cur_eq.attr,
	&dev_attr_max_cq.attr,
	&dev_attr_cur_cq.attr,
	&dev_attr_max_qp.attr,
	&dev_attr_cur_qp.attr,
	&dev_attr_max_mr.attr,
	&dev_attr_cur_mr.attr,
	&dev_attr_max_mw.attr,
	&dev_attr_cur_mw.attr,
	&dev_attr_max_pd.attr,
	&dev_attr_max_ah.attr,
	&dev_attr_mr_largepage.attr,
	NULL
};

static struct attribute_group ehca_dev_attr_grp = {
	.attrs = ehca_dev_attrs
};

static int __devinit ehca_probe(struct of_device *dev,
				const struct of_device_id *id)
{
	struct ehca_shca *shca;
	const u64 *handle;
	struct ib_pd *ibpd;
	int ret;

	handle = of_get_property(dev->node, "ibm,hca-handle", NULL);
	if (!handle) {
		ehca_gen_err("Cannot get eHCA handle for adapter: %s.",
			     dev->node->full_name);
		return -ENODEV;
	}

	if (!(*handle)) {
		ehca_gen_err("Wrong eHCA handle for adapter: %s.",
			     dev->node->full_name);
		return -ENODEV;
	}

	shca = (struct ehca_shca *)ib_alloc_device(sizeof(*shca));
	if (!shca) {
		ehca_gen_err("Cannot allocate shca memory.");
		return -ENOMEM;
	}
	mutex_init(&shca->modify_mutex);

	shca->ofdev = dev;
	shca->ipz_hca_handle.handle = *handle;
	dev->dev.driver_data = shca;

	ret = ehca_sense_attributes(shca);
	if (ret < 0) {
		ehca_gen_err("Cannot sense eHCA attributes.");
		goto probe1;
	}

	ret = ehca_init_device(shca);
	if (ret) {
		ehca_gen_err("Cannot init ehca  device struct");
		goto probe1;
	}

	/* create event queues */
	ret = ehca_create_eq(shca, &shca->eq, EHCA_EQ, 2048);
	if (ret) {
		ehca_err(&shca->ib_device, "Cannot create EQ.");
		goto probe1;
	}

	ret = ehca_create_eq(shca, &shca->neq, EHCA_NEQ, 513);
	if (ret) {
		ehca_err(&shca->ib_device, "Cannot create NEQ.");
		goto probe3;
	}

	/* create internal protection domain */
	ibpd = ehca_alloc_pd(&shca->ib_device, (void *)(-1), NULL);
	if (IS_ERR(ibpd)) {
		ehca_err(&shca->ib_device, "Cannot create internal PD.");
		ret = PTR_ERR(ibpd);
		goto probe4;
	}

	shca->pd = container_of(ibpd, struct ehca_pd, ib_pd);
	shca->pd->ib_pd.device = &shca->ib_device;

	/* create internal max MR */
	ret = ehca_reg_internal_maxmr(shca, shca->pd, &shca->maxmr);

	if (ret) {
		ehca_err(&shca->ib_device, "Cannot create internal MR ret=%i",
			 ret);
		goto probe5;
	}

	ret = ib_register_device(&shca->ib_device);
	if (ret) {
		ehca_err(&shca->ib_device,
			 "ib_register_device() failed ret=%i", ret);
		goto probe6;
	}

	/* create AQP1 for port 1 */
	if (ehca_open_aqp1 == 1) {
		shca->sport[0].port_state = IB_PORT_DOWN;
		ret = ehca_create_aqp1(shca, 1);
		if (ret) {
			ehca_err(&shca->ib_device,
				 "Cannot create AQP1 for port 1.");
			goto probe7;
		}
	}

	/* create AQP1 for port 2 */
	if ((ehca_open_aqp1 == 1) && (shca->num_ports == 2)) {
		shca->sport[1].port_state = IB_PORT_DOWN;
		ret = ehca_create_aqp1(shca, 2);
		if (ret) {
			ehca_err(&shca->ib_device,
				 "Cannot create AQP1 for port 2.");
			goto probe8;
		}
	}

	ret = sysfs_create_group(&dev->dev.kobj, &ehca_dev_attr_grp);
	if (ret) /* only complain; we can live without attributes */
		ehca_err(&shca->ib_device,
			 "Cannot create device attributes  ret=%d", ret);

	spin_lock(&shca_list_lock);
	list_add(&shca->shca_list, &shca_list);
	spin_unlock(&shca_list_lock);

	return 0;

probe8:
	ret = ehca_destroy_aqp1(&shca->sport[0]);
	if (ret)
		ehca_err(&shca->ib_device,
			 "Cannot destroy AQP1 for port 1. ret=%i", ret);

probe7:
	ib_unregister_device(&shca->ib_device);

probe6:
	ret = ehca_dereg_internal_maxmr(shca);
	if (ret)
		ehca_err(&shca->ib_device,
			 "Cannot destroy internal MR. ret=%x", ret);

probe5:
	ret = ehca_dealloc_pd(&shca->pd->ib_pd);
	if (ret)
		ehca_err(&shca->ib_device,
			 "Cannot destroy internal PD. ret=%x", ret);

probe4:
	ret = ehca_destroy_eq(shca, &shca->neq);
	if (ret)
		ehca_err(&shca->ib_device,
			 "Cannot destroy NEQ. ret=%x", ret);

probe3:
	ret = ehca_destroy_eq(shca, &shca->eq);
	if (ret)
		ehca_err(&shca->ib_device,
			 "Cannot destroy EQ. ret=%x", ret);

probe1:
	ib_dealloc_device(&shca->ib_device);

	return -EINVAL;
}

static int __devexit ehca_remove(struct of_device *dev)
{
	struct ehca_shca *shca = dev->dev.driver_data;
	int ret;

	sysfs_remove_group(&dev->dev.kobj, &ehca_dev_attr_grp);

	if (ehca_open_aqp1 == 1) {
		int i;
		for (i = 0; i < shca->num_ports; i++) {
			ret = ehca_destroy_aqp1(&shca->sport[i]);
			if (ret)
				ehca_err(&shca->ib_device,
					 "Cannot destroy AQP1 for port %x "
					 "ret=%i", ret, i);
		}
	}

	ib_unregister_device(&shca->ib_device);

	ret = ehca_dereg_internal_maxmr(shca);
	if (ret)
		ehca_err(&shca->ib_device,
			 "Cannot destroy internal MR. ret=%i", ret);

	ret = ehca_dealloc_pd(&shca->pd->ib_pd);
	if (ret)
		ehca_err(&shca->ib_device,
			 "Cannot destroy internal PD. ret=%i", ret);

	ret = ehca_destroy_eq(shca, &shca->eq);
	if (ret)
		ehca_err(&shca->ib_device, "Cannot destroy EQ. ret=%i", ret);

	ret = ehca_destroy_eq(shca, &shca->neq);
	if (ret)
		ehca_err(&shca->ib_device, "Canot destroy NEQ. ret=%i", ret);

	ib_dealloc_device(&shca->ib_device);

	spin_lock(&shca_list_lock);
	list_del(&shca->shca_list);
	spin_unlock(&shca_list_lock);

	return ret;
}

static struct of_device_id ehca_device_table[] =
{
	{
		.name       = "lhca",
		.compatible = "IBM,lhca",
	},
	{},
};

static struct of_platform_driver ehca_driver = {
	.name        = "ehca",
	.match_table = ehca_device_table,
	.probe       = ehca_probe,
	.remove      = ehca_remove,
};

void ehca_poll_eqs(unsigned long data)
{
	struct ehca_shca *shca;

	spin_lock(&shca_list_lock);
	list_for_each_entry(shca, &shca_list, shca_list) {
		if (shca->eq.is_initialized) {
			/* call deadman proc only if eq ptr does not change */
			struct ehca_eq *eq = &shca->eq;
			int max = 3;
			volatile u64 q_ofs, q_ofs2;
			u64 flags;
			spin_lock_irqsave(&eq->spinlock, flags);
			q_ofs = eq->ipz_queue.current_q_offset;
			spin_unlock_irqrestore(&eq->spinlock, flags);
			do {
				spin_lock_irqsave(&eq->spinlock, flags);
				q_ofs2 = eq->ipz_queue.current_q_offset;
				spin_unlock_irqrestore(&eq->spinlock, flags);
				max--;
			} while (q_ofs == q_ofs2 && max > 0);
			if (q_ofs == q_ofs2)
				ehca_process_eq(shca, 0);
		}
	}
	mod_timer(&poll_eqs_timer, jiffies + HZ);
	spin_unlock(&shca_list_lock);
}

int __init ehca_module_init(void)
{
	int ret;

	printk(KERN_INFO "eHCA Infiniband Device Driver "
	       "(Version " HCAD_VERSION ")\n");

	ret = ehca_create_comp_pool();
	if (ret) {
		ehca_gen_err("Cannot create comp pool.");
		return ret;
	}

	ret = ehca_create_slab_caches();
	if (ret) {
		ehca_gen_err("Cannot create SLAB caches");
		ret = -ENOMEM;
		goto module_init1;
	}

	ret = ibmebus_register_driver(&ehca_driver);
	if (ret) {
		ehca_gen_err("Cannot register eHCA device driver");
		ret = -EINVAL;
		goto module_init2;
	}

	ret = sysfs_create_group(&ehca_driver.driver.kobj, &ehca_drv_attr_grp);
	if (ret) /* only complain; we can live without attributes */
		ehca_gen_err("Cannot create driver attributes  ret=%d", ret);

	if (ehca_poll_all_eqs != 1) {
		ehca_gen_err("WARNING!!!");
		ehca_gen_err("It is possible to lose interrupts.");
	} else {
		init_timer(&poll_eqs_timer);
		poll_eqs_timer.function = ehca_poll_eqs;
		poll_eqs_timer.expires = jiffies + HZ;
		add_timer(&poll_eqs_timer);
	}

	return 0;

module_init2:
	ehca_destroy_slab_caches();

module_init1:
	ehca_destroy_comp_pool();
	return ret;
};

void __exit ehca_module_exit(void)
{
	if (ehca_poll_all_eqs == 1)
		del_timer_sync(&poll_eqs_timer);

	sysfs_remove_group(&ehca_driver.driver.kobj, &ehca_drv_attr_grp);
	ibmebus_unregister_driver(&ehca_driver);

	ehca_destroy_slab_caches();

	ehca_destroy_comp_pool();

	idr_destroy(&ehca_cq_idr);
	idr_destroy(&ehca_qp_idr);
};

module_init(ehca_module_init);
module_exit(ehca_module_exit);
