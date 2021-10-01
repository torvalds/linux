// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/net/ethernet/ibm/ehea/ehea_main.c
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *	 Christoph Raisch <raisch@de.ibm.com>
 *	 Jan-Bernd Themann <themann@de.ibm.com>
 *	 Thomas Klein <tklein@de.ibm.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/if_ether.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/memory.h>
#include <asm/kexec.h>
#include <linux/mutex.h>
#include <linux/prefetch.h>

#include <net/ip.h>

#include "ehea.h"
#include "ehea_qmr.h"
#include "ehea_phyp.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christoph Raisch <raisch@de.ibm.com>");
MODULE_DESCRIPTION("IBM eServer HEA Driver");
MODULE_VERSION(DRV_VERSION);


static int msg_level = -1;
static int rq1_entries = EHEA_DEF_ENTRIES_RQ1;
static int rq2_entries = EHEA_DEF_ENTRIES_RQ2;
static int rq3_entries = EHEA_DEF_ENTRIES_RQ3;
static int sq_entries = EHEA_DEF_ENTRIES_SQ;
static int use_mcs = 1;
static int prop_carrier_state;

module_param(msg_level, int, 0);
module_param(rq1_entries, int, 0);
module_param(rq2_entries, int, 0);
module_param(rq3_entries, int, 0);
module_param(sq_entries, int, 0);
module_param(prop_carrier_state, int, 0);
module_param(use_mcs, int, 0);

MODULE_PARM_DESC(msg_level, "msg_level");
MODULE_PARM_DESC(prop_carrier_state, "Propagate carrier state of physical "
		 "port to stack. 1:yes, 0:no.  Default = 0 ");
MODULE_PARM_DESC(rq3_entries, "Number of entries for Receive Queue 3 "
		 "[2^x - 1], x = [7..14]. Default = "
		 __MODULE_STRING(EHEA_DEF_ENTRIES_RQ3) ")");
MODULE_PARM_DESC(rq2_entries, "Number of entries for Receive Queue 2 "
		 "[2^x - 1], x = [7..14]. Default = "
		 __MODULE_STRING(EHEA_DEF_ENTRIES_RQ2) ")");
MODULE_PARM_DESC(rq1_entries, "Number of entries for Receive Queue 1 "
		 "[2^x - 1], x = [7..14]. Default = "
		 __MODULE_STRING(EHEA_DEF_ENTRIES_RQ1) ")");
MODULE_PARM_DESC(sq_entries, " Number of entries for the Send Queue  "
		 "[2^x - 1], x = [7..14]. Default = "
		 __MODULE_STRING(EHEA_DEF_ENTRIES_SQ) ")");
MODULE_PARM_DESC(use_mcs, " Multiple receive queues, 1: enable, 0: disable, "
		 "Default = 1");

static int port_name_cnt;
static LIST_HEAD(adapter_list);
static unsigned long ehea_driver_flags;
static DEFINE_MUTEX(dlpar_mem_lock);
static struct ehea_fw_handle_array ehea_fw_handles;
static struct ehea_bcmc_reg_array ehea_bcmc_regs;


static int ehea_probe_adapter(struct platform_device *dev);

static int ehea_remove(struct platform_device *dev);

static const struct of_device_id ehea_module_device_table[] = {
	{
		.name = "lhea",
		.compatible = "IBM,lhea",
	},
	{
		.type = "network",
		.compatible = "IBM,lhea-ethernet",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ehea_module_device_table);

static const struct of_device_id ehea_device_table[] = {
	{
		.name = "lhea",
		.compatible = "IBM,lhea",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ehea_device_table);

static struct platform_driver ehea_driver = {
	.driver = {
		.name = "ehea",
		.owner = THIS_MODULE,
		.of_match_table = ehea_device_table,
	},
	.probe = ehea_probe_adapter,
	.remove = ehea_remove,
};

void ehea_dump(void *adr, int len, char *msg)
{
	int x;
	unsigned char *deb = adr;
	for (x = 0; x < len; x += 16) {
		pr_info("%s adr=%p ofs=%04x %016llx %016llx\n",
			msg, deb, x, *((u64 *)&deb[0]), *((u64 *)&deb[8]));
		deb += 16;
	}
}

static void ehea_schedule_port_reset(struct ehea_port *port)
{
	if (!test_bit(__EHEA_DISABLE_PORT_RESET, &port->flags))
		schedule_work(&port->reset_task);
}

static void ehea_update_firmware_handles(void)
{
	struct ehea_fw_handle_entry *arr = NULL;
	struct ehea_adapter *adapter;
	int num_adapters = 0;
	int num_ports = 0;
	int num_portres = 0;
	int i = 0;
	int num_fw_handles, k, l;

	/* Determine number of handles */
	mutex_lock(&ehea_fw_handles.lock);

	list_for_each_entry(adapter, &adapter_list, list) {
		num_adapters++;

		for (k = 0; k < EHEA_MAX_PORTS; k++) {
			struct ehea_port *port = adapter->port[k];

			if (!port || (port->state != EHEA_PORT_UP))
				continue;

			num_ports++;
			num_portres += port->num_def_qps;
		}
	}

	num_fw_handles = num_adapters * EHEA_NUM_ADAPTER_FW_HANDLES +
			 num_ports * EHEA_NUM_PORT_FW_HANDLES +
			 num_portres * EHEA_NUM_PORTRES_FW_HANDLES;

	if (num_fw_handles) {
		arr = kcalloc(num_fw_handles, sizeof(*arr), GFP_KERNEL);
		if (!arr)
			goto out;  /* Keep the existing array */
	} else
		goto out_update;

	list_for_each_entry(adapter, &adapter_list, list) {
		if (num_adapters == 0)
			break;

		for (k = 0; k < EHEA_MAX_PORTS; k++) {
			struct ehea_port *port = adapter->port[k];

			if (!port || (port->state != EHEA_PORT_UP) ||
			    (num_ports == 0))
				continue;

			for (l = 0; l < port->num_def_qps; l++) {
				struct ehea_port_res *pr = &port->port_res[l];

				arr[i].adh = adapter->handle;
				arr[i++].fwh = pr->qp->fw_handle;
				arr[i].adh = adapter->handle;
				arr[i++].fwh = pr->send_cq->fw_handle;
				arr[i].adh = adapter->handle;
				arr[i++].fwh = pr->recv_cq->fw_handle;
				arr[i].adh = adapter->handle;
				arr[i++].fwh = pr->eq->fw_handle;
				arr[i].adh = adapter->handle;
				arr[i++].fwh = pr->send_mr.handle;
				arr[i].adh = adapter->handle;
				arr[i++].fwh = pr->recv_mr.handle;
			}
			arr[i].adh = adapter->handle;
			arr[i++].fwh = port->qp_eq->fw_handle;
			num_ports--;
		}

		arr[i].adh = adapter->handle;
		arr[i++].fwh = adapter->neq->fw_handle;

		if (adapter->mr.handle) {
			arr[i].adh = adapter->handle;
			arr[i++].fwh = adapter->mr.handle;
		}
		num_adapters--;
	}

out_update:
	kfree(ehea_fw_handles.arr);
	ehea_fw_handles.arr = arr;
	ehea_fw_handles.num_entries = i;
out:
	mutex_unlock(&ehea_fw_handles.lock);
}

static void ehea_update_bcmc_registrations(void)
{
	unsigned long flags;
	struct ehea_bcmc_reg_entry *arr = NULL;
	struct ehea_adapter *adapter;
	struct ehea_mc_list *mc_entry;
	int num_registrations = 0;
	int i = 0;
	int k;

	spin_lock_irqsave(&ehea_bcmc_regs.lock, flags);

	/* Determine number of registrations */
	list_for_each_entry(adapter, &adapter_list, list)
		for (k = 0; k < EHEA_MAX_PORTS; k++) {
			struct ehea_port *port = adapter->port[k];

			if (!port || (port->state != EHEA_PORT_UP))
				continue;

			num_registrations += 2;	/* Broadcast registrations */

			list_for_each_entry(mc_entry, &port->mc_list->list,list)
				num_registrations += 2;
		}

	if (num_registrations) {
		arr = kcalloc(num_registrations, sizeof(*arr), GFP_ATOMIC);
		if (!arr)
			goto out;  /* Keep the existing array */
	} else
		goto out_update;

	list_for_each_entry(adapter, &adapter_list, list) {
		for (k = 0; k < EHEA_MAX_PORTS; k++) {
			struct ehea_port *port = adapter->port[k];

			if (!port || (port->state != EHEA_PORT_UP))
				continue;

			if (num_registrations == 0)
				goto out_update;

			arr[i].adh = adapter->handle;
			arr[i].port_id = port->logical_port_id;
			arr[i].reg_type = EHEA_BCMC_BROADCAST |
					  EHEA_BCMC_UNTAGGED;
			arr[i++].macaddr = port->mac_addr;

			arr[i].adh = adapter->handle;
			arr[i].port_id = port->logical_port_id;
			arr[i].reg_type = EHEA_BCMC_BROADCAST |
					  EHEA_BCMC_VLANID_ALL;
			arr[i++].macaddr = port->mac_addr;
			num_registrations -= 2;

			list_for_each_entry(mc_entry,
					    &port->mc_list->list, list) {
				if (num_registrations == 0)
					goto out_update;

				arr[i].adh = adapter->handle;
				arr[i].port_id = port->logical_port_id;
				arr[i].reg_type = EHEA_BCMC_MULTICAST |
						  EHEA_BCMC_UNTAGGED;
				if (mc_entry->macaddr == 0)
					arr[i].reg_type |= EHEA_BCMC_SCOPE_ALL;
				arr[i++].macaddr = mc_entry->macaddr;

				arr[i].adh = adapter->handle;
				arr[i].port_id = port->logical_port_id;
				arr[i].reg_type = EHEA_BCMC_MULTICAST |
						  EHEA_BCMC_VLANID_ALL;
				if (mc_entry->macaddr == 0)
					arr[i].reg_type |= EHEA_BCMC_SCOPE_ALL;
				arr[i++].macaddr = mc_entry->macaddr;
				num_registrations -= 2;
			}
		}
	}

out_update:
	kfree(ehea_bcmc_regs.arr);
	ehea_bcmc_regs.arr = arr;
	ehea_bcmc_regs.num_entries = i;
out:
	spin_unlock_irqrestore(&ehea_bcmc_regs.lock, flags);
}

static void ehea_get_stats64(struct net_device *dev,
			     struct rtnl_link_stats64 *stats)
{
	struct ehea_port *port = netdev_priv(dev);
	u64 rx_packets = 0, tx_packets = 0, rx_bytes = 0, tx_bytes = 0;
	int i;

	for (i = 0; i < port->num_def_qps; i++) {
		rx_packets += port->port_res[i].rx_packets;
		rx_bytes   += port->port_res[i].rx_bytes;
	}

	for (i = 0; i < port->num_def_qps; i++) {
		tx_packets += port->port_res[i].tx_packets;
		tx_bytes   += port->port_res[i].tx_bytes;
	}

	stats->tx_packets = tx_packets;
	stats->rx_bytes = rx_bytes;
	stats->tx_bytes = tx_bytes;
	stats->rx_packets = rx_packets;

	stats->multicast = port->stats.multicast;
	stats->rx_errors = port->stats.rx_errors;
}

static void ehea_update_stats(struct work_struct *work)
{
	struct ehea_port *port =
		container_of(work, struct ehea_port, stats_work.work);
	struct net_device *dev = port->netdev;
	struct rtnl_link_stats64 *stats = &port->stats;
	struct hcp_ehea_port_cb2 *cb2;
	u64 hret;

	cb2 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb2) {
		netdev_err(dev, "No mem for cb2. Some interface statistics were not updated\n");
		goto resched;
	}

	hret = ehea_h_query_ehea_port(port->adapter->handle,
				      port->logical_port_id,
				      H_PORT_CB2, H_PORT_CB2_ALL, cb2);
	if (hret != H_SUCCESS) {
		netdev_err(dev, "query_ehea_port failed\n");
		goto out_herr;
	}

	if (netif_msg_hw(port))
		ehea_dump(cb2, sizeof(*cb2), "net_device_stats");

	stats->multicast = cb2->rxmcp;
	stats->rx_errors = cb2->rxuerr;

out_herr:
	free_page((unsigned long)cb2);
resched:
	schedule_delayed_work(&port->stats_work,
			      round_jiffies_relative(msecs_to_jiffies(1000)));
}

static void ehea_refill_rq1(struct ehea_port_res *pr, int index, int nr_of_wqes)
{
	struct sk_buff **skb_arr_rq1 = pr->rq1_skba.arr;
	struct net_device *dev = pr->port->netdev;
	int max_index_mask = pr->rq1_skba.len - 1;
	int fill_wqes = pr->rq1_skba.os_skbs + nr_of_wqes;
	int adder = 0;
	int i;

	pr->rq1_skba.os_skbs = 0;

	if (unlikely(test_bit(__EHEA_STOP_XFER, &ehea_driver_flags))) {
		if (nr_of_wqes > 0)
			pr->rq1_skba.index = index;
		pr->rq1_skba.os_skbs = fill_wqes;
		return;
	}

	for (i = 0; i < fill_wqes; i++) {
		if (!skb_arr_rq1[index]) {
			skb_arr_rq1[index] = netdev_alloc_skb(dev,
							      EHEA_L_PKT_SIZE);
			if (!skb_arr_rq1[index]) {
				pr->rq1_skba.os_skbs = fill_wqes - i;
				break;
			}
		}
		index--;
		index &= max_index_mask;
		adder++;
	}

	if (adder == 0)
		return;

	/* Ring doorbell */
	ehea_update_rq1a(pr->qp, adder);
}

static void ehea_init_fill_rq1(struct ehea_port_res *pr, int nr_rq1a)
{
	struct sk_buff **skb_arr_rq1 = pr->rq1_skba.arr;
	struct net_device *dev = pr->port->netdev;
	int i;

	if (nr_rq1a > pr->rq1_skba.len) {
		netdev_err(dev, "NR_RQ1A bigger than skb array len\n");
		return;
	}

	for (i = 0; i < nr_rq1a; i++) {
		skb_arr_rq1[i] = netdev_alloc_skb(dev, EHEA_L_PKT_SIZE);
		if (!skb_arr_rq1[i])
			break;
	}
	/* Ring doorbell */
	ehea_update_rq1a(pr->qp, i - 1);
}

static int ehea_refill_rq_def(struct ehea_port_res *pr,
			      struct ehea_q_skb_arr *q_skba, int rq_nr,
			      int num_wqes, int wqe_type, int packet_size)
{
	struct net_device *dev = pr->port->netdev;
	struct ehea_qp *qp = pr->qp;
	struct sk_buff **skb_arr = q_skba->arr;
	struct ehea_rwqe *rwqe;
	int i, index, max_index_mask, fill_wqes;
	int adder = 0;
	int ret = 0;

	fill_wqes = q_skba->os_skbs + num_wqes;
	q_skba->os_skbs = 0;

	if (unlikely(test_bit(__EHEA_STOP_XFER, &ehea_driver_flags))) {
		q_skba->os_skbs = fill_wqes;
		return ret;
	}

	index = q_skba->index;
	max_index_mask = q_skba->len - 1;
	for (i = 0; i < fill_wqes; i++) {
		u64 tmp_addr;
		struct sk_buff *skb;

		skb = netdev_alloc_skb_ip_align(dev, packet_size);
		if (!skb) {
			q_skba->os_skbs = fill_wqes - i;
			if (q_skba->os_skbs == q_skba->len - 2) {
				netdev_info(pr->port->netdev,
					    "rq%i ran dry - no mem for skb\n",
					    rq_nr);
				ret = -ENOMEM;
			}
			break;
		}

		skb_arr[index] = skb;
		tmp_addr = ehea_map_vaddr(skb->data);
		if (tmp_addr == -1) {
			dev_consume_skb_any(skb);
			q_skba->os_skbs = fill_wqes - i;
			ret = 0;
			break;
		}

		rwqe = ehea_get_next_rwqe(qp, rq_nr);
		rwqe->wr_id = EHEA_BMASK_SET(EHEA_WR_ID_TYPE, wqe_type)
			    | EHEA_BMASK_SET(EHEA_WR_ID_INDEX, index);
		rwqe->sg_list[0].l_key = pr->recv_mr.lkey;
		rwqe->sg_list[0].vaddr = tmp_addr;
		rwqe->sg_list[0].len = packet_size;
		rwqe->data_segments = 1;

		index++;
		index &= max_index_mask;
		adder++;
	}

	q_skba->index = index;
	if (adder == 0)
		goto out;

	/* Ring doorbell */
	iosync();
	if (rq_nr == 2)
		ehea_update_rq2a(pr->qp, adder);
	else
		ehea_update_rq3a(pr->qp, adder);
out:
	return ret;
}


static int ehea_refill_rq2(struct ehea_port_res *pr, int nr_of_wqes)
{
	return ehea_refill_rq_def(pr, &pr->rq2_skba, 2,
				  nr_of_wqes, EHEA_RWQE2_TYPE,
				  EHEA_RQ2_PKT_SIZE);
}


static int ehea_refill_rq3(struct ehea_port_res *pr, int nr_of_wqes)
{
	return ehea_refill_rq_def(pr, &pr->rq3_skba, 3,
				  nr_of_wqes, EHEA_RWQE3_TYPE,
				  EHEA_MAX_PACKET_SIZE);
}

static inline int ehea_check_cqe(struct ehea_cqe *cqe, int *rq_num)
{
	*rq_num = (cqe->type & EHEA_CQE_TYPE_RQ) >> 5;
	if ((cqe->status & EHEA_CQE_STAT_ERR_MASK) == 0)
		return 0;
	if (((cqe->status & EHEA_CQE_STAT_ERR_TCP) != 0) &&
	    (cqe->header_length == 0))
		return 0;
	return -EINVAL;
}

static inline void ehea_fill_skb(struct net_device *dev,
				 struct sk_buff *skb, struct ehea_cqe *cqe,
				 struct ehea_port_res *pr)
{
	int length = cqe->num_bytes_transfered - 4;	/*remove CRC */

	skb_put(skb, length);
	skb->protocol = eth_type_trans(skb, dev);

	/* The packet was not an IPV4 packet so a complemented checksum was
	   calculated. The value is found in the Internet Checksum field. */
	if (cqe->status & EHEA_CQE_BLIND_CKSUM) {
		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum = csum_unfold(~cqe->inet_checksum_value);
	} else
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	skb_record_rx_queue(skb, pr - &pr->port->port_res[0]);
}

static inline struct sk_buff *get_skb_by_index(struct sk_buff **skb_array,
					       int arr_len,
					       struct ehea_cqe *cqe)
{
	int skb_index = EHEA_BMASK_GET(EHEA_WR_ID_INDEX, cqe->wr_id);
	struct sk_buff *skb;
	void *pref;
	int x;

	x = skb_index + 1;
	x &= (arr_len - 1);

	pref = skb_array[x];
	if (pref) {
		prefetchw(pref);
		prefetchw(pref + EHEA_CACHE_LINE);

		pref = (skb_array[x]->data);
		prefetch(pref);
		prefetch(pref + EHEA_CACHE_LINE);
		prefetch(pref + EHEA_CACHE_LINE * 2);
		prefetch(pref + EHEA_CACHE_LINE * 3);
	}

	skb = skb_array[skb_index];
	skb_array[skb_index] = NULL;
	return skb;
}

static inline struct sk_buff *get_skb_by_index_ll(struct sk_buff **skb_array,
						  int arr_len, int wqe_index)
{
	struct sk_buff *skb;
	void *pref;
	int x;

	x = wqe_index + 1;
	x &= (arr_len - 1);

	pref = skb_array[x];
	if (pref) {
		prefetchw(pref);
		prefetchw(pref + EHEA_CACHE_LINE);

		pref = (skb_array[x]->data);
		prefetchw(pref);
		prefetchw(pref + EHEA_CACHE_LINE);
	}

	skb = skb_array[wqe_index];
	skb_array[wqe_index] = NULL;
	return skb;
}

static int ehea_treat_poll_error(struct ehea_port_res *pr, int rq,
				 struct ehea_cqe *cqe, int *processed_rq2,
				 int *processed_rq3)
{
	struct sk_buff *skb;

	if (cqe->status & EHEA_CQE_STAT_ERR_TCP)
		pr->p_stats.err_tcp_cksum++;
	if (cqe->status & EHEA_CQE_STAT_ERR_IP)
		pr->p_stats.err_ip_cksum++;
	if (cqe->status & EHEA_CQE_STAT_ERR_CRC)
		pr->p_stats.err_frame_crc++;

	if (rq == 2) {
		*processed_rq2 += 1;
		skb = get_skb_by_index(pr->rq2_skba.arr, pr->rq2_skba.len, cqe);
		dev_kfree_skb(skb);
	} else if (rq == 3) {
		*processed_rq3 += 1;
		skb = get_skb_by_index(pr->rq3_skba.arr, pr->rq3_skba.len, cqe);
		dev_kfree_skb(skb);
	}

	if (cqe->status & EHEA_CQE_STAT_FAT_ERR_MASK) {
		if (netif_msg_rx_err(pr->port)) {
			pr_err("Critical receive error for QP %d. Resetting port.\n",
			       pr->qp->init_attr.qp_nr);
			ehea_dump(cqe, sizeof(*cqe), "CQE");
		}
		ehea_schedule_port_reset(pr->port);
		return 1;
	}

	return 0;
}

static int ehea_proc_rwqes(struct net_device *dev,
			   struct ehea_port_res *pr,
			   int budget)
{
	struct ehea_port *port = pr->port;
	struct ehea_qp *qp = pr->qp;
	struct ehea_cqe *cqe;
	struct sk_buff *skb;
	struct sk_buff **skb_arr_rq1 = pr->rq1_skba.arr;
	struct sk_buff **skb_arr_rq2 = pr->rq2_skba.arr;
	struct sk_buff **skb_arr_rq3 = pr->rq3_skba.arr;
	int skb_arr_rq1_len = pr->rq1_skba.len;
	int skb_arr_rq2_len = pr->rq2_skba.len;
	int skb_arr_rq3_len = pr->rq3_skba.len;
	int processed, processed_rq1, processed_rq2, processed_rq3;
	u64 processed_bytes = 0;
	int wqe_index, last_wqe_index, rq, port_reset;

	processed = processed_rq1 = processed_rq2 = processed_rq3 = 0;
	last_wqe_index = 0;

	cqe = ehea_poll_rq1(qp, &wqe_index);
	while ((processed < budget) && cqe) {
		ehea_inc_rq1(qp);
		processed_rq1++;
		processed++;
		if (netif_msg_rx_status(port))
			ehea_dump(cqe, sizeof(*cqe), "CQE");

		last_wqe_index = wqe_index;
		rmb();
		if (!ehea_check_cqe(cqe, &rq)) {
			if (rq == 1) {
				/* LL RQ1 */
				skb = get_skb_by_index_ll(skb_arr_rq1,
							  skb_arr_rq1_len,
							  wqe_index);
				if (unlikely(!skb)) {
					netif_info(port, rx_err, dev,
						  "LL rq1: skb=NULL\n");

					skb = netdev_alloc_skb(dev,
							       EHEA_L_PKT_SIZE);
					if (!skb)
						break;
				}
				skb_copy_to_linear_data(skb, ((char *)cqe) + 64,
						 cqe->num_bytes_transfered - 4);
				ehea_fill_skb(dev, skb, cqe, pr);
			} else if (rq == 2) {
				/* RQ2 */
				skb = get_skb_by_index(skb_arr_rq2,
						       skb_arr_rq2_len, cqe);
				if (unlikely(!skb)) {
					netif_err(port, rx_err, dev,
						  "rq2: skb=NULL\n");
					break;
				}
				ehea_fill_skb(dev, skb, cqe, pr);
				processed_rq2++;
			} else {
				/* RQ3 */
				skb = get_skb_by_index(skb_arr_rq3,
						       skb_arr_rq3_len, cqe);
				if (unlikely(!skb)) {
					netif_err(port, rx_err, dev,
						  "rq3: skb=NULL\n");
					break;
				}
				ehea_fill_skb(dev, skb, cqe, pr);
				processed_rq3++;
			}

			processed_bytes += skb->len;

			if (cqe->status & EHEA_CQE_VLAN_TAG_XTRACT)
				__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
						       cqe->vlan_tag);

			napi_gro_receive(&pr->napi, skb);
		} else {
			pr->p_stats.poll_receive_errors++;
			port_reset = ehea_treat_poll_error(pr, rq, cqe,
							   &processed_rq2,
							   &processed_rq3);
			if (port_reset)
				break;
		}
		cqe = ehea_poll_rq1(qp, &wqe_index);
	}

	pr->rx_packets += processed;
	pr->rx_bytes += processed_bytes;

	ehea_refill_rq1(pr, last_wqe_index, processed_rq1);
	ehea_refill_rq2(pr, processed_rq2);
	ehea_refill_rq3(pr, processed_rq3);

	return processed;
}

