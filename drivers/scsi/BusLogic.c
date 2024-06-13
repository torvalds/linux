// SPDX-License-Identifier: GPL-2.0-only

/*

  Linux Driver for BusLogic MultiMaster and FlashPoint SCSI Host Adapters

  Copyright 1995-1998 by Leonard N. Zubkoff <lnz@dandelion.com>


  The author respectfully requests that any modifications to this software be
  sent directly to him for evaluation and testing.

  Special thanks to Wayne Yen, Jin-Lon Hon, and Alex Win of BusLogic, whose
  advice has been invaluable, to David Gentzel, for writing the original Linux
  BusLogic driver, and to Paul Gortmaker, for being such a dedicated test site.

  Finally, special thanks to Mylex/BusLogic for making the FlashPoint SCCB
  Manager available as freely redistributable source code.

*/

#define blogic_drvr_version		"2.1.17"
#define blogic_drvr_date		"12 September 2013"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/msdos_partition.h>
#include <scsi/scsicam.h>

#include <asm/dma.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include "BusLogic.h"
#include "FlashPoint.c"

#ifndef FAILURE
#define FAILURE (-1)
#endif

static struct scsi_host_template blogic_template;

/*
  blogic_drvr_options_count is a count of the number of BusLogic Driver
  Options specifications provided via the Linux Kernel Command Line or via
  the Loadable Kernel Module Installation Facility.
*/

static int blogic_drvr_options_count;


/*
  blogic_drvr_options is an array of Driver Options structures representing
  BusLogic Driver Options specifications provided via the Linux Kernel Command
  Line or via the Loadable Kernel Module Installation Facility.
*/

static struct blogic_drvr_options blogic_drvr_options[BLOGIC_MAX_ADAPTERS];


/*
  BusLogic can be assigned a string by insmod.
*/

MODULE_LICENSE("GPL");
#ifdef MODULE
static char *BusLogic;
module_param(BusLogic, charp, 0);
#endif


/*
  blogic_probe_options is a set of Probe Options to be applied across
  all BusLogic Host Adapters.
*/

static struct blogic_probe_options blogic_probe_options;


/*
  blogic_global_options is a set of Global Options to be applied across
  all BusLogic Host Adapters.
*/

static struct blogic_global_options blogic_global_options;

static LIST_HEAD(blogic_host_list);

/*
  blogic_probeinfo_count is the number of entries in blogic_probeinfo_list.
*/

static int blogic_probeinfo_count;


/*
  blogic_probeinfo_list is the list of I/O Addresses and Bus Probe Information
  to be checked for potential BusLogic Host Adapters.  It is initialized by
  interrogating the PCI Configuration Space on PCI machines as well as from the
  list of standard BusLogic I/O Addresses.
*/

static struct blogic_probeinfo *blogic_probeinfo_list;


/*
  blogic_cmd_failure_reason holds a string identifying the reason why a
  call to blogic_cmd failed.  It is only non-NULL when blogic_cmd
  returns a failure code.
*/

static char *blogic_cmd_failure_reason;

/*
  blogic_announce_drvr announces the Driver Version and Date, Author's
  Name, Copyright Notice, and Electronic Mail Address.
*/

static void blogic_announce_drvr(struct blogic_adapter *adapter)
{
	blogic_announce("***** BusLogic SCSI Driver Version " blogic_drvr_version " of " blogic_drvr_date " *****\n", adapter);
	blogic_announce("Copyright 1995-1998 by Leonard N. Zubkoff <lnz@dandelion.com>\n", adapter);
}


/*
  blogic_drvr_info returns the Host Adapter Name to identify this SCSI
  Driver and Host Adapter.
*/

static const char *blogic_drvr_info(struct Scsi_Host *host)
{
	struct blogic_adapter *adapter =
				(struct blogic_adapter *) host->hostdata;
	return adapter->full_model;
}

/*
  blogic_init_ccbs initializes a group of Command Control Blocks (CCBs)
  for Host Adapter from the blk_size bytes located at blk_pointer.  The newly
  created CCBs are added to Host Adapter's free list.
*/

static void blogic_init_ccbs(struct blogic_adapter *adapter, void *blk_pointer,
				int blk_size, dma_addr_t blkp)
{
	struct blogic_ccb *ccb = (struct blogic_ccb *) blk_pointer;
	unsigned int offset = 0;
	memset(blk_pointer, 0, blk_size);
	ccb->allocgrp_head = blkp;
	ccb->allocgrp_size = blk_size;
	while ((blk_size -= sizeof(struct blogic_ccb)) >= 0) {
		ccb->status = BLOGIC_CCB_FREE;
		ccb->adapter = adapter;
		ccb->dma_handle = (u32) blkp + offset;
		if (blogic_flashpoint_type(adapter)) {
			ccb->callback = blogic_qcompleted_ccb;
			ccb->base_addr = adapter->fpinfo.base_addr;
		}
		ccb->next = adapter->free_ccbs;
		ccb->next_all = adapter->all_ccbs;
		adapter->free_ccbs = ccb;
		adapter->all_ccbs = ccb;
		adapter->alloc_ccbs++;
		ccb++;
		offset += sizeof(struct blogic_ccb);
	}
}


/*
  blogic_create_initccbs allocates the initial CCBs for Host Adapter.
*/

static bool __init blogic_create_initccbs(struct blogic_adapter *adapter)
{
	int blk_size = BLOGIC_CCB_GRP_ALLOCSIZE * sizeof(struct blogic_ccb);
	void *blk_pointer;
	dma_addr_t blkp;

	while (adapter->alloc_ccbs < adapter->initccbs) {
		blk_pointer = dma_alloc_coherent(&adapter->pci_device->dev,
				blk_size, &blkp, GFP_KERNEL);
		if (blk_pointer == NULL) {
			blogic_err("UNABLE TO ALLOCATE CCB GROUP - DETACHING\n",
					adapter);
			return false;
		}
		blogic_init_ccbs(adapter, blk_pointer, blk_size, blkp);
	}
	return true;
}


/*
  blogic_destroy_ccbs deallocates the CCBs for Host Adapter.
*/

static void blogic_destroy_ccbs(struct blogic_adapter *adapter)
{
	struct blogic_ccb *next_ccb = adapter->all_ccbs, *ccb, *lastccb = NULL;
	adapter->all_ccbs = NULL;
	adapter->free_ccbs = NULL;
	while ((ccb = next_ccb) != NULL) {
		next_ccb = ccb->next_all;
		if (ccb->allocgrp_head) {
			if (lastccb)
				dma_free_coherent(&adapter->pci_device->dev,
						lastccb->allocgrp_size, lastccb,
						lastccb->allocgrp_head);
			lastccb = ccb;
		}
	}
	if (lastccb)
		dma_free_coherent(&adapter->pci_device->dev,
				lastccb->allocgrp_size, lastccb,
				lastccb->allocgrp_head);
}


/*
  blogic_create_addlccbs allocates Additional CCBs for Host Adapter.  If
  allocation fails and there are no remaining CCBs available, the Driver Queue
  Depth is decreased to a known safe value to avoid potential deadlocks when
  multiple host adapters share the same IRQ Channel.
*/

static void blogic_create_addlccbs(struct blogic_adapter *adapter,
					int addl_ccbs, bool print_success)
{
	int blk_size = BLOGIC_CCB_GRP_ALLOCSIZE * sizeof(struct blogic_ccb);
	int prev_alloc = adapter->alloc_ccbs;
	void *blk_pointer;
	dma_addr_t blkp;
	if (addl_ccbs <= 0)
		return;
	while (adapter->alloc_ccbs - prev_alloc < addl_ccbs) {
		blk_pointer = dma_alloc_coherent(&adapter->pci_device->dev,
				blk_size, &blkp, GFP_KERNEL);
		if (blk_pointer == NULL)
			break;
		blogic_init_ccbs(adapter, blk_pointer, blk_size, blkp);
	}
	if (adapter->alloc_ccbs > prev_alloc) {
		if (print_success)
			blogic_notice("Allocated %d additional CCBs (total now %d)\n", adapter, adapter->alloc_ccbs - prev_alloc, adapter->alloc_ccbs);
		return;
	}
	blogic_notice("Failed to allocate additional CCBs\n", adapter);
	if (adapter->drvr_qdepth > adapter->alloc_ccbs - adapter->tgt_count) {
		adapter->drvr_qdepth = adapter->alloc_ccbs - adapter->tgt_count;
		adapter->scsi_host->can_queue = adapter->drvr_qdepth;
	}
}

/*
  blogic_alloc_ccb allocates a CCB from Host Adapter's free list,
  allocating more memory from the Kernel if necessary.  The Host Adapter's
  Lock should already have been acquired by the caller.
*/

static struct blogic_ccb *blogic_alloc_ccb(struct blogic_adapter *adapter)
{
	static unsigned long serial;
	struct blogic_ccb *ccb;
	ccb = adapter->free_ccbs;
	if (ccb != NULL) {
		ccb->serial = ++serial;
		adapter->free_ccbs = ccb->next;
		ccb->next = NULL;
		if (adapter->free_ccbs == NULL)
			blogic_create_addlccbs(adapter, adapter->inc_ccbs,
						true);
		return ccb;
	}
	blogic_create_addlccbs(adapter, adapter->inc_ccbs, true);
	ccb = adapter->free_ccbs;
	if (ccb == NULL)
		return NULL;
	ccb->serial = ++serial;
	adapter->free_ccbs = ccb->next;
	ccb->next = NULL;
	return ccb;
}


/*
  blogic_dealloc_ccb deallocates a CCB, returning it to the Host Adapter's
  free list.  The Host Adapter's Lock should already have been acquired by the
  caller.
*/

static void blogic_dealloc_ccb(struct blogic_ccb *ccb, int dma_unmap)
{
	struct blogic_adapter *adapter = ccb->adapter;

	if (ccb->command != NULL)
		scsi_dma_unmap(ccb->command);
	if (dma_unmap)
		dma_unmap_single(&adapter->pci_device->dev, ccb->sensedata,
			 ccb->sense_datalen, DMA_FROM_DEVICE);

	ccb->command = NULL;
	ccb->status = BLOGIC_CCB_FREE;
	ccb->next = adapter->free_ccbs;
	adapter->free_ccbs = ccb;
}


/*
  blogic_cmd sends the command opcode to adapter, optionally
  providing paramlen bytes of param and receiving at most
  replylen bytes of reply; any excess reply data is received but
  discarded.

  On success, this function returns the number of reply bytes read from
  the Host Adapter (including any discarded data); on failure, it returns
  -1 if the command was invalid, or -2 if a timeout occurred.

  blogic_cmd is called exclusively during host adapter detection and
  initialization, so performance and latency are not critical, and exclusive
  access to the Host Adapter hardware is assumed.  Once the host adapter and
  driver are initialized, the only Host Adapter command that is issued is the
  single byte Execute Mailbox Command operation code, which does not require
  waiting for the Host Adapter Ready bit to be set in the Status Register.
*/

static int blogic_cmd(struct blogic_adapter *adapter, enum blogic_opcode opcode,
			void *param, int paramlen, void *reply, int replylen)
{
	unsigned char *param_p = (unsigned char *) param;
	unsigned char *reply_p = (unsigned char *) reply;
	union blogic_stat_reg statusreg;
	union blogic_int_reg intreg;
	unsigned long processor_flag = 0;
	int reply_b = 0, result;
	long timeout;
	/*
	   Clear out the Reply Data if provided.
	 */
	if (replylen > 0)
		memset(reply, 0, replylen);
	/*
	   If the IRQ Channel has not yet been acquired, then interrupts
	   must be disabled while issuing host adapter commands since a
	   Command Complete interrupt could occur if the IRQ Channel was
	   previously enabled by another BusLogic Host Adapter or another
	   driver sharing the same IRQ Channel.
	 */
	if (!adapter->irq_acquired)
		local_irq_save(processor_flag);
	/*
	   Wait for the Host Adapter Ready bit to be set and the
	   Command/Parameter Register Busy bit to be reset in the Status
	   Register.
	 */
	timeout = 10000;
	while (--timeout >= 0) {
		statusreg.all = blogic_rdstatus(adapter);
		if (statusreg.sr.adapter_ready && !statusreg.sr.cmd_param_busy)
			break;
		udelay(100);
	}
	if (timeout < 0) {
		blogic_cmd_failure_reason =
				"Timeout waiting for Host Adapter Ready";
		result = -2;
		goto done;
	}
	/*
	   Write the opcode to the Command/Parameter Register.
	 */
	adapter->adapter_cmd_complete = false;
	blogic_setcmdparam(adapter, opcode);
	/*
	   Write any additional Parameter Bytes.
	 */
	timeout = 10000;
	while (paramlen > 0 && --timeout >= 0) {
		/*
		   Wait 100 microseconds to give the Host Adapter enough
		   time to determine whether the last value written to the
		   Command/Parameter Register was valid or not. If the
		   Command Complete bit is set in the Interrupt Register,
		   then the Command Invalid bit in the Status Register will
		   be reset if the Operation Code or Parameter was valid
		   and the command has completed, or set if the Operation
		   Code or Parameter was invalid. If the Data In Register
		   Ready bit is set in the Status Register, then the
		   Operation Code was valid, and data is waiting to be read
		   back from the Host Adapter. Otherwise, wait for the
		   Command/Parameter Register Busy bit in the Status
		   Register to be reset.
		 */
		udelay(100);
		intreg.all = blogic_rdint(adapter);
		statusreg.all = blogic_rdstatus(adapter);
		if (intreg.ir.cmd_complete)
			break;
		if (adapter->adapter_cmd_complete)
			break;
		if (statusreg.sr.datain_ready)
			break;
		if (statusreg.sr.cmd_param_busy)
			continue;
		blogic_setcmdparam(adapter, *param_p++);
		paramlen--;
	}
	if (timeout < 0) {
		blogic_cmd_failure_reason =
				"Timeout waiting for Parameter Acceptance";
		result = -2;
		goto done;
	}
	/*
	   The Modify I/O Address command does not cause a Command Complete
	   Interrupt.
	 */
	if (opcode == BLOGIC_MOD_IOADDR) {
		statusreg.all = blogic_rdstatus(adapter);
		if (statusreg.sr.cmd_invalid) {
			blogic_cmd_failure_reason =
					"Modify I/O Address Invalid";
			result = -1;
			goto done;
		}
		if (blogic_global_options.trace_config)
			blogic_notice("blogic_cmd(%02X) Status = %02X: (Modify I/O Address)\n", adapter, opcode, statusreg.all);
		result = 0;
		goto done;
	}
	/*
	   Select an appropriate timeout value for awaiting command completion.
	 */
	switch (opcode) {
	case BLOGIC_INQ_DEV0TO7:
	case BLOGIC_INQ_DEV8TO15:
	case BLOGIC_INQ_DEV:
		/* Approximately 60 seconds. */
		timeout = 60 * 10000;
		break;
	default:
		/* Approximately 1 second. */
		timeout = 10000;
		break;
	}
	/*
	   Receive any Reply Bytes, waiting for either the Command
	   Complete bit to be set in the Interrupt Register, or for the
	   Interrupt Handler to set the Host Adapter Command Completed
	   bit in the Host Adapter structure.
	 */
	while (--timeout >= 0) {
		intreg.all = blogic_rdint(adapter);
		statusreg.all = blogic_rdstatus(adapter);
		if (intreg.ir.cmd_complete)
			break;
		if (adapter->adapter_cmd_complete)
			break;
		if (statusreg.sr.datain_ready) {
			if (++reply_b <= replylen)
				*reply_p++ = blogic_rddatain(adapter);
			else
				blogic_rddatain(adapter);
		}
		if (opcode == BLOGIC_FETCH_LOCALRAM &&
				statusreg.sr.adapter_ready)
			break;
		udelay(100);
	}
	if (timeout < 0) {
		blogic_cmd_failure_reason =
					"Timeout waiting for Command Complete";
		result = -2;
		goto done;
	}
	/*
	   Clear any pending Command Complete Interrupt.
	 */
	blogic_intreset(adapter);
	/*
	   Provide tracing information if requested.
	 */
	if (blogic_global_options.trace_config) {
		int i;
		blogic_notice("blogic_cmd(%02X) Status = %02X: %2d ==> %2d:",
				adapter, opcode, statusreg.all, replylen,
				reply_b);
		if (replylen > reply_b)
			replylen = reply_b;
		for (i = 0; i < replylen; i++)
			blogic_notice(" %02X", adapter,
					((unsigned char *) reply)[i]);
		blogic_notice("\n", adapter);
	}
	/*
	   Process Command Invalid conditions.
	 */
	if (statusreg.sr.cmd_invalid) {
		/*
		   Some early BusLogic Host Adapters may not recover
		   properly from a Command Invalid condition, so if this
		   appears to be the case, a Soft Reset is issued to the
		   Host Adapter.  Potentially invalid commands are never
		   attempted after Mailbox Initialization is performed,
		   so there should be no Host Adapter state lost by a
		   Soft Reset in response to a Command Invalid condition.
		 */
		udelay(1000);
		statusreg.all = blogic_rdstatus(adapter);
		if (statusreg.sr.cmd_invalid || statusreg.sr.rsvd ||
				statusreg.sr.datain_ready ||
				statusreg.sr.cmd_param_busy ||
				!statusreg.sr.adapter_ready ||
				!statusreg.sr.init_reqd ||
				statusreg.sr.diag_active ||
				statusreg.sr.diag_failed) {
			blogic_softreset(adapter);
			udelay(1000);
		}
		blogic_cmd_failure_reason = "Command Invalid";
		result = -1;
		goto done;
	}
	/*
	   Handle Excess Parameters Supplied conditions.
	 */
	if (paramlen > 0) {
		blogic_cmd_failure_reason = "Excess Parameters Supplied";
		result = -1;
		goto done;
	}
	/*
	   Indicate the command completed successfully.
	 */
	blogic_cmd_failure_reason = NULL;
	result = reply_b;
	/*
	   Restore the interrupt status if necessary and return.
	 */
done:
	if (!adapter->irq_acquired)
		local_irq_restore(processor_flag);
	return result;
}


/*
  blogic_sort_probeinfo sorts a section of blogic_probeinfo_list in order
  of increasing PCI Bus and Device Number.
*/

