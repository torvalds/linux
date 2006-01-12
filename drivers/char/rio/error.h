
/****************************************************************************
 *******                                                              *******
 *******     E R R O R  H E A D E R   F I L E
 *******                                                              *******
 ****************************************************************************

 Author  : Ian Nandhra
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

#ifndef lint
/* static char *_rio_error_h_sccs = "@(#)error.h	1.3"; */
#endif

#define E_NO_ERROR                       ((ushort) 0)
#define E_PROCESS_NOT_INIT               ((ushort) 1)
#define E_LINK_TIMEOUT                   ((ushort) 2)
#define E_NO_ROUTE                       ((ushort) 3)
#define E_CONFUSED                       ((ushort) 4)
#define E_HOME                           ((ushort) 5)
#define E_CSUM_FAIL                      ((ushort) 6)
#define E_DISCONNECTED                   ((ushort) 7)
#define E_BAD_RUP                        ((ushort) 8)
#define E_NO_VIRGIN                      ((ushort) 9)
#define E_BOOT_RUP_BUSY                  ((ushort) 10)



    /*************************************************
     * Parsed to mem_halt()
     ************************************************/
#define E_CHANALLOC                      ((ushort) 0x80)
#define E_POLL_ALLOC                     ((ushort) 0x81)
#define E_LTTWAKE                        ((ushort) 0x82)
#define E_LTT_ALLOC                      ((ushort) 0x83)
#define E_LRT_ALLOC                      ((ushort) 0x84)
#define E_CIRRUS                         ((ushort) 0x85)
#define E_MONITOR                        ((ushort) 0x86)
#define E_PHB_ALLOC                      ((ushort) 0x87)
#define E_ARRAY_ALLOC                    ((ushort) 0x88)
#define E_QBUF_ALLOC                     ((ushort) 0x89)
#define E_PKT_ALLOC                      ((ushort) 0x8a)
#define E_GET_TX_Q_BUF                   ((ushort) 0x8b)
#define E_GET_RX_Q_BUF                   ((ushort) 0x8c)
#define E_MEM_OUT                        ((ushort) 0x8d)
#define E_MMU_INIT                       ((ushort) 0x8e)
#define E_LTT_INIT                       ((ushort) 0x8f)
#define E_LRT_INIT                       ((ushort) 0x90)
#define E_LINK_RUN                       ((ushort) 0x91)
#define E_MONITOR_ALLOC                  ((ushort) 0x92)
#define E_MONITOR_INIT                   ((ushort) 0x93)
#define E_POLL_INIT                      ((ushort) 0x94)


/*********** end of file ***********/
