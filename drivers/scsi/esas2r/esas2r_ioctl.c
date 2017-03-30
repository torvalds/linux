/*
 *  linux/drivers/scsi/esas2r/esas2r_ioctl.c
 *      For use with ATTO ExpressSAS R6xx SAS/SATA RAID controllers
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.
 *
 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include "esas2r.h"

/*
 * Buffered ioctl handlers.  A buffered ioctl is one which requires that we
 * allocate a DMA-able memory area to communicate with the firmware.  In
 * order to prevent continually allocating and freeing consistent memory,
 * we will allocate a global buffer the first time we need it and re-use
 * it for subsequent ioctl calls that require it.
 */

u8 *esas2r_buffered_ioctl;
dma_addr_t esas2r_buffered_ioctl_addr;
u32 esas2r_buffered_ioctl_size;
struct pci_dev *esas2r_buffered_ioctl_pcid;

static DEFINE_SEMAPHORE(buffered_ioctl_semaphore);
typedef int (*BUFFERED_IOCTL_CALLBACK)(struct esas2r_adapter *,
				       struct esas2r_request *,
				       struct esas2r_sg_context *,
				       void *);
typedef void (*BUFFERED_IOCTL_DONE_CALLBACK)(struct esas2r_adapter *,
					     struct esas2r_request *, void *);

struct esas2r_buffered_ioctl {
	struct esas2r_adapter *a;
	void *ioctl;
	u32 length;
	u32 control_code;
	u32 offset;
	BUFFERED_IOCTL_CALLBACK
		callback;
	void *context;
	BUFFERED_IOCTL_DONE_CALLBACK
		done_callback;
	void *done_context;

};

static void complete_fm_api_req(struct esas2r_adapter *a,
				struct esas2r_request *rq)
{
	a->fm_api_command_done = 1;
	wake_up_interruptible(&a->fm_api_waiter);
}

/* Callbacks for building scatter/gather lists for FM API requests */
static u32 get_physaddr_fm_api(struct esas2r_sg_context *sgc, u64 *addr)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)sgc->adapter;
	int offset = sgc->cur_offset - a->save_offset;

	(*addr) = a->firmware.phys + offset;
	return a->firmware.orig_len - offset;
}

static u32 get_physaddr_fm_api_header(struct esas2r_sg_context *sgc, u64 *addr)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)sgc->adapter;
	int offset = sgc->cur_offset - a->save_offset;

	(*addr) = a->firmware.header_buff_phys + offset;
	return sizeof(struct esas2r_flash_img) - offset;
}

/* Handle EXPRESS_IOCTL_RW_FIRMWARE ioctl with img_type = FW_IMG_FM_API. */
static void do_fm_api(struct esas2r_adapter *a, struct esas2r_flash_img *fi)
{
	struct esas2r_request *rq;

	if (down_interruptible(&a->fm_api_semaphore)) {
		fi->status = FI_STAT_BUSY;
		return;
	}

	rq = esas2r_alloc_request(a);
	if (rq == NULL) {
		fi->status = FI_STAT_BUSY;
		goto free_sem;
	}

	if (fi == &a->firmware.header) {
		a->firmware.header_buff = dma_alloc_coherent(&a->pcid->dev,
							     (size_t)sizeof(
								     struct
								     esas2r_flash_img),
							     (dma_addr_t *)&a->
							     firmware.
							     header_buff_phys,
							     GFP_KERNEL);

		if (a->firmware.header_buff == NULL) {
			esas2r_debug("failed to allocate header buffer!");
			fi->status = FI_STAT_BUSY;
			goto free_req;
		}

		memcpy(a->firmware.header_buff, fi,
		       sizeof(struct esas2r_flash_img));
		a->save_offset = a->firmware.header_buff;
		a->fm_api_sgc.get_phys_addr =
			(PGETPHYSADDR)get_physaddr_fm_api_header;
	} else {
		a->save_offset = (u8 *)fi;
		a->fm_api_sgc.get_phys_addr =
			(PGETPHYSADDR)get_physaddr_fm_api;
	}

	rq->comp_cb = complete_fm_api_req;
	a->fm_api_command_done = 0;
	a->fm_api_sgc.cur_offset = a->save_offset;

	if (!esas2r_fm_api(a, (struct esas2r_flash_img *)a->save_offset, rq,
			   &a->fm_api_sgc))
		goto all_done;

	/* Now wait around for it to complete. */
	while (!a->fm_api_command_done)
		wait_event_interruptible(a->fm_api_waiter,
					 a->fm_api_command_done);
all_done:
	if (fi == &a->firmware.header) {
		memcpy(fi, a->firmware.header_buff,
		       sizeof(struct esas2r_flash_img));

		dma_free_coherent(&a->pcid->dev,
				  (size_t)sizeof(struct esas2r_flash_img),
				  a->firmware.header_buff,
				  (dma_addr_t)a->firmware.header_buff_phys);
	}
free_req:
	esas2r_free_request(a, (struct esas2r_request *)rq);
free_sem:
	up(&a->fm_api_semaphore);
	return;

}

static void complete_nvr_req(struct esas2r_adapter *a,
			     struct esas2r_request *rq)
{
	a->nvram_command_done = 1;
	wake_up_interruptible(&a->nvram_waiter);
}

/* Callback for building scatter/gather lists for buffered ioctls */
static u32 get_physaddr_buffered_ioctl(struct esas2r_sg_context *sgc,
				       u64 *addr)
{
	int offset = (u8 *)sgc->cur_offset - esas2r_buffered_ioctl;

	(*addr) = esas2r_buffered_ioctl_addr + offset;
	return esas2r_buffered_ioctl_size - offset;
}

static void complete_buffered_ioctl_req(struct esas2r_adapter *a,
					struct esas2r_request *rq)
{
	a->buffered_ioctl_done = 1;
	wake_up_interruptible(&a->buffered_ioctl_waiter);
}

static u8 handle_buffered_ioctl(struct esas2r_buffered_ioctl *bi)
{
	struct esas2r_adapter *a = bi->a;
	struct esas2r_request *rq;
	struct esas2r_sg_context sgc;
	u8 result = IOCTL_SUCCESS;

	if (down_interruptible(&buffered_ioctl_semaphore))
		return IOCTL_OUT_OF_RESOURCES;

	/* allocate a buffer or use the existing buffer. */
	if (esas2r_buffered_ioctl) {
		if (esas2r_buffered_ioctl_size < bi->length) {
			/* free the too-small buffer and get a new one */
			dma_free_coherent(&a->pcid->dev,
					  (size_t)esas2r_buffered_ioctl_size,
					  esas2r_buffered_ioctl,
					  esas2r_buffered_ioctl_addr);

			goto allocate_buffer;
		}
	} else {
allocate_buffer:
		esas2r_buffered_ioctl_size = bi->length;
		esas2r_buffered_ioctl_pcid = a->pcid;
		esas2r_buffered_ioctl = dma_alloc_coherent(&a->pcid->dev,
							   (size_t)
							   esas2r_buffered_ioctl_size,
							   &
							   esas2r_buffered_ioctl_addr,
							   GFP_KERNEL);
	}

	if (!esas2r_buffered_ioctl) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "could not allocate %d bytes of consistent memory "
			   "for a buffered ioctl!",
			   bi->length);

		esas2r_debug("buffered ioctl alloc failure");
		result = IOCTL_OUT_OF_RESOURCES;
		goto exit_cleanly;
	}

	memcpy(esas2r_buffered_ioctl, bi->ioctl, bi->length);

	rq = esas2r_alloc_request(a);
	if (rq == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "could not allocate an internal request");

		result = IOCTL_OUT_OF_RESOURCES;
		esas2r_debug("buffered ioctl - no requests");
		goto exit_cleanly;
	}

	a->buffered_ioctl_done = 0;
	rq->comp_cb = complete_buffered_ioctl_req;
	sgc.cur_offset = esas2r_buffered_ioctl + bi->offset;
	sgc.get_phys_addr = (PGETPHYSADDR)get_physaddr_buffered_ioctl;
	sgc.length = esas2r_buffered_ioctl_size;

	if (!(*bi->callback)(a, rq, &sgc, bi->context)) {
		/* completed immediately, no need to wait */
		a->buffered_ioctl_done = 0;
		goto free_andexit_cleanly;
	}

	/* now wait around for it to complete. */
	while (!a->buffered_ioctl_done)
		wait_event_interruptible(a->buffered_ioctl_waiter,
					 a->buffered_ioctl_done);