#define SWQE_RESTART_CHECK 0xdeadbeaff00d0000ull

static void reset_sq_restart_flag(struct ehea_port *port)
{
	int i;

	for (i = 0; i < port->num_def_qps; i++) {
		struct ehea_port_res *pr = &port->port_res[i];
		pr->sq_restart_flag = 0;
	}
	wake_up(&port->restart_wq);
}

static void check_sqs(struct ehea_port *port)
{
	struct ehea_swqe *swqe;
	int swqe_index;
	int i;

	for (i = 0; i < port->num_def_qps; i++) {
		struct ehea_port_res *pr = &port->port_res[i];
		int ret;
		swqe = ehea_get_swqe(pr->qp, &swqe_index);
		memset(swqe, 0, SWQE_HEADER_SIZE);
		atomic_dec(&pr->swqe_avail);

		swqe->tx_control |= EHEA_SWQE_PURGE;
		swqe->wr_id = SWQE_RESTART_CHECK;
		swqe->tx_control |= EHEA_SWQE_SIGNALLED_COMPLETION;
		swqe->tx_control |= EHEA_SWQE_IMM_DATA_PRESENT;
		swqe->immediate_data_length = 80;

		ehea_post_swqe(pr->qp, swqe);

		ret = wait_event_timeout(port->restart_wq,
					 pr->sq_restart_flag == 0,
					 msecs_to_jiffies(100));

		if (!ret) {
			pr_err("HW/SW queues out of sync\n");
			ehea_schedule_port_reset(pr->port);
			return;
		}
	}
}


static struct ehea_cqe *ehea_proc_cqes(struct ehea_port_res *pr, int my_quota)
{
	struct sk_buff *skb;
	struct ehea_cq *send_cq = pr->send_cq;
	struct ehea_cqe *cqe;
	int quota = my_quota;
	int cqe_counter = 0;
	int swqe_av = 0;
	int index;
	struct netdev_queue *txq = netdev_get_tx_queue(pr->port->netdev,
						pr - &pr->port->port_res[0]);

	cqe = ehea_poll_cq(send_cq);
	while (cqe && (quota > 0)) {
		ehea_inc_cq(send_cq);

		cqe_counter++;
		rmb();

		if (cqe->wr_id == SWQE_RESTART_CHECK) {
			pr->sq_restart_flag = 1;
			swqe_av++;
			break;
		}

		if (cqe->status & EHEA_CQE_STAT_ERR_MASK) {
			pr_err("Bad send completion status=0x%04X\n",
			       cqe->status);

			if (netif_msg_tx_err(pr->port))
				ehea_dump(cqe, sizeof(*cqe), "Send CQE");

			if (cqe->status & EHEA_CQE_STAT_RESET_MASK) {
				pr_err("Resetting port\n");
				ehea_schedule_port_reset(pr->port);
				break;
			}
		}

		if (netif_msg_tx_done(pr->port))
			ehea_dump(cqe, sizeof(*cqe), "CQE");

		if (likely(EHEA_BMASK_GET(EHEA_WR_ID_TYPE, cqe->wr_id)
			   == EHEA_SWQE2_TYPE)) {

			index = EHEA_BMASK_GET(EHEA_WR_ID_INDEX, cqe->wr_id);
			skb = pr->sq_skba.arr[index];
			dev_consume_skb_any(skb);
			pr->sq_skba.arr[index] = NULL;
		}

		swqe_av += EHEA_BMASK_GET(EHEA_WR_ID_REFILL, cqe->wr_id);
		quota--;

		cqe = ehea_poll_cq(send_cq);
	}

	ehea_update_feca(send_cq, cqe_counter);
	atomic_add(swqe_av, &pr->swqe_avail);

