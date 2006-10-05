/*
 * Inline routines shareable across OS platforms.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2000-2003 Adaptec Inc.
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
 * $Id: //depot/aic7xxx/aic7xxx/aic79xx_inline.h#59 $
 *
 * $FreeBSD$
 */

#ifndef _AIC79XX_INLINE_H_
#define _AIC79XX_INLINE_H_

/******************************** Debugging ***********************************/
static __inline char *ahd_name(struct ahd_softc *ahd);

static __inline char *
ahd_name(struct ahd_softc *ahd)
{
	return (ahd->name);
}

/************************ Sequencer Execution Control *************************/
static __inline void ahd_known_modes(struct ahd_softc *ahd,
				     ahd_mode src, ahd_mode dst);
static __inline ahd_mode_state ahd_build_mode_state(struct ahd_softc *ahd,
						    ahd_mode src,
						    ahd_mode dst);
static __inline void ahd_extract_mode_state(struct ahd_softc *ahd,
					    ahd_mode_state state,
					    ahd_mode *src, ahd_mode *dst);
static __inline void ahd_set_modes(struct ahd_softc *ahd, ahd_mode src,
				   ahd_mode dst);
static __inline void ahd_update_modes(struct ahd_softc *ahd);
static __inline void ahd_assert_modes(struct ahd_softc *ahd, ahd_mode srcmode,
				      ahd_mode dstmode, const char *file,
				      int line);
static __inline ahd_mode_state ahd_save_modes(struct ahd_softc *ahd);
static __inline void ahd_restore_modes(struct ahd_softc *ahd,
				       ahd_mode_state state);
static __inline int  ahd_is_paused(struct ahd_softc *ahd);
static __inline void ahd_pause(struct ahd_softc *ahd);
static __inline void ahd_unpause(struct ahd_softc *ahd);

static __inline void
ahd_known_modes(struct ahd_softc *ahd, ahd_mode src, ahd_mode dst)
{
	ahd->src_mode = src;
	ahd->dst_mode = dst;
	ahd->saved_src_mode = src;
	ahd->saved_dst_mode = dst;
}

static __inline ahd_mode_state
ahd_build_mode_state(struct ahd_softc *ahd, ahd_mode src, ahd_mode dst)
{
	return ((src << SRC_MODE_SHIFT) | (dst << DST_MODE_SHIFT));
}

static __inline void
ahd_extract_mode_state(struct ahd_softc *ahd, ahd_mode_state state,
		       ahd_mode *src, ahd_mode *dst)
{
	*src = (state & SRC_MODE) >> SRC_MODE_SHIFT;
	*dst = (state & DST_MODE) >> DST_MODE_SHIFT;
}

static __inline void
ahd_set_modes(struct ahd_softc *ahd, ahd_mode src, ahd_mode dst)
{
	if (ahd->src_mode == src && ahd->dst_mode == dst)
		return;
#ifdef AHD_DEBUG
	if (ahd->src_mode == AHD_MODE_UNKNOWN
	 || ahd->dst_mode == AHD_MODE_UNKNOWN)
		panic("Setting mode prior to saving it.\n");
	if ((ahd_debug & AHD_SHOW_MODEPTR) != 0)
		printf("%s: Setting mode 0x%x\n", ahd_name(ahd),
		       ahd_build_mode_state(ahd, src, dst));
#endif
	ahd_outb(ahd, MODE_PTR, ahd_build_mode_state(ahd, src, dst));
	ahd->src_mode = src;
	ahd->dst_mode = dst;
}

static __inline void
ahd_update_modes(struct ahd_softc *ahd)
{
	ahd_mode_state mode_ptr;
	ahd_mode src;
	ahd_mode dst;

	mode_ptr = ahd_inb(ahd, MODE_PTR);
#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_MODEPTR) != 0)
		printf("Reading mode 0x%x\n", mode_ptr);
#endif
	ahd_extract_mode_state(ahd, mode_ptr, &src, &dst);
	ahd_known_modes(ahd, src, dst);
}

static __inline void
ahd_assert_modes(struct ahd_softc *ahd, ahd_mode srcmode,
		 ahd_mode dstmode, const char *file, int line)
{
#ifdef AHD_DEBUG
	if ((srcmode & AHD_MK_MSK(ahd->src_mode)) == 0
	 || (dstmode & AHD_MK_MSK(ahd->dst_mode)) == 0) {
		panic("%s:%s:%d: Mode assertion failed.\n",
		       ahd_name(ahd), file, line);
	}
#endif
}

