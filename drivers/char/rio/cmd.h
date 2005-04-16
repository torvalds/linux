

/****************************************************************************
 *******                                                              *******
 *******           C O M M A N D   P A C K E T   H E A D E R S
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


#ifndef _cmd_h
#define _cmd_h

#ifndef lint
#ifdef SCCS
static char *_rio_cmd_h_sccs = "@(#)cmd.h	1.1" ;
#endif
#endif


#define PRE_EMPTIVE_CMD         0x80
#define INLINE_CMD              ~PRE_EMPTIVE_CMD

#define CMD_IGNORE_PKT          ( (ushort) 0)
#define CMD_STATUS_REQ          ( (ushort) 1)
#define CMD_UNIT_STATUS_REQ     ( (ushort) 2)     /* Is this needed ??? */
#define CMD_CONF_PORT           ( (ushort) 3)
#define CMD_CONF_UNIT           ( (ushort) 4)
#define CMD_ROUTE_MAP_REQ       ( (ushort) 5)
#define CMD_FLUSH_TX            ( (ushort) 6)
#define CMD_FLUSH_RX            ( (ushort) 7)
#define CMD_PARTION_PORT        ( (ushort) 8)
#define CMD_RESET_PORT          ( (ushort) 0x0a)
#define CMD_BOOT_UNIT           ( (ushort) 0x0b)
#define CMD_FOUND_UNIT          ( (ushort) 0x0c)
#define CMD_ATTACHED_RTA_2      ( (ushort) 0x0d)
#define CMD_PROVIDE_BOOT        ( (ushort) 0x0e)
#define CMD_CIRRUS              ( (ushort) 0x0f)

#define FORM_STATUS_PKT         ( (ushort) 1 )
#define FORM_POLL_PKT           ( (ushort) 2 )
#define FORM_LINK_STATUS_PKT    ( (ushort) 3 )


#define CMD_DATA_PORT           ( (ushort) 1 )
#define CMD_DATA                ( (ushort) 2 )

#define CMD_TX_PART             ( (ushort) 2 )
#define CMD_RX_PART             ( (ushort) 3 )
#define CMD_RX_LIMIT            ( (ushort) 4 )

#endif

/*********** end of file ***********/

