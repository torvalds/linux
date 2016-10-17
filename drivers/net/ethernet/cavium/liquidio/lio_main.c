/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/ptp_clock_kernel.h>
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

MODULE_AUTHOR("Cavium Networks, <support@cavium.com>");
MODULE_DESCRIPTION("Cavium LiquidIO Intelligent Server Adapter Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(LIQUIDIO_VERSION);
MODULE_FIRMWARE(LIO_FW_DIR LIO_FW_BASE_NAME LIO_210SV_NAME LIO_FW_NAME_SUFFIX);
MODULE_FIRMWARE(LIO_FW_DIR LIO_FW_BASE_NAME LIO_210NV_NAME LIO_FW_NAME_SUFFIX);
MODULE_FIRMWARE(LIO_FW_DIR LIO_FW_BASE_NAME LIO_410NV_NAME LIO_FW_NAME_SUFFIX);

static int ddr_timeout = 10000;
module_param(ddr_timeout, int, 0644);
MODULE_PARM_DESC(ddr_timeout,
		 "Number of milliseconds to wait for DDR initialization. 0 waits for ddr_timeout to be set to non-zero value before starting to check");

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

#define INCR_INSTRQUEUE_PKT_COUNT(octeon_dev_ptr, iq_no, field, count)  \
	(octeon_dev_ptr->instr_queue[iq_no]->stats.field += count)

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "NETIF_MSG debug bits");

static char fw_type[LIO_MAX_FW_TYPE_LEN];
module_param_string(fw_type, fw_type, sizeof(fw_type), 0000);
MODULE_PARM_DESC(fw_type, "Type of firmware to be loaded. Default \"nic\"");

static int conf_type;
module_param(conf_type, int, 0);
MODULE_PARM_DESC(conf_type, "select octeon configuration 0 default 1 ovs");

static int ptp_enable = 1;

/* Bit mask values for lio->ifstate */
#define   LIO_IFSTATE_DROQ_OPS             0x01
#define   LIO_IFSTATE_REGISTERED           0x02
#define   LIO_IFSTATE_RUNNING              0x04
#define   LIO_IFSTATE_RX_TIMESTAMP_ENABLED 0x08

/* Polling interval for determining when NIC application is alive */
#define LIQUIDIO_STARTER_POLL_INTERVAL_MS 100

/* runtime link query interval */
#define LIQUIDIO_LINK_QUERY_INTERVAL_MS         1000

struct liquidio_if_cfg_context {
	int octeon_id;

	wait_queue_head_t wc;

	int cond;
};

struct liquidio_if_cfg_resp {
	u64 rh;
	struct liquidio_if_cfg_info cfg_info;
	u64 status;
};

struct liquidio_rx_ctl_context {
	int octeon_id;

	wait_queue_head_t wc;

	int cond;
};

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

/** Octeon device properties to be used by the NIC module.
 * Each octeon device in the system will be represented
 * by this structure in the NIC module.
 */

#define OCTNIC_MAX_SG  (MAX_SKB_FRAGS)

#define OCTNIC_GSO_MAX_HEADER_SIZE 128
#define OCTNIC_GSO_MAX_SIZE                                                    \
	(CN23XX_DEFAULT_INPUT_JABBER - OCTNIC_GSO_MAX_HEADER_SIZE)

/** Structure of a node in list of gather components maintained by
 * NIC driver for each network device.
 */
struct octnic_gather {
	/** List manipulation. Next and prev pointers. */
	struct list_head list;

	/** Size of the gather component at sg in bytes. */
	int sg_size;

	/** Number of bytes that sg was adjusted to make it 8B-aligned. */
	int adjust;

	/** Gather component that can accommodate max sized fragment list
	 *  received from the IP layer.
	 */
	struct octeon_sg_entry *sg;

	u64 sg_dma_ptr;
};

struct handshake {
	struct completion init;
	struct completion started;
	struct pci_dev *pci_dev;
	int init_ok;
	int started_ok;
};

struct octeon_device_priv {
	/** Tasklet structures for this device. */
	struct tasklet_struct droq_tasklet;
	unsigned long napi_mask;
};

static int octeon_device_init(struct octeon_device *);
static int liquidio_stop(struct net_device *netdev);
static void liquidio_remove(struct pci_dev *pdev);
static int liquidio_probe(struct pci_dev *pdev,
			  const struct pci_device_id *ent);

static struct handshake handshake[MAX_OCTEON_DEVICES];
static struct completion first_stage;

static void octeon_droq_bh(unsigned long pdev)
{
	int q_no;
	int reschedule = 0;
	struct octeon_device *oct = (struct octeon_device *)pdev;
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;

	/* for (q_no = 0; q_no < oct->num_oqs; q_no++) { */
	for (q_no = 0; q_no < MAX_OCTEON_OUTPUT_QUEUES(oct); q_no++) {
		if (!(oct->io_qmask.oq & (1ULL << q_no)))
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
			if (!(oct->io_qmask.oq & (1ULL << i)))
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
 * \brief Forces all IO queues off on a given device
 * @param oct Pointer to Octeon device
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
 * \brief wait for all pending requests to complete
 * @param oct Pointer to Octeon device
 *
 * Called during shutdown sequence
 */
static int wait_for_pending_requests(struct octeon_device *oct)
{
	int i, pcount = 0;

	for (i = 0; i < 100; i++) {
		pcount =
			atomic_read(&oct->response_list
				[OCTEON_ORDERED_SC_LIST].pending_req_count);
		if (pcount)
			schedule_timeout_uninterruptible(HZ / 10);
		else
			break;
	}

	if (pcount)
		return 1;

	return 0;
}

/**
 * \brief Cause device to go quiet so it can be safely removed/reset/etc
 * @param oct Pointer to Octeon device
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
	schedule_timeout_uninterruptible(100);

	if (wait_for_pending_requests(oct))
		dev_err(&oct->pci_dev->dev, "There were pending requests\n");

	/* Force all requests waiting to be fetched by OCTEON to complete. */
	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
		struct octeon_instr_queue *iq;

		if (!(oct->io_qmask.iq & (1ULL << i)))
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
 * \brief Stop all PCI IO to a given device
 * @param dev Pointer to Octeon device
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

	/* cn63xx_cleanup_aer_uncorrect_error_status(oct->pci_dev); */
	/* making it a common function for all OCTEON models */
	cleanup_aer_uncorrect_error_status(oct->pci_dev);
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

	/* Always return a DISCONNECT. There is no support for recovery but only
	 * for a clean shutdown.
	 */
	return PCI_ERS_RESULT_DISCONNECT;
}

/**
 * \brief mmio handler
 * @param pdev Pointer to PCI device
 */
static pci_ers_result_t liquidio_pcie_mmio_enabled(
				struct pci_dev *pdev __attribute__((unused)))
{
	/* We should never hit this since we never ask for a reset for a Fatal
	 * Error. We always return DISCONNECT in io_error above.
	 * But play safe and return RECOVERED for now.
	 */
	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * \brief called after the pci bus has been reset.
 * @param pdev Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * resembles the first-half of the octeon_resume routine.
 */
static pci_ers_result_t liquidio_pcie_slot_reset(
				struct pci_dev *pdev __attribute__((unused)))
{
	/* We should never hit this since we never ask for a reset for a Fatal
	 * Error. We always return DISCONNECT in io_error above.
	 * But play safe and return RECOVERED for now.
	 */
	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * \brief called when traffic can start flowing again.
 * @param pdev Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the octeon_resume routine.
 */
static void liquidio_pcie_resume(struct pci_dev *pdev __attribute__((unused)))
{
	/* Nothing to be done here. */
}

#ifdef CONFIG_PM
/**
 * \brief called when suspending
 * @param pdev Pointer to PCI device
 * @param state state to suspend to
 */
static int liquidio_suspend(struct pci_dev *pdev __attribute__((unused)),
			    pm_message_t state __attribute__((unused)))
{
	return 0;
}

/**
 * \brief called when resuming
 * @param pdev Pointer to PCI device
 */
static int liquidio_resume(struct pci_dev *pdev __attribute__((unused)))
{
	return 0;
}
#endif

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

static struct pci_driver liquidio_pci_driver = {
	.name		= "LiquidIO",
	.id_table	= liquidio_pci_tbl,
	.probe		= liquidio_probe,
	.remove		= liquidio_remove,
	.err_handler	= &liquidio_err_handler,    /* For AER */

#ifdef CONFIG_PM
	.suspend	= liquidio_suspend,
	.resume		= liquidio_resume,
#endif
};

/**
 * \brief register PCI driver
 */
static int liquidio_init_pci(void)
{
	return pci_register_driver(&liquidio_pci_driver);
}

/**
 * \brief unregister PCI driver
 */
static void liquidio_deinit_pci(void)
{
	pci_unregister_driver(&liquidio_pci_driver);
}

/**
 * \brief check interface state
 * @param lio per-network private data
 * @param state_flag flag state to check
 */
static inline int ifstate_check(struct lio *lio, int state_flag)
{
	return atomic_read(&lio->ifstate) & state_flag;
}

/**
 * \brief set interface state
 * @param lio per-network private data
 * @param state_flag flag state to set
 */
static inline void ifstate_set(struct lio *lio, int state_flag)
{
	atomic_set(&lio->ifstate, (atomic_read(&lio->ifstate) | state_flag));
}

/**
 * \brief clear interface state
 * @param lio per-network private data
 * @param state_flag flag state to clear
 */
static inline void ifstate_reset(struct lio *lio, int state_flag)
{
	atomic_set(&lio->ifstate, (atomic_read(&lio->ifstate) & ~(state_flag)));
}

/**
 * \brief Stop Tx queues
 * @param netdev network device
 */
static inline void txqs_stop(struct net_device *netdev)
{
	if (netif_is_multiqueue(netdev)) {
		int i;

		for (i = 0; i < netdev->num_tx_queues; i++)
			netif_stop_subqueue(netdev, i);
	} else {
		netif_stop_queue(netdev);
	}
}

/**
 * \brief Start Tx queues
 * @param netdev network device
 */
static inline void txqs_start(struct net_device *netdev)
{
	if (netif_is_multiqueue(netdev)) {
		int i;

		for (i = 0; i < netdev->num_tx_queues; i++)
			netif_start_subqueue(netdev, i);
	} else {
		netif_start_queue(netdev);
	}
}

/**
 * \brief Wake Tx queues
 * @param netdev network device
 */
static inline void txqs_wake(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	if (netif_is_multiqueue(netdev)) {
		int i;

		for (i = 0; i < netdev->num_tx_queues; i++) {
			int qno = lio->linfo.txpciq[i %
				(lio->linfo.num_txpciq)].s.q_no;

			if (__netif_subqueue_stopped(netdev, i)) {
				INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, qno,
							  tx_restart, 1);
				netif_wake_subqueue(netdev, i);
			}
		}
	} else {
		INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, lio->txq,
					  tx_restart, 1);
		netif_wake_queue(netdev);
	}
}

/**
 * \brief Stop Tx queue
 * @param netdev network device
 */
static void stop_txq(struct net_device *netdev)
{
	txqs_stop(netdev);
}

/**
 * \brief Start Tx queue
 * @param netdev network device
 */
static void start_txq(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	if (lio->linfo.link.s.link_up) {
		txqs_start(netdev);
		return;
	}
}

/**
 * \brief Wake a queue
 * @param netdev network device
 * @param q which queue to wake
 */
static inline void wake_q(struct net_device *netdev, int q)
{
	if (netif_is_multiqueue(netdev))
		netif_wake_subqueue(netdev, q);
	else
		netif_wake_queue(netdev);
}

/**
 * \brief Stop a queue
 * @param netdev network device
 * @param q which queue to stop
 */
static inline void stop_q(struct net_device *netdev, int q)
{
	if (netif_is_multiqueue(netdev))
		netif_stop_subqueue(netdev, q);
	else
		netif_stop_queue(netdev);
}

/**
 * \brief Check Tx queue status, and take appropriate action
 * @param lio per-network private data
 * @returns 0 if full, number of queues woken up otherwise
 */
static inline int check_txq_status(struct lio *lio)
{
	int ret_val = 0;

	if (netif_is_multiqueue(lio->netdev)) {
		int numqs = lio->netdev->num_tx_queues;
		int q, iq = 0;

		/* check each sub-queue state */
		for (q = 0; q < numqs; q++) {
			iq = lio->linfo.txpciq[q %
				(lio->linfo.num_txpciq)].s.q_no;
			if (octnet_iq_is_full(lio->oct_dev, iq))
				continue;
			if (__netif_subqueue_stopped(lio->netdev, q)) {
				wake_q(lio->netdev, q);
				INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, iq,
							  tx_restart, 1);
				ret_val++;
			}
		}
	} else {
		if (octnet_iq_is_full(lio->oct_dev, lio->txq))
			return 0;
		wake_q(lio->netdev, lio->txq);
		INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, lio->txq,
					  tx_restart, 1);
		ret_val = 1;
	}
	return ret_val;
}

