/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/gfp.h>

#ifndef SYM_HIPD_H
#define SYM_HIPD_H

/*
 *  Generic driver options.
 *
 *  They may be defined in platform specific headers, if they 
 *  are useful.
 *
 *    SYM_OPT_HANDLE_DEVICE_QUEUEING
 *        When this option is set, the driver will use a queue per 
 *        device and handle QUEUE FULL status requeuing internally.
 *
 *    SYM_OPT_LIMIT_COMMAND_REORDERING
 *        When this option is set, the driver tries to limit tagged 
 *        command reordering to some reasonnable value.
 *        (set for Linux)
 */
#if 0
#define SYM_OPT_HANDLE_DEVICE_QUEUEING
#define SYM_OPT_LIMIT_COMMAND_REORDERING
#endif

/*
 *  Active debugging tags and verbosity.
 *  Both DEBUG_FLAGS and sym_verbose can be redefined 
 *  by the platform specific code to something else.
 */
#define DEBUG_ALLOC	(0x0001)
#define DEBUG_PHASE	(0x0002)
#define DEBUG_POLL	(0x0004)
#define DEBUG_QUEUE	(0x0008)
#define DEBUG_RESULT	(0x0010)
#define DEBUG_SCATTER	(0x0020)
#define DEBUG_SCRIPT	(0x0040)
#define DEBUG_TINY	(0x0080)
#define DEBUG_TIMING	(0x0100)
#define DEBUG_NEGO	(0x0200)
#define DEBUG_TAGS	(0x0400)
#define DEBUG_POINTER	(0x0800)

#ifndef DEBUG_FLAGS
#define DEBUG_FLAGS	(0x0000)
#endif

#ifndef sym_verbose
#define sym_verbose	(np->verbose)
#endif

/*
 *  These ones should have been already defined.
 */
#ifndef assert
#define	assert(expression) { \
	if (!(expression)) { \
		(void)panic( \
			"assertion \"%s\" failed: file \"%s\", line %d\n", \
			#expression, \
			__FILE__, __LINE__); \
	} \
}
#endif

/*
 *  Number of tasks per device we want to handle.
 */
#if	SYM_CONF_MAX_TAG_ORDER > 8
#error	"more than 256 tags per logical unit not allowed."
#endif
#define	SYM_CONF_MAX_TASK	(1<<SYM_CONF_MAX_TAG_ORDER)

/*
 *  Donnot use more tasks that we can handle.
 */
#ifndef	SYM_CONF_MAX_TAG
#define	SYM_CONF_MAX_TAG	SYM_CONF_MAX_TASK
#endif
#if	SYM_CONF_MAX_TAG > SYM_CONF_MAX_TASK
#undef	SYM_CONF_MAX_TAG
#define	SYM_CONF_MAX_TAG	SYM_CONF_MAX_TASK
#endif

/*
 *    This one means 'NO TAG for this job'
 */
#define NO_TAG	(256)

/*
 *  Number of SCSI targets.
 */
#if	SYM_CONF_MAX_TARGET > 16
#error	"more than 16 targets not allowed."
#endif

/*
 *  Number of logical units per target.
 */
#if	SYM_CONF_MAX_LUN > 64
#error	"more than 64 logical units per target not allowed."
#endif

/*
 *    Asynchronous pre-scaler (ns). Shall be 40 for 
 *    the SCSI timings to be compliant.
 */
#define	SYM_CONF_MIN_ASYNC (40)


/*
 * MEMORY ALLOCATOR.
 */

#define SYM_MEM_WARN	1	/* Warn on failed operations */

#define SYM_MEM_PAGE_ORDER 0	/* 1 PAGE  maximum */
#define SYM_MEM_CLUSTER_SHIFT	(PAGE_SHIFT+SYM_MEM_PAGE_ORDER)
#define SYM_MEM_FREE_UNUSED	/* Free unused pages immediately */
/*
 *  Shortest memory chunk is (1<<SYM_MEM_SHIFT), currently 16.
 *  Actual allocations happen as SYM_MEM_CLUSTER_SIZE sized.
 *  (1 PAGE at a time is just fine).
 */
#define SYM_MEM_SHIFT	4
#define SYM_MEM_CLUSTER_SIZE	(1UL << SYM_MEM_CLUSTER_SHIFT)
#define SYM_MEM_CLUSTER_MASK	(SYM_MEM_CLUSTER_SIZE-1)

/*
 *  Number of entries in the START and DONE queues.
 *
 *  We limit to 1 PAGE in order to succeed allocation of 
 *  these queues. Each entry is 8 bytes long (2 DWORDS).
 */
#ifdef	SYM_CONF_MAX_START
#define	SYM_CONF_MAX_QUEUE (SYM_CONF_MAX_START+2)
#else
#define	SYM_CONF_MAX_QUEUE (7*SYM_CONF_MAX_TASK+2)
#define	SYM_CONF_MAX_START (SYM_CONF_MAX_QUEUE-2)
#endif

#if	SYM_CONF_MAX_QUEUE > SYM_MEM_CLUSTER_SIZE/8
#undef	SYM_CONF_MAX_QUEUE
#define	SYM_CONF_MAX_QUEUE (SYM_MEM_CLUSTER_SIZE/8)
#undef	SYM_CONF_MAX_START
#define	SYM_CONF_MAX_START (SYM_CONF_MAX_QUEUE-2)
#endif

/*
 *  For this one, we want a short name :-)
 */
#define MAX_QUEUE	SYM_CONF_MAX_QUEUE

/*
 *  Common definitions for both bus space based and legacy IO methods.
 */

#define INB_OFF(np, o)		ioread8(np->s.ioaddr + (o))
#define INW_OFF(np, o)		ioread16(np->s.ioaddr + (o))
#define INL_OFF(np, o)		ioread32(np->s.ioaddr + (o))