	if (unlikely(netif_tx_queue_stopped(txq) &&
		     (atomic_read(&pr->swqe_avail) >= pr->swqe_refill_th))) {
		__netif_tx_lock(txq, smp_processor_id());
		if (netif_tx_queue_stopped(txq) &&
		    (atomic_read(&pr->swqe_avail) >= pr->swqe_refill_th))
			netif_tx_wake_queue(txq);
		__netif_tx_unlock(txq);
	}

	wake_up(&pr->port->swqe_avail_wq);

	return cqe;
}

#define EHEA_POLL_MAX_CQES 65535

static int ehea_poll(struct napi_struct *napi, int budget)
{
	struct ehea_port_res *pr = container_of(napi, struct ehea_port_res,
						napi);
	struct net_device *dev = pr->port->netdev;
	struct ehea_cqe *cqe;
	struct ehea_cqe *cqe_skb = NULL;
	int wqe_index;
	int rx = 0;

	cqe_skb = ehea_proc_cqes(pr, EHEA_POLL_MAX_CQES);
	rx += ehea_proc_rwqes(dev, pr, budget - rx);

	while (rx != budget) {
		napi_complete(napi);
		ehea_reset_cq_ep(pr->recv_cq);
		ehea_reset_cq_ep(pr->send_cq);
		ehea_reset_cq_n1(pr->recv_cq);
		ehea_reset_cq_n1(pr->send_cq);
		rmb();
		cqe = ehea_poll_rq1(pr->qp, &wqe_index);
		cqe_skb = ehea_poll_cq(pr->send_cq);

		if (!cqe && !cqe_skb)
			return rx;

		if (!napi_reschedule(napi))
			return rx;

		cqe_skb = ehea_proc_cqes(pr, EHEA_POLL_MAX_CQES);
		rx += ehea_proc_rwqes(dev, pr, budget - rx);
	}

	return rx;
}

static irqreturn_t ehea_recv_irq_handler(int irq, void *param)
{
	struct ehea_port_res *pr = param;

	napi_schedule(&pr->napi);

	return IRQ_HANDLED;
}

static irqreturn_t ehea_qp_aff_irq_handler(int irq, void *param)
{
	struct ehea_port *port = param;
	struct ehea_eqe *eqe;
	struct ehea_qp *qp;
	u32 qp_token;
	u64 resource_type, aer, aerr;
	int reset_port = 0;

	eqe = ehea_poll_eq(port->qp_eq);

	while (eqe) {
		qp_token = EHEA_BMASK_GET(EHEA_EQE_QP_TOKEN, eqe->entry);
		pr_err("QP aff_err: entry=0x%llx, token=0x%x\n",
		       eqe->entry, qp_token);

		qp = port->port_res[qp_token].qp;

		resource_type = ehea_error_data(port->adapter, qp->fw_handle,
						&aer, &aerr);

		if (resource_type == EHEA_AER_RESTYPE_QP) {
			if ((aer & EHEA_AER_RESET_MASK) ||
			    (aerr & EHEA_AERR_RESET_MASK))
				 reset_port = 1;
		} else
			reset_port = 1;   /* Reset in case of CQ or EQ error */

		eqe = ehea_poll_eq(port->qp_eq);
	}

	if (reset_port) {
		pr_err("Resetting port\n");
		ehea_schedule_port_reset(port);
	}

	return IRQ_HANDLED;
}

static struct ehea_port *ehea_get_port(struct ehea_adapter *adapter,
				       int logical_port)
{
	int i;

	for (i = 0; i < EHEA_MAX_PORTS; i++)
		if (adapter->port[i])
			if (adapter->port[i]->logical_port_id == logical_port)
				return adapter->port[i];
	return NULL;
}

int ehea_sense_port_attr(struct ehea_port *port)
{
	int ret;
	u64 hret;
	struct hcp_ehea_port_cb0 *cb0;

	/* may be called via ehea_neq_tasklet() */
	cb0 = (void *)get_zeroed_page(GFP_ATOMIC);
	if (!cb0) {
		pr_err("no mem for cb0\n");
		ret = -ENOMEM;
		goto out;
	}

	hret = ehea_h_query_ehea_port(port->adapter->handle,
				      port->logical_port_id, H_PORT_CB0,
				      EHEA_BMASK_SET(H_PORT_CB0_ALL, 0xFFFF),
				      cb0);
	if (hret != H_SUCCESS) {
		ret = -EIO;
		goto out_free;
	}

	/* MAC address */
	port->mac_addr = cb0->port_mac_addr << 16;

	if (!is_valid_ether_addr((u8 *)&port->mac_addr)) {
		ret = -EADDRNOTAVAIL;
		goto out_free;
	}

	/* Port speed */
	switch (cb0->port_speed) {
	case H_SPEED_10M_H:
		port->port_speed = EHEA_SPEED_10M;
		port->full_duplex = 0;
		break;
	case H_SPEED_10M_F:
		port->port_speed = EHEA_SPEED_10M;
		port->full_duplex = 1;
		break;
	case H_SPEED_100M_H:
		port->port_speed = EHEA_SPEED_100M;
		port->full_duplex = 0;
		break;
	case H_SPEED_100M_F:
		port->port_speed = EHEA_SPEED_100M;
		port->full_duplex = 1;
		break;
	case H_SPEED_1G_F:
		port->port_speed = EHEA_SPEED_1G;
		port->full_duplex = 1;
		break;
	case H_SPEED_10G_F:
		port->port_speed = EHEA_SPEED_10G;
		port->full_duplex = 1;
		break;
	default:
		port->port_speed = 0;
		port->full_duplex = 0;
		break;
	}

	port->autoneg = 1;
	port->num_mcs = cb0->num_default_qps;

	/* Number of default QPs */
	if (use_mcs)
		port->num_def_qps = cb0->num_default_qps;
	else
		port->num_def_qps = 1;

	if (!port->num_def_qps) {
		ret = -EINVAL;
		goto out_free;
	}

	ret = 0;
out_free:
	if (ret || netif_msg_probe(port))
		ehea_dump(cb0, sizeof(*cb0), "ehea_sense_port_attr");
	free_page((unsigned long)cb0);
out:
	return ret;
}

int ehea_set_portspeed(struct ehea_port *port, u32 port_speed)
{
	struct hcp_ehea_port_cb4 *cb4;
	u64 hret;
	int ret = 0;

	cb4 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb4) {
		pr_err("no mem for cb4\n");
		ret = -ENOMEM;
		goto out;
	}

	cb4->port_speed = port_speed;

	netif_carrier_off(port->netdev);

	hret = ehea_h_modify_ehea_port(port->adapter->handle,
				       port->logical_port_id,
				       H_PORT_CB4, H_PORT_CB4_SPEED, cb4);
	if (hret == H_SUCCESS) {
		port->autoneg = port_speed == EHEA_SPEED_AUTONEG ? 1 : 0;

		hret = ehea_h_query_ehea_port(port->adapter->handle,
					      port->logical_port_id,
					      H_PORT_CB4, H_PORT_CB4_SPEED,
					      cb4);
		if (hret == H_SUCCESS) {
			switch (cb4->port_speed) {
			case H_SPEED_10M_H:
				port->port_speed = EHEA_SPEED_10M;
				port->full_duplex = 0;
				break;
			case H_SPEED_10M_F:
				port->port_speed = EHEA_SPEED_10M;
				port->full_duplex = 1;
				break;
			case H_SPEED_100M_H:
				port->port_speed = EHEA_SPEED_100M;
				port->full_duplex = 0;
				break;
			case H_SPEED_100M_F:
				port->port_speed = EHEA_SPEED_100M;
				port->full_duplex = 1;
				break;
			case H_SPEED_1G_F:
				port->port_speed = EHEA_SPEED_1G;
				port->full_duplex = 1;
				break;
			case H_SPEED_10G_F:
				port->port_speed = EHEA_SPEED_10G;
				port->full_duplex = 1;
				break;
			default:
				port->port_speed = 0;
				port->full_duplex = 0;
				break;
			}
		} else {
			pr_err("Failed sensing port speed\n");
			ret = -EIO;
		}
	} else {
		if (hret == H_AUTHORITY) {
			pr_info("Hypervisor denied setting port speed\n");
			ret = -EPERM;
		} else {
			ret = -EIO;
			pr_err("Failed setting port speed\n");
		}
	}
	if (!prop_carrier_state || (port->phy_link == EHEA_PHY_LINK_UP))
		netif_carrier_on(port->netdev);

	free_page((unsigned long)cb4);
out:
	return ret;
}

static void ehea_parse_eqe(struct ehea_adapter *adapter, u64 eqe)
{
	int ret;
	u8 ec;
	u8 portnum;
	struct ehea_port *port;
	struct net_device *dev;

	ec = EHEA_BMASK_GET(NEQE_EVENT_CODE, eqe);
	portnum = EHEA_BMASK_GET(NEQE_PORTNUM, eqe);
	port = ehea_get_port(adapter, portnum);
	if (!port) {
		netdev_err(NULL, "unknown portnum %x\n", portnum);
		return;
	}
	dev = port->netdev;

	switch (ec) {
	case EHEA_EC_PORTSTATE_CHG:	/* port state change */

		if (EHEA_BMASK_GET(NEQE_PORT_UP, eqe)) {
			if (!netif_carrier_ok(dev)) {
				ret = ehea_sense_port_attr(port);
				if (ret) {
					netdev_err(dev, "failed resensing port attributes\n");
					break;
				}

				netif_info(port, link, dev,
					   "Logical port up: %dMbps %s Duplex\n",
					   port->port_speed,
					   port->full_duplex == 1 ?
					   "Full" : "Half");

				netif_carrier_on(dev);
				netif_wake_queue(dev);
			}
		} else
			if (netif_carrier_ok(dev)) {
				netif_info(port, link, dev,
					   "Logical port down\n");
				netif_carrier_off(dev);
				netif_tx_disable(dev);
			}

		if (EHEA_BMASK_GET(NEQE_EXTSWITCH_PORT_UP, eqe)) {
			port->phy_link = EHEA_PHY_LINK_UP;
			netif_info(port, link, dev,
				   "Physical port up\n");
			if (prop_carrier_state)
				netif_carrier_on(dev);
		} else {
			port->phy_link = EHEA_PHY_LINK_DOWN;
			netif_info(port, link, dev,
				   "Physical port down\n");
			if (prop_carrier_state)
				netif_carrier_off(dev);
		}

		if (EHEA_BMASK_GET(NEQE_EXTSWITCH_PRIMARY, eqe))
			netdev_info(dev,
				    "External switch port is primary port\n");
		else
			netdev_info(dev,
				    "External switch port is backup port\n");

		break;
	case EHEA_EC_ADAPTER_MALFUNC:
		netdev_err(dev, "Adapter malfunction\n");
		break;
	case EHEA_EC_PORT_MALFUNC:
		netdev_info(dev, "Port malfunction\n");
		netif_carrier_off(dev);
		netif_tx_disable(dev);
		break;
	default:
		netdev_err(dev, "unknown event code %x, eqe=0x%llX\n", ec, eqe);
		break;
	}
}

static void ehea_neq_tasklet(struct tasklet_struct *t)
{
	struct ehea_adapter *adapter = from_tasklet(adapter, t, neq_tasklet);
	struct ehea_eqe *eqe;
	u64 event_mask;

	eqe = ehea_poll_eq(adapter->neq);
	pr_debug("eqe=%p\n", eqe);

	while (eqe) {
		pr_debug("*eqe=%lx\n", (unsigned long) eqe->entry);
		ehea_parse_eqe(adapter, eqe->entry);
		eqe = ehea_poll_eq(adapter->neq);
		pr_debug("next eqe=%p\n", eqe);
	}

	event_mask = EHEA_BMASK_SET(NELR_PORTSTATE_CHG, 1)
		   | EHEA_BMASK_SET(NELR_ADAPTER_MALFUNC, 1)
		   | EHEA_BMASK_SET(NELR_PORT_MALFUNC, 1);

	ehea_h_reset_events(adapter->handle,
			    adapter->neq->fw_handle, event_mask);
}

static irqreturn_t ehea_interrupt_neq(int irq, void *param)
{
	struct ehea_adapter *adapter = param;
	tasklet_hi_schedule(&adapter->neq_tasklet);
	return IRQ_HANDLED;
}


static int ehea_fill_port_res(struct ehea_port_res *pr)
{
	int ret;
	struct ehea_qp_init_attr *init_attr = &pr->qp->init_attr;

	ehea_init_fill_rq1(pr, pr->rq1_skba.len);

	ret = ehea_refill_rq2(pr, init_attr->act_nr_rwqes_rq2 - 1);

	ret |= ehea_refill_rq3(pr, init_attr->act_nr_rwqes_rq3 - 1);

	return ret;
}

static int ehea_reg_interrupts(struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_port_res *pr;
	int i, ret;


	snprintf(port->int_aff_name, EHEA_IRQ_NAME_SIZE - 1, "%s-aff",
		 dev->name);

	ret = ibmebus_request_irq(port->qp_eq->attr.ist1,
				  ehea_qp_aff_irq_handler,
				  0, port->int_aff_name, port);
	if (ret) {
		netdev_err(dev, "failed registering irq for qp_aff_irq_handler:ist=%X\n",
			   port->qp_eq->attr.ist1);
		goto out_free_qpeq;
	}

	netif_info(port, ifup, dev,
		   "irq_handle 0x%X for function qp_aff_irq_handler registered\n",
		   port->qp_eq->attr.ist1);


	for (i = 0; i < port->num_def_qps; i++) {
		pr = &port->port_res[i];
		snprintf(pr->int_send_name, EHEA_IRQ_NAME_SIZE - 1,
			 "%s-queue%d", dev->name, i);
		ret = ibmebus_request_irq(pr->eq->attr.ist1,
					  ehea_recv_irq_handler,
					  0, pr->int_send_name, pr);
		if (ret) {
			netdev_err(dev, "failed registering irq for ehea_queue port_res_nr:%d, ist=%X\n",
				   i, pr->eq->attr.ist1);
			goto out_free_req;
		}
		netif_info(port, ifup, dev,
			   "irq_handle 0x%X for function ehea_queue_int %d registered\n",
			   pr->eq->attr.ist1, i);
	}
out:
	return ret;


out_free_req:
	while (--i >= 0) {
		u32 ist = port->port_res[i].eq->attr.ist1;
		ibmebus_free_irq(ist, &port->port_res[i]);
	}

out_free_qpeq:
	ibmebus_free_irq(port->qp_eq->attr.ist1, port);
	i = port->num_def_qps;

	goto out;

}

