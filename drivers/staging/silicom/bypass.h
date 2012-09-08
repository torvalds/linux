/******************************************************************************/
/*                                                                            */
/* Bypass Control utility, Copyright (c) 2005 Silicom                         */
/* All rights reserved.                                                       */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/*                                                                            */
/******************************************************************************/

#ifndef BYPASS_H
#define BYPASS_H

/* Bypass related */

#define SYNC_CMD_VAL               2	/* 10b */
#define SYNC_CMD_LEN               2

#define WR_CMD_VAL                 2	/* 10b */
#define WR_CMD_LEN                 2

#define RD_CMD_VAL                 1	/* 10b */
#define RD_CMD_LEN                 2

#define ADDR_CMD_LEN               4

#define WR_DATA_LEN                8
#define RD_DATA_LEN                8

#define PIC_SIGN_REG_ADDR          0x7
#define PIC_SIGN_VALUE         0xcd

#define STATUS_REG_ADDR           0
#define WDT_EN_MASK            0x01	/* BIT_0 */
#define CMND_EN_MASK           0x02	/* BIT_1 */
#define DIS_BYPASS_CAP_MASK    0x04	/* BIT_2    Bypass Cap is disable*/
#define DFLT_PWRON_MASK        0x08	/* BIT_3 */
#define BYPASS_OFF_MASK        0x10	/* BIT_4 */ 
#define BYPASS_FLAG_MASK       0x20	/* BIT_5 */
#define STD_NIC_MASK           (DIS_BYPASS_CAP_MASK | BYPASS_OFF_MASK | DFLT_PWRON_MASK)
#define WD_EXP_FLAG_MASK       0x40	/* BIT_6 */
#define DFLT_PWROFF_MASK       0x80	/* BIT_7 */
#define STD_NIC_PWOFF_MASK     (DIS_BYPASS_CAP_MASK | BYPASS_OFF_MASK | DFLT_PWRON_MASK | DFLT_PWROFF_MASK)

#define PRODUCT_CAP_REG_ADDR   0x5
#define BYPASS_SUPPORT_MASK    0x01	/* BIT_0 */
#define TAP_SUPPORT_MASK       0x02	/* BIT_1 */
#define NORMAL_UNSUPPORT_MASK  0x04	/* BIT_2 */
#define DISC_SUPPORT_MASK      0x08	/* BIT_3 */
#define TPL2_SUPPORT_MASK      0x10	/* BIT_4 */
#define DISC_PORT_SUPPORT_MASK 0x20	/* BIT_5 */

#define STATUS_TAP_REG_ADDR    0x6
#define WDTE_TAP_BPN_MASK      0x01	/* BIT_1    1 when wdt expired -> TAP, 0 - Bypass */
#define DIS_TAP_CAP_MASK       0x04	/* BIT_2    TAP Cap is disable*/
#define DFLT_PWRON_TAP_MASK    0x08	/* BIT_3    */
#define TAP_OFF_MASK           0x10	/* BIT_4    */
#define TAP_FLAG_MASK          0x20	/* BIT_5    */
#define TX_DISA_MASK            0x40
#define TX_DISB_MASK            0x80

#define STD_NIC_TAP_MASK       (DIS_TAP_CAP_MASK | TAP_OFF_MASK | DFLT_PWRON_TAP_MASK)

#define STATUS_DISC_REG_ADDR    13
#define WDTE_DISC_BPN_MASK      0x01	/* BIT_0    1 when wdt expired -> TAP, 0 - Bypass */
#define STD_NIC_ON_MASK         0x02	/* BIT_1    */ 
#define DIS_DISC_CAP_MASK       0x04	/* BIT_2    TAP Cap is disable*/
#define DFLT_PWRON_DISC_MASK    0x08	/* BIT_3    */
#define DISC_OFF_MASK           0x10	/* BIT_4    */
#define DISC_FLAG_MASK          0x20	/* BIT_5    */
#define TPL2_FLAG_MASK          0x40	/* BIT_6    */
#define STD_NIC_DISC_MASK       DIS_DISC_CAP_MASK

#define CONT_CONFIG_REG_ADDR    12
#define EN_HW_RESET_MASK       0x2	/* BIT_1 */
#define WAIT_AT_PWUP_MASK      0x1	/* BIT_0 */

#define VER_REG_ADDR               0x1
#define BP_FW_VER_A0         0xa0
#define BP_FW_VER_A1         0xa1

