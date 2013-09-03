/*
 *  linux/drivers/scsi/esas2r/esas2r_io.c
 *      For use with ATTO ExpressSAS R6xx SAS/SATA RAID controllers
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)mpt3sas/mpt3sas_trigger_diag.
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

void esas2r_start_request(struct esas2r_adapter *a, struct esas2r_request *rq)
{
	struct esas2r_target *t = NULL;
	struct esas2r_request *startrq = rq;
	unsigned long flags;

	if (unlikely(a->flags & (AF_DEGRADED_MODE | AF_POWER_DOWN))) {
		if (rq->vrq->scsi.function == VDA_FUNC_SCSI)
			rq->req_stat = RS_SEL2;
		else
			rq->req_stat = RS_DEGRADED;
	} else if (likely(rq->vrq->scsi.function == VDA_FUNC_SCSI)) {
		t = a->targetdb + rq->target_id;

		if (unlikely(t >= a->targetdb_end
			     || !(t->flags & TF_USED))) {
			rq->req_stat = RS_SEL;
		} else {
			/* copy in the target ID. */
			rq->vrq->scsi.target_id = cpu_to_le16(t->virt_targ_id);

			/*
			 * Test if we want to report RS_SEL for missing target.
			 * Note that if AF_DISC_PENDING is set than this will
			 * go on the defer queue.
			 */
			if (unlikely(t->target_state != TS_PRESENT
				     && !(a->flags & AF_DISC_PENDING)))
				rq->req_stat = RS_SEL;
		}
	}

	if (unlikely(rq->req_stat != RS_PENDING)) {
		esas2r_complete_request(a, rq);
		return;
	}

	esas2r_trace("rq=%p", rq);
	esas2r_trace("rq->vrq->scsi.handle=%x", rq->vrq->scsi.handle);

	if (rq->vrq->scsi.function == VDA_FUNC_SCSI) {
		esas2r_trace("rq->target_id=%d", rq->target_id);
		esas2r_trace("rq->vrq->scsi.flags=%x", rq->vrq->scsi.flags);
	}

	spin_lock_irqsave(&a->queue_lock, flags);

	if (likely(list_empty(&a->defer_list) &&
		   !(a->flags &
		     (AF_CHPRST_PENDING | AF_FLASHING | AF_DISC_PENDING))))
		esas2r_local_start_request(a, startrq);
	else
		list_add_tail(&startrq->req_list, &a->defer_list);

	spin_unlock_irqrestore(&a->queue_lock, flags);
}

/*
 * Starts the specified request.  all requests have RS_PENDING set when this
 * routine is called.  The caller is usually esas2r_start_request, but
 * esas2r_do_deferred_processes will start request that are deferred.
 *
 * The caller must ensure that requests can be started.
 *
 * esas2r_start_request will defer a request if there are already requests
 * waiting or there is a chip reset pending.  once the reset condition clears,
 * esas2r_do_deferred_processes will call this function to start the request.
 *
 * When a request is started, it is placed on the active list and queued to
 * the controller.
 */
void esas2r_local_start_request(struct esas2r_adapter *a,
				struct esas2r_request *rq)
{
	esas2r_trace_enter();
	esas2r_trace("rq=%p", rq);
	esas2r_trace("rq->vrq:%p", rq->vrq);
	esas2r_trace("rq->vrq_md->phys_addr:%x", rq->vrq_md->phys_addr);

	if (unlikely(rq->vrq->scsi.function == VDA_FUNC_FLASH
		     && rq->vrq->flash.sub_func == VDA_FLASH_COMMIT))
		esas2r_lock_set_flags(&a->flags, AF_FLASHING);

	list_add_tail(&rq->req_list, &a->active_list);
	esas2r_start_vda_request(a, rq);
	esas2r_trace_exit();
	return;
}

void esas2r_start_vda_request(struct esas2r_adapter *a,
			      struct esas2r_request *rq)
{
	struct esas2r_inbound_list_source_entry *element;
	u32 dw;

