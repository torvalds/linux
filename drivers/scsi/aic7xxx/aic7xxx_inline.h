/*
 * Inline routines shareable across OS platforms.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/aic7xxx/aic7xxx_inline.h#43 $
 *
 * $FreeBSD$
 */

#ifndef _AIC7XXX_INLINE_H_
#define _AIC7XXX_INLINE_H_

/************************* Sequencer Execution Control ************************/
static __inline void ahc_pause_bug_fix(struct ahc_softc *ahc);
static __inline int  ahc_is_paused(struct ahc_softc *ahc);
static __inline void ahc_pause(struct ahc_softc *ahc);
static __inline void ahc_unpause(struct ahc_softc *ahc);

/*
 * Work around any chip bugs related to halting sequencer execution.
 * On Ultra2 controllers, we must clear the CIOBUS stretch signal by
 * reading a register that will set this signal and deassert it.
 * Without this workaround, if the chip is paused, by an interrupt or
 * manual pause while accessing scb ram, accesses to certain registers
 * will hang the system (infinite pci retries).
 */
static __inline void
ahc_pause_bug_fix(struct ahc_softc *ahc)
{
	if ((ahc->features & AHC_ULTRA2) != 0)
		(void)ahc_inb(ahc, CCSCBCTL);
}

/*
 * Determine whether the sequencer has halted code execution.
 * Returns non-zero status if the sequencer is stopped.
 */
static __inline int
ahc_is_paused(struct ahc_softc *ahc)
{
	return ((ahc_inb(ahc, HCNTRL) & PAUSE) != 0);
}

/*
 * Request that the sequencer stop and wait, indefinitely, for it
 * to stop.  The sequencer will only acknowledge that it is paused
 * once it has reached an instruction boundary and PAUSEDIS is
 * cleared in the SEQCTL register.  The sequencer may use PAUSEDIS
 * for critical sections.
 */
static __inline void
ahc_pause(struct ahc_softc *ahc)
{
	ahc_outb(ahc, HCNTRL, ahc->pause);

	/*
	 * Since the sequencer can disable pausing in a critical section, we
	 * must loop until it actually stops.
	 */
	while (ahc_is_paused(ahc) == 0)
		;

	ahc_pause_bug_fix(ahc);
}

/*
 * Allow the sequencer to continue program execution.
 * We check here to ensure that no additional interrupt
 * sources that would cause the sequencer to halt have been
 * asserted.  If, for example, a SCSI bus reset is detected
 * while we are fielding a different, pausing, interrupt type,
 * we don't want to release the sequencer before going back
 * into our interrupt handler and dealing with this new
 * condition.
 */
static __inline void
ahc_unpause(struct ahc_softc *ahc)
{
	if ((ahc_inb(ahc, INTSTAT) & (SCSIINT | SEQINT | BRKADRINT)) == 0)
		ahc_outb(ahc, HCNTRL, ahc->unpause);
}

/*********************** Untagged Transaction Routines ************************/
static __inline void	ahc_freeze_untagged_queues(struct ahc_softc *ahc);
static __inline void	ahc_release_untagged_queues(struct ahc_softc *ahc);

/*
 * Block our completion routine from starting the next untagged
 * transaction for this target or target lun.
 */
static __inline void
ahc_freeze_untagged_queues(struct ahc_softc *ahc)
{
	if ((ahc->flags & AHC_SCB_BTT) == 0)
		ahc->untagged_queue_lock++;
}

/*
 * Allow the next untagged transaction for this target or target lun
 * to be executed.  We use a counting semaphore to allow the lock
 * to be acquired recursively.  Once the count drops to zero, the
 * transaction queues will be run.
 */
static __inline void
ahc_release_untagged_queues(struct ahc_softc *ahc)
{
	if ((ahc->flags & AHC_SCB_BTT) == 0) {
		ahc->untagged_queue_lock--;
		if (ahc->untagged_queue_lock == 0)
			ahc_run_untagged_queues(ahc);
	}
}

/************************** Memory mapping routines ***************************/
static __inline struct ahc_dma_seg *
			ahc_sg_bus_to_virt(struct scb *scb,
					   uint32_t sg_busaddr);
static __inline uint32_t
			ahc_sg_virt_to_bus(struct scb *scb,
					   struct ahc_dma_seg *sg);
static __inline uint32_t
			ahc_hscb_busaddr(struct ahc_softc *ahc, u_int index);
static __inline void	ahc_sync_scb(struct ahc_softc *ahc,
				     struct scb *scb, int op);
static __inline void	ahc_sync_sglist(struct ahc_softc *ahc,
					struct scb *scb, int op);
