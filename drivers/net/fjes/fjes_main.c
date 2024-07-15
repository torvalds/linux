// SPDX-License-Identifier: GPL-2.0-only
/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015 FUJITSU LIMITED
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/nls.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>

#include "fjes.h"
#include "fjes_trace.h"

#define MAJ 1
#define MIN 2
#define DRV_VERSION __stringify(MAJ) "." __stringify(MIN)
#define DRV_NAME	"fjes"
char fjes_driver_name[] = DRV_NAME;
char fjes_driver_version[] = DRV_VERSION;
static const char fjes_driver_string[] =
		"FUJITSU Extended Socket Network Device Driver";
static const char fjes_copyright[] =
		"Copyright (c) 2015 FUJITSU LIMITED";

MODULE_AUTHOR("Taku Izumi <izumi.taku@jp.fujitsu.com>");
MODULE_DESCRIPTION("FUJITSU Extended Socket Network Device Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

#define ACPI_MOTHERBOARD_RESOURCE_HID "PNP0C02"

static const struct acpi_device_id fjes_acpi_ids[] = {
	{ACPI_MOTHERBOARD_RESOURCE_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, fjes_acpi_ids);

static bool is_extended_socket_device(struct acpi_device *device)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};
	char str_buf[sizeof(FJES_ACPI_SYMBOL) + 1];
	union acpi_object *str;
	acpi_status status;
	int result;

	status = acpi_evaluate_object(device->handle, "_STR", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return false;

	str = buffer.pointer;
	result = utf16s_to_utf8s((wchar_t *)str->string.pointer,
				 str->string.length, UTF16_LITTLE_ENDIAN,
				 str_buf, sizeof(str_buf) - 1);
	str_buf[result] = 0;

	if (strncmp(FJES_ACPI_SYMBOL, str_buf, strlen(FJES_ACPI_SYMBOL)) != 0) {
		kfree(buffer.pointer);
		return false;
	}
	kfree(buffer.pointer);

	return true;
}

static int acpi_check_extended_socket_status(struct acpi_device *device)
{
	unsigned long long sta;
	acpi_status status;

	status = acpi_evaluate_integer(device->handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if (!((sta & ACPI_STA_DEVICE_PRESENT) &&
	      (sta & ACPI_STA_DEVICE_ENABLED) &&
	      (sta & ACPI_STA_DEVICE_UI) &&
	      (sta & ACPI_STA_DEVICE_FUNCTIONING)))
		return -ENODEV;

	return 0;
}

static acpi_status
fjes_get_acpi_resource(struct acpi_resource *acpi_res, void *data)
{
	struct acpi_resource_address32 *addr;
	struct acpi_resource_irq *irq;
	struct resource *res = data;

	switch (acpi_res->type) {
	case ACPI_RESOURCE_TYPE_ADDRESS32:
		addr = &acpi_res->data.address32;
		res[0].start = addr->address.minimum;
		res[0].end = addr->address.minimum +
			addr->address.address_length - 1;
		break;

	case ACPI_RESOURCE_TYPE_IRQ:
		irq = &acpi_res->data.irq;
		if (irq->interrupt_count != 1)
			return AE_ERROR;
		res[1].start = irq->interrupts[0];
		res[1].end = irq->interrupts[0];
		break;

	default:
		break;
	}

	return AE_OK;
}

static struct resource fjes_resource[] = {
	DEFINE_RES_MEM(0, 1),
	DEFINE_RES_IRQ(0)
};

static int fjes_acpi_add(struct acpi_device *device)
{
	struct platform_device *plat_dev;
	acpi_status status;

	if (!is_extended_socket_device(device))
		return -ENODEV;

	if (acpi_check_extended_socket_status(device))
		return -ENODEV;

	status = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				     fjes_get_acpi_resource, fjes_resource);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	/* create platform_device */
	plat_dev = platform_device_register_simple(DRV_NAME, 0, fjes_resource,
						   ARRAY_SIZE(fjes_resource));
	if (IS_ERR(plat_dev))
		return PTR_ERR(plat_dev);

	device->driver_data = plat_dev;

	return 0;
}

static void fjes_acpi_remove(struct acpi_device *device)
{
	struct platform_device *plat_dev;

	plat_dev = (struct platform_device *)acpi_driver_data(device);
	platform_device_unregister(plat_dev);
}

static struct acpi_driver fjes_acpi_driver = {
	.name = DRV_NAME,
	.class = DRV_NAME,
	.ids = fjes_acpi_ids,
	.ops = {
		.add = fjes_acpi_add,
		.remove = fjes_acpi_remove,
	},
};

static int fjes_setup_resources(struct fjes_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ep_share_mem_info *buf_pair;
	struct fjes_hw *hw = &adapter->hw;
	unsigned long flags;
	int result;
	int epidx;

	mutex_lock(&hw->hw_info.lock);
	result = fjes_hw_request_info(hw);
	switch (result) {
	case 0:
		for (epidx = 0; epidx < hw->max_epid; epidx++) {
			hw->ep_shm_info[epidx].es_status =
			    hw->hw_info.res_buf->info.info[epidx].es_status;
			hw->ep_shm_info[epidx].zone =
			    hw->hw_info.res_buf->info.info[epidx].zone;
		}
		break;
	default:
	case -ENOMSG:
	case -EBUSY:
		adapter->force_reset = true;

		mutex_unlock(&hw->hw_info.lock);
		return result;
	}
	mutex_unlock(&hw->hw_info.lock);

	for (epidx = 0; epidx < (hw->max_epid); epidx++) {
		if ((epidx != hw->my_epid) &&
		    (hw->ep_shm_info[epidx].es_status ==
		     FJES_ZONING_STATUS_ENABLE)) {
			fjes_hw_raise_interrupt(hw, epidx,
						REG_ICTL_MASK_INFO_UPDATE);
			hw->ep_shm_info[epidx].ep_stats
				.send_intr_zoneupdate += 1;
		}
	}

	msleep(FJES_OPEN_ZONE_UPDATE_WAIT * hw->max_epid);

	for (epidx = 0; epidx < (hw->max_epid); epidx++) {
		if (epidx == hw->my_epid)
			continue;

		buf_pair = &hw->ep_shm_info[epidx];

		spin_lock_irqsave(&hw->rx_status_lock, flags);
		fjes_hw_setup_epbuf(&buf_pair->tx, netdev->dev_addr,
				    netdev->mtu);
		spin_unlock_irqrestore(&hw->rx_status_lock, flags);

		if (fjes_hw_epid_is_same_zone(hw, epidx)) {
			mutex_lock(&hw->hw_info.lock);
			result =
			fjes_hw_register_buff_addr(hw, epidx, buf_pair);
			mutex_unlock(&hw->hw_info.lock);

			switch (result) {
			case 0:
				break;
			case -ENOMSG:
			case -EBUSY:
			default:
				adapter->force_reset = true;
				return result;
			}

			hw->ep_shm_info[epidx].ep_stats
				.com_regist_buf_exec += 1;
		}
	}

	return 0;
}

static void fjes_rx_irq(struct fjes_adapter *adapter, int src_epid)
{
	struct fjes_hw *hw = &adapter->hw;

	fjes_hw_set_irqmask(hw, REG_ICTL_MASK_RX_DATA, true);

	adapter->unset_rx_last = true;
	napi_schedule(&adapter->napi);
}

static void fjes_stop_req_irq(struct fjes_adapter *adapter, int src_epid)
{
	struct fjes_hw *hw = &adapter->hw;
	enum ep_partner_status status;
	unsigned long flags;

	set_bit(src_epid, &hw->hw_info.buffer_unshare_reserve_bit);

	status = fjes_hw_get_partner_ep_status(hw, src_epid);
	trace_fjes_stop_req_irq_pre(hw, src_epid, status);
	switch (status) {
	case EP_PARTNER_WAITING:
		spin_lock_irqsave(&hw->rx_status_lock, flags);
		hw->ep_shm_info[src_epid].tx.info->v1i.rx_status |=
				FJES_RX_STOP_REQ_DONE;
		spin_unlock_irqrestore(&hw->rx_status_lock, flags);
		clear_bit(src_epid, &hw->txrx_stop_req_bit);
		fallthrough;
	case EP_PARTNER_UNSHARE:
	case EP_PARTNER_COMPLETE:
	default:
		set_bit(src_epid, &adapter->unshare_watch_bitmask);
		if (!work_pending(&adapter->unshare_watch_task))
			queue_work(adapter->control_wq,
				   &adapter->unshare_watch_task);
		break;
	case EP_PARTNER_SHARED:
		set_bit(src_epid, &hw->epstop_req_bit);

		if (!work_pending(&hw->epstop_task))
			queue_work(adapter->control_wq, &hw->epstop_task);
		break;
	}
	trace_fjes_stop_req_irq_post(hw, src_epid);
}

static void fjes_txrx_stop_req_irq(struct fjes_adapter *adapter,
				   int src_epid)
{
	struct fjes_hw *hw = &adapter->hw;
	enum ep_partner_status status;
	unsigned long flags;

	status = fjes_hw_get_partner_ep_status(hw, src_epid);
	trace_fjes_txrx_stop_req_irq_pre(hw, src_epid, status);
	switch (status) {
	case EP_PARTNER_UNSHARE:
	case EP_PARTNER_COMPLETE:
	default:
		break;
	case EP_PARTNER_WAITING:
		if (src_epid < hw->my_epid) {
			spin_lock_irqsave(&hw->rx_status_lock, flags);
			hw->ep_shm_info[src_epid].tx.info->v1i.rx_status |=
				FJES_RX_STOP_REQ_DONE;
			spin_unlock_irqrestore(&hw->rx_status_lock, flags);

			clear_bit(src_epid, &hw->txrx_stop_req_bit);
			set_bit(src_epid, &adapter->unshare_watch_bitmask);

			if (!work_pending(&adapter->unshare_watch_task))
				queue_work(adapter->control_wq,
					   &adapter->unshare_watch_task);
		}
		break;
	case EP_PARTNER_SHARED:
		if (hw->ep_shm_info[src_epid].rx.info->v1i.rx_status &
		    FJES_RX_STOP_REQ_REQUEST) {
			set_bit(src_epid, &hw->epstop_req_bit);
			if (!work_pending(&hw->epstop_task))
				queue_work(adapter->control_wq,
					   &hw->epstop_task);
		}
		break;
	}
	trace_fjes_txrx_stop_req_irq_post(hw, src_epid);
}

static void fjes_update_zone_irq(struct fjes_adapter *adapter,
				 int src_epid)
{
	struct fjes_hw *hw = &adapter->hw;

	if (!work_pending(&hw->update_zone_task))
		queue_work(adapter->control_wq, &hw->update_zone_task);
}

static irqreturn_t fjes_intr(int irq, void *data)
{
	struct fjes_adapter *adapter = data;
	struct fjes_hw *hw = &adapter->hw;
	irqreturn_t ret;
	u32 icr;

	icr = fjes_hw_capture_interrupt_status(hw);

	if (icr & REG_IS_MASK_IS_ASSERT) {
		if (icr & REG_ICTL_MASK_RX_DATA) {
			fjes_rx_irq(adapter, icr & REG_IS_MASK_EPID);
			hw->ep_shm_info[icr & REG_IS_MASK_EPID].ep_stats
				.recv_intr_rx += 1;
		}

		if (icr & REG_ICTL_MASK_DEV_STOP_REQ) {
			fjes_stop_req_irq(adapter, icr & REG_IS_MASK_EPID);
			hw->ep_shm_info[icr & REG_IS_MASK_EPID].ep_stats
				.recv_intr_stop += 1;
		}

		if (icr & REG_ICTL_MASK_TXRX_STOP_REQ) {
			fjes_txrx_stop_req_irq(adapter, icr & REG_IS_MASK_EPID);
			hw->ep_shm_info[icr & REG_IS_MASK_EPID].ep_stats
				.recv_intr_unshare += 1;
		}

		if (icr & REG_ICTL_MASK_TXRX_STOP_DONE)
			fjes_hw_set_irqmask(hw,
					    REG_ICTL_MASK_TXRX_STOP_DONE, true);

		if (icr & REG_ICTL_MASK_INFO_UPDATE) {
			fjes_update_zone_irq(adapter, icr & REG_IS_MASK_EPID);
			hw->ep_shm_info[icr & REG_IS_MASK_EPID].ep_stats
				.recv_intr_zoneupdate += 1;
		}

		ret = IRQ_HANDLED;
	} else {
		ret = IRQ_NONE;
	}

	return ret;
}

static int fjes_request_irq(struct fjes_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int result = -1;

	adapter->interrupt_watch_enable = true;
	if (!delayed_work_pending(&adapter->interrupt_watch_task)) {
		queue_delayed_work(adapter->control_wq,
				   &adapter->interrupt_watch_task,
				   FJES_IRQ_WATCH_DELAY);
	}

	if (!adapter->irq_registered) {
		result = request_irq(adapter->hw.hw_res.irq, fjes_intr,
				     IRQF_SHARED, netdev->name, adapter);
		if (result)
			adapter->irq_registered = false;
		else
			adapter->irq_registered = true;
	}

	return result;
}

static void fjes_free_irq(struct fjes_adapter *adapter)
{
	struct fjes_hw *hw = &adapter->hw;

	adapter->interrupt_watch_enable = false;
	cancel_delayed_work_sync(&adapter->interrupt_watch_task);

	fjes_hw_set_irqmask(hw, REG_ICTL_MASK_ALL, true);

	if (adapter->irq_registered) {
		free_irq(adapter->hw.hw_res.irq, adapter);
		adapter->irq_registered = false;
	}
}

static void fjes_free_resources(struct fjes_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct fjes_device_command_param param;
	struct ep_share_mem_info *buf_pair;
	struct fjes_hw *hw = &adapter->hw;
	bool reset_flag = false;
	unsigned long flags;
	int result;
	int epidx;

	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;

		mutex_lock(&hw->hw_info.lock);
		result = fjes_hw_unregister_buff_addr(hw, epidx);
		mutex_unlock(&hw->hw_info.lock);

		hw->ep_shm_info[epidx].ep_stats.com_unregist_buf_exec += 1;

		if (result)
			reset_flag = true;

		buf_pair = &hw->ep_shm_info[epidx];

		spin_lock_irqsave(&hw->rx_status_lock, flags);
		fjes_hw_setup_epbuf(&buf_pair->tx,
				    netdev->dev_addr, netdev->mtu);
		spin_unlock_irqrestore(&hw->rx_status_lock, flags);

		clear_bit(epidx, &hw->txrx_stop_req_bit);
	}

	if (reset_flag || adapter->force_reset) {
		result = fjes_hw_reset(hw);

		adapter->force_reset = false;

		if (result)
			adapter->open_guard = true;

		hw->hw_info.buffer_share_bit = 0;

		memset((void *)&param, 0, sizeof(param));

		param.req_len = hw->hw_info.req_buf_size;
		param.req_start = __pa(hw->hw_info.req_buf);
		param.res_len = hw->hw_info.res_buf_size;
		param.res_start = __pa(hw->hw_info.res_buf);
		param.share_start = __pa(hw->hw_info.share->ep_status);

		fjes_hw_init_command_registers(hw, &param);
	}
}