	rq->req_stat = RS_STARTED;
	/*
	 * Calculate the inbound list entry location and the current state of
	 * toggle bit.
	 */
	a->last_write++;
	if (a->last_write >= a->list_size) {
		a->last_write = 0;
		/* update the toggle bit */
		if (a->flags & AF_COMM_LIST_TOGGLE)
			esas2r_lock_clear_flags(&a->flags,
						AF_COMM_LIST_TOGGLE);
		else
			esas2r_lock_set_flags(&a->flags, AF_COMM_LIST_TOGGLE);
	}

	element =
		(struct esas2r_inbound_list_source_entry *)a->inbound_list_md.
		virt_addr
		+ a->last_write;

	/* Set the VDA request size if it was never modified */
	if (rq->vda_req_sz == RQ_SIZE_DEFAULT)
		rq->vda_req_sz = (u16)(a->max_vdareq_size / sizeof(u32));

	element->address = cpu_to_le64(rq->vrq_md->phys_addr);
	element->length = cpu_to_le32(rq->vda_req_sz);

	/* Update the write pointer */
	dw = a->last_write;

	if (a->flags & AF_COMM_LIST_TOGGLE)
		dw |= MU_ILW_TOGGLE;

	esas2r_trace("rq->vrq->scsi.handle:%x", rq->vrq->scsi.handle);
	esas2r_trace("dw:%x", dw);
	esas2r_trace("rq->vda_req_sz:%x", rq->vda_req_sz);
	esas2r_write_register_dword(a, MU_IN_LIST_WRITE, dw);
}

/*
 * Build the scatter/gather list for an I/O request according to the
 * specifications placed in the s/g context.  The caller must initialize
 * context prior to the initial call by calling esas2r_sgc_init().
 */
bool esas2r_build_sg_list_sge(struct esas2r_adapter *a,
			      struct esas2r_sg_context *sgc)
{
	struct esas2r_request *rq = sgc->first_req;
	union atto_vda_req *vrq = rq->vrq;

