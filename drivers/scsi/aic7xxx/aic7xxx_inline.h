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
void ahc_pause_bug_fix(struct ahc_softc *ahc);
int  ahc_is_paused(struct ahc_softc *ahc);
void ahc_pause(struct ahc_softc *ahc);
void ahc_unpause(struct ahc_softc *ahc);

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
struct ahc_dma_seg *
	ahc_sg_bus_to_virt(struct scb *scb,
			   uint32_t sg_busaddr);
uint32_t
	ahc_sg_virt_to_bus(struct scb *scb,
			   struct ahc_dma_seg *sg);
uint32_t
	ahc_hscb_busaddr(struct ahc_softc *ahc, u_int index);
void	ahc_sync_scb(struct ahc_softc *ahc,
		     struct scb *scb, int op);
void	ahc_sync_sglist(struct ahc_softc *ahc,
			struct scb *scb, int op);
uint32_t
	ahc_targetcmd_offset(struct ahc_softc *ahc,
			     u_int index);

/******************************** Debugging ***********************************/
static __inline char *ahc_name(struct ahc_softc *ahc);

static __inline char *
ahc_name(struct ahc_softc *ahc)
{
	return (ahc->name);
}

/*********************** Miscellaneous Support Functions ***********************/

void	ahc_update_residual(struct ahc_softc *ahc,
			    struct scb *scb);
struct ahc_initiator_tinfo *
	ahc_fetch_transinfo(struct ahc_softc *ahc,
			    char channel, u_int our_id,
			    u_int remote_id,
			    struct ahc_tmode_tstate **tstate);
uint16_t
	ahc_inw(struct ahc_softc *ahc, u_int port);
void	ahc_outw(struct ahc_softc *ahc, u_int port,
		 u_int value);
uint32_t
	ahc_inl(struct ahc_softc *ahc, u_int port);
void	ahc_outl(struct ahc_softc *ahc, u_int port,
		 uint32_t value);
uint64_t
	ahc_inq(struct ahc_softc *ahc, u_int port);
void	ahc_outq(struct ahc_softc *ahc, u_int port,
		 uint64_t value);
struct scb*
	ahc_get_scb(struct ahc_softc *ahc);
void	ahc_free_scb(struct ahc_softc *ahc, struct scb *scb);
struct scb *
	ahc_lookup_scb(struct ahc_softc *ahc, u_int tag);
void	ahc_swap_with_next_hscb(struct ahc_softc *ahc,
				struct scb *scb);
void	ahc_queue_scb(struct ahc_softc *ahc, struct scb *scb);
struct scsi_sense_data *
	ahc_get_sense_buf(struct ahc_softc *ahc,
			  struct scb *scb);
uint32_t
	ahc_get_sense_bufaddr(struct ahc_softc *ahc,
			      struct scb *scb);

/************************** Interrupt Processing ******************************/
void	ahc_sync_qoutfifo(struct ahc_softc *ahc, int op);
void	ahc_sync_tqinfifo(struct ahc_softc *ahc, int op);
u_int	ahc_check_cmdcmpltqueues(struct ahc_softc *ahc);
int	ahc_intr(struct ahc_softc *ahc);

#endif  /* _AIC7XXX_INLINE_H_ */
