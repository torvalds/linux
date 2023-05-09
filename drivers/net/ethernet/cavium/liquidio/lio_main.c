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
#include <linux/firmware.h>
#include <net/vxlan.h>
#include <linux/kthread.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"
#include "cn66xx_regs.h"
#include "cn66xx_device.h"
#include "cn68xx_device.h"
#include "cn23xx_pf_device.h"
#include "liquidio_image.h"
#include "lio_vf_rep.h"

MODULE_AUTHOR("Cavium Networks, <support@cavium.com>");
MODULE_DESCRIPTION("Cavium LiquidIO Intelligent Server Adapter Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(LIO_FW_DIR LIO_FW_BASE_NAME LIO_210SV_NAME
		"_" LIO_FW_NAME_TYPE_NIC LIO_FW_NAME_SUFFIX);
MODULE_FIRMWARE(LIO_FW_DIR LIO_FW_BASE_NAME LIO_210NV_NAME
		"_" LIO_FW_NAME_TYPE_NIC LIO_FW_NAME_SUFFIX);
MODULE_FIRMWARE(LIO_FW_DIR LIO_FW_BASE_NAME LIO_410NV_NAME
		"_" LIO_FW_NAME_TYPE_NIC LIO_FW_NAME_SUFFIX);
MODULE_FIRMWARE(LIO_FW_DIR LIO_FW_BASE_NAME LIO_23XX_NAME
		"_" LIO_FW_NAME_TYPE_NIC LIO_FW_NAME_SUFFIX);

static int ddr_timeout = 10000;
module_param(ddr_timeout, int, 0644);
MODULE_PARM_DESC(ddr_timeout,
		 "Number of milliseconds to wait for DDR initialization. 0 waits for ddr_timeout to be set to non-zero value before starting to check");

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "NETIF_MSG debug bits");

static char fw_type[LIO_MAX_FW_TYPE_LEN] = LIO_FW_NAME_TYPE_AUTO;
module_param_string(fw_type, fw_type, sizeof(fw_type), 0444);
MODULE_PARM_DESC(fw_type, "Type of firmware to be loaded (default is \"auto\"), which uses firmware in flash, if present, else loads \"nic\".");

static u32 console_bitmask;
module_param(console_bitmask, int, 0644);
MODULE_PARM_DESC(console_bitmask,
		 "Bitmask indicating which consoles have debug output redirected to syslog.");

/**
 * octeon_console_debug_enabled - determines if a given console has debug enabled.
 * @console: console to check
 * Return:  1 = enabled. 0 otherwise
 */
static int octeon_console_debug_enabled(u32 console)
{
	return (console_bitmask >> (console)) & 0x1;
}

/* Polling interval for determining when NIC application is alive */
#define LIQUIDIO_STARTER_POLL_INTERVAL_MS 100

/* runtime link query interval */
#define LIQUIDIO_LINK_QUERY_INTERVAL_MS         1000
/* update localtime to octeon firmware every 60 seconds.
 * make firmware to use same time reference, so that it will be easy to
 * correlate firmware logged events/errors with host events, for debugging.
 */
#define LIO_SYNC_OCTEON_TIME_INTERVAL_MS 60000

/* time to wait for possible in-flight requests in milliseconds */
#define WAIT_INFLIGHT_REQUEST	msecs_to_jiffies(1000)

struct oct_link_status_resp {
	u64 rh;
	struct oct_link_info link_info;
	u64 status;
};

struct oct_timestamp_resp {
	u64 rh;
	u64 timestamp;
	u64 status;
};

#define OCT_TIMESTAMP_RESP_SIZE (sizeof(struct oct_timestamp_resp))

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

/* Octeon device properties to be used by the NIC module.
 * Each octeon device in the system will be represented
 * by this structure in the NIC module.
 */

#define OCTNIC_GSO_MAX_HEADER_SIZE 128
#define OCTNIC_GSO_MAX_SIZE                                                    \
	(CN23XX_DEFAULT_INPUT_JABBER - OCTNIC_GSO_MAX_HEADER_SIZE)

struct handshake {
	struct completion init;
	struct completion started;
	struct pci_dev *pci_dev;
	int init_ok;
	int started_ok;
};

#ifdef CONFIG_PCI_IOV
static int liquidio_enable_sriov(struct pci_dev *dev, int num_vfs);
#endif

static int octeon_dbg_console_print(struct octeon_device *oct, u32 console_num,
				    char *prefix, char *suffix);

static int octeon_device_init(struct octeon_device *);
static int liquidio_stop(struct net_device *netdev);
static void liquidio_remove(struct pci_dev *pdev);
static int liquidio_probe(struct pci_dev *pdev,
			  const struct pci_device_id *ent);
static int liquidio_set_vf_link_state(struct net_device *netdev, int vfidx,
				      int linkstate);

static struct handshake handshake[MAX_OCTEON_DEVICES];
static struct completion first_stage;

static void octeon_droq_bh(struct tasklet_struct *t)
{
	int q_no;
	int reschedule = 0;
	struct octeon_device_priv *oct_priv = from_tasklet(oct_priv, t,
							  droq_tasklet);
	struct octeon_device *oct = oct_priv->dev;

	for (q_no = 0; q_no < MAX_OCTEON_OUTPUT_QUEUES(oct); q_no++) {
		if (!(oct->io_qmask.oq & BIT_ULL(q_no)))
			continue;
		reschedule |= octeon_droq_process_packets(oct, oct->droq[q_no],
							  MAX_PACKET_BUDGET);
		lio_enable_irq(oct->droq[q_no], NULL);

		if (OCTEON_CN23XX_PF(oct) && oct->msix_on) {
			/* set time and cnt interrupt thresholds for this DROQ
			 * for NAPI
			 */
			int adjusted_q_no = q_no + oct->sriov_info.pf_srn;

			octeon_write_csr64(
			    oct, CN23XX_SLI_OQ_PKT_INT_LEVELS(adjusted_q_no),
			    0x5700000040ULL);
			octeon_write_csr64(
			    oct, CN23XX_SLI_OQ_PKTS_SENT(adjusted_q_no), 0);
		}
	}

	if (reschedule)
		tasklet_schedule(&oct_priv->droq_tasklet);
}

static int lio_wait_for_oq_pkts(struct octeon_device *oct)
{
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	int retry = 100, pkt_cnt = 0, pending_pkts = 0;
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
 * force_io_queues_off - Forces all IO queues off on a given device
 * @oct: Pointer to Octeon device
 */
static void force_io_queues_off(struct octeon_device *oct)
{
	if ((oct->chip_id == OCTEON_CN66XX) ||
	    (oct->chip_id == OCTEON_CN68XX)) {
		/* Reset the Enable bits for Input Queues. */
		octeon_write_csr(oct, CN6XXX_SLI_PKT_INSTR_ENB, 0);

		/* Reset the Enable bits for Output Queues. */
		octeon_write_csr(oct, CN6XXX_SLI_PKT_OUT_ENB, 0);
	}
}

/**
 * pcierror_quiesce_device - Cause device to go quiet so it can be safely removed/reset/etc
 * @oct: Pointer to Octeon device
 */
static inline void pcierror_quiesce_device(struct octeon_device *oct)
{
	int i;

	/* Disable the input and output queues now. No more packets will
	 * arrive from Octeon, but we should wait for all packet processing
	 * to finish.
	 */
	force_io_queues_off(oct);

	/* To allow for in-flight requests */
	schedule_timeout_uninterruptible(WAIT_INFLIGHT_REQUEST);

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
 * cleanup_aer_uncorrect_error_status - Cleanup PCI AER uncorrectable error status
 * @dev: Pointer to PCI device
 */
static void cleanup_aer_uncorrect_error_status(struct pci_dev *dev)
{
	int pos = 0x100;
	u32 status, mask;

	pr_info("%s :\n", __func__);

	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, &status);
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &mask);
	if (dev->error_state == pci_channel_io_normal)
		status &= ~mask;        /* Clear corresponding nonfatal bits */
	else
		status &= mask;         /* Clear corresponding fatal bits */
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, status);
}

/**
 * stop_pci_io - Stop all PCI IO to a given device
 * @oct: Pointer to Octeon device
 */
static void stop_pci_io(struct octeon_device *oct)
{
	/* No more instructions will be forwarded. */
	atomic_set(&oct->status, OCT_DEV_IN_RESET);

	pci_disable_device(oct->pci_dev);

	/* Disable interrupts  */
	oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

	pcierror_quiesce_device(oct);

	/* Release the interrupt line */
	free_irq(oct->pci_dev->irq, oct);

	if (oct->flags & LIO_FLAG_MSI_ENABLED)
		pci_disable_msi(oct->pci_dev);

	dev_dbg(&oct->pci_dev->dev, "Device state is now %s\n",
		lio_get_state_string(&oct->status));

	/* making it a common function for all OCTEON models */
	cleanup_aer_uncorrect_error_status(oct->pci_dev);
}

/**
 * liquidio_pcie_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
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

	/* Always return a DISCONNECT. There is no support for recovery but only
	 * for a clean shutdown.
	 */
	return PCI_ERS_RESULT_DISCONNECT;
}

/**
 * liquidio_pcie_mmio_enabled - mmio handler
 * @pdev: Pointer to PCI device
 */
