/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*************************************************************************
 *
 *	   enables user programs to display messages and control encryption
 *	   on s390 tape devices
 *
 *	   Copyright IBM Corp. 2001, 2006
 *	   Author(s): Michael Holzheu <holzheu@de.ibm.com>
 *
 *************************************************************************/

#ifndef _TAPE390_H
#define _TAPE390_H

#define TAPE390_DISPLAY _IOW('d', 1, struct display_struct)

/*
 * The TAPE390_DISPLAY ioctl calls the Load Display command
 * which transfers 17 bytes of data from the channel to the subsystem:
 *     - 1 format control byte, and
 *     - two 8-byte messages
 *
 * Format control byte:
 *   0-2: New Message Overlay
 *     3: Alternate Messages
 *     4: Blink Message
 *     5: Display Low/High Message
 *     6: Reserved
 *     7: Automatic Load Request
 *
 */

typedef struct display_struct {
        char cntrl;
        char message1[8];
        char message2[8];
} display_struct;

#endif 
