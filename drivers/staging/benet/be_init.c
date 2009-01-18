/*
 * Copyright (C) 2005 - 2008 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */
#include <linux/etherdevice.h>
#include "benet.h"

#define  DRVR_VERSION  "1.0.728"

static const struct pci_device_id be_device_id_table[] = {
	{PCI_DEVICE(0x19a2, 0x0201)},
	{0}
};

MODULE_DEVICE_TABLE(pci, be_device_id_table);

MODULE_VERSION(DRVR_VERSION);

#define DRV_DESCRIPTION "ServerEngines BladeEngine Network Driver Version "

MODULE_DESCRIPTION(DRV_DESCRIPTION DRVR_VERSION);
MODULE_AUTHOR("ServerEngines");
MODULE_LICENSE("GPL");

static unsigned int msix = 1;
module_param(msix, uint, S_IRUGO);
MODULE_PARM_DESC(msix, "Use MSI-x interrupts");

static unsigned int rxbuf_size = 2048;	/* Default RX frag size */
module_param(rxbuf_size, uint, S_IRUGO);
MODULE_PARM_DESC(rxbuf_size, "Size of buffers to hold Rx data");

const char be_drvr_ver[] = DRVR_VERSION;
char be_fw_ver[32];		/* F/W version filled in by be_probe */
char be_driver_name[] = "benet";

/*
 * Number of entries in each queue.
 */
#define EVENT_Q_LEN		1024
#define ETH_TXQ_LEN		2048
#define ETH_TXCQ_LEN		1024
#define ETH_RXQ_LEN		1024	/* Does not support any other value */
#define ETH_UC_RXCQ_LEN		1024
#define ETH_BC_RXCQ_LEN		256
#define MCC_Q_LEN               64	/* total size not to exceed 8 pages */
#define MCC_CQ_LEN              256

/* Bit mask describing events of interest to be traced */
unsigned int trace_level;

static int
init_pci_be_function(struct be_adapter *adapter, struct pci_dev *pdev)
{
	u64 pa;

	/* CSR */
	pa = pci_resource_start(pdev, 2);
	adapter->csr_va = ioremap_nocache(pa, pci_resource_len(pdev, 2));
	if (adapter->csr_va == NULL)
		return -ENOMEM;

	/* Door Bell */
	pa = pci_resource_start(pdev, 4);
	adapter->db_va = ioremap_nocache(pa, (128 * 1024));
	if (adapter->db_va == NULL) {
		iounmap(adapter->csr_va);
		return -ENOMEM;
	}

	/* PCI */
	pa = pci_resource_start(pdev, 1);
	adapter->pci_va = ioremap_nocache(pa, pci_resource_len(pdev, 1));
	if (adapter->pci_va == NULL) {
		iounmap(adapter->csr_va);
		iounmap(adapter->db_va);
		return -ENOMEM;
	}
	return 0;
}

/*
   This function enables the interrupt corresponding to the Event
   queue ID for the given NetObject
*/
void be_enable_eq_intr(struct be_net_object *pnob)
{
	struct CQ_DB_AMAP cqdb;
	cqdb.dw[0] = 0;
	AMAP_SET_BITS_PTR(CQ_DB, event, &cqdb, 1);
	AMAP_SET_BITS_PTR(CQ_DB, rearm, &cqdb, 1);
	AMAP_SET_BITS_PTR(CQ_DB, num_popped, &cqdb, 0);
	AMAP_SET_BITS_PTR(CQ_DB, qid, &cqdb, pnob->event_q_id);
	PD_WRITE(&pnob->fn_obj, cq_db, cqdb.dw[0]);
}

/*
   This function disables the interrupt corresponding to the Event
   queue ID for the given NetObject
*/
void be_disable_eq_intr(struct be_net_object *pnob)
{
	struct CQ_DB_AMAP cqdb;
	cqdb.dw[0] = 0;
	AMAP_SET_BITS_PTR(CQ_DB, event, &cqdb, 1);
	AMAP_SET_BITS_PTR(CQ_DB, rearm, &cqdb, 0);
	AMAP_SET_BITS_PTR(CQ_DB, num_popped, &cqdb, 0);
	AMAP_SET_BITS_PTR(CQ_DB, qid, &cqdb, pnob->event_q_id);
	PD_WRITE(&pnob->fn_obj, cq_db, cqdb.dw[0]);
}

/*
    This function enables the interrupt from the  network function
    of the BladeEngine. Use the function be_disable_eq_intr()
    to enable the interrupt from the event queue of only one specific
    NetObject
*/
void be_enable_intr(struct be_net_object *pnob)
{
	struct PCICFG_HOST_TIMER_INT_CTRL_CSR_AMAP ctrl;
	u32 host_intr;

	ctrl.dw[0] = PCICFG1_READ(&pnob->fn_obj, host_timer_int_ctrl);
	host_intr = AMAP_GET_BITS_PTR(PCICFG_HOST_TIMER_INT_CTRL_CSR,
							hostintr, ctrl.dw);
	if (!host_intr) {
		AMAP_SET_BITS_PTR(PCICFG_HOST_TIMER_INT_CTRL_CSR,
			hostintr, ctrl.dw, 1);
		PCICFG1_WRITE(&pnob->fn_obj, host_timer_int_ctrl,
			ctrl.dw[0]);
	}
}

/*
   This function disables the interrupt from the network function of
   the BladeEngine.  Use the function be_disable_eq_intr() to
   disable the interrupt from the event queue of only one specific NetObject
*/
void be_disable_intr(struct be_net_object *pnob)
{

	struct PCICFG_HOST_TIMER_INT_CTRL_CSR_AMAP ctrl;
	u32 host_intr;
	ctrl.dw[0] = PCICFG1_READ(&pnob->fn_obj, host_timer_int_ctrl);
	host_intr = AMAP_GET_BITS_PTR(PCICFG_HOST_TIMER_INT_CTRL_CSR,
							hostintr, ctrl.dw);
	if (host_intr) {
		AMAP_SET_BITS_PTR(PCICFG_HOST_TIMER_INT_CTRL_CSR, hostintr,
			ctrl.dw, 0);
		PCICFG1_WRITE(&pnob->fn_obj, host_timer_int_ctrl,
			ctrl.dw[0]);
	}
}