#define OUTB_OFF(np, o, val)	iowrite8((val), np->s.ioaddr + (o))
#define OUTW_OFF(np, o, val)	iowrite16((val), np->s.ioaddr + (o))
#define OUTL_OFF(np, o, val)	iowrite32((val), np->s.ioaddr + (o))

#define INB(np, r)		INB_OFF(np, offsetof(struct sym_reg, r))
#define INW(np, r)		INW_OFF(np, offsetof(struct sym_reg, r))
#define INL(np, r)		INL_OFF(np, offsetof(struct sym_reg, r))

#define OUTB(np, r, v)		OUTB_OFF(np, offsetof(struct sym_reg, r), (v))
#define OUTW(np, r, v)		OUTW_OFF(np, offsetof(struct sym_reg, r), (v))
#define OUTL(np, r, v)		OUTL_OFF(np, offsetof(struct sym_reg, r), (v))

#define OUTONB(np, r, m)	OUTB(np, r, INB(np, r) | (m))
#define OUTOFFB(np, r, m)	OUTB(np, r, INB(np, r) & ~(m))
#define OUTONW(np, r, m)	OUTW(np, r, INW(np, r) | (m))
#define OUTOFFW(np, r, m)	OUTW(np, r, INW(np, r) & ~(m))
#define OUTONL(np, r, m)	OUTL(np, r, INL(np, r) | (m))
#define OUTOFFL(np, r, m)	OUTL(np, r, INL(np, r) & ~(m))

/*
 *  We normally want the chip to have a consistent view
 *  of driver internal data structures when we restart it.
 *  Thus these macros.
 */
#define OUTL_DSP(np, v)				\
	do {					\
		MEMORY_WRITE_BARRIER();		\
		OUTL(np, nc_dsp, (v));		\
	} while (0)

#define OUTONB_STD()				\
	do {					\
		MEMORY_WRITE_BARRIER();		\
		OUTONB(np, nc_dcntl, (STD|NOCOM));	\
	} while (0)

/*
 *  Command control block states.
 */
#define HS_IDLE		(0)
#define HS_BUSY		(1)
#define HS_NEGOTIATE	(2)	/* sync/wide data transfer*/
#define HS_DISCONNECT	(3)	/* Disconnected by target */
#define HS_WAIT		(4)	/* waiting for resource	  */

#define HS_DONEMASK	(0x80)
#define HS_COMPLETE	(4|HS_DONEMASK)
#define HS_SEL_TIMEOUT	(5|HS_DONEMASK)	/* Selection timeout      */
#define HS_UNEXPECTED	(6|HS_DONEMASK)	/* Unexpected disconnect  */
#define HS_COMP_ERR	(7|HS_DONEMASK)	/* Completed with error	  */

/*
 *  Software Interrupt Codes
 */
#define	SIR_BAD_SCSI_STATUS	(1)
#define	SIR_SEL_ATN_NO_MSG_OUT	(2)
#define	SIR_MSG_RECEIVED	(3)
#define	SIR_MSG_WEIRD		(4)
#define	SIR_NEGO_FAILED		(5)
#define	SIR_NEGO_PROTO		(6)
#define	SIR_SCRIPT_STOPPED	(7)
#define	SIR_REJECT_TO_SEND	(8)
#define	SIR_SWIDE_OVERRUN	(9)
#define	SIR_SODL_UNDERRUN	(10)
#define	SIR_RESEL_NO_MSG_IN	(11)
#define	SIR_RESEL_NO_IDENTIFY	(12)
#define	SIR_RESEL_BAD_LUN	(13)
#define	SIR_TARGET_SELECTED	(14)
#define	SIR_RESEL_BAD_I_T_L	(15)
#define	SIR_RESEL_BAD_I_T_L_Q	(16)
#define	SIR_ABORT_SENT		(17)
#define	SIR_RESEL_ABORTED	(18)
#define	SIR_MSG_OUT_DONE	(19)
#define	SIR_COMPLETE_ERROR	(20)
#define	SIR_DATA_OVERRUN	(21)
#define	SIR_BAD_PHASE		(22)
#if	SYM_CONF_DMA_ADDRESSING_MODE == 2
#define	SIR_DMAP_DIRTY		(23)
#define	SIR_MAX			(23)
#else
#define	SIR_MAX			(22)
#endif

/*
 *  Extended error bit codes.
 *  xerr_status field of struct sym_ccb.
 */
#define	XE_EXTRA_DATA	(1)	/* unexpected data phase	 */
#define	XE_BAD_PHASE	(1<<1)	/* illegal phase (4/5)		 */
#define	XE_PARITY_ERR	(1<<2)	/* unrecovered SCSI parity error */
#define	XE_SODL_UNRUN	(1<<3)	/* ODD transfer in DATA OUT phase */
#define	XE_SWIDE_OVRUN	(1<<4)	/* ODD transfer in DATA IN phase */

/*
 *  Negotiation status.
 *  nego_status field of struct sym_ccb.
 */
#define NS_SYNC		(1)
#define NS_WIDE		(2)
#define NS_PPR		(3)

/*
 *  A CCB hashed table is used to retrieve CCB address 
 *  from DSA value.
 */
#define CCB_HASH_SHIFT		8
#define CCB_HASH_SIZE		(1UL << CCB_HASH_SHIFT)
#define CCB_HASH_MASK		(CCB_HASH_SIZE-1)
#if 1
#define CCB_HASH_CODE(dsa)	\
	(((dsa) >> (_LGRU16_(sizeof(struct sym_ccb)))) & CCB_HASH_MASK)
#else
#define CCB_HASH_CODE(dsa)	(((dsa) >> 9) & CCB_HASH_MASK)
#endif

