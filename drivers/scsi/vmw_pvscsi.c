/*
 * Linux driver for VMware's para-virtualized SCSI HBA.
 *
 * Copyright (C) 2008-2014, VMware, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained by: Jim Gill <jgill@vmware.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/pci.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>

#include "vmw_pvscsi.h"

#define PVSCSI_LINUX_DRIVER_DESC "VMware PVSCSI driver"

MODULE_DESCRIPTION(PVSCSI_LINUX_DRIVER_DESC);
MODULE_AUTHOR("VMware, Inc.");
MODULE_LICENSE("GPL");
MODULE_VERSION(PVSCSI_DRIVER_VERSION_STRING);

#define PVSCSI_DEFAULT_NUM_PAGES_PER_RING	8
#define PVSCSI_DEFAULT_NUM_PAGES_MSG_RING	1
#define PVSCSI_DEFAULT_QUEUE_DEPTH		254
#define SGL_SIZE				PAGE_SIZE

struct pvscsi_sg_list {
	struct PVSCSISGElement sge[PVSCSI_MAX_NUM_SG_ENTRIES_PER_SEGMENT];
};

struct pvscsi_ctx {
	/*
	 * The index of the context in cmd_map serves as the context ID for a
	 * 1-to-1 mapping completions back to requests.
	 */
	struct scsi_cmnd	*cmd;
	struct pvscsi_sg_list	*sgl;
	struct list_head	list;
	dma_addr_t		dataPA;
	dma_addr_t		sensePA;
	dma_addr_t		sglPA;
	struct completion	*abort_cmp;
};

struct pvscsi_adapter {
	char				*mmioBase;
	u8				rev;
	bool				use_msg;
	bool				use_req_threshold;

	spinlock_t			hw_lock;

	struct workqueue_struct		*workqueue;
	struct work_struct		work;

	struct PVSCSIRingReqDesc	*req_ring;
	unsigned			req_pages;
	unsigned			req_depth;
	dma_addr_t			reqRingPA;

	struct PVSCSIRingCmpDesc	*cmp_ring;
	unsigned			cmp_pages;
	dma_addr_t			cmpRingPA;

	struct PVSCSIRingMsgDesc	*msg_ring;
	unsigned			msg_pages;
	dma_addr_t			msgRingPA;

	struct PVSCSIRingsState		*rings_state;
	dma_addr_t			ringStatePA;

	struct pci_dev			*dev;
	struct Scsi_Host		*host;

	struct list_head		cmd_pool;
	struct pvscsi_ctx		*cmd_map;
};


/* Command line parameters */
static int pvscsi_ring_pages;
static int pvscsi_msg_ring_pages = PVSCSI_DEFAULT_NUM_PAGES_MSG_RING;
static int pvscsi_cmd_per_lun    = PVSCSI_DEFAULT_QUEUE_DEPTH;
static bool pvscsi_disable_msi;
static bool pvscsi_disable_msix;
static bool pvscsi_use_msg       = true;
static bool pvscsi_use_req_threshold = true;

#define PVSCSI_RW (S_IRUSR | S_IWUSR)

module_param_named(ring_pages, pvscsi_ring_pages, int, PVSCSI_RW);
MODULE_PARM_DESC(ring_pages, "Number of pages per req/cmp ring - (default="
		 __stringify(PVSCSI_DEFAULT_NUM_PAGES_PER_RING)
		 "[up to 16 targets],"
		 __stringify(PVSCSI_SETUP_RINGS_MAX_NUM_PAGES)
		 "[for 16+ targets])");

module_param_named(msg_ring_pages, pvscsi_msg_ring_pages, int, PVSCSI_RW);
MODULE_PARM_DESC(msg_ring_pages, "Number of pages for the msg ring - (default="
		 __stringify(PVSCSI_DEFAULT_NUM_PAGES_MSG_RING) ")");

module_param_named(cmd_per_lun, pvscsi_cmd_per_lun, int, PVSCSI_RW);
MODULE_PARM_DESC(cmd_per_lun, "Maximum commands per lun - (default="
		 __stringify(PVSCSI_DEFAULT_QUEUE_DEPTH) ")");

module_param_named(disable_msi, pvscsi_disable_msi, bool, PVSCSI_RW);
MODULE_PARM_DESC(disable_msi, "Disable MSI use in driver - (default=0)");

module_param_named(disable_msix, pvscsi_disable_msix, bool, PVSCSI_RW);
MODULE_PARM_DESC(disable_msix, "Disable MSI-X use in driver - (default=0)");

module_param_named(use_msg, pvscsi_use_msg, bool, PVSCSI_RW);
MODULE_PARM_DESC(use_msg, "Use msg ring when available - (default=1)");

module_param_named(use_req_threshold, pvscsi_use_req_threshold,
		   bool, PVSCSI_RW);
MODULE_PARM_DESC(use_req_threshold, "Use driver-based request coalescing if configured - (default=1)");