free_andexit_cleanly:
	if (result == IOCTL_SUCCESS && bi->done_callback)
		(*bi->done_callback)(a, rq, bi->done_context);

	esas2r_free_request(a, rq);

exit_cleanly:
	if (result == IOCTL_SUCCESS)
		memcpy(bi->ioctl, esas2r_buffered_ioctl, bi->length);

	up(&buffered_ioctl_semaphore);
	return result;
}

/* SMP ioctl support */
static int smp_ioctl_callback(struct esas2r_adapter *a,
			      struct esas2r_request *rq,
			      struct esas2r_sg_context *sgc, void *context)
{
	struct atto_ioctl_smp *si =
		(struct atto_ioctl_smp *)esas2r_buffered_ioctl;

	esas2r_sgc_init(sgc, a, rq, rq->vrq->ioctl.sge);
	esas2r_build_ioctl_req(a, rq, sgc->length, VDA_IOCTL_SMP);

	if (!esas2r_build_sg_list(a, rq, sgc)) {
		si->status = ATTO_STS_OUT_OF_RSRC;
		return false;
	}

	esas2r_start_request(a, rq);
	return true;
}

static u8 handle_smp_ioctl(struct esas2r_adapter *a, struct atto_ioctl_smp *si)
{
	struct esas2r_buffered_ioctl bi;

	memset(&bi, 0, sizeof(bi));

	bi.a = a;
	bi.ioctl = si;
	bi.length = sizeof(struct atto_ioctl_smp)
		    + le32_to_cpu(si->req_length)
		    + le32_to_cpu(si->rsp_length);
	bi.offset = 0;
	bi.callback = smp_ioctl_callback;
	return handle_buffered_ioctl(&bi);
}


/* CSMI ioctl support */
static void esas2r_csmi_ioctl_tunnel_comp_cb(struct esas2r_adapter *a,
					     struct esas2r_request *rq)
{
	rq->target_id = le16_to_cpu(rq->func_rsp.ioctl_rsp.csmi.target_id);
	rq->vrq->scsi.flags |= cpu_to_le32(rq->func_rsp.ioctl_rsp.csmi.lun);

	/* Now call the original completion callback. */
	(*rq->aux_req_cb)(a, rq);
}

/* Tunnel a CSMI IOCTL to the back end driver for processing. */
static bool csmi_ioctl_tunnel(struct esas2r_adapter *a,
			      union atto_ioctl_csmi *ci,
			      struct esas2r_request *rq,
			      struct esas2r_sg_context *sgc,
			      u32 ctrl_code,
			      u16 target_id)
{
	struct atto_vda_ioctl_req *ioctl = &rq->vrq->ioctl;

	if (test_bit(AF_DEGRADED_MODE, &a->flags))
		return false;

	esas2r_sgc_init(sgc, a, rq, rq->vrq->ioctl.sge);
	esas2r_build_ioctl_req(a, rq, sgc->length, VDA_IOCTL_CSMI);
	ioctl->csmi.ctrl_code = cpu_to_le32(ctrl_code);
	ioctl->csmi.target_id = cpu_to_le16(target_id);
	ioctl->csmi.lun = (u8)le32_to_cpu(rq->vrq->scsi.flags);

	/*
	 * Always usurp the completion callback since the interrupt callback
	 * mechanism may be used.
	 */
	rq->aux_req_cx = ci;
	rq->aux_req_cb = rq->comp_cb;
	rq->comp_cb = esas2r_csmi_ioctl_tunnel_comp_cb;

	if (!esas2r_build_sg_list(a, rq, sgc))
		return false;

	esas2r_start_request(a, rq);
	return true;
}

static bool check_lun(struct scsi_lun lun)
{
	bool result;

	result = ((lun.scsi_lun[7] == 0) &&
		  (lun.scsi_lun[6] == 0) &&
		  (lun.scsi_lun[5] == 0) &&
		  (lun.scsi_lun[4] == 0) &&
		  (lun.scsi_lun[3] == 0) &&
		  (lun.scsi_lun[2] == 0) &&
/* Byte 1 is intentionally skipped */
		  (lun.scsi_lun[0] == 0));

	return result;
}

static int csmi_ioctl_callback(struct esas2r_adapter *a,
			       struct esas2r_request *rq,
			       struct esas2r_sg_context *sgc, void *context)
{
	struct atto_csmi *ci = (struct atto_csmi *)context;
	union atto_ioctl_csmi *ioctl_csmi =
		(union atto_ioctl_csmi *)esas2r_buffered_ioctl;
	u8 path = 0;
	u8 tid = 0;
	u8 lun = 0;
	u32 sts = CSMI_STS_SUCCESS;
	struct esas2r_target *t;
	unsigned long flags;

	if (ci->control_code == CSMI_CC_GET_DEV_ADDR) {
		struct atto_csmi_get_dev_addr *gda = &ci->data.dev_addr;

		path = gda->path_id;
		tid = gda->target_id;
		lun = gda->lun;
	} else if (ci->control_code == CSMI_CC_TASK_MGT) {
		struct atto_csmi_task_mgmt *tm = &ci->data.tsk_mgt;

		path = tm->path_id;
		tid = tm->target_id;
		lun = tm->lun;
	}

	if (path > 0) {
		rq->func_rsp.ioctl_rsp.csmi.csmi_status = cpu_to_le32(
			CSMI_STS_INV_PARAM);
		return false;
	}

	rq->target_id = tid;
	rq->vrq->scsi.flags |= cpu_to_le32(lun);

