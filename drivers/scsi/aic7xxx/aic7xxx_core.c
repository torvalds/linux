/*
 * Core routines and tables shareable across OS platforms.
 *
 * Copyright (c) 1994-2002 Justin T. Gibbs.
 * Copyright (c) 2000-2002 Adaptec Inc.
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
 * $Id: //depot/aic7xxx/aic7xxx/aic7xxx.c#155 $
 */

#include "aic7xxx_osm.h"
#include "aic7xxx_inline.h"
#include "aicasm/aicasm_insformat.h"

/***************************** Lookup Tables **********************************/
static const char *const ahc_chip_names[] = {
	"NONE",
	"aic7770",
	"aic7850",
	"aic7855",
	"aic7859",
	"aic7860",
	"aic7870",
	"aic7880",
	"aic7895",
	"aic7895C",
	"aic7890/91",
	"aic7896/97",
	"aic7892",
	"aic7899"
};

/*
 * Hardware error codes.
 */
struct ahc_hard_error_entry {
        uint8_t errno;
	const char *errmesg;
};

static const struct ahc_hard_error_entry ahc_hard_errors[] = {
	{ ILLHADDR,	"Illegal Host Access" },
	{ ILLSADDR,	"Illegal Sequencer Address referenced" },
	{ ILLOPCODE,	"Illegal Opcode in sequencer program" },
	{ SQPARERR,	"Sequencer Parity Error" },
	{ DPARERR,	"Data-path Parity Error" },
	{ MPARERR,	"Scratch or SCB Memory Parity Error" },
	{ PCIERRSTAT,	"PCI Error detected" },
	{ CIOPARERR,	"CIOBUS Parity Error" },
};
static const u_int num_errors = ARRAY_SIZE(ahc_hard_errors);

static const struct ahc_phase_table_entry ahc_phase_table[] =
{
	{ P_DATAOUT,	MSG_NOOP,		"in Data-out phase"	},
	{ P_DATAIN,	MSG_INITIATOR_DET_ERR,	"in Data-in phase"	},
	{ P_DATAOUT_DT,	MSG_NOOP,		"in DT Data-out phase"	},
	{ P_DATAIN_DT,	MSG_INITIATOR_DET_ERR,	"in DT Data-in phase"	},
	{ P_COMMAND,	MSG_NOOP,		"in Command phase"	},
	{ P_MESGOUT,	MSG_NOOP,		"in Message-out phase"	},
	{ P_STATUS,	MSG_INITIATOR_DET_ERR,	"in Status phase"	},
	{ P_MESGIN,	MSG_PARITY_ERROR,	"in Message-in phase"	},
	{ P_BUSFREE,	MSG_NOOP,		"while idle"		},
	{ 0,		MSG_NOOP,		"in unknown phase"	}
};

/*
 * In most cases we only wish to itterate over real phases, so
 * exclude the last element from the count.
 */
static const u_int num_phases = ARRAY_SIZE(ahc_phase_table) - 1;

/*
 * Valid SCSIRATE values.  (p. 3-17)
 * Provides a mapping of tranfer periods in ns to the proper value to
 * stick in the scsixfer reg.
 */
static const struct ahc_syncrate ahc_syncrates[] =
{
      /* ultra2    fast/ultra  period     rate */
	{ 0x42,      0x000,      9,      "80.0" },
	{ 0x03,      0x000,     10,      "40.0" },
	{ 0x04,      0x000,     11,      "33.0" },
	{ 0x05,      0x100,     12,      "20.0" },
	{ 0x06,      0x110,     15,      "16.0" },
	{ 0x07,      0x120,     18,      "13.4" },
	{ 0x08,      0x000,     25,      "10.0" },
	{ 0x19,      0x010,     31,      "8.0"  },
	{ 0x1a,      0x020,     37,      "6.67" },
	{ 0x1b,      0x030,     43,      "5.7"  },
	{ 0x1c,      0x040,     50,      "5.0"  },
	{ 0x00,      0x050,     56,      "4.4"  },
	{ 0x00,      0x060,     62,      "4.0"  },
	{ 0x00,      0x070,     68,      "3.6"  },
	{ 0x00,      0x000,      0,      NULL   }
};

/* Our Sequencer Program */
#include "aic7xxx_seq.h"

/**************************** Function Declarations ***************************/
static void		ahc_force_renegotiation(struct ahc_softc *ahc,
						struct ahc_devinfo *devinfo);
static struct ahc_tmode_tstate*
			ahc_alloc_tstate(struct ahc_softc *ahc,
					 u_int scsi_id, char channel);
#ifdef AHC_TARGET_MODE
static void		ahc_free_tstate(struct ahc_softc *ahc,
					u_int scsi_id, char channel, int force);
#endif
static const struct ahc_syncrate*
			ahc_devlimited_syncrate(struct ahc_softc *ahc,
					        struct ahc_initiator_tinfo *,
						u_int *period,
						u_int *ppr_options,
						role_t role);
static void		ahc_update_pending_scbs(struct ahc_softc *ahc);
static void		ahc_fetch_devinfo(struct ahc_softc *ahc,
					  struct ahc_devinfo *devinfo);
static void		ahc_scb_devinfo(struct ahc_softc *ahc,
					struct ahc_devinfo *devinfo,
					struct scb *scb);
static void		ahc_assert_atn(struct ahc_softc *ahc);
static void		ahc_setup_initiator_msgout(struct ahc_softc *ahc,
						   struct ahc_devinfo *devinfo,
						   struct scb *scb);
static void		ahc_build_transfer_msg(struct ahc_softc *ahc,
					       struct ahc_devinfo *devinfo);
static void		ahc_construct_sdtr(struct ahc_softc *ahc,
					   struct ahc_devinfo *devinfo,
					   u_int period, u_int offset);
static void		ahc_construct_wdtr(struct ahc_softc *ahc,
					   struct ahc_devinfo *devinfo,
					   u_int bus_width);
static void		ahc_construct_ppr(struct ahc_softc *ahc,
					  struct ahc_devinfo *devinfo,
					  u_int period, u_int offset,
					  u_int bus_width, u_int ppr_options);
static void		ahc_clear_msg_state(struct ahc_softc *ahc);
static void		ahc_handle_proto_violation(struct ahc_softc *ahc);
static void		ahc_handle_message_phase(struct ahc_softc *ahc);
typedef enum {
	AHCMSG_1B,
	AHCMSG_2B,
	AHCMSG_EXT
} ahc_msgtype;
static int		ahc_sent_msg(struct ahc_softc *ahc, ahc_msgtype type,
				     u_int msgval, int full);
static int		ahc_parse_msg(struct ahc_softc *ahc,
				      struct ahc_devinfo *devinfo);
static int		ahc_handle_msg_reject(struct ahc_softc *ahc,
					      struct ahc_devinfo *devinfo);
static void		ahc_handle_ign_wide_residue(struct ahc_softc *ahc,
						struct ahc_devinfo *devinfo);
static void		ahc_reinitialize_dataptrs(struct ahc_softc *ahc);
static void		ahc_handle_devreset(struct ahc_softc *ahc,
					    struct ahc_devinfo *devinfo,
					    cam_status status, char *message,
					    int verbose_level);
#ifdef AHC_TARGET_MODE
static void		ahc_setup_target_msgin(struct ahc_softc *ahc,
					       struct ahc_devinfo *devinfo,
					       struct scb *scb);
#endif

static bus_dmamap_callback_t	ahc_dmamap_cb; 
static void		ahc_build_free_scb_list(struct ahc_softc *ahc);
static int		ahc_init_scbdata(struct ahc_softc *ahc);
static void		ahc_fini_scbdata(struct ahc_softc *ahc);
static void		ahc_qinfifo_requeue(struct ahc_softc *ahc,
					    struct scb *prev_scb,
					    struct scb *scb);
static int		ahc_qinfifo_count(struct ahc_softc *ahc);
static u_int		ahc_rem_scb_from_disc_list(struct ahc_softc *ahc,
						   u_int prev, u_int scbptr);
static void		ahc_add_curscb_to_free_list(struct ahc_softc *ahc);
static u_int		ahc_rem_wscb(struct ahc_softc *ahc,
				     u_int scbpos, u_int prev);
static void		ahc_reset_current_bus(struct ahc_softc *ahc);
#ifdef AHC_DUMP_SEQ
static void		ahc_dumpseq(struct ahc_softc *ahc);
#endif
static int		ahc_loadseq(struct ahc_softc *ahc);
static int		ahc_check_patch(struct ahc_softc *ahc,
					const struct patch **start_patch,
					u_int start_instr, u_int *skip_addr);
static void		ahc_download_instr(struct ahc_softc *ahc,
					   u_int instrptr, uint8_t *dconsts);
#ifdef AHC_TARGET_MODE
static void		ahc_queue_lstate_event(struct ahc_softc *ahc,
					       struct ahc_tmode_lstate *lstate,
					       u_int initiator_id,
					       u_int event_type,
					       u_int event_arg);
static void		ahc_update_scsiid(struct ahc_softc *ahc,
					  u_int targid_mask);
static int		ahc_handle_target_cmd(struct ahc_softc *ahc,
					      struct target_cmd *cmd);
#endif

static u_int		ahc_index_busy_tcl(struct ahc_softc *ahc, u_int tcl);
static void		ahc_unbusy_tcl(struct ahc_softc *ahc, u_int tcl);
static void		ahc_busy_tcl(struct ahc_softc *ahc,
				     u_int tcl, u_int busyid);

/************************** SCB and SCB queue management **********************/
static void		ahc_run_untagged_queues(struct ahc_softc *ahc);
static void		ahc_run_untagged_queue(struct ahc_softc *ahc,
					       struct scb_tailq *queue);

/****************************** Initialization ********************************/
static void		 ahc_alloc_scbs(struct ahc_softc *ahc);
static void		 ahc_shutdown(void *arg);

/*************************** Interrupt Services *******************************/
static void		ahc_clear_intstat(struct ahc_softc *ahc);
static void		ahc_run_qoutfifo(struct ahc_softc *ahc);
#ifdef AHC_TARGET_MODE
static void		ahc_run_tqinfifo(struct ahc_softc *ahc, int paused);
#endif
static void		ahc_handle_brkadrint(struct ahc_softc *ahc);
static void		ahc_handle_seqint(struct ahc_softc *ahc, u_int intstat);
static void		ahc_handle_scsiint(struct ahc_softc *ahc,
					   u_int intstat);
static void		ahc_clear_critical_section(struct ahc_softc *ahc);

/***************************** Error Recovery *********************************/
static void		ahc_freeze_devq(struct ahc_softc *ahc, struct scb *scb);
static int		ahc_abort_scbs(struct ahc_softc *ahc, int target,
				       char channel, int lun, u_int tag,
				       role_t role, uint32_t status);
static void		ahc_calc_residual(struct ahc_softc *ahc,
					  struct scb *scb);

/*********************** Untagged Transaction Routines ************************/
static inline void	ahc_freeze_untagged_queues(struct ahc_softc *ahc);
static inline void	ahc_release_untagged_queues(struct ahc_softc *ahc);

/*
 * Block our completion routine from starting the next untagged
 * transaction for this target or target lun.
 */
static inline void
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
static inline void
ahc_release_untagged_queues(struct ahc_softc *ahc)
{
	if ((ahc->flags & AHC_SCB_BTT) == 0) {
		ahc->untagged_queue_lock--;
		if (ahc->untagged_queue_lock == 0)
			ahc_run_untagged_queues(ahc);
	}
}

/************************* Sequencer Execution Control ************************/
/*
 * Work around any chip bugs related to halting sequencer execution.
 * On Ultra2 controllers, we must clear the CIOBUS stretch signal by
 * reading a register that will set this signal and deassert it.
 * Without this workaround, if the chip is paused, by an interrupt or
 * manual pause while accessing scb ram, accesses to certain registers
 * will hang the system (infinite pci retries).
 */
static void
ahc_pause_bug_fix(struct ahc_softc *ahc)
{
	if ((ahc->features & AHC_ULTRA2) != 0)
		(void)ahc_inb(ahc, CCSCBCTL);
}

/*
 * Determine whether the sequencer has halted code execution.
 * Returns non-zero status if the sequencer is stopped.
 */
int
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
void
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
void
ahc_unpause(struct ahc_softc *ahc)
{
	if ((ahc_inb(ahc, INTSTAT) & (SCSIINT | SEQINT | BRKADRINT)) == 0)
		ahc_outb(ahc, HCNTRL, ahc->unpause);
}

/************************** Memory mapping routines ***************************/
static struct ahc_dma_seg *
ahc_sg_bus_to_virt(struct scb *scb, uint32_t sg_busaddr)
{
	int sg_index;

	sg_index = (sg_busaddr - scb->sg_list_phys)/sizeof(struct ahc_dma_seg);
	/* sg_list_phys points to entry 1, not 0 */
	sg_index++;

	return (&scb->sg_list[sg_index]);
}

static uint32_t
ahc_sg_virt_to_bus(struct scb *scb, struct ahc_dma_seg *sg)
{
	int sg_index;

	/* sg_list_phys points to entry 1, not 0 */
	sg_index = sg - &scb->sg_list[1];

	return (scb->sg_list_phys + (sg_index * sizeof(*scb->sg_list)));
}

static uint32_t
ahc_hscb_busaddr(struct ahc_softc *ahc, u_int index)
{
	return (ahc->scb_data->hscb_busaddr
		+ (sizeof(struct hardware_scb) * index));
}

static void
ahc_sync_scb(struct ahc_softc *ahc, struct scb *scb, int op)
{
	ahc_dmamap_sync(ahc, ahc->scb_data->hscb_dmat,
			ahc->scb_data->hscb_dmamap,
			/*offset*/(scb->hscb - ahc->hscbs) * sizeof(*scb->hscb),
			/*len*/sizeof(*scb->hscb), op);
}

void
ahc_sync_sglist(struct ahc_softc *ahc, struct scb *scb, int op)
{
	if (scb->sg_count == 0)
		return;

	ahc_dmamap_sync(ahc, ahc->scb_data->sg_dmat, scb->sg_map->sg_dmamap,
			/*offset*/(scb->sg_list - scb->sg_map->sg_vaddr)
				* sizeof(struct ahc_dma_seg),
			/*len*/sizeof(struct ahc_dma_seg) * scb->sg_count, op);
}

#ifdef AHC_TARGET_MODE
static uint32_t
ahc_targetcmd_offset(struct ahc_softc *ahc, u_int index)
{
	return (((uint8_t *)&ahc->targetcmds[index]) - ahc->qoutfifo);
}
#endif

/*********************** Miscellaneous Support Functions ***********************/
/*
 * Determine whether the sequencer reported a residual
 * for this SCB/transaction.
 */
static void
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
struct ahc_initiator_tinfo *
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

uint16_t
ahc_inw(struct ahc_softc *ahc, u_int port)
{
	uint16_t r = ahc_inb(ahc, port+1) << 8;
	return r | ahc_inb(ahc, port);
}

void
ahc_outw(struct ahc_softc *ahc, u_int port, u_int value)
{
	ahc_outb(ahc, port, value & 0xFF);
	ahc_outb(ahc, port+1, (value >> 8) & 0xFF);
}

uint32_t
ahc_inl(struct ahc_softc *ahc, u_int port)
{
	return ((ahc_inb(ahc, port))
	      | (ahc_inb(ahc, port+1) << 8)
	      | (ahc_inb(ahc, port+2) << 16)
	      | (ahc_inb(ahc, port+3) << 24));
}

void
ahc_outl(struct ahc_softc *ahc, u_int port, uint32_t value)
{
	ahc_outb(ahc, port, (value) & 0xFF);
	ahc_outb(ahc, port+1, ((value) >> 8) & 0xFF);
	ahc_outb(ahc, port+2, ((value) >> 16) & 0xFF);
	ahc_outb(ahc, port+3, ((value) >> 24) & 0xFF);
}

uint64_t
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

void
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
struct scb *
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
void
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

