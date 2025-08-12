/*
 *  linux/include/asm/traps.h
 *
 *  Copyright (C) 1993        Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _M68K_TRAPS_H
#define _M68K_TRAPS_H

#ifndef __ASSEMBLER__

#include <linux/linkage.h>
#include <asm/ptrace.h>

typedef void (*e_vector)(void);
extern e_vector vectors[];
extern e_vector *_ramvec;

asmlinkage void auto_inthandler(void);
asmlinkage void user_inthandler(void);
asmlinkage void bad_inthandler(void);

#endif

#define VEC_RESETSP (0)
#define VEC_RESETPC (1)
#define VEC_BUSERR  (2)
#define VEC_ADDRERR (3)
#define VEC_ILLEGAL (4)
#define VEC_ZERODIV (5)
#define VEC_CHK     (6)
#define VEC_TRAP    (7)
#define VEC_PRIV    (8)
#define VEC_TRACE   (9)
#define VEC_LINE10  (10)
#define VEC_LINE11  (11)
#define VEC_RESV12  (12)
#define VEC_COPROC  (13)
#define VEC_FORMAT  (14)
#define VEC_UNINT   (15)
#define VEC_RESV16  (16)
#define VEC_RESV17  (17)
#define VEC_RESV18  (18)
#define VEC_RESV19  (19)
#define VEC_RESV20  (20)
#define VEC_RESV21  (21)
#define VEC_RESV22  (22)
#define VEC_RESV23  (23)
#define VEC_SPUR    (24)
#define VEC_INT1    (25)
#define VEC_INT2    (26)
#define VEC_INT3    (27)
#define VEC_INT4    (28)
#define VEC_INT5    (29)
#define VEC_INT6    (30)
#define VEC_INT7    (31)
#define VEC_SYS     (32)
#define VEC_TRAP1   (33)
#define VEC_TRAP2   (34)
#define VEC_TRAP3   (35)
#define VEC_TRAP4   (36)
#define VEC_TRAP5   (37)
#define VEC_TRAP6   (38)
#define VEC_TRAP7   (39)
#define VEC_TRAP8   (40)
#define VEC_TRAP9   (41)
#define VEC_TRAP10  (42)
#define VEC_TRAP11  (43)
#define VEC_TRAP12  (44)
#define VEC_TRAP13  (45)
#define VEC_TRAP14  (46)
#define VEC_TRAP15  (47)
#define VEC_FPBRUC  (48)
#define VEC_FPIR    (49)
#define VEC_FPDIVZ  (50)
#define VEC_FPUNDER (51)
#define VEC_FPOE    (52)
#define VEC_FPOVER  (53)
#define VEC_FPNAN   (54)
#define VEC_FPUNSUP (55)
#define VEC_MMUCFG  (56)
#define VEC_MMUILL  (57)
#define VEC_MMUACC  (58)
#define VEC_RESV59  (59)
#define	VEC_UNIMPEA (60)
#define	VEC_UNIMPII (61)
#define VEC_RESV62  (62)
#define VEC_RESV63  (63)
#define VEC_USER    (64)

#define VECOFF(vec) ((vec)<<2)

#ifndef __ASSEMBLER__

/* Status register bits */
#define PS_T  (0x8000)
#define PS_S  (0x2000)
#define PS_M  (0x1000)
#define PS_C  (0x0001)

/* bits for 68020/68030 special status word */

#define FC    (0x8000)
#define FB    (0x4000)
#define RC    (0x2000)
#define RB    (0x1000)
#define DF    (0x0100)
#define RM    (0x0080)
#define RW    (0x0040)
#define SZ    (0x0030)
#define DFC   (0x0007)

/* bits for 68030 MMU status register (mmusr,psr) */

#define MMU_B	     (0x8000)    /* bus error */
#define MMU_L	     (0x4000)    /* limit violation */
#define MMU_S	     (0x2000)    /* supervisor violation */
#define MMU_WP	     (0x0800)    /* write-protected */
#define MMU_I	     (0x0400)    /* invalid descriptor */
#define MMU_M	     (0x0200)    /* ATC entry modified */
#define MMU_T	     (0x0040)    /* transparent translation */
#define MMU_NUM      (0x0007)    /* number of levels traversed */


/* bits for 68040 special status word */
#define CP_040	(0x8000)
#define CU_040	(0x4000)
#define CT_040	(0x2000)
#define CM_040	(0x1000)
#define MA_040	(0x0800)
#define ATC_040 (0x0400)
#define LK_040	(0x0200)
#define RW_040	(0x0100)
#define SIZ_040 (0x0060)
#define TT_040	(0x0018)
#define TM_040	(0x0007)

/* bits for 68040 write back status word */
#define WBV_040   (0x80)
#define WBSIZ_040 (0x60)
#define WBBYT_040 (0x20)
#define WBWRD_040 (0x40)
#define WBLNG_040 (0x00)
#define WBTT_040  (0x18)
#define WBTM_040  (0x07)

/* bus access size codes */
#define BA_SIZE_BYTE    (0x20)
#define BA_SIZE_WORD    (0x40)
#define BA_SIZE_LONG    (0x00)
#define BA_SIZE_LINE    (0x60)

/* bus access transfer type codes */
#define BA_TT_MOVE16    (0x08)

