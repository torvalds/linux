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
#include <linux/module.h>
#include <linux/crc32.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <linux/net_tstamp.h>
#include <linux/if_vlan.h>
#include <linux/firmware.h>
#include <linux/ethtool.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include "octeon_config.h"
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
#include "cn68xx_regs.h"
#include "cn68xx_device.h"
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

static u32 console_bitmask;
module_param(console_bitmask, int, 0644);
MODULE_PARM_DESC(console_bitmask,
		 "Bitmask indicating which consoles have debug output redirected to syslog.");

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "NETIF_MSG debug bits");

static char fw_type[LIO_MAX_FW_TYPE_LEN];
module_param_string(fw_type, fw_type, sizeof(fw_type), 0000);
MODULE_PARM_DESC(fw_type, "Type of firmware to be loaded. Default \"nic\"");

static int conf_type;
module_param(conf_type, int, 0);
MODULE_PARM_DESC(conf_type, "select octeon configuration 0 default 1 ovs");

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
#define OCTNIC_GSO_MAX_SIZE (GSO_MAX_SIZE - OCTNIC_GSO_MAX_HEADER_SIZE)

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
};

/** This structure is used by NIC driver to store information required
 * to free the sk_buff when the packet has been fetched by Octeon.
 * Bytes offset below assume worst-case of a 64-bit system.
 */
struct octnet_buf_free_info {
	/** Bytes 1-8.  Pointer to network device private structure. */
	struct lio *lio;

	/** Bytes 9-16.  Pointer to sk_buff. */
	struct sk_buff *skb;

	/** Bytes 17-24.  Pointer to gather list. */
	struct octnic_gather *g;

	/** Bytes 25-32. Physical address of skb->data or gather list. */
	u64 dptr;