static void ehea_free_interrupts(struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_port_res *pr;
	int i;

	/* send */

	for (i = 0; i < port->num_def_qps; i++) {
		pr = &port->port_res[i];
		ibmebus_free_irq(pr->eq->attr.ist1, pr);
		netif_info(port, intr, dev,
			   "free send irq for res %d with handle 0x%X\n",
			   i, pr->eq->attr.ist1);
	}

	/* associated events */
	ibmebus_free_irq(port->qp_eq->attr.ist1, port);
	netif_info(port, intr, dev,
		   "associated event interrupt for handle 0x%X freed\n",
		   port->qp_eq->attr.ist1);
}

static int ehea_configure_port(struct ehea_port *port)
{
	int ret, i;
	u64 hret, mask;
	struct hcp_ehea_port_cb0 *cb0;

	ret = -ENOMEM;
	cb0 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb0)
		goto out;

	cb0->port_rc = EHEA_BMASK_SET(PXLY_RC_VALID, 1)
		     | EHEA_BMASK_SET(PXLY_RC_IP_CHKSUM, 1)
		     | EHEA_BMASK_SET(PXLY_RC_TCP_UDP_CHKSUM, 1)
		     | EHEA_BMASK_SET(PXLY_RC_VLAN_XTRACT, 1)
		     | EHEA_BMASK_SET(PXLY_RC_VLAN_TAG_FILTER,
				      PXLY_RC_VLAN_FILTER)
		     | EHEA_BMASK_SET(PXLY_RC_JUMBO_FRAME, 1);

	for (i = 0; i < port->num_mcs; i++)
		if (use_mcs)
			cb0->default_qpn_arr[i] =
				port->port_res[i].qp->init_attr.qp_nr;
		else
			cb0->default_qpn_arr[i] =
				port->port_res[0].qp->init_attr.qp_nr;

	if (netif_msg_ifup(port))
		ehea_dump(cb0, sizeof(*cb0), "ehea_configure_port");

	mask = EHEA_BMASK_SET(H_PORT_CB0_PRC, 1)
	     | EHEA_BMASK_SET(H_PORT_CB0_DEFQPNARRAY, 1);

	hret = ehea_h_modify_ehea_port(port->adapter->handle,
				       port->logical_port_id,
				       H_PORT_CB0, mask, cb0);
	ret = -EIO;
	if (hret != H_SUCCESS)
		goto out_free;

	ret = 0;

out_free:
	free_page((unsigned long)cb0);
out:
	return ret;
}

static int ehea_gen_smrs(struct ehea_port_res *pr)
{
	int ret;
	struct ehea_adapter *adapter = pr->port->adapter;

	ret = ehea_gen_smr(adapter, &adapter->mr, &pr->send_mr);
	if (ret)
		goto out;

	ret = ehea_gen_smr(adapter, &adapter->mr, &pr->recv_mr);
	if (ret)
		goto out_free;

	return 0;

out_free:
	ehea_rem_mr(&pr->send_mr);
out:
	pr_err("Generating SMRS failed\n");
	return -EIO;
}

static int ehea_rem_smrs(struct ehea_port_res *pr)
{
	if ((ehea_rem_mr(&pr->send_mr)) ||
	    (ehea_rem_mr(&pr->recv_mr)))
		return -EIO;
	else
		return 0;
}

static int ehea_init_q_skba(struct ehea_q_skb_arr *q_skba, int max_q_entries)
{
	int arr_size = sizeof(void *) * max_q_entries;

	q_skba->arr = vzalloc(arr_size);
	if (!q_skba->arr)
		return -ENOMEM;

	q_skba->len = max_q_entries;
	q_skba->index = 0;
	q_skba->os_skbs = 0;

	return 0;
}

static int ehea_init_port_res(struct ehea_port *port, struct ehea_port_res *pr,
			      struct port_res_cfg *pr_cfg, int queue_token)
{
	struct ehea_adapter *adapter = port->adapter;
	enum ehea_eq_type eq_type = EHEA_EQ;
	struct ehea_qp_init_attr *init_attr = NULL;
	int ret = -EIO;
	u64 tx_bytes, rx_bytes, tx_packets, rx_packets;

	tx_bytes = pr->tx_bytes;
	tx_packets = pr->tx_packets;
	rx_bytes = pr->rx_bytes;
	rx_packets = pr->rx_packets;

	memset(pr, 0, sizeof(struct ehea_port_res));

	pr->tx_bytes = tx_bytes;
	pr->tx_packets = tx_packets;
	pr->rx_bytes = rx_bytes;
	pr->rx_packets = rx_packets;

	pr->port = port;

	pr->eq = ehea_create_eq(adapter, eq_type, EHEA_MAX_ENTRIES_EQ, 0);
	if (!pr->eq) {
		pr_err("create_eq failed (eq)\n");
		goto out_free;
	}

	pr->recv_cq = ehea_create_cq(adapter, pr_cfg->max_entries_rcq,
				     pr->eq->fw_handle,
				     port->logical_port_id);
	if (!pr->recv_cq) {
		pr_err("create_cq failed (cq_recv)\n");
		goto out_free;
	}

	pr->send_cq = ehea_create_cq(adapter, pr_cfg->max_entries_scq,
				     pr->eq->fw_handle,
				     port->logical_port_id);
	if (!pr->send_cq) {
		pr_err("create_cq failed (cq_send)\n");
		goto out_free;
	}

	if (netif_msg_ifup(port))
		pr_info("Send CQ: act_nr_cqes=%d, Recv CQ: act_nr_cqes=%d\n",
			pr->send_cq->attr.act_nr_of_cqes,
			pr->recv_cq->attr.act_nr_of_cqes);

	init_attr = kzalloc(sizeof(*init_attr), GFP_KERNEL);
	if (!init_attr) {
		ret = -ENOMEM;
		pr_err("no mem for ehea_qp_init_attr\n");
		goto out_free;
	}

	init_attr->low_lat_rq1 = 1;
	init_attr->signalingtype = 1;	/* generate CQE if specified in WQE */
	init_attr->rq_count = 3;
	init_attr->qp_token = queue_token;
	init_attr->max_nr_send_wqes = pr_cfg->max_entries_sq;
	init_attr->max_nr_rwqes_rq1 = pr_cfg->max_entries_rq1;
	init_attr->max_nr_rwqes_rq2 = pr_cfg->max_entries_rq2;
	init_attr->max_nr_rwqes_rq3 = pr_cfg->max_entries_rq3;
	init_attr->wqe_size_enc_sq = EHEA_SG_SQ;
	init_attr->wqe_size_enc_rq1 = EHEA_SG_RQ1;
	init_attr->wqe_size_enc_rq2 = EHEA_SG_RQ2;
	init_attr->wqe_size_enc_rq3 = EHEA_SG_RQ3;
	init_attr->rq2_threshold = EHEA_RQ2_THRESHOLD;
	init_attr->rq3_threshold = EHEA_RQ3_THRESHOLD;
	init_attr->port_nr = port->logical_port_id;
	init_attr->send_cq_handle = pr->send_cq->fw_handle;
	init_attr->recv_cq_handle = pr->recv_cq->fw_handle;
	init_attr->aff_eq_handle = port->qp_eq->fw_handle;

	pr->qp = ehea_create_qp(adapter, adapter->pd, init_attr);
	if (!pr->qp) {
		pr_err("create_qp failed\n");
		ret = -EIO;
		goto out_free;
	}

	if (netif_msg_ifup(port))
		pr_info("QP: qp_nr=%d\n act_nr_snd_wqe=%d\n nr_rwqe_rq1=%d\n nr_rwqe_rq2=%d\n nr_rwqe_rq3=%d\n",
			init_attr->qp_nr,
			init_attr->act_nr_send_wqes,
			init_attr->act_nr_rwqes_rq1,
			init_attr->act_nr_rwqes_rq2,
			init_attr->act_nr_rwqes_rq3);

	pr->sq_skba_size = init_attr->act_nr_send_wqes + 1;

	ret = ehea_init_q_skba(&pr->sq_skba, pr->sq_skba_size);
	ret |= ehea_init_q_skba(&pr->rq1_skba, init_attr->act_nr_rwqes_rq1 + 1);
	ret |= ehea_init_q_skba(&pr->rq2_skba, init_attr->act_nr_rwqes_rq2 + 1);
	ret |= ehea_init_q_skba(&pr->rq3_skba, init_attr->act_nr_rwqes_rq3 + 1);
	if (ret)
		goto out_free;

	pr->swqe_refill_th = init_attr->act_nr_send_wqes / 10;
	if (ehea_gen_smrs(pr) != 0) {
		ret = -EIO;
		goto out_free;
	}

	atomic_set(&pr->swqe_avail, init_attr->act_nr_send_wqes - 1);

	kfree(init_attr);

	netif_napi_add(pr->port->netdev, &pr->napi, ehea_poll, 64);

	ret = 0;
	goto out;

out_free:
	kfree(init_attr);
	vfree(pr->sq_skba.arr);
	vfree(pr->rq1_skba.arr);
	vfree(pr->rq2_skba.arr);
	vfree(pr->rq3_skba.arr);
	ehea_destroy_qp(pr->qp);
	ehea_destroy_cq(pr->send_cq);
	ehea_destroy_cq(pr->recv_cq);
	ehea_destroy_eq(pr->eq);
out:
	return ret;
}

static int ehea_clean_portres(struct ehea_port *port, struct ehea_port_res *pr)
{
	int ret, i;

	if (pr->qp)
		netif_napi_del(&pr->napi);

	ret = ehea_destroy_qp(pr->qp);

	if (!ret) {
		ehea_destroy_cq(pr->send_cq);
		ehea_destroy_cq(pr->recv_cq);
		ehea_destroy_eq(pr->eq);

		for (i = 0; i < pr->rq1_skba.len; i++)
			dev_kfree_skb(pr->rq1_skba.arr[i]);

		for (i = 0; i < pr->rq2_skba.len; i++)
			dev_kfree_skb(pr->rq2_skba.arr[i]);

		for (i = 0; i < pr->rq3_skba.len; i++)
			dev_kfree_skb(pr->rq3_skba.arr[i]);

		for (i = 0; i < pr->sq_skba.len; i++)
			dev_kfree_skb(pr->sq_skba.arr[i]);

		vfree(pr->rq1_skba.arr);
		vfree(pr->rq2_skba.arr);
		vfree(pr->rq3_skba.arr);
		vfree(pr->sq_skba.arr);
		ret = ehea_rem_smrs(pr);
	}
	return ret;
}

static void write_swqe2_immediate(struct sk_buff *skb, struct ehea_swqe *swqe,
				  u32 lkey)
{
	int skb_data_size = skb_headlen(skb);
	u8 *imm_data = &swqe->u.immdata_desc.immediate_data[0];
	struct ehea_vsgentry *sg1entry = &swqe->u.immdata_desc.sg_entry;
	unsigned int immediate_len = SWQE2_MAX_IMM;

	swqe->descriptors = 0;

	if (skb_is_gso(skb)) {
		swqe->tx_control |= EHEA_SWQE_TSO;
		swqe->mss = skb_shinfo(skb)->gso_size;
		/*
		 * For TSO packets we only copy the headers into the
		 * immediate area.
		 */
		immediate_len = ETH_HLEN + ip_hdrlen(skb) + tcp_hdrlen(skb);
	}

	if (skb_is_gso(skb) || skb_data_size >= SWQE2_MAX_IMM) {
		skb_copy_from_linear_data(skb, imm_data, immediate_len);
		swqe->immediate_data_length = immediate_len;

		if (skb_data_size > immediate_len) {
			sg1entry->l_key = lkey;
			sg1entry->len = skb_data_size - immediate_len;
			sg1entry->vaddr =
				ehea_map_vaddr(skb->data + immediate_len);
			swqe->descriptors++;
		}
	} else {
		skb_copy_from_linear_data(skb, imm_data, skb_data_size);
		swqe->immediate_data_length = skb_data_size;
	}
}

static inline void write_swqe2_data(struct sk_buff *skb, struct net_device *dev,
				    struct ehea_swqe *swqe, u32 lkey)
{
	struct ehea_vsgentry *sg_list, *sg1entry, *sgentry;
	skb_frag_t *frag;
	int nfrags, sg1entry_contains_frag_data, i;

	nfrags = skb_shinfo(skb)->nr_frags;
	sg1entry = &swqe->u.immdata_desc.sg_entry;
	sg_list = (struct ehea_vsgentry *)&swqe->u.immdata_desc.sg_list;
	sg1entry_contains_frag_data = 0;

	write_swqe2_immediate(skb, swqe, lkey);

	/* write descriptors */
	if (nfrags > 0) {
		if (swqe->descriptors == 0) {
			/* sg1entry not yet used */
			frag = &skb_shinfo(skb)->frags[0];

			/* copy sg1entry data */
			sg1entry->l_key = lkey;
			sg1entry->len = skb_frag_size(frag);
			sg1entry->vaddr =
				ehea_map_vaddr(skb_frag_address(frag));
			swqe->descriptors++;
			sg1entry_contains_frag_data = 1;
		}

		for (i = sg1entry_contains_frag_data; i < nfrags; i++) {

			frag = &skb_shinfo(skb)->frags[i];
			sgentry = &sg_list[i - sg1entry_contains_frag_data];

			sgentry->l_key = lkey;
			sgentry->len = skb_frag_size(frag);
			sgentry->vaddr = ehea_map_vaddr(skb_frag_address(frag));
			swqe->descriptors++;
		}
	}
}

static int ehea_broadcast_reg_helper(struct ehea_port *port, u32 hcallid)
{
	int ret = 0;
	u64 hret;
	u8 reg_type;

	/* De/Register untagged packets */
	reg_type = EHEA_BCMC_BROADCAST | EHEA_BCMC_UNTAGGED;
	hret = ehea_h_reg_dereg_bcmc(port->adapter->handle,
				     port->logical_port_id,
				     reg_type, port->mac_addr, 0, hcallid);
	if (hret != H_SUCCESS) {
		pr_err("%sregistering bc address failed (tagged)\n",
		       hcallid == H_REG_BCMC ? "" : "de");
		ret = -EIO;
		goto out_herr;
	}

	/* De/Register VLAN packets */
	reg_type = EHEA_BCMC_BROADCAST | EHEA_BCMC_VLANID_ALL;
	hret = ehea_h_reg_dereg_bcmc(port->adapter->handle,
				     port->logical_port_id,
				     reg_type, port->mac_addr, 0, hcallid);
	if (hret != H_SUCCESS) {
		pr_err("%sregistering bc address failed (vlan)\n",
		       hcallid == H_REG_BCMC ? "" : "de");
		ret = -EIO;
	}
out_herr:
	return ret;
}

