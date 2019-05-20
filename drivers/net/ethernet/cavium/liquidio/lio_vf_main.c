/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <net/vxlan.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"
#include "cn23xx_vf_device.h"

MODULE_AUTHOR("Cavium Networks, <support@cavium.com>");
MODULE_DESCRIPTION("Cavium LiquidIO Intelligent Server Adapter Virtual Function Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(LIQUIDIO_VERSION);

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "NETIF_MSG debug bits");

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

struct oct_timestamp_resp {
	u64 rh;
	u64 timestamp;
	u64 status;
};

union tx_info {
	u64 u64;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		u16 gso_size;
		u16 gso_segs;
		u32 reserved;
#else
		u32 reserved;
		u16 gso_segs;
		u16 gso_size;
#endif
	} s;
};

#define OCTNIC_GSO_MAX_HEADER_SIZE 128
#define OCTNIC_GSO_MAX_SIZE \
		(CN23XX_DEFAULT_INPUT_JABBER - OCTNIC_GSO_MAX_HEADER_SIZE)

static int
liquidio_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void liquidio_vf_remove(struct pci_dev *pdev);
static int octeon_device_init(struct octeon_device *oct);
static int liquidio_stop(struct net_device *netdev);

static int lio_wait_for_oq_pkts(struct octeon_device *oct)
{
	struct octeon_device_priv *oct_priv =
	    (struct octeon_device_priv *)oct->priv;
	int retry = MAX_IO_PENDING_PKT_COUNT;
	int pkt_cnt = 0, pending_pkts;
	int i;

	do {
		pending_pkts = 0;

		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct); i++) {
			if (!(oct->io_qmask.oq & BIT_ULL(i)))
				continue;
			pkt_cnt += octeon_droq_check_hw_for_pkts(oct->droq[i]);
		}
		if (pkt_cnt > 0) {
			pending_pkts += pkt_cnt;
			tasklet_schedule(&oct_priv->droq_tasklet);
		}
		pkt_cnt = 0;
		schedule_timeout_uninterruptible(1);

	} while (retry-- && pending_pkts);

	return pkt_cnt;
}

/**
 * \brief Cause device to go quiet so it can be safely removed/reset/etc
 * @param oct Pointer to Octeon device
 */
static void pcierror_quiesce_device(struct octeon_device *oct)
{
	int i;

	/* Disable the input and output queues now. No more packets will
	 * arrive from Octeon, but we should wait for all packet processing
	 * to finish.
	 */

	/* To allow for in-flight requests */
	schedule_timeout_uninterruptible(100);

	if (wait_for_pending_requests(oct))
		dev_err(&oct->pci_dev->dev, "There were pending requests\n");

	/* Force all requests waiting to be fetched by OCTEON to complete. */
	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
		struct octeon_instr_queue *iq;

		if (!(oct->io_qmask.iq & BIT_ULL(i)))
			continue;
		iq = oct->instr_queue[i];

		if (atomic_read(&iq->instr_pending)) {
			spin_lock_bh(&iq->lock);
			iq->fill_cnt = 0;
			iq->octeon_read_index = iq->host_write_index;
			iq->stats.instr_processed +=
			    atomic_read(&iq->instr_pending);
			lio_process_iq_request_list(oct, iq, 0);
			spin_unlock_bh(&iq->lock);
		}
	}

	/* Force all pending ordered list requests to time out. */
	lio_process_ordered_list(oct, 1);

	/* We do not need to wait for output queue packets to be processed. */
}

/**
 * \brief Cleanup PCI AER uncorrectable error status
 * @param dev Pointer to PCI device
 */
static void cleanup_aer_uncorrect_error_status(struct pci_dev *dev)
{
	u32 status, mask;
	int pos = 0x100;

	pr_info("%s :\n", __func__);

	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, &status);
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &mask);
	if (dev->error_state == pci_channel_io_normal)
		status &= ~mask; /* Clear corresponding nonfatal bits */
	else
		status &= mask; /* Clear corresponding fatal bits */
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, status);
}

/**
 * \brief Stop all PCI IO to a given device
 * @param dev Pointer to Octeon device
 */
static void stop_pci_io(struct octeon_device *oct)
{
	struct msix_entry *msix_entries;
	int i;

	/* No more instructions will be forwarded. */
	atomic_set(&oct->status, OCT_DEV_IN_RESET);

	for (i = 0; i < oct->ifcount; i++)
		netif_device_detach(oct->props[i].netdev);

	/* Disable interrupts  */
	oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

	pcierror_quiesce_device(oct);
	if (oct->msix_on) {
		msix_entries = (struct msix_entry *)oct->msix_entries;
		for (i = 0; i < oct->num_msix_irqs; i++) {
			/* clear the affinity_cpumask */
			irq_set_affinity_hint(msix_entries[i].vector,
					      NULL);
			free_irq(msix_entries[i].vector,
				 &oct->ioq_vector[i]);
		}
		pci_disable_msix(oct->pci_dev);
		kfree(oct->msix_entries);
		oct->msix_entries = NULL;
		octeon_free_ioq_vector(oct);
	}
	dev_dbg(&oct->pci_dev->dev, "Device state is now %s\n",
		lio_get_state_string(&oct->status));

	/* making it a common function for all OCTEON models */
	cleanup_aer_uncorrect_error_status(oct->pci_dev);

	pci_disable_device(oct->pci_dev);
}

/**
 * \brief called when PCI error is detected
 * @param pdev Pointer to PCI device
 * @param state The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t liquidio_pcie_error_detected(struct pci_dev *pdev,
						     pci_channel_state_t state)
{
	struct octeon_device *oct = pci_get_drvdata(pdev);

	/* Non-correctable Non-fatal errors */
	if (state == pci_channel_io_normal) {
		dev_err(&oct->pci_dev->dev, "Non-correctable non-fatal error reported:\n");
		cleanup_aer_uncorrect_error_status(oct->pci_dev);
		return PCI_ERS_RESULT_CAN_RECOVER;
	}

	/* Non-correctable Fatal errors */
	dev_err(&oct->pci_dev->dev, "Non-correctable FATAL reported by PCI AER driver\n");
	stop_pci_io(oct);

	return PCI_ERS_RESULT_DISCONNECT;
}

/* For PCI-E Advanced Error Recovery (AER) Interface */
static const struct pci_error_handlers liquidio_vf_err_handler = {
	.error_detected = liquidio_pcie_error_detected,
};

static const struct pci_device_id liquidio_vf_pci_tbl[] = {
	{
		PCI_VENDOR_ID_CAVIUM, OCTEON_CN23XX_VF_VID,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0
	},
	{
		0, 0, 0, 0, 0, 0, 0
	}
};
MODULE_DEVICE_TABLE(pci, liquidio_vf_pci_tbl);

static struct pci_driver liquidio_vf_pci_driver = {
	.name		= "LiquidIO_VF",
	.id_table	= liquidio_vf_pci_tbl,
	.probe		= liquidio_vf_probe,
	.remove		= liquidio_vf_remove,
	.err_handler	= &liquidio_vf_err_handler,    /* For AER */
};

/**
 * \brief Print link information
 * @param netdev network device
 */
static void print_link_info(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	if (!ifstate_check(lio, LIO_IFSTATE_RESETTING) &&
	    ifstate_check(lio, LIO_IFSTATE_REGISTERED)) {
		struct oct_link_info *linfo = &lio->linfo;

		if (linfo->link.s.link_up) {
			netif_info(lio, link, lio->netdev, "%d Mbps %s Duplex UP\n",
				   linfo->link.s.speed,
				   (linfo->link.s.duplex) ? "Full" : "Half");
		} else {
			netif_info(lio, link, lio->netdev, "Link Down\n");
		}
	}
}

/**
 * \brief Routine to notify MTU change
 * @param work work_struct data structure
 */
static void octnet_link_status_change(struct work_struct *work)
{
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct lio *lio = (struct lio *)wk->ctxptr;

	/* lio->linfo.link.s.mtu always contains max MTU of the lio interface.
	 * this API is invoked only when new max-MTU of the interface is
	 * less than current MTU.
	 */
	rtnl_lock();
	dev_set_mtu(lio->netdev, lio->linfo.link.s.mtu);
	rtnl_unlock();
}

/**
 * \brief Sets up the mtu status change work
 * @param netdev network device
 */
static int setup_link_status_change_wq(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	lio->link_status_wq.wq = alloc_workqueue("link-status",
						 WQ_MEM_RECLAIM, 0);
	if (!lio->link_status_wq.wq) {
		dev_err(&oct->pci_dev->dev, "unable to create cavium link status wq\n");
		return -1;
	}
	INIT_DELAYED_WORK(&lio->link_status_wq.wk.work,
			  octnet_link_status_change);
	lio->link_status_wq.wk.ctxptr = lio;

	return 0;
}

static void cleanup_link_status_change_wq(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	if (lio->link_status_wq.wq) {
		cancel_delayed_work_sync(&lio->link_status_wq.wk.work);
		destroy_workqueue(lio->link_status_wq.wq);
	}
}

/**
 * \brief Update link status
 * @param netdev network device
 * @param ls link status structure
 *
 * Called on receipt of a link status response from the core application to
 * update each interface's link status.
 */
static void update_link_status(struct net_device *netdev,
			       union oct_link_status *ls)
{
	struct lio *lio = GET_LIO(netdev);
	int current_max_mtu = lio->linfo.link.s.mtu;
	struct octeon_device *oct = lio->oct_dev;