static int be_enable_msix(struct be_adapter *adapter)
{
	int i, ret;

	if (!msix)
		return -1;

	for (i = 0; i < BE_MAX_REQ_MSIX_VECTORS; i++)
		adapter->msix_entries[i].entry = i;

	ret = pci_enable_msix(adapter->pdev, adapter->msix_entries,
		BE_MAX_REQ_MSIX_VECTORS);

	if (ret == 0)
		adapter->msix_enabled = 1;
	return ret;
}

static int be_register_isr(struct be_adapter *adapter,
		struct be_net_object *pnob)
{
	struct net_device *netdev = pnob->netdev;
	int intx = 0, r;

	netdev->irq = adapter->pdev->irq;
	r = be_enable_msix(adapter);

	if (r == 0) {
		r = request_irq(adapter->msix_entries[0].vector,
				be_int, IRQF_SHARED, netdev->name, netdev);
		if (r) {
			printk(KERN_WARNING
				"MSIX Request IRQ failed - Errno %d\n", r);
			intx = 1;
			pci_disable_msix(adapter->pdev);
			adapter->msix_enabled = 0;
		}
	} else {
		intx = 1;
	}

	if (intx) {
		r = request_irq(netdev->irq, be_int, IRQF_SHARED,
				netdev->name, netdev);
		if (r) {
			printk(KERN_WARNING
				"INTx Request IRQ failed - Errno %d\n", r);
			return -1;
		}
	}
	adapter->isr_registered = 1;
	return 0;
}

static void be_unregister_isr(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdevp;
	if (adapter->isr_registered) {
		if (adapter->msix_enabled) {
			free_irq(adapter->msix_entries[0].vector, netdev);
			pci_disable_msix(adapter->pdev);
			adapter->msix_enabled = 0;
		} else {
			free_irq(netdev->irq, netdev);
		}
		adapter->isr_registered = 0;
	}
}

/*
    This function processes the Flush Completions that are issued by the
    ARM F/W, when a Recv Ring is destroyed.  A flush completion is
    identified when a Rx COmpl descriptor has the tcpcksum and udpcksum
    set and the pktsize is 32.  These completions are received on the
    Rx Completion Queue.
*/
static u32 be_process_rx_flush_cmpl(struct be_net_object *pnob)
{
	struct ETH_RX_COMPL_AMAP *rxcp;
	unsigned int i = 0;
	while ((rxcp = be_get_rx_cmpl(pnob)) != NULL) {
		be_notify_cmpl(pnob, 1, pnob->rx_cq_id, 1);
		i++;
	}
	return i;
}

static void be_tx_q_clean(struct be_net_object *pnob)
{
	while (atomic_read(&pnob->tx_q_used))
		process_one_tx_compl(pnob, tx_compl_lastwrb_idx_get(pnob));
}

static void be_rx_q_clean(struct be_net_object *pnob)
{
	if (pnob->rx_ctxt) {
		int i;
		struct be_rx_page_info *rx_page_info;
		for (i = 0; i < pnob->rx_q_len; i++) {
			rx_page_info = &(pnob->rx_page_info[i]);
			if (!pnob->rx_pg_shared || rx_page_info->page_offset) {
				pci_unmap_page(pnob->adapter->pdev,
				       pci_unmap_addr(rx_page_info, bus),
					       pnob->rx_buf_size,
					       PCI_DMA_FROMDEVICE);
			}
			if (rx_page_info->page)
				put_page(rx_page_info->page);
			memset(rx_page_info, 0, sizeof(struct be_rx_page_info));
		}
		pnob->rx_pg_info_hd = 0;
	}
}

static void be_destroy_netobj(struct be_net_object *pnob)
{
	int status;

	if (pnob->tx_q_created) {
		status = be_eth_sq_destroy(&pnob->tx_q_obj);
		pnob->tx_q_created = 0;
	}

	if (pnob->rx_q_created) {
		status = be_eth_rq_destroy(&pnob->rx_q_obj);
		if (status != 0) {
			status = be_eth_rq_destroy_options(&pnob->rx_q_obj, 0,
						      NULL, NULL);
			BUG_ON(status);
		}
		pnob->rx_q_created = 0;
	}

	be_process_rx_flush_cmpl(pnob);

	if (pnob->tx_cq_created) {
		status = be_cq_destroy(&pnob->tx_cq_obj);
		pnob->tx_cq_created = 0;
	}

	if (pnob->rx_cq_created) {
		status = be_cq_destroy(&pnob->rx_cq_obj);
		pnob->rx_cq_created = 0;
	}

	if (pnob->mcc_q_created) {
		status = be_mcc_ring_destroy(&pnob->mcc_q_obj);
		pnob->mcc_q_created = 0;
	}
	if (pnob->mcc_cq_created) {
		status = be_cq_destroy(&pnob->mcc_cq_obj);
		pnob->mcc_cq_created = 0;
	}

	if (pnob->event_q_created) {
		status = be_eq_destroy(&pnob->event_q_obj);
		pnob->event_q_created = 0;
	}
	be_function_cleanup(&pnob->fn_obj);
}

/*
 * free all resources associated with a pnob
 * Called at the time of module cleanup as well a any error during
 * module init.  Some resources may be partially allocated in a NetObj.
 */
static void netobject_cleanup(struct be_adapter *adapter,
			struct be_net_object *pnob)
{
	struct net_device *netdev = adapter->netdevp;

	if (netif_running(netdev)) {
		netif_stop_queue(netdev);
		be_wait_nic_tx_cmplx_cmpl(pnob);
		be_disable_eq_intr(pnob);
	}

	be_unregister_isr(adapter);

	if (adapter->tasklet_started) {
		tasklet_kill(&(adapter->sts_handler));
		adapter->tasklet_started = 0;
	}
	if (pnob->fn_obj_created)
		be_disable_intr(pnob);

	if (adapter->dev_state != BE_DEV_STATE_NONE)
		unregister_netdev(netdev);

	if (pnob->fn_obj_created)
		be_destroy_netobj(pnob);

	adapter->net_obj = NULL;
	adapter->netdevp = NULL;

