/*
 *  linux/drivers/message/fusion/mptspi.c
 *      For use with LSI PCI chip/adapter(s)
 *      running LSI Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2008 LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
 *
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>	/* for mdelay */
#include <linux/interrupt.h>
#include <linux/reboot.h>	/* notifier code */
#include <linux/workqueue.h>
#include <linux/raid_class.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>
#include <scsi/scsi_dbg.h>

#include "mptbase.h"
#include "mptscsih.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#define my_NAME		"Fusion MPT SPI Host driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptspi"

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");
MODULE_VERSION(my_VERSION);

/* Command line args */
static int mpt_saf_te = MPTSCSIH_SAF_TE;
module_param(mpt_saf_te, int, 0);
MODULE_PARM_DESC(mpt_saf_te, " Force enabling SEP Processor: enable=1  (default=MPTSCSIH_SAF_TE=0)");

static void mptspi_write_offset(struct scsi_target *, int);
static void mptspi_write_width(struct scsi_target *, int);
static int mptspi_write_spi_device_pg1(struct scsi_target *,
				       struct _CONFIG_PAGE_SCSI_DEVICE_1 *);

static struct scsi_transport_template *mptspi_transport_template = NULL;

static u8	mptspiDoneCtx = MPT_MAX_PROTOCOL_DRIVERS;
static u8	mptspiTaskCtx = MPT_MAX_PROTOCOL_DRIVERS;
static u8	mptspiInternalCtx = MPT_MAX_PROTOCOL_DRIVERS; /* Used only for internal commands */

/**
 * 	mptspi_setTargetNegoParms  - Update the target negotiation parameters
 *	@hd: Pointer to a SCSI Host Structure
 *	@target: per target private data
 *	@sdev: SCSI device
 *
 *	Update the target negotiation parameters based on the Inquiry
 *	data, adapter capabilities, and NVRAM settings.
 **/
static void
mptspi_setTargetNegoParms(MPT_SCSI_HOST *hd, VirtTarget *target,
			    struct scsi_device *sdev)
{
	MPT_ADAPTER *ioc = hd->ioc;
	SpiCfgData *pspi_data = &ioc->spi_data;
	int  id = (int) target->id;
	int  nvram;
	u8 width = MPT_NARROW;
	u8 factor = MPT_ASYNC;
	u8 offset = 0;
	u8 nfactor;
	u8 noQas = 1;

	target->negoFlags = pspi_data->noQas;

	if (sdev->scsi_level < SCSI_2) {
		width = 0;
		factor = MPT_ULTRA2;
		offset = pspi_data->maxSyncOffset;
		target->tflags &= ~MPT_TARGET_FLAGS_Q_YES;
	} else {
		if (scsi_device_wide(sdev))
			width = 1;

		if (scsi_device_sync(sdev)) {
			factor = pspi_data->minSyncFactor;
			if (!scsi_device_dt(sdev))
					factor = MPT_ULTRA2;
			else {
				if (!scsi_device_ius(sdev) &&
				    !scsi_device_qas(sdev))
					factor = MPT_ULTRA160;
				else {
					factor = MPT_ULTRA320;
					if (scsi_device_qas(sdev)) {
						ddvprintk(ioc,
						printk(MYIOC_s_DEBUG_FMT "Enabling QAS due to "
						"byte56=%02x on id=%d!\n", ioc->name,
						scsi_device_qas(sdev), id));
						noQas = 0;
					}
					if (sdev->type == TYPE_TAPE &&
					    scsi_device_ius(sdev))
						target->negoFlags |= MPT_TAPE_NEGO_IDP;
				}
			}
			offset = pspi_data->maxSyncOffset;

			/* If RAID, never disable QAS
			 * else if non RAID, do not disable
			 *   QAS if bit 1 is set
			 * bit 1 QAS support, non-raid only
			 * bit 0 IU support
			 */
			if (target->raidVolume == 1)
				noQas = 0;
		} else {
			factor = MPT_ASYNC;
			offset = 0;
		}
	}

	if (!sdev->tagged_supported)
		target->tflags &= ~MPT_TARGET_FLAGS_Q_YES;

	/* Update tflags based on NVRAM settings. (SCSI only)
	 */
	if (pspi_data->nvram && (pspi_data->nvram[id] != MPT_HOST_NVRAM_INVALID)) {
		nvram = pspi_data->nvram[id];
		nfactor = (nvram & MPT_NVRAM_SYNC_MASK) >> 8;

		if (width)
			width = nvram & MPT_NVRAM_WIDE_DISABLE ? 0 : 1;

		if (offset > 0) {
			/* Ensure factor is set to the
			 * maximum of: adapter, nvram, inquiry
			 */
			if (nfactor) {
				if (nfactor < pspi_data->minSyncFactor )
					nfactor = pspi_data->minSyncFactor;

				factor = max(factor, nfactor);
				if (factor == MPT_ASYNC)
					offset = 0;
			} else {
				offset = 0;
				factor = MPT_ASYNC;
		}
		} else {
			factor = MPT_ASYNC;
		}
	}

	/* Make sure data is consistent
	 */
	if ((!width) && (factor < MPT_ULTRA2))
		factor = MPT_ULTRA2;

	/* Save the data to the target structure.
	 */
	target->minSyncFactor = factor;
	target->maxOffset = offset;
	target->maxWidth = width;

	spi_min_period(scsi_target(sdev)) = factor;
	spi_max_offset(scsi_target(sdev)) = offset;
	spi_max_width(scsi_target(sdev)) = width;

	target->tflags |= MPT_TARGET_FLAGS_VALID_NEGO;

	/* Disable unused features.
	 */
	if (!width)
		target->negoFlags |= MPT_TARGET_NO_NEGO_WIDE;

	if (!offset)
		target->negoFlags |= MPT_TARGET_NO_NEGO_SYNC;

	if ( factor > MPT_ULTRA320 )
		noQas = 0;

	if (noQas && (pspi_data->noQas == 0)) {
		pspi_data->noQas |= MPT_TARGET_NO_NEGO_QAS;
		target->negoFlags |= MPT_TARGET_NO_NEGO_QAS;

		/* Disable QAS in a mixed configuration case
		 */

		ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"Disabling QAS due to noQas=%02x on id=%d!\n", ioc->name, noQas, id));
	}
}

/**
 * 	mptspi_writeIOCPage4  - write IOC Page 4
 *	@hd: Pointer to a SCSI Host Structure
 *	@channel: channel number
 *	@id: write IOC Page4 for this ID & Bus
 *
 *	Return: -EAGAIN if unable to obtain a Message Frame
 *		or 0 if success.
 *
 *	Remark: We do not wait for a return, write pages sequentially.
 **/
