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

void ahd_set_modes(struct ahd_softc *ahd, ahd_mode src,
		   ahd_mode dst);
void ahd_update_modes(struct ahd_softc *ahd);
void ahd_assert_modes(struct ahd_softc *ahd, ahd_mode srcmode,
		      ahd_mode dstmode, const char *file,
		      int line);
ahd_mode_state ahd_save_modes(struct ahd_softc *ahd);
void ahd_restore_modes(struct ahd_softc *ahd,
		       ahd_mode_state state);
int  ahd_is_paused(struct ahd_softc *ahd);
void ahd_pause(struct ahd_softc *ahd);
void ahd_unpause(struct ahd_softc *ahd);

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

/*********************** Scatter Gather List Handling *************************/
void	*ahd_sg_setup(struct ahd_softc *ahd, struct scb *scb,
		      void *sgptr, dma_addr_t addr,
		      bus_size_t len, int last);
void	 ahd_setup_scb_common(struct ahd_softc *ahd,
			      struct scb *scb);
void	 ahd_setup_data_scb(struct ahd_softc *ahd,
			    struct scb *scb);
void	 ahd_setup_noxfer_scb(struct ahd_softc *ahd,
			      struct scb *scb);

/************************** Memory mapping routines ***************************/
static __inline size_t	ahd_sg_size(struct ahd_softc *ahd);

void *
	ahd_sg_bus_to_virt(struct ahd_softc *ahd,
			   struct scb *scb,
			   uint32_t sg_busaddr);
uint32_t
	ahd_sg_virt_to_bus(struct ahd_softc *ahd,
			   struct scb *scb,
			   void *sg);
void	ahd_sync_scb(struct ahd_softc *ahd,
		     struct scb *scb, int op);
void	ahd_sync_sglist(struct ahd_softc *ahd,
			struct scb *scb, int op);
void	ahd_sync_sense(struct ahd_softc *ahd,
		       struct scb *scb, int op);
uint32_t
	ahd_targetcmd_offset(struct ahd_softc *ahd,
			     u_int index);

static __inline size_t
ahd_sg_size(struct ahd_softc *ahd)
{
	if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0)
		return (sizeof(struct ahd_dma64_seg));
	return (sizeof(struct ahd_dma_seg));
}

/*********************** Miscellaneous Support Functions ***********************/
struct ahd_initiator_tinfo *
	ahd_fetch_transinfo(struct ahd_softc *ahd,
			    char channel, u_int our_id,
			    u_int remote_id,
			    struct ahd_tmode_tstate **tstate);
uint16_t
	ahd_inw(struct ahd_softc *ahd, u_int port);
void	ahd_outw(struct ahd_softc *ahd, u_int port,
		 u_int value);
uint32_t
	ahd_inl(struct ahd_softc *ahd, u_int port);
void	ahd_outl(struct ahd_softc *ahd, u_int port,
		 uint32_t value);
uint64_t
	ahd_inq(struct ahd_softc *ahd, u_int port);
void	ahd_outq(struct ahd_softc *ahd, u_int port,
		 uint64_t value);
u_int	ahd_get_scbptr(struct ahd_softc *ahd);
void	ahd_set_scbptr(struct ahd_softc *ahd, u_int scbptr);
u_int	ahd_get_hnscb_qoff(struct ahd_softc *ahd);
void	ahd_set_hnscb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_get_hescb_qoff(struct ahd_softc *ahd);
void	ahd_set_hescb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_get_snscb_qoff(struct ahd_softc *ahd);
void	ahd_set_snscb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_get_sescb_qoff(struct ahd_softc *ahd);
void	ahd_set_sescb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_get_sdscb_qoff(struct ahd_softc *ahd);
void	ahd_set_sdscb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_inb_scbram(struct ahd_softc *ahd, u_int offset);
u_int	ahd_inw_scbram(struct ahd_softc *ahd, u_int offset);
uint32_t
	ahd_inl_scbram(struct ahd_softc *ahd, u_int offset);
uint64_t
	ahd_inq_scbram(struct ahd_softc *ahd, u_int offset);
struct scb *
	ahd_lookup_scb(struct ahd_softc *ahd, u_int tag);
void	ahd_swap_with_next_hscb(struct ahd_softc *ahd,
				struct scb *scb);
void	ahd_queue_scb(struct ahd_softc *ahd, struct scb *scb);

static __inline uint8_t *
			ahd_get_sense_buf(struct ahd_softc *ahd,
					  struct scb *scb);
static __inline uint32_t
			ahd_get_sense_bufaddr(struct ahd_softc *ahd,
					      struct scb *scb);

#if 0 /* unused */

#define AHD_COPY_COL_IDX(dst, src)				\
do {								\
	dst->hscb->scsiid = src->hscb->scsiid;			\
	dst->hscb->lun = src->hscb->lun;			\
} while (0)

#endif

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
void	ahd_sync_qoutfifo(struct ahd_softc *ahd, int op);
void	ahd_sync_tqinfifo(struct ahd_softc *ahd, int op);
u_int	ahd_check_cmdcmpltqueues(struct ahd_softc *ahd);
int	ahd_intr(struct ahd_softc *ahd);

#endif  /* _AIC79XX_INLINE_H_ */
