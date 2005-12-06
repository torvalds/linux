/*
 *  linux/drivers/message/fusion/mptscsih.c
 *      For use with LSI Logic PCI chip/adapter(s)
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2005 LSI Logic Corporation
 *  (mailto:mpt_linux_developer@lsil.com)
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

#include "linux_compat.h"	/* linux-2.6 tweaks */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>	/* for mdelay */
#include <linux/interrupt.h>	/* needed for in_interrupt() proto */
#include <linux/reboot.h>	/* notifier code */
#include <linux/sched.h>
#include <linux/workqueue.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_dbg.h>

#include "mptbase.h"
#include "mptscsih.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#define my_NAME		"Fusion MPT SCSI Host driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptscsih"

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

typedef struct _BIG_SENSE_BUF {
	u8		data[MPT_SENSE_BUFFER_ALLOC];
} BIG_SENSE_BUF;

#define MPT_SCANDV_GOOD			(0x00000000) /* must be 0 */
#define MPT_SCANDV_DID_RESET		(0x00000001)
#define MPT_SCANDV_SENSE		(0x00000002)
#define MPT_SCANDV_SOME_ERROR		(0x00000004)
#define MPT_SCANDV_SELECTION_TIMEOUT	(0x00000008)
#define MPT_SCANDV_ISSUE_SENSE		(0x00000010)
#define MPT_SCANDV_FALLBACK		(0x00000020)

#define MPT_SCANDV_MAX_RETRIES		(10)

#define MPT_ICFLAG_BUF_CAP	0x01	/* ReadBuffer Read Capacity format */
#define MPT_ICFLAG_ECHO		0x02	/* ReadBuffer Echo buffer format */
#define MPT_ICFLAG_EBOS		0x04	/* ReadBuffer Echo buffer has EBOS */
#define MPT_ICFLAG_PHYS_DISK	0x08	/* Any SCSI IO but do Phys Disk Format */
#define MPT_ICFLAG_TAGGED_CMD	0x10	/* Do tagged IO */
#define MPT_ICFLAG_DID_RESET	0x20	/* Bus Reset occurred with this command */
#define MPT_ICFLAG_RESERVED	0x40	/* Reserved has been issued */

typedef struct _internal_cmd {
	char		*data;		/* data pointer */
	dma_addr_t	data_dma;	/* data dma address */
	int		size;		/* transfer size */
	u8		cmd;		/* SCSI Op Code */
	u8		bus;		/* bus number */
	u8		id;		/* SCSI ID (virtual) */
	u8		lun;
	u8		flags;		/* Bit Field - See above */
	u8		physDiskNum;	/* Phys disk number, -1 else */
	u8		rsvd2;
	u8		rsvd;
} INTERNAL_CMD;

typedef struct _negoparms {
	u8 width;
	u8 offset;
	u8 factor;
	u8 flags;
} NEGOPARMS;

typedef struct _dv_parameters {
	NEGOPARMS	 max;
	NEGOPARMS	 now;
	u8		 cmd;
	u8		 id;
	u16		 pad1;
} DVPARAMETERS;

/*
 *  Other private/forward protos...
 */
int		mptscsih_io_done(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *r);
static void	mptscsih_report_queue_full(struct scsi_cmnd *sc, SCSIIOReply_t *pScsiReply, SCSIIORequest_t *pScsiReq);
int		mptscsih_taskmgmt_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *r);

static int	mptscsih_AddSGE(MPT_ADAPTER *ioc, struct scsi_cmnd *SCpnt,
				 SCSIIORequest_t *pReq, int req_idx);
static void	mptscsih_freeChainBuffers(MPT_ADAPTER *ioc, int req_idx);
static void	mptscsih_copy_sense_data(struct scsi_cmnd *sc, MPT_SCSI_HOST *hd, MPT_FRAME_HDR *mf, SCSIIOReply_t *pScsiReply);
static int	mptscsih_tm_pending_wait(MPT_SCSI_HOST * hd);
static int	mptscsih_tm_wait_for_completion(MPT_SCSI_HOST * hd, ulong timeout );
static u32	SCPNT_TO_LOOKUP_IDX(struct scsi_cmnd *sc);

static int	mptscsih_TMHandler(MPT_SCSI_HOST *hd, u8 type, u8 channel, u8 target, u8 lun, int ctx2abort, ulong timeout);
static int	mptscsih_IssueTaskMgmt(MPT_SCSI_HOST *hd, u8 type, u8 channel, u8 target, u8 lun, int ctx2abort, ulong timeout);

int		mptscsih_ioc_reset(MPT_ADAPTER *ioc, int post_reset);
int		mptscsih_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *pEvReply);

static void	mptscsih_initTarget(MPT_SCSI_HOST *hd, int bus_id, int target_id, u8 lun, char *data, int dlen);
static void	mptscsih_setTargetNegoParms(MPT_SCSI_HOST *hd, VirtDevice *target, char byte56);
static void	mptscsih_set_dvflags(MPT_SCSI_HOST *hd, SCSIIORequest_t *pReq);
static void	mptscsih_setDevicePage1Flags (u8 width, u8 factor, u8 offset, int *requestedPtr, int *configurationPtr, u8 flags);
static void	mptscsih_no_negotiate(MPT_SCSI_HOST *hd, int target_id);
static int	mptscsih_writeSDP1(MPT_SCSI_HOST *hd, int portnum, int target, int flags);
static int	mptscsih_writeIOCPage4(MPT_SCSI_HOST *hd, int target_id, int bus);
int		mptscsih_scandv_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *r);
static int	mptscsih_do_cmd(MPT_SCSI_HOST *hd, INTERNAL_CMD *iocmd);
static int	mptscsih_synchronize_cache(MPT_SCSI_HOST *hd, int portnum);

static struct work_struct   mptscsih_persistTask;

#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
static int	mptscsih_do_raid(MPT_SCSI_HOST *hd, u8 action, INTERNAL_CMD *io);
static void	mptscsih_domainValidation(void *hd);
static int	mptscsih_is_phys_disk(MPT_ADAPTER *ioc, int id);
static void	mptscsih_qas_check(MPT_SCSI_HOST *hd, int id);
static int	mptscsih_doDv(MPT_SCSI_HOST *hd, int channel, int target);
static void	mptscsih_dv_parms(MPT_SCSI_HOST *hd, DVPARAMETERS *dv,void *pPage);
static void	mptscsih_fillbuf(char *buffer, int size, int index, int width);
static void	mptscsih_set_dvflags_raid(MPT_SCSI_HOST *hd, int id);
#endif

void 		mptscsih_remove(struct pci_dev *);
void 		mptscsih_shutdown(struct pci_dev *);
#ifdef CONFIG_PM
int 		mptscsih_suspend(struct pci_dev *pdev, pm_message_t state);
int 		mptscsih_resume(struct pci_dev *pdev);
#endif

#define SNS_LEN(scp)	sizeof((scp)->sense_buffer)

#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
/*
 * Domain Validation task structure
 */
static DEFINE_SPINLOCK(dvtaskQ_lock);
static int dvtaskQ_active = 0;
static int dvtaskQ_release = 0;
static struct work_struct	dvTaskQ_task;
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_add_sge - Place a simple SGE at address pAddr.
 *	@pAddr: virtual address for SGE
 *	@flagslength: SGE flags and data transfer length
 *	@dma_addr: Physical address
 *
 *	This routine places a MPT request frame back on the MPT adapter's
 *	FreeQ.
 */
static inline void
mptscsih_add_sge(char *pAddr, u32 flagslength, dma_addr_t dma_addr)
{
	if (sizeof(dma_addr_t) == sizeof(u64)) {
		SGESimple64_t *pSge = (SGESimple64_t *) pAddr;
		u32 tmp = dma_addr & 0xFFFFFFFF;

		pSge->FlagsLength = cpu_to_le32(flagslength);
		pSge->Address.Low = cpu_to_le32(tmp);
		tmp = (u32) ((u64)dma_addr >> 32);
		pSge->Address.High = cpu_to_le32(tmp);

	} else {
		SGESimple32_t *pSge = (SGESimple32_t *) pAddr;
		pSge->FlagsLength = cpu_to_le32(flagslength);
		pSge->Address = cpu_to_le32(dma_addr);
	}
} /* mptscsih_add_sge() */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_add_chain - Place a chain SGE at address pAddr.
 *	@pAddr: virtual address for SGE
 *	@next: nextChainOffset value (u32's)
 *	@length: length of next SGL segment
 *	@dma_addr: Physical address
 *
 *	This routine places a MPT request frame back on the MPT adapter's
 *	FreeQ.
 */
static inline void
mptscsih_add_chain(char *pAddr, u8 next, u16 length, dma_addr_t dma_addr)
{
	if (sizeof(dma_addr_t) == sizeof(u64)) {
		SGEChain64_t *pChain = (SGEChain64_t *) pAddr;
		u32 tmp = dma_addr & 0xFFFFFFFF;

		pChain->Length = cpu_to_le16(length);
		pChain->Flags = MPI_SGE_FLAGS_CHAIN_ELEMENT | mpt_addr_size();

		pChain->NextChainOffset = next;

		pChain->Address.Low = cpu_to_le32(tmp);
		tmp = (u32) ((u64)dma_addr >> 32);
		pChain->Address.High = cpu_to_le32(tmp);
	} else {
		SGEChain32_t *pChain = (SGEChain32_t *) pAddr;
		pChain->Length = cpu_to_le16(length);
		pChain->Flags = MPI_SGE_FLAGS_CHAIN_ELEMENT | mpt_addr_size();
		pChain->NextChainOffset = next;
		pChain->Address = cpu_to_le32(dma_addr);
	}
} /* mptscsih_add_chain() */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_getFreeChainBuffer - Function to get a free chain
 *	from the MPT_SCSI_HOST FreeChainQ.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@req_idx: Index of the SCSI IO request frame. (output)
 *
 *	return SUCCESS or FAILED
 */
static inline int
mptscsih_getFreeChainBuffer(MPT_ADAPTER *ioc, int *retIndex)
{
	MPT_FRAME_HDR *chainBuf;
	unsigned long flags;
	int rc;
	int chain_idx;

	dsgprintk((MYIOC_s_INFO_FMT "getFreeChainBuffer called\n",
			ioc->name));
	spin_lock_irqsave(&ioc->FreeQlock, flags);
	if (!list_empty(&ioc->FreeChainQ)) {
		int offset;

		chainBuf = list_entry(ioc->FreeChainQ.next, MPT_FRAME_HDR,
				u.frame.linkage.list);
		list_del(&chainBuf->u.frame.linkage.list);
		offset = (u8 *)chainBuf - (u8 *)ioc->ChainBuffer;
		chain_idx = offset / ioc->req_sz;
		rc = SUCCESS;
		dsgprintk((MYIOC_s_ERR_FMT "getFreeChainBuffer chainBuf=%p ChainBuffer=%p offset=%d chain_idx=%d\n",
			ioc->name, chainBuf, ioc->ChainBuffer, offset, chain_idx));
	} else {
		rc = FAILED;
		chain_idx = MPT_HOST_NO_CHAIN;
		dfailprintk((MYIOC_s_INFO_FMT "getFreeChainBuffer failed\n",
			ioc->name));
	}
	spin_unlock_irqrestore(&ioc->FreeQlock, flags);

	*retIndex = chain_idx;
	return rc;
} /* mptscsih_getFreeChainBuffer() */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_AddSGE - Add a SGE (plus chain buffers) to the
 *	SCSIIORequest_t Message Frame.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@SCpnt: Pointer to scsi_cmnd structure
 *	@pReq: Pointer to SCSIIORequest_t structure
 *
 *	Returns ...
 */
static int
mptscsih_AddSGE(MPT_ADAPTER *ioc, struct scsi_cmnd *SCpnt,
		SCSIIORequest_t *pReq, int req_idx)
{
	char 	*psge;
	char	*chainSge;
	struct scatterlist *sg;
	int	 frm_sz;
	int	 sges_left, sg_done;
	int	 chain_idx = MPT_HOST_NO_CHAIN;
	int	 sgeOffset;
	int	 numSgeSlots, numSgeThisFrame;
	u32	 sgflags, sgdir, thisxfer = 0;
	int	 chain_dma_off = 0;
	int	 newIndex;
	int	 ii;
	dma_addr_t v2;
	u32	RequestNB;

	sgdir = le32_to_cpu(pReq->Control) & MPI_SCSIIO_CONTROL_DATADIRECTION_MASK;
	if (sgdir == MPI_SCSIIO_CONTROL_WRITE)  {
		sgdir = MPT_TRANSFER_HOST_TO_IOC;
	} else {
		sgdir = MPT_TRANSFER_IOC_TO_HOST;
	}

	psge = (char *) &pReq->SGL;
	frm_sz = ioc->req_sz;

	/* Map the data portion, if any.
	 * sges_left  = 0 if no data transfer.
	 */
	if ( (sges_left = SCpnt->use_sg) ) {
		sges_left = pci_map_sg(ioc->pcidev,
			       (struct scatterlist *) SCpnt->request_buffer,
 			       SCpnt->use_sg,
			       SCpnt->sc_data_direction);
		if (sges_left == 0)
			return FAILED;
	} else if (SCpnt->request_bufflen) {
		SCpnt->SCp.dma_handle = pci_map_single(ioc->pcidev,
				      SCpnt->request_buffer,
				      SCpnt->request_bufflen,
				      SCpnt->sc_data_direction);
		dsgprintk((MYIOC_s_INFO_FMT "SG: non-SG for %p, len=%d\n",
				ioc->name, SCpnt, SCpnt->request_bufflen));
		mptscsih_add_sge((char *) &pReq->SGL,
			0xD1000000|MPT_SGE_FLAGS_ADDRESSING|sgdir|SCpnt->request_bufflen,
			SCpnt->SCp.dma_handle);

		return SUCCESS;
	}

	/* Handle the SG case.
	 */
	sg = (struct scatterlist *) SCpnt->request_buffer;
	sg_done  = 0;
	sgeOffset = sizeof(SCSIIORequest_t) - sizeof(SGE_IO_UNION);
	chainSge = NULL;

	/* Prior to entering this loop - the following must be set
	 * current MF:  sgeOffset (bytes)
	 *              chainSge (Null if original MF is not a chain buffer)
	 *              sg_done (num SGE done for this MF)
	 */

nextSGEset:
	numSgeSlots = ((frm_sz - sgeOffset) / (sizeof(u32) + sizeof(dma_addr_t)) );
	numSgeThisFrame = (sges_left < numSgeSlots) ? sges_left : numSgeSlots;

	sgflags = MPT_SGE_FLAGS_SIMPLE_ELEMENT | MPT_SGE_FLAGS_ADDRESSING | sgdir;

	/* Get first (num - 1) SG elements
	 * Skip any SG entries with a length of 0
	 * NOTE: at finish, sg and psge pointed to NEXT data/location positions
	 */
	for (ii=0; ii < (numSgeThisFrame-1); ii++) {
		thisxfer = sg_dma_len(sg);
		if (thisxfer == 0) {
			sg ++; /* Get next SG element from the OS */
			sg_done++;
			continue;
		}

		v2 = sg_dma_address(sg);
		mptscsih_add_sge(psge, sgflags | thisxfer, v2);

		sg++;		/* Get next SG element from the OS */
		psge += (sizeof(u32) + sizeof(dma_addr_t));
		sgeOffset += (sizeof(u32) + sizeof(dma_addr_t));
		sg_done++;
	}

	if (numSgeThisFrame == sges_left) {
		/* Add last element, end of buffer and end of list flags.
		 */
		sgflags |= MPT_SGE_FLAGS_LAST_ELEMENT |
				MPT_SGE_FLAGS_END_OF_BUFFER |
				MPT_SGE_FLAGS_END_OF_LIST;

		/* Add last SGE and set termination flags.
		 * Note: Last SGE may have a length of 0 - which should be ok.
		 */
		thisxfer = sg_dma_len(sg);

		v2 = sg_dma_address(sg);
		mptscsih_add_sge(psge, sgflags | thisxfer, v2);
		/*
		sg++;
		psge += (sizeof(u32) + sizeof(dma_addr_t));
		*/
		sgeOffset += (sizeof(u32) + sizeof(dma_addr_t));
		sg_done++;

		if (chainSge) {
			/* The current buffer is a chain buffer,
			 * but there is not another one.
			 * Update the chain element
			 * Offset and Length fields.
			 */
			mptscsih_add_chain((char *)chainSge, 0, sgeOffset, ioc->ChainBufferDMA + chain_dma_off);
		} else {
			/* The current buffer is the original MF
			 * and there is no Chain buffer.
			 */
			pReq->ChainOffset = 0;
			RequestNB = (((sgeOffset - 1) >> ioc->NBShiftFactor)  + 1) & 0x03;
			dsgprintk((MYIOC_s_INFO_FMT
			    "Single Buffer RequestNB=%x, sgeOffset=%d\n", ioc->name, RequestNB, sgeOffset));
			ioc->RequestNB[req_idx] = RequestNB;
		}
	} else {
		/* At least one chain buffer is needed.
		 * Complete the first MF
		 *  - last SGE element, set the LastElement bit
		 *  - set ChainOffset (words) for orig MF
		 *             (OR finish previous MF chain buffer)
		 *  - update MFStructPtr ChainIndex
		 *  - Populate chain element
		 * Also
		 * Loop until done.
		 */

		dsgprintk((MYIOC_s_INFO_FMT "SG: Chain Required! sg done %d\n",
				ioc->name, sg_done));

		/* Set LAST_ELEMENT flag for last non-chain element
		 * in the buffer. Since psge points at the NEXT
		 * SGE element, go back one SGE element, update the flags
		 * and reset the pointer. (Note: sgflags & thisxfer are already
		 * set properly).
		 */
		if (sg_done) {
			u32 *ptmp = (u32 *) (psge - (sizeof(u32) + sizeof(dma_addr_t)));
			sgflags = le32_to_cpu(*ptmp);
			sgflags |= MPT_SGE_FLAGS_LAST_ELEMENT;
			*ptmp = cpu_to_le32(sgflags);
		}

		if (chainSge) {
			/* The current buffer is a chain buffer.
			 * chainSge points to the previous Chain Element.
			 * Update its chain element Offset and Length (must
			 * include chain element size) fields.
			 * Old chain element is now complete.
			 */
			u8 nextChain = (u8) (sgeOffset >> 2);
			sgeOffset += (sizeof(u32) + sizeof(dma_addr_t));
			mptscsih_add_chain((char *)chainSge, nextChain, sgeOffset, ioc->ChainBufferDMA + chain_dma_off);
		} else {
			/* The original MF buffer requires a chain buffer -
			 * set the offset.
			 * Last element in this MF is a chain element.
			 */
			pReq->ChainOffset = (u8) (sgeOffset >> 2);
			RequestNB = (((sgeOffset - 1) >> ioc->NBShiftFactor)  + 1) & 0x03;
			dsgprintk((MYIOC_s_ERR_FMT "Chain Buffer Needed, RequestNB=%x sgeOffset=%d\n", ioc->name, RequestNB, sgeOffset));
			ioc->RequestNB[req_idx] = RequestNB;
		}

		sges_left -= sg_done;


		/* NOTE: psge points to the beginning of the chain element
		 * in current buffer. Get a chain buffer.
		 */
		if ((mptscsih_getFreeChainBuffer(ioc, &newIndex)) == FAILED) {
			dfailprintk((MYIOC_s_INFO_FMT
			    "getFreeChainBuffer FAILED SCSI cmd=%02x (%p)\n",
 			    ioc->name, pReq->CDB[0], SCpnt));
			return FAILED;
		}

		/* Update the tracking arrays.
		 * If chainSge == NULL, update ReqToChain, else ChainToChain
		 */
		if (chainSge) {
			ioc->ChainToChain[chain_idx] = newIndex;
		} else {
			ioc->ReqToChain[req_idx] = newIndex;
		}
		chain_idx = newIndex;
		chain_dma_off = ioc->req_sz * chain_idx;

		/* Populate the chainSGE for the current buffer.
		 * - Set chain buffer pointer to psge and fill
		 *   out the Address and Flags fields.
		 */
		chainSge = (char *) psge;
		dsgprintk((KERN_INFO "  Current buff @ %p (index 0x%x)",
				psge, req_idx));

		/* Start the SGE for the next buffer
		 */
		psge = (char *) (ioc->ChainBuffer + chain_dma_off);
		sgeOffset = 0;
		sg_done = 0;

		dsgprintk((KERN_INFO "  Chain buff @ %p (index 0x%x)\n",
				psge, chain_idx));

		/* Start the SGE for the next buffer
		 */

		goto nextSGEset;
	}

	return SUCCESS;
} /* mptscsih_AddSGE() */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_io_done - Main SCSI IO callback routine registered to
 *	Fusion MPT (base) driver
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mf: Pointer to original MPT request frame
 *	@r: Pointer to MPT reply frame (NULL if TurboReply)
 *
 *	This routine is called from mpt.c::mpt_interrupt() at the completion
 *	of any SCSI IO request.
 *	This routine is registered with the Fusion MPT (base) driver at driver
 *	load/init time via the mpt_register() API call.
 *
 *	Returns 1 indicating alloc'd request frame ptr should be freed.
 */
int
mptscsih_io_done(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *mr)
{
	struct scsi_cmnd	*sc;
	MPT_SCSI_HOST	*hd;
	SCSIIORequest_t	*pScsiReq;
	SCSIIOReply_t	*pScsiReply;
	u16		 req_idx;

	hd = (MPT_SCSI_HOST *) ioc->sh->hostdata;

	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);
	sc = hd->ScsiLookup[req_idx];
	if (sc == NULL) {
		MPIHeader_t *hdr = (MPIHeader_t *)mf;

		/* Remark: writeSDP1 will use the ScsiDoneCtx
		 * If a SCSI I/O cmd, device disabled by OS and
		 * completion done. Cannot touch sc struct. Just free mem.
		 */
		if (hdr->Function == MPI_FUNCTION_SCSI_IO_REQUEST)
			printk(MYIOC_s_ERR_FMT "NULL ScsiCmd ptr!\n",
			ioc->name);

		mptscsih_freeChainBuffers(ioc, req_idx);
		return 1;
	}

	sc->result = DID_OK << 16;		/* Set default reply as OK */
	pScsiReq = (SCSIIORequest_t *) mf;
	pScsiReply = (SCSIIOReply_t *) mr;

	if((ioc->facts.MsgVersion >= MPI_VERSION_01_05) && pScsiReply){
		dmfprintk((MYIOC_s_INFO_FMT
			"ScsiDone (mf=%p,mr=%p,sc=%p,idx=%d,task-tag=%d)\n",
			ioc->name, mf, mr, sc, req_idx, pScsiReply->TaskTag));
	}else{
		dmfprintk((MYIOC_s_INFO_FMT
			"ScsiDone (mf=%p,mr=%p,sc=%p,idx=%d)\n",
			ioc->name, mf, mr, sc, req_idx));
	}

	if (pScsiReply == NULL) {
		/* special context reply handling */
		;
	} else {
		u32	 xfer_cnt;
		u16	 status;
		u8	 scsi_state, scsi_status;

		status = le16_to_cpu(pScsiReply->IOCStatus) & MPI_IOCSTATUS_MASK;
		scsi_state = pScsiReply->SCSIState;
		scsi_status = pScsiReply->SCSIStatus;
		xfer_cnt = le32_to_cpu(pScsiReply->TransferCount);
		sc->resid = sc->request_bufflen - xfer_cnt;

		/*
		 *  if we get a data underrun indication, yet no data was
		 *  transferred and the SCSI status indicates that the
		 *  command was never started, change the data underrun
		 *  to success
		 */
		if (status == MPI_IOCSTATUS_SCSI_DATA_UNDERRUN && xfer_cnt == 0 &&
		    (scsi_status == MPI_SCSI_STATUS_BUSY ||
		     scsi_status == MPI_SCSI_STATUS_RESERVATION_CONFLICT ||
		     scsi_status == MPI_SCSI_STATUS_TASK_SET_FULL)) {
			status = MPI_IOCSTATUS_SUCCESS;
		}

		dreplyprintk((KERN_NOTICE "Reply ha=%d id=%d lun=%d:\n"
			"IOCStatus=%04xh SCSIState=%02xh SCSIStatus=%02xh\n"
			"resid=%d bufflen=%d xfer_cnt=%d\n",
			ioc->id, pScsiReq->TargetID, pScsiReq->LUN[1],
			status, scsi_state, scsi_status, sc->resid,
			sc->request_bufflen, xfer_cnt));

		if (scsi_state & MPI_SCSI_STATE_AUTOSENSE_VALID)
			mptscsih_copy_sense_data(sc, hd, mf, pScsiReply);

		/*
		 *  Look for + dump FCP ResponseInfo[]!
		 */
		if (scsi_state & MPI_SCSI_STATE_RESPONSE_INFO_VALID &&
		    pScsiReply->ResponseInfo) {
			printk(KERN_NOTICE "ha=%d id=%d lun=%d: "
			"FCP_ResponseInfo=%08xh\n",
			ioc->id, pScsiReq->TargetID, pScsiReq->LUN[1],
			le32_to_cpu(pScsiReply->ResponseInfo));
		}

		switch(status) {
		case MPI_IOCSTATUS_BUSY:			/* 0x0002 */
			/* CHECKME!
			 * Maybe: DRIVER_BUSY | SUGGEST_RETRY | DID_SOFT_ERROR (retry)
			 * But not: DID_BUS_BUSY lest one risk
			 * killing interrupt handler:-(
			 */
			sc->result = SAM_STAT_BUSY;
			break;

		case MPI_IOCSTATUS_SCSI_INVALID_BUS:		/* 0x0041 */
		case MPI_IOCSTATUS_SCSI_INVALID_TARGETID:	/* 0x0042 */
			sc->result = DID_BAD_TARGET << 16;
			break;

		case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE:	/* 0x0043 */
			/* Spoof to SCSI Selection Timeout! */
			sc->result = DID_NO_CONNECT << 16;

			if (hd->sel_timeout[pScsiReq->TargetID] < 0xFFFF)
				hd->sel_timeout[pScsiReq->TargetID]++;
			break;

		case MPI_IOCSTATUS_SCSI_TASK_TERMINATED:	/* 0x0048 */
		case MPI_IOCSTATUS_SCSI_IOC_TERMINATED:		/* 0x004B */
		case MPI_IOCSTATUS_SCSI_EXT_TERMINATED:		/* 0x004C */
			/* Linux handles an unsolicited DID_RESET better
			 * than an unsolicited DID_ABORT.
			 */
			sc->result = DID_RESET << 16;

			/* GEM Workaround. */
			if (ioc->bus_type == SCSI)
				mptscsih_no_negotiate(hd, sc->device->id);
			break;

		case MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:	/* 0x0049 */
			sc->resid = sc->request_bufflen - xfer_cnt;
			if((xfer_cnt==0)||(sc->underflow > xfer_cnt))
				sc->result=DID_SOFT_ERROR << 16;
			else /* Sufficient data transfer occurred */
				sc->result = (DID_OK << 16) | scsi_status;
			dreplyprintk((KERN_NOTICE 
			    "RESIDUAL_MISMATCH: result=%x on id=%d\n", sc->result, sc->device->id));
			break;

		case MPI_IOCSTATUS_SCSI_DATA_UNDERRUN:		/* 0x0045 */
			/*
			 *  Do upfront check for valid SenseData and give it
			 *  precedence!
			 */
			sc->result = (DID_OK << 16) | scsi_status;
			if (scsi_state & MPI_SCSI_STATE_AUTOSENSE_VALID) {
				/* Have already saved the status and sense data
				 */
				;
			} else {
				if (xfer_cnt < sc->underflow) {
					if (scsi_status == SAM_STAT_BUSY)
						sc->result = SAM_STAT_BUSY;
					else
						sc->result = DID_SOFT_ERROR << 16;
				}
				if (scsi_state & (MPI_SCSI_STATE_AUTOSENSE_FAILED | MPI_SCSI_STATE_NO_SCSI_STATUS)) {
					/* What to do?
				 	*/
					sc->result = DID_SOFT_ERROR << 16;
				}
				else if (scsi_state & MPI_SCSI_STATE_TERMINATED) {
					/*  Not real sure here either...  */
					sc->result = DID_RESET << 16;
				}
			}

			dreplyprintk((KERN_NOTICE "  sc->underflow={report ERR if < %02xh bytes xfer'd}\n",
					sc->underflow));
			dreplyprintk((KERN_NOTICE "  ActBytesXferd=%02xh\n", xfer_cnt));
			/* Report Queue Full
			 */
			if (scsi_status == MPI_SCSI_STATUS_TASK_SET_FULL)
				mptscsih_report_queue_full(sc, pScsiReply, pScsiReq);

			break;

		case MPI_IOCSTATUS_SCSI_RECOVERED_ERROR:	/* 0x0040 */
		case MPI_IOCSTATUS_SUCCESS:			/* 0x0000 */
			if (scsi_status == MPI_SCSI_STATUS_BUSY)
				sc->result = (DID_BUS_BUSY << 16) | scsi_status;
			else
				sc->result = (DID_OK << 16) | scsi_status;
			if (scsi_state == 0) {
				;
			} else if (scsi_state & MPI_SCSI_STATE_AUTOSENSE_VALID) {
				/*
				 * If running against circa 200003dd 909 MPT f/w,
				 * may get this (AUTOSENSE_VALID) for actual TASK_SET_FULL
				 * (QUEUE_FULL) returned from device! --> get 0x0000?128
				 * and with SenseBytes set to 0.
				 */
				if (pScsiReply->SCSIStatus == MPI_SCSI_STATUS_TASK_SET_FULL)
					mptscsih_report_queue_full(sc, pScsiReply, pScsiReq);

			}
			else if (scsi_state &
			         (MPI_SCSI_STATE_AUTOSENSE_FAILED | MPI_SCSI_STATE_NO_SCSI_STATUS)
			   ) {
				/*
				 * What to do?
				 */
				sc->result = DID_SOFT_ERROR << 16;
			}
			else if (scsi_state & MPI_SCSI_STATE_TERMINATED) {
				/*  Not real sure here either...  */
				sc->result = DID_RESET << 16;
			}
			else if (scsi_state & MPI_SCSI_STATE_QUEUE_TAG_REJECTED) {
				/* Device Inq. data indicates that it supports
				 * QTags, but rejects QTag messages.
				 * This command completed OK.
				 *
				 * Not real sure here either so do nothing...  */
			}

			if (sc->result == MPI_SCSI_STATUS_TASK_SET_FULL)
				mptscsih_report_queue_full(sc, pScsiReply, pScsiReq);

			/* Add handling of:
			 * Reservation Conflict, Busy,
			 * Command Terminated, CHECK
			 */
			break;

		case MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR:		/* 0x0047 */
			sc->result = DID_SOFT_ERROR << 16;
			break;

		case MPI_IOCSTATUS_INVALID_FUNCTION:		/* 0x0001 */
		case MPI_IOCSTATUS_INVALID_SGL:			/* 0x0003 */
		case MPI_IOCSTATUS_INTERNAL_ERROR:		/* 0x0004 */
		case MPI_IOCSTATUS_RESERVED:			/* 0x0005 */
		case MPI_IOCSTATUS_INSUFFICIENT_RESOURCES:	/* 0x0006 */
		case MPI_IOCSTATUS_INVALID_FIELD:		/* 0x0007 */
		case MPI_IOCSTATUS_INVALID_STATE:		/* 0x0008 */
		case MPI_IOCSTATUS_SCSI_DATA_OVERRUN:		/* 0x0044 */
		case MPI_IOCSTATUS_SCSI_IO_DATA_ERROR:		/* 0x0046 */
		case MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED:	/* 0x004A */
		default:
			/*
			 * What to do?
			 */
			sc->result = DID_SOFT_ERROR << 16;
			break;

		}	/* switch(status) */

		dreplyprintk((KERN_NOTICE "  sc->result is %08xh\n", sc->result));
	} /* end of address reply case */

	/* Unmap the DMA buffers, if any. */
	if (sc->use_sg) {
		pci_unmap_sg(ioc->pcidev, (struct scatterlist *) sc->request_buffer,
			    sc->use_sg, sc->sc_data_direction);
	} else if (sc->request_bufflen) {
		pci_unmap_single(ioc->pcidev, sc->SCp.dma_handle,
				sc->request_bufflen, sc->sc_data_direction);
	}

	hd->ScsiLookup[req_idx] = NULL;

	sc->scsi_done(sc);		/* Issue the command callback */

	/* Free Chain buffers */
	mptscsih_freeChainBuffers(ioc, req_idx);
	return 1;
}