static __inline uint32_t
			ahc_targetcmd_offset(struct ahc_softc *ahc,
					     u_int index);

static __inline struct ahc_dma_seg *
ahc_sg_bus_to_virt(struct scb *scb, uint32_t sg_busaddr)
{
	int sg_index;

	sg_index = (sg_busaddr - scb->sg_list_phys)/sizeof(struct ahc_dma_seg);
	/* sg_list_phys points to entry 1, not 0 */
	sg_index++;

	return (&scb->sg_list[sg_index]);
}

static __inline uint32_t
ahc_sg_virt_to_bus(struct scb *scb, struct ahc_dma_seg *sg)
{
	int sg_index;

	/* sg_list_phys points to entry 1, not 0 */
	sg_index = sg - &scb->sg_list[1];

	return (scb->sg_list_phys + (sg_index * sizeof(*scb->sg_list)));
}

static __inline uint32_t
ahc_hscb_busaddr(struct ahc_softc *ahc, u_int index)
{
	return (ahc->scb_data->hscb_busaddr
		+ (sizeof(struct hardware_scb) * index));
}

static __inline void
ahc_sync_scb(struct ahc_softc *ahc, struct scb *scb, int op)
{
	ahc_dmamap_sync(ahc, ahc->scb_data->hscb_dmat,
			ahc->scb_data->hscb_dmamap,
			/*offset*/(scb->hscb - ahc->hscbs) * sizeof(*scb->hscb),
			/*len*/sizeof(*scb->hscb), op);
}

static __inline void
ahc_sync_sglist(struct ahc_softc *ahc, struct scb *scb, int op)
{
	if (scb->sg_count == 0)
		return;

	ahc_dmamap_sync(ahc, ahc->scb_data->sg_dmat, scb->sg_map->sg_dmamap,
			/*offset*/(scb->sg_list - scb->sg_map->sg_vaddr)
				* sizeof(struct ahc_dma_seg),
			/*len*/sizeof(struct ahc_dma_seg) * scb->sg_count, op);
}

static __inline uint32_t
ahc_targetcmd_offset(struct ahc_softc *ahc, u_int index)
{
	return (((uint8_t *)&ahc->targetcmds[index]) - ahc->qoutfifo);
}

/******************************** Debugging ***********************************/
static __inline char *ahc_name(struct ahc_softc *ahc);

static __inline char *
ahc_name(struct ahc_softc *ahc)
{
	return (ahc->name);
}

/*********************** Miscelaneous Support Functions ***********************/

static __inline void	ahc_update_residual(struct ahc_softc *ahc,
					    struct scb *scb);
static __inline struct ahc_initiator_tinfo *
			ahc_fetch_transinfo(struct ahc_softc *ahc,
					    char channel, u_int our_id,
					    u_int remote_id,
					    struct ahc_tmode_tstate **tstate);
static __inline uint16_t
			ahc_inw(struct ahc_softc *ahc, u_int port);
static __inline void	ahc_outw(struct ahc_softc *ahc, u_int port,
				 u_int value);
static __inline uint32_t
			ahc_inl(struct ahc_softc *ahc, u_int port);
static __inline void	ahc_outl(struct ahc_softc *ahc, u_int port,
				 uint32_t value);
static __inline uint64_t
			ahc_inq(struct ahc_softc *ahc, u_int port);
static __inline void	ahc_outq(struct ahc_softc *ahc, u_int port,
				 uint64_t value);
static __inline struct scb*
			ahc_get_scb(struct ahc_softc *ahc);
static __inline void	ahc_free_scb(struct ahc_softc *ahc, struct scb *scb);
static __inline void	ahc_swap_with_next_hscb(struct ahc_softc *ahc,
						struct scb *scb);
static __inline void	ahc_queue_scb(struct ahc_softc *ahc, struct scb *scb);
static __inline struct scsi_sense_data *
			ahc_get_sense_buf(struct ahc_softc *ahc,
					  struct scb *scb);
static __inline uint32_t
			ahc_get_sense_bufaddr(struct ahc_softc *ahc,
					      struct scb *scb);

/*
 * Determine whether the sequencer reported a residual
 * for this SCB/transaction.
 */
static __inline void
ahc_update_residual(struct ahc_softc *ahc, struct scb *scb)
{
	uint32_t sgptr;

	sgptr = ahc_le32toh(scb->hscb->sgptr);
	if ((sgptr & SG_RESID_VALID) != 0)
		ahc_calc_residual(ahc, scb);
}

/*
 * Return pointers to the transfer negotiation information
 * for the specified our_id/remote_id pair.
 */