static const struct pci_device_id pvscsi_pci_tbl[] = {
	{ PCI_VDEVICE(VMWARE, PCI_DEVICE_ID_VMWARE_PVSCSI) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, pvscsi_pci_tbl);

static struct device *
pvscsi_dev(const struct pvscsi_adapter *adapter)
{
	return &(adapter->dev->dev);
}

static struct pvscsi_ctx *
pvscsi_find_context(const struct pvscsi_adapter *adapter, struct scsi_cmnd *cmd)
{
	struct pvscsi_ctx *ctx, *end;

	end = &adapter->cmd_map[adapter->req_depth];
	for (ctx = adapter->cmd_map; ctx < end; ctx++)
		if (ctx->cmd == cmd)
			return ctx;

	return NULL;
}

static struct pvscsi_ctx *
pvscsi_acquire_context(struct pvscsi_adapter *adapter, struct scsi_cmnd *cmd)
{
	struct pvscsi_ctx *ctx;

	if (list_empty(&adapter->cmd_pool))
		return NULL;

	ctx = list_first_entry(&adapter->cmd_pool, struct pvscsi_ctx, list);
	ctx->cmd = cmd;
	list_del(&ctx->list);

	return ctx;
}

static void pvscsi_release_context(struct pvscsi_adapter *adapter,
				   struct pvscsi_ctx *ctx)
{
	ctx->cmd = NULL;
	ctx->abort_cmp = NULL;
	list_add(&ctx->list, &adapter->cmd_pool);
}

/*
 * Map a pvscsi_ctx struct to a context ID field value; we map to a simple
 * non-zero integer. ctx always points to an entry in cmd_map array, hence
 * the return value is always >=1.
 */
static u64 pvscsi_map_context(const struct pvscsi_adapter *adapter,
			      const struct pvscsi_ctx *ctx)
{
	return ctx - adapter->cmd_map + 1;
}

static struct pvscsi_ctx *
pvscsi_get_context(const struct pvscsi_adapter *adapter, u64 context)
{
	return &adapter->cmd_map[context - 1];
}

static void pvscsi_reg_write(const struct pvscsi_adapter *adapter,
			     u32 offset, u32 val)
{
	writel(val, adapter->mmioBase + offset);
}

static u32 pvscsi_reg_read(const struct pvscsi_adapter *adapter, u32 offset)
{
	return readl(adapter->mmioBase + offset);
}

static u32 pvscsi_read_intr_status(const struct pvscsi_adapter *adapter)
{
	return pvscsi_reg_read(adapter, PVSCSI_REG_OFFSET_INTR_STATUS);
}

static void pvscsi_write_intr_status(const struct pvscsi_adapter *adapter,
				     u32 val)
{
	pvscsi_reg_write(adapter, PVSCSI_REG_OFFSET_INTR_STATUS, val);
}

static void pvscsi_unmask_intr(const struct pvscsi_adapter *adapter)
{
	u32 intr_bits;

	intr_bits = PVSCSI_INTR_CMPL_MASK;
	if (adapter->use_msg)
		intr_bits |= PVSCSI_INTR_MSG_MASK;

	pvscsi_reg_write(adapter, PVSCSI_REG_OFFSET_INTR_MASK, intr_bits);
}

static void pvscsi_mask_intr(const struct pvscsi_adapter *adapter)
{
	pvscsi_reg_write(adapter, PVSCSI_REG_OFFSET_INTR_MASK, 0);
}

static void pvscsi_write_cmd_desc(const struct pvscsi_adapter *adapter,
				  u32 cmd, const void *desc, size_t len)
{
	const u32 *ptr = desc;
	size_t i;

	len /= sizeof(*ptr);
	pvscsi_reg_write(adapter, PVSCSI_REG_OFFSET_COMMAND, cmd);
	for (i = 0; i < len; i++)
		pvscsi_reg_write(adapter,
				 PVSCSI_REG_OFFSET_COMMAND_DATA, ptr[i]);
}

static void pvscsi_abort_cmd(const struct pvscsi_adapter *adapter,
			     const struct pvscsi_ctx *ctx)
{
	struct PVSCSICmdDescAbortCmd cmd = { 0 };

	cmd.target = ctx->cmd->device->id;
	cmd.context = pvscsi_map_context(adapter, ctx);

	pvscsi_write_cmd_desc(adapter, PVSCSI_CMD_ABORT_CMD, &cmd, sizeof(cmd));
}

static void pvscsi_kick_rw_io(const struct pvscsi_adapter *adapter)
{
	pvscsi_reg_write(adapter, PVSCSI_REG_OFFSET_KICK_RW_IO, 0);
}

static void pvscsi_process_request_ring(const struct pvscsi_adapter *adapter)
{
	pvscsi_reg_write(adapter, PVSCSI_REG_OFFSET_KICK_NON_RW_IO, 0);
}

static int scsi_is_rw(unsigned char op)
{
	return op == READ_6  || op == WRITE_6 ||
	       op == READ_10 || op == WRITE_10 ||
	       op == READ_12 || op == WRITE_12 ||
	       op == READ_16 || op == WRITE_16;
}

static void pvscsi_kick_io(const struct pvscsi_adapter *adapter,
			   unsigned char op)
{
	if (scsi_is_rw(op)) {
		struct PVSCSIRingsState *s = adapter->rings_state;

		if (!adapter->use_req_threshold ||
		    s->reqProdIdx - s->reqConsIdx >= s->reqCallThreshold)
			pvscsi_kick_rw_io(adapter);
	} else {
		pvscsi_process_request_ring(adapter);
	}
}

static void ll_adapter_reset(const struct pvscsi_adapter *adapter)
{
	dev_dbg(pvscsi_dev(adapter), "Adapter Reset on %p\n", adapter);

	pvscsi_write_cmd_desc(adapter, PVSCSI_CMD_ADAPTER_RESET, NULL, 0);
}

static void ll_bus_reset(const struct pvscsi_adapter *adapter)
{
	dev_dbg(pvscsi_dev(adapter), "Resetting bus on %p\n", adapter);

	pvscsi_write_cmd_desc(adapter, PVSCSI_CMD_RESET_BUS, NULL, 0);
}

static void ll_device_reset(const struct pvscsi_adapter *adapter, u32 target)
{
	struct PVSCSICmdDescResetDevice cmd = { 0 };

	dev_dbg(pvscsi_dev(adapter), "Resetting device: target=%u\n", target);

	cmd.target = target;

	pvscsi_write_cmd_desc(adapter, PVSCSI_CMD_RESET_DEVICE,
			      &cmd, sizeof(cmd));
}

static void pvscsi_create_sg(struct pvscsi_ctx *ctx,
			     struct scatterlist *sg, unsigned count)
{
	unsigned i;
	struct PVSCSISGElement *sge;

	BUG_ON(count > PVSCSI_MAX_NUM_SG_ENTRIES_PER_SEGMENT);

	sge = &ctx->sgl->sge[0];
	for (i = 0; i < count; i++, sg = sg_next(sg)) {
		sge[i].addr   = sg_dma_address(sg);
		sge[i].length = sg_dma_len(sg);
		sge[i].flags  = 0;
	}
}

/*
 * Map all data buffers for a command into PCI space and
 * setup the scatter/gather list if needed.
 */
static int pvscsi_map_buffers(struct pvscsi_adapter *adapter,
			      struct pvscsi_ctx *ctx, struct scsi_cmnd *cmd,
			      struct PVSCSIRingReqDesc *e)
{
	unsigned count;
	unsigned bufflen = scsi_bufflen(cmd);
	struct scatterlist *sg;

	e->dataLen = bufflen;
	e->dataAddr = 0;
	if (bufflen == 0)
		return 0;

	sg = scsi_sglist(cmd);
	count = scsi_sg_count(cmd);
	if (count != 0) {
		int segs = scsi_dma_map(cmd);

		if (segs == -ENOMEM) {
			scmd_printk(KERN_DEBUG, cmd,
				    "vmw_pvscsi: Failed to map cmd sglist for DMA.\n");
			return -ENOMEM;
		} else if (segs > 1) {
			pvscsi_create_sg(ctx, sg, segs);

			e->flags |= PVSCSI_FLAG_CMD_WITH_SG_LIST;
			ctx->sglPA = dma_map_single(&adapter->dev->dev,
					ctx->sgl, SGL_SIZE, DMA_TO_DEVICE);
			if (dma_mapping_error(&adapter->dev->dev, ctx->sglPA)) {
				scmd_printk(KERN_ERR, cmd,
					    "vmw_pvscsi: Failed to map ctx sglist for DMA.\n");
				scsi_dma_unmap(cmd);
				ctx->sglPA = 0;
				return -ENOMEM;
			}
			e->dataAddr = ctx->sglPA;
		} else
			e->dataAddr = sg_dma_address(sg);
	} else {
		/*
		 * In case there is no S/G list, scsi_sglist points
		 * directly to the buffer.
		 */
		ctx->dataPA = dma_map_single(&adapter->dev->dev, sg, bufflen,
					     cmd->sc_data_direction);
		if (dma_mapping_error(&adapter->dev->dev, ctx->dataPA)) {
			scmd_printk(KERN_DEBUG, cmd,
				    "vmw_pvscsi: Failed to map direct data buffer for DMA.\n");
			return -ENOMEM;
		}
		e->dataAddr = ctx->dataPA;
	}

	return 0;
}

/*
 * The device incorrectly doesn't clear the first byte of the sense
 * buffer in some cases. We have to do it ourselves.
 * Otherwise we run into trouble when SWIOTLB is forced.
 */
static void pvscsi_patch_sense(struct scsi_cmnd *cmd)
{
	if (cmd->sense_buffer)
		cmd->sense_buffer[0] = 0;
}

static void pvscsi_unmap_buffers(const struct pvscsi_adapter *adapter,
				 struct pvscsi_ctx *ctx)
{
	struct scsi_cmnd *cmd;
	unsigned bufflen;

	cmd = ctx->cmd;
	bufflen = scsi_bufflen(cmd);

	if (bufflen != 0) {
		unsigned count = scsi_sg_count(cmd);

		if (count != 0) {
			scsi_dma_unmap(cmd);
			if (ctx->sglPA) {
				dma_unmap_single(&adapter->dev->dev, ctx->sglPA,
						 SGL_SIZE, DMA_TO_DEVICE);
				ctx->sglPA = 0;
			}
		} else
			dma_unmap_single(&adapter->dev->dev, ctx->dataPA,
					 bufflen, cmd->sc_data_direction);
	}
	if (cmd->sense_buffer)
		dma_unmap_single(&adapter->dev->dev, ctx->sensePA,
				 SCSI_SENSE_BUFFERSIZE, DMA_FROM_DEVICE);
}

static int pvscsi_allocate_rings(struct pvscsi_adapter *adapter)
{
	adapter->rings_state = dma_alloc_coherent(&adapter->dev->dev, PAGE_SIZE,
			&adapter->ringStatePA, GFP_KERNEL);
	if (!adapter->rings_state)
		return -ENOMEM;

	adapter->req_pages = min(PVSCSI_MAX_NUM_PAGES_REQ_RING,
				 pvscsi_ring_pages);
	adapter->req_depth = adapter->req_pages
					* PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE;
	adapter->req_ring = dma_alloc_coherent(&adapter->dev->dev,
			adapter->req_pages * PAGE_SIZE, &adapter->reqRingPA,
			GFP_KERNEL);
	if (!adapter->req_ring)
		return -ENOMEM;

	adapter->cmp_pages = min(PVSCSI_MAX_NUM_PAGES_CMP_RING,
				 pvscsi_ring_pages);
	adapter->cmp_ring = dma_alloc_coherent(&adapter->dev->dev,
			adapter->cmp_pages * PAGE_SIZE, &adapter->cmpRingPA,
			GFP_KERNEL);
	if (!adapter->cmp_ring)
		return -ENOMEM;

	BUG_ON(!IS_ALIGNED(adapter->ringStatePA, PAGE_SIZE));
	BUG_ON(!IS_ALIGNED(adapter->reqRingPA, PAGE_SIZE));
	BUG_ON(!IS_ALIGNED(adapter->cmpRingPA, PAGE_SIZE));

	if (!adapter->use_msg)
		return 0;

	adapter->msg_pages = min(PVSCSI_MAX_NUM_PAGES_MSG_RING,
				 pvscsi_msg_ring_pages);
	adapter->msg_ring = dma_alloc_coherent(&adapter->dev->dev,
			adapter->msg_pages * PAGE_SIZE, &adapter->msgRingPA,
			GFP_KERNEL);
	if (!adapter->msg_ring)
		return -ENOMEM;
	BUG_ON(!IS_ALIGNED(adapter->msgRingPA, PAGE_SIZE));

	return 0;
}

static void pvscsi_setup_all_rings(const struct pvscsi_adapter *adapter)
{
	struct PVSCSICmdDescSetupRings cmd = { 0 };
	dma_addr_t base;
	unsigned i;

	cmd.ringsStatePPN   = adapter->ringStatePA >> PAGE_SHIFT;
	cmd.reqRingNumPages = adapter->req_pages;
	cmd.cmpRingNumPages = adapter->cmp_pages;

	base = adapter->reqRingPA;
	for (i = 0; i < adapter->req_pages; i++) {
		cmd.reqRingPPNs[i] = base >> PAGE_SHIFT;
		base += PAGE_SIZE;
	}

	base = adapter->cmpRingPA;
	for (i = 0; i < adapter->cmp_pages; i++) {
		cmd.cmpRingPPNs[i] = base >> PAGE_SHIFT;
		base += PAGE_SIZE;
	}

	memset(adapter->rings_state, 0, PAGE_SIZE);
	memset(adapter->req_ring, 0, adapter->req_pages * PAGE_SIZE);
	memset(adapter->cmp_ring, 0, adapter->cmp_pages * PAGE_SIZE);

	pvscsi_write_cmd_desc(adapter, PVSCSI_CMD_SETUP_RINGS,
			      &cmd, sizeof(cmd));

	if (adapter->use_msg) {
		struct PVSCSICmdDescSetupMsgRing cmd_msg = { 0 };

		cmd_msg.numPages = adapter->msg_pages;

		base = adapter->msgRingPA;
		for (i = 0; i < adapter->msg_pages; i++) {
			cmd_msg.ringPPNs[i] = base >> PAGE_SHIFT;
			base += PAGE_SIZE;
		}
		memset(adapter->msg_ring, 0, adapter->msg_pages * PAGE_SIZE);

		pvscsi_write_cmd_desc(adapter, PVSCSI_CMD_SETUP_MSG_RING,
				      &cmd_msg, sizeof(cmd_msg));
	}
}

static int pvscsi_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	if (!sdev->tagged_supported)
		qdepth = 1;
	return scsi_change_queue_depth(sdev, qdepth);
}