static int ehea_set_mac_addr(struct net_device *dev, void *sa)
{
	struct ehea_port *port = netdev_priv(dev);
	struct sockaddr *mac_addr = sa;
	struct hcp_ehea_port_cb0 *cb0;
	int ret;
	u64 hret;

	if (!is_valid_ether_addr(mac_addr->sa_data)) {
		ret = -EADDRNOTAVAIL;
		goto out;
	}

	cb0 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb0) {
		pr_err("no mem for cb0\n");
		ret = -ENOMEM;
		goto out;
	}

	memcpy(&(cb0->port_mac_addr), &(mac_addr->sa_data[0]), ETH_ALEN);

	cb0->port_mac_addr = cb0->port_mac_addr >> 16;

	hret = ehea_h_modify_ehea_port(port->adapter->handle,
				       port->logical_port_id, H_PORT_CB0,
				       EHEA_BMASK_SET(H_PORT_CB0_MAC, 1), cb0);
	if (hret != H_SUCCESS) {
		ret = -EIO;
		goto out_free;
	}

	memcpy(dev->dev_addr, mac_addr->sa_data, dev->addr_len);

	/* Deregister old MAC in pHYP */
	if (port->state == EHEA_PORT_UP) {
		ret = ehea_broadcast_reg_helper(port, H_DEREG_BCMC);
		if (ret)
			goto out_upregs;
	}

	port->mac_addr = cb0->port_mac_addr << 16;

	/* Register new MAC in pHYP */
	if (port->state == EHEA_PORT_UP) {
		ret = ehea_broadcast_reg_helper(port, H_REG_BCMC);
		if (ret)
			goto out_upregs;
	}

	ret = 0;

out_upregs:
	ehea_update_bcmc_registrations();
out_free:
	free_page((unsigned long)cb0);
out:
	return ret;
}

static void ehea_promiscuous_error(u64 hret, int enable)
{
	if (hret == H_AUTHORITY)
		pr_info("Hypervisor denied %sabling promiscuous mode\n",
			enable == 1 ? "en" : "dis");
	else
		pr_err("failed %sabling promiscuous mode\n",
		       enable == 1 ? "en" : "dis");
}

static void ehea_promiscuous(struct net_device *dev, int enable)
{
	struct ehea_port *port = netdev_priv(dev);
	struct hcp_ehea_port_cb7 *cb7;
	u64 hret;

	if (enable == port->promisc)
		return;

	cb7 = (void *)get_zeroed_page(GFP_ATOMIC);
	if (!cb7) {
		pr_err("no mem for cb7\n");
		goto out;
	}

	/* Modify Pxs_DUCQPN in CB7 */
	cb7->def_uc_qpn = enable == 1 ? port->port_res[0].qp->fw_handle : 0;

	hret = ehea_h_modify_ehea_port(port->adapter->handle,
				       port->logical_port_id,
				       H_PORT_CB7, H_PORT_CB7_DUCQPN, cb7);
	if (hret) {
		ehea_promiscuous_error(hret, enable);
		goto out;
	}

	port->promisc = enable;
out:
	free_page((unsigned long)cb7);
}

static u64 ehea_multicast_reg_helper(struct ehea_port *port, u64 mc_mac_addr,
				     u32 hcallid)
{
	u64 hret;
	u8 reg_type;

	reg_type = EHEA_BCMC_MULTICAST | EHEA_BCMC_UNTAGGED;
	if (mc_mac_addr == 0)
		reg_type |= EHEA_BCMC_SCOPE_ALL;

	hret = ehea_h_reg_dereg_bcmc(port->adapter->handle,
				     port->logical_port_id,
				     reg_type, mc_mac_addr, 0, hcallid);
	if (hret)
		goto out;

	reg_type = EHEA_BCMC_MULTICAST | EHEA_BCMC_VLANID_ALL;
	if (mc_mac_addr == 0)
		reg_type |= EHEA_BCMC_SCOPE_ALL;

	hret = ehea_h_reg_dereg_bcmc(port->adapter->handle,
				     port->logical_port_id,
				     reg_type, mc_mac_addr, 0, hcallid);
out:
	return hret;
}

static int ehea_drop_multicast_list(struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_mc_list *mc_entry = port->mc_list;
	struct list_head *pos;
	struct list_head *temp;
	int ret = 0;
	u64 hret;

	list_for_each_safe(pos, temp, &(port->mc_list->list)) {
		mc_entry = list_entry(pos, struct ehea_mc_list, list);

		hret = ehea_multicast_reg_helper(port, mc_entry->macaddr,
						 H_DEREG_BCMC);
		if (hret) {
			pr_err("failed deregistering mcast MAC\n");
			ret = -EIO;
		}

		list_del(pos);
		kfree(mc_entry);
	}
	return ret;
}

static void ehea_allmulti(struct net_device *dev, int enable)
{
	struct ehea_port *port = netdev_priv(dev);
	u64 hret;

	if (!port->allmulti) {
		if (enable) {
			/* Enable ALLMULTI */
			ehea_drop_multicast_list(dev);
			hret = ehea_multicast_reg_helper(port, 0, H_REG_BCMC);
			if (!hret)
				port->allmulti = 1;
			else
				netdev_err(dev,
					   "failed enabling IFF_ALLMULTI\n");
		}
	} else {
		if (!enable) {
			/* Disable ALLMULTI */
			hret = ehea_multicast_reg_helper(port, 0, H_DEREG_BCMC);
			if (!hret)
				port->allmulti = 0;
			else
				netdev_err(dev,
					   "failed disabling IFF_ALLMULTI\n");
		}
	}
}

static void ehea_add_multicast_entry(struct ehea_port *port, u8 *mc_mac_addr)
{
	struct ehea_mc_list *ehea_mcl_entry;
	u64 hret;

	ehea_mcl_entry = kzalloc(sizeof(*ehea_mcl_entry), GFP_ATOMIC);
	if (!ehea_mcl_entry)
		return;

	INIT_LIST_HEAD(&ehea_mcl_entry->list);

	memcpy(&ehea_mcl_entry->macaddr, mc_mac_addr, ETH_ALEN);

	hret = ehea_multicast_reg_helper(port, ehea_mcl_entry->macaddr,
					 H_REG_BCMC);
	if (!hret)
		list_add(&ehea_mcl_entry->list, &port->mc_list->list);
	else {
		pr_err("failed registering mcast MAC\n");
		kfree(ehea_mcl_entry);
	}
}

static void ehea_set_multicast_list(struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	int ret;

	ehea_promiscuous(dev, !!(dev->flags & IFF_PROMISC));

	if (dev->flags & IFF_ALLMULTI) {
		ehea_allmulti(dev, 1);
		goto out;
	}
	ehea_allmulti(dev, 0);

	if (!netdev_mc_empty(dev)) {
		ret = ehea_drop_multicast_list(dev);
		if (ret) {
			/* Dropping the current multicast list failed.
			 * Enabling ALL_MULTI is the best we can do.
			 */
			ehea_allmulti(dev, 1);
		}

		if (netdev_mc_count(dev) > port->adapter->max_mc_mac) {
			pr_info("Mcast registration limit reached (0x%llx). Use ALLMULTI!\n",
				port->adapter->max_mc_mac);
			goto out;
		}

		netdev_for_each_mc_addr(ha, dev)
			ehea_add_multicast_entry(port, ha->addr);

	}
out:
	ehea_update_bcmc_registrations();
}

static void xmit_common(struct sk_buff *skb, struct ehea_swqe *swqe)
{
	swqe->tx_control |= EHEA_SWQE_IMM_DATA_PRESENT | EHEA_SWQE_CRC;

	if (vlan_get_protocol(skb) != htons(ETH_P_IP))
		return;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		swqe->tx_control |= EHEA_SWQE_IP_CHECKSUM;

	swqe->ip_start = skb_network_offset(skb);
	swqe->ip_end = swqe->ip_start + ip_hdrlen(skb) - 1;

	switch (ip_hdr(skb)->protocol) {
	case IPPROTO_UDP:
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			swqe->tx_control |= EHEA_SWQE_TCP_CHECKSUM;

		swqe->tcp_offset = swqe->ip_end + 1 +
				   offsetof(struct udphdr, check);
		break;

	case IPPROTO_TCP:
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			swqe->tx_control |= EHEA_SWQE_TCP_CHECKSUM;

		swqe->tcp_offset = swqe->ip_end + 1 +
				   offsetof(struct tcphdr, check);
		break;
	}
}

static void ehea_xmit2(struct sk_buff *skb, struct net_device *dev,
		       struct ehea_swqe *swqe, u32 lkey)
{
	swqe->tx_control |= EHEA_SWQE_DESCRIPTORS_PRESENT;

	xmit_common(skb, swqe);

	write_swqe2_data(skb, dev, swqe, lkey);
}

static void ehea_xmit3(struct sk_buff *skb, struct net_device *dev,
		       struct ehea_swqe *swqe)
{
	u8 *imm_data = &swqe->u.immdata_nodesc.immediate_data[0];

	xmit_common(skb, swqe);

	if (!skb->data_len)
		skb_copy_from_linear_data(skb, imm_data, skb->len);
	else
		skb_copy_bits(skb, 0, imm_data, skb->len);

	swqe->immediate_data_length = skb->len;
	dev_consume_skb_any(skb);
}

static netdev_tx_t ehea_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_swqe *swqe;
	u32 lkey;
	int swqe_index;
	struct ehea_port_res *pr;
	struct netdev_queue *txq;

	pr = &port->port_res[skb_get_queue_mapping(skb)];
	txq = netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));

	swqe = ehea_get_swqe(pr->qp, &swqe_index);
	memset(swqe, 0, SWQE_HEADER_SIZE);
	atomic_dec(&pr->swqe_avail);

	if (skb_vlan_tag_present(skb)) {
		swqe->tx_control |= EHEA_SWQE_VLAN_INSERT;
		swqe->vlan_tag = skb_vlan_tag_get(skb);
	}

	pr->tx_packets++;
	pr->tx_bytes += skb->len;

	if (skb->len <= SWQE3_MAX_IMM) {
		u32 sig_iv = port->sig_comp_iv;
		u32 swqe_num = pr->swqe_id_counter;
		ehea_xmit3(skb, dev, swqe);
		swqe->wr_id = EHEA_BMASK_SET(EHEA_WR_ID_TYPE, EHEA_SWQE3_TYPE)
			| EHEA_BMASK_SET(EHEA_WR_ID_COUNT, swqe_num);
		if (pr->swqe_ll_count >= (sig_iv - 1)) {
			swqe->wr_id |= EHEA_BMASK_SET(EHEA_WR_ID_REFILL,
						      sig_iv);
			swqe->tx_control |= EHEA_SWQE_SIGNALLED_COMPLETION;
			pr->swqe_ll_count = 0;
		} else
			pr->swqe_ll_count += 1;
	} else {
		swqe->wr_id =
			EHEA_BMASK_SET(EHEA_WR_ID_TYPE, EHEA_SWQE2_TYPE)
		      | EHEA_BMASK_SET(EHEA_WR_ID_COUNT, pr->swqe_id_counter)
		      | EHEA_BMASK_SET(EHEA_WR_ID_REFILL, 1)
		      | EHEA_BMASK_SET(EHEA_WR_ID_INDEX, pr->sq_skba.index);
		pr->sq_skba.arr[pr->sq_skba.index] = skb;

		pr->sq_skba.index++;
		pr->sq_skba.index &= (pr->sq_skba.len - 1);

		lkey = pr->send_mr.lkey;
		ehea_xmit2(skb, dev, swqe, lkey);
		swqe->tx_control |= EHEA_SWQE_SIGNALLED_COMPLETION;
	}
	pr->swqe_id_counter += 1;

	netif_info(port, tx_queued, dev,
		   "post swqe on QP %d\n", pr->qp->init_attr.qp_nr);
	if (netif_msg_tx_queued(port))
		ehea_dump(swqe, 512, "swqe");

	if (unlikely(test_bit(__EHEA_STOP_XFER, &ehea_driver_flags))) {
		netif_tx_stop_queue(txq);
		swqe->tx_control |= EHEA_SWQE_PURGE;
	}

	ehea_post_swqe(pr->qp, swqe);

	if (unlikely(atomic_read(&pr->swqe_avail) <= 1)) {
		pr->p_stats.queue_stopped++;
		netif_tx_stop_queue(txq);
	}

	return NETDEV_TX_OK;
}

static int ehea_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_adapter *adapter = port->adapter;
	struct hcp_ehea_port_cb1 *cb1;
	int index;
	u64 hret;
	int err = 0;

	cb1 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb1) {
		pr_err("no mem for cb1\n");
		err = -ENOMEM;
		goto out;
	}

	hret = ehea_h_query_ehea_port(adapter->handle, port->logical_port_id,
				      H_PORT_CB1, H_PORT_CB1_ALL, cb1);
	if (hret != H_SUCCESS) {
		pr_err("query_ehea_port failed\n");
		err = -EINVAL;
		goto out;
	}

	index = (vid / 64);
	cb1->vlan_filter[index] |= ((u64)(0x8000000000000000 >> (vid & 0x3F)));

	hret = ehea_h_modify_ehea_port(adapter->handle, port->logical_port_id,
				       H_PORT_CB1, H_PORT_CB1_ALL, cb1);
	if (hret != H_SUCCESS) {
		pr_err("modify_ehea_port failed\n");
		err = -EINVAL;
	}
out:
	free_page((unsigned long)cb1);
	return err;
}