static __inline struct ahc_initiator_tinfo *
ahc_fetch_transinfo(struct ahc_softc *ahc, char channel, u_int our_id,
		    u_int remote_id, struct ahc_tmode_tstate **tstate)
{
	/*
	 * Transfer data structures are stored from the perspective
	 * of the target role.  Since the parameters for a connection
	 * in the initiator role to a given target are the same as
	 * when the roles are reversed, we pretend we are the target.
	 */
	if (channel == 'B')
		our_id += 8;
	*tstate = ahc->enabled_targets[our_id];
	return (&(*tstate)->transinfo[remote_id]);
}

static __inline uint16_t
ahc_inw(struct ahc_softc *ahc, u_int port)
{
	uint16_t r = ahc_inb(ahc, port+1) << 8;
	return r | ahc_inb(ahc, port);
}

static __inline void
ahc_outw(struct ahc_softc *ahc, u_int port, u_int value)
{
	ahc_outb(ahc, port, value & 0xFF);
	ahc_outb(ahc, port+1, (value >> 8) & 0xFF);
}

static __inline uint32_t
ahc_inl(struct ahc_softc *ahc, u_int port)
{
	return ((ahc_inb(ahc, port))
	      | (ahc_inb(ahc, port+1) << 8)
	      | (ahc_inb(ahc, port+2) << 16)
	      | (ahc_inb(ahc, port+3) << 24));
}

static __inline void
ahc_outl(struct ahc_softc *ahc, u_int port, uint32_t value)
{
	ahc_outb(ahc, port, (value) & 0xFF);
	ahc_outb(ahc, port+1, ((value) >> 8) & 0xFF);
	ahc_outb(ahc, port+2, ((value) >> 16) & 0xFF);
	ahc_outb(ahc, port+3, ((value) >> 24) & 0xFF);
}

static __inline uint64_t
ahc_inq(struct ahc_softc *ahc, u_int port)
{
	return ((ahc_inb(ahc, port))
	      | (ahc_inb(ahc, port+1) << 8)
	      | (ahc_inb(ahc, port+2) << 16)
	      | (ahc_inb(ahc, port+3) << 24)
	      | (((uint64_t)ahc_inb(ahc, port+4)) << 32)
	      | (((uint64_t)ahc_inb(ahc, port+5)) << 40)
	      | (((uint64_t)ahc_inb(ahc, port+6)) << 48)
	      | (((uint64_t)ahc_inb(ahc, port+7)) << 56));
}

static __inline void
ahc_outq(struct ahc_softc *ahc, u_int port, uint64_t value)
{
	ahc_outb(ahc, port, value & 0xFF);
	ahc_outb(ahc, port+1, (value >> 8) & 0xFF);
	ahc_outb(ahc, port+2, (value >> 16) & 0xFF);
	ahc_outb(ahc, port+3, (value >> 24) & 0xFF);
	ahc_outb(ahc, port+4, (value >> 32) & 0xFF);
	ahc_outb(ahc, port+5, (value >> 40) & 0xFF);
	ahc_outb(ahc, port+6, (value >> 48) & 0xFF);
	ahc_outb(ahc, port+7, (value >> 56) & 0xFF);
}

/*
 * Get a free scb. If there are none, see if we can allocate a new SCB.
 */
static __inline struct scb *
ahc_get_scb(struct ahc_softc *ahc)
{
	struct scb *scb;

	if ((scb = SLIST_FIRST(&ahc->scb_data->free_scbs)) == NULL) {
		ahc_alloc_scbs(ahc);
		scb = SLIST_FIRST(&ahc->scb_data->free_scbs);
		if (scb == NULL)
			return (NULL);
	}
	SLIST_REMOVE_HEAD(&ahc->scb_data->free_scbs, links.sle);
	return (scb);
}

/*
 * Return an SCB resource to the free list.
 */
static __inline void
ahc_free_scb(struct ahc_softc *ahc, struct scb *scb)
{       
	struct hardware_scb *hscb;

	hscb = scb->hscb;
	/* Clean up for the next user */
	ahc->scb_data->scbindex[hscb->tag] = NULL;
	scb->flags = SCB_FREE;
	hscb->control = 0;

	SLIST_INSERT_HEAD(&ahc->scb_data->free_scbs, scb, links.sle);

	/* Notify the OSM that a resource is now available. */
	ahc_platform_scb_free(ahc, scb);
}