	be_rx_q_clean(pnob);
	if (pnob->rx_ctxt) {
		kfree(pnob->rx_page_info);
		kfree(pnob->rx_ctxt);
	}

	be_tx_q_clean(pnob);
	kfree(pnob->tx_ctxt);

	if (pnob->mcc_q)
		pci_free_consistent(adapter->pdev, pnob->mcc_q_size,
			pnob->mcc_q, pnob->mcc_q_bus);

	if (pnob->mcc_wrb_ctxt)
		free_pages((unsigned long)pnob->mcc_wrb_ctxt,
			   get_order(pnob->mcc_wrb_ctxt_size));

	if (pnob->mcc_cq)
		pci_free_consistent(adapter->pdev, pnob->mcc_cq_size,
			pnob->mcc_cq, pnob->mcc_cq_bus);

	if (pnob->event_q)
		pci_free_consistent(adapter->pdev, pnob->event_q_size,
			pnob->event_q, pnob->event_q_bus);

	if (pnob->tx_cq)
		pci_free_consistent(adapter->pdev, pnob->tx_cq_size,
			pnob->tx_cq, pnob->tx_cq_bus);

	if (pnob->tx_q)
		pci_free_consistent(adapter->pdev, pnob->tx_q_size,
			pnob->tx_q, pnob->tx_q_bus);

	if (pnob->rx_q)
		pci_free_consistent(adapter->pdev, pnob->rx_q_size,
			pnob->rx_q, pnob->rx_q_bus);

	if (pnob->rx_cq)
		pci_free_consistent(adapter->pdev, pnob->rx_cq_size,
			pnob->rx_cq, pnob->rx_cq_bus);


	if (pnob->mb_ptr)
		pci_free_consistent(adapter->pdev, pnob->mb_size, pnob->mb_ptr,
			pnob->mb_bus);

	free_netdev(netdev);
}


static int be_nob_ring_alloc(struct be_adapter *adapter,
	struct be_net_object *pnob)
{
	u32 size;

	/* Mail box rd; mailbox pointer needs to be 16 byte aligned */
	pnob->mb_size = sizeof(struct MCC_MAILBOX_AMAP) + 16;
	pnob->mb_ptr = pci_alloc_consistent(adapter->pdev, pnob->mb_size,
				&pnob->mb_bus);
	if (!pnob->mb_bus)
		return -1;
	memset(pnob->mb_ptr, 0, pnob->mb_size);
	pnob->mb_rd.va = PTR_ALIGN(pnob->mb_ptr, 16);
	pnob->mb_rd.pa = PTR_ALIGN(pnob->mb_bus, 16);
	pnob->mb_rd.length = sizeof(struct MCC_MAILBOX_AMAP);
	/*
	 * Event queue
	 */
	pnob->event_q_len = EVENT_Q_LEN;
	pnob->event_q_size = pnob->event_q_len * sizeof(struct EQ_ENTRY_AMAP);
	pnob->event_q = pci_alloc_consistent(adapter->pdev, pnob->event_q_size,
				&pnob->event_q_bus);
	if (!pnob->event_q_bus)
		return -1;
	memset(pnob->event_q, 0, pnob->event_q_size);
	/*
	 * Eth TX queue
	 */
	pnob->tx_q_len = ETH_TXQ_LEN;
	pnob->tx_q_port = 0;
	pnob->tx_q_size =  pnob->tx_q_len * sizeof(struct ETH_WRB_AMAP);
	pnob->tx_q = pci_alloc_consistent(adapter->pdev, pnob->tx_q_size,
				&pnob->tx_q_bus);
	if (!pnob->tx_q_bus)
		return -1;
	memset(pnob->tx_q, 0, pnob->tx_q_size);
	/*
	 * Eth TX Compl queue
	 */
	pnob->txcq_len = ETH_TXCQ_LEN;
	pnob->tx_cq_size = pnob->txcq_len * sizeof(struct ETH_TX_COMPL_AMAP);
	pnob->tx_cq = pci_alloc_consistent(adapter->pdev, pnob->tx_cq_size,
				&pnob->tx_cq_bus);
	if (!pnob->tx_cq_bus)
		return -1;
	memset(pnob->tx_cq, 0, pnob->tx_cq_size);
	/*
	 * Eth RX queue
	 */
	pnob->rx_q_len = ETH_RXQ_LEN;
	pnob->rx_q_size =  pnob->rx_q_len * sizeof(struct ETH_RX_D_AMAP);
	pnob->rx_q = pci_alloc_consistent(adapter->pdev, pnob->rx_q_size,
				&pnob->rx_q_bus);
	if (!pnob->rx_q_bus)
		return -1;
	memset(pnob->rx_q, 0, pnob->rx_q_size);
	/*
	 * Eth Unicast RX Compl queue
	 */
	pnob->rx_cq_len = ETH_UC_RXCQ_LEN;
	pnob->rx_cq_size =  pnob->rx_cq_len *
			sizeof(struct ETH_RX_COMPL_AMAP);
	pnob->rx_cq = pci_alloc_consistent(adapter->pdev, pnob->rx_cq_size,
				&pnob->rx_cq_bus);
	if (!pnob->rx_cq_bus)
		return -1;
	memset(pnob->rx_cq, 0, pnob->rx_cq_size);

	/* TX resources */
	size = pnob->tx_q_len * sizeof(void **);
	pnob->tx_ctxt = kzalloc(size, GFP_KERNEL);
	if (pnob->tx_ctxt == NULL)
		return -1;

	/* RX resources */
	size = pnob->rx_q_len * sizeof(void *);
	pnob->rx_ctxt = kzalloc(size, GFP_KERNEL);
	if (pnob->rx_ctxt == NULL)
		return -1;

	size = (pnob->rx_q_len * sizeof(struct be_rx_page_info));
	pnob->rx_page_info = kzalloc(size, GFP_KERNEL);
	if (pnob->rx_page_info == NULL)
		return -1;

	adapter->eth_statsp = kzalloc(sizeof(struct FWCMD_ETH_GET_STATISTICS),
				GFP_KERNEL);
	if (adapter->eth_statsp == NULL)
		return -1;
	pnob->rx_buf_size = rxbuf_size;
	return 0;
}

