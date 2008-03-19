/**************************************************************************
 *
 * Copyright  2000-2006 Alacritech, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: slicoss.c
 *
 * The SLICOSS driver for Alacritech's IS-NIC products.
 *
 * This driver is supposed to support:
 *
 *      Mojave cards (single port PCI Gigabit) both copper and fiber
 *      Oasis cards (single and dual port PCI-x Gigabit) copper and fiber
 *      Kalahari cards (dual and quad port PCI-e Gigabit) copper and fiber
 *
 * The driver was acutally tested on Oasis and Kalahari cards.
 *
 *
 * NOTE: This is the standard, non-accelerated version of Alacritech's
 *       IS-NIC driver.
 */

#include <linux/version.h>

#define SLIC_DUMP_ENABLED               0
#define KLUDGE_FOR_4GB_BOUNDARY         1
#define DEBUG_MICROCODE                 1
#define SLIC_PRODUCTION_BUILD	        1
#define SLIC_FAILURE_RESET	            1
#define DBG                             1
#define SLIC_ASSERT_ENABLED		        1
#define SLIC_GET_STATS_ENABLED			1
#define SLIC_GET_STATS_TIMER_ENABLED	0
#define SLIC_PING_TIMER_ENABLED		    1
#define SLIC_POWER_MANAGEMENT_ENABLED	0
#define SLIC_INTERRUPT_PROCESS_LIMIT	1
#define LINUX_FREES_ADAPTER_RESOURCES	1
#define SLIC_OFFLOAD_IP_CHECKSUM		1
#define STATS_TIMER_INTERVAL			2
#define PING_TIMER_INTERVAL			    1

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <asm/unaligned.h>

#include <linux/ethtool.h>
#define SLIC_ETHTOOL_SUPPORT     1

#include <linux/uaccess.h>
#include "slicinc.h"
#include "gbdownload.h"
#include "gbrcvucode.h"
#include "oasisrcvucode.h"

#ifdef DEBUG_MICROCODE
#include "oasisdbgdownload.h"
#else
#include "oasisdownload.h"
#endif

#if SLIC_DUMP_ENABLED
#include "slicdump.h"
#endif

#define SLIC_POWER_MANAGEMENT  0

static uint slic_first_init = 1;
static char *slic_banner = "Alacritech SLIC Technology(tm) Server "\
		"and Storage Accelerator (Non-Accelerated)\n";

static char *slic_proc_version = "2.0.351  2006/07/14 12:26:00";
static char *slic_product_name = "SLIC Technology(tm) Server "\
		"and Storage Accelerator (Non-Accelerated)";
static char *slic_vendor = "Alacritech, Inc.";

static int slic_debug = 1;
static int debug = -1;
static struct net_device *head_netdevice;

base_driver_t slic_global = { {}, 0, 0, 0, 1, NULL, NULL };
static int intagg_delay = 100;
static u32 dynamic_intagg;
static int errormsg;
static int goodmsg;
static unsigned int rcv_count;
static struct dentry *slic_debugfs;

#define DRV_NAME          "slicoss"
#define DRV_VERSION       "2.0.1"
#define DRV_AUTHOR        "Alacritech, Inc. Engineering"
#define DRV_DESCRIPTION   "Alacritech SLIC Techonology(tm) "\
		"Non-Accelerated Driver"
#define DRV_COPYRIGHT     "Copyright  2000-2006 Alacritech, Inc. "\
		"All rights reserved."
#define PFX		   DRV_NAME " "

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_LICENSE("Dual BSD/GPL");

module_param(dynamic_intagg, int, 0);
MODULE_PARM_DESC(dynamic_intagg, "Dynamic Interrupt Aggregation Setting");
module_param(intagg_delay, int, 0);
MODULE_PARM_DESC(intagg_delay, "uSec Interrupt Aggregation Delay");

static struct pci_device_id slic_pci_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_ALACRITECH,
	 SLIC_1GB_DEVICE_ID,
	 PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_ALACRITECH,
	 SLIC_2GB_DEVICE_ID,
	 PCI_ANY_ID, PCI_ANY_ID,},
	{0,}
};

MODULE_DEVICE_TABLE(pci, slic_pci_tbl);

#define SLIC_GET_SLIC_HANDLE(_adapter, _pslic_handle)                   \
{                                                                       \
    SLIC_ACQUIRE_IRQ_SPINLOCK(_adapter->handle_lock);                   \
    _pslic_handle  =  _adapter->pfree_slic_handles;                     \
    if (_pslic_handle) {                                                \
	ASSERT(_pslic_handle->type == SLIC_HANDLE_FREE);                \
	_adapter->pfree_slic_handles = _pslic_handle->next;             \
    }                                                                   \
    SLIC_RELEASE_IRQ_SPINLOCK(_adapter->handle_lock);                   \
}

#define SLIC_FREE_SLIC_HANDLE(_adapter, _pslic_handle)                  \
{                                                                       \
    _pslic_handle->type = SLIC_HANDLE_FREE;                             \
    SLIC_ACQUIRE_IRQ_SPINLOCK(_adapter->handle_lock);                   \
    _pslic_handle->next = _adapter->pfree_slic_handles;                 \
    _adapter->pfree_slic_handles = _pslic_handle;                       \
    SLIC_RELEASE_IRQ_SPINLOCK(_adapter->handle_lock);                   \
}

static void slic_debug_init(void);
static void slic_debug_cleanup(void);
static void slic_debug_adapter_create(p_adapter_t adapter);
static void slic_debug_adapter_destroy(p_adapter_t adapter);
static void slic_debug_card_create(p_sliccard_t card);
static void slic_debug_card_destroy(p_sliccard_t card);

inline void slic_reg32_write(void __iomem *reg, ulong32 value, uint flush)
{
	writel(value, reg);
	if (flush)
		mb();
}

inline void slic_reg64_write(p_adapter_t adapter,
			       void __iomem *reg,
			       ulong32 value,
			       void __iomem *regh, ulong32 paddrh, uint flush)
{
	SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->bit64reglock);
	if (paddrh != adapter->curaddrupper) {
		adapter->curaddrupper = paddrh;
		writel(paddrh, regh);
	}
	writel(value, reg);
	if (flush)
		mb();
	SLIC_RELEASE_IRQ_SPINLOCK(adapter->bit64reglock);
}

inline ulong32 slic_reg32_read(u32 __iomem *reg, uint flush)
{
	return readl(reg);
}

inline ulong32 slic_reg16_read(pulong32 reg, uint flush)
{
	return (ushort) readw(reg);
}

void slic_init_driver(void)
{
	if (slic_first_init) {
		DBG_MSG("slicoss: %s slic_first_init set jiffies[%lx]\n",
			__func__, jiffies);
		slic_first_init = 0;
		SLIC_INIT_SPINLOCK(slic_global.driver_lock);
		slic_debug_init();
	}
}

static void slic_dbg_macaddrs(p_adapter_t adapter)
{
	DBG_MSG("  (%s) curr %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		adapter->netdev->name, adapter->currmacaddr[0],
		adapter->currmacaddr[1], adapter->currmacaddr[2],
		adapter->currmacaddr[3], adapter->currmacaddr[4],
		adapter->currmacaddr[5]);
	DBG_MSG("  (%s) mac  %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		adapter->netdev->name, adapter->macaddr[0],
		adapter->macaddr[1], adapter->macaddr[2],
		adapter->macaddr[3], adapter->macaddr[4], adapter->macaddr[5]);
	return;
}

#ifdef DEBUG_REGISTER_TRACE
static void slic_dbg_register_trace(p_adapter_t adapter, p_sliccard_t card)
{
	uint i;

	DBG_ERROR("Dump Register Write Trace: curr_ix == %d\n", card->debug_ix);
	for (i = 0; i < 32; i++) {
		DBG_ERROR("%2d %d %4x %x %x\n",
			  i, card->reg_type[i], card->reg_offset[i],
			  card->reg_value[i], card->reg_valueh[i]);
	}
}
}
#endif

static void slic_init_adapter(struct net_device *netdev,
			      struct pci_dev *pcidev,
			      const struct pci_device_id *pci_tbl_entry,
			      void __iomem *memaddr, int chip_idx)
{
	ushort index;
	pslic_handle_t pslic_handle;
	p_adapter_t adapter = (p_adapter_t) netdev_priv(netdev);

/*
    DBG_MSG("slicoss: %s (%s)\n    netdev [%p]\n    adapter[%p]\n    "
	    "pcidev [%p]\n", __func__, netdev->name, netdev, adapter, pcidev);*/
/*	adapter->pcidev = pcidev;*/
	adapter->vendid = pci_tbl_entry->vendor;
	adapter->devid = pci_tbl_entry->device;
	adapter->subsysid = pci_tbl_entry->subdevice;
	adapter->busnumber = pcidev->bus->number;
	adapter->slotnumber = ((pcidev->devfn >> 3) & 0x1F);
	adapter->functionnumber = (pcidev->devfn & 0x7);
	adapter->memorylength = pci_resource_len(pcidev, 0);
	adapter->slic_regs = (p_slic_regs_t) memaddr;
	adapter->irq = pcidev->irq;
/*	adapter->netdev = netdev;*/
	adapter->next_netdevice = head_netdevice;
	head_netdevice = netdev;
	adapter->chipid = chip_idx;
	adapter->port = 0;	/*adapter->functionnumber;*/
	adapter->cardindex = adapter->port;
	adapter->memorybase = memaddr;
	SLIC_INIT_SPINLOCK(adapter->upr_lock);
	SLIC_INIT_SPINLOCK(adapter->bit64reglock);
	SLIC_INIT_SPINLOCK(adapter->adapter_lock);
	SLIC_INIT_SPINLOCK(adapter->reset_lock);
	SLIC_INIT_SPINLOCK(adapter->handle_lock);

	adapter->card_size = 1;
	/*
	  Initialize slic_handle array
	*/
	ASSERT(SLIC_CMDQ_MAXCMDS <= 0xFFFF);
	/*
	 Start with 1.  0 is an invalid host handle.
	*/
	for (index = 1, pslic_handle = &adapter->slic_handles[1];
	     index < SLIC_CMDQ_MAXCMDS; index++, pslic_handle++) {

		pslic_handle->token.handle_index = index;
		pslic_handle->type = SLIC_HANDLE_FREE;
		pslic_handle->next = adapter->pfree_slic_handles;
		adapter->pfree_slic_handles = pslic_handle;
	}
/*
    DBG_MSG(".........\nix[%d] phandle[%p] pfree[%p] next[%p]\n",
	index, pslic_handle, adapter->pfree_slic_handles, pslic_handle->next);*/
	adapter->pshmem = (p_slic_shmem_t) pci_alloc_consistent(adapter->pcidev,
								sizeof
								(slic_shmem_t),
								&adapter->
								phys_shmem);
/*
      DBG_MSG("slicoss: %s (%s)\n   pshmem    [%p]\n   phys_shmem[%p]\n"\
		"slic_regs [%p]\n", __func__, netdev->name, adapter->pshmem,
		(pvoid)adapter->phys_shmem, adapter->slic_regs);
*/
	ASSERT(adapter->pshmem);

	SLIC_ZERO_MEMORY(adapter->pshmem, sizeof(slic_shmem_t));

	return;
}

int __devinit slic_entry_probe(struct pci_dev *pcidev,
			       const struct pci_device_id *pci_tbl_entry)
{
	static int cards_found;
	static int did_version;
	int err;
	struct net_device *netdev;
	p_adapter_t adapter;
	void __iomem *memmapped_ioaddr = NULL;
	ulong32 status = 0;
	ulong mmio_start = 0;
	ulong mmio_len = 0;
	p_sliccard_t card = NULL;

	DBG_MSG("slicoss: %s 2.6 VERSION ENTER jiffies[%lx] cpu %d\n",
		__func__, jiffies, smp_processor_id());

	slic_global.dynamic_intagg = dynamic_intagg;

	err = pci_enable_device(pcidev);

	DBG_MSG("Call pci_enable_device(%p)  status[%x]\n", pcidev, err);
	if (err)
		return err;

	if (slic_debug > 0 && did_version++ == 0) {
		printk(slic_banner);
		printk(slic_proc_version);
	}

	err = pci_set_dma_mask(pcidev, DMA_64BIT_MASK);
	if (!err) {
		DBG_MSG("pci_set_dma_mask(DMA_64BIT_MASK) successful\n");
	} else {
		err = pci_set_dma_mask(pcidev, DMA_32BIT_MASK);
		if (err) {
			DBG_MSG
			    ("No usable DMA configuration, aborting  err[%x]\n",
			     err);
			return err;
		}
		DBG_MSG("pci_set_dma_mask(DMA_32BIT_MASK) successful\n");
	}

	DBG_MSG("Call pci_request_regions\n");

	err = pci_request_regions(pcidev, DRV_NAME);
	if (err) {
		DBG_MSG("pci_request_regions FAILED err[%x]\n", err);
		return err;
	}

	DBG_MSG("call pci_set_master\n");
	pci_set_master(pcidev);

	DBG_MSG("call alloc_etherdev\n");
	netdev = alloc_etherdev(sizeof(adapter_t));
	if (!netdev) {
		err = -ENOMEM;
		goto err_out_exit_slic_probe;
	}
	DBG_MSG("alloc_etherdev for slic netdev[%p]\n", netdev);

	SET_NETDEV_DEV(netdev, &pcidev->dev);

	pci_set_drvdata(pcidev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pcidev = pcidev;

	mmio_start = pci_resource_start(pcidev, 0);
	mmio_len = pci_resource_len(pcidev, 0);

	DBG_MSG("slicoss: call ioremap(mmio_start[%lx], mmio_len[%lx])\n",
		mmio_start, mmio_len);

/*  memmapped_ioaddr =  (ulong32)ioremap_nocache(mmio_start, mmio_len);*/
	memmapped_ioaddr = ioremap(mmio_start, mmio_len);
	DBG_MSG("slicoss: %s MEMMAPPED_IOADDR [%p]\n", __func__,
		memmapped_ioaddr);
	if (!memmapped_ioaddr) {
		DBG_ERROR("%s cannot remap MMIO region %lx @ %lx\n",
			  __func__, mmio_len, mmio_start);
		goto err_out_free_mmio_region;
	}

	DBG_MSG
	    ("slicoss: %s found Alacritech SLICOSS PCI, MMIO at %p, "\
	    "start[%lx] len[%lx], IRQ %d.\n",
	     __func__, memmapped_ioaddr, mmio_start, mmio_len, pcidev->irq);

	slic_config_pci(pcidev);

	slic_init_driver();

	slic_init_adapter(netdev,
			  pcidev, pci_tbl_entry, memmapped_ioaddr, cards_found);

	status = slic_card_locate(adapter);
	if (status) {
		DBG_ERROR("%s cannot locate card\n", __func__);
		goto err_out_free_mmio_region;
	}

	card = adapter->card;

	if (!adapter->allocated) {
		card->adapters_allocated++;
		adapter->allocated = 1;
	}

	DBG_MSG("slicoss: %s    card:             %p\n", __func__,
		adapter->card);
	DBG_MSG("slicoss: %s    card->adapter[%d] == [%p]\n", __func__,
		(uint) adapter->port, adapter);
	DBG_MSG("slicoss: %s    card->adapters_allocated [%d]\n", __func__,
		card->adapters_allocated);
	DBG_MSG("slicoss: %s    card->adapters_activated [%d]\n", __func__,
		card->adapters_activated);

	status = slic_card_init(card, adapter);

	if (status != STATUS_SUCCESS) {
		card->state = CARD_FAIL;
		adapter->state = ADAPT_FAIL;
		adapter->linkstate = LINK_DOWN;
		DBG_ERROR("slic_card_init FAILED status[%x]\n", status);
	} else {
		slic_adapter_set_hwaddr(adapter);
	}

	netdev->base_addr = (unsigned long)adapter->memorybase;
	netdev->irq = adapter->irq;
	netdev->open = slic_entry_open;
	netdev->stop = slic_entry_halt;
	netdev->hard_start_xmit = slic_xmit_start;
	netdev->do_ioctl = slic_ioctl;
	netdev->set_mac_address = slic_mac_set_address;
#if SLIC_GET_STATS_ENABLED
	netdev->get_stats = slic_get_stats;
#endif
	netdev->set_multicast_list = slic_mcast_set_list;

	slic_debug_adapter_create(adapter);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err) {
		DBG_ERROR("Cannot register net device, aborting.\n");
		goto err_out_unmap;
	}

	DBG_MSG
	    ("slicoss: addr 0x%lx, irq %d, MAC addr "\
	     "%02X:%02X:%02X:%02X:%02X:%02X\n",
	     mmio_start, /*pci_resource_start(pcidev, 0), */ pcidev->irq,
	     netdev->dev_addr[0], netdev->dev_addr[1], netdev->dev_addr[2],
	     netdev->dev_addr[3], netdev->dev_addr[4], netdev->dev_addr[5]);

	cards_found++;
	DBG_MSG("slicoss: %s EXIT status[%x] jiffies[%lx] cpu %d\n",
		__func__, status, jiffies, smp_processor_id());

	return status;

err_out_unmap:
	iounmap(memmapped_ioaddr);

err_out_free_mmio_region:
	release_mem_region(mmio_start, mmio_len);

err_out_exit_slic_probe:
	DBG_ERROR("%s EXIT jiffies[%lx] cpu %d\n", __func__, jiffies,
		  smp_processor_id());

	return -ENODEV;
}

int slic_entry_open(struct net_device *dev)
{
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	p_sliccard_t card = adapter->card;
	ulong32 locked = 0;
	int status;

	ASSERT(adapter);
	ASSERT(card);
	DBG_MSG
	    ("slicoss: %s adapter->activated[%d] card->adapters[%x] "\
	     "allocd[%x]\n", __func__, adapter->activated,
	     card->adapters_activated,
	     card->adapters_allocated);
	DBG_MSG
	    ("slicoss: %s (%s): [jiffies[%lx] cpu %d] dev[%p] adapt[%p] "\
	     "port[%d] card[%p]\n",
	     __func__, adapter->netdev->name, jiffies, smp_processor_id(),
	     adapter->netdev, adapter, adapter->port, card);

	netif_stop_queue(adapter->netdev);

	SLIC_ACQUIRE_IRQ_SPINLOCK(slic_global.driver_lock);
	locked = 1;
	if (!adapter->activated) {
		card->adapters_activated++;
		slic_global.num_slic_ports_active++;
		adapter->activated = 1;
	}
	status = slic_if_init(adapter);

	if (status != STATUS_SUCCESS) {
		if (adapter->activated) {
			card->adapters_activated--;
			slic_global.num_slic_ports_active--;
			adapter->activated = 0;
		}
		if (locked) {
			SLIC_RELEASE_IRQ_SPINLOCK(slic_global.driver_lock);
			locked = 0;
		}
		return status;
	}
	DBG_MSG("slicoss: %s set card->master[%p] adapter[%p]\n", __func__,
		card->master, adapter);
	if (!card->master)
		card->master = adapter;
#if SLIC_DUMP_ENABLED
	if (!(card->dumpthread_running))
		init_waitqueue_head(&card->dump_wq);
#endif

	if (locked) {
		SLIC_RELEASE_IRQ_SPINLOCK(slic_global.driver_lock);
		locked = 0;
	}
#if SLIC_DUMP_ENABLED
	if (!(card->dumpthread_running)) {
		DBG_MSG("attempt to initialize dump thread\n");
		status = slic_init_dump_thread(card);
		/*
		Even if the dump thread fails, we will continue at this point
		*/
	}
#endif

	return STATUS_SUCCESS;
}

void __devexit slic_entry_remove(struct pci_dev *pcidev)
{
	struct net_device *dev = pci_get_drvdata(pcidev);
	ulong32 mmio_start = 0;
	uint mmio_len = 0;
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	p_sliccard_t card;

	ASSERT(adapter);
	DBG_MSG("slicoss: %s ENTER dev[%p] adapter[%p]\n", __func__, dev,
		adapter);
	slic_adapter_freeresources(adapter);
	slic_unmap_mmio_space(adapter);
	DBG_MSG("slicoss: %s unregister_netdev\n", __func__);
	unregister_netdev(dev);

	mmio_start = pci_resource_start(pcidev, 0);
	mmio_len = pci_resource_len(pcidev, 0);

	DBG_MSG("slicoss: %s rel_region(0) start[%x] len[%x]\n", __func__,
		mmio_start, mmio_len);
	release_mem_region(mmio_start, mmio_len);

	DBG_MSG("slicoss: %s iounmap dev->base_addr[%x]\n", __func__,
		(uint) dev->base_addr);
	iounmap((void __iomem *)dev->base_addr);
	ASSERT(adapter->card);
	card = adapter->card;
	ASSERT(card->adapters_allocated);
	card->adapters_allocated--;
	adapter->allocated = 0;
	DBG_MSG
	    ("slicoss: %s init[%x] alloc[%x] card[%p] adapter[%p]\n",
	     __func__, card->adapters_activated, card->adapters_allocated,
	     card, adapter);
	if (!card->adapters_allocated) {
		p_sliccard_t curr_card = slic_global.slic_card;
		if (curr_card == card) {
			slic_global.slic_card = card->next;
		} else {
			while (curr_card->next != card)
				curr_card = curr_card->next;
			ASSERT(curr_card);
			curr_card->next = card->next;
		}
		ASSERT(slic_global.num_slic_cards);
		slic_global.num_slic_cards--;
		slic_card_cleanup(card);
	}
	DBG_MSG("slicoss: %s deallocate device\n", __func__);
	SLIC_DEALLOCATE_MEM(dev);
	DBG_MSG("slicoss: %s EXIT\n", __func__);
}

int slic_entry_halt(struct net_device *dev)
{
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	p_sliccard_t card = adapter->card;
	p_slic_regs_t slic_regs = adapter->slic_regs;

	SLIC_ACQUIRE_IRQ_SPINLOCK(slic_global.driver_lock);
	ASSERT(card);
	DBG_MSG("slicoss: %s (%s) ENTER\n", __func__, dev->name);
	DBG_MSG("slicoss: %s (%s) actvtd[%d] alloc[%d] state[%x] adapt[%p]\n",
		__func__, dev->name, card->adapters_activated,
		card->adapters_allocated, card->state, adapter);
	slic_if_stop_queue(adapter);
	adapter->state = ADAPT_DOWN;
	adapter->linkstate = LINK_DOWN;
	adapter->upr_list = NULL;
	adapter->upr_busy = 0;
	adapter->devflags_prev = 0;
	DBG_MSG("slicoss: %s (%s) set adapter[%p] state to ADAPT_DOWN(%d)\n",
		__func__, dev->name, adapter, adapter->state);
	ASSERT(card->adapter[adapter->cardindex] == adapter);
	WRITE_REG(slic_regs->slic_icr, ICR_INT_OFF, FLUSH);
	adapter->all_reg_writes++;
	adapter->icr_reg_writes++;
	slic_config_clear(adapter);
	DBG_MSG("slicoss: %s (%s) dev[%p] adapt[%p] card[%p]\n",
		__func__, dev->name, dev, adapter, card);
	if (adapter->activated) {
		card->adapters_activated--;
		slic_global.num_slic_ports_active--;
		adapter->activated = 0;
	}
#ifdef AUTOMATIC_RESET
	WRITE_REG(slic_regs->slic_reset_iface, 0, FLUSH);
#endif
	/*
	 *  Reset the adapter's rsp, cmd, and rcv queues
	 */
	slic_cmdq_reset(adapter);
	slic_rspqueue_reset(adapter);
	slic_rcvqueue_reset(adapter);

#ifdef AUTOMATIC_RESET
	if (!card->adapters_activated) {

#if SLIC_DUMP_ENABLED
		if (card->dumpthread_running) {
			uint status;
			DBG_MSG("attempt to terminate dump thread pid[%x]\n",
				card->dump_task_id);
			status = kill_proc(card->dump_task_id->pid, SIGKILL, 1);

			if (!status) {
				int count = 10 * 100;
				while (card->dumpthread_running && --count) {
					current->state = TASK_INTERRUPTIBLE;
					schedule_timeout(1);
				}

				if (!count) {
					DBG_MSG
					    ("slicmon thread cleanup FAILED \
					     pid[%x]\n",
					     card->dump_task_id->pid);
				}
			}
		}
#endif
		DBG_MSG("slicoss: %s (%s) initiate CARD_HALT\n", __func__,
			dev->name);

		slic_card_init(card, adapter);
	}
#endif

	DBG_MSG("slicoss: %s (%s) EXIT\n", __func__, dev->name);
	DBG_MSG("slicoss: %s EXIT\n", __func__);
	SLIC_RELEASE_IRQ_SPINLOCK(slic_global.driver_lock);
	return STATUS_SUCCESS;
}