static int
mptspi_writeIOCPage4(MPT_SCSI_HOST *hd, u8 channel , u8 id)
{
	MPT_ADAPTER		*ioc = hd->ioc;
	Config_t		*pReq;
	IOCPage4_t		*IOCPage4Ptr;
	MPT_FRAME_HDR		*mf;
	dma_addr_t		 dataDma;
	u32			 flagsLength;
	int			 ii;

	/* Get a MF for this command.
	 */
	if ((mf = mpt_get_msg_frame(ioc->DoneCtx, ioc)) == NULL) {
		dfailprintk(ioc, printk(MYIOC_s_WARN_FMT
				"writeIOCPage4 : no msg frames!\n",ioc->name));
		return -EAGAIN;
	}

	/* Set the request and the data pointers.
	 * Place data at end of MF.
	 */
	pReq = (Config_t *)mf;

	/* Complete the request frame (same for all requests).
	 */
	pReq->Action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	pReq->Reserved = 0;
	pReq->ChainOffset = 0;
	pReq->Function = MPI_FUNCTION_CONFIG;
	pReq->ExtPageLength = 0;
	pReq->ExtPageType = 0;
	pReq->MsgFlags = 0;
	for (ii=0; ii < 8; ii++) {
		pReq->Reserved2[ii] = 0;
	}

	IOCPage4Ptr = ioc->spi_data.pIocPg4;
	dataDma = ioc->spi_data.IocPg4_dma;
	ii = IOCPage4Ptr->ActiveSEP++;
	IOCPage4Ptr->SEP[ii].SEPTargetID = id;
	IOCPage4Ptr->SEP[ii].SEPBus = channel;
	pReq->Header = IOCPage4Ptr->Header;
	pReq->PageAddress = cpu_to_le32(id | (channel << 8 ));

	/* Add a SGE to the config request.
	 */
	flagsLength = MPT_SGE_FLAGS_SSIMPLE_WRITE |
		(IOCPage4Ptr->Header.PageLength + ii) * 4;

	ioc->add_sge((char *)&pReq->PageBufferSGE, flagsLength, dataDma);

	ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		"writeIOCPage4: MaxSEP=%d ActiveSEP=%d id=%d bus=%d\n",
		ioc->name, IOCPage4Ptr->MaxSEP, IOCPage4Ptr->ActiveSEP, id, channel));

	mpt_put_msg_frame(ioc->DoneCtx, ioc, mf);

	return 0;
}

/**
 *	mptspi_initTarget - Target, LUN alloc/free functionality.
 *	@hd: Pointer to MPT_SCSI_HOST structure
 *	@vtarget: per target private data
 *	@sdev: SCSI device
 *
 *	NOTE: It's only SAFE to call this routine if data points to
 *	sane & valid STANDARD INQUIRY data!
 *
 *	Allocate and initialize memory for this target.
 *	Save inquiry data.
 *
 **/
static void
mptspi_initTarget(MPT_SCSI_HOST *hd, VirtTarget *vtarget,
		    struct scsi_device *sdev)
{

	/* Is LUN supported? If so, upper 2 bits will be 0
	* in first byte of inquiry data.
	*/
	if (sdev->inq_periph_qual != 0)
		return;

	if (vtarget == NULL)
		return;

	vtarget->type = sdev->type;

	if ((sdev->type == TYPE_PROCESSOR) && (hd->ioc->spi_data.Saf_Te)) {
		/* Treat all Processors as SAF-TE if
		 * command line option is set */
		vtarget->tflags |= MPT_TARGET_FLAGS_SAF_TE_ISSUED;
		mptspi_writeIOCPage4(hd, vtarget->channel, vtarget->id);
	}else if ((sdev->type == TYPE_PROCESSOR) &&
		!(vtarget->tflags & MPT_TARGET_FLAGS_SAF_TE_ISSUED )) {
		if (sdev->inquiry_len > 49 ) {
			if (sdev->inquiry[44] == 'S' &&
			    sdev->inquiry[45] == 'A' &&
			    sdev->inquiry[46] == 'F' &&
			    sdev->inquiry[47] == '-' &&
			    sdev->inquiry[48] == 'T' &&
			    sdev->inquiry[49] == 'E' ) {
				vtarget->tflags |= MPT_TARGET_FLAGS_SAF_TE_ISSUED;
				mptspi_writeIOCPage4(hd, vtarget->channel, vtarget->id);
			}
		}
	}
	mptspi_setTargetNegoParms(hd, vtarget, sdev);
}

/**
 *	mptspi_is_raid - Determines whether target is belonging to volume
 *	@hd: Pointer to a SCSI HOST structure
 *	@id: target device id
 *
 *	Return:
 *		non-zero = true
 *		zero = false
 *
 */
static int
mptspi_is_raid(struct _MPT_SCSI_HOST *hd, u32 id)
{
	int i, rc = 0;
	MPT_ADAPTER *ioc = hd->ioc;

	if (!ioc->raid_data.pIocPg2)
		goto out;

	if (!ioc->raid_data.pIocPg2->NumActiveVolumes)
		goto out;
	for (i=0; i < ioc->raid_data.pIocPg2->NumActiveVolumes; i++) {
		if (ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID == id) {
			rc = 1;
			goto out;
		}
	}

 out:
	return rc;
}

static int mptspi_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct _MPT_SCSI_HOST *hd = shost_priv(shost);
	VirtTarget		*vtarget;
	MPT_ADAPTER *ioc;

	if (hd == NULL)
		return -ENODEV;

	ioc = hd->ioc;
	vtarget = kzalloc(sizeof(VirtTarget), GFP_KERNEL);
	if (!vtarget)
		return -ENOMEM;

	vtarget->ioc_id = ioc->id;
	vtarget->tflags = MPT_TARGET_FLAGS_Q_YES;
	vtarget->id = (u8)starget->id;
	vtarget->channel = (u8)starget->channel;
	vtarget->starget = starget;
	starget->hostdata = vtarget;

	if (starget->channel == 1) {
		if (mptscsih_is_phys_disk(ioc, 0, starget->id) == 0)
			return 0;
		vtarget->tflags |= MPT_TARGET_FLAGS_RAID_COMPONENT;
		/* The real channel for this device is zero */
		vtarget->channel = 0;
		/* The actual physdisknum (for RAID passthrough) */
		vtarget->id = mptscsih_raid_id_to_num(ioc, 0,
		    starget->id);
	}

	if (starget->channel == 0 &&
	    mptspi_is_raid(hd, starget->id)) {
		vtarget->raidVolume = 1;
		ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "RAID Volume @ channel=%d id=%d\n", ioc->name, starget->channel,
		    starget->id));
	}

	if (ioc->spi_data.nvram &&
	    ioc->spi_data.nvram[starget->id] != MPT_HOST_NVRAM_INVALID) {
		u32 nvram = ioc->spi_data.nvram[starget->id];
		spi_min_period(starget) = (nvram & MPT_NVRAM_SYNC_MASK) >> MPT_NVRAM_SYNC_SHIFT;
		spi_max_width(starget) = nvram & MPT_NVRAM_WIDE_DISABLE ? 0 : 1;
	} else {
		spi_min_period(starget) = ioc->spi_data.minSyncFactor;
		spi_max_width(starget) = ioc->spi_data.maxBusWidth;
	}
	spi_max_offset(starget) = ioc->spi_data.maxSyncOffset;

	spi_offset(starget) = 0;
	spi_period(starget) = 0xFF;
	mptspi_write_width(starget, 0);

	return 0;
}