/* fjes_open - Called when a network interface is made active */
static int fjes_open(struct net_device *netdev)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	int result;

	if (adapter->open_guard)
		return -ENXIO;

	result = fjes_setup_resources(adapter);
	if (result)
		goto err_setup_res;

	hw->txrx_stop_req_bit = 0;
	hw->epstop_req_bit = 0;

	napi_enable(&adapter->napi);

	fjes_hw_capture_interrupt_status(hw);

	result = fjes_request_irq(adapter);
	if (result)
		goto err_req_irq;

	fjes_hw_set_irqmask(hw, REG_ICTL_MASK_ALL, false);

	netif_tx_start_all_queues(netdev);
	netif_carrier_on(netdev);

	return 0;

err_req_irq:
	fjes_free_irq(adapter);
	napi_disable(&adapter->napi);

err_setup_res:
	fjes_free_resources(adapter);
	return result;
}

/* fjes_close - Disables a network interface */
static int fjes_close(struct net_device *netdev)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	unsigned long flags;
	int epidx;

	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);

	fjes_hw_raise_epstop(hw);

	napi_disable(&adapter->napi);

	spin_lock_irqsave(&hw->rx_status_lock, flags);
	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;

		if (fjes_hw_get_partner_ep_status(hw, epidx) ==
		    EP_PARTNER_SHARED)
			adapter->hw.ep_shm_info[epidx]
				   .tx.info->v1i.rx_status &=
				~FJES_RX_POLL_WORK;
	}
	spin_unlock_irqrestore(&hw->rx_status_lock, flags);

	fjes_free_irq(adapter);

	cancel_delayed_work_sync(&adapter->interrupt_watch_task);
	cancel_work_sync(&adapter->unshare_watch_task);
	adapter->unshare_watch_bitmask = 0;
	cancel_work_sync(&adapter->raise_intr_rxdata_task);
	cancel_work_sync(&adapter->tx_stall_task);

	cancel_work_sync(&hw->update_zone_task);
	cancel_work_sync(&hw->epstop_task);

	fjes_hw_wait_epstop(hw);

	fjes_free_resources(adapter);

	return 0;
}