#if	SYM_CONF_DMA_ADDRESSING_MODE == 2
/*
 *  We may want to use segment registers for 64 bit DMA.
 *  16 segments registers -> up to 64 GB addressable.
 */
#define SYM_DMAP_SHIFT	(4)
#define SYM_DMAP_SIZE	(1u<<SYM_DMAP_SHIFT)
#define SYM_DMAP_MASK	(SYM_DMAP_SIZE-1)
#endif

/*
 *  Device flags.
 */
#define SYM_DISC_ENABLED	(1)
#define SYM_TAGS_ENABLED	(1<<1)
#define SYM_SCAN_BOOT_DISABLED	(1<<2)
#define SYM_SCAN_LUNS_DISABLED	(1<<3)

/*
 *  Host adapter miscellaneous flags.
 */
#define SYM_AVOID_BUS_RESET	(1)

/*
 *  Misc.
 */
#define SYM_SNOOP_TIMEOUT (10000000)
#define BUS_8_BIT	0
#define BUS_16_BIT	1

/*
 *  Gather negotiable parameters value
 */
struct sym_trans {
	u8 period;
	u8 offset;
	unsigned int width:1;
	unsigned int iu:1;
	unsigned int dt:1;
	unsigned int qas:1;
	unsigned int check_nego:1;
	unsigned int renego:2;
};

/*
 *  Global TCB HEADER.
 *
 *  Due to lack of indirect addressing on earlier NCR chips,
 *  this substructure is copied from the TCB to a global 
 *  address after selection.
 *  For SYMBIOS chips that support LOAD/STORE this copy is 
 *  not needed and thus not performed.
 */
struct sym_tcbh {
	/*
	 *  Scripts bus addresses of LUN table accessed from scripts.
	 *  LUN #0 is a special case, since multi-lun devices are rare, 
	 *  and we we want to speed-up the general case and not waste 
	 *  resources.
	 */
	u32	luntbl_sa;	/* bus address of this table	*/
	u32	lun0_sa;	/* bus address of LCB #0	*/
	/*
	 *  Actual SYNC/WIDE IO registers value for this target.
	 *  'sval', 'wval' and 'uval' are read from SCRIPTS and 
	 *  so have alignment constraints.
	 */
/*0*/	u_char	uval;		/* -> SCNTL4 register		*/
/*1*/	u_char	sval;		/* -> SXFER  io register	*/
/*2*/	u_char	filler1;
/*3*/	u_char	wval;		/* -> SCNTL3 io register	*/
};

/*
 *  Target Control Block
 */
struct sym_tcb {
	/*
	 *  TCB header.
	 *  Assumed at offset 0.
	 */
/*0*/	struct sym_tcbh head;

	/*
	 *  LUN table used by the SCRIPTS processor.
	 *  An array of bus addresses is used on reselection.
	 */
	u32	*luntbl;	/* LCBs bus address table	*/

	/*
	 *  LUN table used by the C code.
	 */
	struct sym_lcb *lun0p;		/* LCB of LUN #0 (usual case)	*/
#if SYM_CONF_MAX_LUN > 1
	struct sym_lcb **lunmp;		/* Other LCBs [1..MAX_LUN]	*/
#endif

#ifdef	SYM_HAVE_STCB
	/*
	 *  O/S specific data structure.
	 */
	struct sym_stcb s;
#endif

	/* Transfer goal */
	struct sym_trans tgoal;

	/* Last printed transfer speed */
	struct sym_trans tprint;

	/*
	 * Keep track of the CCB used for the negotiation in order
	 * to ensure that only 1 negotiation is queued at a time.
	 */
	struct sym_ccb *  nego_cp;	/* CCB used for the nego		*/

	/*
	 *  Set when we want to reset the device.
	 */
	u_char	to_reset;

	/*
	 *  Other user settable limits and options.
	 *  These limits are read from the NVRAM if present.
	 */
	unsigned char	usrflags;
	unsigned char	usr_period;
	unsigned char	usr_width;
	unsigned short	usrtags;
	struct scsi_target *starget;
};

/*
 *  Global LCB HEADER.
 *
 *  Due to lack of indirect addressing on earlier NCR chips,
 *  this substructure is copied from the LCB to a global 
 *  address after selection.
 *  For SYMBIOS chips that support LOAD/STORE this copy is 
 *  not needed and thus not performed.
 */
struct sym_lcbh {
	/*
	 *  SCRIPTS address jumped by SCRIPTS on reselection.
	 *  For not probed logical units, this address points to 
	 *  SCRIPTS that deal with bad LU handling (must be at 
	 *  offset zero of the LCB for that reason).
	 */
/*0*/	u32	resel_sa;

	/*
	 *  Task (bus address of a CCB) read from SCRIPTS that points 
	 *  to the unique ITL nexus allowed to be disconnected.
	 */
	u32	itl_task_sa;

	/*
	 *  Task table bus address (read from SCRIPTS).
	 */
	u32	itlq_tbl_sa;
};

/*
 *  Logical Unit Control Block
 */
struct sym_lcb {
	/*
	 *  TCB header.
	 *  Assumed at offset 0.
	 */
/*0*/	struct sym_lcbh head;

	/*
	 *  Task table read from SCRIPTS that contains pointers to 
	 *  ITLQ nexuses. The bus address read from SCRIPTS is 
	 *  inside the header.
	 */
	u32	*itlq_tbl;	/* Kernel virtual address	*/

	/*
	 *  Busy CCBs management.
	 */
	u_short	busy_itlq;	/* Number of busy tagged CCBs	*/
	u_short	busy_itl;	/* Number of busy untagged CCBs	*/