	/** Bytes 33-47. Piggybacked soft command, if any */
	struct octeon_soft_command *sc;
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
	for (q_no = 0; q_no < MAX_OCTEON_OUTPUT_QUEUES; q_no++) {
		if (!(oct->io_qmask.oq & (1UL << q_no)))
			continue;
		reschedule |= octeon_droq_process_packets(oct, oct->droq[q_no],
							  MAX_PACKET_BUDGET);
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

		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES; i++) {
			if (!(oct->io_qmask.oq & (1UL << i)))
				continue;
			pkt_cnt += octeon_droq_check_hw_for_pkts(oct,
								 oct->droq[i]);
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

void octeon_report_tx_completion_to_bql(void *txq, unsigned int pkts_compl,
					unsigned int bytes_compl)
{
	struct netdev_queue *netdev_queue = txq;

	netdev_tx_completed_queue(netdev_queue, pkts_compl, bytes_compl);
}

void octeon_update_tx_completion_counters(void *buf, int reqtype,
					  unsigned int *pkts_compl,
					  unsigned int *bytes_compl)
{
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb = NULL;
	struct octeon_soft_command *sc;

	switch (reqtype) {
	case REQTYPE_NORESP_NET:
	case REQTYPE_NORESP_NET_SG:
		finfo = buf;
		skb = finfo->skb;
		break;

	case REQTYPE_RESP_NET_SG:
	case REQTYPE_RESP_NET:
		sc = buf;
		skb = sc->callback_arg;
		break;

	default:
		return;
	}

	(*pkts_compl)++;
	*bytes_compl += skb->len;
}

void octeon_report_sent_bytes_to_bql(void *buf, int reqtype)
{
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb;
	struct octeon_soft_command *sc;
	struct netdev_queue *txq;

	switch (reqtype) {
	case REQTYPE_NORESP_NET:
	case REQTYPE_NORESP_NET_SG:
		finfo = buf;
		skb = finfo->skb;
		break;

	case REQTYPE_RESP_NET_SG:
	case REQTYPE_RESP_NET:
		sc = buf;
		skb = sc->callback_arg;
		break;

	default:
		return;
	}

	txq = netdev_get_tx_queue(skb->dev, skb_get_queue_mapping(skb));
	netdev_tx_sent_queue(txq, skb->len);
}

int octeon_console_debug_enabled(u32 console)
{
	return (console_bitmask >> (console)) & 0x1;
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
	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES; i++) {
		struct octeon_instr_queue *iq;

		if (!(oct->io_qmask.iq & (1UL << i)))
			continue;
		iq = oct->instr_queue[i];

		if (atomic_read(&iq->instr_pending)) {
			spin_lock_bh(&iq->lock);
			iq->fill_cnt = 0;
			iq->octeon_read_index = iq->host_write_index;
			iq->stats.instr_processed +=
				atomic_read(&iq->instr_pending);
			lio_process_iq_request_list(oct, iq);
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
	oct->fn_list.disable_interrupt(oct->chip);

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
static pci_ers_result_t liquidio_pcie_mmio_enabled(struct pci_dev *pdev)
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
static pci_ers_result_t liquidio_pcie_slot_reset(struct pci_dev *pdev)
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
static void liquidio_pcie_resume(struct pci_dev *pdev)
{
	/* Nothing to be done here. */
}

#ifdef CONFIG_PM
/**
 * \brief called when suspending
 * @param pdev Pointer to PCI device
 * @param state state to suspend to
 */
static int liquidio_suspend(struct pci_dev *pdev, pm_message_t state)
{
	return 0;
}

/**
 * \brief called when resuming
 * @param pdev Pointer to PCI device
 */
static int liquidio_resume(struct pci_dev *pdev)
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
	if (netif_is_multiqueue(netdev)) {
		int i;

		for (i = 0; i < netdev->num_tx_queues; i++)
			netif_wake_subqueue(netdev, i);
	} else {
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

	if (lio->linfo.link.s.status) {
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
			iq = lio->linfo.txpciq[q & (lio->linfo.num_txpciq - 1)];
			if (octnet_iq_is_full(lio->oct_dev, iq))
				continue;
			wake_q(lio->netdev, q);
			ret_val++;
		}
	} else {
		if (octnet_iq_is_full(lio->oct_dev, lio->txq))
			return 0;
		wake_q(lio->netdev, lio->txq);
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
 * \brief Delete gather list
 * @param lio per-network private data
 */
static void delete_glist(struct lio *lio)
{
	struct octnic_gather *g;

	do {
		g = (struct octnic_gather *)
		    list_delete_head(&lio->glist);
		if (g) {
			if (g->sg)
				kfree((void *)((unsigned long)g->sg -
						g->adjust));
			kfree(g);
		}
	} while (g);
}

/**
 * \brief Setup gather list
 * @param lio per-network private data
 */
static int setup_glist(struct lio *lio)
{
	int i;
	struct octnic_gather *g;

	INIT_LIST_HEAD(&lio->glist);

	for (i = 0; i < lio->tx_qsize; i++) {
		g = kzalloc(sizeof(*g), GFP_KERNEL);
		if (!g)
			break;

		g->sg_size =
			((ROUNDUP4(OCTNIC_MAX_SG) >> 2) * OCT_SG_ENTRY_SIZE);

		g->sg = kmalloc(g->sg_size + 8, GFP_KERNEL);
		if (!g->sg) {
			kfree(g);
			break;
		}

		/* The gather component should be aligned on 64-bit boundary */
		if (((unsigned long)g->sg) & 7) {
			g->adjust = 8 - (((unsigned long)g->sg) & 7);
			g->sg = (struct octeon_sg_entry *)
				((unsigned long)g->sg + g->adjust);
		}
		list_add_tail(&g->list, &lio->glist);
	}

	if (i == lio->tx_qsize)
		return 0;

	delete_glist(lio);
	return 1;
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

		if (linfo->link.s.status) {
			netif_info(lio, link, lio->netdev, "%d Mbps %s Duplex UP\n",
				   linfo->link.s.speed,
				   (linfo->link.s.duplex) ? "Full" : "Half");
		} else {
			netif_info(lio, link, lio->netdev, "Link Down\n");
		}
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

	if ((lio->intf_open) && (lio->linfo.link.u64 != ls->u64)) {
		lio->linfo.link.u64 = ls->u64;

		print_link_info(netdev);

		if (lio->linfo.link.s.status) {
			netif_carrier_on(netdev);
			/* start_txq(netdev); */
			txqs_wake(netdev);
		} else {
			netif_carrier_off(netdev);
			stop_txq(netdev);
		}
	}
}

/**
 * \brief Droq packet processor sceduler
 * @param oct octeon device
 */
static
void liquidio_schedule_droq_pkt_handlers(struct octeon_device *oct)
{
	struct octeon_device_priv *oct_priv =
		(struct octeon_device_priv *)oct->priv;
	u64 oq_no;
	struct octeon_droq *droq;

	if (oct->int_status & OCT_DEV_INTR_PKT_DATA) {
		for (oq_no = 0; oq_no < MAX_OCTEON_OUTPUT_QUEUES; oq_no++) {
			if (!(oct->droq_intr & (1 << oq_no)))
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

/**
 * \brief Interrupt handler for octeon
 * @param irq unused
 * @param dev octeon device
 */
static
irqreturn_t liquidio_intr_handler(int irq __attribute__((unused)), void *dev)
{
	struct octeon_device *oct = (struct octeon_device *)dev;
	irqreturn_t ret;

	/* Disable our interrupts for the duration of ISR */
	oct->fn_list.disable_interrupt(oct->chip);

	ret = oct->fn_list.process_interrupt_regs(oct);

	if (ret == IRQ_HANDLED)
		liquidio_schedule_droq_pkt_handlers(oct);

	/* Re-enable our interrupts  */
	if (!(atomic_read(&oct->status) == OCT_DEV_IN_RESET))
		oct->fn_list.enable_interrupt(oct->chip);

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

	err = pci_enable_msi(oct->pci_dev);
	if (err)
		dev_warn(&oct->pci_dev->dev, "Reverting to legacy interrupts. Error: %d\n",
			 err);
	else
		oct->flags |= LIO_FLAG_MSI_ENABLED;

	irqret = request_irq(oct->pci_dev->irq, liquidio_intr_handler,
			     IRQF_SHARED, "octeon", oct);
	if (irqret) {
		if (oct->flags & LIO_FLAG_MSI_ENABLED)
			pci_disable_msi(oct->pci_dev);
		dev_err(&oct->pci_dev->dev, "Request IRQ failed with code: %d\n",
			irqret);
		return 1;
	}

	return 0;
}

/**
 * \brief PCI probe handler
 * @param pdev PCI device structure
 * @param ent unused
 */
static int liquidio_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct octeon_device *oct_dev = NULL;
	struct handshake *hs;

	oct_dev = octeon_allocate_device(pdev->device,
					 sizeof(struct octeon_device_priv));
	if (!oct_dev) {
		dev_err(&pdev->dev, "Unable to allocate device\n");
		return -ENOMEM;
	}

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
		oct->fn_list.disable_interrupt(oct->chip);

		/* Release the interrupt line */
		free_irq(oct->pci_dev->irq, oct);

		if (oct->flags & LIO_FLAG_MSI_ENABLED)
			pci_disable_msi(oct->pci_dev);

		/* Soft reset the octeon device before exiting */
		oct->fn_list.soft_reset(oct);

		/* Disable the device, releasing the PCI INT */
		pci_disable_device(oct->pci_dev);

		/* fallthrough */
	case OCT_DEV_IN_RESET:
	case OCT_DEV_DROQ_INIT_DONE:
		/*atomic_set(&oct->status, OCT_DEV_DROQ_INIT_DONE);*/
		mdelay(100);
		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES; i++) {
			if (!(oct->io_qmask.oq & (1UL << i)))
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
	case OCT_DEV_SC_BUFF_POOL_INIT_DONE:
		octeon_free_sc_buffer_pool(oct);

		/* fallthrough */
	case OCT_DEV_INSTR_QUEUE_INIT_DONE:
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES; i++) {
			if (!(oct->io_qmask.iq & (1UL << i)))
				continue;
			octeon_delete_instr_queue(oct, i);
		}

		/* fallthrough */
	case OCT_DEV_DISPATCH_INIT_DONE:
		octeon_delete_dispatch_list(oct);
		cancel_delayed_work_sync(&oct->nic_poll_work.work);

		/* fallthrough */
	case OCT_DEV_PCI_MAP_DONE:
		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);

		/* fallthrough */
	case OCT_DEV_BEGIN_STATE:
		/* Nothing to be done here either */
		break;
	}                       /* end switch(oct->status) */

	tasklet_kill(&oct_priv->droq_tasklet);
}

/**
 * \brief Send Rx control command
 * @param lio per-network private data
 * @param start_stop whether to start or stop
 */
static void send_rx_ctrl_cmd(struct lio *lio, int start_stop)
{
	struct octnic_ctrl_pkt nctrl;
	struct octnic_ctrl_params nparams;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.s.cmd = OCTNET_CMD_RX_CTL;
	nctrl.ncmd.s.param1 = lio->linfo.ifidx;
	nctrl.ncmd.s.param2 = start_stop;
	nctrl.netpndev = (u64)lio->netdev;

	nparams.resp_order = OCTEON_RESP_NORESPONSE;

	if (octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl, nparams) < 0)
		netif_info(lio, rx_err, lio->netdev, "Failed to send RX Control message\n");
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

	if (!netdev) {
		dev_err(&oct->pci_dev->dev, "%s No netdevice ptr for index %d\n",
			__func__, ifidx);
		return;
	}

	lio = GET_LIO(netdev);

	dev_dbg(&oct->pci_dev->dev, "NIC device cleanup\n");

	send_rx_ctrl_cmd(lio, 0);

	if (atomic_read(&lio->ifstate) & LIO_IFSTATE_RUNNING)
		txqs_stop(netdev);

	if (atomic_read(&lio->ifstate) & LIO_IFSTATE_REGISTERED)
		unregister_netdev(netdev);

	delete_glist(lio);

	free_netdev(netdev);

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

	for (i = 0; i < oct->ifcount; i++) {
		lio = GET_LIO(oct->props[i].netdev);
		for (j = 0; j < lio->linfo.num_rxpciq; j++)
			octeon_unregister_droq_ops(oct, lio->linfo.rxpciq[j]);
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
	default:
		dev_err(&oct->pci_dev->dev, "Unknown device found (dev_id: %x)\n",
			dev_id);
	}

	if (!ret)
		dev_info(&oct->pci_dev->dev, "CN68XX PASS%d.%d %s\n",
			 OCTEON_MAJOR_REV(oct),
			 OCTEON_MINOR_REV(oct),
			 octeon_get_conf(oct)->card_name);

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
		iq = lio->linfo.txpciq[(q & (lio->linfo.num_txpciq - 1))];
	} else {
		iq = lio->txq;
	}

	if (octnet_iq_is_full(lio->oct_dev, iq))
		return 0;
	wake_q(lio->netdev, q);
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

	recv_buffer_free((struct sk_buff *)skb);
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
	int i, frags;

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

	dma_unmap_single(&lio->oct_dev->pci_dev->dev,
			 finfo->dptr, g->sg_size,
			 DMA_TO_DEVICE);

	spin_lock(&lio->lock);
	list_add_tail(&g->list, &lio->glist);
	spin_unlock(&lio->lock);

	check_txq_state(lio, skb);     /* mq support: sub-queue state check */

	recv_buffer_free((struct sk_buff *)skb);
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
	int i, frags;

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

	dma_unmap_single(&lio->oct_dev->pci_dev->dev,
			 finfo->dptr, g->sg_size,
			 DMA_TO_DEVICE);

	spin_lock(&lio->lock);
	list_add_tail(&g->list, &lio->glist);
	spin_unlock(&lio->lock);

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
static int liquidio_ptp_enable(struct ptp_clock_info *ptp,
			       struct ptp_clock_request *rq, int on)
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
			    u32 status,
			    void *buf)
{
	struct octeon_soft_command *sc = (struct octeon_soft_command *)buf;
	struct liquidio_if_cfg_resp *resp;
	struct liquidio_if_cfg_context *ctx;

	resp = (struct liquidio_if_cfg_resp *)sc->virtrptr;
	ctx  = (struct liquidio_if_cfg_context *)sc->ctxptr;

	oct = lio_get_device(ctx->octeon_id);
	if (resp->status)
		dev_err(&oct->pci_dev->dev, "nic if cfg instruction failed. Status: %llx\n",
			CVM_CAST64(resp->status));
	ACCESS_ONCE(ctx->cond) = 1;

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
		    void *accel_priv, select_queue_fallback_t fallback)
{
	int qindex;
	struct lio *lio;

	lio = GET_LIO(dev);
	/* select queue on chosen queue_mapping or core */
	qindex = skb_rx_queue_recorded(skb) ?
		 skb_get_rx_queue(skb) : smp_processor_id();
	return (u16)(qindex & (lio->linfo.num_txpciq - 1));
}

/** Routine to push packets arriving on Octeon interface upto network layer.
 * @param oct_id   - octeon device id.
 * @param skbuff   - skbuff struct to be passed to network layer.
 * @param len      - size of total data received.
 * @param rh       - Control header associated with the packet
 * @param param    - additional control data with the packet
 */
static void
liquidio_push_packet(u32 octeon_id,
		     void *skbuff,
		     u32 len,
		     union octeon_rh *rh,
		     void *param)
{
	struct napi_struct *napi = param;
	struct octeon_device *oct = lio_get_device(octeon_id);
	struct sk_buff *skb = (struct sk_buff *)skbuff;
	struct skb_shared_hwtstamps *shhwtstamps;
	u64 ns;
	struct net_device *netdev =
		(struct net_device *)oct->props[rh->r_dh.link].netdev;
	struct octeon_droq *droq = container_of(param, struct octeon_droq,
						napi);
	if (netdev) {
		int packet_was_received;
		struct lio *lio = GET_LIO(netdev);

		/* Do not proceed if the interface is not in RUNNING state. */
		if (!ifstate_check(lio, LIO_IFSTATE_RUNNING)) {
			recv_buffer_free(skb);
			droq->stats.rx_dropped++;
			return;
		}

		skb->dev = netdev;

		if (rh->r_dh.has_hwtstamp) {
			/* timestamp is included from the hardware at the
			 * beginning of the packet.
			 */
			if (ifstate_check(lio,
					  LIO_IFSTATE_RX_TIMESTAMP_ENABLED)) {
				/* Nanoseconds are in the first 64-bits
				 * of the packet.
				 */
				memcpy(&ns, (skb->data), sizeof(ns));
				shhwtstamps = skb_hwtstamps(skb);
				shhwtstamps->hwtstamp =
					ns_to_ktime(ns + lio->ptp_adjust);
			}
			skb_pull(skb, sizeof(ns));
		}

		skb->protocol = eth_type_trans(skb, skb->dev);

		if ((netdev->features & NETIF_F_RXCSUM) &&
		    (rh->r_dh.csum_verified == CNNIC_CSUM_VERIFIED))
			/* checksum has already been verified */
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;

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
	struct octeon_droq *droq = arg;
	int this_cpu = smp_processor_id();

	if (droq->cpu_id == this_cpu) {
		napi_schedule(&droq->napi);
	} else {
		struct call_single_data *csd = &droq->csd;

		csd->func = napi_schedule_wrapper;
		csd->info = &droq->napi;
		csd->flags = 0;

		smp_call_function_single_async(droq->cpu_id, csd);
	}
}

/**
 * \brief Main NAPI poll function
 * @param droq octeon output queue
 * @param budget maximum number of items to process
 */
static int liquidio_napi_do_rx(struct octeon_droq *droq, int budget)
{
	int work_done;
	struct lio *lio = GET_LIO(droq->napi.dev);
	struct octeon_device *oct = lio->oct_dev;

	work_done = octeon_process_droq_poll_cmd(oct, droq->q_no,
						 POLL_EVENT_PROCESS_PKTS,
						 budget);
	if (work_done < 0) {
		netif_info(lio, rx_err, lio->netdev,
			   "Receive work_done < 0, rxq:%d\n", droq->q_no);
		goto octnet_napi_finish;
	}

	if (work_done > budget)
		dev_err(&oct->pci_dev->dev, ">>>> %s work_done: %d budget: %d\n",
			__func__, work_done, budget);

	return work_done;

octnet_napi_finish:
	napi_complete(&droq->napi);
	octeon_process_droq_poll_cmd(oct, droq->q_no, POLL_EVENT_ENABLE_INTR,
				     0);
	return 0;
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

	droq = container_of(napi, struct octeon_droq, napi);

	work_done = liquidio_napi_do_rx(droq, budget);

	if (work_done < budget) {
		napi_complete(napi);
		octeon_process_droq_poll_cmd(droq->oct_dev, droq->q_no,
					     POLL_EVENT_ENABLE_INTR, 0);
		return 0;
	}

	return work_done;
}

/**
 * \brief Setup input and output queues
 * @param octeon_dev octeon device
 * @param net_device Net device
 *
 * Note: Queues are with respect to the octeon device. Thus
 * an input queue is for egress packets, and output queues
 * are for ingress packets.
 */
static inline int setup_io_queues(struct octeon_device *octeon_dev,
				  struct net_device *net_device)
{
	static int first_time = 1;
	static struct octeon_droq_ops droq_ops;
	static int cpu_id;
	static int cpu_id_modulus;
	struct octeon_droq *droq;
	struct napi_struct *napi;
	int q, q_no, retval = 0;
	struct lio *lio;
	int num_tx_descs;

	lio = GET_LIO(net_device);
	if (first_time) {
		first_time = 0;
		memset(&droq_ops, 0, sizeof(struct octeon_droq_ops));

		droq_ops.fptr = liquidio_push_packet;

		droq_ops.poll_mode = 1;
		droq_ops.napi_fn = liquidio_napi_drv_callback;
		cpu_id = 0;
		cpu_id_modulus = num_present_cpus();
	}

	/* set up DROQs. */
	for (q = 0; q < lio->linfo.num_rxpciq; q++) {
		q_no = lio->linfo.rxpciq[q];

		retval = octeon_setup_droq(octeon_dev, q_no,
					   CFG_GET_NUM_RX_DESCS_NIC_IF
						   (octeon_get_conf(octeon_dev),
						   lio->ifidx),
					   CFG_GET_NUM_RX_BUF_SIZE_NIC_IF
						   (octeon_get_conf(octeon_dev),
						   lio->ifidx), NULL);
		if (retval) {
			dev_err(&octeon_dev->pci_dev->dev,
				" %s : Runtime DROQ(RxQ) creation failed.\n",
				__func__);
			return 1;
		}

		droq = octeon_dev->droq[q_no];
		napi = &droq->napi;
		netif_napi_add(net_device, napi, liquidio_napi_poll, 64);

		/* designate a CPU for this droq */
		droq->cpu_id = cpu_id;
		cpu_id++;
		if (cpu_id >= cpu_id_modulus)
			cpu_id = 0;

		octeon_register_droq_ops(octeon_dev, q_no, &droq_ops);
	}

	/* set up IQs. */
	for (q = 0; q < lio->linfo.num_txpciq; q++) {
		num_tx_descs = CFG_GET_NUM_TX_DESCS_NIC_IF(octeon_get_conf
							   (octeon_dev),
							   lio->ifidx);
		retval = octeon_setup_iq(octeon_dev, lio->linfo.txpciq[q],
					 num_tx_descs,
					 netdev_get_tx_queue(net_device, q));
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
static inline void setup_tx_poll_fn(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	lio->txq_status_wq.wq = create_workqueue("txq-status");
	if (!lio->txq_status_wq.wq) {
		dev_err(&oct->pci_dev->dev, "unable to create cavium txq status wq\n");
		return;
	}
	INIT_DELAYED_WORK(&lio->txq_status_wq.wk.work,
			  octnet_poll_check_txq_status);
	lio->txq_status_wq.wk.ctxptr = lio;
	queue_delayed_work(lio->txq_status_wq.wq,
			   &lio->txq_status_wq.wk.work, msecs_to_jiffies(1));
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

	list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
		napi_enable(napi);

	oct_ptp_open(netdev);

	ifstate_set(lio, LIO_IFSTATE_RUNNING);
	setup_tx_poll_fn(netdev);
	start_txq(netdev);

	netif_info(lio, ifup, lio->netdev, "Interface Open, ready for traffic\n");
	try_module_get(THIS_MODULE);

	/* tell Octeon to start forwarding packets to host */
	send_rx_ctrl_cmd(lio, 1);

	/* Ready for link status updates */
	lio->intf_open = 1;

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
	struct napi_struct *napi, *n;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	netif_info(lio, ifdown, lio->netdev, "Stopping interface!\n");
	/* Inform that netif carrier is down */
	lio->intf_open = 0;
	lio->linfo.link.s.status = 0;

	netif_carrier_off(netdev);

	/* tell Octeon to stop forwarding packets to host */
	send_rx_ctrl_cmd(lio, 0);

	cancel_delayed_work_sync(&lio->txq_status_wq.wk.work);
	flush_workqueue(lio->txq_status_wq.wq);
	destroy_workqueue(lio->txq_status_wq.wq);

	if (lio->ptp_clock) {
		ptp_clock_unregister(lio->ptp_clock);
		lio->ptp_clock = NULL;
	}

	ifstate_reset(lio, LIO_IFSTATE_RUNNING);

	/* This is a hack that allows DHCP to continue working. */
	set_bit(__LINK_STATE_START, &lio->netdev->state);

	list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
		napi_disable(napi);

	txqs_stop(netdev);

	dev_info(&oct->pci_dev->dev, "%s interface is stopped\n", netdev->name);
	module_put(THIS_MODULE);

	return 0;
}

void liquidio_link_ctrl_cmd_completion(void *nctrl_ptr)
{
	struct octnic_ctrl_pkt *nctrl = (struct octnic_ctrl_pkt *)nctrl_ptr;
	struct net_device *netdev = (struct net_device *)nctrl->netpndev;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	switch (nctrl->ncmd.s.cmd) {
	case OCTNET_CMD_CHANGE_DEVFLAGS:
	case OCTNET_CMD_SET_MULTI_LIST:
		break;

	case OCTNET_CMD_CHANGE_MACADDR:
		/* If command is successful, change the MACADDR. */
		netif_info(lio, probe, lio->netdev, " MACAddr changed to 0x%llx\n",
			   CVM_CAST64(nctrl->udd[0]));
		dev_info(&oct->pci_dev->dev, "%s MACAddr changed to 0x%llx\n",
			 netdev->name, CVM_CAST64(nctrl->udd[0]));
		memcpy(netdev->dev_addr, ((u8 *)&nctrl->udd[0]) + 2, ETH_ALEN);
		break;

	case OCTNET_CMD_CHANGE_MTU:
		/* If command is successful, change the MTU. */
		netif_info(lio, probe, lio->netdev, " MTU Changed from %d to %d\n",
			   netdev->mtu, nctrl->ncmd.s.param2);
		dev_info(&oct->pci_dev->dev, "%s MTU Changed from %d to %d\n",
			 netdev->name, netdev->mtu,
			 nctrl->ncmd.s.param2);
		netdev->mtu = nctrl->ncmd.s.param2;
		break;

	case OCTNET_CMD_GPIO_ACCESS:
		netif_info(lio, probe, lio->netdev, "LED Flashing visual identification\n");

		break;

	case OCTNET_CMD_LRO_ENABLE:
		dev_info(&oct->pci_dev->dev, "%s LRO Enabled\n", netdev->name);
		break;

	case OCTNET_CMD_LRO_DISABLE:
		dev_info(&oct->pci_dev->dev, "%s LRO Disabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_VERBOSE_ENABLE:
		dev_info(&oct->pci_dev->dev, "%s LRO Enabled\n", netdev->name);
		break;

	case OCTNET_CMD_VERBOSE_DISABLE:
		dev_info(&oct->pci_dev->dev, "%s LRO Disabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_SET_SETTINGS:
		dev_info(&oct->pci_dev->dev, "%s settings changed\n",
			 netdev->name);

		break;

	default:
		dev_err(&oct->pci_dev->dev, "%s Unknown cmd %d\n", __func__,
			nctrl->ncmd.s.cmd);
	}
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
	struct octnic_ctrl_params nparams;
	struct netdev_hw_addr *ha;
	u64 *mc;
	int ret, i;
	int mc_count = min(netdev_mc_count(netdev), MAX_OCTEON_MULTICAST_ADDR);

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	/* Create a ctrl pkt command to be sent to core app. */
	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_MULTI_LIST;
	nctrl.ncmd.s.param1 = lio->linfo.ifidx;
	nctrl.ncmd.s.param2 = get_new_flags(netdev);
	nctrl.ncmd.s.param3 = mc_count;
	nctrl.ncmd.s.more = mc_count;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	/* copy all the addresses into the udd */
	i = 0;
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

	nparams.resp_order = OCTEON_RESP_NORESPONSE;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl, nparams);
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
	struct octnic_ctrl_params nparams;

	if ((!is_valid_ether_addr(addr->sa_data)) ||
	    (ifstate_check(lio, LIO_IFSTATE_RUNNING)))
		return -EADDRNOTAVAIL;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_CHANGE_MACADDR;
	nctrl.ncmd.s.param1 = lio->linfo.ifidx;
	nctrl.ncmd.s.param2 = 0;
	nctrl.ncmd.s.more = 1;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;
	nctrl.wait_time = 100;

	nctrl.udd[0] = 0;
	/* The MAC Address is presented in network byte order. */
	memcpy((u8 *)&nctrl.udd[0] + 2, addr->sa_data, ETH_ALEN);

	nparams.resp_order = OCTEON_RESP_ORDERED;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl, nparams);
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
		iq_no = lio->linfo.txpciq[i];
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
		oq_no = lio->linfo.rxpciq[i];
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
	struct octnic_ctrl_params nparams;
	int max_frm_size = new_mtu + OCTNET_FRM_HEADER_SIZE;
	int ret = 0;

	/* Limit the MTU to make sure the ethernet packets are between 64 bytes
	 * and 65535 bytes
	 */
	if ((max_frm_size < OCTNET_MIN_FRM_SIZE) ||
	    (max_frm_size > OCTNET_MAX_FRM_SIZE)) {
		dev_err(&oct->pci_dev->dev, "Invalid MTU: %d\n", new_mtu);
		dev_err(&oct->pci_dev->dev, "Valid range %d and %d\n",
			(OCTNET_MIN_FRM_SIZE - OCTNET_FRM_HEADER_SIZE),
			(OCTNET_MAX_FRM_SIZE - OCTNET_FRM_HEADER_SIZE));
		return -EINVAL;
	}

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_CHANGE_MTU;
	nctrl.ncmd.s.param1 = lio->linfo.ifidx;
	nctrl.ncmd.s.param2 = new_mtu;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	nparams.resp_order = OCTEON_RESP_ORDERED;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl, nparams);
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
static int hwtstamp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
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
		return hwtstamp_ioctl(netdev, ifr, cmd);
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
	recv_buffer_free(skb);
}

/* \brief Send a data packet that will be timestamped
 * @param oct octeon device
 * @param ndata pointer to network data
 * @param finfo pointer to private network data
 */
static inline int send_nic_timestamp_pkt(struct octeon_device *oct,
					 struct octnic_data_pkt *ndata,
					 struct octnet_buf_free_info *finfo,
					 int xmit_more)
{
	int retval;
	struct octeon_soft_command *sc;
	struct octeon_instr_ih *ih;
	struct octeon_instr_rdp *rdp;
	struct lio *lio;
	int ring_doorbell;

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

	ih = (struct octeon_instr_ih *)&sc->cmd.ih;
	rdp = (struct octeon_instr_rdp *)&sc->cmd.rdp;

	ring_doorbell = !xmit_more;
	retval = octeon_send_command(oct, sc->iq_no, ring_doorbell, &sc->cmd,
				     sc, ih->dlengsz, ndata->reqtype);

	if (retval) {
		dev_err(&oct->pci_dev->dev, "timestamp data packet failed status: %x\n",
			retval);
		octeon_free_soft_command(oct, sc);
	} else {
		netif_info(lio, tx_queued, lio->netdev, "Queued timestamp packet\n");
	}

	return retval;
}

static inline int is_ipv4(struct sk_buff *skb)
{
	return (skb->protocol == htons(ETH_P_IP)) &&
	       (ip_hdr(skb)->version == 4);
}

static inline int is_vlan(struct sk_buff *skb)
{
	return skb->protocol == htons(ETH_P_8021Q);
}

static inline int is_ip_fragmented(struct sk_buff *skb)
{
	/* The Don't fragment and Reserved flag fields are ignored.
	 * IP is fragmented if
	 * -  the More fragments bit is set (indicating this IP is a fragment
	 * with more to follow; the current offset could be 0 ).
	 * -  ths offset field is non-zero.
	 */
	return (ip_hdr(skb)->frag_off & htons(IP_MF | IP_OFFSET)) ? 1 : 0;
}

static inline int is_ipv6(struct sk_buff *skb)
{
	return (skb->protocol == htons(ETH_P_IPV6)) &&
	       (ipv6_hdr(skb)->version == 6);
}

static inline int is_with_extn_hdr(struct sk_buff *skb)
{
	return (ipv6_hdr(skb)->nexthdr != IPPROTO_TCP) &&
	       (ipv6_hdr(skb)->nexthdr != IPPROTO_UDP);
}

static inline int is_tcpudp(struct sk_buff *skb)
{
	return (ip_hdr(skb)->protocol == IPPROTO_TCP) ||
	       (ip_hdr(skb)->protocol == IPPROTO_UDP);
}

static inline u32 get_ipv4_5tuple_tag(struct sk_buff *skb)
{
	u32 tag;
	struct iphdr *iphdr = ip_hdr(skb);

	tag = crc32(0, &iphdr->protocol, 1);
	tag = crc32(tag, (u8 *)&iphdr->saddr, 8);
	tag = crc32(tag, skb_transport_header(skb), 4);
	return tag;
}

static inline u32 get_ipv6_5tuple_tag(struct sk_buff *skb)
{
	u32 tag;
	struct ipv6hdr *ipv6hdr = ipv6_hdr(skb);

	tag = crc32(0, &ipv6hdr->nexthdr, 1);
	tag = crc32(tag, (u8 *)&ipv6hdr->saddr, 32);
	tag = crc32(tag, skb_transport_header(skb), 4);
	return tag;
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
	int cpu = 0, status = 0;
	int q_idx = 0, iq_no = 0;
	int xmit_more;
	u32 tag = 0;

	lio = GET_LIO(netdev);
	oct = lio->oct_dev;

	if (netif_is_multiqueue(netdev)) {
		cpu = skb->queue_mapping;
		q_idx = (cpu & (lio->linfo.num_txpciq - 1));
		iq_no = lio->linfo.txpciq[q_idx];
	} else {
		iq_no = lio->txq;
	}

	stats = &oct->instr_queue[iq_no]->stats;

	/* Check for all conditions in which the current packet cannot be
	 * transmitted.
	 */
	if (!(atomic_read(&lio->ifstate) & LIO_IFSTATE_RUNNING) ||
	    (!lio->linfo.link.s.status) ||
	    (skb->len <= 0)) {
		netif_info(lio, tx_err, lio->netdev,
			   "Transmit failed link_status : %d\n",
			   lio->linfo.link.s.status);
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
				   ndata.q_no);
			return NETDEV_TX_BUSY;
		}
	}
	/* pr_info(" XMIT - valid Qs: %d, 1st Q no: %d, cpu:  %d, q_no:%d\n",
	 *	lio->linfo.num_txpciq, lio->txq, cpu, ndata.q_no );
	 */

	ndata.datasize = skb->len;

	cmdsetup.u64 = 0;
	cmdsetup.s.ifidx = lio->linfo.ifidx;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (is_ipv4(skb) && !is_ip_fragmented(skb) && is_tcpudp(skb)) {
			tag = get_ipv4_5tuple_tag(skb);

			cmdsetup.s.cksum_offset = sizeof(struct ethhdr) + 1;

			if (ip_hdr(skb)->ihl > 5)
				cmdsetup.s.ipv4opts_ipv6exthdr =
						OCT_PKT_PARAM_IPV4OPTS;

		} else if (is_ipv6(skb)) {
			tag = get_ipv6_5tuple_tag(skb);

			cmdsetup.s.cksum_offset = sizeof(struct ethhdr) + 1;

			if (is_with_extn_hdr(skb))
				cmdsetup.s.ipv4opts_ipv6exthdr =
						OCT_PKT_PARAM_IPV6EXTHDR;

		} else if (is_vlan(skb)) {
			if (vlan_eth_hdr(skb)->h_vlan_encapsulated_proto
				== htons(ETH_P_IP) &&
				!is_ip_fragmented(skb) && is_tcpudp(skb)) {
				tag = get_ipv4_5tuple_tag(skb);

				cmdsetup.s.cksum_offset =
					sizeof(struct vlan_ethhdr) + 1;

				if (ip_hdr(skb)->ihl > 5)
					cmdsetup.s.ipv4opts_ipv6exthdr =
						OCT_PKT_PARAM_IPV4OPTS;

			} else if (vlan_eth_hdr(skb)->h_vlan_encapsulated_proto
				== htons(ETH_P_IPV6)) {
				tag = get_ipv6_5tuple_tag(skb);

				cmdsetup.s.cksum_offset =
					sizeof(struct vlan_ethhdr) + 1;

				if (is_with_extn_hdr(skb))
					cmdsetup.s.ipv4opts_ipv6exthdr =
						OCT_PKT_PARAM_IPV6EXTHDR;
			}
		}
	}
	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		cmdsetup.s.timestamp = 1;
	}