	while (sgc->length) {
		u32 rem = 0;
		u64 addr;
		u32 len;

		len = (*sgc->get_phys_addr)(sgc, &addr);

		if (unlikely(len == 0))
			return false;

		/* if current length is more than what's left, stop there */
		if (unlikely(len > sgc->length))
			len = sgc->length;

another_entry:
		/* limit to a round number less than the maximum length */
		if (len > SGE_LEN_MAX) {
			/*
			 * Save the remainder of the split.  Whenever we limit
			 * an entry we come back around to build entries out
			 * of the leftover.  We do this to prevent multiple
			 * calls to the get_phys_addr() function for an SGE
			 * that is too large.
			 */
			rem = len - SGE_LEN_MAX;
			len = SGE_LEN_MAX;
		}

		/* See if we need to allocate a new SGL */
		if (unlikely(sgc->sge.a64.curr > sgc->sge.a64.limit)) {
			u8 sgelen;
			struct esas2r_mem_desc *sgl;

			/*
			 * If no SGls are available, return failure.  The
			 * caller can call us later with the current context
			 * to pick up here.
			 */
			sgl = esas2r_alloc_sgl(a);

			if (unlikely(sgl == NULL))
				return false;

			/* Calculate the length of the last SGE filled in */
			sgelen = (u8)((u8 *)sgc->sge.a64.curr
				      - (u8 *)sgc->sge.a64.last);

			/*
			 * Copy the last SGE filled in to the first entry of
			 * the new SGL to make room for the chain entry.
			 */
			memcpy(sgl->virt_addr, sgc->sge.a64.last, sgelen);

			/* Figure out the new curr pointer in the new segment */
			sgc->sge.a64.curr =
				(struct atto_vda_sge *)((u8 *)sgl->virt_addr +
							sgelen);

			/* Set the limit pointer and build the chain entry */
			sgc->sge.a64.limit =
				(struct atto_vda_sge *)((u8 *)sgl->virt_addr
							+ sgl_page_size
							- sizeof(struct
								 atto_vda_sge));
			sgc->sge.a64.last->length = cpu_to_le32(
				SGE_CHAIN | SGE_ADDR_64);
			sgc->sge.a64.last->address =
				cpu_to_le64(sgl->phys_addr);

			/*
			 * Now, if there was a previous chain entry, then
			 * update it to contain the length of this segment
			 * and size of this chain.  otherwise this is the
			 * first SGL, so set the chain_offset in the request.
			 */
			if (sgc->sge.a64.chain) {
				sgc->sge.a64.chain->length |=
					cpu_to_le32(
						((u8 *)(sgc->sge.a64.
							last + 1)
						 - (u8 *)rq->sg_table->
						 virt_addr)
						+ sizeof(struct atto_vda_sge) *
						LOBIT(SGE_CHAIN_SZ));
			} else {
				vrq->scsi.chain_offset = (u8)
							 ((u8 *)sgc->
							  sge.a64.last -
							  (u8 *)vrq);

				/*
				 * This is the first SGL, so set the
				 * chain_offset and the VDA request size in
				 * the request.
				 */
				rq->vda_req_sz =
					(vrq->scsi.chain_offset +
					 sizeof(struct atto_vda_sge) +
					 3)
					/ sizeof(u32);
			}

			/*
			 * Remember this so when we get a new SGL filled in we
			 * can update the length of this chain entry.
			 */
			sgc->sge.a64.chain = sgc->sge.a64.last;

			/* Now link the new SGL onto the primary request. */
			list_add(&sgl->next_desc, &rq->sg_table_head);
		}

		/* Update last one filled in */
		sgc->sge.a64.last = sgc->sge.a64.curr;

		/* Build the new SGE and update the S/G context */
		sgc->sge.a64.curr->length = cpu_to_le32(SGE_ADDR_64 | len);
		sgc->sge.a64.curr->address = cpu_to_le32(addr);
		sgc->sge.a64.curr++;
		sgc->cur_offset += len;
		sgc->length -= len;

		/*
		 * Check if we previously split an entry.  If so we have to
		 * pick up where we left off.
		 */
		if (rem) {
			addr += len;
			len = rem;
			rem = 0;
			goto another_entry;
		}
	}

	/* Mark the end of the SGL */
	sgc->sge.a64.last->length |= cpu_to_le32(SGE_LAST);

	/*
	 * If there was a previous chain entry, update the length to indicate
	 * the length of this last segment.
	 */
	if (sgc->sge.a64.chain) {
		sgc->sge.a64.chain->length |= cpu_to_le32(
			((u8 *)(sgc->sge.a64.curr) -
			 (u8 *)rq->sg_table->virt_addr));
	} else {
		u16 reqsize;

		/*
		 * The entire VDA request was not used so lets
		 * set the size of the VDA request to be DMA'd
		 */
		reqsize =
			((u16)((u8 *)sgc->sge.a64.last - (u8 *)vrq)
			 + sizeof(struct atto_vda_sge) + 3) / sizeof(u32);

		/*
		 * Only update the request size if it is bigger than what is
		 * already there.  We can come in here twice for some management
		 * commands.
		 */
		if (reqsize > rq->vda_req_sz)
			rq->vda_req_sz = reqsize;
	}
	return true;
}


/*
 * Create PRD list for each I-block consumed by the command. This routine
 * determines how much data is required from each I-block being consumed
 * by the command. The first and last I-blocks can be partials and all of
 * the I-blocks in between are for a full I-block of data.
 *
 * The interleave size is used to determine the number of bytes in the 1st
 * I-block and the remaining I-blocks are what remeains.
 */
static bool esas2r_build_prd_iblk(struct esas2r_adapter *a,
				  struct esas2r_sg_context *sgc)
{
	struct esas2r_request *rq = sgc->first_req;
	u64 addr;
	u32 len;
	struct esas2r_mem_desc *sgl;
	u32 numchain = 1;
	u32 rem = 0;