	/*
	 *  Circular tag allocation buffer.
	 */
	u_short	ia_tag;		/* Tag allocation index		*/
	u_short	if_tag;		/* Tag release index		*/
	u_char	*cb_tags;	/* Circular tags buffer		*/

	/*
	 *  O/S specific data structure.
	 */
#ifdef	SYM_HAVE_SLCB
	struct sym_slcb s;
#endif

#ifdef SYM_OPT_HANDLE_DEVICE_QUEUEING
	/*
	 *  Optionnaly the driver can handle device queueing, 
	 *  and requeues internally command to redo.
	 */
	SYM_QUEHEAD waiting_ccbq;
	SYM_QUEHEAD started_ccbq;
	int	num_sgood;
	u_short	started_tags;
	u_short	started_no_tag;
	u_short	started_max;
	u_short	started_limit;
#endif

#ifdef SYM_OPT_LIMIT_COMMAND_REORDERING
	/*
	 *  Optionally the driver can try to prevent SCSI 
	 *  IOs from being reordered too much.
	 */
	u_char		tags_si;	/* Current index to tags sum	*/
	u_short		tags_sum[2];	/* Tags sum counters		*/
	u_short		tags_since;	/* # of tags since last switch	*/
#endif

	/*
	 *  Set when we want to clear all tasks.
	 */
	u_char to_clear;

	/*
	 *  Capabilities.
	 */
	u_char	user_flags;
	u_char	curr_flags;
};

/*
 *  Action from SCRIPTS on a task.
 *  Is part of the CCB, but is also used separately to plug 
 *  error handling action to perform from SCRIPTS.
 */
struct sym_actscr {
	u32	start;		/* Jumped by SCRIPTS after selection	*/
	u32	restart;	/* Jumped by SCRIPTS on relection	*/
};

/*
 *  Phase mismatch context.
 *
 *  It is part of the CCB and is used as parameters for the 
 *  DATA pointer. We need two contexts to handle correctly the 
 *  SAVED DATA POINTER.
 */
struct sym_pmc {
	struct	sym_tblmove sg;	/* Updated interrupted SG block	*/
	u32	ret;		/* SCRIPT return address	*/
};

/*
 *  LUN control block lookup.
 *  We use a direct pointer for LUN #0, and a table of 
 *  pointers which is only allocated for devices that support 
 *  LUN(s) > 0.
 */
#if SYM_CONF_MAX_LUN <= 1
#define sym_lp(tp, lun) (!lun) ? (tp)->lun0p : NULL
#else
#define sym_lp(tp, lun) \
	(!lun) ? (tp)->lun0p : (tp)->lunmp ? (tp)->lunmp[(lun)] : NULL
#endif

/*
 *  Status are used by the host and the script processor.
 *
 *  The last four bytes (status[4]) are copied to the 
 *  scratchb register (declared as scr0..scr3) just after the 
 *  select/reselect, and copied back just after disconnecting.
 *  Inside the script the XX_REG are used.
 */

/*
 *  Last four bytes (script)
 */
#define  HX_REG	scr0
#define  HX_PRT	nc_scr0
#define  HS_REG	scr1
#define  HS_PRT	nc_scr1
#define  SS_REG	scr2
#define  SS_PRT	nc_scr2
#define  HF_REG	scr3
#define  HF_PRT	nc_scr3

/*
 *  Last four bytes (host)
 */
#define  host_xflags   phys.head.status[0]
#define  host_status   phys.head.status[1]
#define  ssss_status   phys.head.status[2]
#define  host_flags    phys.head.status[3]

/*
 *  Host flags
 */
#define HF_IN_PM0	1u
#define HF_IN_PM1	(1u<<1)
#define HF_ACT_PM	(1u<<2)
#define HF_DP_SAVED	(1u<<3)
#define HF_SENSE	(1u<<4)
#define HF_EXT_ERR	(1u<<5)
#define HF_DATA_IN	(1u<<6)
#ifdef SYM_CONF_IARB_SUPPORT
#define HF_HINT_IARB	(1u<<7)
#endif

/*
 *  More host flags
 */
#if	SYM_CONF_DMA_ADDRESSING_MODE == 2
#define	HX_DMAP_DIRTY	(1u<<7)
#endif

/*
 *  Global CCB HEADER.
 *
 *  Due to lack of indirect addressing on earlier NCR chips,
 *  this substructure is copied from the ccb to a global 
 *  address after selection (or reselection) and copied back 
 *  before disconnect.
 *  For SYMBIOS chips that support LOAD/STORE this copy is 
 *  not needed and thus not performed.
 */

struct sym_ccbh {
	/*
	 *  Start and restart SCRIPTS addresses (must be at 0).
	 */
/*0*/	struct sym_actscr go;

	/*
	 *  SCRIPTS jump address that deal with data pointers.
	 *  'savep' points to the position in the script responsible 
	 *  for the actual transfer of data.
	 *  It's written on reception of a SAVE_DATA_POINTER message.
	 */
	u32	savep;		/* Jump address to saved data pointer	*/
	u32	lastp;		/* SCRIPTS address at end of data	*/

	/*
	 *  Status fields.
	 */
	u8	status[4];
};

/*
 *  GET/SET the value of the data pointer used by SCRIPTS.
 *
 *  We must distinguish between the LOAD/STORE-based SCRIPTS 
 *  that use directly the header in the CCB, and the NCR-GENERIC 
 *  SCRIPTS that use the copy of the header in the HCB.
 */
#if	SYM_CONF_GENERIC_SUPPORT
#define sym_set_script_dp(np, cp, dp)				\
	do {							\
		if (np->features & FE_LDSTR)			\
			cp->phys.head.lastp = cpu_to_scr(dp);	\
		else						\
			np->ccb_head.lastp = cpu_to_scr(dp);	\
	} while (0)
