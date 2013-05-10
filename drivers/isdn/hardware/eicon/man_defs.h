/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    1.9
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/* Definitions for use with the Management Information Element      */

/*------------------------------------------------------------------*/
/* Management information element                                   */
/* ----------------------------------------------------------       */
/* Byte     Coding            Comment                               */
/* ----------------------------------------------------------       */
/*    0 | 0 1 1 1 1 1 1 1 | ESC                                     */
/*    1 | 0 x x x x x x x | Length of information element (m-1)     */
/*    2 | 1 0 0 0 0 0 0 0 | Management Information Id               */
/*    3 | x x x x x x x x | Type                                    */
/*    4 | x x x x x x x x | Attribute                               */
/*    5 | x x x x x x x x | Status                                  */
/*    6 | x x x x x x x x | Variable Value Length (m-n)             */
/*    7 | x x x x x x x x | Path / Variable Name String Length (n-8)*/
/* 8..n | x x x x x x x x | Path/Node Name String separated by '\'  */
/* n..m | x x x x x x x x | Variable content                        */
/*------------------------------------------------------------------*/

/*------------------------------------------------------------------*/
/* Type Field                                                       */
/*                                                                  */
/* MAN_READ:      not used                                          */
/* MAN_WRITE:     not used                                          */
/* MAN_EVENT_ON:  not used                                          */
/* MAN_EVENT_OFF: not used                                          */
/* MAN_INFO_IND:  type of variable                                  */
/* MAN_EVENT_IND: type of variable                                  */
/* MAN_TRACE_IND  not used                                          */
/*------------------------------------------------------------------*/
#define MI_DIR          0x01  /* Directory string (zero terminated) */
#define MI_EXECUTE      0x02  /* Executable function (has no value) */
#define MI_ASCIIZ       0x03  /* Zero terminated string             */
#define MI_ASCII        0x04  /* String, first byte is length       */
#define MI_NUMBER       0x05  /* Number string, first byte is length*/
#define MI_TRACE        0x06  /* Trace information, format see below*/

#define MI_FIXED_LENGTH 0x80  /* get length from MAN_INFO max_len   */
#define MI_INT          0x81  /* number to display as signed int    */
#define MI_UINT         0x82  /* number to display as unsigned int  */
#define MI_HINT         0x83  /* number to display in hex format    */
#define MI_HSTR         0x84  /* number to display as a hex string  */
#define MI_BOOLEAN      0x85  /* number to display as boolean       */
#define MI_IP_ADDRESS   0x86  /* number to display as IP address    */
#define MI_BITFLD       0x87  /* number to display as bit field     */
#define MI_SPID_STATE   0x88  /* state# of SPID initialisation      */

/*------------------------------------------------------------------*/
/* Attribute Field                                                  */
/*                                                                  */
/* MAN_READ:      not used                                          */
/* MAN_WRITE:     not used                                          */
/* MAN_EVENT_ON:  not used                                          */
/* MAN_EVENT_OFF: not used                                          */
/* MAN_INFO_IND:  set according to capabilities of that variable    */
/* MAN_EVENT_IND: not used                                          */
/* MAN_TRACE_IND  not used                                          */
/*------------------------------------------------------------------*/
#define MI_WRITE        0x01  /* Variable is writeable              */
#define MI_EVENT        0x02  /* Variable can indicate changes      */

/*------------------------------------------------------------------*/
/* Status Field                                                     */
/*                                                                  */
/* MAN_READ:      not used                                          */
/* MAN_WRITE:     not used                                          */
/* MAN_EVENT_ON:  not used                                          */
/* MAN_EVENT_OFF: not used                                          */
/* MAN_INFO_IND:  set according to the actual status                */
/* MAN_EVENT_IND: set according to the actual statu                 */
/* MAN_TRACE_IND  not used                                          */
/*------------------------------------------------------------------*/
#define MI_LOCKED       0x01  /* write protected by another instance*/
#define MI_EVENT_ON     0x02  /* Event logging switched on          */
#define MI_PROTECTED    0x04  /* write protected by this instance   */

/*------------------------------------------------------------------*/
/* Data Format used for MAN_TRACE_IND (no MI-element used)          */
/*------------------------------------------------------------------*/
typedef struct mi_xlog_hdr_s MI_XLOG_HDR;
struct mi_xlog_hdr_s
{
  unsigned long  time;   /* Timestamp in msec units                 */
  unsigned short size;   /* Size of data that follows               */
  unsigned short code;   /* code of trace event                     */
};                       /* unspecified data follows this header    */

/*------------------------------------------------------------------*/
/* Trace mask definitions for trace events except B channel and     */
/* debug trace events                                               */
/*------------------------------------------------------------------*/
#define TM_D_CHAN   0x0001  /* D-Channel        (D-.) Code 3,4      */
#define TM_L_LAYER  0x0002  /* Low Layer        (LL)  Code 6,7      */
#define TM_N_LAYER  0x0004  /* Network Layer    (N)   Code 14,15    */
#define TM_DL_ERR   0x0008  /* Data Link Error  (MDL) Code 9        */
#define TM_LAYER1   0x0010  /* Layer 1                Code 20       */
#define TM_C_COMM   0x0020  /* Call Comment     (SIG) Code 5,21,22  */
#define TM_M_DATA   0x0040  /* Modulation Data  (EYE) Code 23       */
#define TM_STRING   0x0080  /* Sting data             Code 24       */
#define TM_N_USED2  0x0100  /* not used                             */
#define TM_N_USED3  0x0200  /* not used                             */
#define TM_N_USED4  0x0400  /* not used                             */
#define TM_N_USED5  0x0800  /* not used                             */
#define TM_N_USED6  0x1000  /* not used                             */
#define TM_N_USED7  0x2000  /* not used                             */
#define TM_N_USED8  0x4000  /* not used                             */
#define TM_REST     0x8000  /* Codes 10,11,12,13,16,18,19,128,129   */

/*------ End of file -----------------------------------------------*/
