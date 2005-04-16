

/****************************************************************************
 *******                                                              *******
 *******         F O R M   P A C K E T   H E A D E R   F I L E
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

#ifndef _formpkt_h
#define _formpkt_h 1

#ifndef lint
#ifdef SCCS
static char *_rio_formpkt_h_sccs = "@(#)formpkt.h	1.1" ;
#endif
#endif

typedef struct FORM_BOOT_PKT_1 FORM_BOOT_PKT_1 ;
struct FORM_BOOT_PKT_1 {
                           ushort pkt_number ;
                           ushort pkt_total ;
                           ushort boot_top ;
                       } ;

typedef struct FORM_BOOT_PKT_2 FORM_BOOT_PKT_2 ;
struct FORM_BOOT_PKT_2 {
                           ushort pkt_number ;
                           char   boot_data[10] ;
                       } ;


typedef struct FORM_ATTACH_RTA   FORM_ATTACH_RTA ;
struct FORM_ATTACH_RTA  {
                       char    cmd_code ;
                       char    booter_serial[4] ;
                       char    booter_link ;
                       char    bootee_serial[4] ;
                       char    bootee_link ;
                   } ;


typedef struct FORM_BOOT_ID   FORM_BOOT_ID ;
struct FORM_BOOT_ID  {
                       char    cmd_code ;
                       char    bootee_serial[4] ;
                       char    bootee_prod_id ;
                       char    bootee_link ;
                   } ;



typedef struct FORM_ROUTE_1   FORM_ROUTE_1 ;
struct FORM_ROUTE_1 {
                        char     cmd_code ;
                        char     pkt_number ;
                        char     total_in_sequence ;
                        char     unit_id ;
                        char     host_unit_id ;
                    } ;

typedef struct FORM_ROUTE_2   FORM_ROUTE_2 ;
struct FORM_ROUTE_2 {
                        char   cmd_code ;
                        char   pkt_number ;
                        char   total_in_sequence ;
                        char   route_data[9] ;
                    } ;

typedef struct FORM_ROUTE_REQ   FORM_ROUTE_REQ ;
struct FORM_ROUTE_REQ {
                          char   cmd_code ;
                          char   pkt_number ;
                          char   total_in_sequence ;
                          char   route_data[10] ;
                      } ;


typedef struct FORM_ERROR   FORM_ERROR ;
struct FORM_ERROR {
                        char   cmd_code ;
                        char   error_code ;

                    } ;

typedef struct FORM_STATUS   FORM_STATUS ;
struct FORM_STATUS {
                        char   cmd_code ;
                        char   status_code ;
                        char   last_packet_valid ;
                        char   tx_buffer ;
                        char   rx_buffer ;
                        char   port_status ;
                        char   phb_status ;
                    } ;


typedef struct FORM_LINK_STATUS   FORM_LINK_STATUS ;
struct FORM_LINK_STATUS {
                        char    cmd_code ;
                        char    status_code ;
                        char    link_number ;
                        ushort  rx_errors ;
                        ushort  tx_errors ;
                        ushort  csum_errors ;
                        ushort  disconnects ;
                    } ;



typedef struct FORM_PARTITION FORM_PARTITION ;
struct FORM_PARTITION {
                        char    cmd_code ;
                        char    status_code ;
                        char    port_number ;
                        char    tx_max ;
                        char    rx_max ;
                        char    rx_limit ;
                      } ;


#endif

/*********** end of file ***********/