static __inline ahd_mode_state
ahd_save_modes(struct ahd_softc *ahd)
{
	if (ahd->src_mode == AHD_MODE_UNKNOWN
	 || ahd->dst_mode == AHD_MODE_UNKNOWN)
		ahd_update_modes(ahd);

	return (ahd_build_mode_state(ahd, ahd->src_mode, ahd->dst_mode));
}

static __inline void
ahd_restore_modes(struct ahd_softc *ahd, ahd_mode_state state)
{
	ahd_mode src;
	ahd_mode dst;

	ahd_extract_mode_state(ahd, state, &src, &dst);
	ahd_set_modes(ahd, src, dst);
}

#define AHD_ASSERT_MODES(ahd, source, dest) \
	ahd_assert_modes(ahd, source, dest, __FILE__, __LINE__);

/*
 * Determine whether the sequencer has halted code execution.
 * Returns non-zero status if the sequencer is stopped.
 */
static __inline int
ahd_is_paused(struct ahd_softc *ahd)
{
	return ((ahd_inb(ahd, HCNTRL) & PAUSE) != 0);
}

/*
 * Request that the sequencer stop and wait, indefinitely, for it
 * to stop.  The sequencer will only acknowledge that it is paused
 * once it has reached an instruction boundary and PAUSEDIS is
 * cleared in the SEQCTL register.  The sequencer may use PAUSEDIS
 * for critical sections.
 */
static __inline void
ahd_pause(struct ahd_softc *ahd)
{
	ahd_outb(ahd, HCNTRL, ahd->pause);

	/*
	 * Since the sequencer can disable pausing in a critical section, we
	 * must loop until it actually stops.
	 */
	while (ahd_is_paused(ahd) == 0)
		;
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
ahd_unpause(struct ahd_softc *ahd)
{
	/*
	 * Automatically restore our modes to those saved
	 * prior to the first change of the mode.
	 */
	if (ahd->saved_src_mode != AHD_MODE_UNKNOWN
	 && ahd->saved_dst_mode != AHD_MODE_UNKNOWN) {
		if ((ahd->flags & AHD_UPDATE_PEND_CMDS) != 0)
			ahd_reset_cmds_pending(ahd);
		ahd_set_modes(ahd, ahd->saved_src_mode, ahd->saved_dst_mode);
	}

	if ((ahd_inb(ahd, INTSTAT) & ~CMDCMPLT) == 0)
		ahd_outb(ahd, HCNTRL, ahd->unpause);

	ahd_known_modes(ahd, AHD_MODE_UNKNOWN, AHD_MODE_UNKNOWN);
}

/*********************** Scatter Gather List Handling *************************/
static __inline void	*ahd_sg_setup(struct ahd_softc *ahd, struct scb *scb,
				      void *sgptr, dma_addr_t addr,
				      bus_size_t len, int last);
static __inline void	 ahd_setup_scb_common(struct ahd_softc *ahd,
					      struct scb *scb);
static __inline void	 ahd_setup_data_scb(struct ahd_softc *ahd,
					    struct scb *scb);
static __inline void	 ahd_setup_noxfer_scb(struct ahd_softc *ahd,
					      struct scb *scb);

static __inline void *
ahd_sg_setup(struct ahd_softc *ahd, struct scb *scb,
	     void *sgptr, dma_addr_t addr, bus_size_t len, int last)
{
	scb->sg_count++;
	if (sizeof(dma_addr_t) > 4
	 && (ahd->flags & AHD_64BIT_ADDRESSING) != 0) {
		struct ahd_dma64_seg *sg;

		sg = (struct ahd_dma64_seg *)sgptr;
		sg->addr = ahd_htole64(addr);
		sg->len = ahd_htole32(len | (last ? AHD_DMA_LAST_SEG : 0));
		return (sg + 1);
	} else {
		struct ahd_dma_seg *sg;

		sg = (struct ahd_dma_seg *)sgptr;
		sg->addr = ahd_htole32(addr & 0xFFFFFFFF);
		sg->len = ahd_htole32(len | ((addr >> 8) & 0x7F000000)
				    | (last ? AHD_DMA_LAST_SEG : 0));
		return (sg + 1);
	}
}

static __inline void
ahd_setup_scb_common(struct ahd_softc *ahd, struct scb *scb)
{
	/* XXX Handle target mode SCBs. */
	scb->crc_retry_count = 0;
	if ((scb->flags & SCB_PACKETIZED) != 0) {
		/* XXX what about ACA??  It is type 4, but TAG_TYPE == 0x3. */
		scb->hscb->task_attribute = scb->hscb->control & SCB_TAG_TYPE;
	} else {
		if (ahd_get_transfer_length(scb) & 0x01)
			scb->hscb->task_attribute = SCB_XFERLEN_ODD;
		else
			scb->hscb->task_attribute = 0;
	}

	if (scb->hscb->cdb_len <= MAX_CDB_LEN_WITH_SENSE_ADDR
	 || (scb->hscb->cdb_len & SCB_CDB_LEN_PTR) != 0)
		scb->hscb->shared_data.idata.cdb_plus_saddr.sense_addr =
		    ahd_htole32(scb->sense_busaddr);
}

static __inline void
ahd_setup_data_scb(struct ahd_softc *ahd, struct scb *scb)
{
	/*
	 * Copy the first SG into the "current" data ponter area.
	 */
	if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0) {
		struct ahd_dma64_seg *sg;

		sg = (struct ahd_dma64_seg *)scb->sg_list;
		scb->hscb->dataptr = sg->addr;
		scb->hscb->datacnt = sg->len;
	} else {
		struct ahd_dma_seg *sg;
		uint32_t *dataptr_words;

		sg = (struct ahd_dma_seg *)scb->sg_list;
		dataptr_words = (uint32_t*)&scb->hscb->dataptr;
		dataptr_words[0] = sg->addr;
		dataptr_words[1] = 0;
		if ((ahd->flags & AHD_39BIT_ADDRESSING) != 0) {
			uint64_t high_addr;

			high_addr = ahd_le32toh(sg->len) & 0x7F000000;
			scb->hscb->dataptr |= ahd_htole64(high_addr << 8);
		}
		scb->hscb->datacnt = sg->len;
	}
	/*
	 * Note where to find the SG entries in bus space.
	 * We also set the full residual flag which the 
	 * sequencer will clear as soon as a data transfer
	 * occurs.
	 */
	scb->hscb->sgptr = ahd_htole32(scb->sg_list_busaddr|SG_FULL_RESID);
}