	if ((lio->intf_open) && (lio->linfo.link.u64 != ls->u64)) {
		lio->linfo.link.u64 = ls->u64;

		print_link_info(netdev);
		lio->link_changes++;

		if (lio->linfo.link.s.link_up) {
			netif_carrier_on(netdev);
			wake_txqs(netdev);
		} else {
			netif_carrier_off(netdev);
			stop_txqs(netdev);
		}

		if (lio->linfo.link.s.mtu != current_max_mtu) {
			dev_info(&oct->pci_dev->dev,
				 "Max MTU Changed from %d to %d\n",
				 current_max_mtu, lio->linfo.link.s.mtu);
			netdev->max_mtu = lio->linfo.link.s.mtu;
		}

		if (lio->linfo.link.s.mtu < netdev->mtu) {
			dev_warn(&oct->pci_dev->dev,
				 "Current MTU is higher than new max MTU; Reducing the current mtu from %d to %d\n",
				 netdev->mtu, lio->linfo.link.s.mtu);
			queue_delayed_work(lio->link_status_wq.wq,
					   &lio->link_status_wq.wk.work, 0);
		}
	}
}

/**
 * \brief PCI probe handler
 * @param pdev PCI device structure
 * @param ent unused
 */
static int
liquidio_vf_probe(struct pci_dev *pdev,
		  const struct pci_device_id *ent __attribute__((unused)))
{
	struct octeon_device *oct_dev = NULL;

	oct_dev = octeon_allocate_device(pdev->device,
					 sizeof(struct octeon_device_priv));

	if (!oct_dev) {
		dev_err(&pdev->dev, "Unable to allocate device\n");
		return -ENOMEM;
	}
	oct_dev->msix_on = LIO_FLAG_MSIX_ENABLED;

	dev_info(&pdev->dev, "Initializing device %x:%x.\n",
		 (u32)pdev->vendor, (u32)pdev->device);

	/* Assign octeon_device for this device to the private data area. */
	pci_set_drvdata(pdev, oct_dev);

	/* set linux specific device pointer */
	oct_dev->pci_dev = pdev;

	oct_dev->subsystem_id = pdev->subsystem_vendor |
		(pdev->subsystem_device << 16);

	if (octeon_device_init(oct_dev)) {
		liquidio_vf_remove(pdev);
		return -ENOMEM;
	}

	dev_dbg(&oct_dev->pci_dev->dev, "Device is ready\n");

	return 0;
}

/**
 * \brief PCI FLR for each Octeon device.
 * @param oct octeon device
 */
static void octeon_pci_flr(struct octeon_device *oct)
{
	pci_save_state(oct->pci_dev);

	pci_cfg_access_lock(oct->pci_dev);

	/* Quiesce the device completely */
	pci_write_config_word(oct->pci_dev, PCI_COMMAND,
			      PCI_COMMAND_INTX_DISABLE);

	pcie_flr(oct->pci_dev);

	pci_cfg_access_unlock(oct->pci_dev);

	pci_restore_state(oct->pci_dev);
}

/**
 *\brief Destroy resources associated with octeon device
 * @param pdev PCI device structure
 * @param ent unused
 */
static void octeon_destroy_resources(struct octeon_device *oct)
{
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	struct msix_entry *msix_entries;
	int i;

	switch (atomic_read(&oct->status)) {
	case OCT_DEV_RUNNING:
	case OCT_DEV_CORE_OK:
		/* No more instructions will be forwarded. */
		atomic_set(&oct->status, OCT_DEV_IN_RESET);

		oct->app_mode = CVM_DRV_INVALID_APP;
		dev_dbg(&oct->pci_dev->dev, "Device state is now %s\n",
			lio_get_state_string(&oct->status));

		schedule_timeout_uninterruptible(HZ / 10);

		/* fallthrough */
	case OCT_DEV_HOST_OK:
		/* fallthrough */
	case OCT_DEV_IO_QUEUES_DONE:
		if (lio_wait_for_instr_fetch(oct))
			dev_err(&oct->pci_dev->dev, "IQ had pending instructions\n");

		if (wait_for_pending_requests(oct))
			dev_err(&oct->pci_dev->dev, "There were pending requests\n");

		/* Disable the input and output queues now. No more packets will
		 * arrive from Octeon, but we should wait for all packet
		 * processing to finish.
		 */
		oct->fn_list.disable_io_queues(oct);

		if (lio_wait_for_oq_pkts(oct))
			dev_err(&oct->pci_dev->dev, "OQ had pending packets\n");

		/* Force all requests waiting to be fetched by OCTEON to
		 * complete.
		 */
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
			struct octeon_instr_queue *iq;

			if (!(oct->io_qmask.iq & BIT_ULL(i)))
				continue;
			iq = oct->instr_queue[i];

			if (atomic_read(&iq->instr_pending)) {
				spin_lock_bh(&iq->lock);
				iq->fill_cnt = 0;
				iq->octeon_read_index = iq->host_write_index;
				iq->stats.instr_processed +=
					atomic_read(&iq->instr_pending);
				lio_process_iq_request_list(oct, iq, 0);
				spin_unlock_bh(&iq->lock);
			}
		}

		lio_process_ordered_list(oct, 1);
		octeon_free_sc_done_list(oct);
		octeon_free_sc_zombie_list(oct);

	/* fall through */
	case OCT_DEV_INTR_SET_DONE:
		/* Disable interrupts  */
		oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

		if (oct->msix_on) {
			msix_entries = (struct msix_entry *)oct->msix_entries;
			for (i = 0; i < oct->num_msix_irqs; i++) {
				if (oct->ioq_vector[i].vector) {
					irq_set_affinity_hint(
							msix_entries[i].vector,
							NULL);
					free_irq(msix_entries[i].vector,
						 &oct->ioq_vector[i]);
					oct->ioq_vector[i].vector = 0;
				}
			}
			pci_disable_msix(oct->pci_dev);
			kfree(oct->msix_entries);
			oct->msix_entries = NULL;
			kfree(oct->irq_name_storage);
			oct->irq_name_storage = NULL;
		}
		/* Soft reset the octeon device before exiting */
		if (oct->pci_dev->reset_fn)
			octeon_pci_flr(oct);
		else
			cn23xx_vf_ask_pf_to_do_flr(oct);

		/* fallthrough */
	case OCT_DEV_MSIX_ALLOC_VECTOR_DONE:
		octeon_free_ioq_vector(oct);

		/* fallthrough */
	case OCT_DEV_MBOX_SETUP_DONE:
		oct->fn_list.free_mbox(oct);

		/* fallthrough */
	case OCT_DEV_IN_RESET:
	case OCT_DEV_DROQ_INIT_DONE:
		mdelay(100);
		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct); i++) {
			if (!(oct->io_qmask.oq & BIT_ULL(i)))
				continue;
			octeon_delete_droq(oct, i);
		}

		/* fallthrough */
	case OCT_DEV_RESP_LIST_INIT_DONE:
		octeon_delete_response_list(oct);

		/* fallthrough */
	case OCT_DEV_INSTR_QUEUE_INIT_DONE:
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
			if (!(oct->io_qmask.iq & BIT_ULL(i)))
				continue;
			octeon_delete_instr_queue(oct, i);
		}

		/* fallthrough */
	case OCT_DEV_SC_BUFF_POOL_INIT_DONE:
		octeon_free_sc_buffer_pool(oct);

		/* fallthrough */
	case OCT_DEV_DISPATCH_INIT_DONE:
		octeon_delete_dispatch_list(oct);
		cancel_delayed_work_sync(&oct->nic_poll_work.work);

		/* fallthrough */
	case OCT_DEV_PCI_MAP_DONE:
		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);

		/* fallthrough */
	case OCT_DEV_PCI_ENABLE_DONE:
		pci_clear_master(oct->pci_dev);
		/* Disable the device, releasing the PCI INT */
		pci_disable_device(oct->pci_dev);

		/* fallthrough */
	case OCT_DEV_BEGIN_STATE:
		/* Nothing to be done here either */
		break;
	}

	tasklet_kill(&oct_priv->droq_tasklet);
}

/**
 * \brief Send Rx control command
 * @param lio per-network private data
 * @param start_stop whether to start or stop
 */
static void send_rx_ctrl_cmd(struct lio *lio, int start_stop)
{
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;
	struct octeon_soft_command *sc;
	union octnet_cmd *ncmd;
	int retval;

	if (oct->props[lio->ifidx].rx_on == start_stop)
		return;

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct, OCTNET_CMD_SIZE,
					  16, 0);

	ncmd = (union octnet_cmd *)sc->virtdptr;

	ncmd->u64 = 0;
	ncmd->s.cmd = OCTNET_CMD_RX_CTL;
	ncmd->s.param1 = start_stop;

	octeon_swap_8B_data((u64 *)ncmd, (OCTNET_CMD_SIZE >> 3));

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct, sc, OPCODE_NIC,
				    OPCODE_NIC_CMD, 0, 0, 0);

	init_completion(&sc->complete);
	sc->sc_status = OCTEON_REQUEST_PENDING;

	retval = octeon_send_soft_command(oct, sc);
	if (retval == IQ_SEND_FAILED) {
		netif_info(lio, rx_err, lio->netdev, "Failed to send RX Control message\n");
		octeon_free_soft_command(oct, sc);
	} else {
		/* Sleep on a wait queue till the cond flag indicates that the
		 * response arrived or timed-out.
		 */
		retval = wait_for_sc_completion_timeout(oct, sc, 0);
		if (retval)
			return;

		oct->props[lio->ifidx].rx_on = start_stop;
		WRITE_ONCE(sc->caller_is_done, true);
	}
}

/**
 * \brief Destroy NIC device interface
 * @param oct octeon device
 * @param ifidx which interface to destroy
 *
 * Cleanup associated with each interface for an Octeon device  when NIC
 * module is being unloaded or if initialization fails during load.
 */