/*
 *	mptscsih_flush_running_cmds - For each command found, search
 *		Scsi_Host instance taskQ and reply to OS.
 *		Called only if recovering from a FW reload.
 *	@hd: Pointer to a SCSI HOST structure
 *
 *	Returns: None.
 *
 *	Must be called while new I/Os are being queued.
 */
static void
mptscsih_flush_running_cmds(MPT_SCSI_HOST *hd)
{
	MPT_ADAPTER *ioc = hd->ioc;
	struct scsi_cmnd	*SCpnt;
	MPT_FRAME_HDR	*mf;
	int		 ii;
	int		 max = ioc->req_depth;

	dprintk((KERN_INFO MYNAM ": flush_ScsiLookup called\n"));
	for (ii= 0; ii < max; ii++) {
		if ((SCpnt = hd->ScsiLookup[ii]) != NULL) {

			/* Command found.
			 */

			/* Null ScsiLookup index
			 */
			hd->ScsiLookup[ii] = NULL;

			mf = MPT_INDEX_2_MFPTR(ioc, ii);
			dmfprintk(( "flush: ScsiDone (mf=%p,sc=%p)\n",
					mf, SCpnt));

			/* Set status, free OS resources (SG DMA buffers)
			 * Do OS callback
			 * Free driver resources (chain, msg buffers)
			 */
			if (SCpnt->use_sg) {
				pci_unmap_sg(ioc->pcidev,
					(struct scatterlist *) SCpnt->request_buffer,
					SCpnt->use_sg,
					SCpnt->sc_data_direction);
			} else if (SCpnt->request_bufflen) {
				pci_unmap_single(ioc->pcidev,
					SCpnt->SCp.dma_handle,
					SCpnt->request_bufflen,
					SCpnt->sc_data_direction);
			}
			SCpnt->result = DID_RESET << 16;
			SCpnt->host_scribble = NULL;

			/* Free Chain buffers */
			mptscsih_freeChainBuffers(ioc, ii);

			/* Free Message frames */
			mpt_free_msg_frame(ioc, mf);

			SCpnt->scsi_done(SCpnt);	/* Issue the command callback */
		}
	}

	return;
}

/*
 *	mptscsih_search_running_cmds - Delete any commands associated
 *		with the specified target and lun. Function called only
 *		when a lun is disable by mid-layer.
 *		Do NOT access the referenced scsi_cmnd structure or
 *		members. Will cause either a paging or NULL ptr error.
 *	@hd: Pointer to a SCSI HOST structure
 *	@target: target id
 *	@lun: lun
 *
 *	Returns: None.
 *
 *	Called from slave_destroy.
 */