/*
 * Pull a completion descriptor off and pass the completion back
 * to the SCSI mid layer.
 */
static void pvscsi_complete_request(struct pvscsi_adapter *adapter,
				    const struct PVSCSIRingCmpDesc *e)
{
	struct pvscsi_ctx *ctx;
	struct scsi_cmnd *cmd;
	struct completion *abort_cmp;
	u32 btstat = e->hostStatus;
	u32 sdstat = e->scsiStatus;

	ctx = pvscsi_get_context(adapter, e->context);
	cmd = ctx->cmd;
	abort_cmp = ctx->abort_cmp;
	pvscsi_unmap_buffers(adapter, ctx);
	if (sdstat != SAM_STAT_CHECK_CONDITION)
		pvscsi_patch_sense(cmd);
	pvscsi_release_context(adapter, ctx);
	if (abort_cmp) {
		/*
		 * The command was requested to be aborted. Just signal that
		 * the request completed and swallow the actual cmd completion
		 * here. The abort handler will post a completion for this
		 * command indicating that it got successfully aborted.
		 */
		complete(abort_cmp);
		return;
	}

	cmd->result = 0;
	if (sdstat != SAM_STAT_GOOD &&
	    (btstat == BTSTAT_SUCCESS ||
	     btstat == BTSTAT_LINKED_COMMAND_COMPLETED ||
	     btstat == BTSTAT_LINKED_COMMAND_COMPLETED_WITH_FLAG)) {
		if (sdstat == SAM_STAT_COMMAND_TERMINATED) {
			cmd->result = (DID_RESET << 16);
		} else {
			cmd->result = (DID_OK << 16) | sdstat;
			if (sdstat == SAM_STAT_CHECK_CONDITION &&
			    cmd->sense_buffer)
				cmd->result |= (DRIVER_SENSE << 24);
		}
	} else
		switch (btstat) {
		case BTSTAT_SUCCESS:
		case BTSTAT_LINKED_COMMAND_COMPLETED:
		case BTSTAT_LINKED_COMMAND_COMPLETED_WITH_FLAG:
			/*
			 * Commands like INQUIRY may transfer less data than
			 * requested by the initiator via bufflen. Set residual
			 * count to make upper layer aware of the actual amount
			 * of data returned. There are cases when controller
			 * returns zero dataLen with non zero data - do not set
			 * residual count in that case.
			 */
			if (e->dataLen && (e->dataLen < scsi_bufflen(cmd)))
				scsi_set_resid(cmd, scsi_bufflen(cmd) - e->dataLen);
			cmd->result = (DID_OK << 16);
			break;

		case BTSTAT_DATARUN:
		case BTSTAT_DATA_UNDERRUN:
			/* Report residual data in underruns */
			scsi_set_resid(cmd, scsi_bufflen(cmd) - e->dataLen);
			cmd->result = (DID_ERROR << 16);
			break;

		case BTSTAT_SELTIMEO:
			/* Our emulation returns this for non-connected devs */
			cmd->result = (DID_BAD_TARGET << 16);
			break;

		case BTSTAT_LUNMISMATCH:
		case BTSTAT_TAGREJECT:
		case BTSTAT_BADMSG:
			cmd->result = (DRIVER_INVALID << 24);
			fallthrough;

		case BTSTAT_HAHARDWARE:
		case BTSTAT_INVPHASE:
		case BTSTAT_HATIMEOUT:
		case BTSTAT_NORESPONSE:
		case BTSTAT_DISCONNECT:
		case BTSTAT_HASOFTWARE:
		case BTSTAT_BUSFREE:
		case BTSTAT_SENSFAILED:
			cmd->result |= (DID_ERROR << 16);
			break;

		case BTSTAT_SENTRST:
		case BTSTAT_RECVRST:
		case BTSTAT_BUSRESET:
			cmd->result = (DID_RESET << 16);
			break;

		case BTSTAT_ABORTQUEUE:
			cmd->result = (DID_BUS_BUSY << 16);
			break;

		case BTSTAT_SCSIPARITY:
			cmd->result = (DID_PARITY << 16);
			break;

		default:
			cmd->result = (DID_ERROR << 16);
			scmd_printk(KERN_DEBUG, cmd,
				    "Unknown completion status: 0x%x\n",
				    btstat);
	}

	dev_dbg(&cmd->device->sdev_gendev,
		"cmd=%p %x ctx=%p result=0x%x status=0x%x,%x\n",
		cmd, cmd->cmnd[0], ctx, cmd->result, btstat, sdstat);

	cmd->scsi_done(cmd);
}