/*
    This function initializes the be_net_object for subsequent
    network operations.

    Before calling this function, the driver  must have allocated
    space for the NetObject structure, initialized the structure,
    allocated DMAable memory for all the network queues that form
    part of the NetObject and populated the start address (virtual)
    and number of entries allocated for each queue in the NetObject structure.

    The driver must also have allocated memory to hold the
    mailbox structure (MCC_MAILBOX) and post the physical address,
    virtual addresses and the size of the mailbox memory in the
    NetObj.mb_rd.  This structure is used by BECLIB for
    initial communication with the embedded MCC processor. BECLIB
    uses the mailbox until MCC rings are created for  more  efficient
    communication with the MCC processor.

    If the driver wants to create multiple network interface for more
    than one protection domain, it can call be_create_netobj()
    multiple times  once for each protection domain.  A Maximum of
    32 protection domains are supported.

*/
static int
be_create_netobj(struct be_net_object *pnob, u8 __iomem *csr_va,
	u8 __iomem *db_va, u8 __iomem *pci_va)
{
	int status = 0;
	bool  eventable = false, tx_no_delay = false, rx_no_delay = false;
	struct be_eq_object *eq_objectp = NULL;
	struct be_function_object *pfob = &pnob->fn_obj;
	struct ring_desc rd;
	u32 set_rxbuf_size;
	u32 tx_cmpl_wm = CEV_WMARK_96;	/* 0xffffffff to disable */
	u32 rx_cmpl_wm = CEV_WMARK_160;	/* 0xffffffff to disable */
	u32 eq_delay = 0; /* delay in 8usec units. 0xffffffff to disable */

	memset(&rd, 0, sizeof(struct ring_desc));

	status = be_function_object_create(csr_va, db_va, pci_va,
			BE_FUNCTION_TYPE_NETWORK, &pnob->mb_rd, pfob);
	if (status != BE_SUCCESS)
		return status;
	pnob->fn_obj_created = true;

	if (tx_cmpl_wm == 0xffffffff)
		tx_no_delay = true;
	if (rx_cmpl_wm == 0xffffffff)
		rx_no_delay = true;
	/*
	 * now create the necessary rings
	 * Event Queue first.
	 */
	if (pnob->event_q_len) {
		rd.va = pnob->event_q;
		rd.pa = pnob->event_q_bus;
		rd.length = pnob->event_q_size;

		status = be_eq_create(pfob, &rd, 4, pnob->event_q_len,
				(u32) -1,	/* CEV_WMARK_* or -1 */
				eq_delay,	/* in 8us units, or -1 */
				&pnob->event_q_obj);
		if (status != BE_SUCCESS)
			goto error_ret;
		pnob->event_q_id = pnob->event_q_obj.eq_id;
		pnob->event_q_created = 1;
		eventable = true;
		eq_objectp = &pnob->event_q_obj;
	}
	/*
	 * Now Eth Tx Compl. queue.
	 */
	if (pnob->txcq_len) {
		rd.va = pnob->tx_cq;
		rd.pa = pnob->tx_cq_bus;
		rd.length = pnob->tx_cq_size;

		status = be_cq_create(pfob, &rd,
			pnob->txcq_len * sizeof(struct ETH_TX_COMPL_AMAP),
			false,	/* solicted events,  */
			tx_no_delay,	/* nodelay  */
			tx_cmpl_wm,	/* Watermark encodings */
			eq_objectp, &pnob->tx_cq_obj);
		if (status != BE_SUCCESS)
			goto error_ret;

		pnob->tx_cq_id = pnob->tx_cq_obj.cq_id;
		pnob->tx_cq_created = 1;
	}
	/*
	 * Eth Tx queue
	 */
	if (pnob->tx_q_len) {
		struct be_eth_sq_parameters ex_params = { 0 };
		u32 type;

		if (pnob->tx_q_port) {
			/* TXQ to be bound to a specific port */
			type = BE_ETH_TX_RING_TYPE_BOUND;
			ex_params.port = pnob->tx_q_port - 1;
		} else
			type = BE_ETH_TX_RING_TYPE_STANDARD;

		rd.va = pnob->tx_q;
		rd.pa = pnob->tx_q_bus;
		rd.length = pnob->tx_q_size;

		status = be_eth_sq_create_ex(pfob, &rd,
				pnob->tx_q_len * sizeof(struct ETH_WRB_AMAP),
				type, 2, &pnob->tx_cq_obj,
				&ex_params, &pnob->tx_q_obj);

		if (status != BE_SUCCESS)
			goto error_ret;

		pnob->tx_q_id = pnob->tx_q_obj.bid;
		pnob->tx_q_created = 1;
	}
	/*
	 * Now Eth Rx compl. queue.  Always needed.
	 */
	rd.va = pnob->rx_cq;
	rd.pa = pnob->rx_cq_bus;
	rd.length = pnob->rx_cq_size;

	status = be_cq_create(pfob, &rd,
			pnob->rx_cq_len * sizeof(struct ETH_RX_COMPL_AMAP),
			false,	/* solicted events,  */
			rx_no_delay,	/* nodelay  */
			rx_cmpl_wm,	/* Watermark encodings */
			eq_objectp, &pnob->rx_cq_obj);
	if (status != BE_SUCCESS)
		goto error_ret;

	pnob->rx_cq_id = pnob->rx_cq_obj.cq_id;
	pnob->rx_cq_created = 1;

	status = be_eth_rq_set_frag_size(pfob, pnob->rx_buf_size,
			(u32 *) &set_rxbuf_size);
	if (status != BE_SUCCESS) {
		be_eth_rq_get_frag_size(pfob, (u32 *) &pnob->rx_buf_size);
		if ((pnob->rx_buf_size != 2048) && (pnob->rx_buf_size != 4096)
		    && (pnob->rx_buf_size != 8192))
			goto error_ret;
	} else {
		if (pnob->rx_buf_size != set_rxbuf_size)
			pnob->rx_buf_size = set_rxbuf_size;
	}
	/*
	 * Eth RX queue. be_eth_rq_create() always assumes 2 pages size
	 */
	rd.va = pnob->rx_q;
	rd.pa = pnob->rx_q_bus;
	rd.length = pnob->rx_q_size;

	status = be_eth_rq_create(pfob, &rd, &pnob->rx_cq_obj,
			     &pnob->rx_cq_obj, &pnob->rx_q_obj);

	if (status != BE_SUCCESS)
		goto error_ret;

	pnob->rx_q_id = pnob->rx_q_obj.rid;
	pnob->rx_q_created = 1;

	return BE_SUCCESS;	/* All required queues created. */

error_ret:
	be_destroy_netobj(pnob);
	return status;
}