static int fjes_tx_send(struct fjes_adapter *adapter, int dest,
			void *data, size_t len)
{
	int retval;

	retval = fjes_hw_epbuf_tx_pkt_send(&adapter->hw.ep_shm_info[dest].tx,
					   data, len);
	if (retval)
		return retval;

	adapter->hw.ep_shm_info[dest].tx.info->v1i.tx_status =
		FJES_TX_DELAY_SEND_PENDING;
	if (!work_pending(&adapter->raise_intr_rxdata_task))
		queue_work(adapter->txrx_wq,
			   &adapter->raise_intr_rxdata_task);

	retval = 0;
	return retval;
}

static netdev_tx_t
fjes_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;

	int max_epid, my_epid, dest_epid;
	enum ep_partner_status pstatus;
	struct netdev_queue *cur_queue;
	char shortpkt[VLAN_ETH_HLEN];
	bool is_multi, vlan;
	struct ethhdr *eth;
	u16 queue_no = 0;
	u16 vlan_id = 0;
	netdev_tx_t ret;
	char *data;
	int len;

	ret = NETDEV_TX_OK;
	is_multi = false;
	cur_queue = netdev_get_tx_queue(netdev, queue_no);

	eth = (struct ethhdr *)skb->data;
	my_epid = hw->my_epid;

	vlan = (vlan_get_tag(skb, &vlan_id) == 0) ? true : false;

	data = skb->data;
	len = skb->len;

	if (is_multicast_ether_addr(eth->h_dest)) {
		dest_epid = 0;
		max_epid = hw->max_epid;
		is_multi = true;
	} else if (is_local_ether_addr(eth->h_dest)) {
		dest_epid = eth->h_dest[ETH_ALEN - 1];
		max_epid = dest_epid + 1;

		if ((eth->h_dest[0] == 0x02) &&
		    (0x00 == (eth->h_dest[1] | eth->h_dest[2] |
			      eth->h_dest[3] | eth->h_dest[4])) &&
		    (dest_epid < hw->max_epid)) {
			;
		} else {
			dest_epid = 0;
			max_epid = 0;
			ret = NETDEV_TX_OK;

			adapter->stats64.tx_packets += 1;
			hw->ep_shm_info[my_epid].net_stats.tx_packets += 1;
			adapter->stats64.tx_bytes += len;
			hw->ep_shm_info[my_epid].net_stats.tx_bytes += len;
		}
	} else {
		dest_epid = 0;
		max_epid = 0;
		ret = NETDEV_TX_OK;

		adapter->stats64.tx_packets += 1;
		hw->ep_shm_info[my_epid].net_stats.tx_packets += 1;
		adapter->stats64.tx_bytes += len;
		hw->ep_shm_info[my_epid].net_stats.tx_bytes += len;
	}

	for (; dest_epid < max_epid; dest_epid++) {
		if (my_epid == dest_epid)
			continue;

		pstatus = fjes_hw_get_partner_ep_status(hw, dest_epid);
		if (pstatus != EP_PARTNER_SHARED) {
			if (!is_multi)
				hw->ep_shm_info[dest_epid].ep_stats
					.tx_dropped_not_shared += 1;
			ret = NETDEV_TX_OK;
		} else if (!fjes_hw_check_epbuf_version(
				&adapter->hw.ep_shm_info[dest_epid].rx, 0)) {
			/* version is NOT 0 */
			adapter->stats64.tx_carrier_errors += 1;
			hw->ep_shm_info[dest_epid].net_stats
						.tx_carrier_errors += 1;
			hw->ep_shm_info[dest_epid].ep_stats
					.tx_dropped_ver_mismatch += 1;

			ret = NETDEV_TX_OK;
		} else if (!fjes_hw_check_mtu(
				&adapter->hw.ep_shm_info[dest_epid].rx,
				netdev->mtu)) {
			adapter->stats64.tx_dropped += 1;
			hw->ep_shm_info[dest_epid].net_stats.tx_dropped += 1;
			adapter->stats64.tx_errors += 1;
			hw->ep_shm_info[dest_epid].net_stats.tx_errors += 1;
			hw->ep_shm_info[dest_epid].ep_stats
					.tx_dropped_buf_size_mismatch += 1;

			ret = NETDEV_TX_OK;
		} else if (vlan &&
			   !fjes_hw_check_vlan_id(
				&adapter->hw.ep_shm_info[dest_epid].rx,
				vlan_id)) {
			hw->ep_shm_info[dest_epid].ep_stats
				.tx_dropped_vlanid_mismatch += 1;
			ret = NETDEV_TX_OK;
		} else {
			if (len < VLAN_ETH_HLEN) {
				memset(shortpkt, 0, VLAN_ETH_HLEN);
				memcpy(shortpkt, skb->data, skb->len);
				len = VLAN_ETH_HLEN;
				data = shortpkt;
			}

			if (adapter->tx_retry_count == 0) {
				adapter->tx_start_jiffies = jiffies;
				adapter->tx_retry_count = 1;
			} else {
				adapter->tx_retry_count++;
			}

			if (fjes_tx_send(adapter, dest_epid, data, len)) {
				if (is_multi) {
					ret = NETDEV_TX_OK;
				} else if (
					   ((long)jiffies -
					    (long)adapter->tx_start_jiffies) >=
					    FJES_TX_RETRY_TIMEOUT) {
					adapter->stats64.tx_fifo_errors += 1;
					hw->ep_shm_info[dest_epid].net_stats
								.tx_fifo_errors += 1;
					adapter->stats64.tx_errors += 1;
					hw->ep_shm_info[dest_epid].net_stats
								.tx_errors += 1;

					ret = NETDEV_TX_OK;
				} else {
					netif_trans_update(netdev);
					hw->ep_shm_info[dest_epid].ep_stats
						.tx_buffer_full += 1;
					netif_tx_stop_queue(cur_queue);

					if (!work_pending(&adapter->tx_stall_task))
						queue_work(adapter->txrx_wq,
							   &adapter->tx_stall_task);

					ret = NETDEV_TX_BUSY;
				}
			} else {
				if (!is_multi) {
					adapter->stats64.tx_packets += 1;
					hw->ep_shm_info[dest_epid].net_stats
								.tx_packets += 1;
					adapter->stats64.tx_bytes += len;
					hw->ep_shm_info[dest_epid].net_stats
								.tx_bytes += len;
				}

				adapter->tx_retry_count = 0;
				ret = NETDEV_TX_OK;
			}
		}
	}

	if (ret == NETDEV_TX_OK) {
		dev_kfree_skb(skb);
		if (is_multi) {
			adapter->stats64.tx_packets += 1;
			hw->ep_shm_info[my_epid].net_stats.tx_packets += 1;
			adapter->stats64.tx_bytes += 1;
			hw->ep_shm_info[my_epid].net_stats.tx_bytes += len;
		}
	}

	return ret;
}

