/*
 * File:         include/asm-blackfin/mach-bf518/defBF514.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _DEF_BF514_H
#define _DEF_BF514_H

/* Include all Core registers and bit definitions */
#include <asm/def_LPBlackfin.h>

/* SYSTEM & MMR ADDRESS DEFINITIONS FOR ADSP-BF514 */

/* Include defBF51x_base.h for the set of #defines that are common to all ADSP-BF51x processors */
#include "defBF51x_base.h"

/* The following are the #defines needed by ADSP-BF514 that are not in the common header */

/* SDH Registers */

#define SDH_PWR_CTL                    0xFFC03900 /* SDH Power Control */
#define SDH_CLK_CTL                    0xFFC03904 /* SDH Clock Control */
#define SDH_ARGUMENT                   0xFFC03908 /* SDH Argument */
#define SDH_COMMAND                    0xFFC0390C /* SDH Command */
#define SDH_RESP_CMD                   0xFFC03910 /* SDH Response Command */
#define SDH_RESPONSE0                  0xFFC03914 /* SDH Response0 */
#define SDH_RESPONSE1                  0xFFC03918 /* SDH Response1 */
#define SDH_RESPONSE2                  0xFFC0391C /* SDH Response2 */
#define SDH_RESPONSE3                  0xFFC03920 /* SDH Response3 */
#define SDH_DATA_TIMER                 0xFFC03924 /* SDH Data Timer */
#define SDH_DATA_LGTH                  0xFFC03928 /* SDH Data Length */
#define SDH_DATA_CTL                   0xFFC0392C /* SDH Data Control */
#define SDH_DATA_CNT                   0xFFC03930 /* SDH Data Counter */
#define SDH_STATUS                     0xFFC03934 /* SDH Status */
#define SDH_STATUS_CLR                 0xFFC03938 /* SDH Status Clear */
#define SDH_MASK0                      0xFFC0393C /* SDH Interrupt0 Mask */
#define SDH_MASK1                      0xFFC03940 /* SDH Interrupt1 Mask */
#define SDH_FIFO_CNT                   0xFFC03948 /* SDH FIFO Counter */
#define SDH_FIFO                       0xFFC03980 /* SDH Data FIFO */
#define SDH_E_STATUS                   0xFFC039C0 /* SDH Exception Status */
#define SDH_E_MASK                     0xFFC039C4 /* SDH Exception Mask */
#define SDH_CFG                        0xFFC039C8 /* SDH Configuration */
#define SDH_RD_WAIT_EN                 0xFFC039CC /* SDH Read Wait Enable */
#define SDH_PID0                       0xFFC039D0 /* SDH Peripheral Identification0 */
#define SDH_PID1                       0xFFC039D4 /* SDH Peripheral Identification1 */
#define SDH_PID2                       0xFFC039D8 /* SDH Peripheral Identification2 */
#define SDH_PID3                       0xFFC039DC /* SDH Peripheral Identification3 */
#define SDH_PID4                       0xFFC039E0 /* SDH Peripheral Identification4 */
#define SDH_PID5                       0xFFC039E4 /* SDH Peripheral Identification5 */
#define SDH_PID6                       0xFFC039E8 /* SDH Peripheral Identification6 */
#define SDH_PID7                       0xFFC039EC /* SDH Peripheral Identification7 */

/* Removable Storage Interface Registers */

#define RSI_PWR_CONTROL                0xFFC03800 /* RSI Power Control Register */
#define RSI_CLK_CONTROL                0xFFC03804 /* RSI Clock Control Register */
#define RSI_ARGUMENT                   0xFFC03808 /* RSI Argument Register */
#define RSI_COMMAND                    0xFFC0380C /* RSI Command Register */
#define RSI_RESP_CMD                   0xFFC03810 /* RSI Response Command Register */
#define RSI_RESPONSE0                  0xFFC03814 /* RSI Response Register */
#define RSI_RESPONSE1                  0xFFC03818 /* RSI Response Register */
#define RSI_RESPONSE2                  0xFFC0381C /* RSI Response Register */
#define RSI_RESPONSE3                  0xFFC03820 /* RSI Response Register */
#define RSI_DATA_TIMER                 0xFFC03824 /* RSI Data Timer Register */
#define RSI_DATA_LGTH                  0xFFC03828 /* RSI Data Length Register */
#define RSI_DATA_CONTROL               0xFFC0382C /* RSI Data Control Register */
#define RSI_DATA_CNT                   0xFFC03830 /* RSI Data Counter Register */
#define RSI_STATUS                     0xFFC03834 /* RSI Status Register */
#define RSI_STATUSCL                   0xFFC03838 /* RSI Status Clear Register */
#define RSI_MASK0                      0xFFC0383C /* RSI Interrupt 0 Mask Register */
#define RSI_MASK1                      0xFFC03840 /* RSI Interrupt 1 Mask Register */
#define RSI_FIFO_CNT                   0xFFC03848 /* RSI FIFO Counter Register */
#define RSI_CEATA_CONTROL              0xFFC0384C /* RSI CEATA Register */
#define RSI_FIFO                       0xFFC03880 /* RSI Data FIFO Register */
#define RSI_ESTAT                      0xFFC038C0 /* RSI Exception Status Register */
#define RSI_EMASK                      0xFFC038C4 /* RSI Exception Mask Register */
#define RSI_CONFIG                     0xFFC038C8 /* RSI Configuration Register */
#define RSI_RD_WAIT_EN                 0xFFC038CC /* RSI Read Wait Enable Register */
#define RSI_PID0                       0xFFC03FE0 /* RSI Peripheral ID Register 0 */
#define RSI_PID1                       0xFFC03FE4 /* RSI Peripheral ID Register 1 */
#define RSI_PID2                       0xFFC03FE8 /* RSI Peripheral ID Register 2 */
#define RSI_PID3                       0xFFC03FEC /* RSI Peripheral ID Register 3 */
#define RSI_PID4                       0xFFC03FF0 /* RSI Peripheral ID Register 4 */
#define RSI_PID5                       0xFFC03FF4 /* RSI Peripheral ID Register 5 */
#define RSI_PID6                       0xFFC03FF8 /* RSI Peripheral ID Register 6 */
#define RSI_PID7                       0xFFC03FFC /* RSI Peripheral ID Register 7 */

#endif /* _DEF_BF514_H */