/**
 * Remove the node at the head of the list. The list would be empty at
 * the end of this call if there are no more nodes in the list.
 */
static inline struct list_head *list_delete_head(struct list_head *root)
{
	struct list_head *node;

	if ((root->prev == root) && (root->next == root))
		node = NULL;
	else
		node = root->next;

	if (node)
		list_del(node);

	return node;
}

/**
 * \brief Delete gather lists
 * @param lio per-network private data
 */
static void delete_glists(struct lio *lio)
{
	struct octnic_gather *g;
	int i;

	if (!lio->glist)
		return;

	for (i = 0; i < lio->linfo.num_txpciq; i++) {
		do {
			g = (struct octnic_gather *)
				list_delete_head(&lio->glist[i]);
			if (g) {
				if (g->sg) {
					dma_unmap_single(&lio->oct_dev->
							 pci_dev->dev,
							 g->sg_dma_ptr,
							 g->sg_size,
							 DMA_TO_DEVICE);
					kfree((void *)((unsigned long)g->sg -
						       g->adjust));
				}
				kfree(g);
			}
		} while (g);
	}

	kfree((void *)lio->glist);
}

/**
 * \brief Setup gather lists
 * @param lio per-network private data
 */
static int setup_glists(struct octeon_device *oct, struct lio *lio, int num_iqs)
{
	int i, j;
	struct octnic_gather *g;

	lio->glist_lock = kcalloc(num_iqs, sizeof(*lio->glist_lock),
				  GFP_KERNEL);
	if (!lio->glist_lock)
		return 1;

	lio->glist = kcalloc(num_iqs, sizeof(*lio->glist),
			     GFP_KERNEL);
	if (!lio->glist) {
		kfree((void *)lio->glist_lock);
		return 1;
	}

	for (i = 0; i < num_iqs; i++) {
		int numa_node = cpu_to_node(i % num_online_cpus());

		spin_lock_init(&lio->glist_lock[i]);

		INIT_LIST_HEAD(&lio->glist[i]);

		for (j = 0; j < lio->tx_qsize; j++) {
			g = kzalloc_node(sizeof(*g), GFP_KERNEL,
					 numa_node);
			if (!g)
				g = kzalloc(sizeof(*g), GFP_KERNEL);
			if (!g)
				break;

			g->sg_size = ((ROUNDUP4(OCTNIC_MAX_SG) >> 2) *
				      OCT_SG_ENTRY_SIZE);

			g->sg = kmalloc_node(g->sg_size + 8,
					     GFP_KERNEL, numa_node);
			if (!g->sg)
				g->sg = kmalloc(g->sg_size + 8, GFP_KERNEL);
			if (!g->sg) {
				kfree(g);
				break;
			}

			/* The gather component should be aligned on 64-bit
			 * boundary
			 */
			if (((unsigned long)g->sg) & 7) {
				g->adjust = 8 - (((unsigned long)g->sg) & 7);
				g->sg = (struct octeon_sg_entry *)
					((unsigned long)g->sg + g->adjust);
			}
			g->sg_dma_ptr = dma_map_single(&oct->pci_dev->dev,
						       g->sg, g->sg_size,
						       DMA_TO_DEVICE);
			if (dma_mapping_error(&oct->pci_dev->dev,
					      g->sg_dma_ptr)) {
				kfree((void *)((unsigned long)g->sg -
					       g->adjust));
				kfree(g);
				break;
			}

			list_add_tail(&g->list, &lio->glist[i]);
		}

		if (j != lio->tx_qsize) {
			delete_glists(lio);
			return 1;
		}
	}

	return 0;
}

/**
 * \brief Print link information
 * @param netdev network device
 */
static void print_link_info(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	if (atomic_read(&lio->ifstate) & LIO_IFSTATE_REGISTERED) {
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

	rtnl_lock();
	call_netdevice_notifiers(NETDEV_CHANGEMTU, lio->netdev);
	rtnl_unlock();
}

/**
 * \brief Sets up the mtu status change work
 * @param netdev network device
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
 * \brief Update link status
 * @param netdev network device
 * @param ls link status structure
 *
 * Called on receipt of a link status response from the core application to
 * update each interface's link status.
 */
static inline void update_link_status(struct net_device *netdev,
				      union oct_link_status *ls)
{
	struct lio *lio = GET_LIO(netdev);
	int changed = (lio->linfo.link.u64 != ls->u64);

	lio->linfo.link.u64 = ls->u64;

	if ((lio->intf_open) && (changed)) {
		print_link_info(netdev);
		lio->link_changes++;

		if (lio->linfo.link.s.link_up) {
			netif_carrier_on(netdev);
			/* start_txq(netdev); */
			txqs_wake(netdev);
		} else {
			netif_carrier_off(netdev);
			stop_txq(netdev);
		}
	}
}

/* Runs in interrupt context. */
static void update_txq_status(struct octeon_device *oct, int iq_num)
{
	struct net_device *netdev;
	struct lio *lio;
	struct octeon_instr_queue *iq = oct->instr_queue[iq_num];

	netdev = oct->props[iq->ifidx].netdev;

	/* This is needed because the first IQ does not have
	 * a netdev associated with it.
	 */
	if (!netdev)
		return;

	lio = GET_LIO(netdev);
	if (netif_is_multiqueue(netdev)) {
		if (__netif_subqueue_stopped(netdev, iq->q_index) &&
		    lio->linfo.link.s.link_up &&
		    (!octnet_iq_is_full(oct, iq_num))) {
			INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, iq_num,
						  tx_restart, 1);
			netif_wake_subqueue(netdev, iq->q_index);
		} else {
			if (!octnet_iq_is_full(oct, lio->txq)) {
				INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev,
							  lio->txq,
							  tx_restart, 1);
				wake_q(netdev, lio->txq);
			}
		}
	}
}

static
int liquidio_schedule_msix_droq_pkt_handler(struct octeon_droq *droq, u64 ret)
{
	struct octeon_device *oct = droq->oct_dev;
	struct octeon_device_priv *oct_priv =
	    (struct octeon_device_priv *)oct->priv;

	if (droq->ops.poll_mode) {
		droq->ops.napi_fn(droq);
	} else {
		if (ret & MSIX_PO_INT) {
			tasklet_schedule(&oct_priv->droq_tasklet);
			return 1;
		}
		/* this will be flushed periodically by check iq db */
		if (ret & MSIX_PI_INT)
			return 0;
	}
	return 0;
}

/**
 * \brief Droq packet processor sceduler
 * @param oct octeon device
 */
static void liquidio_schedule_droq_pkt_handlers(struct octeon_device *oct)
{
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	u64 oq_no;
	struct octeon_droq *droq;

	if (oct->int_status & OCT_DEV_INTR_PKT_DATA) {
		for (oq_no = 0; oq_no < MAX_OCTEON_OUTPUT_QUEUES(oct);
		     oq_no++) {
			if (!(oct->droq_intr & (1ULL << oq_no)))
				continue;

			droq = oct->droq[oq_no];

			if (droq->ops.poll_mode) {
				droq->ops.napi_fn(droq);
				oct_priv->napi_mask |= (1 << oq_no);
			} else {
				tasklet_schedule(&oct_priv->droq_tasklet);
			}
		}
	}
}

static irqreturn_t
liquidio_msix_intr_handler(int irq __attribute__((unused)), void *dev)
{
	u64 ret;
	struct octeon_ioq_vector *ioq_vector = (struct octeon_ioq_vector *)dev;
	struct octeon_device *oct = ioq_vector->oct_dev;
	struct octeon_droq *droq = oct->droq[ioq_vector->droq_index];

	ret = oct->fn_list.msix_interrupt_handler(ioq_vector);

	if ((ret & MSIX_PO_INT) || (ret & MSIX_PI_INT))
		liquidio_schedule_msix_droq_pkt_handler(droq, ret);

	return IRQ_HANDLED;
}