int slic_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	ASSERT(rq);
/*
      DBG_MSG("slicoss: %s cmd[%x] rq[%p] dev[%p]\n", __func__, cmd, rq, dev);
*/
	switch (cmd) {
	case SIOCSLICSETINTAGG:
		{
			p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
			ulong32 data[7];
			ulong32 intagg;

			if (copy_from_user(data, rq->ifr_data, 28)) {
				DBG_ERROR
				    ("copy_from_user FAILED getting initial \
				     params\n");
				return -EFAULT;
			}
			intagg = data[0];
			printk(KERN_EMERG
			       "%s: set interrupt aggregation to %d\n",
			       __func__, intagg);
			slic_intagg_set(adapter, intagg);
			return 0;
		}
#ifdef SLIC_USER_REQUEST_DUMP_ENABLED
	case SIOCSLICDUMPCARD:
		{
			p_adapter_t adapter = (p_adapter_t) dev->priv;
			p_sliccard_t card;

			ASSERT(adapter);
			ASSERT(adapter->card)
			card = adapter->card;

			DBG_IOCTL("slic_ioctl  SIOCSLIC_DUMP_CARD\n");

			if (card->dump_requested == SLIC_DUMP_DONE) {
				printk(SLICLEVEL
				       "SLIC Card dump to be overwritten\n");
				card->dump_requested = SLIC_DUMP_REQUESTED;
			} else if ((card->dump_requested == SLIC_DUMP_REQUESTED)
				   || (card->dump_requested ==
				       SLIC_DUMP_IN_PROGRESS)) {
				printk(SLICLEVEL
				       "SLIC Card dump Requested but already \
					in progress... ignore\n");
			} else {
				printk(SLICLEVEL
				       "SLIC Card #%d Dump Requested\n",
				       card->cardnum);
				card->dump_requested = SLIC_DUMP_REQUESTED;
			}
			return 0;
		}
#endif

#ifdef SLIC_TRACE_DUMP_ENABLED
	case SIOCSLICTRACEDUMP:
		{
			ulong data[7];
			ulong value;

			DBG_IOCTL("slic_ioctl  SIOCSLIC_TRACE_DUMP\n");

			if (copy_from_user(data, rq->ifr_data, 28)) {
				PRINT_ERROR
				    ("slic: copy_from_user FAILED getting \
				     initial simba param\n");
				return -EFAULT;
			}

			value = data[0];
			if (tracemon_request == SLIC_DUMP_DONE) {
				PRINT_ERROR
				    ("ATK Diagnostic Trace Dump Requested\n");
				tracemon_request = SLIC_DUMP_REQUESTED;
				tracemon_request_type = value;
				tracemon_timestamp = jiffies;
			} else if ((tracemon_request == SLIC_DUMP_REQUESTED) ||
				   (tracemon_request ==
				    SLIC_DUMP_IN_PROGRESS)) {
				PRINT_ERROR
				    ("ATK Diagnostic Trace Dump Requested but \
				     already in progress... ignore\n");
			} else {
				PRINT_ERROR
				    ("ATK Diagnostic Trace Dump Requested\n");
				tracemon_request = SLIC_DUMP_REQUESTED;
				tracemon_request_type = value;
				tracemon_timestamp = jiffies;
			}
			return 0;
		}
#endif
#if SLIC_ETHTOOL_SUPPORT
	case SIOCETHTOOL:
		{
			p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
			struct ethtool_cmd data;
			struct ethtool_cmd ecmd;

			ASSERT(adapter);
/*                      DBG_MSG("slicoss: %s SIOCETHTOOL\n", __func__); */
			if (copy_from_user(&ecmd, rq->ifr_data, sizeof(ecmd)))
				return -EFAULT;

			if (ecmd.cmd == ETHTOOL_GSET) {
				data.supported =
				    (SUPPORTED_10baseT_Half |
				     SUPPORTED_10baseT_Full |
				     SUPPORTED_100baseT_Half |
				     SUPPORTED_100baseT_Full |
				     SUPPORTED_Autoneg | SUPPORTED_MII);
				data.port = PORT_MII;
				data.transceiver = XCVR_INTERNAL;
				data.phy_address = 0;
				if (adapter->linkspeed == LINK_100MB)
					data.speed = SPEED_100;
				else if (adapter->linkspeed == LINK_10MB)
					data.speed = SPEED_10;
				else
					data.speed = 0;

				if (adapter->linkduplex == LINK_FULLD)
					data.duplex = DUPLEX_FULL;
				else
					data.duplex = DUPLEX_HALF;

				data.autoneg = AUTONEG_ENABLE;
				data.maxtxpkt = 1;
				data.maxrxpkt = 1;
				if (copy_to_user
				    (rq->ifr_data, &data, sizeof(data)))
					return -EFAULT;

			} else if (ecmd.cmd == ETHTOOL_SSET) {
				if (!capable(CAP_NET_ADMIN))
					return -EPERM;

				if (adapter->linkspeed == LINK_100MB)
					data.speed = SPEED_100;
				else if (adapter->linkspeed == LINK_10MB)
					data.speed = SPEED_10;
				else
					data.speed = 0;

				if (adapter->linkduplex == LINK_FULLD)
					data.duplex = DUPLEX_FULL;
				else
					data.duplex = DUPLEX_HALF;

				data.autoneg = AUTONEG_ENABLE;
				data.maxtxpkt = 1;
				data.maxrxpkt = 1;
				if ((ecmd.speed != data.speed) ||
				    (ecmd.duplex != data.duplex)) {
					ulong32 speed;
					ulong32 duplex;

					if (ecmd.speed == SPEED_10) {
						speed = 0;
						SLIC_DISPLAY
						    ("%s: slic ETHTOOL set \
						     link speed==10MB",
						     dev->name);
					} else {
						speed = PCR_SPEED_100;
						SLIC_DISPLAY
						    ("%s: slic ETHTOOL set \
						    link speed==100MB",
						     dev->name);
					}
					if (ecmd.duplex == DUPLEX_FULL) {
						duplex = PCR_DUPLEX_FULL;
						SLIC_DISPLAY
						    (": duplex==FULL\n");
					} else {
						duplex = 0;
						SLIC_DISPLAY
						    (": duplex==HALF\n");
					}
					slic_link_config(adapter,
							 speed, duplex);
					slic_link_event_handler(adapter);
				}
			}
			return 0;
		}
#endif
	default:
/*              DBG_MSG("slicoss: %s UNSUPPORTED[%x]\n", __func__, cmd); */
		return -EOPNOTSUPP;
	}
}

#define  XMIT_FAIL_LINK_STATE               1
#define  XMIT_FAIL_ZERO_LENGTH              2
#define  XMIT_FAIL_HOSTCMD_FAIL             3

static void slic_xmit_build_request(p_adapter_t adapter,
			     p_slic_hostcmd_t hcmd, struct sk_buff *skb)
{
	p_slic_host64_cmd_t ihcmd;
	ulong phys_addr;

	ihcmd = &hcmd->cmd64;

	ihcmd->flags = (adapter->port << IHFLG_IFSHFT);
	ihcmd->command = IHCMD_XMT_REQ;
	ihcmd->u.slic_buffers.totlen = skb->len;
	phys_addr = SLIC_GET_DMA_ADDRESS_WRITE(adapter, skb->data, skb->len);
	ihcmd->u.slic_buffers.bufs[0].paddrl = SLIC_GET_ADDR_LOW(phys_addr);
	ihcmd->u.slic_buffers.bufs[0].paddrh = SLIC_GET_ADDR_HIGH(phys_addr);
	ihcmd->u.slic_buffers.bufs[0].length = skb->len;
#if defined(CONFIG_X86_64)
	hcmd->cmdsize = (ulong32) ((((ulong64)&ihcmd->u.slic_buffers.bufs[1] -
				     (ulong64) hcmd) + 31) >> 5);
#elif defined(CONFIG_X86)
	hcmd->cmdsize = ((((ulong32) &ihcmd->u.slic_buffers.bufs[1] -
			   (ulong32) hcmd) + 31) >> 5);
#else
	Stop Compilation;
#endif
}

#define NORMAL_ETHFRAME     0

int slic_xmit_start(struct sk_buff *skb, struct net_device *dev)
{
	p_sliccard_t card;
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	p_slic_hostcmd_t hcmd = NULL;
	ulong32 status = 0;
	ulong32 skbtype = NORMAL_ETHFRAME;
	pvoid offloadcmd = NULL;

	card = adapter->card;
	ASSERT(card);
/*
    DBG_ERROR("xmit_start (%s) ENTER skb[%p] len[%d] linkstate[%x] state[%x]\n",
	adapter->netdev->name, skb, skb->len, adapter->linkstate,
	 adapter->state);
*/
	if ((adapter->linkstate != LINK_UP) ||
	    (adapter->state != ADAPT_UP) || (card->state != CARD_UP)) {
		status = XMIT_FAIL_LINK_STATE;
		goto xmit_fail;

	} else if (skb->len == 0) {
		status = XMIT_FAIL_ZERO_LENGTH;
		goto xmit_fail;
	}

	if (skbtype == NORMAL_ETHFRAME) {
		hcmd = slic_cmdq_getfree(adapter);
		if (!hcmd) {
			adapter->xmitq_full = 1;
			status = XMIT_FAIL_HOSTCMD_FAIL;
			goto xmit_fail;
		}
		ASSERT(hcmd->pslic_handle);
		ASSERT(hcmd->cmd64.hosthandle ==
		       hcmd->pslic_handle->token.handle_token);
		hcmd->skb = skb;
		hcmd->busy = 1;
		hcmd->type = SLIC_CMD_DUMB;
		if (skbtype == NORMAL_ETHFRAME)
			slic_xmit_build_request(adapter, hcmd, skb);
	}
	adapter->stats.tx_packets++;
	adapter->stats.tx_bytes += skb->len;

#ifdef DEBUG_DUMP
	if (adapter->kill_card) {
		p_slic_host64_cmd_t ihcmd;

		ihcmd = &hcmd->cmd64;

		ihcmd->flags |= 0x40;
		adapter->kill_card = 0;	/* only do this once */
	}
#endif
	if (hcmd->paddrh == 0) {
		WRITE_REG(adapter->slic_regs->slic_cbar,
			  (hcmd->paddrl | hcmd->cmdsize), DONT_FLUSH);
	} else {
		WRITE_REG64(adapter,
			    adapter->slic_regs->slic_cbar64,
			    (hcmd->paddrl | hcmd->cmdsize),
			    adapter->slic_regs->slic_addr_upper,
			    hcmd->paddrh, DONT_FLUSH);
	}
xmit_done:
	return 0;
xmit_fail:
	slic_xmit_fail(adapter, skb, offloadcmd, skbtype, status);
	goto xmit_done;
}

void slic_xmit_fail(p_adapter_t adapter,
		    struct sk_buff *skb,
		    pvoid cmd, ulong32 skbtype, ulong32 status)
{
	if (adapter->xmitq_full)
		slic_if_stop_queue(adapter);
	if ((cmd == NULL) && (status <= XMIT_FAIL_HOSTCMD_FAIL)) {
		switch (status) {
		case XMIT_FAIL_LINK_STATE:
			DBG_ERROR
			    ("(%s) reject xmit skb[%p: %x] linkstate[%s] \
			     adapter[%s:%d] card[%s:%d]\n",
			     adapter->netdev->name, skb, skb->pkt_type,
			     SLIC_LINKSTATE(adapter->linkstate),
			     SLIC_ADAPTER_STATE(adapter->state), adapter->state,
			     SLIC_CARD_STATE(adapter->card->state),
			     adapter->card->state);
			break;
		case XMIT_FAIL_ZERO_LENGTH:
			DBG_ERROR
			    ("xmit_start skb->len == 0 skb[%p] type[%x]!!!! \n",
			     skb, skb->pkt_type);
			break;
		case XMIT_FAIL_HOSTCMD_FAIL:
			DBG_ERROR
			    ("xmit_start skb[%p] type[%x] No host commands \
			     available !!!! \n",
			     skb, skb->pkt_type);
			break;
		default:
			ASSERT(0);
		}
	}
	dev_kfree_skb(skb);
	adapter->stats.tx_dropped++;
}

void slic_xmit_timeout(struct net_device *dev)
{
	p_sliccard_t card;
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	ulong32 i;

	ASSERT(adapter);
	card = adapter->card;
	ASSERT(card);
	for (i = 0; i < card->card_size; i++) {
		if (card->adapter[i])
			slic_if_stop_queue(card->adapter[i]);
	}
	if (!card->reset_in_progress) {
		DBG_ERROR
		    ("%s card[%p] state[%x] adapter[%p] port[%d] state[%x]\n",
		     __func__, card, card->state, adapter, adapter->port,
		     adapter->state);
		slic_card_reset(adapter);
	}
}

void slic_rcv_handle_error(p_adapter_t adapter, p_slic_rcvbuf_t rcvbuf)
{
	p_slic_hddr_wds hdr = (p_slic_hddr_wds) rcvbuf->data;

	if (adapter->devid != SLIC_1GB_DEVICE_ID) {
		if (hdr->frame_status14 & VRHSTAT_802OE)
			adapter->if_events.oflow802++;
		if (hdr->frame_status14 & VRHSTAT_TPOFLO)
			adapter->if_events.Tprtoflow++;
		if (hdr->frame_status_b14 & VRHSTATB_802UE)
			adapter->if_events.uflow802++;
		if (hdr->frame_status_b14 & VRHSTATB_RCVE) {
			adapter->if_events.rcvearly++;
			adapter->stats.rx_fifo_errors++;
		}
		if (hdr->frame_status_b14 & VRHSTATB_BUFF) {
			adapter->if_events.Bufov++;
			adapter->stats.rx_over_errors++;
		}
		if (hdr->frame_status_b14 & VRHSTATB_CARRE) {
			adapter->if_events.Carre++;
			adapter->stats.tx_carrier_errors++;
		}
		if (hdr->frame_status_b14 & VRHSTATB_LONGE)
			adapter->if_events.Longe++;
		if (hdr->frame_status_b14 & VRHSTATB_PREA)
			adapter->if_events.Invp++;
		if (hdr->frame_status_b14 & VRHSTATB_CRC) {
			adapter->if_events.Crc++;
			adapter->stats.rx_crc_errors++;
		}
		if (hdr->frame_status_b14 & VRHSTATB_DRBL)
			adapter->if_events.Drbl++;
		if (hdr->frame_status_b14 & VRHSTATB_CODE)
			adapter->if_events.Code++;
		if (hdr->frame_status_b14 & VRHSTATB_TPCSUM)
			adapter->if_events.TpCsum++;
		if (hdr->frame_status_b14 & VRHSTATB_TPHLEN)
			adapter->if_events.TpHlen++;
		if (hdr->frame_status_b14 & VRHSTATB_IPCSUM)
			adapter->if_events.IpCsum++;
		if (hdr->frame_status_b14 & VRHSTATB_IPLERR)
			adapter->if_events.IpLen++;
		if (hdr->frame_status_b14 & VRHSTATB_IPHERR)
			adapter->if_events.IpHlen++;
	} else {
		if (hdr->frame_statusGB & VGBSTAT_XPERR) {
			ulong32 xerr = hdr->frame_statusGB >> VGBSTAT_XERRSHFT;

			if (xerr == VGBSTAT_XCSERR)
				adapter->if_events.TpCsum++;
			if (xerr == VGBSTAT_XUFLOW)
				adapter->if_events.Tprtoflow++;
			if (xerr == VGBSTAT_XHLEN)
				adapter->if_events.TpHlen++;
		}
		if (hdr->frame_statusGB & VGBSTAT_NETERR) {
			ulong32 nerr =
			    (hdr->
			     frame_statusGB >> VGBSTAT_NERRSHFT) &
			    VGBSTAT_NERRMSK;
			if (nerr == VGBSTAT_NCSERR)
				adapter->if_events.IpCsum++;
			if (nerr == VGBSTAT_NUFLOW)
				adapter->if_events.IpLen++;
			if (nerr == VGBSTAT_NHLEN)
				adapter->if_events.IpHlen++;
		}
		if (hdr->frame_statusGB & VGBSTAT_LNKERR) {
			ulong32 lerr = hdr->frame_statusGB & VGBSTAT_LERRMSK;

			if (lerr == VGBSTAT_LDEARLY)
				adapter->if_events.rcvearly++;
			if (lerr == VGBSTAT_LBOFLO)
				adapter->if_events.Bufov++;
			if (lerr == VGBSTAT_LCODERR)
				adapter->if_events.Code++;
			if (lerr == VGBSTAT_LDBLNBL)
				adapter->if_events.Drbl++;
			if (lerr == VGBSTAT_LCRCERR)
				adapter->if_events.Crc++;
			if (lerr == VGBSTAT_LOFLO)
				adapter->if_events.oflow802++;
			if (lerr == VGBSTAT_LUFLO)
				adapter->if_events.uflow802++;
		}
	}
	return;
}

#define TCP_OFFLOAD_FRAME_PUSHFLAG  0x10000000
#define M_FAST_PATH                 0x0040

void slic_rcv_handler(p_adapter_t adapter)
{
	struct sk_buff *skb;
	p_slic_rcvbuf_t rcvbuf;
	ulong32 frames = 0;

	while ((skb = slic_rcvqueue_getnext(adapter))) {
		ulong32 rx_bytes;

		ASSERT(skb->head);
		rcvbuf = (p_slic_rcvbuf_t) skb->head;
		adapter->card->events++;
		if (rcvbuf->status & IRHDDR_ERR) {
			adapter->rx_errors++;
			slic_rcv_handle_error(adapter, rcvbuf);
			slic_rcvqueue_reinsert(adapter, skb);
			continue;
		}

		if (!slic_mac_filter(adapter, (p_ether_header) rcvbuf->data)) {
#if 0
			DBG_MSG
			    ("slicoss: %s (%s) drop frame due to mac filter\n",
			     __func__, adapter->netdev->name);
#endif
			slic_rcvqueue_reinsert(adapter, skb);
			continue;
		}
		skb_pull(skb, SLIC_RCVBUF_HEADSIZE);
		rx_bytes = (rcvbuf->length & IRHDDR_FLEN_MSK);
		skb_put(skb, rx_bytes);
		adapter->stats.rx_packets++;
		adapter->stats.rx_bytes += rx_bytes;
#if SLIC_OFFLOAD_IP_CHECKSUM
		skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif

		skb->dev = adapter->netdev;
		skb->protocol = eth_type_trans(skb, skb->dev);
		netif_rx(skb);

		++frames;
#if SLIC_INTERRUPT_PROCESS_LIMIT
		if (frames >= SLIC_RCVQ_MAX_PROCESS_ISR) {
			adapter->rcv_interrupt_yields++;
			break;
		}
#endif
	}
	adapter->max_isr_rcvs = max(adapter->max_isr_rcvs, frames);
}

void slic_xmit_complete(p_adapter_t adapter)
{
	p_slic_hostcmd_t hcmd;
	p_slic_rspbuf_t rspbuf;
	ulong32 frames = 0;
	slic_handle_word_t slic_handle_word;

	do {
		rspbuf = slic_rspqueue_getnext(adapter);
		if (!rspbuf)
			break;
		adapter->xmit_completes++;
		adapter->card->events++;
		/*
		 Get the complete host command buffer
		*/
		slic_handle_word.handle_token = rspbuf->hosthandle;
		ASSERT(slic_handle_word.handle_index);
		ASSERT(slic_handle_word.handle_index <= SLIC_CMDQ_MAXCMDS);
		hcmd =
		    (p_slic_hostcmd_t) adapter->slic_handles[slic_handle_word.
							     handle_index].
		    address;
/*      hcmd = (p_slic_hostcmd_t) rspbuf->hosthandle; */
		ASSERT(hcmd);
		ASSERT(hcmd->pslic_handle ==
		       &adapter->slic_handles[slic_handle_word.handle_index]);
/*
      DBG_ERROR("xmit_complete (%s)   hcmd[%p]  hosthandle[%x]\n",
		adapter->netdev->name, hcmd, hcmd->cmd64.hosthandle);
      DBG_ERROR("    skb[%p] len %d  hcmdtype[%x]\n", hcmd->skb,
		hcmd->skb->len, hcmd->type);
*/
		if (hcmd->type == SLIC_CMD_DUMB) {
			if (hcmd->skb)
				dev_kfree_skb_irq(hcmd->skb);
			slic_cmdq_putdone_irq(adapter, hcmd);
		}
		rspbuf->status = 0;
		rspbuf->hosthandle = 0;
		frames++;
	} while (1);
	adapter->max_isr_xmits = max(adapter->max_isr_xmits, frames);
}

static irqreturn_t slic_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	ulong32 isr;

	if ((adapter->pshmem) && (adapter->pshmem->isr)) {
		WRITE_REG(adapter->slic_regs->slic_icr, ICR_INT_MASK, FLUSH);
		isr = adapter->isrcopy = adapter->pshmem->isr;
		adapter->pshmem->isr = 0;
		adapter->num_isrs++;
		switch (adapter->card->state) {
		case CARD_UP:
			if (isr & ~ISR_IO) {
				if (isr & ISR_ERR) {
					adapter->error_interrupts++;
					if (isr & ISR_RMISS) {
						int count;
						int pre_count;
						int errors;

						p_slic_rcvqueue_t rcvq =
						    &adapter->rcvqueue;

						adapter->
						    error_rmiss_interrupts++;
						if (!rcvq->errors)
							rcv_count = rcvq->count;
						pre_count = rcvq->count;
						errors = rcvq->errors;

						while (rcvq->count <
						       SLIC_RCVQ_FILLTHRESH) {
							count =
							    slic_rcvqueue_fill
							    (adapter);
							if (!count)
								break;
						}
						DBG_MSG
						    ("(%s): [%x] ISR_RMISS \
						     initial[%x] pre[%x] \
						     errors[%x] \
						     post_count[%x]\n",
						     adapter->netdev->name,
						     isr, rcv_count, pre_count,
						     errors, rcvq->count);
					} else if (isr & ISR_XDROP) {
						DBG_ERROR
						    ("isr & ISR_ERR [%x] \
						     ISR_XDROP \n",
						     isr);
					} else {
						DBG_ERROR
						    ("isr & ISR_ERR [%x]\n",
						     isr);
					}
				}

				if (isr & ISR_LEVENT) {
					/*DBG_MSG("%s (%s)  ISR_LEVENT \n",
					   __func__, adapter->netdev->name);*/
					adapter->linkevent_interrupts++;
					slic_link_event_handler(adapter);
				}

				if ((isr & ISR_UPC) ||
				    (isr & ISR_UPCERR) || (isr & ISR_UPCBSY)) {
					adapter->upr_interrupts++;
					slic_upr_request_complete(adapter, isr);
				}
			}

			if (isr & ISR_RCV) {
				adapter->rcv_interrupts++;
				slic_rcv_handler(adapter);
			}

			if (isr & ISR_CMD) {
				adapter->xmit_interrupts++;
				slic_xmit_complete(adapter);
			}
			break;

		case CARD_DOWN:
			if ((isr & ISR_UPC) ||
			    (isr & ISR_UPCERR) || (isr & ISR_UPCBSY)) {
				adapter->upr_interrupts++;
				slic_upr_request_complete(adapter, isr);
			}
			break;

		default:
			break;
		}

		adapter->isrcopy = 0;
		adapter->all_reg_writes += 2;
		adapter->isr_reg_writes++;
		WRITE_REG(adapter->slic_regs->slic_isr, 0, FLUSH);
	} else {
		adapter->false_interrupts++;
	}
	return IRQ_HANDLED;
}

/*
 * slic_link_event_handler -
 *
 * Initiate a link configuration sequence.  The link configuration begins
 * by issuing a READ_LINK_STATUS command to the Utility Processor on the
 * SLIC.  Since the command finishes asynchronously, the slic_upr_comlete
 * routine will follow it up witha UP configuration write command, which
 * will also complete asynchronously.
 *
 */
void slic_link_event_handler(p_adapter_t adapter)
{
	int status;
	p_slic_shmem_t pshmem;

	if (adapter->state != ADAPT_UP) {
		/* Adapter is not operational.  Ignore.  */
		return;
	}

	pshmem = (p_slic_shmem_t) adapter->phys_shmem;

#if defined(CONFIG_X86_64)
/*
    DBG_MSG("slic_event_handler  pshmem->linkstatus[%x]  pshmem[%p]\n   \
	&linkstatus[%p] &isr[%p]\n", adapter->pshmem->linkstatus, pshmem,
	&pshmem->linkstatus, &pshmem->isr);
*/
	status = slic_upr_request(adapter,
				  SLIC_UPR_RLSR,
				  SLIC_GET_ADDR_LOW(&pshmem->linkstatus),
				  SLIC_GET_ADDR_HIGH(&pshmem->linkstatus),
				  0, 0);
#elif defined(CONFIG_X86)
	status = slic_upr_request(adapter, SLIC_UPR_RLSR,
		(ulong32) &pshmem->linkstatus,	/* no 4GB wrap guaranteed */
				  0, 0, 0);
#else
	Stop compilation;
#endif
	ASSERT((status == STATUS_SUCCESS) || (status == STATUS_PENDING));
}

void slic_init_cleanup(p_adapter_t adapter)
{
	DBG_MSG("slicoss: %s ENTER adapter[%p] ", __func__, adapter);
	if (adapter->intrregistered) {
		DBG_MSG("FREE_IRQ ");
		adapter->intrregistered = 0;
		free_irq(adapter->netdev->irq, adapter->netdev);

	}
	if (adapter->pshmem) {
		DBG_MSG("FREE_SHMEM ");
		DBG_MSG("adapter[%p] port %d pshmem[%p] FreeShmem ",
			adapter, adapter->port, (pvoid) adapter->pshmem);
		pci_free_consistent(adapter->pcidev,
				    sizeof(slic_shmem_t),
				    adapter->pshmem, adapter->phys_shmem);
		adapter->pshmem = NULL;
		adapter->phys_shmem = (dma_addr_t) NULL;
	}
#if SLIC_GET_STATS_TIMER_ENABLED
	if (adapter->statstimerset) {
		DBG_MSG("statstimer ");
		adapter->statstimerset = 0;
		del_timer(&adapter->statstimer);
	}
#endif
#if !SLIC_DUMP_ENABLED && SLIC_PING_TIMER_ENABLED
/*#if SLIC_DUMP_ENABLED && SLIC_PING_TIMER_ENABLED*/
	if (adapter->pingtimerset) {
		DBG_MSG("pingtimer ");
		adapter->pingtimerset = 0;
		del_timer(&adapter->pingtimer);
	}
#endif
	slic_rspqueue_free(adapter);
	slic_cmdq_free(adapter);
	slic_rcvqueue_free(adapter);

	DBG_MSG("\n");
}

#if SLIC_GET_STATS_ENABLED
struct net_device_stats *slic_get_stats(struct net_device *dev)
{
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	struct net_device_stats *stats;

	ASSERT(adapter);
	stats = &adapter->stats;
	stats->collisions = adapter->slic_stats.iface.xmit_collisions;
	stats->rx_errors = adapter->slic_stats.iface.rcv_errors;
	stats->tx_errors = adapter->slic_stats.iface.xmt_errors;
	stats->rx_missed_errors = adapter->slic_stats.iface.rcv_discards;
	stats->tx_heartbeat_errors = 0;
	stats->tx_aborted_errors = 0;
	stats->tx_window_errors = 0;
	stats->tx_fifo_errors = 0;
	stats->rx_frame_errors = 0;
	stats->rx_length_errors = 0;
	return &adapter->stats;
}
#endif

/*
 *  Allocate a mcast_address structure to hold the multicast address.
 *  Link it in.
 */
int slic_mcast_add_list(p_adapter_t adapter, pchar address)
{
	p_mcast_address_t mcaddr, mlist;
	boolean equaladdr;

	/* Check to see if it already exists */
	mlist = adapter->mcastaddrs;
	while (mlist) {
		ETHER_EQ_ADDR(mlist->address, address, equaladdr);
		if (equaladdr)
			return STATUS_SUCCESS;
		mlist = mlist->next;
	}

	/* Doesn't already exist.  Allocate a structure to hold it */
	mcaddr = SLIC_ALLOCATE_MEM(sizeof(mcast_address_t), GFP_ATOMIC);
	if (mcaddr == NULL)
		return 1;

	memcpy(mcaddr->address, address, 6);

	mcaddr->next = adapter->mcastaddrs;
	adapter->mcastaddrs = mcaddr;

	return STATUS_SUCCESS;
}

/*
 * Functions to obtain the CRC corresponding to the destination mac address.
 * This is a standard ethernet CRC in that it is a 32-bit, reflected CRC using
 * the polynomial:
 *   x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 +
 *   x^4 + x^2 + x^1.
 *
 * After the CRC for the 6 bytes is generated (but before the value is
 * complemented),
 * we must then transpose the value and return bits 30-23.
 *
 */
static u32 slic_crc_table[256];	/* Table of CRCs for all possible byte values */
static u32 slic_crc_init;	/* Is table initialized */

/*
 *  Contruct the CRC32 table
 */
void slic_mcast_init_crc32(void)
{
	ulong32 c;		/*  CRC shit reg                 */
	ulong32 e = 0;		/*  Poly X-or pattern            */
	int i;			/*  counter                      */
	int k;			/*  byte being shifted into crc  */

	static int p[] = { 0, 1, 2, 4, 5, 7, 8, 10, 11, 12, 16, 22, 23, 26 };

	for (i = 0; i < sizeof(p) / sizeof(int); i++)
		e |= 1L << (31 - p[i]);

	for (i = 1; i < 256; i++) {
		c = i;
		for (k = 8; k; k--)
			c = c & 1 ? (c >> 1) ^ e : c >> 1;
		slic_crc_table[i] = c;
	}
}