static pci_ers_result_t liquidio_pcie_mmio_enabled(struct pci_dev __maybe_unused *pdev)
{
	/* We should never hit this since we never ask for a reset for a Fatal
	 * Error. We always return DISCONNECT in io_error above.
	 * But play safe and return RECOVERED for now.
	 */
	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * liquidio_pcie_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * resembles the first-half of the octeon_resume routine.
 */
static pci_ers_result_t liquidio_pcie_slot_reset(struct pci_dev __maybe_unused *pdev)
{
	/* We should never hit this since we never ask for a reset for a Fatal
	 * Error. We always return DISCONNECT in io_error above.
	 * But play safe and return RECOVERED for now.
	 */
	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * liquidio_pcie_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the octeon_resume routine.
 */
static void liquidio_pcie_resume(struct pci_dev __maybe_unused *pdev)
{
	/* Nothing to be done here. */
}

#define liquidio_suspend NULL
#define liquidio_resume NULL

/* For PCI-E Advanced Error Recovery (AER) Interface */
static const struct pci_error_handlers liquidio_err_handler = {
	.error_detected = liquidio_pcie_error_detected,
	.mmio_enabled	= liquidio_pcie_mmio_enabled,
	.slot_reset	= liquidio_pcie_slot_reset,
	.resume		= liquidio_pcie_resume,
};

static const struct pci_device_id liquidio_pci_tbl[] = {
	{       /* 68xx */
		PCI_VENDOR_ID_CAVIUM, 0x91, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0
	},
	{       /* 66xx */
		PCI_VENDOR_ID_CAVIUM, 0x92, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0
	},
	{       /* 23xx pf */
		PCI_VENDOR_ID_CAVIUM, 0x9702, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0
	},
	{
		0, 0, 0, 0, 0, 0, 0
	}
};
MODULE_DEVICE_TABLE(pci, liquidio_pci_tbl);

static SIMPLE_DEV_PM_OPS(liquidio_pm_ops, liquidio_suspend, liquidio_resume);

static struct pci_driver liquidio_pci_driver = {
	.name		= "LiquidIO",
	.id_table	= liquidio_pci_tbl,
	.probe		= liquidio_probe,
	.remove		= liquidio_remove,
	.err_handler	= &liquidio_err_handler,    /* For AER */
	.driver.pm	= &liquidio_pm_ops,
#ifdef CONFIG_PCI_IOV
	.sriov_configure = liquidio_enable_sriov,
#endif
};

/**
 * liquidio_init_pci - register PCI driver
 */
static int liquidio_init_pci(void)
{
	return pci_register_driver(&liquidio_pci_driver);
}

/**
 * liquidio_deinit_pci - unregister PCI driver
 */
static void liquidio_deinit_pci(void)
{
	pci_unregister_driver(&liquidio_pci_driver);
}

/**
 * check_txq_status - Check Tx queue status, and take appropriate action
 * @lio: per-network private data
 * Return: 0 if full, number of queues woken up otherwise
 */
static inline int check_txq_status(struct lio *lio)
{
	int numqs = lio->netdev->real_num_tx_queues;
	int ret_val = 0;
	int q, iq;

	/* check each sub-queue state */
	for (q = 0; q < numqs; q++) {
		iq = lio->linfo.txpciq[q %
			lio->oct_dev->num_iqs].s.q_no;
		if (octnet_iq_is_full(lio->oct_dev, iq))
			continue;
		if (__netif_subqueue_stopped(lio->netdev, q)) {
			netif_wake_subqueue(lio->netdev, q);
			INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, iq,
						  tx_restart, 1);
			ret_val++;
		}
	}

	return ret_val;
}

/**
 * print_link_info -  Print link information
 * @netdev: network device
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
 * octnet_link_status_change - Routine to notify MTU change
 * @work: work_struct data structure
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
 * setup_link_status_change_wq - Sets up the mtu status change work
 * @netdev: network device
 */
static inline int setup_link_status_change_wq(struct net_device *netdev)
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

static inline void cleanup_link_status_change_wq(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	if (lio->link_status_wq.wq) {
		cancel_delayed_work_sync(&lio->link_status_wq.wk.work);
		destroy_workqueue(lio->link_status_wq.wq);
	}
}

/**
 * update_link_status - Update link status
 * @netdev: network device
 * @ls: link status structure
 *
 * Called on receipt of a link status response from the core application to
 * update each interface's link status.
 */
static inline void update_link_status(struct net_device *netdev,
				      union oct_link_status *ls)
{
	struct lio *lio = GET_LIO(netdev);
	int changed = (lio->linfo.link.u64 != ls->u64);
	int current_max_mtu = lio->linfo.link.s.mtu;
	struct octeon_device *oct = lio->oct_dev;

	dev_dbg(&oct->pci_dev->dev, "%s: lio->linfo.link.u64=%llx, ls->u64=%llx\n",
		__func__, lio->linfo.link.u64, ls->u64);
	lio->linfo.link.u64 = ls->u64;

	if ((lio->intf_open) && (changed)) {
		print_link_info(netdev);
		lio->link_changes++;

		if (lio->linfo.link.s.link_up) {
			dev_dbg(&oct->pci_dev->dev, "%s: link_up", __func__);
			netif_carrier_on(netdev);
			wake_txqs(netdev);
		} else {
			dev_dbg(&oct->pci_dev->dev, "%s: link_off", __func__);
			netif_carrier_off(netdev);
			stop_txqs(netdev);
		}
		if (lio->linfo.link.s.mtu != current_max_mtu) {
			netif_info(lio, probe, lio->netdev, "Max MTU changed from %d to %d\n",
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
 * lio_sync_octeon_time - send latest localtime to octeon firmware so that
 * firmware will correct it's time, in case there is a time skew
 *
 * @work: work scheduled to send time update to octeon firmware
 **/
static void lio_sync_octeon_time(struct work_struct *work)
{
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct lio *lio = (struct lio *)wk->ctxptr;
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_soft_command *sc;
	struct timespec64 ts;
	struct lio_time *lt;
	int ret;

	sc = octeon_alloc_soft_command(oct, sizeof(struct lio_time), 16, 0);
	if (!sc) {
		dev_err(&oct->pci_dev->dev,
			"Failed to sync time to octeon: soft command allocation failed\n");
		return;
	}

	lt = (struct lio_time *)sc->virtdptr;

	/* Get time of the day */
	ktime_get_real_ts64(&ts);
	lt->sec = ts.tv_sec;
	lt->nsec = ts.tv_nsec;
	octeon_swap_8B_data((u64 *)lt, (sizeof(struct lio_time)) / 8);

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;
	octeon_prepare_soft_command(oct, sc, OPCODE_NIC,
				    OPCODE_NIC_SYNC_OCTEON_TIME, 0, 0, 0);

	init_completion(&sc->complete);
	sc->sc_status = OCTEON_REQUEST_PENDING;

	ret = octeon_send_soft_command(oct, sc);
	if (ret == IQ_SEND_FAILED) {
		dev_err(&oct->pci_dev->dev,
			"Failed to sync time to octeon: failed to send soft command\n");
		octeon_free_soft_command(oct, sc);
	} else {
		WRITE_ONCE(sc->caller_is_done, true);
	}

	queue_delayed_work(lio->sync_octeon_time_wq.wq,
			   &lio->sync_octeon_time_wq.wk.work,
			   msecs_to_jiffies(LIO_SYNC_OCTEON_TIME_INTERVAL_MS));
}

/**
 * setup_sync_octeon_time_wq - prepare work to periodically update local time to octeon firmware
 *
 * @netdev: network device which should send time update to firmware
 **/
static inline int setup_sync_octeon_time_wq(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	lio->sync_octeon_time_wq.wq =
		alloc_workqueue("update-octeon-time", WQ_MEM_RECLAIM, 0);
	if (!lio->sync_octeon_time_wq.wq) {
		dev_err(&oct->pci_dev->dev, "Unable to create wq to update octeon time\n");
		return -1;
	}
	INIT_DELAYED_WORK(&lio->sync_octeon_time_wq.wk.work,
			  lio_sync_octeon_time);
	lio->sync_octeon_time_wq.wk.ctxptr = lio;
	queue_delayed_work(lio->sync_octeon_time_wq.wq,
			   &lio->sync_octeon_time_wq.wk.work,
			   msecs_to_jiffies(LIO_SYNC_OCTEON_TIME_INTERVAL_MS));

	return 0;
}

/**
 * cleanup_sync_octeon_time_wq - destroy wq
 *
 * @netdev: network device which should send time update to firmware
 *
 * Stop scheduling and destroy the work created to periodically update local
 * time to octeon firmware.
 **/
static inline void cleanup_sync_octeon_time_wq(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct cavium_wq *time_wq = &lio->sync_octeon_time_wq;

	if (time_wq->wq) {
		cancel_delayed_work_sync(&time_wq->wk.work);
		destroy_workqueue(time_wq->wq);
	}
}

static struct octeon_device *get_other_octeon_device(struct octeon_device *oct)
{
	struct octeon_device *other_oct;

	other_oct = lio_get_device(oct->octeon_id + 1);

	if (other_oct && other_oct->pci_dev) {
		int oct_busnum, other_oct_busnum;

		oct_busnum = oct->pci_dev->bus->number;
		other_oct_busnum = other_oct->pci_dev->bus->number;

		if (oct_busnum == other_oct_busnum) {
			int oct_slot, other_oct_slot;

			oct_slot = PCI_SLOT(oct->pci_dev->devfn);
			other_oct_slot = PCI_SLOT(other_oct->pci_dev->devfn);

			if (oct_slot == other_oct_slot)
				return other_oct;
		}
	}

	return NULL;
}

static void disable_all_vf_links(struct octeon_device *oct)
{
	struct net_device *netdev;
	int max_vfs, vf, i;

	if (!oct)
		return;

	max_vfs = oct->sriov_info.max_vfs;

	for (i = 0; i < oct->ifcount; i++) {
		netdev = oct->props[i].netdev;
		if (!netdev)
			continue;

		for (vf = 0; vf < max_vfs; vf++)
			liquidio_set_vf_link_state(netdev, vf,
						   IFLA_VF_LINK_STATE_DISABLE);
	}
}

static int liquidio_watchdog(void *param)
{
	bool err_msg_was_printed[LIO_MAX_CORES];
	u16 mask_of_crashed_or_stuck_cores = 0;
	bool all_vf_links_are_disabled = false;
	struct octeon_device *oct = param;
	struct octeon_device *other_oct;
#ifdef CONFIG_MODULE_UNLOAD
	long refcount, vfs_referencing_pf;
	u64 vfs_mask1, vfs_mask2;
#endif
	int core;

	memset(err_msg_was_printed, 0, sizeof(err_msg_was_printed));

	while (!kthread_should_stop()) {
		/* sleep for a couple of seconds so that we don't hog the CPU */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(2000));

		mask_of_crashed_or_stuck_cores =
		    (u16)octeon_read_csr64(oct, CN23XX_SLI_SCRATCH2);

		if (!mask_of_crashed_or_stuck_cores)
			continue;

		WRITE_ONCE(oct->cores_crashed, true);
		other_oct = get_other_octeon_device(oct);
		if (other_oct)
			WRITE_ONCE(other_oct->cores_crashed, true);

		for (core = 0; core < LIO_MAX_CORES; core++) {
			bool core_crashed_or_got_stuck;

			core_crashed_or_got_stuck =
						(mask_of_crashed_or_stuck_cores
						 >> core) & 1;

			if (core_crashed_or_got_stuck &&
			    !err_msg_was_printed[core]) {
				dev_err(&oct->pci_dev->dev,
					"ERROR: Octeon core %d crashed or got stuck!  See oct-fwdump for details.\n",
					core);
				err_msg_was_printed[core] = true;
			}
		}

		if (all_vf_links_are_disabled)
			continue;

		disable_all_vf_links(oct);
		disable_all_vf_links(other_oct);
		all_vf_links_are_disabled = true;

#ifdef CONFIG_MODULE_UNLOAD
		vfs_mask1 = READ_ONCE(oct->sriov_info.vf_drv_loaded_mask);
		vfs_mask2 = READ_ONCE(other_oct->sriov_info.vf_drv_loaded_mask);

		vfs_referencing_pf  = hweight64(vfs_mask1);
		vfs_referencing_pf += hweight64(vfs_mask2);

		refcount = module_refcount(THIS_MODULE);
		if (refcount >= vfs_referencing_pf) {
			while (vfs_referencing_pf) {
				module_put(THIS_MODULE);
				vfs_referencing_pf--;
			}
		}
#endif
	}

	return 0;
}

/**
 * liquidio_probe - PCI probe handler
 * @pdev: PCI device structure
 * @ent: unused
 */
static int
liquidio_probe(struct pci_dev *pdev, const struct pci_device_id __maybe_unused *ent)
{
	struct octeon_device *oct_dev = NULL;
	struct handshake *hs;

	oct_dev = octeon_allocate_device(pdev->device,
					 sizeof(struct octeon_device_priv));
	if (!oct_dev) {
		dev_err(&pdev->dev, "Unable to allocate device\n");
		return -ENOMEM;
	}

	if (pdev->device == OCTEON_CN23XX_PF_VID)
		oct_dev->msix_on = LIO_FLAG_MSIX_ENABLED;

	/* Enable PTP for 6XXX Device */
	if (((pdev->device == OCTEON_CN66XX) ||
	     (pdev->device == OCTEON_CN68XX)))
		oct_dev->ptp_enable = true;
	else
		oct_dev->ptp_enable = false;

	dev_info(&pdev->dev, "Initializing device %x:%x.\n",
		 (u32)pdev->vendor, (u32)pdev->device);

	/* Assign octeon_device for this device to the private data area. */
	pci_set_drvdata(pdev, oct_dev);

	/* set linux specific device pointer */
	oct_dev->pci_dev = (void *)pdev;

	oct_dev->subsystem_id = pdev->subsystem_vendor |
		(pdev->subsystem_device << 16);

	hs = &handshake[oct_dev->octeon_id];
	init_completion(&hs->init);
	init_completion(&hs->started);
	hs->pci_dev = pdev;

	if (oct_dev->octeon_id == 0)
		/* first LiquidIO NIC is detected */
		complete(&first_stage);

	if (octeon_device_init(oct_dev)) {
		complete(&hs->init);
		liquidio_remove(pdev);
		return -ENOMEM;
	}

	if (OCTEON_CN23XX_PF(oct_dev)) {
		u8 bus, device, function;

		if (atomic_read(oct_dev->adapter_refcount) == 1) {
			/* Each NIC gets one watchdog kernel thread.  The first
			 * PF (of each NIC) that gets pci_driver->probe()'d
			 * creates that thread.
			 */
			bus = pdev->bus->number;
			device = PCI_SLOT(pdev->devfn);
			function = PCI_FUNC(pdev->devfn);
			oct_dev->watchdog_task = kthread_run(liquidio_watchdog,
							     oct_dev,
							     "liowd/%02hhx:%02hhx.%hhx",
							     bus, device, function);
			if (IS_ERR(oct_dev->watchdog_task)) {
				oct_dev->watchdog_task = NULL;
				dev_err(&oct_dev->pci_dev->dev,
					"failed to create kernel_thread\n");
				liquidio_remove(pdev);
				return -1;
			}
		}
	}

	oct_dev->rx_pause = 1;
	oct_dev->tx_pause = 1;

	dev_dbg(&oct_dev->pci_dev->dev, "Device is ready\n");

	return 0;
}

static bool fw_type_is_auto(void)
{
	return strncmp(fw_type, LIO_FW_NAME_TYPE_AUTO,
		       sizeof(LIO_FW_NAME_TYPE_AUTO)) == 0;
}

/**
 * octeon_pci_flr - PCI FLR for each Octeon device.
 * @oct: octeon device
 */
static void octeon_pci_flr(struct octeon_device *oct)
{
	int rc;

	pci_save_state(oct->pci_dev);

	pci_cfg_access_lock(oct->pci_dev);

	/* Quiesce the device completely */
	pci_write_config_word(oct->pci_dev, PCI_COMMAND,
			      PCI_COMMAND_INTX_DISABLE);

	rc = __pci_reset_function_locked(oct->pci_dev);

	if (rc != 0)
		dev_err(&oct->pci_dev->dev, "Error %d resetting PCI function %d\n",
			rc, oct->pf_num);

	pci_cfg_access_unlock(oct->pci_dev);

	pci_restore_state(oct->pci_dev);
}

/**
 * octeon_destroy_resources - Destroy resources associated with octeon device
 * @oct: octeon device
 */
static void octeon_destroy_resources(struct octeon_device *oct)
{
	int i, refcount;
	struct msix_entry *msix_entries;
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;

	struct handshake *hs;

	switch (atomic_read(&oct->status)) {
	case OCT_DEV_RUNNING:
	case OCT_DEV_CORE_OK:

		/* No more instructions will be forwarded. */
		atomic_set(&oct->status, OCT_DEV_IN_RESET);

		oct->app_mode = CVM_DRV_INVALID_APP;
		dev_dbg(&oct->pci_dev->dev, "Device state is now %s\n",
			lio_get_state_string(&oct->status));

		schedule_timeout_uninterruptible(HZ / 10);

		fallthrough;
	case OCT_DEV_HOST_OK:

	case OCT_DEV_CONSOLE_INIT_DONE:
		/* Remove any consoles */
		octeon_remove_consoles(oct);

		fallthrough;
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

		fallthrough;
	case OCT_DEV_INTR_SET_DONE:
		/* Disable interrupts  */
		oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

		if (oct->msix_on) {
			msix_entries = (struct msix_entry *)oct->msix_entries;
			for (i = 0; i < oct->num_msix_irqs - 1; i++) {
				if (oct->ioq_vector[i].vector) {
					/* clear the affinity_cpumask */
					irq_set_affinity_hint(
							msix_entries[i].vector,
							NULL);
					free_irq(msix_entries[i].vector,
						 &oct->ioq_vector[i]);
					oct->ioq_vector[i].vector = 0;
				}
			}
			/* non-iov vector's argument is oct struct */
			free_irq(msix_entries[i].vector, oct);

			pci_disable_msix(oct->pci_dev);
			kfree(oct->msix_entries);
			oct->msix_entries = NULL;
		} else {
			/* Release the interrupt line */
			free_irq(oct->pci_dev->irq, oct);

			if (oct->flags & LIO_FLAG_MSI_ENABLED)
				pci_disable_msi(oct->pci_dev);
		}

		kfree(oct->irq_name_storage);
		oct->irq_name_storage = NULL;

		fallthrough;
	case OCT_DEV_MSIX_ALLOC_VECTOR_DONE:
		if (OCTEON_CN23XX_PF(oct))
			octeon_free_ioq_vector(oct);

		fallthrough;
	case OCT_DEV_MBOX_SETUP_DONE:
		if (OCTEON_CN23XX_PF(oct))
			oct->fn_list.free_mbox(oct);

		fallthrough;
	case OCT_DEV_IN_RESET:
	case OCT_DEV_DROQ_INIT_DONE:
		/* Wait for any pending operations */
		mdelay(100);
		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct); i++) {
			if (!(oct->io_qmask.oq & BIT_ULL(i)))
				continue;
			octeon_delete_droq(oct, i);
		}

		/* Force any pending handshakes to complete */
		for (i = 0; i < MAX_OCTEON_DEVICES; i++) {
			hs = &handshake[i];

			if (hs->pci_dev) {
				handshake[oct->octeon_id].init_ok = 0;
				complete(&handshake[oct->octeon_id].init);
				handshake[oct->octeon_id].started_ok = 0;
				complete(&handshake[oct->octeon_id].started);
			}
		}

		fallthrough;
	case OCT_DEV_RESP_LIST_INIT_DONE:
		octeon_delete_response_list(oct);

		fallthrough;
	case OCT_DEV_INSTR_QUEUE_INIT_DONE:
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
			if (!(oct->io_qmask.iq & BIT_ULL(i)))
				continue;
			octeon_delete_instr_queue(oct, i);
		}
#ifdef CONFIG_PCI_IOV
		if (oct->sriov_info.sriov_enabled)
			pci_disable_sriov(oct->pci_dev);
#endif
		fallthrough;
	case OCT_DEV_SC_BUFF_POOL_INIT_DONE:
		octeon_free_sc_buffer_pool(oct);

		fallthrough;
	case OCT_DEV_DISPATCH_INIT_DONE:
		octeon_delete_dispatch_list(oct);
		cancel_delayed_work_sync(&oct->nic_poll_work.work);

		fallthrough;
	case OCT_DEV_PCI_MAP_DONE:
		refcount = octeon_deregister_device(oct);

		/* Soft reset the octeon device before exiting.
		 * However, if fw was loaded from card (i.e. autoboot),
		 * perform an FLR instead.
		 * Implementation note: only soft-reset the device
		 * if it is a CN6XXX OR the LAST CN23XX device.
		 */
		if (atomic_read(oct->adapter_fw_state) == FW_IS_PRELOADED)
			octeon_pci_flr(oct);
		else if (OCTEON_CN6XXX(oct) || !refcount)
			oct->fn_list.soft_reset(oct);

		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);

		fallthrough;
	case OCT_DEV_PCI_ENABLE_DONE:
		/* Disable the device, releasing the PCI INT */
		pci_disable_device(oct->pci_dev);

		fallthrough;
	case OCT_DEV_BEGIN_STATE:
		/* Nothing to be done here either */
		break;
	}                       /* end switch (oct->status) */

	tasklet_kill(&oct_priv->droq_tasklet);
}