/**
 * \brief Interrupt handler for octeon
 * @param irq unused
 * @param dev octeon device
 */
static
irqreturn_t liquidio_legacy_intr_handler(int irq __attribute__((unused)),
					 void *dev)
{
	struct octeon_device *oct = (struct octeon_device *)dev;
	irqreturn_t ret;

	/* Disable our interrupts for the duration of ISR */
	oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

	ret = oct->fn_list.process_interrupt_regs(oct);

	if (ret == IRQ_HANDLED)
		liquidio_schedule_droq_pkt_handlers(oct);

	/* Re-enable our interrupts  */
	if (!(atomic_read(&oct->status) == OCT_DEV_IN_RESET))
		oct->fn_list.enable_interrupt(oct, OCTEON_ALL_INTR);

	return ret;
}

/**
 * \brief Setup interrupt for octeon device
 * @param oct octeon device
 *
 *  Enable interrupt in Octeon device as given in the PCI interrupt mask.
 */
static int octeon_setup_interrupt(struct octeon_device *oct)
{
	int irqret, err;
	struct msix_entry *msix_entries;
	int i;
	int num_ioq_vectors;
	int num_alloc_ioq_vectors;

	if (OCTEON_CN23XX_PF(oct) && oct->msix_on) {
		oct->num_msix_irqs = oct->sriov_info.num_pf_rings;
		/* one non ioq interrupt for handling sli_mac_pf_int_sum */
		oct->num_msix_irqs += 1;

		oct->msix_entries = kcalloc(
		    oct->num_msix_irqs, sizeof(struct msix_entry), GFP_KERNEL);
		if (!oct->msix_entries)
			return 1;

		msix_entries = (struct msix_entry *)oct->msix_entries;
		/*Assumption is that pf msix vectors start from pf srn to pf to
		 * trs and not from 0. if not change this code
		 */
		for (i = 0; i < oct->num_msix_irqs - 1; i++)
			msix_entries[i].entry = oct->sriov_info.pf_srn + i;
		msix_entries[oct->num_msix_irqs - 1].entry =
		    oct->sriov_info.trs;
		num_alloc_ioq_vectors = pci_enable_msix_range(
						oct->pci_dev, msix_entries,
						oct->num_msix_irqs,
						oct->num_msix_irqs);
		if (num_alloc_ioq_vectors < 0) {
			dev_err(&oct->pci_dev->dev, "unable to Allocate MSI-X interrupts\n");
			kfree(oct->msix_entries);
			oct->msix_entries = NULL;
			return 1;
		}
		dev_dbg(&oct->pci_dev->dev, "OCTEON: Enough MSI-X interrupts are allocated...\n");

		num_ioq_vectors = oct->num_msix_irqs;

		/** For PF, there is one non-ioq interrupt handler */
		num_ioq_vectors -= 1;
		irqret = request_irq(msix_entries[num_ioq_vectors].vector,
				     liquidio_legacy_intr_handler, 0, "octeon",
				     oct);
		if (irqret) {
			dev_err(&oct->pci_dev->dev,
				"OCTEON: Request_irq failed for MSIX interrupt Error: %d\n",
				irqret);
			pci_disable_msix(oct->pci_dev);
			kfree(oct->msix_entries);
			oct->msix_entries = NULL;
			return 1;
		}

		for (i = 0; i < num_ioq_vectors; i++) {
			irqret = request_irq(msix_entries[i].vector,
					     liquidio_msix_intr_handler, 0,
					     "octeon", &oct->ioq_vector[i]);
			if (irqret) {
				dev_err(&oct->pci_dev->dev,
					"OCTEON: Request_irq failed for MSIX interrupt Error: %d\n",
					irqret);
				/** Freeing the non-ioq irq vector here . */
				free_irq(msix_entries[num_ioq_vectors].vector,
					 oct);

				while (i) {
					i--;
					/** clearing affinity mask. */
					irq_set_affinity_hint(
						msix_entries[i].vector, NULL);
					free_irq(msix_entries[i].vector,
						 &oct->ioq_vector[i]);
				}
				pci_disable_msix(oct->pci_dev);
				kfree(oct->msix_entries);
				oct->msix_entries = NULL;
				return 1;
			}
			oct->ioq_vector[i].vector = msix_entries[i].vector;
			/* assign the cpu mask for this msix interrupt vector */
			irq_set_affinity_hint(
					msix_entries[i].vector,
					(&oct->ioq_vector[i].affinity_mask));
		}
		dev_dbg(&oct->pci_dev->dev, "OCTEON[%d]: MSI-X enabled\n",
			oct->octeon_id);
	} else {
		err = pci_enable_msi(oct->pci_dev);
		if (err)
			dev_warn(&oct->pci_dev->dev, "Reverting to legacy interrupts. Error: %d\n",
				 err);
		else
			oct->flags |= LIO_FLAG_MSI_ENABLED;

		irqret = request_irq(oct->pci_dev->irq,
				     liquidio_legacy_intr_handler, IRQF_SHARED,
				     "octeon", oct);
		if (irqret) {
			if (oct->flags & LIO_FLAG_MSI_ENABLED)
				pci_disable_msi(oct->pci_dev);
			dev_err(&oct->pci_dev->dev, "Request IRQ failed with code: %d\n",
				irqret);
			return 1;
		}
	}
	return 0;
}

static int liquidio_watchdog(void *param)
{
	u64 wdog;
	u16 mask_of_stuck_cores = 0;
	u16 mask_of_crashed_cores = 0;
	int core_num;
	u8 core_is_stuck[LIO_MAX_CORES];
	u8 core_crashed[LIO_MAX_CORES];
	struct octeon_device *oct = param;

	memset(core_is_stuck, 0, sizeof(core_is_stuck));
	memset(core_crashed, 0, sizeof(core_crashed));

	while (!kthread_should_stop()) {
		mask_of_crashed_cores =
		    (u16)octeon_read_csr64(oct, CN23XX_SLI_SCRATCH2);

		for (core_num = 0; core_num < LIO_MAX_CORES; core_num++) {
			if (!core_is_stuck[core_num]) {
				wdog = lio_pci_readq(oct, CIU3_WDOG(core_num));

				/* look at watchdog state field */
				wdog &= CIU3_WDOG_MASK;
				if (wdog) {
					/* this watchdog timer has expired */
					core_is_stuck[core_num] =
						LIO_MONITOR_WDOG_EXPIRE;
					mask_of_stuck_cores |= (1 << core_num);
				}
			}

			if (!core_crashed[core_num])
				core_crashed[core_num] =
				    (mask_of_crashed_cores >> core_num) & 1;
		}

		if (mask_of_stuck_cores) {
			for (core_num = 0; core_num < LIO_MAX_CORES;
			     core_num++) {
				if (core_is_stuck[core_num] == 1) {
					dev_err(&oct->pci_dev->dev,
						"ERROR: Octeon core %d is stuck!\n",
						core_num);
					/* 2 means we have printk'd  an error
					 * so no need to repeat the same printk
					 */
					core_is_stuck[core_num] =
						LIO_MONITOR_CORE_STUCK_MSGD;
				}
			}
		}

		if (mask_of_crashed_cores) {
			for (core_num = 0; core_num < LIO_MAX_CORES;
			     core_num++) {
				if (core_crashed[core_num] == 1) {
					dev_err(&oct->pci_dev->dev,
						"ERROR: Octeon core %d crashed!  See oct-fwdump for details.\n",
						core_num);
					/* 2 means we have printk'd  an error
					 * so no need to repeat the same printk
					 */
					core_crashed[core_num] =
						LIO_MONITOR_CORE_STUCK_MSGD;
				}
			}
		}
#ifdef CONFIG_MODULE_UNLOAD
		if (mask_of_stuck_cores || mask_of_crashed_cores) {
			/* make module refcount=0 so that rmmod will work */
			long refcount;

			refcount = module_refcount(THIS_MODULE);

			while (refcount > 0) {
				module_put(THIS_MODULE);
				refcount = module_refcount(THIS_MODULE);
			}

			/* compensate for and withstand an unlikely (but still
			 * possible) race condition
			 */
			while (refcount < 0) {
				try_module_get(THIS_MODULE);
				refcount = module_refcount(THIS_MODULE);
			}
		}
#endif
		/* sleep for two seconds */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2 * HZ);
	}

	return 0;
}

/**
 * \brief PCI probe handler
 * @param pdev PCI device structure
 * @param ent unused
 */
static int
liquidio_probe(struct pci_dev *pdev,
	       const struct pci_device_id *ent __attribute__((unused)))
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

	dev_info(&pdev->dev, "Initializing device %x:%x.\n",
		 (u32)pdev->vendor, (u32)pdev->device);

	/* Assign octeon_device for this device to the private data area. */
	pci_set_drvdata(pdev, oct_dev);

	/* set linux specific device pointer */
	oct_dev->pci_dev = (void *)pdev;

	hs = &handshake[oct_dev->octeon_id];
	init_completion(&hs->init);
	init_completion(&hs->started);
	hs->pci_dev = pdev;

	if (oct_dev->octeon_id == 0)
		/* first LiquidIO NIC is detected */
		complete(&first_stage);

	if (octeon_device_init(oct_dev)) {
		liquidio_remove(pdev);
		return -ENOMEM;
	}

	if (OCTEON_CN23XX_PF(oct_dev)) {
		u64 scratch1;
		u8 bus, device, function;

		scratch1 = octeon_read_csr64(oct_dev, CN23XX_SLI_SCRATCH1);
		if (!(scratch1 & 4ULL)) {
			/* Bit 2 of SLI_SCRATCH_1 is a flag that indicates that
			 * the lio watchdog kernel thread is running for this
			 * NIC.  Each NIC gets one watchdog kernel thread.
			 */
			scratch1 |= 4ULL;
			octeon_write_csr64(oct_dev, CN23XX_SLI_SCRATCH1,
					   scratch1);

			bus = pdev->bus->number;
			device = PCI_SLOT(pdev->devfn);
			function = PCI_FUNC(pdev->devfn);
			oct_dev->watchdog_task = kthread_create(
			    liquidio_watchdog, oct_dev,
			    "liowd/%02hhx:%02hhx.%hhx", bus, device, function);
			wake_up_process(oct_dev->watchdog_task);
		}
	}

	oct_dev->rx_pause = 1;
	oct_dev->tx_pause = 1;

	dev_dbg(&oct_dev->pci_dev->dev, "Device is ready\n");

	return 0;
}