static void __init blogic_sort_probeinfo(struct blogic_probeinfo
					*probeinfo_list, int probeinfo_cnt)
{
	int last_exchange = probeinfo_cnt - 1, bound, j;

	while (last_exchange > 0) {
		bound = last_exchange;
		last_exchange = 0;
		for (j = 0; j < bound; j++) {
			struct blogic_probeinfo *probeinfo1 =
							&probeinfo_list[j];
			struct blogic_probeinfo *probeinfo2 =
							&probeinfo_list[j + 1];
			if (probeinfo1->bus > probeinfo2->bus ||
				(probeinfo1->bus == probeinfo2->bus &&
				(probeinfo1->dev > probeinfo2->dev))) {
				struct blogic_probeinfo tmp_probeinfo;

				memcpy(&tmp_probeinfo, probeinfo1,
					sizeof(struct blogic_probeinfo));
				memcpy(probeinfo1, probeinfo2,
					sizeof(struct blogic_probeinfo));
				memcpy(probeinfo2, &tmp_probeinfo,
					sizeof(struct blogic_probeinfo));
				last_exchange = j;
			}
		}
	}
}


/*
  blogic_init_mm_probeinfo initializes the list of I/O Address
  and Bus Probe Information to be checked for potential BusLogic MultiMaster
  SCSI Host Adapters by interrogating the PCI Configuration Space on PCI
  machines as well as from the list of standard BusLogic MultiMaster ISA
  I/O Addresses.  It returns the number of PCI MultiMaster Host Adapters found.
*/

static int __init blogic_init_mm_probeinfo(struct blogic_adapter *adapter)
{
	struct blogic_probeinfo *pr_probeinfo =
		&blogic_probeinfo_list[blogic_probeinfo_count];
	int nonpr_mmindex = blogic_probeinfo_count + 1;
	int nonpr_mmcount = 0, mmcount = 0;
	bool force_scan_order = false;
	bool force_scan_order_checked = false;
	struct pci_dev *pci_device = NULL;
	int i;
	if (blogic_probeinfo_count >= BLOGIC_MAX_ADAPTERS)
		return 0;
	blogic_probeinfo_count++;
	/*
	   Iterate over the MultiMaster PCI Host Adapters.  For each
	   enumerated host adapter, determine whether its ISA Compatible
	   I/O Port is enabled and if so, whether it is assigned the
	   Primary I/O Address.  A host adapter that is assigned the
	   Primary I/O Address will always be the preferred boot device.
	   The MultiMaster BIOS will first recognize a host adapter at
	   the Primary I/O Address, then any other PCI host adapters,
	   and finally any host adapters located at the remaining
	   standard ISA I/O Addresses.  When a PCI host adapter is found
	   with its ISA Compatible I/O Port enabled, a command is issued
	   to disable the ISA Compatible I/O Port, and it is noted that the
	   particular standard ISA I/O Address need not be probed.
	 */
	pr_probeinfo->io_addr = 0;
	while ((pci_device = pci_get_device(PCI_VENDOR_ID_BUSLOGIC,
					PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER,
					pci_device)) != NULL) {
		struct blogic_adapter *host_adapter = adapter;
		struct blogic_adapter_info adapter_info;
		enum blogic_isa_ioport mod_ioaddr_req;
		unsigned char bus;
		unsigned char device;
		unsigned int irq_ch;
		unsigned long base_addr0;
		unsigned long base_addr1;
		unsigned long io_addr;
		unsigned long pci_addr;

		if (pci_enable_device(pci_device))
			continue;

		if (dma_set_mask(&pci_device->dev, DMA_BIT_MASK(32)))
			continue;

		bus = pci_device->bus->number;
		device = pci_device->devfn >> 3;
		irq_ch = pci_device->irq;
		io_addr = base_addr0 = pci_resource_start(pci_device, 0);
		pci_addr = base_addr1 = pci_resource_start(pci_device, 1);

		if (pci_resource_flags(pci_device, 0) & IORESOURCE_MEM) {
			blogic_err("BusLogic: Base Address0 0x%lX not I/O for MultiMaster Host Adapter\n", NULL, base_addr0);
			blogic_err("at PCI Bus %d Device %d I/O Address 0x%lX\n", NULL, bus, device, io_addr);
			continue;
		}
		if (pci_resource_flags(pci_device, 1) & IORESOURCE_IO) {
			blogic_err("BusLogic: Base Address1 0x%lX not Memory for MultiMaster Host Adapter\n", NULL, base_addr1);
			blogic_err("at PCI Bus %d Device %d PCI Address 0x%lX\n", NULL, bus, device, pci_addr);
			continue;
		}
		if (irq_ch == 0) {
			blogic_err("BusLogic: IRQ Channel %d invalid for MultiMaster Host Adapter\n", NULL, irq_ch);
			blogic_err("at PCI Bus %d Device %d I/O Address 0x%lX\n", NULL, bus, device, io_addr);
			continue;
		}
		if (blogic_global_options.trace_probe) {
			blogic_notice("BusLogic: PCI MultiMaster Host Adapter detected at\n", NULL);
			blogic_notice("BusLogic: PCI Bus %d Device %d I/O Address 0x%lX PCI Address 0x%lX\n", NULL, bus, device, io_addr, pci_addr);
		}
		/*
		   Issue the Inquire PCI Host Adapter Information command to determine
		   the ISA Compatible I/O Port.  If the ISA Compatible I/O Port is
		   known and enabled, note that the particular Standard ISA I/O
		   Address should not be probed.
		 */
		host_adapter->io_addr = io_addr;
		blogic_intreset(host_adapter);
		if (blogic_cmd(host_adapter, BLOGIC_INQ_PCI_INFO, NULL, 0,
				&adapter_info, sizeof(adapter_info)) !=
				sizeof(adapter_info))
			adapter_info.isa_port = BLOGIC_IO_DISABLE;
		/*
		   Issue the Modify I/O Address command to disable the
		   ISA Compatible I/O Port. On PCI Host Adapters, the
		   Modify I/O Address command allows modification of the
		   ISA compatible I/O Address that the Host Adapter
		   responds to; it does not affect the PCI compliant
		   I/O Address assigned at system initialization.
		 */
		mod_ioaddr_req = BLOGIC_IO_DISABLE;
		blogic_cmd(host_adapter, BLOGIC_MOD_IOADDR, &mod_ioaddr_req,
				sizeof(mod_ioaddr_req), NULL, 0);
		/*
		   For the first MultiMaster Host Adapter enumerated,
		   issue the Fetch Host Adapter Local RAM command to read
		   byte 45 of the AutoSCSI area, for the setting of the
		   "Use Bus And Device # For PCI Scanning Seq." option.
		   Issue the Inquire Board ID command since this option is
		   only valid for the BT-948/958/958D.
		 */
		if (!force_scan_order_checked) {
			struct blogic_fetch_localram fetch_localram;
			struct blogic_autoscsi_byte45 autoscsi_byte45;
			struct blogic_board_id id;

			fetch_localram.offset = BLOGIC_AUTOSCSI_BASE + 45;
			fetch_localram.count = sizeof(autoscsi_byte45);
			blogic_cmd(host_adapter, BLOGIC_FETCH_LOCALRAM,
					&fetch_localram, sizeof(fetch_localram),
					&autoscsi_byte45,
					sizeof(autoscsi_byte45));
			blogic_cmd(host_adapter, BLOGIC_GET_BOARD_ID, NULL, 0,
					&id, sizeof(id));
			if (id.fw_ver_digit1 == '5')
				force_scan_order =
					autoscsi_byte45.force_scan_order;
			force_scan_order_checked = true;
		}
		/*
		   Determine whether this MultiMaster Host Adapter has its
		   ISA Compatible I/O Port enabled and is assigned the
		   Primary I/O Address. If it does, then it is the Primary
		   MultiMaster Host Adapter and must be recognized first.
		   If it does not, then it is added to the list for probing
		   after any Primary MultiMaster Host Adapter is probed.
		 */
		if (adapter_info.isa_port == BLOGIC_IO_330) {
			pr_probeinfo->adapter_type = BLOGIC_MULTIMASTER;
			pr_probeinfo->adapter_bus_type = BLOGIC_PCI_BUS;
			pr_probeinfo->io_addr = io_addr;
			pr_probeinfo->pci_addr = pci_addr;
			pr_probeinfo->bus = bus;
			pr_probeinfo->dev = device;
			pr_probeinfo->irq_ch = irq_ch;
			pr_probeinfo->pci_device = pci_dev_get(pci_device);
			mmcount++;
		} else if (blogic_probeinfo_count < BLOGIC_MAX_ADAPTERS) {
			struct blogic_probeinfo *probeinfo =
				&blogic_probeinfo_list[blogic_probeinfo_count++];
			probeinfo->adapter_type = BLOGIC_MULTIMASTER;
			probeinfo->adapter_bus_type = BLOGIC_PCI_BUS;
			probeinfo->io_addr = io_addr;
			probeinfo->pci_addr = pci_addr;
			probeinfo->bus = bus;
			probeinfo->dev = device;
			probeinfo->irq_ch = irq_ch;
			probeinfo->pci_device = pci_dev_get(pci_device);
			nonpr_mmcount++;
			mmcount++;
		} else
			blogic_warn("BusLogic: Too many Host Adapters detected\n", NULL);
	}
	/*
	   If the AutoSCSI "Use Bus And Device # For PCI Scanning Seq."
	   option is ON for the first enumerated MultiMaster Host Adapter,
	   and if that host adapter is a BT-948/958/958D, then the
	   MultiMaster BIOS will recognize MultiMaster Host Adapters in
	   the order of increasing PCI Bus and Device Number. In that case,
	   sort the probe information into the same order the BIOS uses.
	   If this option is OFF, then the MultiMaster BIOS will recognize
	   MultiMaster Host Adapters in the order they are enumerated by
	   the PCI BIOS, and hence no sorting is necessary.
	 */
	if (force_scan_order)
		blogic_sort_probeinfo(&blogic_probeinfo_list[nonpr_mmindex],
					nonpr_mmcount);
	/*
	   Iterate over the older non-compliant MultiMaster PCI Host Adapters,
	   noting the PCI bus location and assigned IRQ Channel.
	 */
	pci_device = NULL;
	while ((pci_device = pci_get_device(PCI_VENDOR_ID_BUSLOGIC,
					PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER_NC,
					pci_device)) != NULL) {
		unsigned char bus;
		unsigned char device;
		unsigned int irq_ch;
		unsigned long io_addr;

		if (pci_enable_device(pci_device))
			continue;

		if (dma_set_mask(&pci_device->dev, DMA_BIT_MASK(32)))
			continue;

		bus = pci_device->bus->number;
		device = pci_device->devfn >> 3;
		irq_ch = pci_device->irq;
		io_addr = pci_resource_start(pci_device, 0);

		if (io_addr == 0 || irq_ch == 0)
			continue;
		for (i = 0; i < blogic_probeinfo_count; i++) {
			struct blogic_probeinfo *probeinfo =
						&blogic_probeinfo_list[i];
			if (probeinfo->io_addr == io_addr &&
				probeinfo->adapter_type == BLOGIC_MULTIMASTER) {
				probeinfo->adapter_bus_type = BLOGIC_PCI_BUS;
				probeinfo->pci_addr = 0;
				probeinfo->bus = bus;
				probeinfo->dev = device;
				probeinfo->irq_ch = irq_ch;
				probeinfo->pci_device = pci_dev_get(pci_device);
				break;
			}
		}
	}
	return mmcount;
}


/*
  blogic_init_fp_probeinfo initializes the list of I/O Address
  and Bus Probe Information to be checked for potential BusLogic FlashPoint
  Host Adapters by interrogating the PCI Configuration Space.  It returns the
  number of FlashPoint Host Adapters found.
*/

static int __init blogic_init_fp_probeinfo(struct blogic_adapter *adapter)
{
	int fpindex = blogic_probeinfo_count, fpcount = 0;
	struct pci_dev *pci_device = NULL;
	/*
	   Interrogate PCI Configuration Space for any FlashPoint Host Adapters.
	 */
	while ((pci_device = pci_get_device(PCI_VENDOR_ID_BUSLOGIC,
					PCI_DEVICE_ID_BUSLOGIC_FLASHPOINT,
					pci_device)) != NULL) {
		unsigned char bus;
		unsigned char device;
		unsigned int irq_ch;
		unsigned long base_addr0;
		unsigned long base_addr1;
		unsigned long io_addr;
		unsigned long pci_addr;

		if (pci_enable_device(pci_device))
			continue;

		if (dma_set_mask(&pci_device->dev, DMA_BIT_MASK(32)))
			continue;

		bus = pci_device->bus->number;
		device = pci_device->devfn >> 3;
		irq_ch = pci_device->irq;
		io_addr = base_addr0 = pci_resource_start(pci_device, 0);
		pci_addr = base_addr1 = pci_resource_start(pci_device, 1);
#ifdef CONFIG_SCSI_FLASHPOINT
		if (pci_resource_flags(pci_device, 0) & IORESOURCE_MEM) {
			blogic_err("BusLogic: Base Address0 0x%lX not I/O for FlashPoint Host Adapter\n", NULL, base_addr0);
			blogic_err("at PCI Bus %d Device %d I/O Address 0x%lX\n", NULL, bus, device, io_addr);
			continue;
		}
		if (pci_resource_flags(pci_device, 1) & IORESOURCE_IO) {
			blogic_err("BusLogic: Base Address1 0x%lX not Memory for FlashPoint Host Adapter\n", NULL, base_addr1);
			blogic_err("at PCI Bus %d Device %d PCI Address 0x%lX\n", NULL, bus, device, pci_addr);
			continue;
		}
		if (irq_ch == 0) {
			blogic_err("BusLogic: IRQ Channel %d invalid for FlashPoint Host Adapter\n", NULL, irq_ch);
			blogic_err("at PCI Bus %d Device %d I/O Address 0x%lX\n", NULL, bus, device, io_addr);
			continue;
		}
		if (blogic_global_options.trace_probe) {
			blogic_notice("BusLogic: FlashPoint Host Adapter detected at\n", NULL);
			blogic_notice("BusLogic: PCI Bus %d Device %d I/O Address 0x%lX PCI Address 0x%lX\n", NULL, bus, device, io_addr, pci_addr);
		}
		if (blogic_probeinfo_count < BLOGIC_MAX_ADAPTERS) {
			struct blogic_probeinfo *probeinfo =
				&blogic_probeinfo_list[blogic_probeinfo_count++];
			probeinfo->adapter_type = BLOGIC_FLASHPOINT;
			probeinfo->adapter_bus_type = BLOGIC_PCI_BUS;
			probeinfo->io_addr = io_addr;
			probeinfo->pci_addr = pci_addr;
			probeinfo->bus = bus;
			probeinfo->dev = device;
			probeinfo->irq_ch = irq_ch;
			probeinfo->pci_device = pci_dev_get(pci_device);
			fpcount++;
		} else
			blogic_warn("BusLogic: Too many Host Adapters detected\n", NULL);
#else
		blogic_err("BusLogic: FlashPoint Host Adapter detected at PCI Bus %d Device %d\n", NULL, bus, device);
		blogic_err("BusLogic: I/O Address 0x%lX PCI Address 0x%lX, irq %d, but FlashPoint\n", NULL, io_addr, pci_addr, irq_ch);
		blogic_err("BusLogic: support was omitted in this kernel configuration.\n", NULL);
#endif
	}
	/*
	   The FlashPoint BIOS will scan for FlashPoint Host Adapters in the order of
	   increasing PCI Bus and Device Number, so sort the probe information into
	   the same order the BIOS uses.
	 */
	blogic_sort_probeinfo(&blogic_probeinfo_list[fpindex], fpcount);
	return fpcount;
}


/*
  blogic_init_probeinfo_list initializes the list of I/O Address and Bus
  Probe Information to be checked for potential BusLogic SCSI Host Adapters by
  interrogating the PCI Configuration Space on PCI machines as well as from the
  list of standard BusLogic MultiMaster ISA I/O Addresses.  By default, if both
  FlashPoint and PCI MultiMaster Host Adapters are present, this driver will
  probe for FlashPoint Host Adapters first unless the BIOS primary disk is
  controlled by the first PCI MultiMaster Host Adapter, in which case
  MultiMaster Host Adapters will be probed first.  The BusLogic Driver Options
  specifications "MultiMasterFirst" and "FlashPointFirst" can be used to force
  a particular probe order.
*/

static void __init blogic_init_probeinfo_list(struct blogic_adapter *adapter)
{
	/*
	   If a PCI BIOS is present, interrogate it for MultiMaster and
	   FlashPoint Host Adapters; otherwise, default to the standard
	   ISA MultiMaster probe.
	 */
	if (!blogic_probe_options.noprobe_pci) {
		if (blogic_probe_options.multimaster_first) {
			blogic_init_mm_probeinfo(adapter);
			blogic_init_fp_probeinfo(adapter);
		} else if (blogic_probe_options.flashpoint_first) {
			blogic_init_fp_probeinfo(adapter);
			blogic_init_mm_probeinfo(adapter);
		} else {
			int fpcount = blogic_init_fp_probeinfo(adapter);
			int mmcount = blogic_init_mm_probeinfo(adapter);
			if (fpcount > 0 && mmcount > 0) {
				struct blogic_probeinfo *probeinfo =
					&blogic_probeinfo_list[fpcount];
				struct blogic_adapter *myadapter = adapter;
				struct blogic_fetch_localram fetch_localram;
				struct blogic_bios_drvmap d0_mapbyte;

				while (probeinfo->adapter_bus_type !=
						BLOGIC_PCI_BUS)
					probeinfo++;
				myadapter->io_addr = probeinfo->io_addr;
				fetch_localram.offset =
					BLOGIC_BIOS_BASE + BLOGIC_BIOS_DRVMAP;
				fetch_localram.count = sizeof(d0_mapbyte);
				blogic_cmd(myadapter, BLOGIC_FETCH_LOCALRAM,
						&fetch_localram,
						sizeof(fetch_localram),
						&d0_mapbyte,
						sizeof(d0_mapbyte));
				/*
				   If the Map Byte for BIOS Drive 0 indicates
				   that BIOS Drive 0 is controlled by this
				   PCI MultiMaster Host Adapter, then reverse
				   the probe order so that MultiMaster Host
				   Adapters are probed before FlashPoint Host
				   Adapters.
				 */
				if (d0_mapbyte.diskgeom != BLOGIC_BIOS_NODISK) {
					struct blogic_probeinfo saved_probeinfo[BLOGIC_MAX_ADAPTERS];
					int mmcount = blogic_probeinfo_count - fpcount;

					memcpy(saved_probeinfo,
						blogic_probeinfo_list,
						blogic_probeinfo_count * sizeof(struct blogic_probeinfo));
					memcpy(&blogic_probeinfo_list[0],
						&saved_probeinfo[fpcount],
						mmcount * sizeof(struct blogic_probeinfo));
					memcpy(&blogic_probeinfo_list[mmcount],
						&saved_probeinfo[0],
						fpcount * sizeof(struct blogic_probeinfo));
				}
			}
		}
	}
}


/*
  blogic_failure prints a standardized error message, and then returns false.
*/

