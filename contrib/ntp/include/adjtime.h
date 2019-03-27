/*************************************************************************/
/* (c) Copyright Tai Jin, 1988.  All Rights Reserved.                    */
/*     Hewlett-Packard Laboratories.                                     */
/*                                                                       */
/* Permission is hereby granted for unlimited modification, use, and     */
/* distribution.  This software is made available with no warranty of    */
/* any kind, express or implied.  This copyright notice must remain      */
/* intact in all versions of this software.                              */
/*                                                                       */
/* The author would appreciate it if any bug fixes and enhancements were */
/* to be sent back to him for incorporation into future versions of this */
/* software.  Please send changes to tai@iag.hp.com or ken@sdd.hp.com.   */
/*************************************************************************/

/* "adjtime.h,v 3.1 1993/07/06 01:04:43 jbj Exp" */
/* adjtime.h,v
 * Revision 3.1  1993/07/06  01:04:43  jbj
 * NTP release 3.1
 *
 *
 * Revision 1.5  90/02/07  15:34:18  15:34:18  src (Source Hacker)
 * CHANGED KEY !!!
 * 
 * Revision 1.4  89/02/09  12:26:35  12:26:35  tai (Tai Jin (Guest))
 * *** empty log message ***
 * 
 * Revision 1.4  89/02/09  12:26:35  12:26:35  tai (Tai Jin)
 * added comment
 * 
 * Revision 1.3  88/08/30  01:08:29  01:08:29  tai (Tai Jin)
 * fix copyright notice again
 * 
 * Revision 1.2  88/08/30  00:51:55  00:51:55  tai (Tai Jin)
 * fix copyright notice
 * 
 * Revision 1.1  88/04/02  14:56:54  14:56:54  tai (Tai Jin)
 * Initial revision
 *  */

#include "ntp_types.h"

#define KEY	659847L

typedef union {
  struct msgbuf msgp;
  struct {
    long mtype;
    int code;
    struct timeval tv;
  } msgb;
} MsgBuf;

#define MSGSIZE	(sizeof(int) + sizeof(struct timeval))
/*
 * mtype values
 */
#define CLIENT	1L
#define SERVER	2L
/*
 * code values
 */
#define DELTA1	0
#define DELTA2	1