/**
 *\brief Destroy resources associated with octeon device
 * @param pdev PCI device structure
 * @param ent unused
 */
static void octeon_destroy_resources(struct octeon_device *oct)
{
	int i;
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

		/* fallthrough */
	case OCT_DEV_HOST_OK:

		/* fallthrough */
	case OCT_DEV_CONSOLE_INIT_DONE:
		/* Remove any consoles */
		octeon_remove_consoles(oct);

		/* fallthrough */
	case OCT_DEV_IO_QUEUES_DONE:
		if (wait_for_pending_requests(oct))
			dev_err(&oct->pci_dev->dev, "There were pending requests\n");

		if (lio_wait_for_instr_fetch(oct))
			dev_err(&oct->pci_dev->dev, "IQ had pending instructions\n");

		/* Disable the input and output queues now. No more packets will
		 * arrive from Octeon, but we should wait for all packet
		 * processing to finish.
		 */
		oct->fn_list.disable_io_queues(oct);

		if (lio_wait_for_oq_pkts(oct))
			dev_err(&oct->pci_dev->dev, "OQ had pending packets\n");

		/* Disable interrupts  */
		oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

		if (oct->msix_on) {
			msix_entries = (struct msix_entry *)oct->msix_entries;
			for (i = 0; i < oct->num_msix_irqs - 1; i++) {
				/* clear the affinity_cpumask */
				irq_set_affinity_hint(msix_entries[i].vector,
						      NULL);
				free_irq(msix_entries[i].vector,
					 &oct->ioq_vector[i]);
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

		if (OCTEON_CN23XX_PF(oct))
			octeon_free_ioq_vector(oct);
	/* fallthrough */
	case OCT_DEV_IN_RESET:
	case OCT_DEV_DROQ_INIT_DONE:
		/*atomic_set(&oct->status, OCT_DEV_DROQ_INIT_DONE);*/
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
		/* Soft reset the octeon device before exiting */
		if ((!OCTEON_CN23XX_PF(oct)) || !oct->octeon_id)
			oct->fn_list.soft_reset(oct);

		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);

		/* fallthrough */
	case OCT_DEV_BEGIN_STATE:
		/* Disable the device, releasing the PCI INT */
		pci_disable_device(oct->pci_dev);

		/* Nothing to be done here either */
		break;
	}                       /* end switch (oct->status) */

	tasklet_kill(&oct_priv->droq_tasklet);
}

/**
 * \brief Callback for rx ctrl
 * @param status status of request
 * @param buf pointer to resp structure
 */
static void rx_ctl_callback(struct octeon_device *oct,
			    u32 status,
			    void *buf)
{
	struct octeon_soft_command *sc = (struct octeon_soft_command *)buf;
	struct liquidio_rx_ctl_context *ctx;

	ctx  = (struct liquidio_rx_ctl_context *)sc->ctxptr;

	oct = lio_get_device(ctx->octeon_id);
	if (status)
		dev_err(&oct->pci_dev->dev, "rx ctl instruction failed. Status: %llx\n",
			CVM_CAST64(status));
	WRITE_ONCE(ctx->cond, 1);

	/* This barrier is required to be sure that the response has been
	 * written fully before waking up the handler
	 */
	wmb();

	wake_up_interruptible(&ctx->wc);
}

/**
 * \brief Send Rx control command
 * @param lio per-network private data
 * @param start_stop whether to start or stop
 */
static void send_rx_ctrl_cmd(struct lio *lio, int start_stop)
{
	struct octeon_soft_command *sc;
	struct liquidio_rx_ctl_context *ctx;
	union octnet_cmd *ncmd;
	int ctx_size = sizeof(struct liquidio_rx_ctl_context);
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;
	int retval;

	if (oct->props[lio->ifidx].rx_on == start_stop)
		return;

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct, OCTNET_CMD_SIZE,
					  16, ctx_size);

	ncmd = (union octnet_cmd *)sc->virtdptr;
	ctx  = (struct liquidio_rx_ctl_context *)sc->ctxptr;

	WRITE_ONCE(ctx->cond, 0);
	ctx->octeon_id = lio_get_device_id(oct);
	init_waitqueue_head(&ctx->wc);

	ncmd->u64 = 0;
	ncmd->s.cmd = OCTNET_CMD_RX_CTL;
	ncmd->s.param1 = start_stop;

	octeon_swap_8B_data((u64 *)ncmd, (OCTNET_CMD_SIZE >> 3));

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct, sc, OPCODE_NIC,
				    OPCODE_NIC_CMD, 0, 0, 0);

	sc->callback = rx_ctl_callback;
	sc->callback_arg = sc;
	sc->wait_time = 5000;

	retval = octeon_send_soft_command(oct, sc);
	if (retval == IQ_SEND_FAILED) {
		netif_info(lio, rx_err, lio->netdev, "Failed to send RX Control message\n");
	} else {
		/* Sleep on a wait queue till the cond flag indicates that the
		 * response arrived or timed-out.
		 */
		if (sleep_cond(&ctx->wc, &ctx->cond) == -EINTR)
			return;
		oct->props[lio->ifidx].rx_on = start_stop;
	}

	octeon_free_soft_command(oct, sc);
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
	struct lio *lio;
	struct napi_struct *napi, *n;

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

	if (atomic_read(&lio->ifstate) & LIO_IFSTATE_REGISTERED)
		unregister_netdev(netdev);

	cleanup_link_status_change_wq(netdev);

	delete_glists(lio);

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
	int i, j;
	struct lio *lio;

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
		for (j = 0; j < lio->linfo.num_rxpciq; j++)
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
static void liquidio_remove(struct pci_dev *pdev)
{
	struct octeon_device *oct_dev = pci_get_drvdata(pdev);

	dev_dbg(&oct_dev->pci_dev->dev, "Stopping device\n");

	if (oct_dev->watchdog_task)
		kthread_stop(oct_dev->watchdog_task);

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
 * \brief Identify the Octeon device and to map the BAR address space
 * @param oct octeon device
 */
static int octeon_chip_specific_setup(struct octeon_device *oct)
{
	u32 dev_id, rev_id;
	int ret = 1;
	char *s;

	pci_read_config_dword(oct->pci_dev, 0, &dev_id);
	pci_read_config_dword(oct->pci_dev, 8, &rev_id);
	oct->rev_id = rev_id & 0xff;

	switch (dev_id) {
	case OCTEON_CN68XX_PCIID:
		oct->chip_id = OCTEON_CN68XX;
		ret = lio_setup_cn68xx_octeon_device(oct);
		s = "CN68XX";
		break;

	case OCTEON_CN66XX_PCIID:
		oct->chip_id = OCTEON_CN66XX;
		ret = lio_setup_cn66xx_octeon_device(oct);
		s = "CN66XX";
		break;

	case OCTEON_CN23XX_PCIID_PF:
		oct->chip_id = OCTEON_CN23XX_PF_VID;
		ret = setup_cn23xx_octeon_pf_device(oct);
		s = "CN23XX";
		break;

	default:
		s = "?";
		dev_err(&oct->pci_dev->dev, "Unknown device found (dev_id: %x)\n",
			dev_id);
	}

	if (!ret)
		dev_info(&oct->pci_dev->dev, "%s PASS%d.%d %s Version: %s\n", s,
			 OCTEON_MAJOR_REV(oct),
			 OCTEON_MINOR_REV(oct),
			 octeon_get_conf(oct)->card_name,
			 LIQUIDIO_VERSION);

	return ret;
}

/**
 * \brief PCI initialization for each Octeon device.
 * @param oct octeon device
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
		return 1;
	}

	/* Enable PCI DMA Master. */
	pci_set_master(oct->pci_dev);

	return 0;
}

static inline int skb_iq(struct lio *lio, struct sk_buff *skb)
{
	int q = 0;

	if (netif_is_multiqueue(lio->netdev))
		q = skb->queue_mapping % lio->linfo.num_txpciq;

	return q;
}

/**
 * \brief Check Tx queue state for a given network buffer
 * @param lio per-network private data
 * @param skb network buffer
 */
static inline int check_txq_state(struct lio *lio, struct sk_buff *skb)
{
	int q = 0, iq = 0;

	if (netif_is_multiqueue(lio->netdev)) {
		q = skb->queue_mapping;
		iq = lio->linfo.txpciq[(q % (lio->linfo.num_txpciq))].s.q_no;
	} else {
		iq = lio->txq;
		q = iq;
	}

	if (octnet_iq_is_full(lio->oct_dev, iq))
		return 0;

	if (__netif_subqueue_stopped(lio->netdev, q)) {
		INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, iq, tx_restart, 1);
		wake_q(lio->netdev, q);
	}
	return 1;
}

/**
 * \brief Unmap and free network buffer
 * @param buf buffer
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

	check_txq_state(lio, skb);

	tx_buffer_free(skb);
}

/**
 * \brief Unmap and free gather buffer
 * @param buf buffer
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
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i - 1];

		pci_unmap_page((lio->oct_dev)->pci_dev,
			       g->sg[(i >> 2)].ptr[(i & 3)],
			       frag->size, DMA_TO_DEVICE);
		i++;
	}

	dma_sync_single_for_cpu(&lio->oct_dev->pci_dev->dev,
				g->sg_dma_ptr, g->sg_size, DMA_TO_DEVICE);

	iq = skb_iq(lio, skb);
	spin_lock(&lio->glist_lock[iq]);
	list_add_tail(&g->list, &lio->glist[iq]);
	spin_unlock(&lio->glist_lock[iq]);

	check_txq_state(lio, skb);     /* mq support: sub-queue state check */

	tx_buffer_free(skb);
}

/**
 * \brief Unmap and free gather buffer with response
 * @param buf buffer
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
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i - 1];

		pci_unmap_page((lio->oct_dev)->pci_dev,
			       g->sg[(i >> 2)].ptr[(i & 3)],
			       frag->size, DMA_TO_DEVICE);
		i++;
	}

	dma_sync_single_for_cpu(&lio->oct_dev->pci_dev->dev,
				g->sg_dma_ptr, g->sg_size, DMA_TO_DEVICE);

	iq = skb_iq(lio, skb);

	spin_lock(&lio->glist_lock[iq]);
	list_add_tail(&g->list, &lio->glist[iq]);
	spin_unlock(&lio->glist_lock[iq]);

	/* Don't free the skb yet */

	check_txq_state(lio, skb);
}