static bool blogic_failure(struct blogic_adapter *adapter, char *msg)
{
	blogic_announce_drvr(adapter);
	if (adapter->adapter_bus_type == BLOGIC_PCI_BUS) {
		blogic_err("While configuring BusLogic PCI Host Adapter at\n",
				adapter);
		blogic_err("Bus %d Device %d I/O Address 0x%lX PCI Address 0x%lX:\n", adapter, adapter->bus, adapter->dev, adapter->io_addr, adapter->pci_addr);
	} else
		blogic_err("While configuring BusLogic Host Adapter at I/O Address 0x%lX:\n", adapter, adapter->io_addr);
	blogic_err("%s FAILED - DETACHING\n", adapter, msg);
	if (blogic_cmd_failure_reason != NULL)
		blogic_err("ADDITIONAL FAILURE INFO - %s\n", adapter,
				blogic_cmd_failure_reason);
	return false;
}


/*
  blogic_probe probes for a BusLogic Host Adapter.
*/

static bool __init blogic_probe(struct blogic_adapter *adapter)
{
	union blogic_stat_reg statusreg;
	union blogic_int_reg intreg;
	union blogic_geo_reg georeg;
	/*
	   FlashPoint Host Adapters are Probed by the FlashPoint SCCB Manager.
	 */
	if (blogic_flashpoint_type(adapter)) {
		struct fpoint_info *fpinfo = &adapter->fpinfo;
		fpinfo->base_addr = (u32) adapter->io_addr;
		fpinfo->irq_ch = adapter->irq_ch;
		fpinfo->present = false;
		if (!(FlashPoint_ProbeHostAdapter(fpinfo) == 0 &&
					fpinfo->present)) {
			blogic_err("BusLogic: FlashPoint Host Adapter detected at PCI Bus %d Device %d\n", adapter, adapter->bus, adapter->dev);
			blogic_err("BusLogic: I/O Address 0x%lX PCI Address 0x%lX, but FlashPoint\n", adapter, adapter->io_addr, adapter->pci_addr);
			blogic_err("BusLogic: Probe Function failed to validate it.\n", adapter);
			return false;
		}
		if (blogic_global_options.trace_probe)
			blogic_notice("BusLogic_Probe(0x%lX): FlashPoint Found\n", adapter, adapter->io_addr);
		/*
		   Indicate the Host Adapter Probe completed successfully.
		 */
		return true;
	}
	/*
	   Read the Status, Interrupt, and Geometry Registers to test if there are I/O
	   ports that respond, and to check the values to determine if they are from a
	   BusLogic Host Adapter.  A nonexistent I/O port will return 0xFF, in which
	   case there is definitely no BusLogic Host Adapter at this base I/O Address.
	   The test here is a subset of that used by the BusLogic Host Adapter BIOS.
	 */
	statusreg.all = blogic_rdstatus(adapter);
	intreg.all = blogic_rdint(adapter);
	georeg.all = blogic_rdgeom(adapter);
	if (blogic_global_options.trace_probe)
		blogic_notice("BusLogic_Probe(0x%lX): Status 0x%02X, Interrupt 0x%02X, Geometry 0x%02X\n", adapter, adapter->io_addr, statusreg.all, intreg.all, georeg.all);
	if (statusreg.all == 0 || statusreg.sr.diag_active ||
			statusreg.sr.cmd_param_busy || statusreg.sr.rsvd ||
			statusreg.sr.cmd_invalid || intreg.ir.rsvd != 0)
		return false;
	/*
	   Check the undocumented Geometry Register to test if there is
	   an I/O port that responded.  Adaptec Host Adapters do not
	   implement the Geometry Register, so this test helps serve to
	   avoid incorrectly recognizing an Adaptec 1542A or 1542B as a
	   BusLogic.  Unfortunately, the Adaptec 1542C series does respond
	   to the Geometry Register I/O port, but it will be rejected
	   later when the Inquire Extended Setup Information command is
	   issued in blogic_checkadapter.  The AMI FastDisk Host Adapter
	   is a BusLogic clone that implements the same interface as
	   earlier BusLogic Host Adapters, including the undocumented
	   commands, and is therefore supported by this driver. However,
	   the AMI FastDisk always returns 0x00 upon reading the Geometry
	   Register, so the extended translation option should always be
	   left disabled on the AMI FastDisk.
	 */
	if (georeg.all == 0xFF)
		return false;
	/*
	   Indicate the Host Adapter Probe completed successfully.
	 */
	return true;
}


/*
  blogic_hwreset issues a Hardware Reset to the Host Adapter
  and waits for Host Adapter Diagnostics to complete.  If hard_reset is true, a
  Hard Reset is performed which also initiates a SCSI Bus Reset.  Otherwise, a
  Soft Reset is performed which only resets the Host Adapter without forcing a
  SCSI Bus Reset.
*/

static bool blogic_hwreset(struct blogic_adapter *adapter, bool hard_reset)
{
	union blogic_stat_reg statusreg;
	int timeout;
	/*
	   FlashPoint Host Adapters are Hard Reset by the FlashPoint
	   SCCB Manager.
	 */
	if (blogic_flashpoint_type(adapter)) {
		struct fpoint_info *fpinfo = &adapter->fpinfo;
		fpinfo->softreset = !hard_reset;
		fpinfo->report_underrun = true;
		adapter->cardhandle =
			FlashPoint_HardwareResetHostAdapter(fpinfo);
		if (adapter->cardhandle == (void *)FPOINT_BADCARD_HANDLE)
			return false;
		/*
		   Indicate the Host Adapter Hard Reset completed successfully.
		 */
		return true;
	}
	/*
	   Issue a Hard Reset or Soft Reset Command to the Host Adapter.
	   The Host Adapter should respond by setting Diagnostic Active in
	   the Status Register.
	 */
	if (hard_reset)
		blogic_hardreset(adapter);
	else
		blogic_softreset(adapter);
	/*
	   Wait until Diagnostic Active is set in the Status Register.
	 */
	timeout = 5 * 10000;
	while (--timeout >= 0) {
		statusreg.all = blogic_rdstatus(adapter);
		if (statusreg.sr.diag_active)
			break;
		udelay(100);
	}
	if (blogic_global_options.trace_hw_reset)
		blogic_notice("BusLogic_HardwareReset(0x%lX): Diagnostic Active, Status 0x%02X\n", adapter, adapter->io_addr, statusreg.all);
	if (timeout < 0)
		return false;
	/*
	   Wait 100 microseconds to allow completion of any initial diagnostic
	   activity which might leave the contents of the Status Register
	   unpredictable.
	 */
	udelay(100);
	/*
	   Wait until Diagnostic Active is reset in the Status Register.
	 */
	timeout = 10 * 10000;
	while (--timeout >= 0) {
		statusreg.all = blogic_rdstatus(adapter);
		if (!statusreg.sr.diag_active)
			break;
		udelay(100);
	}
	if (blogic_global_options.trace_hw_reset)
		blogic_notice("BusLogic_HardwareReset(0x%lX): Diagnostic Completed, Status 0x%02X\n", adapter, adapter->io_addr, statusreg.all);
	if (timeout < 0)
		return false;
	/*
	   Wait until at least one of the Diagnostic Failure, Host Adapter
	   Ready, or Data In Register Ready bits is set in the Status Register.
	 */
	timeout = 10000;
	while (--timeout >= 0) {
		statusreg.all = blogic_rdstatus(adapter);
		if (statusreg.sr.diag_failed || statusreg.sr.adapter_ready ||
				statusreg.sr.datain_ready)
			break;
		udelay(100);
	}
	if (blogic_global_options.trace_hw_reset)
		blogic_notice("BusLogic_HardwareReset(0x%lX): Host Adapter Ready, Status 0x%02X\n", adapter, adapter->io_addr, statusreg.all);
	if (timeout < 0)
		return false;
	/*
	   If Diagnostic Failure is set or Host Adapter Ready is reset,
	   then an error occurred during the Host Adapter diagnostics.
	   If Data In Register Ready is set, then there is an Error Code
	   available.
	 */
	if (statusreg.sr.diag_failed || !statusreg.sr.adapter_ready) {
		blogic_cmd_failure_reason = NULL;
		blogic_failure(adapter, "HARD RESET DIAGNOSTICS");
		blogic_err("HOST ADAPTER STATUS REGISTER = %02X\n", adapter,
				statusreg.all);
		if (statusreg.sr.datain_ready)
			blogic_err("HOST ADAPTER ERROR CODE = %d\n", adapter,
					blogic_rddatain(adapter));
		return false;
	}
	/*
	   Indicate the Host Adapter Hard Reset completed successfully.
	 */
	return true;
}


/*
  blogic_checkadapter checks to be sure this really is a BusLogic
  Host Adapter.
*/

static bool __init blogic_checkadapter(struct blogic_adapter *adapter)
{
	struct blogic_ext_setup ext_setupinfo;
	unsigned char req_replylen;
	bool result = true;
	/*
	   FlashPoint Host Adapters do not require this protection.
	 */
	if (blogic_flashpoint_type(adapter))
		return true;
	/*
	   Issue the Inquire Extended Setup Information command. Only genuine
	   BusLogic Host Adapters and true clones support this command.
	   Adaptec 1542C series Host Adapters that respond to the Geometry
	   Register I/O port will fail this command.
	 */
	req_replylen = sizeof(ext_setupinfo);
	if (blogic_cmd(adapter, BLOGIC_INQ_EXTSETUP, &req_replylen,
				sizeof(req_replylen), &ext_setupinfo,
				sizeof(ext_setupinfo)) != sizeof(ext_setupinfo))
		result = false;
	/*
	   Provide tracing information if requested and return.
	 */
	if (blogic_global_options.trace_probe)
		blogic_notice("BusLogic_Check(0x%lX): MultiMaster %s\n", adapter,
				adapter->io_addr,
				(result ? "Found" : "Not Found"));
	return result;
}


/*
  blogic_rdconfig reads the Configuration Information
  from Host Adapter and initializes the Host Adapter structure.
*/