static int be_nob_ring_init(struct be_adapter *adapter,
				struct be_net_object *pnob)
{
	int status;

	pnob->event_q_tl = 0;

	pnob->tx_q_hd = 0;
	pnob->tx_q_tl = 0;

	pnob->tx_cq_tl = 0;

	pnob->rx_cq_tl = 0;

	memset(pnob->event_q, 0, pnob->event_q_size);
	memset(pnob->tx_cq, 0, pnob->tx_cq_size);
	memset(pnob->tx_ctxt, 0, pnob->tx_q_len * sizeof(void **));
	memset(pnob->rx_ctxt, 0, pnob->rx_q_len * sizeof(void *));
	pnob->rx_pg_info_hd = 0;
	pnob->rx_q_hd = 0;
	atomic_set(&pnob->rx_q_posted, 0);

	status = be_create_netobj(pnob, adapter->csr_va, adapter->db_va,
				adapter->pci_va);
	if (status != BE_SUCCESS)
		return -1;

	be_post_eth_rx_buffs(pnob);
	return 0;
}

/* This function handles async callback for link status */
static void
be_link_status_async_callback(void *context, u32 event_code, void *event)
{
	struct ASYNC_EVENT_LINK_STATE_AMAP *link_status = event;
	struct be_adapter *adapter = context;
	bool link_enable = false;
	struct be_net_object *pnob;
	struct ASYNC_EVENT_TRAILER_AMAP *async_trailer;
	struct net_device *netdev;
	u32 async_event_code, async_event_type, active_port;
	u32 port0_link_status, port1_link_status, port0_duplex, port1_duplex;
	u32 port0_speed, port1_speed;

	if (event_code != ASYNC_EVENT_CODE_LINK_STATE) {
		/* Not our event to handle */
		return;
	}
	async_trailer = (struct ASYNC_EVENT_TRAILER_AMAP *)
	    ((u8 *) event + sizeof(struct MCC_CQ_ENTRY_AMAP) -
	     sizeof(struct ASYNC_EVENT_TRAILER_AMAP));

	async_event_code = AMAP_GET_BITS_PTR(ASYNC_EVENT_TRAILER, event_code,
					     async_trailer);
	BUG_ON(async_event_code != ASYNC_EVENT_CODE_LINK_STATE);

	pnob = adapter->net_obj;
	netdev = pnob->netdev;

	/* Determine if this event is a switch VLD or a physical link event */
	async_event_type = AMAP_GET_BITS_PTR(ASYNC_EVENT_TRAILER, event_type,
					     async_trailer);
	active_port = AMAP_GET_BITS_PTR(ASYNC_EVENT_LINK_STATE,
					active_port, link_status);
	port0_link_status = AMAP_GET_BITS_PTR(ASYNC_EVENT_LINK_STATE,
					      port0_link_status, link_status);
	port1_link_status = AMAP_GET_BITS_PTR(ASYNC_EVENT_LINK_STATE,
					      port1_link_status, link_status);
	port0_duplex = AMAP_GET_BITS_PTR(ASYNC_EVENT_LINK_STATE,
					 port0_duplex, link_status);
	port1_duplex = AMAP_GET_BITS_PTR(ASYNC_EVENT_LINK_STATE,
					 port1_duplex, link_status);
	port0_speed = AMAP_GET_BITS_PTR(ASYNC_EVENT_LINK_STATE,
					port0_speed, link_status);
	port1_speed = AMAP_GET_BITS_PTR(ASYNC_EVENT_LINK_STATE,
					port1_speed, link_status);
	if (async_event_type == NTWK_LINK_TYPE_VIRTUAL) {
		adapter->be_stat.bes_link_change_virtual++;
		if (adapter->be_link_sts->active_port != active_port) {
			dev_notice(&netdev->dev,
			       "Active port changed due to VLD on switch\n");
		} else {
			dev_notice(&netdev->dev, "Link status update\n");
		}

	} else {
		adapter->be_stat.bes_link_change_physical++;
		if (adapter->be_link_sts->active_port != active_port) {
			dev_notice(&netdev->dev,
			       "Active port changed due to port link"
			       " status change\n");
		} else {
			dev_notice(&netdev->dev, "Link status update\n");
		}
	}

	memset(adapter->be_link_sts, 0, sizeof(adapter->be_link_sts));

	if ((port0_link_status == ASYNC_EVENT_LINK_UP) ||
	    (port1_link_status == ASYNC_EVENT_LINK_UP)) {
		if ((adapter->port0_link_sts == BE_PORT_LINK_DOWN) &&
		    (adapter->port1_link_sts == BE_PORT_LINK_DOWN)) {
			/* Earlier both the ports are down So link is up */
			link_enable = true;
		}

		if (port0_link_status == ASYNC_EVENT_LINK_UP) {
			adapter->port0_link_sts = BE_PORT_LINK_UP;
			adapter->be_link_sts->mac0_duplex = port0_duplex;
			adapter->be_link_sts->mac0_speed = port0_speed;
			if (active_port == NTWK_PORT_A)
				adapter->be_link_sts->active_port = 0;
		} else
			adapter->port0_link_sts = BE_PORT_LINK_DOWN;

		if (port1_link_status == ASYNC_EVENT_LINK_UP) {
			adapter->port1_link_sts = BE_PORT_LINK_UP;
			adapter->be_link_sts->mac1_duplex = port1_duplex;
			adapter->be_link_sts->mac1_speed = port1_speed;
			if (active_port == NTWK_PORT_B)
				adapter->be_link_sts->active_port = 1;
		} else
			adapter->port1_link_sts = BE_PORT_LINK_DOWN;

		printk(KERN_INFO "Link Properties for %s:\n", netdev->name);
		dev_info(&netdev->dev, "Link Properties:\n");
		be_print_link_info(adapter->be_link_sts);

		if (!link_enable)
			return;
		/*
		 * Both ports were down previously, but atleast one of
		 * them has come up if this netdevice's carrier is not up,
		 * then indicate to stack
		 */
		if (!netif_carrier_ok(netdev)) {
			netif_start_queue(netdev);
			netif_carrier_on(netdev);
		}
		return;
	}

	/* Now both the ports are down. Tell the stack about it */
	dev_info(&netdev->dev, "Both ports are down\n");
	adapter->port0_link_sts = BE_PORT_LINK_DOWN;
	adapter->port1_link_sts = BE_PORT_LINK_DOWN;
	if (netif_carrier_ok(netdev)) {
		netif_carrier_off(netdev);
		netif_stop_queue(netdev);
	}
	return;
}