static void
mptscsih_search_running_cmds(MPT_SCSI_HOST *hd, uint target, uint lun)
{
	SCSIIORequest_t	*mf = NULL;
	int		 ii;
	int		 max = hd->ioc->req_depth;
	struct scsi_cmnd *sc;

	dsprintk((KERN_INFO MYNAM ": search_running target %d lun %d max %d\n",
			target, lun, max));

	for (ii=0; ii < max; ii++) {
		if ((sc = hd->ScsiLookup[ii]) != NULL) {

			mf = (SCSIIORequest_t *)MPT_INDEX_2_MFPTR(hd->ioc, ii);

			dsprintk(( "search_running: found (sc=%p, mf = %p) target %d, lun %d \n",
					hd->ScsiLookup[ii], mf, mf->TargetID, mf->LUN[1]));

			if ((mf->TargetID != ((u8)target)) || (mf->LUN[1] != ((u8) lun)))
				continue;

			/* Cleanup
			 */
			hd->ScsiLookup[ii] = NULL;
			mptscsih_freeChainBuffers(hd->ioc, ii);
			mpt_free_msg_frame(hd->ioc, (MPT_FRAME_HDR *)mf);
			if (sc->use_sg) {
				pci_unmap_sg(hd->ioc->pcidev,
				(struct scatterlist *) sc->request_buffer,
					sc->use_sg,
					sc->sc_data_direction);
			} else if (sc->request_bufflen) {
				pci_unmap_single(hd->ioc->pcidev,
					sc->SCp.dma_handle,
					sc->request_bufflen,
					sc->sc_data_direction);
			}
			sc->host_scribble = NULL;
			sc->result = DID_NO_CONNECT << 16;
			sc->scsi_done(sc);
		}
	}
	return;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_report_queue_full - Report QUEUE_FULL status returned
 *	from a SCSI target device.
 *	@sc: Pointer to scsi_cmnd structure
 *	@pScsiReply: Pointer to SCSIIOReply_t
 *	@pScsiReq: Pointer to original SCSI request
 *
 *	This routine periodically reports QUEUE_FULL status returned from a
 *	SCSI target device.  It reports this to the console via kernel
 *	printk() API call, not more than once every 10 seconds.
 */
static void
mptscsih_report_queue_full(struct scsi_cmnd *sc, SCSIIOReply_t *pScsiReply, SCSIIORequest_t *pScsiReq)
{
	long time = jiffies;
	MPT_SCSI_HOST		*hd;

	if (sc->device == NULL)
		return;
	if (sc->device->host == NULL)
		return;
	if ((hd = (MPT_SCSI_HOST *)sc->device->host->hostdata) == NULL)
		return;

	if (time - hd->last_queue_full > 10 * HZ) {
		dprintk((MYIOC_s_WARN_FMT "Device (%d:%d:%d) reported QUEUE_FULL!\n",
				hd->ioc->name, 0, sc->device->id, sc->device->lun));
		hd->last_queue_full = time;
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_remove - Removed scsi devices
 *	@pdev: Pointer to pci_dev structure
 *
 *
 */
void
mptscsih_remove(struct pci_dev *pdev)
{
	MPT_ADAPTER 		*ioc = pci_get_drvdata(pdev);
	struct Scsi_Host 	*host = ioc->sh;
	MPT_SCSI_HOST		*hd;
	int 		 	count;
	unsigned long	 	flags;
	int sz1;

	if(!host) {
		mpt_detach(pdev);
		return;
	}

	scsi_remove_host(host);

	if((hd = (MPT_SCSI_HOST *)host->hostdata) == NULL)
		return;

#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
	/* Check DV thread active */
	count = 10 * HZ;
	spin_lock_irqsave(&dvtaskQ_lock, flags);
	if (dvtaskQ_active) {
		spin_unlock_irqrestore(&dvtaskQ_lock, flags);
		while(dvtaskQ_active && --count)
			schedule_timeout_interruptible(1);
	} else {
		spin_unlock_irqrestore(&dvtaskQ_lock, flags);
	}
	if (!count)
		printk(KERN_ERR MYNAM ": ERROR - DV thread still active!\n");
#if defined(MPT_DEBUG_DV) || defined(MPT_DEBUG_DV_TINY)
	else
		printk(KERN_ERR MYNAM ": DV thread orig %d, count %d\n", 10 * HZ, count);
#endif
#endif

	mptscsih_shutdown(pdev);

	sz1=0;

	if (hd->ScsiLookup != NULL) {
		sz1 = hd->ioc->req_depth * sizeof(void *);
		kfree(hd->ScsiLookup);
		hd->ScsiLookup = NULL;
	}

	/*
	 * Free pointer array.
	 */
	kfree(hd->Targets);
	hd->Targets = NULL;

	dprintk((MYIOC_s_INFO_FMT
	    "Free'd ScsiLookup (%d) memory\n",
	    hd->ioc->name, sz1));

	kfree(hd->info_kbuf);

	/* NULL the Scsi_Host pointer
	 */
	hd->ioc->sh = NULL;

	scsi_host_put(host);

	mpt_detach(pdev);

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_shutdown - reboot notifier
 *
 */
void
mptscsih_shutdown(struct pci_dev *pdev)
{
	MPT_ADAPTER 		*ioc = pci_get_drvdata(pdev);
	struct Scsi_Host 	*host = ioc->sh;
	MPT_SCSI_HOST		*hd;

	if(!host)
		return;

	hd = (MPT_SCSI_HOST *)host->hostdata;

	/* Flush the cache of this adapter
	 */
	if(hd != NULL)
		mptscsih_synchronize_cache(hd, 0);

}

#ifdef CONFIG_PM
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_suspend - Fusion MPT scsi driver suspend routine.
 *
 *
 */
int
mptscsih_suspend(struct pci_dev *pdev, pm_message_t state)
{
	mptscsih_shutdown(pdev);
	return mpt_suspend(pdev,state);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_resume - Fusion MPT scsi driver resume routine.
 *
 *
 */
int
mptscsih_resume(struct pci_dev *pdev)
{
	MPT_ADAPTER 		*ioc = pci_get_drvdata(pdev);
	struct Scsi_Host 	*host = ioc->sh;
	MPT_SCSI_HOST		*hd;

	mpt_resume(pdev);

	if(!host)
		return 0;

	hd = (MPT_SCSI_HOST *)host->hostdata;
	if(!hd)
		return 0;

#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
	{
	unsigned long lflags;
	spin_lock_irqsave(&dvtaskQ_lock, lflags);
	if (!dvtaskQ_active) {
		dvtaskQ_active = 1;
		spin_unlock_irqrestore(&dvtaskQ_lock, lflags);
		INIT_WORK(&dvTaskQ_task,
		  mptscsih_domainValidation, (void *) hd);
		schedule_work(&dvTaskQ_task);
	} else {
		spin_unlock_irqrestore(&dvtaskQ_lock, lflags);
	}
	}
#endif
	return 0;
}

#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_info - Return information about MPT adapter
 *	@SChost: Pointer to Scsi_Host structure
 *
 *	(linux scsi_host_template.info routine)
 *
 *	Returns pointer to buffer where information was written.
 */
const char *
mptscsih_info(struct Scsi_Host *SChost)
{
	MPT_SCSI_HOST *h;
	int size = 0;

	h = (MPT_SCSI_HOST *)SChost->hostdata;

	if (h) {
		if (h->info_kbuf == NULL)
			if ((h->info_kbuf = kmalloc(0x1000 /* 4Kb */, GFP_KERNEL)) == NULL)
				return h->info_kbuf;
		h->info_kbuf[0] = '\0';

		mpt_print_ioc_summary(h->ioc, h->info_kbuf, &size, 0, 0);
		h->info_kbuf[size-1] = '\0';
	}

	return h->info_kbuf;
}

struct info_str {
	char *buffer;
	int   length;
	int   offset;
	int   pos;
};

static void
mptscsih_copy_mem_info(struct info_str *info, char *data, int len)
{
	if (info->pos + len > info->length)
		len = info->length - info->pos;

	if (info->pos + len < info->offset) {
		info->pos += len;
		return;
	}

	if (info->pos < info->offset) {
	        data += (info->offset - info->pos);
	        len  -= (info->offset - info->pos);
	}

	if (len > 0) {
                memcpy(info->buffer + info->pos, data, len);
                info->pos += len;
	}
}

static int
mptscsih_copy_info(struct info_str *info, char *fmt, ...)
{
	va_list args;
	char buf[81];
	int len;

	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	mptscsih_copy_mem_info(info, buf, len);
	return len;
}

static int
mptscsih_host_info(MPT_ADAPTER *ioc, char *pbuf, off_t offset, int len)
{
	struct info_str info;

	info.buffer	= pbuf;
	info.length	= len;
	info.offset	= offset;
	info.pos	= 0;

	mptscsih_copy_info(&info, "%s: %s, ", ioc->name, ioc->prod_name);
	mptscsih_copy_info(&info, "%s%08xh, ", MPT_FW_REV_MAGIC_ID_STRING, ioc->facts.FWVersion.Word);
	mptscsih_copy_info(&info, "Ports=%d, ", ioc->facts.NumberOfPorts);
	mptscsih_copy_info(&info, "MaxQ=%d\n", ioc->req_depth);

	return ((info.pos > info.offset) ? info.pos - info.offset : 0);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_proc_info - Return information about MPT adapter
 *
 *	(linux scsi_host_template.info routine)
 *
 * 	buffer: if write, user data; if read, buffer for user
 * 	length: if write, return length;
 * 	offset: if write, 0; if read, the current offset into the buffer from
 * 		the previous read.
 * 	hostno: scsi host number
 *	func:   if write = 1; if read = 0
 */
int
mptscsih_proc_info(struct Scsi_Host *host, char *buffer, char **start, off_t offset,
			int length, int func)
{
	MPT_SCSI_HOST	*hd = (MPT_SCSI_HOST *)host->hostdata;
	MPT_ADAPTER	*ioc = hd->ioc;
	int size = 0;

	if (func) {
		/*
		 * write is not supported
		 */
	} else {
		if (start)
			*start = buffer;

		size = mptscsih_host_info(ioc, buffer, offset, length);
	}

	return size;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#define ADD_INDEX_LOG(req_ent)	do { } while(0)

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_qcmd - Primary Fusion MPT SCSI initiator IO start routine.
 *	@SCpnt: Pointer to scsi_cmnd structure
 *	@done: Pointer SCSI mid-layer IO completion function
 *
 *	(linux scsi_host_template.queuecommand routine)
 *	This is the primary SCSI IO start routine.  Create a MPI SCSIIORequest
 *	from a linux scsi_cmnd request and send it to the IOC.
 *
 *	Returns 0. (rtn value discarded by linux scsi mid-layer)
 */
int
mptscsih_qcmd(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *))
{
	MPT_SCSI_HOST		*hd;
	MPT_FRAME_HDR		*mf;
	SCSIIORequest_t		*pScsiReq;
	VirtDevice		*pTarget = SCpnt->device->hostdata;
	int	 lun;
	u32	 datalen;
	u32	 scsictl;
	u32	 scsidir;
	u32	 cmd_len;
	int	 my_idx;
	int	 ii;

	hd = (MPT_SCSI_HOST *) SCpnt->device->host->hostdata;
	lun = SCpnt->device->lun;
	SCpnt->scsi_done = done;

	dmfprintk((MYIOC_s_INFO_FMT "qcmd: SCpnt=%p, done()=%p\n",
			(hd && hd->ioc) ? hd->ioc->name : "ioc?", SCpnt, done));

	if (hd->resetPending) {
		dtmprintk((MYIOC_s_WARN_FMT "qcmd: SCpnt=%p timeout + 60HZ\n",
			(hd && hd->ioc) ? hd->ioc->name : "ioc?", SCpnt));
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/*
	 *  Put together a MPT SCSI request...
	 */
	if ((mf = mpt_get_msg_frame(hd->ioc->DoneCtx, hd->ioc)) == NULL) {
		dprintk((MYIOC_s_WARN_FMT "QueueCmd, no msg frames!!\n",
				hd->ioc->name));
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	pScsiReq = (SCSIIORequest_t *) mf;

	my_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);

	ADD_INDEX_LOG(my_idx);

	/*    TUR's being issued with scsictl=0x02000000 (DATA_IN)!
	 *    Seems we may receive a buffer (datalen>0) even when there
	 *    will be no data transfer!  GRRRRR...
	 */
	if (SCpnt->sc_data_direction == DMA_FROM_DEVICE) {
		datalen = SCpnt->request_bufflen;
		scsidir = MPI_SCSIIO_CONTROL_READ;	/* DATA IN  (host<--ioc<--dev) */
	} else if (SCpnt->sc_data_direction == DMA_TO_DEVICE) {
		datalen = SCpnt->request_bufflen;
		scsidir = MPI_SCSIIO_CONTROL_WRITE;	/* DATA OUT (host-->ioc-->dev) */
	} else {
		datalen = 0;
		scsidir = MPI_SCSIIO_CONTROL_NODATATRANSFER;
	}

	/* Default to untagged. Once a target structure has been allocated,
	 * use the Inquiry data to determine if device supports tagged.
	 */
	if (pTarget
	    && (pTarget->tflags & MPT_TARGET_FLAGS_Q_YES)
	    && (SCpnt->device->tagged_supported)) {
		scsictl = scsidir | MPI_SCSIIO_CONTROL_SIMPLEQ;
	} else {
		scsictl = scsidir | MPI_SCSIIO_CONTROL_UNTAGGED;
	}

	/* Use the above information to set up the message frame
	 */
	pScsiReq->TargetID = (u8) pTarget->target_id;
	pScsiReq->Bus = pTarget->bus_id;
	pScsiReq->ChainOffset = 0;
	pScsiReq->Function = MPI_FUNCTION_SCSI_IO_REQUEST;
	pScsiReq->CDBLength = SCpnt->cmd_len;
	pScsiReq->SenseBufferLength = MPT_SENSE_BUFFER_SIZE;
	pScsiReq->Reserved = 0;
	pScsiReq->MsgFlags = mpt_msg_flags();
	pScsiReq->LUN[0] = 0;
	pScsiReq->LUN[1] = lun;
	pScsiReq->LUN[2] = 0;
	pScsiReq->LUN[3] = 0;
	pScsiReq->LUN[4] = 0;
	pScsiReq->LUN[5] = 0;
	pScsiReq->LUN[6] = 0;
	pScsiReq->LUN[7] = 0;
	pScsiReq->Control = cpu_to_le32(scsictl);

	/*
	 *  Write SCSI CDB into the message
	 */
	cmd_len = SCpnt->cmd_len;
	for (ii=0; ii < cmd_len; ii++)
		pScsiReq->CDB[ii] = SCpnt->cmnd[ii];

	for (ii=cmd_len; ii < 16; ii++)
		pScsiReq->CDB[ii] = 0;

	/* DataLength */
	pScsiReq->DataLength = cpu_to_le32(datalen);

	/* SenseBuffer low address */
	pScsiReq->SenseBufferLowAddr = cpu_to_le32(hd->ioc->sense_buf_low_dma
					   + (my_idx * MPT_SENSE_BUFFER_ALLOC));

	/* Now add the SG list
	 * Always have a SGE even if null length.
	 */
	if (datalen == 0) {
		/* Add a NULL SGE */
		mptscsih_add_sge((char *)&pScsiReq->SGL, MPT_SGE_FLAGS_SSIMPLE_READ | 0,
			(dma_addr_t) -1);
	} else {
		/* Add a 32 or 64 bit SGE */
		if (mptscsih_AddSGE(hd->ioc, SCpnt, pScsiReq, my_idx) != SUCCESS)
			goto fail;
	}

	hd->ScsiLookup[my_idx] = SCpnt;
	SCpnt->host_scribble = NULL;

#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
	if (hd->ioc->bus_type == SCSI) {
		int dvStatus = hd->ioc->spi_data.dvStatus[pTarget->target_id];
		int issueCmd = 1;

		if (dvStatus || hd->ioc->spi_data.forceDv) {

			if ((dvStatus & MPT_SCSICFG_NEED_DV) ||
				(hd->ioc->spi_data.forceDv & MPT_SCSICFG_NEED_DV)) {
				unsigned long lflags;
				/* Schedule DV if necessary */
				spin_lock_irqsave(&dvtaskQ_lock, lflags);
				if (!dvtaskQ_active) {
					dvtaskQ_active = 1;
					spin_unlock_irqrestore(&dvtaskQ_lock, lflags);
					INIT_WORK(&dvTaskQ_task, mptscsih_domainValidation, (void *) hd);

					schedule_work(&dvTaskQ_task);
				} else {
					spin_unlock_irqrestore(&dvtaskQ_lock, lflags);
				}
				hd->ioc->spi_data.forceDv &= ~MPT_SCSICFG_NEED_DV;
			}

			/* Trying to do DV to this target, extend timeout.
			 * Wait to issue until flag is clear
			 */
			if (dvStatus & MPT_SCSICFG_DV_PENDING) {
				mod_timer(&SCpnt->eh_timeout, jiffies + 40 * HZ);
				issueCmd = 0;
			}

			/* Set the DV flags.
			 */
			if (dvStatus & MPT_SCSICFG_DV_NOT_DONE)
				mptscsih_set_dvflags(hd, pScsiReq);

			if (!issueCmd)
				goto fail;
		}
	}
#endif

	mpt_put_msg_frame(hd->ioc->DoneCtx, hd->ioc, mf);
	dmfprintk((MYIOC_s_INFO_FMT "Issued SCSI cmd (%p) mf=%p idx=%d\n",
			hd->ioc->name, SCpnt, mf, my_idx));
	DBG_DUMP_REQUEST_FRAME(mf)
	return 0;

 fail:
	hd->ScsiLookup[my_idx] = NULL;
	mptscsih_freeChainBuffers(hd->ioc, my_idx);
	mpt_free_msg_frame(hd->ioc, mf);
	return SCSI_MLQUEUE_HOST_BUSY;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_freeChainBuffers - Function to free chain buffers associated
 *	with a SCSI IO request
 *	@hd: Pointer to the MPT_SCSI_HOST instance
 *	@req_idx: Index of the SCSI IO request frame.
 *
 *	Called if SG chain buffer allocation fails and mptscsih callbacks.
 *	No return.
 */
static void
mptscsih_freeChainBuffers(MPT_ADAPTER *ioc, int req_idx)
{
	MPT_FRAME_HDR *chain;
	unsigned long flags;
	int chain_idx;
	int next;

	/* Get the first chain index and reset
	 * tracker state.
	 */
	chain_idx = ioc->ReqToChain[req_idx];
	ioc->ReqToChain[req_idx] = MPT_HOST_NO_CHAIN;

	while (chain_idx != MPT_HOST_NO_CHAIN) {

		/* Save the next chain buffer index */
		next = ioc->ChainToChain[chain_idx];

		/* Free this chain buffer and reset
		 * tracker
		 */
		ioc->ChainToChain[chain_idx] = MPT_HOST_NO_CHAIN;

		chain = (MPT_FRAME_HDR *) (ioc->ChainBuffer
					+ (chain_idx * ioc->req_sz));

		spin_lock_irqsave(&ioc->FreeQlock, flags);
		list_add_tail(&chain->u.frame.linkage.list, &ioc->FreeChainQ);
		spin_unlock_irqrestore(&ioc->FreeQlock, flags);

		dmfprintk((MYIOC_s_INFO_FMT "FreeChainBuffers (index %d)\n",
				ioc->name, chain_idx));

		/* handle next */
		chain_idx = next;
	}
	return;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	Reset Handling
 */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_TMHandler - Generic handler for SCSI Task Management.
 *	Fall through to mpt_HardResetHandler if: not operational, too many
 *	failed TM requests or handshake failure.
 *
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@type: Task Management type
 *	@target: Logical Target ID for reset (if appropriate)
 *	@lun: Logical Unit for reset (if appropriate)
 *	@ctx2abort: Context for the task to be aborted (if appropriate)
 *
 *	Remark: Currently invoked from a non-interrupt thread (_bh).
 *
 *	Remark: With old EH code, at most 1 SCSI TaskMgmt function per IOC
 *	will be active.
 *
 *	Returns 0 for SUCCESS or -1 if FAILED.
 */
static int
mptscsih_TMHandler(MPT_SCSI_HOST *hd, u8 type, u8 channel, u8 target, u8 lun, int ctx2abort, ulong timeout)
{
	MPT_ADAPTER	*ioc;
	int		 rc = -1;
	int		 doTask = 1;
	u32		 ioc_raw_state;
	unsigned long	 flags;

	/* If FW is being reloaded currently, return success to
	 * the calling function.
	 */
	if (hd == NULL)
		return 0;

	ioc = hd->ioc;
	if (ioc == NULL) {
		printk(KERN_ERR MYNAM " TMHandler" " NULL ioc!\n");
		return FAILED;
	}
	dtmprintk((MYIOC_s_INFO_FMT "TMHandler Entered!\n", ioc->name));

	// SJR - CHECKME - Can we avoid this here?
	// (mpt_HardResetHandler has this check...)
	spin_lock_irqsave(&ioc->diagLock, flags);
	if ((ioc->diagPending) || (ioc->alt_ioc && ioc->alt_ioc->diagPending)) {
		spin_unlock_irqrestore(&ioc->diagLock, flags);
		return FAILED;
	}
	spin_unlock_irqrestore(&ioc->diagLock, flags);

	/*  Wait a fixed amount of time for the TM pending flag to be cleared.
	 *  If we time out and not bus reset, then we return a FAILED status to the caller.
	 *  The call to mptscsih_tm_pending_wait() will set the pending flag if we are
	 *  successful. Otherwise, reload the FW.
	 */
	if (mptscsih_tm_pending_wait(hd) == FAILED) {
		if (type == MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK) {
			dtmprintk((KERN_INFO MYNAM ": %s: TMHandler abort: "
			   "Timed out waiting for last TM (%d) to complete! \n",
			   hd->ioc->name, hd->tmPending));
			return FAILED;
		} else if (type == MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET) {
			dtmprintk((KERN_INFO MYNAM ": %s: TMHandler target reset: "
			   "Timed out waiting for last TM (%d) to complete! \n",
			   hd->ioc->name, hd->tmPending));
			return FAILED;
		} else if (type == MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS) {
			dtmprintk((KERN_INFO MYNAM ": %s: TMHandler bus reset: "
			   "Timed out waiting for last TM (%d) to complete! \n",
			   hd->ioc->name, hd->tmPending));
			if (hd->tmPending & (1 << MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS))
				return FAILED;

			doTask = 0;
		}
	} else {
		spin_lock_irqsave(&hd->ioc->FreeQlock, flags);
		hd->tmPending |=  (1 << type);
		spin_unlock_irqrestore(&hd->ioc->FreeQlock, flags);
	}

	/* Is operational?
	 */
	ioc_raw_state = mpt_GetIocState(hd->ioc, 0);

#ifdef MPT_DEBUG_RESET
	if ((ioc_raw_state & MPI_IOC_STATE_MASK) != MPI_IOC_STATE_OPERATIONAL) {
		printk(MYIOC_s_WARN_FMT
			"TM Handler: IOC Not operational(0x%x)!\n",
			hd->ioc->name, ioc_raw_state);
	}
#endif

	if (doTask && ((ioc_raw_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_OPERATIONAL)
				&& !(ioc_raw_state & MPI_DOORBELL_ACTIVE)) {

		/* Isse the Task Mgmt request.
		 */
		if (hd->hard_resets < -1)
			hd->hard_resets++;
		rc = mptscsih_IssueTaskMgmt(hd, type, channel, target, lun, ctx2abort, timeout);
		if (rc) {
			printk(MYIOC_s_INFO_FMT "Issue of TaskMgmt failed!\n", hd->ioc->name);
		} else {
			dtmprintk((MYIOC_s_INFO_FMT "Issue of TaskMgmt Successful!\n", hd->ioc->name));
		}
	}

	/* Only fall through to the HRH if this is a bus reset
	 */
	if ((type == MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS) && (rc ||
		ioc->reload_fw || (ioc->alt_ioc && ioc->alt_ioc->reload_fw))) {
		dtmprintk((MYIOC_s_INFO_FMT "Calling HardReset! \n",
			 hd->ioc->name));
		rc = mpt_HardResetHandler(hd->ioc, CAN_SLEEP);
	}

	dtmprintk((MYIOC_s_INFO_FMT "TMHandler rc = %d!\n", hd->ioc->name, rc));

	return rc;
}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_IssueTaskMgmt - Generic send Task Management function.
 *	@hd: Pointer to MPT_SCSI_HOST structure
 *	@type: Task Management type
 *	@target: Logical Target ID for reset (if appropriate)
 *	@lun: Logical Unit for reset (if appropriate)
 *	@ctx2abort: Context for the task to be aborted (if appropriate)
 *
 *	Remark: _HardResetHandler can be invoked from an interrupt thread (timer)
 *	or a non-interrupt thread.  In the former, must not call schedule().
 *
 *	Not all fields are meaningfull for all task types.
 *
 *	Returns 0 for SUCCESS, -999 for "no msg frames",
 *	else other non-zero value returned.
 */
static int
mptscsih_IssueTaskMgmt(MPT_SCSI_HOST *hd, u8 type, u8 channel, u8 target, u8 lun, int ctx2abort, ulong timeout)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;
	int		 ii;
	int		 retval;

	/* Return Fail to calling function if no message frames available.
	 */
	if ((mf = mpt_get_msg_frame(hd->ioc->TaskCtx, hd->ioc)) == NULL) {
		dfailprintk((MYIOC_s_ERR_FMT "IssueTaskMgmt, no msg frames!!\n",
				hd->ioc->name));
		return FAILED;
	}
	dtmprintk((MYIOC_s_INFO_FMT "IssueTaskMgmt request @ %p\n",
			hd->ioc->name, mf));

	/* Format the Request
	 */
	pScsiTm = (SCSITaskMgmt_t *) mf;
	pScsiTm->TargetID = target;
	pScsiTm->Bus = channel;
	pScsiTm->ChainOffset = 0;
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;

	pScsiTm->Reserved = 0;
	pScsiTm->TaskType = type;
	pScsiTm->Reserved1 = 0;
	pScsiTm->MsgFlags = (type == MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS)
                    ? MPI_SCSITASKMGMT_MSGFLAGS_LIPRESET_RESET_OPTION : 0;

	for (ii= 0; ii < 8; ii++) {
		pScsiTm->LUN[ii] = 0;
	}
	pScsiTm->LUN[1] = lun;

	for (ii=0; ii < 7; ii++)
		pScsiTm->Reserved2[ii] = 0;

	pScsiTm->TaskMsgContext = ctx2abort;

	dtmprintk((MYIOC_s_INFO_FMT "IssueTaskMgmt: ctx2abort (0x%08x) type=%d\n",
			hd->ioc->name, ctx2abort, type));

	DBG_DUMP_TM_REQUEST_FRAME((u32 *)pScsiTm);

	if ((retval = mpt_send_handshake_request(hd->ioc->TaskCtx, hd->ioc,
		sizeof(SCSITaskMgmt_t), (u32*)pScsiTm,
		CAN_SLEEP)) != 0) {
		dfailprintk((MYIOC_s_ERR_FMT "_send_handshake FAILED!"
			" (hd %p, ioc %p, mf %p) \n", hd->ioc->name, hd,
			hd->ioc, mf));
		mpt_free_msg_frame(hd->ioc, mf);
		return retval;
	}

	if(mptscsih_tm_wait_for_completion(hd, timeout) == FAILED) {
		dfailprintk((MYIOC_s_ERR_FMT "_wait_for_completion FAILED!"
			" (hd %p, ioc %p, mf %p) \n", hd->ioc->name, hd,
			hd->ioc, mf));
		mpt_free_msg_frame(hd->ioc, mf);
		dtmprintk((MYIOC_s_INFO_FMT "Calling HardReset! \n",
			 hd->ioc->name));
		retval = mpt_HardResetHandler(hd->ioc, CAN_SLEEP);
	}

	return retval;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_abort - Abort linux scsi_cmnd routine, new_eh variant
 *	@SCpnt: Pointer to scsi_cmnd structure, IO to be aborted
 *
 *	(linux scsi_host_template.eh_abort_handler routine)
 *
 *	Returns SUCCESS or FAILED.
 */
int
mptscsih_abort(struct scsi_cmnd * SCpnt)
{
	MPT_SCSI_HOST	*hd;
	MPT_ADAPTER	*ioc;
	MPT_FRAME_HDR	*mf;
	u32		 ctx2abort;
	int		 scpnt_idx;
	int		 retval;

	/* If we can't locate our host adapter structure, return FAILED status.
	 */
	if ((hd = (MPT_SCSI_HOST *) SCpnt->device->host->hostdata) == NULL) {
		SCpnt->result = DID_RESET << 16;
		SCpnt->scsi_done(SCpnt);
		dfailprintk((KERN_INFO MYNAM ": mptscsih_abort: "
			   "Can't locate host! (sc=%p)\n",
			   SCpnt));
		return FAILED;
	}

	ioc = hd->ioc;
	if (hd->resetPending) {
		return FAILED;
	}

	if (hd->timeouts < -1)
		hd->timeouts++;

	/* Find this command
	 */
	if ((scpnt_idx = SCPNT_TO_LOOKUP_IDX(SCpnt)) < 0) {
		/* Cmd not found in ScsiLookup.
		 * Do OS callback.
		 */
		SCpnt->result = DID_RESET << 16;
		dtmprintk((KERN_INFO MYNAM ": %s: mptscsih_abort: "
			   "Command not in the active list! (sc=%p)\n",
			   hd->ioc->name, SCpnt));
		return SUCCESS;
	}

	printk(KERN_WARNING MYNAM ": %s: attempting task abort! (sc=%p)\n",
	       hd->ioc->name, SCpnt);
	scsi_print_command(SCpnt);

	/* Most important!  Set TaskMsgContext to SCpnt's MsgContext!
	 * (the IO to be ABORT'd)
	 *
	 * NOTE: Since we do not byteswap MsgContext, we do not
	 *	 swap it here either.  It is an opaque cookie to
	 *	 the controller, so it does not matter. -DaveM
	 */
	mf = MPT_INDEX_2_MFPTR(hd->ioc, scpnt_idx);
	ctx2abort = mf->u.frame.hwhdr.msgctxu.MsgContext;

	hd->abortSCpnt = SCpnt;

	retval = mptscsih_TMHandler(hd, MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK,
		SCpnt->device->channel, SCpnt->device->id, SCpnt->device->lun,
		ctx2abort, 2 /* 2 second timeout */);

	printk (KERN_WARNING MYNAM ": %s: task abort: %s (sc=%p)\n",
		hd->ioc->name,
		((retval == 0) ? "SUCCESS" : "FAILED" ), SCpnt);

	if (retval == 0)
		return SUCCESS;

	if(retval != FAILED ) {
		hd->tmPending = 0;
		hd->tmState = TM_STATE_NONE;
	}
	return FAILED;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_dev_reset - Perform a SCSI TARGET_RESET!  new_eh variant
 *	@SCpnt: Pointer to scsi_cmnd structure, IO which reset is due to
 *
 *	(linux scsi_host_template.eh_dev_reset_handler routine)
 *
 *	Returns SUCCESS or FAILED.
 */
int
mptscsih_dev_reset(struct scsi_cmnd * SCpnt)
{
	MPT_SCSI_HOST	*hd;
	int		 retval;

	/* If we can't locate our host adapter structure, return FAILED status.
	 */
	if ((hd = (MPT_SCSI_HOST *) SCpnt->device->host->hostdata) == NULL){
		dtmprintk((KERN_INFO MYNAM ": mptscsih_dev_reset: "
			   "Can't locate host! (sc=%p)\n",
			   SCpnt));
		return FAILED;
	}

	if (hd->resetPending)
		return FAILED;

	printk(KERN_WARNING MYNAM ": %s: attempting target reset! (sc=%p)\n",
	       hd->ioc->name, SCpnt);
	scsi_print_command(SCpnt);

	retval = mptscsih_TMHandler(hd, MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET,
		SCpnt->device->channel, SCpnt->device->id,
		0, 0, 5 /* 5 second timeout */);

	printk (KERN_WARNING MYNAM ": %s: target reset: %s (sc=%p)\n",
		hd->ioc->name,
		((retval == 0) ? "SUCCESS" : "FAILED" ), SCpnt);

	if (retval == 0)
		return SUCCESS;

	if(retval != FAILED ) {
		hd->tmPending = 0;
		hd->tmState = TM_STATE_NONE;
	}
	return FAILED;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_bus_reset - Perform a SCSI BUS_RESET!	new_eh variant
 *	@SCpnt: Pointer to scsi_cmnd structure, IO which reset is due to
 *
 *	(linux scsi_host_template.eh_bus_reset_handler routine)
 *
 *	Returns SUCCESS or FAILED.
 */
int
mptscsih_bus_reset(struct scsi_cmnd * SCpnt)
{
	MPT_SCSI_HOST	*hd;
	int		 retval;

	/* If we can't locate our host adapter structure, return FAILED status.
	 */
	if ((hd = (MPT_SCSI_HOST *) SCpnt->device->host->hostdata) == NULL){
		dtmprintk((KERN_INFO MYNAM ": mptscsih_bus_reset: "
			   "Can't locate host! (sc=%p)\n",
			   SCpnt ) );
		return FAILED;
	}

	printk(KERN_WARNING MYNAM ": %s: attempting bus reset! (sc=%p)\n",
	       hd->ioc->name, SCpnt);
	scsi_print_command(SCpnt);

	if (hd->timeouts < -1)
		hd->timeouts++;

	retval = mptscsih_TMHandler(hd, MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS,
		SCpnt->device->channel, 0, 0, 0, 5 /* 5 second timeout */);

	printk (KERN_WARNING MYNAM ": %s: bus reset: %s (sc=%p)\n",
		hd->ioc->name,
		((retval == 0) ? "SUCCESS" : "FAILED" ), SCpnt);

	if (retval == 0)
		return SUCCESS;

	if(retval != FAILED ) {
		hd->tmPending = 0;
		hd->tmState = TM_STATE_NONE;
	}
	return FAILED;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_host_reset - Perform a SCSI host adapter RESET!
 *	new_eh variant
 *	@SCpnt: Pointer to scsi_cmnd structure, IO which reset is due to
 *
 *	(linux scsi_host_template.eh_host_reset_handler routine)
 *
 *	Returns SUCCESS or FAILED.
 */
int
mptscsih_host_reset(struct scsi_cmnd *SCpnt)
{
	MPT_SCSI_HOST *  hd;
	int              status = SUCCESS;

	/*  If we can't locate the host to reset, then we failed. */
	if ((hd = (MPT_SCSI_HOST *) SCpnt->device->host->hostdata) == NULL){
		dtmprintk( ( KERN_INFO MYNAM ": mptscsih_host_reset: "
			     "Can't locate host! (sc=%p)\n",
			     SCpnt ) );
		return FAILED;
	}

	printk(KERN_WARNING MYNAM ": %s: Attempting host reset! (sc=%p)\n",
	       hd->ioc->name, SCpnt);

	/*  If our attempts to reset the host failed, then return a failed
	 *  status.  The host will be taken off line by the SCSI mid-layer.
	 */
	if (mpt_HardResetHandler(hd->ioc, CAN_SLEEP) < 0){
		status = FAILED;
	} else {
		/*  Make sure TM pending is cleared and TM state is set to
		 *  NONE.
		 */
		hd->tmPending = 0;
		hd->tmState = TM_STATE_NONE;
	}

	dtmprintk( ( KERN_INFO MYNAM ": mptscsih_host_reset: "
		     "Status = %s\n",
		     (status == SUCCESS) ? "SUCCESS" : "FAILED" ) );

	return status;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_tm_pending_wait - wait for pending task management request to
 *		complete.
 *	@hd: Pointer to MPT host structure.
 *
 *	Returns {SUCCESS,FAILED}.
 */
static int
mptscsih_tm_pending_wait(MPT_SCSI_HOST * hd)
{
	unsigned long  flags;
	int            loop_count = 4 * 10;  /* Wait 10 seconds */
	int            status = FAILED;

	do {
		spin_lock_irqsave(&hd->ioc->FreeQlock, flags);
		if (hd->tmState == TM_STATE_NONE) {
			hd->tmState = TM_STATE_IN_PROGRESS;
			hd->tmPending = 1;
			spin_unlock_irqrestore(&hd->ioc->FreeQlock, flags);
			status = SUCCESS;
			break;
		}
		spin_unlock_irqrestore(&hd->ioc->FreeQlock, flags);
		msleep(250);
	} while (--loop_count);

	return status;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_tm_wait_for_completion - wait for completion of TM task
 *	@hd: Pointer to MPT host structure.
 *
 *	Returns {SUCCESS,FAILED}.
 */
static int
mptscsih_tm_wait_for_completion(MPT_SCSI_HOST * hd, ulong timeout )
{
	unsigned long  flags;
	int            loop_count = 4 * timeout;
	int            status = FAILED;

	do {
		spin_lock_irqsave(&hd->ioc->FreeQlock, flags);
		if(hd->tmPending == 0) {
			status = SUCCESS;
 			spin_unlock_irqrestore(&hd->ioc->FreeQlock, flags);
			break;
		}
		spin_unlock_irqrestore(&hd->ioc->FreeQlock, flags);
		msleep_interruptible(250);
	} while (--loop_count);

	return status;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_taskmgmt_complete - Registered with Fusion MPT base driver
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mf: Pointer to SCSI task mgmt request frame
 *	@mr: Pointer to SCSI task mgmt reply frame
 *
 *	This routine is called from mptbase.c::mpt_interrupt() at the completion
 *	of any SCSI task management request.
 *	This routine is registered with the MPT (base) driver at driver
 *	load/init time via the mpt_register() API call.
 *
 *	Returns 1 indicating alloc'd request frame ptr should be freed.
 */
int
mptscsih_taskmgmt_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *mr)
{
	SCSITaskMgmtReply_t	*pScsiTmReply;
	SCSITaskMgmt_t		*pScsiTmReq;
	MPT_SCSI_HOST		*hd;
	unsigned long		 flags;
	u16			 iocstatus;
	u8			 tmType;

	dtmprintk((MYIOC_s_WARN_FMT "TaskMgmt completed (mf=%p,mr=%p)\n",
			ioc->name, mf, mr));
	if (ioc->sh) {
		/* Depending on the thread, a timer is activated for
		 * the TM request.  Delete this timer on completion of TM.
		 * Decrement count of outstanding TM requests.
		 */
		hd = (MPT_SCSI_HOST *)ioc->sh->hostdata;
	} else {
		dtmprintk((MYIOC_s_WARN_FMT "TaskMgmt Complete: NULL Scsi Host Ptr\n",
			ioc->name));
		return 1;
	}

	if (mr == NULL) {
		dtmprintk((MYIOC_s_WARN_FMT "ERROR! TaskMgmt Reply: NULL Request %p\n",
			ioc->name, mf));
		return 1;
	} else {
		pScsiTmReply = (SCSITaskMgmtReply_t*)mr;
		pScsiTmReq = (SCSITaskMgmt_t*)mf;

		/* Figure out if this was ABORT_TASK, TARGET_RESET, or BUS_RESET! */
		tmType = pScsiTmReq->TaskType;

		dtmprintk((MYIOC_s_WARN_FMT "  TaskType = %d, TerminationCount=%d\n",
				ioc->name, tmType, le32_to_cpu(pScsiTmReply->TerminationCount)));
		DBG_DUMP_TM_REPLY_FRAME((u32 *)pScsiTmReply);

		iocstatus = le16_to_cpu(pScsiTmReply->IOCStatus) & MPI_IOCSTATUS_MASK;
		dtmprintk((MYIOC_s_WARN_FMT "  SCSI TaskMgmt (%d) IOCStatus=%04x IOCLogInfo=%08x\n",
			ioc->name, tmType, iocstatus, le32_to_cpu(pScsiTmReply->IOCLogInfo)));
		/* Error?  (anything non-zero?) */
		if (iocstatus) {

			/* clear flags and continue.
			 */
			if (tmType == MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK)
				hd->abortSCpnt = NULL;

			/* If an internal command is present
			 * or the TM failed - reload the FW.
			 * FC FW may respond FAILED to an ABORT
			 */
			if (tmType == MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS) {
				if ((hd->cmdPtr) ||
				    (iocstatus == MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED)) {
					if (mpt_HardResetHandler(ioc, NO_SLEEP) < 0) {
						printk((KERN_WARNING
							" Firmware Reload FAILED!!\n"));
					}
				}
			}
		} else {
			dtmprintk((MYIOC_s_WARN_FMT " TaskMgmt SUCCESS\n", ioc->name));

			hd->abortSCpnt = NULL;

		}
	}

	spin_lock_irqsave(&ioc->FreeQlock, flags);
	hd->tmPending = 0;
	spin_unlock_irqrestore(&ioc->FreeQlock, flags);
	hd->tmState = TM_STATE_NONE;

	return 1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	This is anyones guess quite frankly.
 */
int
mptscsih_bios_param(struct scsi_device * sdev, struct block_device *bdev,
		sector_t capacity, int geom[])
{
	int		heads;
	int		sectors;
	sector_t	cylinders;
	ulong 		dummy;

	heads = 64;
	sectors = 32;

	dummy = heads * sectors;
	cylinders = capacity;
	sector_div(cylinders,dummy);

	/*
	 * Handle extended translation size for logical drives
	 * > 1Gb
	 */
	if ((ulong)capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		dummy = heads * sectors;
		cylinders = capacity;
		sector_div(cylinders,dummy);
	}

	/* return result */
	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	dprintk((KERN_NOTICE
		": bios_param: Id=%i Lun=%i Channel=%i CHS=%i/%i/%i\n",
		sdev->id, sdev->lun,sdev->channel,(int)cylinders,heads,sectors));

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	OS entry point to allow host driver to alloc memory
 *	for each scsi device. Called once per device the bus scan.
 *	Return non-zero if allocation fails.
 *	Init memory once per id (not LUN).
 */
int
mptscsih_slave_alloc(struct scsi_device *device)
{
	struct Scsi_Host	*host = device->host;
	MPT_SCSI_HOST		*hd = (MPT_SCSI_HOST *)host->hostdata;
	VirtDevice		*vdev;
	uint			target = device->id;

	if (hd == NULL)
		return -ENODEV;

	if ((vdev = hd->Targets[target]) != NULL)
		goto out;

	vdev = kmalloc(sizeof(VirtDevice), GFP_KERNEL);
	if (!vdev) {
		printk(MYIOC_s_ERR_FMT "slave_alloc kmalloc(%zd) FAILED!\n",
				hd->ioc->name, sizeof(VirtDevice));
		return -ENOMEM;
	}

	memset(vdev, 0, sizeof(VirtDevice));
	vdev->tflags = MPT_TARGET_FLAGS_Q_YES;
	vdev->ioc_id = hd->ioc->id;
	vdev->target_id = device->id;
	vdev->bus_id = device->channel;
	vdev->raidVolume = 0;
	hd->Targets[device->id] = vdev;
	if (hd->ioc->bus_type == SCSI) {
		if (hd->ioc->raid_data.isRaid & (1 << device->id)) {
			vdev->raidVolume = 1;
			ddvtprintk((KERN_INFO
			    "RAID Volume @ id %d\n", device->id));
		}
	} else {
		vdev->tflags |= MPT_TARGET_FLAGS_VALID_INQUIRY;
	}

 out:
	vdev->num_luns++;
	device->hostdata = vdev;
	return 0;
}

/*
 *	OS entry point to allow for host driver to free allocated memory
 *	Called if no device present or device being unloaded
 */
void
mptscsih_slave_destroy(struct scsi_device *device)
{
	struct Scsi_Host	*host = device->host;
	MPT_SCSI_HOST		*hd = (MPT_SCSI_HOST *)host->hostdata;
	VirtDevice		*vdev;
	uint			target = device->id;
	uint			lun = device->lun;

	if (hd == NULL)
		return;

	mptscsih_search_running_cmds(hd, target, lun);

	vdev = hd->Targets[target];
	vdev->luns[0] &= ~(1 << lun);
	if (--vdev->num_luns)
		return;

	kfree(hd->Targets[target]);
	hd->Targets[target] = NULL;

	if (hd->ioc->bus_type == SCSI) {
		if (mptscsih_is_phys_disk(hd->ioc, target)) {
			hd->ioc->spi_data.forceDv |= MPT_SCSICFG_RELOAD_IOC_PG3;
		} else {
			hd->ioc->spi_data.dvStatus[target] =
				MPT_SCSICFG_NEGOTIATE;

			if (!hd->negoNvram) {
				hd->ioc->spi_data.dvStatus[target] |=
					MPT_SCSICFG_DV_NOT_DONE;
			}
		}
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_change_queue_depth - This function will set a devices queue depth
 *	@sdev: per scsi_device pointer
 *	@qdepth: requested queue depth
 *
 *	Adding support for new 'change_queue_depth' api.
*/
int
mptscsih_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	MPT_SCSI_HOST	*hd = (MPT_SCSI_HOST *)sdev->host->hostdata;
	VirtDevice *pTarget;
	int	max_depth;
	int	tagged;

	if (hd == NULL)
		return 0;
	if (!(pTarget = hd->Targets[sdev->id]))
		return 0;

	if (hd->ioc->bus_type == SCSI) {
		if (pTarget->tflags & MPT_TARGET_FLAGS_VALID_INQUIRY) {
			if (!(pTarget->tflags & MPT_TARGET_FLAGS_Q_YES))
				max_depth = 1;
			else if (((pTarget->inq_data[0] & 0x1f) == 0x00) &&
			         (pTarget->minSyncFactor <= MPT_ULTRA160 ))
				max_depth = MPT_SCSI_CMD_PER_DEV_HIGH;
			else
				max_depth = MPT_SCSI_CMD_PER_DEV_LOW;
		} else {
			/* error case - No Inq. Data */
			max_depth = 1;
		}
	} else
		max_depth = MPT_SCSI_CMD_PER_DEV_HIGH;

	if (qdepth > max_depth)
		qdepth = max_depth;
	if (qdepth == 1)
		tagged = 0;
	else
		tagged = MSG_SIMPLE_TAG;

	scsi_adjust_queue_depth(sdev, tagged, qdepth);
	return sdev->queue_depth;
}

/*
 *	OS entry point to adjust the queue_depths on a per-device basis.
 *	Called once per device the bus scan. Use it to force the queue_depth
 *	member to 1 if a device does not support Q tags.
 *	Return non-zero if fails.
 */
int
mptscsih_slave_configure(struct scsi_device *device)
{
	struct Scsi_Host	*sh = device->host;
	VirtDevice		*pTarget;
	MPT_SCSI_HOST		*hd = (MPT_SCSI_HOST *)sh->hostdata;

	if ((hd == NULL) || (hd->Targets == NULL)) {
		return 0;
	}

	dsprintk((MYIOC_s_INFO_FMT
		"device @ %p, id=%d, LUN=%d, channel=%d\n",
		hd->ioc->name, device, device->id, device->lun, device->channel));
	dsprintk((MYIOC_s_INFO_FMT
		"sdtr %d wdtr %d ppr %d inq length=%d\n",
		hd->ioc->name, device->sdtr, device->wdtr,
		device->ppr, device->inquiry_len));

	if (device->id > sh->max_id) {
		/* error case, should never happen */
		scsi_adjust_queue_depth(device, 0, 1);
		goto slave_configure_exit;
	}

	pTarget = hd->Targets[device->id];

	if (pTarget == NULL) {
		/* Driver doesn't know about this device.
		 * Kernel may generate a "Dummy Lun 0" which
		 * may become a real Lun if a
		 * "scsi add-single-device" command is executed
		 * while the driver is active (hot-plug a
		 * device).  LSI Raid controllers need
		 * queue_depth set to DEV_HIGH for this reason.
		 */
		scsi_adjust_queue_depth(device, MSG_SIMPLE_TAG,
			MPT_SCSI_CMD_PER_DEV_HIGH);
		goto slave_configure_exit;
	}

	mptscsih_initTarget(hd, device->channel, device->id, device->lun,
		device->inquiry, device->inquiry_len );
	mptscsih_change_queue_depth(device, MPT_SCSI_CMD_PER_DEV_HIGH);

	dsprintk((MYIOC_s_INFO_FMT
		"Queue depth=%d, tflags=%x\n",
		hd->ioc->name, device->queue_depth, pTarget->tflags));

	dsprintk((MYIOC_s_INFO_FMT
		"negoFlags=%x, maxOffset=%x, SyncFactor=%x\n",
		hd->ioc->name, pTarget->negoFlags, pTarget->maxOffset, pTarget->minSyncFactor));

slave_configure_exit:

	dsprintk((MYIOC_s_INFO_FMT
		"tagged %d, simple %d, ordered %d\n",
		hd->ioc->name,device->tagged_supported, device->simple_tags,
		device->ordered_tags));

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Private routines...
 */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Utility function to copy sense data from the scsi_cmnd buffer
 * to the FC and SCSI target structures.
 *
 */
static void
mptscsih_copy_sense_data(struct scsi_cmnd *sc, MPT_SCSI_HOST *hd, MPT_FRAME_HDR *mf, SCSIIOReply_t *pScsiReply)
{
	VirtDevice	*target;
	SCSIIORequest_t	*pReq;
	u32		 sense_count = le32_to_cpu(pScsiReply->SenseCount);
	int		 index;

	/* Get target structure
	 */
	pReq = (SCSIIORequest_t *) mf;
	index = (int) pReq->TargetID;
	target = hd->Targets[index];

	if (sense_count) {
		u8 *sense_data;
		int req_index;

		/* Copy the sense received into the scsi command block. */
		req_index = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);
		sense_data = ((u8 *)hd->ioc->sense_buf_pool + (req_index * MPT_SENSE_BUFFER_ALLOC));
		memcpy(sc->sense_buffer, sense_data, SNS_LEN(sc));

		/* Log SMART data (asc = 0x5D, non-IM case only) if required.
		 */
		if ((hd->ioc->events) && (hd->ioc->eventTypes & (1 << MPI_EVENT_SCSI_DEVICE_STATUS_CHANGE))) {
			if ((sense_data[12] == 0x5D) && (target->raidVolume == 0)) {
				int idx;
				MPT_ADAPTER *ioc = hd->ioc;

				idx = ioc->eventContext % ioc->eventLogSize;
				ioc->events[idx].event = MPI_EVENT_SCSI_DEVICE_STATUS_CHANGE;
				ioc->events[idx].eventContext = ioc->eventContext;

				ioc->events[idx].data[0] = (pReq->LUN[1] << 24) ||
					(MPI_EVENT_SCSI_DEV_STAT_RC_SMART_DATA << 16) ||
					(pReq->Bus << 8) || pReq->TargetID;

				ioc->events[idx].data[1] = (sense_data[13] << 8) || sense_data[12];

				ioc->eventContext++;
			}
		}
	} else {
		dprintk((MYIOC_s_INFO_FMT "Hmmm... SenseData len=0! (?)\n",
				hd->ioc->name));
	}
}

static u32
SCPNT_TO_LOOKUP_IDX(struct scsi_cmnd *sc)
{
	MPT_SCSI_HOST *hd;
	int i;

	hd = (MPT_SCSI_HOST *) sc->device->host->hostdata;

	for (i = 0; i < hd->ioc->req_depth; i++) {
		if (hd->ScsiLookup[i] == sc) {
			return i;
		}
	}

	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
int
mptscsih_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	MPT_SCSI_HOST	*hd;
	unsigned long	 flags;
	int 		ii;

	dtmprintk((KERN_WARNING MYNAM
			": IOC %s_reset routed to SCSI host driver!\n",
			reset_phase==MPT_IOC_SETUP_RESET ? "setup" : (
			reset_phase==MPT_IOC_PRE_RESET ? "pre" : "post")));

	/* If a FW reload request arrives after base installed but
	 * before all scsi hosts have been attached, then an alt_ioc
	 * may have a NULL sh pointer.
	 */
	if ((ioc->sh == NULL) || (ioc->sh->hostdata == NULL))
		return 0;
	else
		hd = (MPT_SCSI_HOST *) ioc->sh->hostdata;

	if (reset_phase == MPT_IOC_SETUP_RESET) {
		dtmprintk((MYIOC_s_WARN_FMT "Setup-Diag Reset\n", ioc->name));

		/* Clean Up:
		 * 1. Set Hard Reset Pending Flag
		 * All new commands go to doneQ
		 */
		hd->resetPending = 1;

	} else if (reset_phase == MPT_IOC_PRE_RESET) {
		dtmprintk((MYIOC_s_WARN_FMT "Pre-Diag Reset\n", ioc->name));

		/* 2. Flush running commands
		 *	Clean ScsiLookup (and associated memory)
		 *	AND clean mytaskQ
		 */

		/* 2b. Reply to OS all known outstanding I/O commands.
		 */
		mptscsih_flush_running_cmds(hd);

		/* 2c. If there was an internal command that
		 * has not completed, configuration or io request,
		 * free these resources.
		 */
		if (hd->cmdPtr) {
			del_timer(&hd->timer);
			mpt_free_msg_frame(ioc, hd->cmdPtr);
		}

		dtmprintk((MYIOC_s_WARN_FMT "Pre-Reset complete.\n", ioc->name));

	} else {
		dtmprintk((MYIOC_s_WARN_FMT "Post-Diag Reset\n", ioc->name));

		/* Once a FW reload begins, all new OS commands are
		 * redirected to the doneQ w/ a reset status.
		 * Init all control structures.
		 */

		/* ScsiLookup initialization
		 */
		for (ii=0; ii < hd->ioc->req_depth; ii++)
			hd->ScsiLookup[ii] = NULL;

		/* 2. Chain Buffer initialization
		 */

		/* 4. Renegotiate to all devices, if SCSI
		 */
		if (ioc->bus_type == SCSI) {
			dnegoprintk(("writeSDP1: ALL_IDS USE_NVRAM\n"));
			mptscsih_writeSDP1(hd, 0, 0, MPT_SCSICFG_ALL_IDS | MPT_SCSICFG_USE_NVRAM);
		}

		/* 5. Enable new commands to be posted
		 */
		spin_lock_irqsave(&ioc->FreeQlock, flags);
		hd->tmPending = 0;
		spin_unlock_irqrestore(&ioc->FreeQlock, flags);
		hd->resetPending = 0;
		hd->tmState = TM_STATE_NONE;

		/* 6. If there was an internal command,
		 * wake this process up.
		 */
		if (hd->cmdPtr) {
			/*
			 * Wake up the original calling thread
			 */
			hd->pLocal = &hd->localReply;
			hd->pLocal->completion = MPT_SCANDV_DID_RESET;
			hd->scandv_wait_done = 1;
			wake_up(&hd->scandv_waitq);
			hd->cmdPtr = NULL;
		}

		/* 7. Set flag to force DV and re-read IOC Page 3
		 */
		if (ioc->bus_type == SCSI) {
			ioc->spi_data.forceDv = MPT_SCSICFG_NEED_DV | MPT_SCSICFG_RELOAD_IOC_PG3;
			ddvtprintk(("Set reload IOC Pg3 Flag\n"));
		}

		dtmprintk((MYIOC_s_WARN_FMT "Post-Reset complete.\n", ioc->name));

	}

	return 1;		/* currently means nothing really */
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* work queue thread to clear the persitency table */
static void
mptscsih_sas_persist_clear_table(void * arg)
{
	MPT_ADAPTER *ioc = (MPT_ADAPTER *)arg;

	mptbase_sas_persist_operation(ioc, MPI_SAS_OP_CLEAR_NOT_PRESENT);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
int
mptscsih_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *pEvReply)
{
	MPT_SCSI_HOST *hd;
	u8 event = le32_to_cpu(pEvReply->Event) & 0xFF;

	devtprintk((MYIOC_s_INFO_FMT "MPT event (=%02Xh) routed to SCSI host driver!\n",
			ioc->name, event));

	if (ioc->sh == NULL ||
		((hd = (MPT_SCSI_HOST *)ioc->sh->hostdata) == NULL))
		return 1;

	switch (event) {
	case MPI_EVENT_UNIT_ATTENTION:			/* 03 */
		/* FIXME! */
		break;
	case MPI_EVENT_IOC_BUS_RESET:			/* 04 */
	case MPI_EVENT_EXT_BUS_RESET:			/* 05 */
		if (hd && (ioc->bus_type == SCSI) && (hd->soft_resets < -1))
			hd->soft_resets++;
		break;
	case MPI_EVENT_LOGOUT:				/* 09 */
		/* FIXME! */
		break;

		/*
		 *  CHECKME! Don't think we need to do
		 *  anything for these, but...
		 */
	case MPI_EVENT_RESCAN:				/* 06 */
	case MPI_EVENT_LINK_STATUS_CHANGE:		/* 07 */
	case MPI_EVENT_LOOP_STATE_CHANGE:		/* 08 */
		/*
		 *  CHECKME!  Falling thru...
		 */
		break;

	case MPI_EVENT_INTEGRATED_RAID:			/* 0B */
	{
		pMpiEventDataRaid_t pRaidEventData =
		    (pMpiEventDataRaid_t) pEvReply->Data;
#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
		/* Domain Validation Needed */
		if (ioc->bus_type == SCSI &&
		    pRaidEventData->ReasonCode ==
		    MPI_EVENT_RAID_RC_DOMAIN_VAL_NEEDED)
			mptscsih_set_dvflags_raid(hd, pRaidEventData->PhysDiskNum);
#endif
		break;
	}

	/* Persistent table is full. */
	case MPI_EVENT_PERSISTENT_TABLE_FULL:
		INIT_WORK(&mptscsih_persistTask,
		    mptscsih_sas_persist_clear_table,(void *)ioc);
		schedule_work(&mptscsih_persistTask);
		break;

	case MPI_EVENT_NONE:				/* 00 */
	case MPI_EVENT_LOG_DATA:			/* 01 */
	case MPI_EVENT_STATE_CHANGE:			/* 02 */
	case MPI_EVENT_EVENT_CHANGE:			/* 0A */
	default:
		dprintk((KERN_INFO "  Ignoring event (=%02Xh)\n", event));
		break;
	}

	return 1;		/* currently means nothing really */
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_initTarget - Target, LUN alloc/free functionality.
 *	@hd: Pointer to MPT_SCSI_HOST structure
 *	@bus_id: Bus number (?)
 *	@target_id: SCSI target id
 *	@lun: SCSI LUN id
 *	@data: Pointer to data
 *	@dlen: Number of INQUIRY bytes
 *
 *	NOTE: It's only SAFE to call this routine if data points to
 *	sane & valid STANDARD INQUIRY data!
 *
 *	Allocate and initialize memory for this target.
 *	Save inquiry data.
 *
 */
static void
mptscsih_initTarget(MPT_SCSI_HOST *hd, int bus_id, int target_id, u8 lun, char *data, int dlen)
{
	int		indexed_lun, lun_index;
	VirtDevice	*vdev;
	SpiCfgData	*pSpi;
	char		data_56;

	dinitprintk((MYIOC_s_INFO_FMT "initTarget bus=%d id=%d lun=%d hd=%p\n",
			hd->ioc->name, bus_id, target_id, lun, hd));

	/*
	 * If the peripheral qualifier filter is enabled then if the target reports a 0x1
	 * (i.e. The targer is capable of supporting the specified peripheral device type
	 * on this logical unit; however, the physical device is not currently connected
	 * to this logical unit) it will be converted to a 0x3 (i.e. The target is not
	 * capable of supporting a physical device on this logical unit). This is to work
	 * around a bug in th emid-layer in some distributions in which the mid-layer will
	 * continue to try to communicate to the LUN and evntually create a dummy LUN.
	*/
	if (hd->mpt_pq_filter && dlen && (data[0] & 0xE0))
		data[0] |= 0x40;

	/* Is LUN supported? If so, upper 2 bits will be 0
	* in first byte of inquiry data.
	*/
	if (data[0] & 0xe0)
		return;

	if ((vdev = hd->Targets[target_id]) == NULL) {
		return;
	}

	lun_index = (lun >> 5);  /* 32 luns per lun_index */
	indexed_lun = (lun % 32);
	vdev->luns[lun_index] |= (1 << indexed_lun);

	if (hd->ioc->bus_type == SCSI) {
		if ((data[0] == TYPE_PROCESSOR) && (hd->ioc->spi_data.Saf_Te)) {
			/* Treat all Processors as SAF-TE if
			 * command line option is set */
			vdev->tflags |= MPT_TARGET_FLAGS_SAF_TE_ISSUED;
			mptscsih_writeIOCPage4(hd, target_id, bus_id);
		}else if ((data[0] == TYPE_PROCESSOR) &&
			!(vdev->tflags & MPT_TARGET_FLAGS_SAF_TE_ISSUED )) {
			if ( dlen > 49 ) {
				vdev->tflags |= MPT_TARGET_FLAGS_VALID_INQUIRY;
				if ( data[44] == 'S' &&
				     data[45] == 'A' &&
				     data[46] == 'F' &&
				     data[47] == '-' &&
				     data[48] == 'T' &&
				     data[49] == 'E' ) {
					vdev->tflags |= MPT_TARGET_FLAGS_SAF_TE_ISSUED;
					mptscsih_writeIOCPage4(hd, target_id, bus_id);
				}
			}
		}
		if (!(vdev->tflags & MPT_TARGET_FLAGS_VALID_INQUIRY)) {
			if ( dlen > 8 ) {
				memcpy (vdev->inq_data, data, 8);
			} else {
				memcpy (vdev->inq_data, data, dlen);
			}

			/* If have not done DV, set the DV flag.
			 */
			pSpi = &hd->ioc->spi_data;
			if ((data[0] == TYPE_TAPE) || (data[0] == TYPE_PROCESSOR)) {
				if (pSpi->dvStatus[target_id] & MPT_SCSICFG_DV_NOT_DONE)
					pSpi->dvStatus[target_id] |= MPT_SCSICFG_NEED_DV;
			}

			vdev->tflags |= MPT_TARGET_FLAGS_VALID_INQUIRY;


			data_56 = 0x0F;  /* Default to full capabilities if Inq data length is < 57 */
			if (dlen > 56) {
				if ( (!(vdev->tflags & MPT_TARGET_FLAGS_VALID_56))) {
				/* Update the target capabilities
				 */
					data_56 = data[56];
					vdev->tflags |= MPT_TARGET_FLAGS_VALID_56;
				}
			}
			mptscsih_setTargetNegoParms(hd, vdev, data_56);
		} else {
			/* Initial Inquiry may not request enough data bytes to
			 * obtain byte 57.  DV will; if target doesn't return
			 * at least 57 bytes, data[56] will be zero. */
			if (dlen > 56) {
				if ( (!(vdev->tflags & MPT_TARGET_FLAGS_VALID_56))) {
				/* Update the target capabilities
				 */
					data_56 = data[56];
					vdev->tflags |= MPT_TARGET_FLAGS_VALID_56;
					mptscsih_setTargetNegoParms(hd, vdev, data_56);
				}
			}
		}
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Update the target negotiation parameters based on the
 *  the Inquiry data, adapter capabilities, and NVRAM settings.
 *
 */
static void
mptscsih_setTargetNegoParms(MPT_SCSI_HOST *hd, VirtDevice *target, char byte56)
{
	SpiCfgData *pspi_data = &hd->ioc->spi_data;
	int  id = (int) target->target_id;
	int  nvram;
	VirtDevice	*vdev;
	int ii;
	u8 width = MPT_NARROW;
	u8 factor = MPT_ASYNC;
	u8 offset = 0;
	u8 version, nfactor;
	u8 noQas = 1;

	target->negoFlags = pspi_data->noQas;

	/* noQas == 0 => device supports QAS. Need byte 56 of Inq to determine
	 * support. If available, default QAS to off and allow enabling.
	 * If not available, default QAS to on, turn off for non-disks.
	 */

	/* Set flags based on Inquiry data
	 */
	version = target->inq_data[2] & 0x07;
	if (version < 2) {
		width = 0;
		factor = MPT_ULTRA2;
		offset = pspi_data->maxSyncOffset;
		target->tflags &= ~MPT_TARGET_FLAGS_Q_YES;
	} else {
		if (target->inq_data[7] & 0x20) {
			width = 1;
		}

		if (target->inq_data[7] & 0x10) {
			factor = pspi_data->minSyncFactor;
			if (target->tflags & MPT_TARGET_FLAGS_VALID_56) {
				/* bits 2 & 3 show Clocking support */
				if ((byte56 & 0x0C) == 0)
					factor = MPT_ULTRA2;
				else {
					if ((byte56 & 0x03) == 0)
						factor = MPT_ULTRA160;
					else {
						factor = MPT_ULTRA320;
						if (byte56 & 0x02)
						{
							ddvtprintk((KERN_INFO "Enabling QAS due to byte56=%02x on id=%d!\n", byte56, id));
							noQas = 0;
						}
						if (target->inq_data[0] == TYPE_TAPE) {
							if (byte56 & 0x01)
								target->negoFlags |= MPT_TAPE_NEGO_IDP;
						}
					}
				}
			} else {
				ddvtprintk((KERN_INFO "Enabling QAS on id=%d due to ~TARGET_FLAGS_VALID_56!\n", id));
				noQas = 0;
			}

			offset = pspi_data->maxSyncOffset;

			/* If RAID, never disable QAS
			 * else if non RAID, do not disable
			 *   QAS if bit 1 is set
			 * bit 1 QAS support, non-raid only
			 * bit 0 IU support
			 */
			if (target->raidVolume == 1) {
				noQas = 0;
			}
		} else {
			factor = MPT_ASYNC;
			offset = 0;
		}
	}

	if ( (target->inq_data[7] & 0x02) == 0) {
		target->tflags &= ~MPT_TARGET_FLAGS_Q_YES;
	}

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
	if ((!width) && (factor < MPT_ULTRA2)) {
		factor = MPT_ULTRA2;
	}

	/* Save the data to the target structure.
	 */
	target->minSyncFactor = factor;
	target->maxOffset = offset;
	target->maxWidth = width;

	target->tflags |= MPT_TARGET_FLAGS_VALID_NEGO;

	/* Disable unused features.
	 */
	if (!width)
		target->negoFlags |= MPT_TARGET_NO_NEGO_WIDE;

	if (!offset)
		target->negoFlags |= MPT_TARGET_NO_NEGO_SYNC;

	if ( factor > MPT_ULTRA320 )
		noQas = 0;

	/* GEM, processor WORKAROUND
	 */
	if ((target->inq_data[0] == TYPE_PROCESSOR) || (target->inq_data[0] > 0x08)) {
		target->negoFlags |= (MPT_TARGET_NO_NEGO_WIDE | MPT_TARGET_NO_NEGO_SYNC);
		pspi_data->dvStatus[id] |= MPT_SCSICFG_BLK_NEGO;
	} else {
		if (noQas && (pspi_data->noQas == 0)) {
			pspi_data->noQas |= MPT_TARGET_NO_NEGO_QAS;
			target->negoFlags |= MPT_TARGET_NO_NEGO_QAS;

			/* Disable QAS in a mixed configuration case
	 		*/

			ddvtprintk((KERN_INFO "Disabling QAS due to noQas=%02x on id=%d!\n", noQas, id));
			for (ii = 0; ii < id; ii++) {
				if ( (vdev = hd->Targets[ii]) ) {
					vdev->negoFlags |= MPT_TARGET_NO_NEGO_QAS;
					mptscsih_writeSDP1(hd, 0, ii, vdev->negoFlags);
				}
			}
		}
	}

	/* Write SDP1 on this I/O to this target */
	if (pspi_data->dvStatus[id] & MPT_SCSICFG_NEGOTIATE) {
		ddvtprintk((KERN_INFO "MPT_SCSICFG_NEGOTIATE on id=%d!\n", id));
		mptscsih_writeSDP1(hd, 0, id, hd->negoNvram);
		pspi_data->dvStatus[id] &= ~MPT_SCSICFG_NEGOTIATE;
	} else if (pspi_data->dvStatus[id] & MPT_SCSICFG_BLK_NEGO) {
		ddvtprintk((KERN_INFO "MPT_SCSICFG_BLK_NEGO on id=%d!\n", id));
		mptscsih_writeSDP1(hd, 0, id, MPT_SCSICFG_BLK_NEGO);
		pspi_data->dvStatus[id] &= ~MPT_SCSICFG_BLK_NEGO;
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* If DV disabled (negoNvram set to USE_NVARM) or if not LUN 0, return.
 * Else set the NEED_DV flag after Read Capacity Issued (disks)
 * or Mode Sense (cdroms).
 *
 * Tapes, initTarget will set this flag on completion of Inquiry command.
 * Called only if DV_NOT_DONE flag is set
 */
static void
mptscsih_set_dvflags(MPT_SCSI_HOST *hd, SCSIIORequest_t *pReq)
{
	MPT_ADAPTER	*ioc = hd->ioc;
	u8 cmd;
	SpiCfgData	*pSpi;

	ddvtprintk((MYIOC_s_NOTE_FMT
		" set_dvflags: id=%d lun=%d negoNvram=%x cmd=%x\n",
		hd->ioc->name, pReq->TargetID, pReq->LUN[1], hd->negoNvram, pReq->CDB[0]));

	if ((pReq->LUN[1] != 0) || (hd->negoNvram != 0))
		return;

	cmd = pReq->CDB[0];

	if ((cmd == READ_CAPACITY) || (cmd == MODE_SENSE)) {
		pSpi = &ioc->spi_data;
		if ((ioc->raid_data.isRaid & (1 << pReq->TargetID)) && ioc->raid_data.pIocPg3) {
			/* Set NEED_DV for all hidden disks
			 */
			Ioc3PhysDisk_t *pPDisk =  ioc->raid_data.pIocPg3->PhysDisk;
			int		numPDisk = ioc->raid_data.pIocPg3->NumPhysDisks;

			while (numPDisk) {
				pSpi->dvStatus[pPDisk->PhysDiskID] |= MPT_SCSICFG_NEED_DV;
				ddvtprintk(("NEED_DV set for phys disk id %d\n", pPDisk->PhysDiskID));
				pPDisk++;
				numPDisk--;
			}
		}
		pSpi->dvStatus[pReq->TargetID] |= MPT_SCSICFG_NEED_DV;
		ddvtprintk(("NEED_DV set for visible disk id %d\n", pReq->TargetID));
	}
}

/* mptscsih_raid_set_dv_flags()
 *
 * New or replaced disk. Set DV flag and schedule DV.
 */
static void
mptscsih_set_dvflags_raid(MPT_SCSI_HOST *hd, int id)
{
	MPT_ADAPTER	*ioc = hd->ioc;
	SpiCfgData	*pSpi = &ioc->spi_data;
	Ioc3PhysDisk_t	*pPDisk;
	int		 numPDisk;

	if (hd->negoNvram != 0)
		return;

	ddvtprintk(("DV requested for phys disk id %d\n", id));
	if (ioc->raid_data.pIocPg3) {
		pPDisk =  ioc->raid_data.pIocPg3->PhysDisk;
		numPDisk = ioc->raid_data.pIocPg3->NumPhysDisks;
		while (numPDisk) {
			if (id == pPDisk->PhysDiskNum) {
				pSpi->dvStatus[pPDisk->PhysDiskID] =
				    (MPT_SCSICFG_NEED_DV | MPT_SCSICFG_DV_NOT_DONE);
				pSpi->forceDv = MPT_SCSICFG_NEED_DV;
				ddvtprintk(("NEED_DV set for phys disk id %d\n",
				    pPDisk->PhysDiskID));
				break;
			}
			pPDisk++;
			numPDisk--;
		}

		if (numPDisk == 0) {
			/* The physical disk that needs DV was not found
			 * in the stored IOC Page 3. The driver must reload
			 * this page. DV routine will set the NEED_DV flag for
			 * all phys disks that have DV_NOT_DONE set.
			 */
			pSpi->forceDv = MPT_SCSICFG_NEED_DV | MPT_SCSICFG_RELOAD_IOC_PG3;
			ddvtprintk(("phys disk %d not found. Setting reload IOC Pg3 Flag\n",id));
		}
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * If no Target, bus reset on 1st I/O. Set the flag to
 * prevent any future negotiations to this device.
 */
static void
mptscsih_no_negotiate(MPT_SCSI_HOST *hd, int target_id)
{

	if ((hd->Targets) && (hd->Targets[target_id] == NULL))
		hd->ioc->spi_data.dvStatus[target_id] |= MPT_SCSICFG_BLK_NEGO;

	return;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  SCSI Config Page functionality ...
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*	mptscsih_setDevicePage1Flags  - add Requested and Configuration fields flags
 *	based on width, factor and offset parameters.
 *	@width: bus width
 *	@factor: sync factor
 *	@offset: sync offset
 *	@requestedPtr: pointer to requested values (updated)
 *	@configurationPtr: pointer to configuration values (updated)
 *	@flags: flags to block WDTR or SDTR negotiation
 *
 *	Return: None.
 *
 *	Remark: Called by writeSDP1 and _dv_params
 */
static void
mptscsih_setDevicePage1Flags (u8 width, u8 factor, u8 offset, int *requestedPtr, int *configurationPtr, u8 flags)
{
	u8 nowide = flags & MPT_TARGET_NO_NEGO_WIDE;
	u8 nosync = flags & MPT_TARGET_NO_NEGO_SYNC;

	*configurationPtr = 0;
	*requestedPtr = width ? MPI_SCSIDEVPAGE1_RP_WIDE : 0;
	*requestedPtr |= (offset << 16) | (factor << 8);

	if (width && offset && !nowide && !nosync) {
		if (factor < MPT_ULTRA160) {
			*requestedPtr |= (MPI_SCSIDEVPAGE1_RP_IU + MPI_SCSIDEVPAGE1_RP_DT);
			if ((flags & MPT_TARGET_NO_NEGO_QAS) == 0)
				*requestedPtr |= MPI_SCSIDEVPAGE1_RP_QAS;
			if (flags & MPT_TAPE_NEGO_IDP)
				*requestedPtr |= 0x08000000;
		} else if (factor < MPT_ULTRA2) {
			*requestedPtr |= MPI_SCSIDEVPAGE1_RP_DT;
		}
	}

	if (nowide)
		*configurationPtr |= MPI_SCSIDEVPAGE1_CONF_WDTR_DISALLOWED;

	if (nosync)
		*configurationPtr |= MPI_SCSIDEVPAGE1_CONF_SDTR_DISALLOWED;

	return;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*	mptscsih_writeSDP1  - write SCSI Device Page 1
 *	@hd: Pointer to a SCSI Host Strucutre
 *	@portnum: IOC port number
 *	@target_id: writeSDP1 for single ID
 *	@flags: MPT_SCSICFG_ALL_IDS, MPT_SCSICFG_USE_NVRAM, MPT_SCSICFG_BLK_NEGO
 *
 *	Return: -EFAULT if read of config page header fails
 *		or 0 if success.
 *
 *	Remark: If a target has been found, the settings from the
 *		target structure are used, else the device is set
 *		to async/narrow.
 *
 *	Remark: Called during init and after a FW reload.
 *	Remark: We do not wait for a return, write pages sequentially.
 */
static int
mptscsih_writeSDP1(MPT_SCSI_HOST *hd, int portnum, int target_id, int flags)
{
	MPT_ADAPTER		*ioc = hd->ioc;
	Config_t		*pReq;
	SCSIDevicePage1_t	*pData;
	VirtDevice		*pTarget=NULL;
	MPT_FRAME_HDR		*mf;
	dma_addr_t		 dataDma;
	u16			 req_idx;
	u32			 frameOffset;
	u32			 requested, configuration, flagsLength;
	int			 ii, nvram;
	int			 id = 0, maxid = 0;
	u8			 width;
	u8			 factor;
	u8			 offset;
	u8			 bus = 0;
	u8			 negoFlags;
	u8			 maxwidth, maxoffset, maxfactor;

	if (ioc->spi_data.sdp1length == 0)
		return 0;

	if (flags & MPT_SCSICFG_ALL_IDS) {
		id = 0;
		maxid = ioc->sh->max_id - 1;
	} else if (ioc->sh) {
		id = target_id;
		maxid = min_t(int, id, ioc->sh->max_id - 1);
	}

	for (; id <= maxid; id++) {

		if (id == ioc->pfacts[portnum].PortSCSIID)
			continue;

		/* Use NVRAM to get adapter and target maximums
		 * Data over-riden by target structure information, if present
		 */
		maxwidth = ioc->spi_data.maxBusWidth;
		maxoffset = ioc->spi_data.maxSyncOffset;
		maxfactor = ioc->spi_data.minSyncFactor;
		if (ioc->spi_data.nvram && (ioc->spi_data.nvram[id] != MPT_HOST_NVRAM_INVALID)) {
			nvram = ioc->spi_data.nvram[id];

			if (maxwidth)
				maxwidth = nvram & MPT_NVRAM_WIDE_DISABLE ? 0 : 1;

			if (maxoffset > 0) {
				maxfactor = (nvram & MPT_NVRAM_SYNC_MASK) >> 8;
				if (maxfactor == 0) {
					/* Key for async */
					maxfactor = MPT_ASYNC;
					maxoffset = 0;
				} else if (maxfactor < ioc->spi_data.minSyncFactor) {
					maxfactor = ioc->spi_data.minSyncFactor;
				}
			} else
				maxfactor = MPT_ASYNC;
		}

		/* Set the negotiation flags.
		 */
		negoFlags = ioc->spi_data.noQas;
		if (!maxwidth)
			negoFlags |= MPT_TARGET_NO_NEGO_WIDE;

		if (!maxoffset)
			negoFlags |= MPT_TARGET_NO_NEGO_SYNC;

		if (flags & MPT_SCSICFG_USE_NVRAM) {
			width = maxwidth;
			factor = maxfactor;
			offset = maxoffset;
		} else {
			width = 0;
			factor = MPT_ASYNC;
			offset = 0;
			//negoFlags = 0;
			//negoFlags = MPT_TARGET_NO_NEGO_SYNC;
		}

		/* If id is not a raid volume, get the updated
		 * transmission settings from the target structure.
		 */
		if (hd->Targets && (pTarget = hd->Targets[id]) && !pTarget->raidVolume) {
			width = pTarget->maxWidth;
			factor = pTarget->minSyncFactor;
			offset = pTarget->maxOffset;
			negoFlags = pTarget->negoFlags;
		}

#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
		/* Force to async and narrow if DV has not been executed
		 * for this ID
		 */
		if ((hd->ioc->spi_data.dvStatus[id] & MPT_SCSICFG_DV_NOT_DONE) != 0) {
			width = 0;
			factor = MPT_ASYNC;
			offset = 0;
		}
#endif

		if (flags & MPT_SCSICFG_BLK_NEGO)
			negoFlags |= MPT_TARGET_NO_NEGO_WIDE | MPT_TARGET_NO_NEGO_SYNC;

		mptscsih_setDevicePage1Flags(width, factor, offset,
					&requested, &configuration, negoFlags);
		dnegoprintk(("writeSDP1: id=%d width=%d factor=%x offset=%x negoFlags=%x request=%x config=%x\n",
			target_id, width, factor, offset, negoFlags, requested, configuration));

		/* Get a MF for this command.
		 */
		if ((mf = mpt_get_msg_frame(ioc->DoneCtx, ioc)) == NULL) {
			dfailprintk((MYIOC_s_WARN_FMT "write SDP1: no msg frames!\n",
				ioc->name));
			return -EAGAIN;
		}

		ddvprintk((MYIOC_s_INFO_FMT "WriteSDP1 (mf=%p, id=%d, req=0x%x, cfg=0x%x)\n",
			hd->ioc->name, mf, id, requested, configuration));


		/* Set the request and the data pointers.
		 * Request takes: 36 bytes (32 bit SGE)
		 * SCSI Device Page 1 requires 16 bytes
		 * 40 + 16 <= size of SCSI IO Request = 56 bytes
		 * and MF size >= 64 bytes.
		 * Place data at end of MF.
		 */
		pReq = (Config_t *)mf;

		req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);
		frameOffset = ioc->req_sz - sizeof(SCSIDevicePage1_t);

		pData = (SCSIDevicePage1_t *)((u8 *) mf + frameOffset);
		dataDma = ioc->req_frames_dma + (req_idx * ioc->req_sz) + frameOffset;

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
		pReq->Header.PageVersion = ioc->spi_data.sdp1version;
		pReq->Header.PageLength = ioc->spi_data.sdp1length;
		pReq->Header.PageNumber = 1;
		pReq->Header.PageType = MPI_CONFIG_PAGETYPE_SCSI_DEVICE;
		pReq->PageAddress = cpu_to_le32(id | (bus << 8 ));

		/* Add a SGE to the config request.
		 */
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_WRITE | ioc->spi_data.sdp1length * 4;

		mpt_add_sge((char *)&pReq->PageBufferSGE, flagsLength, dataDma);

		/* Set up the common data portion
		 */
		pData->Header.PageVersion = pReq->Header.PageVersion;
		pData->Header.PageLength = pReq->Header.PageLength;
		pData->Header.PageNumber = pReq->Header.PageNumber;
		pData->Header.PageType = pReq->Header.PageType;
		pData->RequestedParameters = cpu_to_le32(requested);
		pData->Reserved = 0;
		pData->Configuration = cpu_to_le32(configuration);

		dprintk((MYIOC_s_INFO_FMT
			"write SDP1: id %d pgaddr 0x%x req 0x%x config 0x%x\n",
				ioc->name, id, (id | (bus<<8)),
				requested, configuration));

		mpt_put_msg_frame(ioc->DoneCtx, ioc, mf);
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*	mptscsih_writeIOCPage4  - write IOC Page 4
 *	@hd: Pointer to a SCSI Host Structure
 *	@target_id: write IOC Page4 for this ID & Bus
 *
 *	Return: -EAGAIN if unable to obtain a Message Frame
 *		or 0 if success.
 *
 *	Remark: We do not wait for a return, write pages sequentially.
 */
static int
mptscsih_writeIOCPage4(MPT_SCSI_HOST *hd, int target_id, int bus)
{
	MPT_ADAPTER		*ioc = hd->ioc;
	Config_t		*pReq;
	IOCPage4_t		*IOCPage4Ptr;
	MPT_FRAME_HDR		*mf;
	dma_addr_t		 dataDma;
	u16			 req_idx;
	u32			 frameOffset;
	u32			 flagsLength;
	int			 ii;

	/* Get a MF for this command.
	 */
	if ((mf = mpt_get_msg_frame(ioc->DoneCtx, ioc)) == NULL) {
		dfailprintk((MYIOC_s_WARN_FMT "writeIOCPage4 : no msg frames!\n",
					ioc->name));
		return -EAGAIN;
	}

	/* Set the request and the data pointers.
	 * Place data at end of MF.
	 */
	pReq = (Config_t *)mf;

	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);
	frameOffset = ioc->req_sz - sizeof(IOCPage4_t);

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
       	IOCPage4Ptr->SEP[ii].SEPTargetID = target_id;
       	IOCPage4Ptr->SEP[ii].SEPBus = bus;
       	pReq->Header = IOCPage4Ptr->Header;
	pReq->PageAddress = cpu_to_le32(target_id | (bus << 8 ));

	/* Add a SGE to the config request.
	 */
	flagsLength = MPT_SGE_FLAGS_SSIMPLE_WRITE |
		(IOCPage4Ptr->Header.PageLength + ii) * 4;

	mpt_add_sge((char *)&pReq->PageBufferSGE, flagsLength, dataDma);

	dinitprintk((MYIOC_s_INFO_FMT
		"writeIOCPage4: MaxSEP=%d ActiveSEP=%d id=%d bus=%d\n",
			ioc->name, IOCPage4Ptr->MaxSEP, IOCPage4Ptr->ActiveSEP, target_id, bus));

	mpt_put_msg_frame(ioc->DoneCtx, ioc, mf);

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Bus Scan and Domain Validation functionality ...
 */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_scandv_complete - Scan and DV callback routine registered
 *	to Fustion MPT (base) driver.
 *
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mf: Pointer to original MPT request frame
 *	@mr: Pointer to MPT reply frame (NULL if TurboReply)
 *
 *	This routine is called from mpt.c::mpt_interrupt() at the completion
 *	of any SCSI IO request.
 *	This routine is registered with the Fusion MPT (base) driver at driver
 *	load/init time via the mpt_register() API call.
 *
 *	Returns 1 indicating alloc'd request frame ptr should be freed.
 *
 *	Remark: Sets a completion code and (possibly) saves sense data
 *	in the IOC member localReply structure.
 *	Used ONLY for DV and other internal commands.
 */
int
mptscsih_scandv_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *mr)
{
	MPT_SCSI_HOST	*hd;
	SCSIIORequest_t *pReq;
	int		 completionCode;
	u16		 req_idx;

	hd = (MPT_SCSI_HOST *) ioc->sh->hostdata;

	if ((mf == NULL) ||
	    (mf >= MPT_INDEX_2_MFPTR(ioc, ioc->req_depth))) {
		printk(MYIOC_s_ERR_FMT
			"ScanDvComplete, %s req frame ptr! (=%p)\n",
				ioc->name, mf?"BAD":"NULL", (void *) mf);
		goto wakeup;
	}

	del_timer(&hd->timer);
	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);
	hd->ScsiLookup[req_idx] = NULL;
	pReq = (SCSIIORequest_t *) mf;

	if (mf != hd->cmdPtr) {
		printk(MYIOC_s_WARN_FMT "ScanDvComplete (mf=%p, cmdPtr=%p, idx=%d)\n",
				hd->ioc->name, (void *)mf, (void *) hd->cmdPtr, req_idx);
	}
	hd->cmdPtr = NULL;

	ddvprintk((MYIOC_s_INFO_FMT "ScanDvComplete (mf=%p,mr=%p,idx=%d)\n",
			hd->ioc->name, mf, mr, req_idx));

	hd->pLocal = &hd->localReply;
	hd->pLocal->scsiStatus = 0;

	/* If target struct exists, clear sense valid flag.
	 */
	if (mr == NULL) {
		completionCode = MPT_SCANDV_GOOD;
	} else {
		SCSIIOReply_t	*pReply;
		u16		 status;
		u8		 scsi_status;

		pReply = (SCSIIOReply_t *) mr;

		status = le16_to_cpu(pReply->IOCStatus) & MPI_IOCSTATUS_MASK;
		scsi_status = pReply->SCSIStatus;

		ddvtprintk((KERN_NOTICE "  IOCStatus=%04xh, SCSIState=%02xh, SCSIStatus=%02xh, IOCLogInfo=%08xh\n",
			     status, pReply->SCSIState, scsi_status,
			     le32_to_cpu(pReply->IOCLogInfo)));

		switch(status) {

		case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE:	/* 0x0043 */
			completionCode = MPT_SCANDV_SELECTION_TIMEOUT;
			break;

		case MPI_IOCSTATUS_SCSI_IO_DATA_ERROR:		/* 0x0046 */
		case MPI_IOCSTATUS_SCSI_TASK_TERMINATED:	/* 0x0048 */
		case MPI_IOCSTATUS_SCSI_IOC_TERMINATED:		/* 0x004B */
		case MPI_IOCSTATUS_SCSI_EXT_TERMINATED:		/* 0x004C */
			completionCode = MPT_SCANDV_DID_RESET;
			break;

		case MPI_IOCSTATUS_SCSI_DATA_UNDERRUN:		/* 0x0045 */
		case MPI_IOCSTATUS_SCSI_RECOVERED_ERROR:	/* 0x0040 */
		case MPI_IOCSTATUS_SUCCESS:			/* 0x0000 */
			if (pReply->Function == MPI_FUNCTION_CONFIG) {
				ConfigReply_t *pr = (ConfigReply_t *)mr;
				completionCode = MPT_SCANDV_GOOD;
				hd->pLocal->header.PageVersion = pr->Header.PageVersion;
				hd->pLocal->header.PageLength = pr->Header.PageLength;
				hd->pLocal->header.PageNumber = pr->Header.PageNumber;
				hd->pLocal->header.PageType = pr->Header.PageType;

			} else if (pReply->Function == MPI_FUNCTION_RAID_ACTION) {
				/* If the RAID Volume request is successful,
				 * return GOOD, else indicate that
				 * some type of error occurred.
				 */
				MpiRaidActionReply_t	*pr = (MpiRaidActionReply_t *)mr;
				if (le16_to_cpu(pr->ActionStatus) == MPI_RAID_ACTION_ASTATUS_SUCCESS)
					completionCode = MPT_SCANDV_GOOD;
				else
					completionCode = MPT_SCANDV_SOME_ERROR;

			} else if (pReply->SCSIState & MPI_SCSI_STATE_AUTOSENSE_VALID) {
				u8		*sense_data;
				int		 sz;

				/* save sense data in global structure
				 */
				completionCode = MPT_SCANDV_SENSE;
				hd->pLocal->scsiStatus = scsi_status;
				sense_data = ((u8 *)hd->ioc->sense_buf_pool +
					(req_idx * MPT_SENSE_BUFFER_ALLOC));

				sz = min_t(int, pReq->SenseBufferLength,
							SCSI_STD_SENSE_BYTES);
				memcpy(hd->pLocal->sense, sense_data, sz);

				ddvprintk((KERN_NOTICE "  Check Condition, sense ptr %p\n",
						sense_data));
			} else if (pReply->SCSIState & MPI_SCSI_STATE_AUTOSENSE_FAILED) {
				if (pReq->CDB[0] == INQUIRY)
					completionCode = MPT_SCANDV_ISSUE_SENSE;
				else
					completionCode = MPT_SCANDV_DID_RESET;
			}
			else if (pReply->SCSIState & MPI_SCSI_STATE_NO_SCSI_STATUS)
				completionCode = MPT_SCANDV_DID_RESET;
			else if (pReply->SCSIState & MPI_SCSI_STATE_TERMINATED)
				completionCode = MPT_SCANDV_DID_RESET;
			else {
				completionCode = MPT_SCANDV_GOOD;
				hd->pLocal->scsiStatus = scsi_status;
			}
			break;

		case MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR:		/* 0x0047 */
			if (pReply->SCSIState & MPI_SCSI_STATE_TERMINATED)
				completionCode = MPT_SCANDV_DID_RESET;
			else
				completionCode = MPT_SCANDV_SOME_ERROR;
			break;

		default:
			completionCode = MPT_SCANDV_SOME_ERROR;
			break;

		}	/* switch(status) */

		ddvtprintk((KERN_NOTICE "  completionCode set to %08xh\n",
				completionCode));
	} /* end of address reply case */

	hd->pLocal->completion = completionCode;

	/* MF and RF are freed in mpt_interrupt
	 */
wakeup:
	/* Free Chain buffers (will never chain) in scan or dv */
	//mptscsih_freeChainBuffers(ioc, req_idx);

	/*
	 * Wake up the original calling thread
	 */
	hd->scandv_wait_done = 1;
	wake_up(&hd->scandv_waitq);

	return 1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*	mptscsih_timer_expired - Call back for timer process.
 *	Used only for dv functionality.
 *	@data: Pointer to MPT_SCSI_HOST recast as an unsigned long
 *
 */
void
mptscsih_timer_expired(unsigned long data)
{
	MPT_SCSI_HOST *hd = (MPT_SCSI_HOST *) data;

	ddvprintk((MYIOC_s_WARN_FMT "Timer Expired! Cmd %p\n", hd->ioc->name, hd->cmdPtr));

	if (hd->cmdPtr) {
		MPIHeader_t *cmd = (MPIHeader_t *)hd->cmdPtr;

		if (cmd->Function == MPI_FUNCTION_SCSI_IO_REQUEST) {
			/* Desire to issue a task management request here.
			 * TM requests MUST be single threaded.
			 * If old eh code and no TM current, issue request.
			 * If new eh code, do nothing. Wait for OS cmd timeout
			 *	for bus reset.
			 */
			ddvtprintk((MYIOC_s_NOTE_FMT "DV Cmd Timeout: NoOp\n", hd->ioc->name));
		} else {
			/* Perform a FW reload */
			if (mpt_HardResetHandler(hd->ioc, NO_SLEEP) < 0) {
				printk(MYIOC_s_WARN_FMT "Firmware Reload FAILED!\n", hd->ioc->name);
			}
		}
	} else {
		/* This should NEVER happen */
		printk(MYIOC_s_WARN_FMT "Null cmdPtr!!!!\n", hd->ioc->name);
	}

	/* No more processing.
	 * TM call will generate an interrupt for SCSI TM Management.
	 * The FW will reply to all outstanding commands, callback will finish cleanup.
	 * Hard reset clean-up will free all resources.
	 */
	ddvprintk((MYIOC_s_WARN_FMT "Timer Expired Complete!\n", hd->ioc->name));

	return;
}

#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*	mptscsih_do_raid - Format and Issue a RAID volume request message.
 *	@hd: Pointer to scsi host structure
 *	@action: What do be done.
 *	@id: Logical target id.
 *	@bus: Target locations bus.
 *
 *	Returns: < 0 on a fatal error
 *		0 on success
 *
 *	Remark: Wait to return until reply processed by the ISR.
 */
static int
mptscsih_do_raid(MPT_SCSI_HOST *hd, u8 action, INTERNAL_CMD *io)
{
	MpiRaidActionRequest_t	*pReq;
	MPT_FRAME_HDR		*mf;
	int			in_isr;

	in_isr = in_interrupt();
	if (in_isr) {
		dprintk((MYIOC_s_WARN_FMT "Internal raid request not allowed in ISR context!\n",
       				hd->ioc->name));
		return -EPERM;
	}

	/* Get and Populate a free Frame
	 */
	if ((mf = mpt_get_msg_frame(hd->ioc->InternalCtx, hd->ioc)) == NULL) {
		ddvprintk((MYIOC_s_WARN_FMT "_do_raid: no msg frames!\n",
					hd->ioc->name));
		return -EAGAIN;
	}
	pReq = (MpiRaidActionRequest_t *)mf;
	pReq->Action = action;
	pReq->Reserved1 = 0;
	pReq->ChainOffset = 0;
	pReq->Function = MPI_FUNCTION_RAID_ACTION;
	pReq->VolumeID = io->id;
	pReq->VolumeBus = io->bus;
	pReq->PhysDiskNum = io->physDiskNum;
	pReq->MsgFlags = 0;
	pReq->Reserved2 = 0;
	pReq->ActionDataWord = 0; /* Reserved for this action */
	//pReq->ActionDataSGE = 0;

	mpt_add_sge((char *)&pReq->ActionDataSGE,
		MPT_SGE_FLAGS_SSIMPLE_READ | 0, (dma_addr_t) -1);

	ddvprintk((MYIOC_s_INFO_FMT "RAID Volume action %x id %d\n",
			hd->ioc->name, action, io->id));

	hd->pLocal = NULL;
	hd->timer.expires = jiffies + HZ*10; /* 10 second timeout */
	hd->scandv_wait_done = 0;

	/* Save cmd pointer, for resource free if timeout or
	 * FW reload occurs
	 */
	hd->cmdPtr = mf;

	add_timer(&hd->timer);
	mpt_put_msg_frame(hd->ioc->InternalCtx, hd->ioc, mf);
	wait_event(hd->scandv_waitq, hd->scandv_wait_done);

	if ((hd->pLocal == NULL) || (hd->pLocal->completion != MPT_SCANDV_GOOD))
		return -1;

	return 0;
}
#endif /* ~MPTSCSIH_ENABLE_DOMAIN_VALIDATION */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_do_cmd - Do internal command.
 *	@hd: MPT_SCSI_HOST pointer
 *	@io: INTERNAL_CMD pointer.
 *
 *	Issue the specified internally generated command and do command
 *	specific cleanup. For bus scan / DV only.
 *	NOTES: If command is Inquiry and status is good,
 *	initialize a target structure, save the data
 *
 *	Remark: Single threaded access only.
 *
 *	Return:
 *		< 0 if an illegal command or no resources
 *
 *		   0 if good
 *
 *		 > 0 if command complete but some type of completion error.
 */
static int
mptscsih_do_cmd(MPT_SCSI_HOST *hd, INTERNAL_CMD *io)
{
	MPT_FRAME_HDR	*mf;
	SCSIIORequest_t	*pScsiReq;
	SCSIIORequest_t	 ReqCopy;
	int		 my_idx, ii, dir;
	int		 rc, cmdTimeout;
	int		in_isr;
	char		 cmdLen;
	char		 CDB[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	char		 cmd = io->cmd;

	in_isr = in_interrupt();
	if (in_isr) {
		dprintk((MYIOC_s_WARN_FMT "Internal SCSI IO request not allowed in ISR context!\n",
       				hd->ioc->name));
		return -EPERM;
	}


	/* Set command specific information
	 */
	switch (cmd) {
	case INQUIRY:
		cmdLen = 6;
		dir = MPI_SCSIIO_CONTROL_READ;
		CDB[0] = cmd;
		CDB[4] = io->size;
		cmdTimeout = 10;
		break;

	case TEST_UNIT_READY:
		cmdLen = 6;
		dir = MPI_SCSIIO_CONTROL_READ;
		cmdTimeout = 10;
		break;

	case START_STOP:
		cmdLen = 6;
		dir = MPI_SCSIIO_CONTROL_READ;
		CDB[0] = cmd;
		CDB[4] = 1;	/*Spin up the disk */
		cmdTimeout = 15;
		break;

	case REQUEST_SENSE:
		cmdLen = 6;
		CDB[0] = cmd;
		CDB[4] = io->size;
		dir = MPI_SCSIIO_CONTROL_READ;
		cmdTimeout = 10;
		break;

	case READ_BUFFER:
		cmdLen = 10;
		dir = MPI_SCSIIO_CONTROL_READ;
		CDB[0] = cmd;
		if (io->flags & MPT_ICFLAG_ECHO) {
			CDB[1] = 0x0A;
		} else {
			CDB[1] = 0x02;
		}

		if (io->flags & MPT_ICFLAG_BUF_CAP) {
			CDB[1] |= 0x01;
		}
		CDB[6] = (io->size >> 16) & 0xFF;
		CDB[7] = (io->size >>  8) & 0xFF;
		CDB[8] = io->size & 0xFF;
		cmdTimeout = 10;
		break;

	case WRITE_BUFFER:
		cmdLen = 10;
		dir = MPI_SCSIIO_CONTROL_WRITE;
		CDB[0] = cmd;
		if (io->flags & MPT_ICFLAG_ECHO) {
			CDB[1] = 0x0A;
		} else {
			CDB[1] = 0x02;
		}
		CDB[6] = (io->size >> 16) & 0xFF;
		CDB[7] = (io->size >>  8) & 0xFF;
		CDB[8] = io->size & 0xFF;
		cmdTimeout = 10;
		break;

	case RESERVE:
		cmdLen = 6;
		dir = MPI_SCSIIO_CONTROL_READ;
		CDB[0] = cmd;
		cmdTimeout = 10;
		break;

	case RELEASE:
		cmdLen = 6;
		dir = MPI_SCSIIO_CONTROL_READ;
		CDB[0] = cmd;
		cmdTimeout = 10;
		break;

	case SYNCHRONIZE_CACHE:
		cmdLen = 10;
		dir = MPI_SCSIIO_CONTROL_READ;
		CDB[0] = cmd;
//		CDB[1] = 0x02;	/* set immediate bit */
		cmdTimeout = 10;
		break;

	default:
		/* Error Case */
		return -EFAULT;
	}

	/* Get and Populate a free Frame
	 */
	if ((mf = mpt_get_msg_frame(hd->ioc->InternalCtx, hd->ioc)) == NULL) {
		ddvprintk((MYIOC_s_WARN_FMT "No msg frames!\n",
					hd->ioc->name));
		return -EBUSY;
	}

	pScsiReq = (SCSIIORequest_t *) mf;

	/* Get the request index */
	my_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);
	ADD_INDEX_LOG(my_idx); /* for debug */

	if (io->flags & MPT_ICFLAG_PHYS_DISK) {
		pScsiReq->TargetID = io->physDiskNum;
		pScsiReq->Bus = 0;
		pScsiReq->ChainOffset = 0;
		pScsiReq->Function = MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH;
	} else {
		pScsiReq->TargetID = io->id;
		pScsiReq->Bus = io->bus;
		pScsiReq->ChainOffset = 0;
		pScsiReq->Function = MPI_FUNCTION_SCSI_IO_REQUEST;
	}

	pScsiReq->CDBLength = cmdLen;
	pScsiReq->SenseBufferLength = MPT_SENSE_BUFFER_SIZE;

	pScsiReq->Reserved = 0;

	pScsiReq->MsgFlags = mpt_msg_flags();
	/* MsgContext set in mpt_get_msg_fram call  */

	for (ii=0; ii < 8; ii++)
		pScsiReq->LUN[ii] = 0;
	pScsiReq->LUN[1] = io->lun;

	if (io->flags & MPT_ICFLAG_TAGGED_CMD)
		pScsiReq->Control = cpu_to_le32(dir | MPI_SCSIIO_CONTROL_SIMPLEQ);
	else
		pScsiReq->Control = cpu_to_le32(dir | MPI_SCSIIO_CONTROL_UNTAGGED);

	if (cmd == REQUEST_SENSE) {
		pScsiReq->Control = cpu_to_le32(dir | MPI_SCSIIO_CONTROL_UNTAGGED);
		ddvprintk((MYIOC_s_INFO_FMT "Untagged! 0x%2x\n",
			hd->ioc->name, cmd));
	}

	for (ii=0; ii < 16; ii++)
		pScsiReq->CDB[ii] = CDB[ii];

	pScsiReq->DataLength = cpu_to_le32(io->size);
	pScsiReq->SenseBufferLowAddr = cpu_to_le32(hd->ioc->sense_buf_low_dma
					   + (my_idx * MPT_SENSE_BUFFER_ALLOC));

	ddvprintk((MYIOC_s_INFO_FMT "Sending Command 0x%x for (%d:%d:%d)\n",
			hd->ioc->name, cmd, io->bus, io->id, io->lun));

	if (dir == MPI_SCSIIO_CONTROL_READ) {
		mpt_add_sge((char *) &pScsiReq->SGL,
			MPT_SGE_FLAGS_SSIMPLE_READ | io->size,
			io->data_dma);
	} else {
		mpt_add_sge((char *) &pScsiReq->SGL,
			MPT_SGE_FLAGS_SSIMPLE_WRITE | io->size,
			io->data_dma);
	}

	/* The ISR will free the request frame, but we need
	 * the information to initialize the target. Duplicate.
	 */
	memcpy(&ReqCopy, pScsiReq, sizeof(SCSIIORequest_t));

	/* Issue this command after:
	 *	finish init
	 *	add timer
	 * Wait until the reply has been received
	 *  ScsiScanDvCtx callback function will
	 *	set hd->pLocal;
	 *	set scandv_wait_done and call wake_up
	 */
	hd->pLocal = NULL;
	hd->timer.expires = jiffies + HZ*cmdTimeout;
	hd->scandv_wait_done = 0;

	/* Save cmd pointer, for resource free if timeout or
	 * FW reload occurs
	 */
	hd->cmdPtr = mf;

	add_timer(&hd->timer);
	mpt_put_msg_frame(hd->ioc->InternalCtx, hd->ioc, mf);
	wait_event(hd->scandv_waitq, hd->scandv_wait_done);

	if (hd->pLocal) {
		rc = hd->pLocal->completion;
		hd->pLocal->skip = 0;

		/* Always set fatal error codes in some cases.
		 */
		if (rc == MPT_SCANDV_SELECTION_TIMEOUT)
			rc = -ENXIO;
		else if (rc == MPT_SCANDV_SOME_ERROR)
			rc =  -rc;
	} else {
		rc = -EFAULT;
		/* This should never happen. */
		ddvprintk((MYIOC_s_INFO_FMT "_do_cmd: Null pLocal!!!\n",
				hd->ioc->name));
	}

	return rc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_synchronize_cache - Send SYNCHRONIZE_CACHE to all disks.
 *	@hd: Pointer to MPT_SCSI_HOST structure
 *	@portnum: IOC port number
 *
 *	Uses the ISR, but with special processing.
 *	MUST be single-threaded.
 *
 *	Return: 0 on completion
 */
static int
mptscsih_synchronize_cache(MPT_SCSI_HOST *hd, int portnum)
{
	MPT_ADAPTER		*ioc= hd->ioc;
	VirtDevice		*pTarget;
	SCSIDevicePage1_t	*pcfg1Data = NULL;
	INTERNAL_CMD		 iocmd;
	CONFIGPARMS		 cfg;
	dma_addr_t		 cfg1_dma_addr = -1;
	ConfigPageHeader_t	 header1;
	int			 bus = 0;
	int			 id = 0;
	int			 lun;
	int			 indexed_lun, lun_index;
	int			 hostId = ioc->pfacts[portnum].PortSCSIID;
	int			 max_id;
	int			 requested, configuration, data;
	int			 doConfig = 0;
	u8			 flags, factor;

	max_id = ioc->sh->max_id - 1;

	/* Following parameters will not change
	 * in this routine.
	 */
	iocmd.cmd = SYNCHRONIZE_CACHE;
	iocmd.flags = 0;
	iocmd.physDiskNum = -1;
	iocmd.data = NULL;
	iocmd.data_dma = -1;
	iocmd.size = 0;
	iocmd.rsvd = iocmd.rsvd2 = 0;

	/* No SCSI hosts
	 */
	if (hd->Targets == NULL)
		return 0;

	/* Skip the host
	 */
	if (id == hostId)
		id++;

	/* Write SDP1 for all SCSI devices
	 * Alloc memory and set up config buffer
	 */
	if (ioc->bus_type == SCSI) {
		if (ioc->spi_data.sdp1length > 0) {
			pcfg1Data = (SCSIDevicePage1_t *)pci_alloc_consistent(ioc->pcidev,
					 ioc->spi_data.sdp1length * 4, &cfg1_dma_addr);

			if (pcfg1Data != NULL) {
				doConfig = 1;
				header1.PageVersion = ioc->spi_data.sdp1version;
				header1.PageLength = ioc->spi_data.sdp1length;
				header1.PageNumber = 1;
				header1.PageType = MPI_CONFIG_PAGETYPE_SCSI_DEVICE;
				cfg.cfghdr.hdr = &header1;
				cfg.physAddr = cfg1_dma_addr;
				cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
				cfg.dir = 1;
				cfg.timeout = 0;
			}
		}
	}

	/* loop through all devices on this port
	 */
	while (bus < MPT_MAX_BUS) {
		iocmd.bus = bus;
		iocmd.id = id;
		pTarget = hd->Targets[(int)id];

		if (doConfig) {

			/* Set the negotiation flags */
			if (pTarget && (pTarget = hd->Targets[id]) && !pTarget->raidVolume) {
				flags = pTarget->negoFlags;
			} else {
				flags = hd->ioc->spi_data.noQas;
				if (hd->ioc->spi_data.nvram && (hd->ioc->spi_data.nvram[id] != MPT_HOST_NVRAM_INVALID)) {
					data = hd->ioc->spi_data.nvram[id];

					if (data & MPT_NVRAM_WIDE_DISABLE)
						flags |= MPT_TARGET_NO_NEGO_WIDE;

					factor = (data & MPT_NVRAM_SYNC_MASK) >> MPT_NVRAM_SYNC_SHIFT;
					if ((factor == 0) || (factor == MPT_ASYNC))
						flags |= MPT_TARGET_NO_NEGO_SYNC;
				}
			}

			/* Force to async, narrow */
			mptscsih_setDevicePage1Flags(0, MPT_ASYNC, 0, &requested,
					&configuration, flags);
			dnegoprintk(("syncronize cache: id=%d width=0 factor=MPT_ASYNC "
				"offset=0 negoFlags=%x request=%x config=%x\n",
				id, flags, requested, configuration));
			pcfg1Data->RequestedParameters = cpu_to_le32(requested);
			pcfg1Data->Reserved = 0;
			pcfg1Data->Configuration = cpu_to_le32(configuration);
			cfg.pageAddr = (bus<<8) | id;
			mpt_config(hd->ioc, &cfg);
		}

		/* If target Ptr NULL or if this target is NOT a disk, skip.
		 */
		if ((pTarget) && (pTarget->inq_data[0] == TYPE_DISK)){
			for (lun=0; lun <= MPT_LAST_LUN; lun++) {
				/* If LUN present, issue the command
				 */
				lun_index = (lun >> 5);  /* 32 luns per lun_index */
				indexed_lun = (lun % 32);
				if (pTarget->luns[lun_index] & (1<<indexed_lun)) {
					iocmd.lun = lun;
					(void) mptscsih_do_cmd(hd, &iocmd);
				}
			}
		}

		/* get next relevant device */
		id++;

		if (id == hostId)
			id++;

		if (id > max_id) {
			id = 0;
			bus++;
		}
	}

	if (pcfg1Data) {
		pci_free_consistent(ioc->pcidev, header1.PageLength * 4, pcfg1Data, cfg1_dma_addr);
	}

	return 0;
}

#ifdef MPTSCSIH_ENABLE_DOMAIN_VALIDATION
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_domainValidation - Top level handler for domain validation.
 *	@hd: Pointer to MPT_SCSI_HOST structure.
 *
 *	Uses the ISR, but with special processing.
 *	Called from schedule, should not be in interrupt mode.
 *	While thread alive, do dv for all devices needing dv
 *
 *	Return: None.
 */
static void
mptscsih_domainValidation(void *arg)
{
	MPT_SCSI_HOST		*hd;
	MPT_ADAPTER		*ioc;
	unsigned long		 flags;
	int 			 id, maxid, dvStatus, did;
	int			 ii, isPhysDisk;

	spin_lock_irqsave(&dvtaskQ_lock, flags);
	dvtaskQ_active = 1;
	if (dvtaskQ_release) {
		dvtaskQ_active = 0;
		spin_unlock_irqrestore(&dvtaskQ_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&dvtaskQ_lock, flags);

	/* For this ioc, loop through all devices and do dv to each device.
	 * When complete with this ioc, search through the ioc list, and
	 * for each scsi ioc found, do dv for all devices. Exit when no
	 * device needs dv.
	 */
	did = 1;
	while (did) {
		did = 0;
		list_for_each_entry(ioc, &ioc_list, list) {
			spin_lock_irqsave(&dvtaskQ_lock, flags);
			if (dvtaskQ_release) {
				dvtaskQ_active = 0;
				spin_unlock_irqrestore(&dvtaskQ_lock, flags);
				return;
			}
			spin_unlock_irqrestore(&dvtaskQ_lock, flags);

			msleep(250);

			/* DV only to SCSI adapters */
			if (ioc->bus_type != SCSI)
				continue;

			/* Make sure everything looks ok */
			if (ioc->sh == NULL)
				continue;

			hd = (MPT_SCSI_HOST *) ioc->sh->hostdata;
			if (hd == NULL)
				continue;

			if ((ioc->spi_data.forceDv & MPT_SCSICFG_RELOAD_IOC_PG3) != 0) {
				mpt_read_ioc_pg_3(ioc);
				if (ioc->raid_data.pIocPg3) {
					Ioc3PhysDisk_t *pPDisk = ioc->raid_data.pIocPg3->PhysDisk;
					int		numPDisk = ioc->raid_data.pIocPg3->NumPhysDisks;

					while (numPDisk) {
						if (ioc->spi_data.dvStatus[pPDisk->PhysDiskID] & MPT_SCSICFG_DV_NOT_DONE)
							ioc->spi_data.dvStatus[pPDisk->PhysDiskID] |= MPT_SCSICFG_NEED_DV;

						pPDisk++;
						numPDisk--;
					}
				}
				ioc->spi_data.forceDv &= ~MPT_SCSICFG_RELOAD_IOC_PG3;
			}

			maxid = min_t(int, ioc->sh->max_id, MPT_MAX_SCSI_DEVICES);

			for (id = 0; id < maxid; id++) {
				spin_lock_irqsave(&dvtaskQ_lock, flags);
				if (dvtaskQ_release) {
					dvtaskQ_active = 0;
					spin_unlock_irqrestore(&dvtaskQ_lock, flags);
					return;
				}
				spin_unlock_irqrestore(&dvtaskQ_lock, flags);
				dvStatus = hd->ioc->spi_data.dvStatus[id];

				if (dvStatus & MPT_SCSICFG_NEED_DV) {
					did++;
					hd->ioc->spi_data.dvStatus[id] |= MPT_SCSICFG_DV_PENDING;
					hd->ioc->spi_data.dvStatus[id] &= ~MPT_SCSICFG_NEED_DV;

					msleep(250);

					/* If hidden phys disk, block IO's to all
					 *	raid volumes
					 * else, process normally
					 */
					isPhysDisk = mptscsih_is_phys_disk(ioc, id);
					if (isPhysDisk) {
						for (ii=0; ii < MPT_MAX_SCSI_DEVICES; ii++) {
							if (hd->ioc->raid_data.isRaid & (1 << ii)) {
								hd->ioc->spi_data.dvStatus[ii] |= MPT_SCSICFG_DV_PENDING;
							}
						}
					}

					if(mpt_alt_ioc_wait(hd->ioc)!=0) {
						ddvprintk((MYIOC_s_WARN_FMT "alt_ioc busy!\n",
						    hd->ioc->name));
						continue;
					}

					if (mptscsih_doDv(hd, 0, id) == 1) {
						/* Untagged device was busy, try again
						 */
						hd->ioc->spi_data.dvStatus[id] |= MPT_SCSICFG_NEED_DV;
						hd->ioc->spi_data.dvStatus[id] &= ~MPT_SCSICFG_DV_PENDING;
					} else {
						/* DV is complete. Clear flags.
						 */
						hd->ioc->spi_data.dvStatus[id] &= ~(MPT_SCSICFG_DV_NOT_DONE | MPT_SCSICFG_DV_PENDING);
					}

					spin_lock(&hd->ioc->initializing_hba_lock);
					hd->ioc->initializing_hba_lock_flag=0;
					spin_unlock(&hd->ioc->initializing_hba_lock);

					if (isPhysDisk) {
						for (ii=0; ii < MPT_MAX_SCSI_DEVICES; ii++) {
							if (hd->ioc->raid_data.isRaid & (1 << ii)) {
								hd->ioc->spi_data.dvStatus[ii] &= ~MPT_SCSICFG_DV_PENDING;
							}
						}
					}

					if (hd->ioc->spi_data.noQas)
						mptscsih_qas_check(hd, id);
				}
			}
		}
	}

	spin_lock_irqsave(&dvtaskQ_lock, flags);
	dvtaskQ_active = 0;
	spin_unlock_irqrestore(&dvtaskQ_lock, flags);

	return;
}

/* Search IOC page 3 to determine if this is hidden physical disk
 */
/* Search IOC page 3 to determine if this is hidden physical disk
 */
static int
mptscsih_is_phys_disk(MPT_ADAPTER *ioc, int id)
{
	int i;

	if (!ioc->raid_data.isRaid || !ioc->raid_data.pIocPg3)
		return 0;

	for (i = 0; i < ioc->raid_data.pIocPg3->NumPhysDisks; i++) {
		if (id == ioc->raid_data.pIocPg3->PhysDisk[i].PhysDiskID)
			return 1;
	}

	return 0;
}

/* Write SDP1 if no QAS has been enabled
 */
static void
mptscsih_qas_check(MPT_SCSI_HOST *hd, int id)
{
	VirtDevice *pTarget;
	int ii;

	if (hd->Targets == NULL)
		return;

	for (ii=0; ii < MPT_MAX_SCSI_DEVICES; ii++) {
		if (ii == id)
			continue;

		if ((hd->ioc->spi_data.dvStatus[ii] & MPT_SCSICFG_DV_NOT_DONE) != 0)
			continue;

		pTarget = hd->Targets[ii];

		if ((pTarget != NULL) && (!pTarget->raidVolume)) {
			if ((pTarget->negoFlags & hd->ioc->spi_data.noQas) == 0) {
				pTarget->negoFlags |= hd->ioc->spi_data.noQas;
				dnegoprintk(("writeSDP1: id=%d flags=0\n", id));
				mptscsih_writeSDP1(hd, 0, ii, 0);
			}
		} else {
			if (mptscsih_is_phys_disk(hd->ioc, ii) == 1) {
				dnegoprintk(("writeSDP1: id=%d SCSICFG_USE_NVRAM\n", id));
				mptscsih_writeSDP1(hd, 0, ii, MPT_SCSICFG_USE_NVRAM);
			}
		}
	}
	return;
}



#define MPT_GET_NVRAM_VALS	0x01
#define MPT_UPDATE_MAX		0x02
#define MPT_SET_MAX		0x04
#define MPT_SET_MIN		0x08
#define MPT_FALLBACK		0x10
#define MPT_SAVE		0x20

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_doDv - Perform domain validation to a target.
 *	@hd: Pointer to MPT_SCSI_HOST structure.
 *	@portnum: IOC port number.
 *	@target: Physical ID of this target
 *
 *	Uses the ISR, but with special processing.
 *	MUST be single-threaded.
 *	Test will exit if target is at async & narrow.
 *
 *	Return: None.
 */
static int
mptscsih_doDv(MPT_SCSI_HOST *hd, int bus_number, int id)
{
	MPT_ADAPTER		*ioc = hd->ioc;
	VirtDevice		*pTarget;
	SCSIDevicePage1_t	*pcfg1Data;
	SCSIDevicePage0_t	*pcfg0Data;
	u8			*pbuf1;
	u8			*pbuf2;
	u8			*pDvBuf;
	dma_addr_t		 dvbuf_dma = -1;
	dma_addr_t		 buf1_dma = -1;
	dma_addr_t		 buf2_dma = -1;
	dma_addr_t		 cfg1_dma_addr = -1;
	dma_addr_t		 cfg0_dma_addr = -1;
	ConfigPageHeader_t	 header1;
	ConfigPageHeader_t	 header0;
	DVPARAMETERS		 dv;
	INTERNAL_CMD		 iocmd;
	CONFIGPARMS		 cfg;
	int			 dv_alloc = 0;
	int			 rc, sz = 0;
	int			 bufsize = 0;
	int			 dataBufSize = 0;
	int			 echoBufSize = 0;
	int			 notDone;
	int			 patt;
	int			 repeat;
	int			 retcode = 0;
	int			 nfactor =  MPT_ULTRA320;
	char			 firstPass = 1;
	char			 doFallback = 0;
	char			 readPage0;
	char			 bus, lun;
	char			 inq0 = 0;

	if (ioc->spi_data.sdp1length == 0)
		return 0;

	if (ioc->spi_data.sdp0length == 0)
		return 0;

	/* If multiple buses are used, require that the initiator
	 * id be the same on all buses.
	 */
	if (id == ioc->pfacts[0].PortSCSIID)
		return 0;

	lun = 0;
	bus = (u8) bus_number;
	ddvtprintk((MYIOC_s_NOTE_FMT
			"DV started: bus=%d, id=%d dv @ %p\n",
			ioc->name, bus, id, &dv));

	/* Prep DV structure
	 */
	memset (&dv, 0, sizeof(DVPARAMETERS));
	dv.id = id;

	/* Populate tmax with the current maximum
	 * transfer parameters for this target.
	 * Exit if narrow and async.
	 */
	dv.cmd = MPT_GET_NVRAM_VALS;
	mptscsih_dv_parms(hd, &dv, NULL);

	/* Prep SCSI IO structure
	 */
	iocmd.id = id;
	iocmd.bus = bus;
	iocmd.lun = lun;
	iocmd.flags = 0;
	iocmd.physDiskNum = -1;
	iocmd.rsvd = iocmd.rsvd2 = 0;

	pTarget = hd->Targets[id];

	/* Use tagged commands if possible.
	 */
	if (pTarget) {
		if (pTarget->tflags & MPT_TARGET_FLAGS_Q_YES)
			iocmd.flags |= MPT_ICFLAG_TAGGED_CMD;
		else {
			if (hd->ioc->facts.FWVersion.Word < 0x01000600)
				return 0;

			if ((hd->ioc->facts.FWVersion.Word >= 0x01010000) &&
				(hd->ioc->facts.FWVersion.Word < 0x01010B00))
				return 0;
		}
	}

	/* Prep cfg structure
	 */
	cfg.pageAddr = (bus<<8) | id;
	cfg.cfghdr.hdr = NULL;

	/* Prep SDP0 header
	 */
	header0.PageVersion = ioc->spi_data.sdp0version;
	header0.PageLength = ioc->spi_data.sdp0length;
	header0.PageNumber = 0;
	header0.PageType = MPI_CONFIG_PAGETYPE_SCSI_DEVICE;

	/* Prep SDP1 header
	 */
	header1.PageVersion = ioc->spi_data.sdp1version;
	header1.PageLength = ioc->spi_data.sdp1length;
	header1.PageNumber = 1;
	header1.PageType = MPI_CONFIG_PAGETYPE_SCSI_DEVICE;

	if (header0.PageLength & 1)
		dv_alloc = (header0.PageLength * 4) + 4;

	dv_alloc +=  (2048 + (header1.PageLength * 4));

	pDvBuf = pci_alloc_consistent(ioc->pcidev, dv_alloc, &dvbuf_dma);
	if (pDvBuf == NULL)
		return 0;

	sz = 0;
	pbuf1 = (u8 *)pDvBuf;
	buf1_dma = dvbuf_dma;
	sz +=1024;

	pbuf2 = (u8 *) (pDvBuf + sz);
	buf2_dma = dvbuf_dma + sz;
	sz +=1024;

	pcfg0Data = (SCSIDevicePage0_t *) (pDvBuf + sz);
	cfg0_dma_addr = dvbuf_dma + sz;
	sz += header0.PageLength * 4;

	/* 8-byte alignment
	 */
	if (header0.PageLength & 1)
		sz += 4;

	pcfg1Data = (SCSIDevicePage1_t *) (pDvBuf + sz);
	cfg1_dma_addr = dvbuf_dma + sz;

	/* Skip this ID? Set cfg.cfghdr.hdr to force config page write
	 */
	{
		SpiCfgData *pspi_data = &hd->ioc->spi_data;
		if (pspi_data->nvram && (pspi_data->nvram[id] != MPT_HOST_NVRAM_INVALID)) {
			/* Set the factor from nvram */
			nfactor = (pspi_data->nvram[id] & MPT_NVRAM_SYNC_MASK) >> 8;
			if (nfactor < pspi_data->minSyncFactor )
				nfactor = pspi_data->minSyncFactor;

			if (!(pspi_data->nvram[id] & MPT_NVRAM_ID_SCAN_ENABLE) ||
				(pspi_data->PortFlags == MPI_SCSIPORTPAGE2_PORT_FLAGS_OFF_DV) ) {

				ddvprintk((MYIOC_s_NOTE_FMT "DV Skipped: bus, id, lun (%d, %d, %d)\n",
					ioc->name, bus, id, lun));

				dv.cmd = MPT_SET_MAX;
				mptscsih_dv_parms(hd, &dv, (void *)pcfg1Data);
				cfg.cfghdr.hdr = &header1;

				/* Save the final negotiated settings to
				 * SCSI device page 1.
				 */
				cfg.physAddr = cfg1_dma_addr;
				cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
				cfg.dir = 1;
				mpt_config(hd->ioc, &cfg);
				goto target_done;
			}
		}
	}

	/* Finish iocmd inititialization - hidden or visible disk? */
	if (ioc->raid_data.pIocPg3) {
		/* Search IOC page 3 for matching id
		 */
		Ioc3PhysDisk_t *pPDisk =  ioc->raid_data.pIocPg3->PhysDisk;
		int		numPDisk = ioc->raid_data.pIocPg3->NumPhysDisks;

		while (numPDisk) {
			if (pPDisk->PhysDiskID == id) {
				/* match */
				iocmd.flags |= MPT_ICFLAG_PHYS_DISK;
				iocmd.physDiskNum = pPDisk->PhysDiskNum;

				/* Quiesce the IM
				 */
				if (mptscsih_do_raid(hd, MPI_RAID_ACTION_QUIESCE_PHYS_IO, &iocmd) < 0) {
					ddvprintk((MYIOC_s_ERR_FMT "RAID Queisce FAILED!\n", ioc->name));
					goto target_done;
				}
				break;
			}
			pPDisk++;
			numPDisk--;
		}
	}

	/* RAID Volume ID's may double for a physical device. If RAID but
	 * not a physical ID as well, skip DV.
	 */
	if ((hd->ioc->raid_data.isRaid & (1 << id)) && !(iocmd.flags & MPT_ICFLAG_PHYS_DISK))
		goto target_done;


	/* Basic Test.
	 * Async & Narrow - Inquiry
	 * Async & Narrow - Inquiry
	 * Maximum transfer rate - Inquiry
	 * Compare buffers:
	 *	If compare, test complete.
	 *	If miscompare and first pass, repeat
	 *	If miscompare and not first pass, fall back and repeat
	 */
	hd->pLocal = NULL;
	readPage0 = 0;
	sz = SCSI_MAX_INQUIRY_BYTES;
	rc = MPT_SCANDV_GOOD;
	while (1) {
		ddvprintk((MYIOC_s_NOTE_FMT "DV: Start Basic test on id=%d\n", ioc->name, id));
		retcode = 0;
		dv.cmd = MPT_SET_MIN;
		mptscsih_dv_parms(hd, &dv, (void *)pcfg1Data);

		cfg.cfghdr.hdr = &header1;
		cfg.physAddr = cfg1_dma_addr;
		cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
		cfg.dir = 1;
		if (mpt_config(hd->ioc, &cfg) != 0)
			goto target_done;

		/* Wide - narrow - wide workaround case
		 */
		if ((rc == MPT_SCANDV_ISSUE_SENSE) && dv.max.width) {
			/* Send an untagged command to reset disk Qs corrupted
			 * when a parity error occurs on a Request Sense.
			 */
			if ((hd->ioc->facts.FWVersion.Word >= 0x01000600) ||
				((hd->ioc->facts.FWVersion.Word >= 0x01010000) &&
				(hd->ioc->facts.FWVersion.Word < 0x01010B00)) ) {

				iocmd.cmd = REQUEST_SENSE;
				iocmd.data_dma = buf1_dma;
				iocmd.data = pbuf1;
				iocmd.size = 0x12;
				if (mptscsih_do_cmd(hd, &iocmd) < 0)
					goto target_done;
				else {
					if (hd->pLocal == NULL)
						goto target_done;
					rc = hd->pLocal->completion;
					if ((rc == MPT_SCANDV_GOOD) || (rc == MPT_SCANDV_SENSE)) {
						dv.max.width = 0;
						doFallback = 0;
					} else
						goto target_done;
				}
			} else
				goto target_done;
		}

		iocmd.cmd = INQUIRY;
		iocmd.data_dma = buf1_dma;
		iocmd.data = pbuf1;
		iocmd.size = sz;
		memset(pbuf1, 0x00, sz);
		if (mptscsih_do_cmd(hd, &iocmd) < 0)
			goto target_done;
		else {
			if (hd->pLocal == NULL)
				goto target_done;
			rc = hd->pLocal->completion;
			if (rc == MPT_SCANDV_GOOD) {
				if (hd->pLocal->scsiStatus == SAM_STAT_BUSY) {
					if ((iocmd.flags & MPT_ICFLAG_TAGGED_CMD) == 0)
						retcode = 1;
					else
						retcode = 0;

					goto target_done;
				}
			} else if  (rc == MPT_SCANDV_SENSE) {
				;
			} else {
				/* If first command doesn't complete
				 * with a good status or with a check condition,
				 * exit.
				 */
				goto target_done;
			}
		}

		/* Reset the size for disks
		 */
		inq0 = (*pbuf1) & 0x1F;
		if ((inq0 == 0) && pTarget && !pTarget->raidVolume) {
			sz = 0x40;
			iocmd.size = sz;
		}

		/* Another GEM workaround. Check peripheral device type,
		 * if PROCESSOR, quit DV.
		 */
		if (inq0 == TYPE_PROCESSOR) {
			mptscsih_initTarget(hd,
				bus,
				id,
				lun,
				pbuf1,
				sz);
			goto target_done;
		}

		if (inq0 > 0x08)
			goto target_done;

		if (mptscsih_do_cmd(hd, &iocmd) < 0)
			goto target_done;

		if (sz == 0x40) {
			if ((pTarget->maxWidth == 1) && (pTarget->maxOffset) && (nfactor < 0x0A)
				&& (pTarget->minSyncFactor > 0x09)) {
				if ((pbuf1[56] & 0x04) == 0)
					;
				else if ((pbuf1[56] & 0x01) == 1) {
					pTarget->minSyncFactor =
					    nfactor > MPT_ULTRA320 ? nfactor : MPT_ULTRA320;
				} else {
					pTarget->minSyncFactor =
					    nfactor > MPT_ULTRA160 ? nfactor : MPT_ULTRA160;
				}

				dv.max.factor = pTarget->minSyncFactor;

				if ((pbuf1[56] & 0x02) == 0) {
					pTarget->negoFlags |= MPT_TARGET_NO_NEGO_QAS;
					hd->ioc->spi_data.noQas = MPT_TARGET_NO_NEGO_QAS;
					ddvprintk((MYIOC_s_NOTE_FMT
					    "DV: Start Basic noQas on id=%d due to pbuf1[56]=%x\n",
					    ioc->name, id, pbuf1[56]));
				}
			}
		}

		if (doFallback)
			dv.cmd = MPT_FALLBACK;
		else
			dv.cmd = MPT_SET_MAX;

		mptscsih_dv_parms(hd, &dv, (void *)pcfg1Data);
		if (mpt_config(hd->ioc, &cfg) != 0)
			goto target_done;

		if ((!dv.now.width) && (!dv.now.offset))
			goto target_done;

		iocmd.cmd = INQUIRY;
		iocmd.data_dma = buf2_dma;
		iocmd.data = pbuf2;
		iocmd.size = sz;
		memset(pbuf2, 0x00, sz);
		if (mptscsih_do_cmd(hd, &iocmd) < 0)
			goto target_done;
		else if (hd->pLocal == NULL)
			goto target_done;
		else {
			/* Save the return code.
			 * If this is the first pass,
			 * read SCSI Device Page 0
			 * and update the target max parameters.
			 */
			rc = hd->pLocal->completion;
			doFallback = 0;
			if (rc == MPT_SCANDV_GOOD) {
				if (!readPage0) {
					u32 sdp0_info;
					u32 sdp0_nego;

					cfg.cfghdr.hdr = &header0;
					cfg.physAddr = cfg0_dma_addr;
					cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
					cfg.dir = 0;

					if (mpt_config(hd->ioc, &cfg) != 0)
						goto target_done;

					sdp0_info = le32_to_cpu(pcfg0Data->Information) & 0x0E;
					sdp0_nego = (le32_to_cpu(pcfg0Data->NegotiatedParameters) & 0xFF00 ) >> 8;

					/* Quantum and Fujitsu workarounds.
					 * Quantum: PPR U320 -> PPR reply with Ultra2 and wide
					 * Fujitsu: PPR U320 -> Msg Reject and Ultra2 and wide
					 * Resetart with a request for U160.
					 */
					if ((dv.now.factor == MPT_ULTRA320) && (sdp0_nego == MPT_ULTRA2)) {
							doFallback = 1;
					} else {
						dv.cmd = MPT_UPDATE_MAX;
						mptscsih_dv_parms(hd, &dv, (void *)pcfg0Data);
						/* Update the SCSI device page 1 area
						 */
						pcfg1Data->RequestedParameters = pcfg0Data->NegotiatedParameters;
						readPage0 = 1;
					}
				}

				/* Quantum workaround. Restart this test will the fallback
				 * flag set.
				 */
				if (doFallback == 0) {
					if (memcmp(pbuf1, pbuf2, sz) != 0) {
						if (!firstPass)
							doFallback = 1;
					} else {
						ddvprintk((MYIOC_s_NOTE_FMT
						    "DV:Inquiry compared id=%d, calling initTarget\n", ioc->name, id));
						hd->ioc->spi_data.dvStatus[id] &= ~MPT_SCSICFG_DV_NOT_DONE;
						mptscsih_initTarget(hd,
							bus,
							id,
							lun,
							pbuf1,
							sz);
						break;	/* test complete */
					}
				}


			} else if (rc == MPT_SCANDV_ISSUE_SENSE)
				doFallback = 1;	/* set fallback flag */
			else if ((rc == MPT_SCANDV_DID_RESET) ||
				 (rc == MPT_SCANDV_SENSE) ||
				 (rc == MPT_SCANDV_FALLBACK))
				doFallback = 1;	/* set fallback flag */
			else
				goto target_done;

			firstPass = 0;
		}
	}
	ddvprintk((MYIOC_s_NOTE_FMT "DV: Basic test on id=%d completed OK.\n", ioc->name, id));

	if (ioc->spi_data.mpt_dv == 0)
		goto target_done;

	inq0 = (*pbuf1) & 0x1F;

	/* Continue only for disks
	 */
	if (inq0 != 0)
		goto target_done;

	if ( ioc->spi_data.PortFlags == MPI_SCSIPORTPAGE2_PORT_FLAGS_BASIC_DV_ONLY )
		goto target_done;

	/* Start the Enhanced Test.
	 * 0) issue TUR to clear out check conditions
	 * 1) read capacity of echo (regular) buffer
	 * 2) reserve device
	 * 3) do write-read-compare data pattern test
	 * 4) release
	 * 5) update nego parms to target struct
	 */
	cfg.cfghdr.hdr = &header1;
	cfg.physAddr = cfg1_dma_addr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	cfg.dir = 1;

	iocmd.cmd = TEST_UNIT_READY;
	iocmd.data_dma = -1;
	iocmd.data = NULL;
	iocmd.size = 0;
	notDone = 1;
	while (notDone) {
		if (mptscsih_do_cmd(hd, &iocmd) < 0)
			goto target_done;

		if (hd->pLocal == NULL)
			goto target_done;

		rc = hd->pLocal->completion;
		if (rc == MPT_SCANDV_GOOD)
			notDone = 0;
		else if (rc == MPT_SCANDV_SENSE) {
			u8 skey = hd->pLocal->sense[2] & 0x0F;
			u8 asc = hd->pLocal->sense[12];
			u8 ascq = hd->pLocal->sense[13];
			ddvprintk((MYIOC_s_INFO_FMT
				"SenseKey:ASC:ASCQ = (%x:%02x:%02x)\n",
				ioc->name, skey, asc, ascq));

			if (skey == UNIT_ATTENTION)
				notDone++; /* repeat */
			else if ((skey == NOT_READY) &&
					(asc == 0x04)&&(ascq == 0x01)) {
				/* wait then repeat */
				mdelay (2000);
				notDone++;
			} else if ((skey == NOT_READY) && (asc == 0x3A)) {
				/* no medium, try read test anyway */
				notDone = 0;
			} else {
				/* All other errors are fatal.
				 */
				ddvprintk((MYIOC_s_INFO_FMT "DV: fatal error.",
						ioc->name));
				goto target_done;
			}
		} else
			goto target_done;
	}

	iocmd.cmd = READ_BUFFER;
	iocmd.data_dma = buf1_dma;
	iocmd.data = pbuf1;
	iocmd.size = 4;
	iocmd.flags |= MPT_ICFLAG_BUF_CAP;

	dataBufSize = 0;
	echoBufSize = 0;
	for (patt = 0; patt < 2; patt++) {
		if (patt == 0)
			iocmd.flags |= MPT_ICFLAG_ECHO;
		else
			iocmd.flags &= ~MPT_ICFLAG_ECHO;

		notDone = 1;
		while (notDone) {
			bufsize = 0;

			/* If not ready after 8 trials,
			 * give up on this device.
			 */
			if (notDone > 8)
				goto target_done;

			if (mptscsih_do_cmd(hd, &iocmd) < 0)
				goto target_done;
			else if (hd->pLocal == NULL)
				goto target_done;
			else {
				rc = hd->pLocal->completion;
				ddvprintk(("ReadBuffer Comp Code %d", rc));
				ddvprintk(("  buff: %0x %0x %0x %0x\n",
					pbuf1[0], pbuf1[1], pbuf1[2], pbuf1[3]));

				if (rc == MPT_SCANDV_GOOD) {
					notDone = 0;
					if (iocmd.flags & MPT_ICFLAG_ECHO) {
						bufsize =  ((pbuf1[2] & 0x1F) <<8) | pbuf1[3];
						if (pbuf1[0] & 0x01)
							iocmd.flags |= MPT_ICFLAG_EBOS;
					} else {
						bufsize =  pbuf1[1]<<16 | pbuf1[2]<<8 | pbuf1[3];
					}
				} else if (rc == MPT_SCANDV_SENSE) {
					u8 skey = hd->pLocal->sense[2] & 0x0F;
					u8 asc = hd->pLocal->sense[12];
					u8 ascq = hd->pLocal->sense[13];
					ddvprintk((MYIOC_s_INFO_FMT
						"SenseKey:ASC:ASCQ = (%x:%02x:%02x)\n",
						ioc->name, skey, asc, ascq));
					if (skey == ILLEGAL_REQUEST) {
						notDone = 0;
					} else if (skey == UNIT_ATTENTION) {
						notDone++; /* repeat */
					} else if ((skey == NOT_READY) &&
						(asc == 0x04)&&(ascq == 0x01)) {
						/* wait then repeat */
						mdelay (2000);
						notDone++;
					} else {
						/* All other errors are fatal.
						 */
						ddvprintk((MYIOC_s_INFO_FMT "DV: fatal error.",
							ioc->name));
						goto target_done;
					}
				} else {
					/* All other errors are fatal
					 */
					goto target_done;
				}
			}
		}

		if (iocmd.flags & MPT_ICFLAG_ECHO)
			echoBufSize = bufsize;
		else
			dataBufSize = bufsize;
	}
	sz = 0;
	iocmd.flags &= ~MPT_ICFLAG_BUF_CAP;

	/* Use echo buffers if possible,
	 * Exit if both buffers are 0.
	 */
	if (echoBufSize > 0) {
		iocmd.flags |= MPT_ICFLAG_ECHO;
		if (dataBufSize > 0)
			bufsize = min(echoBufSize, dataBufSize);
		else
			bufsize = echoBufSize;
	} else if (dataBufSize == 0)
		goto target_done;

	ddvprintk((MYIOC_s_INFO_FMT "%s Buffer Capacity %d\n", ioc->name,
		(iocmd.flags & MPT_ICFLAG_ECHO) ? "Echo" : " ", bufsize));

	/* Data buffers for write-read-compare test max 1K.
	 */
	sz = min(bufsize, 1024);

	/* --- loop ----
	 * On first pass, always issue a reserve.
	 * On additional loops, only if a reset has occurred.
	 * iocmd.flags indicates if echo or regular buffer
	 */
	for (patt = 0; patt < 4; patt++) {
		ddvprintk(("Pattern %d\n", patt));
		if ((iocmd.flags & MPT_ICFLAG_RESERVED) && (iocmd.flags & MPT_ICFLAG_DID_RESET)) {
			iocmd.cmd = TEST_UNIT_READY;
			iocmd.data_dma = -1;
			iocmd.data = NULL;
			iocmd.size = 0;
			if (mptscsih_do_cmd(hd, &iocmd) < 0)
				goto target_done;

			iocmd.cmd = RELEASE;
			iocmd.data_dma = -1;
			iocmd.data = NULL;
			iocmd.size = 0;
			if (mptscsih_do_cmd(hd, &iocmd) < 0)
				goto target_done;
			else if (hd->pLocal == NULL)
				goto target_done;
			else {
				rc = hd->pLocal->completion;
				ddvprintk(("Release rc %d\n", rc));
				if (rc == MPT_SCANDV_GOOD)
					iocmd.flags &= ~MPT_ICFLAG_RESERVED;
				else
					goto target_done;
			}
			iocmd.flags &= ~MPT_ICFLAG_RESERVED;
		}
		iocmd.flags &= ~MPT_ICFLAG_DID_RESET;

		if (iocmd.flags & MPT_ICFLAG_EBOS)
			goto skip_Reserve;

		repeat = 5;
		while (repeat && (!(iocmd.flags & MPT_ICFLAG_RESERVED))) {
			iocmd.cmd = RESERVE;
			iocmd.data_dma = -1;
			iocmd.data = NULL;
			iocmd.size = 0;
			if (mptscsih_do_cmd(hd, &iocmd) < 0)
				goto target_done;
			else if (hd->pLocal == NULL)
				goto target_done;
			else {
				rc = hd->pLocal->completion;
				if (rc == MPT_SCANDV_GOOD) {
					iocmd.flags |= MPT_ICFLAG_RESERVED;
				} else if (rc == MPT_SCANDV_SENSE) {
					/* Wait if coming ready
					 */
					u8 skey = hd->pLocal->sense[2] & 0x0F;
					u8 asc = hd->pLocal->sense[12];
					u8 ascq = hd->pLocal->sense[13];
					ddvprintk((MYIOC_s_INFO_FMT
						"DV: Reserve Failed: ", ioc->name));
					ddvprintk(("SenseKey:ASC:ASCQ = (%x:%02x:%02x)\n",
							skey, asc, ascq));

					if ((skey == NOT_READY) && (asc == 0x04)&&
									(ascq == 0x01)) {
						/* wait then repeat */
						mdelay (2000);
						notDone++;
					} else {
						ddvprintk((MYIOC_s_INFO_FMT
							"DV: Reserved Failed.", ioc->name));
						goto target_done;
					}
				} else {
					ddvprintk((MYIOC_s_INFO_FMT "DV: Reserved Failed.",
							 ioc->name));
					goto target_done;
				}
			}
		}

skip_Reserve:
		mptscsih_fillbuf(pbuf1, sz, patt, 1);
		iocmd.cmd = WRITE_BUFFER;
		iocmd.data_dma = buf1_dma;
		iocmd.data = pbuf1;
		iocmd.size = sz;
		if (mptscsih_do_cmd(hd, &iocmd) < 0)
			goto target_done;
		else if (hd->pLocal == NULL)
			goto target_done;
		else {
			rc = hd->pLocal->completion;
			if (rc == MPT_SCANDV_GOOD)
				;		/* Issue read buffer */
			else if (rc == MPT_SCANDV_DID_RESET) {
				/* If using echo buffers, reset to data buffers.
				 * Else do Fallback and restart
				 * this test (re-issue reserve
				 * because of bus reset).
				 */
				if ((iocmd.flags & MPT_ICFLAG_ECHO) && (dataBufSize >= bufsize)) {
					iocmd.flags &= ~MPT_ICFLAG_ECHO;
				} else {
					dv.cmd = MPT_FALLBACK;
					mptscsih_dv_parms(hd, &dv, (void *)pcfg1Data);

					if (mpt_config(hd->ioc, &cfg) != 0)
						goto target_done;

					if ((!dv.now.width) && (!dv.now.offset))
						goto target_done;
				}

				iocmd.flags |= MPT_ICFLAG_DID_RESET;
				patt = -1;
				continue;
			} else if (rc == MPT_SCANDV_SENSE) {
				/* Restart data test if UA, else quit.
				 */
				u8 skey = hd->pLocal->sense[2] & 0x0F;
				ddvprintk((MYIOC_s_INFO_FMT
					"SenseKey:ASC:ASCQ = (%x:%02x:%02x)\n", ioc->name, skey,
					hd->pLocal->sense[12], hd->pLocal->sense[13]));
				if (skey == UNIT_ATTENTION) {
					patt = -1;
					continue;
				} else if (skey == ILLEGAL_REQUEST) {
					if (iocmd.flags & MPT_ICFLAG_ECHO) {
						if (dataBufSize >= bufsize) {
							iocmd.flags &= ~MPT_ICFLAG_ECHO;
							patt = -1;
							continue;
						}
					}
					goto target_done;
				}
				else
					goto target_done;
			} else {
				/* fatal error */
				goto target_done;
			}
		}

		iocmd.cmd = READ_BUFFER;
		iocmd.data_dma = buf2_dma;
		iocmd.data = pbuf2;
		iocmd.size = sz;
		if (mptscsih_do_cmd(hd, &iocmd) < 0)
			goto target_done;
		else if (hd->pLocal == NULL)
			goto target_done;
		else {
			rc = hd->pLocal->completion;
			if (rc == MPT_SCANDV_GOOD) {
				 /* If buffers compare,
				  * go to next pattern,
				  * else, do a fallback and restart
				  * data transfer test.
				  */
				if (memcmp (pbuf1, pbuf2, sz) == 0) {
					; /* goto next pattern */
				} else {
					/* Miscompare with Echo buffer, go to data buffer,
					 * if that buffer exists.
					 * Miscompare with Data buffer, check first 4 bytes,
					 * some devices return capacity. Exit in this case.
					 */
					if (iocmd.flags & MPT_ICFLAG_ECHO) {
						if (dataBufSize >= bufsize)
							iocmd.flags &= ~MPT_ICFLAG_ECHO;
						else
							goto target_done;
					} else {
						if (dataBufSize == (pbuf2[1]<<16 | pbuf2[2]<<8 | pbuf2[3])) {
							/* Argh. Device returning wrong data.
							 * Quit DV for this device.
							 */
							goto target_done;
						}

						/* Had an actual miscompare. Slow down.*/
						dv.cmd = MPT_FALLBACK;
						mptscsih_dv_parms(hd, &dv, (void *)pcfg1Data);

						if (mpt_config(hd->ioc, &cfg) != 0)
							goto target_done;

						if ((!dv.now.width) && (!dv.now.offset))
							goto target_done;
					}

					patt = -1;
					continue;
				}
			} else if (rc == MPT_SCANDV_DID_RESET) {
				/* Do Fallback and restart
				 * this test (re-issue reserve
				 * because of bus reset).
				 */
				dv.cmd = MPT_FALLBACK;
				mptscsih_dv_parms(hd, &dv, (void *)pcfg1Data);

				if (mpt_config(hd->ioc, &cfg) != 0)
					 goto target_done;

				if ((!dv.now.width) && (!dv.now.offset))
					goto target_done;

				iocmd.flags |= MPT_ICFLAG_DID_RESET;
				patt = -1;
				continue;
			} else if (rc == MPT_SCANDV_SENSE) {
				/* Restart data test if UA, else quit.
				 */
				u8 skey = hd->pLocal->sense[2] & 0x0F;
				ddvprintk((MYIOC_s_INFO_FMT
					"SenseKey:ASC:ASCQ = (%x:%02x:%02x)\n", ioc->name, skey,
					hd->pLocal->sense[12], hd->pLocal->sense[13]));
				if (skey == UNIT_ATTENTION) {
					patt = -1;
					continue;
				}
				else
					goto target_done;
			} else {
				/* fatal error */
				goto target_done;
			}
		}

	} /* --- end of patt loop ---- */

target_done:
	if (iocmd.flags & MPT_ICFLAG_RESERVED) {
		iocmd.cmd = RELEASE;
		iocmd.data_dma = -1;
		iocmd.data = NULL;
		iocmd.size = 0;
		if (mptscsih_do_cmd(hd, &iocmd) < 0)
			printk(MYIOC_s_INFO_FMT "DV: Release failed. id %d",
					ioc->name, id);
		else if (hd->pLocal) {
			if (hd->pLocal->completion == MPT_SCANDV_GOOD)
				iocmd.flags &= ~MPT_ICFLAG_RESERVED;
		} else {
			printk(MYIOC_s_INFO_FMT "DV: Release failed. id %d",
						ioc->name, id);
		}
	}


	/* Set if cfg1_dma_addr contents is valid
	 */
	if ((cfg.cfghdr.hdr != NULL) && (retcode == 0)){
		/* If disk, not U320, disable QAS
		 */
		if ((inq0 == 0) && (dv.now.factor > MPT_ULTRA320)) {
			hd->ioc->spi_data.noQas = MPT_TARGET_NO_NEGO_QAS;
			ddvprintk((MYIOC_s_NOTE_FMT
			    "noQas set due to id=%d has factor=%x\n", ioc->name, id, dv.now.factor));
		}

		dv.cmd = MPT_SAVE;
		mptscsih_dv_parms(hd, &dv, (void *)pcfg1Data);

		/* Double writes to SDP1 can cause problems,
		 * skip save of the final negotiated settings to
		 * SCSI device page 1.
		 *
		cfg.cfghdr.hdr = &header1;
		cfg.physAddr = cfg1_dma_addr;
		cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
		cfg.dir = 1;
		mpt_config(hd->ioc, &cfg);
		 */
	}

	/* If this is a RAID Passthrough, enable internal IOs
	 */
	if (iocmd.flags & MPT_ICFLAG_PHYS_DISK) {
		if (mptscsih_do_raid(hd, MPI_RAID_ACTION_ENABLE_PHYS_IO, &iocmd) < 0)
			ddvprintk((MYIOC_s_ERR_FMT "RAID Enable FAILED!\n", ioc->name));
	}

	/* Done with the DV scan of the current target
	 */
	if (pDvBuf)
		pci_free_consistent(ioc->pcidev, dv_alloc, pDvBuf, dvbuf_dma);

	ddvtprintk((MYIOC_s_INFO_FMT "DV Done id=%d\n",
			ioc->name, id));

	return retcode;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*	mptscsih_dv_parms - perform a variety of operations on the
 *	parameters used for negotiation.
 *	@hd: Pointer to a SCSI host.
 *	@dv: Pointer to a structure that contains the maximum and current
 *		negotiated parameters.
 */
static void
mptscsih_dv_parms(MPT_SCSI_HOST *hd, DVPARAMETERS *dv,void *pPage)
{
	VirtDevice		*pTarget;
	SCSIDevicePage0_t	*pPage0;
	SCSIDevicePage1_t	*pPage1;
	int			val = 0, data, configuration;
	u8			width = 0;
	u8			offset = 0;
	u8			factor = 0;
	u8			negoFlags = 0;
	u8			cmd = dv->cmd;
	u8			id = dv->id;

	switch (cmd) {
	case MPT_GET_NVRAM_VALS:
		ddvprintk((MYIOC_s_NOTE_FMT "Getting NVRAM: ",
							 hd->ioc->name));
		/* Get the NVRAM values and save in tmax
		 * If not an LVD bus, the adapter minSyncFactor has been
		 * already throttled back.
		 */
		negoFlags = hd->ioc->spi_data.noQas;
		if ((hd->Targets)&&((pTarget = hd->Targets[(int)id]) != NULL) && !pTarget->raidVolume) {
			width = pTarget->maxWidth;
			offset = pTarget->maxOffset;
			factor = pTarget->minSyncFactor;
			negoFlags |= pTarget->negoFlags;
		} else {
			if (hd->ioc->spi_data.nvram && (hd->ioc->spi_data.nvram[id] != MPT_HOST_NVRAM_INVALID)) {
				data = hd->ioc->spi_data.nvram[id];
				width = data & MPT_NVRAM_WIDE_DISABLE ? 0 : 1;
				if ((offset = hd->ioc->spi_data.maxSyncOffset) == 0)
					factor = MPT_ASYNC;
				else {
					factor = (data & MPT_NVRAM_SYNC_MASK) >> MPT_NVRAM_SYNC_SHIFT;
					if ((factor == 0) || (factor == MPT_ASYNC)){
						factor = MPT_ASYNC;
						offset = 0;
					}
				}
			} else {
				width = MPT_NARROW;
				offset = 0;
				factor = MPT_ASYNC;
			}

			/* Set the negotiation flags */
			if (!width)
				negoFlags |= MPT_TARGET_NO_NEGO_WIDE;

			if (!offset)
				negoFlags |= MPT_TARGET_NO_NEGO_SYNC;
		}

		/* limit by adapter capabilities */
		width = min(width, hd->ioc->spi_data.maxBusWidth);
		offset = min(offset, hd->ioc->spi_data.maxSyncOffset);
		factor = max(factor, hd->ioc->spi_data.minSyncFactor);

		/* Check Consistency */
		if (offset && (factor < MPT_ULTRA2) && !width)
			factor = MPT_ULTRA2;

		dv->max.width = width;
		dv->max.offset = offset;
		dv->max.factor = factor;
		dv->max.flags = negoFlags;
		ddvprintk((" id=%d width=%d factor=%x offset=%x flags=%x\n",
				id, width, factor, offset, negoFlags));
		break;

	case MPT_UPDATE_MAX:
		ddvprintk((MYIOC_s_NOTE_FMT
			"Updating with SDP0 Data: ", hd->ioc->name));
		/* Update tmax values with those from Device Page 0.*/
		pPage0 = (SCSIDevicePage0_t *) pPage;
		if (pPage0) {
			val = le32_to_cpu(pPage0->NegotiatedParameters);
			dv->max.width = val & MPI_SCSIDEVPAGE0_NP_WIDE ? 1 : 0;
			dv->max.offset = (val&MPI_SCSIDEVPAGE0_NP_NEG_SYNC_OFFSET_MASK) >> 16;
			dv->max.factor = (val&MPI_SCSIDEVPAGE0_NP_NEG_SYNC_PERIOD_MASK) >> 8;
		}

		dv->now.width = dv->max.width;
		dv->now.offset = dv->max.offset;
		dv->now.factor = dv->max.factor;
		ddvprintk(("id=%d width=%d factor=%x offset=%x flags=%x\n",
				id, dv->now.width, dv->now.factor, dv->now.offset, dv->now.flags));
		break;

	case MPT_SET_MAX:
		ddvprintk((MYIOC_s_NOTE_FMT "Setting Max: ",
								hd->ioc->name));
		/* Set current to the max values. Update the config page.*/
		dv->now.width = dv->max.width;
		dv->now.offset = dv->max.offset;
		dv->now.factor = dv->max.factor;
		dv->now.flags = dv->max.flags;

		pPage1 = (SCSIDevicePage1_t *)pPage;
		if (pPage1) {
			mptscsih_setDevicePage1Flags (dv->now.width, dv->now.factor,
				dv->now.offset, &val, &configuration, dv->now.flags);
			dnegoprintk(("Setting Max: id=%d width=%d factor=%x offset=%x negoFlags=%x request=%x config=%x\n",
				id, dv->now.width, dv->now.factor, dv->now.offset, dv->now.flags, val, configuration));
			pPage1->RequestedParameters = cpu_to_le32(val);
			pPage1->Reserved = 0;
			pPage1->Configuration = cpu_to_le32(configuration);
		}

		ddvprintk(("id=%d width=%d factor=%x offset=%x negoFlags=%x request=%x configuration=%x\n",
				id, dv->now.width, dv->now.factor, dv->now.offset, dv->now.flags, val, configuration));
		break;

	case MPT_SET_MIN:
		ddvprintk((MYIOC_s_NOTE_FMT "Setting Min: ",
								hd->ioc->name));
		/* Set page to asynchronous and narrow
		 * Do not update now, breaks fallback routine. */
		width = MPT_NARROW;
		offset = 0;
		factor = MPT_ASYNC;
		negoFlags = dv->max.flags;

		pPage1 = (SCSIDevicePage1_t *)pPage;
		if (pPage1) {
			mptscsih_setDevicePage1Flags (width, factor,
				offset, &val, &configuration, negoFlags);
			dnegoprintk(("Setting Min: id=%d width=%d factor=%x offset=%x negoFlags=%x request=%x config=%x\n",
				id, width, factor, offset, negoFlags, val, configuration));
			pPage1->RequestedParameters = cpu_to_le32(val);
			pPage1->Reserved = 0;
			pPage1->Configuration = cpu_to_le32(configuration);
		}
		ddvprintk(("id=%d width=%d factor=%x offset=%x request=%x config=%x negoFlags=%x\n",
				id, width, factor, offset, val, configuration, negoFlags));
		break;

	case MPT_FALLBACK:
		ddvprintk((MYIOC_s_NOTE_FMT
			"Fallback: Start: offset %d, factor %x, width %d \n",
				hd->ioc->name, dv->now.offset,
				dv->now.factor, dv->now.width));
		width = dv->now.width;
		offset = dv->now.offset;
		factor = dv->now.factor;
		if ((offset) && (dv->max.width)) {
			if (factor < MPT_ULTRA160)
				factor = MPT_ULTRA160;
			else if (factor < MPT_ULTRA2) {
				factor = MPT_ULTRA2;
				width = MPT_WIDE;
			} else if ((factor == MPT_ULTRA2) && width) {
				factor = MPT_ULTRA2;
				width = MPT_NARROW;
			} else if (factor < MPT_ULTRA) {
				factor = MPT_ULTRA;
				width = MPT_WIDE;
			} else if ((factor == MPT_ULTRA) && width) {
				width = MPT_NARROW;
			} else if (factor < MPT_FAST) {
				factor = MPT_FAST;
				width = MPT_WIDE;
			} else if ((factor == MPT_FAST) && width) {
				factor = MPT_FAST;
				width = MPT_NARROW;
			} else if (factor < MPT_SCSI) {
				factor = MPT_SCSI;
				width = MPT_WIDE;
			} else if ((factor == MPT_SCSI) && width) {
				factor = MPT_SCSI;
				width = MPT_NARROW;
			} else {
				factor = MPT_ASYNC;
				offset = 0;
			}

		} else if (offset) {
			width = MPT_NARROW;
			if (factor < MPT_ULTRA)
				factor = MPT_ULTRA;
			else if (factor < MPT_FAST)
				factor = MPT_FAST;
			else if (factor < MPT_SCSI)
				factor = MPT_SCSI;
			else {
				factor = MPT_ASYNC;
				offset = 0;
			}

		} else {
			width = MPT_NARROW;
			factor = MPT_ASYNC;
		}
		dv->max.flags |= MPT_TARGET_NO_NEGO_QAS;
		dv->max.flags &= ~MPT_TAPE_NEGO_IDP;

		dv->now.width = width;
		dv->now.offset = offset;
		dv->now.factor = factor;
		dv->now.flags = dv->max.flags;

		pPage1 = (SCSIDevicePage1_t *)pPage;
		if (pPage1) {
			mptscsih_setDevicePage1Flags (width, factor, offset, &val,
						&configuration, dv->now.flags);
			dnegoprintk(("Finish: id=%d width=%d offset=%d factor=%x negoFlags=%x request=%x config=%x\n",
			     id, width, offset, factor, dv->now.flags, val, configuration));

			pPage1->RequestedParameters = cpu_to_le32(val);
			pPage1->Reserved = 0;
			pPage1->Configuration = cpu_to_le32(configuration);
		}

		ddvprintk(("Finish: id=%d offset=%d factor=%x width=%d request=%x config=%x\n",
			     id, dv->now.offset, dv->now.factor, dv->now.width, val, configuration));
		break;

	case MPT_SAVE:
		ddvprintk((MYIOC_s_NOTE_FMT
			"Saving to Target structure: ", hd->ioc->name));
		ddvprintk(("id=%d width=%x factor=%x offset=%d flags=%x\n",
			     id, dv->now.width, dv->now.factor, dv->now.offset, dv->now.flags));

		/* Save these values to target structures
		 * or overwrite nvram (phys disks only).
		 */

		if ((hd->Targets)&&((pTarget = hd->Targets[(int)id]) != NULL) && !pTarget->raidVolume ) {
			pTarget->maxWidth = dv->now.width;
			pTarget->maxOffset = dv->now.offset;
			pTarget->minSyncFactor = dv->now.factor;
			pTarget->negoFlags = dv->now.flags;
		} else {
			/* Preserv all flags, use
			 * read-modify-write algorithm
			 */
			if (hd->ioc->spi_data.nvram) {
				data = hd->ioc->spi_data.nvram[id];

				if (dv->now.width)
					data &= ~MPT_NVRAM_WIDE_DISABLE;
				else
					data |= MPT_NVRAM_WIDE_DISABLE;

				if (!dv->now.offset)
					factor = MPT_ASYNC;

				data &= ~MPT_NVRAM_SYNC_MASK;
				data |= (dv->now.factor << MPT_NVRAM_SYNC_SHIFT) & MPT_NVRAM_SYNC_MASK;

				hd->ioc->spi_data.nvram[id] = data;
			}
		}
		break;
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*	mptscsih_fillbuf - fill a buffer with a special data pattern
 *		cleanup. For bus scan only.
 *
 *	@buffer: Pointer to data buffer to be filled.
 *	@size: Number of bytes to fill
 *	@index: Pattern index
 *	@width: bus width, 0 (8 bits) or 1 (16 bits)
 */
static void
mptscsih_fillbuf(char *buffer, int size, int index, int width)
{
	char *ptr = buffer;
	int ii;
	char byte;
	short val;

	switch (index) {
	case 0:

		if (width) {
			/* Pattern:  0000 FFFF 0000 FFFF
			 */
			for (ii=0; ii < size; ii++, ptr++) {
				if (ii & 0x02)
					*ptr = 0xFF;
				else
					*ptr = 0x00;
			}
		} else {
			/* Pattern:  00 FF 00 FF
			 */
			for (ii=0; ii < size; ii++, ptr++) {
				if (ii & 0x01)
					*ptr = 0xFF;
				else
					*ptr = 0x00;
			}
		}
		break;

	case 1:
		if (width) {
			/* Pattern:  5555 AAAA 5555 AAAA 5555
			 */
			for (ii=0; ii < size; ii++, ptr++) {
				if (ii & 0x02)
					*ptr = 0xAA;
				else
					*ptr = 0x55;
			}
		} else {
			/* Pattern:  55 AA 55 AA 55
			 */
			for (ii=0; ii < size; ii++, ptr++) {
				if (ii & 0x01)
					*ptr = 0xAA;
				else
					*ptr = 0x55;
			}
		}
		break;

	case 2:
		/* Pattern:  00 01 02 03 04 05
		 * ... FE FF 00 01..
		 */
		for (ii=0; ii < size; ii++, ptr++)
			*ptr = (char) ii;
		break;

	case 3:
		if (width) {
			/* Wide Pattern:  FFFE 0001 FFFD 0002
			 * ...  4000 DFFF 8000 EFFF
			 */
			byte = 0;
			for (ii=0; ii < size/2; ii++) {
				/* Create the base pattern
				 */
				val = (1 << byte);
				/* every 64 (0x40) bytes flip the pattern
				 * since we fill 2 bytes / iteration,
				 * test for ii = 0x20
				 */
				if (ii & 0x20)
					val = ~(val);

				if (ii & 0x01) {
					*ptr = (char)( (val & 0xFF00) >> 8);
					ptr++;
					*ptr = (char)(val & 0xFF);
					byte++;
					byte &= 0x0F;
				} else {
					val = ~val;
					*ptr = (char)( (val & 0xFF00) >> 8);
					ptr++;
					*ptr = (char)(val & 0xFF);
				}

				ptr++;
			}
		} else {
			/* Narrow Pattern:  FE 01 FD 02 FB 04
			 * .. 7F 80 01 FE 02 FD ...  80 7F
			 */
			byte = 0;
			for (ii=0; ii < size; ii++, ptr++) {
				/* Base pattern - first 32 bytes
				 */
				if (ii & 0x01) {
					*ptr = (1 << byte);
					byte++;
					byte &= 0x07;
				} else {
					*ptr = (char) (~(1 << byte));
				}

				/* Flip the pattern every 32 bytes
				 */
				if (ii & 0x20)
					*ptr = ~(*ptr);
			}
		}
		break;
	}
}
#endif /* ~MPTSCSIH_ENABLE_DOMAIN_VALIDATION */

EXPORT_SYMBOL(mptscsih_remove);
EXPORT_SYMBOL(mptscsih_shutdown);
#ifdef CONFIG_PM
EXPORT_SYMBOL(mptscsih_suspend);
EXPORT_SYMBOL(mptscsih_resume);
#endif
EXPORT_SYMBOL(mptscsih_proc_info);
EXPORT_SYMBOL(mptscsih_info);
EXPORT_SYMBOL(mptscsih_qcmd);
EXPORT_SYMBOL(mptscsih_slave_alloc);
EXPORT_SYMBOL(mptscsih_slave_destroy);
EXPORT_SYMBOL(mptscsih_slave_configure);
EXPORT_SYMBOL(mptscsih_abort);
EXPORT_SYMBOL(mptscsih_dev_reset);
EXPORT_SYMBOL(mptscsih_bus_reset);
EXPORT_SYMBOL(mptscsih_host_reset);
EXPORT_SYMBOL(mptscsih_bios_param);
EXPORT_SYMBOL(mptscsih_io_done);
EXPORT_SYMBOL(mptscsih_taskmgmt_complete);
EXPORT_SYMBOL(mptscsih_scandv_complete);
EXPORT_SYMBOL(mptscsih_event_process);
EXPORT_SYMBOL(mptscsih_ioc_reset);
EXPORT_SYMBOL(mptscsih_change_queue_depth);
EXPORT_SYMBOL(mptscsih_timer_expired);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