static bool __init blogic_rdconfig(struct blogic_adapter *adapter)
{
	struct blogic_board_id id;
	struct blogic_config config;
	struct blogic_setup_info setupinfo;
	struct blogic_ext_setup ext_setupinfo;
	unsigned char model[5];
	unsigned char fw_ver_digit3;
	unsigned char fw_ver_letter;
	struct blogic_adapter_info adapter_info;
	struct blogic_fetch_localram fetch_localram;
	struct blogic_autoscsi autoscsi;
	union blogic_geo_reg georeg;
	unsigned char req_replylen;
	unsigned char *tgt, ch;
	int tgt_id, i;
	/*
	   Configuration Information for FlashPoint Host Adapters is
	   provided in the fpoint_info structure by the FlashPoint
	   SCCB Manager's Probe Function. Initialize fields in the
	   Host Adapter structure from the fpoint_info structure.
	 */
	if (blogic_flashpoint_type(adapter)) {
		struct fpoint_info *fpinfo = &adapter->fpinfo;
		tgt = adapter->model;
		*tgt++ = 'B';
		*tgt++ = 'T';
		*tgt++ = '-';
		for (i = 0; i < sizeof(fpinfo->model); i++)
			*tgt++ = fpinfo->model[i];
		*tgt++ = '\0';
		strcpy(adapter->fw_ver, FLASHPOINT_FW_VER);
		adapter->scsi_id = fpinfo->scsi_id;
		adapter->ext_trans_enable = fpinfo->ext_trans_enable;
		adapter->parity = fpinfo->parity;
		adapter->reset_enabled = !fpinfo->softreset;
		adapter->level_int = true;
		adapter->wide = fpinfo->wide;
		adapter->differential = false;
		adapter->scam = true;
		adapter->ultra = true;
		adapter->ext_lun = true;
		adapter->terminfo_valid = true;
		adapter->low_term = fpinfo->low_term;
		adapter->high_term = fpinfo->high_term;
		adapter->scam_enabled = fpinfo->scam_enabled;
		adapter->scam_lev2 = fpinfo->scam_lev2;
		adapter->drvr_sglimit = BLOGIC_SG_LIMIT;
		adapter->maxdev = (adapter->wide ? 16 : 8);
		adapter->maxlun = 32;
		adapter->initccbs = 4 * BLOGIC_CCB_GRP_ALLOCSIZE;
		adapter->inc_ccbs = BLOGIC_CCB_GRP_ALLOCSIZE;
		adapter->drvr_qdepth = 255;
		adapter->adapter_qdepth = adapter->drvr_qdepth;
		adapter->sync_ok = fpinfo->sync_ok;
		adapter->fast_ok = fpinfo->fast_ok;
		adapter->ultra_ok = fpinfo->ultra_ok;
		adapter->wide_ok = fpinfo->wide_ok;
		adapter->discon_ok = fpinfo->discon_ok;
		adapter->tagq_ok = 0xFFFF;
		goto common;
	}
	/*
	   Issue the Inquire Board ID command.
	 */
	if (blogic_cmd(adapter, BLOGIC_GET_BOARD_ID, NULL, 0, &id,
				sizeof(id)) != sizeof(id))
		return blogic_failure(adapter, "INQUIRE BOARD ID");
	/*
	   Issue the Inquire Configuration command.
	 */
	if (blogic_cmd(adapter, BLOGIC_INQ_CONFIG, NULL, 0, &config,
				sizeof(config))
	    != sizeof(config))
		return blogic_failure(adapter, "INQUIRE CONFIGURATION");
	/*
	   Issue the Inquire Setup Information command.
	 */
	req_replylen = sizeof(setupinfo);
	if (blogic_cmd(adapter, BLOGIC_INQ_SETUPINFO, &req_replylen,
				sizeof(req_replylen), &setupinfo,
				sizeof(setupinfo)) != sizeof(setupinfo))
		return blogic_failure(adapter, "INQUIRE SETUP INFORMATION");
	/*
	   Issue the Inquire Extended Setup Information command.
	 */
	req_replylen = sizeof(ext_setupinfo);
	if (blogic_cmd(adapter, BLOGIC_INQ_EXTSETUP, &req_replylen,
				sizeof(req_replylen), &ext_setupinfo,
				sizeof(ext_setupinfo)) != sizeof(ext_setupinfo))
		return blogic_failure(adapter,
					"INQUIRE EXTENDED SETUP INFORMATION");
	/*
	   Issue the Inquire Firmware Version 3rd Digit command.
	 */
	fw_ver_digit3 = '\0';
	if (id.fw_ver_digit1 > '0')
		if (blogic_cmd(adapter, BLOGIC_INQ_FWVER_D3, NULL, 0,
				&fw_ver_digit3,
				sizeof(fw_ver_digit3)) != sizeof(fw_ver_digit3))
			return blogic_failure(adapter,
						"INQUIRE FIRMWARE 3RD DIGIT");
	/*
	   Issue the Inquire Host Adapter Model Number command.
	 */
	if (ext_setupinfo.bus_type == 'A' && id.fw_ver_digit1 == '2')
		/* BusLogic BT-542B ISA 2.xx */
		strcpy(model, "542B");
	else if (ext_setupinfo.bus_type == 'E' && id.fw_ver_digit1 == '2' &&
			(id.fw_ver_digit2 <= '1' || (id.fw_ver_digit2 == '2' &&
						     fw_ver_digit3 == '0')))
		/* BusLogic BT-742A EISA 2.1x or 2.20 */
		strcpy(model, "742A");
	else if (ext_setupinfo.bus_type == 'E' && id.fw_ver_digit1 == '0')
		/* AMI FastDisk EISA Series 441 0.x */
		strcpy(model, "747A");
	else {
		req_replylen = sizeof(model);
		if (blogic_cmd(adapter, BLOGIC_INQ_MODELNO, &req_replylen,
					sizeof(req_replylen), &model,
					sizeof(model)) != sizeof(model))
			return blogic_failure(adapter,
					"INQUIRE HOST ADAPTER MODEL NUMBER");
	}
	/*
	   BusLogic MultiMaster Host Adapters can be identified by their
	   model number and the major version number of their firmware
	   as follows:

	   5.xx       BusLogic "W" Series Host Adapters:
	   BT-948/958/958D
	   4.xx       BusLogic "C" Series Host Adapters:
	   BT-946C/956C/956CD/747C/757C/757CD/445C/545C/540CF
	   3.xx       BusLogic "S" Series Host Adapters:
	   BT-747S/747D/757S/757D/445S/545S/542D
	   BT-542B/742A (revision H)
	   2.xx       BusLogic "A" Series Host Adapters:
	   BT-542B/742A (revision G and below)
	   0.xx       AMI FastDisk VLB/EISA BusLogic Clone Host Adapter
	 */
	/*
	   Save the Model Name and Host Adapter Name in the Host Adapter
	   structure.
	 */
	tgt = adapter->model;
	*tgt++ = 'B';
	*tgt++ = 'T';
	*tgt++ = '-';
	for (i = 0; i < sizeof(model); i++) {
		ch = model[i];
		if (ch == ' ' || ch == '\0')
			break;
		*tgt++ = ch;
	}
	*tgt++ = '\0';
	/*
	   Save the Firmware Version in the Host Adapter structure.
	 */
	tgt = adapter->fw_ver;
	*tgt++ = id.fw_ver_digit1;
	*tgt++ = '.';
	*tgt++ = id.fw_ver_digit2;
	if (fw_ver_digit3 != ' ' && fw_ver_digit3 != '\0')
		*tgt++ = fw_ver_digit3;
	*tgt = '\0';
	/*
	   Issue the Inquire Firmware Version Letter command.
	 */
	if (strcmp(adapter->fw_ver, "3.3") >= 0) {
		if (blogic_cmd(adapter, BLOGIC_INQ_FWVER_LETTER, NULL, 0,
				&fw_ver_letter,
				sizeof(fw_ver_letter)) != sizeof(fw_ver_letter))
			return blogic_failure(adapter,
					"INQUIRE FIRMWARE VERSION LETTER");
		if (fw_ver_letter != ' ' && fw_ver_letter != '\0')
			*tgt++ = fw_ver_letter;
		*tgt = '\0';
	}
	/*
	   Save the Host Adapter SCSI ID in the Host Adapter structure.
	 */
	adapter->scsi_id = config.id;
	/*
	   Determine the Bus Type and save it in the Host Adapter structure,
	   determine and save the IRQ Channel if necessary, and determine
	   and save the DMA Channel for ISA Host Adapters.
	 */
	adapter->adapter_bus_type =
			blogic_adater_bus_types[adapter->model[3] - '4'];
	if (adapter->irq_ch == 0) {
		if (config.irq_ch9)
			adapter->irq_ch = 9;
		else if (config.irq_ch10)
			adapter->irq_ch = 10;
		else if (config.irq_ch11)
			adapter->irq_ch = 11;
		else if (config.irq_ch12)
			adapter->irq_ch = 12;
		else if (config.irq_ch14)
			adapter->irq_ch = 14;
		else if (config.irq_ch15)
			adapter->irq_ch = 15;
	}
	/*
	   Determine whether Extended Translation is enabled and save it in
	   the Host Adapter structure.
	 */
	georeg.all = blogic_rdgeom(adapter);
	adapter->ext_trans_enable = georeg.gr.ext_trans_enable;
	/*
	   Save the Scatter Gather Limits, Level Sensitive Interrupt flag, Wide
	   SCSI flag, Differential SCSI flag, SCAM Supported flag, and
	   Ultra SCSI flag in the Host Adapter structure.
	 */
	adapter->adapter_sglimit = ext_setupinfo.sg_limit;
	adapter->drvr_sglimit = adapter->adapter_sglimit;
	if (adapter->adapter_sglimit > BLOGIC_SG_LIMIT)
		adapter->drvr_sglimit = BLOGIC_SG_LIMIT;
	if (ext_setupinfo.misc.level_int)
		adapter->level_int = true;
	adapter->wide = ext_setupinfo.wide;
	adapter->differential = ext_setupinfo.differential;
	adapter->scam = ext_setupinfo.scam;
	adapter->ultra = ext_setupinfo.ultra;
	/*
	   Determine whether Extended LUN Format CCBs are supported and save the
	   information in the Host Adapter structure.
	 */
	if (adapter->fw_ver[0] == '5' || (adapter->fw_ver[0] == '4' &&
				adapter->wide))
		adapter->ext_lun = true;
	/*
	   Issue the Inquire PCI Host Adapter Information command to read the
	   Termination Information from "W" series MultiMaster Host Adapters.
	 */
	if (adapter->fw_ver[0] == '5') {
		if (blogic_cmd(adapter, BLOGIC_INQ_PCI_INFO, NULL, 0,
				&adapter_info,
				sizeof(adapter_info)) != sizeof(adapter_info))
			return blogic_failure(adapter,
					"INQUIRE PCI HOST ADAPTER INFORMATION");
		/*
		   Save the Termination Information in the Host Adapter
		   structure.
		 */
		if (adapter_info.genericinfo_valid) {
			adapter->terminfo_valid = true;
			adapter->low_term = adapter_info.low_term;
			adapter->high_term = adapter_info.high_term;
		}
	}
	/*
	   Issue the Fetch Host Adapter Local RAM command to read the
	   AutoSCSI data from "W" and "C" series MultiMaster Host Adapters.
	 */
	if (adapter->fw_ver[0] >= '4') {
		fetch_localram.offset = BLOGIC_AUTOSCSI_BASE;
		fetch_localram.count = sizeof(autoscsi);
		if (blogic_cmd(adapter, BLOGIC_FETCH_LOCALRAM, &fetch_localram,
					sizeof(fetch_localram), &autoscsi,
					sizeof(autoscsi)) != sizeof(autoscsi))
			return blogic_failure(adapter,
						"FETCH HOST ADAPTER LOCAL RAM");
		/*
		   Save the Parity Checking Enabled, Bus Reset Enabled,
		   and Termination Information in the Host Adapter structure.
		 */
		adapter->parity = autoscsi.parity;
		adapter->reset_enabled = autoscsi.reset_enabled;
		if (adapter->fw_ver[0] == '4') {
			adapter->terminfo_valid = true;
			adapter->low_term = autoscsi.low_term;
			adapter->high_term = autoscsi.high_term;
		}
		/*
		   Save the Wide Permitted, Fast Permitted, Synchronous
		   Permitted, Disconnect Permitted, Ultra Permitted, and
		   SCAM Information in the Host Adapter structure.
		 */
		adapter->wide_ok = autoscsi.wide_ok;
		adapter->fast_ok = autoscsi.fast_ok;
		adapter->sync_ok = autoscsi.sync_ok;
		adapter->discon_ok = autoscsi.discon_ok;
		if (adapter->ultra)
			adapter->ultra_ok = autoscsi.ultra_ok;
		if (adapter->scam) {
			adapter->scam_enabled = autoscsi.scam_enabled;
			adapter->scam_lev2 = autoscsi.scam_lev2;
		}
	}
	/*
	   Initialize fields in the Host Adapter structure for "S" and "A"
	   series MultiMaster Host Adapters.
	 */
	if (adapter->fw_ver[0] < '4') {
		if (setupinfo.sync) {
			adapter->sync_ok = 0xFF;
			if (adapter->adapter_bus_type == BLOGIC_EISA_BUS) {
				if (ext_setupinfo.misc.fast_on_eisa)
					adapter->fast_ok = 0xFF;
				if (strcmp(adapter->model, "BT-757") == 0)
					adapter->wide_ok = 0xFF;
			}
		}
		adapter->discon_ok = 0xFF;
		adapter->parity = setupinfo.parity;
		adapter->reset_enabled = true;
	}
	/*
	   Determine the maximum number of Target IDs and Logical Units
	   supported by this driver for Wide and Narrow Host Adapters.
	 */
	adapter->maxdev = (adapter->wide ? 16 : 8);
	adapter->maxlun = (adapter->ext_lun ? 32 : 8);
	/*
	   Select appropriate values for the Mailbox Count, Driver Queue Depth,
	   Initial CCBs, and Incremental CCBs variables based on whether
	   or not Strict Round Robin Mode is supported.  If Strict Round
	   Robin Mode is supported, then there is no performance degradation
	   in using the maximum possible number of Outgoing and Incoming
	   Mailboxes and allowing the Tagged and Untagged Queue Depths to
	   determine the actual utilization.  If Strict Round Robin Mode is
	   not supported, then the Host Adapter must scan all the Outgoing
	   Mailboxes whenever an Outgoing Mailbox entry is made, which can
	   cause a substantial performance penalty.  The host adapters
	   actually have room to store the following number of CCBs
	   internally; that is, they can internally queue and manage this
	   many active commands on the SCSI bus simultaneously.  Performance
	   measurements demonstrate that the Driver Queue Depth should be
	   set to the Mailbox Count, rather than the Host Adapter Queue
	   Depth (internal CCB capacity), as it is more efficient to have the
	   queued commands waiting in Outgoing Mailboxes if necessary than
	   to block the process in the higher levels of the SCSI Subsystem.

	   192          BT-948/958/958D
	   100          BT-946C/956C/956CD/747C/757C/757CD/445C
	   50   BT-545C/540CF
	   30   BT-747S/747D/757S/757D/445S/545S/542D/542B/742A
	 */
	if (adapter->fw_ver[0] == '5')
		adapter->adapter_qdepth = 192;
	else if (adapter->fw_ver[0] == '4')
		adapter->adapter_qdepth = 100;
	else
		adapter->adapter_qdepth = 30;
	if (strcmp(adapter->fw_ver, "3.31") >= 0) {
		adapter->strict_rr = true;
		adapter->mbox_count = BLOGIC_MAX_MAILBOX;
	} else {
		adapter->strict_rr = false;
		adapter->mbox_count = 32;
	}
	adapter->drvr_qdepth = adapter->mbox_count;
	adapter->initccbs = 4 * BLOGIC_CCB_GRP_ALLOCSIZE;
	adapter->inc_ccbs = BLOGIC_CCB_GRP_ALLOCSIZE;
	/*
	   Tagged Queuing support is available and operates properly on
	   all "W" series MultiMaster Host Adapters, on "C" series
	   MultiMaster Host Adapters with firmware version 4.22 and above,
	   and on "S" series MultiMaster Host Adapters with firmware version
	   3.35 and above.
	 */
	adapter->tagq_ok = 0;
	switch (adapter->fw_ver[0]) {
	case '5':
		adapter->tagq_ok = 0xFFFF;
		break;
	case '4':
		if (strcmp(adapter->fw_ver, "4.22") >= 0)
			adapter->tagq_ok = 0xFFFF;
		break;
	case '3':
		if (strcmp(adapter->fw_ver, "3.35") >= 0)
			adapter->tagq_ok = 0xFFFF;
		break;
	}
	/*
	   Determine the Host Adapter BIOS Address if the BIOS is enabled and
	   save it in the Host Adapter structure.  The BIOS is disabled if the
	   bios_addr is 0.
	 */
	adapter->bios_addr = ext_setupinfo.bios_addr << 12;
	/*
	   BusLogic BT-445S Host Adapters prior to board revision E have a
	   hardware bug whereby when the BIOS is enabled, transfers to/from
	   the same address range the BIOS occupies modulo 16MB are handled
	   incorrectly.  Only properly functioning BT-445S Host Adapters
	   have firmware version 3.37.
	 */
	if (adapter->bios_addr > 0 &&
	    strcmp(adapter->model, "BT-445S") == 0 &&
	    strcmp(adapter->fw_ver, "3.37") < 0)
		return blogic_failure(adapter, "Too old firmware");
	/*
	   Initialize parameters common to MultiMaster and FlashPoint
	   Host Adapters.
	 */
common:
	/*
	   Initialize the Host Adapter Full Model Name from the Model Name.
	 */
	strcpy(adapter->full_model, "BusLogic ");
	strcat(adapter->full_model, adapter->model);
	/*
	   Select an appropriate value for the Tagged Queue Depth either from a
	   BusLogic Driver Options specification, or based on whether this Host
	   Adapter requires that ISA Bounce Buffers be used.  The Tagged Queue
	   Depth is left at 0 for automatic determination in
	   BusLogic_SelectQueueDepths. Initialize the Untagged Queue Depth.
	 */
	for (tgt_id = 0; tgt_id < BLOGIC_MAXDEV; tgt_id++) {
		unsigned char qdepth = 0;
		if (adapter->drvr_opts != NULL &&
				adapter->drvr_opts->qdepth[tgt_id] > 0)
			qdepth = adapter->drvr_opts->qdepth[tgt_id];
		adapter->qdepth[tgt_id] = qdepth;
	}
	adapter->untag_qdepth = BLOGIC_UNTAG_DEPTH;
	if (adapter->drvr_opts != NULL)
		adapter->common_qdepth = adapter->drvr_opts->common_qdepth;
	if (adapter->common_qdepth > 0 &&
			adapter->common_qdepth < adapter->untag_qdepth)
		adapter->untag_qdepth = adapter->common_qdepth;
	/*
	   Tagged Queuing is only allowed if Disconnect/Reconnect is permitted.
	   Therefore, mask the Tagged Queuing Permitted Default bits with the
	   Disconnect/Reconnect Permitted bits.
	 */
	adapter->tagq_ok &= adapter->discon_ok;
	/*
	   Combine the default Tagged Queuing Permitted bits with any
	   BusLogic Driver Options Tagged Queuing specification.
	 */
	if (adapter->drvr_opts != NULL)
		adapter->tagq_ok = (adapter->drvr_opts->tagq_ok &
				adapter->drvr_opts->tagq_ok_mask) |
			(adapter->tagq_ok & ~adapter->drvr_opts->tagq_ok_mask);

	/*
	   Select an appropriate value for Bus Settle Time either from a
	   BusLogic Driver Options specification, or from
	   BLOGIC_BUS_SETTLE_TIME.
	 */
	if (adapter->drvr_opts != NULL &&
			adapter->drvr_opts->bus_settle_time > 0)
		adapter->bus_settle_time = adapter->drvr_opts->bus_settle_time;
	else
		adapter->bus_settle_time = BLOGIC_BUS_SETTLE_TIME;
	/*
	   Indicate reading the Host Adapter Configuration completed
	   successfully.
	 */
	return true;
}


/*
  blogic_reportconfig reports the configuration of Host Adapter.
*/

static bool __init blogic_reportconfig(struct blogic_adapter *adapter)
{
	unsigned short alltgt_mask = (1 << adapter->maxdev) - 1;
	unsigned short sync_ok, fast_ok;
	unsigned short ultra_ok, wide_ok;
	unsigned short discon_ok, tagq_ok;
	bool common_syncneg, common_tagq_depth;
	char syncstr[BLOGIC_MAXDEV + 1];
	char widestr[BLOGIC_MAXDEV + 1];
	char discon_str[BLOGIC_MAXDEV + 1];
	char tagq_str[BLOGIC_MAXDEV + 1];
	char *syncmsg = syncstr;
	char *widemsg = widestr;
	char *discon_msg = discon_str;
	char *tagq_msg = tagq_str;
	int tgt_id;

	blogic_info("Configuring BusLogic Model %s %s%s%s%s SCSI Host Adapter\n", adapter, adapter->model, blogic_adapter_busnames[adapter->adapter_bus_type], (adapter->wide ? " Wide" : ""), (adapter->differential ? " Differential" : ""), (adapter->ultra ? " Ultra" : ""));
	blogic_info("  Firmware Version: %s, I/O Address: 0x%lX, IRQ Channel: %d/%s\n", adapter, adapter->fw_ver, adapter->io_addr, adapter->irq_ch, (adapter->level_int ? "Level" : "Edge"));
	if (adapter->adapter_bus_type != BLOGIC_PCI_BUS) {
		blogic_info("  DMA Channel: None, ", adapter);
		if (adapter->bios_addr > 0)
			blogic_info("BIOS Address: 0x%X, ", adapter,
					adapter->bios_addr);
		else
			blogic_info("BIOS Address: None, ", adapter);
	} else {
		blogic_info("  PCI Bus: %d, Device: %d, Address: ", adapter,
				adapter->bus, adapter->dev);
		if (adapter->pci_addr > 0)
			blogic_info("0x%lX, ", adapter, adapter->pci_addr);
		else
			blogic_info("Unassigned, ", adapter);
	}
	blogic_info("Host Adapter SCSI ID: %d\n", adapter, adapter->scsi_id);
	blogic_info("  Parity Checking: %s, Extended Translation: %s\n",
			adapter, (adapter->parity ? "Enabled" : "Disabled"),
			(adapter->ext_trans_enable ? "Enabled" : "Disabled"));
	alltgt_mask &= ~(1 << adapter->scsi_id);
	sync_ok = adapter->sync_ok & alltgt_mask;
	fast_ok = adapter->fast_ok & alltgt_mask;
	ultra_ok = adapter->ultra_ok & alltgt_mask;
	if ((blogic_multimaster_type(adapter) &&
			(adapter->fw_ver[0] >= '4' ||
			 adapter->adapter_bus_type == BLOGIC_EISA_BUS)) ||
			blogic_flashpoint_type(adapter)) {
		common_syncneg = false;
		if (sync_ok == 0) {
			syncmsg = "Disabled";
			common_syncneg = true;
		} else if (sync_ok == alltgt_mask) {
			if (fast_ok == 0) {
				syncmsg = "Slow";
				common_syncneg = true;
			} else if (fast_ok == alltgt_mask) {
				if (ultra_ok == 0) {
					syncmsg = "Fast";
					common_syncneg = true;
				} else if (ultra_ok == alltgt_mask) {
					syncmsg = "Ultra";
					common_syncneg = true;
				}
			}
		}
		if (!common_syncneg) {
			for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
				syncstr[tgt_id] = ((!(sync_ok & (1 << tgt_id))) ? 'N' : (!(fast_ok & (1 << tgt_id)) ? 'S' : (!(ultra_ok & (1 << tgt_id)) ? 'F' : 'U')));
			syncstr[adapter->scsi_id] = '#';
			syncstr[adapter->maxdev] = '\0';
		}
	} else
		syncmsg = (sync_ok == 0 ? "Disabled" : "Enabled");
	wide_ok = adapter->wide_ok & alltgt_mask;
	if (wide_ok == 0)
		widemsg = "Disabled";
	else if (wide_ok == alltgt_mask)
		widemsg = "Enabled";
	else {
		for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
			widestr[tgt_id] = ((wide_ok & (1 << tgt_id)) ? 'Y' : 'N');
		widestr[adapter->scsi_id] = '#';
		widestr[adapter->maxdev] = '\0';
	}
	discon_ok = adapter->discon_ok & alltgt_mask;
	if (discon_ok == 0)
		discon_msg = "Disabled";
	else if (discon_ok == alltgt_mask)
		discon_msg = "Enabled";
	else {
		for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
			discon_str[tgt_id] = ((discon_ok & (1 << tgt_id)) ? 'Y' : 'N');
		discon_str[adapter->scsi_id] = '#';
		discon_str[adapter->maxdev] = '\0';
	}
	tagq_ok = adapter->tagq_ok & alltgt_mask;
	if (tagq_ok == 0)
		tagq_msg = "Disabled";
	else if (tagq_ok == alltgt_mask)
		tagq_msg = "Enabled";
	else {
		for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
			tagq_str[tgt_id] = ((tagq_ok & (1 << tgt_id)) ? 'Y' : 'N');
		tagq_str[adapter->scsi_id] = '#';
		tagq_str[adapter->maxdev] = '\0';
	}
	blogic_info("  Synchronous Negotiation: %s, Wide Negotiation: %s\n",
			adapter, syncmsg, widemsg);
	blogic_info("  Disconnect/Reconnect: %s, Tagged Queuing: %s\n", adapter,
			discon_msg, tagq_msg);
	if (blogic_multimaster_type(adapter)) {
		blogic_info("  Scatter/Gather Limit: %d of %d segments, Mailboxes: %d\n", adapter, adapter->drvr_sglimit, adapter->adapter_sglimit, adapter->mbox_count);
		blogic_info("  Driver Queue Depth: %d, Host Adapter Queue Depth: %d\n", adapter, adapter->drvr_qdepth, adapter->adapter_qdepth);
	} else
		blogic_info("  Driver Queue Depth: %d, Scatter/Gather Limit: %d segments\n", adapter, adapter->drvr_qdepth, adapter->drvr_sglimit);
	blogic_info("  Tagged Queue Depth: ", adapter);
	common_tagq_depth = true;
	for (tgt_id = 1; tgt_id < adapter->maxdev; tgt_id++)
		if (adapter->qdepth[tgt_id] != adapter->qdepth[0]) {
			common_tagq_depth = false;
			break;
		}
	if (common_tagq_depth) {
		if (adapter->qdepth[0] > 0)
			blogic_info("%d", adapter, adapter->qdepth[0]);
		else
			blogic_info("Automatic", adapter);
	} else
		blogic_info("Individual", adapter);
	blogic_info(", Untagged Queue Depth: %d\n", adapter,
			adapter->untag_qdepth);
	if (adapter->terminfo_valid) {
		if (adapter->wide)
			blogic_info("  SCSI Bus Termination: %s", adapter,
				(adapter->low_term ? (adapter->high_term ? "Both Enabled" : "Low Enabled") : (adapter->high_term ? "High Enabled" : "Both Disabled")));
		else
			blogic_info("  SCSI Bus Termination: %s", adapter,
				(adapter->low_term ? "Enabled" : "Disabled"));
		if (adapter->scam)
			blogic_info(", SCAM: %s", adapter,
				(adapter->scam_enabled ? (adapter->scam_lev2 ? "Enabled, Level 2" : "Enabled, Level 1") : "Disabled"));
		blogic_info("\n", adapter);
	}
	/*
	   Indicate reporting the Host Adapter configuration completed
	   successfully.
	 */
	return true;
}


