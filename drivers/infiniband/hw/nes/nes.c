/*
 * Copyright (c) 2006 - 2008 NetEffect, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <linux/highmem.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_pack.h>
#include <rdma/iw_cm.h>

#include "nes.h"

#include <net/netevent.h>
#include <net/neighbour.h>
#include <linux/route.h>
#include <net/ip_fib.h>

MODULE_AUTHOR("NetEffect");
MODULE_DESCRIPTION("NetEffect RNIC Low-level iWARP Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

int max_mtu = 9000;
int nics_per_function = 1;
int interrupt_mod_interval = 0;


/* Interoperability */
int mpa_version = 1;
module_param(mpa_version, int, 0);
MODULE_PARM_DESC(mpa_version, "MPA version to be used int MPA Req/Resp (0 or 1)");

/* Interoperability */
int disable_mpa_crc = 0;
module_param(disable_mpa_crc, int, 0);
MODULE_PARM_DESC(disable_mpa_crc, "Disable checking of MPA CRC");

unsigned int send_first = 0;
module_param(send_first, int, 0);
MODULE_PARM_DESC(send_first, "Send RDMA Message First on Active Connection");


unsigned int nes_drv_opt = 0;
module_param(nes_drv_opt, int, 0);
MODULE_PARM_DESC(nes_drv_opt, "Driver option parameters");

unsigned int nes_debug_level = 0;
module_param_named(debug_level, nes_debug_level, uint, 0644);
MODULE_PARM_DESC(debug_level, "Enable debug output level");

LIST_HEAD(nes_adapter_list);
LIST_HEAD(nes_dev_list);

atomic_t qps_destroyed;
atomic_t cqp_reqs_allocated;
atomic_t cqp_reqs_freed;
atomic_t cqp_reqs_dynallocated;
atomic_t cqp_reqs_dynfreed;
atomic_t cqp_reqs_queued;
atomic_t cqp_reqs_redriven;

static void nes_print_macaddr(struct net_device *netdev);
static irqreturn_t nes_interrupt(int, void *);
static int __devinit nes_probe(struct pci_dev *, const struct pci_device_id *);
static void __devexit nes_remove(struct pci_dev *);
static int __init nes_init_module(void);
static void __exit nes_exit_module(void);
static unsigned int ee_flsh_adapter;
static unsigned int sysfs_nonidx_addr;
static unsigned int sysfs_idx_addr;

static struct pci_device_id nes_pci_table[] = {
	{PCI_VENDOR_ID_NETEFFECT, PCI_DEVICE_ID_NETEFFECT_NE020, PCI_ANY_ID, PCI_ANY_ID},
	{0}
};

MODULE_DEVICE_TABLE(pci, nes_pci_table);

static int nes_inetaddr_event(struct notifier_block *, unsigned long, void *);
static int nes_net_event(struct notifier_block *, unsigned long, void *);
static int nes_notifiers_registered;


static struct notifier_block nes_inetaddr_notifier = {
	.notifier_call = nes_inetaddr_event
};

static struct notifier_block nes_net_notifier = {
	.notifier_call = nes_net_event
};




/**
 * nes_inetaddr_event
 */
static int nes_inetaddr_event(struct notifier_block *notifier,
		unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	struct net_device *event_netdev = ifa->ifa_dev->dev;
	struct nes_device *nesdev;
	struct net_device *netdev;
	struct nes_vnic *nesvnic;
	unsigned int addr;
	unsigned int mask;

	addr = ntohl(ifa->ifa_address);
	mask = ntohl(ifa->ifa_mask);
	nes_debug(NES_DBG_NETDEV, "nes_inetaddr_event: ip address %08X, netmask %08X.\n",
			addr, mask);
	list_for_each_entry(nesdev, &nes_dev_list, list) {
		nes_debug(NES_DBG_NETDEV, "Nesdev list entry = 0x%p. (%s)\n",
				nesdev, nesdev->netdev[0]->name);
		netdev = nesdev->netdev[0];
		nesvnic = netdev_priv(netdev);
		if (netdev == event_netdev) {
			if (nesvnic->rdma_enabled == 0) {
				nes_debug(NES_DBG_NETDEV, "Returning without processing event for %s since"
						" RDMA is not enabled.\n",
						netdev->name);
				return NOTIFY_OK;
			}
			/* we have ifa->ifa_address/mask here if we need it */
			switch (event) {
				case NETDEV_DOWN:
					nes_debug(NES_DBG_NETDEV, "event:DOWN\n");
					nes_write_indexed(nesdev,
							NES_IDX_DST_IP_ADDR+(0x10*PCI_FUNC(nesdev->pcidev->devfn)), 0);

					nes_manage_arp_cache(netdev, netdev->dev_addr,
							ntohl(nesvnic->local_ipaddr), NES_ARP_DELETE);
					nesvnic->local_ipaddr = 0;
					return NOTIFY_OK;
					break;
				case NETDEV_UP:
					nes_debug(NES_DBG_NETDEV, "event:UP\n");

					if (nesvnic->local_ipaddr != 0) {
						nes_debug(NES_DBG_NETDEV, "Interface already has local_ipaddr\n");
						return NOTIFY_OK;
					}
					/* Add the address to the IP table */
					nesvnic->local_ipaddr = ifa->ifa_address;

					nes_write_indexed(nesdev,
							NES_IDX_DST_IP_ADDR+(0x10*PCI_FUNC(nesdev->pcidev->devfn)),
							ntohl(ifa->ifa_address));
					nes_manage_arp_cache(netdev, netdev->dev_addr,
							ntohl(nesvnic->local_ipaddr), NES_ARP_ADD);
					return NOTIFY_OK;
					break;
				default:
					break;
			}
		}
	}

	return NOTIFY_DONE;
}