/*
 * barrier usage : Since the PVSCSI device is emulated, there could be cases
 * where we may want to serialize some accesses between the driver and the
 * emulation layer. We use compiler barriers instead of the more expensive
 * memory barriers because PVSCSI is only supported on X86 which has strong
 * memory access ordering.
 */
static void pvscsi_process_completion_ring(struct pvscsi_adapter *adapter)
{
	struct PVSCSIRingsState *s = adapter->rings_state;
	struct PVSCSIRingCmpDesc *ring = adapter->cmp_ring;
	u32 cmp_entries = s->cmpNumEntriesLog2;

	while (s->cmpConsIdx != s->cmpProdIdx) {
		struct PVSCSIRingCmpDesc *e = ring + (s->cmpConsIdx &
						      MASK(cmp_entries));
		/*
		 * This barrier() ensures that *e is not dereferenced while
		 * the device emulation still writes data into the slot.
		 * Since the device emulation advances s->cmpProdIdx only after
		 * updating the slot we want to check it first.
		 */
		barrier();
		pvscsi_complete_request(adapter, e);
		/*
		 * This barrier() ensures that compiler doesn't reorder write
		 * to s->cmpConsIdx before the read of (*e) inside
		 * pvscsi_complete_request. Otherwise, device emulation may
		 * overwrite *e before we had a chance to read it.
		 */
		barrier();
		s->cmpConsIdx++;
	}
}

/*
 * Translate a Linux SCSI request into a request ring entry.
 */
static int pvscsi_queue_ring(struct pvscsi_adapter *adapter,
			     struct pvscsi_ctx *ctx, struct scsi_cmnd *cmd)
{
	struct PVSCSIRingsState *s;
	struct PVSCSIRingReqDesc *e;
	struct scsi_device *sdev;
	u32 req_entries;

	s = adapter->rings_state;
	sdev = cmd->device;
	req_entries = s->reqNumEntriesLog2;

	/*
	 * If this condition holds, we might have room on the request ring, but
	 * we might not have room on the completion ring for the response.
	 * However, we have already ruled out this possibility - we would not
	 * have successfully allocated a context if it were true, since we only
	 * have one context per request entry.  Check for it anyway, since it
	 * would be a serious bug.
	 */
	if (s->reqProdIdx - s->cmpConsIdx >= 1 << req_entries) {
		scmd_printk(KERN_ERR, cmd, "vmw_pvscsi: "
			    "ring full: reqProdIdx=%d cmpConsIdx=%d\n",
			    s->reqProdIdx, s->cmpConsIdx);
		return -1;
	}

	e = adapter->req_ring + (s->reqProdIdx & MASK(req_entries));

	e->bus    = sdev->channel;
	e->target = sdev->id;
	memset(e->lun, 0, sizeof(e->lun));
	e->lun[1] = sdev->lun;

	if (cmd->sense_buffer) {
		ctx->sensePA = dma_map_single(&adapter->dev->dev,
				cmd->sense_buffer, SCSI_SENSE_BUFFERSIZE,
				DMA_FROM_DEVICE);
		if (dma_mapping_error(&adapter->dev->dev, ctx->sensePA)) {
			scmd_printk(KERN_DEBUG, cmd,
				    "vmw_pvscsi: Failed to map sense buffer for DMA.\n");
			ctx->sensePA = 0;
			return -ENOMEM;
		}
		e->senseAddr = ctx->sensePA;
		e->senseLen = SCSI_SENSE_BUFFERSIZE;
	} else {
		e->senseLen  = 0;
		e->senseAddr = 0;
	}
	e->cdbLen   = cmd->cmd_len;
	e->vcpuHint = smp_processor_id();
	memcpy(e->cdb, cmd->cmnd, e->cdbLen);

	e->tag = SIMPLE_QUEUE_TAG;

	if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		e->flags = PVSCSI_FLAG_CMD_DIR_TOHOST;
	else if (cmd->sc_data_direction == DMA_TO_DEVICE)
		e->flags = PVSCSI_FLAG_CMD_DIR_TODEVICE;
	else if (cmd->sc_data_direction == DMA_NONE)
		e->flags = PVSCSI_FLAG_CMD_DIR_NONE;
	else
		e->flags = 0;

	if (pvscsi_map_buffers(adapter, ctx, cmd, e) != 0) {
		if (cmd->sense_buffer) {
			dma_unmap_single(&adapter->dev->dev, ctx->sensePA,
					 SCSI_SENSE_BUFFERSIZE,
					 DMA_FROM_DEVICE);
			ctx->sensePA = 0;
		}
		return -ENOMEM;
	}

	e->context = pvscsi_map_context(adapter, ctx);

	barrier();

	s->reqProdIdx++;

	return 0;
}