static void
mptspi_target_destroy(struct scsi_target *starget)
{
	kfree(starget->hostdata);
	starget->hostdata = NULL;
}

/**
 *	mptspi_print_write_nego - negotiation parameters debug info that is being sent
 *	@hd: Pointer to a SCSI HOST structure
 *	@starget: SCSI target
 *	@ii: negotiation parameters
 *
 */
static void
mptspi_print_write_nego(struct _MPT_SCSI_HOST *hd, struct scsi_target *starget, u32 ii)
{
	ddvprintk(hd->ioc, printk(MYIOC_s_DEBUG_FMT "id=%d Requested = 0x%08x"
	    " ( %s factor = 0x%02x @ offset = 0x%02x %s%s%s%s%s%s%s%s)\n",
	    hd->ioc->name, starget->id, ii,
	    ii & MPI_SCSIDEVPAGE0_NP_WIDE ? "Wide ": "",
	    ((ii >> 8) & 0xFF), ((ii >> 16) & 0xFF),
	    ii & MPI_SCSIDEVPAGE0_NP_IU ? "IU ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_DT ? "DT ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_QAS ? "QAS ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_HOLD_MCS ? "HOLDMCS ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_WR_FLOW ? "WRFLOW ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_RD_STRM ? "RDSTRM ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_RTI ? "RTI ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_PCOMP_EN ? "PCOMP ": ""));
}

/**
 *	mptspi_print_read_nego - negotiation parameters debug info that is being read
 *	@hd: Pointer to a SCSI HOST structure
 *	@starget: SCSI target
 *	@ii: negotiation parameters
 *
 */
static void
mptspi_print_read_nego(struct _MPT_SCSI_HOST *hd, struct scsi_target *starget, u32 ii)
{
	ddvprintk(hd->ioc, printk(MYIOC_s_DEBUG_FMT "id=%d Read = 0x%08x"
	    " ( %s factor = 0x%02x @ offset = 0x%02x %s%s%s%s%s%s%s%s)\n",
	    hd->ioc->name, starget->id, ii,
	    ii & MPI_SCSIDEVPAGE0_NP_WIDE ? "Wide ": "",
	    ((ii >> 8) & 0xFF), ((ii >> 16) & 0xFF),
	    ii & MPI_SCSIDEVPAGE0_NP_IU ? "IU ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_DT ? "DT ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_QAS ? "QAS ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_HOLD_MCS ? "HOLDMCS ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_WR_FLOW ? "WRFLOW ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_RD_STRM ? "RDSTRM ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_RTI ? "RTI ": "",
	    ii & MPI_SCSIDEVPAGE0_NP_PCOMP_EN ? "PCOMP ": ""));
}

static int mptspi_read_spi_device_pg0(struct scsi_target *starget,
			     struct _CONFIG_PAGE_SCSI_DEVICE_0 *pass_pg0)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct _MPT_SCSI_HOST *hd = shost_priv(shost);
	struct _MPT_ADAPTER *ioc = hd->ioc;
	struct _CONFIG_PAGE_SCSI_DEVICE_0 *spi_dev_pg0;
	dma_addr_t spi_dev_pg0_dma;
	int size;
	struct _x_config_parms cfg;
	struct _CONFIG_PAGE_HEADER hdr;
	int err = -EBUSY;

	/* No SPI parameters for RAID devices */
	if (starget->channel == 0 &&
	    mptspi_is_raid(hd, starget->id))
		return -1;

	size = ioc->spi_data.sdp0length * 4;
	/*
	if (ioc->spi_data.sdp0length & 1)
		size += size + 4;
	size += 2048;
	*/

	spi_dev_pg0 = dma_alloc_coherent(&ioc->pcidev->dev, size, &spi_dev_pg0_dma, GFP_KERNEL);
	if (spi_dev_pg0 == NULL) {
		starget_printk(KERN_ERR, starget, MYIOC_s_FMT
		    "dma_alloc_coherent for parameters failed\n", ioc->name);
		return -EINVAL;
	}

	memset(&hdr, 0, sizeof(hdr));

	hdr.PageVersion = ioc->spi_data.sdp0version;
	hdr.PageLength = ioc->spi_data.sdp0length;
	hdr.PageNumber = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_SCSI_DEVICE;

	memset(&cfg, 0, sizeof(cfg));

	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = spi_dev_pg0_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.dir = 0;
	cfg.pageAddr = starget->id;
	cfg.timeout = 60;

	if (mpt_config(ioc, &cfg)) {
		starget_printk(KERN_ERR, starget, MYIOC_s_FMT "mpt_config failed\n", ioc->name);
		goto out_free;
	}
	err = 0;
	memcpy(pass_pg0, spi_dev_pg0, size);

	mptspi_print_read_nego(hd, starget, le32_to_cpu(spi_dev_pg0->NegotiatedParameters));

 out_free:
	dma_free_coherent(&ioc->pcidev->dev, size, spi_dev_pg0, spi_dev_pg0_dma);
	return err;
}

static u32 mptspi_getRP(struct scsi_target *starget)
{
	u32 nego = 0;

	nego |= spi_iu(starget) ? MPI_SCSIDEVPAGE1_RP_IU : 0;
	nego |= spi_dt(starget) ? MPI_SCSIDEVPAGE1_RP_DT : 0;
	nego |= spi_qas(starget) ? MPI_SCSIDEVPAGE1_RP_QAS : 0;
	nego |= spi_hold_mcs(starget) ? MPI_SCSIDEVPAGE1_RP_HOLD_MCS : 0;
	nego |= spi_wr_flow(starget) ? MPI_SCSIDEVPAGE1_RP_WR_FLOW : 0;
	nego |= spi_rd_strm(starget) ? MPI_SCSIDEVPAGE1_RP_RD_STRM : 0;
	nego |= spi_rti(starget) ? MPI_SCSIDEVPAGE1_RP_RTI : 0;
	nego |= spi_pcomp_en(starget) ? MPI_SCSIDEVPAGE1_RP_PCOMP_EN : 0;

	nego |= (spi_period(starget) <<  MPI_SCSIDEVPAGE1_RP_SHIFT_MIN_SYNC_PERIOD) & MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK;
	nego |= (spi_offset(starget) << MPI_SCSIDEVPAGE1_RP_SHIFT_MAX_SYNC_OFFSET) & MPI_SCSIDEVPAGE1_RP_MAX_SYNC_OFFSET_MASK;
	nego |= spi_width(starget) ?  MPI_SCSIDEVPAGE1_RP_WIDE : 0;

	return nego;
}

static void mptspi_read_parameters(struct scsi_target *starget)
{
	int nego;
	struct _CONFIG_PAGE_SCSI_DEVICE_0 spi_dev_pg0;

	mptspi_read_spi_device_pg0(starget, &spi_dev_pg0);

	nego = le32_to_cpu(spi_dev_pg0.NegotiatedParameters);

	spi_iu(starget) = (nego & MPI_SCSIDEVPAGE0_NP_IU) ? 1 : 0;
	spi_dt(starget) = (nego & MPI_SCSIDEVPAGE0_NP_DT) ? 1 : 0;
	spi_qas(starget) = (nego & MPI_SCSIDEVPAGE0_NP_QAS) ? 1 : 0;
	spi_wr_flow(starget) = (nego & MPI_SCSIDEVPAGE0_NP_WR_FLOW) ? 1 : 0;
	spi_rd_strm(starget) = (nego & MPI_SCSIDEVPAGE0_NP_RD_STRM) ? 1 : 0;
	spi_rti(starget) = (nego & MPI_SCSIDEVPAGE0_NP_RTI) ? 1 : 0;
	spi_pcomp_en(starget) = (nego & MPI_SCSIDEVPAGE0_NP_PCOMP_EN) ? 1 : 0;
	spi_hold_mcs(starget) = (nego & MPI_SCSIDEVPAGE0_NP_HOLD_MCS) ? 1 : 0;
	spi_period(starget) = (nego & MPI_SCSIDEVPAGE0_NP_NEG_SYNC_PERIOD_MASK) >> MPI_SCSIDEVPAGE0_NP_SHIFT_SYNC_PERIOD;
	spi_offset(starget) = (nego & MPI_SCSIDEVPAGE0_NP_NEG_SYNC_OFFSET_MASK) >> MPI_SCSIDEVPAGE0_NP_SHIFT_SYNC_OFFSET;
	spi_width(starget) = (nego & MPI_SCSIDEVPAGE0_NP_WIDE) ? 1 : 0;
}

static int
mptscsih_quiesce_raid(MPT_SCSI_HOST *hd, int quiesce, u8 channel, u8 id)
{
	MPT_ADAPTER	*ioc = hd->ioc;
	MpiRaidActionRequest_t	*pReq;
	MPT_FRAME_HDR		*mf;
	int			ret;
	unsigned long 	 	timeleft;

	mutex_lock(&ioc->internal_cmds.mutex);

	/* Get and Populate a free Frame
	 */
	if ((mf = mpt_get_msg_frame(ioc->InternalCtx, ioc)) == NULL) {
		dfailprintk(hd->ioc, printk(MYIOC_s_WARN_FMT
			"%s: no msg frames!\n", ioc->name, __func__));
		ret = -EAGAIN;
		goto out;
	}
	pReq = (MpiRaidActionRequest_t *)mf;
	if (quiesce)
		pReq->Action = MPI_RAID_ACTION_QUIESCE_PHYS_IO;
	else
		pReq->Action = MPI_RAID_ACTION_ENABLE_PHYS_IO;
	pReq->Reserved1 = 0;
	pReq->ChainOffset = 0;
	pReq->Function = MPI_FUNCTION_RAID_ACTION;
	pReq->VolumeID = id;
	pReq->VolumeBus = channel;
	pReq->PhysDiskNum = 0;
	pReq->MsgFlags = 0;
	pReq->Reserved2 = 0;
	pReq->ActionDataWord = 0; /* Reserved for this action */

	ioc->add_sge((char *)&pReq->ActionDataSGE,
		MPT_SGE_FLAGS_SSIMPLE_READ | 0, (dma_addr_t) -1);

	ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT "RAID Volume action=%x channel=%d id=%d\n",
			ioc->name, pReq->Action, channel, id));

	INITIALIZE_MGMT_STATUS(ioc->internal_cmds.status)
	mpt_put_msg_frame(ioc->InternalCtx, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->internal_cmds.done, 10*HZ);
	if (!(ioc->internal_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		ret = -ETIME;
		dfailprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: TIMED OUT!\n",
		    ioc->name, __func__));
		if (ioc->internal_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto out;
		if (!timeleft) {
			printk(MYIOC_s_WARN_FMT "Issuing Reset from %s!!\n",
			    ioc->name, __func__);
			mpt_HardResetHandler(ioc, CAN_SLEEP);
			mpt_free_msg_frame(ioc, mf);
		}
		goto out;
	}

	ret = ioc->internal_cmds.completion_code;

 out:
	CLEAR_MGMT_STATUS(ioc->internal_cmds.status)
	mutex_unlock(&ioc->internal_cmds.mutex);
	return ret;
}