	switch (ci->control_code) {
	case CSMI_CC_GET_DRVR_INFO:
	{
		struct atto_csmi_get_driver_info *gdi = &ioctl_csmi->drvr_info;

		strcpy(gdi->description, esas2r_get_model_name(a));
		gdi->csmi_major_rev = CSMI_MAJOR_REV;
		gdi->csmi_minor_rev = CSMI_MINOR_REV;
		break;
	}

	case CSMI_CC_GET_CNTLR_CFG:
	{
		struct atto_csmi_get_cntlr_cfg *gcc = &ioctl_csmi->cntlr_cfg;

		gcc->base_io_addr = 0;
		pci_read_config_dword(a->pcid, PCI_BASE_ADDRESS_2,
				      &gcc->base_memaddr_lo);
		pci_read_config_dword(a->pcid, PCI_BASE_ADDRESS_3,
				      &gcc->base_memaddr_hi);
		gcc->board_id = MAKEDWORD(a->pcid->subsystem_device,
					  a->pcid->subsystem_vendor);
		gcc->slot_num = CSMI_SLOT_NUM_UNKNOWN;
		gcc->cntlr_class = CSMI_CNTLR_CLASS_HBA;
		gcc->io_bus_type = CSMI_BUS_TYPE_PCI;
		gcc->pci_addr.bus_num = a->pcid->bus->number;
		gcc->pci_addr.device_num = PCI_SLOT(a->pcid->devfn);
		gcc->pci_addr.function_num = PCI_FUNC(a->pcid->devfn);

		memset(gcc->serial_num, 0, sizeof(gcc->serial_num));

		gcc->major_rev = LOBYTE(LOWORD(a->fw_version));
		gcc->minor_rev = HIBYTE(LOWORD(a->fw_version));
		gcc->build_rev = LOBYTE(HIWORD(a->fw_version));
		gcc->release_rev = HIBYTE(HIWORD(a->fw_version));
		gcc->bios_major_rev = HIBYTE(HIWORD(a->flash_ver));
		gcc->bios_minor_rev = LOBYTE(HIWORD(a->flash_ver));
		gcc->bios_build_rev = LOWORD(a->flash_ver);

		if (test_bit(AF2_THUNDERLINK, &a->flags2))
			gcc->cntlr_flags = CSMI_CNTLRF_SAS_HBA
					   | CSMI_CNTLRF_SATA_HBA;
		else
			gcc->cntlr_flags = CSMI_CNTLRF_SAS_RAID
					   | CSMI_CNTLRF_SATA_RAID;

		gcc->rrom_major_rev = 0;
		gcc->rrom_minor_rev = 0;
		gcc->rrom_build_rev = 0;
		gcc->rrom_release_rev = 0;
		gcc->rrom_biosmajor_rev = 0;
		gcc->rrom_biosminor_rev = 0;
		gcc->rrom_biosbuild_rev = 0;
		gcc->rrom_biosrelease_rev = 0;
		break;
	}

	case CSMI_CC_GET_CNTLR_STS:
	{
		struct atto_csmi_get_cntlr_sts *gcs = &ioctl_csmi->cntlr_sts;

		if (test_bit(AF_DEGRADED_MODE, &a->flags))
			gcs->status = CSMI_CNTLR_STS_FAILED;
		else
			gcs->status = CSMI_CNTLR_STS_GOOD;

		gcs->offline_reason = CSMI_OFFLINE_NO_REASON;
		break;
	}

	case CSMI_CC_FW_DOWNLOAD:
	case CSMI_CC_GET_RAID_INFO:
	case CSMI_CC_GET_RAID_CFG:

		sts = CSMI_STS_BAD_CTRL_CODE;
		break;

	case CSMI_CC_SMP_PASSTHRU:
	case CSMI_CC_SSP_PASSTHRU:
	case CSMI_CC_STP_PASSTHRU:
	case CSMI_CC_GET_PHY_INFO:
	case CSMI_CC_SET_PHY_INFO:
	case CSMI_CC_GET_LINK_ERRORS:
	case CSMI_CC_GET_SATA_SIG:
	case CSMI_CC_GET_CONN_INFO:
	case CSMI_CC_PHY_CTRL:

		if (!csmi_ioctl_tunnel(a, ioctl_csmi, rq, sgc,
				       ci->control_code,
				       ESAS2R_TARG_ID_INV)) {
			sts = CSMI_STS_FAILED;
			break;
		}

		return true;

	case CSMI_CC_GET_SCSI_ADDR:
	{
		struct atto_csmi_get_scsi_addr *gsa = &ioctl_csmi->scsi_addr;

		struct scsi_lun lun;

		memcpy(&lun, gsa->sas_lun, sizeof(struct scsi_lun));

		if (!check_lun(lun)) {
			sts = CSMI_STS_NO_SCSI_ADDR;
			break;
		}

		/* make sure the device is present */
		spin_lock_irqsave(&a->mem_lock, flags);
		t = esas2r_targ_db_find_by_sas_addr(a, (u64 *)gsa->sas_addr);
		spin_unlock_irqrestore(&a->mem_lock, flags);

		if (t == NULL) {
			sts = CSMI_STS_NO_SCSI_ADDR;
			break;
		}

		gsa->host_index = 0xFF;
		gsa->lun = gsa->sas_lun[1];
		rq->target_id = esas2r_targ_get_id(t, a);
		break;
	}

	case CSMI_CC_GET_DEV_ADDR:
	{
		struct atto_csmi_get_dev_addr *gda = &ioctl_csmi->dev_addr;

		/* make sure the target is present */
		t = a->targetdb + rq->target_id;

		if (t >= a->targetdb_end
		    || t->target_state != TS_PRESENT
		    || t->sas_addr == 0) {
			sts = CSMI_STS_NO_DEV_ADDR;
			break;
		}

		/* fill in the result */
		*(u64 *)gda->sas_addr = t->sas_addr;
		memset(gda->sas_lun, 0, sizeof(gda->sas_lun));
		gda->sas_lun[1] = (u8)le32_to_cpu(rq->vrq->scsi.flags);
		break;
	}

	case CSMI_CC_TASK_MGT:

		/* make sure the target is present */
		t = a->targetdb + rq->target_id;

		if (t >= a->targetdb_end
		    || t->target_state != TS_PRESENT
		    || !(t->flags & TF_PASS_THRU)) {
			sts = CSMI_STS_NO_DEV_ADDR;
			break;
		}

		if (!csmi_ioctl_tunnel(a, ioctl_csmi, rq, sgc,
				       ci->control_code,
				       t->phys_targ_id)) {
			sts = CSMI_STS_FAILED;
			break;
		}

		return true;

	default:

		sts = CSMI_STS_BAD_CTRL_CODE;
		break;
	}

	rq->func_rsp.ioctl_rsp.csmi.csmi_status = cpu_to_le32(sts);

	return false;
}


static void csmi_ioctl_done_callback(struct esas2r_adapter *a,
				     struct esas2r_request *rq, void *context)
{
	struct atto_csmi *ci = (struct atto_csmi *)context;
	union atto_ioctl_csmi *ioctl_csmi =
		(union atto_ioctl_csmi *)esas2r_buffered_ioctl;

	switch (ci->control_code) {
	case CSMI_CC_GET_DRVR_INFO:
	{
		struct atto_csmi_get_driver_info *gdi =
			&ioctl_csmi->drvr_info;

		strcpy(gdi->name, ESAS2R_VERSION_STR);

		gdi->major_rev = ESAS2R_MAJOR_REV;
		gdi->minor_rev = ESAS2R_MINOR_REV;
		gdi->build_rev = 0;
		gdi->release_rev = 0;
		break;
	}

	case CSMI_CC_GET_SCSI_ADDR:
	{
		struct atto_csmi_get_scsi_addr *gsa = &ioctl_csmi->scsi_addr;

		if (le32_to_cpu(rq->func_rsp.ioctl_rsp.csmi.csmi_status) ==
		    CSMI_STS_SUCCESS) {
			gsa->target_id = rq->target_id;
			gsa->path_id = 0;
		}

		break;
	}
	}

	ci->status = le32_to_cpu(rq->func_rsp.ioctl_rsp.csmi.csmi_status);
}


static u8 handle_csmi_ioctl(struct esas2r_adapter *a, struct atto_csmi *ci)
{
	struct esas2r_buffered_ioctl bi;

	memset(&bi, 0, sizeof(bi));

	bi.a = a;
	bi.ioctl = &ci->data;
	bi.length = sizeof(union atto_ioctl_csmi);
	bi.offset = 0;
	bi.callback = csmi_ioctl_callback;
	bi.context = ci;
	bi.done_callback = csmi_ioctl_done_callback;
	bi.done_context = ci;

	return handle_buffered_ioctl(&bi);
}

/* ATTO HBA ioctl support */

/* Tunnel an ATTO HBA IOCTL to the back end driver for processing. */
static bool hba_ioctl_tunnel(struct esas2r_adapter *a,
			     struct atto_ioctl *hi,
			     struct esas2r_request *rq,
			     struct esas2r_sg_context *sgc)
{
	esas2r_sgc_init(sgc, a, rq, rq->vrq->ioctl.sge);

	esas2r_build_ioctl_req(a, rq, sgc->length, VDA_IOCTL_HBA);

	if (!esas2r_build_sg_list(a, rq, sgc)) {
		hi->status = ATTO_STS_OUT_OF_RSRC;

		return false;
	}

	esas2r_start_request(a, rq);

	return true;
}

static void scsi_passthru_comp_cb(struct esas2r_adapter *a,
				  struct esas2r_request *rq)
{
	struct atto_ioctl *hi = (struct atto_ioctl *)rq->aux_req_cx;
	struct atto_hba_scsi_pass_thru *spt = &hi->data.scsi_pass_thru;
	u8 sts = ATTO_SPT_RS_FAILED;

	spt->scsi_status = rq->func_rsp.scsi_rsp.scsi_stat;
	spt->sense_length = rq->sense_len;
	spt->residual_length =
		le32_to_cpu(rq->func_rsp.scsi_rsp.residual_length);

	switch (rq->req_stat) {
	case RS_SUCCESS:
	case RS_SCSI_ERROR:
		sts = ATTO_SPT_RS_SUCCESS;
		break;
	case RS_UNDERRUN:
		sts = ATTO_SPT_RS_UNDERRUN;
		break;
	case RS_OVERRUN:
		sts = ATTO_SPT_RS_OVERRUN;
		break;
	case RS_SEL:
	case RS_SEL2:
		sts = ATTO_SPT_RS_NO_DEVICE;
		break;
	case RS_NO_LUN:
		sts = ATTO_SPT_RS_NO_LUN;
		break;
	case RS_TIMEOUT:
		sts = ATTO_SPT_RS_TIMEOUT;
		break;
	case RS_DEGRADED:
		sts = ATTO_SPT_RS_DEGRADED;
		break;
	case RS_BUSY:
		sts = ATTO_SPT_RS_BUSY;
		break;
	case RS_ABORTED:
		sts = ATTO_SPT_RS_ABORTED;
		break;
	case RS_RESET:
		sts = ATTO_SPT_RS_BUS_RESET;
		break;
	}

	spt->req_status = sts;

	/* Update the target ID to the next one present. */
	spt->target_id =
		esas2r_targ_db_find_next_present(a, (u16)spt->target_id);

	/* Done, call the completion callback. */
	(*rq->aux_req_cb)(a, rq);
}