static int ehea_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_adapter *adapter = port->adapter;
	struct hcp_ehea_port_cb1 *cb1;
	int index;
	u64 hret;
	int err = 0;

	cb1 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb1) {
		pr_err("no mem for cb1\n");
		err = -ENOMEM;
		goto out;
	}

	hret = ehea_h_query_ehea_port(adapter->handle, port->logical_port_id,
				      H_PORT_CB1, H_PORT_CB1_ALL, cb1);
	if (hret != H_SUCCESS) {
		pr_err("query_ehea_port failed\n");
		err = -EINVAL;
		goto out;
	}

	index = (vid / 64);
	cb1->vlan_filter[index] &= ~((u64)(0x8000000000000000 >> (vid & 0x3F)));

	hret = ehea_h_modify_ehea_port(adapter->handle, port->logical_port_id,
				       H_PORT_CB1, H_PORT_CB1_ALL, cb1);
	if (hret != H_SUCCESS) {
		pr_err("modify_ehea_port failed\n");
		err = -EINVAL;
	}
out:
	free_page((unsigned long)cb1);
	return err;
}

static int ehea_activate_qp(struct ehea_adapter *adapter, struct ehea_qp *qp)
{
	int ret = -EIO;
	u64 hret;
	u16 dummy16 = 0;
	u64 dummy64 = 0;
	struct hcp_modify_qp_cb0 *cb0;

	cb0 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb0) {
		ret = -ENOMEM;
		goto out;
	}

	hret = ehea_h_query_ehea_qp(adapter->handle, 0, qp->fw_handle,
				    EHEA_BMASK_SET(H_QPCB0_ALL, 0xFFFF), cb0);
	if (hret != H_SUCCESS) {
		pr_err("query_ehea_qp failed (1)\n");
		goto out;
	}

	cb0->qp_ctl_reg = H_QP_CR_STATE_INITIALIZED;
	hret = ehea_h_modify_ehea_qp(adapter->handle, 0, qp->fw_handle,
				     EHEA_BMASK_SET(H_QPCB0_QP_CTL_REG, 1), cb0,
				     &dummy64, &dummy64, &dummy16, &dummy16);
	if (hret != H_SUCCESS) {
		pr_err("modify_ehea_qp failed (1)\n");
		goto out;
	}

	hret = ehea_h_query_ehea_qp(adapter->handle, 0, qp->fw_handle,
				    EHEA_BMASK_SET(H_QPCB0_ALL, 0xFFFF), cb0);
	if (hret != H_SUCCESS) {
		pr_err("query_ehea_qp failed (2)\n");
		goto out;
	}

	cb0->qp_ctl_reg = H_QP_CR_ENABLED | H_QP_CR_STATE_INITIALIZED;
	hret = ehea_h_modify_ehea_qp(adapter->handle, 0, qp->fw_handle,
				     EHEA_BMASK_SET(H_QPCB0_QP_CTL_REG, 1), cb0,
				     &dummy64, &dummy64, &dummy16, &dummy16);
	if (hret != H_SUCCESS) {
		pr_err("modify_ehea_qp failed (2)\n");
		goto out;
	}

	hret = ehea_h_query_ehea_qp(adapter->handle, 0, qp->fw_handle,
				    EHEA_BMASK_SET(H_QPCB0_ALL, 0xFFFF), cb0);
	if (hret != H_SUCCESS) {
		pr_err("query_ehea_qp failed (3)\n");
		goto out;
	}

	cb0->qp_ctl_reg = H_QP_CR_ENABLED | H_QP_CR_STATE_RDY2SND;
	hret = ehea_h_modify_ehea_qp(adapter->handle, 0, qp->fw_handle,
				     EHEA_BMASK_SET(H_QPCB0_QP_CTL_REG, 1), cb0,
				     &dummy64, &dummy64, &dummy16, &dummy16);
	if (hret != H_SUCCESS) {
		pr_err("modify_ehea_qp failed (3)\n");
		goto out;
	}

	hret = ehea_h_query_ehea_qp(adapter->handle, 0, qp->fw_handle,
				    EHEA_BMASK_SET(H_QPCB0_ALL, 0xFFFF), cb0);
	if (hret != H_SUCCESS) {
		pr_err("query_ehea_qp failed (4)\n");
		goto out;
	}

	ret = 0;
out:
	free_page((unsigned long)cb0);
	return ret;
}

static int ehea_port_res_setup(struct ehea_port *port, int def_qps)
{
	int ret, i;
	struct port_res_cfg pr_cfg, pr_cfg_small_rx;
	enum ehea_eq_type eq_type = EHEA_EQ;

	port->qp_eq = ehea_create_eq(port->adapter, eq_type,
				   EHEA_MAX_ENTRIES_EQ, 1);
	if (!port->qp_eq) {
		ret = -EINVAL;
		pr_err("ehea_create_eq failed (qp_eq)\n");
		goto out_kill_eq;
	}

	pr_cfg.max_entries_rcq = rq1_entries + rq2_entries + rq3_entries;
	pr_cfg.max_entries_scq = sq_entries * 2;
	pr_cfg.max_entries_sq = sq_entries;
	pr_cfg.max_entries_rq1 = rq1_entries;
	pr_cfg.max_entries_rq2 = rq2_entries;
	pr_cfg.max_entries_rq3 = rq3_entries;

	pr_cfg_small_rx.max_entries_rcq = 1;
	pr_cfg_small_rx.max_entries_scq = sq_entries;
	pr_cfg_small_rx.max_entries_sq = sq_entries;
	pr_cfg_small_rx.max_entries_rq1 = 1;
	pr_cfg_small_rx.max_entries_rq2 = 1;
	pr_cfg_small_rx.max_entries_rq3 = 1;

	for (i = 0; i < def_qps; i++) {
		ret = ehea_init_port_res(port, &port->port_res[i], &pr_cfg, i);
		if (ret)
			goto out_clean_pr;
	}
	for (i = def_qps; i < def_qps; i++) {
		ret = ehea_init_port_res(port, &port->port_res[i],
					 &pr_cfg_small_rx, i);
		if (ret)
			goto out_clean_pr;
	}

	return 0;

out_clean_pr:
	while (--i >= 0)
		ehea_clean_portres(port, &port->port_res[i]);

out_kill_eq:
	ehea_destroy_eq(port->qp_eq);
	return ret;
}

static int ehea_clean_all_portres(struct ehea_port *port)
{
	int ret = 0;
	int i;

	for (i = 0; i < port->num_def_qps; i++)
		ret |= ehea_clean_portres(port, &port->port_res[i]);

	ret |= ehea_destroy_eq(port->qp_eq);

	return ret;
}

static void ehea_remove_adapter_mr(struct ehea_adapter *adapter)
{
	if (adapter->active_ports)
		return;

	ehea_rem_mr(&adapter->mr);
}

static int ehea_add_adapter_mr(struct ehea_adapter *adapter)
{
	if (adapter->active_ports)
		return 0;

	return ehea_reg_kernel_mr(adapter, &adapter->mr);
}

static int ehea_up(struct net_device *dev)
{
	int ret, i;
	struct ehea_port *port = netdev_priv(dev);

	if (port->state == EHEA_PORT_UP)
		return 0;

	ret = ehea_port_res_setup(port, port->num_def_qps);
	if (ret) {
		netdev_err(dev, "port_res_failed\n");
		goto out;
	}

	/* Set default QP for this port */
	ret = ehea_configure_port(port);
	if (ret) {
		netdev_err(dev, "ehea_configure_port failed. ret:%d\n", ret);
		goto out_clean_pr;
	}

	ret = ehea_reg_interrupts(dev);
	if (ret) {
		netdev_err(dev, "reg_interrupts failed. ret:%d\n", ret);
		goto out_clean_pr;
	}

	for (i = 0; i < port->num_def_qps; i++) {
		ret = ehea_activate_qp(port->adapter, port->port_res[i].qp);
		if (ret) {
			netdev_err(dev, "activate_qp failed\n");
			goto out_free_irqs;
		}
	}

	for (i = 0; i < port->num_def_qps; i++) {
		ret = ehea_fill_port_res(&port->port_res[i]);
		if (ret) {
			netdev_err(dev, "out_free_irqs\n");
			goto out_free_irqs;
		}
	}

	ret = ehea_broadcast_reg_helper(port, H_REG_BCMC);
	if (ret) {
		ret = -EIO;
		goto out_free_irqs;
	}

	port->state = EHEA_PORT_UP;

	ret = 0;
	goto out;

out_free_irqs:
	ehea_free_interrupts(dev);

out_clean_pr:
	ehea_clean_all_portres(port);
out:
	if (ret)
		netdev_info(dev, "Failed starting. ret=%i\n", ret);

	ehea_update_bcmc_registrations();
	ehea_update_firmware_handles();

	return ret;
}

static void port_napi_disable(struct ehea_port *port)
{
	int i;

	for (i = 0; i < port->num_def_qps; i++)
		napi_disable(&port->port_res[i].napi);
}

static void port_napi_enable(struct ehea_port *port)
{
	int i;

	for (i = 0; i < port->num_def_qps; i++)
		napi_enable(&port->port_res[i].napi);
}

static int ehea_open(struct net_device *dev)
{
	int ret;
	struct ehea_port *port = netdev_priv(dev);

	mutex_lock(&port->port_lock);

	netif_info(port, ifup, dev, "enabling port\n");

	netif_carrier_off(dev);

	ret = ehea_up(dev);
	if (!ret) {
		port_napi_enable(port);
		netif_tx_start_all_queues(dev);
	}

	mutex_unlock(&port->port_lock);
	schedule_delayed_work(&port->stats_work,
			      round_jiffies_relative(msecs_to_jiffies(1000)));

	return ret;
}

static int ehea_down(struct net_device *dev)
{
	int ret;
	struct ehea_port *port = netdev_priv(dev);

	if (port->state == EHEA_PORT_DOWN)
		return 0;

	ehea_drop_multicast_list(dev);
	ehea_allmulti(dev, 0);
	ehea_broadcast_reg_helper(port, H_DEREG_BCMC);

	ehea_free_interrupts(dev);

	port->state = EHEA_PORT_DOWN;

	ehea_update_bcmc_registrations();

	ret = ehea_clean_all_portres(port);
	if (ret)
		netdev_info(dev, "Failed freeing resources. ret=%i\n", ret);

	ehea_update_firmware_handles();

	return ret;
}

static int ehea_stop(struct net_device *dev)
{
	int ret;
	struct ehea_port *port = netdev_priv(dev);

	netif_info(port, ifdown, dev, "disabling port\n");

	set_bit(__EHEA_DISABLE_PORT_RESET, &port->flags);
	cancel_work_sync(&port->reset_task);
	cancel_delayed_work_sync(&port->stats_work);
	mutex_lock(&port->port_lock);
	netif_tx_stop_all_queues(dev);
	port_napi_disable(port);
	ret = ehea_down(dev);
	mutex_unlock(&port->port_lock);
	clear_bit(__EHEA_DISABLE_PORT_RESET, &port->flags);
	return ret;
}

static void ehea_purge_sq(struct ehea_qp *orig_qp)
{
	struct ehea_qp qp = *orig_qp;
	struct ehea_qp_init_attr *init_attr = &qp.init_attr;
	struct ehea_swqe *swqe;
	int wqe_index;
	int i;

	for (i = 0; i < init_attr->act_nr_send_wqes; i++) {
		swqe = ehea_get_swqe(&qp, &wqe_index);
		swqe->tx_control |= EHEA_SWQE_PURGE;
	}
}

static void ehea_flush_sq(struct ehea_port *port)
{
	int i;

	for (i = 0; i < port->num_def_qps; i++) {
		struct ehea_port_res *pr = &port->port_res[i];
		int swqe_max = pr->sq_skba_size - 2 - pr->swqe_ll_count;
		int ret;

		ret = wait_event_timeout(port->swqe_avail_wq,
			 atomic_read(&pr->swqe_avail) >= swqe_max,
			 msecs_to_jiffies(100));

		if (!ret) {
			pr_err("WARNING: sq not flushed completely\n");
			break;
		}
	}
}

static int ehea_stop_qps(struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_adapter *adapter = port->adapter;
	struct hcp_modify_qp_cb0 *cb0;
	int ret = -EIO;
	int dret;
	int i;
	u64 hret;
	u64 dummy64 = 0;
	u16 dummy16 = 0;

	cb0 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb0) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < (port->num_def_qps); i++) {
		struct ehea_port_res *pr =  &port->port_res[i];
		struct ehea_qp *qp = pr->qp;

		/* Purge send queue */
		ehea_purge_sq(qp);

		/* Disable queue pair */
		hret = ehea_h_query_ehea_qp(adapter->handle, 0, qp->fw_handle,
					    EHEA_BMASK_SET(H_QPCB0_ALL, 0xFFFF),
					    cb0);
		if (hret != H_SUCCESS) {
			pr_err("query_ehea_qp failed (1)\n");
			goto out;
		}

		cb0->qp_ctl_reg = (cb0->qp_ctl_reg & H_QP_CR_RES_STATE) << 8;
		cb0->qp_ctl_reg &= ~H_QP_CR_ENABLED;

		hret = ehea_h_modify_ehea_qp(adapter->handle, 0, qp->fw_handle,
					     EHEA_BMASK_SET(H_QPCB0_QP_CTL_REG,
							    1), cb0, &dummy64,
					     &dummy64, &dummy16, &dummy16);
		if (hret != H_SUCCESS) {
			pr_err("modify_ehea_qp failed (1)\n");
			goto out;
		}

		hret = ehea_h_query_ehea_qp(adapter->handle, 0, qp->fw_handle,
					    EHEA_BMASK_SET(H_QPCB0_ALL, 0xFFFF),
					    cb0);
		if (hret != H_SUCCESS) {
			pr_err("query_ehea_qp failed (2)\n");
			goto out;
		}

		/* deregister shared memory regions */
		dret = ehea_rem_smrs(pr);
		if (dret) {
			pr_err("unreg shared memory region failed\n");
			goto out;
		}
	}

	ret = 0;
out:
	free_page((unsigned long)cb0);

	return ret;
}