static void liquidio_destroy_nic_device(struct octeon_device *oct, int ifidx)
{
	struct net_device *netdev = oct->props[ifidx].netdev;
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	struct napi_struct *napi, *n;
	struct lio *lio;

	if (!netdev) {
		dev_err(&oct->pci_dev->dev, "%s No netdevice ptr for index %d\n",
			__func__, ifidx);
		return;
	}

	lio = GET_LIO(netdev);

	dev_dbg(&oct->pci_dev->dev, "NIC device cleanup\n");

	if (atomic_read(&lio->ifstate) & LIO_IFSTATE_RUNNING)
		liquidio_stop(netdev);

	if (oct->props[lio->ifidx].napi_enabled == 1) {
		list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
			napi_disable(napi);

		oct->props[lio->ifidx].napi_enabled = 0;

		oct->droq[0]->ops.poll_mode = 0;
	}

	/* Delete NAPI */
	list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
		netif_napi_del(napi);

	tasklet_enable(&oct_priv->droq_tasklet);

	if (atomic_read(&lio->ifstate) & LIO_IFSTATE_REGISTERED)
		unregister_netdev(netdev);

	cleanup_rx_oom_poll_fn(netdev);

	cleanup_link_status_change_wq(netdev);

	lio_delete_glists(lio);

	free_netdev(netdev);

	oct->props[ifidx].gmxport = -1;

	oct->props[ifidx].netdev = NULL;
}

/**
 * \brief Stop complete NIC functionality
 * @param oct octeon device
 */
static int liquidio_stop_nic_module(struct octeon_device *oct)
{
	struct lio *lio;
	int i, j;

	dev_dbg(&oct->pci_dev->dev, "Stopping network interfaces\n");
	if (!oct->ifcount) {
		dev_err(&oct->pci_dev->dev, "Init for Octeon was not completed\n");
		return 1;
	}

	spin_lock_bh(&oct->cmd_resp_wqlock);
	oct->cmd_resp_state = OCT_DRV_OFFLINE;
	spin_unlock_bh(&oct->cmd_resp_wqlock);

	for (i = 0; i < oct->ifcount; i++) {
		lio = GET_LIO(oct->props[i].netdev);
		for (j = 0; j < oct->num_oqs; j++)
			octeon_unregister_droq_ops(oct,
						   lio->linfo.rxpciq[j].s.q_no);
	}

	for (i = 0; i < oct->ifcount; i++)
		liquidio_destroy_nic_device(oct, i);

	dev_dbg(&oct->pci_dev->dev, "Network interfaces stopped\n");
	return 0;
}

/**
 * \brief Cleans up resources at unload time
 * @param pdev PCI device structure
 */
static void liquidio_vf_remove(struct pci_dev *pdev)
{
	struct octeon_device *oct_dev = pci_get_drvdata(pdev);

	dev_dbg(&oct_dev->pci_dev->dev, "Stopping device\n");

	if (oct_dev->app_mode == CVM_DRV_NIC_APP)
		liquidio_stop_nic_module(oct_dev);

	/* Reset the octeon device and cleanup all memory allocated for
	 * the octeon device by driver.
	 */
	octeon_destroy_resources(oct_dev);

	dev_info(&oct_dev->pci_dev->dev, "Device removed\n");

	/* This octeon device has been removed. Update the global
	 * data structure to reflect this. Free the device structure.
	 */
	octeon_free_device_mem(oct_dev);
}

/**
 * \brief PCI initialization for each Octeon device.
 * @param oct octeon device
 */
static int octeon_pci_os_setup(struct octeon_device *oct)
{
#ifdef CONFIG_PCI_IOV
	/* setup PCI stuff first */
	if (!oct->pci_dev->physfn)
		octeon_pci_flr(oct);
#endif

	if (pci_enable_device(oct->pci_dev)) {
		dev_err(&oct->pci_dev->dev, "pci_enable_device failed\n");
		return 1;
	}

	if (dma_set_mask_and_coherent(&oct->pci_dev->dev, DMA_BIT_MASK(64))) {
		dev_err(&oct->pci_dev->dev, "Unexpected DMA device capability\n");
		pci_disable_device(oct->pci_dev);
		return 1;
	}

	/* Enable PCI DMA Master. */
	pci_set_master(oct->pci_dev);

	return 0;
}

/**
 * \brief Unmap and free network buffer
 * @param buf buffer
 */
static void free_netbuf(void *buf)
{
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb;
	struct lio *lio;

	finfo = (struct octnet_buf_free_info *)buf;
	skb = finfo->skb;
	lio = finfo->lio;

	dma_unmap_single(&lio->oct_dev->pci_dev->dev, finfo->dptr, skb->len,
			 DMA_TO_DEVICE);

	tx_buffer_free(skb);
}

/**
 * \brief Unmap and free gather buffer
 * @param buf buffer
 */
static void free_netsgbuf(void *buf)
{
	struct octnet_buf_free_info *finfo;
	struct octnic_gather *g;
	struct sk_buff *skb;
	int i, frags, iq;
	struct lio *lio;

	finfo = (struct octnet_buf_free_info *)buf;
	skb = finfo->skb;
	lio = finfo->lio;
	g = finfo->g;
	frags = skb_shinfo(skb)->nr_frags;

	dma_unmap_single(&lio->oct_dev->pci_dev->dev,
			 g->sg[0].ptr[0], (skb->len - skb->data_len),
			 DMA_TO_DEVICE);

	i = 1;
	while (frags--) {
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i - 1];

		pci_unmap_page((lio->oct_dev)->pci_dev,
			       g->sg[(i >> 2)].ptr[(i & 3)],
			       frag->size, DMA_TO_DEVICE);
		i++;
	}

	iq = skb_iq(lio->oct_dev, skb);

	spin_lock(&lio->glist_lock[iq]);
	list_add_tail(&g->list, &lio->glist[iq]);
	spin_unlock(&lio->glist_lock[iq]);

	tx_buffer_free(skb);
}

/**
 * \brief Unmap and free gather buffer with response
 * @param buf buffer
 */
static void free_netsgbuf_with_resp(void *buf)
{
	struct octnet_buf_free_info *finfo;
	struct octeon_soft_command *sc;
	struct octnic_gather *g;
	struct sk_buff *skb;
	int i, frags, iq;
	struct lio *lio;

	sc = (struct octeon_soft_command *)buf;
	skb = (struct sk_buff *)sc->callback_arg;
	finfo = (struct octnet_buf_free_info *)&skb->cb;

	lio = finfo->lio;
	g = finfo->g;
	frags = skb_shinfo(skb)->nr_frags;

	dma_unmap_single(&lio->oct_dev->pci_dev->dev,
			 g->sg[0].ptr[0], (skb->len - skb->data_len),
			 DMA_TO_DEVICE);

	i = 1;
	while (frags--) {
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i - 1];

		pci_unmap_page((lio->oct_dev)->pci_dev,
			       g->sg[(i >> 2)].ptr[(i & 3)],
			       frag->size, DMA_TO_DEVICE);
		i++;
	}

	iq = skb_iq(lio->oct_dev, skb);

	spin_lock(&lio->glist_lock[iq]);
	list_add_tail(&g->list, &lio->glist[iq]);
	spin_unlock(&lio->glist_lock[iq]);

	/* Don't free the skb yet */
}

/**
 * \brief Net device open for LiquidIO
 * @param netdev network device
 */
static int liquidio_open(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	struct napi_struct *napi, *n;

	if (!oct->props[lio->ifidx].napi_enabled) {
		tasklet_disable(&oct_priv->droq_tasklet);

		list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
			napi_enable(napi);

		oct->props[lio->ifidx].napi_enabled = 1;

		oct->droq[0]->ops.poll_mode = 1;
	}

	ifstate_set(lio, LIO_IFSTATE_RUNNING);

	/* Ready for link status updates */
	lio->intf_open = 1;

	netif_info(lio, ifup, lio->netdev, "Interface Open, ready for traffic\n");
	start_txqs(netdev);

	INIT_DELAYED_WORK(&lio->stats_wk.work, lio_fetch_stats);
	lio->stats_wk.ctxptr = lio;
	schedule_delayed_work(&lio->stats_wk.work, msecs_to_jiffies
					(LIQUIDIO_NDEV_STATS_POLL_TIME_MS));

	/* tell Octeon to start forwarding packets to host */
	send_rx_ctrl_cmd(lio, 1);

	dev_info(&oct->pci_dev->dev, "%s interface is opened\n", netdev->name);

	return 0;
}

/**
 * \brief Net device stop for LiquidIO
 * @param netdev network device
 */
static int liquidio_stop(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	struct napi_struct *napi, *n;

	/* tell Octeon to stop forwarding packets to host */
	send_rx_ctrl_cmd(lio, 0);

	netif_info(lio, ifdown, lio->netdev, "Stopping interface!\n");
	/* Inform that netif carrier is down */
	lio->intf_open = 0;
	lio->linfo.link.s.link_up = 0;

	netif_carrier_off(netdev);
	lio->link_changes++;

	ifstate_reset(lio, LIO_IFSTATE_RUNNING);

	stop_txqs(netdev);

	/* Wait for any pending Rx descriptors */
	if (lio_wait_for_clean_oq(oct))
		netif_info(lio, rx_err, lio->netdev,
			   "Proceeding with stop interface after partial RX desc processing\n");

	if (oct->props[lio->ifidx].napi_enabled == 1) {
		list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
			napi_disable(napi);

		oct->props[lio->ifidx].napi_enabled = 0;

		oct->droq[0]->ops.poll_mode = 0;

		tasklet_enable(&oct_priv->droq_tasklet);
	}

	cancel_delayed_work_sync(&lio->stats_wk.work);

	dev_info(&oct->pci_dev->dev, "%s interface is stopped\n", netdev->name);

	return 0;
}