/*
  blogic_getres acquires the system resources necessary to use
  Host Adapter.
*/

static bool __init blogic_getres(struct blogic_adapter *adapter)
{
	if (adapter->irq_ch == 0) {
		blogic_err("NO LEGAL INTERRUPT CHANNEL ASSIGNED - DETACHING\n",
				adapter);
		return false;
	}
	/*
	   Acquire shared access to the IRQ Channel.
	 */
	if (request_irq(adapter->irq_ch, blogic_inthandler, IRQF_SHARED,
				adapter->full_model, adapter) < 0) {
		blogic_err("UNABLE TO ACQUIRE IRQ CHANNEL %d - DETACHING\n",
				adapter, adapter->irq_ch);
		return false;
	}
	adapter->irq_acquired = true;
	/*
	   Indicate the System Resource Acquisition completed successfully,
	 */
	return true;
}


/*
  blogic_relres releases any system resources previously acquired
  by blogic_getres.
*/

static void blogic_relres(struct blogic_adapter *adapter)
{
	/*
	   Release shared access to the IRQ Channel.
	 */
	if (adapter->irq_acquired)
		free_irq(adapter->irq_ch, adapter);
	/*
	   Release any allocated memory structs not released elsewhere
	 */
	if (adapter->mbox_space)
		dma_free_coherent(&adapter->pci_device->dev, adapter->mbox_sz,
			adapter->mbox_space, adapter->mbox_space_handle);
	pci_dev_put(adapter->pci_device);
	adapter->mbox_space = NULL;
	adapter->mbox_space_handle = 0;
	adapter->mbox_sz = 0;
}


/*
  blogic_initadapter initializes Host Adapter.  This is the only
  function called during SCSI Host Adapter detection which modifies the state
  of the Host Adapter from its initial power on or hard reset state.
*/

static bool blogic_initadapter(struct blogic_adapter *adapter)
{
	struct blogic_extmbox_req extmbox_req;
	enum blogic_rr_req rr_req;
	enum blogic_setccb_fmt setccb_fmt;
	int tgt_id;

	/*
	   Initialize the pointers to the first and last CCBs that are
	   queued for completion processing.
	 */
	adapter->firstccb = NULL;
	adapter->lastccb = NULL;

	/*
	   Initialize the Bus Device Reset Pending CCB, Tagged Queuing Active,
	   Command Successful Flag, Active Commands, and Commands Since Reset
	   for each Target Device.
	 */
	for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++) {
		adapter->bdr_pend[tgt_id] = NULL;
		adapter->tgt_flags[tgt_id].tagq_active = false;
		adapter->tgt_flags[tgt_id].cmd_good = false;
		adapter->active_cmds[tgt_id] = 0;
		adapter->cmds_since_rst[tgt_id] = 0;
	}

	/*
	   FlashPoint Host Adapters do not use Outgoing and Incoming Mailboxes.
	 */
	if (blogic_flashpoint_type(adapter))
		goto done;

	/*
	   Initialize the Outgoing and Incoming Mailbox pointers.
	 */
	adapter->mbox_sz = adapter->mbox_count * (sizeof(struct blogic_outbox) + sizeof(struct blogic_inbox));
	adapter->mbox_space = dma_alloc_coherent(&adapter->pci_device->dev,
				adapter->mbox_sz, &adapter->mbox_space_handle,
				GFP_KERNEL);
	if (adapter->mbox_space == NULL)
		return blogic_failure(adapter, "MAILBOX ALLOCATION");
	adapter->first_outbox = (struct blogic_outbox *) adapter->mbox_space;
	adapter->last_outbox = adapter->first_outbox + adapter->mbox_count - 1;
	adapter->next_outbox = adapter->first_outbox;
	adapter->first_inbox = (struct blogic_inbox *) (adapter->last_outbox + 1);
	adapter->last_inbox = adapter->first_inbox + adapter->mbox_count - 1;
	adapter->next_inbox = adapter->first_inbox;

	/*
	   Initialize the Outgoing and Incoming Mailbox structures.
	 */
	memset(adapter->first_outbox, 0,
			adapter->mbox_count * sizeof(struct blogic_outbox));
	memset(adapter->first_inbox, 0,
			adapter->mbox_count * sizeof(struct blogic_inbox));

	/*
	   Initialize the Host Adapter's Pointer to the Outgoing/Incoming
	   Mailboxes.
	 */
	extmbox_req.mbox_count = adapter->mbox_count;
	extmbox_req.base_mbox_addr = (u32) adapter->mbox_space_handle;
	if (blogic_cmd(adapter, BLOGIC_INIT_EXT_MBOX, &extmbox_req,
				sizeof(extmbox_req), NULL, 0) < 0)
		return blogic_failure(adapter, "MAILBOX INITIALIZATION");
	/*
	   Enable Strict Round Robin Mode if supported by the Host Adapter. In
	   Strict Round Robin Mode, the Host Adapter only looks at the next
	   Outgoing Mailbox for each new command, rather than scanning
	   through all the Outgoing Mailboxes to find any that have new
	   commands in them.  Strict Round Robin Mode is significantly more
	   efficient.
	 */
	if (adapter->strict_rr) {
		rr_req = BLOGIC_STRICT_RR_MODE;
		if (blogic_cmd(adapter, BLOGIC_STRICT_RR, &rr_req,
					sizeof(rr_req), NULL, 0) < 0)
			return blogic_failure(adapter,
					"ENABLE STRICT ROUND ROBIN MODE");
	}

	/*
	   For Host Adapters that support Extended LUN Format CCBs, issue the
	   Set CCB Format command to allow 32 Logical Units per Target Device.
	 */
	if (adapter->ext_lun) {
		setccb_fmt = BLOGIC_EXT_LUN_CCB;
		if (blogic_cmd(adapter, BLOGIC_SETCCB_FMT, &setccb_fmt,
					sizeof(setccb_fmt), NULL, 0) < 0)
			return blogic_failure(adapter, "SET CCB FORMAT");
	}

	/*
	   Announce Successful Initialization.
	 */
done:
	if (!adapter->adapter_initd) {
		blogic_info("*** %s Initialized Successfully ***\n", adapter,
				adapter->full_model);
		blogic_info("\n", adapter);
	} else
		blogic_warn("*** %s Initialized Successfully ***\n", adapter,
				adapter->full_model);
	adapter->adapter_initd = true;

	/*
	   Indicate the Host Adapter Initialization completed successfully.
	 */
	return true;
}


/*
  blogic_inquiry inquires about the Target Devices accessible
  through Host Adapter.
*/

static bool __init blogic_inquiry(struct blogic_adapter *adapter)
{
	u16 installed_devs;
	u8 installed_devs0to7[8];
	struct blogic_setup_info setupinfo;
	u8 sync_period[BLOGIC_MAXDEV];
	unsigned char req_replylen;
	int tgt_id;

	/*
	   Wait a few seconds between the Host Adapter Hard Reset which
	   initiates a SCSI Bus Reset and issuing any SCSI Commands. Some
	   SCSI devices get confused if they receive SCSI Commands too soon
	   after a SCSI Bus Reset.
	 */
	blogic_delay(adapter->bus_settle_time);
	/*
	   FlashPoint Host Adapters do not provide for Target Device Inquiry.
	 */
	if (blogic_flashpoint_type(adapter))
		return true;
	/*
	   Inhibit the Target Device Inquiry if requested.
	 */
	if (adapter->drvr_opts != NULL && adapter->drvr_opts->stop_tgt_inquiry)
		return true;
	/*
	   Issue the Inquire Target Devices command for host adapters with
	   firmware version 4.25 or later, or the Inquire Installed Devices
	   ID 0 to 7 command for older host adapters.  This is necessary to
	   force Synchronous Transfer Negotiation so that the Inquire Setup
	   Information and Inquire Synchronous Period commands will return
	   valid data.  The Inquire Target Devices command is preferable to
	   Inquire Installed Devices ID 0 to 7 since it only probes Logical
	   Unit 0 of each Target Device.
	 */
	if (strcmp(adapter->fw_ver, "4.25") >= 0) {

		/*
		   Issue a Inquire Target Devices command. Inquire Target
		   Devices only tests Logical Unit 0 of each Target Device
		   unlike the Inquire Installed Devices commands which test
		   Logical Units 0 - 7.  Two bytes are returned, where byte
		   0 bit 0 set indicates that Target Device 0 exists, and so on.
		 */

		if (blogic_cmd(adapter, BLOGIC_INQ_DEV, NULL, 0,
					&installed_devs, sizeof(installed_devs))
		    != sizeof(installed_devs))
			return blogic_failure(adapter, "INQUIRE TARGET DEVICES");
		for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
			adapter->tgt_flags[tgt_id].tgt_exists =
				(installed_devs & (1 << tgt_id) ? true : false);
	} else {

		/*
		   Issue an Inquire Installed Devices command. For each
		   Target Device, a byte is returned where bit 0 set
		   indicates that Logical Unit 0 * exists, bit 1 set
		   indicates that Logical Unit 1 exists, and so on.
		 */

		if (blogic_cmd(adapter, BLOGIC_INQ_DEV0TO7, NULL, 0,
				&installed_devs0to7, sizeof(installed_devs0to7))
		    != sizeof(installed_devs0to7))
			return blogic_failure(adapter,
					"INQUIRE INSTALLED DEVICES ID 0 TO 7");
		for (tgt_id = 0; tgt_id < 8; tgt_id++)
			adapter->tgt_flags[tgt_id].tgt_exists =
				installed_devs0to7[tgt_id] != 0;
	}
	/*
	   Issue the Inquire Setup Information command.
	 */
	req_replylen = sizeof(setupinfo);
	if (blogic_cmd(adapter, BLOGIC_INQ_SETUPINFO, &req_replylen,
			sizeof(req_replylen), &setupinfo, sizeof(setupinfo))
	    != sizeof(setupinfo))
		return blogic_failure(adapter, "INQUIRE SETUP INFORMATION");
	for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
		adapter->sync_offset[tgt_id] = (tgt_id < 8 ? setupinfo.sync0to7[tgt_id].offset : setupinfo.sync8to15[tgt_id - 8].offset);
	if (strcmp(adapter->fw_ver, "5.06L") >= 0)
		for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
			adapter->tgt_flags[tgt_id].wide_active = (tgt_id < 8 ? (setupinfo.wide_tx_active0to7 & (1 << tgt_id) ? true : false) : (setupinfo.wide_tx_active8to15 & (1 << (tgt_id - 8)) ? true : false));
	/*
	   Issue the Inquire Synchronous Period command.
	 */
	if (adapter->fw_ver[0] >= '3') {

		/* Issue a Inquire Synchronous Period command. For each
		   Target Device, a byte is returned which represents the
		   Synchronous Transfer Period in units of 10 nanoseconds.
		 */

		req_replylen = sizeof(sync_period);
		if (blogic_cmd(adapter, BLOGIC_INQ_SYNC_PERIOD, &req_replylen,
				sizeof(req_replylen), &sync_period,
				sizeof(sync_period)) != sizeof(sync_period))
			return blogic_failure(adapter,
					"INQUIRE SYNCHRONOUS PERIOD");
		for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
			adapter->sync_period[tgt_id] = sync_period[tgt_id];
	} else
		for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
			if (setupinfo.sync0to7[tgt_id].offset > 0)
				adapter->sync_period[tgt_id] = 20 + 5 * setupinfo.sync0to7[tgt_id].tx_period;
	/*
	   Indicate the Target Device Inquiry completed successfully.
	 */
	return true;
}

/*
  blogic_inithoststruct initializes the fields in the SCSI Host
  structure.  The base, io_port, n_io_ports, irq, and dma_channel fields in the
  SCSI Host structure are intentionally left uninitialized, as this driver
  handles acquisition and release of these resources explicitly, as well as
  ensuring exclusive access to the Host Adapter hardware and data structures
  through explicit acquisition and release of the Host Adapter's Lock.
*/

static void __init blogic_inithoststruct(struct blogic_adapter *adapter,
		struct Scsi_Host *host)
{
	host->max_id = adapter->maxdev;
	host->max_lun = adapter->maxlun;
	host->max_channel = 0;
	host->unique_id = adapter->io_addr;
	host->this_id = adapter->scsi_id;
	host->can_queue = adapter->drvr_qdepth;
	host->sg_tablesize = adapter->drvr_sglimit;
	host->cmd_per_lun = adapter->untag_qdepth;
}

/*
  blogic_slaveconfig will actually set the queue depth on individual
  scsi devices as they are permanently added to the device chain.  We
  shamelessly rip off the SelectQueueDepths code to make this work mostly
  like it used to.  Since we don't get called once at the end of the scan
  but instead get called for each device, we have to do things a bit
  differently.
*/
static int blogic_slaveconfig(struct scsi_device *dev)
{
	struct blogic_adapter *adapter =
		(struct blogic_adapter *) dev->host->hostdata;
	int tgt_id = dev->id;
	int qdepth = adapter->qdepth[tgt_id];

	if (adapter->tgt_flags[tgt_id].tagq_ok &&
			(adapter->tagq_ok & (1 << tgt_id))) {
		if (qdepth == 0)
			qdepth = BLOGIC_MAX_AUTO_TAG_DEPTH;
		adapter->qdepth[tgt_id] = qdepth;
		scsi_change_queue_depth(dev, qdepth);
	} else {
		adapter->tagq_ok &= ~(1 << tgt_id);
		qdepth = adapter->untag_qdepth;
		adapter->qdepth[tgt_id] = qdepth;
		scsi_change_queue_depth(dev, qdepth);
	}
	qdepth = 0;
	for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++)
		if (adapter->tgt_flags[tgt_id].tgt_exists)
			qdepth += adapter->qdepth[tgt_id];
	if (qdepth > adapter->alloc_ccbs)
		blogic_create_addlccbs(adapter, qdepth - adapter->alloc_ccbs,
				false);
	return 0;
}

/*
  blogic_init probes for BusLogic Host Adapters at the standard
  I/O Addresses where they may be located, initializing, registering, and
  reporting the configuration of each BusLogic Host Adapter it finds.  It
  returns the number of BusLogic Host Adapters successfully initialized and
  registered.
*/

static int __init blogic_init(void)
{
	int adapter_count = 0, drvr_optindex = 0, probeindex;
	struct blogic_adapter *adapter;
	int ret = 0;

#ifdef MODULE
	if (BusLogic)
		blogic_setup(BusLogic);
#endif

	if (blogic_probe_options.noprobe)
		return -ENODEV;
	blogic_probeinfo_list =
	    kcalloc(BLOGIC_MAX_ADAPTERS, sizeof(struct blogic_probeinfo),
			    GFP_KERNEL);
	if (blogic_probeinfo_list == NULL) {
		blogic_err("BusLogic: Unable to allocate Probe Info List\n",
				NULL);
		return -ENOMEM;
	}

	adapter = kzalloc(sizeof(struct blogic_adapter), GFP_KERNEL);
	if (adapter == NULL) {
		kfree(blogic_probeinfo_list);
		blogic_err("BusLogic: Unable to allocate Prototype Host Adapter\n", NULL);
		return -ENOMEM;
	}

#ifdef MODULE
	if (BusLogic != NULL)
		blogic_setup(BusLogic);
#endif
	blogic_init_probeinfo_list(adapter);
	for (probeindex = 0; probeindex < blogic_probeinfo_count; probeindex++) {
		struct blogic_probeinfo *probeinfo =
			&blogic_probeinfo_list[probeindex];
		struct blogic_adapter *myadapter = adapter;
		struct Scsi_Host *host;

		if (probeinfo->io_addr == 0)
			continue;
		memset(myadapter, 0, sizeof(struct blogic_adapter));
		myadapter->adapter_type = probeinfo->adapter_type;
		myadapter->adapter_bus_type = probeinfo->adapter_bus_type;
		myadapter->io_addr = probeinfo->io_addr;
		myadapter->pci_addr = probeinfo->pci_addr;
		myadapter->bus = probeinfo->bus;
		myadapter->dev = probeinfo->dev;
		myadapter->pci_device = probeinfo->pci_device;
		myadapter->irq_ch = probeinfo->irq_ch;
		myadapter->addr_count =
			blogic_adapter_addr_count[myadapter->adapter_type];

		/*
		   Make sure region is free prior to probing.
		 */
		if (!request_region(myadapter->io_addr, myadapter->addr_count,
					"BusLogic"))
			continue;
		/*
		   Probe the Host Adapter. If unsuccessful, abort further
		   initialization.
		 */
		if (!blogic_probe(myadapter)) {
			release_region(myadapter->io_addr,
					myadapter->addr_count);
			continue;
		}
		/*
		   Hard Reset the Host Adapter.  If unsuccessful, abort further
		   initialization.
		 */
		if (!blogic_hwreset(myadapter, true)) {
			release_region(myadapter->io_addr,
					myadapter->addr_count);
			continue;
		}
		/*
		   Check the Host Adapter.  If unsuccessful, abort further
		   initialization.
		 */
		if (!blogic_checkadapter(myadapter)) {
			release_region(myadapter->io_addr,
					myadapter->addr_count);
			continue;
		}
		/*
		   Initialize the Driver Options field if provided.
		 */
		if (drvr_optindex < blogic_drvr_options_count)
			myadapter->drvr_opts =
				&blogic_drvr_options[drvr_optindex++];
		/*
		   Announce the Driver Version and Date, Author's Name,
		   Copyright Notice, and Electronic Mail Address.
		 */
		blogic_announce_drvr(myadapter);
		/*
		   Register the SCSI Host structure.
		 */

		host = scsi_host_alloc(&blogic_template,
				sizeof(struct blogic_adapter));
		if (host == NULL) {
			release_region(myadapter->io_addr,
					myadapter->addr_count);
			continue;
		}
		myadapter = (struct blogic_adapter *) host->hostdata;
		memcpy(myadapter, adapter, sizeof(struct blogic_adapter));
		myadapter->scsi_host = host;
		myadapter->host_no = host->host_no;
		/*
		   Add Host Adapter to the end of the list of registered
		   BusLogic Host Adapters.
		 */
		list_add_tail(&myadapter->host_list, &blogic_host_list);

		/*
		   Read the Host Adapter Configuration, Configure the Host
		   Adapter, Acquire the System Resources necessary to use
		   the Host Adapter, then Create the Initial CCBs, Initialize
		   the Host Adapter, and finally perform Target Device
		   Inquiry. From this point onward, any failure will be
		   assumed to be due to a problem with the Host Adapter,
		   rather than due to having mistakenly identified this port
		   as belonging to a BusLogic Host Adapter. The I/O Address
		   range will not be released, thereby preventing it from
		   being incorrectly identified as any other type of Host
		   Adapter.
		 */
		if (blogic_rdconfig(myadapter) &&
		    blogic_reportconfig(myadapter) &&
		    blogic_getres(myadapter) &&
		    blogic_create_initccbs(myadapter) &&
		    blogic_initadapter(myadapter) &&
		    blogic_inquiry(myadapter)) {
			/*
			   Initialization has been completed successfully.
			   Release and re-register usage of the I/O Address
			   range so that the Model Name of the Host Adapter
			   will appear, and initialize the SCSI Host structure.
			 */
			release_region(myadapter->io_addr,
				       myadapter->addr_count);
			if (!request_region(myadapter->io_addr,
					    myadapter->addr_count,
					    myadapter->full_model)) {
				printk(KERN_WARNING
					"BusLogic: Release and re-register of "
					"port 0x%04lx failed \n",
					(unsigned long)myadapter->io_addr);
				blogic_destroy_ccbs(myadapter);
				blogic_relres(myadapter);
				list_del(&myadapter->host_list);
				scsi_host_put(host);
				ret = -ENOMEM;
			} else {
				blogic_inithoststruct(myadapter,
								 host);
				if (scsi_add_host(host, myadapter->pci_device
						? &myadapter->pci_device->dev
						  : NULL)) {
					printk(KERN_WARNING
					       "BusLogic: scsi_add_host()"
					       "failed!\n");
					blogic_destroy_ccbs(myadapter);
					blogic_relres(myadapter);
					list_del(&myadapter->host_list);
					scsi_host_put(host);
					ret = -ENODEV;
				} else {
					scsi_scan_host(host);
					adapter_count++;
				}
			}
		} else {
			/*
			   An error occurred during Host Adapter Configuration
			   Querying, Host Adapter Configuration, Resource
			   Acquisition, CCB Creation, Host Adapter
			   Initialization, or Target Device Inquiry, so
			   remove Host Adapter from the list of registered
			   BusLogic Host Adapters, destroy the CCBs, Release
			   the System Resources, and Unregister the SCSI
			   Host.
			 */
			blogic_destroy_ccbs(myadapter);
			blogic_relres(myadapter);
			list_del(&myadapter->host_list);
			scsi_host_put(host);
			ret = -ENODEV;
		}
	}
	kfree(adapter);
	kfree(blogic_probeinfo_list);
	blogic_probeinfo_list = NULL;
	return ret;
}