	if (skb_shinfo(skb)->nr_frags == 0) {
		cmdsetup.s.u.datasize = skb->len;
		octnet_prepare_pci_cmd(&ndata.cmd, &cmdsetup, tag);
		/* Offload checksum calculation for TCP/UDP packets */
		ndata.cmd.dptr = dma_map_single(&oct->pci_dev->dev,
						skb->data,
						skb->len,
						DMA_TO_DEVICE);
		if (dma_mapping_error(&oct->pci_dev->dev, ndata.cmd.dptr)) {
			dev_err(&oct->pci_dev->dev, "%s DMA mapping error 1\n",
				__func__);
			return NETDEV_TX_BUSY;
		}

		finfo->dptr = ndata.cmd.dptr;

		ndata.reqtype = REQTYPE_NORESP_NET;

	} else {
		int i, frags;
		struct skb_frag_struct *frag;
		struct octnic_gather *g;

		spin_lock(&lio->lock);
		g = (struct octnic_gather *)list_delete_head(&lio->glist);
		spin_unlock(&lio->lock);

		if (!g) {
			netif_info(lio, tx_err, lio->netdev,
				   "Transmit scatter gather: glist null!\n");
			goto lio_xmit_failed;
		}

		cmdsetup.s.gather = 1;
		cmdsetup.s.u.gatherptrs = (skb_shinfo(skb)->nr_frags + 1);
		octnet_prepare_pci_cmd(&ndata.cmd, &cmdsetup, tag);

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

			add_sg_size(&g->sg[(i >> 2)], frag->size, (i & 3));
			i++;
		}