static int hba_ioctl_callback(struct esas2r_adapter *a,
			      struct esas2r_request *rq,
			      struct esas2r_sg_context *sgc,
			      void *context)
{
	struct atto_ioctl *hi = (struct atto_ioctl *)esas2r_buffered_ioctl;

	hi->status = ATTO_STS_SUCCESS;

	switch (hi->function) {
	case ATTO_FUNC_GET_ADAP_INFO:
	{
		u8 *class_code = (u8 *)&a->pcid->class;

		struct atto_hba_get_adapter_info *gai =
			&hi->data.get_adap_info;
		int pcie_cap_reg;

		if (hi->flags & HBAF_TUNNEL) {
			hi->status = ATTO_STS_UNSUPPORTED;
			break;
		}

		if (hi->version > ATTO_VER_GET_ADAP_INFO0) {
			hi->status = ATTO_STS_INV_VERSION;
			hi->version = ATTO_VER_GET_ADAP_INFO0;
			break;
		}

		memset(gai, 0, sizeof(*gai));

		gai->pci.vendor_id = a->pcid->vendor;
		gai->pci.device_id = a->pcid->device;
		gai->pci.ss_vendor_id = a->pcid->subsystem_vendor;
		gai->pci.ss_device_id = a->pcid->subsystem_device;
		gai->pci.class_code[0] = class_code[0];
		gai->pci.class_code[1] = class_code[1];
		gai->pci.class_code[2] = class_code[2];
		gai->pci.rev_id = a->pcid->revision;
		gai->pci.bus_num = a->pcid->bus->number;
		gai->pci.dev_num = PCI_SLOT(a->pcid->devfn);
		gai->pci.func_num = PCI_FUNC(a->pcid->devfn);

		pcie_cap_reg = pci_find_capability(a->pcid, PCI_CAP_ID_EXP);
		if (pcie_cap_reg) {
			u16 stat;
			u32 caps;

			pci_read_config_word(a->pcid,
					     pcie_cap_reg + PCI_EXP_LNKSTA,
					     &stat);
			pci_read_config_dword(a->pcid,
					      pcie_cap_reg + PCI_EXP_LNKCAP,
					      &caps);

			gai->pci.link_speed_curr =
				(u8)(stat & PCI_EXP_LNKSTA_CLS);
			gai->pci.link_speed_max =
				(u8)(caps & PCI_EXP_LNKCAP_SLS);
			gai->pci.link_width_curr =
				(u8)((stat & PCI_EXP_LNKSTA_NLW)
				     >> PCI_EXP_LNKSTA_NLW_SHIFT);
			gai->pci.link_width_max =
				(u8)((caps & PCI_EXP_LNKCAP_MLW)
				     >> 4);
		}

		gai->pci.msi_vector_cnt = 1;

		if (a->pcid->msix_enabled)
			gai->pci.interrupt_mode = ATTO_GAI_PCIIM_MSIX;
		else if (a->pcid->msi_enabled)
			gai->pci.interrupt_mode = ATTO_GAI_PCIIM_MSI;
		else
			gai->pci.interrupt_mode = ATTO_GAI_PCIIM_LEGACY;

		gai->adap_type = ATTO_GAI_AT_ESASRAID2;

		if (test_bit(AF2_THUNDERLINK, &a->flags2))
			gai->adap_type = ATTO_GAI_AT_TLSASHBA;

		if (test_bit(AF_DEGRADED_MODE, &a->flags))
			gai->adap_flags |= ATTO_GAI_AF_DEGRADED;

		gai->adap_flags |= ATTO_GAI_AF_SPT_SUPP |
				   ATTO_GAI_AF_DEVADDR_SUPP;

		if (a->pcid->subsystem_device == ATTO_ESAS_R60F
		    || a->pcid->subsystem_device == ATTO_ESAS_R608
		    || a->pcid->subsystem_device == ATTO_ESAS_R644
		    || a->pcid->subsystem_device == ATTO_TSSC_3808E)
			gai->adap_flags |= ATTO_GAI_AF_VIRT_SES;

		gai->num_ports = ESAS2R_NUM_PHYS;
		gai->num_phys = ESAS2R_NUM_PHYS;

		strcpy(gai->firmware_rev, a->fw_rev);
		strcpy(gai->flash_rev, a->flash_rev);
		strcpy(gai->model_name_short, esas2r_get_model_name_short(a));
		strcpy(gai->model_name, esas2r_get_model_name(a));

		gai->num_targets = ESAS2R_MAX_TARGETS;

		gai->num_busses = 1;
		gai->num_targsper_bus = gai->num_targets;
		gai->num_lunsper_targ = 256;

		if (a->pcid->subsystem_device == ATTO_ESAS_R6F0
		    || a->pcid->subsystem_device == ATTO_ESAS_R60F)
			gai->num_connectors = 4;
		else
			gai->num_connectors = 2;

		gai->adap_flags2 |= ATTO_GAI_AF2_ADAP_CTRL_SUPP;

		gai->num_targets_backend = a->num_targets_backend;

		gai->tunnel_flags = a->ioctl_tunnel
				    & (ATTO_GAI_TF_MEM_RW
				       | ATTO_GAI_TF_TRACE
				       | ATTO_GAI_TF_SCSI_PASS_THRU
				       | ATTO_GAI_TF_GET_DEV_ADDR
				       | ATTO_GAI_TF_PHY_CTRL
				       | ATTO_GAI_TF_CONN_CTRL
				       | ATTO_GAI_TF_GET_DEV_INFO);
		break;
	}

	case ATTO_FUNC_GET_ADAP_ADDR:
	{
		struct atto_hba_get_adapter_address *gaa =
			&hi->data.get_adap_addr;

		if (hi->flags & HBAF_TUNNEL) {
			hi->status = ATTO_STS_UNSUPPORTED;
			break;
		}

		if (hi->version > ATTO_VER_GET_ADAP_ADDR0) {
			hi->status = ATTO_STS_INV_VERSION;
			hi->version = ATTO_VER_GET_ADAP_ADDR0;
		} else if (gaa->addr_type == ATTO_GAA_AT_PORT
			   || gaa->addr_type == ATTO_GAA_AT_NODE) {
			if (gaa->addr_type == ATTO_GAA_AT_PORT
			    && gaa->port_id >= ESAS2R_NUM_PHYS) {
				hi->status = ATTO_STS_NOT_APPL;
			} else {
				memcpy((u64 *)gaa->address,
				       &a->nvram->sas_addr[0], sizeof(u64));
				gaa->addr_len = sizeof(u64);
			}
		} else {
			hi->status = ATTO_STS_INV_PARAM;
		}

		break;
	}

	case ATTO_FUNC_MEM_RW:
	{
		if (hi->flags & HBAF_TUNNEL) {
			if (hba_ioctl_tunnel(a, hi, rq, sgc))
				return true;

			break;
		}

		hi->status = ATTO_STS_UNSUPPORTED;

		break;
	}

	case ATTO_FUNC_TRACE:
	{
		struct atto_hba_trace *trc = &hi->data.trace;

		if (hi->flags & HBAF_TUNNEL) {
			if (hba_ioctl_tunnel(a, hi, rq, sgc))
				return true;

			break;
		}

		if (hi->version > ATTO_VER_TRACE1) {
			hi->status = ATTO_STS_INV_VERSION;
			hi->version = ATTO_VER_TRACE1;
			break;
		}

		if (trc->trace_type == ATTO_TRC_TT_FWCOREDUMP
		    && hi->version >= ATTO_VER_TRACE1) {
			if (trc->trace_func == ATTO_TRC_TF_UPLOAD) {
				u32 len = hi->data_length;
				u32 offset = trc->current_offset;
				u32 total_len = ESAS2R_FWCOREDUMP_SZ;

				/* Size is zero if a core dump isn't present */
				if (!test_bit(AF2_COREDUMP_SAVED, &a->flags2))
					total_len = 0;

				if (len > total_len)
					len = total_len;

				if (offset >= total_len
				    || offset + len > total_len
				    || len == 0) {
					hi->status = ATTO_STS_INV_PARAM;
					break;
				}

				memcpy(trc + 1,
				       a->fw_coredump_buff + offset,
				       len);

				hi->data_length = len;
			} else if (trc->trace_func == ATTO_TRC_TF_RESET) {
				memset(a->fw_coredump_buff, 0,
				       ESAS2R_FWCOREDUMP_SZ);

				clear_bit(AF2_COREDUMP_SAVED, &a->flags2);
			} else if (trc->trace_func != ATTO_TRC_TF_GET_INFO) {
				hi->status = ATTO_STS_UNSUPPORTED;
				break;
			}

			/* Always return all the info we can. */
			trc->trace_mask = 0;
			trc->current_offset = 0;
			trc->total_length = ESAS2R_FWCOREDUMP_SZ;

			/* Return zero length buffer if core dump not present */
			if (!test_bit(AF2_COREDUMP_SAVED, &a->flags2))
				trc->total_length = 0;
		} else {
			hi->status = ATTO_STS_UNSUPPORTED;
		}

		break;
	}

	case ATTO_FUNC_SCSI_PASS_THRU:
	{
		struct atto_hba_scsi_pass_thru *spt = &hi->data.scsi_pass_thru;
		struct scsi_lun lun;

		memcpy(&lun, spt->lun, sizeof(struct scsi_lun));

		if (hi->flags & HBAF_TUNNEL) {
			if (hba_ioctl_tunnel(a, hi, rq, sgc))
				return true;

			break;
		}

		if (hi->version > ATTO_VER_SCSI_PASS_THRU0) {
			hi->status = ATTO_STS_INV_VERSION;
			hi->version = ATTO_VER_SCSI_PASS_THRU0;
			break;
		}

		if (spt->target_id >= ESAS2R_MAX_TARGETS || !check_lun(lun)) {
			hi->status = ATTO_STS_INV_PARAM;
			break;
		}

		esas2r_sgc_init(sgc, a, rq, NULL);

		sgc->length = hi->data_length;
		sgc->cur_offset += offsetof(struct atto_ioctl, data.byte)
				   + sizeof(struct atto_hba_scsi_pass_thru);

		/* Finish request initialization */
		rq->target_id = (u16)spt->target_id;
		rq->vrq->scsi.flags |= cpu_to_le32(spt->lun[1]);
		memcpy(rq->vrq->scsi.cdb, spt->cdb, 16);
		rq->vrq->scsi.length = cpu_to_le32(hi->data_length);
		rq->sense_len = spt->sense_length;
		rq->sense_buf = (u8 *)spt->sense_data;
		/* NOTE: we ignore spt->timeout */

		/*
		 * always usurp the completion callback since the interrupt
		 * callback mechanism may be used.
		 */

		rq->aux_req_cx = hi;
		rq->aux_req_cb = rq->comp_cb;
		rq->comp_cb = scsi_passthru_comp_cb;

		if (spt->flags & ATTO_SPTF_DATA_IN) {
			rq->vrq->scsi.flags |= cpu_to_le32(FCP_CMND_RDD);
		} else if (spt->flags & ATTO_SPTF_DATA_OUT) {
			rq->vrq->scsi.flags |= cpu_to_le32(FCP_CMND_WRD);
		} else {
			if (sgc->length) {
				hi->status = ATTO_STS_INV_PARAM;
				break;
			}
		}

		if (spt->flags & ATTO_SPTF_ORDERED_Q)
			rq->vrq->scsi.flags |=
				cpu_to_le32(FCP_CMND_TA_ORDRD_Q);
		else if (spt->flags & ATTO_SPTF_HEAD_OF_Q)
			rq->vrq->scsi.flags |= cpu_to_le32(FCP_CMND_TA_HEAD_Q);


		if (!esas2r_build_sg_list(a, rq, sgc)) {
			hi->status = ATTO_STS_OUT_OF_RSRC;
			break;
		}

		esas2r_start_request(a, rq);

		return true;
	}

	case ATTO_FUNC_GET_DEV_ADDR:
	{
		struct atto_hba_get_device_address *gda =
			&hi->data.get_dev_addr;
		struct esas2r_target *t;

		if (hi->flags & HBAF_TUNNEL) {
			if (hba_ioctl_tunnel(a, hi, rq, sgc))
				return true;

			break;
		}

		if (hi->version > ATTO_VER_GET_DEV_ADDR0) {
			hi->status = ATTO_STS_INV_VERSION;
			hi->version = ATTO_VER_GET_DEV_ADDR0;
			break;
		}

		if (gda->target_id >= ESAS2R_MAX_TARGETS) {
			hi->status = ATTO_STS_INV_PARAM;
			break;
		}

		t = a->targetdb + (u16)gda->target_id;

		if (t->target_state != TS_PRESENT) {
			hi->status = ATTO_STS_FAILED;
		} else if (gda->addr_type == ATTO_GDA_AT_PORT) {
			if (t->sas_addr == 0) {
				hi->status = ATTO_STS_UNSUPPORTED;
			} else {
				*(u64 *)gda->address = t->sas_addr;

				gda->addr_len = sizeof(u64);
			}
		} else if (gda->addr_type == ATTO_GDA_AT_NODE) {
			hi->status = ATTO_STS_NOT_APPL;
		} else {
			hi->status = ATTO_STS_INV_PARAM;
		}

		/* update the target ID to the next one present. */

		gda->target_id =
			esas2r_targ_db_find_next_present(a,
							 (u16)gda->target_id);
		break;
	}

	case ATTO_FUNC_PHY_CTRL:
	case ATTO_FUNC_CONN_CTRL:
	{
		if (hba_ioctl_tunnel(a, hi, rq, sgc))
			return true;

		break;
	}

	case ATTO_FUNC_ADAP_CTRL:
	{
		struct atto_hba_adap_ctrl *ac = &hi->data.adap_ctrl;

		if (hi->flags & HBAF_TUNNEL) {
			hi->status = ATTO_STS_UNSUPPORTED;
			break;
		}

		if (hi->version > ATTO_VER_ADAP_CTRL0) {
			hi->status = ATTO_STS_INV_VERSION;
			hi->version = ATTO_VER_ADAP_CTRL0;
			break;
		}

		if (ac->adap_func == ATTO_AC_AF_HARD_RST) {
			esas2r_reset_adapter(a);
		} else if (ac->adap_func != ATTO_AC_AF_GET_STATE) {
			hi->status = ATTO_STS_UNSUPPORTED;
			break;
		}

		if (test_bit(AF_CHPRST_NEEDED, &a->flags))
			ac->adap_state = ATTO_AC_AS_RST_SCHED;
		else if (test_bit(AF_CHPRST_PENDING, &a->flags))
			ac->adap_state = ATTO_AC_AS_RST_IN_PROG;
		else if (test_bit(AF_DISC_PENDING, &a->flags))
			ac->adap_state = ATTO_AC_AS_RST_DISC;
		else if (test_bit(AF_DISABLED, &a->flags))
			ac->adap_state = ATTO_AC_AS_DISABLED;
		else if (test_bit(AF_DEGRADED_MODE, &a->flags))
			ac->adap_state = ATTO_AC_AS_DEGRADED;
		else
			ac->adap_state = ATTO_AC_AS_OK;

		break;
	}

	case ATTO_FUNC_GET_DEV_INFO:
	{
		struct atto_hba_get_device_info *gdi = &hi->data.get_dev_info;
		struct esas2r_target *t;

		if (hi->flags & HBAF_TUNNEL) {
			if (hba_ioctl_tunnel(a, hi, rq, sgc))
				return true;

			break;
		}

		if (hi->version > ATTO_VER_GET_DEV_INFO0) {
			hi->status = ATTO_STS_INV_VERSION;
			hi->version = ATTO_VER_GET_DEV_INFO0;
			break;
		}

		if (gdi->target_id >= ESAS2R_MAX_TARGETS) {
			hi->status = ATTO_STS_INV_PARAM;
			break;
		}

		t = a->targetdb + (u16)gdi->target_id;

		/* update the target ID to the next one present. */

		gdi->target_id =
			esas2r_targ_db_find_next_present(a,
							 (u16)gdi->target_id);

		if (t->target_state != TS_PRESENT) {
			hi->status = ATTO_STS_FAILED;
			break;
		}

		hi->status = ATTO_STS_UNSUPPORTED;
		break;
	}

	default:

		hi->status = ATTO_STS_INV_FUNC;
		break;
	}

	return false;
}