static int be_mcc_create(struct be_adapter *adapter)
{
	struct be_net_object *pnob;

	pnob = adapter->net_obj;
	/*
	 * Create the MCC ring so that all further communication with
	 * MCC can go thru the ring. we do this at the end since
	 * we do not want to be dealing with interrupts until the
	 * initialization is complete.
	 */
	pnob->mcc_q_len = MCC_Q_LEN;
	pnob->mcc_q_size = pnob->mcc_q_len * sizeof(struct MCC_WRB_AMAP);
	pnob->mcc_q =  pci_alloc_consistent(adapter->pdev, pnob->mcc_q_size,
				&pnob->mcc_q_bus);
	if (!pnob->mcc_q_bus)
		return -1;
	/*
	 * space for MCC WRB context
	 */
	pnob->mcc_wrb_ctxtLen = MCC_Q_LEN;
	pnob->mcc_wrb_ctxt_size =  pnob->mcc_wrb_ctxtLen *
		sizeof(struct be_mcc_wrb_context);
	pnob->mcc_wrb_ctxt = (void *)__get_free_pages(GFP_KERNEL,
		get_order(pnob->mcc_wrb_ctxt_size));
	if (pnob->mcc_wrb_ctxt == NULL)
		return -1;
	/*
	 * Space for MCC compl. ring
	 */
	pnob->mcc_cq_len = MCC_CQ_LEN;
	pnob->mcc_cq_size = pnob->mcc_cq_len * sizeof(struct MCC_CQ_ENTRY_AMAP);
	pnob->mcc_cq = pci_alloc_consistent(adapter->pdev, pnob->mcc_cq_size,
				&pnob->mcc_cq_bus);
	if (!pnob->mcc_cq_bus)
		return -1;
	return 0;
}

/*
    This function creates the MCC request and completion ring required
    for communicating with the ARM processor.  The caller must have
    allocated required amount of memory for the MCC ring and MCC
    completion ring and posted the virtual address and number of
    entries in the corresponding members (mcc_q and mcc_cq) in the
    NetObject struture.

    When this call is completed, all further communication with
    ARM will switch from mailbox to this ring.

    pnob	- Pointer to the NetObject structure. This NetObject should
		  have been created using a previous call to be_create_netobj()
*/
int be_create_mcc_rings(struct be_net_object *pnob)
{
	int status = 0;
	struct ring_desc rd;
	struct be_function_object *pfob = &pnob->fn_obj;

	memset(&rd, 0, sizeof(struct ring_desc));
	if (pnob->mcc_cq_len) {
		rd.va = pnob->mcc_cq;
		rd.pa = pnob->mcc_cq_bus;
		rd.length = pnob->mcc_cq_size;

		status = be_cq_create(pfob, &rd,
			pnob->mcc_cq_len * sizeof(struct MCC_CQ_ENTRY_AMAP),
			false,	/* solicted events,  */
			true,	/* nodelay  */
			0,	/* 0 Watermark since Nodelay is true */
			&pnob->event_q_obj,
			&pnob->mcc_cq_obj);

		if (status != BE_SUCCESS)
			return status;

		pnob->mcc_cq_id = pnob->mcc_cq_obj.cq_id;
		pnob->mcc_cq_created = 1;
	}
	if (pnob->mcc_q_len) {
		rd.va = pnob->mcc_q;
		rd.pa = pnob->mcc_q_bus;
		rd.length = pnob->mcc_q_size;

		status = be_mcc_ring_create(pfob, &rd,
				pnob->mcc_q_len * sizeof(struct MCC_WRB_AMAP),
				pnob->mcc_wrb_ctxt, pnob->mcc_wrb_ctxtLen,
				&pnob->mcc_cq_obj, &pnob->mcc_q_obj);

		if (status != BE_SUCCESS)
			return status;

		pnob->mcc_q_created = 1;
	}
	return BE_SUCCESS;
}

static int be_mcc_init(struct be_adapter *adapter)
{
	u32 r;
	struct be_net_object *pnob;

	pnob = adapter->net_obj;
	memset(pnob->mcc_q, 0, pnob->mcc_q_size);
	pnob->mcc_q_hd = 0;

	memset(pnob->mcc_wrb_ctxt, 0, pnob->mcc_wrb_ctxt_size);

	memset(pnob->mcc_cq, 0, pnob->mcc_cq_size);
	pnob->mcc_cq_tl = 0;

	r = be_create_mcc_rings(adapter->net_obj);
	if (r != BE_SUCCESS)
		return -1;

	return 0;
}

static void be_remove(struct pci_dev *pdev)
{
	struct be_net_object *pnob;
	struct be_adapter *adapter;

	adapter = pci_get_drvdata(pdev);
	if (!adapter)
		return;

	pci_set_drvdata(pdev, NULL);
	pnob = (struct be_net_object *)adapter->net_obj;

	flush_scheduled_work();

	if (pnob) {
		/* Unregister async callback function for link status updates */
		if (pnob->mcc_q_created)
			be_mcc_add_async_event_callback(&pnob->mcc_q_obj,
								NULL, NULL);
		netobject_cleanup(adapter, pnob);
	}

	if (adapter->csr_va)
		iounmap(adapter->csr_va);
	if (adapter->db_va)
		iounmap(adapter->db_va);
	if (adapter->pci_va)
		iounmap(adapter->pci_va);

	pci_release_regions(adapter->pdev);
	pci_disable_device(adapter->pdev);

	kfree(adapter->be_link_sts);
	kfree(adapter->eth_statsp);

	if (adapter->timer_ctxt.get_stats_timer.function)
		del_timer_sync(&adapter->timer_ctxt.get_stats_timer);
	kfree(adapter);
}