static int pvscsi_queue_lck(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	struct Scsi_Host *host = cmd->device->host;
	struct pvscsi_adapter *adapter = shost_priv(host);
	struct pvscsi_ctx *ctx;
	unsigned long flags;
	unsigned char op;

	spin_lock_irqsave(&adapter->hw_lock, flags);

	ctx = pvscsi_acquire_context(adapter, cmd);
	if (!ctx || pvscsi_queue_ring(adapter, ctx, cmd) != 0) {
		if (ctx)
			pvscsi_release_context(adapter, ctx);
		spin_unlock_irqrestore(&adapter->hw_lock, flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	cmd->scsi_done = done;
	op = cmd->cmnd[0];

	dev_dbg(&cmd->device->sdev_gendev,
		"queued cmd %p, ctx %p, op=%x\n", cmd, ctx, op);

	spin_unlock_irqrestore(&adapter->hw_lock, flags);

	pvscsi_kick_io(adapter, op);

	return 0;
}

static DEF_SCSI_QCMD(pvscsi_queue)

static int pvscsi_abort(struct scsi_cmnd *cmd)
{
	struct pvscsi_adapter *adapter = shost_priv(cmd->device->host);
	struct pvscsi_ctx *ctx;
	unsigned long flags;
	int result = SUCCESS;
	DECLARE_COMPLETION_ONSTACK(abort_cmp);
	int done;

	scmd_printk(KERN_DEBUG, cmd, "task abort on host %u, %p\n",
		    adapter->host->host_no, cmd);

	spin_lock_irqsave(&adapter->hw_lock, flags);

	/*
	 * Poll the completion ring first - we might be trying to abort
	 * a command that is waiting to be dispatched in the completion ring.
	 */
	pvscsi_process_completion_ring(adapter);

	/*
	 * If there is no context for the command, it either already succeeded
	 * or else was never properly issued.  Not our problem.
	 */
	ctx = pvscsi_find_context(adapter, cmd);
	if (!ctx) {
		scmd_printk(KERN_DEBUG, cmd, "Failed to abort cmd %p\n", cmd);
		goto out;
	}

	/*
	 * Mark that the command has been requested to be aborted and issue
	 * the abort.
	 */
	ctx->abort_cmp = &abort_cmp;

	pvscsi_abort_cmd(adapter, ctx);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	/* Wait for 2 secs for the completion. */
	done = wait_for_completion_timeout(&abort_cmp, msecs_to_jiffies(2000));
	spin_lock_irqsave(&adapter->hw_lock, flags);

	if (!done) {
		/*
		 * Failed to abort the command, unmark the fact that it
		 * was requested to be aborted.
		 */
		ctx->abort_cmp = NULL;
		result = FAILED;
		scmd_printk(KERN_DEBUG, cmd,
			    "Failed to get completion for aborted cmd %p\n",
			    cmd);
		goto out;
	}

	/*
	 * Successfully aborted the command.
	 */
	cmd->result = (DID_ABORT << 16);
	cmd->scsi_done(cmd);

out:
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	return result;
}

/*
 * Abort all outstanding requests.  This is only safe to use if the completion
 * ring will never be walked again or the device has been reset, because it
 * destroys the 1-1 mapping between context field passed to emulation and our
 * request structure.
 */
static void pvscsi_reset_all(struct pvscsi_adapter *adapter)
{
	unsigned i;

	for (i = 0; i < adapter->req_depth; i++) {
		struct pvscsi_ctx *ctx = &adapter->cmd_map[i];
		struct scsi_cmnd *cmd = ctx->cmd;
		if (cmd) {
			scmd_printk(KERN_ERR, cmd,
				    "Forced reset on cmd %p\n", cmd);
			pvscsi_unmap_buffers(adapter, ctx);
			pvscsi_patch_sense(cmd);
			pvscsi_release_context(adapter, ctx);
			cmd->result = (DID_RESET << 16);
			cmd->scsi_done(cmd);
		}
	}
}

static int pvscsi_host_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct pvscsi_adapter *adapter = shost_priv(host);
	unsigned long flags;
	bool use_msg;

	scmd_printk(KERN_INFO, cmd, "SCSI Host reset\n");

	spin_lock_irqsave(&adapter->hw_lock, flags);

	use_msg = adapter->use_msg;

	if (use_msg) {
		adapter->use_msg = false;
		spin_unlock_irqrestore(&adapter->hw_lock, flags);

		/*
		 * Now that we know that the ISR won't add more work on the
		 * workqueue we can safely flush any outstanding work.
		 */
		flush_workqueue(adapter->workqueue);
		spin_lock_irqsave(&adapter->hw_lock, flags);
	}

	/*
	 * We're going to tear down the entire ring structure and set it back
	 * up, so stalling new requests until all completions are flushed and
	 * the rings are back in place.
	 */

	pvscsi_process_request_ring(adapter);

	ll_adapter_reset(adapter);

	/*
	 * Now process any completions.  Note we do this AFTER adapter reset,
	 * which is strange, but stops races where completions get posted
	 * between processing the ring and issuing the reset.  The backend will
	 * not touch the ring memory after reset, so the immediately pre-reset
	 * completion ring state is still valid.
	 */
	pvscsi_process_completion_ring(adapter);

	pvscsi_reset_all(adapter);
	adapter->use_msg = use_msg;
	pvscsi_setup_all_rings(adapter);
	pvscsi_unmask_intr(adapter);

	spin_unlock_irqrestore(&adapter->hw_lock, flags);

	return SUCCESS;
}

static int pvscsi_bus_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct pvscsi_adapter *adapter = shost_priv(host);
	unsigned long flags;

	scmd_printk(KERN_INFO, cmd, "SCSI Bus reset\n");

	/*
	 * We don't want to queue new requests for this bus after
	 * flushing all pending requests to emulation, since new
	 * requests could then sneak in during this bus reset phase,
	 * so take the lock now.
	 */
	spin_lock_irqsave(&adapter->hw_lock, flags);

	pvscsi_process_request_ring(adapter);
	ll_bus_reset(adapter);
	pvscsi_process_completion_ring(adapter);

	spin_unlock_irqrestore(&adapter->hw_lock, flags);

	return SUCCESS;
}

static int pvscsi_device_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct pvscsi_adapter *adapter = shost_priv(host);
	unsigned long flags;

	scmd_printk(KERN_INFO, cmd, "SCSI device reset on scsi%u:%u\n",
		    host->host_no, cmd->device->id);

	/*
	 * We don't want to queue new requests for this device after flushing
	 * all pending requests to emulation, since new requests could then
	 * sneak in during this device reset phase, so take the lock now.
	 */
	spin_lock_irqsave(&adapter->hw_lock, flags);

	pvscsi_process_request_ring(adapter);
	ll_device_reset(adapter, cmd->device->id);
	pvscsi_process_completion_ring(adapter);

	spin_unlock_irqrestore(&adapter->hw_lock, flags);

	return SUCCESS;
}

static struct scsi_host_template pvscsi_template;

static const char *pvscsi_info(struct Scsi_Host *host)
{
	struct pvscsi_adapter *adapter = shost_priv(host);
	static char buf[256];

	sprintf(buf, "VMware PVSCSI storage adapter rev %d, req/cmp/msg rings: "
		"%u/%u/%u pages, cmd_per_lun=%u", adapter->rev,
		adapter->req_pages, adapter->cmp_pages, adapter->msg_pages,
		pvscsi_template.cmd_per_lun);

	return buf;
}