/*
 *  Return the MAC hast as described above.
 */
uchar slic_mcast_get_mac_hash(pchar macaddr)
{
	ulong32 crc;
	pchar p;
	int i;
	uchar machash = 0;

	if (!slic_crc_init) {
		slic_mcast_init_crc32();
		slic_crc_init = 1;
	}

	crc = 0xFFFFFFFF;	/* Preload shift register, per crc-32 spec */
	for (i = 0, p = macaddr; i < 6; ++p, ++i)
		crc = (crc >> 8) ^ slic_crc_table[(crc ^ *p) & 0xFF];

	/* Return bits 1-8, transposed */
	for (i = 1; i < 9; i++)
		machash |= (((crc >> i) & 1) << (8 - i));

	return machash;
}

void slic_mcast_set_bit(p_adapter_t adapter, pchar address)
{
	uchar crcpoly;

	/* Get the CRC polynomial for the mac address */
	crcpoly = slic_mcast_get_mac_hash(address);

	/* We only have space on the SLIC for 64 entries.  Lop
	 * off the top two bits. (2^6 = 64)
	 */
	crcpoly &= 0x3F;

	/* OR in the new bit into our 64 bit mask. */
	adapter->mcastmask |= (ulong64) 1 << crcpoly;
}

void slic_mcast_set_list(struct net_device *dev)
{
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	int status = STATUS_SUCCESS;
	int i;
	pchar addresses;
	struct dev_mc_list *mc_list = dev->mc_list;
	int mc_count = dev->mc_count;

	ASSERT(adapter);

	for (i = 1; i <= mc_count; i++) {
		addresses = (pchar) &mc_list->dmi_addr;
		if (mc_list->dmi_addrlen == 6) {
			status = slic_mcast_add_list(adapter, addresses);
			if (status != STATUS_SUCCESS)
				break;
		} else {
			status = -EINVAL;
			break;
		}
		slic_mcast_set_bit(adapter, addresses);
		mc_list = mc_list->next;
	}

	DBG_MSG("%s a->devflags_prev[%x] dev->flags[%x] status[%x]\n",
		__func__, adapter->devflags_prev, dev->flags, status);
	if (adapter->devflags_prev != dev->flags) {
		adapter->macopts = MAC_DIRECTED;
		if (dev->flags) {
			if (dev->flags & IFF_BROADCAST)
				adapter->macopts |= MAC_BCAST;
			if (dev->flags & IFF_PROMISC)
				adapter->macopts |= MAC_PROMISC;
			if (dev->flags & IFF_ALLMULTI)
				adapter->macopts |= MAC_ALLMCAST;
			if (dev->flags & IFF_MULTICAST)
				adapter->macopts |= MAC_MCAST;
		}
		adapter->devflags_prev = dev->flags;
		DBG_MSG("%s call slic_config_set adapter->macopts[%x]\n",
			__func__, adapter->macopts);
		slic_config_set(adapter, TRUE);
	} else {
		if (status == STATUS_SUCCESS)
			slic_mcast_set_mask(adapter);
	}
	return;
}

void slic_mcast_set_mask(p_adapter_t adapter)
{
	p_slic_regs_t slic_regs = adapter->slic_regs;

	DBG_MSG("%s ENTER (%s) macopts[%x] mask[%llx]\n", __func__,
		adapter->netdev->name, (uint) adapter->macopts,
		adapter->mcastmask);

	if (adapter->macopts & (MAC_ALLMCAST | MAC_PROMISC)) {
		/* Turn on all multicast addresses. We have to do this for
		 * promiscuous mode as well as ALLMCAST mode.  It saves the
		 * Microcode from having to keep state about the MAC
		 * configuration.
		 */
/*              DBG_MSG("slicoss: %s macopts = MAC_ALLMCAST | MAC_PROMISC\n\
		SLUT MODE!!!\n",__func__); */
		WRITE_REG(slic_regs->slic_mcastlow, 0xFFFFFFFF, FLUSH);
		WRITE_REG(slic_regs->slic_mcasthigh, 0xFFFFFFFF, FLUSH);
/*        DBG_MSG("%s (%s) WRITE to slic_regs slic_mcastlow&high 0xFFFFFFFF\n",
		_func__, adapter->netdev->name); */
	} else {
		/* Commit our multicast mast to the SLIC by writing to the
		 * multicast address mask registers
		 */
		DBG_MSG("%s (%s) WRITE mcastlow[%x] mcasthigh[%x]\n",
			__func__, adapter->netdev->name,
			((ulong) (adapter->mcastmask & 0xFFFFFFFF)),
			((ulong) ((adapter->mcastmask >> 32) & 0xFFFFFFFF)));

		WRITE_REG(slic_regs->slic_mcastlow,
			  (ulong32) (adapter->mcastmask & 0xFFFFFFFF), FLUSH);
		WRITE_REG(slic_regs->slic_mcasthigh,
			  (ulong32) ((adapter->mcastmask >> 32) & 0xFFFFFFFF),
			  FLUSH);
	}
}

void slic_timer_ping(ulong dev)
{
	p_adapter_t adapter;
	p_sliccard_t card;

	ASSERT(dev);
	adapter = (p_adapter_t) ((struct net_device *) dev)->priv;
	ASSERT(adapter);
	card = adapter->card;
	ASSERT(card);
#if !SLIC_DUMP_ENABLED
/*#if SLIC_DUMP_ENABLED*/
	if ((adapter->state == ADAPT_UP) && (card->state == CARD_UP)) {
		int status;

		if (card->pingstatus != ISR_PINGMASK) {
			if (errormsg++ < 5) {
				DBG_MSG
				    ("%s (%s) CARD HAS CRASHED  PING_status == \
				     %x ERRORMSG# %d\n",
				     __func__, adapter->netdev->name,
				     card->pingstatus, errormsg);
			}
			/*   ASSERT(card->pingstatus == ISR_PINGMASK); */
		} else {
			if (goodmsg++ < 5) {
				DBG_MSG
				    ("slicoss: %s (%s) PING_status == %x \
				     GOOD!!!!!!!! msg# %d\n",
				     __func__, adapter->netdev->name,
				     card->pingstatus, errormsg);
			}
		}
		card->pingstatus = 0;
		status = slic_upr_request(adapter, SLIC_UPR_PING, 0, 0, 0, 0);

		ASSERT(status == 0);
	} else {
		DBG_MSG("slicoss %s (%s) adapter[%p] NOT UP!!!!\n",
			__func__, adapter->netdev->name, adapter);
	}
#endif
	adapter->pingtimer.expires =
	    jiffies + SLIC_SECS_TO_JIFFS(PING_TIMER_INTERVAL);
	add_timer(&adapter->pingtimer);
}

void slic_if_stop_queue(p_adapter_t adapter)
{
	netif_stop_queue(adapter->netdev);
}

void slic_if_start_queue(p_adapter_t adapter)
{
	netif_start_queue(adapter->netdev);
}

/*
 *  slic_if_init
 *
 *  Perform initialization of our slic interface.
 *
 */
int slic_if_init(p_adapter_t adapter)
{
	p_sliccard_t card = adapter->card;
	struct net_device *dev = adapter->netdev;
	p_slic_regs_t slic_regs = adapter->slic_regs;
	p_slic_shmem_t pshmem;
	int status = 0;

	ASSERT(card);
	DBG_MSG("slicoss: %s (%s) ENTER states[%d:%d:%d:%d] flags[%x]\n",
		__func__, adapter->netdev->name,
		adapter->queues_initialized, adapter->state, adapter->linkstate,
		card->state, dev->flags);

	/* adapter should be down at this point */
	if (adapter->state != ADAPT_DOWN) {
		DBG_ERROR("slic_if_init adapter->state != ADAPT_DOWN\n");
		return -EIO;
	}
	ASSERT(adapter->linkstate == LINK_DOWN);

	adapter->devflags_prev = dev->flags;
	adapter->macopts = MAC_DIRECTED;
	if (dev->flags) {
		DBG_MSG("slicoss: %s (%s) Set MAC options: ", __func__,
			adapter->netdev->name);
		if (dev->flags & IFF_BROADCAST) {
			adapter->macopts |= MAC_BCAST;
			DBG_MSG("BCAST ");
		}
		if (dev->flags & IFF_PROMISC) {
			adapter->macopts |= MAC_PROMISC;
			DBG_MSG("PROMISC ");
		}
		if (dev->flags & IFF_ALLMULTI) {
			adapter->macopts |= MAC_ALLMCAST;
			DBG_MSG("ALL_MCAST ");
		}
		if (dev->flags & IFF_MULTICAST) {
			adapter->macopts |= MAC_MCAST;
			DBG_MSG("MCAST ");
		}
		DBG_MSG("\n");
	}
	status = slic_adapter_allocresources(adapter);
	if (status != STATUS_SUCCESS) {
		DBG_ERROR
		    ("slic_if_init: slic_adapter_allocresources FAILED %x\n",
		     status);
		slic_adapter_freeresources(adapter);
		return status;
	}

	if (!adapter->queues_initialized) {
		DBG_MSG("slicoss: %s call slic_rspqueue_init\n", __func__);
		if (slic_rspqueue_init(adapter))
			return -ENOMEM;
		DBG_MSG
		    ("slicoss: %s call slic_cmdq_init adapter[%p] port %d \n",
		     __func__, adapter, adapter->port);
		if (slic_cmdq_init(adapter))
			return -ENOMEM;
		DBG_MSG
		    ("slicoss: %s call slic_rcvqueue_init adapter[%p] \
		     port %d \n", __func__, adapter, adapter->port);
		if (slic_rcvqueue_init(adapter))
			return -ENOMEM;
		adapter->queues_initialized = 1;
	}
	DBG_MSG("slicoss: %s disable interrupts(slic)\n", __func__);

	WRITE_REG(slic_regs->slic_icr, ICR_INT_OFF, FLUSH);
	slic_stall_msec(1);

	if (!adapter->isp_initialized) {
		pshmem = (p_slic_shmem_t) adapter->phys_shmem;

		SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->bit64reglock);

#if defined(CONFIG_X86_64)
		WRITE_REG(slic_regs->slic_addr_upper,
			  SLIC_GET_ADDR_HIGH(&pshmem->isr), DONT_FLUSH);
		WRITE_REG(slic_regs->slic_isp,
			  SLIC_GET_ADDR_LOW(&pshmem->isr), FLUSH);
#elif defined(CONFIG_X86)
		WRITE_REG(slic_regs->slic_addr_upper, (ulong32) 0, DONT_FLUSH);
		WRITE_REG(slic_regs->slic_isp, (ulong32) &pshmem->isr, FLUSH);
#else
		Stop Compilations
#endif
		SLIC_RELEASE_IRQ_SPINLOCK(adapter->bit64reglock);
		adapter->isp_initialized = 1;
	}

	adapter->state = ADAPT_UP;
	if (!card->loadtimerset) {
		init_timer(&card->loadtimer);
		card->loadtimer.expires =
		    jiffies + SLIC_SECS_TO_JIFFS(SLIC_LOADTIMER_PERIOD);
		card->loadtimer.data = (ulong) card;
		card->loadtimer.function = &slic_timer_load_check;
		add_timer(&card->loadtimer);

		card->loadtimerset = 1;
	}
#if SLIC_GET_STATS_TIMER_ENABLED
	if (!adapter->statstimerset) {
		DBG_MSG("slicoss: %s start getstats_timer(slic)\n",
			__func__);
		init_timer(&adapter->statstimer);
		adapter->statstimer.expires =
		    jiffies + SLIC_SECS_TO_JIFFS(STATS_TIMER_INTERVAL);
		adapter->statstimer.data = (ulong) adapter->netdev;
		adapter->statstimer.function = &slic_timer_get_stats;
		add_timer(&adapter->statstimer);
		adapter->statstimerset = 1;
	}
#endif
#if !SLIC_DUMP_ENABLED && SLIC_PING_TIMER_ENABLED
/*#if SLIC_DUMP_ENABLED && SLIC_PING_TIMER_ENABLED*/
	if (!adapter->pingtimerset) {
		DBG_MSG("slicoss: %s start card_ping_timer(slic)\n",
			__func__);
		init_timer(&adapter->pingtimer);
		adapter->pingtimer.expires =
		    jiffies + SLIC_SECS_TO_JIFFS(PING_TIMER_INTERVAL);
		adapter->pingtimer.data = (ulong) dev;
		adapter->pingtimer.function = &slic_timer_ping;
		add_timer(&adapter->pingtimer);
		adapter->pingtimerset = 1;
		adapter->card->pingstatus = ISR_PINGMASK;
	}
#endif

	/*
	 *    clear any pending events, then enable interrupts
	 */
	DBG_MSG("slicoss: %s ENABLE interrupts(slic)\n", __func__);
	adapter->isrcopy = 0;
	adapter->pshmem->isr = 0;
	WRITE_REG(slic_regs->slic_isr, 0, FLUSH);
	WRITE_REG(slic_regs->slic_icr, ICR_INT_ON, FLUSH);

	DBG_MSG("slicoss: %s call slic_link_config(slic)\n", __func__);
	slic_link_config(adapter, LINK_AUTOSPEED, LINK_AUTOD);
	slic_link_event_handler(adapter);

	DBG_MSG("slicoss: %s EXIT\n", __func__);
	return STATUS_SUCCESS;
}

void slic_unmap_mmio_space(p_adapter_t adapter)
{
#if LINUX_FREES_ADAPTER_RESOURCES
	if (adapter->slic_regs)
		iounmap(adapter->slic_regs);
	adapter->slic_regs = NULL;
#endif
}

int slic_adapter_allocresources(p_adapter_t adapter)
{
	if (!adapter->intrregistered) {
		int retval;

		DBG_MSG
		    ("slicoss: %s AllocAdaptRsrcs adapter[%p] shmem[%p] \
		     phys_shmem[%p] dev->irq[%x] %x\n",
		     __func__, adapter, adapter->pshmem,
		     (void *)adapter->phys_shmem, adapter->netdev->irq,
		     NR_IRQS);

		SLIC_RELEASE_IRQ_SPINLOCK(slic_global.driver_lock);

		retval = request_irq(adapter->netdev->irq,
				     &slic_interrupt,
				     IRQF_SHARED,
				     adapter->netdev->name, adapter->netdev);

		SLIC_ACQUIRE_IRQ_SPINLOCK(slic_global.driver_lock);

		if (retval) {
			DBG_ERROR("slicoss: request_irq (%s) FAILED [%x]\n",
				  adapter->netdev->name, retval);
			return retval;
		}
		adapter->intrregistered = 1;
		DBG_MSG
		    ("slicoss: %s AllocAdaptRsrcs adapter[%p] shmem[%p] \
		     pshmem[%p] dev->irq[%x]\n",
		     __func__, adapter, adapter->pshmem,
		     (void *)adapter->pshmem, adapter->netdev->irq);
	}
	return STATUS_SUCCESS;
}

void slic_config_pci(struct pci_dev *pcidev)
{
	u16 pci_command;
	u16 new_command;

	pci_read_config_word(pcidev, PCI_COMMAND, &pci_command);
	DBG_MSG("slicoss: %s  PCI command[%4.4x]\n", __func__, pci_command);

	new_command = pci_command | PCI_COMMAND_MASTER
	    | PCI_COMMAND_MEMORY
	    | PCI_COMMAND_INVALIDATE
	    | PCI_COMMAND_PARITY | PCI_COMMAND_SERR | PCI_COMMAND_FAST_BACK;
	if (pci_command != new_command) {
		DBG_MSG("%s -- Updating PCI COMMAND register %4.4x->%4.4x.\n",
			__func__, pci_command, new_command);
		pci_write_config_word(pcidev, PCI_COMMAND, new_command);
	}
}

void slic_adapter_freeresources(p_adapter_t adapter)
{
	DBG_MSG("slicoss: %s ENTER adapter[%p]\n", __func__, adapter);
	slic_init_cleanup(adapter);
	SLIC_ZERO_MEMORY(&adapter->stats, sizeof(struct net_device_stats));
	adapter->error_interrupts = 0;
	adapter->rcv_interrupts = 0;
	adapter->xmit_interrupts = 0;
	adapter->linkevent_interrupts = 0;
	adapter->upr_interrupts = 0;
	adapter->num_isrs = 0;
	adapter->xmit_completes = 0;
	adapter->rcv_broadcasts = 0;
	adapter->rcv_multicasts = 0;
	adapter->rcv_unicasts = 0;
	DBG_MSG("slicoss: %s EXIT\n", __func__);
}

/*
 *  slic_link_config
 *
 *  Write phy control to configure link duplex/speed
 *
 */
void slic_link_config(p_adapter_t adapter,
		      ulong32 linkspeed, ulong32 linkduplex)
{
	ulong32 speed;
	ulong32 duplex;
	ulong32 phy_config;
	ulong32 phy_advreg;
	ulong32 phy_gctlreg;

	if (adapter->state != ADAPT_UP) {
		DBG_MSG
		    ("%s (%s) ADAPT Not up yet, Return! speed[%x] duplex[%x]\n",
		     __func__, adapter->netdev->name, linkspeed,
		     linkduplex);
		return;
	}
	DBG_MSG("slicoss: %s (%s) slic_link_config: speed[%x] duplex[%x]\n",
		__func__, adapter->netdev->name, linkspeed, linkduplex);

	ASSERT((adapter->devid == SLIC_1GB_DEVICE_ID)
	       || (adapter->devid == SLIC_2GB_DEVICE_ID));

	if (linkspeed > LINK_1000MB)
		linkspeed = LINK_AUTOSPEED;
	if (linkduplex > LINK_AUTOD)
		linkduplex = LINK_AUTOD;

	if ((linkspeed == LINK_AUTOSPEED) || (linkspeed == LINK_1000MB)) {
		if (adapter->flags & ADAPT_FLAGS_FIBERMEDIA) {
			/*  We've got a fiber gigabit interface, and register
			 *  4 is different in fiber mode than in copper mode
			 */

			/* advertise FD only @1000 Mb */
			phy_advreg = (MIICR_REG_4 | (PAR_ADV1000XFD));
			/* enable PAUSE frames        */
			phy_advreg |= PAR_ASYMPAUSE_FIBER;
			WRITE_REG(adapter->slic_regs->slic_wphy, phy_advreg,
				  FLUSH);

			if (linkspeed == LINK_AUTOSPEED) {
				/* reset phy, enable auto-neg  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_AUTONEG |
				      PCR_AUTONEG_RST));
				WRITE_REG(adapter->slic_regs->slic_wphy,
					  phy_config, FLUSH);
			} else {	/* forced 1000 Mb FD*/
				/* power down phy to break link
				   this may not work) */
				phy_config = (MIICR_REG_PCR | PCR_POWERDOWN);
				WRITE_REG(adapter->slic_regs->slic_wphy,
					  phy_config, FLUSH);
				/* wait, Marvell says 1 sec,
				   try to get away with 10 ms  */
				slic_stall_msec(10);

				/* disable auto-neg, set speed/duplex,
				   soft reset phy, powerup */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_SPEED_1000 |
				      PCR_DUPLEX_FULL));
				WRITE_REG(adapter->slic_regs->slic_wphy,
					  phy_config, FLUSH);
			}
		} else {	/* copper gigabit */

			/* Auto-Negotiate or 1000 Mb must be auto negotiated
			 * We've got a copper gigabit interface, and
			 * register 4 is different in copper mode than
			 * in fiber mode
			 */
			if (linkspeed == LINK_AUTOSPEED) {
				/* advertise 10/100 Mb modes   */
				phy_advreg =
				    (MIICR_REG_4 |
				     (PAR_ADV100FD | PAR_ADV100HD | PAR_ADV10FD
				      | PAR_ADV10HD));
			} else {
			/* linkspeed == LINK_1000MB -
			   don't advertise 10/100 Mb modes  */
				phy_advreg = MIICR_REG_4;
			}
			/* enable PAUSE frames  */
			phy_advreg |= PAR_ASYMPAUSE;
			/* required by the Cicada PHY  */
			phy_advreg |= PAR_802_3;
			WRITE_REG(adapter->slic_regs->slic_wphy, phy_advreg,
				  FLUSH);
			/* advertise FD only @1000 Mb  */
			phy_gctlreg = (MIICR_REG_9 | (PGC_ADV1000FD));
			WRITE_REG(adapter->slic_regs->slic_wphy, phy_gctlreg,
				  FLUSH);

			if (adapter->subsysid != SLIC_1GB_CICADA_SUBSYS_ID) {
				/* if a Marvell PHY
				   enable auto crossover */
				phy_config =
				    (MIICR_REG_16 | (MRV_REG16_XOVERON));
				WRITE_REG(adapter->slic_regs->slic_wphy,
					  phy_config, FLUSH);

				/* reset phy, enable auto-neg  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_AUTONEG |
				      PCR_AUTONEG_RST));
				WRITE_REG(adapter->slic_regs->slic_wphy,
					  phy_config, FLUSH);
			} else {	/* it's a Cicada PHY  */
				/* enable and restart auto-neg (don't reset)  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_AUTONEG | PCR_AUTONEG_RST));
				WRITE_REG(adapter->slic_regs->slic_wphy,
					  phy_config, FLUSH);
			}
		}
	} else {
		/* Forced 10/100  */
		if (linkspeed == LINK_10MB)
			speed = 0;
		else
			speed = PCR_SPEED_100;
		if (linkduplex == LINK_HALFD)
			duplex = 0;
		else
			duplex = PCR_DUPLEX_FULL;

		if (adapter->subsysid != SLIC_1GB_CICADA_SUBSYS_ID) {
			/* if a Marvell PHY
			   disable auto crossover  */
			phy_config = (MIICR_REG_16 | (MRV_REG16_XOVEROFF));
			WRITE_REG(adapter->slic_regs->slic_wphy, phy_config,
				  FLUSH);
		}

		/* power down phy to break link (this may not work)  */
		phy_config = (MIICR_REG_PCR | (PCR_POWERDOWN | speed | duplex));
		WRITE_REG(adapter->slic_regs->slic_wphy, phy_config, FLUSH);

		/* wait, Marvell says 1 sec, try to get away with 10 ms */
		slic_stall_msec(10);

		if (adapter->subsysid != SLIC_1GB_CICADA_SUBSYS_ID) {
			/* if a Marvell PHY
			   disable auto-neg, set speed,
			   soft reset phy, powerup */
			phy_config =
			    (MIICR_REG_PCR | (PCR_RESET | speed | duplex));
			WRITE_REG(adapter->slic_regs->slic_wphy, phy_config,
				  FLUSH);
		} else {	/* it's a Cicada PHY  */
			/* disable auto-neg, set speed, powerup  */
			phy_config = (MIICR_REG_PCR | (speed | duplex));
			WRITE_REG(adapter->slic_regs->slic_wphy, phy_config,
				  FLUSH);
		}
	}

	DBG_MSG
	    ("slicoss: %s (%s) EXIT slic_link_config : state[%d] \
	    phy_config[%x]\n", __func__, adapter->netdev->name, adapter->state,
	    phy_config);
}

void slic_card_cleanup(p_sliccard_t card)
{
	DBG_MSG("slicoss: %s ENTER\n", __func__);

#if SLIC_DUMP_ENABLED
	if (card->dumpbuffer) {
		SLIC_DEALLOCATE_MEM(card->dumpbuffer);
		card->dumpbuffer = NULL;
		card->dumpbuffer_phys = 0;
		card->dumpbuffer_physl = 0;
		card->dumpbuffer_physh = 0;
	}
	if (card->cmdbuffer) {
		SLIC_DEALLOCATE_MEM(card->cmdbuffer);
		card->cmdbuffer = NULL;
		card->cmdbuffer_phys = 0;
		card->cmdbuffer_physl = 0;
		card->cmdbuffer_physh = 0;
	}
#endif

	if (card->loadtimerset) {
		card->loadtimerset = 0;
		del_timer(&card->loadtimer);
	}

	slic_debug_card_destroy(card);

	SLIC_DEALLOCATE_MEM(card);
	DBG_MSG("slicoss: %s EXIT\n", __func__);
}

static int slic_card_download_gbrcv(p_adapter_t adapter)
{
	p_slic_regs_t slic_regs = adapter->slic_regs;
	ulong32 codeaddr;
	puchar instruction = NULL;
	ulong32 rcvucodelen = 0;

	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
		instruction = (puchar) &OasisRcvUCode[0];
		rcvucodelen = OasisRcvUCodeLen;
		break;
	case SLIC_1GB_DEVICE_ID:
		instruction = (puchar) &GBRcvUCode[0];
		rcvucodelen = GBRcvUCodeLen;
		break;
	default:
		ASSERT(0);
		break;
	}

	/* start download */
	WRITE_REG(slic_regs->slic_rcv_wcs, SLIC_RCVWCS_BEGIN, FLUSH);

	/* download the rcv sequencer ucode */
	for (codeaddr = 0; codeaddr < rcvucodelen; codeaddr++) {
		/* write out instruction address */
		WRITE_REG(slic_regs->slic_rcv_wcs, codeaddr, FLUSH);

		/* write out the instruction data low addr */
		WRITE_REG(slic_regs->slic_rcv_wcs,
			  (ulong32) *(pulong32) instruction, FLUSH);
		instruction += 4;

		/* write out the instruction data high addr */
		WRITE_REG(slic_regs->slic_rcv_wcs, (ulong32) *instruction,
			  FLUSH);
		instruction += 1;
	}

	/* download finished */
	WRITE_REG(slic_regs->slic_rcv_wcs, SLIC_RCVWCS_FINISH, FLUSH);

	return 0;
}