/**
 * \brief Converts a mask based on net device flags
 * @param netdev network device
 *
 * This routine generates a octnet_ifflags mask from the net device flags
 * received from the OS.
 */
static enum octnet_ifflags get_new_flags(struct net_device *netdev)
{
	enum octnet_ifflags f = OCTNET_IFFLAG_UNICAST;

	if (netdev->flags & IFF_PROMISC)
		f |= OCTNET_IFFLAG_PROMISC;

	if (netdev->flags & IFF_ALLMULTI)
		f |= OCTNET_IFFLAG_ALLMULTI;

	if (netdev->flags & IFF_MULTICAST) {
		f |= OCTNET_IFFLAG_MULTICAST;

		/* Accept all multicast addresses if there are more than we
		 * can handle
		 */
		if (netdev_mc_count(netdev) > MAX_OCTEON_MULTICAST_ADDR)
			f |= OCTNET_IFFLAG_ALLMULTI;
	}

	if (netdev->flags & IFF_BROADCAST)
		f |= OCTNET_IFFLAG_BROADCAST;

	return f;
}

static void liquidio_set_uc_list(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	struct netdev_hw_addr *ha;
	u64 *mac;

	if (lio->netdev_uc_count == netdev_uc_count(netdev))
		return;

	if (netdev_uc_count(netdev) > MAX_NCTRL_UDD) {
		dev_err(&oct->pci_dev->dev, "too many MAC addresses in netdev uc list\n");
		return;
	}

	lio->netdev_uc_count = netdev_uc_count(netdev);

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_UC_LIST;
	nctrl.ncmd.s.more = lio->netdev_uc_count;
	nctrl.ncmd.s.param1 = oct->vf_num;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	/* copy all the addresses into the udd */
	mac = &nctrl.udd[0];
	netdev_for_each_uc_addr(ha, netdev) {
		ether_addr_copy(((u8 *)mac) + 2, ha->addr);
		mac++;
	}

	octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
}

/**
 * \brief Net device set_multicast_list
 * @param netdev network device
 */
static void liquidio_set_mcast_list(struct net_device *netdev)
{
	int mc_count = min(netdev_mc_count(netdev), MAX_OCTEON_MULTICAST_ADDR);
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	struct netdev_hw_addr *ha;
	u64 *mc;
	int ret;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	/* Create a ctrl pkt command to be sent to core app. */
	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_MULTI_LIST;
	nctrl.ncmd.s.param1 = get_new_flags(netdev);
	nctrl.ncmd.s.param2 = mc_count;
	nctrl.ncmd.s.more = mc_count;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	/* copy all the addresses into the udd */
	mc = &nctrl.udd[0];
	netdev_for_each_mc_addr(ha, netdev) {
		*mc = 0;
		ether_addr_copy(((u8 *)mc) + 2, ha->addr);
		/* no need to swap bytes */
		if (++mc > &nctrl.udd[mc_count])
			break;
	}

	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;

	/* Apparently, any activity in this call from the kernel has to
	 * be atomic. So we won't wait for response.
	 */

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "DEVFLAGS change failed in core (ret: 0x%x)\n",
			ret);
	}

	liquidio_set_uc_list(netdev);
}

/**
 * \brief Net device set_mac_address
 * @param netdev network device
 */
static int liquidio_set_mac(struct net_device *netdev, void *p)
{
	struct sockaddr *addr = (struct sockaddr *)p;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(addr->sa_data, netdev->dev_addr))
		return 0;

	if (lio->linfo.macaddr_is_admin_asgnd)
		return -EPERM;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_CHANGE_MACADDR;
	nctrl.ncmd.s.param1 = 0;
	nctrl.ncmd.s.more = 1;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;

	nctrl.udd[0] = 0;
	/* The MAC Address is presented in network byte order. */
	ether_addr_copy((u8 *)&nctrl.udd[0] + 2, addr->sa_data);

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "MAC Address change failed\n");
		return -ENOMEM;
	}

	if (nctrl.sc_status ==
	    FIRMWARE_STATUS_CODE(OCTEON_REQUEST_NO_PERMISSION)) {
		dev_err(&oct->pci_dev->dev, "MAC Address change failed: no permission\n");
		return -EPERM;
	}

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	ether_addr_copy(((u8 *)&lio->linfo.hw_addr) + 2, addr->sa_data);

	return 0;
}

static void
liquidio_get_stats64(struct net_device *netdev,
		     struct rtnl_link_stats64 *lstats)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct;
	u64 pkts = 0, drop = 0, bytes = 0;
	struct oct_droq_stats *oq_stats;
	struct oct_iq_stats *iq_stats;
	int i, iq_no, oq_no;

	oct = lio->oct_dev;

	if (ifstate_check(lio, LIO_IFSTATE_RESETTING))
		return;

	for (i = 0; i < oct->num_iqs; i++) {
		iq_no = lio->linfo.txpciq[i].s.q_no;
		iq_stats = &oct->instr_queue[iq_no]->stats;
		pkts += iq_stats->tx_done;
		drop += iq_stats->tx_dropped;
		bytes += iq_stats->tx_tot_bytes;
	}

	lstats->tx_packets = pkts;
	lstats->tx_bytes = bytes;
	lstats->tx_dropped = drop;

	pkts = 0;
	drop = 0;
	bytes = 0;

	for (i = 0; i < oct->num_oqs; i++) {
		oq_no = lio->linfo.rxpciq[i].s.q_no;
		oq_stats = &oct->droq[oq_no]->stats;
		pkts += oq_stats->rx_pkts_received;
		drop += (oq_stats->rx_dropped +
			 oq_stats->dropped_nodispatch +
			 oq_stats->dropped_toomany +
			 oq_stats->dropped_nomem);
		bytes += oq_stats->rx_bytes_received;
	}

	lstats->rx_bytes = bytes;
	lstats->rx_packets = pkts;
	lstats->rx_dropped = drop;

	lstats->multicast = oct->link_stats.fromwire.fw_total_mcast;

	/* detailed rx_errors: */
	lstats->rx_length_errors = oct->link_stats.fromwire.l2_err;
	/* recved pkt with crc error */
	lstats->rx_crc_errors = oct->link_stats.fromwire.fcs_err;
	/* recv'd frame alignment error */
	lstats->rx_frame_errors = oct->link_stats.fromwire.frame_err;

	lstats->rx_errors = lstats->rx_length_errors + lstats->rx_crc_errors +
			    lstats->rx_frame_errors;

	/* detailed tx_errors */
	lstats->tx_aborted_errors = oct->link_stats.fromhost.fw_err_pko;
	lstats->tx_carrier_errors = oct->link_stats.fromhost.fw_err_link;

	lstats->tx_errors = lstats->tx_aborted_errors +
		lstats->tx_carrier_errors;
}

/**
 * \brief Handler for SIOCSHWTSTAMP ioctl
 * @param netdev network device
 * @param ifr interface request
 * @param cmd command
 */
static int hwtstamp_ioctl(struct net_device *netdev, struct ifreq *ifr)
{
	struct lio *lio = GET_LIO(netdev);
	struct hwtstamp_config conf;

	if (copy_from_user(&conf, ifr->ifr_data, sizeof(conf)))
		return -EFAULT;

	if (conf.flags)
		return -EINVAL;

	switch (conf.tx_type) {
	case HWTSTAMP_TX_ON:
	case HWTSTAMP_TX_OFF:
		break;
	default:
		return -ERANGE;
	}

	switch (conf.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_NTP_ALL:
		conf.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	if (conf.rx_filter == HWTSTAMP_FILTER_ALL)
		ifstate_set(lio, LIO_IFSTATE_RX_TIMESTAMP_ENABLED);

	else
		ifstate_reset(lio, LIO_IFSTATE_RX_TIMESTAMP_ENABLED);

	return copy_to_user(ifr->ifr_data, &conf, sizeof(conf)) ? -EFAULT : 0;
}

/**
 * \brief ioctl handler
 * @param netdev network device
 * @param ifr interface request
 * @param cmd command
 */
static int liquidio_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCSHWTSTAMP:
		return hwtstamp_ioctl(netdev, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

static void handle_timestamp(struct octeon_device *oct, u32 status, void *buf)
{
	struct sk_buff *skb = (struct sk_buff *)buf;
	struct octnet_buf_free_info *finfo;
	struct oct_timestamp_resp *resp;
	struct octeon_soft_command *sc;
	struct lio *lio;

	finfo = (struct octnet_buf_free_info *)skb->cb;
	lio = finfo->lio;
	sc = finfo->sc;
	oct = lio->oct_dev;
	resp = (struct oct_timestamp_resp *)sc->virtrptr;

	if (status != OCTEON_REQUEST_DONE) {
		dev_err(&oct->pci_dev->dev, "Tx timestamp instruction failed. Status: %llx\n",
			CVM_CAST64(status));
		resp->timestamp = 0;
	}

	octeon_swap_8B_data(&resp->timestamp, 1);

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS)) {
		struct skb_shared_hwtstamps ts;
		u64 ns = resp->timestamp;

		netif_info(lio, tx_done, lio->netdev,
			   "Got resulting SKBTX_HW_TSTAMP skb=%p ns=%016llu\n",
			   skb, (unsigned long long)ns);
		ts.hwtstamp = ns_to_ktime(ns + lio->ptp_adjust);
		skb_tstamp_tx(skb, &ts);
	}

	octeon_free_soft_command(oct, sc);
	tx_buffer_free(skb);
}