/**
 * nes_net_event
 */
static int nes_net_event(struct notifier_block *notifier,
		unsigned long event, void *ptr)
{
	struct neighbour *neigh = ptr;
	struct nes_device *nesdev;
	struct net_device *netdev;
	struct nes_vnic *nesvnic;

	switch (event) {
		case NETEVENT_NEIGH_UPDATE:
			list_for_each_entry(nesdev, &nes_dev_list, list) {
				/* nes_debug(NES_DBG_NETDEV, "Nesdev list entry = 0x%p.\n", nesdev); */
				netdev = nesdev->netdev[0];
				nesvnic = netdev_priv(netdev);
				if (netdev == neigh->dev) {
					if (nesvnic->rdma_enabled == 0) {
						nes_debug(NES_DBG_NETDEV, "Skipping device %s since no RDMA\n",
								netdev->name);
					} else {
						if (neigh->nud_state & NUD_VALID) {
							nes_manage_arp_cache(neigh->dev, neigh->ha,
									ntohl(*(__be32 *)neigh->primary_key), NES_ARP_ADD);
						} else {
							nes_manage_arp_cache(neigh->dev, neigh->ha,
									ntohl(*(__be32 *)neigh->primary_key), NES_ARP_DELETE);
						}
					}
					return NOTIFY_OK;
				}
			}
			break;
		default:
			nes_debug(NES_DBG_NETDEV, "NETEVENT_ %lu undefined\n", event);
			break;
	}

	return NOTIFY_DONE;
}


/**
 * nes_add_ref
 */
void nes_add_ref(struct ib_qp *ibqp)
{
	struct nes_qp *nesqp;

	nesqp = to_nesqp(ibqp);
	nes_debug(NES_DBG_QP, "Bumping refcount for QP%u.  Pre-inc value = %u\n",
			ibqp->qp_num, atomic_read(&nesqp->refcount));
	atomic_inc(&nesqp->refcount);
}

static void nes_cqp_rem_ref_callback(struct nes_device *nesdev, struct nes_cqp_request *cqp_request)
{
	unsigned long flags;
	struct nes_qp *nesqp = cqp_request->cqp_callback_pointer;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 qp_id;

	atomic_inc(&qps_destroyed);

	/* Free the control structures */

	qp_id = nesqp->hwqp.qp_id;
	if (nesqp->pbl_vbase) {
		pci_free_consistent(nesdev->pcidev, nesqp->qp_mem_size,
				nesqp->hwqp.q2_vbase, nesqp->hwqp.q2_pbase);
		spin_lock_irqsave(&nesadapter->pbl_lock, flags);
		nesadapter->free_256pbl++;
		spin_unlock_irqrestore(&nesadapter->pbl_lock, flags);
		pci_free_consistent(nesdev->pcidev, 256, nesqp->pbl_vbase, nesqp->pbl_pbase);
		nesqp->pbl_vbase = NULL;

	} else {
		pci_free_consistent(nesdev->pcidev, nesqp->qp_mem_size,
				nesqp->hwqp.sq_vbase, nesqp->hwqp.sq_pbase);
	}
	nes_free_resource(nesadapter, nesadapter->allocated_qps, nesqp->hwqp.qp_id);

	kfree(nesqp->allocated_buffer);

}

/**
 * nes_rem_ref
 */