	while (sgc->length) {
		/* Get the next address/length pair */

		len = (*sgc->get_phys_addr)(sgc, &addr);

		if (unlikely(len == 0))
			return false;

		/* If current length is more than what's left, stop there */

		if (unlikely(len > sgc->length))
			len = sgc->length;

another_entry:
		/* Limit to a round number less than the maximum length */

		if (len > PRD_LEN_MAX) {
			/*
			 * Save the remainder of the split.  whenever we limit
			 * an entry we come back around to build entries out
			 * of the leftover.  We do this to prevent multiple
			 * calls to the get_phys_addr() function for an SGE
			 * that is too large.
			 */
			rem = len - PRD_LEN_MAX;
			len = PRD_LEN_MAX;
		}

		/* See if we need to allocate a new SGL */
		if (sgc->sge.prd.sge_cnt == 0) {
			if (len == sgc->length) {
				/*
				 * We only have 1 PRD entry left.
				 * It can be placed where the chain
				 * entry would have gone
				 */

				/* Build the simple SGE */
				sgc->sge.prd.curr->ctl_len = cpu_to_le32(
					PRD_DATA | len);
				sgc->sge.prd.curr->address = cpu_to_le64(addr);

				/* Adjust length related fields */
				sgc->cur_offset += len;
				sgc->length -= len;

				/* We use the reserved chain entry for data */
				numchain = 0;

				break;
			}

			if (sgc->sge.prd.chain) {
				/*
				 * Fill # of entries of current SGL in previous
				 * chain the length of this current SGL may not
				 * full.
				 */

				sgc->sge.prd.chain->ctl_len |= cpu_to_le32(
					sgc->sge.prd.sgl_max_cnt);
			}

			/*
			 * If no SGls are available, return failure.  The
			 * caller can call us later with the current context
			 * to pick up here.
			 */

			sgl = esas2r_alloc_sgl(a);

			if (unlikely(sgl == NULL))
				return false;

			/*
			 * Link the new SGL onto the chain
			 * They are in reverse order
			 */
			list_add(&sgl->next_desc, &rq->sg_table_head);

			/*
			 * An SGL was just filled in and we are starting
			 * a new SGL. Prime the chain of the ending SGL with
			 * info that points to the new SGL. The length gets
			 * filled in when the new SGL is filled or ended
			 */

			sgc->sge.prd.chain = sgc->sge.prd.curr;

			sgc->sge.prd.chain->ctl_len = cpu_to_le32(PRD_CHAIN);
			sgc->sge.prd.chain->address =
				cpu_to_le64(sgl->phys_addr);

			/*
			 * Start a new segment.
			 * Take one away and save for chain SGE
			 */

			sgc->sge.prd.curr =
				(struct atto_physical_region_description *)sgl
				->
				virt_addr;
			sgc->sge.prd.sge_cnt = sgc->sge.prd.sgl_max_cnt - 1;
		}

		sgc->sge.prd.sge_cnt--;
		/* Build the simple SGE */
		sgc->sge.prd.curr->ctl_len = cpu_to_le32(PRD_DATA | len);
		sgc->sge.prd.curr->address = cpu_to_le64(addr);

		/* Used another element.  Point to the next one */

		sgc->sge.prd.curr++;

		/* Adjust length related fields */

		sgc->cur_offset += len;
		sgc->length -= len;

		/*
		 * Check if we previously split an entry.  If so we have to
		 * pick up where we left off.
		 */

		if (rem) {
			addr += len;
			len = rem;
			rem = 0;
			goto another_entry;
		}
	}

	if (!list_empty(&rq->sg_table_head)) {
		if (sgc->sge.prd.chain) {
			sgc->sge.prd.chain->ctl_len |=
				cpu_to_le32(sgc->sge.prd.sgl_max_cnt
					    - sgc->sge.prd.sge_cnt
					    - numchain);
		}
	}