		ndata.cmd.dptr = dma_map_single(&oct->pci_dev->dev,
						g->sg, g->sg_size,
						DMA_TO_DEVICE);
		if (dma_mapping_error(&oct->pci_dev->dev, ndata.cmd.dptr)) {
			dev_err(&oct->pci_dev->dev, "%s DMA mapping error 3\n",
				__func__);
			dma_unmap_single(&oct->pci_dev->dev, g->sg[0].ptr[0],
					 skb->len - skb->data_len,
					 DMA_TO_DEVICE);
			return NETDEV_TX_BUSY;
		}

		finfo->dptr = ndata.cmd.dptr;
		finfo->g = g;

		ndata.reqtype = REQTYPE_NORESP_NET_SG;
	}

	if (skb_shinfo(skb)->gso_size) {
		struct octeon_instr_irh *irh =
			(struct octeon_instr_irh *)&ndata.cmd.irh;
		union tx_info *tx_info = (union tx_info *)&ndata.cmd.ossp[0];

		irh->len = 1;   /* to indicate that ossp[0] contains tx_info */
		tx_info->s.gso_size = skb_shinfo(skb)->gso_size;
		tx_info->s.gso_segs = skb_shinfo(skb)->gso_segs;
	}

	xmit_more = skb->xmit_more;

	if (unlikely(cmdsetup.s.timestamp))
		status = send_nic_timestamp_pkt(oct, &ndata, finfo, xmit_more);
	else
		status = octnet_send_nic_data_pkt(oct, &ndata, xmit_more);
	if (status == IQ_SEND_FAILED)
		goto lio_xmit_failed;

	netif_info(lio, tx_queued, lio->netdev, "Transmit queued successfully\n");

	if (status == IQ_SEND_STOP)
		stop_q(lio->netdev, q_idx);

	netif_trans_update(netdev);

	stats->tx_done++;
	stats->tx_tot_bytes += skb->len;

	return NETDEV_TX_OK;