int slic_card_download(p_adapter_t adapter)
{
	ulong32 section;
	int thissectionsize;
	int codeaddr;
	p_slic_regs_t slic_regs = adapter->slic_regs;
	ulong32 *instruction = NULL;
	ulong32 *lastinstruct = NULL;
	ulong32 *startinstruct = NULL;
	puchar nextinstruct;
	ulong32 baseaddress;
	ulong32 failure;
	ulong32 i;
	ulong32 numsects = 0;
	ulong32 sectsize[3];
	ulong32 sectstart[3];

/*      DBG_MSG ("slicoss: %s (%s) adapter[%p] card[%p] devid[%x] \
	jiffies[%lx] cpu %d\n", __func__, adapter->netdev->name, adapter,
	    adapter->card, adapter->devid,jiffies, smp_processor_id()); */

	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
/*      DBG_MSG ("slicoss: %s devid==SLIC_2GB_DEVICE_ID sections[%x]\n",
	__func__, (uint) ONumSections); */
		numsects = ONumSections;
		for (i = 0; i < numsects; i++) {
			sectsize[i] = OSectionSize[i];
			sectstart[i] = OSectionStart[i];
		}
		break;
	case SLIC_1GB_DEVICE_ID:
/*              DBG_MSG ("slicoss: %s devid==SLIC_1GB_DEVICE_ID sections[%x]\n",
		__func__, (uint) MNumSections); */
		numsects = MNumSections;
		for (i = 0; i < numsects; i++) {
			sectsize[i] = MSectionSize[i];
			sectstart[i] = MSectionStart[i];
		}
		break;
	default:
		ASSERT(0);
		break;
	}

	ASSERT(numsects <= 3);

	for (section = 0; section < numsects; section++) {
		switch (adapter->devid) {
		case SLIC_2GB_DEVICE_ID:
			instruction = (pulong32) &OasisUCode[section][0];
			baseaddress = sectstart[section];
			thissectionsize = sectsize[section] >> 3;
			lastinstruct =
			    (pulong32) &OasisUCode[section][sectsize[section] -
							     8];
			break;
		case SLIC_1GB_DEVICE_ID:
			instruction = (pulong32) &MojaveUCode[section][0];
			baseaddress = sectstart[section];
			thissectionsize = sectsize[section] >> 3;
			lastinstruct =
			    (pulong32) &MojaveUCode[section][sectsize[section]
							      - 8];
			break;
		default:
			ASSERT(0);
			break;
		}

		baseaddress = sectstart[section];
		thissectionsize = sectsize[section] >> 3;

		for (codeaddr = 0; codeaddr < thissectionsize; codeaddr++) {
			startinstruct = instruction;
			nextinstruct = ((puchar) instruction) + 8;
			/* Write out instruction address */
			WRITE_REG(slic_regs->slic_wcs, baseaddress + codeaddr,
				  FLUSH);
			/* Write out instruction to low addr */
			WRITE_REG(slic_regs->slic_wcs, *instruction, FLUSH);
#ifdef CONFIG_X86_64
			instruction = (pulong32) ((puchar) instruction + 4);
#else
			instruction++;
#endif
			/* Write out instruction to high addr */
			WRITE_REG(slic_regs->slic_wcs, *instruction, FLUSH);
#ifdef CONFIG_X86_64
			instruction = (pulong32) ((puchar) instruction + 4);
#else
			instruction++;
#endif
		}
	}

	for (section = 0; section < numsects; section++) {
		switch (adapter->devid) {
		case SLIC_2GB_DEVICE_ID:
			instruction = (pulong32) &OasisUCode[section][0];
			break;
		case SLIC_1GB_DEVICE_ID:
			instruction = (pulong32) &MojaveUCode[section][0];
			break;
		default:
			ASSERT(0);
			break;
		}

		baseaddress = sectstart[section];
		if (baseaddress < 0x8000)
			continue;
		thissectionsize = sectsize[section] >> 3;

/*        DBG_MSG ("slicoss: COMPARE secton[%x] baseaddr[%x] sectnsize[%x]\n",
		(uint)section,baseaddress,thissectionsize);*/

		for (codeaddr = 0; codeaddr < thissectionsize; codeaddr++) {
			/* Write out instruction address */
			WRITE_REG(slic_regs->slic_wcs,
				  SLIC_WCS_COMPARE | (baseaddress + codeaddr),
				  FLUSH);
			/* Write out instruction to low addr */
			WRITE_REG(slic_regs->slic_wcs, *instruction, FLUSH);
#ifdef CONFIG_X86_64
			instruction = (pulong32) ((puchar) instruction + 4);
#else
			instruction++;
#endif
			/* Write out instruction to high addr */
			WRITE_REG(slic_regs->slic_wcs, *instruction, FLUSH);
#ifdef CONFIG_X86_64
			instruction = (pulong32) ((puchar) instruction + 4);
#else
			instruction++;
#endif
			/* Check SRAM location zero. If it is non-zero. Abort.*/
			failure = READ_REG(slic_regs->slic_reset, 0);
			if (failure) {
				DBG_MSG
				    ("slicoss: %s FAILURE EXIT codeaddr[%x] \
				    thissectionsize[%x] failure[%x]\n",
				     __func__, codeaddr, thissectionsize,
				     failure);

				return -EIO;
			}
		}
	}
/*    DBG_MSG ("slicoss: Compare done\n");*/

	/* Everything OK, kick off the card */
	slic_stall_msec(10);
	WRITE_REG(slic_regs->slic_wcs, SLIC_WCS_START, FLUSH);

	/* stall for 20 ms, long enough for ucode to init card
	   and reach mainloop */
	slic_stall_msec(20);

	DBG_MSG("slicoss: %s (%s) EXIT adapter[%p] card[%p]\n",
		__func__, adapter->netdev->name, adapter, adapter->card);

	return STATUS_SUCCESS;
}

void slic_adapter_set_hwaddr(p_adapter_t adapter)
{
	p_sliccard_t card = adapter->card;

/*  DBG_MSG ("%s ENTER card->config_set[%x] port[%d] physport[%d] funct#[%d]\n",
    __func__, card->config_set, adapter->port, adapter->physport,
    adapter->functionnumber);

    slic_dbg_macaddrs(adapter); */

	if ((adapter->card) && (card->config_set)) {
		memcpy(adapter->macaddr,
		       card->config.MacInfo[adapter->functionnumber].macaddrA,
		       sizeof(slic_config_mac_t));
/*      DBG_MSG ("%s AFTER copying from config.macinfo into currmacaddr\n",
	__func__);
	slic_dbg_macaddrs(adapter);*/
		if (!(adapter->currmacaddr[0] || adapter->currmacaddr[1] ||
		      adapter->currmacaddr[2] || adapter->currmacaddr[3] ||
		      adapter->currmacaddr[4] || adapter->currmacaddr[5])) {
			memcpy(adapter->currmacaddr, adapter->macaddr, 6);
		}
		if (adapter->netdev) {
			memcpy(adapter->netdev->dev_addr, adapter->currmacaddr,
			       6);
		}
	}
/*  DBG_MSG ("%s EXIT port %d\n", __func__, adapter->port);
    slic_dbg_macaddrs(adapter); */
}

void slic_card_halt(p_sliccard_t card, p_adapter_t adapter)
{
	p_slic_regs_t slic_regs = adapter->slic_regs;

	DBG_MSG("slicoss: %s ENTER card[%p] adapter[%p] card->state[%x]\n",
		__func__, card, adapter, card->state);
	WRITE_REG(slic_regs->slic_icr, ICR_INT_OFF, FLUSH);
	adapter->all_reg_writes++;
	adapter->icr_reg_writes++;
	slic_config_clear(adapter);
	WRITE_REG(slic_regs->slic_reset_iface, 0, FLUSH);
	slic_soft_reset(adapter);
	DBG_MSG("slicoss: %s EXIT card[%p] adapter[%p] card->state[%x]\n",
		__func__, card, adapter, card->state);
	return;

}

void slic_intagg_set(p_adapter_t adapter, ulong32 value)
{
	p_slic_regs_t slic_regs = adapter->slic_regs;

	WRITE_REG(slic_regs->slic_intagg, value, FLUSH);
	adapter->card->loadlevel_current = value;
}

int slic_card_init(p_sliccard_t card, p_adapter_t adapter)
{
	p_slic_regs_t slic_regs = adapter->slic_regs;
	pslic_eeprom_t peeprom;
	poslic_eeprom_t pOeeprom;
	dma_addr_t phys_config;
	ulong32 phys_configh;
	ulong32 phys_configl;
	ulong32 i = 0;
	p_slic_shmem_t pshmem;
	int status;
	uint macaddrs = card->card_size;
	ushort eecodesize;
	ushort dramsize;
	ushort ee_chksum;
	ushort calc_chksum;
	pslic_config_mac_t pmac;
	uchar fruformat;
	uchar oemfruformat;
	patk_fru_t patkfru;
	poemfru_t poemfru;

	DBG_MSG
	    ("slicoss: %s ENTER card[%p] adapter[%p] card->state[%x] \
	    size[%d]\n", __func__, card, adapter, card->state, card->card_size);

	/* Reset everything except PCI configuration space */
	slic_soft_reset(adapter);

	/* Download the microcode */
	status = slic_card_download(adapter);

	if (status != STATUS_SUCCESS) {
		DBG_ERROR("SLIC download failed bus %d slot %d\n",
			  (uint) adapter->busnumber,
			  (uint) adapter->slotnumber);
		return status;
	}

	if (!card->config_set) {
		peeprom = pci_alloc_consistent(adapter->pcidev,
					       sizeof(slic_eeprom_t),
					       &phys_config);

		phys_configl = SLIC_GET_ADDR_LOW(phys_config);
		phys_configh = SLIC_GET_ADDR_HIGH(phys_config);

		DBG_MSG("slicoss: %s Eeprom info  adapter [%p]\n    "
			"size        [%x]\n    peeprom     [%p]\n    "
			"phys_config [%p]\n    phys_configl[%x]\n    "
			"phys_configh[%x]\n",
			__func__, adapter, (ulong32) sizeof(slic_eeprom_t),
			peeprom, (pvoid) phys_config, phys_configl,
			phys_configh);
		if (!peeprom) {
			DBG_ERROR
			    ("SLIC eeprom read failed to get memory bus %d \
			    slot %d\n",
			     (uint) adapter->busnumber,
			     (uint) adapter->slotnumber);
			return -ENOMEM;
		} else {
			SLIC_ZERO_MEMORY(peeprom, sizeof(slic_eeprom_t));
		}
		WRITE_REG(slic_regs->slic_icr, ICR_INT_OFF, FLUSH);
		slic_stall_msec(1);
		pshmem = (p_slic_shmem_t) adapter->phys_shmem;

		SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->bit64reglock);
		WRITE_REG(slic_regs->slic_addr_upper, 0, DONT_FLUSH);
		WRITE_REG(slic_regs->slic_isp,
			  SLIC_GET_ADDR_LOW(&pshmem->isr), FLUSH);
		SLIC_RELEASE_IRQ_SPINLOCK(adapter->bit64reglock);

		slic_config_get(adapter, phys_configl, phys_configh);

		for (;;) {
			if (adapter->pshmem->isr) {
				DBG_MSG("%s shmem[%p] shmem->isr[%x]\n",
					__func__, adapter->pshmem,
					adapter->pshmem->isr);

				if (adapter->pshmem->isr & ISR_UPC) {
					adapter->pshmem->isr = 0;
					WRITE_REG64(adapter,
						    slic_regs->slic_isp,
						    0,
						    slic_regs->slic_addr_upper,
						    0, FLUSH);
					WRITE_REG(slic_regs->slic_isr, 0,
						  FLUSH);

					slic_upr_request_complete(adapter, 0);
					break;
				} else {
					adapter->pshmem->isr = 0;
					WRITE_REG(slic_regs->slic_isr, 0,
						  FLUSH);
				}
			} else {
				slic_stall_msec(1);
				i++;
				if (i > 5000) {
					DBG_ERROR
					    ("SLIC: %d config data fetch timed\
					      out!\n", adapter->port);
					DBG_MSG("%s shmem[%p] shmem->isr[%x]\n",
						__func__, adapter->pshmem,
						adapter->pshmem->isr);
					WRITE_REG64(adapter,
						    slic_regs->slic_isp, 0,
						    slic_regs->slic_addr_upper,
						    0, FLUSH);
					return -EINVAL;
				}
			}
		}

		switch (adapter->devid) {
		/* Oasis card */
		case SLIC_2GB_DEVICE_ID:
			/* extract EEPROM data and pointers to EEPROM data */
			pOeeprom = (poslic_eeprom_t) peeprom;
			eecodesize = pOeeprom->EecodeSize;
			dramsize = pOeeprom->DramSize;
			pmac = pOeeprom->MacInfo;
			fruformat = pOeeprom->FruFormat;
			patkfru = &pOeeprom->AtkFru;
			oemfruformat = pOeeprom->OemFruFormat;
			poemfru = &pOeeprom->OemFru;
			macaddrs = 2;
			/* Minor kludge for Oasis card
			     get 2 MAC addresses from the
			     EEPROM to ensure that function 1
			     gets the Port 1 MAC address */
			break;
		default:
			/* extract EEPROM data and pointers to EEPROM data */
			eecodesize = peeprom->EecodeSize;
			dramsize = peeprom->DramSize;
			pmac = peeprom->u2.mac.MacInfo;
			fruformat = peeprom->FruFormat;
			patkfru = &peeprom->AtkFru;
			oemfruformat = peeprom->OemFruFormat;
			poemfru = &peeprom->OemFru;
			break;
		}

		card->config.EepromValid = FALSE;

		/*  see if the EEPROM is valid by checking it's checksum */
		if ((eecodesize <= MAX_EECODE_SIZE) &&
		    (eecodesize >= MIN_EECODE_SIZE)) {

			ee_chksum =
			    *(pushort) ((pchar) peeprom + (eecodesize - 2));
			/*
			    calculate the EEPROM checksum
			*/
			calc_chksum =
			    ~slic_eeprom_cksum((pchar) peeprom,
					       (eecodesize - 2));
			/*
			    if the ucdoe chksum flag bit worked,
			    we wouldn't need this shit
			*/
			if (ee_chksum == calc_chksum)
				card->config.EepromValid = TRUE;
		}
		/*  copy in the DRAM size */
		card->config.DramSize = dramsize;

		/*  copy in the MAC address(es) */
		for (i = 0; i < macaddrs; i++) {
			memcpy(&card->config.MacInfo[i],
			       &pmac[i], sizeof(slic_config_mac_t));
		}
/*      DBG_MSG ("%s EEPROM Checksum Good? %d  MacAddress\n",__func__,
		card->config.EepromValid); */

		/*  copy the Alacritech FRU information */
		card->config.FruFormat = fruformat;
		memcpy(&card->config.AtkFru, patkfru, sizeof(atk_fru_t));

		pci_free_consistent(adapter->pcidev,
				    sizeof(slic_eeprom_t),
				    peeprom, phys_config);
		DBG_MSG
		    ("slicoss: %s adapter%d [%p] size[%x] FREE peeprom[%p] \
		     phys_config[%p]\n",
		     __func__, adapter->port, adapter,
		     (ulong32) sizeof(slic_eeprom_t), peeprom,
		     (pvoid) phys_config);

		if ((!card->config.EepromValid) &&
		    (adapter->reg_params.fail_on_bad_eeprom)) {
			WRITE_REG64(adapter,
				    slic_regs->slic_isp,
				    0, slic_regs->slic_addr_upper, 0, FLUSH);
			DBG_ERROR
			    ("unsupported CONFIGURATION  EEPROM invalid\n");
			return -EINVAL;
		}

		card->config_set = 1;
	}

	if (slic_card_download_gbrcv(adapter)) {
		DBG_ERROR("%s unable to download GB receive microcode\n",
			  __func__);
		return -EINVAL;
	}

	if (slic_global.dynamic_intagg) {
		DBG_MSG
		    ("Dynamic Interrupt Aggregation[ENABLED]: slic%d \
		     SET intagg to %d\n",
		     card->cardnum, 0);
		slic_intagg_set(adapter, 0);
	} else {
		slic_intagg_set(adapter, intagg_delay);
		DBG_MSG
		    ("Dynamic Interrupt Aggregation[DISABLED]: slic%d \
		     SET intagg to %d\n",
		     card->cardnum, intagg_delay);
	}

	/*
	 *  Initialize ping status to "ok"
	 */
	card->pingstatus = ISR_PINGMASK;

#if SLIC_DUMP_ENABLED
	if (!card->dumpbuffer) {
		card->dumpbuffer =
		    SLIC_ALLOCATE_MEM(DUMP_PAGE_SIZE, GFP_ATOMIC);

		ASSERT(card->dumpbuffer);
		if (card->dumpbuffer == NULL)
			return -ENOMEM;
	}
	/*
	 *  Smear the shared memory structure and then obtain
	 *  the PHYSICAL address of this structure
	 */
	SLIC_ZERO_MEMORY(card->dumpbuffer, DUMP_PAGE_SIZE);
	card->dumpbuffer_phys = SLIC_GET_PHYSICAL_ADDRESS(card->dumpbuffer);
	card->dumpbuffer_physh = SLIC_GET_ADDR_HIGH(card->dumpbuffer_phys);
	card->dumpbuffer_physl = SLIC_GET_ADDR_LOW(card->dumpbuffer_phys);

	/*
	 *  Allocate COMMAND BUFFER
	 */
	if (!card->cmdbuffer) {
		card->cmdbuffer =
		    SLIC_ALLOCATE_MEM(sizeof(dump_cmd_t), GFP_ATOMIC);

		ASSERT(card->cmdbuffer);
		if (card->cmdbuffer == NULL)
			return -ENOMEM;
	}
	/*
	 *  Smear the shared memory structure and then obtain
	 *  the PHYSICAL address of this structure
	 */
	SLIC_ZERO_MEMORY(card->cmdbuffer, sizeof(dump_cmd_t));
	card->cmdbuffer_phys = SLIC_GET_PHYSICAL_ADDRESS(card->cmdbuffer);
	card->cmdbuffer_physh = SLIC_GET_ADDR_HIGH(card->cmdbuffer_phys);
	card->cmdbuffer_physl = SLIC_GET_ADDR_LOW(card->cmdbuffer_phys);
#endif

	/*
	 * Lastly, mark our card state as up and return success
	 */
	card->state = CARD_UP;
	card->reset_in_progress = 0;
	DBG_MSG("slicoss: %s EXIT card[%p] adapter[%p] card->state[%x]\n",
		__func__, card, adapter, card->state);

	return STATUS_SUCCESS;
}

ulong32 slic_card_locate(p_adapter_t adapter)
{
	p_sliccard_t card = slic_global.slic_card;
	p_physcard_t physcard = slic_global.phys_card;
	ushort card_hostid;
	u16 __iomem *hostid_reg;
	uint i;
	uint rdhostid_offset = 0;

	DBG_MSG("slicoss: %s adapter[%p] slot[%x] bus[%x] port[%x]\n",
		__func__, adapter, adapter->slotnumber, adapter->busnumber,
		adapter->port);

	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
		rdhostid_offset = SLIC_RDHOSTID_2GB;
		break;
	case SLIC_1GB_DEVICE_ID:
		rdhostid_offset = SLIC_RDHOSTID_1GB;
		break;
	default:
		ASSERT(0);
		break;
	}

	hostid_reg =
	    (u16 __iomem *) (((u8 __iomem *) (adapter->slic_regs)) +
	    rdhostid_offset);
	DBG_MSG("slicoss: %s *hostid_reg[%p] == ", __func__, hostid_reg);

	/* read the 16 bit hostid from SRAM */
/*  card_hostid = READ_REGP16(hostid_reg, 0);*/
	card_hostid = (ushort) readw(hostid_reg);
	DBG_MSG(" card_hostid[%x]\n", card_hostid);

	/* Initialize a new card structure if need be */
	if (card_hostid == SLIC_HOSTID_DEFAULT) {
		card = kzalloc(sizeof(sliccard_t), GFP_KERNEL);
		if (card == NULL)
			return -ENOMEM;

		card->next = slic_global.slic_card;
		slic_global.slic_card = card;
#if DBG
		if (adapter->devid == SLIC_2GB_DEVICE_ID) {
			DBG_MSG
			    ("SLICOSS ==> Initialize 2 Port Gigabit Server \
			     and Storage Accelerator\n");
		} else {
			DBG_MSG
			    ("SLICOSS ==> Initialize 1 Port Gigabit Server \
			     and Storage Accelerator\n");
		}
#endif
		card->busnumber = adapter->busnumber;
		card->slotnumber = adapter->slotnumber;

		/* Find an available cardnum */
		for (i = 0; i < SLIC_MAX_CARDS; i++) {
			if (slic_global.cardnuminuse[i] == 0) {
				slic_global.cardnuminuse[i] = 1;
				card->cardnum = i;
				break;
			}
		}
		slic_global.num_slic_cards++;
		DBG_MSG("\nCARDNUM == %d  Total %d  Card[%p]\n\n",
			card->cardnum, slic_global.num_slic_cards, card);

		slic_debug_card_create(card);
	} else {
		DBG_MSG
		    ("slicoss: %s CARD already allocated, find the \
		     correct card\n", __func__);
		/* Card exists, find the card this adapter belongs to */
		while (card) {
			DBG_MSG
			    ("slicoss: %s card[%p] slot[%x] bus[%x] \
			      adaptport[%p] hostid[%x] cardnum[%x]\n",
			     __func__, card, card->slotnumber,
			     card->busnumber, card->adapter[adapter->port],
			     card_hostid, card->cardnum);

			if (card->cardnum == card_hostid)
				break;
			card = card->next;
		}
	}

	ASSERT(card);
	if (!card)
		return STATUS_FAILURE;
	/* Put the adapter in the card's adapter list */
	ASSERT(card->adapter[adapter->port] == NULL);
	if (!card->adapter[adapter->port]) {
		card->adapter[adapter->port] = adapter;
		adapter->card = card;
	}

	card->card_size = 1;	/* one port per *logical* card */

	while (physcard) {
		for (i = 0; i < SLIC_MAX_PORTS; i++) {
			if (!physcard->adapter[i])
				continue;
			else
				break;
		}
		ASSERT(i != SLIC_MAX_PORTS);
		if (physcard->adapter[i]->slotnumber == adapter->slotnumber)
			break;
		physcard = physcard->next;
	}
	if (!physcard) {
		/* no structure allocated for this physical card yet */
		physcard =
		    (p_physcard_t) SLIC_ALLOCATE_MEM(sizeof(physcard_t),
						     GFP_ATOMIC);
		ASSERT(physcard);
		SLIC_ZERO_MEMORY(physcard, sizeof(physcard_t));

		DBG_MSG
		    ("\n%s Allocate a PHYSICALcard:\n    PHYSICAL_Card[%p]\n\
		     LogicalCard  [%p]\n    adapter      [%p]\n",
		     __func__, physcard, card, adapter);

		physcard->next = slic_global.phys_card;
		slic_global.phys_card = physcard;
		physcard->adapters_allocd = 1;
	} else {
		physcard->adapters_allocd++;
	}
	/* Note - this is ZERO relative */
	adapter->physport = physcard->adapters_allocd - 1;

	ASSERT(physcard->adapter[adapter->physport] == NULL);
	physcard->adapter[adapter->physport] = adapter;
	adapter->physcard = physcard;
	DBG_MSG("    PHYSICAL_Port %d    Logical_Port  %d\n", adapter->physport,
		adapter->port);

	return 0;
}

void slic_card_remaster(p_adapter_t adapter)
{
	p_sliccard_t card = adapter->card;
	int i;

	DBG_MSG("slicoss: %s card->master[%p] == adapter[%p]??\n",
		__func__, card->master, adapter);
	if (card->master != adapter)
		return;
	card->master = NULL;
	for (i = 0; i < SLIC_MAX_PORTS; i++) {
		if (card->adapter[i] && (card->adapter[i] != adapter)) {
			card->master = card->adapter[i];
			DBG_MSG("slicoss: %s NEW MASTER SET card->master[%p]"
				" == card->adapter[%d]\n", __func__,
				card->master, i);
			break;
		}
	}
}

void slic_soft_reset(p_adapter_t adapter)
{
	if (adapter->card->state == CARD_UP) {
		DBG_MSG("slicoss: %s QUIESCE adapter[%p] card[%p] devid[%x]\n",
			__func__, adapter, adapter->card, adapter->devid);
		WRITE_REG(adapter->slic_regs->slic_quiesce, 0, FLUSH);
		slic_stall_msec(1);
	}
/*      DBG_MSG ("slicoss: %s (%s) adapter[%p] card[%p] devid[%x]\n",
	__func__, adapter->netdev->name, adapter, adapter->card,
	   adapter->devid); */

	WRITE_REG(adapter->slic_regs->slic_reset, SLIC_RESET_MAGIC, FLUSH);
	slic_stall_msec(1);
}

void slic_card_reset(p_adapter_t adapter)
{
	p_sliccard_t card = adapter->card;
	p_slic_upr_t upr = adapter->upr_list;
	p_slic_upr_t upr_next = NULL;
	ulong32 i;
#if SLIC_FAILURE_RESET
	ulong32 status = 0;
#endif
	DBG_MSG
	    ("slicoss: %s adapter[%p] port[%d] state[%x] card[%p] state[%x]\n",
	     __func__, adapter, adapter->port, adapter->state, card,
	     card->state);
	SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->adapter_lock);
	SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->reset_lock);
	if (card->state == CARD_DIAG) {
		SLIC_RELEASE_IRQ_SPINLOCK(adapter->reset_lock);
		SLIC_RELEASE_IRQ_SPINLOCK(adapter->adapter_lock);
		return;
	}
	SLIC_ACQUIRE_IRQ_SPINLOCK(slic_global.driver_lock);
	card->reset_in_progress = 1;
#if SLIC_FAILURE_RESET
	if (adapter->state != ADAPT_RESET) {
		SLIC_RELEASE_IRQ_SPINLOCK(slic_global.driver_lock);
		SLIC_RELEASE_IRQ_SPINLOCK(adapter->reset_lock);
		SLIC_RELEASE_IRQ_SPINLOCK(adapter->adapter_lock);
		return;
	}

	adapter->state = ADAPT_DOWN;
	adapter->linkstate = LINK_DOWN;
#endif
	if (adapter->gennumber == card->gennumber) {
		for (i = 0; i < card->card_size; i++) {
			if (card->adapter[i]) {
				if (card->adapter[i] == adapter)
					continue;
				if (card->adapter[i]->state == ADAPT_UP) {
					card->adapter[i]->state = ADAPT_RESET;
					adapter->linkstate = LINK_DOWN;
				}
			}
		}
#if SLIC_FAILURE_RESET
		slic_soft_reset(adapter);
		card->state = CARD_DOWN;
		card->master = NULL;
		card->adapters_activated = 0;
#endif
		card->gennumber++;
	}
	adapter->gennumber = card->gennumber;
	adapter->pshmem->isr = 0;
	adapter->isrcopy = 0;
	SLIC_RELEASE_IRQ_SPINLOCK(adapter->reset_lock);
	for (i = 0; i < card->card_size; i++) {
		if (card->adapter[i])
			slic_cmdq_reset(card->adapter[i]);
	}
	while (upr) {
		upr_next = upr->next;
		SLIC_DEALLOCATE_MEM(upr);
		upr = upr_next;
	}
	adapter->upr_list = NULL;
	adapter->upr_busy = 0;
#if SLIC_FAILURE_RESET
	status = slic_if_init(adapter);
	if ((status == 0) && (!card->master))
		card->master = adapter;
	slic_mcast_set_mask(adapter);
#endif
	SLIC_RELEASE_IRQ_SPINLOCK(slic_global.driver_lock);
	SLIC_RELEASE_IRQ_SPINLOCK(adapter->adapter_lock);
	DBG_MSG
	    ("slicoss: %s EXIT adapter[%p] port[%d] state[%x] card[%p] \
	      state[%x]\n", __func__, adapter, adapter->port, adapter->state,
	      card, card->state);
	return;
}