static void mptspi_dv_device(struct _MPT_SCSI_HOST *hd,
			     struct scsi_device *sdev)
{
	VirtTarget *vtarget = scsi_target(sdev)->hostdata;
	MPT_ADAPTER *ioc = hd->ioc;

	/* no DV on RAID devices */
	if (sdev->channel == 0 &&
	    mptspi_is_raid(hd, sdev->id))
		return;

	/* If this is a piece of a RAID, then quiesce first */
	if (sdev->channel == 1 &&
	    mptscsih_quiesce_raid(hd, 1, vtarget->channel, vtarget->id) < 0) {
		starget_printk(KERN_ERR, scsi_target(sdev), MYIOC_s_FMT
		    "Integrated RAID quiesce failed\n", ioc->name);
		return;
	}

	hd->spi_pending |= (1 << sdev->id);
	spi_dv_device(sdev);
	hd->spi_pending &= ~(1 << sdev->id);

	if (sdev->channel == 1 &&
	    mptscsih_quiesce_raid(hd, 0, vtarget->channel, vtarget->id) < 0)
		starget_printk(KERN_ERR, scsi_target(sdev), MYIOC_s_FMT
		    "Integrated RAID resume failed\n", ioc->name);

	mptspi_read_parameters(sdev->sdev_target);
	spi_display_xfer_agreement(sdev->sdev_target);
	mptspi_read_parameters(sdev->sdev_target);
}

static int mptspi_sdev_init(struct scsi_device *sdev)
{
	MPT_SCSI_HOST *hd = shost_priv(sdev->host);
	VirtTarget		*vtarget;
	VirtDevice		*vdevice;
	struct scsi_target 	*starget;
	MPT_ADAPTER *ioc = hd->ioc;

	if (sdev->channel == 1 &&
		mptscsih_is_phys_disk(ioc, 0, sdev->id) == 0)
			return -ENXIO;

	vdevice = kzalloc(sizeof(VirtDevice), GFP_KERNEL);
	if (!vdevice) {
		printk(MYIOC_s_ERR_FMT "sdev_init kmalloc(%zd) FAILED!\n",
				ioc->name, sizeof(VirtDevice));
		return -ENOMEM;
	}

	vdevice->lun = sdev->lun;
	sdev->hostdata = vdevice;

	starget = scsi_target(sdev);
	vtarget = starget->hostdata;
	vdevice->vtarget = vtarget;
	vtarget->num_luns++;

	if (sdev->channel == 1)
		sdev->no_uld_attach = 1;

	return 0;
}

static int mptspi_sdev_configure(struct scsi_device *sdev,
				 struct queue_limits *lim)
{
	struct _MPT_SCSI_HOST *hd = shost_priv(sdev->host);
	VirtTarget *vtarget = scsi_target(sdev)->hostdata;
	int ret;