/**
 * \brief Adjust ptp frequency
 * @param ptp PTP clock info
 * @param ppb how much to adjust by, in parts-per-billion
 */
static int liquidio_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct lio *lio = container_of(ptp, struct lio, ptp_info);
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;
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
 * \brief Adjust ptp time
 * @param ptp PTP clock info
 * @param delta how much to adjust by, in nanosecs
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
 * \brief Get hardware clock time, including any adjustment
 * @param ptp PTP clock info
 * @param ts timespec
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
 * \brief Set hardware clock time. Reset adjustment
 * @param ptp PTP clock info
 * @param ts timespec
 */
static int liquidio_ptp_settime(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	u64 ns;
	unsigned long flags;
	struct lio *lio = container_of(ptp, struct lio, ptp_info);
	struct octeon_device *oct = (struct octeon_device *)lio->oct_dev;

	ns = timespec_to_ns(ts);

	spin_lock_irqsave(&lio->ptp_lock, flags);
	lio_pci_writeq(oct, ns, CN6XXX_MIO_PTP_CLOCK_HI);
	lio->ptp_adjust = 0;
	spin_unlock_irqrestore(&lio->ptp_lock, flags);

	return 0;
}

/**
 * \brief Check if PTP is enabled
 * @param ptp PTP clock info
 * @param rq request
 * @param on is it on
 */
static int
liquidio_ptp_enable(struct ptp_clock_info *ptp __attribute__((unused)),
		    struct ptp_clock_request *rq __attribute__((unused)),
		    int on __attribute__((unused)))
{
	return -EOPNOTSUPP;
}

/**
 * \brief Open PTP clock source
 * @param netdev network device
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
	lio->ptp_info.adjfreq = liquidio_ptp_adjfreq;
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
 * \brief Init PTP clock
 * @param oct octeon device
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
 * \brief Load firmware to device
 * @param oct octeon device
 *
 * Maps device to firmware filename, requests firmware, and downloads it
 */
static int load_firmware(struct octeon_device *oct)
{
	int ret = 0;
	const struct firmware *fw;
	char fw_name[LIO_MAX_FW_FILENAME_LEN];
	char *tmp_fw_type;

	if (strncmp(fw_type, LIO_FW_NAME_TYPE_NONE,
		    sizeof(LIO_FW_NAME_TYPE_NONE)) == 0) {
		dev_info(&oct->pci_dev->dev, "Skipping firmware load\n");
		return ret;
	}

	if (fw_type[0] == '\0')
		tmp_fw_type = LIO_FW_NAME_TYPE_NIC;
	else
		tmp_fw_type = fw_type;

	sprintf(fw_name, "%s%s%s_%s%s", LIO_FW_DIR, LIO_FW_BASE_NAME,
		octeon_get_conf(oct)->card_name, tmp_fw_type,
		LIO_FW_NAME_SUFFIX);

	ret = request_firmware(&fw, fw_name, &oct->pci_dev->dev);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "Request firmware failed. Could not find file %s.\n.",
			fw_name);
		release_firmware(fw);
		return ret;
	}

	ret = octeon_download_firmware(oct, fw->data, fw->size);

	release_firmware(fw);

	return ret;
}

/**
 * \brief Setup output queue
 * @param oct octeon device
 * @param q_no which queue
 * @param num_descs how many descriptors
 * @param desc_size size of each descriptor
 * @param app_ctx application context
 */
static int octeon_setup_droq(struct octeon_device *oct, int q_no, int num_descs,
			     int desc_size, void *app_ctx)
{
	int ret_val = 0;

	dev_dbg(&oct->pci_dev->dev, "Creating Droq: %d\n", q_no);
	/* droq creation and local register settings. */
	ret_val = octeon_create_droq(oct, q_no, num_descs, desc_size, app_ctx);
	if (ret_val < 0)
		return ret_val;

	if (ret_val == 1) {
		dev_dbg(&oct->pci_dev->dev, "Using default droq %d\n", q_no);
		return 0;
	}
	/* tasklet creation for the droq */

	/* Enable the droq queues */
	octeon_set_droq_pkt_op(oct, q_no, 1);

	/* Send Credit for Octeon Output queues. Credits are always
	 * sent after the output queue is enabled.
	 */
	writel(oct->droq[q_no]->max_count,
	       oct->droq[q_no]->pkts_credit_reg);

	return ret_val;
}

/**
 * \brief Callback for getting interface configuration
 * @param status status of request
 * @param buf pointer to resp structure
 */
static void if_cfg_callback(struct octeon_device *oct,
			    u32 status __attribute__((unused)),
			    void *buf)
{
	struct octeon_soft_command *sc = (struct octeon_soft_command *)buf;
	struct liquidio_if_cfg_resp *resp;
	struct liquidio_if_cfg_context *ctx;

	resp = (struct liquidio_if_cfg_resp *)sc->virtrptr;
	ctx = (struct liquidio_if_cfg_context *)sc->ctxptr;

	oct = lio_get_device(ctx->octeon_id);
	if (resp->status)
		dev_err(&oct->pci_dev->dev, "nic if cfg instruction failed. Status: %llx\n",
			CVM_CAST64(resp->status));
	WRITE_ONCE(ctx->cond, 1);

	snprintf(oct->fw_info.liquidio_firmware_version, 32, "%s",
		 resp->cfg_info.liquidio_firmware_version);

	/* This barrier is required to be sure that the response has been
	 * written fully before waking up the handler
	 */
	wmb();

	wake_up_interruptible(&ctx->wc);
}

/**
 * \brief Select queue based on hash
 * @param dev Net device
 * @param skb sk_buff structure
 * @returns selected queue number
 */
static u16 select_q(struct net_device *dev, struct sk_buff *skb,
		    void *accel_priv __attribute__((unused)),
		    select_queue_fallback_t fallback __attribute__((unused)))
{
	u32 qindex = 0;
	struct lio *lio;

	lio = GET_LIO(dev);
	qindex = skb_tx_hash(dev, skb);

	return (u16)(qindex % (lio->linfo.num_txpciq));
}

/** Routine to push packets arriving on Octeon interface upto network layer.
 * @param oct_id   - octeon device id.
 * @param skbuff   - skbuff struct to be passed to network layer.
 * @param len      - size of total data received.
 * @param rh       - Control header associated with the packet
 * @param param    - additional control data with the packet
 * @param arg	   - farg registered in droq_ops
 */
static void
liquidio_push_packet(u32 octeon_id __attribute__((unused)),
		     void *skbuff,
		     u32 len,
		     union octeon_rh *rh,
		     void *param,
		     void *arg)
{
	struct napi_struct *napi = param;
	struct sk_buff *skb = (struct sk_buff *)skbuff;
	struct skb_shared_hwtstamps *shhwtstamps;
	u64 ns;
	u16 vtag = 0;
	struct net_device *netdev = (struct net_device *)arg;
	struct octeon_droq *droq = container_of(param, struct octeon_droq,
						napi);
	if (netdev) {
		int packet_was_received;
		struct lio *lio = GET_LIO(netdev);
		struct octeon_device *oct = lio->oct_dev;

		/* Do not proceed if the interface is not in RUNNING state. */
		if (!ifstate_check(lio, LIO_IFSTATE_RUNNING)) {
			recv_buffer_free(skb);
			droq->stats.rx_dropped++;
			return;
		}

		skb->dev = netdev;

		skb_record_rx_queue(skb, droq->q_no);
		if (likely(len > MIN_SKB_SIZE)) {
			struct octeon_skb_page_info *pg_info;
			unsigned char *va;

			pg_info = ((struct octeon_skb_page_info *)(skb->cb));
			if (pg_info->page) {
				/* For Paged allocation use the frags */
				va = page_address(pg_info->page) +
					pg_info->page_offset;
				memcpy(skb->data, va, MIN_SKB_SIZE);
				skb_put(skb, MIN_SKB_SIZE);
				skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
						pg_info->page,
						pg_info->page_offset +
						MIN_SKB_SIZE,
						len - MIN_SKB_SIZE,
						LIO_RXBUFFER_SZ);
			}
		} else {
			struct octeon_skb_page_info *pg_info =
				((struct octeon_skb_page_info *)(skb->cb));
			skb_copy_to_linear_data(skb, page_address(pg_info->page)
						+ pg_info->page_offset, len);
			skb_put(skb, len);
			put_page(pg_info->page);
		}

		if (((oct->chip_id == OCTEON_CN66XX) ||
		     (oct->chip_id == OCTEON_CN68XX)) &&
		    ptp_enable) {
			if (rh->r_dh.has_hwtstamp) {
				/* timestamp is included from the hardware at
				 * the beginning of the packet.
				 */
				if (ifstate_check
				    (lio, LIO_IFSTATE_RX_TIMESTAMP_ENABLED)) {
					/* Nanoseconds are in the first 64-bits
					 * of the packet.
					 */
					memcpy(&ns, (skb->data), sizeof(ns));
					shhwtstamps = skb_hwtstamps(skb);
					shhwtstamps->hwtstamp =
						ns_to_ktime(ns +
							    lio->ptp_adjust);
				}
				skb_pull(skb, sizeof(ns));
			}
		}

		skb->protocol = eth_type_trans(skb, skb->dev);
		if ((netdev->features & NETIF_F_RXCSUM) &&
		    (((rh->r_dh.encap_on) &&
		      (rh->r_dh.csum_verified & CNNIC_TUN_CSUM_VERIFIED)) ||
		     (!(rh->r_dh.encap_on) &&
		      (rh->r_dh.csum_verified & CNNIC_CSUM_VERIFIED))))
			/* checksum has already been verified */
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;

		/* Setting Encapsulation field on basis of status received
		 * from the firmware
		 */
		if (rh->r_dh.encap_on) {
			skb->encapsulation = 1;
			skb->csum_level = 1;
			droq->stats.rx_vxlan++;
		}

		/* inbound VLAN tag */
		if ((netdev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
		    (rh->r_dh.vlan != 0)) {
			u16 vid = rh->r_dh.vlan;
			u16 priority = rh->r_dh.priority;

			vtag = priority << 13 | vid;
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vtag);
		}

		packet_was_received = napi_gro_receive(napi, skb) != GRO_DROP;

		if (packet_was_received) {
			droq->stats.rx_bytes_received += len;
			droq->stats.rx_pkts_received++;
			netdev->last_rx = jiffies;
		} else {
			droq->stats.rx_dropped++;
			netif_info(lio, rx_err, lio->netdev,
				   "droq:%d  error rx_dropped:%llu\n",
				   droq->q_no, droq->stats.rx_dropped);
		}

	} else {
		recv_buffer_free(skb);
	}
}