void slic_config_set(p_adapter_t adapter, boolean linkchange)
{
	ulong32 value;
	ulong32 RcrReset;
	p_slic_regs_t slic_regs = adapter->slic_regs;

	DBG_MSG("slicoss: %s (%s) slic_interface_enable[%p](%d)\n",
		__func__, adapter->netdev->name, adapter,
		adapter->cardindex);

	if (linkchange) {
		/* Setup MAC */
		slic_mac_config(adapter);
		RcrReset = GRCR_RESET;
	} else {
		slic_mac_address_config(adapter);
		RcrReset = 0;
	}

	if (adapter->linkduplex == LINK_FULLD) {
		/* setup xmtcfg */
		value = (GXCR_RESET |	/* Always reset     */
			 GXCR_XMTEN |	/* Enable transmit  */
			 GXCR_PAUSEEN);	/* Enable pause     */

		DBG_MSG("slicoss: FDX adapt[%p] set xmtcfg to [%x]\n", adapter,
			value);
		WRITE_REG(slic_regs->slic_wxcfg, value, FLUSH);

		/* Setup rcvcfg last */
		value = (RcrReset |	/* Reset, if linkchange */
			 GRCR_CTLEN |	/* Enable CTL frames    */
			 GRCR_ADDRAEN |	/* Address A enable     */
			 GRCR_RCVBAD |	/* Rcv bad frames       */
			 (GRCR_HASHSIZE << GRCR_HASHSIZE_SHIFT));
	} else {
		/* setup xmtcfg */
		value = (GXCR_RESET |	/* Always reset     */
			 GXCR_XMTEN);	/* Enable transmit  */

		DBG_MSG("slicoss: HDX adapt[%p] set xmtcfg to [%x]\n", adapter,
			value);
		WRITE_REG(slic_regs->slic_wxcfg, value, FLUSH);

		/* Setup rcvcfg last */
		value = (RcrReset |	/* Reset, if linkchange */
			 GRCR_ADDRAEN |	/* Address A enable     */
			 GRCR_RCVBAD |	/* Rcv bad frames       */
			 (GRCR_HASHSIZE << GRCR_HASHSIZE_SHIFT));
	}

	if (adapter->state != ADAPT_DOWN) {
		/* Only enable receive if we are restarting or running */
		value |= GRCR_RCVEN;
	}

	if (adapter->macopts & MAC_PROMISC)
		value |= GRCR_RCVALL;

	DBG_MSG("slicoss: adapt[%p] set rcvcfg to [%x]\n", adapter, value);
	WRITE_REG(slic_regs->slic_wrcfg, value, FLUSH);
}

/*
 *  Turn off RCV and XMT, power down PHY
 */
void slic_config_clear(p_adapter_t adapter)
{
	ulong32 value;
	ulong32 phy_config;
	p_slic_regs_t slic_regs = adapter->slic_regs;

	/* Setup xmtcfg */
	value = (GXCR_RESET |	/* Always reset */
		 GXCR_PAUSEEN);	/* Enable pause */

	WRITE_REG(slic_regs->slic_wxcfg, value, FLUSH);

	value = (GRCR_RESET |	/* Always reset      */
		 GRCR_CTLEN |	/* Enable CTL frames */
		 GRCR_ADDRAEN |	/* Address A enable  */
		 (GRCR_HASHSIZE << GRCR_HASHSIZE_SHIFT));

	WRITE_REG(slic_regs->slic_wrcfg, value, FLUSH);

	/* power down phy */
	phy_config = (MIICR_REG_PCR | (PCR_POWERDOWN));
	WRITE_REG(slic_regs->slic_wphy, phy_config, FLUSH);
}

void slic_config_get(p_adapter_t adapter, ulong32 config, ulong32 config_h)
{
	int status;

	status = slic_upr_request(adapter,
				  SLIC_UPR_RCONFIG,
				  (ulong32) config, (ulong32) config_h, 0, 0);
	ASSERT(status == 0);
}

void slic_mac_address_config(p_adapter_t adapter)
{
	ulong32 value;
	ulong32 value2;
	p_slic_regs_t slic_regs = adapter->slic_regs;

	value = *(pulong32) &adapter->currmacaddr[2];
	value = ntohl(value);
	WRITE_REG(slic_regs->slic_wraddral, value, FLUSH);
	WRITE_REG(slic_regs->slic_wraddrbl, value, FLUSH);

	value2 = (ulong32) ((adapter->currmacaddr[0] << 8 |
			     adapter->currmacaddr[1]) & 0xFFFF);

	WRITE_REG(slic_regs->slic_wraddrah, value2, FLUSH);
	WRITE_REG(slic_regs->slic_wraddrbh, value2, FLUSH);

	DBG_MSG("%s value1[%x] value2[%x] Call slic_mcast_set_mask\n",
		__func__, value, value2);
	slic_dbg_macaddrs(adapter);

	/* Write our multicast mask out to the card.  This is done */
	/* here in addition to the slic_mcast_addr_set routine     */
	/* because ALL_MCAST may have been enabled or disabled     */
	slic_mcast_set_mask(adapter);
}

void slic_mac_config(p_adapter_t adapter)
{
	ulong32 value;
	p_slic_regs_t slic_regs = adapter->slic_regs;

	/* Setup GMAC gaps */
	if (adapter->linkspeed == LINK_1000MB) {
		value = ((GMCR_GAPBB_1000 << GMCR_GAPBB_SHIFT) |
			 (GMCR_GAPR1_1000 << GMCR_GAPR1_SHIFT) |
			 (GMCR_GAPR2_1000 << GMCR_GAPR2_SHIFT));
	} else {
		value = ((GMCR_GAPBB_100 << GMCR_GAPBB_SHIFT) |
			 (GMCR_GAPR1_100 << GMCR_GAPR1_SHIFT) |
			 (GMCR_GAPR2_100 << GMCR_GAPR2_SHIFT));
	}

	/* enable GMII */
	if (adapter->linkspeed == LINK_1000MB)
		value |= GMCR_GBIT;

	/* enable fullduplex */
	if ((adapter->linkduplex == LINK_FULLD)
	    || (adapter->macopts & MAC_LOOPBACK)) {
		value |= GMCR_FULLD;
	}

	/* write mac config */
	WRITE_REG(slic_regs->slic_wmcfg, value, FLUSH);

	/* setup mac addresses */
	slic_mac_address_config(adapter);
}

boolean slic_mac_filter(p_adapter_t adapter, p_ether_header ether_frame)
{
	ulong32 opts = adapter->macopts;
	pulong32 dhost4 = (pulong32) &ether_frame->ether_dhost[0];
	pushort dhost2 = (pushort) &ether_frame->ether_dhost[4];
	boolean equaladdr;

	if (opts & MAC_PROMISC) {
		DBG_MSG("slicoss: %s (%s) PROMISCUOUS. Accept frame\n",
			__func__, adapter->netdev->name);
		return TRUE;
	}

	if ((*dhost4 == 0xFFFFFFFF) && (*dhost2 == 0xFFFF)) {
		if (opts & MAC_BCAST) {
			adapter->rcv_broadcasts++;
			return TRUE;
		} else {
			return FALSE;
		}
	}

	if (ether_frame->ether_dhost[0] & 0x01) {
		if (opts & MAC_ALLMCAST) {
			adapter->rcv_multicasts++;
			adapter->stats.multicast++;
			return TRUE;
		}
		if (opts & MAC_MCAST) {
			p_mcast_address_t mcaddr = adapter->mcastaddrs;

			while (mcaddr) {
				ETHER_EQ_ADDR(mcaddr->address,
					      ether_frame->ether_dhost,
					      equaladdr);
				if (equaladdr) {
					adapter->rcv_multicasts++;
					adapter->stats.multicast++;
					return TRUE;
				}
				mcaddr = mcaddr->next;
			}
			return FALSE;
		} else {
			return FALSE;
		}
	}
	if (opts & MAC_DIRECTED) {
		adapter->rcv_unicasts++;
		return TRUE;
	}
	return FALSE;

}

int slic_mac_set_address(struct net_device *dev, pvoid ptr)
{
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	struct sockaddr *addr = ptr;

	DBG_MSG("%s ENTER (%s)\n", __func__, adapter->netdev->name);

	if (netif_running(dev))
		return -EBUSY;
	if (!adapter)
		return -EBUSY;
	DBG_MSG("slicoss: %s (%s) curr %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		__func__, adapter->netdev->name, adapter->currmacaddr[0],
		adapter->currmacaddr[1], adapter->currmacaddr[2],
		adapter->currmacaddr[3], adapter->currmacaddr[4],
		adapter->currmacaddr[5]);
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	memcpy(adapter->currmacaddr, addr->sa_data, dev->addr_len);
	DBG_MSG("slicoss: %s (%s) new %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		__func__, adapter->netdev->name, adapter->currmacaddr[0],
		adapter->currmacaddr[1], adapter->currmacaddr[2],
		adapter->currmacaddr[3], adapter->currmacaddr[4],
		adapter->currmacaddr[5]);

	slic_config_set(adapter, TRUE);
	return 0;
}

/*
 *  slic_timer_get_stats
 *
 * Timer function used to suck the statistics out of the card every
 * 50 seconds or whatever STATS_TIMER_INTERVAL is set to.
 *
 */
void slic_timer_get_stats(ulong dev)
{
	p_adapter_t adapter;
	p_sliccard_t card;
	p_slic_shmem_t pshmem;

	ASSERT(dev);
	adapter = (p_adapter_t) ((struct net_device *)dev)->priv;
	ASSERT(adapter);
	card = adapter->card;
	ASSERT(card);

	if ((card->state == CARD_UP) &&
	    (adapter->state == ADAPT_UP) && (adapter->linkstate == LINK_UP)) {
		pshmem = (p_slic_shmem_t) adapter->phys_shmem;
#ifdef CONFIG_X86_64
		slic_upr_request(adapter,
				 SLIC_UPR_STATS,
				 SLIC_GET_ADDR_LOW(&pshmem->inicstats),
				 SLIC_GET_ADDR_HIGH(&pshmem->inicstats), 0, 0);
#elif defined(CONFIG_X86)
		slic_upr_request(adapter,
				 SLIC_UPR_STATS,
				 (ulong32) &pshmem->inicstats, 0, 0, 0);
#else
		Stop compilation;
#endif
	} else {
/*		DBG_MSG ("slicoss: %s adapter[%p] linkstate[%x] NOT UP!\n",
			__func__, adapter, adapter->linkstate); */
	}
	adapter->statstimer.expires = jiffies +
	    SLIC_SECS_TO_JIFFS(STATS_TIMER_INTERVAL);
	add_timer(&adapter->statstimer);
}

void slic_timer_load_check(ulong cardaddr)
{
	p_sliccard_t card = (p_sliccard_t) cardaddr;
	p_adapter_t adapter = card->master;
	ulong32 load = card->events;
	ulong32 level = 0;

	if ((adapter) && (adapter->state == ADAPT_UP) &&
	    (card->state == CARD_UP) && (slic_global.dynamic_intagg)) {
		if (adapter->devid == SLIC_1GB_DEVICE_ID) {
			if (adapter->linkspeed == LINK_1000MB)
				level = 100;
			else {
				if (load > SLIC_LOAD_5)
					level = SLIC_INTAGG_5;
				else if (load > SLIC_LOAD_4)
					level = SLIC_INTAGG_4;
				else if (load > SLIC_LOAD_3)
					level = SLIC_INTAGG_3;
				else if (load > SLIC_LOAD_2)
					level = SLIC_INTAGG_2;
				else if (load > SLIC_LOAD_1)
					level = SLIC_INTAGG_1;
				else
					level = SLIC_INTAGG_0;
			}
			if (card->loadlevel_current != level) {
				card->loadlevel_current = level;
				WRITE_REG(adapter->slic_regs->slic_intagg,
					  level, FLUSH);
			}
		} else {
			if (load > SLIC_LOAD_5)
				level = SLIC_INTAGG_5;
			else if (load > SLIC_LOAD_4)
				level = SLIC_INTAGG_4;
			else if (load > SLIC_LOAD_3)
				level = SLIC_INTAGG_3;
			else if (load > SLIC_LOAD_2)
				level = SLIC_INTAGG_2;
			else if (load > SLIC_LOAD_1)
				level = SLIC_INTAGG_1;
			else
				level = SLIC_INTAGG_0;
			if (card->loadlevel_current != level) {
				card->loadlevel_current = level;
				WRITE_REG(adapter->slic_regs->slic_intagg,
					  level, FLUSH);
			}
		}
	}
	card->events = 0;
	card->loadtimer.expires =
	    jiffies + SLIC_SECS_TO_JIFFS(SLIC_LOADTIMER_PERIOD);
	add_timer(&card->loadtimer);
}

void slic_stall_msec(int stall)
{
	mdelay(stall);
}

void slic_stall_usec(int stall)
{
	udelay(stall);
}

void slic_assert_fail(void)
{
	ulong32 cpuid;
	ulong32 curr_pid;
	cpuid = smp_processor_id();
	curr_pid = current->pid;

	DBG_ERROR("%s CPU # %d ---- PID # %d\n", __func__, cpuid, curr_pid);
}

int slic_upr_queue_request(p_adapter_t adapter,
			   ulong32 upr_request,
			   ulong32 upr_data,
			   ulong32 upr_data_h,
			   ulong32 upr_buffer, ulong32 upr_buffer_h)
{
	p_slic_upr_t upr;
	p_slic_upr_t uprqueue;

	upr = SLIC_ALLOCATE_MEM(sizeof(slic_upr_t), GFP_ATOMIC);
	if (!upr) {
		DBG_MSG("%s COULD NOT ALLOCATE UPR MEM\n", __func__);

		return -ENOMEM;
	}
	upr->adapter = adapter->port;
	upr->upr_request = upr_request;
	upr->upr_data = upr_data;
	upr->upr_buffer = upr_buffer;
	upr->upr_data_h = upr_data_h;
	upr->upr_buffer_h = upr_buffer_h;
	upr->next = NULL;
	if (adapter->upr_list) {
		uprqueue = adapter->upr_list;

		while (uprqueue->next)
			uprqueue = uprqueue->next;
		uprqueue->next = upr;
	} else {
		adapter->upr_list = upr;
	}
	return STATUS_SUCCESS;
}

int slic_upr_request(p_adapter_t adapter,
		     ulong32 upr_request,
		     ulong32 upr_data,
		     ulong32 upr_data_h,
		     ulong32 upr_buffer, ulong32 upr_buffer_h)
{
	int status;

	SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->upr_lock);
	status = slic_upr_queue_request(adapter,
					upr_request,
					upr_data,
					upr_data_h, upr_buffer, upr_buffer_h);
	if (status != STATUS_SUCCESS) {
		SLIC_RELEASE_IRQ_SPINLOCK(adapter->upr_lock);
		return status;
	}
	slic_upr_start(adapter);
	SLIC_RELEASE_IRQ_SPINLOCK(adapter->upr_lock);
	return STATUS_PENDING;
}

void slic_upr_request_complete(p_adapter_t adapter, ulong32 isr)
{
	p_sliccard_t card = adapter->card;
	p_slic_upr_t upr;

/*    if (card->dump_requested) {
	DBG_MSG("ENTER slic_upr_request_complete Dump in progress ISR[%x]\n",
		isr);
      } */
	SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->upr_lock);
	upr = adapter->upr_list;
	if (!upr) {
		ASSERT(0);
		SLIC_RELEASE_IRQ_SPINLOCK(adapter->upr_lock);
		return;
	}
	adapter->upr_list = upr->next;
	upr->next = NULL;
	adapter->upr_busy = 0;
	ASSERT(adapter->port == upr->adapter);
	switch (upr->upr_request) {
	case SLIC_UPR_STATS:
		{
#if SLIC_GET_STATS_ENABLED
			p_slic_stats_t slicstats =
			    (p_slic_stats_t) &adapter->pshmem->inicstats;
			p_slic_stats_t newstats = slicstats;
			p_slic_stats_t old = &adapter->inicstats_prev;
			p_slicnet_stats_t stst = &adapter->slic_stats;
#endif
			if (isr & ISR_UPCERR) {
				DBG_ERROR
				    ("SLIC_UPR_STATS command failed isr[%x]\n",
				     isr);

				break;
			}
#if SLIC_GET_STATS_ENABLED
/*			DBG_MSG ("slicoss: %s rcv %lx:%lx:%lx:%lx:%lx %lx %lx "
				"xmt %lx:%lx:%lx:%lx:%lx %lx %lx\n",
				 __func__,
			     slicstats->rcv_unicasts100,
			     slicstats->rcv_bytes100,
			     slicstats->rcv_bytes100,
			     slicstats->rcv_tcp_bytes100,
			     slicstats->rcv_tcp_segs100,
			     slicstats->rcv_other_error100,
			     slicstats->rcv_drops100,
			     slicstats->xmit_unicasts100,
			     slicstats->xmit_bytes100,
			     slicstats->xmit_bytes100,
			     slicstats->xmit_tcp_bytes100,
			     slicstats->xmit_tcp_segs100,
			     slicstats->xmit_other_error100,
			     slicstats->xmit_collisions100);*/
			UPDATE_STATS_GB(stst->tcp.xmit_tcp_segs,
					newstats->xmit_tcp_segs_gb,
					old->xmit_tcp_segs_gb);

			UPDATE_STATS_GB(stst->tcp.xmit_tcp_bytes,
					newstats->xmit_tcp_bytes_gb,
					old->xmit_tcp_bytes_gb);

			UPDATE_STATS_GB(stst->tcp.rcv_tcp_segs,
					newstats->rcv_tcp_segs_gb,
					old->rcv_tcp_segs_gb);

			UPDATE_STATS_GB(stst->tcp.rcv_tcp_bytes,
					newstats->rcv_tcp_bytes_gb,
					old->rcv_tcp_bytes_gb);

			UPDATE_STATS_GB(stst->iface.xmt_bytes,
					newstats->xmit_bytes_gb,
					old->xmit_bytes_gb);

			UPDATE_STATS_GB(stst->iface.xmt_ucast,
					newstats->xmit_unicasts_gb,
					old->xmit_unicasts_gb);

			UPDATE_STATS_GB(stst->iface.rcv_bytes,
					newstats->rcv_bytes_gb,
					old->rcv_bytes_gb);

			UPDATE_STATS_GB(stst->iface.rcv_ucast,
					newstats->rcv_unicasts_gb,
					old->rcv_unicasts_gb);

			UPDATE_STATS_GB(stst->iface.xmt_errors,
					newstats->xmit_collisions_gb,
					old->xmit_collisions_gb);

			UPDATE_STATS_GB(stst->iface.xmt_errors,
					newstats->xmit_excess_collisions_gb,
					old->xmit_excess_collisions_gb);

			UPDATE_STATS_GB(stst->iface.xmt_errors,
					newstats->xmit_other_error_gb,
					old->xmit_other_error_gb);

			UPDATE_STATS_GB(stst->iface.rcv_errors,
					newstats->rcv_other_error_gb,
					old->rcv_other_error_gb);

			UPDATE_STATS_GB(stst->iface.rcv_discards,
					newstats->rcv_drops_gb,
					old->rcv_drops_gb);

			if (newstats->rcv_drops_gb > old->rcv_drops_gb) {
				adapter->rcv_drops +=
				    (newstats->rcv_drops_gb -
				     old->rcv_drops_gb);
			}
			memcpy(old, newstats, sizeof(slic_stats_t));
#endif
			break;
		}
	case SLIC_UPR_RLSR:
		slic_link_upr_complete(adapter, isr);
		break;
	case SLIC_UPR_RCONFIG:
		break;
	case SLIC_UPR_RPHY:
		ASSERT(0);
		break;
	case SLIC_UPR_ENLB:
		ASSERT(0);
		break;
	case SLIC_UPR_ENCT:
		ASSERT(0);
		break;
	case SLIC_UPR_PDWN:
		ASSERT(0);
		break;
	case SLIC_UPR_PING:
		card->pingstatus |= (isr & ISR_PINGDSMASK);
		break;
#if SLIC_DUMP_ENABLED
	case SLIC_UPR_DUMP:
		card->dumpstatus |= (isr & ISR_UPCMASK);
		break;
#endif
	default:
		ASSERT(0);
	}
	SLIC_DEALLOCATE_MEM(upr);
	slic_upr_start(adapter);
	SLIC_RELEASE_IRQ_SPINLOCK(adapter->upr_lock);
}

void slic_upr_start(p_adapter_t adapter)
{
	p_slic_upr_t upr;
	p_slic_regs_t slic_regs = adapter->slic_regs;
/*
    char * ptr1;
    char * ptr2;
    uint cmdoffset;
*/
	upr = adapter->upr_list;
	if (!upr)
		return;
	if (adapter->upr_busy)
		return;
	adapter->upr_busy = 1;

	switch (upr->upr_request) {
	case SLIC_UPR_STATS:
		if (upr->upr_data_h == 0) {
			WRITE_REG(slic_regs->slic_stats, upr->upr_data, FLUSH);
		} else {
			WRITE_REG64(adapter,
				    slic_regs->slic_stats64,
				    upr->upr_data,
				    slic_regs->slic_addr_upper,
				    upr->upr_data_h, FLUSH);
		}
		break;

	case SLIC_UPR_RLSR:
		WRITE_REG64(adapter,
			    slic_regs->slic_rlsr,
			    upr->upr_data,
			    slic_regs->slic_addr_upper, upr->upr_data_h, FLUSH);
		break;

	case SLIC_UPR_RCONFIG:
		DBG_MSG("%s SLIC_UPR_RCONFIG!!!!\n", __func__);
		DBG_MSG("WRITE_REG64 adapter[%p]\n"
			"    a->slic_regs[%p] slic_regs[%p]\n"
			"    &slic_rconfig[%p] &slic_addr_upper[%p]\n"
			"    upr[%p]\n"
			"    uprdata[%x] uprdatah[%x]\n",
			adapter, adapter->slic_regs, slic_regs,
			&slic_regs->slic_rconfig, &slic_regs->slic_addr_upper,
			upr, upr->upr_data, upr->upr_data_h);
		WRITE_REG64(adapter,
			    slic_regs->slic_rconfig,
			    upr->upr_data,
			    slic_regs->slic_addr_upper, upr->upr_data_h, FLUSH);
		break;
#if SLIC_DUMP_ENABLED
	case SLIC_UPR_DUMP:
#if 0
		DBG_MSG("%s SLIC_UPR_DUMP!!!!\n", __func__);
		DBG_MSG("WRITE_REG64 adapter[%p]\n"
			 "    upr_buffer[%x]   upr_bufferh[%x]\n"
			 "    upr_data[%x]     upr_datah[%x]\n"
			 "    cmdbuff[%p] cmdbuffP[%p]\n"
			 "    dumpbuff[%p] dumpbuffP[%p]\n",
			 adapter, upr->upr_buffer, upr->upr_buffer_h,
			 upr->upr_data, upr->upr_data_h,
			 adapter->card->cmdbuffer,
			 (void *)adapter->card->cmdbuffer_phys,
			 adapter->card->dumpbuffer, (
			 void *)adapter->card->dumpbuffer_phys);

		ptr1 = (char *)slic_regs;
		ptr2 = (char *)(&slic_regs->slic_dump_cmd);
		cmdoffset = ptr2 - ptr1;
		DBG_MSG("slic_dump_cmd register offset [%x]\n", cmdoffset);
#endif
		if (upr->upr_buffer || upr->upr_buffer_h) {
			WRITE_REG64(adapter,
				    slic_regs->slic_dump_data,
				    upr->upr_buffer,
				    slic_regs->slic_addr_upper,
				    upr->upr_buffer_h, FLUSH);
		}
		WRITE_REG64(adapter,
			    slic_regs->slic_dump_cmd,
			    upr->upr_data,
			    slic_regs->slic_addr_upper, upr->upr_data_h, FLUSH);
		break;
#endif
	case SLIC_UPR_PING:
		WRITE_REG(slic_regs->slic_ping, 1, FLUSH);
		break;
	default:
		ASSERT(0);
	}
}

void slic_link_upr_complete(p_adapter_t adapter, ulong32 isr)
{
	ulong32 linkstatus = adapter->pshmem->linkstatus;
	uint linkup;
	uchar linkspeed;
	uchar linkduplex;

	DBG_MSG("%s: %s ISR[%x] linkstatus[%x]\n   adapter[%p](%d)\n",
		__func__, adapter->netdev->name, isr, linkstatus, adapter,
		adapter->cardindex);

	if ((isr & ISR_UPCERR) || (isr & ISR_UPCBSY)) {
		p_slic_shmem_t pshmem;

		pshmem = (p_slic_shmem_t) adapter->phys_shmem;
#if defined(CONFIG_X86_64)
		slic_upr_queue_request(adapter,
				       SLIC_UPR_RLSR,
				       SLIC_GET_ADDR_LOW(&pshmem->linkstatus),
				       SLIC_GET_ADDR_HIGH(&pshmem->linkstatus),
				       0, 0);
#elif defined(CONFIG_X86)
		slic_upr_queue_request(adapter,
				       SLIC_UPR_RLSR,
				       (ulong32) &pshmem->linkstatus,
				       SLIC_GET_ADDR_HIGH(pshmem), 0, 0);
#else
		Stop Compilation;
#endif
		return;
	}
	if (adapter->state != ADAPT_UP)
		return;

	ASSERT((adapter->devid == SLIC_1GB_DEVICE_ID)
	       || (adapter->devid == SLIC_2GB_DEVICE_ID));

	linkup = linkstatus & GIG_LINKUP ? LINK_UP : LINK_DOWN;
	if (linkstatus & GIG_SPEED_1000) {
		linkspeed = LINK_1000MB;
		DBG_MSG("slicoss: %s (%s) GIGABIT Speed==1000MB  ",
			__func__, adapter->netdev->name);
	} else if (linkstatus & GIG_SPEED_100) {
		linkspeed = LINK_100MB;
		DBG_MSG("slicoss: %s (%s) GIGABIT Speed==100MB  ", __func__,
			adapter->netdev->name);
	} else {
		linkspeed = LINK_10MB;
		DBG_MSG("slicoss: %s (%s) GIGABIT Speed==10MB  ", __func__,
			adapter->netdev->name);
	}
	if (linkstatus & GIG_FULLDUPLEX) {
		linkduplex = LINK_FULLD;
		DBG_MSG(" Duplex == FULL\n");
	} else {
		linkduplex = LINK_HALFD;
		DBG_MSG(" Duplex == HALF\n");
	}

	if ((adapter->linkstate == LINK_DOWN) && (linkup == LINK_DOWN)) {
		DBG_MSG("slicoss: %s (%s) physport(%d) link still down\n",
			__func__, adapter->netdev->name, adapter->physport);
		return;
	}

	/* link up event, but nothing has changed */
	if ((adapter->linkstate == LINK_UP) &&
	    (linkup == LINK_UP) &&
	    (adapter->linkspeed == linkspeed) &&
	    (adapter->linkduplex == linkduplex)) {
		DBG_MSG("slicoss: %s (%s) port(%d) link still up\n",
			__func__, adapter->netdev->name, adapter->physport);
		return;
	}

	/* link has changed at this point */

	/* link has gone from up to down */
	if (linkup == LINK_DOWN) {
		adapter->linkstate = LINK_DOWN;
		DBG_MSG("slicoss: %s %d LinkDown!\n", __func__,
			adapter->physport);
		return;
	}

	/* link has gone from down to up */
	adapter->linkspeed = linkspeed;
	adapter->linkduplex = linkduplex;

	if (adapter->linkstate != LINK_UP) {
		/* setup the mac */
		DBG_MSG("%s call slic_config_set\n", __func__);
		slic_config_set(adapter, TRUE);
		adapter->linkstate = LINK_UP;
		DBG_MSG("\n(%s) Link UP: CALL slic_if_start_queue",
			adapter->netdev->name);
		slic_if_start_queue(adapter);
	}
#if 1
	switch (linkspeed) {
	case LINK_1000MB:
		DBG_MSG
		    ("\n(%s) LINK UP!: GIGABIT SPEED == 1000MB  duplex[%x]\n",
		     adapter->netdev->name, adapter->linkduplex);
		break;
	case LINK_100MB:
		DBG_MSG("\n(%s) LINK UP!: SPEED == 100MB  duplex[%x]\n",
			adapter->netdev->name, adapter->linkduplex);
		break;
	default:
		DBG_MSG("\n(%s) LINK UP!: SPEED == 10MB  duplex[%x]\n",
			adapter->netdev->name, adapter->linkduplex);
		break;
	}
#endif
}