	return true;
}

bool esas2r_build_sg_list_prd(struct esas2r_adapter *a,
			      struct esas2r_sg_context *sgc)
{
	struct esas2r_request *rq = sgc->first_req;
	u32 len = sgc->length;
	struct esas2r_target *t = a->targetdb + rq->target_id;
	u8 is_i_o = 0;
	u16 reqsize;
	struct atto_physical_region_description *curr_iblk_chn;
	u8 *cdb = (u8 *)&rq->vrq->scsi.cdb[0];

	/*
	 * extract LBA from command so we can determine
	 * the I-Block boundary
	 */

	if (rq->vrq->scsi.function == VDA_FUNC_SCSI
	    && t->target_state == TS_PRESENT
	    && !(t->flags & TF_PASS_THRU)) {
		u32 lbalo = 0;

		switch (rq->vrq->scsi.cdb[0]) {
		case    READ_16:
		case    WRITE_16:
		{
			lbalo =
				MAKEDWORD(MAKEWORD(cdb[9],
						   cdb[8]),
					  MAKEWORD(cdb[7],
						   cdb[6]));
			is_i_o = 1;
			break;
		}

		case    READ_12:
		case    WRITE_12:
		case    READ_10:
		case    WRITE_10:
		{
			lbalo =
				MAKEDWORD(MAKEWORD(cdb[5],
						   cdb[4]),
					  MAKEWORD(cdb[3],
						   cdb[2]));
			is_i_o = 1;
			break;
		}

		case    READ_6:
		case    WRITE_6:
		{
			lbalo =
				MAKEDWORD(MAKEWORD(cdb[3],
						   cdb[2]),
					  MAKEWORD(cdb[1] & 0x1F,
						   0));
			is_i_o = 1;
			break;
		}

		default:
			break;
		}

		if (is_i_o) {
			u32 startlba;

			rq->vrq->scsi.iblk_cnt_prd = 0;

			/* Determine size of 1st I-block PRD list       */
			startlba = t->inter_block - (lbalo & (t->inter_block -
							      1));
			sgc->length = startlba * t->block_size;

			/* Chk if the 1st iblk chain starts at base of Iblock */
			if ((lbalo & (t->inter_block - 1)) == 0)
				rq->flags |= RF_1ST_IBLK_BASE;

			if (sgc->length > len)
				sgc->length = len;
		} else {
			sgc->length = len;
		}
	} else {
		sgc->length = len;
	}

	/* get our starting chain address   */

	curr_iblk_chn =
		(struct atto_physical_region_description *)sgc->sge.a64.curr;

	sgc->sge.prd.sgl_max_cnt = sgl_page_size /
				   sizeof(struct
					  atto_physical_region_description);

	/* create all of the I-block PRD lists          */

	while (len) {
		sgc->sge.prd.sge_cnt = 0;
		sgc->sge.prd.chain = NULL;
		sgc->sge.prd.curr = curr_iblk_chn;

		/* increment to next I-Block    */

		len -= sgc->length;

		/* go build the next I-Block PRD list   */

		if (unlikely(!esas2r_build_prd_iblk(a, sgc)))
			return false;

		curr_iblk_chn++;

		if (is_i_o) {
			rq->vrq->scsi.iblk_cnt_prd++;

			if (len > t->inter_byte)
				sgc->length = t->inter_byte;
			else
				sgc->length = len;
		}
	}

	/* figure out the size used of the VDA request */

	reqsize = ((u16)((u8 *)curr_iblk_chn - (u8 *)rq->vrq))
		  / sizeof(u32);

	/*
	 * only update the request size if it is bigger than what is
	 * already there.  we can come in here twice for some management
	 * commands.
	 */

	if (reqsize > rq->vda_req_sz)
		rq->vda_req_sz = reqsize;

	return true;
}