static __inline void
ahd_setup_noxfer_scb(struct ahd_softc *ahd, struct scb *scb)
{
	scb->hscb->sgptr = ahd_htole32(SG_LIST_NULL);
	scb->hscb->dataptr = 0;
	scb->hscb->datacnt = 0;
}

/************************** Memory mapping routines ***************************/
static __inline size_t	ahd_sg_size(struct ahd_softc *ahd);
static __inline void *
			ahd_sg_bus_to_virt(struct ahd_softc *ahd,
					   struct scb *scb,
					   uint32_t sg_busaddr);
static __inline uint32_t
			ahd_sg_virt_to_bus(struct ahd_softc *ahd,
					   struct scb *scb,
					   void *sg);
static __inline void	ahd_sync_scb(struct ahd_softc *ahd,
				     struct scb *scb, int op);
static __inline void	ahd_sync_sglist(struct ahd_softc *ahd,
					struct scb *scb, int op);
static __inline void	ahd_sync_sense(struct ahd_softc *ahd,
				       struct scb *scb, int op);
static __inline uint32_t
			ahd_targetcmd_offset(struct ahd_softc *ahd,
					     u_int index);

static __inline size_t
ahd_sg_size(struct ahd_softc *ahd)
{
	if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0)
		return (sizeof(struct ahd_dma64_seg));
	return (sizeof(struct ahd_dma_seg));
}

static __inline void *
ahd_sg_bus_to_virt(struct ahd_softc *ahd, struct scb *scb, uint32_t sg_busaddr)
{
	dma_addr_t sg_offset;

	/* sg_list_phys points to entry 1, not 0 */
	sg_offset = sg_busaddr - (scb->sg_list_busaddr - ahd_sg_size(ahd));
	return ((uint8_t *)scb->sg_list + sg_offset);
}

static __inline uint32_t
ahd_sg_virt_to_bus(struct ahd_softc *ahd, struct scb *scb, void *sg)
{
	dma_addr_t sg_offset;

	/* sg_list_phys points to entry 1, not 0 */
	sg_offset = ((uint8_t *)sg - (uint8_t *)scb->sg_list)
		  - ahd_sg_size(ahd);

	return (scb->sg_list_busaddr + sg_offset);
}

static __inline void
ahd_sync_scb(struct ahd_softc *ahd, struct scb *scb, int op)
{
	ahd_dmamap_sync(ahd, ahd->scb_data.hscb_dmat,
			scb->hscb_map->dmamap,
			/*offset*/(uint8_t*)scb->hscb - scb->hscb_map->vaddr,
			/*len*/sizeof(*scb->hscb), op);
}

