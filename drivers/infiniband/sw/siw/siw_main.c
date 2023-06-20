// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <net/net_namespace.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <net/addrconf.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/rdma_netlink.h>
#include <linux/kthread.h>

#include "siw.h"
#include "siw_verbs.h"

MODULE_AUTHOR("Bernard Metzler");
MODULE_DESCRIPTION("Software iWARP Driver");
MODULE_LICENSE("Dual BSD/GPL");

/* transmit from user buffer, if possible */
const bool zcopy_tx = true;

/* Restrict usage of GSO, if hardware peer iwarp is unable to process
 * large packets. try_gso = true lets siw try to use local GSO,
 * if peer agrees.  Not using GSO severly limits siw maximum tx bandwidth.
 */
const bool try_gso;

/* Attach siw also with loopback devices */
const bool loopback_enabled = true;

/* We try to negotiate CRC on, if true */
const bool mpa_crc_required;

/* MPA CRC on/off enforced */
const bool mpa_crc_strict;

/* Control TCP_NODELAY socket option */
const bool siw_tcp_nagle;

/* Select MPA version to be used during connection setup */
u_char mpa_version = MPA_REVISION_2;

/* Selects MPA P2P mode (additional handshake during connection
 * setup, if true.
 */
const bool peer_to_peer;

struct task_struct *siw_tx_thread[NR_CPUS];
struct crypto_shash *siw_crypto_shash;

static int siw_device_register(struct siw_device *sdev, const char *name)
{
	struct ib_device *base_dev = &sdev->base_dev;
	static int dev_id = 1;
	int rv;

	sdev->vendor_part_id = dev_id++;

	rv = ib_register_device(base_dev, name, NULL);
	if (rv) {
		pr_warn("siw: device registration error %d\n", rv);
		return rv;
	}

	siw_dbg(base_dev, "HWaddr=%pM\n", sdev->netdev->dev_addr);

	return 0;
}

static void siw_device_cleanup(struct ib_device *base_dev)
{
	struct siw_device *sdev = to_siw_dev(base_dev);

	xa_destroy(&sdev->qp_xa);
	xa_destroy(&sdev->mem_xa);
}

static int siw_create_tx_threads(void)
{
	int cpu, assigned = 0;

	for_each_online_cpu(cpu) {
		/* Skip HT cores */
		if (cpu % cpumask_weight(topology_sibling_cpumask(cpu)))
			continue;

		siw_tx_thread[cpu] =
			kthread_run_on_cpu(siw_run_sq,
					   (unsigned long *)(long)cpu,
					   cpu, "siw_tx/%u");
		if (IS_ERR(siw_tx_thread[cpu])) {
			siw_tx_thread[cpu] = NULL;
			continue;
		}

		assigned++;
	}
	return assigned;
}

static int siw_dev_qualified(struct net_device *netdev)
{
	/*
	 * Additional hardware support can be added here
	 * (e.g. ARPHRD_FDDI, ARPHRD_ATM, ...) - see
	 * <linux/if_arp.h> for type identifiers.
	 */
	if (netdev->type == ARPHRD_ETHER || netdev->type == ARPHRD_IEEE802 ||
	    netdev->type == ARPHRD_NONE ||
	    (netdev->type == ARPHRD_LOOPBACK && loopback_enabled))
		return 1;

	return 0;
}

static DEFINE_PER_CPU(atomic_t, siw_use_cnt);

static struct {
	struct cpumask **tx_valid_cpus;
	int num_nodes;
} siw_cpu_info;