/* \brief Send a data packet that will be timestamped
 * @param oct octeon device
 * @param ndata pointer to network data
 * @param finfo pointer to private network data
 */
static int send_nic_timestamp_pkt(struct octeon_device *oct,
				  struct octnic_data_pkt *ndata,
				  struct octnet_buf_free_info *finfo,
				  int xmit_more)
{
	struct octeon_soft_command *sc;
	int ring_doorbell;
	struct lio *lio;
	int retval;
	u32 len;

	lio = finfo->lio;

	sc = octeon_alloc_soft_command_resp(oct, &ndata->cmd,
					    sizeof(struct oct_timestamp_resp));
	finfo->sc = sc;

	if (!sc) {
		dev_err(&oct->pci_dev->dev, "No memory for timestamped data packet\n");
		return IQ_SEND_FAILED;
	}

	if (ndata->reqtype == REQTYPE_NORESP_NET)
		ndata->reqtype = REQTYPE_RESP_NET;
	else if (ndata->reqtype == REQTYPE_NORESP_NET_SG)
		ndata->reqtype = REQTYPE_RESP_NET_SG;

	sc->callback = handle_timestamp;
	sc->callback_arg = finfo->skb;
	sc->iq_no = ndata->q_no;

	len = (u32)((struct octeon_instr_ih3 *)(&sc->cmd.cmd3.ih3))->dlengsz;

	ring_doorbell = !xmit_more;

	retval = octeon_send_command(oct, sc->iq_no, ring_doorbell, &sc->cmd,
				     sc, len, ndata->reqtype);

	if (retval == IQ_SEND_FAILED) {
		dev_err(&oct->pci_dev->dev, "timestamp data packet failed status: %x\n",
			retval);
		octeon_free_soft_command(oct, sc);
	} else {
		netif_info(lio, tx_queued, lio->netdev, "Queued timestamp packet\n");
	}

	return retval;
}

/** \brief Transmit networks packets to the Octeon interface
 * @param skbuff   skbuff struct to be passed to network layer.
 * @param netdev   pointer to network device
 * @returns whether the packet was transmitted to the device okay or not
 *             (NETDEV_TX_OK or NETDEV_TX_BUSY)
 */
static netdev_tx_t liquidio_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct octnet_buf_free_info *finfo;
	union octnic_cmd_setup cmdsetup;
	struct octnic_data_pkt ndata;
	struct octeon_instr_irh *irh;
	struct oct_iq_stats *stats;
	struct octeon_device *oct;
	int q_idx = 0, iq_no = 0;
	union tx_info *tx_info;
	int xmit_more = 0;
	struct lio *lio;
	int status = 0;
	u64 dptr = 0;
	u32 tag = 0;
	int j;

	lio = GET_LIO(netdev);
	oct = lio->oct_dev;

	q_idx = skb_iq(lio->oct_dev, skb);
	tag = q_idx;
	iq_no = lio->linfo.txpciq[q_idx].s.q_no;

	stats = &oct->instr_queue[iq_no]->stats;

	/* Check for all conditions in which the current packet cannot be
	 * transmitted.
	 */
	if (!(atomic_read(&lio->ifstate) & LIO_IFSTATE_RUNNING) ||
	    (!lio->linfo.link.s.link_up) || (skb->len <= 0)) {
		netif_info(lio, tx_err, lio->netdev, "Transmit failed link_status : %d\n",
			   lio->linfo.link.s.link_up);
		goto lio_xmit_failed;
	}

	/* Use space in skb->cb to store info used to unmap and
	 * free the buffers.
	 */
	finfo = (struct octnet_buf_free_info *)skb->cb;
	finfo->lio = lio;
	finfo->skb = skb;
	finfo->sc = NULL;

	/* Prepare the attributes for the data to be passed to OSI. */
	memset(&ndata, 0, sizeof(struct octnic_data_pkt));

	ndata.buf = finfo;

	ndata.q_no = iq_no;

	if (octnet_iq_is_full(oct, ndata.q_no)) {
		/* defer sending if queue is full */
		netif_info(lio, tx_err, lio->netdev, "Transmit failed iq:%d full\n",
			   ndata.q_no);
		stats->tx_iq_busy++;
		return NETDEV_TX_BUSY;
	}

	ndata.datasize = skb->len;

	cmdsetup.u64 = 0;
	cmdsetup.s.iq_no = iq_no;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (skb->encapsulation) {
			cmdsetup.s.tnl_csum = 1;
			stats->tx_vxlan++;
		} else {
			cmdsetup.s.transport_csum = 1;
		}
	}
	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		cmdsetup.s.timestamp = 1;
	}

	if (!skb_shinfo(skb)->nr_frags) {
		cmdsetup.s.u.datasize = skb->len;
		octnet_prepare_pci_cmd(oct, &ndata.cmd, &cmdsetup, tag);
		/* Offload checksum calculation for TCP/UDP packets */
		dptr = dma_map_single(&oct->pci_dev->dev,
				      skb->data,
				      skb->len,
				      DMA_TO_DEVICE);
		if (dma_mapping_error(&oct->pci_dev->dev, dptr)) {
			dev_err(&oct->pci_dev->dev, "%s DMA mapping error 1\n",
				__func__);
			return NETDEV_TX_BUSY;
		}

		ndata.cmd.cmd3.dptr = dptr;
		finfo->dptr = dptr;
		ndata.reqtype = REQTYPE_NORESP_NET;

	} else {
		struct skb_frag_struct *frag;
		struct octnic_gather *g;
		int i, frags;

		spin_lock(&lio->glist_lock[q_idx]);
		g = (struct octnic_gather *)
			lio_list_delete_head(&lio->glist[q_idx]);
		spin_unlock(&lio->glist_lock[q_idx]);

		if (!g) {
			netif_info(lio, tx_err, lio->netdev,
				   "Transmit scatter gather: glist null!\n");
			goto lio_xmit_failed;
		}

		cmdsetup.s.gather = 1;
		cmdsetup.s.u.gatherptrs = (skb_shinfo(skb)->nr_frags + 1);
		octnet_prepare_pci_cmd(oct, &ndata.cmd, &cmdsetup, tag);

		memset(g->sg, 0, g->sg_size);

		g->sg[0].ptr[0] = dma_map_single(&oct->pci_dev->dev,
						 skb->data,
						 (skb->len - skb->data_len),
						 DMA_TO_DEVICE);
		if (dma_mapping_error(&oct->pci_dev->dev, g->sg[0].ptr[0])) {
			dev_err(&oct->pci_dev->dev, "%s DMA mapping error 2\n",
				__func__);
			return NETDEV_TX_BUSY;
		}
		add_sg_size(&g->sg[0], (skb->len - skb->data_len), 0);

		frags = skb_shinfo(skb)->nr_frags;
		i = 1;
		while (frags--) {
			frag = &skb_shinfo(skb)->frags[i - 1];

			g->sg[(i >> 2)].ptr[(i & 3)] =
				dma_map_page(&oct->pci_dev->dev,
					     frag->page.p,
					     frag->page_offset,
					     frag->size,
					     DMA_TO_DEVICE);
			if (dma_mapping_error(&oct->pci_dev->dev,
					      g->sg[i >> 2].ptr[i & 3])) {
				dma_unmap_single(&oct->pci_dev->dev,
						 g->sg[0].ptr[0],
						 skb->len - skb->data_len,
						 DMA_TO_DEVICE);
				for (j = 1; j < i; j++) {
					frag = &skb_shinfo(skb)->frags[j - 1];
					dma_unmap_page(&oct->pci_dev->dev,
						       g->sg[j >> 2].ptr[j & 3],
						       frag->size,
						       DMA_TO_DEVICE);
				}
				dev_err(&oct->pci_dev->dev, "%s DMA mapping error 3\n",
					__func__);
				return NETDEV_TX_BUSY;
			}

			add_sg_size(&g->sg[(i >> 2)], frag->size, (i & 3));
			i++;
		}

		dptr = g->sg_dma_ptr;

		ndata.cmd.cmd3.dptr = dptr;
		finfo->dptr = dptr;
		finfo->g = g;

		ndata.reqtype = REQTYPE_NORESP_NET_SG;
	}

	irh = (struct octeon_instr_irh *)&ndata.cmd.cmd3.irh;
	tx_info = (union tx_info *)&ndata.cmd.cmd3.ossp[0];

	if (skb_shinfo(skb)->gso_size) {
		tx_info->s.gso_size = skb_shinfo(skb)->gso_size;
		tx_info->s.gso_segs = skb_shinfo(skb)->gso_segs;
	}

	/* HW insert VLAN tag */
	if (skb_vlan_tag_present(skb)) {
		irh->priority = skb_vlan_tag_get(skb) >> VLAN_PRIO_SHIFT;
		irh->vlan = skb_vlan_tag_get(skb) & VLAN_VID_MASK;
	}

	xmit_more = netdev_xmit_more();

	if (unlikely(cmdsetup.s.timestamp))
		status = send_nic_timestamp_pkt(oct, &ndata, finfo, xmit_more);
	else
		status = octnet_send_nic_data_pkt(oct, &ndata, xmit_more);
	if (status == IQ_SEND_FAILED)
		goto lio_xmit_failed;

	netif_info(lio, tx_queued, lio->netdev, "Transmit queued successfully\n");

	if (status == IQ_SEND_STOP) {
		dev_err(&oct->pci_dev->dev, "Rcvd IQ_SEND_STOP signal; stopping IQ-%d\n",
			iq_no);
		netif_stop_subqueue(netdev, q_idx);
	}

	netif_trans_update(netdev);

	if (tx_info->s.gso_segs)
		stats->tx_done += tx_info->s.gso_segs;
	else
		stats->tx_done++;
	stats->tx_tot_bytes += ndata.datasize;

	return NETDEV_TX_OK;