/*
  blogic_deladapter releases all resources previously acquired to
  support a specific Host Adapter, including the I/O Address range, and
  unregisters the BusLogic Host Adapter.
*/

static int __exit blogic_deladapter(struct blogic_adapter *adapter)
{
	struct Scsi_Host *host = adapter->scsi_host;

	scsi_remove_host(host);

	/*
	   FlashPoint Host Adapters must first be released by the FlashPoint
	   SCCB Manager.
	 */
	if (blogic_flashpoint_type(adapter))
		FlashPoint_ReleaseHostAdapter(adapter->cardhandle);
	/*
	   Destroy the CCBs and release any system resources acquired to
	   support Host Adapter.
	 */
	blogic_destroy_ccbs(adapter);
	blogic_relres(adapter);
	/*
	   Release usage of the I/O Address range.
	 */
	release_region(adapter->io_addr, adapter->addr_count);
	/*
	   Remove Host Adapter from the list of registered BusLogic
	   Host Adapters.
	 */
	list_del(&adapter->host_list);

	scsi_host_put(host);
	return 0;
}


/*
  blogic_qcompleted_ccb queues CCB for completion processing.
*/

static void blogic_qcompleted_ccb(struct blogic_ccb *ccb)
{
	struct blogic_adapter *adapter = ccb->adapter;

	ccb->status = BLOGIC_CCB_COMPLETE;
	ccb->next = NULL;
	if (adapter->firstccb == NULL) {
		adapter->firstccb = ccb;
		adapter->lastccb = ccb;
	} else {
		adapter->lastccb->next = ccb;
		adapter->lastccb = ccb;
	}
	adapter->active_cmds[ccb->tgt_id]--;
}


/*
  blogic_resultcode computes a SCSI Subsystem Result Code from
  the Host Adapter Status and Target Device Status.
*/

static int blogic_resultcode(struct blogic_adapter *adapter,
		enum blogic_adapter_status adapter_status,
		enum blogic_tgt_status tgt_status)
{
	int hoststatus;

	switch (adapter_status) {
	case BLOGIC_CMD_CMPLT_NORMAL:
	case BLOGIC_LINK_CMD_CMPLT:
	case BLOGIC_LINK_CMD_CMPLT_FLAG:
		hoststatus = DID_OK;
		break;
	case BLOGIC_SELECT_TIMEOUT:
		hoststatus = DID_TIME_OUT;
		break;
	case BLOGIC_INVALID_OUTBOX_CODE:
	case BLOGIC_INVALID_CMD_CODE:
	case BLOGIC_BAD_CMD_PARAM:
		blogic_warn("BusLogic Driver Protocol Error 0x%02X\n",
				adapter, adapter_status);
		fallthrough;
	case BLOGIC_DATA_UNDERRUN:
	case BLOGIC_DATA_OVERRUN:
	case BLOGIC_NOEXPECT_BUSFREE:
	case BLOGIC_LINKCCB_BADLUN:
	case BLOGIC_AUTOREQSENSE_FAIL:
	case BLOGIC_TAGQUEUE_REJECT:
	case BLOGIC_BAD_MSG_RCVD:
	case BLOGIC_HW_FAIL:
	case BLOGIC_BAD_RECONNECT:
	case BLOGIC_ABRT_QUEUE:
	case BLOGIC_ADAPTER_SW_ERROR:
	case BLOGIC_HW_TIMEOUT:
	case BLOGIC_PARITY_ERR:
		hoststatus = DID_ERROR;
		break;
	case BLOGIC_INVALID_BUSPHASE:
	case BLOGIC_NORESPONSE_TO_ATN:
	case BLOGIC_HW_RESET:
	case BLOGIC_RST_FROM_OTHERDEV:
	case BLOGIC_HW_BDR:
		hoststatus = DID_RESET;
		break;
	default:
		blogic_warn("Unknown Host Adapter Status 0x%02X\n", adapter,
				adapter_status);
		hoststatus = DID_ERROR;
		break;
	}
	return (hoststatus << 16) | tgt_status;
}

/*
 * turn the dma address from an inbox into a ccb pointer
 * This is rather inefficient.
 */
static struct blogic_ccb *
blogic_inbox_to_ccb(struct blogic_adapter *adapter, struct blogic_inbox *inbox)
{
	struct blogic_ccb *ccb;

	for (ccb = adapter->all_ccbs; ccb; ccb = ccb->next_all)
		if (inbox->ccb == ccb->dma_handle)
			break;

	return ccb;
}

/*
  blogic_scan_inbox scans the Incoming Mailboxes saving any
  Incoming Mailbox entries for completion processing.
*/
static void blogic_scan_inbox(struct blogic_adapter *adapter)
{
	/*
	   Scan through the Incoming Mailboxes in Strict Round Robin
	   fashion, saving any completed CCBs for further processing. It
	   is essential that for each CCB and SCSI Command issued, command
	   completion processing is performed exactly once.  Therefore,
	   only Incoming Mailboxes with completion code Command Completed
	   Without Error, Command Completed With Error, or Command Aborted
	   At Host Request are saved for completion processing. When an
	   Incoming Mailbox has a completion code of Aborted Command Not
	   Found, the CCB had already completed or been aborted before the
	   current Abort request was processed, and so completion processing
	   has already occurred and no further action should be taken.
	 */
	struct blogic_inbox *next_inbox = adapter->next_inbox;
	enum blogic_cmplt_code comp_code;

	while ((comp_code = next_inbox->comp_code) != BLOGIC_INBOX_FREE) {
		struct blogic_ccb *ccb = blogic_inbox_to_ccb(adapter, next_inbox);
		if (!ccb) {
			/*
			 * This should never happen, unless the CCB list is
			 * corrupted in memory.
			 */
			blogic_warn("Could not find CCB for dma address %x\n", adapter, next_inbox->ccb);
		} else if (comp_code != BLOGIC_CMD_NOTFOUND) {
			if (ccb->status == BLOGIC_CCB_ACTIVE ||
					ccb->status == BLOGIC_CCB_RESET) {
				/*
				   Save the Completion Code for this CCB and
				   queue the CCB for completion processing.
				 */
				ccb->comp_code = comp_code;
				blogic_qcompleted_ccb(ccb);
			} else {
				/*
				   If a CCB ever appears in an Incoming Mailbox
				   and is not marked as status Active or Reset,
				   then there is most likely a bug in
				   the Host Adapter firmware.
				 */
				blogic_warn("Illegal CCB #%ld status %d in Incoming Mailbox\n", adapter, ccb->serial, ccb->status);
			}
		}
		next_inbox->comp_code = BLOGIC_INBOX_FREE;
		if (++next_inbox > adapter->last_inbox)
			next_inbox = adapter->first_inbox;
	}
	adapter->next_inbox = next_inbox;
}


/*
  blogic_process_ccbs iterates over the completed CCBs for Host
  Adapter setting the SCSI Command Result Codes, deallocating the CCBs, and
  calling the SCSI Subsystem Completion Routines.  The Host Adapter's Lock
  should already have been acquired by the caller.
*/

static void blogic_process_ccbs(struct blogic_adapter *adapter)
{
	if (adapter->processing_ccbs)
		return;
	adapter->processing_ccbs = true;
	while (adapter->firstccb != NULL) {
		struct blogic_ccb *ccb = adapter->firstccb;
		struct scsi_cmnd *command = ccb->command;
		adapter->firstccb = ccb->next;
		if (adapter->firstccb == NULL)
			adapter->lastccb = NULL;
		/*
		   Process the Completed CCB.
		 */
		if (ccb->opcode == BLOGIC_BDR) {
			int tgt_id = ccb->tgt_id;

			blogic_warn("Bus Device Reset CCB #%ld to Target %d Completed\n", adapter, ccb->serial, tgt_id);
			blogic_inc_count(&adapter->tgt_stats[tgt_id].bdr_done);
			adapter->tgt_flags[tgt_id].tagq_active = false;
			adapter->cmds_since_rst[tgt_id] = 0;
			adapter->last_resetdone[tgt_id] = jiffies;
			/*
			   Place CCB back on the Host Adapter's free list.
			 */
			blogic_dealloc_ccb(ccb, 1);
#if 0			/* this needs to be redone different for new EH */
			/*
			   Bus Device Reset CCBs have the command field
			   non-NULL only when a Bus Device Reset was requested
			   for a command that did not have a currently active
			   CCB in the Host Adapter (i.e., a Synchronous Bus
			   Device Reset), and hence would not have its
			   Completion Routine called otherwise.
			 */
			while (command != NULL) {
				struct scsi_cmnd *nxt_cmd =
					command->reset_chain;
				command->reset_chain = NULL;
				command->result = DID_RESET << 16;
				scsi_done(command);
				command = nxt_cmd;
			}
#endif
			/*
			   Iterate over the CCBs for this Host Adapter
			   performing completion processing for any CCBs
			   marked as Reset for this Target.
			 */
			for (ccb = adapter->all_ccbs; ccb != NULL;
					ccb = ccb->next_all)
				if (ccb->status == BLOGIC_CCB_RESET &&
						ccb->tgt_id == tgt_id) {
					command = ccb->command;
					blogic_dealloc_ccb(ccb, 1);
					adapter->active_cmds[tgt_id]--;
					command->result = DID_RESET << 16;
					scsi_done(command);
				}
			adapter->bdr_pend[tgt_id] = NULL;
		} else {
			/*
			   Translate the Completion Code, Host Adapter Status,
			   and Target Device Status into a SCSI Subsystem
			   Result Code.
			 */
			switch (ccb->comp_code) {
			case BLOGIC_INBOX_FREE:
			case BLOGIC_CMD_NOTFOUND:
			case BLOGIC_INVALID_CCB:
				blogic_warn("CCB #%ld to Target %d Impossible State\n", adapter, ccb->serial, ccb->tgt_id);
				break;
			case BLOGIC_CMD_COMPLETE_GOOD:
				adapter->tgt_stats[ccb->tgt_id]
				    .cmds_complete++;
				adapter->tgt_flags[ccb->tgt_id]
				    .cmd_good = true;
				command->result = DID_OK << 16;
				break;
			case BLOGIC_CMD_ABORT_BY_HOST:
				blogic_warn("CCB #%ld to Target %d Aborted\n",
					adapter, ccb->serial, ccb->tgt_id);
				blogic_inc_count(&adapter->tgt_stats[ccb->tgt_id].aborts_done);
				command->result = DID_ABORT << 16;
				break;
			case BLOGIC_CMD_COMPLETE_ERROR:
				command->result = blogic_resultcode(adapter,
					ccb->adapter_status, ccb->tgt_status);
				if (ccb->adapter_status != BLOGIC_SELECT_TIMEOUT) {
					adapter->tgt_stats[ccb->tgt_id]
					    .cmds_complete++;
					if (blogic_global_options.trace_err) {
						int i;
						blogic_notice("CCB #%ld Target %d: Result %X Host "
								"Adapter Status %02X Target Status %02X\n", adapter, ccb->serial, ccb->tgt_id, command->result, ccb->adapter_status, ccb->tgt_status);
						blogic_notice("CDB   ", adapter);
						for (i = 0; i < ccb->cdblen; i++)
							blogic_notice(" %02X", adapter, ccb->cdb[i]);
						blogic_notice("\n", adapter);
						blogic_notice("Sense ", adapter);
						for (i = 0; i < ccb->sense_datalen; i++)
							blogic_notice(" %02X", adapter, command->sense_buffer[i]);
						blogic_notice("\n", adapter);
					}
				}
				break;
			}
			/*
			   When an INQUIRY command completes normally, save the
			   CmdQue (Tagged Queuing Supported) and WBus16 (16 Bit
			   Wide Data Transfers Supported) bits.
			 */
			if (ccb->cdb[0] == INQUIRY && ccb->cdb[1] == 0 &&
				ccb->adapter_status == BLOGIC_CMD_CMPLT_NORMAL) {
				struct blogic_tgt_flags *tgt_flags =
					&adapter->tgt_flags[ccb->tgt_id];
				struct scsi_inquiry *inquiry =
					(struct scsi_inquiry *) scsi_sglist(command);
				tgt_flags->tgt_exists = true;
				tgt_flags->tagq_ok = inquiry->CmdQue;
				tgt_flags->wide_ok = inquiry->WBus16;
			}
			/*
			   Place CCB back on the Host Adapter's free list.
			 */
			blogic_dealloc_ccb(ccb, 1);
			/*
			   Call the SCSI Command Completion Routine.
			 */
			scsi_done(command);
		}
	}
	adapter->processing_ccbs = false;
}


/*
  blogic_inthandler handles hardware interrupts from BusLogic Host
  Adapters.
*/

static irqreturn_t blogic_inthandler(int irq_ch, void *devid)
{
	struct blogic_adapter *adapter = (struct blogic_adapter *) devid;
	unsigned long processor_flag;
	/*
	   Acquire exclusive access to Host Adapter.
	 */
	spin_lock_irqsave(adapter->scsi_host->host_lock, processor_flag);
	/*
	   Handle Interrupts appropriately for each Host Adapter type.
	 */
	if (blogic_multimaster_type(adapter)) {
		union blogic_int_reg intreg;
		/*
		   Read the Host Adapter Interrupt Register.
		 */
		intreg.all = blogic_rdint(adapter);
		if (intreg.ir.int_valid) {
			/*
			   Acknowledge the interrupt and reset the Host Adapter
			   Interrupt Register.
			 */
			blogic_intreset(adapter);
			/*
			   Process valid External SCSI Bus Reset and Incoming
			   Mailbox Loaded Interrupts. Command Complete
			   Interrupts are noted, and Outgoing Mailbox Available
			   Interrupts are ignored, as they are never enabled.
			 */
			if (intreg.ir.ext_busreset)
				adapter->adapter_extreset = true;
			else if (intreg.ir.mailin_loaded)
				blogic_scan_inbox(adapter);
			else if (intreg.ir.cmd_complete)
				adapter->adapter_cmd_complete = true;
		}
	} else {
		/*
		   Check if there is a pending interrupt for this Host Adapter.
		 */
		if (FlashPoint_InterruptPending(adapter->cardhandle))
			switch (FlashPoint_HandleInterrupt(adapter->cardhandle)) {
			case FPOINT_NORMAL_INT:
				break;
			case FPOINT_EXT_RESET:
				adapter->adapter_extreset = true;
				break;
			case FPOINT_INTERN_ERR:
				blogic_warn("Internal FlashPoint Error detected - Resetting Host Adapter\n", adapter);
				adapter->adapter_intern_err = true;
				break;
			}
	}
	/*
	   Process any completed CCBs.
	 */
	if (adapter->firstccb != NULL)
		blogic_process_ccbs(adapter);
	/*
	   Reset the Host Adapter if requested.
	 */
	if (adapter->adapter_extreset) {
		blogic_warn("Resetting %s due to External SCSI Bus Reset\n", adapter, adapter->full_model);
		blogic_inc_count(&adapter->ext_resets);
		blogic_resetadapter(adapter, false);
		adapter->adapter_extreset = false;
	} else if (adapter->adapter_intern_err) {
		blogic_warn("Resetting %s due to Host Adapter Internal Error\n", adapter, adapter->full_model);
		blogic_inc_count(&adapter->adapter_intern_errors);
		blogic_resetadapter(adapter, true);
		adapter->adapter_intern_err = false;
	}
	/*
	   Release exclusive access to Host Adapter.
	 */
	spin_unlock_irqrestore(adapter->scsi_host->host_lock, processor_flag);
	return IRQ_HANDLED;
}


/*
  blogic_write_outbox places CCB and Action Code into an Outgoing
  Mailbox for execution by Host Adapter.  The Host Adapter's Lock should
  already have been acquired by the caller.
*/

static bool blogic_write_outbox(struct blogic_adapter *adapter,
		enum blogic_action action, struct blogic_ccb *ccb)
{
	struct blogic_outbox *next_outbox;