/*
 *  this is here to checksum the eeprom, there is some ucode bug
 *  which prevens us from using the ucode result.
 *  remove this once ucode is fixed.
 */
ushort slic_eeprom_cksum(pchar m, int len)
{
#define ADDCARRY(x)  (x > 65535 ? x -= 65535 : x)
#define REDUCE {l_util.l = sum; sum = l_util.s[0] + l_util.s[1]; ADDCARRY(sum);\
		}

	pushort w;
	ulong32 sum = 0;
	ulong32 byte_swapped = 0;
	ulong32 w_int;

	union {
		char c[2];
		ushort s;
	} s_util;

	union {
		ushort s[2];
		int l;
	} l_util;

	l_util.l = 0;
	s_util.s = 0;

	w = (pushort) m;
#ifdef CONFIG_X86_64
	w_int = (ulong32) ((ulong) w & 0x00000000FFFFFFFF);
#else
	w_int = (ulong32) (w);
#endif
	if ((1 & w_int) && (len > 0)) {
		REDUCE;
		sum <<= 8;
		s_util.c[0] = *(puchar) w;
		w = (pushort) ((char *)w + 1);
		len--;
		byte_swapped = 1;
	}

	/* Unroll the loop to make overhead from branches &c small. */
	while ((len -= 32) >= 0) {
		sum += w[0];
		sum += w[1];
		sum += w[2];
		sum += w[3];
		sum += w[4];
		sum += w[5];
		sum += w[6];
		sum += w[7];
		sum += w[8];
		sum += w[9];
		sum += w[10];
		sum += w[11];
		sum += w[12];
		sum += w[13];
		sum += w[14];
		sum += w[15];
		w = (pushort) ((ulong) w + 16);	/* verify */
	}
	len += 32;
	while ((len -= 8) >= 0) {
		sum += w[0];
		sum += w[1];
		sum += w[2];
		sum += w[3];
		w = (pushort) ((ulong) w + 4);	/* verify */
	}
	len += 8;
	if (len != 0 || byte_swapped != 0) {
		REDUCE;
		while ((len -= 2) >= 0)
			sum += *w++;	/* verify */
		if (byte_swapped) {
			REDUCE;
			sum <<= 8;
			byte_swapped = 0;
			if (len == -1) {
				s_util.c[1] = *(pchar) w;
				sum += s_util.s;
				len = 0;
			} else {
				len = -1;
			}

		} else if (len == -1) {
			s_util.c[0] = *(pchar) w;
		}

		if (len == -1) {
			s_util.c[1] = 0;
			sum += s_util.s;
		}
	}
	REDUCE;
	return (ushort) sum;
}

int slic_rspqueue_init(p_adapter_t adapter)
{
	int i;
	p_slic_rspqueue_t rspq = &adapter->rspqueue;
	p_slic_regs_t slic_regs = adapter->slic_regs;
	ulong32 paddrh = 0;

	DBG_MSG("slicoss: %s (%s) ENTER adapter[%p]\n", __func__,
		adapter->netdev->name, adapter);
	ASSERT(adapter->state == ADAPT_DOWN);
	SLIC_ZERO_MEMORY(rspq, sizeof(slic_rspqueue_t));

	rspq->num_pages = SLIC_RSPQ_PAGES_GB;

	for (i = 0; i < rspq->num_pages; i++) {
		rspq->vaddr[i] =
		    pci_alloc_consistent(adapter->pcidev, PAGE_SIZE,
					 &rspq->paddr[i]);
		if (!rspq->vaddr[i]) {
			DBG_ERROR
			    ("rspqueue_init_failed  pci_alloc_consistent\n");
			slic_rspqueue_free(adapter);
			return STATUS_FAILURE;
		}
#ifndef CONFIG_X86_64
		ASSERT(((ulong32) rspq->vaddr[i] & 0xFFFFF000) ==
		       (ulong32) rspq->vaddr[i]);
		ASSERT(((ulong32) rspq->paddr[i] & 0xFFFFF000) ==
		       (ulong32) rspq->paddr[i]);
#endif
		SLIC_ZERO_MEMORY(rspq->vaddr[i], PAGE_SIZE);
/*              DBG_MSG("slicoss: %s UPLOAD RSPBUFF Page pageix[%x] paddr[%p] "
			"vaddr[%p]\n",
			__func__, i, (void *)rspq->paddr[i], rspq->vaddr[i]); */

		if (paddrh == 0) {
			WRITE_REG(slic_regs->slic_rbar,
				  (rspq->paddr[i] | SLIC_RSPQ_BUFSINPAGE),
				  DONT_FLUSH);
		} else {
			WRITE_REG64(adapter,
				    slic_regs->slic_rbar64,
				    (rspq->paddr[i] | SLIC_RSPQ_BUFSINPAGE),
				    slic_regs->slic_addr_upper,
				    paddrh, DONT_FLUSH);
		}
	}
	rspq->offset = 0;
	rspq->pageindex = 0;
	rspq->rspbuf = (p_slic_rspbuf_t) rspq->vaddr[0];
	DBG_MSG("slicoss: %s (%s) EXIT adapter[%p]\n", __func__,
		adapter->netdev->name, adapter);
	return STATUS_SUCCESS;
}

int slic_rspqueue_reset(p_adapter_t adapter)
{
	p_slic_rspqueue_t rspq = &adapter->rspqueue;

	DBG_MSG("slicoss: %s (%s) ENTER adapter[%p]\n", __func__,
		adapter->netdev->name, adapter);
	ASSERT(adapter->state == ADAPT_DOWN);
	ASSERT(rspq);

	DBG_MSG("slicoss: Nothing to do. rspq[%p]\n"
		"                             offset[%x]\n"
		"                             pageix[%x]\n"
		"                             rspbuf[%p]\n",
		rspq, rspq->offset, rspq->pageindex, rspq->rspbuf);

	DBG_MSG("slicoss: %s (%s) EXIT adapter[%p]\n", __func__,
		adapter->netdev->name, adapter);
	return STATUS_SUCCESS;
}

void slic_rspqueue_free(p_adapter_t adapter)
{
	int i;
	slic_rspqueue_t *rspq = &adapter->rspqueue;

	DBG_MSG("slicoss: %s adapter[%p] port %d rspq[%p] FreeRSPQ\n",
		__func__, adapter, adapter->physport, rspq);
	for (i = 0; i < rspq->num_pages; i++) {
		if (rspq->vaddr[i]) {
			DBG_MSG
			    ("slicoss:  pci_free_consistent rspq->vaddr[%p] \
			    paddr[%p]\n",
			     rspq->vaddr[i], (pvoid) rspq->paddr[i]);
			pci_free_consistent(adapter->pcidev, PAGE_SIZE,
					    rspq->vaddr[i], rspq->paddr[i]);
		}
		rspq->vaddr[i] = NULL;
		rspq->paddr[i] = 0;
	}
	rspq->offset = 0;
	rspq->pageindex = 0;
	rspq->rspbuf = NULL;
}

p_slic_rspbuf_t slic_rspqueue_getnext(p_adapter_t adapter)
{
	p_slic_rspqueue_t rspq = &adapter->rspqueue;
	p_slic_rspbuf_t buf;

	if (!(rspq->rspbuf->status))
		return NULL;

	buf = rspq->rspbuf;
#ifndef CONFIG_X86_64
	ASSERT((buf->status & 0xFFFFFFE0) == 0);
#endif
	ASSERT(buf->hosthandle);
	if (++rspq->offset < SLIC_RSPQ_BUFSINPAGE) {
		rspq->rspbuf++;
#ifndef CONFIG_X86_64
		ASSERT(((ulong32) rspq->rspbuf & 0xFFFFFFE0) ==
		       (ulong32) rspq->rspbuf);
#endif
	} else {
		ASSERT(rspq->offset == SLIC_RSPQ_BUFSINPAGE);
		WRITE_REG64(adapter,
			    adapter->slic_regs->slic_rbar64,
			    (rspq->
			     paddr[rspq->pageindex] | SLIC_RSPQ_BUFSINPAGE),
			    adapter->slic_regs->slic_addr_upper, 0, DONT_FLUSH);
		rspq->pageindex = (++rspq->pageindex) % rspq->num_pages;
		rspq->offset = 0;
		rspq->rspbuf = (p_slic_rspbuf_t) rspq->vaddr[rspq->pageindex];
#ifndef CONFIG_X86_64
		ASSERT(((ulong32) rspq->rspbuf & 0xFFFFF000) ==
		       (ulong32) rspq->rspbuf);
#endif
	}
#ifndef CONFIG_X86_64
	ASSERT(((ulong32) buf & 0xFFFFFFE0) == (ulong32) buf);
#endif
	return buf;
}

void slic_cmdqmem_init(p_adapter_t adapter)
{
	slic_cmdqmem_t *cmdqmem = &adapter->cmdqmem;

	SLIC_ZERO_MEMORY(cmdqmem, sizeof(slic_cmdqmem_t));
}

void slic_cmdqmem_free(p_adapter_t adapter)
{
	slic_cmdqmem_t *cmdqmem = &adapter->cmdqmem;
	int i;

	DBG_MSG("slicoss: (%s) adapter[%p] port %d rspq[%p] Free CMDQ Memory\n",
		__func__, adapter, adapter->physport, cmdqmem);
	for (i = 0; i < SLIC_CMDQ_MAXPAGES; i++) {
		if (cmdqmem->pages[i]) {
			DBG_MSG("slicoss: %s Deallocate page  CmdQPage[%p]\n",
				__func__, (pvoid) cmdqmem->pages[i]);
			pci_free_consistent(adapter->pcidev,
					    PAGE_SIZE,
					    (pvoid) cmdqmem->pages[i],
					    cmdqmem->dma_pages[i]);
		}
	}
	SLIC_ZERO_MEMORY(cmdqmem, sizeof(slic_cmdqmem_t));
}

pulong32 slic_cmdqmem_addpage(p_adapter_t adapter)
{
	p_slic_cmdqmem_t cmdqmem = &adapter->cmdqmem;
	pulong32 pageaddr;

	if (cmdqmem->pagecnt >= SLIC_CMDQ_MAXPAGES)
		return NULL;
	pageaddr = pci_alloc_consistent(adapter->pcidev,
					PAGE_SIZE,
					&cmdqmem->dma_pages[cmdqmem->pagecnt]);
	if (!pageaddr)
		return NULL;
#ifndef CONFIG_X86_64
	ASSERT(((ulong32) pageaddr & 0xFFFFF000) == (ulong32) pageaddr);
#endif
	cmdqmem->pages[cmdqmem->pagecnt] = pageaddr;
	cmdqmem->pagecnt++;
	return pageaddr;
}

int slic_cmdq_init(p_adapter_t adapter)
{
	int i;
	pulong32 pageaddr;

	DBG_MSG("slicoss: %s ENTER adapter[%p]\n", __func__, adapter);
	ASSERT(adapter->state == ADAPT_DOWN);
	SLIC_ZERO_MEMORY(&adapter->cmdq_all, sizeof(slic_cmdqueue_t));
	SLIC_ZERO_MEMORY(&adapter->cmdq_free, sizeof(slic_cmdqueue_t));
	SLIC_ZERO_MEMORY(&adapter->cmdq_done, sizeof(slic_cmdqueue_t));
	SLIC_INIT_SPINLOCK(adapter->cmdq_all.lock);
	SLIC_INIT_SPINLOCK(adapter->cmdq_free.lock);
	SLIC_INIT_SPINLOCK(adapter->cmdq_done.lock);
	slic_cmdqmem_init(adapter);
	adapter->slic_handle_ix = 1;
	for (i = 0; i < SLIC_CMDQ_INITPAGES; i++) {
		pageaddr = slic_cmdqmem_addpage(adapter);
#ifndef CONFIG_X86_64
		ASSERT(((ulong32) pageaddr & 0xFFFFF000) == (ulong32) pageaddr);
#endif
		if (!pageaddr) {
			slic_cmdq_free(adapter);
			return STATUS_FAILURE;
		}
		slic_cmdq_addcmdpage(adapter, pageaddr);
	}
	adapter->slic_handle_ix = 1;
	DBG_MSG("slicoss: %s reset slic_handle_ix to ONE\n", __func__);

	return STATUS_SUCCESS;
}

void slic_cmdq_free(p_adapter_t adapter)
{
	p_slic_hostcmd_t cmd;

	DBG_MSG("slicoss: %s adapter[%p] port %d FreeCommandsFrom CMDQ\n",
		__func__, adapter, adapter->physport);
	cmd = adapter->cmdq_all.head;
	while (cmd) {
		if (cmd->busy) {
			struct sk_buff *tempskb;

			tempskb = cmd->skb;
			if (tempskb) {
				cmd->skb = NULL;
				dev_kfree_skb_irq(tempskb);
			}
		}
		cmd = cmd->next_all;
	}
	SLIC_ZERO_MEMORY(&adapter->cmdq_all, sizeof(slic_cmdqueue_t));
	SLIC_ZERO_MEMORY(&adapter->cmdq_free, sizeof(slic_cmdqueue_t));
	SLIC_ZERO_MEMORY(&adapter->cmdq_done, sizeof(slic_cmdqueue_t));
	slic_cmdqmem_free(adapter);
}

void slic_cmdq_reset(p_adapter_t adapter)
{
	p_slic_hostcmd_t hcmd;
	struct sk_buff *skb;
	ulong32 outstanding;

	DBG_MSG("%s ENTER adapter[%p]\n", __func__, adapter);
	SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->cmdq_free.lock);
	SLIC_ACQUIRE_IRQ_SPINLOCK(adapter->cmdq_done.lock);
	outstanding = adapter->cmdq_all.count - adapter->cmdq_done.count;
	outstanding -= adapter->cmdq_free.count;
	hcmd = adapter->cmdq_all.head;
	while (hcmd) {
		if (hcmd->busy) {
			skb = hcmd->skb;
			ASSERT(skb);
			DBG_MSG("slicoss: %s hcmd[%p] skb[%p] ", __func__,
				hcmd, skb);
			hcmd->busy = 0;
			hcmd->skb = NULL;
			DBG_MSG(" Free SKB\n");
			dev_kfree_skb_irq(skb);
		}
		hcmd = hcmd->next_all;
	}
	adapter->cmdq_free.count = 0;
	adapter->cmdq_free.head = NULL;
	adapter->cmdq_free.tail = NULL;
	adapter->cmdq_done.count = 0;
	adapter->cmdq_done.head = NULL;
	adapter->cmdq_done.tail = NULL;
	adapter->cmdq_free.head = adapter->cmdq_all.head;
	hcmd = adapter->cmdq_all.head;
	while (hcmd) {
		adapter->cmdq_free.count++;
		hcmd->next = hcmd->next_all;
		hcmd = hcmd->next_all;
	}
	if (adapter->cmdq_free.count != adapter->cmdq_all.count) {
		DBG_ERROR("%s free_count %d != all count %d\n", __func__,
			  adapter->cmdq_free.count, adapter->cmdq_all.count);
	}
	SLIC_RELEASE_IRQ_SPINLOCK(adapter->cmdq_done.lock);
	SLIC_RELEASE_IRQ_SPINLOCK(adapter->cmdq_free.lock);
	DBG_MSG("%s EXIT adapter[%p]\n", __func__, adapter);
}

void slic_cmdq_addcmdpage(p_adapter_t adapter, pulong32 page)
{
	p_slic_hostcmd_t cmd;
	p_slic_hostcmd_t prev;
	p_slic_hostcmd_t tail;
	p_slic_cmdqueue_t cmdq;
	int cmdcnt;
	pvoid cmdaddr;
	ulong phys_addr;
	ulong32 phys_addrl;
	ulong32 phys_addrh;
	pslic_handle_t pslic_handle;

	cmdaddr = page;
	cmd = (p_slic_hostcmd_t) cmdaddr;
/*  DBG_MSG("CMDQ Page addr[%p] ix[%d] pfree[%p]\n", cmdaddr, slic_handle_ix,
    adapter->pfree_slic_handles); */
	cmdcnt = 0;

	phys_addr = SLIC_GET_PHYSICAL_ADDRESS((void *)page);
	phys_addrl = SLIC_GET_ADDR_LOW(phys_addr);
	phys_addrh = SLIC_GET_ADDR_HIGH(phys_addr);

	prev = NULL;
	tail = cmd;
	while ((cmdcnt < SLIC_CMDQ_CMDSINPAGE) &&
	       (adapter->slic_handle_ix < 256)) {
		/* Allocate and initialize a SLIC_HANDLE for this command */
		SLIC_GET_SLIC_HANDLE(adapter, pslic_handle);
		if (pslic_handle == NULL)
			ASSERT(0);
		ASSERT(pslic_handle ==
		       &adapter->slic_handles[pslic_handle->token.
					      handle_index]);
		pslic_handle->type = SLIC_HANDLE_CMD;
		pslic_handle->address = (pvoid) cmd;
		pslic_handle->offset = (ushort) adapter->slic_handle_ix++;
		pslic_handle->other_handle = NULL;
		pslic_handle->next = NULL;

		cmd->pslic_handle = pslic_handle;
		cmd->cmd64.hosthandle = pslic_handle->token.handle_token;
		cmd->busy = FALSE;
		cmd->paddrl = phys_addrl;
		cmd->paddrh = phys_addrh;
		cmd->next_all = prev;
		cmd->next = prev;
		prev = cmd;
		phys_addrl += SLIC_HOSTCMD_SIZE;
		cmdaddr += SLIC_HOSTCMD_SIZE;

		cmd = (p_slic_hostcmd_t) cmdaddr;
		cmdcnt++;
	}

	cmdq = &adapter->cmdq_all;
	cmdq->count += cmdcnt;	/*  SLIC_CMDQ_CMDSINPAGE;   mooktodo */
	tail->next_all = cmdq->head;
	ASSERT(VALID_ADDRESS(prev));
	cmdq->head = prev;
	cmdq = &adapter->cmdq_free;
	SLIC_ACQUIRE_IRQ_SPINLOCK(cmdq->lock);
	cmdq->count += cmdcnt;	/*  SLIC_CMDQ_CMDSINPAGE;   mooktodo */
	tail->next = cmdq->head;
	ASSERT(VALID_ADDRESS(prev));
	cmdq->head = prev;
	SLIC_RELEASE_IRQ_SPINLOCK(cmdq->lock);
}

p_slic_hostcmd_t slic_cmdq_getfree(p_adapter_t adapter)
{
	p_slic_cmdqueue_t cmdq = &adapter->cmdq_free;
	p_slic_hostcmd_t cmd = NULL;

lock_and_retry:
	SLIC_ACQUIRE_IRQ_SPINLOCK(cmdq->lock);
retry:
	cmd = cmdq->head;
	if (cmd) {
		cmdq->head = cmd->next;
		cmdq->count--;
		SLIC_RELEASE_IRQ_SPINLOCK(cmdq->lock);
	} else {
		slic_cmdq_getdone(adapter);
		cmd = cmdq->head;
		if (cmd) {
			goto retry;
		} else {
			pulong32 pageaddr;

			SLIC_RELEASE_IRQ_SPINLOCK(cmdq->lock);
			pageaddr = slic_cmdqmem_addpage(adapter);
			if (pageaddr) {
				slic_cmdq_addcmdpage(adapter, pageaddr);
				goto lock_and_retry;
			}
		}
	}
	return cmd;
}

void slic_cmdq_getdone(p_adapter_t adapter)
{
	p_slic_cmdqueue_t done_cmdq = &adapter->cmdq_done;
	p_slic_cmdqueue_t free_cmdq = &adapter->cmdq_free;

	ASSERT(free_cmdq->head == NULL);
	SLIC_ACQUIRE_IRQ_SPINLOCK(done_cmdq->lock);
	ASSERT(VALID_ADDRESS(done_cmdq->head));

	free_cmdq->head = done_cmdq->head;
	free_cmdq->count = done_cmdq->count;
	done_cmdq->head = NULL;
	done_cmdq->tail = NULL;
	done_cmdq->count = 0;
	SLIC_RELEASE_IRQ_SPINLOCK(done_cmdq->lock);
}

void slic_cmdq_putdone(p_adapter_t adapter, p_slic_hostcmd_t cmd)
{
	p_slic_cmdqueue_t cmdq = &adapter->cmdq_done;

	SLIC_ACQUIRE_IRQ_SPINLOCK(cmdq->lock);
	cmd->busy = 0;
	ASSERT(VALID_ADDRESS(cmdq->head));
	cmd->next = cmdq->head;
	ASSERT(VALID_ADDRESS(cmd));
	cmdq->head = cmd;
	cmdq->count++;
	SLIC_RELEASE_IRQ_SPINLOCK(cmdq->lock);
}

void slic_cmdq_putdone_irq(p_adapter_t adapter, p_slic_hostcmd_t cmd)
{
	p_slic_cmdqueue_t cmdq = &adapter->cmdq_done;

	SLIC_ACQUIRE_SPINLOCK(cmdq->lock);
	cmd->busy = 0;
	ASSERT(VALID_ADDRESS(cmdq->head));
	cmd->next = cmdq->head;
	ASSERT(VALID_ADDRESS(cmd));
	cmdq->head = cmd;
	cmdq->count++;
	if ((adapter->xmitq_full) && (cmdq->count > 10))
		netif_wake_queue(adapter->netdev);
	SLIC_RELEASE_SPINLOCK(cmdq->lock);
}

int slic_rcvqueue_init(p_adapter_t adapter)
{
	int i, count;
	p_slic_rcvqueue_t rcvq = &adapter->rcvqueue;

	DBG_MSG("slicoss: %s ENTER adapter[%p]\n", __func__, adapter);
	ASSERT(adapter->state == ADAPT_DOWN);
	rcvq->tail = NULL;
	rcvq->head = NULL;
	rcvq->size = SLIC_RCVQ_ENTRIES;
	rcvq->errors = 0;
	rcvq->count = 0;
	i = (SLIC_RCVQ_ENTRIES / SLIC_RCVQ_FILLENTRIES);
	count = 0;
	while (i) {
		count += slic_rcvqueue_fill(adapter);
		i--;
	}
	if (rcvq->count < SLIC_RCVQ_MINENTRIES) {
		slic_rcvqueue_free(adapter);
		return STATUS_FAILURE;
	}
	DBG_MSG("slicoss: %s EXIT adapter[%p]\n", __func__, adapter);
	return STATUS_SUCCESS;
}

int slic_rcvqueue_reset(p_adapter_t adapter)
{
	p_slic_rcvqueue_t rcvq = &adapter->rcvqueue;

	DBG_MSG("slicoss: %s ENTER adapter[%p]\n", __func__, adapter);
	ASSERT(adapter->state == ADAPT_DOWN);
	ASSERT(rcvq);

	DBG_MSG("slicoss: Nothing to do. rcvq[%p]\n"
		"                             count[%x]\n"
		"                             head[%p]\n"
		"                             tail[%p]\n",
		rcvq, rcvq->count, rcvq->head, rcvq->tail);

	DBG_MSG("slicoss: %s EXIT adapter[%p]\n", __func__, adapter);
	return STATUS_SUCCESS;
}

void slic_rcvqueue_free(p_adapter_t adapter)
{
	slic_rcvqueue_t *rcvq = &adapter->rcvqueue;
	struct sk_buff *skb;

	while (rcvq->head) {
		skb = rcvq->head;
		rcvq->head = rcvq->head->next;
		dev_kfree_skb(skb);
	}
	rcvq->tail = NULL;
	rcvq->head = NULL;
	rcvq->count = 0;
}

struct sk_buff *slic_rcvqueue_getnext(p_adapter_t adapter)
{
	p_slic_rcvqueue_t rcvq = &adapter->rcvqueue;
	struct sk_buff *skb;
	p_slic_rcvbuf_t rcvbuf;
	int count;

	if (rcvq->count) {
		skb = rcvq->head;
		rcvbuf = (p_slic_rcvbuf_t) skb->head;
		ASSERT(rcvbuf);

		if (rcvbuf->status & IRHDDR_SVALID) {
			rcvq->head = rcvq->head->next;
			skb->next = NULL;
			rcvq->count--;
		} else {
			skb = NULL;
		}
	} else {
		DBG_ERROR("RcvQ Empty!! adapter[%p] rcvq[%p] count[%x]\n",
			  adapter, rcvq, rcvq->count);
		skb = NULL;
	}
	while (rcvq->count < SLIC_RCVQ_FILLTHRESH) {
		count = slic_rcvqueue_fill(adapter);
		if (!count)
			break;
	}
	if (skb)
		rcvq->errors = 0;
	return skb;
}

int slic_rcvqueue_fill(p_adapter_t adapter)
{
	pvoid paddr;
	ulong32 paddrl;
	ulong32 paddrh;
	p_slic_rcvqueue_t rcvq = &adapter->rcvqueue;
	int i = 0;

	while (i < SLIC_RCVQ_FILLENTRIES) {
		p_slic_rcvbuf_t rcvbuf;
		struct sk_buff *skb;
#ifdef KLUDGE_FOR_4GB_BOUNDARY
retry_rcvqfill:
#endif
		skb = alloc_skb(SLIC_RCVQ_RCVBUFSIZE, GFP_ATOMIC);
		if (skb) {
			paddr = (void *)SLIC_GET_DMA_ADDRESS_READ(adapter,
							  skb->data,
							  SLIC_RCVQ_RCVBUFSIZE);
			paddrl = SLIC_GET_ADDR_LOW(paddr);
			paddrh = SLIC_GET_ADDR_HIGH(paddr);

			skb->len = SLIC_RCVBUF_HEADSIZE;
			rcvbuf = (p_slic_rcvbuf_t) skb->head;
			rcvbuf->status = 0;
			skb->next = NULL;
#ifdef KLUDGE_FOR_4GB_BOUNDARY
			if (paddrl == 0) {
				DBG_ERROR
				    ("%s: LOW 32bits PHYSICAL ADDRESS == 0 "
				     "skb[%p]   PROBLEM\n"
				     "         skbdata[%p]\n"
				     "         skblen[%x]\n"
				     "         paddr[%p]\n"
				     "         paddrl[%x]\n"
				     "         paddrh[%x]\n", __func__, skb,
				     skb->data, skb->len, paddr, paddrl,
				     paddrh);
				DBG_ERROR("         rcvq->head[%p]\n"
					  "         rcvq->tail[%p]\n"
					  "         rcvq->count[%x]\n",
					  rcvq->head, rcvq->tail, rcvq->count);
				DBG_ERROR("SKIP THIS SKB!!!!!!!!\n");
				goto retry_rcvqfill;
			}
#else
			if (paddrl == 0) {
				DBG_ERROR
				    ("\n\n%s: LOW 32bits PHYSICAL ADDRESS == 0 "
				     "skb[%p]  GIVE TO CARD ANYWAY\n"
				     "         skbdata[%p]\n"
				     "         paddr[%p]\n"
				     "         paddrl[%x]\n"
				     "         paddrh[%x]\n", __func__, skb,
				     skb->data, paddr, paddrl, paddrh);
			}
#endif
			if (paddrh == 0) {
				WRITE_REG(adapter->slic_regs->slic_hbar,
					  (ulong32) paddrl, DONT_FLUSH);
			} else {
				WRITE_REG64(adapter,
					    adapter->slic_regs->slic_hbar64,
					    (ulong32) paddrl,
					    adapter->slic_regs->slic_addr_upper,
					    (ulong32) paddrh, DONT_FLUSH);
			}
			if (rcvq->head)
				rcvq->tail->next = skb;
			else
				rcvq->head = skb;
			rcvq->tail = skb;
			rcvq->count++;
			i++;
		} else {
			DBG_ERROR
			    ("%s slic_rcvqueue_fill could only get [%d] "
			     "skbuffs\n",
			     adapter->netdev->name, i);
			break;
		}
	}
	return i;
}