/**
 * send_rx_ctrl_cmd - Send Rx control command
 * @lio: per-network private data
 * @start_stop: whether to start or stop
 */
static int send_rx_ctrl_cmd(struct lio *lio, int start_stop)
{
	struct octeon_soft_command *sc;
	union octnet_cmd *ncmd;
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;
	int retval;

	if (oct->props[lio->ifidx].rx_on == start_stop)
		return 0;

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct, OCTNET_CMD_SIZE,
					  16, 0);
	if (!sc) {
		netif_info(lio, rx_err, lio->netdev,
			   "Failed to allocate octeon_soft_command struct\n");
		return -ENOMEM;
	}

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
			return retval;

		oct->props[lio->ifidx].rx_on = start_stop;
		WRITE_ONCE(sc->caller_is_done, true);
	}

	return retval;
}

/**
 * liquidio_destroy_nic_device - Destroy NIC device interface
 * @oct: octeon device
 * @ifidx: which interface to destroy
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

		if (OCTEON_CN23XX_PF(oct))
			oct->droq[0]->ops.poll_mode = 0;
	}

	/* Delete NAPI */
	list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
		netif_napi_del(napi);

	tasklet_enable(&oct_priv->droq_tasklet);

	if (atomic_read(&lio->ifstate) & LIO_IFSTATE_REGISTERED)
		unregister_netdev(netdev);

	cleanup_sync_octeon_time_wq(netdev);
	cleanup_link_status_change_wq(netdev);

	cleanup_rx_oom_poll_fn(netdev);

	lio_delete_glists(lio);

	free_netdev(netdev);

	oct->props[ifidx].gmxport = -1;

	oct->props[ifidx].netdev = NULL;
}

/**
 * liquidio_stop_nic_module - Stop complete NIC functionality
 * @oct: octeon device
 */
static int liquidio_stop_nic_module(struct octeon_device *oct)
{
	int i, j;
	struct lio *lio;

	dev_dbg(&oct->pci_dev->dev, "Stopping network interfaces\n");
	device_lock(&oct->pci_dev->dev);
	if (oct->devlink) {
		devlink_unregister(oct->devlink);
		devlink_free(oct->devlink);
		oct->devlink = NULL;
	}
	device_unlock(&oct->pci_dev->dev);

	if (!oct->ifcount) {
		dev_err(&oct->pci_dev->dev, "Init for Octeon was not completed\n");
		return 1;
	}

	spin_lock_bh(&oct->cmd_resp_wqlock);
	oct->cmd_resp_state = OCT_DRV_OFFLINE;
	spin_unlock_bh(&oct->cmd_resp_wqlock);

	lio_vf_rep_destroy(oct);

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
 * liquidio_remove - Cleans up resources at unload time
 * @pdev: PCI device structure
 */
static void liquidio_remove(struct pci_dev *pdev)
{
	struct octeon_device *oct_dev = pci_get_drvdata(pdev);

	dev_dbg(&oct_dev->pci_dev->dev, "Stopping device\n");

	if (oct_dev->watchdog_task)
		kthread_stop(oct_dev->watchdog_task);

	if (!oct_dev->octeon_id &&
	    oct_dev->fw_info.app_cap_flags & LIQUIDIO_SWITCHDEV_CAP)
		lio_vf_rep_modexit();

	if (oct_dev->app_mode && (oct_dev->app_mode == CVM_DRV_NIC_APP))
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
 * octeon_chip_specific_setup - Identify the Octeon device and to map the BAR address space
 * @oct: octeon device
 */
static int octeon_chip_specific_setup(struct octeon_device *oct)
{
	u32 dev_id, rev_id;
	int ret = 1;

	pci_read_config_dword(oct->pci_dev, 0, &dev_id);
	pci_read_config_dword(oct->pci_dev, 8, &rev_id);
	oct->rev_id = rev_id & 0xff;

	switch (dev_id) {
	case OCTEON_CN68XX_PCIID:
		oct->chip_id = OCTEON_CN68XX;
		ret = lio_setup_cn68xx_octeon_device(oct);
		break;

	case OCTEON_CN66XX_PCIID:
		oct->chip_id = OCTEON_CN66XX;
		ret = lio_setup_cn66xx_octeon_device(oct);
		break;

	case OCTEON_CN23XX_PCIID_PF:
		oct->chip_id = OCTEON_CN23XX_PF_VID;
		ret = setup_cn23xx_octeon_pf_device(oct);
		if (ret)
			break;
#ifdef CONFIG_PCI_IOV
		if (!ret)
			pci_sriov_set_totalvfs(oct->pci_dev,
					       oct->sriov_info.max_vfs);
#endif
		break;

	default:
		dev_err(&oct->pci_dev->dev, "Unknown device found (dev_id: %x)\n",
			dev_id);
	}

	return ret;
}

/**
 * octeon_pci_os_setup - PCI initialization for each Octeon device.
 * @oct: octeon device
 */
static int octeon_pci_os_setup(struct octeon_device *oct)
{
	/* setup PCI stuff first */
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
 * free_netbuf - Unmap and free network buffer
 * @buf: buffer
 */
static void free_netbuf(void *buf)
{
	struct sk_buff *skb;
	struct octnet_buf_free_info *finfo;
	struct lio *lio;

	finfo = (struct octnet_buf_free_info *)buf;
	skb = finfo->skb;
	lio = finfo->lio;

	dma_unmap_single(&lio->oct_dev->pci_dev->dev, finfo->dptr, skb->len,
			 DMA_TO_DEVICE);

	tx_buffer_free(skb);
}

/**
 * free_netsgbuf - Unmap and free gather buffer
 * @buf: buffer
 */
static void free_netsgbuf(void *buf)
{
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb;
	struct lio *lio;
	struct octnic_gather *g;
	int i, frags, iq;

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
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i - 1];

		dma_unmap_page(&lio->oct_dev->pci_dev->dev,
			       g->sg[(i >> 2)].ptr[(i & 3)],
			       skb_frag_size(frag), DMA_TO_DEVICE);
		i++;
	}

	iq = skb_iq(lio->oct_dev, skb);
	spin_lock(&lio->glist_lock[iq]);
	list_add_tail(&g->list, &lio->glist[iq]);
	spin_unlock(&lio->glist_lock[iq]);

	tx_buffer_free(skb);
}

/**
 * free_netsgbuf_with_resp - Unmap and free gather buffer with response
 * @buf: buffer
 */
static void free_netsgbuf_with_resp(void *buf)
{
	struct octeon_soft_command *sc;
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb;
	struct lio *lio;
	struct octnic_gather *g;
	int i, frags, iq;

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
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i - 1];

		dma_unmap_page(&lio->oct_dev->pci_dev->dev,
			       g->sg[(i >> 2)].ptr[(i & 3)],
			       skb_frag_size(frag), DMA_TO_DEVICE);
		i++;
	}

	iq = skb_iq(lio->oct_dev, skb);

	spin_lock(&lio->glist_lock[iq]);
	list_add_tail(&g->list, &lio->glist[iq]);
	spin_unlock(&lio->glist_lock[iq]);

	/* Don't free the skb yet */
}

/**
 * liquidio_ptp_adjfine - Adjust ptp frequency
 * @ptp: PTP clock info
 * @scaled_ppm: how much to adjust by, in scaled parts-per-million
 *
 * Scaled parts per million is ppm with a 16-bit binary fractional field.
 */
static int liquidio_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct lio *lio = container_of(ptp, struct lio, ptp_info);
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;
	s32 ppb = scaled_ppm_to_ppb(scaled_ppm);
	u64 comp, delta;
	unsigned long flags;
	bool neg_adj = false;

	if (ppb < 0) {
		neg_adj = true;
		ppb = -ppb;
	}

	/* The hardware adds the clock compensation value to the
	 * PTP clock on every coprocessor clock cycle, so we
	 * compute the delta in terms of coprocessor clocks.
	 */
	delta = (u64)ppb << 32;
	do_div(delta, oct->coproc_clock_rate);

	spin_lock_irqsave(&lio->ptp_lock, flags);
	comp = lio_pci_readq(oct, CN6XXX_MIO_PTP_CLOCK_COMP);
	if (neg_adj)
		comp -= delta;
	else
		comp += delta;
	lio_pci_writeq(oct, comp, CN6XXX_MIO_PTP_CLOCK_COMP);
	spin_unlock_irqrestore(&lio->ptp_lock, flags);

	return 0;
}

/**
 * liquidio_ptp_adjtime - Adjust ptp time
 * @ptp: PTP clock info
 * @delta: how much to adjust by, in nanosecs
 */
static int liquidio_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	unsigned long flags;
	struct lio *lio = container_of(ptp, struct lio, ptp_info);

	spin_lock_irqsave(&lio->ptp_lock, flags);
	lio->ptp_adjust += delta;
	spin_unlock_irqrestore(&lio->ptp_lock, flags);

	return 0;
}

/**
 * liquidio_ptp_gettime - Get hardware clock time, including any adjustment
 * @ptp: PTP clock info
 * @ts: timespec
 */
static int liquidio_ptp_gettime(struct ptp_clock_info *ptp,
				struct timespec64 *ts)
{
	u64 ns;
	unsigned long flags;
	struct lio *lio = container_of(ptp, struct lio, ptp_info);
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;

	spin_lock_irqsave(&lio->ptp_lock, flags);
	ns = lio_pci_readq(oct, CN6XXX_MIO_PTP_CLOCK_HI);
	ns += lio->ptp_adjust;
	spin_unlock_irqrestore(&lio->ptp_lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

/**
 * liquidio_ptp_settime - Set hardware clock time. Reset adjustment
 * @ptp: PTP clock info
 * @ts: timespec
 */
static int liquidio_ptp_settime(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	u64 ns;
	unsigned long flags;
	struct lio *lio = container_of(ptp, struct lio, ptp_info);
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;

	ns = timespec64_to_ns(ts);

	spin_lock_irqsave(&lio->ptp_lock, flags);
	lio_pci_writeq(oct, ns, CN6XXX_MIO_PTP_CLOCK_HI);
	lio->ptp_adjust = 0;
	spin_unlock_irqrestore(&lio->ptp_lock, flags);

	return 0;
}

/**
 * liquidio_ptp_enable - Check if PTP is enabled
 * @ptp: PTP clock info
 * @rq: request
 * @on: is it on
 */
static int
liquidio_ptp_enable(struct ptp_clock_info __maybe_unused *ptp,
		    struct ptp_clock_request __maybe_unused *rq,
		    int __maybe_unused on)
{
	return -EOPNOTSUPP;
}

/**
 * oct_ptp_open - Open PTP clock source
 * @netdev: network device
 */
static void oct_ptp_open(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;

	spin_lock_init(&lio->ptp_lock);

	snprintf(lio->ptp_info.name, 16, "%s", netdev->name);
	lio->ptp_info.owner = THIS_MODULE;
	lio->ptp_info.max_adj = 250000000;
	lio->ptp_info.n_alarm = 0;
	lio->ptp_info.n_ext_ts = 0;
	lio->ptp_info.n_per_out = 0;
	lio->ptp_info.pps = 0;
	lio->ptp_info.adjfine = liquidio_ptp_adjfine;
	lio->ptp_info.adjtime = liquidio_ptp_adjtime;
	lio->ptp_info.gettime64 = liquidio_ptp_gettime;
	lio->ptp_info.settime64 = liquidio_ptp_settime;
	lio->ptp_info.enable = liquidio_ptp_enable;

	lio->ptp_adjust = 0;

	lio->ptp_clock = ptp_clock_register(&lio->ptp_info,
					     &oct->pci_dev->dev);

	if (IS_ERR(lio->ptp_clock))
		lio->ptp_clock = NULL;
}

/**
 * liquidio_ptp_init - Init PTP clock
 * @oct: octeon device
 */
static void liquidio_ptp_init(struct octeon_device *oct)
{
	u64 clock_comp, cfg;

	clock_comp = (u64)NSEC_PER_SEC << 32;
	do_div(clock_comp, oct->coproc_clock_rate);
	lio_pci_writeq(oct, clock_comp, CN6XXX_MIO_PTP_CLOCK_COMP);

	/* Enable */
	cfg = lio_pci_readq(oct, CN6XXX_MIO_PTP_CLOCK_CFG);
	lio_pci_writeq(oct, cfg | 0x01, CN6XXX_MIO_PTP_CLOCK_CFG);
}

/**
 * load_firmware - Load firmware to device
 * @oct: octeon device
 *
 * Maps device to firmware filename, requests firmware, and downloads it
 */
static int load_firmware(struct octeon_device *oct)
{
	int ret = 0;
	const struct firmware *fw;
	char fw_name[LIO_MAX_FW_FILENAME_LEN];
	char *tmp_fw_type;

	if (fw_type_is_auto()) {
		tmp_fw_type = LIO_FW_NAME_TYPE_NIC;
		strncpy(fw_type, tmp_fw_type, sizeof(fw_type));
	} else {
		tmp_fw_type = fw_type;
	}

	sprintf(fw_name, "%s%s%s_%s%s", LIO_FW_DIR, LIO_FW_BASE_NAME,
		octeon_get_conf(oct)->card_name, tmp_fw_type,
		LIO_FW_NAME_SUFFIX);

	ret = request_firmware(&fw, fw_name, &oct->pci_dev->dev);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "Request firmware failed. Could not find file %s.\n",
			fw_name);
		release_firmware(fw);
		return ret;
	}