static __inline struct scb *
ahc_lookup_scb(struct ahc_softc *ahc, u_int tag)
{
	struct scb* scb;

	scb = ahc->scb_data->scbindex[tag];
	if (scb != NULL)
		ahc_sync_scb(ahc, scb,
			     BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	return (scb);
}

static __inline void
ahc_swap_with_next_hscb(struct ahc_softc *ahc, struct scb *scb)
{
	struct hardware_scb *q_hscb;
	u_int  saved_tag;

	/*
	 * Our queuing method is a bit tricky.  The card
	 * knows in advance which HSCB to download, and we
	 * can't disappoint it.  To achieve this, the next
	 * SCB to download is saved off in ahc->next_queued_scb.
	 * When we are called to queue "an arbitrary scb",
	 * we copy the contents of the incoming HSCB to the one
	 * the sequencer knows about, swap HSCB pointers and
	 * finally assign the SCB to the tag indexed location
	 * in the scb_array.  This makes sure that we can still
	 * locate the correct SCB by SCB_TAG.
	 */
	q_hscb = ahc->next_queued_scb->hscb;
	saved_tag = q_hscb->tag;
	memcpy(q_hscb, scb->hscb, sizeof(*scb->hscb));
	if ((scb->flags & SCB_CDB32_PTR) != 0) {
		q_hscb->shared_data.cdb_ptr =
		    ahc_htole32(ahc_hscb_busaddr(ahc, q_hscb->tag)
			      + offsetof(struct hardware_scb, cdb32));
	}
	q_hscb->tag = saved_tag;
	q_hscb->next = scb->hscb->tag;

	/* Now swap HSCB pointers. */
	ahc->next_queued_scb->hscb = scb->hscb;
	scb->hscb = q_hscb;

	/* Now define the mapping from tag to SCB in the scbindex */
	ahc->scb_data->scbindex[scb->hscb->tag] = scb;
}

/*
 * Tell the sequencer about a new transaction to execute.
 */
static __inline void
ahc_queue_scb(struct ahc_softc *ahc, struct scb *scb)
{
	ahc_swap_with_next_hscb(ahc, scb);

	if (scb->hscb->tag == SCB_LIST_NULL
	 || scb->hscb->next == SCB_LIST_NULL)
		panic("Attempt to queue invalid SCB tag %x:%x\n",
		      scb->hscb->tag, scb->hscb->next);

	/*
	 * Setup data "oddness".
	 */
	scb->hscb->lun &= LID;
	if (ahc_get_transfer_length(scb) & 0x1)
		scb->hscb->lun |= SCB_XFERLEN_ODD;

	/*
	 * Keep a history of SCBs we've downloaded in the qinfifo.
	 */
	ahc->qinfifo[ahc->qinfifonext++] = scb->hscb->tag;

	/*
	 * Make sure our data is consistent from the
	 * perspective of the adapter.
	 */
	ahc_sync_scb(ahc, scb, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	/* Tell the adapter about the newly queued SCB */
	if ((ahc->features & AHC_QUEUE_REGS) != 0) {
		ahc_outb(ahc, HNSCB_QOFF, ahc->qinfifonext);
	} else {
		if ((ahc->features & AHC_AUTOPAUSE) == 0)
			ahc_pause(ahc);
		ahc_outb(ahc, KERNEL_QINPOS, ahc->qinfifonext);
		if ((ahc->features & AHC_AUTOPAUSE) == 0)
			ahc_unpause(ahc);
	}
}

static __inline struct scsi_sense_data *
ahc_get_sense_buf(struct ahc_softc *ahc, struct scb *scb)
{
	int offset;

	offset = scb - ahc->scb_data->scbarray;
	return (&ahc->scb_data->sense[offset]);
}

static __inline uint32_t
ahc_get_sense_bufaddr(struct ahc_softc *ahc, struct scb *scb)
{
	int offset;

	offset = scb - ahc->scb_data->scbarray;
	return (ahc->scb_data->sense_busaddr
	      + (offset * sizeof(struct scsi_sense_data)));
}

/************************** Interrupt Processing ******************************/
static __inline void	ahc_sync_qoutfifo(struct ahc_softc *ahc, int op);
static __inline void	ahc_sync_tqinfifo(struct ahc_softc *ahc, int op);
static __inline u_int	ahc_check_cmdcmpltqueues(struct ahc_softc *ahc);
static __inline int	ahc_intr(struct ahc_softc *ahc);

static __inline void
ahc_sync_qoutfifo(struct ahc_softc *ahc, int op)
{
	ahc_dmamap_sync(ahc, ahc->shared_data_dmat, ahc->shared_data_dmamap,
			/*offset*/0, /*len*/256, op);
}

static __inline void
ahc_sync_tqinfifo(struct ahc_softc *ahc, int op)
{
#ifdef AHC_TARGET_MODE
	if ((ahc->flags & AHC_TARGETROLE) != 0) {
		ahc_dmamap_sync(ahc, ahc->shared_data_dmat,
				ahc->shared_data_dmamap,
				ahc_targetcmd_offset(ahc, 0),
				sizeof(struct target_cmd) * AHC_TMODE_CMDS,
				op);
	}
#endif
}

/*
 * See if the firmware has posted any completed commands
 * into our in-core command complete fifos.
 */
#define AHC_RUN_QOUTFIFO 0x1
#define AHC_RUN_TQINFIFO 0x2
static __inline u_int
ahc_check_cmdcmpltqueues(struct ahc_softc *ahc)
{
	u_int retval;

	retval = 0;
	ahc_dmamap_sync(ahc, ahc->shared_data_dmat, ahc->shared_data_dmamap,
			/*offset*/ahc->qoutfifonext, /*len*/1,
			BUS_DMASYNC_POSTREAD);
	if (ahc->qoutfifo[ahc->qoutfifonext] != SCB_LIST_NULL)
		retval |= AHC_RUN_QOUTFIFO;
#ifdef AHC_TARGET_MODE
	if ((ahc->flags & AHC_TARGETROLE) != 0
	 && (ahc->flags & AHC_TQINFIFO_BLOCKED) == 0) {
		ahc_dmamap_sync(ahc, ahc->shared_data_dmat,
				ahc->shared_data_dmamap,
				ahc_targetcmd_offset(ahc, ahc->tqinfifofnext),
				/*len*/sizeof(struct target_cmd),
				BUS_DMASYNC_POSTREAD);
		if (ahc->targetcmds[ahc->tqinfifonext].cmd_valid != 0)
			retval |= AHC_RUN_TQINFIFO;
	}
#endif
	return (retval);
}

/*
 * Catch an interrupt from the adapter
 */
static __inline int
ahc_intr(struct ahc_softc *ahc)
{
	u_int	intstat;

	if ((ahc->pause & INTEN) == 0) {
		/*
		 * Our interrupt is not enabled on the chip
		 * and may be disabled for re-entrancy reasons,
		 * so just return.  This is likely just a shared
		 * interrupt.
		 */
		return (0);
	}
	/*
	 * Instead of directly reading the interrupt status register,
	 * infer the cause of the interrupt by checking our in-core
	 * completion queues.  This avoids a costly PCI bus read in
	 * most cases.
	 */
	if ((ahc->flags & (AHC_ALL_INTERRUPTS|AHC_EDGE_INTERRUPT)) == 0
	 && (ahc_check_cmdcmpltqueues(ahc) != 0))
		intstat = CMDCMPLT;
	else {
		intstat = ahc_inb(ahc, INTSTAT);
	}

	if ((intstat & INT_PEND) == 0) {
#if AHC_PCI_CONFIG > 0
		if (ahc->unsolicited_ints > 500) {
			ahc->unsolicited_ints = 0;
			if ((ahc->chip & AHC_PCI) != 0
			 && (ahc_inb(ahc, ERROR) & PCIERRSTAT) != 0)
				ahc->bus_intr(ahc);
		}
#endif
		ahc->unsolicited_ints++;
		return (0);
	}
	ahc->unsolicited_ints = 0;

	if (intstat & CMDCMPLT) {
		ahc_outb(ahc, CLRINT, CLRCMDINT);

		/*
		 * Ensure that the chip sees that we've cleared
		 * this interrupt before we walk the output fifo.
		 * Otherwise, we may, due to posted bus writes,
		 * clear the interrupt after we finish the scan,
		 * and after the sequencer has added new entries
		 * and asserted the interrupt again.
		 */
		ahc_flush_device_writes(ahc);
		ahc_run_qoutfifo(ahc);
#ifdef AHC_TARGET_MODE
		if ((ahc->flags & AHC_TARGETROLE) != 0)
			ahc_run_tqinfifo(ahc, /*paused*/FALSE);
#endif
	}

	/*
	 * Handle statuses that may invalidate our cached
	 * copy of INTSTAT separately.
	 */
	if (intstat == 0xFF && (ahc->features & AHC_REMOVABLE) != 0) {
		/* Hot eject.  Do nothing */
	} else if (intstat & BRKADRINT) {
		ahc_handle_brkadrint(ahc);
	} else if ((intstat & (SEQINT|SCSIINT)) != 0) {

		ahc_pause_bug_fix(ahc);

		if ((intstat & SEQINT) != 0)
			ahc_handle_seqint(ahc, intstat);

		if ((intstat & SCSIINT) != 0)
			ahc_handle_scsiint(ahc, intstat);
	}
	return (1);
}

#endif  /* _AIC7XXX_INLINE_H_ */