static struct scsi_host_template pvscsi_template = {
	.module				= THIS_MODULE,
	.name				= "VMware PVSCSI Host Adapter",
	.proc_name			= "vmw_pvscsi",
	.info				= pvscsi_info,
	.queuecommand			= pvscsi_queue,
	.this_id			= -1,
	.sg_tablesize			= PVSCSI_MAX_NUM_SG_ENTRIES_PER_SEGMENT,
	.dma_boundary			= UINT_MAX,
	.max_sectors			= 0xffff,
	.change_queue_depth		= pvscsi_change_queue_depth,
	.eh_abort_handler		= pvscsi_abort,
	.eh_device_reset_handler	= pvscsi_device_reset,
	.eh_bus_reset_handler		= pvscsi_bus_reset,
	.eh_host_reset_handler		= pvscsi_host_reset,
};

static void pvscsi_process_msg(const struct pvscsi_adapter *adapter,
			       const struct PVSCSIRingMsgDesc *e)
{
	struct PVSCSIRingsState *s = adapter->rings_state;
	struct Scsi_Host *host = adapter->host;
	struct scsi_device *sdev;

	printk(KERN_INFO "vmw_pvscsi: msg type: 0x%x - MSG RING: %u/%u (%u) \n",
	       e->type, s->msgProdIdx, s->msgConsIdx, s->msgNumEntriesLog2);

	BUILD_BUG_ON(PVSCSI_MSG_LAST != 2);

	if (e->type == PVSCSI_MSG_DEV_ADDED) {
		struct PVSCSIMsgDescDevStatusChanged *desc;
		desc = (struct PVSCSIMsgDescDevStatusChanged *)e;

		printk(KERN_INFO
		       "vmw_pvscsi: msg: device added at scsi%u:%u:%u\n",
		       desc->bus, desc->target, desc->lun[1]);

		if (!scsi_host_get(host))
			return;

		sdev = scsi_device_lookup(host, desc->bus, desc->target,
					  desc->lun[1]);
		if (sdev) {
			printk(KERN_INFO "vmw_pvscsi: device already exists\n");
			scsi_device_put(sdev);
		} else
			scsi_add_device(adapter->host, desc->bus,
					desc->target, desc->lun[1]);

		scsi_host_put(host);
	} else if (e->type == PVSCSI_MSG_DEV_REMOVED) {
		struct PVSCSIMsgDescDevStatusChanged *desc;
		desc = (struct PVSCSIMsgDescDevStatusChanged *)e;

		printk(KERN_INFO
		       "vmw_pvscsi: msg: device removed at scsi%u:%u:%u\n",
		       desc->bus, desc->target, desc->lun[1]);

		if (!scsi_host_get(host))
			return;

		sdev = scsi_device_lookup(host, desc->bus, desc->target,
					  desc->lun[1]);
		if (sdev) {
			scsi_remove_device(sdev);
			scsi_device_put(sdev);
		} else
			printk(KERN_INFO
			       "vmw_pvscsi: failed to lookup scsi%u:%u:%u\n",
			       desc->bus, desc->target, desc->lun[1]);

		scsi_host_put(host);
	}
}

static int pvscsi_msg_pending(const struct pvscsi_adapter *adapter)
{
	struct PVSCSIRingsState *s = adapter->rings_state;

	return s->msgProdIdx != s->msgConsIdx;
}

static void pvscsi_process_msg_ring(const struct pvscsi_adapter *adapter)
{
	struct PVSCSIRingsState *s = adapter->rings_state;
	struct PVSCSIRingMsgDesc *ring = adapter->msg_ring;
	u32 msg_entries = s->msgNumEntriesLog2;

	while (pvscsi_msg_pending(adapter)) {
		struct PVSCSIRingMsgDesc *e = ring + (s->msgConsIdx &
						      MASK(msg_entries));

		barrier();
		pvscsi_process_msg(adapter, e);
		barrier();
		s->msgConsIdx++;
	}
}

static void pvscsi_msg_workqueue_handler(struct work_struct *data)
{
	struct pvscsi_adapter *adapter;

	adapter = container_of(data, struct pvscsi_adapter, work);

	pvscsi_process_msg_ring(adapter);
}

static int pvscsi_setup_msg_workqueue(struct pvscsi_adapter *adapter)
{
	char name[32];

	if (!pvscsi_use_msg)
		return 0;

	pvscsi_reg_write(adapter, PVSCSI_REG_OFFSET_COMMAND,
			 PVSCSI_CMD_SETUP_MSG_RING);

	if (pvscsi_reg_read(adapter, PVSCSI_REG_OFFSET_COMMAND_STATUS) == -1)
		return 0;

	snprintf(name, sizeof(name),
		 "vmw_pvscsi_wq_%u", adapter->host->host_no);

	adapter->workqueue = create_singlethread_workqueue(name);
	if (!adapter->workqueue) {
		printk(KERN_ERR "vmw_pvscsi: failed to create work queue\n");
		return 0;
	}
	INIT_WORK(&adapter->work, pvscsi_msg_workqueue_handler);

	return 1;
}

static bool pvscsi_setup_req_threshold(struct pvscsi_adapter *adapter,
				      bool enable)
{
	u32 val;

	if (!pvscsi_use_req_threshold)
		return false;

	pvscsi_reg_write(adapter, PVSCSI_REG_OFFSET_COMMAND,
			 PVSCSI_CMD_SETUP_REQCALLTHRESHOLD);
	val = pvscsi_reg_read(adapter, PVSCSI_REG_OFFSET_COMMAND_STATUS);
	if (val == -1) {
		printk(KERN_INFO "vmw_pvscsi: device does not support req_threshold\n");
		return false;
	} else {
		struct PVSCSICmdDescSetupReqCall cmd_msg = { 0 };
		cmd_msg.enable = enable;
		printk(KERN_INFO
		       "vmw_pvscsi: %sabling reqCallThreshold\n",
			enable ? "en" : "dis");
		pvscsi_write_cmd_desc(adapter,
				      PVSCSI_CMD_SETUP_REQCALLTHRESHOLD,
				      &cmd_msg, sizeof(cmd_msg));
		return pvscsi_reg_read(adapter,
				       PVSCSI_REG_OFFSET_COMMAND_STATUS) != 0;
	}
}