	ret = octeon_download_firmware(oct, fw->data, fw->size);

	release_firmware(fw);

	return ret;
}

/**
 * octnet_poll_check_txq_status - Poll routine for checking transmit queue status
 * @work: work_struct data structure
 */
static void octnet_poll_check_txq_status(struct work_struct *work)
{
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct lio *lio = (struct lio *)wk->ctxptr;

	if (!ifstate_check(lio, LIO_IFSTATE_RUNNING))
		return;

	check_txq_status(lio);
	queue_delayed_work(lio->txq_status_wq.wq,
			   &lio->txq_status_wq.wk.work, msecs_to_jiffies(1));
}

/**
 * setup_tx_poll_fn - Sets up the txq poll check
 * @netdev: network device
 */
static inline int setup_tx_poll_fn(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	lio->txq_status_wq.wq = alloc_workqueue("txq-status",
						WQ_MEM_RECLAIM, 0);
	if (!lio->txq_status_wq.wq) {
		dev_err(&oct->pci_dev->dev, "unable to create cavium txq status wq\n");
		return -1;
	}
	INIT_DELAYED_WORK(&lio->txq_status_wq.wk.work,
			  octnet_poll_check_txq_status);
	lio->txq_status_wq.wk.ctxptr = lio;
	queue_delayed_work(lio->txq_status_wq.wq,
			   &lio->txq_status_wq.wk.work, msecs_to_jiffies(1));
	return 0;
}

static inline void cleanup_tx_poll_fn(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	if (lio->txq_status_wq.wq) {
		cancel_delayed_work_sync(&lio->txq_status_wq.wk.work);
		destroy_workqueue(lio->txq_status_wq.wq);
	}
}

/**
 * liquidio_open - Net device open for LiquidIO
 * @netdev: network device
 */
static int liquidio_open(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	struct napi_struct *napi, *n;
	int ret = 0;

	if (oct->props[lio->ifidx].napi_enabled == 0) {
		tasklet_disable(&oct_priv->droq_tasklet);

		list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
			napi_enable(napi);

		oct->props[lio->ifidx].napi_enabled = 1;

		if (OCTEON_CN23XX_PF(oct))
			oct->droq[0]->ops.poll_mode = 1;
	}

	if (oct->ptp_enable)
		oct_ptp_open(netdev);

	ifstate_set(lio, LIO_IFSTATE_RUNNING);

	if (!OCTEON_CN23XX_PF(oct) || !oct->msix_on) {
		ret = setup_tx_poll_fn(netdev);
		if (ret)
			goto err_poll;
	}

	netif_tx_start_all_queues(netdev);

	/* Ready for link status updates */
	lio->intf_open = 1;

	netif_info(lio, ifup, lio->netdev, "Interface Open, ready for traffic\n");

	/* tell Octeon to start forwarding packets to host */
	ret = send_rx_ctrl_cmd(lio, 1);
	if (ret)
		goto err_rx_ctrl;

	/* start periodical statistics fetch */
	INIT_DELAYED_WORK(&lio->stats_wk.work, lio_fetch_stats);
	lio->stats_wk.ctxptr = lio;
	schedule_delayed_work(&lio->stats_wk.work, msecs_to_jiffies
					(LIQUIDIO_NDEV_STATS_POLL_TIME_MS));

	dev_info(&oct->pci_dev->dev, "%s interface is opened\n",
		 netdev->name);

	return 0;

err_rx_ctrl:
	if (!OCTEON_CN23XX_PF(oct) || !oct->msix_on)
		cleanup_tx_poll_fn(netdev);
err_poll:
	if (lio->ptp_clock) {
		ptp_clock_unregister(lio->ptp_clock);
		lio->ptp_clock = NULL;
	}

	if (oct->props[lio->ifidx].napi_enabled == 1) {
		list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
			napi_disable(napi);

		oct->props[lio->ifidx].napi_enabled = 0;

		if (OCTEON_CN23XX_PF(oct))
			oct->droq[0]->ops.poll_mode = 0;
	}

	return ret;
}

/**
 * liquidio_stop - Net device stop for LiquidIO
 * @netdev: network device
 */
static int liquidio_stop(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	struct napi_struct *napi, *n;
	int ret = 0;

	ifstate_reset(lio, LIO_IFSTATE_RUNNING);

	/* Stop any link updates */
	lio->intf_open = 0;

	stop_txqs(netdev);

	/* Inform that netif carrier is down */
	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	lio->linfo.link.s.link_up = 0;
	lio->link_changes++;

	/* Tell Octeon that nic interface is down. */
	ret = send_rx_ctrl_cmd(lio, 0);
	if (ret)
		return ret;

	if (OCTEON_CN23XX_PF(oct)) {
		if (!oct->msix_on)
			cleanup_tx_poll_fn(netdev);
	} else {
		cleanup_tx_poll_fn(netdev);
	}

	cancel_delayed_work_sync(&lio->stats_wk.work);

	if (lio->ptp_clock) {
		ptp_clock_unregister(lio->ptp_clock);
		lio->ptp_clock = NULL;
	}

	/* Wait for any pending Rx descriptors */
	if (lio_wait_for_clean_oq(oct))
		netif_info(lio, rx_err, lio->netdev,
			   "Proceeding with stop interface after partial RX desc processing\n");

	if (oct->props[lio->ifidx].napi_enabled == 1) {
		list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
			napi_disable(napi);

		oct->props[lio->ifidx].napi_enabled = 0;

		if (OCTEON_CN23XX_PF(oct))
			oct->droq[0]->ops.poll_mode = 0;

		tasklet_enable(&oct_priv->droq_tasklet);
	}

	dev_info(&oct->pci_dev->dev, "%s interface is stopped\n", netdev->name);

	return ret;
}

/**
 * get_new_flags - Converts a mask based on net device flags
 * @netdev: network device
 *
 * This routine generates a octnet_ifflags mask from the net device flags
 * received from the OS.
 */
static inline enum octnet_ifflags get_new_flags(struct net_device *netdev)
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

/**
 * liquidio_set_mcast_list - Net device set_multicast_list
 * @netdev: network device
 */
static void liquidio_set_mcast_list(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	struct netdev_hw_addr *ha;
	u64 *mc;
	int ret;
	int mc_count = min(netdev_mc_count(netdev), MAX_OCTEON_MULTICAST_ADDR);

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	/* Create a ctrl pkt command to be sent to core app. */
	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_MULTI_LIST;
	nctrl.ncmd.s.param1 = get_new_flags(netdev);
	nctrl.ncmd.s.param2 = mc_count;
	nctrl.ncmd.s.more = mc_count;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	/* copy all the addresses into the udd */
	mc = &nctrl.udd[0];
	netdev_for_each_mc_addr(ha, netdev) {
		*mc = 0;
		memcpy(((u8 *)mc) + 2, ha->addr, ETH_ALEN);
		/* no need to swap bytes */

		if (++mc > &nctrl.udd[mc_count])
			break;
	}

	/* Apparently, any activity in this call from the kernel has to
	 * be atomic. So we won't wait for response.
	 */

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "DEVFLAGS change failed in core (ret: 0x%x)\n",
			ret);
	}
}

/**
 * liquidio_set_mac - Net device set_mac_address
 * @netdev: network device
 * @p: pointer to sockaddr
 */
static int liquidio_set_mac(struct net_device *netdev, void *p)
{
	int ret = 0;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct sockaddr *addr = (struct sockaddr *)p;
	struct octnic_ctrl_pkt nctrl;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_CHANGE_MACADDR;
	nctrl.ncmd.s.param1 = 0;
	nctrl.ncmd.s.more = 1;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;

	nctrl.udd[0] = 0;
	/* The MAC Address is presented in network byte order. */
	memcpy((u8 *)&nctrl.udd[0] + 2, addr->sa_data, ETH_ALEN);

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "MAC Address change failed\n");
		return -ENOMEM;
	}

	if (nctrl.sc_status) {
		dev_err(&oct->pci_dev->dev,
			"%s: MAC Address change failed. sc return=%x\n",
			 __func__, nctrl.sc_status);
		return -EIO;
	}

	eth_hw_addr_set(netdev, addr->sa_data);
	memcpy(((u8 *)&lio->linfo.hw_addr) + 2, addr->sa_data, ETH_ALEN);

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
	lstats->collisions = oct->link_stats.fromhost.total_collisions;

	/* detailed rx_errors: */
	lstats->rx_length_errors = oct->link_stats.fromwire.l2_err;
	/* recved pkt with crc error    */
	lstats->rx_crc_errors = oct->link_stats.fromwire.fcs_err;
	/* recv'd frame alignment error */
	lstats->rx_frame_errors = oct->link_stats.fromwire.frame_err;
	/* recv'r fifo overrun */
	lstats->rx_fifo_errors = oct->link_stats.fromwire.fifo_err;

	lstats->rx_errors = lstats->rx_length_errors + lstats->rx_crc_errors +
		lstats->rx_frame_errors + lstats->rx_fifo_errors;

	/* detailed tx_errors */
	lstats->tx_aborted_errors = oct->link_stats.fromhost.fw_err_pko;
	lstats->tx_carrier_errors = oct->link_stats.fromhost.fw_err_link;
	lstats->tx_fifo_errors = oct->link_stats.fromhost.fifo_err;

	lstats->tx_errors = lstats->tx_aborted_errors +
		lstats->tx_carrier_errors +
		lstats->tx_fifo_errors;
}

/**
 * hwtstamp_ioctl - Handler for SIOCSHWTSTAMP ioctl
 * @netdev: network device
 * @ifr: interface request
 */
static int hwtstamp_ioctl(struct net_device *netdev, struct ifreq *ifr)
{
	struct hwtstamp_config conf;
	struct lio *lio = GET_LIO(netdev);

	if (copy_from_user(&conf, ifr->ifr_data, sizeof(conf)))
		return -EFAULT;

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
 * liquidio_ioctl - ioctl handler
 * @netdev: network device
 * @ifr: interface request
 * @cmd: command
 */
static int liquidio_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct lio *lio = GET_LIO(netdev);

	switch (cmd) {
	case SIOCSHWTSTAMP:
		if (lio->oct_dev->ptp_enable)
			return hwtstamp_ioctl(netdev, ifr);
		fallthrough;
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * handle_timestamp - handle a Tx timestamp response
 * @oct: octeon device
 * @status: response status
 * @buf: pointer to skb
 */
static void handle_timestamp(struct octeon_device *oct,
			     u32 status,
			     void *buf)
{
	struct octnet_buf_free_info *finfo;
	struct octeon_soft_command *sc;
	struct oct_timestamp_resp *resp;
	struct lio *lio;
	struct sk_buff *skb = (struct sk_buff *)buf;

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

	if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS) != 0)) {
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

/**
 * send_nic_timestamp_pkt - Send a data packet that will be timestamped
 * @oct: octeon device
 * @ndata: pointer to network data
 * @finfo: pointer to private network data
 * @xmit_more: more is coming
 */
static inline int send_nic_timestamp_pkt(struct octeon_device *oct,
					 struct octnic_data_pkt *ndata,
					 struct octnet_buf_free_info *finfo,
					 int xmit_more)
{
	int retval;
	struct octeon_soft_command *sc;
	struct lio *lio;
	int ring_doorbell;
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

	if (OCTEON_CN23XX_PF(oct))
		len = (u32)((struct octeon_instr_ih3 *)
			    (&sc->cmd.cmd3.ih3))->dlengsz;
	else
		len = (u32)((struct octeon_instr_ih2 *)
			    (&sc->cmd.cmd2.ih2))->dlengsz;

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

/**
 * liquidio_xmit - Transmit networks packets to the Octeon interface
 * @skb: skbuff struct to be passed to network layer.
 * @netdev: pointer to network device
 *
 * Return: whether the packet was transmitted to the device okay or not
 *             (NETDEV_TX_OK or NETDEV_TX_BUSY)
 */
static netdev_tx_t liquidio_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct lio *lio;
	struct octnet_buf_free_info *finfo;
	union octnic_cmd_setup cmdsetup;
	struct octnic_data_pkt ndata;
	struct octeon_device *oct;
	struct oct_iq_stats *stats;
	struct octeon_instr_irh *irh;
	union tx_info *tx_info;
	int status = 0;
	int q_idx = 0, iq_no = 0;
	int j, xmit_more = 0;
	u64 dptr = 0;
	u32 tag = 0;

	lio = GET_LIO(netdev);
	oct = lio->oct_dev;

	q_idx = skb_iq(oct, skb);
	tag = q_idx;
	iq_no = lio->linfo.txpciq[q_idx].s.q_no;

	stats = &oct->instr_queue[iq_no]->stats;

	/* Check for all conditions in which the current packet cannot be
	 * transmitted.
	 */
	if (!(atomic_read(&lio->ifstate) & LIO_IFSTATE_RUNNING) ||
	    (!lio->linfo.link.s.link_up) ||
	    (skb->len <= 0)) {
		netif_info(lio, tx_err, lio->netdev,
			   "Transmit failed link_status : %d\n",
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

	ndata.buf = (void *)finfo;

	ndata.q_no = iq_no;

	if (octnet_iq_is_full(oct, ndata.q_no)) {
		/* defer sending if queue is full */
		netif_info(lio, tx_err, lio->netdev, "Transmit failed iq:%d full\n",
			   ndata.q_no);
		stats->tx_iq_busy++;
		return NETDEV_TX_BUSY;
	}

	/* pr_info(" XMIT - valid Qs: %d, 1st Q no: %d, cpu:  %d, q_no:%d\n",
	 *	lio->linfo.num_txpciq, lio->txq, cpu, ndata.q_no);
	 */

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

	if (skb_shinfo(skb)->nr_frags == 0) {
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
			stats->tx_dmamap_fail++;
			return NETDEV_TX_BUSY;
		}

		if (OCTEON_CN23XX_PF(oct))
			ndata.cmd.cmd3.dptr = dptr;
		else
			ndata.cmd.cmd2.dptr = dptr;
		finfo->dptr = dptr;
		ndata.reqtype = REQTYPE_NORESP_NET;

	} else {
		int i, frags;
		skb_frag_t *frag;
		struct octnic_gather *g;

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
			stats->tx_dmamap_fail++;
			return NETDEV_TX_BUSY;
		}
		add_sg_size(&g->sg[0], (skb->len - skb->data_len), 0);

		frags = skb_shinfo(skb)->nr_frags;
		i = 1;
		while (frags--) {
			frag = &skb_shinfo(skb)->frags[i - 1];

			g->sg[(i >> 2)].ptr[(i & 3)] =
				skb_frag_dma_map(&oct->pci_dev->dev,
					         frag, 0, skb_frag_size(frag),
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
						       skb_frag_size(frag),
						       DMA_TO_DEVICE);
				}
				dev_err(&oct->pci_dev->dev, "%s DMA mapping error 3\n",
					__func__);
				return NETDEV_TX_BUSY;
			}

			add_sg_size(&g->sg[(i >> 2)], skb_frag_size(frag),
				    (i & 3));
			i++;
		}

		dptr = g->sg_dma_ptr;

		if (OCTEON_CN23XX_PF(oct))
			ndata.cmd.cmd3.dptr = dptr;
		else
			ndata.cmd.cmd2.dptr = dptr;
		finfo->dptr = dptr;
		finfo->g = g;

		ndata.reqtype = REQTYPE_NORESP_NET_SG;
	}

	if (OCTEON_CN23XX_PF(oct)) {
		irh = (struct octeon_instr_irh *)&ndata.cmd.cmd3.irh;
		tx_info = (union tx_info *)&ndata.cmd.cmd3.ossp[0];
	} else {
		irh = (struct octeon_instr_irh *)&ndata.cmd.cmd2.irh;
		tx_info = (union tx_info *)&ndata.cmd.cmd2.ossp[0];
	}

	if (skb_shinfo(skb)->gso_size) {
		tx_info->s.gso_size = skb_shinfo(skb)->gso_size;
		tx_info->s.gso_segs = skb_shinfo(skb)->gso_segs;
		stats->tx_gso++;
	}

	/* HW insert VLAN tag */
	if (skb_vlan_tag_present(skb)) {
		irh->priority = skb_vlan_tag_get(skb) >> 13;
		irh->vlan = skb_vlan_tag_get(skb) & 0xfff;
	}

	xmit_more = netdev_xmit_more();

	if (unlikely(cmdsetup.s.timestamp))
		status = send_nic_timestamp_pkt(oct, &ndata, finfo, xmit_more);
	else
		status = octnet_send_nic_data_pkt(oct, &ndata, xmit_more);
	if (status == IQ_SEND_FAILED)
		goto lio_xmit_failed;

	netif_info(lio, tx_queued, lio->netdev, "Transmit queued successfully\n");

	if (status == IQ_SEND_STOP)
		netif_stop_subqueue(netdev, q_idx);

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