static __inline void
ahd_sync_sglist(struct ahd_softc *ahd, struct scb *scb, int op)
{
	if (scb->sg_count == 0)
		return;

	ahd_dmamap_sync(ahd, ahd->scb_data.sg_dmat,
			scb->sg_map->dmamap,
			/*offset*/scb->sg_list_busaddr - ahd_sg_size(ahd),
			/*len*/ahd_sg_size(ahd) * scb->sg_count, op);
}

static __inline void
ahd_sync_sense(struct ahd_softc *ahd, struct scb *scb, int op)
{
	ahd_dmamap_sync(ahd, ahd->scb_data.sense_dmat,
			scb->sense_map->dmamap,
			/*offset*/scb->sense_busaddr,
			/*len*/AHD_SENSE_BUFSIZE, op);
}

static __inline uint32_t
ahd_targetcmd_offset(struct ahd_softc *ahd, u_int index)
{
	return (((uint8_t *)&ahd->targetcmds[index])
	       - (uint8_t *)ahd->qoutfifo);
}

/*********************** Miscelaneous Support Functions ***********************/
static __inline void	ahd_complete_scb(struct ahd_softc *ahd,
					 struct scb *scb);
static __inline void	ahd_update_residual(struct ahd_softc *ahd,
					    struct scb *scb);
static __inline struct ahd_initiator_tinfo *
			ahd_fetch_transinfo(struct ahd_softc *ahd,
					    char channel, u_int our_id,
					    u_int remote_id,
					    struct ahd_tmode_tstate **tstate);
static __inline uint16_t
			ahd_inw(struct ahd_softc *ahd, u_int port);
static __inline void	ahd_outw(struct ahd_softc *ahd, u_int port,
				 u_int value);
static __inline uint32_t
			ahd_inl(struct ahd_softc *ahd, u_int port);
static __inline void	ahd_outl(struct ahd_softc *ahd, u_int port,
				 uint32_t value);
static __inline uint64_t
			ahd_inq(struct ahd_softc *ahd, u_int port);
static __inline void	ahd_outq(struct ahd_softc *ahd, u_int port,
				 uint64_t value);
static __inline u_int	ahd_get_scbptr(struct ahd_softc *ahd);
static __inline void	ahd_set_scbptr(struct ahd_softc *ahd, u_int scbptr);
static __inline u_int	ahd_get_hnscb_qoff(struct ahd_softc *ahd);
static __inline void	ahd_set_hnscb_qoff(struct ahd_softc *ahd, u_int value);
static __inline u_int	ahd_get_hescb_qoff(struct ahd_softc *ahd);
static __inline void	ahd_set_hescb_qoff(struct ahd_softc *ahd, u_int value);
static __inline u_int	ahd_get_snscb_qoff(struct ahd_softc *ahd);
static __inline void	ahd_set_snscb_qoff(struct ahd_softc *ahd, u_int value);
static __inline u_int	ahd_get_sescb_qoff(struct ahd_softc *ahd);
static __inline void	ahd_set_sescb_qoff(struct ahd_softc *ahd, u_int value);
static __inline u_int	ahd_get_sdscb_qoff(struct ahd_softc *ahd);
static __inline void	ahd_set_sdscb_qoff(struct ahd_softc *ahd, u_int value);
static __inline u_int	ahd_inb_scbram(struct ahd_softc *ahd, u_int offset);
static __inline u_int	ahd_inw_scbram(struct ahd_softc *ahd, u_int offset);
static __inline uint32_t
			ahd_inl_scbram(struct ahd_softc *ahd, u_int offset);
static __inline uint64_t
			ahd_inq_scbram(struct ahd_softc *ahd, u_int offset);
static __inline void	ahd_swap_with_next_hscb(struct ahd_softc *ahd,
						struct scb *scb);
static __inline void	ahd_queue_scb(struct ahd_softc *ahd, struct scb *scb);
static __inline uint8_t *
			ahd_get_sense_buf(struct ahd_softc *ahd,
					  struct scb *scb);
static __inline uint32_t
			ahd_get_sense_bufaddr(struct ahd_softc *ahd,
					      struct scb *scb);

static __inline void
ahd_complete_scb(struct ahd_softc *ahd, struct scb *scb)
{
	uint32_t sgptr;

	sgptr = ahd_le32toh(scb->hscb->sgptr);
	if ((sgptr & SG_STATUS_VALID) != 0)
		ahd_handle_scb_status(ahd, scb);
	else
		ahd_done(ahd, scb);
}