	next_outbox = adapter->next_outbox;
	if (next_outbox->action == BLOGIC_OUTBOX_FREE) {
		ccb->status = BLOGIC_CCB_ACTIVE;
		/*
		   The CCB field must be written before the Action Code field
		   since the Host Adapter is operating asynchronously and the
		   locking code does not protect against simultaneous access
		   by the Host Adapter.
		 */
		next_outbox->ccb = ccb->dma_handle;
		next_outbox->action = action;
		blogic_execmbox(adapter);
		if (++next_outbox > adapter->last_outbox)
			next_outbox = adapter->first_outbox;
		adapter->next_outbox = next_outbox;
		if (action == BLOGIC_MBOX_START) {
			adapter->active_cmds[ccb->tgt_id]++;
			if (ccb->opcode != BLOGIC_BDR)
				adapter->tgt_stats[ccb->tgt_id].cmds_tried++;
		}
		return true;
	}
	return false;
}

/* Error Handling (EH) support */

static int blogic_hostreset(struct scsi_cmnd *SCpnt)
{
	struct blogic_adapter *adapter =
		(struct blogic_adapter *) SCpnt->device->host->hostdata;

	unsigned int id = SCpnt->device->id;
	struct blogic_tgt_stats *stats = &adapter->tgt_stats[id];
	int rc;

	spin_lock_irq(SCpnt->device->host->host_lock);

	blogic_inc_count(&stats->adapter_reset_req);

	rc = blogic_resetadapter(adapter, false);
	spin_unlock_irq(SCpnt->device->host->host_lock);
	return rc;
}

/*
  blogic_qcmd creates a CCB for Command and places it into an
  Outgoing Mailbox for execution by the associated Host Adapter.
*/

static int blogic_qcmd_lck(struct scsi_cmnd *command)
{
	void (*comp_cb)(struct scsi_cmnd *) = scsi_done;
	struct blogic_adapter *adapter =
		(struct blogic_adapter *) command->device->host->hostdata;
	struct blogic_tgt_flags *tgt_flags =
		&adapter->tgt_flags[command->device->id];
	struct blogic_tgt_stats *tgt_stats = adapter->tgt_stats;
	unsigned char *cdb = command->cmnd;
	int cdblen = command->cmd_len;
	int tgt_id = command->device->id;
	int lun = command->device->lun;
	int buflen = scsi_bufflen(command);
	int count;
	struct blogic_ccb *ccb;
	dma_addr_t sense_buf;

	/*
	   SCSI REQUEST_SENSE commands will be executed automatically by the
	   Host Adapter for any errors, so they should not be executed
	   explicitly unless the Sense Data is zero indicating that no error
	   occurred.
	 */
	if (cdb[0] == REQUEST_SENSE && command->sense_buffer[0] != 0) {
		command->result = DID_OK << 16;
		comp_cb(command);
		return 0;
	}
	/*
	   Allocate a CCB from the Host Adapter's free list. In the unlikely
	   event that there are none available and memory allocation fails,
	   wait 1 second and try again. If that fails, the Host Adapter is
	   probably hung so signal an error as a Host Adapter Hard Reset
	   should be initiated soon.
	 */
	ccb = blogic_alloc_ccb(adapter);
	if (ccb == NULL) {
		spin_unlock_irq(adapter->scsi_host->host_lock);
		blogic_delay(1);
		spin_lock_irq(adapter->scsi_host->host_lock);
		ccb = blogic_alloc_ccb(adapter);
		if (ccb == NULL) {
			command->result = DID_ERROR << 16;
			comp_cb(command);
			return 0;
		}
	}

	/*
	   Initialize the fields in the BusLogic Command Control Block (CCB).
	 */
	count = scsi_dma_map(command);
	BUG_ON(count < 0);
	if (count) {
		struct scatterlist *sg;
		int i;

		ccb->opcode = BLOGIC_INITIATOR_CCB_SG;
		ccb->datalen = count * sizeof(struct blogic_sg_seg);
		if (blogic_multimaster_type(adapter))
			ccb->data = (unsigned int) ccb->dma_handle +
					((unsigned long) &ccb->sglist -
					(unsigned long) ccb);
		else
			ccb->data = virt_to_32bit_virt(ccb->sglist);

		scsi_for_each_sg(command, sg, count, i) {
			ccb->sglist[i].segbytes = sg_dma_len(sg);
			ccb->sglist[i].segdata = sg_dma_address(sg);
		}
	} else if (!count) {
		ccb->opcode = BLOGIC_INITIATOR_CCB;
		ccb->datalen = buflen;
		ccb->data = 0;
	}

	switch (cdb[0]) {
	case READ_6:
	case READ_10:
		ccb->datadir = BLOGIC_DATAIN_CHECKED;
		tgt_stats[tgt_id].read_cmds++;
		blogic_addcount(&tgt_stats[tgt_id].bytesread, buflen);
		blogic_incszbucket(tgt_stats[tgt_id].read_sz_buckets, buflen);
		break;
	case WRITE_6:
	case WRITE_10:
		ccb->datadir = BLOGIC_DATAOUT_CHECKED;
		tgt_stats[tgt_id].write_cmds++;
		blogic_addcount(&tgt_stats[tgt_id].byteswritten, buflen);
		blogic_incszbucket(tgt_stats[tgt_id].write_sz_buckets, buflen);
		break;
	default:
		ccb->datadir = BLOGIC_UNCHECKED_TX;
		break;
	}
	ccb->cdblen = cdblen;
	ccb->adapter_status = 0;
	ccb->tgt_status = 0;
	ccb->tgt_id = tgt_id;
	ccb->lun = lun;
	ccb->tag_enable = false;
	ccb->legacytag_enable = false;
	/*
	   BusLogic recommends that after a Reset the first couple of
	   commands that are sent to a Target Device be sent in a non
	   Tagged Queue fashion so that the Host Adapter and Target Device
	   can establish Synchronous and Wide Transfer before Queue Tag
	   messages can interfere with the Synchronous and Wide Negotiation
	   messages.  By waiting to enable Tagged Queuing until after the
	   first BLOGIC_MAX_TAG_DEPTH commands have been queued, it is
	   assured that after a Reset any pending commands are requeued
	   before Tagged Queuing is enabled and that the Tagged Queuing
	   message will not occur while the partition table is being printed.
	   In addition, some devices do not properly handle the transition
	   from non-tagged to tagged commands, so it is necessary to wait
	   until there are no pending commands for a target device
	   before queuing tagged commands.
	 */
	if (adapter->cmds_since_rst[tgt_id]++ >= BLOGIC_MAX_TAG_DEPTH &&
			!tgt_flags->tagq_active &&
			adapter->active_cmds[tgt_id] == 0
			&& tgt_flags->tagq_ok &&
			(adapter->tagq_ok & (1 << tgt_id))) {
		tgt_flags->tagq_active = true;
		blogic_notice("Tagged Queuing now active for Target %d\n",
					adapter, tgt_id);
	}
	if (tgt_flags->tagq_active) {
		enum blogic_queuetag queuetag = BLOGIC_SIMPLETAG;
		/*
		   When using Tagged Queuing with Simple Queue Tags, it
		   appears that disk drive controllers do not guarantee that
		   a queued command will not remain in a disconnected state
		   indefinitely if commands that read or write nearer the
		   head position continue to arrive without interruption.
		   Therefore, for each Target Device this driver keeps track
		   of the last time either the queue was empty or an Ordered
		   Queue Tag was issued. If more than 4 seconds (one fifth
		   of the 20 second disk timeout) have elapsed since this
		   last sequence point, this command will be issued with an
		   Ordered Queue Tag rather than a Simple Queue Tag, which
		   forces the Target Device to complete all previously
		   queued commands before this command may be executed.
		 */
		if (adapter->active_cmds[tgt_id] == 0)
			adapter->last_seqpoint[tgt_id] = jiffies;
		else if (time_after(jiffies,
				adapter->last_seqpoint[tgt_id] + 4 * HZ)) {
			adapter->last_seqpoint[tgt_id] = jiffies;
			queuetag = BLOGIC_ORDEREDTAG;
		}
		if (adapter->ext_lun) {
			ccb->tag_enable = true;
			ccb->queuetag = queuetag;
		} else {
			ccb->legacytag_enable = true;
			ccb->legacy_tag = queuetag;
		}
	}
	memcpy(ccb->cdb, cdb, cdblen);
	ccb->sense_datalen = SCSI_SENSE_BUFFERSIZE;
	ccb->command = command;
	sense_buf = dma_map_single(&adapter->pci_device->dev,
				command->sense_buffer, ccb->sense_datalen,
				DMA_FROM_DEVICE);
	if (dma_mapping_error(&adapter->pci_device->dev, sense_buf)) {
		blogic_err("DMA mapping for sense data buffer failed\n",
				adapter);
		blogic_dealloc_ccb(ccb, 0);
		return SCSI_MLQUEUE_HOST_BUSY;
	}
	ccb->sensedata = sense_buf;
	if (blogic_multimaster_type(adapter)) {
		/*
		   Place the CCB in an Outgoing Mailbox. The higher levels
		   of the SCSI Subsystem should not attempt to queue more
		   commands than can be placed in Outgoing Mailboxes, so
		   there should always be one free.  In the unlikely event
		   that there are none available, wait 1 second and try
		   again. If that fails, the Host Adapter is probably hung
		   so signal an error as a Host Adapter Hard Reset should
		   be initiated soon.
		 */
		if (!blogic_write_outbox(adapter, BLOGIC_MBOX_START, ccb)) {
			spin_unlock_irq(adapter->scsi_host->host_lock);
			blogic_warn("Unable to write Outgoing Mailbox - Pausing for 1 second\n", adapter);
			blogic_delay(1);
			spin_lock_irq(adapter->scsi_host->host_lock);
			if (!blogic_write_outbox(adapter, BLOGIC_MBOX_START,
						ccb)) {
				blogic_warn("Still unable to write Outgoing Mailbox - Host Adapter Dead?\n", adapter);
				blogic_dealloc_ccb(ccb, 1);
				command->result = DID_ERROR << 16;
				scsi_done(command);
			}
		}
	} else {
		/*
		   Call the FlashPoint SCCB Manager to start execution of
		   the CCB.
		 */
		ccb->status = BLOGIC_CCB_ACTIVE;
		adapter->active_cmds[tgt_id]++;
		tgt_stats[tgt_id].cmds_tried++;
		FlashPoint_StartCCB(adapter->cardhandle, ccb);
		/*
		   The Command may have already completed and
		   blogic_qcompleted_ccb been called, or it may still be
		   pending.
		 */
		if (ccb->status == BLOGIC_CCB_COMPLETE)
			blogic_process_ccbs(adapter);
	}
	return 0;
}

static DEF_SCSI_QCMD(blogic_qcmd)

#if 0
/*
  blogic_abort aborts Command if possible.
*/

static int blogic_abort(struct scsi_cmnd *command)
{
	struct blogic_adapter *adapter =
		(struct blogic_adapter *) command->device->host->hostdata;

	int tgt_id = command->device->id;
	struct blogic_ccb *ccb;
	blogic_inc_count(&adapter->tgt_stats[tgt_id].aborts_request);

	/*
	   Attempt to find an Active CCB for this Command. If no Active
	   CCB for this Command is found, then no Abort is necessary.
	 */
	for (ccb = adapter->all_ccbs; ccb != NULL; ccb = ccb->next_all)
		if (ccb->command == command)
			break;
	if (ccb == NULL) {
		blogic_warn("Unable to Abort Command to Target %d - No CCB Found\n", adapter, tgt_id);
		return SUCCESS;
	} else if (ccb->status == BLOGIC_CCB_COMPLETE) {
		blogic_warn("Unable to Abort Command to Target %d - CCB Completed\n", adapter, tgt_id);
		return SUCCESS;
	} else if (ccb->status == BLOGIC_CCB_RESET) {
		blogic_warn("Unable to Abort Command to Target %d - CCB Reset\n", adapter, tgt_id);
		return SUCCESS;
	}
	if (blogic_multimaster_type(adapter)) {
		/*
		   Attempt to Abort this CCB.  MultiMaster Firmware versions
		   prior to 5.xx do not generate Abort Tag messages, but only
		   generate the non-tagged Abort message.  Since non-tagged
		   commands are not sent by the Host Adapter until the queue
		   of outstanding tagged commands has completed, and the
		   Abort message is treated as a non-tagged command, it is
		   effectively impossible to abort commands when Tagged
		   Queuing is active. Firmware version 5.xx does generate
		   Abort Tag messages, so it is possible to abort commands
		   when Tagged Queuing is active.
		 */
		if (adapter->tgt_flags[tgt_id].tagq_active &&
				adapter->fw_ver[0] < '5') {
			blogic_warn("Unable to Abort CCB #%ld to Target %d - Abort Tag Not Supported\n", adapter, ccb->serial, tgt_id);
			return FAILURE;
		} else if (blogic_write_outbox(adapter, BLOGIC_MBOX_ABORT,
					ccb)) {
			blogic_warn("Aborting CCB #%ld to Target %d\n",
					adapter, ccb->serial, tgt_id);
			blogic_inc_count(&adapter->tgt_stats[tgt_id].aborts_tried);
			return SUCCESS;
		} else {
			blogic_warn("Unable to Abort CCB #%ld to Target %d - No Outgoing Mailboxes\n", adapter, ccb->serial, tgt_id);
			return FAILURE;
		}
	} else {
		/*
		   Call the FlashPoint SCCB Manager to abort execution of
		   the CCB.
		 */
		blogic_warn("Aborting CCB #%ld to Target %d\n", adapter,
				ccb->serial, tgt_id);
		blogic_inc_count(&adapter->tgt_stats[tgt_id].aborts_tried);
		FlashPoint_AbortCCB(adapter->cardhandle, ccb);
		/*
		   The Abort may have already been completed and
		   blogic_qcompleted_ccb been called, or it
		   may still be pending.
		 */
		if (ccb->status == BLOGIC_CCB_COMPLETE)
			blogic_process_ccbs(adapter);
		return SUCCESS;
	}
	return SUCCESS;
}

#endif
/*
  blogic_resetadapter resets Host Adapter if possible, marking all
  currently executing SCSI Commands as having been Reset.
*/

static int blogic_resetadapter(struct blogic_adapter *adapter, bool hard_reset)
{
	struct blogic_ccb *ccb;
	int tgt_id;

	/*
	 * Attempt to Reset and Reinitialize the Host Adapter.
	 */

	if (!(blogic_hwreset(adapter, hard_reset) &&
				blogic_initadapter(adapter))) {
		blogic_err("Resetting %s Failed\n", adapter,
						adapter->full_model);
		return FAILURE;
	}

	/*
	 * Deallocate all currently executing CCBs.
	 */

	for (ccb = adapter->all_ccbs; ccb != NULL; ccb = ccb->next_all)
		if (ccb->status == BLOGIC_CCB_ACTIVE)
			blogic_dealloc_ccb(ccb, 1);
	/*
	 * Wait a few seconds between the Host Adapter Hard Reset which
	 * initiates a SCSI Bus Reset and issuing any SCSI Commands.  Some
	 * SCSI devices get confused if they receive SCSI Commands too soon
	 * after a SCSI Bus Reset.
	 */

	if (hard_reset) {
		spin_unlock_irq(adapter->scsi_host->host_lock);
		blogic_delay(adapter->bus_settle_time);
		spin_lock_irq(adapter->scsi_host->host_lock);
	}

	for (tgt_id = 0; tgt_id < adapter->maxdev; tgt_id++) {
		adapter->last_resettried[tgt_id] = jiffies;
		adapter->last_resetdone[tgt_id] = jiffies;
	}
	return SUCCESS;
}

/*
  blogic_diskparam returns the Heads/Sectors/Cylinders BIOS Disk
  Parameters for Disk.  The default disk geometry is 64 heads, 32 sectors, and
  the appropriate number of cylinders so as not to exceed drive capacity.  In
  order for disks equal to or larger than 1 GB to be addressable by the BIOS
  without exceeding the BIOS limitation of 1024 cylinders, Extended Translation
  may be enabled in AutoSCSI on FlashPoint Host Adapters and on "W" and "C"
  series MultiMaster Host Adapters, or by a dip switch setting on "S" and "A"
  series MultiMaster Host Adapters.  With Extended Translation enabled, drives
  between 1 GB inclusive and 2 GB exclusive are given a disk geometry of 128
  heads and 32 sectors, and drives above 2 GB inclusive are given a disk
  geometry of 255 heads and 63 sectors.  However, if the BIOS detects that the
  Extended Translation setting does not match the geometry in the partition
  table, then the translation inferred from the partition table will be used by
  the BIOS, and a warning may be displayed.
*/

static int blogic_diskparam(struct scsi_device *sdev, struct block_device *dev,
		sector_t capacity, int *params)
{
	struct blogic_adapter *adapter =
				(struct blogic_adapter *) sdev->host->hostdata;
	struct bios_diskparam *diskparam = (struct bios_diskparam *) params;
	unsigned char *buf;

	if (adapter->ext_trans_enable && capacity >= 2 * 1024 * 1024 /* 1 GB in 512 byte sectors */) {
		if (capacity >= 4 * 1024 * 1024 /* 2 GB in 512 byte sectors */) {
			diskparam->heads = 255;
			diskparam->sectors = 63;
		} else {
			diskparam->heads = 128;
			diskparam->sectors = 32;
		}
	} else {
		diskparam->heads = 64;
		diskparam->sectors = 32;
	}
	diskparam->cylinders = (unsigned long) capacity / (diskparam->heads * diskparam->sectors);
	buf = scsi_bios_ptable(dev);
	if (buf == NULL)
		return 0;
	/*
	   If the boot sector partition table flag is valid, search for
	   a partition table entry whose end_head matches one of the
	   standard BusLogic geometry translations (64/32, 128/32, or 255/63).
	 */
	if (*(unsigned short *) (buf + 64) == MSDOS_LABEL_MAGIC) {
		struct msdos_partition *part1_entry =
				(struct msdos_partition *)buf;
		struct msdos_partition *part_entry = part1_entry;
		int saved_cyl = diskparam->cylinders, part_no;
		unsigned char part_end_head = 0, part_end_sector = 0;

		for (part_no = 0; part_no < 4; part_no++) {
			part_end_head = part_entry->end_head;
			part_end_sector = part_entry->end_sector & 0x3F;
			if (part_end_head == 64 - 1) {
				diskparam->heads = 64;
				diskparam->sectors = 32;
				break;
			} else if (part_end_head == 128 - 1) {
				diskparam->heads = 128;
				diskparam->sectors = 32;
				break;
			} else if (part_end_head == 255 - 1) {
				diskparam->heads = 255;
				diskparam->sectors = 63;
				break;
			}
			part_entry++;
		}
		if (part_no == 4) {
			part_end_head = part1_entry->end_head;
			part_end_sector = part1_entry->end_sector & 0x3F;
		}
		diskparam->cylinders = (unsigned long) capacity / (diskparam->heads * diskparam->sectors);
		if (part_no < 4 && part_end_sector == diskparam->sectors) {
			if (diskparam->cylinders != saved_cyl)
				blogic_warn("Adopting Geometry %d/%d from Partition Table\n", adapter, diskparam->heads, diskparam->sectors);
		} else if (part_end_head > 0 || part_end_sector > 0) {
			blogic_warn("Warning: Partition Table appears to have Geometry %d/%d which is\n", adapter, part_end_head + 1, part_end_sector);
			blogic_warn("not compatible with current BusLogic Host Adapter Geometry %d/%d\n", adapter, diskparam->heads, diskparam->sectors);
		}
	}
	kfree(buf);
	return 0;
}