void nes_rem_ref(struct ib_qp *ibqp)
{
	u64 u64temp;
	struct nes_qp *nesqp;
	struct nes_vnic *nesvnic = to_nesvnic(ibqp->device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	u32 opcode;

	nesqp = to_nesqp(ibqp);

	if (atomic_read(&nesqp->refcount) == 0) {
		printk(KERN_INFO PFX "%s: Reference count already 0 for QP%d, last aeq = 0x%04X.\n",
				__FUNCTION__, ibqp->qp_num, nesqp->last_aeq);
		BUG();
	}

	if (atomic_dec_and_test(&nesqp->refcount)) {
		nesadapter->qp_table[nesqp->hwqp.qp_id-NES_FIRST_QPN] = NULL;

		/* Destroy the QP */
		cqp_request = nes_get_cqp_request(nesdev);
		if (cqp_request == NULL) {
			nes_debug(NES_DBG_QP, "Failed to get a cqp_request.\n");
			return;
		}
		cqp_request->waiting = 0;
		cqp_request->callback = 1;
		cqp_request->cqp_callback = nes_cqp_rem_ref_callback;
		cqp_request->cqp_callback_pointer = nesqp;
		cqp_wqe = &cqp_request->cqp_wqe;

		nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
		opcode = NES_CQP_DESTROY_QP | NES_CQP_QP_TYPE_IWARP;

		if (nesqp->hte_added) {
			opcode  |= NES_CQP_QP_DEL_HTE;
			nesqp->hte_added = 0;
		}
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX, opcode);
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX, nesqp->hwqp.qp_id);
		u64temp = (u64)nesqp->nesqp_context_pbase;
		set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_QP_WQE_CONTEXT_LOW_IDX, u64temp);
		nes_post_cqp_request(nesdev, cqp_request, NES_CQP_REQUEST_RING_DOORBELL);
	}
}


/**
 * nes_get_qp
 */
struct ib_qp *nes_get_qp(struct ib_device *device, int qpn)
{
	struct nes_vnic *nesvnic = to_nesvnic(device);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;

	if ((qpn < NES_FIRST_QPN) || (qpn >= (NES_FIRST_QPN + nesadapter->max_qp)))
		return NULL;

	return &nesadapter->qp_table[qpn - NES_FIRST_QPN]->ibqp;
}


/**
 * nes_print_macaddr
 */
static void nes_print_macaddr(struct net_device *netdev)
{
	nes_debug(NES_DBG_INIT, "%s: MAC %02X:%02X:%02X:%02X:%02X:%02X, IRQ %u\n",
			netdev->name,
			netdev->dev_addr[0], netdev->dev_addr[1], netdev->dev_addr[2],
			netdev->dev_addr[3], netdev->dev_addr[4], netdev->dev_addr[5],
			netdev->irq);
}


/**
 * nes_interrupt - handle interrupts
 */