static void
fjes_get_stats64(struct net_device *netdev, struct rtnl_link_stats64 *stats)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);

	memcpy(stats, &adapter->stats64, sizeof(struct rtnl_link_stats64));
}

static int fjes_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	bool running = netif_running(netdev);
	struct fjes_hw *hw = &adapter->hw;
	unsigned long flags;
	int ret = -EINVAL;
	int idx, epidx;

	for (idx = 0; fjes_support_mtu[idx] != 0; idx++) {
		if (new_mtu <= fjes_support_mtu[idx]) {
			new_mtu = fjes_support_mtu[idx];
			if (new_mtu == netdev->mtu)
				return 0;

			ret = 0;
			break;
		}
	}

	if (ret)
		return ret;

	if (running) {
		spin_lock_irqsave(&hw->rx_status_lock, flags);
		for (epidx = 0; epidx < hw->max_epid; epidx++) {
			if (epidx == hw->my_epid)
				continue;
			hw->ep_shm_info[epidx].tx.info->v1i.rx_status &=
				~FJES_RX_MTU_CHANGING_DONE;
		}
		spin_unlock_irqrestore(&hw->rx_status_lock, flags);

		netif_tx_stop_all_queues(netdev);
		netif_carrier_off(netdev);
		cancel_work_sync(&adapter->tx_stall_task);
		napi_disable(&adapter->napi);

		msleep(1000);

		netif_tx_stop_all_queues(netdev);
	}

	WRITE_ONCE(netdev->mtu, new_mtu);

	if (running) {
		for (epidx = 0; epidx < hw->max_epid; epidx++) {
			if (epidx == hw->my_epid)
				continue;

			spin_lock_irqsave(&hw->rx_status_lock, flags);
			fjes_hw_setup_epbuf(&hw->ep_shm_info[epidx].tx,
					    netdev->dev_addr,
					    netdev->mtu);

			hw->ep_shm_info[epidx].tx.info->v1i.rx_status |=
				FJES_RX_MTU_CHANGING_DONE;
			spin_unlock_irqrestore(&hw->rx_status_lock, flags);
		}

		netif_tx_wake_all_queues(netdev);
		netif_carrier_on(netdev);
		napi_enable(&adapter->napi);
		napi_schedule(&adapter->napi);
	}

	return ret;
}