static void hba_ioctl_done_callback(struct esas2r_adapter *a,
				    struct esas2r_request *rq, void *context)
{
	struct atto_ioctl *ioctl_hba =
		(struct atto_ioctl *)esas2r_buffered_ioctl;

	esas2r_debug("hba_ioctl_done_callback %d", a->index);

	if (ioctl_hba->function == ATTO_FUNC_GET_ADAP_INFO) {
		struct atto_hba_get_adapter_info *gai =
			&ioctl_hba->data.get_adap_info;

		esas2r_debug("ATTO_FUNC_GET_ADAP_INFO");

		gai->drvr_rev_major = ESAS2R_MAJOR_REV;
		gai->drvr_rev_minor = ESAS2R_MINOR_REV;

		strcpy(gai->drvr_rev_ascii, ESAS2R_VERSION_STR);
		strcpy(gai->drvr_name, ESAS2R_DRVR_NAME);

		gai->num_busses = 1;
		gai->num_targsper_bus = ESAS2R_MAX_ID + 1;
		gai->num_lunsper_targ = 1;
	}
}

u8 handle_hba_ioctl(struct esas2r_adapter *a,
		    struct atto_ioctl *ioctl_hba)
{
	struct esas2r_buffered_ioctl bi;

	memset(&bi, 0, sizeof(bi));

