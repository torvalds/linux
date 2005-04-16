/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources. 
 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**	Module		: board.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:07
**	Retrieved	: 11/6/98 11:34:20
**
**  ident @(#)board.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef	__rio_board_h__
#define	__rio_board_h__

#ifdef SCCS_LABELS
static char *_board_h_sccs_ = "@(#)board.h	1.2";
#endif

/*
** board.h contains the definitions for the *hardware* of the host cards.
** It describes the memory overlay for the dual port RAM area.
*/

#define	DP_SRAM1_SIZE	0x7C00
#define	DP_SRAM2_SIZE	0x0200
#define	DP_SRAM3_SIZE	0x7000
#define	DP_SCRATCH_SIZE	0x1000
#define	DP_PARMMAP_ADDR	0x01FE	/* offset into SRAM2 */
#define	DP_STARTUP_ADDR	0x01F8	/* offset into SRAM2 */

/*
**	The shape of the Host Control area, at offset 0x7C00, Write Only
*/
struct s_Ctrl
{
	BYTE	DpCtl;				/* 7C00 */
	BYTE	Dp_Unused2_[127];
	BYTE	DpIntSet;			/* 7C80 */
	BYTE	Dp_Unused3_[127];
	BYTE	DpTpuReset;			/* 7D00 */
	BYTE	Dp_Unused4_[127];
	BYTE	DpIntReset;			/* 7D80 */
	BYTE	Dp_Unused5_[127];
};

/*
** The PROM data area on the host (0x7C00), Read Only
*/
struct s_Prom
{
	WORD	DpSlxCode[2];
	WORD	DpRev;
	WORD	Dp_Unused6_;
	WORD	DpUniq[4];
	WORD	DpJahre;
	WORD	DpWoche;
	WORD	DpHwFeature[5];
	WORD	DpOemId;
	WORD	DpSiggy[16];
};

/*
** Union of the Ctrl and Prom areas
*/
union u_CtrlProm	/* This is the control/PROM area (0x7C00) */
{
	struct s_Ctrl	DpCtrl;
	struct s_Prom	DpProm;
};

/*
** The top end of memory!
*/
struct s_ParmMapS		/* Area containing Parm Map Pointer */
{
	BYTE	Dp_Unused8_[DP_PARMMAP_ADDR];
	WORD	DpParmMapAd;
};

struct s_StartUpS
{
	BYTE    Dp_Unused9_[DP_STARTUP_ADDR];
	BYTE	Dp_LongJump[0x4];
	BYTE	Dp_Unused10_[2];
	BYTE	Dp_ShortJump[0x2];
};

union u_Sram2ParmMap	/* This is the top of memory (0x7E00-0x7FFF) */
{
	BYTE	DpSramMem[DP_SRAM2_SIZE];
	struct s_ParmMapS DpParmMapS;
	struct s_StartUpS DpStartUpS;
};

/*
**	This is the DP RAM overlay.
*/
struct DpRam
{
    BYTE 		 DpSram1[DP_SRAM1_SIZE];     /* 0000 - 7BFF */
    union u_CtrlProm     DpCtrlProm;                 /* 7C00 - 7DFF */
    union u_Sram2ParmMap DpSram2ParmMap;             /* 7E00 - 7FFF */
    BYTE		 DpScratch[DP_SCRATCH_SIZE]; /* 8000 - 8FFF */
    BYTE		 DpSram3[DP_SRAM3_SIZE];     /* 9000 - FFFF */
};

#define	DpControl	DpCtrlProm.DpCtrl.DpCtl
#define	DpSetInt	DpCtrlProm.DpCtrl.DpIntSet
#define	DpResetTpu	DpCtrlProm.DpCtrl.DpTpuReset
#define	DpResetInt	DpCtrlProm.DpCtrl.DpIntReset

#define	DpSlx		DpCtrlProm.DpProm.DpSlxCode
#define	DpRevision	DpCtrlProm.DpProm.DpRev
#define	DpUnique	DpCtrlProm.DpProm.DpUniq
#define	DpYear		DpCtrlProm.DpProm.DpJahre
#define	DpWeek		DpCtrlProm.DpProm.DpWoche
#define	DpSignature	DpCtrlProm.DpProm.DpSiggy

#define	DpParmMapR	DpSram2ParmMap.DpParmMapS.DpParmMapAd
#define	DpSram2		DpSram2ParmMap.DpSramMem

#endif