/*
 * Determine whether the sequencer reported a residual
 * for this SCB/transaction.
 */
static __inline void
ahd_update_residual(struct ahd_softc *ahd, struct scb *scb)
{
	uint32_t sgptr;

	sgptr = ahd_le32toh(scb->hscb->sgptr);
	if ((sgptr & SG_STATUS_VALID) != 0)
		ahd_calc_residual(ahd, scb);
}

/*
 * Return pointers to the transfer negotiation information
 * for the specified our_id/remote_id pair.
 */
static __inline struct ahd_initiator_tinfo *
ahd_fetch_transinfo(struct ahd_softc *ahd, char channel, u_int our_id,
		    u_int remote_id, struct ahd_tmode_tstate **tstate)
{
	/*
	 * Transfer data structures are stored from the perspective
	 * of the target role.  Since the parameters for a connection
	 * in the initiator role to a given target are the same as
	 * when the roles are reversed, we pretend we are the target.
	 */
	if (channel == 'B')
		our_id += 8;
	*tstate = ahd->enabled_targets[our_id];
	return (&(*tstate)->transinfo[remote_id]);
}

#define AHD_COPY_COL_IDX(dst, src)				\
do {								\
	dst->hscb->scsiid = src->hscb->scsiid;			\
	dst->hscb->lun = src->hscb->lun;			\
} while (0)

static __inline uint16_t
ahd_inw(struct ahd_softc *ahd, u_int port)
{
	/*
	 * Read high byte first as some registers increment
	 * or have other side effects when the low byte is
	 * read.
	 */
	uint16_t r = ahd_inb(ahd, port+1) << 8;
	return r | ahd_inb(ahd, port);
}

static __inline void
ahd_outw(struct ahd_softc *ahd, u_int port, u_int value)
{
	/*
	 * Write low byte first to accomodate registers
	 * such as PRGMCNT where the order maters.
	 */
	ahd_outb(ahd, port, value & 0xFF);
	ahd_outb(ahd, port+1, (value >> 8) & 0xFF);
}

static __inline uint32_t
ahd_inl(struct ahd_softc *ahd, u_int port)
{
	return ((ahd_inb(ahd, port))
	      | (ahd_inb(ahd, port+1) << 8)
	      | (ahd_inb(ahd, port+2) << 16)
	      | (ahd_inb(ahd, port+3) << 24));
}

static __inline void
ahd_outl(struct ahd_softc *ahd, u_int port, uint32_t value)
{
	ahd_outb(ahd, port, (value) & 0xFF);
	ahd_outb(ahd, port+1, ((value) >> 8) & 0xFF);
	ahd_outb(ahd, port+2, ((value) >> 16) & 0xFF);
	ahd_outb(ahd, port+3, ((value) >> 24) & 0xFF);
}

static __inline uint64_t
ahd_inq(struct ahd_softc *ahd, u_int port)
{
	return ((ahd_inb(ahd, port))
	      | (ahd_inb(ahd, port+1) << 8)
	      | (ahd_inb(ahd, port+2) << 16)
	      | (ahd_inb(ahd, port+3) << 24)
	      | (((uint64_t)ahd_inb(ahd, port+4)) << 32)
	      | (((uint64_t)ahd_inb(ahd, port+5)) << 40)
	      | (((uint64_t)ahd_inb(ahd, port+6)) << 48)
	      | (((uint64_t)ahd_inb(ahd, port+7)) << 56));
}

static __inline void
ahd_outq(struct ahd_softc *ahd, u_int port, uint64_t value)
{
	ahd_outb(ahd, port, value & 0xFF);
	ahd_outb(ahd, port+1, (value >> 8) & 0xFF);
	ahd_outb(ahd, port+2, (value >> 16) & 0xFF);
	ahd_outb(ahd, port+3, (value >> 24) & 0xFF);
	ahd_outb(ahd, port+4, (value >> 32) & 0xFF);
	ahd_outb(ahd, port+5, (value >> 40) & 0xFF);
	ahd_outb(ahd, port+6, (value >> 48) & 0xFF);
	ahd_outb(ahd, port+7, (value >> 56) & 0xFF);
}

static __inline u_int
ahd_get_scbptr(struct ahd_softc *ahd)
{
	AHD_ASSERT_MODES(ahd, ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK),
			 ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK));
	return (ahd_inb(ahd, SCBPTR) | (ahd_inb(ahd, SCBPTR + 1) << 8));
}