static irqreturn_t nes_interrupt(int irq, void *dev_id)
{
	struct nes_device *nesdev = (struct nes_device *)dev_id;
	int handled = 0;
	u32 int_mask;
	u32 int_req;
	u32 int_stat;
	u32 intf_int_stat;
	u32 timer_stat;

	if (nesdev->msi_enabled) {
		/* No need to read the interrupt pending register if msi is enabled */
		handled = 1;
	} else {
		if (unlikely(nesdev->nesadapter->hw_rev == NE020_REV)) {
			/* Master interrupt enable provides synchronization for kicking off bottom half
			  when interrupt sharing is going on */
			int_mask = nes_read32(nesdev->regs + NES_INT_MASK);
			if (int_mask & 0x80000000) {
				/* Check interrupt status to see if this might be ours */
				int_stat = nes_read32(nesdev->regs + NES_INT_STAT);
				int_req = nesdev->int_req;
				if (int_stat&int_req) {
					/* if interesting CEQ or AEQ is pending, claim the interrupt */
					if ((int_stat&int_req) & (~(NES_INT_TIMER|NES_INT_INTF))) {
						handled = 1;
					} else {
						if (((int_stat & int_req) & NES_INT_TIMER) == NES_INT_TIMER) {
							/* Timer might be running but might be for another function */
							timer_stat = nes_read32(nesdev->regs + NES_TIMER_STAT);
							if ((timer_stat & nesdev->timer_int_req) != 0) {
								handled = 1;
							}
						}
						if ((((int_stat & int_req) & NES_INT_INTF) == NES_INT_INTF) &&
								(handled == 0)) {
							intf_int_stat = nes_read32(nesdev->regs+NES_INTF_INT_STAT);
							if ((intf_int_stat & nesdev->intf_int_req) != 0) {
								handled = 1;
							}
						}
					}
					if (handled) {
						nes_write32(nesdev->regs+NES_INT_MASK, int_mask & (~0x80000000));
						int_mask = nes_read32(nesdev->regs+NES_INT_MASK);
						/* Save off the status to save an additional read */
						nesdev->int_stat = int_stat;
						nesdev->napi_isr_ran = 1;
					}
				}
			}
		} else {
			handled = nes_read32(nesdev->regs+NES_INT_PENDING);
		}
	}

	if (handled) {

		if (nes_napi_isr(nesdev) == 0) {
			tasklet_schedule(&nesdev->dpc_tasklet);

		}
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}


/**
 * nes_probe - Device initialization
 */
static int __devinit nes_probe(struct pci_dev *pcidev, const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	struct nes_device *nesdev = NULL;
	int ret = 0;
	struct nes_vnic *nesvnic = NULL;
	void __iomem *mmio_regs = NULL;
	u8 hw_rev;

	assert(pcidev != NULL);
	assert(ent != NULL);

	printk(KERN_INFO PFX "NetEffect RNIC driver v%s loading. (%s)\n",
			DRV_VERSION, pci_name(pcidev));

	ret = pci_enable_device(pcidev);
	if (ret) {
		printk(KERN_ERR PFX "Unable to enable PCI device. (%s)\n", pci_name(pcidev));
		goto bail0;
	}

	nes_debug(NES_DBG_INIT, "BAR0 (@0x%08lX) size = 0x%lX bytes\n",
			(long unsigned int)pci_resource_start(pcidev, BAR_0),
			(long unsigned int)pci_resource_len(pcidev, BAR_0));
	nes_debug(NES_DBG_INIT, "BAR1 (@0x%08lX) size = 0x%lX bytes\n",
			(long unsigned int)pci_resource_start(pcidev, BAR_1),
			(long unsigned int)pci_resource_len(pcidev, BAR_1));

	/* Make sure PCI base addr are MMIO */
	if (!(pci_resource_flags(pcidev, BAR_0) & IORESOURCE_MEM) ||
			!(pci_resource_flags(pcidev, BAR_1) & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX "PCI regions not an MMIO resource\n");
		ret = -ENODEV;
		goto bail1;
	}

	/* Reserve PCI I/O and memory resources */
	ret = pci_request_regions(pcidev, DRV_NAME);
	if (ret) {
		printk(KERN_ERR PFX "Unable to request regions. (%s)\n", pci_name(pcidev));
		goto bail1;
	}

	if ((sizeof(dma_addr_t) > 4)) {
		ret = pci_set_dma_mask(pcidev, DMA_64BIT_MASK);
		if (ret < 0) {
			printk(KERN_ERR PFX "64b DMA mask configuration failed\n");
			goto bail2;
		}
		ret = pci_set_consistent_dma_mask(pcidev, DMA_64BIT_MASK);
		if (ret) {
			printk(KERN_ERR PFX "64b DMA consistent mask configuration failed\n");
			goto bail2;
		}
	} else {
		ret = pci_set_dma_mask(pcidev, DMA_32BIT_MASK);
		if (ret < 0) {
			printk(KERN_ERR PFX "32b DMA mask configuration failed\n");
			goto bail2;
		}
		ret = pci_set_consistent_dma_mask(pcidev, DMA_32BIT_MASK);
		if (ret) {
			printk(KERN_ERR PFX "32b DMA consistent mask configuration failed\n");
			goto bail2;
		}
	}

	pci_set_master(pcidev);

	/* Allocate hardware structure */
	nesdev = kzalloc(sizeof(struct nes_device), GFP_KERNEL);
	if (!nesdev) {
		printk(KERN_ERR PFX "%s: Unable to alloc hardware struct\n", pci_name(pcidev));
		ret = -ENOMEM;
		goto bail2;
	}

	nes_debug(NES_DBG_INIT, "Allocated nes device at %p\n", nesdev);
	nesdev->pcidev = pcidev;
	pci_set_drvdata(pcidev, nesdev);

	pci_read_config_byte(pcidev, 0x0008, &hw_rev);
	nes_debug(NES_DBG_INIT, "hw_rev=%u\n", hw_rev);

	spin_lock_init(&nesdev->indexed_regs_lock);

	/* Remap the PCI registers in adapter BAR0 to kernel VA space */
	mmio_regs = ioremap_nocache(pci_resource_start(pcidev, BAR_0), sizeof(mmio_regs));
	if (mmio_regs == NULL) {
		printk(KERN_ERR PFX "Unable to remap BAR0\n");
		ret = -EIO;
		goto bail3;
	}
	nesdev->regs = mmio_regs;
	nesdev->index_reg = 0x50 + (PCI_FUNC(pcidev->devfn)*8) + mmio_regs;

	/* Ensure interrupts are disabled */
	nes_write32(nesdev->regs+NES_INT_MASK, 0x7fffffff);

	if (nes_drv_opt & NES_DRV_OPT_ENABLE_MSI) {
		if (!pci_enable_msi(nesdev->pcidev)) {
			nesdev->msi_enabled = 1;
			nes_debug(NES_DBG_INIT, "MSI is enabled for device %s\n",
					pci_name(pcidev));
		} else {
			nes_debug(NES_DBG_INIT, "MSI is disabled by linux for device %s\n",
					pci_name(pcidev));
		}
	} else {
		nes_debug(NES_DBG_INIT, "MSI not requested due to driver options for device %s\n",
				pci_name(pcidev));
	}

	nesdev->csr_start = pci_resource_start(nesdev->pcidev, BAR_0);
	nesdev->doorbell_region = pci_resource_start(nesdev->pcidev, BAR_1);

	/* Init the adapter */
	nesdev->nesadapter = nes_init_adapter(nesdev, hw_rev);
	if (!nesdev->nesadapter) {
		printk(KERN_ERR PFX "Unable to initialize adapter.\n");
		ret = -ENOMEM;
		goto bail5;
	}
	nesdev->nesadapter->et_rx_coalesce_usecs_irq = interrupt_mod_interval;

	/* nesdev->base_doorbell_index =
			nesdev->nesadapter->pd_config_base[PCI_FUNC(nesdev->pcidev->devfn)]; */
	nesdev->base_doorbell_index = 1;
	nesdev->doorbell_start = nesdev->nesadapter->doorbell_start;
	nesdev->mac_index = PCI_FUNC(nesdev->pcidev->devfn) % nesdev->nesadapter->port_count;

	tasklet_init(&nesdev->dpc_tasklet, nes_dpc, (unsigned long)nesdev);

	/* bring up the Control QP */
	if (nes_init_cqp(nesdev)) {
		ret = -ENODEV;
		goto bail6;
	}

	/* Arm the CCQ */
	nes_write32(nesdev->regs+NES_CQE_ALLOC, NES_CQE_ALLOC_NOTIFY_NEXT |
			PCI_FUNC(nesdev->pcidev->devfn));
	nes_read32(nesdev->regs+NES_CQE_ALLOC);

	/* Enable the interrupts */
	nesdev->int_req = (0x101 << PCI_FUNC(nesdev->pcidev->devfn)) |
			(1 << (PCI_FUNC(nesdev->pcidev->devfn)+16));
	if (PCI_FUNC(nesdev->pcidev->devfn) < 4) {
		nesdev->int_req |= (1 << (PCI_FUNC(nesdev->pcidev->devfn)+24));
	}

	/* TODO: This really should be the first driver to load, not function 0 */
	if (PCI_FUNC(nesdev->pcidev->devfn) == 0) {
		/* pick up PCI and critical errors if the first driver to load */
		nesdev->intf_int_req = NES_INTF_INT_PCIERR | NES_INTF_INT_CRITERR;
		nesdev->int_req |= NES_INT_INTF;
	} else {
		nesdev->intf_int_req = 0;
	}
	nesdev->intf_int_req |= (1 << (PCI_FUNC(nesdev->pcidev->devfn)+16));
	nes_write_indexed(nesdev, NES_IDX_DEBUG_ERROR_MASKS0, 0);
	nes_write_indexed(nesdev, NES_IDX_DEBUG_ERROR_MASKS1, 0);
	nes_write_indexed(nesdev, NES_IDX_DEBUG_ERROR_MASKS2, 0x00001265);
	nes_write_indexed(nesdev, NES_IDX_DEBUG_ERROR_MASKS4, 0x18021804);

	nes_write_indexed(nesdev, NES_IDX_DEBUG_ERROR_MASKS3, 0x17801790);

	/* deal with both periodic and one_shot */
	nesdev->timer_int_req = 0x101 << PCI_FUNC(nesdev->pcidev->devfn);
	nesdev->nesadapter->timer_int_req |= nesdev->timer_int_req;
	nes_debug(NES_DBG_INIT, "setting int_req for function %u, nesdev = 0x%04X, adapter = 0x%04X\n",
			PCI_FUNC(nesdev->pcidev->devfn),
			nesdev->timer_int_req, nesdev->nesadapter->timer_int_req);

	nes_write32(nesdev->regs+NES_INTF_INT_MASK, ~(nesdev->intf_int_req));

	list_add_tail(&nesdev->list, &nes_dev_list);

	/* Request an interrupt line for the driver */
	ret = request_irq(pcidev->irq, nes_interrupt, IRQF_SHARED, DRV_NAME, nesdev);
	if (ret) {
		printk(KERN_ERR PFX "%s: requested IRQ %u is busy\n",
				pci_name(pcidev), pcidev->irq);
		goto bail65;
	}

	nes_write32(nesdev->regs+NES_INT_MASK, ~nesdev->int_req);

	if (nes_notifiers_registered == 0) {
		register_inetaddr_notifier(&nes_inetaddr_notifier);
		register_netevent_notifier(&nes_net_notifier);
	}
	nes_notifiers_registered++;

	/* Initialize network devices */
		if ((netdev = nes_netdev_init(nesdev, mmio_regs)) == NULL) {
			goto bail7;
		}

		/* Register network device */
		ret = register_netdev(netdev);
		if (ret) {
			printk(KERN_ERR PFX "Unable to register netdev, ret = %d\n", ret);
			nes_netdev_destroy(netdev);
			goto bail7;
		}

		nes_print_macaddr(netdev);
		/* create a CM core for this netdev */
		nesvnic = netdev_priv(netdev);

		nesdev->netdev_count++;
		nesdev->nesadapter->netdev_count++;


	printk(KERN_ERR PFX "%s: NetEffect RNIC driver successfully loaded.\n",
			pci_name(pcidev));
	return 0;

	bail7:
	printk(KERN_ERR PFX "bail7\n");
	while (nesdev->netdev_count > 0) {
		nesdev->netdev_count--;
		nesdev->nesadapter->netdev_count--;

		unregister_netdev(nesdev->netdev[nesdev->netdev_count]);
		nes_netdev_destroy(nesdev->netdev[nesdev->netdev_count]);
	}

	nes_debug(NES_DBG_INIT, "netdev_count=%d, nesadapter->netdev_count=%d\n",
			nesdev->netdev_count, nesdev->nesadapter->netdev_count);

	nes_notifiers_registered--;
	if (nes_notifiers_registered == 0) {
		unregister_netevent_notifier(&nes_net_notifier);
		unregister_inetaddr_notifier(&nes_inetaddr_notifier);
	}

	list_del(&nesdev->list);
	nes_destroy_cqp(nesdev);

	bail65:
	printk(KERN_ERR PFX "bail65\n");
	free_irq(pcidev->irq, nesdev);
	if (nesdev->msi_enabled) {
		pci_disable_msi(pcidev);
	}
	bail6:
	printk(KERN_ERR PFX "bail6\n");
	tasklet_kill(&nesdev->dpc_tasklet);
	/* Deallocate the Adapter Structure */
	nes_destroy_adapter(nesdev->nesadapter);

	bail5:
	printk(KERN_ERR PFX "bail5\n");
	iounmap(nesdev->regs);

	bail3:
	printk(KERN_ERR PFX "bail3\n");
	kfree(nesdev);

	bail2:
	pci_release_regions(pcidev);

	bail1:
	pci_disable_device(pcidev);

	bail0:
	return ret;
}


/**
 * nes_remove - unload from kernel
 */
static void __devexit nes_remove(struct pci_dev *pcidev)
{
	struct nes_device *nesdev = pci_get_drvdata(pcidev);
	struct net_device *netdev;
	int netdev_index = 0;

		if (nesdev->netdev_count) {
			netdev = nesdev->netdev[netdev_index];
			if (netdev) {
				netif_stop_queue(netdev);
				unregister_netdev(netdev);
				nes_netdev_destroy(netdev);

				nesdev->netdev[netdev_index] = NULL;
				nesdev->netdev_count--;
				nesdev->nesadapter->netdev_count--;
			}
		}

	nes_notifiers_registered--;
	if (nes_notifiers_registered == 0) {
		unregister_netevent_notifier(&nes_net_notifier);
		unregister_inetaddr_notifier(&nes_inetaddr_notifier);
	}

	list_del(&nesdev->list);
	nes_destroy_cqp(nesdev);
	tasklet_kill(&nesdev->dpc_tasklet);

	/* Deallocate the Adapter Structure */
	nes_destroy_adapter(nesdev->nesadapter);

	free_irq(pcidev->irq, nesdev);

	if (nesdev->msi_enabled) {
		pci_disable_msi(pcidev);
	}

	iounmap(nesdev->regs);
	kfree(nesdev);

	/* nes_debug(NES_DBG_SHUTDOWN, "calling pci_release_regions.\n"); */
	pci_release_regions(pcidev);
	pci_disable_device(pcidev);
	pci_set_drvdata(pcidev, NULL);
}


static struct pci_driver nes_pci_driver = {
	.name = DRV_NAME,
	.id_table = nes_pci_table,
	.probe = nes_probe,
	.remove = __devexit_p(nes_remove),
};

static ssize_t nes_show_adapter(struct device_driver *ddp, char *buf)
{
	unsigned int  devfn = 0xffffffff;
	unsigned char bus_number = 0xff;
	unsigned int  i = 0;
	struct nes_device *nesdev;

	list_for_each_entry(nesdev, &nes_dev_list, list) {
		if (i == ee_flsh_adapter) {
			devfn      = nesdev->nesadapter->devfn;
			bus_number = nesdev->nesadapter->bus_number;
			break;
		}
		i++;
	}

	return snprintf(buf, PAGE_SIZE, "%x:%x", bus_number, devfn);
}

static ssize_t nes_store_adapter(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;

	ee_flsh_adapter = simple_strtoul(p, &p, 10);
	return strnlen(buf, count);
}

static ssize_t nes_show_ee_cmd(struct device_driver *ddp, char *buf)
{
	u32 eeprom_cmd = 0xdead;
	u32 i = 0;
	struct nes_device *nesdev;

	list_for_each_entry(nesdev, &nes_dev_list, list) {
		if (i == ee_flsh_adapter) {
			eeprom_cmd = nes_read32(nesdev->regs + NES_EEPROM_COMMAND);
			break;
		}
		i++;
	}
	return snprintf(buf, PAGE_SIZE, "0x%x\n", eeprom_cmd);
}

static ssize_t nes_store_ee_cmd(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;
	u32 i = 0;
	struct nes_device *nesdev;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		val = simple_strtoul(p, &p, 16);
		list_for_each_entry(nesdev, &nes_dev_list, list) {
			if (i == ee_flsh_adapter) {
				nes_write32(nesdev->regs + NES_EEPROM_COMMAND, val);
				break;
			}
			i++;
		}
	}
	return strnlen(buf, count);
}