lio_xmit_failed:
	stats->tx_dropped++;
	netif_info(lio, tx_err, lio->netdev, "IQ%d Transmit dropped:%llu\n",
		   iq_no, stats->tx_dropped);
	if (dptr)
		dma_unmap_single(&oct->pci_dev->dev, dptr,
				 ndata.datasize, DMA_TO_DEVICE);

	octeon_ring_doorbell_locked(oct, iq_no);

	tx_buffer_free(skb);
	return NETDEV_TX_OK;
}

/** \brief Network device Tx timeout
 * @param netdev    pointer to network device
 */
static void liquidio_tx_timeout(struct net_device *netdev)
{
	struct lio *lio;

	lio = GET_LIO(netdev);

	netif_info(lio, tx_err, lio->netdev,
		   "Transmit timeout tx_dropped:%ld, waking up queues now!!\n",
		   netdev->stats.tx_dropped);
	netif_trans_update(netdev);
	wake_txqs(netdev);
}

static int
liquidio_vlan_rx_add_vid(struct net_device *netdev,
			 __be16 proto __attribute__((unused)), u16 vid)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_ADD_VLAN_FILTER;
	nctrl.ncmd.s.param1 = vid;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "Add VLAN filter failed in core (ret: 0x%x)\n",
			ret);
		return -EPERM;
	}

	return 0;
}

static int
liquidio_vlan_rx_kill_vid(struct net_device *netdev,
			  __be16 proto __attribute__((unused)), u16 vid)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_DEL_VLAN_FILTER;
	nctrl.ncmd.s.param1 = vid;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "Del VLAN filter failed in core (ret: 0x%x)\n",
			ret);
		if (ret > 0)
			ret = -EIO;
	}
	return ret;
}

/** Sending command to enable/disable RX checksum offload
 * @param netdev                pointer to network device
 * @param command               OCTNET_CMD_TNL_RX_CSUM_CTL
 * @param rx_cmd_bit            OCTNET_CMD_RXCSUM_ENABLE/
 *                              OCTNET_CMD_RXCSUM_DISABLE
 * @returns                     SUCCESS or FAILURE
 */
static int liquidio_set_rxcsum_command(struct net_device *netdev, int command,
				       u8 rx_cmd)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = command;
	nctrl.ncmd.s.param1 = rx_cmd;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "DEVFLAGS RXCSUM change failed in core (ret:0x%x)\n",
			ret);
		if (ret > 0)
			ret = -EIO;
	}
	return ret;
}

/** Sending command to add/delete VxLAN UDP port to firmware
 * @param netdev                pointer to network device
 * @param command               OCTNET_CMD_VXLAN_PORT_CONFIG
 * @param vxlan_port            VxLAN port to be added or deleted
 * @param vxlan_cmd_bit         OCTNET_CMD_VXLAN_PORT_ADD,
 *                              OCTNET_CMD_VXLAN_PORT_DEL
 * @returns                     SUCCESS or FAILURE
 */
static int liquidio_vxlan_port_command(struct net_device *netdev, int command,
				       u16 vxlan_port, u8 vxlan_cmd_bit)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = command;
	nctrl.ncmd.s.more = vxlan_cmd_bit;
	nctrl.ncmd.s.param1 = vxlan_port;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev,
			"DEVFLAGS VxLAN port add/delete failed in core (ret : 0x%x)\n",
			ret);
		if (ret > 0)
			ret = -EIO;
	}
	return ret;
}

/** \brief Net device fix features
 * @param netdev  pointer to network device
 * @param request features requested
 * @returns updated features list
 */
static netdev_features_t liquidio_fix_features(struct net_device *netdev,
					       netdev_features_t request)
{
	struct lio *lio = netdev_priv(netdev);

	if ((request & NETIF_F_RXCSUM) &&
	    !(lio->dev_capability & NETIF_F_RXCSUM))
		request &= ~NETIF_F_RXCSUM;

	if ((request & NETIF_F_HW_CSUM) &&
	    !(lio->dev_capability & NETIF_F_HW_CSUM))
		request &= ~NETIF_F_HW_CSUM;

	if ((request & NETIF_F_TSO) && !(lio->dev_capability & NETIF_F_TSO))
		request &= ~NETIF_F_TSO;

	if ((request & NETIF_F_TSO6) && !(lio->dev_capability & NETIF_F_TSO6))
		request &= ~NETIF_F_TSO6;

	if ((request & NETIF_F_LRO) && !(lio->dev_capability & NETIF_F_LRO))
		request &= ~NETIF_F_LRO;

	/* Disable LRO if RXCSUM is off */
	if (!(request & NETIF_F_RXCSUM) && (netdev->features & NETIF_F_LRO) &&
	    (lio->dev_capability & NETIF_F_LRO))
		request &= ~NETIF_F_LRO;

	return request;
}

/** \brief Net device set features
 * @param netdev  pointer to network device
 * @param features features to enable/disable
 */
static int liquidio_set_features(struct net_device *netdev,
				 netdev_features_t features)
{
	struct lio *lio = netdev_priv(netdev);

	if (!((netdev->features ^ features) & NETIF_F_LRO))
		return 0;

	if ((features & NETIF_F_LRO) && (lio->dev_capability & NETIF_F_LRO))
		liquidio_set_feature(netdev, OCTNET_CMD_LRO_ENABLE,
				     OCTNIC_LROIPV4 | OCTNIC_LROIPV6);
	else if (!(features & NETIF_F_LRO) &&
		 (lio->dev_capability & NETIF_F_LRO))
		liquidio_set_feature(netdev, OCTNET_CMD_LRO_DISABLE,
				     OCTNIC_LROIPV4 | OCTNIC_LROIPV6);
	if (!(netdev->features & NETIF_F_RXCSUM) &&
	    (lio->enc_dev_capability & NETIF_F_RXCSUM) &&
	    (features & NETIF_F_RXCSUM))
		liquidio_set_rxcsum_command(netdev, OCTNET_CMD_TNL_RX_CSUM_CTL,
					    OCTNET_CMD_RXCSUM_ENABLE);
	else if ((netdev->features & NETIF_F_RXCSUM) &&
		 (lio->enc_dev_capability & NETIF_F_RXCSUM) &&
		 !(features & NETIF_F_RXCSUM))
		liquidio_set_rxcsum_command(netdev, OCTNET_CMD_TNL_RX_CSUM_CTL,
					    OCTNET_CMD_RXCSUM_DISABLE);

	return 0;
}

static void liquidio_add_vxlan_port(struct net_device *netdev,
				    struct udp_tunnel_info *ti)
{
	if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
		return;

	liquidio_vxlan_port_command(netdev,
				    OCTNET_CMD_VXLAN_PORT_CONFIG,
				    htons(ti->port),
				    OCTNET_CMD_VXLAN_PORT_ADD);
}

static void liquidio_del_vxlan_port(struct net_device *netdev,
				    struct udp_tunnel_info *ti)
{
	if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
		return;

	liquidio_vxlan_port_command(netdev,
				    OCTNET_CMD_VXLAN_PORT_CONFIG,
				    htons(ti->port),
				    OCTNET_CMD_VXLAN_PORT_DEL);
}

static const struct net_device_ops lionetdevops = {
	.ndo_open		= liquidio_open,
	.ndo_stop		= liquidio_stop,
	.ndo_start_xmit		= liquidio_xmit,
	.ndo_get_stats64	= liquidio_get_stats64,
	.ndo_set_mac_address	= liquidio_set_mac,
	.ndo_set_rx_mode	= liquidio_set_mcast_list,
	.ndo_tx_timeout		= liquidio_tx_timeout,
	.ndo_vlan_rx_add_vid    = liquidio_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = liquidio_vlan_rx_kill_vid,
	.ndo_change_mtu		= liquidio_change_mtu,
	.ndo_do_ioctl		= liquidio_ioctl,
	.ndo_fix_features	= liquidio_fix_features,
	.ndo_set_features	= liquidio_set_features,
	.ndo_udp_tunnel_add     = liquidio_add_vxlan_port,
	.ndo_udp_tunnel_del     = liquidio_del_vxlan_port,
};

static int lio_nic_info(struct octeon_recv_info *recv_info, void *buf)
{
	struct octeon_device *oct = (struct octeon_device *)buf;
	struct octeon_recv_pkt *recv_pkt = recv_info->recv_pkt;
	union oct_link_status *ls;
	int gmxport = 0;
	int i;

	if (recv_pkt->buffer_size[0] != (sizeof(*ls) + OCT_DROQ_INFO_SIZE)) {
		dev_err(&oct->pci_dev->dev, "Malformed NIC_INFO, len=%d, ifidx=%d\n",
			recv_pkt->buffer_size[0],
			recv_pkt->rh.r_nic_info.gmxport);
		goto nic_info_err;
	}

	gmxport = recv_pkt->rh.r_nic_info.gmxport;
	ls = (union oct_link_status *)(get_rbd(recv_pkt->buffer_ptr[0]) +
		OCT_DROQ_INFO_SIZE);

	octeon_swap_8B_data((u64 *)ls, (sizeof(union oct_link_status)) >> 3);

	for (i = 0; i < oct->ifcount; i++) {
		if (oct->props[i].gmxport == gmxport) {
			update_link_status(oct->props[i].netdev, ls);
			break;
		}
	}

nic_info_err:
	for (i = 0; i < recv_pkt->buffer_count; i++)
		recv_buffer_free(recv_pkt->buffer_ptr[i]);
	octeon_free_recv_info(recv_info);
	return 0;
}