#define sym_get_script_dp(np, cp) 				\
	scr_to_cpu((np->features & FE_LDSTR) ?			\
		cp->phys.head.lastp : np->ccb_head.lastp)
#else
#define sym_set_script_dp(np, cp, dp)				\
	do {							\
		cp->phys.head.lastp = cpu_to_scr(dp);		\
	} while (0)

#define sym_get_script_dp(np, cp) (cp->phys.head.lastp)
#endif

/*
 *  Data Structure Block
 *
 *  During execution of a ccb by the script processor, the 
 *  DSA (data structure address) register points to this 
 *  substructure of the ccb.
 */
struct sym_dsb {
	/*
	 *  CCB header.
	 *  Also assumed at offset 0 of the sym_ccb structure.
	 */
/*0*/	struct sym_ccbh head;

	/*
	 *  Phase mismatch contexts.
	 *  We need two to handle correctly the SAVED DATA POINTER.
	 *  MUST BOTH BE AT OFFSET < 256, due to using 8 bit arithmetic 
	 *  for address calculation from SCRIPTS.
	 */
	struct sym_pmc pm0;
	struct sym_pmc pm1;

	/*
	 *  Table data for Script
	 */
	struct sym_tblsel  select;
	struct sym_tblmove smsg;
	struct sym_tblmove smsg_ext;
	struct sym_tblmove cmd;
	struct sym_tblmove sense;
	struct sym_tblmove wresid;
	struct sym_tblmove data [SYM_CONF_MAX_SG];
};

/*
 *  Our Command Control Block
 */
struct sym_ccb {
	/*
	 *  This is the data structure which is pointed by the DSA 
	 *  register when it is executed by the script processor.
	 *  It must be the first entry.
	 */
	struct sym_dsb phys;

	/*
	 *  Pointer to CAM ccb and related stuff.
	 */
	struct scsi_cmnd *cmd;	/* CAM scsiio ccb		*/
	u8	cdb_buf[16];	/* Copy of CDB			*/
#define	SYM_SNS_BBUF_LEN 32
	u8	sns_bbuf[SYM_SNS_BBUF_LEN]; /* Bounce buffer for sense data */
	int	data_len;	/* Total data length		*/
	int	segments;	/* Number of SG segments	*/

	u8	order;		/* Tag type (if tagged command)	*/
	unsigned char odd_byte_adjustment;	/* odd-sized req on wide bus */

	u_char	nego_status;	/* Negotiation status		*/
	u_char	xerr_status;	/* Extended error flags		*/
	u32	extra_bytes;	/* Extraneous bytes transferred	*/

	/*
	 *  Message areas.
	 *  We prepare a message to be sent after selection.
	 *  We may use a second one if the command is rescheduled 
	 *  due to CHECK_CONDITION or COMMAND TERMINATED.
	 *  Contents are IDENTIFY and SIMPLE_TAG.
	 *  While negotiating sync or wide transfer,
	 *  a SDTR or WDTR message is appended.
	 */
	u_char	scsi_smsg [12];
	u_char	scsi_smsg2[12];

	/*
	 *  Auto request sense related fields.
	 */
	u_char	sensecmd[6];	/* Request Sense command	*/
	u_char	sv_scsi_status;	/* Saved SCSI status 		*/
	u_char	sv_xerr_status;	/* Saved extended status	*/
	int	sv_resid;	/* Saved residual		*/

	/*
	 *  Other fields.
	 */
	u32	ccb_ba;		/* BUS address of this CCB	*/
	u_short	tag;		/* Tag for this transfer	*/
				/*  NO_TAG means no tag		*/
	u_char	target;
	u_char	lun;
	struct sym_ccb *link_ccbh;	/* Host adapter CCB hash chain	*/
	SYM_QUEHEAD link_ccbq;	/* Link to free/busy CCB queue	*/
	u32	startp;		/* Initial data pointer		*/
	u32	goalp;		/* Expected last data pointer	*/
	int	ext_sg;		/* Extreme data pointer, used	*/
	int	ext_ofs;	/*  to calculate the residual.	*/
#ifdef SYM_OPT_HANDLE_DEVICE_QUEUEING
	SYM_QUEHEAD link2_ccbq;	/* Link for device queueing	*/
	u_char	started;	/* CCB queued to the squeue	*/
#endif
	u_char	to_abort;	/* Want this IO to be aborted	*/
#ifdef SYM_OPT_LIMIT_COMMAND_REORDERING
	u_char	tags_si;	/* Lun tags sum index (0,1)	*/
#endif
};

#define CCB_BA(cp,lbl)	cpu_to_scr(cp->ccb_ba + offsetof(struct sym_ccb, lbl))

typedef struct device *m_pool_ident_t;

/*
 *  Host Control Block
 */
struct sym_hcb {
	/*
	 *  Global headers.
	 *  Due to poorness of addressing capabilities, earlier 
	 *  chips (810, 815, 825) copy part of the data structures 
	 *  (CCB, TCB and LCB) in fixed areas.
	 */
#if	SYM_CONF_GENERIC_SUPPORT
	struct sym_ccbh	ccb_head;
	struct sym_tcbh	tcb_head;
	struct sym_lcbh	lcb_head;
#endif
	/*
	 *  Idle task and invalid task actions and 
	 *  their bus addresses.
	 */
	struct sym_actscr idletask, notask, bad_itl, bad_itlq;
	u32 idletask_ba, notask_ba, bad_itl_ba, bad_itlq_ba;

	/*
	 *  Dummy lun table to protect us against target 
	 *  returning bad lun number on reselection.
	 */
	u32	*badluntbl;	/* Table physical address	*/
	u32	badlun_sa;	/* SCRIPT handler BUS address	*/