static __inline void
ahd_set_scbptr(struct ahd_softc *ahd, u_int scbptr)
{
	AHD_ASSERT_MODES(ahd, ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK),
			 ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK));
	ahd_outb(ahd, SCBPTR, scbptr & 0xFF);
	ahd_outb(ahd, SCBPTR+1, (scbptr >> 8) & 0xFF);
}

static __inline u_int
ahd_get_hnscb_qoff(struct ahd_softc *ahd)
{
	return (ahd_inw_atomic(ahd, HNSCB_QOFF));
}

static __inline void
ahd_set_hnscb_qoff(struct ahd_softc *ahd, u_int value)
{
	ahd_outw_atomic(ahd, HNSCB_QOFF, value);
}

static __inline u_int
ahd_get_hescb_qoff(struct ahd_softc *ahd)
{
	return (ahd_inb(ahd, HESCB_QOFF));
}

static __inline void
ahd_set_hescb_qoff(struct ahd_softc *ahd, u_int value)
{
	ahd_outb(ahd, HESCB_QOFF, value);
}

static __inline u_int
ahd_get_snscb_qoff(struct ahd_softc *ahd)
{
	u_int oldvalue;

	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	oldvalue = ahd_inw(ahd, SNSCB_QOFF);
	ahd_outw(ahd, SNSCB_QOFF, oldvalue);
	return (oldvalue);
}

static __inline void
ahd_set_snscb_qoff(struct ahd_softc *ahd, u_int value)
{
	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	ahd_outw(ahd, SNSCB_QOFF, value);
}

static __inline u_int
ahd_get_sescb_qoff(struct ahd_softc *ahd)
{
	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	return (ahd_inb(ahd, SESCB_QOFF));
}

static __inline void
ahd_set_sescb_qoff(struct ahd_softc *ahd, u_int value)
{
	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	ahd_outb(ahd, SESCB_QOFF, value);
}

static __inline u_int
ahd_get_sdscb_qoff(struct ahd_softc *ahd)
{
	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	return (ahd_inb(ahd, SDSCB_QOFF) | (ahd_inb(ahd, SDSCB_QOFF + 1) << 8));
}

static __inline void
ahd_set_sdscb_qoff(struct ahd_softc *ahd, u_int value)
{
	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	ahd_outb(ahd, SDSCB_QOFF, value & 0xFF);
	ahd_outb(ahd, SDSCB_QOFF+1, (value >> 8) & 0xFF);
}

static __inline u_int
ahd_inb_scbram(struct ahd_softc *ahd, u_int offset)
{
	u_int value;

	/*
	 * Workaround PCI-X Rev A. hardware bug.
	 * After a host read of SCB memory, the chip
	 * may become confused into thinking prefetch
	 * was required.  This starts the discard timer
	 * running and can cause an unexpected discard
	 * timer interrupt.  The work around is to read
	 * a normal register prior to the exhaustion of
	 * the discard timer.  The mode pointer register
	 * has no side effects and so serves well for
	 * this purpose.
	 *
	 * Razor #528
	 */
	value = ahd_inb(ahd, offset);
	if ((ahd->bugs & AHD_PCIX_SCBRAM_RD_BUG) != 0)
		ahd_inb(ahd, MODE_PTR);
	return (value);
}

static __inline u_int
ahd_inw_scbram(struct ahd_softc *ahd, u_int offset)
{
	return (ahd_inb_scbram(ahd, offset)
	      | (ahd_inb_scbram(ahd, offset+1) << 8));
}

static __inline uint32_t
ahd_inl_scbram(struct ahd_softc *ahd, u_int offset)
{
	return (ahd_inw_scbram(ahd, offset)
	      | (ahd_inw_scbram(ahd, offset+2) << 16));
}

static __inline uint64_t
ahd_inq_scbram(struct ahd_softc *ahd, u_int offset)
{
	return (ahd_inl_scbram(ahd, offset)
	      | ((uint64_t)ahd_inl_scbram(ahd, offset+4)) << 32);
}