static void esas2r_handle_pending_reset(struct esas2r_adapter *a, u32 currtime)
{
	u32 delta = currtime - a->chip_init_time;

	if (delta <= ESAS2R_CHPRST_WAIT_TIME) {
		/* Wait before accessing registers */
	} else if (delta >= ESAS2R_CHPRST_TIME) {
		/*
		 * The last reset failed so try again. Reset
		 * processing will give up after three tries.
		 */
		esas2r_local_reset_adapter(a);
	} else {
		/* We can now see if the firmware is ready */
		u32 doorbell;

		doorbell = esas2r_read_register_dword(a, MU_DOORBELL_OUT);
		if (doorbell == 0xFFFFFFFF || !(doorbell & DRBL_FORCE_INT)) {
			esas2r_force_interrupt(a);
		} else {
			u32 ver = (doorbell & DRBL_FW_VER_MSK);

			/* Driver supports API version 0 and 1 */
			esas2r_write_register_dword(a, MU_DOORBELL_OUT,
						    doorbell);
			if (ver == DRBL_FW_VER_0) {
				esas2r_lock_set_flags(&a->flags,
						      AF_CHPRST_DETECTED);
				esas2r_lock_set_flags(&a->flags,
						      AF_LEGACY_SGE_MODE);

				a->max_vdareq_size = 128;
				a->build_sgl = esas2r_build_sg_list_sge;
			} else if (ver == DRBL_FW_VER_1) {
				esas2r_lock_set_flags(&a->flags,
						      AF_CHPRST_DETECTED);
				esas2r_lock_clear_flags(&a->flags,
							AF_LEGACY_SGE_MODE);

				a->max_vdareq_size = 1024;
				a->build_sgl = esas2r_build_sg_list_prd;
			} else {
				esas2r_local_reset_adapter(a);
			}
		}
	}
}


/* This function must be called once per timer tick */
void esas2r_timer_tick(struct esas2r_adapter *a)
{
	u32 currtime = jiffies_to_msecs(jiffies);
	u32 deltatime = currtime - a->last_tick_time;

	a->last_tick_time = currtime;

	/* count down the uptime */
	if (a->chip_uptime
	    && !(a->flags & (AF_CHPRST_PENDING | AF_DISC_PENDING))) {
		if (deltatime >= a->chip_uptime)
			a->chip_uptime = 0;
		else
			a->chip_uptime -= deltatime;
	}

	if (a->flags & AF_CHPRST_PENDING) {
		if (!(a->flags & AF_CHPRST_NEEDED)
		    && !(a->flags & AF_CHPRST_DETECTED))
			esas2r_handle_pending_reset(a, currtime);
	} else {
		if (a->flags & AF_DISC_PENDING)
			esas2r_disc_check_complete(a);

		if (a->flags & AF_HEARTBEAT_ENB) {
			if (a->flags & AF_HEARTBEAT) {
				if ((currtime - a->heartbeat_time) >=
				    ESAS2R_HEARTBEAT_TIME) {
					esas2r_lock_clear_flags(&a->flags,
								AF_HEARTBEAT);
					esas2r_hdebug("heartbeat failed");
					esas2r_log(ESAS2R_LOG_CRIT,
						   "heartbeat failed");
					esas2r_bugon();
					esas2r_local_reset_adapter(a);
				}
			} else {
				esas2r_lock_set_flags(&a->flags, AF_HEARTBEAT);
				a->heartbeat_time = currtime;
				esas2r_force_interrupt(a);
			}
		}
	}

	if (atomic_read(&a->disable_cnt) == 0)
		esas2r_do_deferred_processes(a);
}

/*
 * Send the specified task management function to the target and LUN
 * specified in rqaux.  in addition, immediately abort any commands that
 * are queued but not sent to the device according to the rules specified
 * by the task management function.
 */
bool esas2r_send_task_mgmt(struct esas2r_adapter *a,
			   struct esas2r_request *rqaux, u8 task_mgt_func)
{
	u16 targetid = rqaux->target_id;
	u8 lun = (u8)le32_to_cpu(rqaux->vrq->scsi.flags);
	bool ret = false;
	struct esas2r_request *rq;
	struct list_head *next, *element;
	unsigned long flags;