	mptspi_initTarget(hd, vtarget, sdev);

	ret = mptscsih_sdev_configure(sdev, lim);

	if (ret)
		return ret;

	ddvprintk(hd->ioc, printk(MYIOC_s_DEBUG_FMT "id=%d min_period=0x%02x"
		" max_offset=0x%02x max_width=%d\n", hd->ioc->name,
		sdev->id, spi_min_period(scsi_target(sdev)),
		spi_max_offset(scsi_target(sdev)),
		spi_max_width(scsi_target(sdev))));

	if ((sdev->channel == 1 ||
	     !(mptspi_is_raid(hd, sdev->id))) &&
	    !spi_initial_dv(sdev->sdev_target))
		mptspi_dv_device(hd, sdev);

	return 0;
}

static int
mptspi_qcmd(struct Scsi_Host *shost, struct scsi_cmnd *SCpnt)
{
	struct _MPT_SCSI_HOST *hd = shost_priv(shost);
	VirtDevice	*vdevice = SCpnt->device->hostdata;
	MPT_ADAPTER *ioc = hd->ioc;

	if (!vdevice || !vdevice->vtarget) {
		SCpnt->result = DID_NO_CONNECT << 16;
		scsi_done(SCpnt);
		return 0;
	}

	if (SCpnt->device->channel == 1 &&
		mptscsih_is_phys_disk(ioc, 0, SCpnt->device->id) == 0) {
		SCpnt->result = DID_NO_CONNECT << 16;
		scsi_done(SCpnt);
		return 0;
	}

	if (spi_dv_pending(scsi_target(SCpnt->device)))
		ddvprintk(ioc, scsi_print_command(SCpnt));

	return mptscsih_qcmd(SCpnt);
}

static void mptspi_sdev_destroy(struct scsi_device *sdev)
{
	struct scsi_target *starget = scsi_target(sdev);
	VirtTarget *vtarget = starget->hostdata;
	VirtDevice *vdevice = sdev->hostdata;

	/* Will this be the last lun on a non-raid device? */
	if (vtarget->num_luns == 1 && vdevice->configured_lun) {
		struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;

		/* Async Narrow */
		pg1.RequestedParameters = 0;
		pg1.Reserved = 0;
		pg1.Configuration = 0;

		mptspi_write_spi_device_pg1(starget, &pg1);
	}

	mptscsih_sdev_destroy(sdev);
}

static const struct scsi_host_template mptspi_driver_template = {
	.module				= THIS_MODULE,
	.proc_name			= "mptspi",
	.show_info			= mptscsih_show_info,
	.name				= "MPT SPI Host",
	.info				= mptscsih_info,
	.queuecommand			= mptspi_qcmd,
	.target_alloc			= mptspi_target_alloc,
	.sdev_init			= mptspi_sdev_init,
	.sdev_configure			= mptspi_sdev_configure,
	.target_destroy			= mptspi_target_destroy,
	.sdev_destroy			= mptspi_sdev_destroy,
	.change_queue_depth 		= mptscsih_change_queue_depth,
	.eh_abort_handler		= mptscsih_abort,
	.eh_device_reset_handler	= mptscsih_dev_reset,
	.eh_bus_reset_handler		= mptscsih_bus_reset,
	.eh_host_reset_handler		= mptscsih_host_reset,
	.bios_param			= mptscsih_bios_param,
	.can_queue			= MPT_SCSI_CAN_QUEUE,
	.this_id			= -1,
	.sg_tablesize			= MPT_SCSI_SG_DEPTH,
	.max_sectors			= 8192,
	.cmd_per_lun			= 7,
	.dma_alignment			= 511,
	.shost_groups			= mptscsih_host_attr_groups,
};

static int mptspi_write_spi_device_pg1(struct scsi_target *starget,
			       struct _CONFIG_PAGE_SCSI_DEVICE_1 *pass_pg1)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct _MPT_SCSI_HOST *hd = shost_priv(shost);
	struct _MPT_ADAPTER *ioc = hd->ioc;
	struct _CONFIG_PAGE_SCSI_DEVICE_1 *pg1;
	dma_addr_t pg1_dma;
	int size;
	struct _x_config_parms cfg;
	struct _CONFIG_PAGE_HEADER hdr;
	int err = -EBUSY;
	u32 nego_parms;
	u32 period;
	struct scsi_device *sdev;
	int i;

	/* don't allow updating nego parameters on RAID devices */
	if (starget->channel == 0 &&
	    mptspi_is_raid(hd, starget->id))
		return -1;

	size = ioc->spi_data.sdp1length * 4;

	pg1 = dma_alloc_coherent(&ioc->pcidev->dev, size, &pg1_dma, GFP_KERNEL);
	if (pg1 == NULL) {
		starget_printk(KERN_ERR, starget, MYIOC_s_FMT
		    "dma_alloc_coherent for parameters failed\n", ioc->name);
		return -EINVAL;
	}

	memset(&hdr, 0, sizeof(hdr));

	hdr.PageVersion = ioc->spi_data.sdp1version;
	hdr.PageLength = ioc->spi_data.sdp1length;
	hdr.PageNumber = 1;
	hdr.PageType = MPI_CONFIG_PAGETYPE_SCSI_DEVICE;

	memset(&cfg, 0, sizeof(cfg));

	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = pg1_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	cfg.dir = 1;
	cfg.pageAddr = starget->id;

	memcpy(pg1, pass_pg1, size);

	pg1->Header.PageVersion = hdr.PageVersion;
	pg1->Header.PageLength = hdr.PageLength;
	pg1->Header.PageNumber = hdr.PageNumber;
	pg1->Header.PageType = hdr.PageType;

	nego_parms = le32_to_cpu(pg1->RequestedParameters);
	period = (nego_parms & MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK) >>
		MPI_SCSIDEVPAGE1_RP_SHIFT_MIN_SYNC_PERIOD;
	if (period == 8) {
		/* Turn on inline data padding for TAPE when running U320 */
		for (i = 0 ; i < 16; i++) {
			sdev = scsi_device_lookup_by_target(starget, i);
			if (sdev && sdev->type == TYPE_TAPE) {
				sdev_printk(KERN_DEBUG, sdev, MYIOC_s_FMT
					    "IDP:ON\n", ioc->name);
				nego_parms |= MPI_SCSIDEVPAGE1_RP_IDP;
				pg1->RequestedParameters =
				    cpu_to_le32(nego_parms);
				break;
			}
		}
	}

	mptspi_print_write_nego(hd, starget, le32_to_cpu(pg1->RequestedParameters));

	if (mpt_config(ioc, &cfg)) {
		starget_printk(KERN_ERR, starget, MYIOC_s_FMT
		    "mpt_config failed\n", ioc->name);
		goto out_free;
	}
	err = 0;

 out_free:
	dma_free_coherent(&ioc->pcidev->dev, size, pg1, pg1_dma);
	return err;
}