	bi.a = a;
	bi.ioctl = ioctl_hba;
	bi.length = sizeof(struct atto_ioctl) + ioctl_hba->data_length;
	bi.callback = hba_ioctl_callback;
	bi.context = NULL;
	bi.done_callback = hba_ioctl_done_callback;
	bi.done_context = NULL;
	bi.offset = 0;

	return handle_buffered_ioctl(&bi);
}


int esas2r_write_params(struct esas2r_adapter *a, struct esas2r_request *rq,
			struct esas2r_sas_nvram *data)
{
	int result = 0;

	a->nvram_command_done = 0;
	rq->comp_cb = complete_nvr_req;

	if (esas2r_nvram_write(a, rq, data)) {
		/* now wait around for it to complete. */
		while (!a->nvram_command_done)
			wait_event_interruptible(a->nvram_waiter,
						 a->nvram_command_done);
		;

		/* done, check the status. */
		if (rq->req_stat == RS_SUCCESS)
			result = 1;
	}
	return result;
}


/* This function only cares about ATTO-specific ioctls (atto_express_ioctl) */
int esas2r_ioctl_handler(void *hostdata, int cmd, void __user *arg)
{
	struct atto_express_ioctl *ioctl = NULL;
	struct esas2r_adapter *a;
	struct esas2r_request *rq;
	u16 code;
	int err;

	esas2r_log(ESAS2R_LOG_DEBG, "ioctl (%p, %x, %p)", hostdata, cmd, arg);

	if ((arg == NULL)
	    || (cmd < EXPRESS_IOCTL_MIN)
	    || (cmd > EXPRESS_IOCTL_MAX))
		return -ENOTSUPP;

	ioctl = memdup_user(arg, sizeof(struct atto_express_ioctl));
	if (IS_ERR(ioctl)) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "ioctl_handler access_ok failed for cmd %d, "
			   "address %p", cmd,
			   arg);
		return PTR_ERR(ioctl);
	}

	/* verify the signature */

	if (memcmp(ioctl->header.signature,
		   EXPRESS_IOCTL_SIGNATURE,
		   EXPRESS_IOCTL_SIGNATURE_SIZE) != 0) {
		esas2r_log(ESAS2R_LOG_WARN, "invalid signature");
		kfree(ioctl);

		return -ENOTSUPP;
	}

	/* assume success */

	ioctl->header.return_code = IOCTL_SUCCESS;
	err = 0;

	/*
	 * handle EXPRESS_IOCTL_GET_CHANNELS
	 * without paying attention to channel
	 */

	if (cmd == EXPRESS_IOCTL_GET_CHANNELS) {
		int i = 0, k = 0;

		ioctl->data.chanlist.num_channels = 0;

		while (i < MAX_ADAPTERS) {
			if (esas2r_adapters[i]) {
				ioctl->data.chanlist.num_channels++;
				ioctl->data.chanlist.channel[k] = i;
				k++;
			}
			i++;
		}

		goto ioctl_done;
	}

	/* get the channel */

	if (ioctl->header.channel == 0xFF) {
		a = (struct esas2r_adapter *)hostdata;
	} else {
		if (ioctl->header.channel >= MAX_ADAPTERS ||
			esas2r_adapters[ioctl->header.channel] == NULL) {
			ioctl->header.return_code = IOCTL_BAD_CHANNEL;
			esas2r_log(ESAS2R_LOG_WARN, "bad channel value");
			kfree(ioctl);

			return -ENOTSUPP;
		}
		a = esas2r_adapters[ioctl->header.channel];
	}

	switch (cmd) {
	case EXPRESS_IOCTL_RW_FIRMWARE:

		if (ioctl->data.fwrw.img_type == FW_IMG_FM_API) {
			err = esas2r_write_fw(a,
					      (char *)ioctl->data.fwrw.image,
					      0,
					      sizeof(struct
						     atto_express_ioctl));

			if (err >= 0) {
				err = esas2r_read_fw(a,
						     (char *)ioctl->data.fwrw.
						     image,
						     0,
						     sizeof(struct
							    atto_express_ioctl));
			}
		} else if (ioctl->data.fwrw.img_type == FW_IMG_FS_API) {
			err = esas2r_write_fs(a,
					      (char *)ioctl->data.fwrw.image,
					      0,
					      sizeof(struct
						     atto_express_ioctl));

			if (err >= 0) {
				err = esas2r_read_fs(a,
						     (char *)ioctl->data.fwrw.
						     image,
						     0,
						     sizeof(struct
							    atto_express_ioctl));
			}
		} else {
			ioctl->header.return_code = IOCTL_BAD_FLASH_IMGTYPE;
		}

		break;

	case EXPRESS_IOCTL_READ_PARAMS:

		memcpy(ioctl->data.prw.data_buffer, a->nvram,
		       sizeof(struct esas2r_sas_nvram));
		ioctl->data.prw.code = 1;
		break;

	case EXPRESS_IOCTL_WRITE_PARAMS:

		rq = esas2r_alloc_request(a);
		if (rq == NULL) {
			kfree(ioctl);
			esas2r_log(ESAS2R_LOG_WARN,
			   "could not allocate an internal request");
			return -ENOMEM;
		}

		code = esas2r_write_params(a, rq,
					   (struct esas2r_sas_nvram *)ioctl->data.prw.data_buffer);
		ioctl->data.prw.code = code;

		esas2r_free_request(a, rq);

		break;

	case EXPRESS_IOCTL_DEFAULT_PARAMS:

		esas2r_nvram_get_defaults(a,
					  (struct esas2r_sas_nvram *)ioctl->data.prw.data_buffer);
		ioctl->data.prw.code = 1;
		break;

	case EXPRESS_IOCTL_CHAN_INFO:

		ioctl->data.chaninfo.major_rev = ESAS2R_MAJOR_REV;
		ioctl->data.chaninfo.minor_rev = ESAS2R_MINOR_REV;
		ioctl->data.chaninfo.IRQ = a->pcid->irq;
		ioctl->data.chaninfo.device_id = a->pcid->device;
		ioctl->data.chaninfo.vendor_id = a->pcid->vendor;
		ioctl->data.chaninfo.ven_dev_id = a->pcid->subsystem_device;
		ioctl->data.chaninfo.revision_id = a->pcid->revision;
		ioctl->data.chaninfo.pci_bus = a->pcid->bus->number;
		ioctl->data.chaninfo.pci_dev_func = a->pcid->devfn;
		ioctl->data.chaninfo.core_rev = 0;
		ioctl->data.chaninfo.host_no = a->host->host_no;
		ioctl->data.chaninfo.hbaapi_rev = 0;
		break;

	case EXPRESS_IOCTL_SMP:
		ioctl->header.return_code = handle_smp_ioctl(a,
							     &ioctl->data.
							     ioctl_smp);
		break;

	case EXPRESS_CSMI:
		ioctl->header.return_code =
			handle_csmi_ioctl(a, &ioctl->data.csmi);
		break;

	case EXPRESS_IOCTL_HBA:
		ioctl->header.return_code = handle_hba_ioctl(a,
							     &ioctl->data.
							     ioctl_hba);
		break;

	case EXPRESS_IOCTL_VDA:
		err = esas2r_write_vda(a,
				       (char *)&ioctl->data.ioctl_vda,
				       0,
				       sizeof(struct atto_ioctl_vda) +
				       ioctl->data.ioctl_vda.data_length);

		if (err >= 0) {
			err = esas2r_read_vda(a,
					      (char *)&ioctl->data.ioctl_vda,
					      0,
					      sizeof(struct atto_ioctl_vda) +
					      ioctl->data.ioctl_vda.data_length);
		}




		break;

	case EXPRESS_IOCTL_GET_MOD_INFO:

		ioctl->data.modinfo.adapter = a;
		ioctl->data.modinfo.pci_dev = a->pcid;
		ioctl->data.modinfo.scsi_host = a->host;
		ioctl->data.modinfo.host_no = a->host->host_no;

		break;

	default:
		esas2r_debug("esas2r_ioctl invalid cmd %p!", cmd);
		ioctl->header.return_code = IOCTL_ERR_INVCMD;
	}