	LIST_HEAD(comp_list);

	esas2r_trace_enter();
	esas2r_trace("rqaux:%p", rqaux);
	esas2r_trace("task_mgt_func:%x", task_mgt_func);
	spin_lock_irqsave(&a->queue_lock, flags);

	/* search the defer queue looking for requests for the device */
	list_for_each_safe(element, next, &a->defer_list) {
		rq = list_entry(element, struct esas2r_request, req_list);

		if (rq->vrq->scsi.function == VDA_FUNC_SCSI
		    && rq->target_id == targetid
		    && (((u8)le32_to_cpu(rq->vrq->scsi.flags)) == lun
			|| task_mgt_func == 0x20)) { /* target reset */
			/* Found a request affected by the task management */
			if (rq->req_stat == RS_PENDING) {
				/*
				 * The request is pending or waiting.  We can
				 * safelycomplete the request now.
				 */
				if (esas2r_ioreq_aborted(a, rq, RS_ABORTED))
					list_add_tail(&rq->comp_list,
						      &comp_list);
			}
		}
	}

	/* Send the task management request to the firmware */
	rqaux->sense_len = 0;
	rqaux->vrq->scsi.length = 0;
	rqaux->target_id = targetid;
	rqaux->vrq->scsi.flags |= cpu_to_le32(lun);
	memset(rqaux->vrq->scsi.cdb, 0, sizeof(rqaux->vrq->scsi.cdb));
	rqaux->vrq->scsi.flags |=
		cpu_to_le16(task_mgt_func * LOBIT(FCP_CMND_TM_MASK));

	if (a->flags & AF_FLASHING) {
		/* Assume success.  if there are active requests, return busy */
		rqaux->req_stat = RS_SUCCESS;

		list_for_each_safe(element, next, &a->active_list) {
			rq = list_entry(element, struct esas2r_request,
					req_list);
			if (rq->vrq->scsi.function == VDA_FUNC_SCSI
			    && rq->target_id == targetid
			    && (((u8)le32_to_cpu(rq->vrq->scsi.flags)) == lun
				|| task_mgt_func == 0x20))  /* target reset */
				rqaux->req_stat = RS_BUSY;
		}

		ret = true;
	}

	spin_unlock_irqrestore(&a->queue_lock, flags);

	if (!(a->flags & AF_FLASHING))
		esas2r_start_request(a, rqaux);

	esas2r_comp_list_drain(a, &comp_list);

	if (atomic_read(&a->disable_cnt) == 0)
		esas2r_do_deferred_processes(a);

	esas2r_trace_exit();

	return ret;
}

void esas2r_reset_bus(struct esas2r_adapter *a)
{
	esas2r_log(ESAS2R_LOG_INFO, "performing a bus reset");

	if (!(a->flags & AF_DEGRADED_MODE)
	    && !(a->flags & (AF_CHPRST_PENDING | AF_DISC_PENDING))) {
		esas2r_lock_set_flags(&a->flags, AF_BUSRST_NEEDED);
		esas2r_lock_set_flags(&a->flags, AF_BUSRST_PENDING);
		esas2r_lock_set_flags(&a->flags, AF_OS_RESET);

		esas2r_schedule_tasklet(a);
	}
}

bool esas2r_ioreq_aborted(struct esas2r_adapter *a, struct esas2r_request *rq,
			  u8 status)
{
	esas2r_trace_enter();
	esas2r_trace("rq:%p", rq);
	list_del_init(&rq->req_list);
	if (rq->timeout > RQ_MAX_TIMEOUT) {
		/*
		 * The request timed out, but we could not abort it because a
		 * chip reset occurred.  Return busy status.
		 */
		rq->req_stat = RS_BUSY;
		esas2r_trace_exit();
		return true;
	}

	rq->req_stat = status;
	esas2r_trace_exit();
	return true;
}