static void mptspi_write_offset(struct scsi_target *starget, int offset)
{
	struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;
	u32 nego;

	if (offset < 0)
		offset = 0;

	if (offset > 255)
		offset = 255;

	if (spi_offset(starget) == -1)
		mptspi_read_parameters(starget);

	spi_offset(starget) = offset;

	nego = mptspi_getRP(starget);

	pg1.RequestedParameters = cpu_to_le32(nego);
	pg1.Reserved = 0;
	pg1.Configuration = 0;

	mptspi_write_spi_device_pg1(starget, &pg1);
}

static void mptspi_write_period(struct scsi_target *starget, int period)
{
	struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;
	u32 nego;

	if (period < 8)
		period = 8;

	if (period > 255)
		period = 255;

	if (spi_period(starget) == -1)
		mptspi_read_parameters(starget);

	if (period == 8) {
		spi_iu(starget) = 1;
		spi_dt(starget) = 1;
	} else if (period == 9) {
		spi_dt(starget) = 1;
	}

	spi_period(starget) = period;

	nego = mptspi_getRP(starget);

	pg1.RequestedParameters = cpu_to_le32(nego);
	pg1.Reserved = 0;
	pg1.Configuration = 0;

	mptspi_write_spi_device_pg1(starget, &pg1);
}

static void mptspi_write_dt(struct scsi_target *starget, int dt)
{
	struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;
	u32 nego;

	if (spi_period(starget) == -1)
		mptspi_read_parameters(starget);

	if (!dt && spi_period(starget) < 10)
		spi_period(starget) = 10;

	spi_dt(starget) = dt;

	nego = mptspi_getRP(starget);


	pg1.RequestedParameters = cpu_to_le32(nego);
	pg1.Reserved = 0;
	pg1.Configuration = 0;

	mptspi_write_spi_device_pg1(starget, &pg1);
}

static void mptspi_write_iu(struct scsi_target *starget, int iu)
{
	struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;
	u32 nego;

	if (spi_period(starget) == -1)
		mptspi_read_parameters(starget);

	if (!iu && spi_period(starget) < 9)
		spi_period(starget) = 9;

	spi_iu(starget) = iu;

	nego = mptspi_getRP(starget);

	pg1.RequestedParameters = cpu_to_le32(nego);
	pg1.Reserved = 0;
	pg1.Configuration = 0;

	mptspi_write_spi_device_pg1(starget, &pg1);
}

#define MPTSPI_SIMPLE_TRANSPORT_PARM(parm) 				\
static void mptspi_write_##parm(struct scsi_target *starget, int parm)\
{									\
	struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;				\
	u32 nego;							\
									\
	spi_##parm(starget) = parm;					\
									\
	nego = mptspi_getRP(starget);					\
									\
	pg1.RequestedParameters = cpu_to_le32(nego);			\
	pg1.Reserved = 0;						\
	pg1.Configuration = 0;						\
									\
	mptspi_write_spi_device_pg1(starget, &pg1);				\
}

MPTSPI_SIMPLE_TRANSPORT_PARM(rd_strm)
MPTSPI_SIMPLE_TRANSPORT_PARM(wr_flow)
MPTSPI_SIMPLE_TRANSPORT_PARM(rti)
MPTSPI_SIMPLE_TRANSPORT_PARM(hold_mcs)
MPTSPI_SIMPLE_TRANSPORT_PARM(pcomp_en)

static void mptspi_write_qas(struct scsi_target *starget, int qas)
{
	struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct _MPT_SCSI_HOST *hd = shost_priv(shost);
	VirtTarget *vtarget = starget->hostdata;
	u32 nego;

	if ((vtarget->negoFlags & MPT_TARGET_NO_NEGO_QAS) ||
	    hd->ioc->spi_data.noQas)
		spi_qas(starget) = 0;
	else
		spi_qas(starget) = qas;

	nego = mptspi_getRP(starget);

	pg1.RequestedParameters = cpu_to_le32(nego);
	pg1.Reserved = 0;
	pg1.Configuration = 0;

	mptspi_write_spi_device_pg1(starget, &pg1);
}

static void mptspi_write_width(struct scsi_target *starget, int width)
{
	struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;
	u32 nego;

	if (!width) {
		spi_dt(starget) = 0;
		if (spi_period(starget) < 10)
			spi_period(starget) = 10;
	}

	spi_width(starget) = width;

	nego = mptspi_getRP(starget);

	pg1.RequestedParameters = cpu_to_le32(nego);
	pg1.Reserved = 0;
	pg1.Configuration = 0;

	mptspi_write_spi_device_pg1(starget, &pg1);
}

struct work_queue_wrapper {
	struct work_struct	work;
	struct _MPT_SCSI_HOST	*hd;
	int			disk;
};

static void mpt_work_wrapper(struct work_struct *work)
{
	struct work_queue_wrapper *wqw =
		container_of(work, struct work_queue_wrapper, work);
	struct _MPT_SCSI_HOST *hd = wqw->hd;
	MPT_ADAPTER *ioc = hd->ioc;
	struct Scsi_Host *shost = ioc->sh;
	struct scsi_device *sdev;
	int disk = wqw->disk;
	struct _CONFIG_PAGE_IOC_3 *pg3;

	kfree(wqw);

	mpt_findImVolumes(ioc);
	pg3 = ioc->raid_data.pIocPg3;
	if (!pg3)
		return;

	shost_for_each_device(sdev,shost) {
		struct scsi_target *starget = scsi_target(sdev);
		VirtTarget *vtarget = starget->hostdata;

		/* only want to search RAID components */
		if (sdev->channel != 1)
			continue;

		/* The id is the raid PhysDiskNum, even if
		 * starget->id is the actual target address */
		if(vtarget->id != disk)
			continue;

		starget_printk(KERN_INFO, vtarget->starget, MYIOC_s_FMT
		    "Integrated RAID requests DV of new device\n", ioc->name);
		mptspi_dv_device(hd, sdev);
	}
	shost_printk(KERN_INFO, shost, MYIOC_s_FMT
	    "Integrated RAID detects new device %d\n", ioc->name, disk);
	scsi_scan_target(&ioc->sh->shost_gendev, 1, disk, 0, SCSI_SCAN_RESCAN);
}


static void mpt_dv_raid(struct _MPT_SCSI_HOST *hd, int disk)
{
	struct work_queue_wrapper *wqw = kmalloc(sizeof(*wqw), GFP_ATOMIC);
	MPT_ADAPTER *ioc = hd->ioc;

	if (!wqw) {
		shost_printk(KERN_ERR, ioc->sh, MYIOC_s_FMT
		    "Failed to act on RAID event for physical disk %d\n",
		    ioc->name, disk);
		return;
	}
	INIT_WORK(&wqw->work, mpt_work_wrapper);
	wqw->hd = hd;
	wqw->disk = disk;

	schedule_work(&wqw->work);
}

