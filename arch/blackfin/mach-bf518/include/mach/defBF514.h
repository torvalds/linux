/*
 * Copyright 2008-2009 Analog Devices Inc.
 *
 * Licensed under the Clear BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF514_H
#define _DEF_BF514_H

/* BF514 is BF512 + RSI */
#include "defBF512.h"

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
#define RSI_PID0                       0xFFC038D0 /* RSI Peripheral ID Register 0 */
#define RSI_PID1                       0xFFC038D4 /* RSI Peripheral ID Register 1 */
#define RSI_PID2                       0xFFC038D8 /* RSI Peripheral ID Register 2 */
#define RSI_PID3                       0xFFC038DC /* RSI Peripheral ID Register 3 */
#define RSI_PID4                       0xFFC038E0 /* RSI Peripheral ID Register 0 */
#define RSI_PID5                       0xFFC038E4 /* RSI Peripheral ID Register 1 */
#define RSI_PID6                       0xFFC038E8 /* RSI Peripheral ID Register 2 */
#define RSI_PID7                       0xFFC038EC /* RSI Peripheral ID Register 3 */

#endif /* _DEF_BF514_H */