static void ehea_update_rqs(struct ehea_qp *orig_qp, struct ehea_port_res *pr)
{
	struct ehea_qp qp = *orig_qp;
	struct ehea_qp_init_attr *init_attr = &qp.init_attr;
	struct ehea_rwqe *rwqe;
	struct sk_buff **skba_rq2 = pr->rq2_skba.arr;
	struct sk_buff **skba_rq3 = pr->rq3_skba.arr;
	struct sk_buff *skb;
	u32 lkey = pr->recv_mr.lkey;


	int i;
	int index;

	for (i = 0; i < init_attr->act_nr_rwqes_rq2 + 1; i++) {
		rwqe = ehea_get_next_rwqe(&qp, 2);
		rwqe->sg_list[0].l_key = lkey;
		index = EHEA_BMASK_GET(EHEA_WR_ID_INDEX, rwqe->wr_id);
		skb = skba_rq2[index];
		if (skb)
			rwqe->sg_list[0].vaddr = ehea_map_vaddr(skb->data);
	}

	for (i = 0; i < init_attr->act_nr_rwqes_rq3 + 1; i++) {
		rwqe = ehea_get_next_rwqe(&qp, 3);
		rwqe->sg_list[0].l_key = lkey;
		index = EHEA_BMASK_GET(EHEA_WR_ID_INDEX, rwqe->wr_id);
		skb = skba_rq3[index];
		if (skb)
			rwqe->sg_list[0].vaddr = ehea_map_vaddr(skb->data);
	}
}

static int ehea_restart_qps(struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_adapter *adapter = port->adapter;
	int ret = 0;
	int i;

	struct hcp_modify_qp_cb0 *cb0;
	u64 hret;
	u64 dummy64 = 0;
	u16 dummy16 = 0;

	cb0 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb0)
		return -ENOMEM;

	for (i = 0; i < (port->num_def_qps); i++) {
		struct ehea_port_res *pr =  &port->port_res[i];
		struct ehea_qp *qp = pr->qp;

		ret = ehea_gen_smrs(pr);
		if (ret) {
			netdev_err(dev, "creation of shared memory regions failed\n");
			goto out;
		}

		ehea_update_rqs(qp, pr);

		/* Enable queue pair */
		hret = ehea_h_query_ehea_qp(adapter->handle, 0, qp->fw_handle,
					    EHEA_BMASK_SET(H_QPCB0_ALL, 0xFFFF),
					    cb0);
		if (hret != H_SUCCESS) {
			netdev_err(dev, "query_ehea_qp failed (1)\n");
			ret = -EFAULT;
			goto out;
		}

		cb0->qp_ctl_reg = (cb0->qp_ctl_reg & H_QP_CR_RES_STATE) << 8;
		cb0->qp_ctl_reg |= H_QP_CR_ENABLED;

		hret = ehea_h_modify_ehea_qp(adapter->handle, 0, qp->fw_handle,
					     EHEA_BMASK_SET(H_QPCB0_QP_CTL_REG,
							    1), cb0, &dummy64,
					     &dummy64, &dummy16, &dummy16);
		if (hret != H_SUCCESS) {
			netdev_err(dev, "modify_ehea_qp failed (1)\n");
			ret = -EFAULT;
			goto out;
		}

		hret = ehea_h_query_ehea_qp(adapter->handle, 0, qp->fw_handle,
					    EHEA_BMASK_SET(H_QPCB0_ALL, 0xFFFF),
					    cb0);
		if (hret != H_SUCCESS) {
			netdev_err(dev, "query_ehea_qp failed (2)\n");
			ret = -EFAULT;
			goto out;
		}

		/* refill entire queue */
		ehea_refill_rq1(pr, pr->rq1_skba.index, 0);
		ehea_refill_rq2(pr, 0);
		ehea_refill_rq3(pr, 0);
	}
out:
	free_page((unsigned long)cb0);

	return ret;
}

static void ehea_reset_port(struct work_struct *work)
{
	int ret;
	struct ehea_port *port =
		container_of(work, struct ehea_port, reset_task);
	struct net_device *dev = port->netdev;

	mutex_lock(&dlpar_mem_lock);
	port->resets++;
	mutex_lock(&port->port_lock);
	netif_tx_disable(dev);

	port_napi_disable(port);

	ehea_down(dev);

	ret = ehea_up(dev);
	if (ret)
		goto out;

	ehea_set_multicast_list(dev);

	netif_info(port, timer, dev, "reset successful\n");

	port_napi_enable(port);

	netif_tx_wake_all_queues(dev);
out:
	mutex_unlock(&port->port_lock);
	mutex_unlock(&dlpar_mem_lock);
}

static void ehea_rereg_mrs(void)
{
	int ret, i;
	struct ehea_adapter *adapter;

	pr_info("LPAR memory changed - re-initializing driver\n");

	list_for_each_entry(adapter, &adapter_list, list)
		if (adapter->active_ports) {
			/* Shutdown all ports */
			for (i = 0; i < EHEA_MAX_PORTS; i++) {
				struct ehea_port *port = adapter->port[i];
				struct net_device *dev;

				if (!port)
					continue;

				dev = port->netdev;

				if (dev->flags & IFF_UP) {
					mutex_lock(&port->port_lock);
					netif_tx_disable(dev);
					ehea_flush_sq(port);
					ret = ehea_stop_qps(dev);
					if (ret) {
						mutex_unlock(&port->port_lock);
						goto out;
					}
					port_napi_disable(port);
					mutex_unlock(&port->port_lock);
				}
				reset_sq_restart_flag(port);
			}

			/* Unregister old memory region */
			ret = ehea_rem_mr(&adapter->mr);
			if (ret) {
				pr_err("unregister MR failed - driver inoperable!\n");
				goto out;
			}
		}

	clear_bit(__EHEA_STOP_XFER, &ehea_driver_flags);

	list_for_each_entry(adapter, &adapter_list, list)
		if (adapter->active_ports) {
			/* Register new memory region */
			ret = ehea_reg_kernel_mr(adapter, &adapter->mr);
			if (ret) {
				pr_err("register MR failed - driver inoperable!\n");
				goto out;
			}

			/* Restart all ports */
			for (i = 0; i < EHEA_MAX_PORTS; i++) {
				struct ehea_port *port = adapter->port[i];

				if (port) {
					struct net_device *dev = port->netdev;

					if (dev->flags & IFF_UP) {
						mutex_lock(&port->port_lock);
						ret = ehea_restart_qps(dev);
						if (!ret) {
							check_sqs(port);
							port_napi_enable(port);
							netif_tx_wake_all_queues(dev);
						} else {
							netdev_err(dev, "Unable to restart QPS\n");
						}
						mutex_unlock(&port->port_lock);
					}
				}
			}
		}
	pr_info("re-initializing driver complete\n");
out:
	return;
}

static void ehea_tx_watchdog(struct net_device *dev, unsigned int txqueue)
{
	struct ehea_port *port = netdev_priv(dev);

	if (netif_carrier_ok(dev) &&
	    !test_bit(__EHEA_STOP_XFER, &ehea_driver_flags))
		ehea_schedule_port_reset(port);
}

static int ehea_sense_adapter_attr(struct ehea_adapter *adapter)
{
	struct hcp_query_ehea *cb;
	u64 hret;
	int ret;

	cb = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb) {
		ret = -ENOMEM;
		goto out;
	}

	hret = ehea_h_query_ehea(adapter->handle, cb);

	if (hret != H_SUCCESS) {
		ret = -EIO;
		goto out_herr;
	}

	adapter->max_mc_mac = cb->max_mc_mac - 1;
	ret = 0;

out_herr:
	free_page((unsigned long)cb);
out:
	return ret;
}

static int ehea_get_jumboframe_status(struct ehea_port *port, int *jumbo)
{
	struct hcp_ehea_port_cb4 *cb4;
	u64 hret;
	int ret = 0;

	*jumbo = 0;

	/* (Try to) enable *jumbo frames */
	cb4 = (void *)get_zeroed_page(GFP_KERNEL);
	if (!cb4) {
		pr_err("no mem for cb4\n");
		ret = -ENOMEM;
		goto out;
	} else {
		hret = ehea_h_query_ehea_port(port->adapter->handle,
					      port->logical_port_id,
					      H_PORT_CB4,
					      H_PORT_CB4_JUMBO, cb4);
		if (hret == H_SUCCESS) {
			if (cb4->jumbo_frame)
				*jumbo = 1;
			else {
				cb4->jumbo_frame = 1;
				hret = ehea_h_modify_ehea_port(port->adapter->
							       handle,
							       port->
							       logical_port_id,
							       H_PORT_CB4,
							       H_PORT_CB4_JUMBO,
							       cb4);
				if (hret == H_SUCCESS)
					*jumbo = 1;
			}
		} else
			ret = -EINVAL;

		free_page((unsigned long)cb4);
	}
out:
	return ret;
}

static ssize_t log_port_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ehea_port *port = container_of(dev, struct ehea_port, ofdev.dev);
	return sprintf(buf, "%d", port->logical_port_id);
}

static DEVICE_ATTR_RO(log_port_id);

static void logical_port_release(struct device *dev)
{
	struct ehea_port *port = container_of(dev, struct ehea_port, ofdev.dev);
	of_node_put(port->ofdev.dev.of_node);
}

static struct device *ehea_register_port(struct ehea_port *port,
					 struct device_node *dn)
{
	int ret;

	port->ofdev.dev.of_node = of_node_get(dn);
	port->ofdev.dev.parent = &port->adapter->ofdev->dev;
	port->ofdev.dev.bus = &ibmebus_bus_type;

	dev_set_name(&port->ofdev.dev, "port%d", port_name_cnt++);
	port->ofdev.dev.release = logical_port_release;

	ret = of_device_register(&port->ofdev);
	if (ret) {
		pr_err("failed to register device. ret=%d\n", ret);
		goto out;
	}

	ret = device_create_file(&port->ofdev.dev, &dev_attr_log_port_id);
	if (ret) {
		pr_err("failed to register attributes, ret=%d\n", ret);
		goto out_unreg_of_dev;
	}

	return &port->ofdev.dev;

out_unreg_of_dev:
	of_device_unregister(&port->ofdev);
out:
	return NULL;
}

static void ehea_unregister_port(struct ehea_port *port)
{
	device_remove_file(&port->ofdev.dev, &dev_attr_log_port_id);
	of_device_unregister(&port->ofdev);
}

static const struct net_device_ops ehea_netdev_ops = {
	.ndo_open		= ehea_open,
	.ndo_stop		= ehea_stop,
	.ndo_start_xmit		= ehea_start_xmit,
	.ndo_get_stats64	= ehea_get_stats64,
	.ndo_set_mac_address	= ehea_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= ehea_set_multicast_list,
	.ndo_vlan_rx_add_vid	= ehea_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= ehea_vlan_rx_kill_vid,
	.ndo_tx_timeout		= ehea_tx_watchdog,
};

static struct ehea_port *ehea_setup_single_port(struct ehea_adapter *adapter,
					 u32 logical_port_id,
					 struct device_node *dn)
{
	int ret;
	struct net_device *dev;
	struct ehea_port *port;
	struct device *port_dev;
	int jumbo;

	/* allocate memory for the port structures */
	dev = alloc_etherdev_mq(sizeof(struct ehea_port), EHEA_MAX_PORT_RES);

	if (!dev) {
		ret = -ENOMEM;
		goto out_err;
	}

	port = netdev_priv(dev);

	mutex_init(&port->port_lock);
	port->state = EHEA_PORT_DOWN;
	port->sig_comp_iv = sq_entries / 10;

	port->adapter = adapter;
	port->netdev = dev;
	port->logical_port_id = logical_port_id;

	port->msg_enable = netif_msg_init(msg_level, EHEA_MSG_DEFAULT);

	port->mc_list = kzalloc(sizeof(struct ehea_mc_list), GFP_KERNEL);
	if (!port->mc_list) {
		ret = -ENOMEM;
		goto out_free_ethdev;
	}

	INIT_LIST_HEAD(&port->mc_list->list);

	ret = ehea_sense_port_attr(port);
	if (ret)
		goto out_free_mc_list;

	netif_set_real_num_rx_queues(dev, port->num_def_qps);
	netif_set_real_num_tx_queues(dev, port->num_def_qps);

	port_dev = ehea_register_port(port, dn);
	if (!port_dev)
		goto out_free_mc_list;

	SET_NETDEV_DEV(dev, port_dev);

	/* initialize net_device structure */
	eth_hw_addr_set(dev, &port->mac_addr);

	dev->netdev_ops = &ehea_netdev_ops;
	ehea_set_ethtool_ops(dev);

	dev->hw_features = NETIF_F_SG | NETIF_F_TSO |
		      NETIF_F_IP_CSUM | NETIF_F_HW_VLAN_CTAG_TX;
	dev->features = NETIF_F_SG | NETIF_F_TSO |
		      NETIF_F_HIGHDMA | NETIF_F_IP_CSUM |
		      NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
		      NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_RXCSUM;
	dev->vlan_features = NETIF_F_SG | NETIF_F_TSO | NETIF_F_HIGHDMA |
			NETIF_F_IP_CSUM;
	dev->watchdog_timeo = EHEA_WATCH_DOG_TIMEOUT;

	/* MTU range: 68 - 9022 */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = EHEA_MAX_PACKET_SIZE;

	INIT_WORK(&port->reset_task, ehea_reset_port);
	INIT_DELAYED_WORK(&port->stats_work, ehea_update_stats);

	init_waitqueue_head(&port->swqe_avail_wq);
	init_waitqueue_head(&port->restart_wq);

	ret = register_netdev(dev);
	if (ret) {
		pr_err("register_netdev failed. ret=%d\n", ret);
		goto out_unreg_port;
	}

	ret = ehea_get_jumboframe_status(port, &jumbo);
	if (ret)
		netdev_err(dev, "failed determining jumbo frame status\n");

	netdev_info(dev, "Jumbo frames are %sabled\n",
		    jumbo == 1 ? "en" : "dis");

	adapter->active_ports++;

	return port;

out_unreg_port:
	ehea_unregister_port(port);

out_free_mc_list:
	kfree(port->mc_list);

out_free_ethdev:
	free_netdev(dev);

out_err:
	pr_err("setting up logical port with id=%d failed, ret=%d\n",
	       logical_port_id, ret);
	return NULL;
}

static void ehea_shutdown_single_port(struct ehea_port *port)
{
	struct ehea_adapter *adapter = port->adapter;

	cancel_work_sync(&port->reset_task);
	cancel_delayed_work_sync(&port->stats_work);
	unregister_netdev(port->netdev);
	ehea_unregister_port(port);
	kfree(port->mc_list);
	free_netdev(port->netdev);
	adapter->active_ports--;
}