static int
mptspi_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *pEvReply)
{
	u8 event = le32_to_cpu(pEvReply->Event) & 0xFF;
	struct _MPT_SCSI_HOST *hd = shost_priv(ioc->sh);

	if (ioc->bus_type != SPI)
		return 0;

	if (hd && event ==  MPI_EVENT_INTEGRATED_RAID) {
		int reason
			= (le32_to_cpu(pEvReply->Data[0]) & 0x00FF0000) >> 16;

		if (reason == MPI_EVENT_RAID_RC_DOMAIN_VAL_NEEDED) {
			int disk = (le32_to_cpu(pEvReply->Data[0]) & 0xFF000000) >> 24;
			mpt_dv_raid(hd, disk);
		}
	}
	return mptscsih_event_process(ioc, pEvReply);
}

static int
mptspi_deny_binding(struct scsi_target *starget)
{
	struct _MPT_SCSI_HOST *hd =
		(struct _MPT_SCSI_HOST *)dev_to_shost(starget->dev.parent)->hostdata;
	return ((mptspi_is_raid(hd, starget->id)) &&
		starget->channel == 0) ? 1 : 0;
}

static struct spi_function_template mptspi_transport_functions = {
	.get_offset	= mptspi_read_parameters,
	.set_offset	= mptspi_write_offset,
	.show_offset	= 1,
	.get_period	= mptspi_read_parameters,
	.set_period	= mptspi_write_period,
	.show_period	= 1,
	.get_width	= mptspi_read_parameters,
	.set_width	= mptspi_write_width,
	.show_width	= 1,
	.get_iu		= mptspi_read_parameters,
	.set_iu		= mptspi_write_iu,
	.show_iu	= 1,
	.get_dt		= mptspi_read_parameters,
	.set_dt		= mptspi_write_dt,
	.show_dt	= 1,
	.get_qas	= mptspi_read_parameters,
	.set_qas	= mptspi_write_qas,
	.show_qas	= 1,
	.get_wr_flow	= mptspi_read_parameters,
	.set_wr_flow	= mptspi_write_wr_flow,
	.show_wr_flow	= 1,
	.get_rd_strm	= mptspi_read_parameters,
	.set_rd_strm	= mptspi_write_rd_strm,
	.show_rd_strm	= 1,
	.get_rti	= mptspi_read_parameters,
	.set_rti	= mptspi_write_rti,
	.show_rti	= 1,
	.get_pcomp_en	= mptspi_read_parameters,
	.set_pcomp_en	= mptspi_write_pcomp_en,
	.show_pcomp_en	= 1,
	.get_hold_mcs	= mptspi_read_parameters,
	.set_hold_mcs	= mptspi_write_hold_mcs,
	.show_hold_mcs	= 1,
	.deny_binding	= mptspi_deny_binding,
};

/****************************************************************************
 * Supported hardware
 */

static const struct pci_device_id mptspi_pci_table[] = {
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_53C1030,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_ATTO, MPI_MANUFACTPAGE_DEVID_53C1030,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_53C1035,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mptspi_pci_table);


/*
 * renegotiate for a given target
 */
static void
mptspi_dv_renegotiate_work(struct work_struct *work)
{
	struct work_queue_wrapper *wqw =
		container_of(work, struct work_queue_wrapper, work);
	struct _MPT_SCSI_HOST *hd = wqw->hd;
	struct scsi_device *sdev;
	struct scsi_target *starget;
	struct _CONFIG_PAGE_SCSI_DEVICE_1 pg1;
	u32 nego;
	MPT_ADAPTER *ioc = hd->ioc;

	kfree(wqw);

	if (hd->spi_pending) {
		shost_for_each_device(sdev, ioc->sh) {
			if  (hd->spi_pending & (1 << sdev->id))
				continue;
			starget = scsi_target(sdev);
			nego = mptspi_getRP(starget);
			pg1.RequestedParameters = cpu_to_le32(nego);
			pg1.Reserved = 0;
			pg1.Configuration = 0;
			mptspi_write_spi_device_pg1(starget, &pg1);
		}
	} else {
		shost_for_each_device(sdev, ioc->sh)
			mptspi_dv_device(hd, sdev);
	}
}

static void
mptspi_dv_renegotiate(struct _MPT_SCSI_HOST *hd)
{
	struct work_queue_wrapper *wqw = kmalloc(sizeof(*wqw), GFP_ATOMIC);

	if (!wqw)
		return;

	INIT_WORK(&wqw->work, mptspi_dv_renegotiate_work);
	wqw->hd = hd;

	schedule_work(&wqw->work);
}

/*
 * spi module reset handler
 */
static int
mptspi_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	int rc;

	rc = mptscsih_ioc_reset(ioc, reset_phase);
	if ((ioc->bus_type != SPI) || (!rc))
		return rc;

	/* only try to do a renegotiation if we're properly set up
	 * if we get an ioc fault on bringup, ioc->sh will be NULL */
	if (reset_phase == MPT_IOC_POST_RESET &&
	    ioc->sh) {
		struct _MPT_SCSI_HOST *hd = shost_priv(ioc->sh);

		mptspi_dv_renegotiate(hd);
	}

	return rc;
}

#ifdef CONFIG_PM
/*
 * spi module resume handler
 */
static int
mptspi_resume(struct pci_dev *pdev)
{
	MPT_ADAPTER 	*ioc = pci_get_drvdata(pdev);
	struct _MPT_SCSI_HOST *hd = shost_priv(ioc->sh);
	int rc;

	rc = mptscsih_resume(pdev);
	mptspi_dv_renegotiate(hd);

	return rc;
}
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptspi_probe - Installs scsi devices per bus.
 *	@pdev: Pointer to pci_dev structure
 *
 *	Returns 0 for success, non-zero for failure.
 *
 */
