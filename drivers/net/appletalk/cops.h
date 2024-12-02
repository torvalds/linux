/* SPDX-License-Identifier: GPL-2.0 */
/*      cops.h: LocalTalk driver for Linux.
 *
 *      Authors:
 *      - Jay Schulist <jschlst@samba.org>
 */

#ifndef __LINUX_COPSLTALK_H
#define __LINUX_COPSLTALK_H

#ifdef __KERNEL__

/* Max LLAP size we will accept. */
#define MAX_LLAP_SIZE		603

/* Tangent */
#define TANG_CARD_STATUS        1
#define TANG_CLEAR_INT          1
#define TANG_RESET              3

#define TANG_TX_READY           1
#define TANG_RX_READY           2

/* Dayna */
#define DAYNA_CMD_DATA          0
#define DAYNA_CLEAR_INT         1
#define DAYNA_CARD_STATUS       2
#define DAYNA_INT_CARD          3
#define DAYNA_RESET             4

#define DAYNA_RX_READY          0
#define DAYNA_TX_READY          1
#define DAYNA_RX_REQUEST        3

/* Same on both card types */
#define COPS_CLEAR_INT  1

/* LAP response codes received from the cards. */
#define LAP_INIT        1       /* Init cmd */
#define LAP_INIT_RSP    2       /* Init response */
#define LAP_WRITE       3       /* Write cmd */
#define DATA_READ       4       /* Data read */
#define LAP_RESPONSE    4       /* Received ALAP frame response */
#define LAP_GETSTAT     5       /* Get LAP and HW status */
#define LAP_RSPSTAT     6       /* Status response */

#endif

/*
 *	Structure to hold the firmware information.
 */
struct ltfirmware
{
        unsigned int length;
        const unsigned char *data;
};

#define DAYNA 1
#define TANGENT 2

#endif