ioctl_done:

	if (err < 0) {
		esas2r_log(ESAS2R_LOG_WARN, "err %d on ioctl cmd %d", err,
			   cmd);

		switch (err) {
		case -ENOMEM:
		case -EBUSY:
			ioctl->header.return_code = IOCTL_OUT_OF_RESOURCES;
			break;

		case -ENOSYS:
		case -EINVAL:
			ioctl->header.return_code = IOCTL_INVALID_PARAM;
			break;

		default:
			ioctl->header.return_code = IOCTL_GENERAL_ERROR;
			break;
		}

	}

	/* Always copy the buffer back, if only to pick up the status */
	err = __copy_to_user(arg, ioctl, sizeof(struct atto_express_ioctl));
	if (err != 0) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "ioctl_handler copy_to_user didn't copy "
			   "everything (err %d, cmd %d)", err,
			   cmd);
		kfree(ioctl);

		return -EFAULT;
	}

	kfree(ioctl);

	return 0;
}

int esas2r_ioctl(struct scsi_device *sd, int cmd, void __user *arg)
{
	return esas2r_ioctl_handler(sd->host->hostdata, cmd, arg);
}

static void free_fw_buffers(struct esas2r_adapter *a)
{
	if (a->firmware.data) {
		dma_free_coherent(&a->pcid->dev,
				  (size_t)a->firmware.orig_len,
				  a->firmware.data,
				  (dma_addr_t)a->firmware.phys);

		a->firmware.data = NULL;
	}
}

static int allocate_fw_buffers(struct esas2r_adapter *a, u32 length)
{
	free_fw_buffers(a);

	a->firmware.orig_len = length;

	a->firmware.data = (u8 *)dma_alloc_coherent(&a->pcid->dev,
						    (size_t)length,
						    (dma_addr_t *)&a->firmware.
						    phys,
						    GFP_KERNEL);

	if (!a->firmware.data) {
		esas2r_debug("buffer alloc failed!");
		return 0;
	}

	return 1;
}

/* Handle a call to read firmware. */
int esas2r_read_fw(struct esas2r_adapter *a, char *buf, long off, int count)
{
	esas2r_trace_enter();
	/* if the cached header is a status, simply copy it over and return. */
	if (a->firmware.state == FW_STATUS_ST) {
		int size = min_t(int, count, sizeof(a->firmware.header));
		esas2r_trace_exit();
		memcpy(buf, &a->firmware.header, size);
		esas2r_debug("esas2r_read_fw: STATUS size %d", size);
		return size;
	}

	/*
	 * if the cached header is a command, do it if at
	 * offset 0, otherwise copy the pieces.
	 */

	if (a->firmware.state == FW_COMMAND_ST) {
		u32 length = a->firmware.header.length;
		esas2r_trace_exit();

		esas2r_debug("esas2r_read_fw: COMMAND length %d off %d",
			     length,
			     off);

		if (off == 0) {
			if (a->firmware.header.action == FI_ACT_UP) {
				if (!allocate_fw_buffers(a, length))
					return -ENOMEM;


				/* copy header over */

				memcpy(a->firmware.data,
				       &a->firmware.header,
				       sizeof(a->firmware.header));

				do_fm_api(a,
					  (struct esas2r_flash_img *)a->firmware.data);
			} else if (a->firmware.header.action == FI_ACT_UPSZ) {
				int size =
					min((int)count,
					    (int)sizeof(a->firmware.header));
				do_fm_api(a, &a->firmware.header);
				memcpy(buf, &a->firmware.header, size);
				esas2r_debug("FI_ACT_UPSZ size %d", size);
				return size;
			} else {
				esas2r_debug("invalid action %d",
					     a->firmware.header.action);
				return -ENOSYS;
			}
		}

		if (count + off > length)
			count = length - off;

		if (count < 0)
			return 0;

		if (!a->firmware.data) {
			esas2r_debug(
				"read: nonzero offset but no buffer available!");
			return -ENOMEM;
		}

		esas2r_debug("esas2r_read_fw: off %d count %d length %d ", off,
			     count,
			     length);

		memcpy(buf, &a->firmware.data[off], count);

		/* when done, release the buffer */

		if (length <= off + count) {
			esas2r_debug("esas2r_read_fw: freeing buffer!");

			free_fw_buffers(a);
		}

		return count;
	}

	esas2r_trace_exit();
	esas2r_debug("esas2r_read_fw: invalid firmware state %d",
		     a->firmware.state);

	return -EINVAL;
}

/* Handle a call to write firmware. */
int esas2r_write_fw(struct esas2r_adapter *a, const char *buf, long off,
		    int count)
{
	u32 length;

	if (off == 0) {
		struct esas2r_flash_img *header =
			(struct esas2r_flash_img *)buf;

		/* assume version 0 flash image */

		int min_size = sizeof(struct esas2r_flash_img_v0);

		a->firmware.state = FW_INVALID_ST;

		/* validate the version field first */

		if (count < 4
		    ||  header->fi_version > FI_VERSION_1) {
			esas2r_debug(
				"esas2r_write_fw: short header or invalid version");
			return -EINVAL;
		}

		/* See if its a version 1 flash image */

		if (header->fi_version == FI_VERSION_1)
			min_size = sizeof(struct esas2r_flash_img);

		/* If this is the start, the header must be full and valid. */
		if (count < min_size) {
			esas2r_debug("esas2r_write_fw: short header, aborting");
			return -EINVAL;
		}

		/* Make sure the size is reasonable. */
		length = header->length;

		if (length > 1024 * 1024) {
			esas2r_debug(
				"esas2r_write_fw: hosed, length %d  fi_version %d",
				length, header->fi_version);
			return -EINVAL;
		}

		/*
		 * If this is a write command, allocate memory because
		 * we have to cache everything. otherwise, just cache
		 * the header, because the read op will do the command.
		 */

		if (header->action == FI_ACT_DOWN) {
			if (!allocate_fw_buffers(a, length))
				return -ENOMEM;

			/*
			 * Store the command, so there is context on subsequent
			 * calls.
			 */
			memcpy(&a->firmware.header,
			       buf,
			       sizeof(*header));
		} else if (header->action == FI_ACT_UP
			   ||  header->action == FI_ACT_UPSZ) {
			/* Save the command, result will be picked up on read */
			memcpy(&a->firmware.header,
			       buf,
			       sizeof(*header));

			a->firmware.state = FW_COMMAND_ST;

			esas2r_debug(
				"esas2r_write_fw: COMMAND, count %d, action %d ",
				count, header->action);

			/*
			 * Pretend we took the whole buffer,
			 * so we don't get bothered again.
			 */

			return count;
		} else {
			esas2r_debug("esas2r_write_fw: invalid action %d ",
				     a->firmware.header.action);
			return -ENOSYS;
		}
	} else {
		length = a->firmware.header.length;
	}

	/*
	 * We only get here on a download command, regardless of offset.
	 * the chunks written by the system need to be cached, and when
	 * the final one arrives, issue the fmapi command.
	 */

	if (off + count > length)
		count = length - off;

	if (count > 0) {
		esas2r_debug("esas2r_write_fw: off %d count %d length %d", off,
			     count,
			     length);

		/*
		 * On a full upload, the system tries sending the whole buffer.
		 * there's nothing to do with it, so just drop it here, before
		 * trying to copy over into unallocated memory!
		 */
		if (a->firmware.header.action == FI_ACT_UP)
			return count;

		if (!a->firmware.data) {
			esas2r_debug(
				"write: nonzero offset but no buffer available!");
			return -ENOMEM;
		}

		memcpy(&a->firmware.data[off], buf, count);

		if (length == off + count) {
			do_fm_api(a,
				  (struct esas2r_flash_img *)a->firmware.data);

			/*
			 * Now copy the header result to be picked up by the
			 * next read
			 */
			memcpy(&a->firmware.header,
			       a->firmware.data,
			       sizeof(a->firmware.header));

			a->firmware.state = FW_STATUS_ST;

			esas2r_debug("write completed");

			/*
			 * Since the system has the data buffered, the only way
			 * this can leak is if a root user writes a program
			 * that writes a shorter buffer than it claims, and the
			 * copyin fails.
			 */
			free_fw_buffers(a);
		}
	}

	return count;
}