/**
 * \brief wrapper for calling napi_schedule
 * @param param parameters to pass to napi_schedule
 *
 * Used when scheduling on different CPUs
 */
static void napi_schedule_wrapper(void *param)
{
	struct napi_struct *napi = param;

	napi_schedule(napi);
}

/**
 * \brief callback when receive interrupt occurs and we are in NAPI mode
 * @param arg pointer to octeon output queue
 */
static void liquidio_napi_drv_callback(void *arg)
{
	struct octeon_device *oct;
	struct octeon_droq *droq = arg;
	int this_cpu = smp_processor_id();

	oct = droq->oct_dev;

	if (OCTEON_CN23XX_PF(oct) || droq->cpu_id == this_cpu) {
		napi_schedule_irqoff(&droq->napi);
	} else {
		struct call_single_data *csd = &droq->csd;

		csd->func = napi_schedule_wrapper;
		csd->info = &droq->napi;
		csd->flags = 0;

		smp_call_function_single_async(droq->cpu_id, csd);
	}
}

/**
 * \brief Entry point for NAPI polling
 * @param napi NAPI structure
 * @param budget maximum number of items to process
 */
static int liquidio_napi_poll(struct napi_struct *napi, int budget)
{
	struct octeon_droq *droq;
	int work_done;
	int tx_done = 0, iq_no;
	struct octeon_instr_queue *iq;
	struct octeon_device *oct;

	droq = container_of(napi, struct octeon_droq, napi);
	oct = droq->oct_dev;
	iq_no = droq->q_no;
	/* Handle Droq descriptors */
	work_done = octeon_process_droq_poll_cmd(oct, droq->q_no,
						 POLL_EVENT_PROCESS_PKTS,
						 budget);

	/* Flush the instruction queue */
	iq = oct->instr_queue[iq_no];
	if (iq) {
		/* Process iq buffers with in the budget limits */
		tx_done = octeon_flush_iq(oct, iq, 1, budget);
		/* Update iq read-index rather than waiting for next interrupt.
		 * Return back if tx_done is false.
		 */
		update_txq_status(oct, iq_no);
		/*tx_done = (iq->flush_index == iq->octeon_read_index);*/
	} else {
		dev_err(&oct->pci_dev->dev, "%s:  iq (%d) num invalid\n",
			__func__, iq_no);
	}

	if ((work_done < budget) && (tx_done)) {
		napi_complete(napi);
		octeon_process_droq_poll_cmd(droq->oct_dev, droq->q_no,
					     POLL_EVENT_ENABLE_INTR, 0);
		return 0;
	}

	return (!tx_done) ? (budget) : (work_done);
}

/**
 * \brief Setup input and output queues
 * @param octeon_dev octeon device
 * @param ifidx  Interface Index
 *
 * Note: Queues are with respect to the octeon device. Thus
 * an input queue is for egress packets, and output queues
 * are for ingress packets.
 */
static inline int setup_io_queues(struct octeon_device *octeon_dev,
				  int ifidx)
{
	struct octeon_droq_ops droq_ops;
	struct net_device *netdev;
	static int cpu_id;
	static int cpu_id_modulus;
	struct octeon_droq *droq;
	struct napi_struct *napi;
	int q, q_no, retval = 0;
	struct lio *lio;
	int num_tx_descs;

	netdev = octeon_dev->props[ifidx].netdev;

	lio = GET_LIO(netdev);

	memset(&droq_ops, 0, sizeof(struct octeon_droq_ops));

	droq_ops.fptr = liquidio_push_packet;
	droq_ops.farg = (void *)netdev;

	droq_ops.poll_mode = 1;
	droq_ops.napi_fn = liquidio_napi_drv_callback;
	cpu_id = 0;
	cpu_id_modulus = num_present_cpus();

	/* set up DROQs. */
	for (q = 0; q < lio->linfo.num_rxpciq; q++) {
		q_no = lio->linfo.rxpciq[q].s.q_no;
		dev_dbg(&octeon_dev->pci_dev->dev,
			"setup_io_queues index:%d linfo.rxpciq.s.q_no:%d\n",
			q, q_no);
		retval = octeon_setup_droq(octeon_dev, q_no,
					   CFG_GET_NUM_RX_DESCS_NIC_IF
						   (octeon_get_conf(octeon_dev),
						   lio->ifidx),
					   CFG_GET_NUM_RX_BUF_SIZE_NIC_IF
						   (octeon_get_conf(octeon_dev),
						   lio->ifidx), NULL);
		if (retval) {
			dev_err(&octeon_dev->pci_dev->dev,
				"%s : Runtime DROQ(RxQ) creation failed.\n",
				__func__);
			return 1;
		}

		droq = octeon_dev->droq[q_no];
		napi = &droq->napi;
		dev_dbg(&octeon_dev->pci_dev->dev, "netif_napi_add netdev:%llx oct:%llx pf_num:%d\n",
			(u64)netdev, (u64)octeon_dev, octeon_dev->pf_num);
		netif_napi_add(netdev, napi, liquidio_napi_poll, 64);

		/* designate a CPU for this droq */
		droq->cpu_id = cpu_id;
		cpu_id++;
		if (cpu_id >= cpu_id_modulus)
			cpu_id = 0;

		octeon_register_droq_ops(octeon_dev, q_no, &droq_ops);
	}

	if (OCTEON_CN23XX_PF(octeon_dev)) {
		/* 23XX PF can receive control messages (via the first PF-owned
		 * droq) from the firmware even if the ethX interface is down,
		 * so that's why poll_mode must be off for the first droq.
		 */
		octeon_dev->droq[0]->ops.poll_mode = 0;
	}

	/* set up IQs. */
	for (q = 0; q < lio->linfo.num_txpciq; q++) {
		num_tx_descs = CFG_GET_NUM_TX_DESCS_NIC_IF(octeon_get_conf
							   (octeon_dev),
							   lio->ifidx);
		retval = octeon_setup_iq(octeon_dev, ifidx, q,
					 lio->linfo.txpciq[q], num_tx_descs,
					 netdev_get_tx_queue(netdev, q));
		if (retval) {
			dev_err(&octeon_dev->pci_dev->dev,
				" %s : Runtime IQ(TxQ) creation failed.\n",
				__func__);
			return 1;
		}
	}

	return 0;
}

/**
 * \brief Poll routine for checking transmit queue status
 * @param work work_struct data structure
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
 * \brief Sets up the txq poll check
 * @param netdev network device
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
 * \brief Net device open for LiquidIO
 * @param netdev network device
 */
static int liquidio_open(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct napi_struct *napi, *n;

	if (oct->props[lio->ifidx].napi_enabled == 0) {
		list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
			napi_enable(napi);

		oct->props[lio->ifidx].napi_enabled = 1;

		if (OCTEON_CN23XX_PF(oct))
			oct->droq[0]->ops.poll_mode = 1;
	}

	oct_ptp_open(netdev);

	ifstate_set(lio, LIO_IFSTATE_RUNNING);

	/* Ready for link status updates */
	lio->intf_open = 1;

	netif_info(lio, ifup, lio->netdev, "Interface Open, ready for traffic\n");

	if (OCTEON_CN23XX_PF(oct)) {
		if (!oct->msix_on)
			if (setup_tx_poll_fn(netdev))
				return -1;
	} else {
		if (setup_tx_poll_fn(netdev))
			return -1;
	}

	start_txq(netdev);

	/* tell Octeon to start forwarding packets to host */
	send_rx_ctrl_cmd(lio, 1);

	dev_info(&oct->pci_dev->dev, "%s interface is opened\n",
		 netdev->name);

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

	ifstate_reset(lio, LIO_IFSTATE_RUNNING);

	netif_tx_disable(netdev);

	/* Inform that netif carrier is down */
	netif_carrier_off(netdev);
	lio->intf_open = 0;
	lio->linfo.link.s.link_up = 0;
	lio->link_changes++;

	/* Pause for a moment and wait for Octeon to flush out (to the wire) any
	 * egress packets that are in-flight.
	 */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(100));

	/* Now it should be safe to tell Octeon that nic interface is down. */
	send_rx_ctrl_cmd(lio, 0);

	if (OCTEON_CN23XX_PF(oct)) {
		if (!oct->msix_on)
			cleanup_tx_poll_fn(netdev);
	} else {
		cleanup_tx_poll_fn(netdev);
	}

	if (lio->ptp_clock) {
		ptp_clock_unregister(lio->ptp_clock);
		lio->ptp_clock = NULL;
	}

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
 * \brief Net device set_multicast_list
 * @param netdev network device
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
	nctrl.wait_time = 0;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "DEVFLAGS change failed in core (ret: 0x%x)\n",
			ret);
	}
}

/**
 * \brief Net device set_mac_address
 * @param netdev network device
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
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;
	nctrl.wait_time = 100;

	nctrl.udd[0] = 0;
	/* The MAC Address is presented in network byte order. */
	memcpy((u8 *)&nctrl.udd[0] + 2, addr->sa_data, ETH_ALEN);

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "MAC Address change failed\n");
		return -ENOMEM;
	}
	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(((u8 *)&lio->linfo.hw_addr) + 2, addr->sa_data, ETH_ALEN);

	return 0;
}

/**
 * \brief Net device get_stats
 * @param netdev network device
 */
static struct net_device_stats *liquidio_get_stats(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct net_device_stats *stats = &netdev->stats;
	struct octeon_device *oct;
	u64 pkts = 0, drop = 0, bytes = 0;
	struct oct_droq_stats *oq_stats;
	struct oct_iq_stats *iq_stats;
	int i, iq_no, oq_no;

	oct = lio->oct_dev;

	for (i = 0; i < lio->linfo.num_txpciq; i++) {
		iq_no = lio->linfo.txpciq[i].s.q_no;
		iq_stats = &oct->instr_queue[iq_no]->stats;
		pkts += iq_stats->tx_done;
		drop += iq_stats->tx_dropped;
		bytes += iq_stats->tx_tot_bytes;
	}

	stats->tx_packets = pkts;
	stats->tx_bytes = bytes;
	stats->tx_dropped = drop;

	pkts = 0;
	drop = 0;
	bytes = 0;

	for (i = 0; i < lio->linfo.num_rxpciq; i++) {
		oq_no = lio->linfo.rxpciq[i].s.q_no;
		oq_stats = &oct->droq[oq_no]->stats;
		pkts += oq_stats->rx_pkts_received;
		drop += (oq_stats->rx_dropped +
			 oq_stats->dropped_nodispatch +
			 oq_stats->dropped_toomany +
			 oq_stats->dropped_nomem);
		bytes += oq_stats->rx_bytes_received;
	}

	stats->rx_bytes = bytes;
	stats->rx_packets = pkts;
	stats->rx_dropped = drop;

	return stats;
}