/**
 * liquidio_tx_timeout - Network device Tx timeout
 * @netdev:    pointer to network device
 * @txqueue: index of the hung transmit queue
 */
static void liquidio_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct lio *lio;

	lio = GET_LIO(netdev);

	netif_info(lio, tx_err, lio->netdev,
		   "Transmit timeout tx_dropped:%ld, waking up queues now!!\n",
		   netdev->stats.tx_dropped);
	netif_trans_update(netdev);
	wake_txqs(netdev);
}

static int liquidio_vlan_rx_add_vid(struct net_device *netdev,
				    __be16 proto __attribute__((unused)),
				    u16 vid)
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
		if (ret > 0)
			ret = -EIO;
	}

	return ret;
}

static int liquidio_vlan_rx_kill_vid(struct net_device *netdev,
				     __be16 proto __attribute__((unused)),
				     u16 vid)
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

/**
 * liquidio_set_rxcsum_command - Sending command to enable/disable RX checksum offload
 * @netdev:                pointer to network device
 * @command:               OCTNET_CMD_TNL_RX_CSUM_CTL
 * @rx_cmd:                OCTNET_CMD_RXCSUM_ENABLE/OCTNET_CMD_RXCSUM_DISABLE
 * Returns:                SUCCESS or FAILURE
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
		dev_err(&oct->pci_dev->dev,
			"DEVFLAGS RXCSUM change failed in core(ret:0x%x)\n",
			ret);
		if (ret > 0)
			ret = -EIO;
	}
	return ret;
}

/**
 * liquidio_vxlan_port_command - Sending command to add/delete VxLAN UDP port to firmware
 * @netdev:                pointer to network device
 * @command:               OCTNET_CMD_VXLAN_PORT_CONFIG
 * @vxlan_port:            VxLAN port to be added or deleted
 * @vxlan_cmd_bit:         OCTNET_CMD_VXLAN_PORT_ADD,
 *                              OCTNET_CMD_VXLAN_PORT_DEL
 * Return:                     SUCCESS or FAILURE
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
			"VxLAN port add/delete failed in core (ret:0x%x)\n",
			ret);
		if (ret > 0)
			ret = -EIO;
	}
	return ret;
}

static int liquidio_udp_tunnel_set_port(struct net_device *netdev,
					unsigned int table, unsigned int entry,
					struct udp_tunnel_info *ti)
{
	return liquidio_vxlan_port_command(netdev,
					   OCTNET_CMD_VXLAN_PORT_CONFIG,
					   htons(ti->port),
					   OCTNET_CMD_VXLAN_PORT_ADD);
}

static int liquidio_udp_tunnel_unset_port(struct net_device *netdev,
					  unsigned int table,
					  unsigned int entry,
					  struct udp_tunnel_info *ti)
{
	return liquidio_vxlan_port_command(netdev,
					   OCTNET_CMD_VXLAN_PORT_CONFIG,
					   htons(ti->port),
					   OCTNET_CMD_VXLAN_PORT_DEL);
}

static const struct udp_tunnel_nic_info liquidio_udp_tunnels = {
	.set_port	= liquidio_udp_tunnel_set_port,
	.unset_port	= liquidio_udp_tunnel_unset_port,
	.tables		= {
		{ .n_entries = 1024, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN, },
	},
};

/**
 * liquidio_fix_features - Net device fix features
 * @netdev:  pointer to network device
 * @request: features requested
 * Return: updated features list
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

	/*Disable LRO if RXCSUM is off */
	if (!(request & NETIF_F_RXCSUM) && (netdev->features & NETIF_F_LRO) &&
	    (lio->dev_capability & NETIF_F_LRO))
		request &= ~NETIF_F_LRO;

	if ((request & NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    !(lio->dev_capability & NETIF_F_HW_VLAN_CTAG_FILTER))
		request &= ~NETIF_F_HW_VLAN_CTAG_FILTER;

	return request;
}

/**
 * liquidio_set_features - Net device set features
 * @netdev:  pointer to network device
 * @features: features to enable/disable
 */
static int liquidio_set_features(struct net_device *netdev,
				 netdev_features_t features)
{
	struct lio *lio = netdev_priv(netdev);

	if ((features & NETIF_F_LRO) &&
	    (lio->dev_capability & NETIF_F_LRO) &&
	    !(netdev->features & NETIF_F_LRO))
		liquidio_set_feature(netdev, OCTNET_CMD_LRO_ENABLE,
				     OCTNIC_LROIPV4 | OCTNIC_LROIPV6);
	else if (!(features & NETIF_F_LRO) &&
		 (lio->dev_capability & NETIF_F_LRO) &&
		 (netdev->features & NETIF_F_LRO))
		liquidio_set_feature(netdev, OCTNET_CMD_LRO_DISABLE,
				     OCTNIC_LROIPV4 | OCTNIC_LROIPV6);

	/* Sending command to firmware to enable/disable RX checksum
	 * offload settings using ethtool
	 */
	if (!(netdev->features & NETIF_F_RXCSUM) &&
	    (lio->enc_dev_capability & NETIF_F_RXCSUM) &&
	    (features & NETIF_F_RXCSUM))
		liquidio_set_rxcsum_command(netdev,
					    OCTNET_CMD_TNL_RX_CSUM_CTL,
					    OCTNET_CMD_RXCSUM_ENABLE);
	else if ((netdev->features & NETIF_F_RXCSUM) &&
		 (lio->enc_dev_capability & NETIF_F_RXCSUM) &&
		 !(features & NETIF_F_RXCSUM))
		liquidio_set_rxcsum_command(netdev, OCTNET_CMD_TNL_RX_CSUM_CTL,
					    OCTNET_CMD_RXCSUM_DISABLE);

	if ((features & NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    (lio->dev_capability & NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    !(netdev->features & NETIF_F_HW_VLAN_CTAG_FILTER))
		liquidio_set_feature(netdev, OCTNET_CMD_VLAN_FILTER_CTL,
				     OCTNET_CMD_VLAN_FILTER_ENABLE);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_FILTER) &&
		 (lio->dev_capability & NETIF_F_HW_VLAN_CTAG_FILTER) &&
		 (netdev->features & NETIF_F_HW_VLAN_CTAG_FILTER))
		liquidio_set_feature(netdev, OCTNET_CMD_VLAN_FILTER_CTL,
				     OCTNET_CMD_VLAN_FILTER_DISABLE);

	return 0;
}

static int __liquidio_set_vf_mac(struct net_device *netdev, int vfidx,
				 u8 *mac, bool is_admin_assigned)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	if (!is_valid_ether_addr(mac))
		return -EINVAL;

	if (vfidx < 0 || vfidx >= oct->sriov_info.max_vfs)
		return -EINVAL;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_CHANGE_MACADDR;
	/* vfidx is 0 based, but vf_num (param1) is 1 based */
	nctrl.ncmd.s.param1 = vfidx + 1;
	nctrl.ncmd.s.more = 1;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	if (is_admin_assigned) {
		nctrl.ncmd.s.param2 = true;
		nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;
	}

	nctrl.udd[0] = 0;
	/* The MAC Address is presented in network byte order. */
	ether_addr_copy((u8 *)&nctrl.udd[0] + 2, mac);

	oct->sriov_info.vf_macaddr[vfidx] = nctrl.udd[0];

	ret = octnet_send_nic_ctrl_pkt(oct, &nctrl);
	if (ret > 0)
		ret = -EIO;

	return ret;
}

static int liquidio_set_vf_mac(struct net_device *netdev, int vfidx, u8 *mac)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	int retval;

	if (vfidx < 0 || vfidx >= oct->sriov_info.num_vfs_alloced)
		return -EINVAL;

	retval = __liquidio_set_vf_mac(netdev, vfidx, mac, true);
	if (!retval)
		cn23xx_tell_vf_its_macaddr_changed(oct, vfidx, mac);

	return retval;
}

static int liquidio_set_vf_spoofchk(struct net_device *netdev, int vfidx,
				    bool enable)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int retval;

	if (!(oct->fw_info.app_cap_flags & LIQUIDIO_SPOOFCHK_CAP)) {
		netif_info(lio, drv, lio->netdev,
			   "firmware does not support spoofchk\n");
		return -EOPNOTSUPP;
	}

	if (vfidx < 0 || vfidx >= oct->sriov_info.num_vfs_alloced) {
		netif_info(lio, drv, lio->netdev, "Invalid vfidx %d\n", vfidx);
		return -EINVAL;
	}

	if (enable) {
		if (oct->sriov_info.vf_spoofchk[vfidx])
			return 0;
	} else {
		/* Clear */
		if (!oct->sriov_info.vf_spoofchk[vfidx])
			return 0;
	}

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));
	nctrl.ncmd.s.cmdgroup = OCTNET_CMD_GROUP1;
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_VF_SPOOFCHK;
	nctrl.ncmd.s.param1 =
		vfidx + 1; /* vfidx is 0 based,
			    * but vf_num (param1) is 1 based
			    */
	nctrl.ncmd.s.param2 = enable;
	nctrl.ncmd.s.more = 0;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.cb_fn = NULL;

	retval = octnet_send_nic_ctrl_pkt(oct, &nctrl);

	if (retval) {
		netif_info(lio, drv, lio->netdev,
			   "Failed to set VF %d spoofchk %s\n", vfidx,
			enable ? "on" : "off");
		return -1;
	}

	oct->sriov_info.vf_spoofchk[vfidx] = enable;
	netif_info(lio, drv, lio->netdev, "VF %u spoofchk is %s\n", vfidx,
		   enable ? "on" : "off");

	return 0;
}

static int liquidio_set_vf_vlan(struct net_device *netdev, int vfidx,
				u16 vlan, u8 qos, __be16 vlan_proto)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	u16 vlantci;
	int ret = 0;

	if (vfidx < 0 || vfidx >= oct->sriov_info.num_vfs_alloced)
		return -EINVAL;

	if (vlan_proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	if (vlan >= VLAN_N_VID || qos > 7)
		return -EINVAL;

	if (vlan)
		vlantci = vlan | (u16)qos << VLAN_PRIO_SHIFT;
	else
		vlantci = 0;

	if (oct->sriov_info.vf_vlantci[vfidx] == vlantci)
		return 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	if (vlan)
		nctrl.ncmd.s.cmd = OCTNET_CMD_ADD_VLAN_FILTER;
	else
		nctrl.ncmd.s.cmd = OCTNET_CMD_DEL_VLAN_FILTER;

	nctrl.ncmd.s.param1 = vlantci;
	nctrl.ncmd.s.param2 =
	    vfidx + 1; /* vfidx is 0 based, but vf_num (param2) is 1 based */
	nctrl.ncmd.s.more = 0;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.cb_fn = NULL;

	ret = octnet_send_nic_ctrl_pkt(oct, &nctrl);
	if (ret) {
		if (ret > 0)
			ret = -EIO;
		return ret;
	}

	oct->sriov_info.vf_vlantci[vfidx] = vlantci;

	return ret;
}

static int liquidio_get_vf_config(struct net_device *netdev, int vfidx,
				  struct ifla_vf_info *ivi)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	u8 *macaddr;

	if (vfidx < 0 || vfidx >= oct->sriov_info.num_vfs_alloced)
		return -EINVAL;

	memset(ivi, 0, sizeof(struct ifla_vf_info));

	ivi->vf = vfidx;
	macaddr = 2 + (u8 *)&oct->sriov_info.vf_macaddr[vfidx];
	ether_addr_copy(&ivi->mac[0], macaddr);
	ivi->vlan = oct->sriov_info.vf_vlantci[vfidx] & VLAN_VID_MASK;
	ivi->qos = oct->sriov_info.vf_vlantci[vfidx] >> VLAN_PRIO_SHIFT;
	if (oct->sriov_info.trusted_vf.active &&
	    oct->sriov_info.trusted_vf.id == vfidx)
		ivi->trusted = true;
	else
		ivi->trusted = false;
	ivi->linkstate = oct->sriov_info.vf_linkstate[vfidx];
	ivi->spoofchk = oct->sriov_info.vf_spoofchk[vfidx];
	ivi->max_tx_rate = lio->linfo.link.s.speed;
	ivi->min_tx_rate = 0;

	return 0;
}