lio_xmit_failed:
	stats->tx_dropped++;
	netif_info(lio, tx_err, lio->netdev, "IQ%d Transmit dropped:%llu\n",
		   iq_no, stats->tx_dropped);
	dma_unmap_single(&oct->pci_dev->dev, ndata.cmd.dptr,
			 ndata.datasize, DMA_TO_DEVICE);
	recv_buffer_free(skb);
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

int liquidio_set_feature(struct net_device *netdev, int cmd)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	struct octnic_ctrl_params nparams;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = cmd;
	nctrl.ncmd.s.param1 = lio->linfo.ifidx;
	nctrl.ncmd.s.param2 = OCTNIC_LROIPV4 | OCTNIC_LROIPV6;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	nparams.resp_order = OCTEON_RESP_NORESPONSE;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl, nparams);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Feature change failed in core (ret: 0x%x)\n",
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
		liquidio_set_feature(netdev, OCTNET_CMD_LRO_ENABLE);
	else if (!(features & NETIF_F_LRO) &&
		 (lio->dev_capability & NETIF_F_LRO))
		liquidio_set_feature(netdev, OCTNET_CMD_LRO_DISABLE);

	return 0;
}

static struct net_device_ops lionetdevops = {
	.ndo_open		= liquidio_open,
	.ndo_stop		= liquidio_stop,
	.ndo_start_xmit		= liquidio_xmit,
	.ndo_get_stats		= liquidio_get_stats,
	.ndo_set_mac_address	= liquidio_set_mac,
	.ndo_set_rx_mode	= liquidio_set_mcast_list,
	.ndo_tx_timeout		= liquidio_tx_timeout,
	.ndo_change_mtu		= liquidio_change_mtu,
	.ndo_do_ioctl		= liquidio_ioctl,
	.ndo_fix_features	= liquidio_fix_features,
	.ndo_set_features	= liquidio_set_features,
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
	int ifidx = 0;
	union oct_link_status *ls;
	int i;