static irqreturn_t pvscsi_isr(int irq, void *devp)
{
	struct pvscsi_adapter *adapter = devp;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	pvscsi_process_completion_ring(adapter);
	if (adapter->use_msg && pvscsi_msg_pending(adapter))
		queue_work(adapter->workqueue, &adapter->work);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t pvscsi_shared_isr(int irq, void *devp)
{
	struct pvscsi_adapter *adapter = devp;
	u32 val = pvscsi_read_intr_status(adapter);

	if (!(val & PVSCSI_INTR_ALL_SUPPORTED))
		return IRQ_NONE;
	pvscsi_write_intr_status(devp, val);
	return pvscsi_isr(irq, devp);
}

static void pvscsi_free_sgls(const struct pvscsi_adapter *adapter)
{
	struct pvscsi_ctx *ctx = adapter->cmd_map;
	unsigned i;

	for (i = 0; i < adapter->req_depth; ++i, ++ctx)
		free_pages((unsigned long)ctx->sgl, get_order(SGL_SIZE));
}

static void pvscsi_shutdown_intr(struct pvscsi_adapter *adapter)
{
	free_irq(pci_irq_vector(adapter->dev, 0), adapter);
	pci_free_irq_vectors(adapter->dev);
}

static void pvscsi_release_resources(struct pvscsi_adapter *adapter)
{
	if (adapter->workqueue)
		destroy_workqueue(adapter->workqueue);

	if (adapter->mmioBase)
		pci_iounmap(adapter->dev, adapter->mmioBase);

	pci_release_regions(adapter->dev);

	if (adapter->cmd_map) {
		pvscsi_free_sgls(adapter);
		kfree(adapter->cmd_map);
	}

	if (adapter->rings_state)
		dma_free_coherent(&adapter->dev->dev, PAGE_SIZE,
				    adapter->rings_state, adapter->ringStatePA);

	if (adapter->req_ring)
		dma_free_coherent(&adapter->dev->dev,
				    adapter->req_pages * PAGE_SIZE,
				    adapter->req_ring, adapter->reqRingPA);

	if (adapter->cmp_ring)
		dma_free_coherent(&adapter->dev->dev,
				    adapter->cmp_pages * PAGE_SIZE,
				    adapter->cmp_ring, adapter->cmpRingPA);

	if (adapter->msg_ring)
		dma_free_coherent(&adapter->dev->dev,
				    adapter->msg_pages * PAGE_SIZE,
				    adapter->msg_ring, adapter->msgRingPA);
}

/*
 * Allocate scatter gather lists.
 *
 * These are statically allocated.  Trying to be clever was not worth it.
 *
 * Dynamic allocation can fail, and we can't go deep into the memory
 * allocator, since we're a SCSI driver, and trying too hard to allocate
 * memory might generate disk I/O.  We also don't want to fail disk I/O
 * in that case because we can't get an allocation - the I/O could be
 * trying to swap out data to free memory.  Since that is pathological,
 * just use a statically allocated scatter list.
 *
 */
static int pvscsi_allocate_sg(struct pvscsi_adapter *adapter)
{
	struct pvscsi_ctx *ctx;
	int i;

	ctx = adapter->cmd_map;
	BUILD_BUG_ON(sizeof(struct pvscsi_sg_list) > SGL_SIZE);

	for (i = 0; i < adapter->req_depth; ++i, ++ctx) {
		ctx->sgl = (void *)__get_free_pages(GFP_KERNEL,
						    get_order(SGL_SIZE));
		ctx->sglPA = 0;
		BUG_ON(!IS_ALIGNED(((unsigned long)ctx->sgl), PAGE_SIZE));
		if (!ctx->sgl) {
			for (; i >= 0; --i, --ctx) {
				free_pages((unsigned long)ctx->sgl,
					   get_order(SGL_SIZE));
				ctx->sgl = NULL;
			}
			return -ENOMEM;
		}
	}

	return 0;
}

/*
 * Query the device, fetch the config info and return the
 * maximum number of targets on the adapter. In case of
 * failure due to any reason return default i.e. 16.
 */
static u32 pvscsi_get_max_targets(struct pvscsi_adapter *adapter)
{
	struct PVSCSICmdDescConfigCmd cmd;
	struct PVSCSIConfigPageHeader *header;
	struct device *dev;
	dma_addr_t configPagePA;
	void *config_page;
	u32 numPhys = 16;

	dev = pvscsi_dev(adapter);
	config_page = dma_alloc_coherent(&adapter->dev->dev, PAGE_SIZE,
			&configPagePA, GFP_KERNEL);
	if (!config_page) {
		dev_warn(dev, "vmw_pvscsi: failed to allocate memory for config page\n");
		goto exit;
	}
	BUG_ON(configPagePA & ~PAGE_MASK);

	/* Fetch config info from the device. */
	cmd.configPageAddress = ((u64)PVSCSI_CONFIG_CONTROLLER_ADDRESS) << 32;
	cmd.configPageNum = PVSCSI_CONFIG_PAGE_CONTROLLER;
	cmd.cmpAddr = configPagePA;
	cmd._pad = 0;

	/*
	 * Mark the completion page header with error values. If the device
	 * completes the command successfully, it sets the status values to
	 * indicate success.
	 */
	header = config_page;
	memset(header, 0, sizeof *header);
	header->hostStatus = BTSTAT_INVPARAM;
	header->scsiStatus = SDSTAT_CHECK;

	pvscsi_write_cmd_desc(adapter, PVSCSI_CMD_CONFIG, &cmd, sizeof cmd);

	if (header->hostStatus == BTSTAT_SUCCESS &&
	    header->scsiStatus == SDSTAT_GOOD) {
		struct PVSCSIConfigPageController *config;

		config = config_page;
		numPhys = config->numPhys;
	} else
		dev_warn(dev, "vmw_pvscsi: PVSCSI_CMD_CONFIG failed. hostStatus = 0x%x, scsiStatus = 0x%x\n",
			 header->hostStatus, header->scsiStatus);
	dma_free_coherent(&adapter->dev->dev, PAGE_SIZE, config_page,
			  configPagePA);
exit:
	return numPhys;
}

static int pvscsi_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	unsigned int irq_flag = PCI_IRQ_MSIX | PCI_IRQ_MSI | PCI_IRQ_LEGACY;
	struct pvscsi_adapter *adapter;
	struct pvscsi_adapter adapter_temp;
	struct Scsi_Host *host = NULL;
	unsigned int i;
	int error;
	u32 max_id;

	error = -ENODEV;

	if (pci_enable_device(pdev))
		return error;

	if (!dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		printk(KERN_INFO "vmw_pvscsi: using 64bit dma\n");
	} else if (!dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))) {
		printk(KERN_INFO "vmw_pvscsi: using 32bit dma\n");
	} else {
		printk(KERN_ERR "vmw_pvscsi: failed to set DMA mask\n");
		goto out_disable_device;
	}

	/*
	 * Let's use a temp pvscsi_adapter struct until we find the number of
	 * targets on the adapter, after that we will switch to the real
	 * allocated struct.
	 */
	adapter = &adapter_temp;
	memset(adapter, 0, sizeof(*adapter));
	adapter->dev  = pdev;
	adapter->rev = pdev->revision;

	if (pci_request_regions(pdev, "vmw_pvscsi")) {
		printk(KERN_ERR "vmw_pvscsi: pci memory selection failed\n");
		goto out_disable_device;
	}

	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		if ((pci_resource_flags(pdev, i) & PCI_BASE_ADDRESS_SPACE_IO))
			continue;

		if (pci_resource_len(pdev, i) < PVSCSI_MEM_SPACE_SIZE)
			continue;

		break;
	}

	if (i == DEVICE_COUNT_RESOURCE) {
		printk(KERN_ERR
		       "vmw_pvscsi: adapter has no suitable MMIO region\n");
		goto out_release_resources_and_disable;
	}

	adapter->mmioBase = pci_iomap(pdev, i, PVSCSI_MEM_SPACE_SIZE);

	if (!adapter->mmioBase) {
		printk(KERN_ERR
		       "vmw_pvscsi: can't iomap for BAR %d memsize %lu\n",
		       i, PVSCSI_MEM_SPACE_SIZE);
		goto out_release_resources_and_disable;
	}

	pci_set_master(pdev);

	/*
	 * Ask the device for max number of targets before deciding the
	 * default pvscsi_ring_pages value.
	 */
	max_id = pvscsi_get_max_targets(adapter);
	printk(KERN_INFO "vmw_pvscsi: max_id: %u\n", max_id);

	if (pvscsi_ring_pages == 0)
		/*
		 * Set the right default value. Up to 16 it is 8, above it is
		 * max.
		 */
		pvscsi_ring_pages = (max_id > 16) ?
			PVSCSI_SETUP_RINGS_MAX_NUM_PAGES :
			PVSCSI_DEFAULT_NUM_PAGES_PER_RING;
	printk(KERN_INFO
	       "vmw_pvscsi: setting ring_pages to %d\n",
	       pvscsi_ring_pages);

	pvscsi_template.can_queue =
		min(PVSCSI_MAX_NUM_PAGES_REQ_RING, pvscsi_ring_pages) *
		PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE;
	pvscsi_template.cmd_per_lun =
		min(pvscsi_template.can_queue, pvscsi_cmd_per_lun);
	host = scsi_host_alloc(&pvscsi_template, sizeof(struct pvscsi_adapter));
	if (!host) {
		printk(KERN_ERR "vmw_pvscsi: failed to allocate host\n");
		goto out_release_resources_and_disable;
	}

	/*
	 * Let's use the real pvscsi_adapter struct here onwards.
	 */
	adapter = shost_priv(host);
	memset(adapter, 0, sizeof(*adapter));
	adapter->dev  = pdev;
	adapter->host = host;
	/*
	 * Copy back what we already have to the allocated adapter struct.
	 */
	adapter->rev = adapter_temp.rev;
	adapter->mmioBase = adapter_temp.mmioBase;

	spin_lock_init(&adapter->hw_lock);
	host->max_channel = 0;
	host->max_lun     = 1;
	host->max_cmd_len = 16;
	host->max_id      = max_id;

	pci_set_drvdata(pdev, host);

	ll_adapter_reset(adapter);

	adapter->use_msg = pvscsi_setup_msg_workqueue(adapter);

	error = pvscsi_allocate_rings(adapter);
	if (error) {
		printk(KERN_ERR "vmw_pvscsi: unable to allocate ring memory\n");
		goto out_release_resources;
	}

	/*
	 * From this point on we should reset the adapter if anything goes
	 * wrong.
	 */
	pvscsi_setup_all_rings(adapter);

	adapter->cmd_map = kcalloc(adapter->req_depth,
				   sizeof(struct pvscsi_ctx), GFP_KERNEL);
	if (!adapter->cmd_map) {
		printk(KERN_ERR "vmw_pvscsi: failed to allocate memory.\n");
		error = -ENOMEM;
		goto out_reset_adapter;
	}

	INIT_LIST_HEAD(&adapter->cmd_pool);
	for (i = 0; i < adapter->req_depth; i++) {
		struct pvscsi_ctx *ctx = adapter->cmd_map + i;
		list_add(&ctx->list, &adapter->cmd_pool);
	}

	error = pvscsi_allocate_sg(adapter);
	if (error) {
		printk(KERN_ERR "vmw_pvscsi: unable to allocate s/g table\n");
		goto out_reset_adapter;
	}

	if (pvscsi_disable_msix)
		irq_flag &= ~PCI_IRQ_MSIX;
	if (pvscsi_disable_msi)
		irq_flag &= ~PCI_IRQ_MSI;

	error = pci_alloc_irq_vectors(adapter->dev, 1, 1, irq_flag);
	if (error < 0)
		goto out_reset_adapter;

	adapter->use_req_threshold = pvscsi_setup_req_threshold(adapter, true);
	printk(KERN_DEBUG "vmw_pvscsi: driver-based request coalescing %sabled\n",
	       adapter->use_req_threshold ? "en" : "dis");

	if (adapter->dev->msix_enabled || adapter->dev->msi_enabled) {
		printk(KERN_INFO "vmw_pvscsi: using MSI%s\n",
			adapter->dev->msix_enabled ? "-X" : "");
		error = request_irq(pci_irq_vector(pdev, 0), pvscsi_isr,
				0, "vmw_pvscsi", adapter);
	} else {
		printk(KERN_INFO "vmw_pvscsi: using INTx\n");
		error = request_irq(pci_irq_vector(pdev, 0), pvscsi_shared_isr,
				IRQF_SHARED, "vmw_pvscsi", adapter);
	}

	if (error) {
		printk(KERN_ERR
		       "vmw_pvscsi: unable to request IRQ: %d\n", error);
		goto out_reset_adapter;
	}

	error = scsi_add_host(host, &pdev->dev);
	if (error) {
		printk(KERN_ERR
		       "vmw_pvscsi: scsi_add_host failed: %d\n", error);
		goto out_reset_adapter;
	}

	dev_info(&pdev->dev, "VMware PVSCSI rev %d host #%u\n",
		 adapter->rev, host->host_no);

	pvscsi_unmask_intr(adapter);

	scsi_scan_host(host);

	return 0;