static int siw_init_cpulist(void)
{
	int i, num_nodes = nr_node_ids;

	memset(siw_tx_thread, 0, sizeof(siw_tx_thread));

	siw_cpu_info.num_nodes = num_nodes;

	siw_cpu_info.tx_valid_cpus =
		kcalloc(num_nodes, sizeof(struct cpumask *), GFP_KERNEL);
	if (!siw_cpu_info.tx_valid_cpus) {
		siw_cpu_info.num_nodes = 0;
		return -ENOMEM;
	}
	for (i = 0; i < siw_cpu_info.num_nodes; i++) {
		siw_cpu_info.tx_valid_cpus[i] =
			kzalloc(sizeof(struct cpumask), GFP_KERNEL);
		if (!siw_cpu_info.tx_valid_cpus[i])
			goto out_err;

		cpumask_clear(siw_cpu_info.tx_valid_cpus[i]);
	}
	for_each_possible_cpu(i)
		cpumask_set_cpu(i, siw_cpu_info.tx_valid_cpus[cpu_to_node(i)]);

	return 0;

out_err:
	siw_cpu_info.num_nodes = 0;
	while (--i >= 0)
		kfree(siw_cpu_info.tx_valid_cpus[i]);
	kfree(siw_cpu_info.tx_valid_cpus);
	siw_cpu_info.tx_valid_cpus = NULL;

	return -ENOMEM;
}

static void siw_destroy_cpulist(void)
{
	int i = 0;

	while (i < siw_cpu_info.num_nodes)
		kfree(siw_cpu_info.tx_valid_cpus[i++]);

	kfree(siw_cpu_info.tx_valid_cpus);
}

/*
 * Choose CPU with least number of active QP's from NUMA node of
 * TX interface.
 */
int siw_get_tx_cpu(struct siw_device *sdev)
{
	const struct cpumask *tx_cpumask;
	int i, num_cpus, cpu, min_use, node = sdev->numa_node, tx_cpu = -1;

	if (node < 0)
		tx_cpumask = cpu_online_mask;
	else
		tx_cpumask = siw_cpu_info.tx_valid_cpus[node];

	num_cpus = cpumask_weight(tx_cpumask);
	if (!num_cpus) {
		/* no CPU on this NUMA node */
		tx_cpumask = cpu_online_mask;
		num_cpus = cpumask_weight(tx_cpumask);
	}
	if (!num_cpus)
		goto out;

	cpu = cpumask_first(tx_cpumask);

	for (i = 0, min_use = SIW_MAX_QP; i < num_cpus;
	     i++, cpu = cpumask_next(cpu, tx_cpumask)) {
		int usage;

		/* Skip any cores which have no TX thread */
		if (!siw_tx_thread[cpu])
			continue;

		usage = atomic_read(&per_cpu(siw_use_cnt, cpu));
		if (usage <= min_use) {
			tx_cpu = cpu;
			min_use = usage;
		}
	}
	siw_dbg(&sdev->base_dev,
		"tx cpu %d, node %d, %d qp's\n", tx_cpu, node, min_use);

out:
	if (tx_cpu >= 0)
		atomic_inc(&per_cpu(siw_use_cnt, tx_cpu));
	else
		pr_warn("siw: no tx cpu found\n");

	return tx_cpu;
}

void siw_put_tx_cpu(int cpu)
{
	atomic_dec(&per_cpu(siw_use_cnt, cpu));
}

static struct ib_qp *siw_get_base_qp(struct ib_device *base_dev, int id)
{
	struct siw_qp *qp = siw_qp_id2obj(to_siw_dev(base_dev), id);

	if (qp) {
		/*
		 * siw_qp_id2obj() increments object reference count
		 */
		siw_qp_put(qp);
		return &qp->base_qp;
	}
	return NULL;
}