	if ((recv_pkt->buffer_size[0] != sizeof(*ls)) ||
	    (recv_pkt->rh.r_nic_info.ifidx > oct->ifcount)) {
		dev_err(&oct->pci_dev->dev, "Malformed NIC_INFO, len=%d, ifidx=%d\n",
			recv_pkt->buffer_size[0],
			recv_pkt->rh.r_nic_info.ifidx);
		goto nic_info_err;
	}

	ifidx = recv_pkt->rh.r_nic_info.ifidx;
	ls = (union oct_link_status *)get_rbd(recv_pkt->buffer_ptr[0]);

	octeon_swap_8B_data((u64 *)ls, (sizeof(union oct_link_status)) >> 3);

	update_link_status(oct->props[ifidx].netdev, ls);

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
	int retval, num_iqueues, num_oqueues, q_no;
	u64 q_mask;
	int num_cpus = num_online_cpus();
	union oct_nic_if_cfg if_cfg;
	unsigned int base_queue;
	unsigned int gmx_port_id;
	u32 resp_size, ctx_size;

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
		sc = (struct octeon_soft_command *)
			octeon_alloc_soft_command(octeon_dev, 0,
						  resp_size, ctx_size);
		resp = (struct liquidio_if_cfg_resp *)sc->virtrptr;
		ctx  = (struct liquidio_if_cfg_context *)sc->ctxptr;