out_reset_adapter:
	ll_adapter_reset(adapter);
out_release_resources:
	pvscsi_shutdown_intr(adapter);
	pvscsi_release_resources(adapter);
	scsi_host_put(host);
out_disable_device:
	pci_disable_device(pdev);

	return error;

out_release_resources_and_disable:
	pvscsi_shutdown_intr(adapter);
	pvscsi_release_resources(adapter);
	goto out_disable_device;
}

static void __pvscsi_shutdown(struct pvscsi_adapter *adapter)
{
	pvscsi_mask_intr(adapter);

	if (adapter->workqueue)
		flush_workqueue(adapter->workqueue);

	pvscsi_shutdown_intr(adapter);

	pvscsi_process_request_ring(adapter);
	pvscsi_process_completion_ring(adapter);
	ll_adapter_reset(adapter);
}

static void pvscsi_shutdown(struct pci_dev *dev)
{
	struct Scsi_Host *host = pci_get_drvdata(dev);
	struct pvscsi_adapter *adapter = shost_priv(host);

	__pvscsi_shutdown(adapter);
}

static void pvscsi_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct pvscsi_adapter *adapter = shost_priv(host);

	scsi_remove_host(host);

	__pvscsi_shutdown(adapter);
	pvscsi_release_resources(adapter);

	scsi_host_put(host);

	pci_disable_device(pdev);
}

static struct pci_driver pvscsi_pci_driver = {
	.name		= "vmw_pvscsi",
	.id_table	= pvscsi_pci_tbl,
	.probe		= pvscsi_probe,
	.remove		= pvscsi_remove,
	.shutdown       = pvscsi_shutdown,
};

static int __init pvscsi_init(void)
{
	pr_info("%s - version %s\n",
		PVSCSI_LINUX_DRIVER_DESC, PVSCSI_DRIVER_VERSION_STRING);
	return pci_register_driver(&pvscsi_pci_driver);
}

static void __exit pvscsi_exit(void)
{
	pci_unregister_driver(&pvscsi_pci_driver);
}

module_init(pvscsi_init);
module_exit(pvscsi_exit);