static const struct ib_device_ops siw_device_ops = {
	.owner = THIS_MODULE,
	.uverbs_abi_ver = SIW_ABI_VERSION,
	.driver_id = RDMA_DRIVER_SIW,

	.alloc_mr = siw_alloc_mr,
	.alloc_pd = siw_alloc_pd,
	.alloc_ucontext = siw_alloc_ucontext,
	.create_cq = siw_create_cq,
	.create_qp = siw_create_qp,
	.create_srq = siw_create_srq,
	.dealloc_driver = siw_device_cleanup,
	.dealloc_pd = siw_dealloc_pd,
	.dealloc_ucontext = siw_dealloc_ucontext,
	.dereg_mr = siw_dereg_mr,
	.destroy_cq = siw_destroy_cq,
	.destroy_qp = siw_destroy_qp,
	.destroy_srq = siw_destroy_srq,
	.get_dma_mr = siw_get_dma_mr,
	.get_port_immutable = siw_get_port_immutable,
	.iw_accept = siw_accept,
	.iw_add_ref = siw_qp_get_ref,
	.iw_connect = siw_connect,
	.iw_create_listen = siw_create_listen,
	.iw_destroy_listen = siw_destroy_listen,
	.iw_get_qp = siw_get_base_qp,
	.iw_reject = siw_reject,
	.iw_rem_ref = siw_qp_put_ref,
	.map_mr_sg = siw_map_mr_sg,
	.mmap = siw_mmap,
	.mmap_free = siw_mmap_free,
	.modify_qp = siw_verbs_modify_qp,
	.modify_srq = siw_modify_srq,
	.poll_cq = siw_poll_cq,
	.post_recv = siw_post_receive,
	.post_send = siw_post_send,
	.post_srq_recv = siw_post_srq_recv,
	.query_device = siw_query_device,
	.query_gid = siw_query_gid,
	.query_port = siw_query_port,
	.query_qp = siw_query_qp,
	.query_srq = siw_query_srq,
	.req_notify_cq = siw_req_notify_cq,
	.reg_user_mr = siw_reg_user_mr,

	INIT_RDMA_OBJ_SIZE(ib_cq, siw_cq, base_cq),
	INIT_RDMA_OBJ_SIZE(ib_pd, siw_pd, base_pd),
	INIT_RDMA_OBJ_SIZE(ib_qp, siw_qp, base_qp),
	INIT_RDMA_OBJ_SIZE(ib_srq, siw_srq, base_srq),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, siw_ucontext, base_ucontext),
};

static struct siw_device *siw_device_create(struct net_device *netdev)
{
	struct siw_device *sdev = NULL;
	struct ib_device *base_dev;
	int rv;

	sdev = ib_alloc_device(siw_device, base_dev);
	if (!sdev)
		return NULL;

	base_dev = &sdev->base_dev;

	sdev->netdev = netdev;

	if (netdev->type != ARPHRD_LOOPBACK && netdev->type != ARPHRD_NONE) {
		addrconf_addr_eui48((unsigned char *)&base_dev->node_guid,
				    netdev->dev_addr);
	} else {
		/*
		 * This device does not have a HW address,
		 * but connection mangagement lib expects gid != 0
		 */
		size_t len = min_t(size_t, strlen(base_dev->name), 6);
		char addr[6] = { };

		memcpy(addr, base_dev->name, len);
		addrconf_addr_eui48((unsigned char *)&base_dev->node_guid,
				    addr);
	}

	base_dev->uverbs_cmd_mask |= BIT_ULL(IB_USER_VERBS_CMD_POST_SEND);

	base_dev->node_type = RDMA_NODE_RNIC;
	memcpy(base_dev->node_desc, SIW_NODE_DESC_COMMON,
	       sizeof(SIW_NODE_DESC_COMMON));

	/*
	 * Current model (one-to-one device association):
	 * One Softiwarp device per net_device or, equivalently,
	 * per physical port.
	 */
	base_dev->phys_port_cnt = 1;
	base_dev->num_comp_vectors = num_possible_cpus();

	xa_init_flags(&sdev->qp_xa, XA_FLAGS_ALLOC1);
	xa_init_flags(&sdev->mem_xa, XA_FLAGS_ALLOC1);

	ib_set_device_ops(base_dev, &siw_device_ops);
	rv = ib_device_set_netdev(base_dev, netdev, 1);
	if (rv)
		goto error;

	memcpy(base_dev->iw_ifname, netdev->name,
	       sizeof(base_dev->iw_ifname));

	/* Disable TCP port mapping */
	base_dev->iw_driver_flags = IW_F_NO_PORT_MAP;