static __inline struct scb *
ahd_lookup_scb(struct ahd_softc *ahd, u_int tag)
{
	struct scb* scb;

	if (tag >= AHD_SCB_MAX)
		return (NULL);
	scb = ahd->scb_data.scbindex[tag];
	if (scb != NULL)
		ahd_sync_scb(ahd, scb,
			     BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	return (scb);
}

static __inline void
ahd_swap_with_next_hscb(struct ahd_softc *ahd, struct scb *scb)
{
	struct	 hardware_scb *q_hscb;
	struct	 map_node *q_hscb_map;
	uint32_t saved_hscb_busaddr;

	/*
	 * Our queuing method is a bit tricky.  The card
	 * knows in advance which HSCB (by address) to download,
	 * and we can't disappoint it.  To achieve this, the next
	 * HSCB to download is saved off in ahd->next_queued_hscb.
	 * When we are called to queue "an arbitrary scb",
	 * we copy the contents of the incoming HSCB to the one
	 * the sequencer knows about, swap HSCB pointers and
	 * finally assign the SCB to the tag indexed location
	 * in the scb_array.  This makes sure that we can still
	 * locate the correct SCB by SCB_TAG.
	 */
	q_hscb = ahd->next_queued_hscb;
	q_hscb_map = ahd->next_queued_hscb_map;
	saved_hscb_busaddr = q_hscb->hscb_busaddr;
	memcpy(q_hscb, scb->hscb, sizeof(*scb->hscb));
	q_hscb->hscb_busaddr = saved_hscb_busaddr;
	q_hscb->next_hscb_busaddr = scb->hscb->hscb_busaddr;

	/* Now swap HSCB pointers. */
	ahd->next_queued_hscb = scb->hscb;
	ahd->next_queued_hscb_map = scb->hscb_map;
	scb->hscb = q_hscb;
	scb->hscb_map = q_hscb_map;

	/* Now define the mapping from tag to SCB in the scbindex */
	ahd->scb_data.scbindex[SCB_GET_TAG(scb)] = scb;
}

/*
 * Tell the sequencer about a new transaction to execute.
 */
static __inline void
ahd_queue_scb(struct ahd_softc *ahd, struct scb *scb)
{
	ahd_swap_with_next_hscb(ahd, scb);

	if (SCBID_IS_NULL(SCB_GET_TAG(scb)))
		panic("Attempt to queue invalid SCB tag %x\n",
		      SCB_GET_TAG(scb));

	/*
	 * Keep a history of SCBs we've downloaded in the qinfifo.
	 */
	ahd->qinfifo[AHD_QIN_WRAP(ahd->qinfifonext)] = SCB_GET_TAG(scb);
	ahd->qinfifonext++;

	if (scb->sg_count != 0)
		ahd_setup_data_scb(ahd, scb);
	else
		ahd_setup_noxfer_scb(ahd, scb);
	ahd_setup_scb_common(ahd, scb);

	/*
	 * Make sure our data is consistent from the
	 * perspective of the adapter.
	 */
	ahd_sync_scb(ahd, scb, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_QUEUE) != 0) {
		uint64_t host_dataptr;

		host_dataptr = ahd_le64toh(scb->hscb->dataptr);
		printf("%s: Queueing SCB %d:0x%x bus addr 0x%x - 0x%x%x/0x%x\n",
		       ahd_name(ahd),
		       SCB_GET_TAG(scb), scb->hscb->scsiid,
		       ahd_le32toh(scb->hscb->hscb_busaddr),
		       (u_int)((host_dataptr >> 32) & 0xFFFFFFFF),
		       (u_int)(host_dataptr & 0xFFFFFFFF),
		       ahd_le32toh(scb->hscb->datacnt));
	}
#endif
	/* Tell the adapter about the newly queued SCB */
	ahd_set_hnscb_qoff(ahd, ahd->qinfifonext);
}

static __inline uint8_t *
ahd_get_sense_buf(struct ahd_softc *ahd, struct scb *scb)
{
	return (scb->sense_data);
}

static __inline uint32_t
ahd_get_sense_bufaddr(struct ahd_softc *ahd, struct scb *scb)
{
	return (scb->sense_busaddr);
}

/************************** Interrupt Processing ******************************/
static __inline void	ahd_sync_qoutfifo(struct ahd_softc *ahd, int op);
static __inline void	ahd_sync_tqinfifo(struct ahd_softc *ahd, int op);
static __inline u_int	ahd_check_cmdcmpltqueues(struct ahd_softc *ahd);
static __inline int	ahd_intr(struct ahd_softc *ahd);

static __inline void
ahd_sync_qoutfifo(struct ahd_softc *ahd, int op)
{
	ahd_dmamap_sync(ahd, ahd->shared_data_dmat, ahd->shared_data_map.dmamap,
			/*offset*/0,
			/*len*/AHD_SCB_MAX * sizeof(struct ahd_completion), op);
}