		num_iqueues =
			CFG_GET_NUM_TXQS_NIC_IF(octeon_get_conf(octeon_dev), i);
		num_oqueues =
			CFG_GET_NUM_RXQS_NIC_IF(octeon_get_conf(octeon_dev), i);
		base_queue =
			CFG_GET_BASE_QUE_NIC_IF(octeon_get_conf(octeon_dev), i);
		gmx_port_id =
			CFG_GET_GMXID_NIC_IF(octeon_get_conf(octeon_dev), i);
		if (num_iqueues > num_cpus)
			num_iqueues = num_cpus;
		if (num_oqueues > num_cpus)
			num_oqueues = num_cpus;
		dev_dbg(&octeon_dev->pci_dev->dev,
			"requesting config for interface %d, iqs %d, oqs %d\n",
			i, num_iqueues, num_oqueues);
		ACCESS_ONCE(ctx->cond) = 0;
		ctx->octeon_id = lio_get_device_id(octeon_dev);
		init_waitqueue_head(&ctx->wc);

		if_cfg.u64 = 0;
		if_cfg.s.num_iqueues = num_iqueues;
		if_cfg.s.num_oqueues = num_oqueues;
		if_cfg.s.base_queue = base_queue;
		if_cfg.s.gmx_port_id = gmx_port_id;
		octeon_prepare_soft_command(octeon_dev, sc, OPCODE_NIC,
					    OPCODE_NIC_IF_CFG, i,
					    if_cfg.u64, 0);

		sc->callback = if_cfg_callback;
		sc->callback_arg = sc;
		sc->wait_time = 1000;

		retval = octeon_send_soft_command(octeon_dev, sc);
		if (retval) {
			dev_err(&octeon_dev->pci_dev->dev,
				"iq/oq config failed status: %x\n",
				retval);
			/* Soft instr is freed by driver in case of failure. */
			goto setup_nic_dev_fail;
		}

		/* Sleep on a wait queue till the cond flag indicates that the
		 * response arrived or timed-out.
		 */
		sleep_cond(&ctx->wc, &ctx->cond);
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

		props = &octeon_dev->props[i];
		props->netdev = netdev;

		if (num_iqueues > 1)
			lionetdevops.ndo_select_queue = select_q;

		/* Associate the routines that will handle different
		 * netdev tasks.
		 */
		netdev->netdev_ops = &lionetdevops;

		lio = GET_LIO(netdev);

		memset(lio, 0, sizeof(struct lio));

		lio->linfo.ifidx = resp->cfg_info.ifidx;
		lio->ifidx = resp->cfg_info.ifidx;

		lio->linfo.num_rxpciq = num_oqueues;
		lio->linfo.num_txpciq = num_iqueues;
		q_mask = resp->cfg_info.oqmask;
		/* q_mask is 0-based and already verified mask is nonzero */
		for (j = 0; j < num_oqueues; j++) {
			q_no = __ffs64(q_mask);
			q_mask &= (~(1UL << q_no));
			lio->linfo.rxpciq[j] = q_no;
		}
		q_mask = resp->cfg_info.iqmask;
		for (j = 0; j < num_iqueues; j++) {
			q_no = __ffs64(q_mask);
			q_mask &= (~(1UL << q_no));
			lio->linfo.txpciq[j] = q_no;
		}
		lio->linfo.hw_addr = resp->cfg_info.linfo.hw_addr;
		lio->linfo.gmxport = resp->cfg_info.linfo.gmxport;
		lio->linfo.link.u64 = resp->cfg_info.linfo.link.u64;

		lio->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

		lio->dev_capability = NETIF_F_HIGHDMA
				      | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM
				      | NETIF_F_SG | NETIF_F_RXCSUM
				      | NETIF_F_TSO | NETIF_F_TSO6
				      | NETIF_F_LRO;
		netif_set_gso_max_size(netdev, OCTNIC_GSO_MAX_SIZE);

		netdev->features = lio->dev_capability;
		netdev->vlan_features = lio->dev_capability;

		netdev->hw_features = lio->dev_capability;

		/* Point to the  properties for octeon device to which this
		 * interface belongs.
		 */
		lio->oct_dev = octeon_dev;
		lio->octprops = props;
		lio->netdev = netdev;
		spin_lock_init(&lio->lock);

		dev_dbg(&octeon_dev->pci_dev->dev,
			"if%d gmx: %d hw_addr: 0x%llx\n", i,
			lio->linfo.gmxport, CVM_CAST64(lio->linfo.hw_addr));

		/* 64-bit swap required on LE machines */
		octeon_swap_8B_data(&lio->linfo.hw_addr, 1);
		for (j = 0; j < 6; j++)
			mac[j] = *((u8 *)(((u8 *)&lio->linfo.hw_addr) + 2 + j));