	sdev->attrs.max_qp = SIW_MAX_QP;
	sdev->attrs.max_qp_wr = SIW_MAX_QP_WR;
	sdev->attrs.max_ord = SIW_MAX_ORD_QP;
	sdev->attrs.max_ird = SIW_MAX_IRD_QP;
	sdev->attrs.max_sge = SIW_MAX_SGE;
	sdev->attrs.max_sge_rd = SIW_MAX_SGE_RD;
	sdev->attrs.max_cq = SIW_MAX_CQ;
	sdev->attrs.max_cqe = SIW_MAX_CQE;
	sdev->attrs.max_mr = SIW_MAX_MR;
	sdev->attrs.max_pd = SIW_MAX_PD;
	sdev->attrs.max_mw = SIW_MAX_MW;
	sdev->attrs.max_srq = SIW_MAX_SRQ;
	sdev->attrs.max_srq_wr = SIW_MAX_SRQ_WR;
	sdev->attrs.max_srq_sge = SIW_MAX_SGE;

	INIT_LIST_HEAD(&sdev->cep_list);
	INIT_LIST_HEAD(&sdev->qp_list);

	atomic_set(&sdev->num_ctx, 0);
	atomic_set(&sdev->num_srq, 0);
	atomic_set(&sdev->num_qp, 0);
	atomic_set(&sdev->num_cq, 0);
	atomic_set(&sdev->num_mr, 0);
	atomic_set(&sdev->num_pd, 0);

	sdev->numa_node = dev_to_node(&netdev->dev);
	spin_lock_init(&sdev->lock);

	return sdev;
error:
	ib_dealloc_device(base_dev);

	return NULL;
}

/*
 * Network link becomes unavailable. Mark all
 * affected QP's accordingly.
 */
static void siw_netdev_down(struct work_struct *work)
{
	struct siw_device *sdev =
		container_of(work, struct siw_device, netdev_down);

	struct siw_qp_attrs qp_attrs;
	struct list_head *pos, *tmp;

	memset(&qp_attrs, 0, sizeof(qp_attrs));
	qp_attrs.state = SIW_QP_STATE_ERROR;

	list_for_each_safe(pos, tmp, &sdev->qp_list) {
		struct siw_qp *qp = list_entry(pos, struct siw_qp, devq);

		down_write(&qp->state_lock);
		WARN_ON(siw_qp_modify(qp, &qp_attrs, SIW_QP_ATTR_STATE));
		up_write(&qp->state_lock);
	}
	ib_device_put(&sdev->base_dev);
}

static void siw_device_goes_down(struct siw_device *sdev)
{
	if (ib_device_try_get(&sdev->base_dev)) {
		INIT_WORK(&sdev->netdev_down, siw_netdev_down);
		schedule_work(&sdev->netdev_down);
	}
}

static int siw_netdev_event(struct notifier_block *nb, unsigned long event,
			    void *arg)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(arg);
	struct ib_device *base_dev;
	struct siw_device *sdev;

	dev_dbg(&netdev->dev, "siw: event %lu\n", event);

	if (dev_net(netdev) != &init_net)
		return NOTIFY_OK;

	base_dev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_SIW);
	if (!base_dev)
		return NOTIFY_OK;

	sdev = to_siw_dev(base_dev);

	switch (event) {
	case NETDEV_UP:
		sdev->state = IB_PORT_ACTIVE;
		siw_port_event(sdev, 1, IB_EVENT_PORT_ACTIVE);
		break;

	case NETDEV_GOING_DOWN:
		siw_device_goes_down(sdev);
		break;

	case NETDEV_DOWN:
		sdev->state = IB_PORT_DOWN;
		siw_port_event(sdev, 1, IB_EVENT_PORT_ERR);
		break;

	case NETDEV_REGISTER:
		/*
		 * Device registration now handled only by
		 * rdma netlink commands. So it shall be impossible
		 * to end up here with a valid siw device.
		 */
		siw_dbg(base_dev, "unexpected NETDEV_REGISTER event\n");
		break;

	case NETDEV_UNREGISTER:
		ib_unregister_device_queued(&sdev->base_dev);
		break;

	case NETDEV_CHANGEADDR:
		siw_port_event(sdev, 1, IB_EVENT_LID_CHANGE);
		break;
	/*
	 * Todo: Below netdev events are currently not handled.
	 */
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGE:
		break;

	default:
		break;
	}
	ib_device_put(&sdev->base_dev);

	return NOTIFY_OK;
}

