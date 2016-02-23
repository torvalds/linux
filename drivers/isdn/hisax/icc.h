/* $Id: icc.h,v 1.4.2.2 2004/01/12 22:52:26 keil Exp $
 *
 * ICC specific routines
 *
 * Author       Matt Henderson & Guy Ellis
 * Copyright    by Traverse Technologies Pty Ltd, www.travers.com.au
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * 1999.7.14 Initial implementation of routines for Siemens ISDN
 * Communication Controller PEB 2070 based on the ISAC routines
 * written by Karsten Keil.
 */

/* All Registers original Siemens Spec  */

#define ICC_MASK 0x20
#define ICC_ISTA 0x20
#define ICC_STAR 0x21
#define ICC_CMDR 0x21
#define ICC_EXIR 0x24
#define ICC_ADF2 0x39
#define ICC_SPCR 0x30
#define ICC_ADF1 0x38
#define ICC_CIR0 0x31
#define ICC_CIX0 0x31
#define ICC_CIR1 0x33
#define ICC_CIX1 0x33
#define ICC_STCR 0x37
#define ICC_MODE 0x22
#define ICC_RSTA 0x27
#define ICC_RBCL 0x25
#define ICC_RBCH 0x2A
#define ICC_TIMR 0x23
#define ICC_SQXR 0x3b
#define ICC_MOSR 0x3a
#define ICC_MOCR 0x3a
#define ICC_MOR0 0x32
#define ICC_MOX0 0x32
#define ICC_MOR1 0x34
#define ICC_MOX1 0x34

#define ICC_RBCH_XAC 0x80

#define ICC_CMD_TIM    0x0
#define ICC_CMD_RES    0x1
#define ICC_CMD_DU     0x3
#define ICC_CMD_EI1    0x4
#define ICC_CMD_SSP    0x5
#define ICC_CMD_DT     0x6
#define ICC_CMD_AR     0x8
#define ICC_CMD_ARL    0xA
#define ICC_CMD_AI     0xC
#define ICC_CMD_DI     0xF

#define ICC_IND_DR     0x0
#define ICC_IND_FJ     0x2
#define ICC_IND_EI1    0x4
#define ICC_IND_INT    0x6
#define ICC_IND_PU     0x7
#define ICC_IND_AR     0x8
#define ICC_IND_ARL    0xA
#define ICC_IND_AI     0xC
#define ICC_IND_AIL    0xE
#define ICC_IND_DC     0xF

extern void ICCVersion(struct IsdnCardState *cs, char *s);
extern void initicc(struct IsdnCardState *cs);
extern void icc_interrupt(struct IsdnCardState *cs, u_char val);
extern void clear_pending_icc_ints(struct IsdnCardState *cs);
extern void setup_icc(struct IsdnCardState *);