		/* Copy MAC Address to OS network device structure */

		ether_addr_copy(netdev->dev_addr, mac);

		if (setup_io_queues(octeon_dev, netdev)) {
			dev_err(&octeon_dev->pci_dev->dev, "I/O queues creation failed\n");
			goto setup_nic_dev_fail;
		}

		ifstate_set(lio, LIO_IFSTATE_DROQ_OPS);

		/* By default all interfaces on a single Octeon uses the same
		 * tx and rx queues
		 */
		lio->txq = lio->linfo.txpciq[0];
		lio->rxq = lio->linfo.rxpciq[0];

		lio->tx_qsize = octeon_get_tx_qsize(octeon_dev, lio->txq);
		lio->rx_qsize = octeon_get_rx_qsize(octeon_dev, lio->rxq);

		if (setup_glist(lio)) {
			dev_err(&octeon_dev->pci_dev->dev,
				"Gather list allocation failed\n");
			goto setup_nic_dev_fail;
		}

		/* Register ethtool support */
		liquidio_set_ethtool_ops(netdev);

		liquidio_set_feature(netdev, OCTNET_CMD_LRO_ENABLE);

		if ((debug != -1) && (debug & NETIF_MSG_HW))
			liquidio_set_feature(netdev, OCTNET_CMD_VERBOSE_ENABLE);

		/* Register the network device with the OS */
		if (register_netdev(netdev)) {
			dev_err(&octeon_dev->pci_dev->dev, "Device registration failed\n");
			goto setup_nic_dev_fail;
		}

		dev_dbg(&octeon_dev->pci_dev->dev,
			"Setup NIC ifidx:%d mac:%02x%02x%02x%02x%02x%02x\n",
			i, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		netif_carrier_off(netdev);

		if (lio->linfo.link.s.status) {
			netif_carrier_on(netdev);
			start_txq(netdev);
		} else {
			netif_carrier_off(netdev);
		}

		ifstate_set(lio, LIO_IFSTATE_REGISTERED);

		dev_dbg(&octeon_dev->pci_dev->dev,
			"NIC ifidx:%d Setup successful\n", i);

		octeon_free_soft_command(octeon_dev, sc);
	}

	return 0;

setup_nic_dev_fail:

	octeon_free_soft_command(octeon_dev, sc);

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
	int retval = 0;
	int num_nic_ports = CFG_GET_NUM_NIC_PORTS(octeon_get_conf(oct));

	dev_dbg(&oct->pci_dev->dev, "Initializing network interfaces\n");

	/* only default iq and oq were initialized
	 * initialize the rest as well
	 */
	/* run port_config command for each port */
	oct->ifcount = num_nic_ports;

	memset(oct->props, 0,
	       sizeof(struct octdev_props) * num_nic_ports);

	retval = setup_nic_devices(oct);
	if (retval) {
		dev_err(&oct->pci_dev->dev, "Setup NIC devices failed\n");
		goto octnet_init_failure;
	}

	liquidio_ptp_init(oct);

	/* Initialize interrupt moderation params */
	intrmod_cfg = &((struct octeon_device *)oct)->intrmod;
	intrmod_cfg->intrmod_enable = 1;
	intrmod_cfg->intrmod_check_intrvl = LIO_INTRMOD_CHECK_INTERVAL;
	intrmod_cfg->intrmod_maxpkt_ratethr = LIO_INTRMOD_MAXPKT_RATETHR;
	intrmod_cfg->intrmod_minpkt_ratethr = LIO_INTRMOD_MINPKT_RATETHR;
	intrmod_cfg->intrmod_maxcnt_trigger = LIO_INTRMOD_MAXCNT_TRIGGER;
	intrmod_cfg->intrmod_maxtmr_trigger = LIO_INTRMOD_MAXTMR_TRIGGER;
	intrmod_cfg->intrmod_mintmr_trigger = LIO_INTRMOD_MINTMR_TRIGGER;
	intrmod_cfg->intrmod_mincnt_trigger = LIO_INTRMOD_MINCNT_TRIGGER;

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

	/* Do a soft reset of the Octeon device. */
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

	INIT_DELAYED_WORK(&octeon_dev->nic_poll_work.work, nic_starter);
	octeon_dev->nic_poll_work.ctxptr = (void *)octeon_dev;
	schedule_delayed_work(&octeon_dev->nic_poll_work.work,
			      LIQUIDIO_STARTER_POLL_INTERVAL_MS);

	atomic_set(&octeon_dev->status, OCT_DEV_DISPATCH_INIT_DONE);

	octeon_set_io_queues_off(octeon_dev);

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

	/* Initialize soft command buffer pool
	 */
	if (octeon_setup_sc_buffer_pool(octeon_dev)) {
		dev_err(&octeon_dev->pci_dev->dev, "sc buffer pool allocation failed\n");
		return 1;
	}
	atomic_set(&octeon_dev->status, OCT_DEV_SC_BUFF_POOL_INIT_DONE);

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
	}

	atomic_set(&octeon_dev->status, OCT_DEV_DROQ_INIT_DONE);

	/* The input and output queue registers were setup earlier (the queues
	 * were not enabled). Any additional registers that need to be
	 * programmed should be done now.
	 */
	ret = octeon_dev->fn_list.setup_device_regs(octeon_dev);
	if (ret) {
		dev_err(&octeon_dev->pci_dev->dev,
			"Failed to configure device registers\n");
		return ret;
	}

	/* Initialize the tasklet that handles output queue packet processing.*/
	dev_dbg(&octeon_dev->pci_dev->dev, "Initializing droq tasklet\n");
	tasklet_init(&oct_priv->droq_tasklet, octeon_droq_bh,
		     (unsigned long)octeon_dev);

	/* Setup the interrupt handler and record the INT SUM register address
	 */
	octeon_setup_interrupt(octeon_dev);

	/* Enable Octeon device interrupts */
	octeon_dev->fn_list.enable_interrupt(octeon_dev->chip);

	/* Enable the input and output queues for this Octeon device */
	octeon_dev->fn_list.enable_io_queues(octeon_dev);

	atomic_set(&octeon_dev->status, OCT_DEV_IO_QUEUES_DONE);

	dev_dbg(&octeon_dev->pci_dev->dev, "Waiting for DDR initialization...\n");

	if (ddr_timeout == 0) {
		dev_info(&octeon_dev->pci_dev->dev,
			 "WAITING. Set ddr_timeout to non-zero value to proceed with initialization.\n");
	}

	schedule_timeout_uninterruptible(HZ * LIO_RESET_SECS);

	/* Wait for the octeon to initialize DDR after the soft-reset. */
	ret = octeon_wait_for_ddr_init(octeon_dev, &ddr_timeout);
	if (ret) {
		dev_err(&octeon_dev->pci_dev->dev,
			"DDR not initialized. Please confirm that board is configured to boot from Flash, ret: %d\n",
			ret);
		return 1;
	}

	if (octeon_wait_for_bootloader(octeon_dev, 1000) != 0) {
		dev_err(&octeon_dev->pci_dev->dev, "Board not responding\n");
		return 1;
	}

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