struct scb *
ahc_lookup_scb(struct ahc_softc *ahc, u_int tag)
{
	struct scb* scb;

	scb = ahc->scb_data->scbindex[tag];
	if (scb != NULL)
		ahc_sync_scb(ahc, scb,
			     BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	return (scb);
}

static void
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
void
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

struct scsi_sense_data *
ahc_get_sense_buf(struct ahc_softc *ahc, struct scb *scb)
{
	int offset;

	offset = scb - ahc->scb_data->scbarray;
	return (&ahc->scb_data->sense[offset]);
}

static uint32_t
ahc_get_sense_bufaddr(struct ahc_softc *ahc, struct scb *scb)
{
	int offset;

	offset = scb - ahc->scb_data->scbarray;
	return (ahc->scb_data->sense_busaddr
	      + (offset * sizeof(struct scsi_sense_data)));
}

/************************** Interrupt Processing ******************************/
static void
ahc_sync_qoutfifo(struct ahc_softc *ahc, int op)
{
	ahc_dmamap_sync(ahc, ahc->shared_data_dmat, ahc->shared_data_dmamap,
			/*offset*/0, /*len*/256, op);
}

static void
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
static u_int
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
int
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

/************************* Sequencer Execution Control ************************/
/*
 * Restart the sequencer program from address zero
 */
static void
ahc_restart(struct ahc_softc *ahc)
{
	uint8_t	sblkctl;

	ahc_pause(ahc);

	/* No more pending messages. */
	ahc_clear_msg_state(ahc);

	ahc_outb(ahc, SCSISIGO, 0);		/* De-assert BSY */
	ahc_outb(ahc, MSG_OUT, MSG_NOOP);	/* No message to send */
	ahc_outb(ahc, SXFRCTL1, ahc_inb(ahc, SXFRCTL1) & ~BITBUCKET);
	ahc_outb(ahc, LASTPHASE, P_BUSFREE);
	ahc_outb(ahc, SAVED_SCSIID, 0xFF);
	ahc_outb(ahc, SAVED_LUN, 0xFF);

	/*
	 * Ensure that the sequencer's idea of TQINPOS
	 * matches our own.  The sequencer increments TQINPOS
	 * only after it sees a DMA complete and a reset could
	 * occur before the increment leaving the kernel to believe
	 * the command arrived but the sequencer to not.
	 */
	ahc_outb(ahc, TQINPOS, ahc->tqinfifonext);

	/* Always allow reselection */
	ahc_outb(ahc, SCSISEQ,
		 ahc_inb(ahc, SCSISEQ_TEMPLATE) & (ENSELI|ENRSELI|ENAUTOATNP));
	if ((ahc->features & AHC_CMD_CHAN) != 0) {
		/* Ensure that no DMA operations are in progress */
		ahc_outb(ahc, CCSCBCNT, 0);
		ahc_outb(ahc, CCSGCTL, 0);
		ahc_outb(ahc, CCSCBCTL, 0);
	}
	/*
	 * If we were in the process of DMA'ing SCB data into
	 * an SCB, replace that SCB on the free list.  This prevents
	 * an SCB leak.
	 */
	if ((ahc_inb(ahc, SEQ_FLAGS2) & SCB_DMA) != 0) {
		ahc_add_curscb_to_free_list(ahc);
		ahc_outb(ahc, SEQ_FLAGS2,
			 ahc_inb(ahc, SEQ_FLAGS2) & ~SCB_DMA);
	}

	/*
	 * Clear any pending sequencer interrupt.  It is no
	 * longer relevant since we're resetting the Program
	 * Counter.
	 */
	ahc_outb(ahc, CLRINT, CLRSEQINT);

	ahc_outb(ahc, MWI_RESIDUAL, 0);
	ahc_outb(ahc, SEQCTL, ahc->seqctl);
	ahc_outb(ahc, SEQADDR0, 0);
	ahc_outb(ahc, SEQADDR1, 0);

	/*
	 * Take the LED out of diagnostic mode on PM resume, too
	 */
	sblkctl = ahc_inb(ahc, SBLKCTL);
	ahc_outb(ahc, SBLKCTL, (sblkctl & ~(DIAGLEDEN|DIAGLEDON)));

	ahc_unpause(ahc);
}

/************************* Input/Output Queues ********************************/
static void
ahc_run_qoutfifo(struct ahc_softc *ahc)
{
	struct scb *scb;
	u_int  scb_index;

	ahc_sync_qoutfifo(ahc, BUS_DMASYNC_POSTREAD);
	while (ahc->qoutfifo[ahc->qoutfifonext] != SCB_LIST_NULL) {

		scb_index = ahc->qoutfifo[ahc->qoutfifonext];
		if ((ahc->qoutfifonext & 0x03) == 0x03) {
			u_int modnext;

			/*
			 * Clear 32bits of QOUTFIFO at a time
			 * so that we don't clobber an incoming
			 * byte DMA to the array on architectures
			 * that only support 32bit load and store
			 * operations.
			 */
			modnext = ahc->qoutfifonext & ~0x3;
			*((uint32_t *)(&ahc->qoutfifo[modnext])) = 0xFFFFFFFFUL;
			ahc_dmamap_sync(ahc, ahc->shared_data_dmat,
					ahc->shared_data_dmamap,
					/*offset*/modnext, /*len*/4,
					BUS_DMASYNC_PREREAD);
		}
		ahc->qoutfifonext++;

		scb = ahc_lookup_scb(ahc, scb_index);
		if (scb == NULL) {
			printk("%s: WARNING no command for scb %d "
			       "(cmdcmplt)\nQOUTPOS = %d\n",
			       ahc_name(ahc), scb_index,
			       (ahc->qoutfifonext - 1) & 0xFF);
			continue;
		}

		/*
		 * Save off the residual
		 * if there is one.
		 */
		ahc_update_residual(ahc, scb);
		ahc_done(ahc, scb);
	}
}

static void
ahc_run_untagged_queues(struct ahc_softc *ahc)
{
	int i;

	for (i = 0; i < 16; i++)
		ahc_run_untagged_queue(ahc, &ahc->untagged_queues[i]);
}

static void
ahc_run_untagged_queue(struct ahc_softc *ahc, struct scb_tailq *queue)
{
	struct scb *scb;

	if (ahc->untagged_queue_lock != 0)
		return;

	if ((scb = TAILQ_FIRST(queue)) != NULL
	 && (scb->flags & SCB_ACTIVE) == 0) {
		scb->flags |= SCB_ACTIVE;
		ahc_queue_scb(ahc, scb);
	}
}

/************************* Interrupt Handling *********************************/
static void
ahc_handle_brkadrint(struct ahc_softc *ahc)
{
	/*
	 * We upset the sequencer :-(
	 * Lookup the error message
	 */
	int i;
	int error;

	error = ahc_inb(ahc, ERROR);
	for (i = 0; error != 1 && i < num_errors; i++)
		error >>= 1;
	printk("%s: brkadrint, %s at seqaddr = 0x%x\n",
	       ahc_name(ahc), ahc_hard_errors[i].errmesg,
	       ahc_inb(ahc, SEQADDR0) |
	       (ahc_inb(ahc, SEQADDR1) << 8));

	ahc_dump_card_state(ahc);

	/* Tell everyone that this HBA is no longer available */
	ahc_abort_scbs(ahc, CAM_TARGET_WILDCARD, ALL_CHANNELS,
		       CAM_LUN_WILDCARD, SCB_LIST_NULL, ROLE_UNKNOWN,
		       CAM_NO_HBA);

	/* Disable all interrupt sources by resetting the controller */
	ahc_shutdown(ahc);
}

static void
ahc_handle_seqint(struct ahc_softc *ahc, u_int intstat)
{
	struct scb *scb;
	struct ahc_devinfo devinfo;
	
	ahc_fetch_devinfo(ahc, &devinfo);

	/*
	 * Clear the upper byte that holds SEQINT status
	 * codes and clear the SEQINT bit. We will unpause
	 * the sequencer, if appropriate, after servicing
	 * the request.
	 */
	ahc_outb(ahc, CLRINT, CLRSEQINT);
	switch (intstat & SEQINT_MASK) {
	case BAD_STATUS:
	{
		u_int  scb_index;
		struct hardware_scb *hscb;

		/*
		 * Set the default return value to 0 (don't
		 * send sense).  The sense code will change
		 * this if needed.
		 */
		ahc_outb(ahc, RETURN_1, 0);

		/*
		 * The sequencer will notify us when a command
		 * has an error that would be of interest to
		 * the kernel.  This allows us to leave the sequencer
		 * running in the common case of command completes
		 * without error.  The sequencer will already have
		 * dma'd the SCB back up to us, so we can reference
		 * the in kernel copy directly.
		 */
		scb_index = ahc_inb(ahc, SCB_TAG);
		scb = ahc_lookup_scb(ahc, scb_index);
		if (scb == NULL) {
			ahc_print_devinfo(ahc, &devinfo);
			printk("ahc_intr - referenced scb "
			       "not valid during seqint 0x%x scb(%d)\n",
			       intstat, scb_index);
			ahc_dump_card_state(ahc);
			panic("for safety");
			goto unpause;
		}

		hscb = scb->hscb; 

		/* Don't want to clobber the original sense code */
		if ((scb->flags & SCB_SENSE) != 0) {
			/*
			 * Clear the SCB_SENSE Flag and have
			 * the sequencer do a normal command
			 * complete.
			 */
			scb->flags &= ~SCB_SENSE;
			ahc_set_transaction_status(scb, CAM_AUTOSENSE_FAIL);
			break;
		}
		ahc_set_transaction_status(scb, CAM_SCSI_STATUS_ERROR);
		/* Freeze the queue until the client sees the error. */
		ahc_freeze_devq(ahc, scb);
		ahc_freeze_scb(scb);
		ahc_set_scsi_status(scb, hscb->shared_data.status.scsi_status);
		switch (hscb->shared_data.status.scsi_status) {
		case SCSI_STATUS_OK:
			printk("%s: Interrupted for status of 0???\n",
			       ahc_name(ahc));
			break;
		case SCSI_STATUS_CMD_TERMINATED:
		case SCSI_STATUS_CHECK_COND:
		{
			struct ahc_dma_seg *sg;
			struct scsi_sense *sc;
			struct ahc_initiator_tinfo *targ_info;
			struct ahc_tmode_tstate *tstate;
			struct ahc_transinfo *tinfo;
#ifdef AHC_DEBUG
			if (ahc_debug & AHC_SHOW_SENSE) {
				ahc_print_path(ahc, scb);
				printk("SCB %d: requests Check Status\n",
				       scb->hscb->tag);
			}
#endif

			if (ahc_perform_autosense(scb) == 0)
				break;

			targ_info = ahc_fetch_transinfo(ahc,
							devinfo.channel,
							devinfo.our_scsiid,
							devinfo.target,
							&tstate);
			tinfo = &targ_info->curr;
			sg = scb->sg_list;
			sc = (struct scsi_sense *)(&hscb->shared_data.cdb); 
			/*
			 * Save off the residual if there is one.
			 */
			ahc_update_residual(ahc, scb);
#ifdef AHC_DEBUG
			if (ahc_debug & AHC_SHOW_SENSE) {
				ahc_print_path(ahc, scb);
				printk("Sending Sense\n");
			}
#endif
			sg->addr = ahc_get_sense_bufaddr(ahc, scb);
			sg->len = ahc_get_sense_bufsize(ahc, scb);
			sg->len |= AHC_DMA_LAST_SEG;

			/* Fixup byte order */
			sg->addr = ahc_htole32(sg->addr);
			sg->len = ahc_htole32(sg->len);

			sc->opcode = REQUEST_SENSE;
			sc->byte2 = 0;
			if (tinfo->protocol_version <= SCSI_REV_2
			 && SCB_GET_LUN(scb) < 8)
				sc->byte2 = SCB_GET_LUN(scb) << 5;
			sc->unused[0] = 0;
			sc->unused[1] = 0;
			sc->length = sg->len;
			sc->control = 0;

			/*
			 * We can't allow the target to disconnect.
			 * This will be an untagged transaction and
			 * having the target disconnect will make this
			 * transaction indestinguishable from outstanding
			 * tagged transactions.
			 */
			hscb->control = 0;

			/*
			 * This request sense could be because the
			 * the device lost power or in some other
			 * way has lost our transfer negotiations.
			 * Renegotiate if appropriate.  Unit attention
			 * errors will be reported before any data
			 * phases occur.
			 */
			if (ahc_get_residual(scb) 
			 == ahc_get_transfer_length(scb)) {
				ahc_update_neg_request(ahc, &devinfo,
						       tstate, targ_info,
						       AHC_NEG_IF_NON_ASYNC);
			}
			if (tstate->auto_negotiate & devinfo.target_mask) {
				hscb->control |= MK_MESSAGE;
				scb->flags &= ~SCB_NEGOTIATE;
				scb->flags |= SCB_AUTO_NEGOTIATE;
			}
			hscb->cdb_len = sizeof(*sc);
			hscb->dataptr = sg->addr; 
			hscb->datacnt = sg->len;
			hscb->sgptr = scb->sg_list_phys | SG_FULL_RESID;
			hscb->sgptr = ahc_htole32(hscb->sgptr);
			scb->sg_count = 1;
			scb->flags |= SCB_SENSE;
			ahc_qinfifo_requeue_tail(ahc, scb);
			ahc_outb(ahc, RETURN_1, SEND_SENSE);
			/*
			 * Ensure we have enough time to actually
			 * retrieve the sense.
			 */
			ahc_scb_timer_reset(scb, 5 * 1000000);
			break;
		}
		default:
			break;
		}
		break;
	}
	case NO_MATCH:
	{
		/* Ensure we don't leave the selection hardware on */
		ahc_outb(ahc, SCSISEQ,
			 ahc_inb(ahc, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP));

		printk("%s:%c:%d: no active SCB for reconnecting "
		       "target - issuing BUS DEVICE RESET\n",
		       ahc_name(ahc), devinfo.channel, devinfo.target);
		printk("SAVED_SCSIID == 0x%x, SAVED_LUN == 0x%x, "
		       "ARG_1 == 0x%x ACCUM = 0x%x\n",
		       ahc_inb(ahc, SAVED_SCSIID), ahc_inb(ahc, SAVED_LUN),
		       ahc_inb(ahc, ARG_1), ahc_inb(ahc, ACCUM));
		printk("SEQ_FLAGS == 0x%x, SCBPTR == 0x%x, BTT == 0x%x, "
		       "SINDEX == 0x%x\n",
		       ahc_inb(ahc, SEQ_FLAGS), ahc_inb(ahc, SCBPTR),
		       ahc_index_busy_tcl(ahc,
			    BUILD_TCL(ahc_inb(ahc, SAVED_SCSIID),
				      ahc_inb(ahc, SAVED_LUN))),
		       ahc_inb(ahc, SINDEX));
		printk("SCSIID == 0x%x, SCB_SCSIID == 0x%x, SCB_LUN == 0x%x, "
		       "SCB_TAG == 0x%x, SCB_CONTROL == 0x%x\n",
		       ahc_inb(ahc, SCSIID), ahc_inb(ahc, SCB_SCSIID),
		       ahc_inb(ahc, SCB_LUN), ahc_inb(ahc, SCB_TAG),
		       ahc_inb(ahc, SCB_CONTROL));
		printk("SCSIBUSL == 0x%x, SCSISIGI == 0x%x\n",
		       ahc_inb(ahc, SCSIBUSL), ahc_inb(ahc, SCSISIGI));
		printk("SXFRCTL0 == 0x%x\n", ahc_inb(ahc, SXFRCTL0));
		printk("SEQCTL == 0x%x\n", ahc_inb(ahc, SEQCTL));
		ahc_dump_card_state(ahc);
		ahc->msgout_buf[0] = MSG_BUS_DEV_RESET;
		ahc->msgout_len = 1;
		ahc->msgout_index = 0;
		ahc->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
		ahc_outb(ahc, MSG_OUT, HOST_MSG);
		ahc_assert_atn(ahc);
		break;
	}
	case SEND_REJECT: 
	{
		u_int rejbyte = ahc_inb(ahc, ACCUM);
		printk("%s:%c:%d: Warning - unknown message received from "
		       "target (0x%x).  Rejecting\n", 
		       ahc_name(ahc), devinfo.channel, devinfo.target, rejbyte);
		break; 
	}
	case PROTO_VIOLATION:
	{
		ahc_handle_proto_violation(ahc);
		break;
	}
	case IGN_WIDE_RES:
		ahc_handle_ign_wide_residue(ahc, &devinfo);
		break;
	case PDATA_REINIT:
		ahc_reinitialize_dataptrs(ahc);
		break;
	case BAD_PHASE:
	{
		u_int lastphase;

		lastphase = ahc_inb(ahc, LASTPHASE);
		printk("%s:%c:%d: unknown scsi bus phase %x, "
		       "lastphase = 0x%x.  Attempting to continue\n",
		       ahc_name(ahc), devinfo.channel, devinfo.target,
		       lastphase, ahc_inb(ahc, SCSISIGI));
		break;
	}
	case MISSED_BUSFREE:
	{
		u_int lastphase;

		lastphase = ahc_inb(ahc, LASTPHASE);
		printk("%s:%c:%d: Missed busfree. "
		       "Lastphase = 0x%x, Curphase = 0x%x\n",
		       ahc_name(ahc), devinfo.channel, devinfo.target,
		       lastphase, ahc_inb(ahc, SCSISIGI));
		ahc_restart(ahc);
		return;
	}
	case HOST_MSG_LOOP:
	{
		/*
		 * The sequencer has encountered a message phase
		 * that requires host assistance for completion.
		 * While handling the message phase(s), we will be
		 * notified by the sequencer after each byte is
		 * transferred so we can track bus phase changes.
		 *
		 * If this is the first time we've seen a HOST_MSG_LOOP
		 * interrupt, initialize the state of the host message
		 * loop.
		 */
		if (ahc->msg_type == MSG_TYPE_NONE) {
			struct scb *scb;
			u_int scb_index;
			u_int bus_phase;

			bus_phase = ahc_inb(ahc, SCSISIGI) & PHASE_MASK;
			if (bus_phase != P_MESGIN
			 && bus_phase != P_MESGOUT) {
				printk("ahc_intr: HOST_MSG_LOOP bad "
				       "phase 0x%x\n",
				      bus_phase);
				/*
				 * Probably transitioned to bus free before
				 * we got here.  Just punt the message.
				 */
				ahc_clear_intstat(ahc);
				ahc_restart(ahc);
				return;
			}

			scb_index = ahc_inb(ahc, SCB_TAG);
			scb = ahc_lookup_scb(ahc, scb_index);
			if (devinfo.role == ROLE_INITIATOR) {
				if (bus_phase == P_MESGOUT) {
					if (scb == NULL)
						panic("HOST_MSG_LOOP with "
						      "invalid SCB %x\n",
						      scb_index);

					ahc_setup_initiator_msgout(ahc,
								   &devinfo,
								   scb);
				} else {
					ahc->msg_type =
					    MSG_TYPE_INITIATOR_MSGIN;
					ahc->msgin_index = 0;
				}
			}
#ifdef AHC_TARGET_MODE
			else {
				if (bus_phase == P_MESGOUT) {
					ahc->msg_type =
					    MSG_TYPE_TARGET_MSGOUT;
					ahc->msgin_index = 0;
				}
				else 
					ahc_setup_target_msgin(ahc,
							       &devinfo,
							       scb);
			}
#endif
		}

		ahc_handle_message_phase(ahc);
		break;
	}
	case PERR_DETECTED:
	{
		/*
		 * If we've cleared the parity error interrupt
		 * but the sequencer still believes that SCSIPERR
		 * is true, it must be that the parity error is
		 * for the currently presented byte on the bus,
		 * and we are not in a phase (data-in) where we will
		 * eventually ack this byte.  Ack the byte and
		 * throw it away in the hope that the target will
		 * take us to message out to deliver the appropriate
		 * error message.
		 */
		if ((intstat & SCSIINT) == 0
		 && (ahc_inb(ahc, SSTAT1) & SCSIPERR) != 0) {

			if ((ahc->features & AHC_DT) == 0) {
				u_int curphase;

				/*
				 * The hardware will only let you ack bytes
				 * if the expected phase in SCSISIGO matches
				 * the current phase.  Make sure this is
				 * currently the case.
				 */
				curphase = ahc_inb(ahc, SCSISIGI) & PHASE_MASK;
				ahc_outb(ahc, LASTPHASE, curphase);
				ahc_outb(ahc, SCSISIGO, curphase);
			}
			if ((ahc_inb(ahc, SCSISIGI) & (CDI|MSGI)) == 0) {
				int wait;

				/*
				 * In a data phase.  Faster to bitbucket
				 * the data than to individually ack each
				 * byte.  This is also the only strategy
				 * that will work with AUTOACK enabled.
				 */
				ahc_outb(ahc, SXFRCTL1,
					 ahc_inb(ahc, SXFRCTL1) | BITBUCKET);
				wait = 5000;
				while (--wait != 0) {
					if ((ahc_inb(ahc, SCSISIGI)
					  & (CDI|MSGI)) != 0)
						break;
					ahc_delay(100);
				}
				ahc_outb(ahc, SXFRCTL1,
					 ahc_inb(ahc, SXFRCTL1) & ~BITBUCKET);
				if (wait == 0) {
					struct	scb *scb;
					u_int	scb_index;

					ahc_print_devinfo(ahc, &devinfo);
					printk("Unable to clear parity error.  "
					       "Resetting bus.\n");
					scb_index = ahc_inb(ahc, SCB_TAG);
					scb = ahc_lookup_scb(ahc, scb_index);
					if (scb != NULL)
						ahc_set_transaction_status(scb,
						    CAM_UNCOR_PARITY);
					ahc_reset_channel(ahc, devinfo.channel, 
							  /*init reset*/TRUE);
				}
			} else {
				ahc_inb(ahc, SCSIDATL);
			}
		}
		break;
	}
	case DATA_OVERRUN:
	{
		/*
		 * When the sequencer detects an overrun, it
		 * places the controller in "BITBUCKET" mode
		 * and allows the target to complete its transfer.
		 * Unfortunately, none of the counters get updated
		 * when the controller is in this mode, so we have
		 * no way of knowing how large the overrun was.
		 */
		u_int scbindex = ahc_inb(ahc, SCB_TAG);
		u_int lastphase = ahc_inb(ahc, LASTPHASE);
		u_int i;

		scb = ahc_lookup_scb(ahc, scbindex);
		for (i = 0; i < num_phases; i++) {
			if (lastphase == ahc_phase_table[i].phase)
				break;
		}
		ahc_print_path(ahc, scb);
		printk("data overrun detected %s."
		       "  Tag == 0x%x.\n",
		       ahc_phase_table[i].phasemsg,
  		       scb->hscb->tag);
		ahc_print_path(ahc, scb);
		printk("%s seen Data Phase.  Length = %ld.  NumSGs = %d.\n",
		       ahc_inb(ahc, SEQ_FLAGS) & DPHASE ? "Have" : "Haven't",
		       ahc_get_transfer_length(scb), scb->sg_count);
		if (scb->sg_count > 0) {
			for (i = 0; i < scb->sg_count; i++) {

				printk("sg[%d] - Addr 0x%x%x : Length %d\n",
				       i,
				       (ahc_le32toh(scb->sg_list[i].len) >> 24
				        & SG_HIGH_ADDR_BITS),
				       ahc_le32toh(scb->sg_list[i].addr),
				       ahc_le32toh(scb->sg_list[i].len)
				       & AHC_SG_LEN_MASK);
			}
		}
		/*
		 * Set this and it will take effect when the
		 * target does a command complete.
		 */
		ahc_freeze_devq(ahc, scb);
		if ((scb->flags & SCB_SENSE) == 0) {
			ahc_set_transaction_status(scb, CAM_DATA_RUN_ERR);
		} else {
			scb->flags &= ~SCB_SENSE;
			ahc_set_transaction_status(scb, CAM_AUTOSENSE_FAIL);
		}
		ahc_freeze_scb(scb);

		if ((ahc->features & AHC_ULTRA2) != 0) {
			/*
			 * Clear the channel in case we return
			 * to data phase later.
			 */
			ahc_outb(ahc, SXFRCTL0,
				 ahc_inb(ahc, SXFRCTL0) | CLRSTCNT|CLRCHN);
			ahc_outb(ahc, SXFRCTL0,
				 ahc_inb(ahc, SXFRCTL0) | CLRSTCNT|CLRCHN);
		}
		if ((ahc->flags & AHC_39BIT_ADDRESSING) != 0) {
			u_int dscommand1;

			/* Ensure HHADDR is 0 for future DMA operations. */
			dscommand1 = ahc_inb(ahc, DSCOMMAND1);
			ahc_outb(ahc, DSCOMMAND1, dscommand1 | HADDLDSEL0);
			ahc_outb(ahc, HADDR, 0);
			ahc_outb(ahc, DSCOMMAND1, dscommand1);
		}
		break;
	}
	case MKMSG_FAILED:
	{
		u_int scbindex;

		printk("%s:%c:%d:%d: Attempt to issue message failed\n",
		       ahc_name(ahc), devinfo.channel, devinfo.target,
		       devinfo.lun);
		scbindex = ahc_inb(ahc, SCB_TAG);
		scb = ahc_lookup_scb(ahc, scbindex);
		if (scb != NULL
		 && (scb->flags & SCB_RECOVERY_SCB) != 0)
			/*
			 * Ensure that we didn't put a second instance of this
			 * SCB into the QINFIFO.
			 */
			ahc_search_qinfifo(ahc, SCB_GET_TARGET(ahc, scb),
					   SCB_GET_CHANNEL(ahc, scb),
					   SCB_GET_LUN(scb), scb->hscb->tag,
					   ROLE_INITIATOR, /*status*/0,
					   SEARCH_REMOVE);
		break;
	}
	case NO_FREE_SCB:
	{
		printk("%s: No free or disconnected SCBs\n", ahc_name(ahc));
		ahc_dump_card_state(ahc);
		panic("for safety");
		break;
	}
	case SCB_MISMATCH:
	{
		u_int scbptr;

		scbptr = ahc_inb(ahc, SCBPTR);
		printk("Bogus TAG after DMA.  SCBPTR %d, tag %d, our tag %d\n",
		       scbptr, ahc_inb(ahc, ARG_1),
		       ahc->scb_data->hscbs[scbptr].tag);
		ahc_dump_card_state(ahc);
		panic("for safety");
		break;
	}
	case OUT_OF_RANGE:
	{
		printk("%s: BTT calculation out of range\n", ahc_name(ahc));
		printk("SAVED_SCSIID == 0x%x, SAVED_LUN == 0x%x, "
		       "ARG_1 == 0x%x ACCUM = 0x%x\n",
		       ahc_inb(ahc, SAVED_SCSIID), ahc_inb(ahc, SAVED_LUN),
		       ahc_inb(ahc, ARG_1), ahc_inb(ahc, ACCUM));
		printk("SEQ_FLAGS == 0x%x, SCBPTR == 0x%x, BTT == 0x%x, "
		       "SINDEX == 0x%x\n, A == 0x%x\n",
		       ahc_inb(ahc, SEQ_FLAGS), ahc_inb(ahc, SCBPTR),
		       ahc_index_busy_tcl(ahc,
			    BUILD_TCL(ahc_inb(ahc, SAVED_SCSIID),
				      ahc_inb(ahc, SAVED_LUN))),
		       ahc_inb(ahc, SINDEX),
		       ahc_inb(ahc, ACCUM));
		printk("SCSIID == 0x%x, SCB_SCSIID == 0x%x, SCB_LUN == 0x%x, "
		       "SCB_TAG == 0x%x, SCB_CONTROL == 0x%x\n",
		       ahc_inb(ahc, SCSIID), ahc_inb(ahc, SCB_SCSIID),
		       ahc_inb(ahc, SCB_LUN), ahc_inb(ahc, SCB_TAG),
		       ahc_inb(ahc, SCB_CONTROL));
		printk("SCSIBUSL == 0x%x, SCSISIGI == 0x%x\n",
		       ahc_inb(ahc, SCSIBUSL), ahc_inb(ahc, SCSISIGI));
		ahc_dump_card_state(ahc);
		panic("for safety");
		break;
	}
	default:
		printk("ahc_intr: seqint, "
		       "intstat == 0x%x, scsisigi = 0x%x\n",
		       intstat, ahc_inb(ahc, SCSISIGI));
		break;
	}
unpause:
	/*
	 *  The sequencer is paused immediately on
	 *  a SEQINT, so we should restart it when
	 *  we're done.
	 */
	ahc_unpause(ahc);
}

static void
ahc_handle_scsiint(struct ahc_softc *ahc, u_int intstat)
{
	u_int	scb_index;
	u_int	status0;
	u_int	status;
	struct	scb *scb;
	char	cur_channel;
	char	intr_channel;

	if ((ahc->features & AHC_TWIN) != 0
	 && ((ahc_inb(ahc, SBLKCTL) & SELBUSB) != 0))
		cur_channel = 'B';
	else
		cur_channel = 'A';
	intr_channel = cur_channel;

	if ((ahc->features & AHC_ULTRA2) != 0)
		status0 = ahc_inb(ahc, SSTAT0) & IOERR;
	else
		status0 = 0;
	status = ahc_inb(ahc, SSTAT1) & (SELTO|SCSIRSTI|BUSFREE|SCSIPERR);
	if (status == 0 && status0 == 0) {
		if ((ahc->features & AHC_TWIN) != 0) {
			/* Try the other channel */
		 	ahc_outb(ahc, SBLKCTL, ahc_inb(ahc, SBLKCTL) ^ SELBUSB);
			status = ahc_inb(ahc, SSTAT1)
			       & (SELTO|SCSIRSTI|BUSFREE|SCSIPERR);
			intr_channel = (cur_channel == 'A') ? 'B' : 'A';
		}
		if (status == 0) {
			printk("%s: Spurious SCSI interrupt\n", ahc_name(ahc));
			ahc_outb(ahc, CLRINT, CLRSCSIINT);
			ahc_unpause(ahc);
			return;
		}
	}

	/* Make sure the sequencer is in a safe location. */
	ahc_clear_critical_section(ahc);

	scb_index = ahc_inb(ahc, SCB_TAG);
	scb = ahc_lookup_scb(ahc, scb_index);
	if (scb != NULL
	 && (ahc_inb(ahc, SEQ_FLAGS) & NOT_IDENTIFIED) != 0)
		scb = NULL;

	if ((ahc->features & AHC_ULTRA2) != 0
	 && (status0 & IOERR) != 0) {
		int now_lvd;

		now_lvd = ahc_inb(ahc, SBLKCTL) & ENAB40;
		printk("%s: Transceiver State Has Changed to %s mode\n",
		       ahc_name(ahc), now_lvd ? "LVD" : "SE");
		ahc_outb(ahc, CLRSINT0, CLRIOERR);
		/*
		 * When transitioning to SE mode, the reset line
		 * glitches, triggering an arbitration bug in some
		 * Ultra2 controllers.  This bug is cleared when we
		 * assert the reset line.  Since a reset glitch has
		 * already occurred with this transition and a
		 * transceiver state change is handled just like
		 * a bus reset anyway, asserting the reset line
		 * ourselves is safe.
		 */
		ahc_reset_channel(ahc, intr_channel,
				 /*Initiate Reset*/now_lvd == 0);
	} else if ((status & SCSIRSTI) != 0) {
		printk("%s: Someone reset channel %c\n",
			ahc_name(ahc), intr_channel);
		if (intr_channel != cur_channel)
		 	ahc_outb(ahc, SBLKCTL, ahc_inb(ahc, SBLKCTL) ^ SELBUSB);
		ahc_reset_channel(ahc, intr_channel, /*Initiate Reset*/FALSE);
	} else if ((status & SCSIPERR) != 0) {
		/*
		 * Determine the bus phase and queue an appropriate message.
		 * SCSIPERR is latched true as soon as a parity error
		 * occurs.  If the sequencer acked the transfer that
		 * caused the parity error and the currently presented
		 * transfer on the bus has correct parity, SCSIPERR will
		 * be cleared by CLRSCSIPERR.  Use this to determine if
		 * we should look at the last phase the sequencer recorded,
		 * or the current phase presented on the bus.
		 */
		struct	ahc_devinfo devinfo;
		u_int	mesg_out;
		u_int	curphase;
		u_int	errorphase;
		u_int	lastphase;
		u_int	scsirate;
		u_int	i;
		u_int	sstat2;
		int	silent;

		lastphase = ahc_inb(ahc, LASTPHASE);
		curphase = ahc_inb(ahc, SCSISIGI) & PHASE_MASK;
		sstat2 = ahc_inb(ahc, SSTAT2);
		ahc_outb(ahc, CLRSINT1, CLRSCSIPERR);
		/*
		 * For all phases save DATA, the sequencer won't
		 * automatically ack a byte that has a parity error
		 * in it.  So the only way that the current phase
		 * could be 'data-in' is if the parity error is for
		 * an already acked byte in the data phase.  During
		 * synchronous data-in transfers, we may actually
		 * ack bytes before latching the current phase in
		 * LASTPHASE, leading to the discrepancy between
		 * curphase and lastphase.
		 */
		if ((ahc_inb(ahc, SSTAT1) & SCSIPERR) != 0
		 || curphase == P_DATAIN || curphase == P_DATAIN_DT)
			errorphase = curphase;
		else
			errorphase = lastphase;

		for (i = 0; i < num_phases; i++) {
			if (errorphase == ahc_phase_table[i].phase)
				break;
		}
		mesg_out = ahc_phase_table[i].mesg_out;
		silent = FALSE;
		if (scb != NULL) {
			if (SCB_IS_SILENT(scb))
				silent = TRUE;
			else
				ahc_print_path(ahc, scb);
			scb->flags |= SCB_TRANSMISSION_ERROR;
		} else
			printk("%s:%c:%d: ", ahc_name(ahc), intr_channel,
			       SCSIID_TARGET(ahc, ahc_inb(ahc, SAVED_SCSIID)));
		scsirate = ahc_inb(ahc, SCSIRATE);
		if (silent == FALSE) {
			printk("parity error detected %s. "
			       "SEQADDR(0x%x) SCSIRATE(0x%x)\n",
			       ahc_phase_table[i].phasemsg,
			       ahc_inw(ahc, SEQADDR0),
			       scsirate);
			if ((ahc->features & AHC_DT) != 0) {
				if ((sstat2 & CRCVALERR) != 0)
					printk("\tCRC Value Mismatch\n");
				if ((sstat2 & CRCENDERR) != 0)
					printk("\tNo terminal CRC packet "
					       "recevied\n");
				if ((sstat2 & CRCREQERR) != 0)
					printk("\tIllegal CRC packet "
					       "request\n");
				if ((sstat2 & DUAL_EDGE_ERR) != 0)
					printk("\tUnexpected %sDT Data Phase\n",
					       (scsirate & SINGLE_EDGE)
					     ? "" : "non-");
			}
		}

		if ((ahc->features & AHC_DT) != 0
		 && (sstat2 & DUAL_EDGE_ERR) != 0) {
			/*
			 * This error applies regardless of
			 * data direction, so ignore the value
			 * in the phase table.
			 */
			mesg_out = MSG_INITIATOR_DET_ERR;
		}

		/*
		 * We've set the hardware to assert ATN if we   
		 * get a parity error on "in" phases, so all we  
		 * need to do is stuff the message buffer with
		 * the appropriate message.  "In" phases have set
		 * mesg_out to something other than MSG_NOP.
		 */
		if (mesg_out != MSG_NOOP) {
			if (ahc->msg_type != MSG_TYPE_NONE)
				ahc->send_msg_perror = TRUE;
			else
				ahc_outb(ahc, MSG_OUT, mesg_out);
		}
		/*
		 * Force a renegotiation with this target just in
		 * case we are out of sync for some external reason
		 * unknown (or unreported) by the target.
		 */
		ahc_fetch_devinfo(ahc, &devinfo);
		ahc_force_renegotiation(ahc, &devinfo);

		ahc_outb(ahc, CLRINT, CLRSCSIINT);
		ahc_unpause(ahc);
	} else if ((status & SELTO) != 0) {
		u_int	scbptr;

		/* Stop the selection */
		ahc_outb(ahc, SCSISEQ, 0);

		/* No more pending messages */
		ahc_clear_msg_state(ahc);

		/* Clear interrupt state */
		ahc_outb(ahc, SIMODE1, ahc_inb(ahc, SIMODE1) & ~ENBUSFREE);
		ahc_outb(ahc, CLRSINT1, CLRSELTIMEO|CLRBUSFREE|CLRSCSIPERR);

		/*
		 * Although the driver does not care about the
		 * 'Selection in Progress' status bit, the busy
		 * LED does.  SELINGO is only cleared by a successful
		 * selection, so we must manually clear it to insure
		 * the LED turns off just incase no future successful
		 * selections occur (e.g. no devices on the bus).
		 */
		ahc_outb(ahc, CLRSINT0, CLRSELINGO);

		scbptr = ahc_inb(ahc, WAITING_SCBH);
		ahc_outb(ahc, SCBPTR, scbptr);
		scb_index = ahc_inb(ahc, SCB_TAG);

		scb = ahc_lookup_scb(ahc, scb_index);
		if (scb == NULL) {
			printk("%s: ahc_intr - referenced scb not "
			       "valid during SELTO scb(%d, %d)\n",
			       ahc_name(ahc), scbptr, scb_index);
			ahc_dump_card_state(ahc);
		} else {
			struct ahc_devinfo devinfo;
#ifdef AHC_DEBUG
			if ((ahc_debug & AHC_SHOW_SELTO) != 0) {
				ahc_print_path(ahc, scb);
				printk("Saw Selection Timeout for SCB 0x%x\n",
				       scb_index);
			}
#endif
			ahc_scb_devinfo(ahc, &devinfo, scb);
			ahc_set_transaction_status(scb, CAM_SEL_TIMEOUT);
			ahc_freeze_devq(ahc, scb);

			/*
			 * Cancel any pending transactions on the device
			 * now that it seems to be missing.  This will
			 * also revert us to async/narrow transfers until
			 * we can renegotiate with the device.
			 */
			ahc_handle_devreset(ahc, &devinfo,
					    CAM_SEL_TIMEOUT,
					    "Selection Timeout",
					    /*verbose_level*/1);
		}
		ahc_outb(ahc, CLRINT, CLRSCSIINT);
		ahc_restart(ahc);
	} else if ((status & BUSFREE) != 0
		&& (ahc_inb(ahc, SIMODE1) & ENBUSFREE) != 0) {
		struct	ahc_devinfo devinfo;
		u_int	lastphase;
		u_int	saved_scsiid;
		u_int	saved_lun;
		u_int	target;
		u_int	initiator_role_id;
		char	channel;
		int	printerror;

		/*
		 * Clear our selection hardware as soon as possible.
		 * We may have an entry in the waiting Q for this target,
		 * that is affected by this busfree and we don't want to
		 * go about selecting the target while we handle the event.
		 */
		ahc_outb(ahc, SCSISEQ,
			 ahc_inb(ahc, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP));

		/*
		 * Disable busfree interrupts and clear the busfree
		 * interrupt status.  We do this here so that several
		 * bus transactions occur prior to clearing the SCSIINT
		 * latch.  It can take a bit for the clearing to take effect.
		 */
		ahc_outb(ahc, SIMODE1, ahc_inb(ahc, SIMODE1) & ~ENBUSFREE);
		ahc_outb(ahc, CLRSINT1, CLRBUSFREE|CLRSCSIPERR);

		/*
		 * Look at what phase we were last in.
		 * If its message out, chances are pretty good
		 * that the busfree was in response to one of
		 * our abort requests.
		 */
		lastphase = ahc_inb(ahc, LASTPHASE);
		saved_scsiid = ahc_inb(ahc, SAVED_SCSIID);
		saved_lun = ahc_inb(ahc, SAVED_LUN);
		target = SCSIID_TARGET(ahc, saved_scsiid);
		initiator_role_id = SCSIID_OUR_ID(saved_scsiid);
		channel = SCSIID_CHANNEL(ahc, saved_scsiid);
		ahc_compile_devinfo(&devinfo, initiator_role_id,
				    target, saved_lun, channel, ROLE_INITIATOR);
		printerror = 1;

		if (lastphase == P_MESGOUT) {
			u_int tag;

			tag = SCB_LIST_NULL;
			if (ahc_sent_msg(ahc, AHCMSG_1B, MSG_ABORT_TAG, TRUE)
			 || ahc_sent_msg(ahc, AHCMSG_1B, MSG_ABORT, TRUE)) {
				if (ahc->msgout_buf[ahc->msgout_index - 1]
				 == MSG_ABORT_TAG)
					tag = scb->hscb->tag;
				ahc_print_path(ahc, scb);
				printk("SCB %d - Abort%s Completed.\n",
				       scb->hscb->tag, tag == SCB_LIST_NULL ?
				       "" : " Tag");
				ahc_abort_scbs(ahc, target, channel,
					       saved_lun, tag,
					       ROLE_INITIATOR,
					       CAM_REQ_ABORTED);
				printerror = 0;
			} else if (ahc_sent_msg(ahc, AHCMSG_1B,
						MSG_BUS_DEV_RESET, TRUE)) {
#ifdef __FreeBSD__
				/*
				 * Don't mark the user's request for this BDR
				 * as completing with CAM_BDR_SENT.  CAM3
				 * specifies CAM_REQ_CMP.
				 */
				if (scb != NULL
				 && scb->io_ctx->ccb_h.func_code== XPT_RESET_DEV
				 && ahc_match_scb(ahc, scb, target, channel,
						  CAM_LUN_WILDCARD,
						  SCB_LIST_NULL,
						  ROLE_INITIATOR)) {
					ahc_set_transaction_status(scb, CAM_REQ_CMP);
				}
#endif
				ahc_compile_devinfo(&devinfo,
						    initiator_role_id,
						    target,
						    CAM_LUN_WILDCARD,
						    channel,
						    ROLE_INITIATOR);
				ahc_handle_devreset(ahc, &devinfo,
						    CAM_BDR_SENT,
						    "Bus Device Reset",
						    /*verbose_level*/0);
				printerror = 0;
			} else if (ahc_sent_msg(ahc, AHCMSG_EXT,
						MSG_EXT_PPR, FALSE)) {
				struct ahc_initiator_tinfo *tinfo;
				struct ahc_tmode_tstate *tstate;

				/*
				 * PPR Rejected.  Try non-ppr negotiation
				 * and retry command.
				 */
				tinfo = ahc_fetch_transinfo(ahc,
							    devinfo.channel,
							    devinfo.our_scsiid,
							    devinfo.target,
							    &tstate);
				tinfo->curr.transport_version = 2;
				tinfo->goal.transport_version = 2;
				tinfo->goal.ppr_options = 0;
				ahc_qinfifo_requeue_tail(ahc, scb);
				printerror = 0;
			} else if (ahc_sent_msg(ahc, AHCMSG_EXT,
						MSG_EXT_WDTR, FALSE)) {
				/*
				 * Negotiation Rejected.  Go-narrow and
				 * retry command.
				 */
				ahc_set_width(ahc, &devinfo,
					      MSG_EXT_WDTR_BUS_8_BIT,
					      AHC_TRANS_CUR|AHC_TRANS_GOAL,
					      /*paused*/TRUE);
				ahc_qinfifo_requeue_tail(ahc, scb);
				printerror = 0;
			} else if (ahc_sent_msg(ahc, AHCMSG_EXT,
						MSG_EXT_SDTR, FALSE)) {
				/*
				 * Negotiation Rejected.  Go-async and
				 * retry command.
				 */
				ahc_set_syncrate(ahc, &devinfo,
						/*syncrate*/NULL,
						/*period*/0, /*offset*/0,
						/*ppr_options*/0,
						AHC_TRANS_CUR|AHC_TRANS_GOAL,
						/*paused*/TRUE);
				ahc_qinfifo_requeue_tail(ahc, scb);
				printerror = 0;
			}
		}
		if (printerror != 0) {
			u_int i;

			if (scb != NULL) {
				u_int tag;

				if ((scb->hscb->control & TAG_ENB) != 0)
					tag = scb->hscb->tag;
				else
					tag = SCB_LIST_NULL;
				ahc_print_path(ahc, scb);
				ahc_abort_scbs(ahc, target, channel,
					       SCB_GET_LUN(scb), tag,
					       ROLE_INITIATOR,
					       CAM_UNEXP_BUSFREE);
			} else {
				/*
				 * We had not fully identified this connection,
				 * so we cannot abort anything.
				 */
				printk("%s: ", ahc_name(ahc));
			}
			for (i = 0; i < num_phases; i++) {
				if (lastphase == ahc_phase_table[i].phase)
					break;
			}
			if (lastphase != P_BUSFREE) {
				/*
				 * Renegotiate with this device at the
				 * next opportunity just in case this busfree
				 * is due to a negotiation mismatch with the
				 * device.
				 */
				ahc_force_renegotiation(ahc, &devinfo);
			}
			printk("Unexpected busfree %s\n"
			       "SEQADDR == 0x%x\n",
			       ahc_phase_table[i].phasemsg,
			       ahc_inb(ahc, SEQADDR0)
				| (ahc_inb(ahc, SEQADDR1) << 8));
		}
		ahc_outb(ahc, CLRINT, CLRSCSIINT);
		ahc_restart(ahc);
	} else {
		printk("%s: Missing case in ahc_handle_scsiint. status = %x\n",
		       ahc_name(ahc), status);
		ahc_outb(ahc, CLRINT, CLRSCSIINT);
	}
}

/*
 * Force renegotiation to occur the next time we initiate
 * a command to the current device.
 */
static void
ahc_force_renegotiation(struct ahc_softc *ahc, struct ahc_devinfo *devinfo)
{
	struct	ahc_initiator_tinfo *targ_info;
	struct	ahc_tmode_tstate *tstate;

	targ_info = ahc_fetch_transinfo(ahc,
					devinfo->channel,
					devinfo->our_scsiid,
					devinfo->target,
					&tstate);
	ahc_update_neg_request(ahc, devinfo, tstate,
			       targ_info, AHC_NEG_IF_NON_ASYNC);
}

#define AHC_MAX_STEPS 2000
static void
ahc_clear_critical_section(struct ahc_softc *ahc)
{
	int	stepping;
	int	steps;
	u_int	simode0;
	u_int	simode1;

	if (ahc->num_critical_sections == 0)
		return;

	stepping = FALSE;
	steps = 0;
	simode0 = 0;
	simode1 = 0;
	for (;;) {
		struct	cs *cs;
		u_int	seqaddr;
		u_int	i;

		seqaddr = ahc_inb(ahc, SEQADDR0)
			| (ahc_inb(ahc, SEQADDR1) << 8);

		/*
		 * Seqaddr represents the next instruction to execute, 
		 * so we are really executing the instruction just
		 * before it.
		 */
		if (seqaddr != 0)
			seqaddr -= 1;
		cs = ahc->critical_sections;
		for (i = 0; i < ahc->num_critical_sections; i++, cs++) {
			
			if (cs->begin < seqaddr && cs->end >= seqaddr)
				break;
		}

		if (i == ahc->num_critical_sections)
			break;

		if (steps > AHC_MAX_STEPS) {
			printk("%s: Infinite loop in critical section\n",
			       ahc_name(ahc));
			ahc_dump_card_state(ahc);
			panic("critical section loop");
		}

		steps++;
		if (stepping == FALSE) {

			/*
			 * Disable all interrupt sources so that the
			 * sequencer will not be stuck by a pausing
			 * interrupt condition while we attempt to
			 * leave a critical section.
			 */
			simode0 = ahc_inb(ahc, SIMODE0);
			ahc_outb(ahc, SIMODE0, 0);
			simode1 = ahc_inb(ahc, SIMODE1);
			if ((ahc->features & AHC_DT) != 0)
				/*
				 * On DT class controllers, we
				 * use the enhanced busfree logic.
				 * Unfortunately we cannot re-enable
				 * busfree detection within the
				 * current connection, so we must
				 * leave it on while single stepping.
				 */
				ahc_outb(ahc, SIMODE1, simode1 & ENBUSFREE);
			else
				ahc_outb(ahc, SIMODE1, 0);
			ahc_outb(ahc, CLRINT, CLRSCSIINT);
			ahc_outb(ahc, SEQCTL, ahc->seqctl | STEP);
			stepping = TRUE;
		}
		if ((ahc->features & AHC_DT) != 0) {
			ahc_outb(ahc, CLRSINT1, CLRBUSFREE);
			ahc_outb(ahc, CLRINT, CLRSCSIINT);
		}
		ahc_outb(ahc, HCNTRL, ahc->unpause);
		while (!ahc_is_paused(ahc))
			ahc_delay(200);
	}
	if (stepping) {
		ahc_outb(ahc, SIMODE0, simode0);
		ahc_outb(ahc, SIMODE1, simode1);
		ahc_outb(ahc, SEQCTL, ahc->seqctl);
	}
}

/*
 * Clear any pending interrupt status.
 */
static void
ahc_clear_intstat(struct ahc_softc *ahc)
{
	/* Clear any interrupt conditions this may have caused */
	ahc_outb(ahc, CLRSINT1, CLRSELTIMEO|CLRATNO|CLRSCSIRSTI
				|CLRBUSFREE|CLRSCSIPERR|CLRPHASECHG|
				CLRREQINIT);
	ahc_flush_device_writes(ahc);
	ahc_outb(ahc, CLRSINT0, CLRSELDO|CLRSELDI|CLRSELINGO);
 	ahc_flush_device_writes(ahc);
	ahc_outb(ahc, CLRINT, CLRSCSIINT);
	ahc_flush_device_writes(ahc);
}

/**************************** Debugging Routines ******************************/
#ifdef AHC_DEBUG
uint32_t ahc_debug = AHC_DEBUG_OPTS;
#endif

#if 0 /* unused */
static void
ahc_print_scb(struct scb *scb)
{
	int i;

	struct hardware_scb *hscb = scb->hscb;

	printk("scb:%p control:0x%x scsiid:0x%x lun:%d cdb_len:%d\n",
	       (void *)scb,
	       hscb->control,
	       hscb->scsiid,
	       hscb->lun,
	       hscb->cdb_len);
	printk("Shared Data: ");
	for (i = 0; i < sizeof(hscb->shared_data.cdb); i++)
		printk("%#02x", hscb->shared_data.cdb[i]);
	printk("        dataptr:%#x datacnt:%#x sgptr:%#x tag:%#x\n",
		ahc_le32toh(hscb->dataptr),
		ahc_le32toh(hscb->datacnt),
		ahc_le32toh(hscb->sgptr),
		hscb->tag);
	if (scb->sg_count > 0) {
		for (i = 0; i < scb->sg_count; i++) {
			printk("sg[%d] - Addr 0x%x%x : Length %d\n",
			       i,
			       (ahc_le32toh(scb->sg_list[i].len) >> 24
			        & SG_HIGH_ADDR_BITS),
			       ahc_le32toh(scb->sg_list[i].addr),
			       ahc_le32toh(scb->sg_list[i].len));
		}
	}
}
#endif

/************************* Transfer Negotiation *******************************/
/*
 * Allocate per target mode instance (ID we respond to as a target)
 * transfer negotiation data structures.
 */
static struct ahc_tmode_tstate *
ahc_alloc_tstate(struct ahc_softc *ahc, u_int scsi_id, char channel)
{
	struct ahc_tmode_tstate *master_tstate;
	struct ahc_tmode_tstate *tstate;
	int i;

	master_tstate = ahc->enabled_targets[ahc->our_id];
	if (channel == 'B') {
		scsi_id += 8;
		master_tstate = ahc->enabled_targets[ahc->our_id_b + 8];
	}
	if (ahc->enabled_targets[scsi_id] != NULL
	 && ahc->enabled_targets[scsi_id] != master_tstate)
		panic("%s: ahc_alloc_tstate - Target already allocated",
		      ahc_name(ahc));
	tstate = kmalloc(sizeof(*tstate), GFP_ATOMIC);
	if (tstate == NULL)
		return (NULL);

	/*
	 * If we have allocated a master tstate, copy user settings from
	 * the master tstate (taken from SRAM or the EEPROM) for this
	 * channel, but reset our current and goal settings to async/narrow
	 * until an initiator talks to us.
	 */
	if (master_tstate != NULL) {
		memcpy(tstate, master_tstate, sizeof(*tstate));
		memset(tstate->enabled_luns, 0, sizeof(tstate->enabled_luns));
		tstate->ultraenb = 0;
		for (i = 0; i < AHC_NUM_TARGETS; i++) {
			memset(&tstate->transinfo[i].curr, 0,
			      sizeof(tstate->transinfo[i].curr));
			memset(&tstate->transinfo[i].goal, 0,
			      sizeof(tstate->transinfo[i].goal));
		}
	} else
		memset(tstate, 0, sizeof(*tstate));
	ahc->enabled_targets[scsi_id] = tstate;
	return (tstate);
}

#ifdef AHC_TARGET_MODE
/*
 * Free per target mode instance (ID we respond to as a target)
 * transfer negotiation data structures.
 */
static void
ahc_free_tstate(struct ahc_softc *ahc, u_int scsi_id, char channel, int force)
{
	struct ahc_tmode_tstate *tstate;

	/*
	 * Don't clean up our "master" tstate.
	 * It has our default user settings.
	 */
	if (((channel == 'B' && scsi_id == ahc->our_id_b)
	  || (channel == 'A' && scsi_id == ahc->our_id))
	 && force == FALSE)
		return;

	if (channel == 'B')
		scsi_id += 8;
	tstate = ahc->enabled_targets[scsi_id];
	if (tstate != NULL)
		kfree(tstate);
	ahc->enabled_targets[scsi_id] = NULL;
}
#endif

/*
 * Called when we have an active connection to a target on the bus,
 * this function finds the nearest syncrate to the input period limited
 * by the capabilities of the bus connectivity of and sync settings for
 * the target.
 */
static const struct ahc_syncrate *
ahc_devlimited_syncrate(struct ahc_softc *ahc,
			struct ahc_initiator_tinfo *tinfo,
			u_int *period, u_int *ppr_options, role_t role)
{
	struct	ahc_transinfo *transinfo;
	u_int	maxsync;

	if ((ahc->features & AHC_ULTRA2) != 0) {
		if ((ahc_inb(ahc, SBLKCTL) & ENAB40) != 0
		 && (ahc_inb(ahc, SSTAT2) & EXP_ACTIVE) == 0) {
			maxsync = AHC_SYNCRATE_DT;
		} else {
			maxsync = AHC_SYNCRATE_ULTRA;
			/* Can't do DT on an SE bus */
			*ppr_options &= ~MSG_EXT_PPR_DT_REQ;
		}
	} else if ((ahc->features & AHC_ULTRA) != 0) {
		maxsync = AHC_SYNCRATE_ULTRA;
	} else {
		maxsync = AHC_SYNCRATE_FAST;
	}
	/*
	 * Never allow a value higher than our current goal
	 * period otherwise we may allow a target initiated
	 * negotiation to go above the limit as set by the
	 * user.  In the case of an initiator initiated
	 * sync negotiation, we limit based on the user
	 * setting.  This allows the system to still accept
	 * incoming negotiations even if target initiated
	 * negotiation is not performed.
	 */
	if (role == ROLE_TARGET)
		transinfo = &tinfo->user;
	else 
		transinfo = &tinfo->goal;
	*ppr_options &= transinfo->ppr_options;
	if (transinfo->width == MSG_EXT_WDTR_BUS_8_BIT) {
		maxsync = max(maxsync, (u_int)AHC_SYNCRATE_ULTRA2);
		*ppr_options &= ~MSG_EXT_PPR_DT_REQ;
	}
	if (transinfo->period == 0) {
		*period = 0;
		*ppr_options = 0;
		return (NULL);
	}
	*period = max(*period, (u_int)transinfo->period);
	return (ahc_find_syncrate(ahc, period, ppr_options, maxsync));
}

/*
 * Look up the valid period to SCSIRATE conversion in our table.
 * Return the period and offset that should be sent to the target
 * if this was the beginning of an SDTR.
 */
const struct ahc_syncrate *
ahc_find_syncrate(struct ahc_softc *ahc, u_int *period,
		  u_int *ppr_options, u_int maxsync)
{
	const struct ahc_syncrate *syncrate;

	if ((ahc->features & AHC_DT) == 0)
		*ppr_options &= ~MSG_EXT_PPR_DT_REQ;

	/* Skip all DT only entries if DT is not available */
	if ((*ppr_options & MSG_EXT_PPR_DT_REQ) == 0
	 && maxsync < AHC_SYNCRATE_ULTRA2)
		maxsync = AHC_SYNCRATE_ULTRA2;

	/* Now set the maxsync based on the card capabilities
	 * DT is already done above */
	if ((ahc->features & (AHC_DT | AHC_ULTRA2)) == 0
	    && maxsync < AHC_SYNCRATE_ULTRA)
		maxsync = AHC_SYNCRATE_ULTRA;
	if ((ahc->features & (AHC_DT | AHC_ULTRA2 | AHC_ULTRA)) == 0
	    && maxsync < AHC_SYNCRATE_FAST)
		maxsync = AHC_SYNCRATE_FAST;

	for (syncrate = &ahc_syncrates[maxsync];
	     syncrate->rate != NULL;
	     syncrate++) {

		/*
		 * The Ultra2 table doesn't go as low
		 * as for the Fast/Ultra cards.
		 */
		if ((ahc->features & AHC_ULTRA2) != 0
		 && (syncrate->sxfr_u2 == 0))
			break;

		if (*period <= syncrate->period) {
			/*
			 * When responding to a target that requests
			 * sync, the requested rate may fall between
			 * two rates that we can output, but still be
			 * a rate that we can receive.  Because of this,
			 * we want to respond to the target with
			 * the same rate that it sent to us even
			 * if the period we use to send data to it
			 * is lower.  Only lower the response period
			 * if we must.
			 */
			if (syncrate == &ahc_syncrates[maxsync])
				*period = syncrate->period;

			/*
			 * At some speeds, we only support
			 * ST transfers.
			 */
		 	if ((syncrate->sxfr_u2 & ST_SXFR) != 0)
				*ppr_options &= ~MSG_EXT_PPR_DT_REQ;
			break;
		}
	}

	if ((*period == 0)
	 || (syncrate->rate == NULL)
	 || ((ahc->features & AHC_ULTRA2) != 0
	  && (syncrate->sxfr_u2 == 0))) {
		/* Use asynchronous transfers. */
		*period = 0;
		syncrate = NULL;
		*ppr_options &= ~MSG_EXT_PPR_DT_REQ;
	}
	return (syncrate);
}

/*
 * Convert from an entry in our syncrate table to the SCSI equivalent
 * sync "period" factor.
 */
u_int
ahc_find_period(struct ahc_softc *ahc, u_int scsirate, u_int maxsync)
{
	const struct ahc_syncrate *syncrate;

	if ((ahc->features & AHC_ULTRA2) != 0)
		scsirate &= SXFR_ULTRA2;
	else
		scsirate &= SXFR;

	/* now set maxsync based on card capabilities */
	if ((ahc->features & AHC_DT) == 0 && maxsync < AHC_SYNCRATE_ULTRA2)
		maxsync = AHC_SYNCRATE_ULTRA2;
	if ((ahc->features & (AHC_DT | AHC_ULTRA2)) == 0
	    && maxsync < AHC_SYNCRATE_ULTRA)
		maxsync = AHC_SYNCRATE_ULTRA;
	if ((ahc->features & (AHC_DT | AHC_ULTRA2 | AHC_ULTRA)) == 0
	    && maxsync < AHC_SYNCRATE_FAST)
		maxsync = AHC_SYNCRATE_FAST;


	syncrate = &ahc_syncrates[maxsync];
	while (syncrate->rate != NULL) {

		if ((ahc->features & AHC_ULTRA2) != 0) {
			if (syncrate->sxfr_u2 == 0)
				break;
			else if (scsirate == (syncrate->sxfr_u2 & SXFR_ULTRA2))
				return (syncrate->period);
		} else if (scsirate == (syncrate->sxfr & SXFR)) {
				return (syncrate->period);
		}
		syncrate++;
	}
	return (0); /* async */
}

/*
 * Truncate the given synchronous offset to a value the
 * current adapter type and syncrate are capable of.
 */
static void
ahc_validate_offset(struct ahc_softc *ahc,
		    struct ahc_initiator_tinfo *tinfo,
		    const struct ahc_syncrate *syncrate,
		    u_int *offset, int wide, role_t role)
{
	u_int maxoffset;

	/* Limit offset to what we can do */
	if (syncrate == NULL) {
		maxoffset = 0;
	} else if ((ahc->features & AHC_ULTRA2) != 0) {
		maxoffset = MAX_OFFSET_ULTRA2;
	} else {
		if (wide)
			maxoffset = MAX_OFFSET_16BIT;
		else
			maxoffset = MAX_OFFSET_8BIT;
	}
	*offset = min(*offset, maxoffset);
	if (tinfo != NULL) {
		if (role == ROLE_TARGET)
			*offset = min(*offset, (u_int)tinfo->user.offset);
		else
			*offset = min(*offset, (u_int)tinfo->goal.offset);
	}
}

/*
 * Truncate the given transfer width parameter to a value the
 * current adapter type is capable of.
 */
static void
ahc_validate_width(struct ahc_softc *ahc, struct ahc_initiator_tinfo *tinfo,
		   u_int *bus_width, role_t role)
{
	switch (*bus_width) {
	default:
		if (ahc->features & AHC_WIDE) {
			/* Respond Wide */
			*bus_width = MSG_EXT_WDTR_BUS_16_BIT;
			break;
		}
		/* FALLTHROUGH */
	case MSG_EXT_WDTR_BUS_8_BIT:
		*bus_width = MSG_EXT_WDTR_BUS_8_BIT;
		break;
	}
	if (tinfo != NULL) {
		if (role == ROLE_TARGET)
			*bus_width = min((u_int)tinfo->user.width, *bus_width);
		else
			*bus_width = min((u_int)tinfo->goal.width, *bus_width);
	}
}

/*
 * Update the bitmask of targets for which the controller should
 * negotiate with at the next convenient opportunity.  This currently
 * means the next time we send the initial identify messages for
 * a new transaction.
 */
int
ahc_update_neg_request(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
		       struct ahc_tmode_tstate *tstate,
		       struct ahc_initiator_tinfo *tinfo, ahc_neg_type neg_type)
{
	u_int auto_negotiate_orig;

	auto_negotiate_orig = tstate->auto_negotiate;
	if (neg_type == AHC_NEG_ALWAYS) {
		/*
		 * Force our "current" settings to be
		 * unknown so that unless a bus reset
		 * occurs the need to renegotiate is
		 * recorded persistently.
		 */
		if ((ahc->features & AHC_WIDE) != 0)
			tinfo->curr.width = AHC_WIDTH_UNKNOWN;
		tinfo->curr.period = AHC_PERIOD_UNKNOWN;
		tinfo->curr.offset = AHC_OFFSET_UNKNOWN;
	}
	if (tinfo->curr.period != tinfo->goal.period
	 || tinfo->curr.width != tinfo->goal.width
	 || tinfo->curr.offset != tinfo->goal.offset
	 || tinfo->curr.ppr_options != tinfo->goal.ppr_options
	 || (neg_type == AHC_NEG_IF_NON_ASYNC
	  && (tinfo->goal.offset != 0
	   || tinfo->goal.width != MSG_EXT_WDTR_BUS_8_BIT
	   || tinfo->goal.ppr_options != 0)))
		tstate->auto_negotiate |= devinfo->target_mask;
	else
		tstate->auto_negotiate &= ~devinfo->target_mask;

	return (auto_negotiate_orig != tstate->auto_negotiate);
}

/*
 * Update the user/goal/curr tables of synchronous negotiation
 * parameters as well as, in the case of a current or active update,
 * any data structures on the host controller.  In the case of an
 * active update, the specified target is currently talking to us on
 * the bus, so the transfer parameter update must take effect
 * immediately.
 */
void
ahc_set_syncrate(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
		 const struct ahc_syncrate *syncrate, u_int period,
		 u_int offset, u_int ppr_options, u_int type, int paused)
{
	struct	ahc_initiator_tinfo *tinfo;
	struct	ahc_tmode_tstate *tstate;
	u_int	old_period;
	u_int	old_offset;
	u_int	old_ppr;
	int	active;
	int	update_needed;

	active = (type & AHC_TRANS_ACTIVE) == AHC_TRANS_ACTIVE;
	update_needed = 0;

	if (syncrate == NULL) {
		period = 0;
		offset = 0;
	}

	tinfo = ahc_fetch_transinfo(ahc, devinfo->channel, devinfo->our_scsiid,
				    devinfo->target, &tstate);

	if ((type & AHC_TRANS_USER) != 0) {
		tinfo->user.period = period;
		tinfo->user.offset = offset;
		tinfo->user.ppr_options = ppr_options;
	}

	if ((type & AHC_TRANS_GOAL) != 0) {
		tinfo->goal.period = period;
		tinfo->goal.offset = offset;
		tinfo->goal.ppr_options = ppr_options;
	}

	old_period = tinfo->curr.period;
	old_offset = tinfo->curr.offset;
	old_ppr	   = tinfo->curr.ppr_options;

	if ((type & AHC_TRANS_CUR) != 0
	 && (old_period != period
	  || old_offset != offset
	  || old_ppr != ppr_options)) {
		u_int	scsirate;

		update_needed++;
		scsirate = tinfo->scsirate;
		if ((ahc->features & AHC_ULTRA2) != 0) {

			scsirate &= ~(SXFR_ULTRA2|SINGLE_EDGE|ENABLE_CRC);
			if (syncrate != NULL) {
				scsirate |= syncrate->sxfr_u2;
				if ((ppr_options & MSG_EXT_PPR_DT_REQ) != 0)
					scsirate |= ENABLE_CRC;
				else
					scsirate |= SINGLE_EDGE;
			}
		} else {

			scsirate &= ~(SXFR|SOFS);
			/*
			 * Ensure Ultra mode is set properly for
			 * this target.
			 */
			tstate->ultraenb &= ~devinfo->target_mask;
			if (syncrate != NULL) {
				if (syncrate->sxfr & ULTRA_SXFR) {
					tstate->ultraenb |=
						devinfo->target_mask;
				}
				scsirate |= syncrate->sxfr & SXFR;
				scsirate |= offset & SOFS;
			}
			if (active) {
				u_int sxfrctl0;

				sxfrctl0 = ahc_inb(ahc, SXFRCTL0);
				sxfrctl0 &= ~FAST20;
				if (tstate->ultraenb & devinfo->target_mask)
					sxfrctl0 |= FAST20;
				ahc_outb(ahc, SXFRCTL0, sxfrctl0);
			}
		}
		if (active) {
			ahc_outb(ahc, SCSIRATE, scsirate);
			if ((ahc->features & AHC_ULTRA2) != 0)
				ahc_outb(ahc, SCSIOFFSET, offset);
		}

		tinfo->scsirate = scsirate;
		tinfo->curr.period = period;
		tinfo->curr.offset = offset;
		tinfo->curr.ppr_options = ppr_options;

		ahc_send_async(ahc, devinfo->channel, devinfo->target,
			       CAM_LUN_WILDCARD, AC_TRANSFER_NEG);
		if (bootverbose) {
			if (offset != 0) {
				printk("%s: target %d synchronous at %sMHz%s, "
				       "offset = 0x%x\n", ahc_name(ahc),
				       devinfo->target, syncrate->rate,
				       (ppr_options & MSG_EXT_PPR_DT_REQ)
				       ? " DT" : "", offset);
			} else {
				printk("%s: target %d using "
				       "asynchronous transfers\n",
				       ahc_name(ahc), devinfo->target);
			}
		}
	}

	update_needed += ahc_update_neg_request(ahc, devinfo, tstate,
						tinfo, AHC_NEG_TO_GOAL);

	if (update_needed)
		ahc_update_pending_scbs(ahc);
}

/*
 * Update the user/goal/curr tables of wide negotiation
 * parameters as well as, in the case of a current or active update,
 * any data structures on the host controller.  In the case of an
 * active update, the specified target is currently talking to us on
 * the bus, so the transfer parameter update must take effect
 * immediately.
 */
void
ahc_set_width(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
	      u_int width, u_int type, int paused)
{
	struct	ahc_initiator_tinfo *tinfo;
	struct	ahc_tmode_tstate *tstate;
	u_int	oldwidth;
	int	active;
	int	update_needed;

	active = (type & AHC_TRANS_ACTIVE) == AHC_TRANS_ACTIVE;
	update_needed = 0;
	tinfo = ahc_fetch_transinfo(ahc, devinfo->channel, devinfo->our_scsiid,
				    devinfo->target, &tstate);

	if ((type & AHC_TRANS_USER) != 0)
		tinfo->user.width = width;

	if ((type & AHC_TRANS_GOAL) != 0)
		tinfo->goal.width = width;

	oldwidth = tinfo->curr.width;
	if ((type & AHC_TRANS_CUR) != 0 && oldwidth != width) {
		u_int	scsirate;

		update_needed++;
		scsirate =  tinfo->scsirate;
		scsirate &= ~WIDEXFER;
		if (width == MSG_EXT_WDTR_BUS_16_BIT)
			scsirate |= WIDEXFER;

		tinfo->scsirate = scsirate;

		if (active)
			ahc_outb(ahc, SCSIRATE, scsirate);

		tinfo->curr.width = width;

		ahc_send_async(ahc, devinfo->channel, devinfo->target,
			       CAM_LUN_WILDCARD, AC_TRANSFER_NEG);
		if (bootverbose) {
			printk("%s: target %d using %dbit transfers\n",
			       ahc_name(ahc), devinfo->target,
			       8 * (0x01 << width));
		}
	}

	update_needed += ahc_update_neg_request(ahc, devinfo, tstate,
						tinfo, AHC_NEG_TO_GOAL);
	if (update_needed)
		ahc_update_pending_scbs(ahc);
}

/*
 * Update the current state of tagged queuing for a given target.
 */
static void
ahc_set_tags(struct ahc_softc *ahc, struct scsi_cmnd *cmd,
	     struct ahc_devinfo *devinfo, ahc_queue_alg alg)
{
	struct scsi_device *sdev = cmd->device;

 	ahc_platform_set_tags(ahc, sdev, devinfo, alg);
 	ahc_send_async(ahc, devinfo->channel, devinfo->target,
 		       devinfo->lun, AC_TRANSFER_NEG);
}

/*
 * When the transfer settings for a connection change, update any
 * in-transit SCBs to contain the new data so the hardware will
 * be set correctly during future (re)selections.
 */
static void
ahc_update_pending_scbs(struct ahc_softc *ahc)
{
	struct	scb *pending_scb;
	int	pending_scb_count;
	int	i;
	int	paused;
	u_int	saved_scbptr;

	/*
	 * Traverse the pending SCB list and ensure that all of the
	 * SCBs there have the proper settings.
	 */
	pending_scb_count = 0;
	LIST_FOREACH(pending_scb, &ahc->pending_scbs, pending_links) {
		struct ahc_devinfo devinfo;
		struct hardware_scb *pending_hscb;
		struct ahc_initiator_tinfo *tinfo;
		struct ahc_tmode_tstate *tstate;

		ahc_scb_devinfo(ahc, &devinfo, pending_scb);
		tinfo = ahc_fetch_transinfo(ahc, devinfo.channel,
					    devinfo.our_scsiid,
					    devinfo.target, &tstate);
		pending_hscb = pending_scb->hscb;
		pending_hscb->control &= ~ULTRAENB;
		if ((tstate->ultraenb & devinfo.target_mask) != 0)
			pending_hscb->control |= ULTRAENB;
		pending_hscb->scsirate = tinfo->scsirate;
		pending_hscb->scsioffset = tinfo->curr.offset;
		if ((tstate->auto_negotiate & devinfo.target_mask) == 0
		 && (pending_scb->flags & SCB_AUTO_NEGOTIATE) != 0) {
			pending_scb->flags &= ~SCB_AUTO_NEGOTIATE;
			pending_hscb->control &= ~MK_MESSAGE;
		}
		ahc_sync_scb(ahc, pending_scb,
			     BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
		pending_scb_count++;
	}

	if (pending_scb_count == 0)
		return;

	if (ahc_is_paused(ahc)) {
		paused = 1;
	} else {
		paused = 0;
		ahc_pause(ahc);
	}

	saved_scbptr = ahc_inb(ahc, SCBPTR);
	/* Ensure that the hscbs down on the card match the new information */
	for (i = 0; i < ahc->scb_data->maxhscbs; i++) {
		struct	hardware_scb *pending_hscb;
		u_int	control;
		u_int	scb_tag;

		ahc_outb(ahc, SCBPTR, i);
		scb_tag = ahc_inb(ahc, SCB_TAG);
		pending_scb = ahc_lookup_scb(ahc, scb_tag);
		if (pending_scb == NULL)
			continue;

		pending_hscb = pending_scb->hscb;
		control = ahc_inb(ahc, SCB_CONTROL);
		control &= ~(ULTRAENB|MK_MESSAGE);
		control |= pending_hscb->control & (ULTRAENB|MK_MESSAGE);
		ahc_outb(ahc, SCB_CONTROL, control);
		ahc_outb(ahc, SCB_SCSIRATE, pending_hscb->scsirate);
		ahc_outb(ahc, SCB_SCSIOFFSET, pending_hscb->scsioffset);
	}
	ahc_outb(ahc, SCBPTR, saved_scbptr);

	if (paused == 0)
		ahc_unpause(ahc);
}

/**************************** Pathing Information *****************************/
static void
ahc_fetch_devinfo(struct ahc_softc *ahc, struct ahc_devinfo *devinfo)
{
	u_int	saved_scsiid;
	role_t	role;
	int	our_id;

	if (ahc_inb(ahc, SSTAT0) & TARGET)
		role = ROLE_TARGET;
	else
		role = ROLE_INITIATOR;

	if (role == ROLE_TARGET
	 && (ahc->features & AHC_MULTI_TID) != 0
	 && (ahc_inb(ahc, SEQ_FLAGS)
 	   & (CMDPHASE_PENDING|TARG_CMD_PENDING|NO_DISCONNECT)) != 0) {
		/* We were selected, so pull our id from TARGIDIN */
		our_id = ahc_inb(ahc, TARGIDIN) & OID;
	} else if ((ahc->features & AHC_ULTRA2) != 0)
		our_id = ahc_inb(ahc, SCSIID_ULTRA2) & OID;
	else
		our_id = ahc_inb(ahc, SCSIID) & OID;

	saved_scsiid = ahc_inb(ahc, SAVED_SCSIID);
	ahc_compile_devinfo(devinfo,
			    our_id,
			    SCSIID_TARGET(ahc, saved_scsiid),
			    ahc_inb(ahc, SAVED_LUN),
			    SCSIID_CHANNEL(ahc, saved_scsiid),
			    role);
}

static const struct ahc_phase_table_entry*
ahc_lookup_phase_entry(int phase)
{
	const struct ahc_phase_table_entry *entry;
	const struct ahc_phase_table_entry *last_entry;

	/*
	 * num_phases doesn't include the default entry which
	 * will be returned if the phase doesn't match.
	 */
	last_entry = &ahc_phase_table[num_phases];
	for (entry = ahc_phase_table; entry < last_entry; entry++) {
		if (phase == entry->phase)
			break;
	}
	return (entry);
}

void
ahc_compile_devinfo(struct ahc_devinfo *devinfo, u_int our_id, u_int target,
		    u_int lun, char channel, role_t role)
{
	devinfo->our_scsiid = our_id;
	devinfo->target = target;
	devinfo->lun = lun;
	devinfo->target_offset = target;
	devinfo->channel = channel;
	devinfo->role = role;
	if (channel == 'B')
		devinfo->target_offset += 8;
	devinfo->target_mask = (0x01 << devinfo->target_offset);
}

void
ahc_print_devinfo(struct ahc_softc *ahc, struct ahc_devinfo *devinfo)
{
	printk("%s:%c:%d:%d: ", ahc_name(ahc), devinfo->channel,
	       devinfo->target, devinfo->lun);
}

static void
ahc_scb_devinfo(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
		struct scb *scb)
{
	role_t	role;
	int	our_id;

	our_id = SCSIID_OUR_ID(scb->hscb->scsiid);
	role = ROLE_INITIATOR;
	if ((scb->flags & SCB_TARGET_SCB) != 0)
		role = ROLE_TARGET;
	ahc_compile_devinfo(devinfo, our_id, SCB_GET_TARGET(ahc, scb),
			    SCB_GET_LUN(scb), SCB_GET_CHANNEL(ahc, scb), role);
}


/************************ Message Phase Processing ****************************/
static void
ahc_assert_atn(struct ahc_softc *ahc)
{
	u_int scsisigo;

	scsisigo = ATNO;
	if ((ahc->features & AHC_DT) == 0)
		scsisigo |= ahc_inb(ahc, SCSISIGI);
	ahc_outb(ahc, SCSISIGO, scsisigo);
}

/*
 * When an initiator transaction with the MK_MESSAGE flag either reconnects
 * or enters the initial message out phase, we are interrupted.  Fill our
 * outgoing message buffer with the appropriate message and beging handing
 * the message phase(s) manually.
 */
static void
ahc_setup_initiator_msgout(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
			   struct scb *scb)
{
	/*
	 * To facilitate adding multiple messages together,
	 * each routine should increment the index and len
	 * variables instead of setting them explicitly.
	 */
	ahc->msgout_index = 0;
	ahc->msgout_len = 0;

	if ((scb->flags & SCB_DEVICE_RESET) == 0
	 && ahc_inb(ahc, MSG_OUT) == MSG_IDENTIFYFLAG) {
		u_int identify_msg;

		identify_msg = MSG_IDENTIFYFLAG | SCB_GET_LUN(scb);
		if ((scb->hscb->control & DISCENB) != 0)
			identify_msg |= MSG_IDENTIFY_DISCFLAG;
		ahc->msgout_buf[ahc->msgout_index++] = identify_msg;
		ahc->msgout_len++;

		if ((scb->hscb->control & TAG_ENB) != 0) {
			ahc->msgout_buf[ahc->msgout_index++] =
			    scb->hscb->control & (TAG_ENB|SCB_TAG_TYPE);
			ahc->msgout_buf[ahc->msgout_index++] = scb->hscb->tag;
			ahc->msgout_len += 2;
		}
	}

	if (scb->flags & SCB_DEVICE_RESET) {
		ahc->msgout_buf[ahc->msgout_index++] = MSG_BUS_DEV_RESET;
		ahc->msgout_len++;
		ahc_print_path(ahc, scb);
		printk("Bus Device Reset Message Sent\n");
		/*
		 * Clear our selection hardware in advance of
		 * the busfree.  We may have an entry in the waiting
		 * Q for this target, and we don't want to go about
		 * selecting while we handle the busfree and blow it
		 * away.
		 */
		ahc_outb(ahc, SCSISEQ, (ahc_inb(ahc, SCSISEQ) & ~ENSELO));
	} else if ((scb->flags & SCB_ABORT) != 0) {
		if ((scb->hscb->control & TAG_ENB) != 0)
			ahc->msgout_buf[ahc->msgout_index++] = MSG_ABORT_TAG;
		else
			ahc->msgout_buf[ahc->msgout_index++] = MSG_ABORT;
		ahc->msgout_len++;
		ahc_print_path(ahc, scb);
		printk("Abort%s Message Sent\n",
		       (scb->hscb->control & TAG_ENB) != 0 ? " Tag" : "");
		/*
		 * Clear our selection hardware in advance of
		 * the busfree.  We may have an entry in the waiting
		 * Q for this target, and we don't want to go about
		 * selecting while we handle the busfree and blow it
		 * away.
		 */
		ahc_outb(ahc, SCSISEQ, (ahc_inb(ahc, SCSISEQ) & ~ENSELO));
	} else if ((scb->flags & (SCB_AUTO_NEGOTIATE|SCB_NEGOTIATE)) != 0) {
		ahc_build_transfer_msg(ahc, devinfo);
	} else {
		printk("ahc_intr: AWAITING_MSG for an SCB that "
		       "does not have a waiting message\n");
		printk("SCSIID = %x, target_mask = %x\n", scb->hscb->scsiid,
		       devinfo->target_mask);
		panic("SCB = %d, SCB Control = %x, MSG_OUT = %x "
		      "SCB flags = %x", scb->hscb->tag, scb->hscb->control,
		      ahc_inb(ahc, MSG_OUT), scb->flags);
	}

	/*
	 * Clear the MK_MESSAGE flag from the SCB so we aren't
	 * asked to send this message again.
	 */
	ahc_outb(ahc, SCB_CONTROL, ahc_inb(ahc, SCB_CONTROL) & ~MK_MESSAGE);
	scb->hscb->control &= ~MK_MESSAGE;
	ahc->msgout_index = 0;
	ahc->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
}

/*
 * Build an appropriate transfer negotiation message for the
 * currently active target.
 */
static void
ahc_build_transfer_msg(struct ahc_softc *ahc, struct ahc_devinfo *devinfo)
{
	/*
	 * We need to initiate transfer negotiations.
	 * If our current and goal settings are identical,
	 * we want to renegotiate due to a check condition.
	 */
	struct	ahc_initiator_tinfo *tinfo;
	struct	ahc_tmode_tstate *tstate;
	const struct ahc_syncrate *rate;
	int	dowide;
	int	dosync;
	int	doppr;
	u_int	period;
	u_int	ppr_options;
	u_int	offset;

	tinfo = ahc_fetch_transinfo(ahc, devinfo->channel, devinfo->our_scsiid,
				    devinfo->target, &tstate);
	/*
	 * Filter our period based on the current connection.
	 * If we can't perform DT transfers on this segment (not in LVD
	 * mode for instance), then our decision to issue a PPR message
	 * may change.
	 */
	period = tinfo->goal.period;
	offset = tinfo->goal.offset;
	ppr_options = tinfo->goal.ppr_options;
	/* Target initiated PPR is not allowed in the SCSI spec */
	if (devinfo->role == ROLE_TARGET)
		ppr_options = 0;
	rate = ahc_devlimited_syncrate(ahc, tinfo, &period,
				       &ppr_options, devinfo->role);
	dowide = tinfo->curr.width != tinfo->goal.width;
	dosync = tinfo->curr.offset != offset || tinfo->curr.period != period;
	/*
	 * Only use PPR if we have options that need it, even if the device
	 * claims to support it.  There might be an expander in the way
	 * that doesn't.
	 */
	doppr = ppr_options != 0;

	if (!dowide && !dosync && !doppr) {
		dowide = tinfo->goal.width != MSG_EXT_WDTR_BUS_8_BIT;
		dosync = tinfo->goal.offset != 0;
	}

	if (!dowide && !dosync && !doppr) {
		/*
		 * Force async with a WDTR message if we have a wide bus,
		 * or just issue an SDTR with a 0 offset.
		 */
		if ((ahc->features & AHC_WIDE) != 0)
			dowide = 1;
		else
			dosync = 1;

		if (bootverbose) {
			ahc_print_devinfo(ahc, devinfo);
			printk("Ensuring async\n");
		}
	}

	/* Target initiated PPR is not allowed in the SCSI spec */
	if (devinfo->role == ROLE_TARGET)
		doppr = 0;

	/*
	 * Both the PPR message and SDTR message require the
	 * goal syncrate to be limited to what the target device
	 * is capable of handling (based on whether an LVD->SE
	 * expander is on the bus), so combine these two cases.
	 * Regardless, guarantee that if we are using WDTR and SDTR
	 * messages that WDTR comes first.
	 */
	if (doppr || (dosync && !dowide)) {

		offset = tinfo->goal.offset;
		ahc_validate_offset(ahc, tinfo, rate, &offset,
				    doppr ? tinfo->goal.width
					  : tinfo->curr.width,
				    devinfo->role);
		if (doppr) {
			ahc_construct_ppr(ahc, devinfo, period, offset,
					  tinfo->goal.width, ppr_options);
		} else {
			ahc_construct_sdtr(ahc, devinfo, period, offset);
		}
	} else {
		ahc_construct_wdtr(ahc, devinfo, tinfo->goal.width);
	}
}

/*
 * Build a synchronous negotiation message in our message
 * buffer based on the input parameters.
 */
static void
ahc_construct_sdtr(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
		   u_int period, u_int offset)
{
	if (offset == 0)
		period = AHC_ASYNC_XFER_PERIOD;
	ahc->msgout_index += spi_populate_sync_msg(
			ahc->msgout_buf + ahc->msgout_index, period, offset);
	ahc->msgout_len += 5;
	if (bootverbose) {
		printk("(%s:%c:%d:%d): Sending SDTR period %x, offset %x\n",
		       ahc_name(ahc), devinfo->channel, devinfo->target,
		       devinfo->lun, period, offset);
	}
}

/*
 * Build a wide negotiation message in our message
 * buffer based on the input parameters.
 */
static void
ahc_construct_wdtr(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
		   u_int bus_width)
{
	ahc->msgout_index += spi_populate_width_msg(
			ahc->msgout_buf + ahc->msgout_index, bus_width);
	ahc->msgout_len += 4;
	if (bootverbose) {
		printk("(%s:%c:%d:%d): Sending WDTR %x\n",
		       ahc_name(ahc), devinfo->channel, devinfo->target,
		       devinfo->lun, bus_width);
	}
}

/*
 * Build a parallel protocol request message in our message
 * buffer based on the input parameters.
 */
static void
ahc_construct_ppr(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
		  u_int period, u_int offset, u_int bus_width,
		  u_int ppr_options)
{
	if (offset == 0)
		period = AHC_ASYNC_XFER_PERIOD;
	ahc->msgout_index += spi_populate_ppr_msg(
			ahc->msgout_buf + ahc->msgout_index, period, offset,
			bus_width, ppr_options);
	ahc->msgout_len += 8;
	if (bootverbose) {
		printk("(%s:%c:%d:%d): Sending PPR bus_width %x, period %x, "
		       "offset %x, ppr_options %x\n", ahc_name(ahc),
		       devinfo->channel, devinfo->target, devinfo->lun,
		       bus_width, period, offset, ppr_options);
	}
}

/*
 * Clear any active message state.
 */
static void
ahc_clear_msg_state(struct ahc_softc *ahc)
{
	ahc->msgout_len = 0;
	ahc->msgin_index = 0;
	ahc->msg_type = MSG_TYPE_NONE;
	if ((ahc_inb(ahc, SCSISIGI) & ATNI) != 0) {
		/*
		 * The target didn't care to respond to our
		 * message request, so clear ATN.
		 */
		ahc_outb(ahc, CLRSINT1, CLRATNO);
	}
	ahc_outb(ahc, MSG_OUT, MSG_NOOP);
	ahc_outb(ahc, SEQ_FLAGS2,
		 ahc_inb(ahc, SEQ_FLAGS2) & ~TARGET_MSG_PENDING);
}

static void
ahc_handle_proto_violation(struct ahc_softc *ahc)
{
	struct	ahc_devinfo devinfo;
	struct	scb *scb;
	u_int	scbid;
	u_int	seq_flags;
	u_int	curphase;
	u_int	lastphase;
	int	found;

	ahc_fetch_devinfo(ahc, &devinfo);
	scbid = ahc_inb(ahc, SCB_TAG);
	scb = ahc_lookup_scb(ahc, scbid);
	seq_flags = ahc_inb(ahc, SEQ_FLAGS);
	curphase = ahc_inb(ahc, SCSISIGI) & PHASE_MASK;
	lastphase = ahc_inb(ahc, LASTPHASE);
	if ((seq_flags & NOT_IDENTIFIED) != 0) {

		/*
		 * The reconnecting target either did not send an
		 * identify message, or did, but we didn't find an SCB
		 * to match.
		 */
		ahc_print_devinfo(ahc, &devinfo);
		printk("Target did not send an IDENTIFY message. "
		       "LASTPHASE = 0x%x.\n", lastphase);
		scb = NULL;
	} else if (scb == NULL) {
		/*
		 * We don't seem to have an SCB active for this
		 * transaction.  Print an error and reset the bus.
		 */
		ahc_print_devinfo(ahc, &devinfo);
		printk("No SCB found during protocol violation\n");
		goto proto_violation_reset;
	} else {
		ahc_set_transaction_status(scb, CAM_SEQUENCE_FAIL);
		if ((seq_flags & NO_CDB_SENT) != 0) {
			ahc_print_path(ahc, scb);
			printk("No or incomplete CDB sent to device.\n");
		} else if ((ahc_inb(ahc, SCB_CONTROL) & STATUS_RCVD) == 0) {
			/*
			 * The target never bothered to provide status to
			 * us prior to completing the command.  Since we don't
			 * know the disposition of this command, we must attempt
			 * to abort it.  Assert ATN and prepare to send an abort
			 * message.
			 */
			ahc_print_path(ahc, scb);
			printk("Completed command without status.\n");
		} else {
			ahc_print_path(ahc, scb);
			printk("Unknown protocol violation.\n");
			ahc_dump_card_state(ahc);
		}
	}
	if ((lastphase & ~P_DATAIN_DT) == 0
	 || lastphase == P_COMMAND) {
proto_violation_reset:
		/*
		 * Target either went directly to data/command
		 * phase or didn't respond to our ATN.
		 * The only safe thing to do is to blow
		 * it away with a bus reset.
		 */
		found = ahc_reset_channel(ahc, 'A', TRUE);
		printk("%s: Issued Channel %c Bus Reset. "
		       "%d SCBs aborted\n", ahc_name(ahc), 'A', found);
	} else {
		/*
		 * Leave the selection hardware off in case
		 * this abort attempt will affect yet to
		 * be sent commands.
		 */
		ahc_outb(ahc, SCSISEQ,
			 ahc_inb(ahc, SCSISEQ) & ~ENSELO);
		ahc_assert_atn(ahc);
		ahc_outb(ahc, MSG_OUT, HOST_MSG);
		if (scb == NULL) {
			ahc_print_devinfo(ahc, &devinfo);
			ahc->msgout_buf[0] = MSG_ABORT_TASK;
			ahc->msgout_len = 1;
			ahc->msgout_index = 0;
			ahc->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
		} else {
			ahc_print_path(ahc, scb);
			scb->flags |= SCB_ABORT;
		}
		printk("Protocol violation %s.  Attempting to abort.\n",
		       ahc_lookup_phase_entry(curphase)->phasemsg);
	}
}

/*
 * Manual message loop handler.
 */
static void
ahc_handle_message_phase(struct ahc_softc *ahc)
{
	struct	ahc_devinfo devinfo;
	u_int	bus_phase;
	int	end_session;

	ahc_fetch_devinfo(ahc, &devinfo);
	end_session = FALSE;
	bus_phase = ahc_inb(ahc, SCSISIGI) & PHASE_MASK;

reswitch:
	switch (ahc->msg_type) {
	case MSG_TYPE_INITIATOR_MSGOUT:
	{
		int lastbyte;
		int phasemis;
		int msgdone;

		if (ahc->msgout_len == 0)
			panic("HOST_MSG_LOOP interrupt with no active message");

#ifdef AHC_DEBUG
		if ((ahc_debug & AHC_SHOW_MESSAGES) != 0) {
			ahc_print_devinfo(ahc, &devinfo);
			printk("INITIATOR_MSG_OUT");
		}
#endif
		phasemis = bus_phase != P_MESGOUT;
		if (phasemis) {
#ifdef AHC_DEBUG
			if ((ahc_debug & AHC_SHOW_MESSAGES) != 0) {
				printk(" PHASEMIS %s\n",
				       ahc_lookup_phase_entry(bus_phase)
							     ->phasemsg);
			}
#endif
			if (bus_phase == P_MESGIN) {
				/*
				 * Change gears and see if
				 * this messages is of interest to
				 * us or should be passed back to
				 * the sequencer.
				 */
				ahc_outb(ahc, CLRSINT1, CLRATNO);
				ahc->send_msg_perror = FALSE;
				ahc->msg_type = MSG_TYPE_INITIATOR_MSGIN;
				ahc->msgin_index = 0;
				goto reswitch;
			}
			end_session = TRUE;
			break;
		}

		if (ahc->send_msg_perror) {
			ahc_outb(ahc, CLRSINT1, CLRATNO);
			ahc_outb(ahc, CLRSINT1, CLRREQINIT);
#ifdef AHC_DEBUG
			if ((ahc_debug & AHC_SHOW_MESSAGES) != 0)
				printk(" byte 0x%x\n", ahc->send_msg_perror);
#endif
			ahc_outb(ahc, SCSIDATL, MSG_PARITY_ERROR);
			break;
		}

		msgdone	= ahc->msgout_index == ahc->msgout_len;
		if (msgdone) {
			/*
			 * The target has requested a retry.
			 * Re-assert ATN, reset our message index to
			 * 0, and try again.
			 */
			ahc->msgout_index = 0;
			ahc_assert_atn(ahc);
		}

		lastbyte = ahc->msgout_index == (ahc->msgout_len - 1);
		if (lastbyte) {
			/* Last byte is signified by dropping ATN */
			ahc_outb(ahc, CLRSINT1, CLRATNO);
		}

		/*
		 * Clear our interrupt status and present
		 * the next byte on the bus.
		 */
		ahc_outb(ahc, CLRSINT1, CLRREQINIT);
#ifdef AHC_DEBUG
		if ((ahc_debug & AHC_SHOW_MESSAGES) != 0)
			printk(" byte 0x%x\n",
			       ahc->msgout_buf[ahc->msgout_index]);
#endif
		ahc_outb(ahc, SCSIDATL, ahc->msgout_buf[ahc->msgout_index++]);
		break;
	}
	case MSG_TYPE_INITIATOR_MSGIN:
	{
		int phasemis;
		int message_done;

#ifdef AHC_DEBUG
		if ((ahc_debug & AHC_SHOW_MESSAGES) != 0) {
			ahc_print_devinfo(ahc, &devinfo);
			printk("INITIATOR_MSG_IN");
		}
#endif
		phasemis = bus_phase != P_MESGIN;
		if (phasemis) {
#ifdef AHC_DEBUG
			if ((ahc_debug & AHC_SHOW_MESSAGES) != 0) {
				printk(" PHASEMIS %s\n",
				       ahc_lookup_phase_entry(bus_phase)
							     ->phasemsg);
			}
#endif
			ahc->msgin_index = 0;
			if (bus_phase == P_MESGOUT
			 && (ahc->send_msg_perror == TRUE
			  || (ahc->msgout_len != 0
			   && ahc->msgout_index == 0))) {
				ahc->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
				goto reswitch;
			}
			end_session = TRUE;
			break;
		}

		/* Pull the byte in without acking it */
		ahc->msgin_buf[ahc->msgin_index] = ahc_inb(ahc, SCSIBUSL);
#ifdef AHC_DEBUG
		if ((ahc_debug & AHC_SHOW_MESSAGES) != 0)
			printk(" byte 0x%x\n",
			       ahc->msgin_buf[ahc->msgin_index]);
#endif

		message_done = ahc_parse_msg(ahc, &devinfo);

		if (message_done) {
			/*
			 * Clear our incoming message buffer in case there
			 * is another message following this one.
			 */
			ahc->msgin_index = 0;

			/*
			 * If this message illicited a response,
			 * assert ATN so the target takes us to the
			 * message out phase.
			 */
			if (ahc->msgout_len != 0) {
#ifdef AHC_DEBUG
				if ((ahc_debug & AHC_SHOW_MESSAGES) != 0) {
					ahc_print_devinfo(ahc, &devinfo);
					printk("Asserting ATN for response\n");
				}
#endif
				ahc_assert_atn(ahc);
			}
		} else 
			ahc->msgin_index++;

		if (message_done == MSGLOOP_TERMINATED) {
			end_session = TRUE;
		} else {
			/* Ack the byte */
			ahc_outb(ahc, CLRSINT1, CLRREQINIT);
			ahc_inb(ahc, SCSIDATL);
		}
		break;
	}
	case MSG_TYPE_TARGET_MSGIN:
	{
		int msgdone;
		int msgout_request;

		if (ahc->msgout_len == 0)
			panic("Target MSGIN with no active message");

		/*
		 * If we interrupted a mesgout session, the initiator
		 * will not know this until our first REQ.  So, we
		 * only honor mesgout requests after we've sent our
		 * first byte.
		 */
		if ((ahc_inb(ahc, SCSISIGI) & ATNI) != 0
		 && ahc->msgout_index > 0)
			msgout_request = TRUE;
		else
			msgout_request = FALSE;

		if (msgout_request) {

			/*
			 * Change gears and see if
			 * this messages is of interest to
			 * us or should be passed back to
			 * the sequencer.
			 */
			ahc->msg_type = MSG_TYPE_TARGET_MSGOUT;
			ahc_outb(ahc, SCSISIGO, P_MESGOUT | BSYO);
			ahc->msgin_index = 0;
			/* Dummy read to REQ for first byte */
			ahc_inb(ahc, SCSIDATL);
			ahc_outb(ahc, SXFRCTL0,
				 ahc_inb(ahc, SXFRCTL0) | SPIOEN);
			break;
		}

		msgdone = ahc->msgout_index == ahc->msgout_len;
		if (msgdone) {
			ahc_outb(ahc, SXFRCTL0,
				 ahc_inb(ahc, SXFRCTL0) & ~SPIOEN);
			end_session = TRUE;
			break;
		}

		/*
		 * Present the next byte on the bus.
		 */
		ahc_outb(ahc, SXFRCTL0, ahc_inb(ahc, SXFRCTL0) | SPIOEN);
		ahc_outb(ahc, SCSIDATL, ahc->msgout_buf[ahc->msgout_index++]);
		break;
	}
	case MSG_TYPE_TARGET_MSGOUT:
	{
		int lastbyte;
		int msgdone;

		/*
		 * The initiator signals that this is
		 * the last byte by dropping ATN.
		 */
		lastbyte = (ahc_inb(ahc, SCSISIGI) & ATNI) == 0;

		/*
		 * Read the latched byte, but turn off SPIOEN first
		 * so that we don't inadvertently cause a REQ for the
		 * next byte.
		 */
		ahc_outb(ahc, SXFRCTL0, ahc_inb(ahc, SXFRCTL0) & ~SPIOEN);
		ahc->msgin_buf[ahc->msgin_index] = ahc_inb(ahc, SCSIDATL);
		msgdone = ahc_parse_msg(ahc, &devinfo);
		if (msgdone == MSGLOOP_TERMINATED) {
			/*
			 * The message is *really* done in that it caused
			 * us to go to bus free.  The sequencer has already
			 * been reset at this point, so pull the ejection
			 * handle.
			 */
			return;
		}
		
		ahc->msgin_index++;

		/*
		 * XXX Read spec about initiator dropping ATN too soon
		 *     and use msgdone to detect it.
		 */
		if (msgdone == MSGLOOP_MSGCOMPLETE) {
			ahc->msgin_index = 0;

			/*
			 * If this message illicited a response, transition
			 * to the Message in phase and send it.
			 */
			if (ahc->msgout_len != 0) {
				ahc_outb(ahc, SCSISIGO, P_MESGIN | BSYO);
				ahc_outb(ahc, SXFRCTL0,
					 ahc_inb(ahc, SXFRCTL0) | SPIOEN);
				ahc->msg_type = MSG_TYPE_TARGET_MSGIN;
				ahc->msgin_index = 0;
				break;
			}
		}

		if (lastbyte)
			end_session = TRUE;
		else {
			/* Ask for the next byte. */
			ahc_outb(ahc, SXFRCTL0,
				 ahc_inb(ahc, SXFRCTL0) | SPIOEN);
		}

		break;
	}
	default:
		panic("Unknown REQINIT message type");
	}

	if (end_session) {
		ahc_clear_msg_state(ahc);
		ahc_outb(ahc, RETURN_1, EXIT_MSG_LOOP);
	} else
		ahc_outb(ahc, RETURN_1, CONT_MSG_LOOP);
}

/*
 * See if we sent a particular extended message to the target.
 * If "full" is true, return true only if the target saw the full
 * message.  If "full" is false, return true if the target saw at
 * least the first byte of the message.
 */
static int
ahc_sent_msg(struct ahc_softc *ahc, ahc_msgtype type, u_int msgval, int full)
{
	int found;
	u_int index;

	found = FALSE;
	index = 0;

	while (index < ahc->msgout_len) {
		if (ahc->msgout_buf[index] == MSG_EXTENDED) {
			u_int end_index;

			end_index = index + 1 + ahc->msgout_buf[index + 1];
			if (ahc->msgout_buf[index+2] == msgval
			 && type == AHCMSG_EXT) {

				if (full) {
					if (ahc->msgout_index > end_index)
						found = TRUE;
				} else if (ahc->msgout_index > index)
					found = TRUE;
			}
			index = end_index;
		} else if (ahc->msgout_buf[index] >= MSG_SIMPLE_TASK
			&& ahc->msgout_buf[index] <= MSG_IGN_WIDE_RESIDUE) {

			/* Skip tag type and tag id or residue param*/
			index += 2;
		} else {
			/* Single byte message */
			if (type == AHCMSG_1B
			 && ahc->msgout_buf[index] == msgval
			 && ahc->msgout_index > index)
				found = TRUE;
			index++;
		}

		if (found)
			break;
	}
	return (found);
}

/*
 * Wait for a complete incoming message, parse it, and respond accordingly.
 */
static int
ahc_parse_msg(struct ahc_softc *ahc, struct ahc_devinfo *devinfo)
{
	struct	ahc_initiator_tinfo *tinfo;
	struct	ahc_tmode_tstate *tstate;
	int	reject;
	int	done;
	int	response;
	u_int	targ_scsirate;

	done = MSGLOOP_IN_PROG;
	response = FALSE;
	reject = FALSE;
	tinfo = ahc_fetch_transinfo(ahc, devinfo->channel, devinfo->our_scsiid,
				    devinfo->target, &tstate);
	targ_scsirate = tinfo->scsirate;

	/*
	 * Parse as much of the message as is available,
	 * rejecting it if we don't support it.  When
	 * the entire message is available and has been
	 * handled, return MSGLOOP_MSGCOMPLETE, indicating
	 * that we have parsed an entire message.
	 *
	 * In the case of extended messages, we accept the length
	 * byte outright and perform more checking once we know the
	 * extended message type.
	 */
	switch (ahc->msgin_buf[0]) {
	case MSG_DISCONNECT:
	case MSG_SAVEDATAPOINTER:
	case MSG_CMDCOMPLETE:
	case MSG_RESTOREPOINTERS:
	case MSG_IGN_WIDE_RESIDUE:
		/*
		 * End our message loop as these are messages
		 * the sequencer handles on its own.
		 */
		done = MSGLOOP_TERMINATED;
		break;
	case MSG_MESSAGE_REJECT:
		response = ahc_handle_msg_reject(ahc, devinfo);
		/* FALLTHROUGH */
	case MSG_NOOP:
		done = MSGLOOP_MSGCOMPLETE;
		break;
	case MSG_EXTENDED:
	{
		/* Wait for enough of the message to begin validation */
		if (ahc->msgin_index < 2)
			break;
		switch (ahc->msgin_buf[2]) {
		case MSG_EXT_SDTR:
		{
			const struct ahc_syncrate *syncrate;
			u_int	 period;
			u_int	 ppr_options;
			u_int	 offset;
			u_int	 saved_offset;
			
			if (ahc->msgin_buf[1] != MSG_EXT_SDTR_LEN) {
				reject = TRUE;
				break;
			}

			/*
			 * Wait until we have both args before validating
			 * and acting on this message.
			 *
			 * Add one to MSG_EXT_SDTR_LEN to account for
			 * the extended message preamble.
			 */
			if (ahc->msgin_index < (MSG_EXT_SDTR_LEN + 1))
				break;

			period = ahc->msgin_buf[3];
			ppr_options = 0;
			saved_offset = offset = ahc->msgin_buf[4];
			syncrate = ahc_devlimited_syncrate(ahc, tinfo, &period,
							   &ppr_options,
							   devinfo->role);
			ahc_validate_offset(ahc, tinfo, syncrate, &offset,
					    targ_scsirate & WIDEXFER,
					    devinfo->role);
			if (bootverbose) {
				printk("(%s:%c:%d:%d): Received "
				       "SDTR period %x, offset %x\n\t"
				       "Filtered to period %x, offset %x\n",
				       ahc_name(ahc), devinfo->channel,
				       devinfo->target, devinfo->lun,
				       ahc->msgin_buf[3], saved_offset,
				       period, offset);
			}
			ahc_set_syncrate(ahc, devinfo, 
					 syncrate, period,
					 offset, ppr_options,
					 AHC_TRANS_ACTIVE|AHC_TRANS_GOAL,
					 /*paused*/TRUE);

			/*
			 * See if we initiated Sync Negotiation
			 * and didn't have to fall down to async
			 * transfers.
			 */
			if (ahc_sent_msg(ahc, AHCMSG_EXT, MSG_EXT_SDTR, TRUE)) {
				/* We started it */
				if (saved_offset != offset) {
					/* Went too low - force async */
					reject = TRUE;
				}
			} else {
				/*
				 * Send our own SDTR in reply
				 */
				if (bootverbose
				 && devinfo->role == ROLE_INITIATOR) {
					printk("(%s:%c:%d:%d): Target "
					       "Initiated SDTR\n",
					       ahc_name(ahc), devinfo->channel,
					       devinfo->target, devinfo->lun);
				}
				ahc->msgout_index = 0;
				ahc->msgout_len = 0;
				ahc_construct_sdtr(ahc, devinfo,
						   period, offset);
				ahc->msgout_index = 0;
				response = TRUE;
			}
			done = MSGLOOP_MSGCOMPLETE;
			break;
		}
		case MSG_EXT_WDTR:
		{
			u_int bus_width;
			u_int saved_width;
			u_int sending_reply;

			sending_reply = FALSE;
			if (ahc->msgin_buf[1] != MSG_EXT_WDTR_LEN) {
				reject = TRUE;
				break;
			}

			/*
			 * Wait until we have our arg before validating
			 * and acting on this message.
			 *
			 * Add one to MSG_EXT_WDTR_LEN to account for
			 * the extended message preamble.
			 */
			if (ahc->msgin_index < (MSG_EXT_WDTR_LEN + 1))
				break;

			bus_width = ahc->msgin_buf[3];
			saved_width = bus_width;
			ahc_validate_width(ahc, tinfo, &bus_width,
					   devinfo->role);
			if (bootverbose) {
				printk("(%s:%c:%d:%d): Received WDTR "
				       "%x filtered to %x\n",
				       ahc_name(ahc), devinfo->channel,
				       devinfo->target, devinfo->lun,
				       saved_width, bus_width);
			}

			if (ahc_sent_msg(ahc, AHCMSG_EXT, MSG_EXT_WDTR, TRUE)) {
				/*
				 * Don't send a WDTR back to the
				 * target, since we asked first.
				 * If the width went higher than our
				 * request, reject it.
				 */
				if (saved_width > bus_width) {
					reject = TRUE;
					printk("(%s:%c:%d:%d): requested %dBit "
					       "transfers.  Rejecting...\n",
					       ahc_name(ahc), devinfo->channel,
					       devinfo->target, devinfo->lun,
					       8 * (0x01 << bus_width));
					bus_width = 0;
				}
			} else {
				/*
				 * Send our own WDTR in reply
				 */
				if (bootverbose
				 && devinfo->role == ROLE_INITIATOR) {
					printk("(%s:%c:%d:%d): Target "
					       "Initiated WDTR\n",
					       ahc_name(ahc), devinfo->channel,
					       devinfo->target, devinfo->lun);
				}
				ahc->msgout_index = 0;
				ahc->msgout_len = 0;
				ahc_construct_wdtr(ahc, devinfo, bus_width);
				ahc->msgout_index = 0;
				response = TRUE;
				sending_reply = TRUE;
			}
			/*
			 * After a wide message, we are async, but
			 * some devices don't seem to honor this portion
			 * of the spec.  Force a renegotiation of the
			 * sync component of our transfer agreement even
			 * if our goal is async.  By updating our width
			 * after forcing the negotiation, we avoid
			 * renegotiating for width.
			 */
			ahc_update_neg_request(ahc, devinfo, tstate,
					       tinfo, AHC_NEG_ALWAYS);
			ahc_set_width(ahc, devinfo, bus_width,
				      AHC_TRANS_ACTIVE|AHC_TRANS_GOAL,
				      /*paused*/TRUE);
			if (sending_reply == FALSE && reject == FALSE) {

				/*
				 * We will always have an SDTR to send.
				 */
				ahc->msgout_index = 0;
				ahc->msgout_len = 0;
				ahc_build_transfer_msg(ahc, devinfo);
				ahc->msgout_index = 0;
				response = TRUE;
			}
			done = MSGLOOP_MSGCOMPLETE;
			break;
		}
		case MSG_EXT_PPR:
		{
			const struct ahc_syncrate *syncrate;
			u_int	period;
			u_int	offset;
			u_int	bus_width;
			u_int	ppr_options;
			u_int	saved_width;
			u_int	saved_offset;
			u_int	saved_ppr_options;

			if (ahc->msgin_buf[1] != MSG_EXT_PPR_LEN) {
				reject = TRUE;
				break;
			}

			/*
			 * Wait until we have all args before validating
			 * and acting on this message.
			 *
			 * Add one to MSG_EXT_PPR_LEN to account for
			 * the extended message preamble.
			 */
			if (ahc->msgin_index < (MSG_EXT_PPR_LEN + 1))
				break;

			period = ahc->msgin_buf[3];
			offset = ahc->msgin_buf[5];
			bus_width = ahc->msgin_buf[6];
			saved_width = bus_width;
			ppr_options = ahc->msgin_buf[7];
			/*
			 * According to the spec, a DT only
			 * period factor with no DT option
			 * set implies async.
			 */
			if ((ppr_options & MSG_EXT_PPR_DT_REQ) == 0
			 && period == 9)
				offset = 0;
			saved_ppr_options = ppr_options;
			saved_offset = offset;

			/*
			 * Mask out any options we don't support
			 * on any controller.  Transfer options are
			 * only available if we are negotiating wide.
			 */
			ppr_options &= MSG_EXT_PPR_DT_REQ;
			if (bus_width == 0)
				ppr_options = 0;

			ahc_validate_width(ahc, tinfo, &bus_width,
					   devinfo->role);
			syncrate = ahc_devlimited_syncrate(ahc, tinfo, &period,
							   &ppr_options,
							   devinfo->role);
			ahc_validate_offset(ahc, tinfo, syncrate,
					    &offset, bus_width,
					    devinfo->role);

			if (ahc_sent_msg(ahc, AHCMSG_EXT, MSG_EXT_PPR, TRUE)) {
				/*
				 * If we are unable to do any of the
				 * requested options (we went too low),
				 * then we'll have to reject the message.
				 */
				if (saved_width > bus_width
				 || saved_offset != offset
				 || saved_ppr_options != ppr_options) {
					reject = TRUE;
					period = 0;
					offset = 0;
					bus_width = 0;
					ppr_options = 0;
					syncrate = NULL;
				}
			} else {
				if (devinfo->role != ROLE_TARGET)
					printk("(%s:%c:%d:%d): Target "
					       "Initiated PPR\n",
					       ahc_name(ahc), devinfo->channel,
					       devinfo->target, devinfo->lun);
				else
					printk("(%s:%c:%d:%d): Initiator "
					       "Initiated PPR\n",
					       ahc_name(ahc), devinfo->channel,
					       devinfo->target, devinfo->lun);
				ahc->msgout_index = 0;
				ahc->msgout_len = 0;
				ahc_construct_ppr(ahc, devinfo, period, offset,
						  bus_width, ppr_options);
				ahc->msgout_index = 0;
				response = TRUE;
			}
			if (bootverbose) {
				printk("(%s:%c:%d:%d): Received PPR width %x, "
				       "period %x, offset %x,options %x\n"
				       "\tFiltered to width %x, period %x, "
				       "offset %x, options %x\n",
				       ahc_name(ahc), devinfo->channel,
				       devinfo->target, devinfo->lun,
				       saved_width, ahc->msgin_buf[3],
				       saved_offset, saved_ppr_options,
				       bus_width, period, offset, ppr_options);
			}
			ahc_set_width(ahc, devinfo, bus_width,
				      AHC_TRANS_ACTIVE|AHC_TRANS_GOAL,
				      /*paused*/TRUE);
			ahc_set_syncrate(ahc, devinfo,
					 syncrate, period,
					 offset, ppr_options,
					 AHC_TRANS_ACTIVE|AHC_TRANS_GOAL,
					 /*paused*/TRUE);
			done = MSGLOOP_MSGCOMPLETE;
			break;
		}
		default:
			/* Unknown extended message.  Reject it. */
			reject = TRUE;
			break;
		}
		break;
	}
#ifdef AHC_TARGET_MODE
	case MSG_BUS_DEV_RESET:
		ahc_handle_devreset(ahc, devinfo,
				    CAM_BDR_SENT,
				    "Bus Device Reset Received",
				    /*verbose_level*/0);
		ahc_restart(ahc);
		done = MSGLOOP_TERMINATED;
		break;
	case MSG_ABORT_TAG:
	case MSG_ABORT:
	case MSG_CLEAR_QUEUE:
	{
		int tag;

		/* Target mode messages */
		if (devinfo->role != ROLE_TARGET) {
			reject = TRUE;
			break;
		}
		tag = SCB_LIST_NULL;
		if (ahc->msgin_buf[0] == MSG_ABORT_TAG)
			tag = ahc_inb(ahc, INITIATOR_TAG);
		ahc_abort_scbs(ahc, devinfo->target, devinfo->channel,
			       devinfo->lun, tag, ROLE_TARGET,
			       CAM_REQ_ABORTED);

		tstate = ahc->enabled_targets[devinfo->our_scsiid];
		if (tstate != NULL) {
			struct ahc_tmode_lstate* lstate;

			lstate = tstate->enabled_luns[devinfo->lun];
			if (lstate != NULL) {
				ahc_queue_lstate_event(ahc, lstate,
						       devinfo->our_scsiid,
						       ahc->msgin_buf[0],
						       /*arg*/tag);
				ahc_send_lstate_events(ahc, lstate);
			}
		}
		ahc_restart(ahc);
		done = MSGLOOP_TERMINATED;
		break;
	}
#endif
	case MSG_TERM_IO_PROC:
	default:
		reject = TRUE;
		break;
	}

	if (reject) {
		/*
		 * Setup to reject the message.
		 */
		ahc->msgout_index = 0;
		ahc->msgout_len = 1;
		ahc->msgout_buf[0] = MSG_MESSAGE_REJECT;
		done = MSGLOOP_MSGCOMPLETE;
		response = TRUE;
	}

	if (done != MSGLOOP_IN_PROG && !response)
		/* Clear the outgoing message buffer */
		ahc->msgout_len = 0;

	return (done);
}

/*
 * Process a message reject message.
 */
static int
ahc_handle_msg_reject(struct ahc_softc *ahc, struct ahc_devinfo *devinfo)
{
	/*
	 * What we care about here is if we had an
	 * outstanding SDTR or WDTR message for this
	 * target.  If we did, this is a signal that
	 * the target is refusing negotiation.
	 */
	struct scb *scb;
	struct ahc_initiator_tinfo *tinfo;
	struct ahc_tmode_tstate *tstate;
	u_int scb_index;
	u_int last_msg;
	int   response = 0;

	scb_index = ahc_inb(ahc, SCB_TAG);
	scb = ahc_lookup_scb(ahc, scb_index);
	tinfo = ahc_fetch_transinfo(ahc, devinfo->channel,
				    devinfo->our_scsiid,
				    devinfo->target, &tstate);
	/* Might be necessary */
	last_msg = ahc_inb(ahc, LAST_MSG);

	if (ahc_sent_msg(ahc, AHCMSG_EXT, MSG_EXT_PPR, /*full*/FALSE)) {
		/*
		 * Target does not support the PPR message.
		 * Attempt to negotiate SPI-2 style.
		 */
		if (bootverbose) {
			printk("(%s:%c:%d:%d): PPR Rejected. "
			       "Trying WDTR/SDTR\n",
			       ahc_name(ahc), devinfo->channel,
			       devinfo->target, devinfo->lun);
		}
		tinfo->goal.ppr_options = 0;
		tinfo->curr.transport_version = 2;
		tinfo->goal.transport_version = 2;
		ahc->msgout_index = 0;
		ahc->msgout_len = 0;
		ahc_build_transfer_msg(ahc, devinfo);
		ahc->msgout_index = 0;
		response = 1;
	} else if (ahc_sent_msg(ahc, AHCMSG_EXT, MSG_EXT_WDTR, /*full*/FALSE)) {

		/* note 8bit xfers */
		printk("(%s:%c:%d:%d): refuses WIDE negotiation.  Using "
		       "8bit transfers\n", ahc_name(ahc),
		       devinfo->channel, devinfo->target, devinfo->lun);
		ahc_set_width(ahc, devinfo, MSG_EXT_WDTR_BUS_8_BIT,
			      AHC_TRANS_ACTIVE|AHC_TRANS_GOAL,
			      /*paused*/TRUE);
		/*
		 * No need to clear the sync rate.  If the target
		 * did not accept the command, our syncrate is
		 * unaffected.  If the target started the negotiation,
		 * but rejected our response, we already cleared the
		 * sync rate before sending our WDTR.
		 */
		if (tinfo->goal.offset != tinfo->curr.offset) {

			/* Start the sync negotiation */
			ahc->msgout_index = 0;
			ahc->msgout_len = 0;
			ahc_build_transfer_msg(ahc, devinfo);
			ahc->msgout_index = 0;
			response = 1;
		}
	} else if (ahc_sent_msg(ahc, AHCMSG_EXT, MSG_EXT_SDTR, /*full*/FALSE)) {
		/* note asynch xfers and clear flag */
		ahc_set_syncrate(ahc, devinfo, /*syncrate*/NULL, /*period*/0,
				 /*offset*/0, /*ppr_options*/0,
				 AHC_TRANS_ACTIVE|AHC_TRANS_GOAL,
				 /*paused*/TRUE);
		printk("(%s:%c:%d:%d): refuses synchronous negotiation. "
		       "Using asynchronous transfers\n",
		       ahc_name(ahc), devinfo->channel,
		       devinfo->target, devinfo->lun);
	} else if ((scb->hscb->control & MSG_SIMPLE_TASK) != 0) {
		int tag_type;
		int mask;

		tag_type = (scb->hscb->control & MSG_SIMPLE_TASK);

		if (tag_type == MSG_SIMPLE_TASK) {
			printk("(%s:%c:%d:%d): refuses tagged commands.  "
			       "Performing non-tagged I/O\n", ahc_name(ahc),
			       devinfo->channel, devinfo->target, devinfo->lun);
			ahc_set_tags(ahc, scb->io_ctx, devinfo, AHC_QUEUE_NONE);
			mask = ~0x23;
		} else {
			printk("(%s:%c:%d:%d): refuses %s tagged commands.  "
			       "Performing simple queue tagged I/O only\n",
			       ahc_name(ahc), devinfo->channel, devinfo->target,
			       devinfo->lun, tag_type == MSG_ORDERED_TASK
			       ? "ordered" : "head of queue");
			ahc_set_tags(ahc, scb->io_ctx, devinfo, AHC_QUEUE_BASIC);
			mask = ~0x03;
		}

		/*
		 * Resend the identify for this CCB as the target
		 * may believe that the selection is invalid otherwise.
		 */
		ahc_outb(ahc, SCB_CONTROL,
			 ahc_inb(ahc, SCB_CONTROL) & mask);
	 	scb->hscb->control &= mask;
		ahc_set_transaction_tag(scb, /*enabled*/FALSE,
					/*type*/MSG_SIMPLE_TASK);
		ahc_outb(ahc, MSG_OUT, MSG_IDENTIFYFLAG);
		ahc_assert_atn(ahc);

		/*
		 * This transaction is now at the head of
		 * the untagged queue for this target.
		 */
		if ((ahc->flags & AHC_SCB_BTT) == 0) {
			struct scb_tailq *untagged_q;

			untagged_q =
			    &(ahc->untagged_queues[devinfo->target_offset]);
			TAILQ_INSERT_HEAD(untagged_q, scb, links.tqe);
			scb->flags |= SCB_UNTAGGEDQ;
		}
		ahc_busy_tcl(ahc, BUILD_TCL(scb->hscb->scsiid, devinfo->lun),
			     scb->hscb->tag);

		/*
		 * Requeue all tagged commands for this target
		 * currently in our possession so they can be
		 * converted to untagged commands.
		 */
		ahc_search_qinfifo(ahc, SCB_GET_TARGET(ahc, scb),
				   SCB_GET_CHANNEL(ahc, scb),
				   SCB_GET_LUN(scb), /*tag*/SCB_LIST_NULL,
				   ROLE_INITIATOR, CAM_REQUEUE_REQ,
				   SEARCH_COMPLETE);
	} else {
		/*
		 * Otherwise, we ignore it.
		 */
		printk("%s:%c:%d: Message reject for %x -- ignored\n",
		       ahc_name(ahc), devinfo->channel, devinfo->target,
		       last_msg);
	}
	return (response);
}

/*
 * Process an ingnore wide residue message.
 */
static void
ahc_handle_ign_wide_residue(struct ahc_softc *ahc, struct ahc_devinfo *devinfo)
{
	u_int scb_index;
	struct scb *scb;

	scb_index = ahc_inb(ahc, SCB_TAG);
	scb = ahc_lookup_scb(ahc, scb_index);
	/*
	 * XXX Actually check data direction in the sequencer?
	 * Perhaps add datadir to some spare bits in the hscb?
	 */
	if ((ahc_inb(ahc, SEQ_FLAGS) & DPHASE) == 0
	 || ahc_get_transfer_dir(scb) != CAM_DIR_IN) {
		/*
		 * Ignore the message if we haven't
		 * seen an appropriate data phase yet.
		 */
	} else {
		/*
		 * If the residual occurred on the last
		 * transfer and the transfer request was
		 * expected to end on an odd count, do
		 * nothing.  Otherwise, subtract a byte
		 * and update the residual count accordingly.
		 */
		uint32_t sgptr;

		sgptr = ahc_inb(ahc, SCB_RESIDUAL_SGPTR);
		if ((sgptr & SG_LIST_NULL) != 0
		 && (ahc_inb(ahc, SCB_LUN) & SCB_XFERLEN_ODD) != 0) {
			/*
			 * If the residual occurred on the last
			 * transfer and the transfer request was
			 * expected to end on an odd count, do
			 * nothing.
			 */
		} else {
			struct ahc_dma_seg *sg;
			uint32_t data_cnt;
			uint32_t data_addr;
			uint32_t sglen;

			/* Pull in all of the sgptr */
			sgptr = ahc_inl(ahc, SCB_RESIDUAL_SGPTR);
			data_cnt = ahc_inl(ahc, SCB_RESIDUAL_DATACNT);

			if ((sgptr & SG_LIST_NULL) != 0) {
				/*
				 * The residual data count is not updated
				 * for the command run to completion case.
				 * Explicitly zero the count.
				 */
				data_cnt &= ~AHC_SG_LEN_MASK;
			}

			data_addr = ahc_inl(ahc, SHADDR);

			data_cnt += 1;
			data_addr -= 1;
			sgptr &= SG_PTR_MASK;

			sg = ahc_sg_bus_to_virt(scb, sgptr);

			/*
			 * The residual sg ptr points to the next S/G
			 * to load so we must go back one.
			 */
			sg--;
			sglen = ahc_le32toh(sg->len) & AHC_SG_LEN_MASK;
			if (sg != scb->sg_list
			 && sglen < (data_cnt & AHC_SG_LEN_MASK)) {

				sg--;
				sglen = ahc_le32toh(sg->len);
				/*
				 * Preserve High Address and SG_LIST bits
				 * while setting the count to 1.
				 */
				data_cnt = 1 | (sglen & (~AHC_SG_LEN_MASK));
				data_addr = ahc_le32toh(sg->addr)
					  + (sglen & AHC_SG_LEN_MASK) - 1;

				/*
				 * Increment sg so it points to the
				 * "next" sg.
				 */
				sg++;
				sgptr = ahc_sg_virt_to_bus(scb, sg);
			}
			ahc_outl(ahc, SCB_RESIDUAL_SGPTR, sgptr);
			ahc_outl(ahc, SCB_RESIDUAL_DATACNT, data_cnt);
			/*
			 * Toggle the "oddness" of the transfer length
			 * to handle this mid-transfer ignore wide
			 * residue.  This ensures that the oddness is
			 * correct for subsequent data transfers.
			 */
			ahc_outb(ahc, SCB_LUN,
				 ahc_inb(ahc, SCB_LUN) ^ SCB_XFERLEN_ODD);
		}
	}
}


/*
 * Reinitialize the data pointers for the active transfer
 * based on its current residual.
 */
static void
ahc_reinitialize_dataptrs(struct ahc_softc *ahc)
{
	struct	 scb *scb;
	struct	 ahc_dma_seg *sg;
	u_int	 scb_index;
	uint32_t sgptr;
	uint32_t resid;
	uint32_t dataptr;

	scb_index = ahc_inb(ahc, SCB_TAG);
	scb = ahc_lookup_scb(ahc, scb_index);
	sgptr = (ahc_inb(ahc, SCB_RESIDUAL_SGPTR + 3) << 24)
	      | (ahc_inb(ahc, SCB_RESIDUAL_SGPTR + 2) << 16)
	      | (ahc_inb(ahc, SCB_RESIDUAL_SGPTR + 1) << 8)
	      |	ahc_inb(ahc, SCB_RESIDUAL_SGPTR);

	sgptr &= SG_PTR_MASK;
	sg = ahc_sg_bus_to_virt(scb, sgptr);

	/* The residual sg_ptr always points to the next sg */
	sg--;

	resid = (ahc_inb(ahc, SCB_RESIDUAL_DATACNT + 2) << 16)
	      | (ahc_inb(ahc, SCB_RESIDUAL_DATACNT + 1) << 8)
	      | ahc_inb(ahc, SCB_RESIDUAL_DATACNT);

	dataptr = ahc_le32toh(sg->addr)
		+ (ahc_le32toh(sg->len) & AHC_SG_LEN_MASK)
		- resid;
	if ((ahc->flags & AHC_39BIT_ADDRESSING) != 0) {
		u_int dscommand1;

		dscommand1 = ahc_inb(ahc, DSCOMMAND1);
		ahc_outb(ahc, DSCOMMAND1, dscommand1 | HADDLDSEL0);
		ahc_outb(ahc, HADDR,
			 (ahc_le32toh(sg->len) >> 24) & SG_HIGH_ADDR_BITS);
		ahc_outb(ahc, DSCOMMAND1, dscommand1);
	}
	ahc_outb(ahc, HADDR + 3, dataptr >> 24);
	ahc_outb(ahc, HADDR + 2, dataptr >> 16);
	ahc_outb(ahc, HADDR + 1, dataptr >> 8);
	ahc_outb(ahc, HADDR, dataptr);
	ahc_outb(ahc, HCNT + 2, resid >> 16);
	ahc_outb(ahc, HCNT + 1, resid >> 8);
	ahc_outb(ahc, HCNT, resid);
	if ((ahc->features & AHC_ULTRA2) == 0) {
		ahc_outb(ahc, STCNT + 2, resid >> 16);
		ahc_outb(ahc, STCNT + 1, resid >> 8);
		ahc_outb(ahc, STCNT, resid);
	}
}

/*
 * Handle the effects of issuing a bus device reset message.
 */
static void
ahc_handle_devreset(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
		    cam_status status, char *message, int verbose_level)
{
#ifdef AHC_TARGET_MODE
	struct ahc_tmode_tstate* tstate;
	u_int lun;
#endif
	int found;

	found = ahc_abort_scbs(ahc, devinfo->target, devinfo->channel,
			       CAM_LUN_WILDCARD, SCB_LIST_NULL, devinfo->role,
			       status);

#ifdef AHC_TARGET_MODE
	/*
	 * Send an immediate notify ccb to all target mord peripheral
	 * drivers affected by this action.
	 */
	tstate = ahc->enabled_targets[devinfo->our_scsiid];
	if (tstate != NULL) {
		for (lun = 0; lun < AHC_NUM_LUNS; lun++) {
			struct ahc_tmode_lstate* lstate;

			lstate = tstate->enabled_luns[lun];
			if (lstate == NULL)
				continue;

			ahc_queue_lstate_event(ahc, lstate, devinfo->our_scsiid,
					       MSG_BUS_DEV_RESET, /*arg*/0);
			ahc_send_lstate_events(ahc, lstate);
		}
	}
#endif

	/*
	 * Go back to async/narrow transfers and renegotiate.
	 */
	ahc_set_width(ahc, devinfo, MSG_EXT_WDTR_BUS_8_BIT,
		      AHC_TRANS_CUR, /*paused*/TRUE);
	ahc_set_syncrate(ahc, devinfo, /*syncrate*/NULL,
			 /*period*/0, /*offset*/0, /*ppr_options*/0,
			 AHC_TRANS_CUR, /*paused*/TRUE);
	
	if (status != CAM_SEL_TIMEOUT)
		ahc_send_async(ahc, devinfo->channel, devinfo->target,
			       CAM_LUN_WILDCARD, AC_SENT_BDR);

	if (message != NULL
	 && (verbose_level <= bootverbose))
		printk("%s: %s on %c:%d. %d SCBs aborted\n", ahc_name(ahc),
		       message, devinfo->channel, devinfo->target, found);
}

#ifdef AHC_TARGET_MODE
static void
ahc_setup_target_msgin(struct ahc_softc *ahc, struct ahc_devinfo *devinfo,
		       struct scb *scb)
{

	/*              
	 * To facilitate adding multiple messages together,
	 * each routine should increment the index and len
	 * variables instead of setting them explicitly.
	 */             
	ahc->msgout_index = 0;
	ahc->msgout_len = 0;

	if (scb != NULL && (scb->flags & SCB_AUTO_NEGOTIATE) != 0)
		ahc_build_transfer_msg(ahc, devinfo);
	else
		panic("ahc_intr: AWAITING target message with no message");

	ahc->msgout_index = 0;
	ahc->msg_type = MSG_TYPE_TARGET_MSGIN;
}
#endif
/**************************** Initialization **********************************/
/*
 * Allocate a controller structure for a new device
 * and perform initial initializion.
 */
struct ahc_softc *
ahc_alloc(void *platform_arg, char *name)
{
	struct  ahc_softc *ahc;
	int	i;

#ifndef	__FreeBSD__
	ahc = kmalloc(sizeof(*ahc), GFP_ATOMIC);
	if (!ahc) {
		printk("aic7xxx: cannot malloc softc!\n");
		kfree(name);
		return NULL;
	}
#else
	ahc = device_get_softc((device_t)platform_arg);
#endif
	memset(ahc, 0, sizeof(*ahc));
	ahc->seep_config = kmalloc(sizeof(*ahc->seep_config), GFP_ATOMIC);
	if (ahc->seep_config == NULL) {
#ifndef	__FreeBSD__
		kfree(ahc);
#endif
		kfree(name);
		return (NULL);
	}
	LIST_INIT(&ahc->pending_scbs);
	/* We don't know our unit number until the OSM sets it */
	ahc->name = name;
	ahc->unit = -1;
	ahc->description = NULL;
	ahc->channel = 'A';
	ahc->channel_b = 'B';
	ahc->chip = AHC_NONE;
	ahc->features = AHC_FENONE;
	ahc->bugs = AHC_BUGNONE;
	ahc->flags = AHC_FNONE;
	/*
	 * Default to all error reporting enabled with the
	 * sequencer operating at its fastest speed.
	 * The bus attach code may modify this.
	 */
	ahc->seqctl = FASTMODE;

	for (i = 0; i < AHC_NUM_TARGETS; i++)
		TAILQ_INIT(&ahc->untagged_queues[i]);
	if (ahc_platform_alloc(ahc, platform_arg) != 0) {
		ahc_free(ahc);
		ahc = NULL;
	}
	return (ahc);
}

int
ahc_softc_init(struct ahc_softc *ahc)
{

	/* The IRQMS bit is only valid on VL and EISA chips */
	if ((ahc->chip & AHC_PCI) == 0)
		ahc->unpause = ahc_inb(ahc, HCNTRL) & IRQMS;
	else
		ahc->unpause = 0;
	ahc->pause = ahc->unpause | PAUSE; 
	/* XXX The shared scb data stuff should be deprecated */
	if (ahc->scb_data == NULL) {
		ahc->scb_data = kzalloc(sizeof(*ahc->scb_data), GFP_ATOMIC);
		if (ahc->scb_data == NULL)
			return (ENOMEM);
	}

	return (0);
}

void
ahc_set_unit(struct ahc_softc *ahc, int unit)
{
	ahc->unit = unit;
}

void
ahc_set_name(struct ahc_softc *ahc, char *name)
{
	if (ahc->name != NULL)
		kfree(ahc->name);
	ahc->name = name;
}

void
ahc_free(struct ahc_softc *ahc)
{
	int i;

	switch (ahc->init_level) {
	default:
	case 5:
		ahc_shutdown(ahc);
		/* FALLTHROUGH */
	case 4:
		ahc_dmamap_unload(ahc, ahc->shared_data_dmat,
				  ahc->shared_data_dmamap);
		/* FALLTHROUGH */
	case 3:
		ahc_dmamem_free(ahc, ahc->shared_data_dmat, ahc->qoutfifo,
				ahc->shared_data_dmamap);
		ahc_dmamap_destroy(ahc, ahc->shared_data_dmat,
				   ahc->shared_data_dmamap);
		/* FALLTHROUGH */
	case 2:
		ahc_dma_tag_destroy(ahc, ahc->shared_data_dmat);
	case 1:
		break;
	case 0:
		break;
	}

	ahc_platform_free(ahc);
	ahc_fini_scbdata(ahc);
	for (i = 0; i < AHC_NUM_TARGETS; i++) {
		struct ahc_tmode_tstate *tstate;

		tstate = ahc->enabled_targets[i];
		if (tstate != NULL) {
#ifdef AHC_TARGET_MODE
			int j;

			for (j = 0; j < AHC_NUM_LUNS; j++) {
				struct ahc_tmode_lstate *lstate;

				lstate = tstate->enabled_luns[j];
				if (lstate != NULL) {
					xpt_free_path(lstate->path);
					kfree(lstate);
				}
			}
#endif
			kfree(tstate);
		}
	}
#ifdef AHC_TARGET_MODE
	if (ahc->black_hole != NULL) {
		xpt_free_path(ahc->black_hole->path);
		kfree(ahc->black_hole);
	}
#endif
	if (ahc->name != NULL)
		kfree(ahc->name);
	if (ahc->seep_config != NULL)
		kfree(ahc->seep_config);
#ifndef __FreeBSD__
	kfree(ahc);
#endif
	return;
}

static void
ahc_shutdown(void *arg)
{
	struct	ahc_softc *ahc;
	int	i;

	ahc = (struct ahc_softc *)arg;

	/* This will reset most registers to 0, but not all */
	ahc_reset(ahc, /*reinit*/FALSE);
	ahc_outb(ahc, SCSISEQ, 0);
	ahc_outb(ahc, SXFRCTL0, 0);
	ahc_outb(ahc, DSPCISTATUS, 0);

	for (i = TARG_SCSIRATE; i < SCSICONF; i++)
		ahc_outb(ahc, i, 0);
}

/*
 * Reset the controller and record some information about it
 * that is only available just after a reset.  If "reinit" is
 * non-zero, this reset occurred after initial configuration
 * and the caller requests that the chip be fully reinitialized
 * to a runable state.  Chip interrupts are *not* enabled after
 * a reinitialization.  The caller must enable interrupts via
 * ahc_intr_enable().
 */
int
ahc_reset(struct ahc_softc *ahc, int reinit)
{
	u_int	sblkctl;
	u_int	sxfrctl1_a, sxfrctl1_b;
	int	error;
	int	wait;
	
	/*
	 * Preserve the value of the SXFRCTL1 register for all channels.
	 * It contains settings that affect termination and we don't want
	 * to disturb the integrity of the bus.
	 */
	ahc_pause(ahc);
	sxfrctl1_b = 0;
	if ((ahc->chip & AHC_CHIPID_MASK) == AHC_AIC7770) {
		u_int sblkctl;

		/*
		 * Save channel B's settings in case this chip
		 * is setup for TWIN channel operation.
		 */
		sblkctl = ahc_inb(ahc, SBLKCTL);
		ahc_outb(ahc, SBLKCTL, sblkctl | SELBUSB);
		sxfrctl1_b = ahc_inb(ahc, SXFRCTL1);
		ahc_outb(ahc, SBLKCTL, sblkctl & ~SELBUSB);
	}
	sxfrctl1_a = ahc_inb(ahc, SXFRCTL1);

	ahc_outb(ahc, HCNTRL, CHIPRST | ahc->pause);

	/*
	 * Ensure that the reset has finished.  We delay 1000us
	 * prior to reading the register to make sure the chip
	 * has sufficiently completed its reset to handle register
	 * accesses.
	 */
	wait = 1000;
	do {
		ahc_delay(1000);
	} while (--wait && !(ahc_inb(ahc, HCNTRL) & CHIPRSTACK));

	if (wait == 0) {
		printk("%s: WARNING - Failed chip reset!  "
		       "Trying to initialize anyway.\n", ahc_name(ahc));
	}
	ahc_outb(ahc, HCNTRL, ahc->pause);

	/* Determine channel configuration */
	sblkctl = ahc_inb(ahc, SBLKCTL) & (SELBUSB|SELWIDE);
	/* No Twin Channel PCI cards */
	if ((ahc->chip & AHC_PCI) != 0)
		sblkctl &= ~SELBUSB;
	switch (sblkctl) {
	case 0:
		/* Single Narrow Channel */
		break;
	case 2:
		/* Wide Channel */
		ahc->features |= AHC_WIDE;
		break;
	case 8:
		/* Twin Channel */
		ahc->features |= AHC_TWIN;
		break;
	default:
		printk(" Unsupported adapter type.  Ignoring\n");
		return(-1);
	}

	/*
	 * Reload sxfrctl1.
	 *
	 * We must always initialize STPWEN to 1 before we
	 * restore the saved values.  STPWEN is initialized
	 * to a tri-state condition which can only be cleared
	 * by turning it on.
	 */
	if ((ahc->features & AHC_TWIN) != 0) {
		u_int sblkctl;

		sblkctl = ahc_inb(ahc, SBLKCTL);
		ahc_outb(ahc, SBLKCTL, sblkctl | SELBUSB);
		ahc_outb(ahc, SXFRCTL1, sxfrctl1_b);
		ahc_outb(ahc, SBLKCTL, sblkctl & ~SELBUSB);
	}
	ahc_outb(ahc, SXFRCTL1, sxfrctl1_a);

	error = 0;
	if (reinit != 0)
		/*
		 * If a recovery action has forced a chip reset,
		 * re-initialize the chip to our liking.
		 */
		error = ahc->bus_chip_init(ahc);
#ifdef AHC_DUMP_SEQ
	else 
		ahc_dumpseq(ahc);
#endif

	return (error);
}

/*
 * Determine the number of SCBs available on the controller
 */
int
ahc_probe_scbs(struct ahc_softc *ahc) {
	int i;

	for (i = 0; i < AHC_SCB_MAX; i++) {

		ahc_outb(ahc, SCBPTR, i);
		ahc_outb(ahc, SCB_BASE, i);
		if (ahc_inb(ahc, SCB_BASE) != i)
			break;
		ahc_outb(ahc, SCBPTR, 0);
		if (ahc_inb(ahc, SCB_BASE) != 0)
			break;
	}
	return (i);
}

static void
ahc_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error) 
{
	dma_addr_t *baddr;

	baddr = (dma_addr_t *)arg;
	*baddr = segs->ds_addr;
}

static void
ahc_build_free_scb_list(struct ahc_softc *ahc)
{
	int scbsize;
	int i;

	scbsize = 32;
	if ((ahc->flags & AHC_LSCBS_ENABLED) != 0)
		scbsize = 64;

	for (i = 0; i < ahc->scb_data->maxhscbs; i++) {
		int j;

		ahc_outb(ahc, SCBPTR, i);

		/*
		 * Touch all SCB bytes to avoid parity errors
		 * should one of our debugging routines read
		 * an otherwise uninitiatlized byte.
		 */
		for (j = 0; j < scbsize; j++)
			ahc_outb(ahc, SCB_BASE+j, 0xFF);

		/* Clear the control byte. */
		ahc_outb(ahc, SCB_CONTROL, 0);

		/* Set the next pointer */
		if ((ahc->flags & AHC_PAGESCBS) != 0)
			ahc_outb(ahc, SCB_NEXT, i+1);
		else 
			ahc_outb(ahc, SCB_NEXT, SCB_LIST_NULL);

		/* Make the tag number, SCSIID, and lun invalid */
		ahc_outb(ahc, SCB_TAG, SCB_LIST_NULL);
		ahc_outb(ahc, SCB_SCSIID, 0xFF);
		ahc_outb(ahc, SCB_LUN, 0xFF);
	}

	if ((ahc->flags & AHC_PAGESCBS) != 0) {
		/* SCB 0 heads the free list. */
		ahc_outb(ahc, FREE_SCBH, 0);
	} else {
		/* No free list. */
		ahc_outb(ahc, FREE_SCBH, SCB_LIST_NULL);
	}

	/* Make sure that the last SCB terminates the free list */
	ahc_outb(ahc, SCBPTR, i-1);
	ahc_outb(ahc, SCB_NEXT, SCB_LIST_NULL);
}

static int
ahc_init_scbdata(struct ahc_softc *ahc)
{
	struct scb_data *scb_data;

	scb_data = ahc->scb_data;
	SLIST_INIT(&scb_data->free_scbs);
	SLIST_INIT(&scb_data->sg_maps);

	/* Allocate SCB resources */
	scb_data->scbarray = kcalloc(AHC_SCB_MAX_ALLOC, sizeof(struct scb),
				     GFP_ATOMIC);
	if (scb_data->scbarray == NULL)
		return (ENOMEM);

	/* Determine the number of hardware SCBs and initialize them */

	scb_data->maxhscbs = ahc_probe_scbs(ahc);
	if (ahc->scb_data->maxhscbs == 0) {
		printk("%s: No SCB space found\n", ahc_name(ahc));
		return (ENXIO);
	}

	/*
	 * Create our DMA tags.  These tags define the kinds of device
	 * accessible memory allocations and memory mappings we will
	 * need to perform during normal operation.
	 *
	 * Unless we need to further restrict the allocation, we rely
	 * on the restrictions of the parent dmat, hence the common
	 * use of MAXADDR and MAXSIZE.
	 */

	/* DMA tag for our hardware scb structures */
	if (ahc_dma_tag_create(ahc, ahc->parent_dmat, /*alignment*/1,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       AHC_SCB_MAX_ALLOC * sizeof(struct hardware_scb),
			       /*nsegments*/1,
			       /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			       /*flags*/0, &scb_data->hscb_dmat) != 0) {
		goto error_exit;
	}

	scb_data->init_level++;

	/* Allocation for our hscbs */
	if (ahc_dmamem_alloc(ahc, scb_data->hscb_dmat,
			     (void **)&scb_data->hscbs,
			     BUS_DMA_NOWAIT, &scb_data->hscb_dmamap) != 0) {
		goto error_exit;
	}

	scb_data->init_level++;

	/* And permanently map them */
	ahc_dmamap_load(ahc, scb_data->hscb_dmat, scb_data->hscb_dmamap,
			scb_data->hscbs,
			AHC_SCB_MAX_ALLOC * sizeof(struct hardware_scb),
			ahc_dmamap_cb, &scb_data->hscb_busaddr, /*flags*/0);

	scb_data->init_level++;

	/* DMA tag for our sense buffers */
	if (ahc_dma_tag_create(ahc, ahc->parent_dmat, /*alignment*/1,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       AHC_SCB_MAX_ALLOC * sizeof(struct scsi_sense_data),
			       /*nsegments*/1,
			       /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			       /*flags*/0, &scb_data->sense_dmat) != 0) {
		goto error_exit;
	}

	scb_data->init_level++;

	/* Allocate them */
	if (ahc_dmamem_alloc(ahc, scb_data->sense_dmat,
			     (void **)&scb_data->sense,
			     BUS_DMA_NOWAIT, &scb_data->sense_dmamap) != 0) {
		goto error_exit;
	}

	scb_data->init_level++;

	/* And permanently map them */
	ahc_dmamap_load(ahc, scb_data->sense_dmat, scb_data->sense_dmamap,
			scb_data->sense,
			AHC_SCB_MAX_ALLOC * sizeof(struct scsi_sense_data),
			ahc_dmamap_cb, &scb_data->sense_busaddr, /*flags*/0);

	scb_data->init_level++;

	/* DMA tag for our S/G structures.  We allocate in page sized chunks */
	if (ahc_dma_tag_create(ahc, ahc->parent_dmat, /*alignment*/8,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       PAGE_SIZE, /*nsegments*/1,
			       /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			       /*flags*/0, &scb_data->sg_dmat) != 0) {
		goto error_exit;
	}

	scb_data->init_level++;

	/* Perform initial CCB allocation */
	memset(scb_data->hscbs, 0,
	       AHC_SCB_MAX_ALLOC * sizeof(struct hardware_scb));
	ahc_alloc_scbs(ahc);

	if (scb_data->numscbs == 0) {
		printk("%s: ahc_init_scbdata - "
		       "Unable to allocate initial scbs\n",
		       ahc_name(ahc));
		goto error_exit;
	}

	/*
	 * Reserve the next queued SCB.
	 */
	ahc->next_queued_scb = ahc_get_scb(ahc);

	/*
	 * Note that we were successful
	 */
	return (0); 

error_exit:

	return (ENOMEM);
}

static void
ahc_fini_scbdata(struct ahc_softc *ahc)
{
	struct scb_data *scb_data;

	scb_data = ahc->scb_data;
	if (scb_data == NULL)
		return;

	switch (scb_data->init_level) {
	default:
	case 7:
	{
		struct sg_map_node *sg_map;

		while ((sg_map = SLIST_FIRST(&scb_data->sg_maps))!= NULL) {
			SLIST_REMOVE_HEAD(&scb_data->sg_maps, links);
			ahc_dmamap_unload(ahc, scb_data->sg_dmat,
					  sg_map->sg_dmamap);
			ahc_dmamem_free(ahc, scb_data->sg_dmat,
					sg_map->sg_vaddr,
					sg_map->sg_dmamap);
			kfree(sg_map);
		}
		ahc_dma_tag_destroy(ahc, scb_data->sg_dmat);
	}
	case 6:
		ahc_dmamap_unload(ahc, scb_data->sense_dmat,
				  scb_data->sense_dmamap);
	case 5:
		ahc_dmamem_free(ahc, scb_data->sense_dmat, scb_data->sense,
				scb_data->sense_dmamap);
		ahc_dmamap_destroy(ahc, scb_data->sense_dmat,
				   scb_data->sense_dmamap);
	case 4:
		ahc_dma_tag_destroy(ahc, scb_data->sense_dmat);
	case 3:
		ahc_dmamap_unload(ahc, scb_data->hscb_dmat,
				  scb_data->hscb_dmamap);
	case 2:
		ahc_dmamem_free(ahc, scb_data->hscb_dmat, scb_data->hscbs,
				scb_data->hscb_dmamap);
		ahc_dmamap_destroy(ahc, scb_data->hscb_dmat,
				   scb_data->hscb_dmamap);
	case 1:
		ahc_dma_tag_destroy(ahc, scb_data->hscb_dmat);
		break;
	case 0:
		break;
	}
	if (scb_data->scbarray != NULL)
		kfree(scb_data->scbarray);
}

static void
ahc_alloc_scbs(struct ahc_softc *ahc)
{
	struct scb_data *scb_data;
	struct scb *next_scb;
	struct sg_map_node *sg_map;
	dma_addr_t physaddr;
	struct ahc_dma_seg *segs;
	int newcount;
	int i;

	scb_data = ahc->scb_data;
	if (scb_data->numscbs >= AHC_SCB_MAX_ALLOC)
		/* Can't allocate any more */
		return;

	next_scb = &scb_data->scbarray[scb_data->numscbs];

	sg_map = kmalloc(sizeof(*sg_map), GFP_ATOMIC);

	if (sg_map == NULL)
		return;

	/* Allocate S/G space for the next batch of SCBS */
	if (ahc_dmamem_alloc(ahc, scb_data->sg_dmat,
			     (void **)&sg_map->sg_vaddr,
			     BUS_DMA_NOWAIT, &sg_map->sg_dmamap) != 0) {
		kfree(sg_map);
		return;
	}

	SLIST_INSERT_HEAD(&scb_data->sg_maps, sg_map, links);

	ahc_dmamap_load(ahc, scb_data->sg_dmat, sg_map->sg_dmamap,
			sg_map->sg_vaddr, PAGE_SIZE, ahc_dmamap_cb,
			&sg_map->sg_physaddr, /*flags*/0);

	segs = sg_map->sg_vaddr;
	physaddr = sg_map->sg_physaddr;

	newcount = (PAGE_SIZE / (AHC_NSEG * sizeof(struct ahc_dma_seg)));
	newcount = min(newcount, (AHC_SCB_MAX_ALLOC - scb_data->numscbs));
	for (i = 0; i < newcount; i++) {
		struct scb_platform_data *pdata;

		pdata = kmalloc(sizeof(*pdata), GFP_ATOMIC);
		if (pdata == NULL)
			break;
		next_scb->platform_data = pdata;
		next_scb->sg_map = sg_map;
		next_scb->sg_list = segs;
		/*
		 * The sequencer always starts with the second entry.
		 * The first entry is embedded in the scb.
		 */
		next_scb->sg_list_phys = physaddr + sizeof(struct ahc_dma_seg);
		next_scb->ahc_softc = ahc;
		next_scb->flags = SCB_FREE;
		next_scb->hscb = &scb_data->hscbs[scb_data->numscbs];
		next_scb->hscb->tag = ahc->scb_data->numscbs;
		SLIST_INSERT_HEAD(&ahc->scb_data->free_scbs,
				  next_scb, links.sle);
		segs += AHC_NSEG;
		physaddr += (AHC_NSEG * sizeof(struct ahc_dma_seg));
		next_scb++;
		ahc->scb_data->numscbs++;
	}
}

void
ahc_controller_info(struct ahc_softc *ahc, char *buf)
{
	int len;

	len = sprintf(buf, "%s: ", ahc_chip_names[ahc->chip & AHC_CHIPID_MASK]);
	buf += len;
	if ((ahc->features & AHC_TWIN) != 0)
 		len = sprintf(buf, "Twin Channel, A SCSI Id=%d, "
			      "B SCSI Id=%d, primary %c, ",
			      ahc->our_id, ahc->our_id_b,
			      (ahc->flags & AHC_PRIMARY_CHANNEL) + 'A');
	else {
		const char *speed;
		const char *type;

		speed = "";
		if ((ahc->features & AHC_ULTRA) != 0) {
			speed = "Ultra ";
		} else if ((ahc->features & AHC_DT) != 0) {
			speed = "Ultra160 ";
		} else if ((ahc->features & AHC_ULTRA2) != 0) {
			speed = "Ultra2 ";
		}
		if ((ahc->features & AHC_WIDE) != 0) {
			type = "Wide";
		} else {
			type = "Single";
		}
		len = sprintf(buf, "%s%s Channel %c, SCSI Id=%d, ",
			      speed, type, ahc->channel, ahc->our_id);
	}
	buf += len;

	if ((ahc->flags & AHC_PAGESCBS) != 0)
		sprintf(buf, "%d/%d SCBs",
			ahc->scb_data->maxhscbs, AHC_MAX_QUEUE);
	else
		sprintf(buf, "%d SCBs", ahc->scb_data->maxhscbs);
}

int
ahc_chip_init(struct ahc_softc *ahc)
{
	int	 term;
	int	 error;
	u_int	 i;
	u_int	 scsi_conf;
	u_int	 scsiseq_template;
	uint32_t physaddr;

	ahc_outb(ahc, SEQ_FLAGS, 0);
	ahc_outb(ahc, SEQ_FLAGS2, 0);

	/* Set the SCSI Id, SXFRCTL0, SXFRCTL1, and SIMODE1, for both channels*/
	if (ahc->features & AHC_TWIN) {

		/*
		 * Setup Channel B first.
		 */
		ahc_outb(ahc, SBLKCTL, ahc_inb(ahc, SBLKCTL) | SELBUSB);
		term = (ahc->flags & AHC_TERM_ENB_B) != 0 ? STPWEN : 0;
		ahc_outb(ahc, SCSIID, ahc->our_id_b);
		scsi_conf = ahc_inb(ahc, SCSICONF + 1);
		ahc_outb(ahc, SXFRCTL1, (scsi_conf & (ENSPCHK|STIMESEL))
					|term|ahc->seltime_b|ENSTIMER|ACTNEGEN);
		if ((ahc->features & AHC_ULTRA2) != 0)
			ahc_outb(ahc, SIMODE0, ahc_inb(ahc, SIMODE0)|ENIOERR);
		ahc_outb(ahc, SIMODE1, ENSELTIMO|ENSCSIRST|ENSCSIPERR);
		ahc_outb(ahc, SXFRCTL0, DFON|SPIOEN);

		/* Select Channel A */
		ahc_outb(ahc, SBLKCTL, ahc_inb(ahc, SBLKCTL) & ~SELBUSB);
	}
	term = (ahc->flags & AHC_TERM_ENB_A) != 0 ? STPWEN : 0;
	if ((ahc->features & AHC_ULTRA2) != 0)
		ahc_outb(ahc, SCSIID_ULTRA2, ahc->our_id);
	else
		ahc_outb(ahc, SCSIID, ahc->our_id);
	scsi_conf = ahc_inb(ahc, SCSICONF);
	ahc_outb(ahc, SXFRCTL1, (scsi_conf & (ENSPCHK|STIMESEL))
				|term|ahc->seltime
				|ENSTIMER|ACTNEGEN);
	if ((ahc->features & AHC_ULTRA2) != 0)
		ahc_outb(ahc, SIMODE0, ahc_inb(ahc, SIMODE0)|ENIOERR);
	ahc_outb(ahc, SIMODE1, ENSELTIMO|ENSCSIRST|ENSCSIPERR);
	ahc_outb(ahc, SXFRCTL0, DFON|SPIOEN);

	/* There are no untagged SCBs active yet. */
	for (i = 0; i < 16; i++) {
		ahc_unbusy_tcl(ahc, BUILD_TCL(i << 4, 0));
		if ((ahc->flags & AHC_SCB_BTT) != 0) {
			int lun;

			/*
			 * The SCB based BTT allows an entry per
			 * target and lun pair.
			 */
			for (lun = 1; lun < AHC_NUM_LUNS; lun++)
				ahc_unbusy_tcl(ahc, BUILD_TCL(i << 4, lun));
		}
	}

	/* All of our queues are empty */
	for (i = 0; i < 256; i++)
		ahc->qoutfifo[i] = SCB_LIST_NULL;
	ahc_sync_qoutfifo(ahc, BUS_DMASYNC_PREREAD);

	for (i = 0; i < 256; i++)
		ahc->qinfifo[i] = SCB_LIST_NULL;

	if ((ahc->features & AHC_MULTI_TID) != 0) {
		ahc_outb(ahc, TARGID, 0);
		ahc_outb(ahc, TARGID + 1, 0);
	}

	/*
	 * Tell the sequencer where it can find our arrays in memory.
	 */
	physaddr = ahc->scb_data->hscb_busaddr;
	ahc_outb(ahc, HSCB_ADDR, physaddr & 0xFF);
	ahc_outb(ahc, HSCB_ADDR + 1, (physaddr >> 8) & 0xFF);
	ahc_outb(ahc, HSCB_ADDR + 2, (physaddr >> 16) & 0xFF);
	ahc_outb(ahc, HSCB_ADDR + 3, (physaddr >> 24) & 0xFF);

	physaddr = ahc->shared_data_busaddr;
	ahc_outb(ahc, SHARED_DATA_ADDR, physaddr & 0xFF);
	ahc_outb(ahc, SHARED_DATA_ADDR + 1, (physaddr >> 8) & 0xFF);
	ahc_outb(ahc, SHARED_DATA_ADDR + 2, (physaddr >> 16) & 0xFF);
	ahc_outb(ahc, SHARED_DATA_ADDR + 3, (physaddr >> 24) & 0xFF);

	/*
	 * Initialize the group code to command length table.
	 * This overrides the values in TARG_SCSIRATE, so only
	 * setup the table after we have processed that information.
	 */
	ahc_outb(ahc, CMDSIZE_TABLE, 5);
	ahc_outb(ahc, CMDSIZE_TABLE + 1, 9);
	ahc_outb(ahc, CMDSIZE_TABLE + 2, 9);
	ahc_outb(ahc, CMDSIZE_TABLE + 3, 0);
	ahc_outb(ahc, CMDSIZE_TABLE + 4, 15);
	ahc_outb(ahc, CMDSIZE_TABLE + 5, 11);
	ahc_outb(ahc, CMDSIZE_TABLE + 6, 0);
	ahc_outb(ahc, CMDSIZE_TABLE + 7, 0);
		
	if ((ahc->features & AHC_HS_MAILBOX) != 0)
		ahc_outb(ahc, HS_MAILBOX, 0);

	/* Tell the sequencer of our initial queue positions */
	if ((ahc->features & AHC_TARGETMODE) != 0) {
		ahc->tqinfifonext = 1;
		ahc_outb(ahc, KERNEL_TQINPOS, ahc->tqinfifonext - 1);
		ahc_outb(ahc, TQINPOS, ahc->tqinfifonext);
	}
	ahc->qinfifonext = 0;
	ahc->qoutfifonext = 0;
	if ((ahc->features & AHC_QUEUE_REGS) != 0) {
		ahc_outb(ahc, QOFF_CTLSTA, SCB_QSIZE_256);
		ahc_outb(ahc, HNSCB_QOFF, ahc->qinfifonext);
		ahc_outb(ahc, SNSCB_QOFF, ahc->qinfifonext);
		ahc_outb(ahc, SDSCB_QOFF, 0);
	} else {
		ahc_outb(ahc, KERNEL_QINPOS, ahc->qinfifonext);
		ahc_outb(ahc, QINPOS, ahc->qinfifonext);
		ahc_outb(ahc, QOUTPOS, ahc->qoutfifonext);
	}

	/* We don't have any waiting selections */
	ahc_outb(ahc, WAITING_SCBH, SCB_LIST_NULL);

	/* Our disconnection list is empty too */
	ahc_outb(ahc, DISCONNECTED_SCBH, SCB_LIST_NULL);

	/* Message out buffer starts empty */
	ahc_outb(ahc, MSG_OUT, MSG_NOOP);

	/*
	 * Setup the allowed SCSI Sequences based on operational mode.
	 * If we are a target, we'll enable select in operations once
	 * we've had a lun enabled.
	 */
	scsiseq_template = ENSELO|ENAUTOATNO|ENAUTOATNP;
	if ((ahc->flags & AHC_INITIATORROLE) != 0)
		scsiseq_template |= ENRSELI;
	ahc_outb(ahc, SCSISEQ_TEMPLATE, scsiseq_template);

	/* Initialize our list of free SCBs. */
	ahc_build_free_scb_list(ahc);

	/*
	 * Tell the sequencer which SCB will be the next one it receives.
	 */
	ahc_outb(ahc, NEXT_QUEUED_SCB, ahc->next_queued_scb->hscb->tag);

	/*
	 * Load the Sequencer program and Enable the adapter
	 * in "fast" mode.
	 */
	if (bootverbose)
		printk("%s: Downloading Sequencer Program...",
		       ahc_name(ahc));

	error = ahc_loadseq(ahc);
	if (error != 0)
		return (error);

	if ((ahc->features & AHC_ULTRA2) != 0) {
		int wait;

		/*
		 * Wait for up to 500ms for our transceivers
		 * to settle.  If the adapter does not have
		 * a cable attached, the transceivers may
		 * never settle, so don't complain if we
		 * fail here.
		 */
		for (wait = 5000;
		     (ahc_inb(ahc, SBLKCTL) & (ENAB40|ENAB20)) == 0 && wait;
		     wait--)
			ahc_delay(100);
	}
	ahc_restart(ahc);
	return (0);
}

/*
 * Start the board, ready for normal operation
 */
int
ahc_init(struct ahc_softc *ahc)
{
	int	 max_targ;
	u_int	 i;
	u_int	 scsi_conf;
	u_int	 ultraenb;
	u_int	 discenable;
	u_int	 tagenable;
	size_t	 driver_data_size;

#ifdef AHC_DEBUG
	if ((ahc_debug & AHC_DEBUG_SEQUENCER) != 0)
		ahc->flags |= AHC_SEQUENCER_DEBUG;
#endif

#ifdef AHC_PRINT_SRAM
	printk("Scratch Ram:");
	for (i = 0x20; i < 0x5f; i++) {
		if (((i % 8) == 0) && (i != 0)) {
			printk ("\n              ");
		}
		printk (" 0x%x", ahc_inb(ahc, i));
	}
	if ((ahc->features & AHC_MORE_SRAM) != 0) {
		for (i = 0x70; i < 0x7f; i++) {
			if (((i % 8) == 0) && (i != 0)) {
				printk ("\n              ");
			}
			printk (" 0x%x", ahc_inb(ahc, i));
		}
	}
	printk ("\n");
	/*
	 * Reading uninitialized scratch ram may
	 * generate parity errors.
	 */
	ahc_outb(ahc, CLRINT, CLRPARERR);
	ahc_outb(ahc, CLRINT, CLRBRKADRINT);
#endif
	max_targ = 15;

	/*
	 * Assume we have a board at this stage and it has been reset.
	 */
	if ((ahc->flags & AHC_USEDEFAULTS) != 0)
		ahc->our_id = ahc->our_id_b = 7;
	
	/*
	 * Default to allowing initiator operations.
	 */
	ahc->flags |= AHC_INITIATORROLE;

	/*
	 * Only allow target mode features if this unit has them enabled.
	 */
	if ((AHC_TMODE_ENABLE & (0x1 << ahc->unit)) == 0)
		ahc->features &= ~AHC_TARGETMODE;

	ahc->init_level++;

	/*
	 * DMA tag for our command fifos and other data in system memory
	 * the card's sequencer must be able to access.  For initiator
	 * roles, we need to allocate space for the qinfifo and qoutfifo.
	 * The qinfifo and qoutfifo are composed of 256 1 byte elements. 
	 * When providing for the target mode role, we must additionally
	 * provide space for the incoming target command fifo and an extra
	 * byte to deal with a dma bug in some chip versions.
	 */
	driver_data_size = 2 * 256 * sizeof(uint8_t);
	if ((ahc->features & AHC_TARGETMODE) != 0)
		driver_data_size += AHC_TMODE_CMDS * sizeof(struct target_cmd)
				 + /*DMA WideOdd Bug Buffer*/1;
	if (ahc_dma_tag_create(ahc, ahc->parent_dmat, /*alignment*/1,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       driver_data_size,
			       /*nsegments*/1,
			       /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			       /*flags*/0, &ahc->shared_data_dmat) != 0) {
		return (ENOMEM);
	}

	ahc->init_level++;

	/* Allocation of driver data */
	if (ahc_dmamem_alloc(ahc, ahc->shared_data_dmat,
			     (void **)&ahc->qoutfifo,
			     BUS_DMA_NOWAIT, &ahc->shared_data_dmamap) != 0) {
		return (ENOMEM);
	}

	ahc->init_level++;

	/* And permanently map it in */
	ahc_dmamap_load(ahc, ahc->shared_data_dmat, ahc->shared_data_dmamap,
			ahc->qoutfifo, driver_data_size, ahc_dmamap_cb,
			&ahc->shared_data_busaddr, /*flags*/0);

	if ((ahc->features & AHC_TARGETMODE) != 0) {
		ahc->targetcmds = (struct target_cmd *)ahc->qoutfifo;
		ahc->qoutfifo = (uint8_t *)&ahc->targetcmds[AHC_TMODE_CMDS];
		ahc->dma_bug_buf = ahc->shared_data_busaddr
				 + driver_data_size - 1;
		/* All target command blocks start out invalid. */
		for (i = 0; i < AHC_TMODE_CMDS; i++)
			ahc->targetcmds[i].cmd_valid = 0;
		ahc_sync_tqinfifo(ahc, BUS_DMASYNC_PREREAD);
		ahc->qoutfifo = (uint8_t *)&ahc->targetcmds[256];
	}
	ahc->qinfifo = &ahc->qoutfifo[256];

	ahc->init_level++;

	/* Allocate SCB data now that buffer_dmat is initialized */
	if (ahc->scb_data->maxhscbs == 0)
		if (ahc_init_scbdata(ahc) != 0)
			return (ENOMEM);

	/*
	 * Allocate a tstate to house information for our
	 * initiator presence on the bus as well as the user
	 * data for any target mode initiator.
	 */
	if (ahc_alloc_tstate(ahc, ahc->our_id, 'A') == NULL) {
		printk("%s: unable to allocate ahc_tmode_tstate.  "
		       "Failing attach\n", ahc_name(ahc));
		return (ENOMEM);
	}

	if ((ahc->features & AHC_TWIN) != 0) {
		if (ahc_alloc_tstate(ahc, ahc->our_id_b, 'B') == NULL) {
			printk("%s: unable to allocate ahc_tmode_tstate.  "
			       "Failing attach\n", ahc_name(ahc));
			return (ENOMEM);
		}
	}

	if (ahc->scb_data->maxhscbs < AHC_SCB_MAX_ALLOC) {
		ahc->flags |= AHC_PAGESCBS;
	} else {
		ahc->flags &= ~AHC_PAGESCBS;
	}

#ifdef AHC_DEBUG
	if (ahc_debug & AHC_SHOW_MISC) {
		printk("%s: hardware scb %u bytes; kernel scb %u bytes; "
		       "ahc_dma %u bytes\n",
			ahc_name(ahc),
			(u_int)sizeof(struct hardware_scb),
			(u_int)sizeof(struct scb),
			(u_int)sizeof(struct ahc_dma_seg));
	}
#endif /* AHC_DEBUG */

	/*
	 * Look at the information that board initialization or
	 * the board bios has left us.
	 */
	if (ahc->features & AHC_TWIN) {
		scsi_conf = ahc_inb(ahc, SCSICONF + 1);
		if ((scsi_conf & RESET_SCSI) != 0
		 && (ahc->flags & AHC_INITIATORROLE) != 0)
			ahc->flags |= AHC_RESET_BUS_B;
	}

	scsi_conf = ahc_inb(ahc, SCSICONF);
	if ((scsi_conf & RESET_SCSI) != 0
	 && (ahc->flags & AHC_INITIATORROLE) != 0)
		ahc->flags |= AHC_RESET_BUS_A;

	ultraenb = 0;	
	tagenable = ALL_TARGETS_MASK;

	/* Grab the disconnection disable table and invert it for our needs */
	if ((ahc->flags & AHC_USEDEFAULTS) != 0) {
		printk("%s: Host Adapter Bios disabled.  Using default SCSI "
			"device parameters\n", ahc_name(ahc));
		ahc->flags |= AHC_EXTENDED_TRANS_A|AHC_EXTENDED_TRANS_B|
			      AHC_TERM_ENB_A|AHC_TERM_ENB_B;
		discenable = ALL_TARGETS_MASK;
		if ((ahc->features & AHC_ULTRA) != 0)
			ultraenb = ALL_TARGETS_MASK;
	} else {
		discenable = ~((ahc_inb(ahc, DISC_DSB + 1) << 8)
			   | ahc_inb(ahc, DISC_DSB));
		if ((ahc->features & (AHC_ULTRA|AHC_ULTRA2)) != 0)
			ultraenb = (ahc_inb(ahc, ULTRA_ENB + 1) << 8)
				      | ahc_inb(ahc, ULTRA_ENB);
	}

	if ((ahc->features & (AHC_WIDE|AHC_TWIN)) == 0)
		max_targ = 7;

	for (i = 0; i <= max_targ; i++) {
		struct ahc_initiator_tinfo *tinfo;
		struct ahc_tmode_tstate *tstate;
		u_int our_id;
		u_int target_id;
		char channel;

		channel = 'A';
		our_id = ahc->our_id;
		target_id = i;
		if (i > 7 && (ahc->features & AHC_TWIN) != 0) {
			channel = 'B';
			our_id = ahc->our_id_b;
			target_id = i % 8;
		}
		tinfo = ahc_fetch_transinfo(ahc, channel, our_id,
					    target_id, &tstate);
		/* Default to async narrow across the board */
		memset(tinfo, 0, sizeof(*tinfo));
		if (ahc->flags & AHC_USEDEFAULTS) {
			if ((ahc->features & AHC_WIDE) != 0)
				tinfo->user.width = MSG_EXT_WDTR_BUS_16_BIT;

			/*
			 * These will be truncated when we determine the
			 * connection type we have with the target.
			 */
			tinfo->user.period = ahc_syncrates->period;
			tinfo->user.offset = MAX_OFFSET;
		} else {
			u_int scsirate;
			uint16_t mask;

			/* Take the settings leftover in scratch RAM. */
			scsirate = ahc_inb(ahc, TARG_SCSIRATE + i);
			mask = (0x01 << i);
			if ((ahc->features & AHC_ULTRA2) != 0) {
				u_int offset;
				u_int maxsync;

				if ((scsirate & SOFS) == 0x0F) {
					/*
					 * Haven't negotiated yet,
					 * so the format is different.
					 */
					scsirate = (scsirate & SXFR) >> 4
						 | (ultraenb & mask)
						  ? 0x08 : 0x0
						 | (scsirate & WIDEXFER);
					offset = MAX_OFFSET_ULTRA2;
				} else
					offset = ahc_inb(ahc, TARG_OFFSET + i);
				if ((scsirate & ~WIDEXFER) == 0 && offset != 0)
					/* Set to the lowest sync rate, 5MHz */
					scsirate |= 0x1c;
				maxsync = AHC_SYNCRATE_ULTRA2;
				if ((ahc->features & AHC_DT) != 0)
					maxsync = AHC_SYNCRATE_DT;
				tinfo->user.period =
				    ahc_find_period(ahc, scsirate, maxsync);
				if (offset == 0)
					tinfo->user.period = 0;
				else
					tinfo->user.offset = MAX_OFFSET;
				if ((scsirate & SXFR_ULTRA2) <= 8/*10MHz*/
				 && (ahc->features & AHC_DT) != 0)
					tinfo->user.ppr_options =
					    MSG_EXT_PPR_DT_REQ;
			} else if ((scsirate & SOFS) != 0) {
				if ((scsirate & SXFR) == 0x40
				 && (ultraenb & mask) != 0) {
					/* Treat 10MHz as a non-ultra speed */
					scsirate &= ~SXFR;
				 	ultraenb &= ~mask;
				}
				tinfo->user.period = 
				    ahc_find_period(ahc, scsirate,
						    (ultraenb & mask)
						   ? AHC_SYNCRATE_ULTRA
						   : AHC_SYNCRATE_FAST);
				if (tinfo->user.period != 0)
					tinfo->user.offset = MAX_OFFSET;
			}
			if (tinfo->user.period == 0)
				tinfo->user.offset = 0;
			if ((scsirate & WIDEXFER) != 0
			 && (ahc->features & AHC_WIDE) != 0)
				tinfo->user.width = MSG_EXT_WDTR_BUS_16_BIT;
			tinfo->user.protocol_version = 4;
			if ((ahc->features & AHC_DT) != 0)
				tinfo->user.transport_version = 3;
			else
				tinfo->user.transport_version = 2;
			tinfo->goal.protocol_version = 2;
			tinfo->goal.transport_version = 2;
			tinfo->curr.protocol_version = 2;
			tinfo->curr.transport_version = 2;
		}
		tstate->ultraenb = 0;
	}
	ahc->user_discenable = discenable;
	ahc->user_tagenable = tagenable;

	return (ahc->bus_chip_init(ahc));
}

void
ahc_intr_enable(struct ahc_softc *ahc, int enable)
{
	u_int hcntrl;

	hcntrl = ahc_inb(ahc, HCNTRL);
	hcntrl &= ~INTEN;
	ahc->pause &= ~INTEN;
	ahc->unpause &= ~INTEN;
	if (enable) {
		hcntrl |= INTEN;
		ahc->pause |= INTEN;
		ahc->unpause |= INTEN;
	}
	ahc_outb(ahc, HCNTRL, hcntrl);
}

/*
 * Ensure that the card is paused in a location
 * outside of all critical sections and that all
 * pending work is completed prior to returning.
 * This routine should only be called from outside
 * an interrupt context.
 */
void
ahc_pause_and_flushwork(struct ahc_softc *ahc)
{
	int intstat;
	int maxloops;
	int paused;

	maxloops = 1000;
	ahc->flags |= AHC_ALL_INTERRUPTS;
	paused = FALSE;
	do {
		if (paused) {
			ahc_unpause(ahc);
			/*
			 * Give the sequencer some time to service
			 * any active selections.
			 */
			ahc_delay(500);
		}
		ahc_intr(ahc);
		ahc_pause(ahc);
		paused = TRUE;
		ahc_outb(ahc, SCSISEQ, ahc_inb(ahc, SCSISEQ) & ~ENSELO);
		intstat = ahc_inb(ahc, INTSTAT);
		if ((intstat & INT_PEND) == 0) {
			ahc_clear_critical_section(ahc);
			intstat = ahc_inb(ahc, INTSTAT);
		}
	} while (--maxloops
	      && (intstat != 0xFF || (ahc->features & AHC_REMOVABLE) == 0)
	      && ((intstat & INT_PEND) != 0
	       || (ahc_inb(ahc, SSTAT0) & (SELDO|SELINGO)) != 0));
	if (maxloops == 0) {
		printk("Infinite interrupt loop, INTSTAT = %x",
		       ahc_inb(ahc, INTSTAT));
	}
	ahc_platform_flushwork(ahc);
	ahc->flags &= ~AHC_ALL_INTERRUPTS;
}

#ifdef CONFIG_PM
int
ahc_suspend(struct ahc_softc *ahc)
{

	ahc_pause_and_flushwork(ahc);

	if (LIST_FIRST(&ahc->pending_scbs) != NULL) {
		ahc_unpause(ahc);
		return (EBUSY);
	}

#ifdef AHC_TARGET_MODE
	/*
	 * XXX What about ATIOs that have not yet been serviced?
	 * Perhaps we should just refuse to be suspended if we
	 * are acting in a target role.
	 */
	if (ahc->pending_device != NULL) {
		ahc_unpause(ahc);
		return (EBUSY);
	}
#endif
	ahc_shutdown(ahc);
	return (0);
}

int
ahc_resume(struct ahc_softc *ahc)
{

	ahc_reset(ahc, /*reinit*/TRUE);
	ahc_intr_enable(ahc, TRUE); 
	ahc_restart(ahc);
	return (0);
}
#endif
/************************** Busy Target Table *********************************/
/*
 * Return the untagged transaction id for a given target/channel lun.
 * Optionally, clear the entry.
 */
static u_int
ahc_index_busy_tcl(struct ahc_softc *ahc, u_int tcl)
{
	u_int scbid;
	u_int target_offset;

	if ((ahc->flags & AHC_SCB_BTT) != 0) {
		u_int saved_scbptr;
		
		saved_scbptr = ahc_inb(ahc, SCBPTR);
		ahc_outb(ahc, SCBPTR, TCL_LUN(tcl));
		scbid = ahc_inb(ahc, SCB_64_BTT + TCL_TARGET_OFFSET(tcl));
		ahc_outb(ahc, SCBPTR, saved_scbptr);
	} else {
		target_offset = TCL_TARGET_OFFSET(tcl);
		scbid = ahc_inb(ahc, BUSY_TARGETS + target_offset);
	}

	return (scbid);
}

static void
ahc_unbusy_tcl(struct ahc_softc *ahc, u_int tcl)
{
	u_int target_offset;

	if ((ahc->flags & AHC_SCB_BTT) != 0) {
		u_int saved_scbptr;
		
		saved_scbptr = ahc_inb(ahc, SCBPTR);
		ahc_outb(ahc, SCBPTR, TCL_LUN(tcl));
		ahc_outb(ahc, SCB_64_BTT+TCL_TARGET_OFFSET(tcl), SCB_LIST_NULL);
		ahc_outb(ahc, SCBPTR, saved_scbptr);
	} else {
		target_offset = TCL_TARGET_OFFSET(tcl);
		ahc_outb(ahc, BUSY_TARGETS + target_offset, SCB_LIST_NULL);
	}
}

static void
ahc_busy_tcl(struct ahc_softc *ahc, u_int tcl, u_int scbid)
{
	u_int target_offset;

	if ((ahc->flags & AHC_SCB_BTT) != 0) {
		u_int saved_scbptr;
		
		saved_scbptr = ahc_inb(ahc, SCBPTR);
		ahc_outb(ahc, SCBPTR, TCL_LUN(tcl));
		ahc_outb(ahc, SCB_64_BTT + TCL_TARGET_OFFSET(tcl), scbid);
		ahc_outb(ahc, SCBPTR, saved_scbptr);
	} else {
		target_offset = TCL_TARGET_OFFSET(tcl);
		ahc_outb(ahc, BUSY_TARGETS + target_offset, scbid);
	}
}

/************************** SCB and SCB queue management **********************/
int
ahc_match_scb(struct ahc_softc *ahc, struct scb *scb, int target,
	      char channel, int lun, u_int tag, role_t role)
{
	int targ = SCB_GET_TARGET(ahc, scb);
	char chan = SCB_GET_CHANNEL(ahc, scb);
	int slun = SCB_GET_LUN(scb);
	int match;

	match = ((chan == channel) || (channel == ALL_CHANNELS));
	if (match != 0)
		match = ((targ == target) || (target == CAM_TARGET_WILDCARD));
	if (match != 0)
		match = ((lun == slun) || (lun == CAM_LUN_WILDCARD));
	if (match != 0) {
#ifdef AHC_TARGET_MODE
		int group;

		group = XPT_FC_GROUP(scb->io_ctx->ccb_h.func_code);
		if (role == ROLE_INITIATOR) {
			match = (group != XPT_FC_GROUP_TMODE)
			      && ((tag == scb->hscb->tag)
			       || (tag == SCB_LIST_NULL));
		} else if (role == ROLE_TARGET) {
			match = (group == XPT_FC_GROUP_TMODE)
			      && ((tag == scb->io_ctx->csio.tag_id)
			       || (tag == SCB_LIST_NULL));
		}
#else /* !AHC_TARGET_MODE */
		match = ((tag == scb->hscb->tag) || (tag == SCB_LIST_NULL));
#endif /* AHC_TARGET_MODE */
	}

	return match;
}

static void
ahc_freeze_devq(struct ahc_softc *ahc, struct scb *scb)
{
	int	target;
	char	channel;
	int	lun;

	target = SCB_GET_TARGET(ahc, scb);
	lun = SCB_GET_LUN(scb);
	channel = SCB_GET_CHANNEL(ahc, scb);
	
	ahc_search_qinfifo(ahc, target, channel, lun,
			   /*tag*/SCB_LIST_NULL, ROLE_UNKNOWN,
			   CAM_REQUEUE_REQ, SEARCH_COMPLETE);

	ahc_platform_freeze_devq(ahc, scb);
}

void
ahc_qinfifo_requeue_tail(struct ahc_softc *ahc, struct scb *scb)
{
	struct scb *prev_scb;

	prev_scb = NULL;
	if (ahc_qinfifo_count(ahc) != 0) {
		u_int prev_tag;
		uint8_t prev_pos;

		prev_pos = ahc->qinfifonext - 1;
		prev_tag = ahc->qinfifo[prev_pos];
		prev_scb = ahc_lookup_scb(ahc, prev_tag);
	}
	ahc_qinfifo_requeue(ahc, prev_scb, scb);
	if ((ahc->features & AHC_QUEUE_REGS) != 0) {
		ahc_outb(ahc, HNSCB_QOFF, ahc->qinfifonext);
	} else {
		ahc_outb(ahc, KERNEL_QINPOS, ahc->qinfifonext);
	}
}

static void
ahc_qinfifo_requeue(struct ahc_softc *ahc, struct scb *prev_scb,
		    struct scb *scb)
{
	if (prev_scb == NULL) {
		ahc_outb(ahc, NEXT_QUEUED_SCB, scb->hscb->tag);
	} else {
		prev_scb->hscb->next = scb->hscb->tag;
		ahc_sync_scb(ahc, prev_scb, 
			     BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	}
	ahc->qinfifo[ahc->qinfifonext++] = scb->hscb->tag;
	scb->hscb->next = ahc->next_queued_scb->hscb->tag;
	ahc_sync_scb(ahc, scb, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
}

static int
ahc_qinfifo_count(struct ahc_softc *ahc)
{
	uint8_t qinpos;
	uint8_t diff;

	if ((ahc->features & AHC_QUEUE_REGS) != 0) {
		qinpos = ahc_inb(ahc, SNSCB_QOFF);
		ahc_outb(ahc, SNSCB_QOFF, qinpos);
	} else
		qinpos = ahc_inb(ahc, QINPOS);
	diff = ahc->qinfifonext - qinpos;
	return (diff);
}

int
ahc_search_qinfifo(struct ahc_softc *ahc, int target, char channel,
		   int lun, u_int tag, role_t role, uint32_t status,
		   ahc_search_action action)
{
	struct	scb *scb;
	struct	scb *prev_scb;
	uint8_t qinstart;
	uint8_t qinpos;
	uint8_t qintail;
	uint8_t next;
	uint8_t prev;
	uint8_t curscbptr;
	int	found;
	int	have_qregs;

	qintail = ahc->qinfifonext;
	have_qregs = (ahc->features & AHC_QUEUE_REGS) != 0;
	if (have_qregs) {
		qinstart = ahc_inb(ahc, SNSCB_QOFF);
		ahc_outb(ahc, SNSCB_QOFF, qinstart);
	} else
		qinstart = ahc_inb(ahc, QINPOS);
	qinpos = qinstart;
	found = 0;
	prev_scb = NULL;

	if (action == SEARCH_COMPLETE) {
		/*
		 * Don't attempt to run any queued untagged transactions
		 * until we are done with the abort process.
		 */
		ahc_freeze_untagged_queues(ahc);
	}

	/*
	 * Start with an empty queue.  Entries that are not chosen
	 * for removal will be re-added to the queue as we go.
	 */
	ahc->qinfifonext = qinpos;
	ahc_outb(ahc, NEXT_QUEUED_SCB, ahc->next_queued_scb->hscb->tag);

	while (qinpos != qintail) {
		scb = ahc_lookup_scb(ahc, ahc->qinfifo[qinpos]);
		if (scb == NULL) {
			printk("qinpos = %d, SCB index = %d\n",
				qinpos, ahc->qinfifo[qinpos]);
			panic("Loop 1\n");
		}

		if (ahc_match_scb(ahc, scb, target, channel, lun, tag, role)) {
			/*
			 * We found an scb that needs to be acted on.
			 */
			found++;
			switch (action) {
			case SEARCH_COMPLETE:
			{
				cam_status ostat;
				cam_status cstat;

				ostat = ahc_get_transaction_status(scb);
				if (ostat == CAM_REQ_INPROG)
					ahc_set_transaction_status(scb, status);
				cstat = ahc_get_transaction_status(scb);
				if (cstat != CAM_REQ_CMP)
					ahc_freeze_scb(scb);
				if ((scb->flags & SCB_ACTIVE) == 0)
					printk("Inactive SCB in qinfifo\n");
				ahc_done(ahc, scb);

				/* FALLTHROUGH */
			}
			case SEARCH_REMOVE:
				break;
			case SEARCH_COUNT:
				ahc_qinfifo_requeue(ahc, prev_scb, scb);
				prev_scb = scb;
				break;
			}
		} else {
			ahc_qinfifo_requeue(ahc, prev_scb, scb);
			prev_scb = scb;
		}
		qinpos++;
	}

	if ((ahc->features & AHC_QUEUE_REGS) != 0) {
		ahc_outb(ahc, HNSCB_QOFF, ahc->qinfifonext);
	} else {
		ahc_outb(ahc, KERNEL_QINPOS, ahc->qinfifonext);
	}

	if (action != SEARCH_COUNT
	 && (found != 0)
	 && (qinstart != ahc->qinfifonext)) {
		/*
		 * The sequencer may be in the process of dmaing
		 * down the SCB at the beginning of the queue.
		 * This could be problematic if either the first,
		 * or the second SCB is removed from the queue
		 * (the first SCB includes a pointer to the "next"
		 * SCB to dma). If we have removed any entries, swap
		 * the first element in the queue with the next HSCB
		 * so the sequencer will notice that NEXT_QUEUED_SCB
		 * has changed during its dma attempt and will retry
		 * the DMA.
		 */
		scb = ahc_lookup_scb(ahc, ahc->qinfifo[qinstart]);

		if (scb == NULL) {
			printk("found = %d, qinstart = %d, qinfifionext = %d\n",
				found, qinstart, ahc->qinfifonext);
			panic("First/Second Qinfifo fixup\n");
		}
		/*
		 * ahc_swap_with_next_hscb forces our next pointer to
		 * point to the reserved SCB for future commands.  Save
		 * and restore our original next pointer to maintain
		 * queue integrity.
		 */
		next = scb->hscb->next;
		ahc->scb_data->scbindex[scb->hscb->tag] = NULL;
		ahc_swap_with_next_hscb(ahc, scb);
		scb->hscb->next = next;
		ahc->qinfifo[qinstart] = scb->hscb->tag;

		/* Tell the card about the new head of the qinfifo. */
		ahc_outb(ahc, NEXT_QUEUED_SCB, scb->hscb->tag);

		/* Fixup the tail "next" pointer. */
		qintail = ahc->qinfifonext - 1;
		scb = ahc_lookup_scb(ahc, ahc->qinfifo[qintail]);
		scb->hscb->next = ahc->next_queued_scb->hscb->tag;
	}

	/*
	 * Search waiting for selection list.
	 */
	curscbptr = ahc_inb(ahc, SCBPTR);
	next = ahc_inb(ahc, WAITING_SCBH);  /* Start at head of list. */
	prev = SCB_LIST_NULL;

	while (next != SCB_LIST_NULL) {
		uint8_t scb_index;

		ahc_outb(ahc, SCBPTR, next);
		scb_index = ahc_inb(ahc, SCB_TAG);
		if (scb_index >= ahc->scb_data->numscbs) {
			printk("Waiting List inconsistency. "
			       "SCB index == %d, yet numscbs == %d.",
			       scb_index, ahc->scb_data->numscbs);
			ahc_dump_card_state(ahc);
			panic("for safety");
		}
		scb = ahc_lookup_scb(ahc, scb_index);
		if (scb == NULL) {
			printk("scb_index = %d, next = %d\n",
				scb_index, next);
			panic("Waiting List traversal\n");
		}
		if (ahc_match_scb(ahc, scb, target, channel,
				  lun, SCB_LIST_NULL, role)) {
			/*
			 * We found an scb that needs to be acted on.
			 */
			found++;
			switch (action) {
			case SEARCH_COMPLETE:
			{
				cam_status ostat;
				cam_status cstat;

				ostat = ahc_get_transaction_status(scb);
				if (ostat == CAM_REQ_INPROG)
					ahc_set_transaction_status(scb,
								   status);
				cstat = ahc_get_transaction_status(scb);
				if (cstat != CAM_REQ_CMP)
					ahc_freeze_scb(scb);
				if ((scb->flags & SCB_ACTIVE) == 0)
					printk("Inactive SCB in Waiting List\n");
				ahc_done(ahc, scb);
				/* FALLTHROUGH */
			}
			case SEARCH_REMOVE:
				next = ahc_rem_wscb(ahc, next, prev);
				break;
			case SEARCH_COUNT:
				prev = next;
				next = ahc_inb(ahc, SCB_NEXT);
				break;
			}
		} else {
			
			prev = next;
			next = ahc_inb(ahc, SCB_NEXT);
		}
	}
	ahc_outb(ahc, SCBPTR, curscbptr);

	found += ahc_search_untagged_queues(ahc, /*ahc_io_ctx_t*/NULL, target,
					    channel, lun, status, action);

	if (action == SEARCH_COMPLETE)
		ahc_release_untagged_queues(ahc);
	return (found);
}

int
ahc_search_untagged_queues(struct ahc_softc *ahc, ahc_io_ctx_t ctx,
			   int target, char channel, int lun, uint32_t status,
			   ahc_search_action action)
{
	struct	scb *scb;
	int	maxtarget;
	int	found;
	int	i;

	if (action == SEARCH_COMPLETE) {
		/*
		 * Don't attempt to run any queued untagged transactions
		 * until we are done with the abort process.
		 */
		ahc_freeze_untagged_queues(ahc);
	}

	found = 0;
	i = 0;
	if ((ahc->flags & AHC_SCB_BTT) == 0) {

		maxtarget = 16;
		if (target != CAM_TARGET_WILDCARD) {

			i = target;
			if (channel == 'B')
				i += 8;
			maxtarget = i + 1;
		}
	} else {
		maxtarget = 0;
	}

	for (; i < maxtarget; i++) {
		struct scb_tailq *untagged_q;
		struct scb *next_scb;

		untagged_q = &(ahc->untagged_queues[i]);
		next_scb = TAILQ_FIRST(untagged_q);
		while (next_scb != NULL) {

			scb = next_scb;
			next_scb = TAILQ_NEXT(scb, links.tqe);

			/*
			 * The head of the list may be the currently
			 * active untagged command for a device.
			 * We're only searching for commands that
			 * have not been started.  A transaction
			 * marked active but still in the qinfifo
			 * is removed by the qinfifo scanning code
			 * above.
			 */
			if ((scb->flags & SCB_ACTIVE) != 0)
				continue;

			if (ahc_match_scb(ahc, scb, target, channel, lun,
					  SCB_LIST_NULL, ROLE_INITIATOR) == 0
			 || (ctx != NULL && ctx != scb->io_ctx))
				continue;

			/*
			 * We found an scb that needs to be acted on.
			 */
			found++;
			switch (action) {
			case SEARCH_COMPLETE:
			{
				cam_status ostat;
				cam_status cstat;

				ostat = ahc_get_transaction_status(scb);
				if (ostat == CAM_REQ_INPROG)
					ahc_set_transaction_status(scb, status);
				cstat = ahc_get_transaction_status(scb);
				if (cstat != CAM_REQ_CMP)
					ahc_freeze_scb(scb);
				if ((scb->flags & SCB_ACTIVE) == 0)
					printk("Inactive SCB in untaggedQ\n");
				ahc_done(ahc, scb);
				break;
			}
			case SEARCH_REMOVE:
				scb->flags &= ~SCB_UNTAGGEDQ;
				TAILQ_REMOVE(untagged_q, scb, links.tqe);
				break;
			case SEARCH_COUNT:
				break;
			}
		}
	}

	if (action == SEARCH_COMPLETE)
		ahc_release_untagged_queues(ahc);
	return (found);
}

int
ahc_search_disc_list(struct ahc_softc *ahc, int target, char channel,
		     int lun, u_int tag, int stop_on_first, int remove,
		     int save_state)
{
	struct	scb *scbp;
	u_int	next;
	u_int	prev;
	u_int	count;
	u_int	active_scb;

	count = 0;
	next = ahc_inb(ahc, DISCONNECTED_SCBH);
	prev = SCB_LIST_NULL;

	if (save_state) {
		/* restore this when we're done */
		active_scb = ahc_inb(ahc, SCBPTR);
	} else
		/* Silence compiler */
		active_scb = SCB_LIST_NULL;

	while (next != SCB_LIST_NULL) {
		u_int scb_index;

		ahc_outb(ahc, SCBPTR, next);
		scb_index = ahc_inb(ahc, SCB_TAG);
		if (scb_index >= ahc->scb_data->numscbs) {
			printk("Disconnected List inconsistency. "
			       "SCB index == %d, yet numscbs == %d.",
			       scb_index, ahc->scb_data->numscbs);
			ahc_dump_card_state(ahc);
			panic("for safety");
		}

		if (next == prev) {
			panic("Disconnected List Loop. "
			      "cur SCBPTR == %x, prev SCBPTR == %x.",
			      next, prev);
		}
		scbp = ahc_lookup_scb(ahc, scb_index);
		if (ahc_match_scb(ahc, scbp, target, channel, lun,
				  tag, ROLE_INITIATOR)) {
			count++;
			if (remove) {
				next =
				    ahc_rem_scb_from_disc_list(ahc, prev, next);
			} else {
				prev = next;
				next = ahc_inb(ahc, SCB_NEXT);
			}
			if (stop_on_first)
				break;
		} else {
			prev = next;
			next = ahc_inb(ahc, SCB_NEXT);
		}
	}
	if (save_state)
		ahc_outb(ahc, SCBPTR, active_scb);
	return (count);
}

/*
 * Remove an SCB from the on chip list of disconnected transactions.
 * This is empty/unused if we are not performing SCB paging.
 */
static u_int
ahc_rem_scb_from_disc_list(struct ahc_softc *ahc, u_int prev, u_int scbptr)
{
	u_int next;

	ahc_outb(ahc, SCBPTR, scbptr);
	next = ahc_inb(ahc, SCB_NEXT);

	ahc_outb(ahc, SCB_CONTROL, 0);

	ahc_add_curscb_to_free_list(ahc);

	if (prev != SCB_LIST_NULL) {
		ahc_outb(ahc, SCBPTR, prev);
		ahc_outb(ahc, SCB_NEXT, next);
	} else
		ahc_outb(ahc, DISCONNECTED_SCBH, next);

	return (next);
}

/*
 * Add the SCB as selected by SCBPTR onto the on chip list of
 * free hardware SCBs.  This list is empty/unused if we are not
 * performing SCB paging.
 */
static void
ahc_add_curscb_to_free_list(struct ahc_softc *ahc)
{
	/*
	 * Invalidate the tag so that our abort
	 * routines don't think it's active.
	 */
	ahc_outb(ahc, SCB_TAG, SCB_LIST_NULL);

	if ((ahc->flags & AHC_PAGESCBS) != 0) {
		ahc_outb(ahc, SCB_NEXT, ahc_inb(ahc, FREE_SCBH));
		ahc_outb(ahc, FREE_SCBH, ahc_inb(ahc, SCBPTR));
	}
}

/*
 * Manipulate the waiting for selection list and return the
 * scb that follows the one that we remove.
 */
static u_int
ahc_rem_wscb(struct ahc_softc *ahc, u_int scbpos, u_int prev)
{
	u_int curscb, next;

	/*
	 * Select the SCB we want to abort and
	 * pull the next pointer out of it.
	 */
	curscb = ahc_inb(ahc, SCBPTR);
	ahc_outb(ahc, SCBPTR, scbpos);
	next = ahc_inb(ahc, SCB_NEXT);

	/* Clear the necessary fields */
	ahc_outb(ahc, SCB_CONTROL, 0);

	ahc_add_curscb_to_free_list(ahc);

	/* update the waiting list */
	if (prev == SCB_LIST_NULL) {
		/* First in the list */
		ahc_outb(ahc, WAITING_SCBH, next); 

		/*
		 * Ensure we aren't attempting to perform
		 * selection for this entry.
		 */
		ahc_outb(ahc, SCSISEQ, (ahc_inb(ahc, SCSISEQ) & ~ENSELO));
	} else {
		/*
		 * Select the scb that pointed to us 
		 * and update its next pointer.
		 */
		ahc_outb(ahc, SCBPTR, prev);
		ahc_outb(ahc, SCB_NEXT, next);
	}

	/*
	 * Point us back at the original scb position.
	 */
	ahc_outb(ahc, SCBPTR, curscb);
	return next;
}

/******************************** Error Handling ******************************/
/*
 * Abort all SCBs that match the given description (target/channel/lun/tag),
 * setting their status to the passed in status if the status has not already
 * been modified from CAM_REQ_INPROG.  This routine assumes that the sequencer
 * is paused before it is called.
 */
static int
ahc_abort_scbs(struct ahc_softc *ahc, int target, char channel,
	       int lun, u_int tag, role_t role, uint32_t status)
{
	struct	scb *scbp;
	struct	scb *scbp_next;
	u_int	active_scb;
	int	i, j;
	int	maxtarget;
	int	minlun;
	int	maxlun;

	int	found;

	/*
	 * Don't attempt to run any queued untagged transactions
	 * until we are done with the abort process.
	 */
	ahc_freeze_untagged_queues(ahc);

	/* restore this when we're done */
	active_scb = ahc_inb(ahc, SCBPTR);

	found = ahc_search_qinfifo(ahc, target, channel, lun, SCB_LIST_NULL,
				   role, CAM_REQUEUE_REQ, SEARCH_COMPLETE);

	/*
	 * Clean out the busy target table for any untagged commands.
	 */
	i = 0;
	maxtarget = 16;
	if (target != CAM_TARGET_WILDCARD) {
		i = target;
		if (channel == 'B')
			i += 8;
		maxtarget = i + 1;
	}

	if (lun == CAM_LUN_WILDCARD) {

		/*
		 * Unless we are using an SCB based
		 * busy targets table, there is only
		 * one table entry for all luns of
		 * a target.
		 */
		minlun = 0;
		maxlun = 1;
		if ((ahc->flags & AHC_SCB_BTT) != 0)
			maxlun = AHC_NUM_LUNS;
	} else {
		minlun = lun;
		maxlun = lun + 1;
	}

	if (role != ROLE_TARGET) {
		for (;i < maxtarget; i++) {
			for (j = minlun;j < maxlun; j++) {
				u_int scbid;
				u_int tcl;

				tcl = BUILD_TCL(i << 4, j);
				scbid = ahc_index_busy_tcl(ahc, tcl);
				scbp = ahc_lookup_scb(ahc, scbid);
				if (scbp == NULL
				 || ahc_match_scb(ahc, scbp, target, channel,
						  lun, tag, role) == 0)
					continue;
				ahc_unbusy_tcl(ahc, BUILD_TCL(i << 4, j));
			}
		}

		/*
		 * Go through the disconnected list and remove any entries we
		 * have queued for completion, 0'ing their control byte too.
		 * We save the active SCB and restore it ourselves, so there
		 * is no reason for this search to restore it too.
		 */
		ahc_search_disc_list(ahc, target, channel, lun, tag,
				     /*stop_on_first*/FALSE, /*remove*/TRUE,
				     /*save_state*/FALSE);
	}

	/*
	 * Go through the hardware SCB array looking for commands that
	 * were active but not on any list.  In some cases, these remnants
	 * might not still have mappings in the scbindex array (e.g. unexpected
	 * bus free with the same scb queued for an abort).  Don't hold this
	 * against them.
	 */
	for (i = 0; i < ahc->scb_data->maxhscbs; i++) {
		u_int scbid;

		ahc_outb(ahc, SCBPTR, i);
		scbid = ahc_inb(ahc, SCB_TAG);
		scbp = ahc_lookup_scb(ahc, scbid);
		if ((scbp == NULL && scbid != SCB_LIST_NULL)
		 || (scbp != NULL
		  && ahc_match_scb(ahc, scbp, target, channel, lun, tag, role)))
			ahc_add_curscb_to_free_list(ahc);
	}

	/*
	 * Go through the pending CCB list and look for
	 * commands for this target that are still active.
	 * These are other tagged commands that were
	 * disconnected when the reset occurred.
	 */
	scbp_next = LIST_FIRST(&ahc->pending_scbs);
	while (scbp_next != NULL) {
		scbp = scbp_next;
		scbp_next = LIST_NEXT(scbp, pending_links);
		if (ahc_match_scb(ahc, scbp, target, channel, lun, tag, role)) {
			cam_status ostat;

			ostat = ahc_get_transaction_status(scbp);
			if (ostat == CAM_REQ_INPROG)
				ahc_set_transaction_status(scbp, status);
			if (ahc_get_transaction_status(scbp) != CAM_REQ_CMP)
				ahc_freeze_scb(scbp);
			if ((scbp->flags & SCB_ACTIVE) == 0)
				printk("Inactive SCB on pending list\n");
			ahc_done(ahc, scbp);
			found++;
		}
	}
	ahc_outb(ahc, SCBPTR, active_scb);
	ahc_platform_abort_scbs(ahc, target, channel, lun, tag, role, status);
	ahc_release_untagged_queues(ahc);
	return found;
}

static void
ahc_reset_current_bus(struct ahc_softc *ahc)
{
	uint8_t scsiseq;

	ahc_outb(ahc, SIMODE1, ahc_inb(ahc, SIMODE1) & ~ENSCSIRST);
	scsiseq = ahc_inb(ahc, SCSISEQ);
	ahc_outb(ahc, SCSISEQ, scsiseq | SCSIRSTO);
	ahc_flush_device_writes(ahc);
	ahc_delay(AHC_BUSRESET_DELAY);
	/* Turn off the bus reset */
	ahc_outb(ahc, SCSISEQ, scsiseq & ~SCSIRSTO);

	ahc_clear_intstat(ahc);

	/* Re-enable reset interrupts */
	ahc_outb(ahc, SIMODE1, ahc_inb(ahc, SIMODE1) | ENSCSIRST);
}

int
ahc_reset_channel(struct ahc_softc *ahc, char channel, int initiate_reset)
{
	struct	ahc_devinfo devinfo;
	u_int	initiator, target, max_scsiid;
	u_int	sblkctl;
	u_int	scsiseq;
	u_int	simode1;
	int	found;
	int	restart_needed;
	char	cur_channel;

	ahc->pending_device = NULL;

	ahc_compile_devinfo(&devinfo,
			    CAM_TARGET_WILDCARD,
			    CAM_TARGET_WILDCARD,
			    CAM_LUN_WILDCARD,
			    channel, ROLE_UNKNOWN);
	ahc_pause(ahc);

	/* Make sure the sequencer is in a safe location. */
	ahc_clear_critical_section(ahc);

	/*
	 * Run our command complete fifos to ensure that we perform
	 * completion processing on any commands that 'completed'
	 * before the reset occurred.
	 */
	ahc_run_qoutfifo(ahc);
#ifdef AHC_TARGET_MODE
	/*
	 * XXX - In Twin mode, the tqinfifo may have commands
	 *	 for an unaffected channel in it.  However, if
	 *	 we have run out of ATIO resources to drain that
	 *	 queue, we may not get them all out here.  Further,
	 *	 the blocked transactions for the reset channel
	 *	 should just be killed off, irrespecitve of whether
	 *	 we are blocked on ATIO resources.  Write a routine
	 *	 to compact the tqinfifo appropriately.
	 */
	if ((ahc->flags & AHC_TARGETROLE) != 0) {
		ahc_run_tqinfifo(ahc, /*paused*/TRUE);
	}
#endif

	/*
	 * Reset the bus if we are initiating this reset
	 */
	sblkctl = ahc_inb(ahc, SBLKCTL);
	cur_channel = 'A';
	if ((ahc->features & AHC_TWIN) != 0
	 && ((sblkctl & SELBUSB) != 0))
	    cur_channel = 'B';
	scsiseq = ahc_inb(ahc, SCSISEQ_TEMPLATE);
	if (cur_channel != channel) {
		/* Case 1: Command for another bus is active
		 * Stealthily reset the other bus without
		 * upsetting the current bus.
		 */
		ahc_outb(ahc, SBLKCTL, sblkctl ^ SELBUSB);
		simode1 = ahc_inb(ahc, SIMODE1) & ~(ENBUSFREE|ENSCSIRST);
#ifdef AHC_TARGET_MODE
		/*
		 * Bus resets clear ENSELI, so we cannot
		 * defer re-enabling bus reset interrupts
		 * if we are in target mode.
		 */
		if ((ahc->flags & AHC_TARGETROLE) != 0)
			simode1 |= ENSCSIRST;
#endif
		ahc_outb(ahc, SIMODE1, simode1);
		if (initiate_reset)
			ahc_reset_current_bus(ahc);
		ahc_clear_intstat(ahc);
		ahc_outb(ahc, SCSISEQ, scsiseq & (ENSELI|ENRSELI|ENAUTOATNP));
		ahc_outb(ahc, SBLKCTL, sblkctl);
		restart_needed = FALSE;
	} else {
		/* Case 2: A command from this bus is active or we're idle */
		simode1 = ahc_inb(ahc, SIMODE1) & ~(ENBUSFREE|ENSCSIRST);
#ifdef AHC_TARGET_MODE
		/*
		 * Bus resets clear ENSELI, so we cannot
		 * defer re-enabling bus reset interrupts
		 * if we are in target mode.
		 */
		if ((ahc->flags & AHC_TARGETROLE) != 0)
			simode1 |= ENSCSIRST;
#endif
		ahc_outb(ahc, SIMODE1, simode1);
		if (initiate_reset)
			ahc_reset_current_bus(ahc);
		ahc_clear_intstat(ahc);
		ahc_outb(ahc, SCSISEQ, scsiseq & (ENSELI|ENRSELI|ENAUTOATNP));
		restart_needed = TRUE;
	}

	/*
	 * Clean up all the state information for the
	 * pending transactions on this bus.
	 */
	found = ahc_abort_scbs(ahc, CAM_TARGET_WILDCARD, channel,
			       CAM_LUN_WILDCARD, SCB_LIST_NULL,
			       ROLE_UNKNOWN, CAM_SCSI_BUS_RESET);

	max_scsiid = (ahc->features & AHC_WIDE) ? 15 : 7;

#ifdef AHC_TARGET_MODE
	/*
	 * Send an immediate notify ccb to all target more peripheral
	 * drivers affected by this action.
	 */
	for (target = 0; target <= max_scsiid; target++) {
		struct ahc_tmode_tstate* tstate;
		u_int lun;

		tstate = ahc->enabled_targets[target];
		if (tstate == NULL)
			continue;
		for (lun = 0; lun < AHC_NUM_LUNS; lun++) {
			struct ahc_tmode_lstate* lstate;

			lstate = tstate->enabled_luns[lun];
			if (lstate == NULL)
				continue;

			ahc_queue_lstate_event(ahc, lstate, CAM_TARGET_WILDCARD,
					       EVENT_TYPE_BUS_RESET, /*arg*/0);
			ahc_send_lstate_events(ahc, lstate);
		}
	}
#endif
	/* Notify the XPT that a bus reset occurred */
	ahc_send_async(ahc, devinfo.channel, CAM_TARGET_WILDCARD,
		       CAM_LUN_WILDCARD, AC_BUS_RESET);

	/*
	 * Revert to async/narrow transfers until we renegotiate.
	 */
	for (target = 0; target <= max_scsiid; target++) {

		if (ahc->enabled_targets[target] == NULL)
			continue;
		for (initiator = 0; initiator <= max_scsiid; initiator++) {
			struct ahc_devinfo devinfo;

			ahc_compile_devinfo(&devinfo, target, initiator,
					    CAM_LUN_WILDCARD,
					    channel, ROLE_UNKNOWN);
			ahc_set_width(ahc, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
				      AHC_TRANS_CUR, /*paused*/TRUE);
			ahc_set_syncrate(ahc, &devinfo, /*syncrate*/NULL,
					 /*period*/0, /*offset*/0,
					 /*ppr_options*/0, AHC_TRANS_CUR,
					 /*paused*/TRUE);
		}
	}

	if (restart_needed)
		ahc_restart(ahc);
	else
		ahc_unpause(ahc);
	return found;
}


/***************************** Residual Processing ****************************/
/*
 * Calculate the residual for a just completed SCB.
 */
static void
ahc_calc_residual(struct ahc_softc *ahc, struct scb *scb)
{
	struct hardware_scb *hscb;
	struct status_pkt *spkt;
	uint32_t sgptr;
	uint32_t resid_sgptr;
	uint32_t resid;

	/*
	 * 5 cases.
	 * 1) No residual.
	 *    SG_RESID_VALID clear in sgptr.
	 * 2) Transferless command
	 * 3) Never performed any transfers.
	 *    sgptr has SG_FULL_RESID set.
	 * 4) No residual but target did not
	 *    save data pointers after the
	 *    last transfer, so sgptr was
	 *    never updated.
	 * 5) We have a partial residual.
	 *    Use residual_sgptr to determine
	 *    where we are.
	 */

	hscb = scb->hscb;
	sgptr = ahc_le32toh(hscb->sgptr);
	if ((sgptr & SG_RESID_VALID) == 0)
		/* Case 1 */
		return;
	sgptr &= ~SG_RESID_VALID;

	if ((sgptr & SG_LIST_NULL) != 0)
		/* Case 2 */
		return;

	spkt = &hscb->shared_data.status;
	resid_sgptr = ahc_le32toh(spkt->residual_sg_ptr);
	if ((sgptr & SG_FULL_RESID) != 0) {
		/* Case 3 */
		resid = ahc_get_transfer_length(scb);
	} else if ((resid_sgptr & SG_LIST_NULL) != 0) {
		/* Case 4 */
		return;
	} else if ((resid_sgptr & ~SG_PTR_MASK) != 0) {
		panic("Bogus resid sgptr value 0x%x\n", resid_sgptr);
	} else {
		struct ahc_dma_seg *sg;

		/*
		 * Remainder of the SG where the transfer
		 * stopped.  
		 */
		resid = ahc_le32toh(spkt->residual_datacnt) & AHC_SG_LEN_MASK;
		sg = ahc_sg_bus_to_virt(scb, resid_sgptr & SG_PTR_MASK);

		/* The residual sg_ptr always points to the next sg */
		sg--;

		/*
		 * Add up the contents of all residual
		 * SG segments that are after the SG where
		 * the transfer stopped.
		 */
		while ((ahc_le32toh(sg->len) & AHC_DMA_LAST_SEG) == 0) {
			sg++;
			resid += ahc_le32toh(sg->len) & AHC_SG_LEN_MASK;
		}
	}
	if ((scb->flags & SCB_SENSE) == 0)
		ahc_set_residual(scb, resid);
	else
		ahc_set_sense_residual(scb, resid);

#ifdef AHC_DEBUG
	if ((ahc_debug & AHC_SHOW_MISC) != 0) {
		ahc_print_path(ahc, scb);
		printk("Handled %sResidual of %d bytes\n",
		       (scb->flags & SCB_SENSE) ? "Sense " : "", resid);
	}
#endif
}

/******************************* Target Mode **********************************/
#ifdef AHC_TARGET_MODE
/*
 * Add a target mode event to this lun's queue
 */
static void
ahc_queue_lstate_event(struct ahc_softc *ahc, struct ahc_tmode_lstate *lstate,
		       u_int initiator_id, u_int event_type, u_int event_arg)
{
	struct ahc_tmode_event *event;
	int pending;

	xpt_freeze_devq(lstate->path, /*count*/1);
	if (lstate->event_w_idx >= lstate->event_r_idx)
		pending = lstate->event_w_idx - lstate->event_r_idx;
	else
		pending = AHC_TMODE_EVENT_BUFFER_SIZE + 1
			- (lstate->event_r_idx - lstate->event_w_idx);

	if (event_type == EVENT_TYPE_BUS_RESET
	 || event_type == MSG_BUS_DEV_RESET) {
		/*
		 * Any earlier events are irrelevant, so reset our buffer.
		 * This has the effect of allowing us to deal with reset
		 * floods (an external device holding down the reset line)
		 * without losing the event that is really interesting.
		 */
		lstate->event_r_idx = 0;
		lstate->event_w_idx = 0;
		xpt_release_devq(lstate->path, pending, /*runqueue*/FALSE);
	}

	if (pending == AHC_TMODE_EVENT_BUFFER_SIZE) {
		xpt_print_path(lstate->path);
		printk("immediate event %x:%x lost\n",
		       lstate->event_buffer[lstate->event_r_idx].event_type,
		       lstate->event_buffer[lstate->event_r_idx].event_arg);
		lstate->event_r_idx++;
		if (lstate->event_r_idx == AHC_TMODE_EVENT_BUFFER_SIZE)
			lstate->event_r_idx = 0;
		xpt_release_devq(lstate->path, /*count*/1, /*runqueue*/FALSE);
	}

	event = &lstate->event_buffer[lstate->event_w_idx];
	event->initiator_id = initiator_id;
	event->event_type = event_type;
	event->event_arg = event_arg;
	lstate->event_w_idx++;
	if (lstate->event_w_idx == AHC_TMODE_EVENT_BUFFER_SIZE)
		lstate->event_w_idx = 0;
}

/*
 * Send any target mode events queued up waiting
 * for immediate notify resources.
 */
void
ahc_send_lstate_events(struct ahc_softc *ahc, struct ahc_tmode_lstate *lstate)
{
	struct ccb_hdr *ccbh;
	struct ccb_immed_notify *inot;

	while (lstate->event_r_idx != lstate->event_w_idx
	    && (ccbh = SLIST_FIRST(&lstate->immed_notifies)) != NULL) {
		struct ahc_tmode_event *event;

		event = &lstate->event_buffer[lstate->event_r_idx];
		SLIST_REMOVE_HEAD(&lstate->immed_notifies, sim_links.sle);
		inot = (struct ccb_immed_notify *)ccbh;
		switch (event->event_type) {
		case EVENT_TYPE_BUS_RESET:
			ccbh->status = CAM_SCSI_BUS_RESET|CAM_DEV_QFRZN;
			break;
		default:
			ccbh->status = CAM_MESSAGE_RECV|CAM_DEV_QFRZN;
			inot->message_args[0] = event->event_type;
			inot->message_args[1] = event->event_arg;
			break;
		}
		inot->initiator_id = event->initiator_id;
		inot->sense_len = 0;
		xpt_done((union ccb *)inot);
		lstate->event_r_idx++;
		if (lstate->event_r_idx == AHC_TMODE_EVENT_BUFFER_SIZE)
			lstate->event_r_idx = 0;
	}
}
#endif

/******************** Sequencer Program Patching/Download *********************/

#ifdef AHC_DUMP_SEQ
void
ahc_dumpseq(struct ahc_softc* ahc)
{
	int i;

	ahc_outb(ahc, SEQCTL, PERRORDIS|FAILDIS|FASTMODE|LOADRAM);
	ahc_outb(ahc, SEQADDR0, 0);
	ahc_outb(ahc, SEQADDR1, 0);
	for (i = 0; i < ahc->instruction_ram_size; i++) {
		uint8_t ins_bytes[4];

		ahc_insb(ahc, SEQRAM, ins_bytes, 4);
		printk("0x%08x\n", ins_bytes[0] << 24
				 | ins_bytes[1] << 16
				 | ins_bytes[2] << 8
				 | ins_bytes[3]);
	}
}
#endif

static int
ahc_loadseq(struct ahc_softc *ahc)
{
	struct	cs cs_table[NUM_CRITICAL_SECTIONS];
	u_int	begin_set[NUM_CRITICAL_SECTIONS];
	u_int	end_set[NUM_CRITICAL_SECTIONS];
	const struct patch *cur_patch;
	u_int	cs_count;
	u_int	cur_cs;
	u_int	i;
	u_int	skip_addr;
	u_int	sg_prefetch_cnt;
	int	downloaded;
	uint8_t	download_consts[7];

	/*
	 * Start out with 0 critical sections
	 * that apply to this firmware load.
	 */
	cs_count = 0;
	cur_cs = 0;
	memset(begin_set, 0, sizeof(begin_set));
	memset(end_set, 0, sizeof(end_set));

	/* Setup downloadable constant table */
	download_consts[QOUTFIFO_OFFSET] = 0;
	if (ahc->targetcmds != NULL)
		download_consts[QOUTFIFO_OFFSET] += 32;
	download_consts[QINFIFO_OFFSET] = download_consts[QOUTFIFO_OFFSET] + 1;
	download_consts[CACHESIZE_MASK] = ahc->pci_cachesize - 1;
	download_consts[INVERTED_CACHESIZE_MASK] = ~(ahc->pci_cachesize - 1);
	sg_prefetch_cnt = ahc->pci_cachesize;
	if (sg_prefetch_cnt < (2 * sizeof(struct ahc_dma_seg)))
		sg_prefetch_cnt = 2 * sizeof(struct ahc_dma_seg);
	download_consts[SG_PREFETCH_CNT] = sg_prefetch_cnt;
	download_consts[SG_PREFETCH_ALIGN_MASK] = ~(sg_prefetch_cnt - 1);
	download_consts[SG_PREFETCH_ADDR_MASK] = (sg_prefetch_cnt - 1);

	cur_patch = patches;
	downloaded = 0;
	skip_addr = 0;
	ahc_outb(ahc, SEQCTL, PERRORDIS|FAILDIS|FASTMODE|LOADRAM);
	ahc_outb(ahc, SEQADDR0, 0);
	ahc_outb(ahc, SEQADDR1, 0);

	for (i = 0; i < sizeof(seqprog)/4; i++) {
		if (ahc_check_patch(ahc, &cur_patch, i, &skip_addr) == 0) {
			/*
			 * Don't download this instruction as it
			 * is in a patch that was removed.
			 */
			continue;
		}

		if (downloaded == ahc->instruction_ram_size) {
			/*
			 * We're about to exceed the instruction
			 * storage capacity for this chip.  Fail
			 * the load.
			 */
			printk("\n%s: Program too large for instruction memory "
			       "size of %d!\n", ahc_name(ahc),
			       ahc->instruction_ram_size);
			return (ENOMEM);
		}

		/*
		 * Move through the CS table until we find a CS
		 * that might apply to this instruction.
		 */
		for (; cur_cs < NUM_CRITICAL_SECTIONS; cur_cs++) {
			if (critical_sections[cur_cs].end <= i) {
				if (begin_set[cs_count] == TRUE
				 && end_set[cs_count] == FALSE) {
					cs_table[cs_count].end = downloaded;
				 	end_set[cs_count] = TRUE;
					cs_count++;
				}
				continue;
			}
			if (critical_sections[cur_cs].begin <= i
			 && begin_set[cs_count] == FALSE) {
				cs_table[cs_count].begin = downloaded;
				begin_set[cs_count] = TRUE;
			}
			break;
		}
		ahc_download_instr(ahc, i, download_consts);
		downloaded++;
	}

	ahc->num_critical_sections = cs_count;
	if (cs_count != 0) {

		cs_count *= sizeof(struct cs);
		ahc->critical_sections = kmalloc(cs_count, GFP_ATOMIC);
		if (ahc->critical_sections == NULL)
			panic("ahc_loadseq: Could not malloc");
		memcpy(ahc->critical_sections, cs_table, cs_count);
	}
	ahc_outb(ahc, SEQCTL, PERRORDIS|FAILDIS|FASTMODE);

	if (bootverbose) {
		printk(" %d instructions downloaded\n", downloaded);
		printk("%s: Features 0x%x, Bugs 0x%x, Flags 0x%x\n",
		       ahc_name(ahc), ahc->features, ahc->bugs, ahc->flags);
	}
	return (0);
}

static int
ahc_check_patch(struct ahc_softc *ahc, const struct patch **start_patch,
		u_int start_instr, u_int *skip_addr)
{
	const struct patch *cur_patch;
	const struct patch *last_patch;
	u_int	num_patches;

	num_patches = ARRAY_SIZE(patches);
	last_patch = &patches[num_patches];
	cur_patch = *start_patch;

	while (cur_patch < last_patch && start_instr == cur_patch->begin) {

		if (cur_patch->patch_func(ahc) == 0) {

			/* Start rejecting code */
			*skip_addr = start_instr + cur_patch->skip_instr;
			cur_patch += cur_patch->skip_patch;
		} else {
			/* Accepted this patch.  Advance to the next
			 * one and wait for our intruction pointer to
			 * hit this point.
			 */
			cur_patch++;
		}
	}

	*start_patch = cur_patch;
	if (start_instr < *skip_addr)
		/* Still skipping */
		return (0);

	return (1);
}

static void
ahc_download_instr(struct ahc_softc *ahc, u_int instrptr, uint8_t *dconsts)
{
	union	ins_formats instr;
	struct	ins_format1 *fmt1_ins;
	struct	ins_format3 *fmt3_ins;
	u_int	opcode;

	/*
	 * The firmware is always compiled into a little endian format.
	 */
	instr.integer = ahc_le32toh(*(uint32_t*)&seqprog[instrptr * 4]);

	fmt1_ins = &instr.format1;
	fmt3_ins = NULL;

	/* Pull the opcode */
	opcode = instr.format1.opcode;
	switch (opcode) {
	case AIC_OP_JMP:
	case AIC_OP_JC:
	case AIC_OP_JNC:
	case AIC_OP_CALL:
	case AIC_OP_JNE:
	case AIC_OP_JNZ:
	case AIC_OP_JE:
	case AIC_OP_JZ:
	{
		const struct patch *cur_patch;
		int address_offset;
		u_int address;
		u_int skip_addr;
		u_int i;

		fmt3_ins = &instr.format3;
		address_offset = 0;
		address = fmt3_ins->address;
		cur_patch = patches;
		skip_addr = 0;

		for (i = 0; i < address;) {

			ahc_check_patch(ahc, &cur_patch, i, &skip_addr);

			if (skip_addr > i) {
				int end_addr;

				end_addr = min(address, skip_addr);
				address_offset += end_addr - i;
				i = skip_addr;
			} else {
				i++;
			}
		}
		address -= address_offset;
		fmt3_ins->address = address;
		/* FALLTHROUGH */
	}
	case AIC_OP_OR:
	case AIC_OP_AND:
	case AIC_OP_XOR:
	case AIC_OP_ADD:
	case AIC_OP_ADC:
	case AIC_OP_BMOV:
		if (fmt1_ins->parity != 0) {
			fmt1_ins->immediate = dconsts[fmt1_ins->immediate];
		}
		fmt1_ins->parity = 0;
		if ((ahc->features & AHC_CMD_CHAN) == 0
		 && opcode == AIC_OP_BMOV) {
			/*
			 * Block move was added at the same time
			 * as the command channel.  Verify that
			 * this is only a move of a single element
			 * and convert the BMOV to a MOV
			 * (AND with an immediate of FF).
			 */
			if (fmt1_ins->immediate != 1)
				panic("%s: BMOV not supported\n",
				      ahc_name(ahc));
			fmt1_ins->opcode = AIC_OP_AND;
			fmt1_ins->immediate = 0xff;
		}
		/* FALLTHROUGH */
	case AIC_OP_ROL:
		if ((ahc->features & AHC_ULTRA2) != 0) {
			int i, count;

			/* Calculate odd parity for the instruction */
			for (i = 0, count = 0; i < 31; i++) {
				uint32_t mask;

				mask = 0x01 << i;
				if ((instr.integer & mask) != 0)
					count++;
			}
			if ((count & 0x01) == 0)
				instr.format1.parity = 1;
		} else {
			/* Compress the instruction for older sequencers */
			if (fmt3_ins != NULL) {
				instr.integer =
					fmt3_ins->immediate
				      | (fmt3_ins->source << 8)
				      | (fmt3_ins->address << 16)
				      |	(fmt3_ins->opcode << 25);
			} else {
				instr.integer =
					fmt1_ins->immediate
				      | (fmt1_ins->source << 8)
				      | (fmt1_ins->destination << 16)
				      |	(fmt1_ins->ret << 24)
				      |	(fmt1_ins->opcode << 25);
			}
		}
		/* The sequencer is a little endian cpu */
		instr.integer = ahc_htole32(instr.integer);
		ahc_outsb(ahc, SEQRAM, instr.bytes, 4);
		break;
	default:
		panic("Unknown opcode encountered in seq program");
		break;
	}
}

int
ahc_print_register(const ahc_reg_parse_entry_t *table, u_int num_entries,
		   const char *name, u_int address, u_int value,
		   u_int *cur_column, u_int wrap_point)
{
	int	printed;
	u_int	printed_mask;

	if (cur_column != NULL && *cur_column >= wrap_point) {
		printk("\n");
		*cur_column = 0;
	}
	printed  = printk("%s[0x%x]", name, value);
	if (table == NULL) {
		printed += printk(" ");
		*cur_column += printed;
		return (printed);
	}
	printed_mask = 0;
	while (printed_mask != 0xFF) {
		int entry;

		for (entry = 0; entry < num_entries; entry++) {
			if (((value & table[entry].mask)
			  != table[entry].value)
			 || ((printed_mask & table[entry].mask)
			  == table[entry].mask))
				continue;

			printed += printk("%s%s",
					  printed_mask == 0 ? ":(" : "|",
					  table[entry].name);
			printed_mask |= table[entry].mask;
			
			break;
		}
		if (entry >= num_entries)
			break;
	}
	if (printed_mask != 0)
		printed += printk(") ");
	else
		printed += printk(" ");
	if (cur_column != NULL)
		*cur_column += printed;
	return (printed);
}

void
ahc_dump_card_state(struct ahc_softc *ahc)
{
	struct	scb *scb;
	struct	scb_tailq *untagged_q;
	u_int	cur_col;
	int	paused;
	int	target;
	int	maxtarget;
	int	i;
	uint8_t last_phase;
	uint8_t qinpos;
	uint8_t qintail;
	uint8_t qoutpos;
	uint8_t scb_index;
	uint8_t saved_scbptr;

	if (ahc_is_paused(ahc)) {
		paused = 1;
	} else {
		paused = 0;
		ahc_pause(ahc);
	}

	saved_scbptr = ahc_inb(ahc, SCBPTR);
	last_phase = ahc_inb(ahc, LASTPHASE);
	printk(">>>>>>>>>>>>>>>>>> Dump Card State Begins <<<<<<<<<<<<<<<<<\n"
	       "%s: Dumping Card State %s, at SEQADDR 0x%x\n",
	       ahc_name(ahc), ahc_lookup_phase_entry(last_phase)->phasemsg,
	       ahc_inb(ahc, SEQADDR0) | (ahc_inb(ahc, SEQADDR1) << 8));
	if (paused)
		printk("Card was paused\n");
	printk("ACCUM = 0x%x, SINDEX = 0x%x, DINDEX = 0x%x, ARG_2 = 0x%x\n",
	       ahc_inb(ahc, ACCUM), ahc_inb(ahc, SINDEX), ahc_inb(ahc, DINDEX),
	       ahc_inb(ahc, ARG_2));
	printk("HCNT = 0x%x SCBPTR = 0x%x\n", ahc_inb(ahc, HCNT),
	       ahc_inb(ahc, SCBPTR));
	cur_col = 0;
	if ((ahc->features & AHC_DT) != 0)
		ahc_scsiphase_print(ahc_inb(ahc, SCSIPHASE), &cur_col, 50);
	ahc_scsisigi_print(ahc_inb(ahc, SCSISIGI), &cur_col, 50);
	ahc_error_print(ahc_inb(ahc, ERROR), &cur_col, 50);
	ahc_scsibusl_print(ahc_inb(ahc, SCSIBUSL), &cur_col, 50);
	ahc_lastphase_print(ahc_inb(ahc, LASTPHASE), &cur_col, 50);
	ahc_scsiseq_print(ahc_inb(ahc, SCSISEQ), &cur_col, 50);
	ahc_sblkctl_print(ahc_inb(ahc, SBLKCTL), &cur_col, 50);
	ahc_scsirate_print(ahc_inb(ahc, SCSIRATE), &cur_col, 50);
	ahc_seqctl_print(ahc_inb(ahc, SEQCTL), &cur_col, 50);
	ahc_seq_flags_print(ahc_inb(ahc, SEQ_FLAGS), &cur_col, 50);
	ahc_sstat0_print(ahc_inb(ahc, SSTAT0), &cur_col, 50);
	ahc_sstat1_print(ahc_inb(ahc, SSTAT1), &cur_col, 50);
	ahc_sstat2_print(ahc_inb(ahc, SSTAT2), &cur_col, 50);
	ahc_sstat3_print(ahc_inb(ahc, SSTAT3), &cur_col, 50);
	ahc_simode0_print(ahc_inb(ahc, SIMODE0), &cur_col, 50);
	ahc_simode1_print(ahc_inb(ahc, SIMODE1), &cur_col, 50);
	ahc_sxfrctl0_print(ahc_inb(ahc, SXFRCTL0), &cur_col, 50);
	ahc_dfcntrl_print(ahc_inb(ahc, DFCNTRL), &cur_col, 50);
	ahc_dfstatus_print(ahc_inb(ahc, DFSTATUS), &cur_col, 50);
	if (cur_col != 0)
		printk("\n");
	printk("STACK:");
	for (i = 0; i < STACK_SIZE; i++)
		printk(" 0x%x", ahc_inb(ahc, STACK)|(ahc_inb(ahc, STACK) << 8));
	printk("\nSCB count = %d\n", ahc->scb_data->numscbs);
	printk("Kernel NEXTQSCB = %d\n", ahc->next_queued_scb->hscb->tag);
	printk("Card NEXTQSCB = %d\n", ahc_inb(ahc, NEXT_QUEUED_SCB));
	/* QINFIFO */
	printk("QINFIFO entries: ");
	if ((ahc->features & AHC_QUEUE_REGS) != 0) {
		qinpos = ahc_inb(ahc, SNSCB_QOFF);
		ahc_outb(ahc, SNSCB_QOFF, qinpos);
	} else
		qinpos = ahc_inb(ahc, QINPOS);
	qintail = ahc->qinfifonext;
	while (qinpos != qintail) {
		printk("%d ", ahc->qinfifo[qinpos]);
		qinpos++;
	}
	printk("\n");

	printk("Waiting Queue entries: ");
	scb_index = ahc_inb(ahc, WAITING_SCBH);
	i = 0;
	while (scb_index != SCB_LIST_NULL && i++ < 256) {
		ahc_outb(ahc, SCBPTR, scb_index);
		printk("%d:%d ", scb_index, ahc_inb(ahc, SCB_TAG));
		scb_index = ahc_inb(ahc, SCB_NEXT);
	}
	printk("\n");

	printk("Disconnected Queue entries: ");
	scb_index = ahc_inb(ahc, DISCONNECTED_SCBH);
	i = 0;
	while (scb_index != SCB_LIST_NULL && i++ < 256) {
		ahc_outb(ahc, SCBPTR, scb_index);
		printk("%d:%d ", scb_index, ahc_inb(ahc, SCB_TAG));
		scb_index = ahc_inb(ahc, SCB_NEXT);
	}
	printk("\n");
		
	ahc_sync_qoutfifo(ahc, BUS_DMASYNC_POSTREAD);
	printk("QOUTFIFO entries: ");
	qoutpos = ahc->qoutfifonext;
	i = 0;
	while (ahc->qoutfifo[qoutpos] != SCB_LIST_NULL && i++ < 256) {
		printk("%d ", ahc->qoutfifo[qoutpos]);
		qoutpos++;
	}
	printk("\n");

	printk("Sequencer Free SCB List: ");
	scb_index = ahc_inb(ahc, FREE_SCBH);
	i = 0;
	while (scb_index != SCB_LIST_NULL && i++ < 256) {
		ahc_outb(ahc, SCBPTR, scb_index);
		printk("%d ", scb_index);
		scb_index = ahc_inb(ahc, SCB_NEXT);
	}
	printk("\n");

	printk("Sequencer SCB Info: ");
	for (i = 0; i < ahc->scb_data->maxhscbs; i++) {
		ahc_outb(ahc, SCBPTR, i);
		cur_col  = printk("\n%3d ", i);

		ahc_scb_control_print(ahc_inb(ahc, SCB_CONTROL), &cur_col, 60);
		ahc_scb_scsiid_print(ahc_inb(ahc, SCB_SCSIID), &cur_col, 60);
		ahc_scb_lun_print(ahc_inb(ahc, SCB_LUN), &cur_col, 60);
		ahc_scb_tag_print(ahc_inb(ahc, SCB_TAG), &cur_col, 60);
	}
	printk("\n");

	printk("Pending list: ");
	i = 0;
	LIST_FOREACH(scb, &ahc->pending_scbs, pending_links) {
		if (i++ > 256)
			break;
		cur_col  = printk("\n%3d ", scb->hscb->tag);
		ahc_scb_control_print(scb->hscb->control, &cur_col, 60);
		ahc_scb_scsiid_print(scb->hscb->scsiid, &cur_col, 60);
		ahc_scb_lun_print(scb->hscb->lun, &cur_col, 60);
		if ((ahc->flags & AHC_PAGESCBS) == 0) {
			ahc_outb(ahc, SCBPTR, scb->hscb->tag);
			printk("(");
			ahc_scb_control_print(ahc_inb(ahc, SCB_CONTROL),
					      &cur_col, 60);
			ahc_scb_tag_print(ahc_inb(ahc, SCB_TAG), &cur_col, 60);
			printk(")");
		}
	}
	printk("\n");

	printk("Kernel Free SCB list: ");
	i = 0;
	SLIST_FOREACH(scb, &ahc->scb_data->free_scbs, links.sle) {
		if (i++ > 256)
			break;
		printk("%d ", scb->hscb->tag);
	}
	printk("\n");

	maxtarget = (ahc->features & (AHC_WIDE|AHC_TWIN)) ? 15 : 7;
	for (target = 0; target <= maxtarget; target++) {
		untagged_q = &ahc->untagged_queues[target];
		if (TAILQ_FIRST(untagged_q) == NULL)
			continue;
		printk("Untagged Q(%d): ", target);
		i = 0;
		TAILQ_FOREACH(scb, untagged_q, links.tqe) {
			if (i++ > 256)
				break;
			printk("%d ", scb->hscb->tag);
		}
		printk("\n");
	}

	printk("\n<<<<<<<<<<<<<<<<< Dump Card State Ends >>>>>>>>>>>>>>>>>>\n");
	ahc_outb(ahc, SCBPTR, saved_scbptr);
	if (paused == 0)
		ahc_unpause(ahc);
}

/************************* Target Mode ****************************************/
#ifdef AHC_TARGET_MODE
cam_status
ahc_find_tmode_devs(struct ahc_softc *ahc, struct cam_sim *sim, union ccb *ccb,
		    struct ahc_tmode_tstate **tstate,
		    struct ahc_tmode_lstate **lstate,
		    int notfound_failure)
{

	if ((ahc->features & AHC_TARGETMODE) == 0)
		return (CAM_REQ_INVALID);

	/*
	 * Handle the 'black hole' device that sucks up
	 * requests to unattached luns on enabled targets.
	 */
	if (ccb->ccb_h.target_id == CAM_TARGET_WILDCARD
	 && ccb->ccb_h.target_lun == CAM_LUN_WILDCARD) {
		*tstate = NULL;
		*lstate = ahc->black_hole;
	} else {
		u_int max_id;

		max_id = (ahc->features & AHC_WIDE) ? 16 : 8;
		if (ccb->ccb_h.target_id >= max_id)
			return (CAM_TID_INVALID);

		if (ccb->ccb_h.target_lun >= AHC_NUM_LUNS)
			return (CAM_LUN_INVALID);

		*tstate = ahc->enabled_targets[ccb->ccb_h.target_id];
		*lstate = NULL;
		if (*tstate != NULL)
			*lstate =
			    (*tstate)->enabled_luns[ccb->ccb_h.target_lun];
	}

	if (notfound_failure != 0 && *lstate == NULL)
		return (CAM_PATH_INVALID);

	return (CAM_REQ_CMP);
}

void
ahc_handle_en_lun(struct ahc_softc *ahc, struct cam_sim *sim, union ccb *ccb)
{
	struct	   ahc_tmode_tstate *tstate;
	struct	   ahc_tmode_lstate *lstate;
	struct	   ccb_en_lun *cel;
	cam_status status;
	u_long	   s;
	u_int	   target;
	u_int	   lun;
	u_int	   target_mask;
	u_int	   our_id;
	int	   error;
	char	   channel;

	status = ahc_find_tmode_devs(ahc, sim, ccb, &tstate, &lstate,
				     /*notfound_failure*/FALSE);

	if (status != CAM_REQ_CMP) {
		ccb->ccb_h.status = status;
		return;
	}

	if (cam_sim_bus(sim) == 0)
		our_id = ahc->our_id;
	else
		our_id = ahc->our_id_b;

	if (ccb->ccb_h.target_id != our_id) {
		/*
		 * our_id represents our initiator ID, or
		 * the ID of the first target to have an
		 * enabled lun in target mode.  There are
		 * two cases that may preclude enabling a
		 * target id other than our_id.
		 *
		 *   o our_id is for an active initiator role.
		 *     Since the hardware does not support
		 *     reselections to the initiator role at
		 *     anything other than our_id, and our_id
		 *     is used by the hardware to indicate the
		 *     ID to use for both select-out and
		 *     reselect-out operations, the only target
		 *     ID we can support in this mode is our_id.
		 *
		 *   o The MULTARGID feature is not available and
		 *     a previous target mode ID has been enabled.
		 */
		if ((ahc->features & AHC_MULTIROLE) != 0) {

			if ((ahc->features & AHC_MULTI_TID) != 0
		   	 && (ahc->flags & AHC_INITIATORROLE) != 0) {
				/*
				 * Only allow additional targets if
				 * the initiator role is disabled.
				 * The hardware cannot handle a re-select-in
				 * on the initiator id during a re-select-out
				 * on a different target id.
				 */
				status = CAM_TID_INVALID;
			} else if ((ahc->flags & AHC_INITIATORROLE) != 0
				|| ahc->enabled_luns > 0) {
				/*
				 * Only allow our target id to change
				 * if the initiator role is not configured
				 * and there are no enabled luns which
				 * are attached to the currently registered
				 * scsi id.
				 */
				status = CAM_TID_INVALID;
			}
		} else if ((ahc->features & AHC_MULTI_TID) == 0
			&& ahc->enabled_luns > 0) {

			status = CAM_TID_INVALID;
		}
	}

	if (status != CAM_REQ_CMP) {
		ccb->ccb_h.status = status;
		return;
	}

	/*
	 * We now have an id that is valid.
	 * If we aren't in target mode, switch modes.
	 */
	if ((ahc->flags & AHC_TARGETROLE) == 0
	 && ccb->ccb_h.target_id != CAM_TARGET_WILDCARD) {
		u_long	 s;
		ahc_flag saved_flags;

		printk("Configuring Target Mode\n");
		ahc_lock(ahc, &s);
		if (LIST_FIRST(&ahc->pending_scbs) != NULL) {
			ccb->ccb_h.status = CAM_BUSY;
			ahc_unlock(ahc, &s);
			return;
		}
		saved_flags = ahc->flags;
		ahc->flags |= AHC_TARGETROLE;
		if ((ahc->features & AHC_MULTIROLE) == 0)
			ahc->flags &= ~AHC_INITIATORROLE;
		ahc_pause(ahc);
		error = ahc_loadseq(ahc);
		if (error != 0) {
			/*
			 * Restore original configuration and notify
			 * the caller that we cannot support target mode.
			 * Since the adapter started out in this
			 * configuration, the firmware load will succeed,
			 * so there is no point in checking ahc_loadseq's
			 * return value.
			 */
			ahc->flags = saved_flags;
			(void)ahc_loadseq(ahc);
			ahc_restart(ahc);
			ahc_unlock(ahc, &s);
			ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
			return;
		}
		ahc_restart(ahc);
		ahc_unlock(ahc, &s);
	}
	cel = &ccb->cel;
	target = ccb->ccb_h.target_id;
	lun = ccb->ccb_h.target_lun;
	channel = SIM_CHANNEL(ahc, sim);
	target_mask = 0x01 << target;
	if (channel == 'B')
		target_mask <<= 8;

	if (cel->enable != 0) {
		u_int scsiseq;

		/* Are we already enabled?? */
		if (lstate != NULL) {
			xpt_print_path(ccb->ccb_h.path);
			printk("Lun already enabled\n");
			ccb->ccb_h.status = CAM_LUN_ALRDY_ENA;
			return;
		}

		if (cel->grp6_len != 0
		 || cel->grp7_len != 0) {
			/*
			 * Don't (yet?) support vendor
			 * specific commands.
			 */
			ccb->ccb_h.status = CAM_REQ_INVALID;
			printk("Non-zero Group Codes\n");
			return;
		}

		/*
		 * Seems to be okay.
		 * Setup our data structures.
		 */
		if (target != CAM_TARGET_WILDCARD && tstate == NULL) {
			tstate = ahc_alloc_tstate(ahc, target, channel);
			if (tstate == NULL) {
				xpt_print_path(ccb->ccb_h.path);
				printk("Couldn't allocate tstate\n");
				ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
				return;
			}
		}
		lstate = kzalloc(sizeof(*lstate), GFP_ATOMIC);
		if (lstate == NULL) {
			xpt_print_path(ccb->ccb_h.path);
			printk("Couldn't allocate lstate\n");
			ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			return;
		}
		status = xpt_create_path(&lstate->path, /*periph*/NULL,
					 xpt_path_path_id(ccb->ccb_h.path),
					 xpt_path_target_id(ccb->ccb_h.path),
					 xpt_path_lun_id(ccb->ccb_h.path));
		if (status != CAM_REQ_CMP) {
			kfree(lstate);
			xpt_print_path(ccb->ccb_h.path);
			printk("Couldn't allocate path\n");
			ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			return;
		}
		SLIST_INIT(&lstate->accept_tios);
		SLIST_INIT(&lstate->immed_notifies);
		ahc_lock(ahc, &s);
		ahc_pause(ahc);
		if (target != CAM_TARGET_WILDCARD) {
			tstate->enabled_luns[lun] = lstate;
			ahc->enabled_luns++;

			if ((ahc->features & AHC_MULTI_TID) != 0) {
				u_int targid_mask;

				targid_mask = ahc_inb(ahc, TARGID)
					    | (ahc_inb(ahc, TARGID + 1) << 8);

				targid_mask |= target_mask;
				ahc_outb(ahc, TARGID, targid_mask);
				ahc_outb(ahc, TARGID+1, (targid_mask >> 8));
				
				ahc_update_scsiid(ahc, targid_mask);
			} else {
				u_int our_id;
				char  channel;

				channel = SIM_CHANNEL(ahc, sim);
				our_id = SIM_SCSI_ID(ahc, sim);

				/*
				 * This can only happen if selections
				 * are not enabled
				 */
				if (target != our_id) {
					u_int sblkctl;
					char  cur_channel;
					int   swap;

					sblkctl = ahc_inb(ahc, SBLKCTL);
					cur_channel = (sblkctl & SELBUSB)
						    ? 'B' : 'A';
					if ((ahc->features & AHC_TWIN) == 0)
						cur_channel = 'A';
					swap = cur_channel != channel;
					if (channel == 'A')
						ahc->our_id = target;
					else
						ahc->our_id_b = target;

					if (swap)
						ahc_outb(ahc, SBLKCTL,
							 sblkctl ^ SELBUSB);

					ahc_outb(ahc, SCSIID, target);

					if (swap)
						ahc_outb(ahc, SBLKCTL, sblkctl);
				}
			}
		} else
			ahc->black_hole = lstate;
		/* Allow select-in operations */
		if (ahc->black_hole != NULL && ahc->enabled_luns > 0) {
			scsiseq = ahc_inb(ahc, SCSISEQ_TEMPLATE);
			scsiseq |= ENSELI;
			ahc_outb(ahc, SCSISEQ_TEMPLATE, scsiseq);
			scsiseq = ahc_inb(ahc, SCSISEQ);
			scsiseq |= ENSELI;
			ahc_outb(ahc, SCSISEQ, scsiseq);
		}
		ahc_unpause(ahc);
		ahc_unlock(ahc, &s);
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_print_path(ccb->ccb_h.path);
		printk("Lun now enabled for target mode\n");
	} else {
		struct scb *scb;
		int i, empty;

		if (lstate == NULL) {
			ccb->ccb_h.status = CAM_LUN_INVALID;
			return;
		}

		ahc_lock(ahc, &s);
		
		ccb->ccb_h.status = CAM_REQ_CMP;
		LIST_FOREACH(scb, &ahc->pending_scbs, pending_links) {
			struct ccb_hdr *ccbh;

			ccbh = &scb->io_ctx->ccb_h;
			if (ccbh->func_code == XPT_CONT_TARGET_IO
			 && !xpt_path_comp(ccbh->path, ccb->ccb_h.path)){
				printk("CTIO pending\n");
				ccb->ccb_h.status = CAM_REQ_INVALID;
				ahc_unlock(ahc, &s);
				return;
			}
		}

		if (SLIST_FIRST(&lstate->accept_tios) != NULL) {
			printk("ATIOs pending\n");
			ccb->ccb_h.status = CAM_REQ_INVALID;
		}

		if (SLIST_FIRST(&lstate->immed_notifies) != NULL) {
			printk("INOTs pending\n");
			ccb->ccb_h.status = CAM_REQ_INVALID;
		}

		if (ccb->ccb_h.status != CAM_REQ_CMP) {
			ahc_unlock(ahc, &s);
			return;
		}

		xpt_print_path(ccb->ccb_h.path);
		printk("Target mode disabled\n");
		xpt_free_path(lstate->path);
		kfree(lstate);

		ahc_pause(ahc);
		/* Can we clean up the target too? */
		if (target != CAM_TARGET_WILDCARD) {
			tstate->enabled_luns[lun] = NULL;
			ahc->enabled_luns--;
			for (empty = 1, i = 0; i < 8; i++)
				if (tstate->enabled_luns[i] != NULL) {
					empty = 0;
					break;
				}

			if (empty) {
				ahc_free_tstate(ahc, target, channel,
						/*force*/FALSE);
				if (ahc->features & AHC_MULTI_TID) {
					u_int targid_mask;

					targid_mask = ahc_inb(ahc, TARGID)
						    | (ahc_inb(ahc, TARGID + 1)
						       << 8);

					targid_mask &= ~target_mask;
					ahc_outb(ahc, TARGID, targid_mask);
					ahc_outb(ahc, TARGID+1,
					 	 (targid_mask >> 8));
					ahc_update_scsiid(ahc, targid_mask);
				}
			}
		} else {

			ahc->black_hole = NULL;

			/*
			 * We can't allow selections without
			 * our black hole device.
			 */
			empty = TRUE;
		}
		if (ahc->enabled_luns == 0) {
			/* Disallow select-in */
			u_int scsiseq;

			scsiseq = ahc_inb(ahc, SCSISEQ_TEMPLATE);
			scsiseq &= ~ENSELI;
			ahc_outb(ahc, SCSISEQ_TEMPLATE, scsiseq);
			scsiseq = ahc_inb(ahc, SCSISEQ);
			scsiseq &= ~ENSELI;
			ahc_outb(ahc, SCSISEQ, scsiseq);

			if ((ahc->features & AHC_MULTIROLE) == 0) {
				printk("Configuring Initiator Mode\n");
				ahc->flags &= ~AHC_TARGETROLE;
				ahc->flags |= AHC_INITIATORROLE;
				/*
				 * Returning to a configuration that
				 * fit previously will always succeed.
				 */
				(void)ahc_loadseq(ahc);
				ahc_restart(ahc);
				/*
				 * Unpaused.  The extra unpause
				 * that follows is harmless.
				 */
			}
		}
		ahc_unpause(ahc);
		ahc_unlock(ahc, &s);
	}
}

static void
ahc_update_scsiid(struct ahc_softc *ahc, u_int targid_mask)
{
	u_int scsiid_mask;
	u_int scsiid;

	if ((ahc->features & AHC_MULTI_TID) == 0)
		panic("ahc_update_scsiid called on non-multitid unit\n");

	/*
	 * Since we will rely on the TARGID mask
	 * for selection enables, ensure that OID
	 * in SCSIID is not set to some other ID
	 * that we don't want to allow selections on.
	 */
	if ((ahc->features & AHC_ULTRA2) != 0)
		scsiid = ahc_inb(ahc, SCSIID_ULTRA2);
	else
		scsiid = ahc_inb(ahc, SCSIID);
	scsiid_mask = 0x1 << (scsiid & OID);
	if ((targid_mask & scsiid_mask) == 0) {
		u_int our_id;

		/* ffs counts from 1 */
		our_id = ffs(targid_mask);
		if (our_id == 0)
			our_id = ahc->our_id;
		else
			our_id--;
		scsiid &= TID;
		scsiid |= our_id;
	}
	if ((ahc->features & AHC_ULTRA2) != 0)
		ahc_outb(ahc, SCSIID_ULTRA2, scsiid);
	else
		ahc_outb(ahc, SCSIID, scsiid);
}

static void
ahc_run_tqinfifo(struct ahc_softc *ahc, int paused)
{
	struct target_cmd *cmd;

	/*
	 * If the card supports auto-access pause,
	 * we can access the card directly regardless
	 * of whether it is paused or not.
	 */
	if ((ahc->features & AHC_AUTOPAUSE) != 0)
		paused = TRUE;

	ahc_sync_tqinfifo(ahc, BUS_DMASYNC_POSTREAD);
	while ((cmd = &ahc->targetcmds[ahc->tqinfifonext])->cmd_valid != 0) {

		/*
		 * Only advance through the queue if we
		 * have the resources to process the command.
		 */
		if (ahc_handle_target_cmd(ahc, cmd) != 0)
			break;

		cmd->cmd_valid = 0;
		ahc_dmamap_sync(ahc, ahc->shared_data_dmat,
				ahc->shared_data_dmamap,
				ahc_targetcmd_offset(ahc, ahc->tqinfifonext),
				sizeof(struct target_cmd),
				BUS_DMASYNC_PREREAD);
		ahc->tqinfifonext++;

		/*
		 * Lazily update our position in the target mode incoming
		 * command queue as seen by the sequencer.
		 */
		if ((ahc->tqinfifonext & (HOST_TQINPOS - 1)) == 1) {
			if ((ahc->features & AHC_HS_MAILBOX) != 0) {
				u_int hs_mailbox;

				hs_mailbox = ahc_inb(ahc, HS_MAILBOX);
				hs_mailbox &= ~HOST_TQINPOS;
				hs_mailbox |= ahc->tqinfifonext & HOST_TQINPOS;
				ahc_outb(ahc, HS_MAILBOX, hs_mailbox);
			} else {
				if (!paused)
					ahc_pause(ahc);	
				ahc_outb(ahc, KERNEL_TQINPOS,
					 ahc->tqinfifonext & HOST_TQINPOS);
				if (!paused)
					ahc_unpause(ahc);
			}
		}
	}
}

static int
ahc_handle_target_cmd(struct ahc_softc *ahc, struct target_cmd *cmd)
{
	struct	  ahc_tmode_tstate *tstate;
	struct	  ahc_tmode_lstate *lstate;
	struct	  ccb_accept_tio *atio;
	uint8_t *byte;
	int	  initiator;
	int	  target;
	int	  lun;

	initiator = SCSIID_TARGET(ahc, cmd->scsiid);
	target = SCSIID_OUR_ID(cmd->scsiid);
	lun    = (cmd->identify & MSG_IDENTIFY_LUNMASK);

	byte = cmd->bytes;
	tstate = ahc->enabled_targets[target];
	lstate = NULL;
	if (tstate != NULL)
		lstate = tstate->enabled_luns[lun];

	/*
	 * Commands for disabled luns go to the black hole driver.
	 */
	if (lstate == NULL)
		lstate = ahc->black_hole;

	atio = (struct ccb_accept_tio*)SLIST_FIRST(&lstate->accept_tios);
	if (atio == NULL) {
		ahc->flags |= AHC_TQINFIFO_BLOCKED;
		/*
		 * Wait for more ATIOs from the peripheral driver for this lun.
		 */
		if (bootverbose)
			printk("%s: ATIOs exhausted\n", ahc_name(ahc));
		return (1);
	} else
		ahc->flags &= ~AHC_TQINFIFO_BLOCKED;
#if 0
	printk("Incoming command from %d for %d:%d%s\n",
	       initiator, target, lun,
	       lstate == ahc->black_hole ? "(Black Holed)" : "");
#endif
	SLIST_REMOVE_HEAD(&lstate->accept_tios, sim_links.sle);

	if (lstate == ahc->black_hole) {
		/* Fill in the wildcards */
		atio->ccb_h.target_id = target;
		atio->ccb_h.target_lun = lun;
	}

	/*
	 * Package it up and send it off to
	 * whomever has this lun enabled.
	 */
	atio->sense_len = 0;
	atio->init_id = initiator;
	if (byte[0] != 0xFF) {
		/* Tag was included */
		atio->tag_action = *byte++;
		atio->tag_id = *byte++;
		atio->ccb_h.flags = CAM_TAG_ACTION_VALID;
	} else {
		atio->ccb_h.flags = 0;
	}
	byte++;

	/* Okay.  Now determine the cdb size based on the command code */
	switch (*byte >> CMD_GROUP_CODE_SHIFT) {
	case 0:
		atio->cdb_len = 6;
		break;
	case 1:
	case 2:
		atio->cdb_len = 10;
		break;
	case 4:
		atio->cdb_len = 16;
		break;
	case 5:
		atio->cdb_len = 12;
		break;
	case 3:
	default:
		/* Only copy the opcode. */
		atio->cdb_len = 1;
		printk("Reserved or VU command code type encountered\n");
		break;
	}
	
	memcpy(atio->cdb_io.cdb_bytes, byte, atio->cdb_len);

	atio->ccb_h.status |= CAM_CDB_RECVD;

	if ((cmd->identify & MSG_IDENTIFY_DISCFLAG) == 0) {
		/*
		 * We weren't allowed to disconnect.
		 * We're hanging on the bus until a
		 * continue target I/O comes in response
		 * to this accept tio.
		 */
#if 0
		printk("Received Immediate Command %d:%d:%d - %p\n",
		       initiator, target, lun, ahc->pending_device);
#endif
		ahc->pending_device = lstate;
		ahc_freeze_ccb((union ccb *)atio);
		atio->ccb_h.flags |= CAM_DIS_DISCONNECT;
	}
	xpt_done((union ccb*)atio);
	return (0);
}

#endif