	/*
	 *  Bus address of this host control block.
	 */
	u32	hcb_ba;

	/*
	 *  Bit 32-63 of the on-chip RAM bus address in LE format.
	 *  The START_RAM64 script loads the MMRS and MMWS from this 
	 *  field.
	 */
	u32	scr_ram_seg;

	/*
	 *  Initial value of some IO register bits.
	 *  These values are assumed to have been set by BIOS, and may 
	 *  be used to probe adapter implementation differences.
	 */
	u_char	sv_scntl0, sv_scntl3, sv_dmode, sv_dcntl, sv_ctest3, sv_ctest4,
		sv_ctest5, sv_gpcntl, sv_stest2, sv_stest4, sv_scntl4,
		sv_stest1;

	/*
	 *  Actual initial value of IO register bits used by the 
	 *  driver. They are loaded at initialisation according to  
	 *  features that are to be enabled/disabled.
	 */
	u_char	rv_scntl0, rv_scntl3, rv_dmode, rv_dcntl, rv_ctest3, rv_ctest4, 
		rv_ctest5, rv_stest2, rv_ccntl0, rv_ccntl1, rv_scntl4;

	/*
	 *  Target data.
	 */
	struct sym_tcb	target[SYM_CONF_MAX_TARGET];

	/*
	 *  Target control block bus address array used by the SCRIPT 
	 *  on reselection.
	 */
	u32		*targtbl;
	u32		targtbl_ba;

	/*
	 *  DMA pool handle for this HBA.
	 */
	m_pool_ident_t	bus_dmat;

	/*
	 *  O/S specific data structure
	 */
	struct sym_shcb s;

	/*
	 *  Physical bus addresses of the chip.
	 */
	u32		mmio_ba;	/* MMIO 32 bit BUS address	*/
	u32		ram_ba;		/* RAM 32 bit BUS address	*/

	/*
	 *  SCRIPTS virtual and physical bus addresses.
	 *  'script'  is loaded in the on-chip RAM if present.
	 *  'scripth' stays in main memory for all chips except the 
	 *  53C895A, 53C896 and 53C1010 that provide 8K on-chip RAM.
	 */
	u_char		*scripta0;	/* Copy of scripts A, B, Z	*/
	u_char		*scriptb0;
	u_char		*scriptz0;
	u32		scripta_ba;	/* Actual scripts A, B, Z	*/
	u32		scriptb_ba;	/* 32 bit bus addresses.	*/
	u32		scriptz_ba;
	u_short		scripta_sz;	/* Actual size of script A, B, Z*/
	u_short		scriptb_sz;
	u_short		scriptz_sz;

	/*
	 *  Bus addresses, setup and patch methods for 
	 *  the selected firmware.
	 */
	struct sym_fwa_ba fwa_bas;	/* Useful SCRIPTA bus addresses	*/
	struct sym_fwb_ba fwb_bas;	/* Useful SCRIPTB bus addresses	*/
	struct sym_fwz_ba fwz_bas;	/* Useful SCRIPTZ bus addresses	*/
	void		(*fw_setup)(struct sym_hcb *np, struct sym_fw *fw);
	void		(*fw_patch)(struct Scsi_Host *);
	char		*fw_name;

	/*
	 *  General controller parameters and configuration.
	 */
	u_int	features;	/* Chip features map		*/
	u_char	myaddr;		/* SCSI id of the adapter	*/
	u_char	maxburst;	/* log base 2 of dwords burst	*/
	u_char	maxwide;	/* Maximum transfer width	*/
	u_char	minsync;	/* Min sync period factor (ST)	*/
	u_char	maxsync;	/* Max sync period factor (ST)	*/
	u_char	maxoffs;	/* Max scsi offset        (ST)	*/
	u_char	minsync_dt;	/* Min sync period factor (DT)	*/
	u_char	maxsync_dt;	/* Max sync period factor (DT)	*/
	u_char	maxoffs_dt;	/* Max scsi offset        (DT)	*/
	u_char	multiplier;	/* Clock multiplier (1,2,4)	*/
	u_char	clock_divn;	/* Number of clock divisors	*/
	u32	clock_khz;	/* SCSI clock frequency in KHz	*/
	u32	pciclk_khz;	/* Estimated PCI clock  in KHz	*/
	/*
	 *  Start queue management.
	 *  It is filled up by the host processor and accessed by the 
	 *  SCRIPTS processor in order to start SCSI commands.
	 */
	volatile		/* Prevent code optimizations	*/
	u32	*squeue;	/* Start queue virtual address	*/
	u32	squeue_ba;	/* Start queue BUS address	*/
	u_short	squeueput;	/* Next free slot of the queue	*/
	u_short	actccbs;	/* Number of allocated CCBs	*/

	/*
	 *  Command completion queue.
	 *  It is the same size as the start queue to avoid overflow.
	 */
	u_short	dqueueget;	/* Next position to scan	*/
	volatile		/* Prevent code optimizations	*/
	u32	*dqueue;	/* Completion (done) queue	*/
	u32	dqueue_ba;	/* Done queue BUS address	*/

	/*
	 *  Miscellaneous buffers accessed by the scripts-processor.
	 *  They shall be DWORD aligned, because they may be read or 
	 *  written with a script command.
	 */
	u_char		msgout[8];	/* Buffer for MESSAGE OUT 	*/
	u_char		msgin [8];	/* Buffer for MESSAGE IN	*/
	u32		lastmsg;	/* Last SCSI message sent	*/
	u32		scratch;	/* Scratch for SCSI receive	*/
					/* Also used for cache test 	*/
	/*
	 *  Miscellaneous configuration and status parameters.
	 */
	u_char		usrflags;	/* Miscellaneous user flags	*/
	u_char		scsi_mode;	/* Current SCSI BUS mode	*/
	u_char		verbose;	/* Verbosity for this controller*/