static ssize_t nes_show_ee_data(struct device_driver *ddp, char *buf)
{
	u32 eeprom_data = 0xdead;
	u32 i = 0;
	struct nes_device *nesdev;

	list_for_each_entry(nesdev, &nes_dev_list, list) {
		if (i == ee_flsh_adapter) {
			eeprom_data = nes_read32(nesdev->regs + NES_EEPROM_DATA);
			break;
		}
		i++;
	}

	return  snprintf(buf, PAGE_SIZE, "0x%x\n", eeprom_data);
}

static ssize_t nes_store_ee_data(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;
	u32 i = 0;
	struct nes_device *nesdev;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		val = simple_strtoul(p, &p, 16);
		list_for_each_entry(nesdev, &nes_dev_list, list) {
			if (i == ee_flsh_adapter) {
				nes_write32(nesdev->regs + NES_EEPROM_DATA, val);
				break;
			}
			i++;
		}
	}
	return strnlen(buf, count);
}

static ssize_t nes_show_flash_cmd(struct device_driver *ddp, char *buf)
{
	u32 flash_cmd = 0xdead;
	u32 i = 0;
	struct nes_device *nesdev;

	list_for_each_entry(nesdev, &nes_dev_list, list) {
		if (i == ee_flsh_adapter) {
			flash_cmd = nes_read32(nesdev->regs + NES_FLASH_COMMAND);
			break;
		}
		i++;
	}

	return  snprintf(buf, PAGE_SIZE, "0x%x\n", flash_cmd);
}