static int ehea_setup_ports(struct ehea_adapter *adapter)
{
	struct device_node *lhea_dn;
	struct device_node *eth_dn = NULL;

	const u32 *dn_log_port_id;
	int i = 0;

	lhea_dn = adapter->ofdev->dev.of_node;
	while ((eth_dn = of_get_next_child(lhea_dn, eth_dn))) {

		dn_log_port_id = of_get_property(eth_dn, "ibm,hea-port-no",
						 NULL);
		if (!dn_log_port_id) {
			pr_err("bad device node: eth_dn name=%pOF\n", eth_dn);
			continue;
		}

		if (ehea_add_adapter_mr(adapter)) {
			pr_err("creating MR failed\n");
			of_node_put(eth_dn);
			return -EIO;
		}

		adapter->port[i] = ehea_setup_single_port(adapter,
							  *dn_log_port_id,
							  eth_dn);
		if (adapter->port[i])
			netdev_info(adapter->port[i]->netdev,
				    "logical port id #%d\n", *dn_log_port_id);
		else
			ehea_remove_adapter_mr(adapter);

		i++;
	}
	return 0;
}

static struct device_node *ehea_get_eth_dn(struct ehea_adapter *adapter,
					   u32 logical_port_id)
{
	struct device_node *lhea_dn;
	struct device_node *eth_dn = NULL;
	const u32 *dn_log_port_id;

	lhea_dn = adapter->ofdev->dev.of_node;
	while ((eth_dn = of_get_next_child(lhea_dn, eth_dn))) {

		dn_log_port_id = of_get_property(eth_dn, "ibm,hea-port-no",
						 NULL);
		if (dn_log_port_id)
			if (*dn_log_port_id == logical_port_id)
				return eth_dn;
	}

	return NULL;
}

static ssize_t probe_port_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ehea_adapter *adapter = dev_get_drvdata(dev);
	struct ehea_port *port;
	struct device_node *eth_dn = NULL;
	int i;

	u32 logical_port_id;

	sscanf(buf, "%d", &logical_port_id);

	port = ehea_get_port(adapter, logical_port_id);

	if (port) {
		netdev_info(port->netdev, "adding port with logical port id=%d failed: port already configured\n",
			    logical_port_id);
		return -EINVAL;
	}

	eth_dn = ehea_get_eth_dn(adapter, logical_port_id);

	if (!eth_dn) {
		pr_info("no logical port with id %d found\n", logical_port_id);
		return -EINVAL;
	}

	if (ehea_add_adapter_mr(adapter)) {
		pr_err("creating MR failed\n");
		of_node_put(eth_dn);
		return -EIO;
	}

	port = ehea_setup_single_port(adapter, logical_port_id, eth_dn);

	of_node_put(eth_dn);

	if (port) {
		for (i = 0; i < EHEA_MAX_PORTS; i++)
			if (!adapter->port[i]) {
				adapter->port[i] = port;
				break;
			}

		netdev_info(port->netdev, "added: (logical port id=%d)\n",
			    logical_port_id);
	} else {
		ehea_remove_adapter_mr(adapter);
		return -EIO;
	}

	return (ssize_t) count;
}

static ssize_t remove_port_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct ehea_adapter *adapter = dev_get_drvdata(dev);
	struct ehea_port *port;
	int i;
	u32 logical_port_id;

	sscanf(buf, "%d", &logical_port_id);

	port = ehea_get_port(adapter, logical_port_id);

	if (port) {
		netdev_info(port->netdev, "removed: (logical port id=%d)\n",
			    logical_port_id);

		ehea_shutdown_single_port(port);

		for (i = 0; i < EHEA_MAX_PORTS; i++)
			if (adapter->port[i] == port) {
				adapter->port[i] = NULL;
				break;
			}
	} else {
		pr_err("removing port with logical port id=%d failed. port not configured.\n",
		       logical_port_id);
		return -EINVAL;
	}

	ehea_remove_adapter_mr(adapter);

	return (ssize_t) count;
}

static DEVICE_ATTR_WO(probe_port);
static DEVICE_ATTR_WO(remove_port);

static int ehea_create_device_sysfs(struct platform_device *dev)
{
	int ret = device_create_file(&dev->dev, &dev_attr_probe_port);
	if (ret)
		goto out;

	ret = device_create_file(&dev->dev, &dev_attr_remove_port);
out:
	return ret;
}

static void ehea_remove_device_sysfs(struct platform_device *dev)
{
	device_remove_file(&dev->dev, &dev_attr_probe_port);
	device_remove_file(&dev->dev, &dev_attr_remove_port);
}

static int ehea_reboot_notifier(struct notifier_block *nb,
				unsigned long action, void *unused)
{
	if (action == SYS_RESTART) {
		pr_info("Reboot: freeing all eHEA resources\n");
		ibmebus_unregister_driver(&ehea_driver);
	}
	return NOTIFY_DONE;
}

static struct notifier_block ehea_reboot_nb = {
	.notifier_call = ehea_reboot_notifier,
};

static int ehea_mem_notifier(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	int ret = NOTIFY_BAD;
	struct memory_notify *arg = data;

	mutex_lock(&dlpar_mem_lock);

	switch (action) {
	case MEM_CANCEL_OFFLINE:
		pr_info("memory offlining canceled");
		fallthrough;	/* re-add canceled memory block */

	case MEM_ONLINE:
		pr_info("memory is going online");
		set_bit(__EHEA_STOP_XFER, &ehea_driver_flags);
		if (ehea_add_sect_bmap(arg->start_pfn, arg->nr_pages))
			goto out_unlock;
		ehea_rereg_mrs();
		break;

	case MEM_GOING_OFFLINE:
		pr_info("memory is going offline");
		set_bit(__EHEA_STOP_XFER, &ehea_driver_flags);
		if (ehea_rem_sect_bmap(arg->start_pfn, arg->nr_pages))
			goto out_unlock;
		ehea_rereg_mrs();
		break;

	default:
		break;
	}

	ehea_update_firmware_handles();
	ret = NOTIFY_OK;

out_unlock:
	mutex_unlock(&dlpar_mem_lock);
	return ret;
}

static struct notifier_block ehea_mem_nb = {
	.notifier_call = ehea_mem_notifier,
};

static void ehea_crash_handler(void)
{
	int i;

	if (ehea_fw_handles.arr)
		for (i = 0; i < ehea_fw_handles.num_entries; i++)
			ehea_h_free_resource(ehea_fw_handles.arr[i].adh,
					     ehea_fw_handles.arr[i].fwh,
					     FORCE_FREE);

	if (ehea_bcmc_regs.arr)
		for (i = 0; i < ehea_bcmc_regs.num_entries; i++)
			ehea_h_reg_dereg_bcmc(ehea_bcmc_regs.arr[i].adh,
					      ehea_bcmc_regs.arr[i].port_id,
					      ehea_bcmc_regs.arr[i].reg_type,
					      ehea_bcmc_regs.arr[i].macaddr,
					      0, H_DEREG_BCMC);
}

static atomic_t ehea_memory_hooks_registered;

/* Register memory hooks on probe of first adapter */
static int ehea_register_memory_hooks(void)
{
	int ret = 0;

	if (atomic_inc_return(&ehea_memory_hooks_registered) > 1)
		return 0;

	ret = ehea_create_busmap();
	if (ret) {
		pr_info("ehea_create_busmap failed\n");
		goto out;
	}

	ret = register_reboot_notifier(&ehea_reboot_nb);
	if (ret) {
		pr_info("register_reboot_notifier failed\n");
		goto out;
	}

	ret = register_memory_notifier(&ehea_mem_nb);
	if (ret) {
		pr_info("register_memory_notifier failed\n");
		goto out2;
	}

	ret = crash_shutdown_register(ehea_crash_handler);
	if (ret) {
		pr_info("crash_shutdown_register failed\n");
		goto out3;
	}

	return 0;

out3:
	unregister_memory_notifier(&ehea_mem_nb);
out2:
	unregister_reboot_notifier(&ehea_reboot_nb);
out:
	atomic_dec(&ehea_memory_hooks_registered);
	return ret;
}

static void ehea_unregister_memory_hooks(void)
{
	/* Only remove the hooks if we've registered them */
	if (atomic_read(&ehea_memory_hooks_registered) == 0)
		return;

	unregister_reboot_notifier(&ehea_reboot_nb);
	if (crash_shutdown_unregister(ehea_crash_handler))
		pr_info("failed unregistering crash handler\n");
	unregister_memory_notifier(&ehea_mem_nb);
}

static int ehea_probe_adapter(struct platform_device *dev)
{
	struct ehea_adapter *adapter;
	const u64 *adapter_handle;
	int ret;
	int i;

	ret = ehea_register_memory_hooks();
	if (ret)
		return ret;

	if (!dev || !dev->dev.of_node) {
		pr_err("Invalid ibmebus device probed\n");
		return -EINVAL;
	}

	adapter = devm_kzalloc(&dev->dev, sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		ret = -ENOMEM;
		dev_err(&dev->dev, "no mem for ehea_adapter\n");
		goto out;
	}

	list_add(&adapter->list, &adapter_list);

	adapter->ofdev = dev;

	adapter_handle = of_get_property(dev->dev.of_node, "ibm,hea-handle",
					 NULL);
	if (adapter_handle)
		adapter->handle = *adapter_handle;

	if (!adapter->handle) {
		dev_err(&dev->dev, "failed getting handle for adapter"
			" '%pOF'\n", dev->dev.of_node);
		ret = -ENODEV;
		goto out_free_ad;
	}

	adapter->pd = EHEA_PD_ID;

	platform_set_drvdata(dev, adapter);


	/* initialize adapter and ports */
	/* get adapter properties */
	ret = ehea_sense_adapter_attr(adapter);
	if (ret) {
		dev_err(&dev->dev, "sense_adapter_attr failed: %d\n", ret);
		goto out_free_ad;
	}

	adapter->neq = ehea_create_eq(adapter,
				      EHEA_NEQ, EHEA_MAX_ENTRIES_EQ, 1);
	if (!adapter->neq) {
		ret = -EIO;
		dev_err(&dev->dev, "NEQ creation failed\n");
		goto out_free_ad;
	}

	tasklet_setup(&adapter->neq_tasklet, ehea_neq_tasklet);

	ret = ehea_create_device_sysfs(dev);
	if (ret)
		goto out_kill_eq;

	ret = ehea_setup_ports(adapter);
	if (ret) {
		dev_err(&dev->dev, "setup_ports failed\n");
		goto out_rem_dev_sysfs;
	}

	ret = ibmebus_request_irq(adapter->neq->attr.ist1,
				  ehea_interrupt_neq, 0,
				  "ehea_neq", adapter);
	if (ret) {
		dev_err(&dev->dev, "requesting NEQ IRQ failed\n");
		goto out_shutdown_ports;
	}

	/* Handle any events that might be pending. */
	tasklet_hi_schedule(&adapter->neq_tasklet);

	ret = 0;
	goto out;

out_shutdown_ports:
	for (i = 0; i < EHEA_MAX_PORTS; i++)
		if (adapter->port[i]) {
			ehea_shutdown_single_port(adapter->port[i]);
			adapter->port[i] = NULL;
		}

out_rem_dev_sysfs:
	ehea_remove_device_sysfs(dev);

out_kill_eq:
	ehea_destroy_eq(adapter->neq);

out_free_ad:
	list_del(&adapter->list);

out:
	ehea_update_firmware_handles();

	return ret;
}

static int ehea_remove(struct platform_device *dev)
{
	struct ehea_adapter *adapter = platform_get_drvdata(dev);
	int i;

	for (i = 0; i < EHEA_MAX_PORTS; i++)
		if (adapter->port[i]) {
			ehea_shutdown_single_port(adapter->port[i]);
			adapter->port[i] = NULL;
		}

	ehea_remove_device_sysfs(dev);

	ibmebus_free_irq(adapter->neq->attr.ist1, adapter);
	tasklet_kill(&adapter->neq_tasklet);

	ehea_destroy_eq(adapter->neq);
	ehea_remove_adapter_mr(adapter);
	list_del(&adapter->list);

	ehea_update_firmware_handles();

	return 0;
}

static int check_module_parm(void)
{
	int ret = 0;

	if ((rq1_entries < EHEA_MIN_ENTRIES_QP) ||
	    (rq1_entries > EHEA_MAX_ENTRIES_RQ1)) {
		pr_info("Bad parameter: rq1_entries\n");
		ret = -EINVAL;
	}
	if ((rq2_entries < EHEA_MIN_ENTRIES_QP) ||
	    (rq2_entries > EHEA_MAX_ENTRIES_RQ2)) {
		pr_info("Bad parameter: rq2_entries\n");
		ret = -EINVAL;
	}
	if ((rq3_entries < EHEA_MIN_ENTRIES_QP) ||
	    (rq3_entries > EHEA_MAX_ENTRIES_RQ3)) {
		pr_info("Bad parameter: rq3_entries\n");
		ret = -EINVAL;
	}
	if ((sq_entries < EHEA_MIN_ENTRIES_QP) ||
	    (sq_entries > EHEA_MAX_ENTRIES_SQ)) {
		pr_info("Bad parameter: sq_entries\n");
		ret = -EINVAL;
	}

	return ret;
}

static ssize_t capabilities_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "%d", EHEA_CAPABILITIES);
}

static DRIVER_ATTR_RO(capabilities);

static int __init ehea_module_init(void)
{
	int ret;

	pr_info("IBM eHEA ethernet device driver (Release %s)\n", DRV_VERSION);

	memset(&ehea_fw_handles, 0, sizeof(ehea_fw_handles));
	memset(&ehea_bcmc_regs, 0, sizeof(ehea_bcmc_regs));

	mutex_init(&ehea_fw_handles.lock);
	spin_lock_init(&ehea_bcmc_regs.lock);

	ret = check_module_parm();
	if (ret)
		goto out;

	ret = ibmebus_register_driver(&ehea_driver);
	if (ret) {
		pr_err("failed registering eHEA device driver on ebus\n");
		goto out;
	}

	ret = driver_create_file(&ehea_driver.driver,
				 &driver_attr_capabilities);
	if (ret) {
		pr_err("failed to register capabilities attribute, ret=%d\n",
		       ret);
		goto out2;
	}

	return ret;

out2:
	ibmebus_unregister_driver(&ehea_driver);
out:
	return ret;
}

static void __exit ehea_module_exit(void)
{
	driver_remove_file(&ehea_driver.driver, &driver_attr_capabilities);
	ibmebus_unregister_driver(&ehea_driver);
	ehea_unregister_memory_hooks();
	kfree(ehea_fw_handles.arr);
	kfree(ehea_bcmc_regs.arr);
	ehea_destroy_busmap();
}

module_init(ehea_module_init);
module_exit(ehea_module_exit);