	/*
	 *  CCB lists and queue.
	 */
	struct sym_ccb **ccbh;			/* CCBs hashed by DSA value	*/
					/* CCB_HASH_SIZE lists of CCBs	*/
	SYM_QUEHEAD	free_ccbq;	/* Queue of available CCBs	*/
	SYM_QUEHEAD	busy_ccbq;	/* Queue of busy CCBs		*/

	/*
	 *  During error handling and/or recovery,
	 *  active CCBs that are to be completed with 
	 *  error or requeued are moved from the busy_ccbq
	 *  to the comp_ccbq prior to completion.
	 */
	SYM_QUEHEAD	comp_ccbq;

#ifdef SYM_OPT_HANDLE_DEVICE_QUEUEING
	SYM_QUEHEAD	dummy_ccbq;
#endif

	/*
	 *  IMMEDIATE ARBITRATION (IARB) control.
	 *
	 *  We keep track in 'last_cp' of the last CCB that has been 
	 *  queued to the SCRIPTS processor and clear 'last_cp' when 
	 *  this CCB completes. If last_cp is not zero at the moment 
	 *  we queue a new CCB, we set a flag in 'last_cp' that is 
	 *  used by the SCRIPTS as a hint for setting IARB.
	 *  We donnot set more than 'iarb_max' consecutive hints for 
	 *  IARB in order to leave devices a chance to reselect.
	 *  By the way, any non zero value of 'iarb_max' is unfair. :)
	 */
#ifdef SYM_CONF_IARB_SUPPORT
	u_short		iarb_max;	/* Max. # consecutive IARB hints*/
	u_short		iarb_count;	/* Actual # of these hints	*/
	struct sym_ccb *	last_cp;
#endif

	/*
	 *  Command abort handling.
	 *  We need to synchronize tightly with the SCRIPTS 
	 *  processor in order to handle things correctly.
	 */
	u_char		abrt_msg[4];	/* Message to send buffer	*/
	struct sym_tblmove abrt_tbl;	/* Table for the MOV of it 	*/
	struct sym_tblsel  abrt_sel;	/* Sync params for selection	*/
	u_char		istat_sem;	/* Tells the chip to stop (SEM)	*/

	/*
	 *  64 bit DMA handling.
	 */
#if	SYM_CONF_DMA_ADDRESSING_MODE != 0
	u_char	use_dac;		/* Use PCI DAC cycles		*/
#if	SYM_CONF_DMA_ADDRESSING_MODE == 2
	u_char	dmap_dirty;		/* Dma segments registers dirty	*/
	u32	dmap_bah[SYM_DMAP_SIZE];/* Segment registers map	*/
#endif
#endif
};

#if SYM_CONF_DMA_ADDRESSING_MODE == 0
#define use_dac(np)	0
#define set_dac(np)	do { } while (0)
#else
#define use_dac(np)	(np)->use_dac
#define set_dac(np)	(np)->use_dac = 1
#endif

#define HCB_BA(np, lbl)	(np->hcb_ba + offsetof(struct sym_hcb, lbl))


/*
 *  FIRMWARES (sym_fw.c)
 */
struct sym_fw * sym_find_firmware(struct sym_chip *chip);
void sym_fw_bind_script(struct sym_hcb *np, u32 *start, int len);

/*
 *  Driver methods called from O/S specific code.
 */
char *sym_driver_name(void);
void sym_print_xerr(struct scsi_cmnd *cmd, int x_status);
int sym_reset_scsi_bus(struct sym_hcb *np, int enab_int);
struct sym_chip *sym_lookup_chip_table(u_short device_id, u_char revision);
#ifdef SYM_OPT_HANDLE_DEVICE_QUEUEING
void sym_start_next_ccbs(struct sym_hcb *np, struct sym_lcb *lp, int maxn);
#else
void sym_put_start_queue(struct sym_hcb *np, struct sym_ccb *cp);
#endif
void sym_start_up(struct Scsi_Host *, int reason);
irqreturn_t sym_interrupt(struct Scsi_Host *);
int sym_clear_tasks(struct sym_hcb *np, int cam_status, int target, int lun, int task);
struct sym_ccb *sym_get_ccb(struct sym_hcb *np, struct scsi_cmnd *cmd, u_char tag_order);
void sym_free_ccb(struct sym_hcb *np, struct sym_ccb *cp);
struct sym_lcb *sym_alloc_lcb(struct sym_hcb *np, u_char tn, u_char ln);
int sym_queue_scsiio(struct sym_hcb *np, struct scsi_cmnd *csio, struct sym_ccb *cp);
int sym_abort_scsiio(struct sym_hcb *np, struct scsi_cmnd *ccb, int timed_out);
int sym_reset_scsi_target(struct sym_hcb *np, int target);
void sym_hcb_free(struct sym_hcb *np);
int sym_hcb_attach(struct Scsi_Host *shost, struct sym_fw *fw, struct sym_nvram *nvram);

/*
 *  Build a scatter/gather entry.
 *
 *  For 64 bit systems, we use the 8 upper bits of the size field 
 *  to provide bus address bits 32-39 to the SCRIPTS processor.
 *  This allows the 895A, 896, 1010 to address up to 1 TB of memory.
 */