static ssize_t nes_store_flash_cmd(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;
	u32 i = 0;
	struct nes_device *nesdev;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		val = simple_strtoul(p, &p, 16);
		list_for_each_entry(nesdev, &nes_dev_list, list) {
			if (i == ee_flsh_adapter) {
				nes_write32(nesdev->regs + NES_FLASH_COMMAND, val);
				break;
			}
			i++;
		}
	}
	return strnlen(buf, count);
}

static ssize_t nes_show_flash_data(struct device_driver *ddp, char *buf)
{
	u32 flash_data = 0xdead;
	u32 i = 0;
	struct nes_device *nesdev;

	list_for_each_entry(nesdev, &nes_dev_list, list) {
		if (i == ee_flsh_adapter) {
			flash_data = nes_read32(nesdev->regs + NES_FLASH_DATA);
			break;
		}
		i++;
	}

	return  snprintf(buf, PAGE_SIZE, "0x%x\n", flash_data);
}

static ssize_t nes_store_flash_data(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;
	u32 i = 0;
	struct nes_device *nesdev;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		val = simple_strtoul(p, &p, 16);
		list_for_each_entry(nesdev, &nes_dev_list, list) {
			if (i == ee_flsh_adapter) {
				nes_write32(nesdev->regs + NES_FLASH_DATA, val);
				break;
			}
			i++;
		}
	}
	return strnlen(buf, count);
}