static int
mptspi_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host	*sh;
	MPT_SCSI_HOST		*hd;
	MPT_ADAPTER 		*ioc;
	unsigned long		 flags;
	int			 ii;
	int			 numSGE = 0;
	int			 scale;
	int			 ioc_cap;
	int			error=0;
	int			r;

	if ((r = mpt_attach(pdev,id)) != 0)
		return r;

	ioc = pci_get_drvdata(pdev);
	ioc->DoneCtx = mptspiDoneCtx;
	ioc->TaskCtx = mptspiTaskCtx;
	ioc->InternalCtx = mptspiInternalCtx;

	/*  Added sanity check on readiness of the MPT adapter.
	 */
	if (ioc->last_state != MPI_IOC_STATE_OPERATIONAL) {
		printk(MYIOC_s_WARN_FMT
		  "Skipping because it's not operational!\n",
		  ioc->name);
		error = -ENODEV;
		goto out_mptspi_probe;
	}

	if (!ioc->active) {
		printk(MYIOC_s_WARN_FMT "Skipping because it's disabled!\n",
		  ioc->name);
		error = -ENODEV;
		goto out_mptspi_probe;
	}

	/*  Sanity check - ensure at least 1 port is INITIATOR capable
	 */
	ioc_cap = 0;
	for (ii=0; ii < ioc->facts.NumberOfPorts; ii++) {
		if (ioc->pfacts[ii].ProtocolFlags &
		    MPI_PORTFACTS_PROTOCOL_INITIATOR)
			ioc_cap ++;
	}

	if (!ioc_cap) {
		printk(MYIOC_s_WARN_FMT
			"Skipping ioc=%p because SCSI Initiator mode is NOT enabled!\n",
			ioc->name, ioc);
		return 0;
	}

	sh = scsi_host_alloc(&mptspi_driver_template, sizeof(MPT_SCSI_HOST));

	if (!sh) {
		printk(MYIOC_s_WARN_FMT
			"Unable to register controller with SCSI subsystem\n",
			ioc->name);
		error = -1;
		goto out_mptspi_probe;
        }

	/* VMWare emulation doesn't properly implement WRITE_SAME
	 */
	if (pdev->subsystem_vendor == 0x15AD)
		sh->no_write_same = 1;

	spin_lock_irqsave(&ioc->FreeQlock, flags);

	/* Attach the SCSI Host to the IOC structure
	 */
	ioc->sh = sh;

	sh->io_port = 0;
	sh->n_io_port = 0;
	sh->irq = 0;

	/* set 16 byte cdb's */
	sh->max_cmd_len = 16;

	/* Yikes!  This is important!
	 * Otherwise, by default, linux
	 * only scans target IDs 0-7!
	 * pfactsN->MaxDevices unreliable
	 * (not supported in early
	 *	versions of the FW).
	 * max_id = 1 + actual max id,
	 * max_lun = 1 + actual last lun,
	 *	see hosts.h :o(
	 */
	sh->max_id = ioc->devices_per_bus;

	sh->max_lun = MPT_LAST_LUN + 1;
	/*
	 * If RAID Firmware Detected, setup virtual channel
	 */
	if (ioc->ir_firmware)
		sh->max_channel = 1;
	else
		sh->max_channel = 0;
	sh->this_id = ioc->pfacts[0].PortSCSIID;

	/* Required entry.
	 */
	sh->unique_id = ioc->id;

	/* Verify that we won't exceed the maximum
	 * number of chain buffers
	 * We can optimize:  ZZ = req_sz/sizeof(SGE)
	 * For 32bit SGE's:
	 *  numSGE = 1 + (ZZ-1)*(maxChain -1) + ZZ
	 *               + (req_sz - 64)/sizeof(SGE)
	 * A slightly different algorithm is required for
	 * 64bit SGEs.
	 */
	scale = ioc->req_sz/ioc->SGE_size;
	if (ioc->sg_addr_size == sizeof(u64)) {
		numSGE = (scale - 1) *
		  (ioc->facts.MaxChainDepth-1) + scale +
		  (ioc->req_sz - 60) / ioc->SGE_size;
	} else {
		numSGE = 1 + (scale - 1) *
		  (ioc->facts.MaxChainDepth-1) + scale +
		  (ioc->req_sz - 64) / ioc->SGE_size;
	}

	if (numSGE < sh->sg_tablesize) {
		/* Reset this value */
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		  "Resetting sg_tablesize to %d from %d\n",
		  ioc->name, numSGE, sh->sg_tablesize));
		sh->sg_tablesize = numSGE;
	}

	spin_unlock_irqrestore(&ioc->FreeQlock, flags);

	hd = shost_priv(sh);
	hd->ioc = ioc;

	/* SCSI needs scsi_cmnd lookup table!
	 * (with size equal to req_depth*PtrSz!)
	 */
	ioc->ScsiLookup = kcalloc(ioc->req_depth, sizeof(void *), GFP_KERNEL);
	if (!ioc->ScsiLookup) {
		error = -ENOMEM;
		goto out_mptspi_probe;
	}
	spin_lock_init(&ioc->scsi_lookup_lock);

	dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ScsiLookup @ %p\n",
		 ioc->name, ioc->ScsiLookup));

	ioc->spi_data.Saf_Te = mpt_saf_te;
	ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		"saf_te %x\n",
		ioc->name,
		mpt_saf_te));
	ioc->spi_data.noQas = 0;

	hd->last_queue_full = 0;
	hd->spi_pending = 0;

	/* Some versions of the firmware don't support page 0; without
	 * that we can't get the parameters */
	if (ioc->spi_data.sdp0length != 0)
		sh->transportt = mptspi_transport_template;

	error = scsi_add_host (sh, &ioc->pcidev->dev);
	if(error) {
		dprintk(ioc, printk(MYIOC_s_ERR_FMT
		  "scsi_add_host failed\n", ioc->name));
		goto out_mptspi_probe;
	}

	/*
	 * issue internal bus reset
	 */
	if (ioc->spi_data.bus_reset)
		mptscsih_IssueTaskMgmt(hd,
		    MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS,
		    0, 0, 0, 0, 5);

	scsi_scan_host(sh);
	return 0;

out_mptspi_probe:

	mptscsih_remove(pdev);
	return error;
}

static void mptspi_remove(struct pci_dev *pdev)
{
	MPT_ADAPTER *ioc = pci_get_drvdata(pdev);

	scsi_remove_host(ioc->sh);
	mptscsih_remove(pdev);
}

static struct pci_driver mptspi_driver = {
	.name		= "mptspi",
	.id_table	= mptspi_pci_table,
	.probe		= mptspi_probe,
	.remove		= mptspi_remove,
	.shutdown	= mptscsih_shutdown,
#ifdef CONFIG_PM
	.suspend	= mptscsih_suspend,
	.resume		= mptspi_resume,
#endif
};

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptspi_init - Register MPT adapter(s) as SCSI host(s) with SCSI mid-layer.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int __init
mptspi_init(void)
{
	int error;

	show_mptmod_ver(my_NAME, my_VERSION);

	mptspi_transport_template = spi_attach_transport(&mptspi_transport_functions);
	if (!mptspi_transport_template)
		return -ENODEV;

	mptspiDoneCtx = mpt_register(mptscsih_io_done, MPTSPI_DRIVER,
	    "mptscsih_io_done");
	mptspiTaskCtx = mpt_register(mptscsih_taskmgmt_complete, MPTSPI_DRIVER,
	    "mptscsih_taskmgmt_complete");
	mptspiInternalCtx = mpt_register(mptscsih_scandv_complete,
	    MPTSPI_DRIVER, "mptscsih_scandv_complete");

	mpt_event_register(mptspiDoneCtx, mptspi_event_process);
	mpt_reset_register(mptspiDoneCtx, mptspi_ioc_reset);

	error = pci_register_driver(&mptspi_driver);
	if (error)
		spi_release_transport(mptspi_transport_template);

	return error;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptspi_exit - Unregisters MPT adapter(s)
 */
static void __exit
mptspi_exit(void)
{
	pci_unregister_driver(&mptspi_driver);

	mpt_reset_deregister(mptspiDoneCtx);
	mpt_event_deregister(mptspiDoneCtx);

	mpt_deregister(mptspiInternalCtx);
	mpt_deregister(mptspiTaskCtx);
	mpt_deregister(mptspiDoneCtx);
	spi_release_transport(mptspi_transport_template);
}

module_init(mptspi_init);
module_exit(mptspi_exit);