static void fjes_tx_retry(struct net_device *netdev, unsigned int txqueue)
{
	struct netdev_queue *queue = netdev_get_tx_queue(netdev, 0);

	netif_tx_wake_queue(queue);
}

static int fjes_vlan_rx_add_vid(struct net_device *netdev,
				__be16 proto, u16 vid)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	bool ret = true;
	int epid;

	for (epid = 0; epid < adapter->hw.max_epid; epid++) {
		if (epid == adapter->hw.my_epid)
			continue;

		if (!fjes_hw_check_vlan_id(
			&adapter->hw.ep_shm_info[epid].tx, vid))
			ret = fjes_hw_set_vlan_id(
				&adapter->hw.ep_shm_info[epid].tx, vid);
	}

	return ret ? 0 : -ENOSPC;
}

static int fjes_vlan_rx_kill_vid(struct net_device *netdev,
				 __be16 proto, u16 vid)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	int epid;

	for (epid = 0; epid < adapter->hw.max_epid; epid++) {
		if (epid == adapter->hw.my_epid)
			continue;

		fjes_hw_del_vlan_id(&adapter->hw.ep_shm_info[epid].tx, vid);
	}

	return 0;
}

static const struct net_device_ops fjes_netdev_ops = {
	.ndo_open		= fjes_open,
	.ndo_stop		= fjes_close,
	.ndo_start_xmit		= fjes_xmit_frame,
	.ndo_get_stats64	= fjes_get_stats64,
	.ndo_change_mtu		= fjes_change_mtu,
	.ndo_tx_timeout		= fjes_tx_retry,
	.ndo_vlan_rx_add_vid	= fjes_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = fjes_vlan_rx_kill_vid,
};

/* fjes_netdev_setup - netdevice initialization routine */
static void fjes_netdev_setup(struct net_device *netdev)
{
	ether_setup(netdev);

	netdev->watchdog_timeo = FJES_TX_RETRY_INTERVAL;
	netdev->netdev_ops = &fjes_netdev_ops;
	fjes_set_ethtool_ops(netdev);
	netdev->mtu = fjes_support_mtu[3];
	netdev->min_mtu = fjes_support_mtu[0];
	netdev->max_mtu = fjes_support_mtu[3];
	netdev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
}

static int fjes_rxframe_search_exist(struct fjes_adapter *adapter,
				     int start_epid)
{
	struct fjes_hw *hw = &adapter->hw;
	enum ep_partner_status pstatus;
	int max_epid, cur_epid;
	int i;

	max_epid = hw->max_epid;
	start_epid = (start_epid + 1 + max_epid) % max_epid;

	for (i = 0; i < max_epid; i++) {
		cur_epid = (start_epid + i) % max_epid;
		if (cur_epid == hw->my_epid)
			continue;

		pstatus = fjes_hw_get_partner_ep_status(hw, cur_epid);
		if (pstatus == EP_PARTNER_SHARED) {
			if (!fjes_hw_epbuf_rx_is_empty(
				&hw->ep_shm_info[cur_epid].rx))
				return cur_epid;
		}
	}
	return -1;
}

static void *fjes_rxframe_get(struct fjes_adapter *adapter, size_t *psize,
			      int *cur_epid)
{
	void *frame;

	*cur_epid = fjes_rxframe_search_exist(adapter, *cur_epid);
	if (*cur_epid < 0)
		return NULL;

	frame =
	fjes_hw_epbuf_rx_curpkt_get_addr(
		&adapter->hw.ep_shm_info[*cur_epid].rx, psize);

	return frame;
}

static void fjes_rxframe_release(struct fjes_adapter *adapter, int cur_epid)
{
	fjes_hw_epbuf_rx_curpkt_drop(&adapter->hw.ep_shm_info[cur_epid].rx);
}