/**
 * \brief Net device change_mtu
 * @param netdev network device
 */
static int liquidio_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_CHANGE_MTU;
	nctrl.ncmd.s.param1 = new_mtu;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Failed to set MTU\n");
		return -1;
	}

	lio->mtu = new_mtu;

	return 0;
}

/**
 * \brief Handler for SIOCSHWTSTAMP ioctl
 * @param netdev network device
 * @param ifr interface request
 * @param cmd command
 */
static int hwtstamp_ioctl(struct net_device *netdev, struct ifreq *ifr)
{
	struct hwtstamp_config conf;
	struct lio *lio = GET_LIO(netdev);

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

/**
 * \brief handle a Tx timestamp response
 * @param status response status
 * @param buf pointer to skb
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

/* \brief Send a data packet that will be timestamped
 * @param oct octeon device
 * @param ndata pointer to network data
 * @param finfo pointer to private network data
 */
static inline int send_nic_timestamp_pkt(struct octeon_device *oct,
					 struct octnic_data_pkt *ndata,
					 struct octnet_buf_free_info *finfo)
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

	ring_doorbell = 1;

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
 * @param netdev    pointer to network device
 * @returns whether the packet was transmitted to the device okay or not
 *             (NETDEV_TX_OK or NETDEV_TX_BUSY)
 */
static int liquidio_xmit(struct sk_buff *skb, struct net_device *netdev)
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
	int j;
	u64 dptr = 0;
	u32 tag = 0;

	lio = GET_LIO(netdev);
	oct = lio->oct_dev;

	if (netif_is_multiqueue(netdev)) {
		q_idx = skb->queue_mapping;
		q_idx = (q_idx % (lio->linfo.num_txpciq));
		tag = q_idx;
		iq_no = lio->linfo.txpciq[q_idx].s.q_no;
	} else {
		iq_no = lio->txq;
	}

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

	if (netif_is_multiqueue(netdev)) {
		if (octnet_iq_is_full(oct, ndata.q_no)) {
			/* defer sending if queue is full */
			netif_info(lio, tx_err, lio->netdev, "Transmit failed iq:%d full\n",
				   ndata.q_no);
			stats->tx_iq_busy++;
			return NETDEV_TX_BUSY;
		}
	} else {
		if (octnet_iq_is_full(oct, lio->txq)) {
			/* defer sending if queue is full */
			stats->tx_iq_busy++;
			netif_info(lio, tx_err, lio->netdev, "Transmit failed iq:%d full\n",
				   lio->txq);
			return NETDEV_TX_BUSY;
		}
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
		struct skb_frag_struct *frag;
		struct octnic_gather *g;

		spin_lock(&lio->glist_lock[q_idx]);
		g = (struct octnic_gather *)
			list_delete_head(&lio->glist[q_idx]);
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

		dma_sync_single_for_device(&oct->pci_dev->dev, g->sg_dma_ptr,
					   g->sg_size, DMA_TO_DEVICE);
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

	if (unlikely(cmdsetup.s.timestamp))
		status = send_nic_timestamp_pkt(oct, &ndata, finfo);
	else
		status = octnet_send_nic_data_pkt(oct, &ndata);
	if (status == IQ_SEND_FAILED)
		goto lio_xmit_failed;

	netif_info(lio, tx_queued, lio->netdev, "Transmit queued successfully\n");

	if (status == IQ_SEND_STOP)
		stop_q(lio->netdev, q_idx);

	netif_trans_update(netdev);

	if (skb_shinfo(skb)->gso_size)
		stats->tx_done += skb_shinfo(skb)->gso_segs;
	else
		stats->tx_done++;
	stats->tx_tot_bytes += skb->len;

	return NETDEV_TX_OK;

lio_xmit_failed:
	stats->tx_dropped++;
	netif_info(lio, tx_err, lio->netdev, "IQ%d Transmit dropped:%llu\n",
		   iq_no, stats->tx_dropped);
	if (dptr)
		dma_unmap_single(&oct->pci_dev->dev, dptr,
				 ndata.datasize, DMA_TO_DEVICE);
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
	txqs_wake(netdev);
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
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Add VLAN filter failed in core (ret: 0x%x)\n",
			ret);
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
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Add VLAN filter failed in core (ret: 0x%x)\n",
			ret);
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

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = command;
	nctrl.ncmd.s.param1 = rx_cmd;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev,
			"DEVFLAGS RXCSUM change failed in core(ret:0x%x)\n",
			ret);
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

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = command;
	nctrl.ncmd.s.more = vxlan_cmd_bit;
	nctrl.ncmd.s.param1 = vxlan_port;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev,
			"VxLAN port add/delete failed in core (ret:0x%x)\n",
			ret);
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

	/*Disable LRO if RXCSUM is off */
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

static struct net_device_ops lionetdevops = {
	.ndo_open		= liquidio_open,
	.ndo_stop		= liquidio_stop,
	.ndo_start_xmit		= liquidio_xmit,
	.ndo_get_stats		= liquidio_get_stats,
	.ndo_set_mac_address	= liquidio_set_mac,
	.ndo_set_rx_mode	= liquidio_set_mcast_list,
	.ndo_tx_timeout		= liquidio_tx_timeout,

	.ndo_vlan_rx_add_vid    = liquidio_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = liquidio_vlan_rx_kill_vid,
	.ndo_change_mtu		= liquidio_change_mtu,
	.ndo_do_ioctl		= liquidio_ioctl,
	.ndo_fix_features	= liquidio_fix_features,
	.ndo_set_features	= liquidio_set_features,
	.ndo_udp_tunnel_add	= liquidio_add_vxlan_port,
	.ndo_udp_tunnel_del	= liquidio_del_vxlan_port,
};

/** \brief Entry point for the liquidio module
 */
static int __init liquidio_init(void)
{
	int i;
	struct handshake *hs;

	init_completion(&first_stage);

	octeon_init_device_list(conf_type);

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

	if (recv_pkt->buffer_size[0] != sizeof(*ls)) {
		dev_err(&oct->pci_dev->dev, "Malformed NIC_INFO, len=%d, ifidx=%d\n",
			recv_pkt->buffer_size[0],
			recv_pkt->rh.r_nic_info.gmxport);
		goto nic_info_err;
	}

	gmxport = recv_pkt->rh.r_nic_info.gmxport;
	ls = (union oct_link_status *)get_rbd(recv_pkt->buffer_ptr[0]);

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
	struct lio *lio = NULL;
	struct net_device *netdev;
	u8 mac[6], i, j;
	struct octeon_soft_command *sc;
	struct liquidio_if_cfg_context *ctx;
	struct liquidio_if_cfg_resp *resp;
	struct octdev_props *props;
	int retval, num_iqueues, num_oqueues;
	union oct_nic_if_cfg if_cfg;
	unsigned int base_queue;
	unsigned int gmx_port_id;
	u32 resp_size, ctx_size, data_size;
	u32 ifidx_or_pfnum;
	struct lio_version *vdata;

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
		ctx_size = sizeof(struct liquidio_if_cfg_context);
		data_size = sizeof(struct lio_version);
		sc = (struct octeon_soft_command *)
			octeon_alloc_soft_command(octeon_dev, data_size,
						  resp_size, ctx_size);
		resp = (struct liquidio_if_cfg_resp *)sc->virtrptr;
		ctx  = (struct liquidio_if_cfg_context *)sc->ctxptr;
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
		WRITE_ONCE(ctx->cond, 0);
		ctx->octeon_id = lio_get_device_id(octeon_dev);
		init_waitqueue_head(&ctx->wc);

		if_cfg.u64 = 0;
		if_cfg.s.num_iqueues = num_iqueues;
		if_cfg.s.num_oqueues = num_oqueues;
		if_cfg.s.base_queue = base_queue;
		if_cfg.s.gmx_port_id = gmx_port_id;

		sc->iq_no = 0;

		octeon_prepare_soft_command(octeon_dev, sc, OPCODE_NIC,
					    OPCODE_NIC_IF_CFG, 0,
					    if_cfg.u64, 0);

		sc->callback = if_cfg_callback;
		sc->callback_arg = sc;
		sc->wait_time = 3000;

		retval = octeon_send_soft_command(octeon_dev, sc);
		if (retval == IQ_SEND_FAILED) {
			dev_err(&octeon_dev->pci_dev->dev,
				"iq/oq config failed status: %x\n",
				retval);
			/* Soft instr is freed by driver in case of failure. */
			goto setup_nic_dev_fail;
		}

		/* Sleep on a wait queue till the cond flag indicates that the
		 * response arrived or timed-out.
		 */
		if (sleep_cond(&ctx->wc, &ctx->cond) == -EINTR) {
			dev_err(&octeon_dev->pci_dev->dev, "Wait interrupted\n");
			goto setup_nic_wait_intr;
		}

		retval = resp->status;
		if (retval) {
			dev_err(&octeon_dev->pci_dev->dev, "iq/oq config failed\n");
			goto setup_nic_dev_fail;
		}

		octeon_swap_8B_data((u64 *)(&resp->cfg_info),
				    (sizeof(struct liquidio_if_cfg_info)) >> 3);

		num_iqueues = hweight64(resp->cfg_info.iqmask);
		num_oqueues = hweight64(resp->cfg_info.oqmask);

		if (!(num_iqueues) || !(num_oqueues)) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Got bad iqueues (%016llx) or oqueues (%016llx) from firmware.\n",
				resp->cfg_info.iqmask,
				resp->cfg_info.oqmask);
			goto setup_nic_dev_fail;
		}
		dev_dbg(&octeon_dev->pci_dev->dev,
			"interface %d, iqmask %016llx, oqmask %016llx, numiqueues %d, numoqueues %d\n",
			i, resp->cfg_info.iqmask, resp->cfg_info.oqmask,
			num_iqueues, num_oqueues);
		netdev = alloc_etherdev_mq(LIO_SIZE, num_iqueues);

		if (!netdev) {
			dev_err(&octeon_dev->pci_dev->dev, "Device allocation failed\n");
			goto setup_nic_dev_fail;
		}

		SET_NETDEV_DEV(netdev, &octeon_dev->pci_dev->dev);

		if (num_iqueues > 1)
			lionetdevops.ndo_select_queue = select_q;

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
		netif_set_gso_max_size(netdev, OCTNIC_GSO_MAX_SIZE);

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

		/* 64-bit swap required on LE machines */
		octeon_swap_8B_data(&lio->linfo.hw_addr, 1);
		for (j = 0; j < 6; j++)
			mac[j] = *((u8 *)(((u8 *)&lio->linfo.hw_addr) + 2 + j));

		/* Copy MAC Address to OS network device structure */

		ether_addr_copy(netdev->dev_addr, mac);

		/* By default all interfaces on a single Octeon uses the same
		 * tx and rx queues
		 */
		lio->txq = lio->linfo.txpciq[0].s.q_no;
		lio->rxq = lio->linfo.rxpciq[0].s.q_no;
		if (setup_io_queues(octeon_dev, i)) {
			dev_err(&octeon_dev->pci_dev->dev, "I/O queues creation failed\n");
			goto setup_nic_dev_fail;
		}

		ifstate_set(lio, LIO_IFSTATE_DROQ_OPS);

		lio->tx_qsize = octeon_get_tx_qsize(octeon_dev, lio->txq);
		lio->rx_qsize = octeon_get_rx_qsize(octeon_dev, lio->rxq);

		if (setup_glists(octeon_dev, lio, num_iqueues)) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Gather list allocation failed\n");
			goto setup_nic_dev_fail;
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

		liquidio_set_feature(netdev, OCTNET_CMD_ENABLE_VLAN_FILTER, 0);

		if ((debug != -1) && (debug & NETIF_MSG_HW))
			liquidio_set_feature(netdev,
					     OCTNET_CMD_VERBOSE_ENABLE, 0);

		if (setup_link_status_change_wq(netdev))
			goto setup_nic_dev_fail;

		/* Register the network device with the OS */
		if (register_netdev(netdev)) {
			dev_err(&octeon_dev->pci_dev->dev, "Device registration failed\n");
			goto setup_nic_dev_fail;
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

		octeon_free_soft_command(octeon_dev, sc);
	}

	return 0;