static ssize_t nes_show_nonidx_addr(struct device_driver *ddp, char *buf)
{
	return  snprintf(buf, PAGE_SIZE, "0x%x\n", sysfs_nonidx_addr);
}

static ssize_t nes_store_nonidx_addr(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X')
		sysfs_nonidx_addr = simple_strtoul(p, &p, 16);

	return strnlen(buf, count);
}

static ssize_t nes_show_nonidx_data(struct device_driver *ddp, char *buf)
{
	u32 nonidx_data = 0xdead;
	u32 i = 0;
	struct nes_device *nesdev;

	list_for_each_entry(nesdev, &nes_dev_list, list) {
		if (i == ee_flsh_adapter) {
			nonidx_data = nes_read32(nesdev->regs + sysfs_nonidx_addr);
			break;
		}
		i++;
	}

	return  snprintf(buf, PAGE_SIZE, "0x%x\n", nonidx_data);
}

static ssize_t nes_store_nonidx_data(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;
	u32 i = 0;
	struct nes_device *nesdev;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		val = simple_strtoul(p, &p, 16);
		list_for_each_entry(nesdev, &nes_dev_list, list) {
			if (i == ee_flsh_adapter) {
				nes_write32(nesdev->regs + sysfs_nonidx_addr, val);
				break;
			}
			i++;
		}
	}
	return strnlen(buf, count);
}

static ssize_t nes_show_idx_addr(struct device_driver *ddp, char *buf)
{
	return  snprintf(buf, PAGE_SIZE, "0x%x\n", sysfs_idx_addr);
}

static ssize_t nes_store_idx_addr(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X')
		sysfs_idx_addr = simple_strtoul(p, &p, 16);

	return strnlen(buf, count);
}