static int fjes_poll(struct napi_struct *napi, int budget)
{
	struct fjes_adapter *adapter =
			container_of(napi, struct fjes_adapter, napi);
	struct net_device *netdev = napi->dev;
	struct fjes_hw *hw = &adapter->hw;
	struct sk_buff *skb;
	int work_done = 0;
	int cur_epid = 0;
	int epidx;
	size_t frame_len;
	void *frame;

	spin_lock(&hw->rx_status_lock);
	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;

		if (fjes_hw_get_partner_ep_status(hw, epidx) ==
		    EP_PARTNER_SHARED)
			adapter->hw.ep_shm_info[epidx]
				   .tx.info->v1i.rx_status |= FJES_RX_POLL_WORK;
	}
	spin_unlock(&hw->rx_status_lock);

	while (work_done < budget) {
		prefetch(&adapter->hw);
		frame = fjes_rxframe_get(adapter, &frame_len, &cur_epid);

		if (frame) {
			skb = napi_alloc_skb(napi, frame_len);
			if (!skb) {
				adapter->stats64.rx_dropped += 1;
				hw->ep_shm_info[cur_epid].net_stats
							 .rx_dropped += 1;
				adapter->stats64.rx_errors += 1;
				hw->ep_shm_info[cur_epid].net_stats
							 .rx_errors += 1;
			} else {
				skb_put_data(skb, frame, frame_len);
				skb->protocol = eth_type_trans(skb, netdev);
				skb->ip_summed = CHECKSUM_UNNECESSARY;

				netif_receive_skb(skb);

				work_done++;

				adapter->stats64.rx_packets += 1;
				hw->ep_shm_info[cur_epid].net_stats
							 .rx_packets += 1;
				adapter->stats64.rx_bytes += frame_len;
				hw->ep_shm_info[cur_epid].net_stats
							 .rx_bytes += frame_len;

				if (is_multicast_ether_addr(
					((struct ethhdr *)frame)->h_dest)) {
					adapter->stats64.multicast += 1;
					hw->ep_shm_info[cur_epid].net_stats
								 .multicast += 1;
				}
			}

			fjes_rxframe_release(adapter, cur_epid);
			adapter->unset_rx_last = true;
		} else {
			break;
		}
	}

	if (work_done < budget) {
		napi_complete_done(napi, work_done);

		if (adapter->unset_rx_last) {
			adapter->rx_last_jiffies = jiffies;
			adapter->unset_rx_last = false;
		}

		if (((long)jiffies - (long)adapter->rx_last_jiffies) < 3) {
			napi_schedule(napi);
		} else {
			spin_lock(&hw->rx_status_lock);
			for (epidx = 0; epidx < hw->max_epid; epidx++) {
				if (epidx == hw->my_epid)
					continue;
				if (fjes_hw_get_partner_ep_status(hw, epidx) ==
				    EP_PARTNER_SHARED)
					adapter->hw.ep_shm_info[epidx].tx
						   .info->v1i.rx_status &=
						~FJES_RX_POLL_WORK;
			}
			spin_unlock(&hw->rx_status_lock);

			fjes_hw_set_irqmask(hw, REG_ICTL_MASK_RX_DATA, false);
		}
	}

	return work_done;
}

static int fjes_sw_init(struct fjes_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	netif_napi_add(netdev, &adapter->napi, fjes_poll);

	return 0;
}

static void fjes_force_close_task(struct work_struct *work)
{
	struct fjes_adapter *adapter = container_of(work,
			struct fjes_adapter, force_close_task);
	struct net_device *netdev = adapter->netdev;

	rtnl_lock();
	dev_close(netdev);
	rtnl_unlock();
}

static void fjes_tx_stall_task(struct work_struct *work)
{
	struct fjes_adapter *adapter = container_of(work,
			struct fjes_adapter, tx_stall_task);
	struct net_device *netdev = adapter->netdev;
	struct fjes_hw *hw = &adapter->hw;
	int all_queue_available, sendable;
	enum ep_partner_status pstatus;
	int max_epid, my_epid, epid;
	union ep_buffer_info *info;
	int i;

	if (((long)jiffies -
		dev_trans_start(netdev)) > FJES_TX_TX_STALL_TIMEOUT) {
		netif_wake_queue(netdev);
		return;
	}

	my_epid = hw->my_epid;
	max_epid = hw->max_epid;

	for (i = 0; i < 5; i++) {
		all_queue_available = 1;

		for (epid = 0; epid < max_epid; epid++) {
			if (my_epid == epid)
				continue;

			pstatus = fjes_hw_get_partner_ep_status(hw, epid);
			sendable = (pstatus == EP_PARTNER_SHARED);
			if (!sendable)
				continue;

			info = adapter->hw.ep_shm_info[epid].tx.info;

			if (!(info->v1i.rx_status & FJES_RX_MTU_CHANGING_DONE))
				return;

			if (EP_RING_FULL(info->v1i.head, info->v1i.tail,
					 info->v1i.count_max)) {
				all_queue_available = 0;
				break;
			}
		}

		if (all_queue_available) {
			netif_wake_queue(netdev);
			return;
		}
	}

	usleep_range(50, 100);

	queue_work(adapter->txrx_wq, &adapter->tx_stall_task);
}

static void fjes_raise_intr_rxdata_task(struct work_struct *work)
{
	struct fjes_adapter *adapter = container_of(work,
			struct fjes_adapter, raise_intr_rxdata_task);
	struct fjes_hw *hw = &adapter->hw;
	enum ep_partner_status pstatus;
	int max_epid, my_epid, epid;

	my_epid = hw->my_epid;
	max_epid = hw->max_epid;

	for (epid = 0; epid < max_epid; epid++)
		hw->ep_shm_info[epid].tx_status_work = 0;

	for (epid = 0; epid < max_epid; epid++) {
		if (epid == my_epid)
			continue;

		pstatus = fjes_hw_get_partner_ep_status(hw, epid);
		if (pstatus == EP_PARTNER_SHARED) {
			hw->ep_shm_info[epid].tx_status_work =
				hw->ep_shm_info[epid].tx.info->v1i.tx_status;

			if (hw->ep_shm_info[epid].tx_status_work ==
				FJES_TX_DELAY_SEND_PENDING) {
				hw->ep_shm_info[epid].tx.info->v1i.tx_status =
					FJES_TX_DELAY_SEND_NONE;
			}
		}
	}

	for (epid = 0; epid < max_epid; epid++) {
		if (epid == my_epid)
			continue;

		pstatus = fjes_hw_get_partner_ep_status(hw, epid);
		if ((hw->ep_shm_info[epid].tx_status_work ==
		     FJES_TX_DELAY_SEND_PENDING) &&
		    (pstatus == EP_PARTNER_SHARED) &&
		    !(hw->ep_shm_info[epid].rx.info->v1i.rx_status &
		      FJES_RX_POLL_WORK)) {
			fjes_hw_raise_interrupt(hw, epid,
						REG_ICTL_MASK_RX_DATA);
			hw->ep_shm_info[epid].ep_stats.send_intr_rx += 1;
		}
	}

	usleep_range(500, 1000);
}