/* bits for 68040 MMU status register (mmusr) */
#define MMU_B_040   (0x0800)
#define MMU_G_040   (0x0400)
#define MMU_S_040   (0x0080)
#define MMU_CM_040  (0x0060)
#define MMU_M_040   (0x0010)
#define MMU_WP_040  (0x0004)
#define MMU_T_040   (0x0002)
#define MMU_R_040   (0x0001)

/* bits in the 68060 fault status long word (FSLW) */
#define	MMU060_MA	(0x08000000)	/* misaligned */
#define	MMU060_LK	(0x02000000)	/* locked transfer */
#define	MMU060_RW	(0x01800000)	/* read/write */
# define MMU060_RW_W	(0x00800000)	/* write */
# define MMU060_RW_R	(0x01000000)	/* read */
# define MMU060_RW_RMW	(0x01800000)	/* read/modify/write */
# define MMU060_W	(0x00800000)	/* general write, includes rmw */
#define	MMU060_SIZ	(0x00600000)	/* transfer size */
#define	MMU060_TT	(0x00180000)	/* transfer type (TT) bits */
#define	MMU060_TM	(0x00070000)	/* transfer modifier (TM) bits */
#define	MMU060_IO	(0x00008000)	/* instruction or operand */
#define	MMU060_PBE	(0x00004000)	/* push buffer bus error */
#define	MMU060_SBE	(0x00002000)	/* store buffer bus error */
#define	MMU060_PTA	(0x00001000)	/* pointer A fault */
#define	MMU060_PTB	(0x00000800)	/* pointer B fault */
#define	MMU060_IL	(0x00000400)	/* double indirect descr fault */
#define	MMU060_PF	(0x00000200)	/* page fault (invalid descr) */
#define	MMU060_SP	(0x00000100)	/* supervisor protection */
#define	MMU060_WP	(0x00000080)	/* write protection */
#define	MMU060_TWE	(0x00000040)	/* bus error on table search */
#define	MMU060_RE	(0x00000020)	/* bus error on read */
#define	MMU060_WE	(0x00000010)	/* bus error on write */
#define	MMU060_TTR	(0x00000008)	/* error caused by TTR translation */
#define	MMU060_BPE	(0x00000004)	/* branch prediction error */
#define	MMU060_SEE	(0x00000001)	/* software emulated error */

/* cases of missing or invalid descriptors */
#define MMU060_DESC_ERR (MMU060_PTA | MMU060_PTB | \
			 MMU060_IL  | MMU060_PF)
/* bits that indicate real errors */
#define MMU060_ERR_BITS (MMU060_PBE | MMU060_SBE | MMU060_DESC_ERR | MMU060_SP | \
			 MMU060_WP  | MMU060_TWE | MMU060_RE       | MMU060_WE)

/* structure for stack frames */

struct frame {
    struct pt_regs ptregs;
    union {
	    struct {
		    unsigned long  iaddr;    /* instruction address */
	    } fmt2;
	    struct {
		    unsigned long  effaddr;  /* effective address */
	    } fmt3;
	    struct {
		    unsigned long  effaddr;  /* effective address */
		    unsigned long  pc;	     /* pc of faulted instr */
	    } fmt4;
	    struct {
		    unsigned long  effaddr;  /* effective address */
		    unsigned short ssw;      /* special status word */
		    unsigned short wb3s;     /* write back 3 status */
		    unsigned short wb2s;     /* write back 2 status */
		    unsigned short wb1s;     /* write back 1 status */
		    unsigned long  faddr;    /* fault address */
		    unsigned long  wb3a;     /* write back 3 address */
		    unsigned long  wb3d;     /* write back 3 data */
		    unsigned long  wb2a;     /* write back 2 address */
		    unsigned long  wb2d;     /* write back 2 data */
		    unsigned long  wb1a;     /* write back 1 address */
		    unsigned long  wb1dpd0;  /* write back 1 data/push data 0*/
		    unsigned long  pd1;      /* push data 1*/
		    unsigned long  pd2;      /* push data 2*/
		    unsigned long  pd3;      /* push data 3*/
	    } fmt7;
	    struct {
		    unsigned long  iaddr;    /* instruction address */
		    unsigned short int1[4];  /* internal registers */
	    } fmt9;
	    struct {
		    unsigned short int1;
		    unsigned short ssw;      /* special status word */
		    unsigned short isc;      /* instruction stage c */
		    unsigned short isb;      /* instruction stage b */
		    unsigned long  daddr;    /* data cycle fault address */
		    unsigned short int2[2];
		    unsigned long  dobuf;    /* data cycle output buffer */
		    unsigned short int3[2];
	    } fmta;
	    struct {
		    unsigned short int1;
		    unsigned short ssw;     /* special status word */
		    unsigned short isc;     /* instruction stage c */
		    unsigned short isb;     /* instruction stage b */
		    unsigned long  daddr;   /* data cycle fault address */
		    unsigned short int2[2];
		    unsigned long  dobuf;   /* data cycle output buffer */
		    unsigned short int3[4];
		    unsigned long  baddr;   /* stage B address */
		    unsigned short int4[2];
		    unsigned long  dibuf;   /* data cycle input buffer */
		    unsigned short int5[3];
		    unsigned	   ver : 4; /* stack frame version # */
		    unsigned	   int6:12;
		    unsigned short int7[18];
	    } fmtb;
    } un;
};

#ifdef CONFIG_M68040
asmlinkage void berr_040cleanup(struct frame *fp);
#endif

#endif /* __ASSEMBLER__ */

#endif /* _M68K_TRAPS_H */
