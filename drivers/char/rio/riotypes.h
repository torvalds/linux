/****************************************************************************
 *******                                                              *******
 *******                      R I O T Y P E S
 *******                                                              *******
 ****************************************************************************

 Author  : Jon Brawn
 Date    :

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

 Version : 0.01


                            Mods
 ----------------------------------------------------------------------------
  Date     By                Description
 ----------------------------------------------------------------------------

 ***************************************************************************/

#ifndef _riotypes_h
#define _riotypes_h 1

#ifdef SCCS_LABELS
#ifndef lint
/* static char *_rio_riotypes_h_sccs = "@(#)riotypes.h	1.10"; */
#endif
#endif

#ifdef INKERNEL

#if !defined(MIPSAT)
typedef unsigned short NUMBER_ptr;
typedef unsigned short WORD_ptr;
typedef unsigned short BYTE_ptr;
typedef unsigned short char_ptr;
typedef unsigned short Channel_ptr;
typedef unsigned short FREE_LIST_ptr_ptr;
typedef unsigned short FREE_LIST_ptr;
typedef unsigned short LPB_ptr;
typedef unsigned short Process_ptr;
typedef unsigned short PHB_ptr;
typedef unsigned short PKT_ptr;
typedef unsigned short PKT_ptr_ptr;
typedef unsigned short Q_BUF_ptr;
typedef unsigned short Q_BUF_ptr_ptr;
typedef unsigned short ROUTE_STR_ptr;
typedef unsigned short RUP_ptr;
typedef unsigned short short_ptr;
typedef unsigned short u_short_ptr;
typedef unsigned short ushort_ptr;
#else
/* MIPSAT types */
typedef char RIO_POINTER[8];
typedef RIO_POINTER NUMBER_ptr;
typedef RIO_POINTER WORD_ptr;
typedef RIO_POINTER BYTE_ptr;
typedef RIO_POINTER char_ptr;
typedef RIO_POINTER Channel_ptr;
typedef RIO_POINTER FREE_LIST_ptr_ptr;
typedef RIO_POINTER FREE_LIST_ptr;
typedef RIO_POINTER LPB_ptr;
typedef RIO_POINTER Process_ptr;
typedef RIO_POINTER PHB_ptr;
typedef RIO_POINTER PKT_ptr;
typedef RIO_POINTER PKT_ptr_ptr;
typedef RIO_POINTER Q_BUF_ptr;
typedef RIO_POINTER Q_BUF_ptr_ptr;
typedef RIO_POINTER ROUTE_STR_ptr;
typedef RIO_POINTER RUP_ptr;
typedef RIO_POINTER short_ptr;
typedef RIO_POINTER u_short_ptr;
typedef RIO_POINTER ushort_ptr;
#endif

#else /* not INKERNEL */
typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned long   DWORD;
typedef short           NUMBER;
typedef short           *NUMBER_ptr;
typedef unsigned short  *WORD_ptr;
typedef unsigned char   *BYTE_ptr;
typedef unsigned char   uchar ;
typedef unsigned short  ushort ;
typedef unsigned int    uint ;
typedef unsigned long   ulong ;
typedef unsigned char   u_char ;
typedef unsigned short  u_short ;
typedef unsigned int    u_int ;
typedef unsigned long   u_long ;
typedef unsigned short  ERROR ;
typedef unsigned long ID ;
typedef char             *char_ptr;
typedef Channel          *Channel_ptr;
typedef struct FREE_LIST *FREE_LIST_ptr;
typedef struct FREE_LIST **FREE_LIST_ptr_ptr;
typedef struct LPB       *LPB_ptr;
typedef struct Process   *Process_ptr;
typedef struct PHB       *PHB_ptr;
typedef struct PKT       *PKT_ptr;
typedef struct PKT       **PKT_ptr_ptr;
typedef struct Q_BUF     *Q_BUF_ptr;
typedef struct Q_BUF     **Q_BUF_ptr_ptr;
typedef struct ROUTE_STR *ROUTE_STR_ptr;
typedef struct RUP       *RUP_ptr;
typedef short            *short_ptr;
typedef u_short          *u_short_ptr;
typedef ushort           *ushort_ptr;
typedef struct PKT	 PKT;
typedef struct LPB	 LPB;
typedef struct RUP	 RUP;
#endif


#endif /* __riotypes__ */

/*********** end of file ***********/