/*
 * This function is called by the PCI sub-system when it finds a PCI
 * device with dev/vendor IDs that match with one of our devices.
 * All of the driver initialization is done in this function.
 */
static int be_probe(struct pci_dev *pdev, const struct pci_device_id *pdev_id)
{
	int status = 0;
	struct be_adapter *adapter;
	struct FWCMD_COMMON_GET_FW_VERSION_RESPONSE_PAYLOAD get_fwv;
	struct be_net_object *pnob;
	struct net_device *netdev;

	status = pci_enable_device(pdev);
	if (status)
		goto error;

	status = pci_request_regions(pdev, be_driver_name);
	if (status)
		goto error_pci_req;

	pci_set_master(pdev);
	adapter = kzalloc(sizeof(struct be_adapter), GFP_KERNEL);
	if (adapter == NULL) {
		status = -ENOMEM;
		goto error_adapter;
	}
	adapter->dev_state = BE_DEV_STATE_NONE;
	adapter->pdev = pdev;
	pci_set_drvdata(pdev, adapter);

	adapter->enable_aic = 1;
	adapter->max_eqd = MAX_EQD;
	adapter->min_eqd = 0;
	adapter->cur_eqd = 0;

	status = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
	if (!status) {
		adapter->dma_64bit_cap = true;
	} else {
		adapter->dma_64bit_cap = false;
		status = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (status != 0) {
			printk(KERN_ERR "Could not set PCI DMA Mask\n");
			goto cleanup;
		}
	}

	status = init_pci_be_function(adapter, pdev);
	if (status != 0) {
		printk(KERN_ERR "Failed to map PCI BARS\n");
		status = -ENOMEM;
		goto cleanup;
	}

	be_trace_set_level(DL_ALWAYS | DL_ERR);

	adapter->be_link_sts = kmalloc(sizeof(struct BE_LINK_STATUS),
					GFP_KERNEL);
	if (adapter->be_link_sts == NULL) {
		printk(KERN_ERR "Memory allocation for link status "
		       "buffer failed\n");
		goto cleanup;
	}
	spin_lock_init(&adapter->txq_lock);

	netdev = alloc_etherdev(sizeof(struct be_net_object));
	if (netdev == NULL) {
		status = -ENOMEM;
		goto cleanup;
	}
	pnob = netdev_priv(netdev);
	adapter->net_obj = pnob;
	adapter->netdevp = netdev;
	pnob->adapter = adapter;
	pnob->netdev = netdev;

	status = be_nob_ring_alloc(adapter, pnob);
	if (status != 0)
		goto cleanup;

	status = be_nob_ring_init(adapter, pnob);
	if (status != 0)
		goto cleanup;

	be_rxf_mac_address_read_write(&pnob->fn_obj, false, false, false,
		false, false, netdev->dev_addr, NULL, NULL);

	netdev->init = &benet_init;
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	SET_NETDEV_DEV(netdev, &(adapter->pdev->dev));

	netif_napi_add(netdev, &pnob->napi, be_poll, 64);

	/* if the rx_frag size if 2K, one page is shared as two RX frags */
	pnob->rx_pg_shared =
		(pnob->rx_buf_size <= PAGE_SIZE / 2) ? true : false;
	if (pnob->rx_buf_size != rxbuf_size) {
		printk(KERN_WARNING
		       "Could not set Rx buffer size to %d. Using %d\n",
				       rxbuf_size, pnob->rx_buf_size);
		rxbuf_size = pnob->rx_buf_size;
	}

	tasklet_init(&(adapter->sts_handler), be_process_intr,
		     (unsigned long)adapter);
	adapter->tasklet_started = 1;
	spin_lock_init(&(adapter->int_lock));

	status = be_register_isr(adapter, pnob);
	if (status != 0)
		goto cleanup;

	adapter->rx_csum = 1;
	adapter->max_rx_coal = BE_LRO_MAX_PKTS;

	memset(&get_fwv, 0,
	       sizeof(struct FWCMD_COMMON_GET_FW_VERSION_RESPONSE_PAYLOAD));
	printk(KERN_INFO "BladeEngine Driver version:%s. "
	       "Copyright ServerEngines, Corporation 2005 - 2008\n",
			       be_drvr_ver);
	status = be_function_get_fw_version(&pnob->fn_obj, &get_fwv, NULL,
					    NULL);
	if (status == BE_SUCCESS) {
		strncpy(be_fw_ver, get_fwv.firmware_version_string, 32);
		printk(KERN_INFO "BladeEngine Firmware Version:%s\n",
		       get_fwv.firmware_version_string);
	} else {
		printk(KERN_WARNING "Unable to get BE Firmware Version\n");
	}

	sema_init(&adapter->get_eth_stat_sem, 0);
	init_timer(&adapter->timer_ctxt.get_stats_timer);
	atomic_set(&adapter->timer_ctxt.get_stat_flag, 0);
	adapter->timer_ctxt.get_stats_timer.function =
	    &be_get_stats_timer_handler;

	status = be_mcc_create(adapter);
	if (status < 0)
		goto cleanup;
	status = be_mcc_init(adapter);
	if (status < 0)
		goto cleanup;


	status = be_mcc_add_async_event_callback(&adapter->net_obj->mcc_q_obj,
			 be_link_status_async_callback, (void *)adapter);
	if (status != BE_SUCCESS) {
		printk(KERN_WARNING "add_async_event_callback failed");
		printk(KERN_WARNING
		       "Link status changes may not be reflected\n");
	}

	status = register_netdev(netdev);
	if (status != 0)
		goto cleanup;
	be_update_link_status(adapter);
	adapter->dev_state = BE_DEV_STATE_INIT;
	return 0;

cleanup:
	be_remove(pdev);
	return status;
error_adapter:
	pci_release_regions(pdev);
error_pci_req:
	pci_disable_device(pdev);
error:
	printk(KERN_ERR "BladeEngine initalization failed\n");
	return status;
}

