/*****************************************************************************
* Copyright 2006 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#ifndef CFG_GLOBAL_DEFINES_H
#define CFG_GLOBAL_DEFINES_H

/* CHIP */
#define BCM1103 1

#define BCM1191 4
#define BCM2153 5
#define BCM2820 6

#define BCM2826 8
#define FPGA11107 9
#define BCM11107   10
#define BCM11109   11
#define BCM11170   12
#define BCM11110   13
#define BCM11211   14

/* CFG_GLOBAL_CHIP_FAMILY types */
#define CFG_GLOBAL_CHIP_FAMILY_NONE        0
#define CFG_GLOBAL_CHIP_FAMILY_BCM116X     2
#define CFG_GLOBAL_CHIP_FAMILY_BCMRING     4
#define CFG_GLOBAL_CHIP_FAMILY_BCM1103     8

#define IMAGE_HEADER_SIZE_CHECKSUM    4
#endif
#ifndef _CFG_GLOBAL_H_
#define _CFG_GLOBAL_H_

#define CFG_GLOBAL_CHIP                         BCM11107
#define CFG_GLOBAL_CHIP_FAMILY                  CFG_GLOBAL_CHIP_FAMILY_BCMRING
#define CFG_GLOBAL_CHIP_REV                     0xB0
#define CFG_GLOBAL_RAM_SIZE                     0x10000000
#define CFG_GLOBAL_RAM_BASE                     0x00000000
#define CFG_GLOBAL_RAM_RESERVED_SIZE            0x000000

#endif /* _CFG_GLOBAL_H_ */