static ssize_t nes_show_idx_data(struct device_driver *ddp, char *buf)
{
	u32 idx_data = 0xdead;
	u32 i = 0;
	struct nes_device *nesdev;

	list_for_each_entry(nesdev, &nes_dev_list, list) {
		if (i == ee_flsh_adapter) {
			idx_data = nes_read_indexed(nesdev, sysfs_idx_addr);
			break;
		}
		i++;
	}

	return  snprintf(buf, PAGE_SIZE, "0x%x\n", idx_data);
}

static ssize_t nes_store_idx_data(struct device_driver *ddp,
	const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;
	u32 i = 0;
	struct nes_device *nesdev;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		val = simple_strtoul(p, &p, 16);
		list_for_each_entry(nesdev, &nes_dev_list, list) {
			if (i == ee_flsh_adapter) {
				nes_write_indexed(nesdev, sysfs_idx_addr, val);
				break;
			}
			i++;
		}
	}
	return strnlen(buf, count);
}

static DRIVER_ATTR(adapter, S_IRUSR | S_IWUSR,
		   nes_show_adapter, nes_store_adapter);
static DRIVER_ATTR(eeprom_cmd, S_IRUSR | S_IWUSR,
		   nes_show_ee_cmd, nes_store_ee_cmd);
static DRIVER_ATTR(eeprom_data, S_IRUSR | S_IWUSR,
		   nes_show_ee_data, nes_store_ee_data);
static DRIVER_ATTR(flash_cmd, S_IRUSR | S_IWUSR,
		   nes_show_flash_cmd, nes_store_flash_cmd);
static DRIVER_ATTR(flash_data, S_IRUSR | S_IWUSR,
		   nes_show_flash_data, nes_store_flash_data);
static DRIVER_ATTR(nonidx_addr, S_IRUSR | S_IWUSR,
		   nes_show_nonidx_addr, nes_store_nonidx_addr);
static DRIVER_ATTR(nonidx_data, S_IRUSR | S_IWUSR,
		   nes_show_nonidx_data, nes_store_nonidx_data);
static DRIVER_ATTR(idx_addr, S_IRUSR | S_IWUSR,
		   nes_show_idx_addr, nes_store_idx_addr);
static DRIVER_ATTR(idx_data, S_IRUSR | S_IWUSR,
		   nes_show_idx_data, nes_store_idx_data);

static int nes_create_driver_sysfs(struct pci_driver *drv)
{
	int error;
	error  = driver_create_file(&drv->driver, &driver_attr_adapter);
	error |= driver_create_file(&drv->driver, &driver_attr_eeprom_cmd);
	error |= driver_create_file(&drv->driver, &driver_attr_eeprom_data);
	error |= driver_create_file(&drv->driver, &driver_attr_flash_cmd);
	error |= driver_create_file(&drv->driver, &driver_attr_flash_data);
	error |= driver_create_file(&drv->driver, &driver_attr_nonidx_addr);
	error |= driver_create_file(&drv->driver, &driver_attr_nonidx_data);
	error |= driver_create_file(&drv->driver, &driver_attr_idx_addr);
	error |= driver_create_file(&drv->driver, &driver_attr_idx_data);
	return error;
}

static void nes_remove_driver_sysfs(struct pci_driver *drv)
{
	driver_remove_file(&drv->driver, &driver_attr_adapter);
	driver_remove_file(&drv->driver, &driver_attr_eeprom_cmd);
	driver_remove_file(&drv->driver, &driver_attr_eeprom_data);
	driver_remove_file(&drv->driver, &driver_attr_flash_cmd);
	driver_remove_file(&drv->driver, &driver_attr_flash_data);
	driver_remove_file(&drv->driver, &driver_attr_nonidx_addr);
	driver_remove_file(&drv->driver, &driver_attr_nonidx_data);
	driver_remove_file(&drv->driver, &driver_attr_idx_addr);
	driver_remove_file(&drv->driver, &driver_attr_idx_data);
}

/**
 * nes_init_module - module initialization entry point
 */
static int __init nes_init_module(void)
{
	int retval;
	int retval1;

	retval = nes_cm_start();
	if (retval) {
		printk(KERN_ERR PFX "Unable to start NetEffect iWARP CM.\n");
		return retval;
	}
	retval = pci_register_driver(&nes_pci_driver);
	if (retval >= 0) {
		retval1 = nes_create_driver_sysfs(&nes_pci_driver);
		if (retval1 < 0)
			printk(KERN_ERR PFX "Unable to create NetEffect sys files.\n");
	}
	return retval;
}


/**
 * nes_exit_module - module unload entry point
 */
static void __exit nes_exit_module(void)
{
	nes_cm_stop();
	nes_remove_driver_sysfs(&nes_pci_driver);

	pci_unregister_driver(&nes_pci_driver);
}


module_init(nes_init_module);
module_exit(nes_exit_module);