/*
 * Get the current link status and print the status on console
 */
void be_update_link_status(struct be_adapter *adapter)
{
	int status;
	struct be_net_object *pnob = adapter->net_obj;

	status = be_rxf_link_status(&pnob->fn_obj, adapter->be_link_sts, NULL,
			NULL, NULL);
	if (status == BE_SUCCESS) {
		if (adapter->be_link_sts->mac0_speed &&
		    adapter->be_link_sts->mac0_duplex)
			adapter->port0_link_sts = BE_PORT_LINK_UP;
		else
			adapter->port0_link_sts = BE_PORT_LINK_DOWN;

		if (adapter->be_link_sts->mac1_speed &&
		    adapter->be_link_sts->mac1_duplex)
			adapter->port1_link_sts = BE_PORT_LINK_UP;
		else
			adapter->port1_link_sts = BE_PORT_LINK_DOWN;

		dev_info(&pnob->netdev->dev, "Link Properties:\n");
		be_print_link_info(adapter->be_link_sts);
		return;
	}
	dev_info(&pnob->netdev->dev, "Could not get link status\n");
	return;
}


#ifdef CONFIG_PM
static void
be_pm_cleanup(struct be_adapter *adapter,
	      struct be_net_object *pnob, struct net_device *netdev)
{
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	be_wait_nic_tx_cmplx_cmpl(pnob);
	be_disable_eq_intr(pnob);

	if (adapter->tasklet_started) {
		tasklet_kill(&adapter->sts_handler);
		adapter->tasklet_started = 0;
	}

	be_unregister_isr(adapter);
	be_disable_intr(pnob);

	be_tx_q_clean(pnob);
	be_rx_q_clean(pnob);

	be_destroy_netobj(pnob);
}

static int be_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev =  adapter->netdevp;
	struct be_net_object *pnob = netdev_priv(netdev);

	adapter->dev_pm_state = adapter->dev_state;
	adapter->dev_state = BE_DEV_STATE_SUSPEND;

	netif_device_detach(netdev);
	if (netif_running(netdev))
		be_pm_cleanup(adapter, pnob, netdev);

	pci_enable_wake(pdev, 3, 1);
	pci_enable_wake(pdev, 4, 1);	/* D3 Cold = 4 */
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static void be_up(struct be_adapter *adapter)
{
	struct be_net_object *pnob = adapter->net_obj;

	if (pnob->num_vlans != 0)
		be_rxf_vlan_config(&pnob->fn_obj, false, pnob->num_vlans,
			pnob->vlan_tag, NULL, NULL, NULL);

}

static int be_resume(struct pci_dev *pdev)
{
	int status = 0;
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev =  adapter->netdevp;
	struct be_net_object *pnob = netdev_priv(netdev);

	netif_device_detach(netdev);

	status = pci_enable_device(pdev);
	if (status)
		return status;

	pci_set_power_state(pdev, 0);
	pci_restore_state(pdev);
	pci_enable_wake(pdev, 3, 0);
	pci_enable_wake(pdev, 4, 0);	/* 4 is D3 cold */

	netif_carrier_on(netdev);
	netif_start_queue(netdev);

	if (netif_running(netdev)) {
		be_rxf_mac_address_read_write(&pnob->fn_obj, false, false,
			false, true, false, netdev->dev_addr, NULL, NULL);

		status = be_nob_ring_init(adapter, pnob);
		if (status < 0)
			return status;

		tasklet_init(&(adapter->sts_handler), be_process_intr,
			     (unsigned long)adapter);
		adapter->tasklet_started = 1;

		if (be_register_isr(adapter, pnob) != 0) {
			printk(KERN_ERR "be_register_isr failed\n");
			return status;
		}


		status = be_mcc_init(adapter);
		if (status < 0) {
			printk(KERN_ERR "be_mcc_init failed\n");
			return status;
		}
		be_update_link_status(adapter);
		/*
		 * Register async call back function to handle link
		 * status updates
		 */
		status = be_mcc_add_async_event_callback(
				&adapter->net_obj->mcc_q_obj,
				be_link_status_async_callback, (void *)adapter);
		if (status != BE_SUCCESS) {
			printk(KERN_WARNING "add_async_event_callback failed");
			printk(KERN_WARNING
			       "Link status changes may not be reflected\n");
		}
		be_enable_intr(pnob);
		be_enable_eq_intr(pnob);
		be_up(adapter);
	}
	netif_device_attach(netdev);
	adapter->dev_state = adapter->dev_pm_state;
	return 0;

}

#endif

/* Wait until no more pending transmits  */
void be_wait_nic_tx_cmplx_cmpl(struct be_net_object *pnob)
{
	int i;

	/* Wait for 20us * 50000 (= 1s) and no more */
	i = 0;
	while ((pnob->tx_q_tl != pnob->tx_q_hd) && (i < 50000)) {
		++i;
		udelay(20);
	}

	/* Check for no more pending transmits */
	if (i >= 50000) {
		printk(KERN_WARNING
		       "Did not receive completions for all TX requests\n");
	}
}

static struct pci_driver be_driver = {
	.name = be_driver_name,
	.id_table = be_device_id_table,
	.probe = be_probe,
#ifdef CONFIG_PM
	.suspend = be_suspend,
	.resume = be_resume,
#endif
	.remove = be_remove
};

/*
 * Module init entry point. Registers our our device and return.
 * Our probe will be called if the device is found.
 */
static int __init be_init_module(void)
{
	int ret;

	if (rxbuf_size != 8192 && rxbuf_size != 4096 && rxbuf_size != 2048) {
		printk(KERN_WARNING
		       "Unsupported receive buffer size (%d) requested\n",
		       rxbuf_size);
		printk(KERN_WARNING
		       "Must be 2048, 4096 or 8192. Defaulting to 2048\n");
		rxbuf_size = 2048;
	}

	ret = pci_register_driver(&be_driver);

	return ret;
}

module_init(be_init_module);

/*
 * be_exit_module - Driver Exit Cleanup Routine
 */
static void __exit be_exit_module(void)
{
	pci_unregister_driver(&be_driver);
}

module_exit(be_exit_module);