static void fjes_watch_unshare_task(struct work_struct *work)
{
	struct fjes_adapter *adapter =
	container_of(work, struct fjes_adapter, unshare_watch_task);

	struct net_device *netdev = adapter->netdev;
	struct fjes_hw *hw = &adapter->hw;

	int unshare_watch, unshare_reserve;
	int max_epid, my_epid, epidx;
	int stop_req, stop_req_done;
	ulong unshare_watch_bitmask;
	unsigned long flags;
	int wait_time = 0;
	int is_shared;
	int ret;

	my_epid = hw->my_epid;
	max_epid = hw->max_epid;

	unshare_watch_bitmask = adapter->unshare_watch_bitmask;
	adapter->unshare_watch_bitmask = 0;

	while ((unshare_watch_bitmask || hw->txrx_stop_req_bit) &&
	       (wait_time < 3000)) {
		for (epidx = 0; epidx < max_epid; epidx++) {
			if (epidx == my_epid)
				continue;

			is_shared = fjes_hw_epid_is_shared(hw->hw_info.share,
							   epidx);

			stop_req = test_bit(epidx, &hw->txrx_stop_req_bit);

			stop_req_done = hw->ep_shm_info[epidx].rx.info->v1i.rx_status &
					FJES_RX_STOP_REQ_DONE;

			unshare_watch = test_bit(epidx, &unshare_watch_bitmask);

			unshare_reserve = test_bit(epidx,
						   &hw->hw_info.buffer_unshare_reserve_bit);

			if ((!stop_req ||
			     (is_shared && (!is_shared || !stop_req_done))) &&
			    (is_shared || !unshare_watch || !unshare_reserve))
				continue;

			mutex_lock(&hw->hw_info.lock);
			ret = fjes_hw_unregister_buff_addr(hw, epidx);
			switch (ret) {
			case 0:
				break;
			case -ENOMSG:
			case -EBUSY:
			default:
				if (!work_pending(
					&adapter->force_close_task)) {
					adapter->force_reset = true;
					schedule_work(
						&adapter->force_close_task);
				}
				break;
			}
			mutex_unlock(&hw->hw_info.lock);
			hw->ep_shm_info[epidx].ep_stats
					.com_unregist_buf_exec += 1;

			spin_lock_irqsave(&hw->rx_status_lock, flags);
			fjes_hw_setup_epbuf(&hw->ep_shm_info[epidx].tx,
					    netdev->dev_addr, netdev->mtu);
			spin_unlock_irqrestore(&hw->rx_status_lock, flags);

			clear_bit(epidx, &hw->txrx_stop_req_bit);
			clear_bit(epidx, &unshare_watch_bitmask);
			clear_bit(epidx,
				  &hw->hw_info.buffer_unshare_reserve_bit);
		}

		msleep(100);
		wait_time += 100;
	}

	if (hw->hw_info.buffer_unshare_reserve_bit) {
		for (epidx = 0; epidx < max_epid; epidx++) {
			if (epidx == my_epid)
				continue;

			if (test_bit(epidx,
				     &hw->hw_info.buffer_unshare_reserve_bit)) {
				mutex_lock(&hw->hw_info.lock);

				ret = fjes_hw_unregister_buff_addr(hw, epidx);
				switch (ret) {
				case 0:
					break;
				case -ENOMSG:
				case -EBUSY:
				default:
					if (!work_pending(
						&adapter->force_close_task)) {
						adapter->force_reset = true;
						schedule_work(
							&adapter->force_close_task);
					}
					break;
				}
				mutex_unlock(&hw->hw_info.lock);

				hw->ep_shm_info[epidx].ep_stats
					.com_unregist_buf_exec += 1;

				spin_lock_irqsave(&hw->rx_status_lock, flags);
				fjes_hw_setup_epbuf(
					&hw->ep_shm_info[epidx].tx,
					netdev->dev_addr, netdev->mtu);
				spin_unlock_irqrestore(&hw->rx_status_lock,
						       flags);

				clear_bit(epidx, &hw->txrx_stop_req_bit);
				clear_bit(epidx, &unshare_watch_bitmask);
				clear_bit(epidx, &hw->hw_info.buffer_unshare_reserve_bit);
			}

			if (test_bit(epidx, &unshare_watch_bitmask)) {
				spin_lock_irqsave(&hw->rx_status_lock, flags);
				hw->ep_shm_info[epidx].tx.info->v1i.rx_status &=
						~FJES_RX_STOP_REQ_DONE;
				spin_unlock_irqrestore(&hw->rx_status_lock,
						       flags);
			}
		}
	}
}

static void fjes_irq_watch_task(struct work_struct *work)
{
	struct fjes_adapter *adapter = container_of(to_delayed_work(work),
			struct fjes_adapter, interrupt_watch_task);

	local_irq_disable();
	fjes_intr(adapter->hw.hw_res.irq, adapter);
	local_irq_enable();

	if (fjes_rxframe_search_exist(adapter, 0) >= 0)
		napi_schedule(&adapter->napi);

	if (adapter->interrupt_watch_enable) {
		if (!delayed_work_pending(&adapter->interrupt_watch_task))
			queue_delayed_work(adapter->control_wq,
					   &adapter->interrupt_watch_task,
					   FJES_IRQ_WATCH_DELAY);
	}
}