/* Callback for the completion of a VDA request. */
static void vda_complete_req(struct esas2r_adapter *a,
			     struct esas2r_request *rq)
{
	a->vda_command_done = 1;
	wake_up_interruptible(&a->vda_waiter);
}

/* Scatter/gather callback for VDA requests */
static u32 get_physaddr_vda(struct esas2r_sg_context *sgc, u64 *addr)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)sgc->adapter;
	int offset = (u8 *)sgc->cur_offset - (u8 *)a->vda_buffer;

	(*addr) = a->ppvda_buffer + offset;
	return VDA_MAX_BUFFER_SIZE - offset;
}

/* Handle a call to read a VDA command. */
int esas2r_read_vda(struct esas2r_adapter *a, char *buf, long off, int count)
{
	if (!a->vda_buffer)
		return -ENOMEM;

	if (off == 0) {
		struct esas2r_request *rq;
		struct atto_ioctl_vda *vi =
			(struct atto_ioctl_vda *)a->vda_buffer;
		struct esas2r_sg_context sgc;
		bool wait_for_completion;

		/*
		 * Presumeably, someone has already written to the vda_buffer,
		 * and now they are reading the node the response, so now we
		 * will actually issue the request to the chip and reply.
		 */

		/* allocate a request */
		rq = esas2r_alloc_request(a);
		if (rq == NULL) {
			esas2r_debug("esas2r_read_vda: out of requestss");
			return -EBUSY;
		}

		rq->comp_cb = vda_complete_req;

		sgc.first_req = rq;
		sgc.adapter = a;
		sgc.cur_offset = a->vda_buffer + VDA_BUFFER_HEADER_SZ;
		sgc.get_phys_addr = (PGETPHYSADDR)get_physaddr_vda;

		a->vda_command_done = 0;

		wait_for_completion =
			esas2r_process_vda_ioctl(a, vi, rq, &sgc);

		if (wait_for_completion) {
			/* now wait around for it to complete. */

			while (!a->vda_command_done)
				wait_event_interruptible(a->vda_waiter,
							 a->vda_command_done);
		}

		esas2r_free_request(a, (struct esas2r_request *)rq);
	}

	if (off > VDA_MAX_BUFFER_SIZE)
		return 0;

	if (count + off > VDA_MAX_BUFFER_SIZE)
		count = VDA_MAX_BUFFER_SIZE - off;

	if (count < 0)
		return 0;

	memcpy(buf, a->vda_buffer + off, count);

	return count;
}

/* Handle a call to write a VDA command. */
int esas2r_write_vda(struct esas2r_adapter *a, const char *buf, long off,
		     int count)
{
	/*
	 * allocate memory for it, if not already done.  once allocated,
	 * we will keep it around until the driver is unloaded.
	 */

	if (!a->vda_buffer) {
		dma_addr_t dma_addr;
		a->vda_buffer = (u8 *)dma_alloc_coherent(&a->pcid->dev,
							 (size_t)
							 VDA_MAX_BUFFER_SIZE,
							 &dma_addr,
							 GFP_KERNEL);

		a->ppvda_buffer = dma_addr;
	}

	if (!a->vda_buffer)
		return -ENOMEM;

	if (off > VDA_MAX_BUFFER_SIZE)
		return 0;

	if (count + off > VDA_MAX_BUFFER_SIZE)
		count = VDA_MAX_BUFFER_SIZE - off;

	if (count < 1)
		return 0;

	memcpy(a->vda_buffer + off, buf, count);

	return count;
}

/* Callback for the completion of an FS_API request.*/
static void fs_api_complete_req(struct esas2r_adapter *a,
				struct esas2r_request *rq)
{
	a->fs_api_command_done = 1;

	wake_up_interruptible(&a->fs_api_waiter);
}

/* Scatter/gather callback for VDA requests */
static u32 get_physaddr_fs_api(struct esas2r_sg_context *sgc, u64 *addr)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)sgc->adapter;
	struct esas2r_ioctl_fs *fs =
		(struct esas2r_ioctl_fs *)a->fs_api_buffer;
	u32 offset = (u8 *)sgc->cur_offset - (u8 *)fs;

	(*addr) = a->ppfs_api_buffer + offset;

	return a->fs_api_buffer_size - offset;
}

/* Handle a call to read firmware via FS_API. */
int esas2r_read_fs(struct esas2r_adapter *a, char *buf, long off, int count)
{
	if (!a->fs_api_buffer)
		return -ENOMEM;

	if (off == 0) {
		struct esas2r_request *rq;
		struct esas2r_sg_context sgc;
		struct esas2r_ioctl_fs *fs =
			(struct esas2r_ioctl_fs *)a->fs_api_buffer;

		/* If another flash request is already in progress, return. */
		if (down_interruptible(&a->fs_api_semaphore)) {
busy:
			fs->status = ATTO_STS_OUT_OF_RSRC;
			return -EBUSY;
		}

		/*
		 * Presumeably, someone has already written to the
		 * fs_api_buffer, and now they are reading the node the
		 * response, so now we will actually issue the request to the
		 * chip and reply. Allocate a request
		 */

		rq = esas2r_alloc_request(a);
		if (rq == NULL) {
			esas2r_debug("esas2r_read_fs: out of requests");
			up(&a->fs_api_semaphore);
			goto busy;
		}

		rq->comp_cb = fs_api_complete_req;

		/* Set up the SGCONTEXT for to build the s/g table */

		sgc.cur_offset = fs->data;
		sgc.get_phys_addr = (PGETPHYSADDR)get_physaddr_fs_api;

		a->fs_api_command_done = 0;

		if (!esas2r_process_fs_ioctl(a, fs, rq, &sgc)) {
			if (fs->status == ATTO_STS_OUT_OF_RSRC)
				count = -EBUSY;

			goto dont_wait;
		}

		/* Now wait around for it to complete. */

		while (!a->fs_api_command_done)
			wait_event_interruptible(a->fs_api_waiter,
						 a->fs_api_command_done);
		;
dont_wait:
		/* Free the request and keep going */
		up(&a->fs_api_semaphore);
		esas2r_free_request(a, (struct esas2r_request *)rq);

		/* Pick up possible error code from above */
		if (count < 0)
			return count;
	}

	if (off > a->fs_api_buffer_size)
		return 0;

	if (count + off > a->fs_api_buffer_size)
		count = a->fs_api_buffer_size - off;

	if (count < 0)
		return 0;

	memcpy(buf, a->fs_api_buffer + off, count);

	return count;
}

/* Handle a call to write firmware via FS_API. */
int esas2r_write_fs(struct esas2r_adapter *a, const char *buf, long off,
		    int count)
{
	if (off == 0) {
		struct esas2r_ioctl_fs *fs = (struct esas2r_ioctl_fs *)buf;
		u32 length = fs->command.length + offsetof(
			struct esas2r_ioctl_fs,
			data);

		/*
		 * Special case, for BEGIN commands, the length field
		 * is lying to us, so just get enough for the header.
		 */

		if (fs->command.command == ESAS2R_FS_CMD_BEGINW)
			length = offsetof(struct esas2r_ioctl_fs, data);

		/*
		 * Beginning a command.  We assume we'll get at least
		 * enough in the first write so we can look at the
		 * header and see how much we need to alloc.
		 */

		if (count < offsetof(struct esas2r_ioctl_fs, data))
			return -EINVAL;

		/* Allocate a buffer or use the existing buffer. */
		if (a->fs_api_buffer) {
			if (a->fs_api_buffer_size < length) {
				/* Free too-small buffer and get a new one */
				dma_free_coherent(&a->pcid->dev,
						  (size_t)a->fs_api_buffer_size,
						  a->fs_api_buffer,
						  (dma_addr_t)a->ppfs_api_buffer);

				goto re_allocate_buffer;
			}
		} else {
re_allocate_buffer:
			a->fs_api_buffer_size = length;

			a->fs_api_buffer = (u8 *)dma_alloc_coherent(
				&a->pcid->dev,
				(size_t)a->fs_api_buffer_size,
				(dma_addr_t *)&a->ppfs_api_buffer,
				GFP_KERNEL);
		}
	}

	if (!a->fs_api_buffer)
		return -ENOMEM;

	if (off > a->fs_api_buffer_size)
		return 0;

	if (count + off > a->fs_api_buffer_size)
		count = a->fs_api_buffer_size - off;

	if (count < 1)
		return 0;

	memcpy(a->fs_api_buffer + off, buf, count);

	return count;
}