static int liquidio_send_vf_trust_cmd(struct lio *lio, int vfidx, bool trusted)
{
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_soft_command *sc;
	int retval;

	sc = octeon_alloc_soft_command(oct, 0, 16, 0);
	if (!sc)
		return -ENOMEM;

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	/* vfidx is 0 based, but vf_num (param1) is 1 based */
	octeon_prepare_soft_command(oct, sc, OPCODE_NIC,
				    OPCODE_NIC_SET_TRUSTED_VF, 0, vfidx + 1,
				    trusted);

	init_completion(&sc->complete);
	sc->sc_status = OCTEON_REQUEST_PENDING;

	retval = octeon_send_soft_command(oct, sc);
	if (retval == IQ_SEND_FAILED) {
		octeon_free_soft_command(oct, sc);
		retval = -1;
	} else {
		/* Wait for response or timeout */
		retval = wait_for_sc_completion_timeout(oct, sc, 0);
		if (retval)
			return (retval);

		WRITE_ONCE(sc->caller_is_done, true);
	}

	return retval;
}

static int liquidio_set_vf_trust(struct net_device *netdev, int vfidx,
				 bool setting)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	if (strcmp(oct->fw_info.liquidio_firmware_version, "1.7.1") < 0) {
		/* trusted vf is not supported by firmware older than 1.7.1 */
		return -EOPNOTSUPP;
	}

	if (vfidx < 0 || vfidx >= oct->sriov_info.num_vfs_alloced) {
		netif_info(lio, drv, lio->netdev, "Invalid vfidx %d\n", vfidx);
		return -EINVAL;
	}

	if (setting) {
		/* Set */

		if (oct->sriov_info.trusted_vf.active &&
		    oct->sriov_info.trusted_vf.id == vfidx)
			return 0;

		if (oct->sriov_info.trusted_vf.active) {
			netif_info(lio, drv, lio->netdev, "More than one trusted VF is not allowed\n");
			return -EPERM;
		}
	} else {
		/* Clear */

		if (!oct->sriov_info.trusted_vf.active)
			return 0;
	}

	if (!liquidio_send_vf_trust_cmd(lio, vfidx, setting)) {
		if (setting) {
			oct->sriov_info.trusted_vf.id = vfidx;
			oct->sriov_info.trusted_vf.active = true;
		} else {
			oct->sriov_info.trusted_vf.active = false;
		}

		netif_info(lio, drv, lio->netdev, "VF %u is %strusted\n", vfidx,
			   setting ? "" : "not ");
	} else {
		netif_info(lio, drv, lio->netdev, "Failed to set VF trusted\n");
		return -1;
	}

	return 0;
}

static int liquidio_set_vf_link_state(struct net_device *netdev, int vfidx,
				      int linkstate)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	if (vfidx < 0 || vfidx >= oct->sriov_info.num_vfs_alloced)
		return -EINVAL;

	if (oct->sriov_info.vf_linkstate[vfidx] == linkstate)
		return 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_VF_LINKSTATE;
	nctrl.ncmd.s.param1 =
	    vfidx + 1; /* vfidx is 0 based, but vf_num (param1) is 1 based */
	nctrl.ncmd.s.param2 = linkstate;
	nctrl.ncmd.s.more = 0;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.cb_fn = NULL;

	ret = octnet_send_nic_ctrl_pkt(oct, &nctrl);

	if (!ret)
		oct->sriov_info.vf_linkstate[vfidx] = linkstate;
	else if (ret > 0)
		ret = -EIO;

	return ret;
}

static int
liquidio_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct lio_devlink_priv *priv;
	struct octeon_device *oct;

	priv = devlink_priv(devlink);
	oct = priv->oct;

	*mode = oct->eswitch_mode;

	return 0;
}

static int
liquidio_eswitch_mode_set(struct devlink *devlink, u16 mode,
			  struct netlink_ext_ack *extack)
{
	struct lio_devlink_priv *priv;
	struct octeon_device *oct;
	int ret = 0;

	priv = devlink_priv(devlink);
	oct = priv->oct;

	if (!(oct->fw_info.app_cap_flags & LIQUIDIO_SWITCHDEV_CAP))
		return -EINVAL;

	if (oct->eswitch_mode == mode)
		return 0;

	switch (mode) {
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		oct->eswitch_mode = mode;
		ret = lio_vf_rep_create(oct);
		break;

	case DEVLINK_ESWITCH_MODE_LEGACY:
		lio_vf_rep_destroy(oct);
		oct->eswitch_mode = mode;
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct devlink_ops liquidio_devlink_ops = {
	.eswitch_mode_get = liquidio_eswitch_mode_get,
	.eswitch_mode_set = liquidio_eswitch_mode_set,
};

static int
liquidio_get_port_parent_id(struct net_device *dev,
			    struct netdev_phys_item_id *ppid)
{
	struct lio *lio = GET_LIO(dev);
	struct octeon_device *oct = lio->oct_dev;

	if (oct->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return -EOPNOTSUPP;

	ppid->id_len = ETH_ALEN;
	ether_addr_copy(ppid->id, (void *)&lio->linfo.hw_addr + 2);

	return 0;
}

static int liquidio_get_vf_stats(struct net_device *netdev, int vfidx,
				 struct ifla_vf_stats *vf_stats)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct oct_vf_stats stats;
	int ret;

	if (vfidx < 0 || vfidx >= oct->sriov_info.num_vfs_alloced)
		return -EINVAL;

	memset(&stats, 0, sizeof(struct oct_vf_stats));
	ret = cn23xx_get_vf_stats(oct, vfidx, &stats);
	if (!ret) {
		vf_stats->rx_packets = stats.rx_packets;
		vf_stats->tx_packets = stats.tx_packets;
		vf_stats->rx_bytes = stats.rx_bytes;
		vf_stats->tx_bytes = stats.tx_bytes;
		vf_stats->broadcast = stats.broadcast;
		vf_stats->multicast = stats.multicast;
	}

	return ret;
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
	.ndo_eth_ioctl		= liquidio_ioctl,
	.ndo_fix_features	= liquidio_fix_features,
	.ndo_set_features	= liquidio_set_features,
	.ndo_set_vf_mac		= liquidio_set_vf_mac,
	.ndo_set_vf_vlan	= liquidio_set_vf_vlan,
	.ndo_get_vf_config	= liquidio_get_vf_config,
	.ndo_set_vf_spoofchk	= liquidio_set_vf_spoofchk,
	.ndo_set_vf_trust	= liquidio_set_vf_trust,
	.ndo_set_vf_link_state  = liquidio_set_vf_link_state,
	.ndo_get_vf_stats	= liquidio_get_vf_stats,
	.ndo_get_port_parent_id	= liquidio_get_port_parent_id,
};

/**
 * liquidio_init - Entry point for the liquidio module
 */
static int __init liquidio_init(void)
{
	int i;
	struct handshake *hs;

	init_completion(&first_stage);

	octeon_init_device_list(OCTEON_CONFIG_TYPE_DEFAULT);

	if (liquidio_init_pci())
		return -EINVAL;

	wait_for_completion_timeout(&first_stage, msecs_to_jiffies(1000));

	for (i = 0; i < MAX_OCTEON_DEVICES; i++) {
		hs = &handshake[i];
		if (hs->pci_dev) {
			wait_for_completion(&hs->init);
			if (!hs->init_ok) {
				/* init handshake failed */
				dev_err(&hs->pci_dev->dev,
					"Failed to init device\n");
				liquidio_deinit_pci();
				return -EIO;
			}
		}
	}

	for (i = 0; i < MAX_OCTEON_DEVICES; i++) {
		hs = &handshake[i];
		if (hs->pci_dev) {
			wait_for_completion_timeout(&hs->started,
						    msecs_to_jiffies(30000));
			if (!hs->started_ok) {
				/* starter handshake failed */
				dev_err(&hs->pci_dev->dev,
					"Firmware failed to start\n");
				liquidio_deinit_pci();
				return -EIO;
			}
		}
	}

	return 0;
}

static int lio_nic_info(struct octeon_recv_info *recv_info, void *buf)
{
	struct octeon_device *oct = (struct octeon_device *)buf;
	struct octeon_recv_pkt *recv_pkt = recv_info->recv_pkt;
	int gmxport = 0;
	union oct_link_status *ls;
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
 * setup_nic_devices - Setup network interfaces
 * @octeon_dev:  octeon device
 *
 * Called during init time for each device. It assumes the NIC
 * is already up and running.  The link information for each
 * interface is passed in link_info.
 */
static int setup_nic_devices(struct octeon_device *octeon_dev)
{
	struct lio *lio = NULL;
	struct net_device *netdev;
	u8 mac[6], i, j, *fw_ver, *micro_ver;
	unsigned long micro;
	u32 cur_ver;
	struct octeon_soft_command *sc;
	struct liquidio_if_cfg_resp *resp;
	struct octdev_props *props;
	int retval, num_iqueues, num_oqueues;
	int max_num_queues = 0;
	union oct_nic_if_cfg if_cfg;
	unsigned int base_queue;
	unsigned int gmx_port_id;
	u32 resp_size, data_size;
	u32 ifidx_or_pfnum;
	struct lio_version *vdata;
	struct devlink *devlink;
	struct lio_devlink_priv *lio_devlink;

	/* This is to handle link status changes */
	octeon_register_dispatch_fn(octeon_dev, OPCODE_NIC,
				    OPCODE_NIC_INFO,
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

		if (OCTEON_CN23XX_PF(octeon_dev)) {
			num_iqueues = octeon_dev->sriov_info.num_pf_rings;
			num_oqueues = octeon_dev->sriov_info.num_pf_rings;
			base_queue = octeon_dev->sriov_info.pf_srn;

			gmx_port_id = octeon_dev->pf_num;
			ifidx_or_pfnum = octeon_dev->pf_num;
		} else {
			num_iqueues = CFG_GET_NUM_TXQS_NIC_IF(
						octeon_get_conf(octeon_dev), i);
			num_oqueues = CFG_GET_NUM_RXQS_NIC_IF(
						octeon_get_conf(octeon_dev), i);
			base_queue = CFG_GET_BASE_QUE_NIC_IF(
						octeon_get_conf(octeon_dev), i);
			gmx_port_id = CFG_GET_GMXID_NIC_IF(
						octeon_get_conf(octeon_dev), i);
			ifidx_or_pfnum = i;
		}

		dev_dbg(&octeon_dev->pci_dev->dev,
			"requesting config for interface %d, iqs %d, oqs %d\n",
			ifidx_or_pfnum, num_iqueues, num_oqueues);

		if_cfg.u64 = 0;
		if_cfg.s.num_iqueues = num_iqueues;
		if_cfg.s.num_oqueues = num_oqueues;
		if_cfg.s.base_queue = base_queue;
		if_cfg.s.gmx_port_id = gmx_port_id;

		sc->iq_no = 0;

		octeon_prepare_soft_command(octeon_dev, sc, OPCODE_NIC,
					    OPCODE_NIC_IF_CFG, 0,
					    if_cfg.u64, 0);

		init_completion(&sc->complete);
		sc->sc_status = OCTEON_REQUEST_PENDING;

		retval = octeon_send_soft_command(octeon_dev, sc);
		if (retval == IQ_SEND_FAILED) {
			dev_err(&octeon_dev->pci_dev->dev,
				"iq/oq config failed status: %x\n",
				retval);
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
			dev_err(&octeon_dev->pci_dev->dev, "iq/oq config failed\n");
			WRITE_ONCE(sc->caller_is_done, true);
			goto setup_nic_dev_done;
		}
		snprintf(octeon_dev->fw_info.liquidio_firmware_version,
			 32, "%s",
			 resp->cfg_info.liquidio_firmware_version);

		/* Verify f/w version (in case of 'auto' loading from flash) */
		fw_ver = octeon_dev->fw_info.liquidio_firmware_version;
		if (memcmp(LIQUIDIO_BASE_VERSION,
			   fw_ver,
			   strlen(LIQUIDIO_BASE_VERSION))) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Unmatched firmware version. Expected %s.x, got %s.\n",
				LIQUIDIO_BASE_VERSION, fw_ver);
			WRITE_ONCE(sc->caller_is_done, true);
			goto setup_nic_dev_done;
		} else if (atomic_read(octeon_dev->adapter_fw_state) ==
			   FW_IS_PRELOADED) {
			dev_info(&octeon_dev->pci_dev->dev,
				 "Using auto-loaded firmware version %s.\n",
				 fw_ver);
		}

		/* extract micro version field; point past '<maj>.<min>.' */
		micro_ver = fw_ver + strlen(LIQUIDIO_BASE_VERSION) + 1;
		if (kstrtoul(micro_ver, 10, &micro) != 0)
			micro = 0;
		octeon_dev->fw_info.ver.maj = LIQUIDIO_BASE_MAJOR_VERSION;
		octeon_dev->fw_info.ver.min = LIQUIDIO_BASE_MINOR_VERSION;
		octeon_dev->fw_info.ver.rev = micro;

		octeon_swap_8B_data((u64 *)(&resp->cfg_info),
				    (sizeof(struct liquidio_if_cfg_info)) >> 3);

		num_iqueues = hweight64(resp->cfg_info.iqmask);
		num_oqueues = hweight64(resp->cfg_info.oqmask);

		if (!(num_iqueues) || !(num_oqueues)) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Got bad iqueues (%016llx) or oqueues (%016llx) from firmware.\n",
				resp->cfg_info.iqmask,
				resp->cfg_info.oqmask);
			WRITE_ONCE(sc->caller_is_done, true);
			goto setup_nic_dev_done;
		}

		if (OCTEON_CN6XXX(octeon_dev)) {
			max_num_queues = CFG_GET_IQ_MAX_Q(CHIP_CONF(octeon_dev,
								    cn6xxx));
		} else if (OCTEON_CN23XX_PF(octeon_dev)) {
			max_num_queues = CFG_GET_IQ_MAX_Q(CHIP_CONF(octeon_dev,
								    cn23xx_pf));
		}

		dev_dbg(&octeon_dev->pci_dev->dev,
			"interface %d, iqmask %016llx, oqmask %016llx, numiqueues %d, numoqueues %d max_num_queues: %d\n",
			i, resp->cfg_info.iqmask, resp->cfg_info.oqmask,
			num_iqueues, num_oqueues, max_num_queues);
		netdev = alloc_etherdev_mq(LIO_SIZE, max_num_queues);

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

		retval = netif_set_real_num_rx_queues(netdev, num_oqueues);
		if (retval) {
			dev_err(&octeon_dev->pci_dev->dev,
				"setting real number rx failed\n");
			WRITE_ONCE(sc->caller_is_done, true);
			goto setup_nic_dev_free;
		}

		retval = netif_set_real_num_tx_queues(netdev, num_iqueues);
		if (retval) {
			dev_err(&octeon_dev->pci_dev->dev,
				"setting real number tx failed\n");
			WRITE_ONCE(sc->caller_is_done, true);
			goto setup_nic_dev_free;
		}

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

