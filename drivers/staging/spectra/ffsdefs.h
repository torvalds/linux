/*
 * NAND Flash Controller Device Driver
 * Copyright (c) 2009, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef _FFSDEFS_
#define _FFSDEFS_

#define CLEAR 0			/*use this to clear a field instead of "fail"*/
#define SET   1			/*use this to set a field instead of "pass"*/
#define FAIL 1			/*failed flag*/
#define PASS 0			/*success flag*/
#define ERR -1			/*error flag*/

#define   ERASE_CMD             10
#define   WRITE_MAIN_CMD        11
#define   READ_MAIN_CMD         12
#define   WRITE_SPARE_CMD       13
#define   READ_SPARE_CMD        14
#define   WRITE_MAIN_SPARE_CMD  15
#define   READ_MAIN_SPARE_CMD   16
#define   MEMCOPY_CMD           17
#define   DUMMY_CMD             99

#define     EVENT_PASS                                  0x00
#define     EVENT_CORRECTABLE_DATA_ERROR_FIXED         0x01
#define     EVENT_UNCORRECTABLE_DATA_ERROR              0x02
#define     EVENT_TIME_OUT                              0x03
#define     EVENT_PROGRAM_FAILURE                       0x04
#define     EVENT_ERASE_FAILURE                         0x05
#define     EVENT_MEMCOPY_FAILURE                       0x06
#define     EVENT_FAIL                                  0x07

#define     EVENT_NONE                                  0x22
#define     EVENT_DMA_CMD_COMP                          0x77
#define     EVENT_ECC_TRANSACTION_DONE                  0x88
#define     EVENT_DMA_CMD_FAIL                          0x99

#define CMD_PASS        0
#define CMD_FAIL        1
#define CMD_ABORT       2
#define CMD_NOT_DONE    3

#endif /* _FFSDEFS_ */