#define INT_VER_MASK           0xf0
#define EXT_VER_MASK           0xf
/* */
#define PXG2BPI_VER            0x0
#define PXG2TBPI_VER           0x1
#define PXE2TBPI_VER           0x2
#define PXG4BPFI_VER           0x4
#define BP_FW_EXT_VER7         0x6
#define BP_FW_EXT_VER8         0x8
#define BP_FW_EXT_VER9         0x9

#define OLD_IF_VER              -1

#define CMND_REG_ADDR              10	/* 1010b */
#define WDT_REG_ADDR               4
#define TMRL_REG_ADDR              2
#define TMRH_REG_ADDR              3

/* NEW_FW */
#define WDT_INTERVAL               1	/* 5     //8   */
#define WDT_CMND_INTERVAL          200	/* 50          */
#define CMND_INTERVAL              200	/* 100    usec */
#define PULSE_TIME                 100

/* OLD_FW */
#define INIT_CMND_INTERVAL         40
#define PULSE_INTERVAL             5
#define WDT_TIME_CNT               3

/* Intel Commands */

#define CMND_OFF_INT               0xf
#define PWROFF_BYPASS_ON_INT       0x5
#define BYPASS_ON_INT              0x6
#define DIS_BYPASS_CAP_INT         0x4
#define RESET_WDT_INT              0x1

/* Intel timing */

#define BYPASS_DELAY_INT           4	/* msec */
#define CMND_INTERVAL_INT          2	/* msec */

/* Silicom Commands */
#define CMND_ON                    0x4
#define CMND_OFF                   0x2
#define BYPASS_ON                  0xa
#define BYPASS_OFF                 0x8
#define PORT_LINK_EN               0xe
#define PORT_LINK_DIS              0xc
#define WDT_ON                     0x10	/* 0x1f (11111) - max */
#define TIMEOUT_UNIT           100
#define TIMEOUT_MAX_STEP       15
#define WDT_TIMEOUT_MIN        100	/*  msec */
#define WDT_TIMEOUT_MAX        3276800	/*  msec */
#define WDT_AUTO_MIN_INT           500
#define WDT_TIMEOUT_DEF        WDT_TIMEOUT_MIN
#define WDT_OFF                    0x6
#define WDT_RELOAD                 0x9
#define RESET_CONT                 0x20
#define DIS_BYPASS_CAP             0x22
#define EN_BYPASS_CAP              0x24
#define BYPASS_STATE_PWRON         0x26
#define NORMAL_STATE_PWRON         0x28
#define BYPASS_STATE_PWROFF        0x27
#define NORMAL_STATE_PWROFF        0x29
#define TAP_ON                     0xb
#define TAP_OFF                    0x9
#define TAP_STATE_PWRON            0x2a
#define DIS_TAP_CAP                0x2c
#define EN_TAP_CAP                 0x2e
#define STD_NIC_OFF       0x86
#define STD_NIC_ON       0x84
#define DISC_ON           0x85
#define DISC_OFF          0x8a
#define DISC_STATE_PWRON  0x87
#define DIS_DISC_CAP      0x88
#define EN_DISC_CAP       0x89
#define TPL2_ON                    0x8c
#define TPL2_OFF                   0x8b
#define BP_WAIT_AT_PWUP_EN        0x80
#define BP_WAIT_AT_PWUP_DIS       0x81
#define BP_HW_RESET_EN             0x82
#define BP_HW_RESET_DIS            0x83

#define TX_DISA                0x8d
#define TX_DISB                0x8e
#define TX_ENA                 0xA0
#define TX_ENB                 0xA1

#define TX_DISA_PWRUP          0xA2
#define TX_DISB_PWRUP          0xA3
#define TX_ENA_PWRUP           0xA4
#define TX_ENB_PWRUP           0xA5

#define BYPASS_CAP_DELAY           21	/* msec */
#define DFLT_PWRON_DELAY           10	/* msec */
#define LATCH_DELAY                13	/* msec */
#define EEPROM_WR_DELAY             8	/* msec */

#define BP_LINK_MON_DELAY          4	/* sec */

#define BP_FW_EXT_VER0                 0xa0
#define BP_FW_EXT_VER1                 0xa1
#define BP_FW_EXT_VER2                0xb1

#define BP_OK        0
#define BP_NOT_CAP  -1
#define WDT_STATUS_EXP -2
#define WDT_STATUS_UNKNOWN -1
#define WDT_STATUS_EN 1
#define WDT_STATUS_DIS 0

#ifdef BP_SELF_TEST
#define ETH_P_BPTEST 0xabba

#define BPTEST_DATA_LEN 60
#endif

#endif				/* BYPASS_H */