#if   SYM_CONF_DMA_ADDRESSING_MODE == 0
#define DMA_DAC_MASK	DMA_32BIT_MASK
#define sym_build_sge(np, data, badd, len)	\
do {						\
	(data)->addr = cpu_to_scr(badd);	\
	(data)->size = cpu_to_scr(len);		\
} while (0)
#elif SYM_CONF_DMA_ADDRESSING_MODE == 1
#define DMA_DAC_MASK	DMA_BIT_MASK(40)
#define sym_build_sge(np, data, badd, len)				\
do {									\
	(data)->addr = cpu_to_scr(badd);				\
	(data)->size = cpu_to_scr((((badd) >> 8) & 0xff000000) + len);	\
} while (0)
#elif SYM_CONF_DMA_ADDRESSING_MODE == 2
#define DMA_DAC_MASK	DMA_BIT_MASK(64)
int sym_lookup_dmap(struct sym_hcb *np, u32 h, int s);
static inline void
sym_build_sge(struct sym_hcb *np, struct sym_tblmove *data, u64 badd, int len)
{
	u32 h = (badd>>32);
	int s = (h&SYM_DMAP_MASK);

	if (h != np->dmap_bah[s])
		goto bad;
good:
	(data)->addr = cpu_to_scr(badd);
	(data)->size = cpu_to_scr((s<<24) + len);
	return;
bad:
	s = sym_lookup_dmap(np, h, s);
	goto good;
}
#else
#error "Unsupported DMA addressing mode"
#endif

/*
 *  MEMORY ALLOCATOR.
 */

#define sym_get_mem_cluster()	\
	(void *) __get_free_pages(GFP_ATOMIC, SYM_MEM_PAGE_ORDER)
#define sym_free_mem_cluster(p)	\
	free_pages((unsigned long)p, SYM_MEM_PAGE_ORDER)

/*
 *  Link between free memory chunks of a given size.
 */
typedef struct sym_m_link {
	struct sym_m_link *next;
} *m_link_p;

/*
 *  Virtual to bus physical translation for a given cluster.
 *  Such a structure is only useful with DMA abstraction.
 */
typedef struct sym_m_vtob {	/* Virtual to Bus address translation */
	struct sym_m_vtob *next;
	void *vaddr;		/* Virtual address */
	dma_addr_t baddr;	/* Bus physical address */
} *m_vtob_p;

/* Hash this stuff a bit to speed up translations */
#define VTOB_HASH_SHIFT		5
#define VTOB_HASH_SIZE		(1UL << VTOB_HASH_SHIFT)
#define VTOB_HASH_MASK		(VTOB_HASH_SIZE-1)
#define VTOB_HASH_CODE(m)	\
	((((unsigned long)(m)) >> SYM_MEM_CLUSTER_SHIFT) & VTOB_HASH_MASK)

/*
 *  Memory pool of a given kind.
 *  Ideally, we want to use:
 *  1) 1 pool for memory we donnot need to involve in DMA.
 *  2) The same pool for controllers that require same DMA 
 *     constraints and features.
 *     The OS specific m_pool_id_t thing and the sym_m_pool_match() 
 *     method are expected to tell the driver about.
 */
typedef struct sym_m_pool {
	m_pool_ident_t	dev_dmat;	/* Identifies the pool (see above) */
	void * (*get_mem_cluster)(struct sym_m_pool *);
#ifdef	SYM_MEM_FREE_UNUSED
	void (*free_mem_cluster)(struct sym_m_pool *, void *);
#endif
#define M_GET_MEM_CLUSTER()		mp->get_mem_cluster(mp)
#define M_FREE_MEM_CLUSTER(p)		mp->free_mem_cluster(mp, p)
	int nump;
	m_vtob_p vtob[VTOB_HASH_SIZE];
	struct sym_m_pool *next;
	struct sym_m_link h[SYM_MEM_CLUSTER_SHIFT - SYM_MEM_SHIFT + 1];
} *m_pool_p;

/*
 *  Alloc, free and translate addresses to bus physical 
 *  for DMAable memory.
 */
void *__sym_calloc_dma(m_pool_ident_t dev_dmat, int size, char *name);
void __sym_mfree_dma(m_pool_ident_t dev_dmat, void *m, int size, char *name);
dma_addr_t __vtobus(m_pool_ident_t dev_dmat, void *m);

/*
 * Verbs used by the driver code for DMAable memory handling.
 * The _uvptv_ macro avoids a nasty warning about pointer to volatile 
 * being discarded.
 */
#define _uvptv_(p) ((void *)((u_long)(p)))

#define _sym_calloc_dma(np, l, n)	__sym_calloc_dma(np->bus_dmat, l, n)
#define _sym_mfree_dma(np, p, l, n)	\
			__sym_mfree_dma(np->bus_dmat, _uvptv_(p), l, n)
#define sym_calloc_dma(l, n)		_sym_calloc_dma(np, l, n)
#define sym_mfree_dma(p, l, n)		_sym_mfree_dma(np, p, l, n)
#define vtobus(p)			__vtobus(np->bus_dmat, _uvptv_(p))

/*
 *  We have to provide the driver memory allocator with methods for 
 *  it to maintain virtual to bus physical address translations.
 */

#define sym_m_pool_match(mp_id1, mp_id2)	(mp_id1 == mp_id2)

static inline void *sym_m_get_dma_mem_cluster(m_pool_p mp, m_vtob_p vbp)
{
	void *vaddr = NULL;
	dma_addr_t baddr = 0;

	vaddr = dma_alloc_coherent(mp->dev_dmat, SYM_MEM_CLUSTER_SIZE, &baddr,
			GFP_ATOMIC);
	if (vaddr) {
		vbp->vaddr = vaddr;
		vbp->baddr = baddr;
	}
	return vaddr;
}

static inline void sym_m_free_dma_mem_cluster(m_pool_p mp, m_vtob_p vbp)
{
	dma_free_coherent(mp->dev_dmat, SYM_MEM_CLUSTER_SIZE, vbp->vaddr,
			vbp->baddr);
}

#endif /* SYM_HIPD_H */