ulong32 slic_rcvqueue_reinsert(p_adapter_t adapter, struct sk_buff *skb)
{
	p_slic_rcvqueue_t rcvq = &adapter->rcvqueue;
	pvoid paddr;
	ulong32 paddrl;
	ulong32 paddrh;
	p_slic_rcvbuf_t rcvbuf = (p_slic_rcvbuf_t) skb->head;

	ASSERT(skb->len == SLIC_RCVBUF_HEADSIZE);
	paddr = (void *)SLIC_GET_DMA_ADDRESS_READ(adapter,
						  skb->head,
						  SLIC_RCVQ_RCVBUFSIZE);
	rcvbuf->status = 0;
	skb->next = NULL;

	paddrl = SLIC_GET_ADDR_LOW(paddr);
	paddrh = SLIC_GET_ADDR_HIGH(paddr);

	if (paddrl == 0) {
		DBG_ERROR
		    ("%s: LOW 32bits PHYSICAL ADDRESS == 0 skb[%p]   PROBLEM\n"
		     "         skbdata[%p]\n" "         skblen[%x]\n"
		     "         paddr[%p]\n" "         paddrl[%x]\n"
		     "         paddrh[%x]\n", __func__, skb, skb->data,
		     skb->len, paddr, paddrl, paddrh);
		DBG_ERROR("         rcvq->head[%p]\n"
			  "         rcvq->tail[%p]\n"
			  "         rcvq->count[%x]\n", rcvq->head, rcvq->tail,
			  rcvq->count);
	}
	if (paddrh == 0) {
		WRITE_REG(adapter->slic_regs->slic_hbar, (ulong32) paddrl,
			  DONT_FLUSH);
	} else {
		WRITE_REG64(adapter,
			    adapter->slic_regs->slic_hbar64,
			    paddrl,
			    adapter->slic_regs->slic_addr_upper,
			    paddrh, DONT_FLUSH);
	}
	if (rcvq->head)
		rcvq->tail->next = skb;
	else
		rcvq->head = skb;
	rcvq->tail = skb;
	rcvq->count++;
	return rcvq->count;
}

static int slic_debug_card_show(struct seq_file *seq, void *v)
{
#ifdef MOOKTODO
	int i;
	p_sliccard_t card = seq->private;
	pslic_config_t config = &card->config;
	puchar fru = (puchar) (&card->config.atk_fru);
	puchar oemfru = (puchar) (&card->config.OemFru);
#endif

	seq_printf(seq, "driver_version           : %s", slic_proc_version);
	seq_printf(seq, "Microcode versions:           \n");
	seq_printf(seq, "    Gigabit (gb)         : %s %s\n",
		    MOJAVE_UCODE_VERS_STRING, MOJAVE_UCODE_VERS_DATE);
	seq_printf(seq, "    Gigabit Receiver     : %s %s\n",
		    GB_RCVUCODE_VERS_STRING, GB_RCVUCODE_VERS_DATE);
	seq_printf(seq, "Vendor                   : %s\n", slic_vendor);
	seq_printf(seq, "Product Name             : %s\n", slic_product_name);
#ifdef MOOKTODO
	seq_printf(seq, "VendorId                 : %4.4X\n",
		    config->VendorId);
	seq_printf(seq, "DeviceId                 : %4.4X\n",
		    config->DeviceId);
	seq_printf(seq, "RevisionId               : %2.2x\n",
		    config->RevisionId);
	seq_printf(seq, "Bus    #                 : %d\n", card->busnumber);
	seq_printf(seq, "Device #                 : %d\n", card->slotnumber);
	seq_printf(seq, "Interfaces               : %d\n", card->card_size);
	seq_printf(seq, "     Initialized         : %d\n",
		    card->adapters_activated);
	seq_printf(seq, "     Allocated           : %d\n",
		    card->adapters_allocated);
	ASSERT(card->card_size <= SLIC_NBR_MACS);
	for (i = 0; i < card->card_size; i++) {
		seq_printf(seq,
			   "     MAC%d : %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n",
			   i, config->macinfo[i].macaddrA[0],
			   config->macinfo[i].macaddrA[1],
			   config->macinfo[i].macaddrA[2],
			   config->macinfo[i].macaddrA[3],
			   config->macinfo[i].macaddrA[4],
			   config->macinfo[i].macaddrA[5]);
	}
	seq_printf(seq, "     IF  Init State Duplex/Speed irq\n");
	seq_printf(seq, "     -------------------------------\n");
	for (i = 0; i < card->adapters_allocated; i++) {
		p_adapter_t adapter;

		adapter = card->adapter[i];
		if (adapter) {
			seq_printf(seq,
				    "     %d   %d   %s  %s  %s    0x%X\n",
				    adapter->physport, adapter->state,
				    SLIC_LINKSTATE(adapter->linkstate),
				    SLIC_DUPLEX(adapter->linkduplex),
				    SLIC_SPEED(adapter->linkspeed),
				    (uint) adapter->irq);
		}
	}
	seq_printf(seq, "Generation #             : %4.4X\n", card->gennumber);
	seq_printf(seq, "RcvQ max entries         : %4.4X\n",
		    SLIC_RCVQ_ENTRIES);
	seq_printf(seq, "Ping Status              : %8.8X\n",
		    card->pingstatus);
	seq_printf(seq, "Minimum grant            : %2.2x\n",
		    config->MinGrant);
	seq_printf(seq, "Maximum Latency          : %2.2x\n", config->MaxLat);
	seq_printf(seq, "PciStatus                : %4.4x\n",
		    config->Pcistatus);
	seq_printf(seq, "Debug Device Id          : %4.4x\n",
		    config->DbgDevId);
	seq_printf(seq, "DRAM ROM Function        : %4.4x\n",
		    config->DramRomFn);
	seq_printf(seq, "Network interface Pin 1  : %2.2x\n",
		    config->NetIntPin1);
	seq_printf(seq, "Network interface Pin 2  : %2.2x\n",
		    config->NetIntPin1);
	seq_printf(seq, "Network interface Pin 3  : %2.2x\n",
		    config->NetIntPin1);
	seq_printf(seq, "PM capabilities          : %4.4X\n",
		    config->PMECapab);
	seq_printf(seq, "Network Clock Controls   : %4.4X\n",
		    config->NwClkCtrls);

	switch (config->FruFormat) {
	case ATK_FRU_FORMAT:
		{
			seq_printf(seq,
			    "Vendor                   : Alacritech, Inc.\n");
			seq_printf(seq,
			    "Assembly #               : %c%c%c%c%c%c\n",
				    fru[0], fru[1], fru[2], fru[3], fru[4],
				    fru[5]);
			seq_printf(seq,
				    "Revision #               : %c%c\n",
				    fru[6], fru[7]);

			if (config->OEMFruFormat == VENDOR4_FRU_FORMAT) {
				seq_printf(seq,
					    "Serial   #               : "
					    "%c%c%c%c%c%c%c%c%c%c%c%c\n",
					    fru[8], fru[9], fru[10],
					    fru[11], fru[12], fru[13],
					    fru[16], fru[17], fru[18],
					    fru[19], fru[20], fru[21]);
			} else {
				seq_printf(seq,
					    "Serial   #               : "
					    "%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
					    fru[8], fru[9], fru[10],
					    fru[11], fru[12], fru[13],
					    fru[14], fru[15], fru[16],
					    fru[17], fru[18], fru[19],
					    fru[20], fru[21]);
			}
			break;
		}

	default:
		{
			seq_printf(seq,
			    "Vendor                   : Alacritech, Inc.\n");
			seq_printf(seq,
			    "Serial   #               : Empty FRU\n");
			break;
		}
	}

	switch (config->OEMFruFormat) {
	case VENDOR1_FRU_FORMAT:
		{
			seq_printf(seq, "FRU Information:\n");
			seq_printf(seq, "    Commodity #          : %c\n",
				    oemfru[0]);
			seq_printf(seq,
				    "    Assembly #           : %c%c%c%c\n",
				    oemfru[1], oemfru[2], oemfru[3], oemfru[4]);
			seq_printf(seq,
				    "    Revision #           : %c%c\n",
				    oemfru[5], oemfru[6]);
			seq_printf(seq,
				    "    Supplier #           : %c%c\n",
				    oemfru[7], oemfru[8]);
			seq_printf(seq,
				    "    Date                 : %c%c\n",
				    oemfru[9], oemfru[10]);
			seq_sprintf(seq,
				    "    Sequence #           : %c%c%c\n",
				    oemfru[11], oemfru[12], oemfru[13]);
			break;
		}

	case VENDOR2_FRU_FORMAT:
		{
			seq_printf(seq, "FRU Information:\n");
			seq_printf(seq,
				    "    Part     #           : "
				    "%c%c%c%c%c%c%c%c\n",
				    oemfru[0], oemfru[1], oemfru[2],
				    oemfru[3], oemfru[4], oemfru[5],
				    oemfru[6], oemfru[7]);
			seq_printf(seq,
				    "    Supplier #           : %c%c%c%c%c\n",
				    oemfru[8], oemfru[9], oemfru[10],
				    oemfru[11], oemfru[12]);
			seq_printf(seq,
				    "    Date                 : %c%c%c\n",
				    oemfru[13], oemfru[14], oemfru[15]);
			seq_sprintf(seq,
				    "    Sequence #           : %c%c%c%c\n",
				    oemfru[16], oemfru[17], oemfru[18],
				    oemfru[19]);
			break;
		}

	case VENDOR3_FRU_FORMAT:
		{
			seq_printf(seq, "FRU Information:\n");
		}

	case VENDOR4_FRU_FORMAT:
		{
			seq_printf(seq, "FRU Information:\n");
			seq_printf(seq,
				    "    FRU Number           : "
				    "%c%c%c%c%c%c%c%c\n",
				    oemfru[0], oemfru[1], oemfru[2],
				    oemfru[3], oemfru[4], oemfru[5],
				    oemfru[6], oemfru[7]);
			seq_sprintf(seq,
				    "    Part Number          : "
				    "%c%c%c%c%c%c%c%c\n",
				    oemfru[8], oemfru[9], oemfru[10],
				    oemfru[11], oemfru[12], oemfru[13],
				    oemfru[14], oemfru[15]);
			seq_printf(seq,
				    "    EC Level             : "
				    "%c%c%c%c%c%c%c%c\n",
				    oemfru[16], oemfru[17], oemfru[18],
				    oemfru[19], oemfru[20], oemfru[21],
				    oemfru[22], oemfru[23]);
			break;
		}

	default:
		break;
	}
#endif

	return 0;
}

static int slic_debug_adapter_show(struct seq_file *seq, void *v)
{
	p_adapter_t adapter = seq->private;

	if ((adapter->netdev) && (adapter->netdev->name)) {
		seq_printf(seq, "info: interface          : %s\n",
			    adapter->netdev->name);
	}
	seq_printf(seq, "info: status             : %s\n",
		SLIC_LINKSTATE(adapter->linkstate));
	seq_printf(seq, "info: port               : %d\n",
		adapter->physport);
	seq_printf(seq, "info: speed              : %s\n",
		SLIC_SPEED(adapter->linkspeed));
	seq_printf(seq, "info: duplex             : %s\n",
		SLIC_DUPLEX(adapter->linkduplex));
	seq_printf(seq, "info: irq                : 0x%X\n",
		(uint) adapter->irq);
	seq_printf(seq, "info: Interrupt Agg Delay: %d usec\n",
		adapter->card->loadlevel_current);
	seq_printf(seq, "info: RcvQ max entries   : %4.4X\n",
		SLIC_RCVQ_ENTRIES);
	seq_printf(seq, "info: RcvQ current       : %4.4X\n",
		    adapter->rcvqueue.count);
	seq_printf(seq, "rx stats: packets                  : %8.8lX\n",
		    adapter->stats.rx_packets);
	seq_printf(seq, "rx stats: bytes                    : %8.8lX\n",
		    adapter->stats.rx_bytes);
	seq_printf(seq, "rx stats: broadcasts               : %8.8X\n",
		    adapter->rcv_broadcasts);
	seq_printf(seq, "rx stats: multicasts               : %8.8X\n",
		    adapter->rcv_multicasts);
	seq_printf(seq, "rx stats: unicasts                 : %8.8X\n",
		    adapter->rcv_unicasts);
	seq_printf(seq, "rx stats: errors                   : %8.8X\n",
		    (ulong32) adapter->slic_stats.iface.rcv_errors);
	seq_printf(seq, "rx stats: Missed errors            : %8.8X\n",
		    (ulong32) adapter->slic_stats.iface.rcv_discards);
	seq_printf(seq, "rx stats: drops                    : %8.8X\n",
			(ulong32) adapter->rcv_drops);
	seq_printf(seq, "tx stats: packets                  : %8.8lX\n",
			adapter->stats.tx_packets);
	seq_printf(seq, "tx stats: bytes                    : %8.8lX\n",
			adapter->stats.tx_bytes);
	seq_printf(seq, "tx stats: errors                   : %8.8X\n",
			(ulong32) adapter->slic_stats.iface.xmt_errors);
	seq_printf(seq, "rx stats: multicasts               : %8.8lX\n",
			adapter->stats.multicast);
	seq_printf(seq, "tx stats: collision errors         : %8.8X\n",
			(ulong32) adapter->slic_stats.iface.xmit_collisions);
	seq_printf(seq, "perf: Max rcv frames/isr           : %8.8X\n",
			adapter->max_isr_rcvs);
	seq_printf(seq, "perf: Rcv interrupt yields         : %8.8X\n",
			adapter->rcv_interrupt_yields);
	seq_printf(seq, "perf: Max xmit complete/isr        : %8.8X\n",
			adapter->max_isr_xmits);
	seq_printf(seq, "perf: error interrupts             : %8.8X\n",
			adapter->error_interrupts);
	seq_printf(seq, "perf: error rmiss interrupts       : %8.8X\n",
			adapter->error_rmiss_interrupts);
	seq_printf(seq, "perf: rcv interrupts               : %8.8X\n",
			adapter->rcv_interrupts);
	seq_printf(seq, "perf: xmit interrupts              : %8.8X\n",
			adapter->xmit_interrupts);
	seq_printf(seq, "perf: link event interrupts        : %8.8X\n",
			adapter->linkevent_interrupts);
	seq_printf(seq, "perf: UPR interrupts               : %8.8X\n",
			adapter->upr_interrupts);
	seq_printf(seq, "perf: interrupt count              : %8.8X\n",
			adapter->num_isrs);
	seq_printf(seq, "perf: false interrupts             : %8.8X\n",
			adapter->false_interrupts);
	seq_printf(seq, "perf: All register writes          : %8.8X\n",
			adapter->all_reg_writes);
	seq_printf(seq, "perf: ICR register writes          : %8.8X\n",
			adapter->icr_reg_writes);
	seq_printf(seq, "perf: ISR register writes          : %8.8X\n",
			adapter->isr_reg_writes);
	seq_printf(seq, "ifevents: overflow 802 errors      : %8.8X\n",
			adapter->if_events.oflow802);
	seq_printf(seq, "ifevents: transport overflow errors: %8.8X\n",
			adapter->if_events.Tprtoflow);
	seq_printf(seq, "ifevents: underflow errors         : %8.8X\n",
			adapter->if_events.uflow802);
	seq_printf(seq, "ifevents: receive early            : %8.8X\n",
			adapter->if_events.rcvearly);
	seq_printf(seq, "ifevents: buffer overflows         : %8.8X\n",
			adapter->if_events.Bufov);
	seq_printf(seq, "ifevents: carrier errors           : %8.8X\n",
			adapter->if_events.Carre);
	seq_printf(seq, "ifevents: Long                     : %8.8X\n",
			adapter->if_events.Longe);
	seq_printf(seq, "ifevents: invalid preambles        : %8.8X\n",
			adapter->if_events.Invp);
	seq_printf(seq, "ifevents: CRC errors               : %8.8X\n",
			adapter->if_events.Crc);
	seq_printf(seq, "ifevents: dribble nibbles          : %8.8X\n",
			adapter->if_events.Drbl);
	seq_printf(seq, "ifevents: Code violations          : %8.8X\n",
			adapter->if_events.Code);
	seq_printf(seq, "ifevents: TCP checksum errors      : %8.8X\n",
			adapter->if_events.TpCsum);
	seq_printf(seq, "ifevents: TCP header short errors  : %8.8X\n",
			adapter->if_events.TpHlen);
	seq_printf(seq, "ifevents: IP checksum errors       : %8.8X\n",
			adapter->if_events.IpCsum);
	seq_printf(seq, "ifevents: IP frame incompletes     : %8.8X\n",
			adapter->if_events.IpLen);
	seq_printf(seq, "ifevents: IP headers shorts        : %8.8X\n",
			adapter->if_events.IpHlen);

	return 0;
}
static int slic_debug_adapter_open(struct inode *inode, struct file *file)
{
	return single_open(file, slic_debug_adapter_show, inode->i_private);
}

static int slic_debug_card_open(struct inode *inode, struct file *file)
{
	return single_open(file, slic_debug_card_show, inode->i_private);
}