static __inline void
ahd_sync_tqinfifo(struct ahd_softc *ahd, int op)
{
#ifdef AHD_TARGET_MODE
	if ((ahd->flags & AHD_TARGETROLE) != 0) {
		ahd_dmamap_sync(ahd, ahd->shared_data_dmat,
				ahd->shared_data_map.dmamap,
				ahd_targetcmd_offset(ahd, 0),
				sizeof(struct target_cmd) * AHD_TMODE_CMDS,
				op);
	}
#endif
}

/*
 * See if the firmware has posted any completed commands
 * into our in-core command complete fifos.
 */
#define AHD_RUN_QOUTFIFO 0x1
#define AHD_RUN_TQINFIFO 0x2
static __inline u_int
ahd_check_cmdcmpltqueues(struct ahd_softc *ahd)
{
	u_int retval;

	retval = 0;
	ahd_dmamap_sync(ahd, ahd->shared_data_dmat, ahd->shared_data_map.dmamap,
			/*offset*/ahd->qoutfifonext * sizeof(*ahd->qoutfifo),
			/*len*/sizeof(*ahd->qoutfifo), BUS_DMASYNC_POSTREAD);
	if (ahd->qoutfifo[ahd->qoutfifonext].valid_tag
	  == ahd->qoutfifonext_valid_tag)
		retval |= AHD_RUN_QOUTFIFO;
#ifdef AHD_TARGET_MODE
	if ((ahd->flags & AHD_TARGETROLE) != 0
	 && (ahd->flags & AHD_TQINFIFO_BLOCKED) == 0) {
		ahd_dmamap_sync(ahd, ahd->shared_data_dmat,
				ahd->shared_data_map.dmamap,
				ahd_targetcmd_offset(ahd, ahd->tqinfifofnext),
				/*len*/sizeof(struct target_cmd),
				BUS_DMASYNC_POSTREAD);
		if (ahd->targetcmds[ahd->tqinfifonext].cmd_valid != 0)
			retval |= AHD_RUN_TQINFIFO;
	}
#endif
	return (retval);
}

/*
 * Catch an interrupt from the adapter
 */
static __inline int
ahd_intr(struct ahd_softc *ahd)
{
	u_int	intstat;

	if ((ahd->pause & INTEN) == 0) {
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
	if ((ahd->flags & AHD_ALL_INTERRUPTS) == 0
	 && (ahd_check_cmdcmpltqueues(ahd) != 0))
		intstat = CMDCMPLT;
	else
		intstat = ahd_inb(ahd, INTSTAT);

	if ((intstat & INT_PEND) == 0)
		return (0);

	if (intstat & CMDCMPLT) {
		ahd_outb(ahd, CLRINT, CLRCMDINT);

		/*
		 * Ensure that the chip sees that we've cleared
		 * this interrupt before we walk the output fifo.
		 * Otherwise, we may, due to posted bus writes,
		 * clear the interrupt after we finish the scan,
		 * and after the sequencer has added new entries
		 * and asserted the interrupt again.
		 */
		if ((ahd->bugs & AHD_INTCOLLISION_BUG) != 0) {
			if (ahd_is_paused(ahd)) {
				/*
				 * Potentially lost SEQINT.
				 * If SEQINTCODE is non-zero,
				 * simulate the SEQINT.
				 */
				if (ahd_inb(ahd, SEQINTCODE) != NO_SEQINT)
					intstat |= SEQINT;
			}
		} else {
			ahd_flush_device_writes(ahd);
		}
		ahd_run_qoutfifo(ahd);
		ahd->cmdcmplt_counts[ahd->cmdcmplt_bucket]++;
		ahd->cmdcmplt_total++;
#ifdef AHD_TARGET_MODE
		if ((ahd->flags & AHD_TARGETROLE) != 0)
			ahd_run_tqinfifo(ahd, /*paused*/FALSE);
#endif
	}

	/*
	 * Handle statuses that may invalidate our cached
	 * copy of INTSTAT separately.
	 */
	if (intstat == 0xFF && (ahd->features & AHD_REMOVABLE) != 0) {
		/* Hot eject.  Do nothing */
	} else if (intstat & HWERRINT) {
		ahd_handle_hwerrint(ahd);
	} else if ((intstat & (PCIINT|SPLTINT)) != 0) {
		ahd->bus_intr(ahd);
	} else {

		if ((intstat & SEQINT) != 0)
			ahd_handle_seqint(ahd, intstat);

		if ((intstat & SCSIINT) != 0)
			ahd_handle_scsiint(ahd, intstat);
	}
	return (1);
}

#endif  /* _AIC79XX_INLINE_H_ */