/**
 * \brief Setup network interfaces
 * @param octeon_dev  octeon device
 *
 * Called during init time for each device. It assumes the NIC
 * is already up and running.  The link information for each
 * interface is passed in link_info.
 */
static int setup_nic_devices(struct octeon_device *octeon_dev)
{
	int retval, num_iqueues, num_oqueues;
	u32 resp_size, data_size;
	struct liquidio_if_cfg_resp *resp;
	struct octeon_soft_command *sc;
	union oct_nic_if_cfg if_cfg;
	struct octdev_props *props;
	struct net_device *netdev;
	struct lio_version *vdata;
	struct lio *lio = NULL;
	u8 mac[ETH_ALEN], i, j;
	u32 ifidx_or_pfnum;

	ifidx_or_pfnum = octeon_dev->pf_num;

	/* This is to handle link status changes */
	octeon_register_dispatch_fn(octeon_dev, OPCODE_NIC, OPCODE_NIC_INFO,
				    lio_nic_info, octeon_dev);

	/* REQTYPE_RESP_NET and REQTYPE_SOFT_COMMAND do not have free functions.
	 * They are handled directly.
	 */
	octeon_register_reqtype_free_fn(octeon_dev, REQTYPE_NORESP_NET,
					free_netbuf);

	octeon_register_reqtype_free_fn(octeon_dev, REQTYPE_NORESP_NET_SG,
					free_netsgbuf);

	octeon_register_reqtype_free_fn(octeon_dev, REQTYPE_RESP_NET_SG,
					free_netsgbuf_with_resp);

	for (i = 0; i < octeon_dev->ifcount; i++) {
		resp_size = sizeof(struct liquidio_if_cfg_resp);
		data_size = sizeof(struct lio_version);
		sc = (struct octeon_soft_command *)
			octeon_alloc_soft_command(octeon_dev, data_size,
						  resp_size, 0);
		resp = (struct liquidio_if_cfg_resp *)sc->virtrptr;
		vdata = (struct lio_version *)sc->virtdptr;

		*((u64 *)vdata) = 0;
		vdata->major = cpu_to_be16(LIQUIDIO_BASE_MAJOR_VERSION);
		vdata->minor = cpu_to_be16(LIQUIDIO_BASE_MINOR_VERSION);
		vdata->micro = cpu_to_be16(LIQUIDIO_BASE_MICRO_VERSION);

		if_cfg.u64 = 0;

		if_cfg.s.num_iqueues = octeon_dev->sriov_info.rings_per_vf;
		if_cfg.s.num_oqueues = octeon_dev->sriov_info.rings_per_vf;
		if_cfg.s.base_queue = 0;

		sc->iq_no = 0;

		octeon_prepare_soft_command(octeon_dev, sc, OPCODE_NIC,
					    OPCODE_NIC_IF_CFG, 0, if_cfg.u64,
					    0);

		init_completion(&sc->complete);
		sc->sc_status = OCTEON_REQUEST_PENDING;

		retval = octeon_send_soft_command(octeon_dev, sc);
		if (retval == IQ_SEND_FAILED) {
			dev_err(&octeon_dev->pci_dev->dev,
				"iq/oq config failed status: %x\n", retval);
			/* Soft instr is freed by driver in case of failure. */
			octeon_free_soft_command(octeon_dev, sc);
			return(-EIO);
		}

		/* Sleep on a wait queue till the cond flag indicates that the
		 * response arrived or timed-out.
		 */
		retval = wait_for_sc_completion_timeout(octeon_dev, sc, 0);
		if (retval)
			return retval;

		retval = resp->status;
		if (retval) {
			dev_err(&octeon_dev->pci_dev->dev,
				"iq/oq config failed, retval = %d\n", retval);
			WRITE_ONCE(sc->caller_is_done, true);
			return -EIO;
		}

		snprintf(octeon_dev->fw_info.liquidio_firmware_version,
			 32, "%s",
			 resp->cfg_info.liquidio_firmware_version);

		octeon_swap_8B_data((u64 *)(&resp->cfg_info),
				    (sizeof(struct liquidio_if_cfg_info)) >> 3);

		num_iqueues = hweight64(resp->cfg_info.iqmask);
		num_oqueues = hweight64(resp->cfg_info.oqmask);

		if (!(num_iqueues) || !(num_oqueues)) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Got bad iqueues (%016llx) or oqueues (%016llx) from firmware.\n",
				resp->cfg_info.iqmask, resp->cfg_info.oqmask);
			WRITE_ONCE(sc->caller_is_done, true);
			goto setup_nic_dev_done;
		}
		dev_dbg(&octeon_dev->pci_dev->dev,
			"interface %d, iqmask %016llx, oqmask %016llx, numiqueues %d, numoqueues %d\n",
			i, resp->cfg_info.iqmask, resp->cfg_info.oqmask,
			num_iqueues, num_oqueues);

		netdev = alloc_etherdev_mq(LIO_SIZE, num_iqueues);

		if (!netdev) {
			dev_err(&octeon_dev->pci_dev->dev, "Device allocation failed\n");
			WRITE_ONCE(sc->caller_is_done, true);
			goto setup_nic_dev_done;
		}

		SET_NETDEV_DEV(netdev, &octeon_dev->pci_dev->dev);

		/* Associate the routines that will handle different
		 * netdev tasks.
		 */
		netdev->netdev_ops = &lionetdevops;

		lio = GET_LIO(netdev);

		memset(lio, 0, sizeof(struct lio));

		lio->ifidx = ifidx_or_pfnum;

		props = &octeon_dev->props[i];
		props->gmxport = resp->cfg_info.linfo.gmxport;
		props->netdev = netdev;

		lio->linfo.num_rxpciq = num_oqueues;
		lio->linfo.num_txpciq = num_iqueues;

		for (j = 0; j < num_oqueues; j++) {
			lio->linfo.rxpciq[j].u64 =
			    resp->cfg_info.linfo.rxpciq[j].u64;
		}
		for (j = 0; j < num_iqueues; j++) {
			lio->linfo.txpciq[j].u64 =
			    resp->cfg_info.linfo.txpciq[j].u64;
		}

		lio->linfo.hw_addr = resp->cfg_info.linfo.hw_addr;
		lio->linfo.gmxport = resp->cfg_info.linfo.gmxport;
		lio->linfo.link.u64 = resp->cfg_info.linfo.link.u64;
		lio->linfo.macaddr_is_admin_asgnd =
			resp->cfg_info.linfo.macaddr_is_admin_asgnd;
		lio->linfo.macaddr_spoofchk =
			resp->cfg_info.linfo.macaddr_spoofchk;

		lio->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

		lio->dev_capability = NETIF_F_HIGHDMA
				      | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM
				      | NETIF_F_SG | NETIF_F_RXCSUM
				      | NETIF_F_TSO | NETIF_F_TSO6
				      | NETIF_F_GRO
				      | NETIF_F_LRO;
		netif_set_gso_max_size(netdev, OCTNIC_GSO_MAX_SIZE);

		/* Copy of transmit encapsulation capabilities:
		 * TSO, TSO6, Checksums for this device
		 */
		lio->enc_dev_capability = NETIF_F_IP_CSUM
					  | NETIF_F_IPV6_CSUM
					  | NETIF_F_GSO_UDP_TUNNEL
					  | NETIF_F_HW_CSUM | NETIF_F_SG
					  | NETIF_F_RXCSUM
					  | NETIF_F_TSO | NETIF_F_TSO6
					  | NETIF_F_LRO;

		netdev->hw_enc_features =
		    (lio->enc_dev_capability & ~NETIF_F_LRO);
		netdev->vlan_features = lio->dev_capability;
		/* Add any unchangeable hw features */
		lio->dev_capability |= NETIF_F_HW_VLAN_CTAG_FILTER |
				       NETIF_F_HW_VLAN_CTAG_RX |
				       NETIF_F_HW_VLAN_CTAG_TX;

		netdev->features = (lio->dev_capability & ~NETIF_F_LRO);

		netdev->hw_features = lio->dev_capability;
		netdev->hw_features &= ~NETIF_F_HW_VLAN_CTAG_RX;

		/* MTU range: 68 - 16000 */
		netdev->min_mtu = LIO_MIN_MTU_SIZE;
		netdev->max_mtu = LIO_MAX_MTU_SIZE;

		WRITE_ONCE(sc->caller_is_done, true);

		/* Point to the  properties for octeon device to which this
		 * interface belongs.
		 */
		lio->oct_dev = octeon_dev;
		lio->octprops = props;
		lio->netdev = netdev;

		dev_dbg(&octeon_dev->pci_dev->dev,
			"if%d gmx: %d hw_addr: 0x%llx\n", i,
			lio->linfo.gmxport, CVM_CAST64(lio->linfo.hw_addr));

		/* 64-bit swap required on LE machines */
		octeon_swap_8B_data(&lio->linfo.hw_addr, 1);
		for (j = 0; j < ETH_ALEN; j++)
			mac[j] = *((u8 *)(((u8 *)&lio->linfo.hw_addr) + 2 + j));

		/* Copy MAC Address to OS network device structure */
		ether_addr_copy(netdev->dev_addr, mac);

		if (liquidio_setup_io_queues(octeon_dev, i,
					     lio->linfo.num_txpciq,
					     lio->linfo.num_rxpciq)) {
			dev_err(&octeon_dev->pci_dev->dev, "I/O queues creation failed\n");
			goto setup_nic_dev_free;
		}

		ifstate_set(lio, LIO_IFSTATE_DROQ_OPS);

		/* For VFs, enable Octeon device interrupts here,
		 * as this is contingent upon IO queue setup
		 */
		octeon_dev->fn_list.enable_interrupt(octeon_dev,
						     OCTEON_ALL_INTR);

		/* By default all interfaces on a single Octeon uses the same
		 * tx and rx queues
		 */
		lio->txq = lio->linfo.txpciq[0].s.q_no;
		lio->rxq = lio->linfo.rxpciq[0].s.q_no;

		lio->tx_qsize = octeon_get_tx_qsize(octeon_dev, lio->txq);
		lio->rx_qsize = octeon_get_rx_qsize(octeon_dev, lio->rxq);

		if (lio_setup_glists(octeon_dev, lio, num_iqueues)) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Gather list allocation failed\n");
			goto setup_nic_dev_free;
		}

		/* Register ethtool support */
		liquidio_set_ethtool_ops(netdev);
		if (lio->oct_dev->chip_id == OCTEON_CN23XX_VF_VID)
			octeon_dev->priv_flags = OCT_PRIV_FLAG_DEFAULT;
		else
			octeon_dev->priv_flags = 0x0;

		if (netdev->features & NETIF_F_LRO)
			liquidio_set_feature(netdev, OCTNET_CMD_LRO_ENABLE,
					     OCTNIC_LROIPV4 | OCTNIC_LROIPV6);

		if (setup_link_status_change_wq(netdev))
			goto setup_nic_dev_free;

		if (setup_rx_oom_poll_fn(netdev))
			goto setup_nic_dev_free;

		/* Register the network device with the OS */
		if (register_netdev(netdev)) {
			dev_err(&octeon_dev->pci_dev->dev, "Device registration failed\n");
			goto setup_nic_dev_free;
		}

		dev_dbg(&octeon_dev->pci_dev->dev,
			"Setup NIC ifidx:%d mac:%02x%02x%02x%02x%02x%02x\n",
			i, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		netif_carrier_off(netdev);
		lio->link_changes++;

		ifstate_set(lio, LIO_IFSTATE_REGISTERED);

		/* Sending command to firmware to enable Rx checksum offload
		 * by default at the time of setup of Liquidio driver for
		 * this device
		 */
		liquidio_set_rxcsum_command(netdev, OCTNET_CMD_TNL_RX_CSUM_CTL,
					    OCTNET_CMD_RXCSUM_ENABLE);
		liquidio_set_feature(netdev, OCTNET_CMD_TNL_TX_CSUM_CTL,
				     OCTNET_CMD_TXCSUM_ENABLE);

		dev_dbg(&octeon_dev->pci_dev->dev,
			"NIC ifidx:%d Setup successful\n", i);

		octeon_dev->no_speed_setting = 1;
	}

	return 0;