/* fjes_probe - Device Initialization Routine */
static int fjes_probe(struct platform_device *plat_dev)
{
	struct fjes_adapter *adapter;
	struct net_device *netdev;
	struct resource *res;
	struct fjes_hw *hw;
	u8 addr[ETH_ALEN];
	int err;

	err = -ENOMEM;
	netdev = alloc_netdev_mq(sizeof(struct fjes_adapter), "es%d",
				 NET_NAME_UNKNOWN, fjes_netdev_setup,
				 FJES_MAX_QUEUES);

	if (!netdev)
		goto err_out;

	SET_NETDEV_DEV(netdev, &plat_dev->dev);

	dev_set_drvdata(&plat_dev->dev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->plat_dev = plat_dev;
	hw = &adapter->hw;
	hw->back = adapter;

	/* setup the private structure */
	err = fjes_sw_init(adapter);
	if (err)
		goto err_free_netdev;

	INIT_WORK(&adapter->force_close_task, fjes_force_close_task);
	adapter->force_reset = false;
	adapter->open_guard = false;

	adapter->txrx_wq = alloc_workqueue(DRV_NAME "/txrx", WQ_MEM_RECLAIM, 0);
	if (unlikely(!adapter->txrx_wq)) {
		err = -ENOMEM;
		goto err_free_netdev;
	}

	adapter->control_wq = alloc_workqueue(DRV_NAME "/control",
					      WQ_MEM_RECLAIM, 0);
	if (unlikely(!adapter->control_wq)) {
		err = -ENOMEM;
		goto err_free_txrx_wq;
	}

	INIT_WORK(&adapter->tx_stall_task, fjes_tx_stall_task);
	INIT_WORK(&adapter->raise_intr_rxdata_task,
		  fjes_raise_intr_rxdata_task);
	INIT_WORK(&adapter->unshare_watch_task, fjes_watch_unshare_task);
	adapter->unshare_watch_bitmask = 0;

	INIT_DELAYED_WORK(&adapter->interrupt_watch_task, fjes_irq_watch_task);
	adapter->interrupt_watch_enable = false;

	res = platform_get_resource(plat_dev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -EINVAL;
		goto err_free_control_wq;
	}
	hw->hw_res.start = res->start;
	hw->hw_res.size = resource_size(res);
	hw->hw_res.irq = platform_get_irq(plat_dev, 0);
	if (hw->hw_res.irq < 0) {
		err = hw->hw_res.irq;
		goto err_free_control_wq;
	}

	err = fjes_hw_init(&adapter->hw);
	if (err)
		goto err_free_control_wq;

	/* setup MAC address (02:00:00:00:00:[epid])*/
	addr[0] = 2;
	addr[1] = 0;
	addr[2] = 0;
	addr[3] = 0;
	addr[4] = 0;
	addr[5] = hw->my_epid; /* EPID */
	eth_hw_addr_set(netdev, addr);

	err = register_netdev(netdev);
	if (err)
		goto err_hw_exit;

	netif_carrier_off(netdev);

	fjes_dbg_adapter_init(adapter);

	return 0;

err_hw_exit:
	fjes_hw_exit(&adapter->hw);
err_free_control_wq:
	destroy_workqueue(adapter->control_wq);
err_free_txrx_wq:
	destroy_workqueue(adapter->txrx_wq);
err_free_netdev:
	free_netdev(netdev);
err_out:
	return err;
}

/* fjes_remove - Device Removal Routine */
static void fjes_remove(struct platform_device *plat_dev)
{
	struct net_device *netdev = dev_get_drvdata(&plat_dev->dev);
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;

	fjes_dbg_adapter_exit(adapter);

	cancel_delayed_work_sync(&adapter->interrupt_watch_task);
	cancel_work_sync(&adapter->unshare_watch_task);
	cancel_work_sync(&adapter->raise_intr_rxdata_task);
	cancel_work_sync(&adapter->tx_stall_task);
	if (adapter->control_wq)
		destroy_workqueue(adapter->control_wq);
	if (adapter->txrx_wq)
		destroy_workqueue(adapter->txrx_wq);

	unregister_netdev(netdev);

	fjes_hw_exit(hw);

	netif_napi_del(&adapter->napi);

	free_netdev(netdev);
}

static struct platform_driver fjes_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = fjes_probe,
	.remove_new = fjes_remove,
};

static acpi_status
acpi_find_extended_socket_device(acpi_handle obj_handle, u32 level,
				 void *context, void **return_value)
{
	struct acpi_device *device;
	bool *found = context;

	device = acpi_fetch_acpi_dev(obj_handle);
	if (!device)
		return AE_OK;

	if (strcmp(acpi_device_hid(device), ACPI_MOTHERBOARD_RESOURCE_HID))
		return AE_OK;

	if (!is_extended_socket_device(device))
		return AE_OK;

	if (acpi_check_extended_socket_status(device))
		return AE_OK;

	*found = true;
	return AE_CTRL_TERMINATE;
}

/* fjes_init_module - Driver Registration Routine */
static int __init fjes_init_module(void)
{
	bool found = false;
	int result;

	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			    acpi_find_extended_socket_device, NULL, &found,
			    NULL);

	if (!found)
		return -ENODEV;

	pr_info("%s - version %s - %s\n",
		fjes_driver_string, fjes_driver_version, fjes_copyright);

	fjes_dbg_init();

	result = platform_driver_register(&fjes_driver);
	if (result < 0) {
		fjes_dbg_exit();
		return result;
	}

	result = acpi_bus_register_driver(&fjes_acpi_driver);
	if (result < 0)
		goto fail_acpi_driver;

	return 0;

fail_acpi_driver:
	platform_driver_unregister(&fjes_driver);
	fjes_dbg_exit();
	return result;
}

module_init(fjes_init_module);

/* fjes_exit_module - Driver Exit Cleanup Routine */
static void __exit fjes_exit_module(void)
{
	acpi_bus_unregister_driver(&fjes_acpi_driver);
	platform_driver_unregister(&fjes_driver);
	fjes_dbg_exit();
}

module_exit(fjes_exit_module);