		WRITE_ONCE(sc->caller_is_done, true);

		lio->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

		if (OCTEON_CN23XX_PF(octeon_dev) ||
		    OCTEON_CN6XXX(octeon_dev)) {
			lio->dev_capability = NETIF_F_HIGHDMA
					      | NETIF_F_IP_CSUM
					      | NETIF_F_IPV6_CSUM
					      | NETIF_F_SG | NETIF_F_RXCSUM
					      | NETIF_F_GRO
					      | NETIF_F_TSO | NETIF_F_TSO6
					      | NETIF_F_LRO;
		}
		netif_set_tso_max_size(netdev, OCTNIC_GSO_MAX_SIZE);

		/*  Copy of transmit encapsulation capabilities:
		 *  TSO, TSO6, Checksums for this device
		 */
		lio->enc_dev_capability = NETIF_F_IP_CSUM
					  | NETIF_F_IPV6_CSUM
					  | NETIF_F_GSO_UDP_TUNNEL
					  | NETIF_F_HW_CSUM | NETIF_F_SG
					  | NETIF_F_RXCSUM
					  | NETIF_F_TSO | NETIF_F_TSO6
					  | NETIF_F_LRO;

		netdev->hw_enc_features = (lio->enc_dev_capability &
					   ~NETIF_F_LRO);

		netdev->udp_tunnel_nic_info = &liquidio_udp_tunnels;

		lio->dev_capability |= NETIF_F_GSO_UDP_TUNNEL;

		netdev->vlan_features = lio->dev_capability;
		/* Add any unchangeable hw features */
		lio->dev_capability |=  NETIF_F_HW_VLAN_CTAG_FILTER |
					NETIF_F_HW_VLAN_CTAG_RX |
					NETIF_F_HW_VLAN_CTAG_TX;

		netdev->features = (lio->dev_capability & ~NETIF_F_LRO);

		netdev->hw_features = lio->dev_capability;
		/*HW_VLAN_RX and HW_VLAN_FILTER is always on*/
		netdev->hw_features = netdev->hw_features &
			~NETIF_F_HW_VLAN_CTAG_RX;

		/* MTU range: 68 - 16000 */
		netdev->min_mtu = LIO_MIN_MTU_SIZE;
		netdev->max_mtu = LIO_MAX_MTU_SIZE;

		/* Point to the  properties for octeon device to which this
		 * interface belongs.
		 */
		lio->oct_dev = octeon_dev;
		lio->octprops = props;
		lio->netdev = netdev;

		dev_dbg(&octeon_dev->pci_dev->dev,
			"if%d gmx: %d hw_addr: 0x%llx\n", i,
			lio->linfo.gmxport, CVM_CAST64(lio->linfo.hw_addr));

		for (j = 0; j < octeon_dev->sriov_info.max_vfs; j++) {
			u8 vfmac[ETH_ALEN];

			eth_random_addr(vfmac);
			if (__liquidio_set_vf_mac(netdev, j, vfmac, false)) {
				dev_err(&octeon_dev->pci_dev->dev,
					"Error setting VF%d MAC address\n",
					j);
				goto setup_nic_dev_free;
			}
		}

		/* 64-bit swap required on LE machines */
		octeon_swap_8B_data(&lio->linfo.hw_addr, 1);
		for (j = 0; j < 6; j++)
			mac[j] = *((u8 *)(((u8 *)&lio->linfo.hw_addr) + 2 + j));

		/* Copy MAC Address to OS network device structure */

		eth_hw_addr_set(netdev, mac);

		/* By default all interfaces on a single Octeon uses the same
		 * tx and rx queues
		 */
		lio->txq = lio->linfo.txpciq[0].s.q_no;
		lio->rxq = lio->linfo.rxpciq[0].s.q_no;
		if (liquidio_setup_io_queues(octeon_dev, i,
					     lio->linfo.num_txpciq,
					     lio->linfo.num_rxpciq)) {
			dev_err(&octeon_dev->pci_dev->dev, "I/O queues creation failed\n");
			goto setup_nic_dev_free;
		}

		ifstate_set(lio, LIO_IFSTATE_DROQ_OPS);

		lio->tx_qsize = octeon_get_tx_qsize(octeon_dev, lio->txq);
		lio->rx_qsize = octeon_get_rx_qsize(octeon_dev, lio->rxq);

		if (lio_setup_glists(octeon_dev, lio, num_iqueues)) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Gather list allocation failed\n");
			goto setup_nic_dev_free;
		}

		/* Register ethtool support */
		liquidio_set_ethtool_ops(netdev);
		if (lio->oct_dev->chip_id == OCTEON_CN23XX_PF_VID)
			octeon_dev->priv_flags = OCT_PRIV_FLAG_DEFAULT;
		else
			octeon_dev->priv_flags = 0x0;

		if (netdev->features & NETIF_F_LRO)
			liquidio_set_feature(netdev, OCTNET_CMD_LRO_ENABLE,
					     OCTNIC_LROIPV4 | OCTNIC_LROIPV6);

		liquidio_set_feature(netdev, OCTNET_CMD_VLAN_FILTER_CTL,
				     OCTNET_CMD_VLAN_FILTER_ENABLE);

		if ((debug != -1) && (debug & NETIF_MSG_HW))
			liquidio_set_feature(netdev,
					     OCTNET_CMD_VERBOSE_ENABLE, 0);

		if (setup_link_status_change_wq(netdev))
			goto setup_nic_dev_free;

		if ((octeon_dev->fw_info.app_cap_flags &
		     LIQUIDIO_TIME_SYNC_CAP) &&
		    setup_sync_octeon_time_wq(netdev))
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

		if (octeon_dev->subsystem_id ==
			OCTEON_CN2350_25GB_SUBSYS_ID ||
		    octeon_dev->subsystem_id ==
			OCTEON_CN2360_25GB_SUBSYS_ID) {
			cur_ver = OCT_FW_VER(octeon_dev->fw_info.ver.maj,
					     octeon_dev->fw_info.ver.min,
					     octeon_dev->fw_info.ver.rev);

			/* speed control unsupported in f/w older than 1.7.2 */
			if (cur_ver < OCT_FW_VER(1, 7, 2)) {
				dev_info(&octeon_dev->pci_dev->dev,
					 "speed setting not supported by f/w.");
				octeon_dev->speed_setting = 25;
				octeon_dev->no_speed_setting = 1;
			} else {
				liquidio_get_speed(lio);
			}

			if (octeon_dev->speed_setting == 0) {
				octeon_dev->speed_setting = 25;
				octeon_dev->no_speed_setting = 1;
			}
		} else {
			octeon_dev->no_speed_setting = 1;
			octeon_dev->speed_setting = 10;
		}
		octeon_dev->speed_boot = octeon_dev->speed_setting;

		/* don't read FEC setting if unsupported by f/w (see above) */
		if (octeon_dev->speed_boot == 25 &&
		    !octeon_dev->no_speed_setting) {
			liquidio_get_fec(lio);
			octeon_dev->props[lio->ifidx].fec_boot =
				octeon_dev->props[lio->ifidx].fec;
		}
	}

	device_lock(&octeon_dev->pci_dev->dev);
	devlink = devlink_alloc(&liquidio_devlink_ops,
				sizeof(struct lio_devlink_priv),
				&octeon_dev->pci_dev->dev);
	if (!devlink) {
		device_unlock(&octeon_dev->pci_dev->dev);
		dev_err(&octeon_dev->pci_dev->dev, "devlink alloc failed\n");
		goto setup_nic_dev_free;
	}

	lio_devlink = devlink_priv(devlink);
	lio_devlink->oct = octeon_dev;

	octeon_dev->devlink = devlink;
	octeon_dev->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;
	devlink_register(devlink);
	device_unlock(&octeon_dev->pci_dev->dev);

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

#ifdef CONFIG_PCI_IOV
static int octeon_enable_sriov(struct octeon_device *oct)
{
	unsigned int num_vfs_alloced = oct->sriov_info.num_vfs_alloced;
	struct pci_dev *vfdev;
	int err;
	u32 u;

	if (OCTEON_CN23XX_PF(oct) && num_vfs_alloced) {
		err = pci_enable_sriov(oct->pci_dev,
				       oct->sriov_info.num_vfs_alloced);
		if (err) {
			dev_err(&oct->pci_dev->dev,
				"OCTEON: Failed to enable PCI sriov: %d\n",
				err);
			oct->sriov_info.num_vfs_alloced = 0;
			return err;
		}
		oct->sriov_info.sriov_enabled = 1;

		/* init lookup table that maps DPI ring number to VF pci_dev
		 * struct pointer
		 */
		u = 0;
		vfdev = pci_get_device(PCI_VENDOR_ID_CAVIUM,
				       OCTEON_CN23XX_VF_VID, NULL);
		while (vfdev) {
			if (vfdev->is_virtfn &&
			    (vfdev->physfn == oct->pci_dev)) {
				oct->sriov_info.dpiring_to_vfpcidev_lut[u] =
					vfdev;
				u += oct->sriov_info.rings_per_vf;
			}
			vfdev = pci_get_device(PCI_VENDOR_ID_CAVIUM,
					       OCTEON_CN23XX_VF_VID, vfdev);
		}
	}

	return num_vfs_alloced;
}

static int lio_pci_sriov_disable(struct octeon_device *oct)
{
	int u;

	if (pci_vfs_assigned(oct->pci_dev)) {
		dev_err(&oct->pci_dev->dev, "VFs are still assigned to VMs.\n");
		return -EPERM;
	}

	pci_disable_sriov(oct->pci_dev);

	u = 0;
	while (u < MAX_POSSIBLE_VFS) {
		oct->sriov_info.dpiring_to_vfpcidev_lut[u] = NULL;
		u += oct->sriov_info.rings_per_vf;
	}

	oct->sriov_info.num_vfs_alloced = 0;
	dev_info(&oct->pci_dev->dev, "oct->pf_num:%d disabled VFs\n",
		 oct->pf_num);

	return 0;
}

static int liquidio_enable_sriov(struct pci_dev *dev, int num_vfs)
{
	struct octeon_device *oct = pci_get_drvdata(dev);
	int ret = 0;

	if ((num_vfs == oct->sriov_info.num_vfs_alloced) &&
	    (oct->sriov_info.sriov_enabled)) {
		dev_info(&oct->pci_dev->dev, "oct->pf_num:%d already enabled num_vfs:%d\n",
			 oct->pf_num, num_vfs);
		return 0;
	}

	if (!num_vfs) {
		lio_vf_rep_destroy(oct);
		ret = lio_pci_sriov_disable(oct);
	} else if (num_vfs > oct->sriov_info.max_vfs) {
		dev_err(&oct->pci_dev->dev,
			"OCTEON: Max allowed VFs:%d user requested:%d",
			oct->sriov_info.max_vfs, num_vfs);
		ret = -EPERM;
	} else {
		oct->sriov_info.num_vfs_alloced = num_vfs;
		ret = octeon_enable_sriov(oct);
		dev_info(&oct->pci_dev->dev, "oct->pf_num:%d num_vfs:%d\n",
			 oct->pf_num, num_vfs);
		ret = lio_vf_rep_create(oct);
		if (ret)
			dev_info(&oct->pci_dev->dev,
				 "vf representor create failed");
	}

	return ret;
}
#endif

/**
 * liquidio_init_nic_module - initialize the NIC
 * @oct: octeon device
 *
 * This initialization routine is called once the Octeon device application is
 * up and running
 */
static int liquidio_init_nic_module(struct octeon_device *oct)
{
	int i, retval = 0;
	int num_nic_ports = CFG_GET_NUM_NIC_PORTS(octeon_get_conf(oct));

	dev_dbg(&oct->pci_dev->dev, "Initializing network interfaces\n");

	/* only default iq and oq were initialized
	 * initialize the rest as well
	 */
	/* run port_config command for each port */
	oct->ifcount = num_nic_ports;

	memset(oct->props, 0, sizeof(struct octdev_props) * num_nic_ports);

	for (i = 0; i < MAX_OCTEON_LINKS; i++)
		oct->props[i].gmxport = -1;

	retval = setup_nic_devices(oct);
	if (retval) {
		dev_err(&oct->pci_dev->dev, "Setup NIC devices failed\n");
		goto octnet_init_failure;
	}

	/* Call vf_rep_modinit if the firmware is switchdev capable
	 * and do it from the first liquidio function probed.
	 */
	if (!oct->octeon_id &&
	    oct->fw_info.app_cap_flags & LIQUIDIO_SWITCHDEV_CAP) {
		retval = lio_vf_rep_modinit();
		if (retval) {
			liquidio_stop_nic_module(oct);
			goto octnet_init_failure;
		}
	}

	liquidio_ptp_init(oct);

	dev_dbg(&oct->pci_dev->dev, "Network interfaces ready\n");

	return retval;

octnet_init_failure:

	oct->ifcount = 0;

	return retval;
}

/**
 * nic_starter - finish init
 * @work:  work struct work_struct
 *
 * starter callback that invokes the remaining initialization work after the NIC is up and running.
 */
static void nic_starter(struct work_struct *work)
{
	struct octeon_device *oct;
	struct cavium_wk *wk = (struct cavium_wk *)work;

	oct = (struct octeon_device *)wk->ctxptr;

	if (atomic_read(&oct->status) == OCT_DEV_RUNNING)
		return;

	/* If the status of the device is CORE_OK, the core
	 * application has reported its application type. Call
	 * any registered handlers now and move to the RUNNING
	 * state.
	 */
	if (atomic_read(&oct->status) != OCT_DEV_CORE_OK) {
		schedule_delayed_work(&oct->nic_poll_work.work,
				      LIQUIDIO_STARTER_POLL_INTERVAL_MS);
		return;
	}

	atomic_set(&oct->status, OCT_DEV_RUNNING);

	if (oct->app_mode && oct->app_mode == CVM_DRV_NIC_APP) {
		dev_dbg(&oct->pci_dev->dev, "Starting NIC module\n");

		if (liquidio_init_nic_module(oct))
			dev_err(&oct->pci_dev->dev, "NIC initialization failed\n");
		else
			handshake[oct->octeon_id].started_ok = 1;
	} else {
		dev_err(&oct->pci_dev->dev,
			"Unexpected application running on NIC (%d). Check firmware.\n",
			oct->app_mode);
	}

	complete(&handshake[oct->octeon_id].started);
}