setup_nic_dev_fail:

	octeon_free_soft_command(octeon_dev, sc);

setup_nic_wait_intr:

	while (i--) {
		dev_err(&octeon_dev->pci_dev->dev,
			"NIC ifidx:%d Setup failed\n", i);
		liquidio_destroy_nic_device(octeon_dev, i);
	}
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
	struct oct_intrmod_cfg *intrmod_cfg;
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

	liquidio_ptp_init(oct);

	/* Initialize interrupt moderation params */
	intrmod_cfg = &((struct octeon_device *)oct)->intrmod;
	intrmod_cfg->rx_enable = 1;
	intrmod_cfg->check_intrvl = LIO_INTRMOD_CHECK_INTERVAL;
	intrmod_cfg->maxpkt_ratethr = LIO_INTRMOD_MAXPKT_RATETHR;
	intrmod_cfg->minpkt_ratethr = LIO_INTRMOD_MINPKT_RATETHR;
	intrmod_cfg->rx_maxcnt_trigger = LIO_INTRMOD_RXMAXCNT_TRIGGER;
	intrmod_cfg->rx_maxtmr_trigger = LIO_INTRMOD_RXMAXTMR_TRIGGER;
	intrmod_cfg->rx_mintmr_trigger = LIO_INTRMOD_RXMINTMR_TRIGGER;
	intrmod_cfg->rx_mincnt_trigger = LIO_INTRMOD_RXMINCNT_TRIGGER;
	intrmod_cfg->tx_enable = 1;
	intrmod_cfg->tx_maxcnt_trigger = LIO_INTRMOD_TXMAXCNT_TRIGGER;
	intrmod_cfg->tx_mincnt_trigger = LIO_INTRMOD_TXMINCNT_TRIGGER;
	intrmod_cfg->rx_frames = CFG_GET_OQ_INTR_PKT(octeon_get_conf(oct));
	intrmod_cfg->rx_usecs = CFG_GET_OQ_INTR_TIME(octeon_get_conf(oct));
	intrmod_cfg->tx_frames = CFG_GET_IQ_INTR_PKT(octeon_get_conf(oct));
	dev_dbg(&oct->pci_dev->dev, "Network interfaces ready\n");

	return retval;

octnet_init_failure:

	oct->ifcount = 0;

	return retval;
}

/**
 * \brief starter callback that invokes the remaining initialization work after
 * the NIC is up and running.
 * @param octptr  work struct work_struct
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

/**
 * \brief Device initialization for each Octeon device that is probed
 * @param octeon_dev  octeon device
 */
static int octeon_device_init(struct octeon_device *octeon_dev)
{
	int j, ret;
	int fw_loaded = 0;
	char bootcmd[] = "\n";
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)octeon_dev->priv;
	atomic_set(&octeon_dev->status, OCT_DEV_BEGIN_STATE);

	/* Enable access to the octeon device and make its DMA capability
	 * known to the OS.
	 */
	if (octeon_pci_os_setup(octeon_dev))
		return 1;

	/* Identify the Octeon type and map the BAR address space. */
	if (octeon_chip_specific_setup(octeon_dev)) {
		dev_err(&octeon_dev->pci_dev->dev, "Chip specific setup failed\n");
		return 1;
	}

	atomic_set(&octeon_dev->status, OCT_DEV_PCI_MAP_DONE);

	octeon_dev->app_mode = CVM_DRV_INVALID_APP;

	if (OCTEON_CN23XX_PF(octeon_dev)) {
		if (!cn23xx_fw_loaded(octeon_dev)) {
			fw_loaded = 0;
			/* Do a soft reset of the Octeon device. */
			if (octeon_dev->fn_list.soft_reset(octeon_dev))
				return 1;
			/* things might have changed */
			if (!cn23xx_fw_loaded(octeon_dev))
				fw_loaded = 0;
			else
				fw_loaded = 1;
		} else {
			fw_loaded = 1;
		}
	} else if (octeon_dev->fn_list.soft_reset(octeon_dev)) {
		return 1;
	}

	/* Initialize the dispatch mechanism used to push packets arriving on
	 * Octeon Output queues.
	 */
	if (octeon_init_dispatch_list(octeon_dev))
		return 1;

	octeon_register_dispatch_fn(octeon_dev, OPCODE_NIC,
				    OPCODE_NIC_CORE_DRV_ACTIVE,
				    octeon_core_drv_init,
				    octeon_dev);

	INIT_DELAYED_WORK(&octeon_dev->nic_poll_work.work, nic_starter);
	octeon_dev->nic_poll_work.ctxptr = (void *)octeon_dev;
	schedule_delayed_work(&octeon_dev->nic_poll_work.work,
			      LIQUIDIO_STARTER_POLL_INTERVAL_MS);

	atomic_set(&octeon_dev->status, OCT_DEV_DISPATCH_INIT_DONE);

	octeon_set_io_queues_off(octeon_dev);

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
		/* On error, release any previously allocated queues */
		for (j = 0; j < octeon_dev->num_iqs; j++)
			octeon_delete_instr_queue(octeon_dev, j);
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
		/* Release any previously allocated queues */
		for (j = 0; j < octeon_dev->num_oqs; j++)
			octeon_delete_droq(octeon_dev, j);
		return 1;
	}

	atomic_set(&octeon_dev->status, OCT_DEV_DROQ_INIT_DONE);

	if (OCTEON_CN23XX_PF(octeon_dev)) {
		if (octeon_allocate_ioq_vector(octeon_dev)) {
			dev_err(&octeon_dev->pci_dev->dev, "OCTEON: ioq vector allocation failed\n");
			return 1;
		}

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
	tasklet_init(&oct_priv->droq_tasklet, octeon_droq_bh,
		     (unsigned long)octeon_dev);

	/* Setup the interrupt handler and record the INT SUM register address
	 */
	if (octeon_setup_interrupt(octeon_dev))
		return 1;

	/* Enable Octeon device interrupts */
	octeon_dev->fn_list.enable_interrupt(octeon_dev, OCTEON_ALL_INTR);

	/* Enable the input and output queues for this Octeon device */
	ret = octeon_dev->fn_list.enable_io_queues(octeon_dev);
	if (ret) {
		dev_err(&octeon_dev->pci_dev->dev, "Failed to enable input/output queues");
		return ret;
	}

	atomic_set(&octeon_dev->status, OCT_DEV_IO_QUEUES_DONE);

	if ((!OCTEON_CN23XX_PF(octeon_dev)) || !fw_loaded) {
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
		ret = octeon_add_console(octeon_dev, 0);
		if (ret) {
			dev_err(&octeon_dev->pci_dev->dev, "Could not access board console\n");
			return 1;
		}

		atomic_set(&octeon_dev->status, OCT_DEV_CONSOLE_INIT_DONE);

		dev_dbg(&octeon_dev->pci_dev->dev, "Loading firmware\n");
		ret = load_firmware(octeon_dev);
		if (ret) {
			dev_err(&octeon_dev->pci_dev->dev, "Could not load firmware to board\n");
			return 1;
		}
		/* set bit 1 of SLI_SCRATCH_1 to indicate that firmware is
		 * loaded
		 */
		if (OCTEON_CN23XX_PF(octeon_dev))
			octeon_write_csr64(octeon_dev, CN23XX_SLI_SCRATCH1,
					   2ULL);
	}

	handshake[octeon_dev->octeon_id].init_ok = 1;
	complete(&handshake[octeon_dev->octeon_id].init);

	atomic_set(&octeon_dev->status, OCT_DEV_HOST_OK);

	/* Send Credit for Octeon Output queues. Credits are always sent after
	 * the output queue is enabled.
	 */
	for (j = 0; j < octeon_dev->num_oqs; j++)
		writel(octeon_dev->droq[j]->max_count,
		       octeon_dev->droq[j]->pkts_credit_reg);

	/* Packets can start arriving on the output queues from this point. */
	return 0;
}

/**
 * \brief Exits the module
 */
static void __exit liquidio_exit(void)
{
	liquidio_deinit_pci();

	pr_info("LiquidIO network module is now unloaded\n");
}

module_init(liquidio_init);
module_exit(liquidio_exit);