/*
  BugLogic_ProcDirectoryInfo implements /proc/scsi/BusLogic/<N>.
*/

static int blogic_write_info(struct Scsi_Host *shost, char *procbuf,
				int bytes_avail)
{
	struct blogic_adapter *adapter =
				(struct blogic_adapter *) shost->hostdata;
	struct blogic_tgt_stats *tgt_stats;

	tgt_stats = adapter->tgt_stats;
	adapter->ext_resets = 0;
	adapter->adapter_intern_errors = 0;
	memset(tgt_stats, 0, BLOGIC_MAXDEV * sizeof(struct blogic_tgt_stats));
	return 0;
}

static int blogic_show_info(struct seq_file *m, struct Scsi_Host *shost)
{
	struct blogic_adapter *adapter = (struct blogic_adapter *) shost->hostdata;
	struct blogic_tgt_stats *tgt_stats;
	int tgt;

	tgt_stats = adapter->tgt_stats;
	seq_write(m, adapter->msgbuf, adapter->msgbuflen);
	seq_printf(m, "\n\
Current Driver Queue Depth:	%d\n\
Currently Allocated CCBs:	%d\n", adapter->drvr_qdepth, adapter->alloc_ccbs);
	seq_puts(m, "\n\n\
			   DATA TRANSFER STATISTICS\n\
\n\
Target	Tagged Queuing	Queue Depth  Active  Attempted	Completed\n\
======	==============	===========  ======  =========	=========\n");
	for (tgt = 0; tgt < adapter->maxdev; tgt++) {
		struct blogic_tgt_flags *tgt_flags = &adapter->tgt_flags[tgt];
		if (!tgt_flags->tgt_exists)
			continue;
		seq_printf(m, "  %2d	%s", tgt, (tgt_flags->tagq_ok ? (tgt_flags->tagq_active ? "    Active" : (adapter->tagq_ok & (1 << tgt)
																				    ? "  Permitted" : "   Disabled"))
									  : "Not Supported"));
		seq_printf(m,
				  "	    %3d       %3u    %9u	%9u\n", adapter->qdepth[tgt], adapter->active_cmds[tgt], tgt_stats[tgt].cmds_tried, tgt_stats[tgt].cmds_complete);
	}
	seq_puts(m, "\n\
Target  Read Commands  Write Commands   Total Bytes Read    Total Bytes Written\n\
======  =============  ==============  ===================  ===================\n");
	for (tgt = 0; tgt < adapter->maxdev; tgt++) {
		struct blogic_tgt_flags *tgt_flags = &adapter->tgt_flags[tgt];
		if (!tgt_flags->tgt_exists)
			continue;
		seq_printf(m, "  %2d	  %9u	 %9u", tgt, tgt_stats[tgt].read_cmds, tgt_stats[tgt].write_cmds);
		if (tgt_stats[tgt].bytesread.billions > 0)
			seq_printf(m, "     %9u%09u", tgt_stats[tgt].bytesread.billions, tgt_stats[tgt].bytesread.units);
		else
			seq_printf(m, "		%9u", tgt_stats[tgt].bytesread.units);
		if (tgt_stats[tgt].byteswritten.billions > 0)
			seq_printf(m, "   %9u%09u\n", tgt_stats[tgt].byteswritten.billions, tgt_stats[tgt].byteswritten.units);
		else
			seq_printf(m, "	     %9u\n", tgt_stats[tgt].byteswritten.units);
	}
	seq_puts(m, "\n\
Target  Command    0-1KB      1-2KB      2-4KB      4-8KB     8-16KB\n\
======  =======  =========  =========  =========  =========  =========\n");
	for (tgt = 0; tgt < adapter->maxdev; tgt++) {
		struct blogic_tgt_flags *tgt_flags = &adapter->tgt_flags[tgt];
		if (!tgt_flags->tgt_exists)
			continue;
		seq_printf(m,
			    "  %2d	 Read	 %9u  %9u  %9u  %9u  %9u\n", tgt,
			    tgt_stats[tgt].read_sz_buckets[0],
			    tgt_stats[tgt].read_sz_buckets[1], tgt_stats[tgt].read_sz_buckets[2], tgt_stats[tgt].read_sz_buckets[3], tgt_stats[tgt].read_sz_buckets[4]);
		seq_printf(m,
			    "  %2d	 Write	 %9u  %9u  %9u  %9u  %9u\n", tgt,
			    tgt_stats[tgt].write_sz_buckets[0],
			    tgt_stats[tgt].write_sz_buckets[1], tgt_stats[tgt].write_sz_buckets[2], tgt_stats[tgt].write_sz_buckets[3], tgt_stats[tgt].write_sz_buckets[4]);
	}
	seq_puts(m, "\n\
Target  Command   16-32KB    32-64KB   64-128KB   128-256KB   256KB+\n\
======  =======  =========  =========  =========  =========  =========\n");
	for (tgt = 0; tgt < adapter->maxdev; tgt++) {
		struct blogic_tgt_flags *tgt_flags = &adapter->tgt_flags[tgt];
		if (!tgt_flags->tgt_exists)
			continue;
		seq_printf(m,
			    "  %2d	 Read	 %9u  %9u  %9u  %9u  %9u\n", tgt,
			    tgt_stats[tgt].read_sz_buckets[5],
			    tgt_stats[tgt].read_sz_buckets[6], tgt_stats[tgt].read_sz_buckets[7], tgt_stats[tgt].read_sz_buckets[8], tgt_stats[tgt].read_sz_buckets[9]);
		seq_printf(m,
			    "  %2d	 Write	 %9u  %9u  %9u  %9u  %9u\n", tgt,
			    tgt_stats[tgt].write_sz_buckets[5],
			    tgt_stats[tgt].write_sz_buckets[6], tgt_stats[tgt].write_sz_buckets[7], tgt_stats[tgt].write_sz_buckets[8], tgt_stats[tgt].write_sz_buckets[9]);
	}
	seq_puts(m, "\n\n\
			   ERROR RECOVERY STATISTICS\n\
\n\
	  Command Aborts      Bus Device Resets	  Host Adapter Resets\n\
Target	Requested Completed  Requested Completed  Requested Completed\n\
  ID	\\\\\\\\ Attempted ////  \\\\\\\\ Attempted ////  \\\\\\\\ Attempted ////\n\
======	 ===== ===== =====    ===== ===== =====	   ===== ===== =====\n");
	for (tgt = 0; tgt < adapter->maxdev; tgt++) {
		struct blogic_tgt_flags *tgt_flags = &adapter->tgt_flags[tgt];
		if (!tgt_flags->tgt_exists)
			continue;
		seq_printf(m, "  %2d	 %5d %5d %5d    %5d %5d %5d	   %5d %5d %5d\n",
			   tgt, tgt_stats[tgt].aborts_request,
			   tgt_stats[tgt].aborts_tried,
			   tgt_stats[tgt].aborts_done,
			   tgt_stats[tgt].bdr_request,
			   tgt_stats[tgt].bdr_tried,
			   tgt_stats[tgt].bdr_done,
			   tgt_stats[tgt].adapter_reset_req,
			   tgt_stats[tgt].adapter_reset_attempt,
			   tgt_stats[tgt].adapter_reset_done);
	}
	seq_printf(m, "\nExternal Host Adapter Resets: %d\n", adapter->ext_resets);
	seq_printf(m, "Host Adapter Internal Errors: %d\n", adapter->adapter_intern_errors);
	return 0;
}


/*
  blogic_msg prints Driver Messages.
*/
__printf(2, 4)
static void blogic_msg(enum blogic_msglevel msglevel, char *fmt,
			struct blogic_adapter *adapter, ...)
{
	static char buf[BLOGIC_LINEBUF_SIZE];
	static bool begin = true;
	va_list args;
	int len = 0;

	va_start(args, adapter);
	len = vscnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (msglevel == BLOGIC_ANNOUNCE_LEVEL) {
		static int msglines = 0;
		strcpy(&adapter->msgbuf[adapter->msgbuflen], buf);
		adapter->msgbuflen += len;
		if (++msglines <= 2)
			printk("%sscsi: %s", blogic_msglevelmap[msglevel], buf);
	} else if (msglevel == BLOGIC_INFO_LEVEL) {
		strcpy(&adapter->msgbuf[adapter->msgbuflen], buf);
		adapter->msgbuflen += len;
		if (begin) {
			if (buf[0] != '\n' || len > 1)
				printk("%sscsi%d: %s", blogic_msglevelmap[msglevel], adapter->host_no, buf);
		} else
			pr_cont("%s", buf);
	} else {
		if (begin) {
			if (adapter != NULL && adapter->adapter_initd)
				printk("%sscsi%d: %s", blogic_msglevelmap[msglevel], adapter->host_no, buf);
			else
				printk("%s%s", blogic_msglevelmap[msglevel], buf);
		} else
			pr_cont("%s", buf);
	}
	begin = (buf[len - 1] == '\n');
}


/*
  blogic_parse parses an individual option keyword.  It returns true
  and updates the pointer if the keyword is recognized and false otherwise.
*/

static bool __init blogic_parse(char **str, char *keyword)
{
	char *pointer = *str;
	while (*keyword != '\0') {
		char strch = *pointer++;
		char keywordch = *keyword++;
		if (strch >= 'A' && strch <= 'Z')
			strch += 'a' - 'Z';
		if (keywordch >= 'A' && keywordch <= 'Z')
			keywordch += 'a' - 'Z';
		if (strch != keywordch)
			return false;
	}
	*str = pointer;
	return true;
}


/*
  blogic_parseopts handles processing of BusLogic Driver Options
  specifications.

  BusLogic Driver Options may be specified either via the Linux Kernel Command
  Line or via the Loadable Kernel Module Installation Facility.  Driver Options
  for multiple host adapters may be specified either by separating the option
  strings by a semicolon, or by specifying multiple "BusLogic=" strings on the
  command line.  Individual option specifications for a single host adapter are
  separated by commas.  The Probing and Debugging Options apply to all host
  adapters whereas the remaining options apply individually only to the
  selected host adapter.

  The BusLogic Driver Probing Options are described in
  <file:Documentation/scsi/BusLogic.rst>.
*/

static int __init blogic_parseopts(char *options)
{
	while (true) {
		struct blogic_drvr_options *drvr_opts =
			&blogic_drvr_options[blogic_drvr_options_count++];
		int tgt_id;

		memset(drvr_opts, 0, sizeof(struct blogic_drvr_options));
		while (*options != '\0' && *options != ';') {
			if (blogic_parse(&options, "NoProbePCI"))
				blogic_probe_options.noprobe_pci = true;
			else if (blogic_parse(&options, "NoProbe"))
				blogic_probe_options.noprobe = true;
			else if (blogic_parse(&options, "NoSortPCI"))
				blogic_probe_options.nosort_pci = true;
			else if (blogic_parse(&options, "MultiMasterFirst"))
				blogic_probe_options.multimaster_first = true;
			else if (blogic_parse(&options, "FlashPointFirst"))
				blogic_probe_options.flashpoint_first = true;
			/* Tagged Queuing Options. */
			else if (blogic_parse(&options, "QueueDepth:[") ||
					blogic_parse(&options, "QD:[")) {
				for (tgt_id = 0; tgt_id < BLOGIC_MAXDEV; tgt_id++) {
					unsigned short qdepth = simple_strtoul(options, &options, 0);
					if (qdepth > BLOGIC_MAX_TAG_DEPTH) {
						blogic_err("BusLogic: Invalid Driver Options (invalid Queue Depth %d)\n", NULL, qdepth);
						return 0;
					}
					drvr_opts->qdepth[tgt_id] = qdepth;
					if (*options == ',')
						options++;
					else if (*options == ']')
						break;
					else {
						blogic_err("BusLogic: Invalid Driver Options (',' or ']' expected at '%s')\n", NULL, options);
						return 0;
					}
				}
				if (*options != ']') {
					blogic_err("BusLogic: Invalid Driver Options (']' expected at '%s')\n", NULL, options);
					return 0;
				} else
					options++;
			} else if (blogic_parse(&options, "QueueDepth:") || blogic_parse(&options, "QD:")) {
				unsigned short qdepth = simple_strtoul(options, &options, 0);
				if (qdepth == 0 ||
						qdepth > BLOGIC_MAX_TAG_DEPTH) {
					blogic_err("BusLogic: Invalid Driver Options (invalid Queue Depth %d)\n", NULL, qdepth);
					return 0;
				}
				drvr_opts->common_qdepth = qdepth;
				for (tgt_id = 0; tgt_id < BLOGIC_MAXDEV; tgt_id++)
					drvr_opts->qdepth[tgt_id] = qdepth;
			} else if (blogic_parse(&options, "TaggedQueuing:") ||
					blogic_parse(&options, "TQ:")) {
				if (blogic_parse(&options, "Default")) {
					drvr_opts->tagq_ok = 0x0000;
					drvr_opts->tagq_ok_mask = 0x0000;
				} else if (blogic_parse(&options, "Enable")) {
					drvr_opts->tagq_ok = 0xFFFF;
					drvr_opts->tagq_ok_mask = 0xFFFF;
				} else if (blogic_parse(&options, "Disable")) {
					drvr_opts->tagq_ok = 0x0000;
					drvr_opts->tagq_ok_mask = 0xFFFF;
				} else {
					unsigned short tgt_bit;
					for (tgt_id = 0, tgt_bit = 1;
						tgt_id < BLOGIC_MAXDEV;
						tgt_id++, tgt_bit <<= 1)
						switch (*options++) {
						case 'Y':
							drvr_opts->tagq_ok |= tgt_bit;
							drvr_opts->tagq_ok_mask |= tgt_bit;
							break;
						case 'N':
							drvr_opts->tagq_ok &= ~tgt_bit;
							drvr_opts->tagq_ok_mask |= tgt_bit;
							break;
						case 'X':
							break;
						default:
							options--;
							tgt_id = BLOGIC_MAXDEV;
							break;
						}
				}
			}
			/* Miscellaneous Options. */
			else if (blogic_parse(&options, "BusSettleTime:") ||
					blogic_parse(&options, "BST:")) {
				unsigned short bus_settle_time =
					simple_strtoul(options, &options, 0);
				if (bus_settle_time > 5 * 60) {
					blogic_err("BusLogic: Invalid Driver Options (invalid Bus Settle Time %d)\n", NULL, bus_settle_time);
					return 0;
				}
				drvr_opts->bus_settle_time = bus_settle_time;
			} else if (blogic_parse(&options,
						"InhibitTargetInquiry"))
				drvr_opts->stop_tgt_inquiry = true;
			/* Debugging Options. */
			else if (blogic_parse(&options, "TraceProbe"))
				blogic_global_options.trace_probe = true;
			else if (blogic_parse(&options, "TraceHardwareReset"))
				blogic_global_options.trace_hw_reset = true;
			else if (blogic_parse(&options, "TraceConfiguration"))
				blogic_global_options.trace_config = true;
			else if (blogic_parse(&options, "TraceErrors"))
				blogic_global_options.trace_err = true;
			else if (blogic_parse(&options, "Debug")) {
				blogic_global_options.trace_probe = true;
				blogic_global_options.trace_hw_reset = true;
				blogic_global_options.trace_config = true;
				blogic_global_options.trace_err = true;
			}
			if (*options == ',')
				options++;
			else if (*options != ';' && *options != '\0') {
				blogic_err("BusLogic: Unexpected Driver Option '%s' ignored\n", NULL, options);
				*options = '\0';
			}
		}
		if (!(blogic_drvr_options_count == 0 ||
			blogic_probeinfo_count == 0 ||
			blogic_drvr_options_count == blogic_probeinfo_count)) {
			blogic_err("BusLogic: Invalid Driver Options (all or no I/O Addresses must be specified)\n", NULL);
			return 0;
		}
		/*
		   Tagged Queuing is disabled when the Queue Depth is 1 since queuing
		   multiple commands is not possible.
		 */
		for (tgt_id = 0; tgt_id < BLOGIC_MAXDEV; tgt_id++)
			if (drvr_opts->qdepth[tgt_id] == 1) {
				unsigned short tgt_bit = 1 << tgt_id;
				drvr_opts->tagq_ok &= ~tgt_bit;
				drvr_opts->tagq_ok_mask |= tgt_bit;
			}
		if (*options == ';')
			options++;
		if (*options == '\0')
			return 0;
	}
	return 1;
}

/*
  Get it all started
*/

static struct scsi_host_template blogic_template = {
	.module = THIS_MODULE,
	.proc_name = "BusLogic",
	.write_info = blogic_write_info,
	.show_info = blogic_show_info,
	.name = "BusLogic",
	.info = blogic_drvr_info,
	.queuecommand = blogic_qcmd,
	.slave_configure = blogic_slaveconfig,
	.bios_param = blogic_diskparam,
	.eh_host_reset_handler = blogic_hostreset,
#if 0
	.eh_abort_handler = blogic_abort,
#endif
	.max_sectors = 128,
};

/*
  blogic_setup handles processing of Kernel Command Line Arguments.
*/

static int __init blogic_setup(char *str)
{
	int ints[3];

	(void) get_options(str, ARRAY_SIZE(ints), ints);

	if (ints[0] != 0) {
		blogic_err("BusLogic: Obsolete Command Line Entry Format Ignored\n", NULL);
		return 0;
	}
	if (str == NULL || *str == '\0')
		return 0;
	return blogic_parseopts(str);
}

/*
 * Exit function.  Deletes all hosts associated with this driver.
 */

static void __exit blogic_exit(void)
{
	struct blogic_adapter *ha, *next;

	list_for_each_entry_safe(ha, next, &blogic_host_list, host_list)
		blogic_deladapter(ha);
}

__setup("BusLogic=", blogic_setup);

#ifdef MODULE
/*static struct pci_device_id blogic_pci_tbl[] = {
	{ PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER_NC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_FLASHPOINT,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ }
};*/
static const struct pci_device_id blogic_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER)},
	{PCI_DEVICE(PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER_NC)},
	{PCI_DEVICE(PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_FLASHPOINT)},
	{0, },
};
#endif
MODULE_DEVICE_TABLE(pci, blogic_pci_tbl);

module_init(blogic_init);
module_exit(blogic_exit);