setup_nic_dev_free:

	while (i--) {
		dev_err(&octeon_dev->pci_dev->dev,
			"NIC ifidx:%d Setup failed\n", i);
		liquidio_destroy_nic_device(octeon_dev, i);
	}

setup_nic_dev_done:

	return -ENODEV;
}

/**
 * \brief initialize the NIC
 * @param oct octeon device
 *
 * This initialization routine is called once the Octeon device application is
 * up and running
 */
static int liquidio_init_nic_module(struct octeon_device *oct)
{
	int num_nic_ports = 1;
	int i, retval = 0;

	dev_dbg(&oct->pci_dev->dev, "Initializing network interfaces\n");

	/* only default iq and oq were initialized
	 * initialize the rest as well run port_config command for each port
	 */
	oct->ifcount = num_nic_ports;
	memset(oct->props, 0,
	       sizeof(struct octdev_props) * num_nic_ports);

	for (i = 0; i < MAX_OCTEON_LINKS; i++)
		oct->props[i].gmxport = -1;

	retval = setup_nic_devices(oct);
	if (retval) {
		dev_err(&oct->pci_dev->dev, "Setup NIC devices failed\n");
		goto octnet_init_failure;
	}

	dev_dbg(&oct->pci_dev->dev, "Network interfaces ready\n");

	return retval;

octnet_init_failure:

	oct->ifcount = 0;

	return retval;
}

/**
 * \brief Device initialization for each Octeon device that is probed
 * @param octeon_dev  octeon device
 */
static int octeon_device_init(struct octeon_device *oct)
{
	u32 rev_id;
	int j;

	atomic_set(&oct->status, OCT_DEV_BEGIN_STATE);

	/* Enable access to the octeon device and make its DMA capability
	 * known to the OS.
	 */
	if (octeon_pci_os_setup(oct))
		return 1;
	atomic_set(&oct->status, OCT_DEV_PCI_ENABLE_DONE);

	oct->chip_id = OCTEON_CN23XX_VF_VID;
	pci_read_config_dword(oct->pci_dev, 8, &rev_id);
	oct->rev_id = rev_id & 0xff;

	if (cn23xx_setup_octeon_vf_device(oct))
		return 1;

	atomic_set(&oct->status, OCT_DEV_PCI_MAP_DONE);

	oct->app_mode = CVM_DRV_NIC_APP;

	/* Initialize the dispatch mechanism used to push packets arriving on
	 * Octeon Output queues.
	 */
	if (octeon_init_dispatch_list(oct))
		return 1;

	atomic_set(&oct->status, OCT_DEV_DISPATCH_INIT_DONE);

	if (octeon_set_io_queues_off(oct)) {
		dev_err(&oct->pci_dev->dev, "setting io queues off failed\n");
		return 1;
	}

	if (oct->fn_list.setup_device_regs(oct)) {
		dev_err(&oct->pci_dev->dev, "device registers configuration failed\n");
		return 1;
	}

	/* Initialize soft command buffer pool */
	if (octeon_setup_sc_buffer_pool(oct)) {
		dev_err(&oct->pci_dev->dev, "sc buffer pool allocation failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_SC_BUFF_POOL_INIT_DONE);

	/* Setup the data structures that manage this Octeon's Input queues. */
	if (octeon_setup_instr_queues(oct)) {
		dev_err(&oct->pci_dev->dev, "instruction queue initialization failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_INSTR_QUEUE_INIT_DONE);

	/* Initialize lists to manage the requests of different types that
	 * arrive from user & kernel applications for this octeon device.
	 */
	if (octeon_setup_response_list(oct)) {
		dev_err(&oct->pci_dev->dev, "Response list allocation failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_RESP_LIST_INIT_DONE);

	if (octeon_setup_output_queues(oct)) {
		dev_err(&oct->pci_dev->dev, "Output queue initialization failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_DROQ_INIT_DONE);

	if (oct->fn_list.setup_mbox(oct)) {
		dev_err(&oct->pci_dev->dev, "Mailbox setup failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_MBOX_SETUP_DONE);

	if (octeon_allocate_ioq_vector(oct, oct->sriov_info.rings_per_vf)) {
		dev_err(&oct->pci_dev->dev, "ioq vector allocation failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_MSIX_ALLOC_VECTOR_DONE);

	dev_info(&oct->pci_dev->dev, "OCTEON_CN23XX VF Version: %s, %d ioqs\n",
		 LIQUIDIO_VERSION, oct->sriov_info.rings_per_vf);

	/* Setup the interrupt handler and record the INT SUM register address*/
	if (octeon_setup_interrupt(oct, oct->sriov_info.rings_per_vf))
		return 1;

	atomic_set(&oct->status, OCT_DEV_INTR_SET_DONE);

	/* ***************************************************************
	 * The interrupts need to be enabled for the PF<-->VF handshake.
	 * They are [re]-enabled after the PF<-->VF handshake so that the
	 * correct OQ tick value is used (i.e. the value retrieved from
	 * the PF as part of the handshake).
	 */

	/* Enable Octeon device interrupts */
	oct->fn_list.enable_interrupt(oct, OCTEON_ALL_INTR);

	if (cn23xx_octeon_pfvf_handshake(oct))
		return 1;

	/* Here we [re]-enable the interrupts so that the correct OQ tick value
	 * is used (i.e. the value that was retrieved during the handshake)
	 */

	/* Enable Octeon device interrupts */
	oct->fn_list.enable_interrupt(oct, OCTEON_ALL_INTR);
	/* *************************************************************** */

	/* Enable the input and output queues for this Octeon device */
	if (oct->fn_list.enable_io_queues(oct)) {
		dev_err(&oct->pci_dev->dev, "enabling io queues failed\n");
		return 1;
	}

	atomic_set(&oct->status, OCT_DEV_IO_QUEUES_DONE);

	atomic_set(&oct->status, OCT_DEV_HOST_OK);

	/* Send Credit for Octeon Output queues. Credits are always sent after
	 * the output queue is enabled.
	 */
	for (j = 0; j < oct->num_oqs; j++)
		writel(oct->droq[j]->max_count, oct->droq[j]->pkts_credit_reg);

	/* Packets can start arriving on the output queues from this point. */

	atomic_set(&oct->status, OCT_DEV_CORE_OK);

	atomic_set(&oct->status, OCT_DEV_RUNNING);

	if (liquidio_init_nic_module(oct))
		return 1;

	return 0;
}

static int __init liquidio_vf_init(void)
{
	octeon_init_device_list(0);
	return pci_register_driver(&liquidio_vf_pci_driver);
}

static void __exit liquidio_vf_exit(void)
{
	pci_unregister_driver(&liquidio_vf_pci_driver);

	pr_info("LiquidIO_VF network module is now unloaded\n");
}

module_init(liquidio_vf_init);
module_exit(liquidio_vf_exit);