static const struct file_operations slic_debug_adapter_fops = {
	.owner		= THIS_MODULE,
	.open		= slic_debug_adapter_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations slic_debug_card_fops = {
	.owner		= THIS_MODULE,
	.open		= slic_debug_card_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void slic_debug_adapter_create(p_adapter_t adapter)
{
	struct dentry *d;
	char    name[7];
	p_sliccard_t card = adapter->card;

	if (!card->debugfs_dir)
		return;

	sprintf(name, "port%d", adapter->port);
	d = debugfs_create_file(name, S_IRUGO,
				card->debugfs_dir, adapter,
				&slic_debug_adapter_fops);
	if (!d || IS_ERR(d))
		pr_info(PFX "%s: debugfs create failed\n", name);
	else
		adapter->debugfs_entry = d;
}

static void slic_debug_adapter_destroy(p_adapter_t adapter)
{
	if (adapter->debugfs_entry) {
		debugfs_remove(adapter->debugfs_entry);
		adapter->debugfs_entry = NULL;
	}
}

static void slic_debug_card_create(p_sliccard_t card)
{
	struct dentry *d;
	char    name[IFNAMSIZ];

	snprintf(name, sizeof(name), "slic%d", card->cardnum);
	d = debugfs_create_dir(name, slic_debugfs);
	if (!d || IS_ERR(d))
		pr_info(PFX "%s: debugfs create dir failed\n",
				name);
	else {
		card->debugfs_dir = d;
		d = debugfs_create_file("cardinfo", S_IRUGO,
				slic_debugfs, card,
				&slic_debug_card_fops);
		if (!d || IS_ERR(d))
			pr_info(PFX "%s: debugfs create failed\n",
					name);
		else
			card->debugfs_cardinfo = d;
	}
}

static void slic_debug_card_destroy(p_sliccard_t card)
{
	int i;

	for (i = 0; i < card->card_size; i++) {
		p_adapter_t adapter;

		adapter = card->adapter[i];
		if (adapter)
			slic_debug_adapter_destroy(adapter);
	}
	if (card->debugfs_cardinfo) {
		debugfs_remove(card->debugfs_cardinfo);
		card->debugfs_cardinfo = NULL;
	}
	if (card->debugfs_dir) {
		debugfs_remove(card->debugfs_dir);
		card->debugfs_dir = NULL;
	}
}

static void slic_debug_init(void)
{
	struct dentry *ent;

	ent = debugfs_create_dir("slic", NULL);
	if (!ent || IS_ERR(ent)) {
		pr_info(PFX "debugfs create directory failed\n");
		return;
	}

	slic_debugfs = ent;
}

static void slic_debug_cleanup(void)
{
	if (slic_debugfs) {
		debugfs_remove(slic_debugfs);
		slic_debugfs = NULL;
	}
}

/*=============================================================================
  =============================================================================
  ===                                                                       ===
  ===       SLIC  DUMP  MANAGEMENT        SECTION                           ===
  ===                                                                       ===
  ===                                                                       ===
  === Dump routines                                                         ===
  ===                                                                       ===
  ===                                                                       ===
  =============================================================================
  ============================================================================*/

#if SLIC_DUMP_ENABLED

#include <stdarg.h>

pvoid slic_dump_handle;		/* thread handle */

/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
static int slic_dump_seek(struct file *SLIChandle, ulong32 file_offset)
{
	if (SLIChandle->f_pos != file_offset) {
		/*DBG_MSG("slic_dump_seek  now needed [%x : %x]\n",
			(ulong32)SLIChandle->f_pos, (ulong32)file_offset); */
		if (SLIChandle->f_op->llseek) {
			if (SLIChandle->f_op->
			    llseek(SLIChandle, file_offset, 0) != file_offset)
				return 0;
		} else {
			SLIChandle->f_pos = file_offset;
		}
	}
	return 1;
}

static int slic_dump_write(p_sliccard_t card,
			   const void *addr, int size, ulong32 file_offset)
{
	int r = 1;
	ulong32 result = 0;
	struct file *SLIChandle = card->dumphandle;

#ifdef HISTORICAL		/* legacy */
	down(&SLIChandle->f_dentry->d_inode->i_sem);
#endif
	if (size) {
		slic_dump_seek(SLIChandle, file_offset);

		result =
		    SLIChandle->f_op->write(SLIChandle, addr, size,
					    &SLIChandle->f_pos);

		r = result == size;
	}

	card->dumptime_complete = jiffies;
	card->dumptime_delta = card->dumptime_complete - card->dumptime_start;
	card->dumptime_start = jiffies;

#ifdef HISTORICAL
	up(&SLIChandle->f_dentry->d_inode->i_sem);
#endif
	if (!r) {
		DBG_ERROR("%s: addr[%p] size[%x] result[%x] file_offset[%x]\n",
			  __func__, addr, size, result, file_offset);
	}
	return r;
}

uint slic_init_dump_thread(p_sliccard_t card)
{
	card->dump_task_id = kthread_run(slic_dump_thread, (void *)card, 0);

/*  DBG_MSG("create slic_dump_thread dump_pid[%x]\n", card->dump_pid); */
	if (IS_ERR(card->dump_task_id)) {
		DBG_MSG("create slic_dump_thread FAILED \n");
		return STATUS_FAILURE;
	}

	return STATUS_SUCCESS;
}

int slic_dump_thread(void *context)
{
	p_sliccard_t card = (p_sliccard_t) context;
	p_adapter_t adapter;
	p_adapter_t dump_adapter = NULL;
	ulong32 dump_complete = 0;
	ulong32 delay = SLIC_SECS_TO_JIFFS(PING_TIMER_INTERVAL);
	p_slic_regs_t pregs;
	ulong32 i;
	p_slic_upr_t upr, uprnext;
	ulong32 dump_card;

	ASSERT(card);

	card->dumpthread_running = 1;

#ifdef HISTORICAL
	lock_kernel();
	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
	exit_files(current);	/* daemonize doesn't do exit_files */
	current->files = init_task.files;
	atomic_inc(&current->files->count);
#endif

	daemonize("%s", "slicmon");

	/* Setup a nice name */
	strcpy(current->comm, "slicmon");
	DBG_ERROR
	    ("slic_dump_thread[slicmon] daemon is alive card[%p] pid[%x]\n",
	     card, card->dump_task_id->pid);

	/*
	 *    Send me a signal to get me to die (for debugging)
	 */
	do {
		/*
		 * If card state is not set to up, skip
		 */
		if (card->state != CARD_UP) {
			if (card->adapters_activated)
				goto wait;
			else
				goto end_thread;
		}
		/*
		 *    Check the results of our last ping.
		 */
		dump_card = 0;
#ifdef SLIC_FAILURE_DUMP
		if (card->pingstatus != ISR_PINGMASK) {
			DBG_MSG
			    ("\n[slicmon]  CARD #%d TIMED OUT - status "
			     "%x: DUMP THE CARD!\n",
			     card->cardnum, card->pingstatus);
			dump_card = 1;
		}
#else
		/*
		 *  Cause a card RESET instead?
		 */
		if (card->pingstatus != ISR_PINGMASK) {
			/* todo. do we want to reset the card in production */
			/* DBG_MSG("\n[slicmon]  CARD #%d TIMED OUT - "
			   status %x: RESET THE CARD!\n", card->cardnum,
			   card->pingstatus); */
			DBG_ERROR
			    ("\n[slicmon]  CARD #%d TIMED OUT - status %x: "
			     "DUMP THE CARD!\n",
			     card->cardnum, card->pingstatus);
			dump_card = 1;
		}
#endif
		if ((dump_card)
		    || (card->dump_requested == SLIC_DUMP_REQUESTED)) {
			if (card->dump_requested == SLIC_DUMP_REQUESTED) {
				DBG_ERROR
			    ("[slicmon]: Dump card Requested: Card %x\n",
				     card->cardnum);
			}
			if (card->pingstatus != ISR_PINGMASK) {
				ushort cpuid = 0;
				ushort crashpc = 0;

				if (card->adapter[0]) {
					if ((card->adapter[0])->memorylength >=
					    CRASH_INFO_OFFSET +
					    sizeof(slic_crash_info)) {
						char *crashptr;
						p_slic_crash_info crashinfo;

						crashptr =
						    ((char *)card->adapter[0]->
						     slic_regs) +
						    CRASH_INFO_OFFSET;
						crashinfo =
						    (p_slic_crash_info)
						    crashptr;
						cpuid = crashinfo->cpu_id;
						crashpc = crashinfo->crash_pc;
					}
				}
				DBG_ERROR
				    ("[slicmon]: Dump card: Card %x crashed "
				     "and failed to answer PING. "
				     "CPUID[%x] PC[%x]\n ",
				     card->cardnum, cpuid, crashpc);
			}

			card->dump_requested = SLIC_DUMP_IN_PROGRESS;

			/*
			 * Set the card state to DOWN and the adapter states
			 * to RESET.They will check this in SimbaCheckForHang
			 * and initiate interface reset (which in turn will
			 * reinitialize the card).
			 */
			card->state = CARD_DOWN;

			for (i = 0; i < card->card_size; i++) {
				adapter = card->adapter[i];
				if (adapter) {
					slic_if_stop_queue(adapter);

					if (adapter->state == ADAPT_UP) {
						adapter->state = ADAPT_RESET;
						adapter->linkstate = LINK_DOWN;
						DBG_ERROR
						    ("[slicmon]: SLIC Card[%d] "
						     "Port[%d] adapter[%p] "
						     "down\n",
						     (uint) card->cardnum,
						     (uint) i, adapter);
					}
#if SLIC_GET_STATS_TIMER_ENABLED
					/* free stats timer */
					if (adapter->statstimerset) {
						adapter->statstimerset = 0;
						del_timer(&adapter->statstimer);
					}
#endif
				}
			}

			for (i = 0; i < card->card_size; i++) {
				adapter = card->adapter[i];
				if ((adapter) && (adapter->activated)) {
					pregs = adapter->slic_regs;
					dump_adapter = adapter;

					/*
					 * If the dump status is zero, then
					 * the utility processor has crashed.
					 * If this is the case, any pending
					 * utilityprocessor requests will not
					 * complete and our dump commands will
					 * not be issued.
					 *
					 * To avoid this we will clear any
					 * pending utility processor requests
					 * now.
					 */
					if (!card->pingstatus) {
						SLIC_ACQUIRE_IRQ_SPINLOCK
						    (adapter->upr_lock);
						upr = adapter->upr_list;
						while (upr) {
							uprnext = upr->next;
							SLIC_DEALLOCATE_MEM
							    (upr);
							upr = uprnext;
						}
						adapter->upr_list = 0;
						adapter->upr_busy = 0;
						SLIC_RELEASE_IRQ_SPINLOCK
						    (adapter->upr_lock);
					}

					slic_dump_card(card, FALSE);
					dump_complete = 1;
				}

				if (dump_complete) {
					DBG_ERROR("SLIC Dump Complete\n");
					/*  Only dump the card one time */
					break;
				}
			}

			if (dump_adapter) {
				DBG_ERROR
				    ("slic dump completed. "
				     "Reenable interfaces\n");
				slic_card_init(card, dump_adapter);

				/*
				 *  Reenable the adapters that were reset
				 */
				for (i = 0; i < card->card_size; i++) {
					adapter = card->adapter[i];
					if (adapter) {
						if (adapter->state ==
						    ADAPT_RESET) {
							DBG_ERROR
							    ("slicdump: SLIC "
					   "Card[%d] Port[%d] adapter[%p] "
					   "bring UP\n",
							     (uint) card->
							     cardnum, (uint) i,
							     adapter);
							adapter->state =
							    ADAPT_DOWN;
							adapter->linkstate =
							    LINK_DOWN;
							slic_entry_open
							    (adapter->netdev);
						}
					}
				}

				card->dump_requested = SLIC_DUMP_DONE;
			}
		} else {
		/* if pingstatus != ISR_PINGMASK) || dump_requested...ELSE
		 *    We received a valid ping response.
		 *    Clear the Pingstatus field, find a valid adapter
		 *    structure and send another ping.
		 */
			for (i = 0; i < card->card_size; i++) {
				adapter = card->adapter[i];
				if (adapter && (adapter->state == ADAPT_UP)) {
					card->pingstatus = 0;
					slic_upr_request(adapter, SLIC_UPR_PING,
							 0, 0, 0, 0);
					break;	/* Only issue one per card */
				}
			}
		}
wait:
		SLIC_INTERRUPTIBLE_SLEEP_ON_TIMEOUT(card->dump_wq, delay);
	} while (!signal_pending(current));

end_thread:
/*  DBG_MSG("[slicmon] slic_dump_thread card[%p] pid[%x] ENDING\n",
    card, card->dump_pid); */
	card->dumpthread_running = 0;

	return 0;
}

/*
 * Read a single byte from our dump index file.  This
 * value is used as our suffix for our dump path.  The
 * value is incremented and written back to the file
 */
uchar slic_get_dump_index(pchar path)
{
	uchar index = 0;
#ifdef SLIC_DUMP_INDEX_SUPPORT
	ulong32 status;
	pvoid FileHandle;
	ulong32 offset;

	offset = 0;

	/*
	 * Open the index file.  If one doesn't exist, create it
	 */
	status = create_file(&FileHandle);

	if (status != STATUS_SUCCESS)
		return (uchar) 0;

	status = read_file(FileHandle, &index, 1, &offset);

	index++;

	status = write_file(FileHandle, &index, 1, &offset);

	close_file(FileHandle);
#else
	index = 0;
#endif
	return index;
}

struct file *slic_dump_open_file(p_sliccard_t card)
{
	struct file *SLIChandle = NULL;
	struct dentry *dentry = NULL;
	struct inode *inode = NULL;
	char SLICfile[50];

	card->dumpfile_fs = get_fs();

	set_fs(KERNEL_DS);

	memset(SLICfile, 0, sizeof(SLICfile));
	sprintf(SLICfile, "/var/tmp/slic%d-dump-%d", card->cardnum,
		(uint) card->dump_count);
	card->dump_count++;

	SLIChandle =
	    filp_open(SLICfile, O_CREAT | O_RDWR | O_SYNC | O_LARGEFILE, 0666);

	DBG_MSG("[slicmon]: Dump Card #%d to file: %s \n", card->cardnum,
		SLICfile);

/*  DBG_MSG("[slicmon] filp_open %s SLIChandle[%p]\n", SLICfile, SLIChandle);*/

	if (IS_ERR(SLIChandle))
		goto end_slicdump;

	dentry = SLIChandle->f_dentry;
	inode = dentry->d_inode;

/*  DBG_MSG("[slicmon] inode[%p] i_nlink[%x] i_mode[%x] i_op[%p] i_fop[%p]\n"
		"f_op->write[%p]\n",
		inode, inode->i_nlink, inode->i_mode, inode->i_op,
		inode->i_fop, SLIChandle->f_op->write); */
	if (inode->i_nlink > 1)
		goto close_slicdump;	/* multiple links - don't dump */
#ifdef HISTORICAL
	if (!S_ISREG(inode->i_mode))
		goto close_slicdump;
#endif
	if (!inode->i_op || !inode->i_fop)
		goto close_slicdump;

	if (!SLIChandle->f_op->write)
		goto close_slicdump;

	/*
	 *  If we got here we have SUCCESSFULLY OPENED the dump file
	 */
/*  DBG_MSG("opened %s SLIChandle[%p]\n", SLICfile, SLIChandle); */
	return SLIChandle;

close_slicdump:
	DBG_MSG("[slicmon] slic_dump_open_file failed close SLIChandle[%p]\n",
		SLIChandle);
	filp_close(SLIChandle, NULL);

end_slicdump:
	set_fs(card->dumpfile_fs);

	return NULL;
}

void slic_dump_close_file(p_sliccard_t card)
{

/*  DBG_MSG("[slicmon] slic_dump_CLOSE_file close SLIChandle[%p]\n",
   card->dumphandle); */

	filp_close(card->dumphandle, NULL);

	set_fs(card->dumpfile_fs);
}

ulong32 slic_dump_card(p_sliccard_t card, boolean resume)
{
	p_adapter_t adapter = card->master;
	ulong32 status;
	ulong32 queue;
	ulong32 len, offset;
	ulong32 sram_size, dram_size, regs;
	sliccore_hdr_t corehdr;
	ulong32 file_offset;
	pchar namestr;
	ulong32 i;
	ulong32 max_queues = 0;
	ulong32 result;

	card->dumphandle = slic_dump_open_file(card);

	if (card->dumphandle == NULL) {
		DBG_MSG("[slicmon] Cant create Dump file - dump failed\n");
		return -ENOMEM;
	}
	if (!card->dumpbuffer) {
		DBG_MSG("[slicmon] Insufficient memory for dump\n");
		return -ENOMEM;
	}
	if (!card->cmdbuffer) {
		DBG_MSG("[slicmon] Insufficient cmd memory for dump\n");
		return -ENOMEM;
	}

	/*
	 * Write the file version to the core header.
	 */
	namestr = slic_proc_version;
	for (i = 0; i < (DRIVER_NAME_SIZE - 1); i++, namestr++) {
		if (!namestr)
			break;
		corehdr.driver_version[i] = *namestr;
	}
	corehdr.driver_version[i] = 0;

	file_offset = sizeof(sliccore_hdr_t);

	/*
	 * Issue the following debug commands to the SLIC:
	 *        - Halt both receive and transmit
	 *        - Dump receive registers
	 *        - Dump transmit registers
	 *        - Dump sram
	 *        - Dump dram
	 *        - Dump queues
	 */
	DBG_MSG("slicDump HALT Receive Processor\n");
	card->dumptime_start = jiffies;

	status = slic_dump_halt(card, PROC_RECEIVE);
	if (status != STATUS_SUCCESS) {
		DBG_ERROR
		    ("Cant halt receive sequencer - dump failed status[%x]\n",
		     status);
		goto done;
	}

	DBG_MSG("slicDump HALT Transmit Processor\n");
	status = slic_dump_halt(card, PROC_TRANSMIT);
	if (status != STATUS_SUCCESS) {
		DBG_ERROR("Cant halt transmit sequencer - dump failed\n");
		goto done;
	}

	/* Dump receive regs */
	status = slic_dump_reg(card, PROC_RECEIVE);
	if (status != STATUS_SUCCESS) {
		DBG_ERROR("Cant dump receive registers - dump failed\n");
		goto done;
	}

	DBG_MSG("slicDump Write Receive REGS len[%x] offset[%x]\n",
		(SLIC_NUM_REG * 4), file_offset);

	result =
	    slic_dump_write(card, card->dumpbuffer, SLIC_NUM_REG * 4,
			    file_offset);
	if (!result) {
		DBG_ERROR
		    ("Cant write rcv registers to dump file - dump failed\n");
		goto done;
	}

	corehdr.RcvRegOff = file_offset;
	corehdr.RcvRegsize = SLIC_NUM_REG * 4;
	file_offset += SLIC_NUM_REG * 4;

	/* Dump transmit regs */
	status = slic_dump_reg(card, PROC_TRANSMIT);
	if (status != STATUS_SUCCESS) {
		DBG_ERROR("Cant dump transmit registers - dump failed\n");
		goto done;
	}

	DBG_MSG("slicDump Write XMIT REGS len[%x] offset[%x]\n",
		(SLIC_NUM_REG * 4), file_offset);

	result =
	    slic_dump_write(card, card->dumpbuffer, SLIC_NUM_REG * 4,
			    file_offset);
	if (!result) {
		DBG_ERROR
		    ("Cant write xmt registers to dump file - dump failed\n");
		goto done;
	}

	corehdr.XmtRegOff = file_offset;
	corehdr.XmtRegsize = SLIC_NUM_REG * 4;
	file_offset += SLIC_NUM_REG * 4;

	regs = SLIC_GBMAX_REG;

	corehdr.FileRegOff = file_offset;
	corehdr.FileRegsize = regs * 4;

	for (offset = 0; regs;) {
		len = MIN(regs, 16);	/* Can only xfr 16 regs at a time */

		status = slic_dump_data(card, offset, (ushort) len, DESC_RFILE);

		if (status != STATUS_SUCCESS) {
			DBG_ERROR("Cant dump register file - dump failed\n");
			goto done;
		}

		DBG_MSG("slicDump Write RegisterFile len[%x] offset[%x]\n",
			(len * 4), file_offset);

		result =
		    slic_dump_write(card, card->dumpbuffer, len * 4,
				    file_offset);
		if (!result) {
			DBG_ERROR
			    ("Cant write register file to dump file - "
			     "dump failed\n");
			goto done;
		}

		file_offset += len * 4;
		offset += len;
		regs -= len;
	}

	dram_size = card->config.DramSize * 0x10000;

	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
		sram_size = SLIC_SRAM_SIZE2GB;
		break;
	case SLIC_1GB_DEVICE_ID:
		sram_size = SLIC_SRAM_SIZE1GB;
		break;
	default:
		sram_size = 0;
		ASSERT(0);
		break;
	}

	corehdr.SramOff = file_offset;
	corehdr.Sramsize = sram_size;

	for (offset = 0; sram_size;) {
		len = MIN(sram_size, DUMP_BUF_SIZE);
		status = slic_dump_data(card, offset, (ushort) len, DESC_SRAM);
		if (status != STATUS_SUCCESS) {
			DBG_ERROR
			    ("[slicmon] Cant dump SRAM at offset %x - "
			     "dump failed\n", (uint) offset);
			goto done;
		}

		DBG_MSG("[slicmon] slicDump Write SRAM  len[%x] offset[%x]\n",
			len, file_offset);

		result =
		    slic_dump_write(card, card->dumpbuffer, len, file_offset);
		if (!result) {
			DBG_ERROR
			    ("[slicmon] Cant write SRAM to dump file - "
			     "dump failed\n");
			goto done;
		}

		file_offset += len;
		offset += len;
		sram_size -= len;
	}

	corehdr.DramOff = file_offset;
	corehdr.Dramsize = dram_size;

	for (offset = 0; dram_size;) {
		len = MIN(dram_size, DUMP_BUF_SIZE);

		status = slic_dump_data(card, offset, (ushort) len, DESC_DRAM);
		if (status != STATUS_SUCCESS) {
			DBG_ERROR
			    ("[slicmon] Cant dump dram at offset %x - "
			     "dump failed\n", (uint) offset);
			goto done;
		}

		DBG_MSG("slicDump Write DRAM  len[%x] offset[%x]\n", len,
			file_offset);

		result =
		    slic_dump_write(card, card->dumpbuffer, len, file_offset);
		if (!result) {
			DBG_ERROR
			    ("[slicmon] Cant write DRAM to dump file - "
			     "dump failed\n");
			goto done;
		}

		file_offset += len;
		offset += len;
		dram_size -= len;
	}

	max_queues = SLIC_MAX_QUEUE;

	for (queue = 0; queue < max_queues; queue++) {
		pulong32 qarray = (pulong32) card->dumpbuffer;
		ulong32 qarray_physl = card->dumpbuffer_physl;
		ulong32 qarray_physh = card->dumpbuffer_physh;
		ulong32 qstart;
		ulong32 qdelta;
		ulong32 qtotal = 0;

		DBG_MSG("[slicmon] Start Dump of QUEUE #0x%x\n", (uint) queue);

		for (offset = 0; offset < (DUMP_BUF_SIZE >> 2); offset++) {
			qstart = jiffies;
			qdelta = 0;

			status = slic_dump_queue(card,
						 qarray_physl,
						 qarray_physh, queue);
			qarray_physl += 4;

			if (status != STATUS_SUCCESS)
				break;

			if (jiffies > qstart) {
				qdelta = jiffies - qstart;
				qtotal += qdelta;
			}
		}

		if (offset)
			qdelta = qtotal / offset;
		else
			qdelta = 0;

/*        DBG_MSG("   slicDump Write QUEUE #0x%x len[%x] offset[%x] "
		"avgjiffs[%x]\n", queue, (offset*4), file_offset, qdelta); */

		result =
		    slic_dump_write(card, card->dumpbuffer, offset * 4,
				    file_offset);

		if (!result) {
			DBG_ERROR
			    ("[slicmon] Cant write QUEUES to dump file - "
			     "dump failed\n");
			goto done;
		}

		corehdr.queues[queue].queueOff = file_offset;
		corehdr.queues[queue].queuesize = offset * 4;
		file_offset += offset * 4;

/*      DBG_MSG("    Reload QUEUE #0x%x elements[%x]\n", (uint)queue, offset);*/
		/*
		 * Fill the queue back up
		 */
		for (i = 0; i < offset; i++) {
			qstart = jiffies;
			qdelta = 0;

			status = slic_dump_load_queue(card, qarray[i], queue);
			if (status != STATUS_SUCCESS)
				break;

			if (jiffies > qstart) {
				qdelta = jiffies - qstart;
				qtotal += qdelta;
			}
		}

		if (offset)
			qdelta = qtotal / offset;
		else
			qdelta = 0;

/*      DBG_MSG("   Reload DONE avgjiffs[%x]\n", qdelta); */

		resume = 1;
	}

	len = SLIC_GB_CAMAB_SZE * 4;
	status = slic_dump_cam(card, 0, len, DUMP_CAM_A);
	if (status != STATUS_SUCCESS) {
		DBG_ERROR("[slicmon] Can't dump CAM_A - dump failed\n");
		goto done;
	}

	result = slic_dump_write(card, card->dumpbuffer, len, file_offset);
	if (result) {
		DBG_ERROR
		    ("[slicmon] Can't write CAM_A data to dump file - "
		     "dump failed\n");
		goto done;
	}
	corehdr.CamAMOff = file_offset;
	corehdr.CamASize = len;
	file_offset += len;

	len = SLIC_GB_CAMCD_SZE * 4;
	status = slic_dump_cam(card, 0, len, DUMP_CAM_C);
	if (status) {
		DBG_ERROR("[slicmon] Can't dump CAM_C - dump failed\n");
		goto done;
	}

	result = slic_dump_write(card, card->dumpbuffer, len, file_offset);
	if (result) {
		DBG_ERROR
		    ("[slicmon] Can't write CAM_C data to dump file - "
		     "dump failed\n");
		goto done;
	}
	corehdr.CamCMOff = file_offset;
	corehdr.CamCSize = len;
	file_offset += len;

done:
	/*
	 * Write out the core header
	 */
	file_offset = 0;
	DBG_MSG("[slicmon] Write CoreHeader len[%x] offset[%x]\n",
		(uint) sizeof(sliccore_hdr_t), file_offset);

	result =
	    slic_dump_write(card, &corehdr, sizeof(sliccore_hdr_t),
			    file_offset);
	DBG_MSG("[slicmon] corehdr  xoff[%x] xsz[%x]\n"
		"    roff[%x] rsz[%x] fileoff[%x] filesz[%x]\n"
		"    sramoff[%x] sramsz[%x], dramoff[%x] dramsz[%x]\n"
		"    corehdr_offset[%x]\n", corehdr.XmtRegOff,
		corehdr.XmtRegsize, corehdr.RcvRegOff, corehdr.RcvRegsize,
		corehdr.FileRegOff, corehdr.FileRegsize, corehdr.SramOff,
		corehdr.Sramsize, corehdr.DramOff, corehdr.Dramsize,
		(uint) sizeof(sliccore_hdr_t));
	for (i = 0; i < max_queues; i++) {
		DBG_MSG("[slicmon]  QUEUE 0x%x  offset[%x] size[%x]\n",
			(uint) i, corehdr.queues[i].queueOff,
			corehdr.queues[i].queuesize);

	}

	slic_dump_close_file(card);

	if (resume) {
		DBG_MSG("slicDump RESTART RECEIVE and XMIT PROCESSORS\n\n");
		slic_dump_resume(card, PROC_RECEIVE);
		slic_dump_resume(card, PROC_TRANSMIT);
	}

	return status;
}

ulong32 slic_dump_halt(p_sliccard_t card, uchar proc)
{
	puchar cmd = card->cmdbuffer;

	*cmd = COMMAND_BYTE(CMD_HALT, 0, proc);

	return slic_dump_send_cmd(card,
				   card->cmdbuffer_physl,
				   card->cmdbuffer_physh, 0, 0);
}

ulong32 slic_dump_resume(p_sliccard_t card, uchar proc)
{
	puchar cmd = card->cmdbuffer;

	*cmd = COMMAND_BYTE(CMD_RUN, 0, proc);

	return slic_dump_send_cmd(card,
				   card->cmdbuffer_physl,
				   card->cmdbuffer_physh, 0, 0);
}

ulong32 slic_dump_reg(p_sliccard_t card, uchar proc)
{
	pdump_cmd_t dump = (pdump_cmd_t) card->cmdbuffer;

	dump->cmd = COMMAND_BYTE(CMD_DUMP, 0, proc);
	dump->desc = DESC_REG;
	dump->count = 0;
	dump->addr = 0;

	return slic_dump_send_cmd(card,
				   card->cmdbuffer_physl,
				   card->cmdbuffer_physh,
				   card->dumpbuffer_physl,
				   card->dumpbuffer_physh);
}

ulong32 slic_dump_data(p_sliccard_t card,
		       ulong32 addr, ushort count, uchar desc)
{
	pdump_cmd_t dump = (pdump_cmd_t) card->cmdbuffer;

	dump->cmd = COMMAND_BYTE(CMD_DUMP, 0, PROC_RECEIVE);
	dump->desc = desc;
	dump->count = count;
	dump->addr = addr;

	return slic_dump_send_cmd(card,
				   card->cmdbuffer_physl,
				   card->cmdbuffer_physh,
				   card->dumpbuffer_physl,
				   card->dumpbuffer_physh);
}

ulong32 slic_dump_queue(p_sliccard_t card,
			ulong32 addr, ulong32 buf_physh, ulong32 queue)
{
	pdump_cmd_t dump = (pdump_cmd_t) card->cmdbuffer;

	dump->cmd = COMMAND_BYTE(CMD_DUMP, 0, PROC_RECEIVE);
	dump->desc = DESC_QUEUE;
	dump->count = 1;
	dump->addr = queue;

	return slic_dump_send_cmd(card,
				   card->cmdbuffer_physl,
				   card->cmdbuffer_physh,
				   addr, card->dumpbuffer_physh);
}

ulong32 slic_dump_load_queue(p_sliccard_t card, ulong32 data, ulong32 queue)
{
	pdump_cmd_t load = (pdump_cmd_t) card->cmdbuffer;

	load->cmd = COMMAND_BYTE(CMD_LOAD, 0, PROC_RECEIVE);
	load->desc = DESC_QUEUE;
	load->count = (ushort) queue;
	load->addr = data;

	return slic_dump_send_cmd(card,
				   card->cmdbuffer_physl,
				   card->cmdbuffer_physh, 0, 0);
}

ulong32 slic_dump_cam(p_sliccard_t card,
		      ulong32 addr, ulong32 count, uchar desc)
{
	pdump_cmd_t dump = (pdump_cmd_t) card->cmdbuffer;

	dump->cmd = COMMAND_BYTE(CMD_CAM_OPS, 0, PROC_NONE);
	dump->desc = desc;
	dump->count = count;
	dump->addr = 0;

	return slic_dump_send_cmd(card,
				   card->cmdbuffer_physl,
				   card->cmdbuffer_physh,
				   addr, card->dumpbuffer_physh);
}

ulong32 slic_dump_send_cmd(p_sliccard_t card,
			   ulong32 cmd_physl,
			   ulong32 cmd_physh,
			   ulong32 buf_physl, ulong32 buf_physh)
{
	ulong timeout = SLIC_MS_TO_JIFFIES(500);	/* 500 msec */
	ulong32 attempts = 5;
	ulong32 delay = SLIC_MS_TO_JIFFIES(10);	/* 10 msec */
	p_adapter_t adapter = card->master;

	ASSERT(adapter);
	do {
		/*
		 * Zero the Dumpstatus field of the adapter structure
		 */
		card->dumpstatus = 0;
		/*
		 * Issue the dump command via a utility processor request.
		 *
		 * Kludge: We use the Informationbuffer parameter to hold
		 * the buffer address
		 */
		slic_upr_request(adapter, SLIC_UPR_DUMP, cmd_physl, cmd_physh,
				 buf_physl, buf_physh);

		timeout += jiffies;
		/*
		 * Spin until completion or timeout.
		 */
		while (!card->dumpstatus) {
			int num_sleeps = 0;

			if (jiffies > timeout) {
				/*
				 *  Complete the timed-out DUMP UPR request.
				 */
				slic_upr_request_complete(adapter, 0);
				DBG_ERROR
				    ("%s: TIMED OUT num_sleeps[%x] "
				     "status[%x]\n",
				     __func__, num_sleeps, STATUS_FAILURE);

				return STATUS_FAILURE;
			}
			num_sleeps++;
			SLIC_INTERRUPTIBLE_SLEEP_ON_TIMEOUT(card->dump_wq,
							    delay);
		}

		if (card->dumpstatus & ISR_UPCERR) {
			/*
			 * Error (or queue empty)
			 */
/*          DBG_ERROR("[slicmon] %s: DUMP_STATUS & ISR_UPCERR status[%x]\n",
		__func__, STATUS_FAILURE); */

			return STATUS_FAILURE;
		} else if (card->dumpstatus & ISR_UPCBSY) {
			/*
			 * Retry
			 */
			DBG_ERROR("%s: ISR_UPCBUSY attempt[%x]\n", __func__,
				  attempts);

			attempts--;
		} else {
			/*
			 * success
			 */
			return STATUS_SUCCESS;
		}

	} while (attempts);

	DBG_ERROR("%s: GAVE UP AFTER SEVERAL ATTEMPTS status[%x]\n",
		  __func__, STATUS_FAILURE);

	/*
	 * Gave up after several attempts
	 */
	return STATUS_FAILURE;
}

#endif
/*=============================================================================
  =============================================================================
  ===                                                                       ===
  ===      *** END **** END **** END **** END ***                           ===
  ===       SLIC  DUMP  MANAGEMENT        SECTION                           ===
  ===                                                                       ===
  ===                                                                       ===
  ===                                                                       ===
  =============================================================================
  ============================================================================*/

/******************************************************************************/
/****************   MODULE INITIATION / TERMINATION FUNCTIONS   ***************/
/******************************************************************************/

static struct pci_driver slic_driver = {
	.name = DRV_NAME,
	.id_table = slic_pci_tbl,
	.probe = slic_entry_probe,
	.remove = slic_entry_remove,
#if SLIC_POWER_MANAGEMENT_ENABLED
	.suspend = slicpm_suspend,
	.resume = slicpm_resume,
#endif
/*    .shutdown   =     slic_shutdown,  MOOK_INVESTIGATE */
};

static int __init slic_module_init(void)
{
	struct pci_device_id *pcidev;
	int ret;

/*      DBG_MSG("slicoss: %s ENTER cpu %d\n", __func__, smp_processor_id()); */

	slic_init_driver();

	if (debug >= 0 && slic_debug != debug)
		printk(SLICLEVEL "slicoss: debug level is %d.\n", debug);
	if (debug >= 0)
		slic_debug = debug;

	pcidev = (struct pci_device_id *)slic_driver.id_table;
/*      DBG_MSG("slicoss: %s call pci_module_init jiffies[%lx] cpu #%d\n",
	__func__, jiffies, smp_processor_id()); */

	ret = pci_register_driver(&slic_driver);

/*  DBG_MSG("slicoss: %s EXIT after call pci_module_init jiffies[%lx] "
	    "cpu #%d status[%x]\n",__func__, jiffies,
	    smp_processor_id(), ret); */

	return ret;
}

static void __exit slic_module_cleanup(void)
{
/*      DBG_MSG("slicoss: %s ENTER\n", __func__); */
	pci_unregister_driver(&slic_driver);
	slic_debug_cleanup();
/*      DBG_MSG("slicoss: %s EXIT\n", __func__); */
}

module_init(slic_module_init);
module_exit(slic_module_cleanup);