static struct notifier_block siw_netdev_nb = {
	.notifier_call = siw_netdev_event,
};

static int siw_newlink(const char *basedev_name, struct net_device *netdev)
{
	struct ib_device *base_dev;
	struct siw_device *sdev = NULL;
	int rv = -ENOMEM;

	if (!siw_dev_qualified(netdev))
		return -EINVAL;

	base_dev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_SIW);
	if (base_dev) {
		ib_device_put(base_dev);
		return -EEXIST;
	}
	sdev = siw_device_create(netdev);
	if (sdev) {
		dev_dbg(&netdev->dev, "siw: new device\n");

		if (netif_running(netdev) && netif_carrier_ok(netdev))
			sdev->state = IB_PORT_ACTIVE;
		else
			sdev->state = IB_PORT_DOWN;

		rv = siw_device_register(sdev, basedev_name);
		if (rv)
			ib_dealloc_device(&sdev->base_dev);
	}
	return rv;
}

static struct rdma_link_ops siw_link_ops = {
	.type = "siw",
	.newlink = siw_newlink,
};

/*
 * siw_init_module - Initialize Softiwarp module and register with netdev
 *                   subsystem.
 */
static __init int siw_init_module(void)
{
	int rv;
	int nr_cpu;

	if (SENDPAGE_THRESH < SIW_MAX_INLINE) {
		pr_info("siw: sendpage threshold too small: %u\n",
			(int)SENDPAGE_THRESH);
		rv = -EINVAL;
		goto out_error;
	}
	rv = siw_init_cpulist();
	if (rv)
		goto out_error;

	rv = siw_cm_init();
	if (rv)
		goto out_error;

	if (!siw_create_tx_threads()) {
		pr_info("siw: Could not start any TX thread\n");
		rv = -ENOMEM;
		goto out_error;
	}
	/*
	 * Locate CRC32 algorithm. If unsuccessful, fail
	 * loading siw only, if CRC is required.
	 */
	siw_crypto_shash = crypto_alloc_shash("crc32c", 0, 0);
	if (IS_ERR(siw_crypto_shash)) {
		pr_info("siw: Loading CRC32c failed: %ld\n",
			PTR_ERR(siw_crypto_shash));
		siw_crypto_shash = NULL;
		if (mpa_crc_required) {
			rv = -EOPNOTSUPP;
			goto out_error;
		}
	}
	rv = register_netdevice_notifier(&siw_netdev_nb);
	if (rv)
		goto out_error;

	rdma_link_register(&siw_link_ops);

	pr_info("SoftiWARP attached\n");
	return 0;

out_error:
	for (nr_cpu = 0; nr_cpu < nr_cpu_ids; nr_cpu++) {
		if (siw_tx_thread[nr_cpu]) {
			siw_stop_tx_thread(nr_cpu);
			siw_tx_thread[nr_cpu] = NULL;
		}
	}
	if (siw_crypto_shash)
		crypto_free_shash(siw_crypto_shash);

	pr_info("SoftIWARP attach failed. Error: %d\n", rv);

	siw_cm_exit();
	siw_destroy_cpulist();

	return rv;
}

static void __exit siw_exit_module(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (siw_tx_thread[cpu]) {
			siw_stop_tx_thread(cpu);
			siw_tx_thread[cpu] = NULL;
		}
	}
	unregister_netdevice_notifier(&siw_netdev_nb);
	rdma_link_unregister(&siw_link_ops);
	ib_unregister_driver(RDMA_DRIVER_SIW);

	siw_cm_exit();

	siw_destroy_cpulist();

	if (siw_crypto_shash)
		crypto_free_shash(siw_crypto_shash);

	pr_info("SoftiWARP detached\n");
}

module_init(siw_init_module);
module_exit(siw_exit_module);

MODULE_ALIAS_RDMA_LINK("siw");