static int
octeon_recv_vf_drv_notice(struct octeon_recv_info *recv_info, void *buf)
{
	struct octeon_device *oct = (struct octeon_device *)buf;
	struct octeon_recv_pkt *recv_pkt = recv_info->recv_pkt;
	int i, notice, vf_idx;
	bool cores_crashed;
	u64 *data, vf_num;

	notice = recv_pkt->rh.r.ossp;
	data = (u64 *)(get_rbd(recv_pkt->buffer_ptr[0]) + OCT_DROQ_INFO_SIZE);

	/* the first 64-bit word of data is the vf_num */
	vf_num = data[0];
	octeon_swap_8B_data(&vf_num, 1);
	vf_idx = (int)vf_num - 1;

	cores_crashed = READ_ONCE(oct->cores_crashed);

	if (notice == VF_DRV_LOADED) {
		if (!(oct->sriov_info.vf_drv_loaded_mask & BIT_ULL(vf_idx))) {
			oct->sriov_info.vf_drv_loaded_mask |= BIT_ULL(vf_idx);
			dev_info(&oct->pci_dev->dev,
				 "driver for VF%d was loaded\n", vf_idx);
			if (!cores_crashed)
				try_module_get(THIS_MODULE);
		}
	} else if (notice == VF_DRV_REMOVED) {
		if (oct->sriov_info.vf_drv_loaded_mask & BIT_ULL(vf_idx)) {
			oct->sriov_info.vf_drv_loaded_mask &= ~BIT_ULL(vf_idx);
			dev_info(&oct->pci_dev->dev,
				 "driver for VF%d was removed\n", vf_idx);
			if (!cores_crashed)
				module_put(THIS_MODULE);
		}
	} else if (notice == VF_DRV_MACADDR_CHANGED) {
		u8 *b = (u8 *)&data[1];

		oct->sriov_info.vf_macaddr[vf_idx] = data[1];
		dev_info(&oct->pci_dev->dev,
			 "VF driver changed VF%d's MAC address to %pM\n",
			 vf_idx, b + 2);
	}

	for (i = 0; i < recv_pkt->buffer_count; i++)
		recv_buffer_free(recv_pkt->buffer_ptr[i]);
	octeon_free_recv_info(recv_info);

	return 0;
}

/**
 * octeon_device_init - Device initialization for each Octeon device that is probed
 * @octeon_dev:  octeon device
 */
static int octeon_device_init(struct octeon_device *octeon_dev)
{
	int j, ret;
	char bootcmd[] = "\n";
	char *dbg_enb = NULL;
	enum lio_fw_state fw_state;
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)octeon_dev->priv;
	atomic_set(&octeon_dev->status, OCT_DEV_BEGIN_STATE);

	/* Enable access to the octeon device and make its DMA capability
	 * known to the OS.
	 */
	if (octeon_pci_os_setup(octeon_dev))
		return 1;

	atomic_set(&octeon_dev->status, OCT_DEV_PCI_ENABLE_DONE);

	/* Identify the Octeon type and map the BAR address space. */
	if (octeon_chip_specific_setup(octeon_dev)) {
		dev_err(&octeon_dev->pci_dev->dev, "Chip specific setup failed\n");
		return 1;
	}

	atomic_set(&octeon_dev->status, OCT_DEV_PCI_MAP_DONE);

	/* Only add a reference after setting status 'OCT_DEV_PCI_MAP_DONE',
	 * since that is what is required for the reference to be removed
	 * during de-initialization (see 'octeon_destroy_resources').
	 */
	octeon_register_device(octeon_dev, octeon_dev->pci_dev->bus->number,
			       PCI_SLOT(octeon_dev->pci_dev->devfn),
			       PCI_FUNC(octeon_dev->pci_dev->devfn),
			       true);

	octeon_dev->app_mode = CVM_DRV_INVALID_APP;

	/* CN23XX supports preloaded firmware if the following is true:
	 *
	 * The adapter indicates that firmware is currently running AND
	 * 'fw_type' is 'auto'.
	 *
	 * (default state is NEEDS_TO_BE_LOADED, override it if appropriate).
	 */
	if (OCTEON_CN23XX_PF(octeon_dev) &&
	    cn23xx_fw_loaded(octeon_dev) && fw_type_is_auto()) {
		atomic_cmpxchg(octeon_dev->adapter_fw_state,
			       FW_NEEDS_TO_BE_LOADED, FW_IS_PRELOADED);
	}

	/* If loading firmware, only first device of adapter needs to do so. */
	fw_state = atomic_cmpxchg(octeon_dev->adapter_fw_state,
				  FW_NEEDS_TO_BE_LOADED,
				  FW_IS_BEING_LOADED);

	/* Here, [local variable] 'fw_state' is set to one of:
	 *
	 *   FW_IS_PRELOADED:       No firmware is to be loaded (see above)
	 *   FW_NEEDS_TO_BE_LOADED: The driver's first instance will load
	 *                          firmware to the adapter.
	 *   FW_IS_BEING_LOADED:    The driver's second instance will not load
	 *                          firmware to the adapter.
	 */

	/* Prior to f/w load, perform a soft reset of the Octeon device;
	 * if error resetting, return w/error.
	 */
	if (fw_state == FW_NEEDS_TO_BE_LOADED)
		if (octeon_dev->fn_list.soft_reset(octeon_dev))
			return 1;

	/* Initialize the dispatch mechanism used to push packets arriving on
	 * Octeon Output queues.
	 */
	if (octeon_init_dispatch_list(octeon_dev))
		return 1;

	octeon_register_dispatch_fn(octeon_dev, OPCODE_NIC,
				    OPCODE_NIC_CORE_DRV_ACTIVE,
				    octeon_core_drv_init,
				    octeon_dev);

	octeon_register_dispatch_fn(octeon_dev, OPCODE_NIC,
				    OPCODE_NIC_VF_DRV_NOTICE,
				    octeon_recv_vf_drv_notice, octeon_dev);
	INIT_DELAYED_WORK(&octeon_dev->nic_poll_work.work, nic_starter);
	octeon_dev->nic_poll_work.ctxptr = (void *)octeon_dev;
	schedule_delayed_work(&octeon_dev->nic_poll_work.work,
			      LIQUIDIO_STARTER_POLL_INTERVAL_MS);

	atomic_set(&octeon_dev->status, OCT_DEV_DISPATCH_INIT_DONE);

	if (octeon_set_io_queues_off(octeon_dev)) {
		dev_err(&octeon_dev->pci_dev->dev, "setting io queues off failed\n");
		return 1;
	}

	if (OCTEON_CN23XX_PF(octeon_dev)) {
		ret = octeon_dev->fn_list.setup_device_regs(octeon_dev);
		if (ret) {
			dev_err(&octeon_dev->pci_dev->dev, "OCTEON: Failed to configure device registers\n");
			return ret;
		}
	}

	/* Initialize soft command buffer pool
	 */
	if (octeon_setup_sc_buffer_pool(octeon_dev)) {
		dev_err(&octeon_dev->pci_dev->dev, "sc buffer pool allocation failed\n");
		return 1;
	}
	atomic_set(&octeon_dev->status, OCT_DEV_SC_BUFF_POOL_INIT_DONE);

	/*  Setup the data structures that manage this Octeon's Input queues. */
	if (octeon_setup_instr_queues(octeon_dev)) {
		dev_err(&octeon_dev->pci_dev->dev,
			"instruction queue initialization failed\n");
		return 1;
	}
	atomic_set(&octeon_dev->status, OCT_DEV_INSTR_QUEUE_INIT_DONE);

	/* Initialize lists to manage the requests of different types that
	 * arrive from user & kernel applications for this octeon device.
	 */
	if (octeon_setup_response_list(octeon_dev)) {
		dev_err(&octeon_dev->pci_dev->dev, "Response list allocation failed\n");
		return 1;
	}
	atomic_set(&octeon_dev->status, OCT_DEV_RESP_LIST_INIT_DONE);

	if (octeon_setup_output_queues(octeon_dev)) {
		dev_err(&octeon_dev->pci_dev->dev, "Output queue initialization failed\n");
		return 1;
	}

	atomic_set(&octeon_dev->status, OCT_DEV_DROQ_INIT_DONE);

	if (OCTEON_CN23XX_PF(octeon_dev)) {
		if (octeon_dev->fn_list.setup_mbox(octeon_dev)) {
			dev_err(&octeon_dev->pci_dev->dev, "OCTEON: Mailbox setup failed\n");
			return 1;
		}
		atomic_set(&octeon_dev->status, OCT_DEV_MBOX_SETUP_DONE);

		if (octeon_allocate_ioq_vector
				(octeon_dev,
				 octeon_dev->sriov_info.num_pf_rings)) {
			dev_err(&octeon_dev->pci_dev->dev, "OCTEON: ioq vector allocation failed\n");
			return 1;
		}
		atomic_set(&octeon_dev->status, OCT_DEV_MSIX_ALLOC_VECTOR_DONE);

	} else {
		/* The input and output queue registers were setup earlier (the
		 * queues were not enabled). Any additional registers
		 * that need to be programmed should be done now.
		 */
		ret = octeon_dev->fn_list.setup_device_regs(octeon_dev);
		if (ret) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Failed to configure device registers\n");
			return ret;
		}
	}

	/* Initialize the tasklet that handles output queue packet processing.*/
	dev_dbg(&octeon_dev->pci_dev->dev, "Initializing droq tasklet\n");
	tasklet_setup(&oct_priv->droq_tasklet, octeon_droq_bh);

	/* Setup the interrupt handler and record the INT SUM register address
	 */
	if (octeon_setup_interrupt(octeon_dev,
				   octeon_dev->sriov_info.num_pf_rings))
		return 1;

	/* Enable Octeon device interrupts */
	octeon_dev->fn_list.enable_interrupt(octeon_dev, OCTEON_ALL_INTR);

	atomic_set(&octeon_dev->status, OCT_DEV_INTR_SET_DONE);

	/* Send Credit for Octeon Output queues. Credits are always sent BEFORE
	 * the output queue is enabled.
	 * This ensures that we'll receive the f/w CORE DRV_ACTIVE message in
	 * case we've configured CN23XX_SLI_GBL_CONTROL[NOPTR_D] = 0.
	 * Otherwise, it is possible that the DRV_ACTIVE message will be sent
	 * before any credits have been issued, causing the ring to be reset
	 * (and the f/w appear to never have started).
	 */
	for (j = 0; j < octeon_dev->num_oqs; j++)
		writel(octeon_dev->droq[j]->max_count,
		       octeon_dev->droq[j]->pkts_credit_reg);

	/* Enable the input and output queues for this Octeon device */
	ret = octeon_dev->fn_list.enable_io_queues(octeon_dev);
	if (ret) {
		dev_err(&octeon_dev->pci_dev->dev, "Failed to enable input/output queues");
		return ret;
	}

	atomic_set(&octeon_dev->status, OCT_DEV_IO_QUEUES_DONE);

	if (fw_state == FW_NEEDS_TO_BE_LOADED) {
		dev_dbg(&octeon_dev->pci_dev->dev, "Waiting for DDR initialization...\n");
		if (!ddr_timeout) {
			dev_info(&octeon_dev->pci_dev->dev,
				 "WAITING. Set ddr_timeout to non-zero value to proceed with initialization.\n");
		}

		schedule_timeout_uninterruptible(HZ * LIO_RESET_SECS);

		/* Wait for the octeon to initialize DDR after the soft-reset.*/
		while (!ddr_timeout) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (schedule_timeout(HZ / 10)) {
				/* user probably pressed Control-C */
				return 1;
			}
		}
		ret = octeon_wait_for_ddr_init(octeon_dev, &ddr_timeout);
		if (ret) {
			dev_err(&octeon_dev->pci_dev->dev,
				"DDR not initialized. Please confirm that board is configured to boot from Flash, ret: %d\n",
				ret);
			return 1;
		}

		if (octeon_wait_for_bootloader(octeon_dev, 1000)) {
			dev_err(&octeon_dev->pci_dev->dev, "Board not responding\n");
			return 1;
		}

		/* Divert uboot to take commands from host instead. */
		ret = octeon_console_send_cmd(octeon_dev, bootcmd, 50);

		dev_dbg(&octeon_dev->pci_dev->dev, "Initializing consoles\n");
		ret = octeon_init_consoles(octeon_dev);
		if (ret) {
			dev_err(&octeon_dev->pci_dev->dev, "Could not access board consoles\n");
			return 1;
		}
		/* If console debug enabled, specify empty string to use default
		 * enablement ELSE specify NULL string for 'disabled'.
		 */
		dbg_enb = octeon_console_debug_enabled(0) ? "" : NULL;
		ret = octeon_add_console(octeon_dev, 0, dbg_enb);
		if (ret) {
			dev_err(&octeon_dev->pci_dev->dev, "Could not access board console\n");
			return 1;
		} else if (octeon_console_debug_enabled(0)) {
			/* If console was added AND we're logging console output
			 * then set our console print function.
			 */
			octeon_dev->console[0].print = octeon_dbg_console_print;
		}

		atomic_set(&octeon_dev->status, OCT_DEV_CONSOLE_INIT_DONE);

		dev_dbg(&octeon_dev->pci_dev->dev, "Loading firmware\n");
		ret = load_firmware(octeon_dev);
		if (ret) {
			dev_err(&octeon_dev->pci_dev->dev, "Could not load firmware to board\n");
			return 1;
		}

		atomic_set(octeon_dev->adapter_fw_state, FW_HAS_BEEN_LOADED);
	}

	handshake[octeon_dev->octeon_id].init_ok = 1;
	complete(&handshake[octeon_dev->octeon_id].init);

	atomic_set(&octeon_dev->status, OCT_DEV_HOST_OK);
	oct_priv->dev = octeon_dev;

	return 0;
}

/**
 * octeon_dbg_console_print - Debug console print function
 * @oct:  octeon device
 * @console_num: console number
 * @prefix:      first portion of line to display
 * @suffix:      second portion of line to display
 *
 * The OCTEON debug console outputs entire lines (excluding '\n').
 * Normally, the line will be passed in the 'prefix' parameter.
 * However, due to buffering, it is possible for a line to be split into two
 * parts, in which case they will be passed as the 'prefix' parameter and
 * 'suffix' parameter.
 */
static int octeon_dbg_console_print(struct octeon_device *oct, u32 console_num,
				    char *prefix, char *suffix)
{
	if (prefix && suffix)
		dev_info(&oct->pci_dev->dev, "%u: %s%s\n", console_num, prefix,
			 suffix);
	else if (prefix)
		dev_info(&oct->pci_dev->dev, "%u: %s\n", console_num, prefix);
	else if (suffix)
		dev_info(&oct->pci_dev->dev, "%u: %s\n", console_num, suffix);

	return 0;
}

/**
 * liquidio_exit - Exits the module
 */
static void __exit liquidio_exit(void)
{
	liquidio_deinit_pci();

	pr_info("LiquidIO network module is now unloaded\n");
}

module_init(liquidio_init);
module_exit(liquidio_exit);
