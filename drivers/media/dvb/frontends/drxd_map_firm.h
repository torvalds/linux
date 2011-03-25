/*
 * drx3973d_map_firm.h
 *
 * Copyright (C) 2006-2007 Micronas
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __DRX3973D_MAP__H__
#define __DRX3973D_MAP__H__

#define HI_SID                                                       0x10

#define HI_COMM_EXEC__A                                              0x400000
#define HI_COMM_EXEC__W                                              3
#define HI_COMM_EXEC__M                                              0x7
#define   HI_COMM_EXEC_CTL__B                                        0
#define   HI_COMM_EXEC_CTL__W                                        3
#define   HI_COMM_EXEC_CTL__M                                        0x7
#define     HI_COMM_EXEC_CTL_STOP                                    0x0
#define     HI_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     HI_COMM_EXEC_CTL_HOLD                                    0x2
#define     HI_COMM_EXEC_CTL_STEP                                    0x3
#define     HI_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     HI_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define HI_COMM_STATE__A                                             0x400001
#define HI_COMM_STATE__W                                             16
#define HI_COMM_STATE__M                                             0xFFFF
#define HI_COMM_MB__A                                                0x400002
#define HI_COMM_MB__W                                                16
#define HI_COMM_MB__M                                                0xFFFF
#define HI_COMM_SERVICE0__A                                          0x400003
#define HI_COMM_SERVICE0__W                                          16
#define HI_COMM_SERVICE0__M                                          0xFFFF
#define HI_COMM_SERVICE1__A                                          0x400004
#define HI_COMM_SERVICE1__W                                          16
#define HI_COMM_SERVICE1__M                                          0xFFFF
#define HI_COMM_INT_STA__A                                           0x400007
#define HI_COMM_INT_STA__W                                           16
#define HI_COMM_INT_STA__M                                           0xFFFF
#define HI_COMM_INT_MSK__A                                           0x400008
#define HI_COMM_INT_MSK__W                                           16
#define HI_COMM_INT_MSK__M                                           0xFFFF

#define HI_CT_REG_COMM_EXEC__A                                       0x410000
#define HI_CT_REG_COMM_EXEC__W                                       3
#define HI_CT_REG_COMM_EXEC__M                                       0x7
#define   HI_CT_REG_COMM_EXEC_CTL__B                                 0
#define   HI_CT_REG_COMM_EXEC_CTL__W                                 3
#define   HI_CT_REG_COMM_EXEC_CTL__M                                 0x7
#define     HI_CT_REG_COMM_EXEC_CTL_STOP                             0x0
#define     HI_CT_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     HI_CT_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     HI_CT_REG_COMM_EXEC_CTL_STEP                             0x3

#define HI_CT_REG_COMM_STATE__A                                      0x410001
#define HI_CT_REG_COMM_STATE__W                                      10
#define HI_CT_REG_COMM_STATE__M                                      0x3FF
#define HI_CT_REG_COMM_SERVICE0__A                                   0x410003
#define HI_CT_REG_COMM_SERVICE0__W                                   16
#define HI_CT_REG_COMM_SERVICE0__M                                   0xFFFF
#define HI_CT_REG_COMM_SERVICE1__A                                   0x410004
#define HI_CT_REG_COMM_SERVICE1__W                                   16
#define HI_CT_REG_COMM_SERVICE1__M                                   0xFFFF
#define   HI_CT_REG_COMM_SERVICE1_HI__B                              0
#define   HI_CT_REG_COMM_SERVICE1_HI__W                              1
#define   HI_CT_REG_COMM_SERVICE1_HI__M                              0x1

#define HI_CT_REG_COMM_INT_STA__A                                    0x410007
#define HI_CT_REG_COMM_INT_STA__W                                    1
#define HI_CT_REG_COMM_INT_STA__M                                    0x1
#define   HI_CT_REG_COMM_INT_STA_REQUEST__B                          0
#define   HI_CT_REG_COMM_INT_STA_REQUEST__W                          1
#define   HI_CT_REG_COMM_INT_STA_REQUEST__M                          0x1

#define HI_CT_REG_COMM_INT_MSK__A                                    0x410008
#define HI_CT_REG_COMM_INT_MSK__W                                    1
#define HI_CT_REG_COMM_INT_MSK__M                                    0x1
#define   HI_CT_REG_COMM_INT_MSK_REQUEST__B                          0
#define   HI_CT_REG_COMM_INT_MSK_REQUEST__W                          1
#define   HI_CT_REG_COMM_INT_MSK_REQUEST__M                          0x1

#define HI_CT_REG_CTL_STK__AX                                        0x410010
#define HI_CT_REG_CTL_STK__XSZ                                       4
#define HI_CT_REG_CTL_STK__W                                         10
#define HI_CT_REG_CTL_STK__M                                         0x3FF

#define HI_CT_REG_CTL_BPT_IDX__A                                     0x41001F
#define HI_CT_REG_CTL_BPT_IDX__W                                     1
#define HI_CT_REG_CTL_BPT_IDX__M                                     0x1

#define HI_CT_REG_CTL_BPT__A                                         0x410020
#define HI_CT_REG_CTL_BPT__W                                         10
#define HI_CT_REG_CTL_BPT__M                                         0x3FF

#define HI_RA_RAM_SLV0_FLG_SMM__A                                    0x420010
#define HI_RA_RAM_SLV0_FLG_SMM__W                                    1
#define HI_RA_RAM_SLV0_FLG_SMM__M                                    0x1
#define   HI_RA_RAM_SLV0_FLG_SMM_MULTI                               0x0
#define   HI_RA_RAM_SLV0_FLG_SMM_SINGLE                              0x1

#define HI_RA_RAM_SLV0_DEV_ID__A                                     0x420011
#define HI_RA_RAM_SLV0_DEV_ID__W                                     7
#define HI_RA_RAM_SLV0_DEV_ID__M                                     0x7F

#define HI_RA_RAM_SLV0_FLG_CRC__A                                    0x420012
#define HI_RA_RAM_SLV0_FLG_CRC__W                                    1
#define HI_RA_RAM_SLV0_FLG_CRC__M                                    0x1
#define   HI_RA_RAM_SLV0_FLG_CRC_CONTINUE                            0x0
#define   HI_RA_RAM_SLV0_FLG_CRC_RESTART                             0x1

#define HI_RA_RAM_SLV0_FLG_ACC__A                                    0x420013
#define HI_RA_RAM_SLV0_FLG_ACC__W                                    3
#define HI_RA_RAM_SLV0_FLG_ACC__M                                    0x7
#define   HI_RA_RAM_SLV0_FLG_ACC_RWM__B                              0
#define   HI_RA_RAM_SLV0_FLG_ACC_RWM__W                              2
#define   HI_RA_RAM_SLV0_FLG_ACC_RWM__M                              0x3
#define     HI_RA_RAM_SLV0_FLG_ACC_RWM_NORMAL                        0x0
#define     HI_RA_RAM_SLV0_FLG_ACC_RWM_READ_WRITE                    0x3
#define   HI_RA_RAM_SLV0_FLG_ACC_BRC__B                              2
#define   HI_RA_RAM_SLV0_FLG_ACC_BRC__W                              1
#define   HI_RA_RAM_SLV0_FLG_ACC_BRC__M                              0x4
#define     HI_RA_RAM_SLV0_FLG_ACC_BRC_NORMAL                        0x0
#define     HI_RA_RAM_SLV0_FLG_ACC_BRC_BROADCAST                     0x4

#define HI_RA_RAM_SLV0_STATE__A                                      0x420014
#define HI_RA_RAM_SLV0_STATE__W                                      1
#define HI_RA_RAM_SLV0_STATE__M                                      0x1
#define   HI_RA_RAM_SLV0_STATE_ADDRESS                               0x0
#define   HI_RA_RAM_SLV0_STATE_DATA                                  0x1

#define HI_RA_RAM_SLV0_BLK_BNK__A                                    0x420015
#define HI_RA_RAM_SLV0_BLK_BNK__W                                    12
#define HI_RA_RAM_SLV0_BLK_BNK__M                                    0xFFF
#define   HI_RA_RAM_SLV0_BLK_BNK_BNK__B                              0
#define   HI_RA_RAM_SLV0_BLK_BNK_BNK__W                              6
#define   HI_RA_RAM_SLV0_BLK_BNK_BNK__M                              0x3F
#define   HI_RA_RAM_SLV0_BLK_BNK_BLK__B                              6
#define   HI_RA_RAM_SLV0_BLK_BNK_BLK__W                              6
#define   HI_RA_RAM_SLV0_BLK_BNK_BLK__M                              0xFC0

#define HI_RA_RAM_SLV0_ADDR__A                                       0x420016
#define HI_RA_RAM_SLV0_ADDR__W                                       16
#define HI_RA_RAM_SLV0_ADDR__M                                       0xFFFF

#define HI_RA_RAM_SLV0_CRC__A                                        0x420017
#define HI_RA_RAM_SLV0_CRC__W                                        16
#define HI_RA_RAM_SLV0_CRC__M                                        0xFFFF

#define HI_RA_RAM_SLV0_READBACK__A                                   0x420018
#define HI_RA_RAM_SLV0_READBACK__W                                   16
#define HI_RA_RAM_SLV0_READBACK__M                                   0xFFFF

#define HI_RA_RAM_SLV1_FLG_SMM__A                                    0x420020
#define HI_RA_RAM_SLV1_FLG_SMM__W                                    1
#define HI_RA_RAM_SLV1_FLG_SMM__M                                    0x1
#define   HI_RA_RAM_SLV1_FLG_SMM_MULTI                               0x0
#define   HI_RA_RAM_SLV1_FLG_SMM_SINGLE                              0x1

#define HI_RA_RAM_SLV1_DEV_ID__A                                     0x420021
#define HI_RA_RAM_SLV1_DEV_ID__W                                     7
#define HI_RA_RAM_SLV1_DEV_ID__M                                     0x7F

#define HI_RA_RAM_SLV1_FLG_CRC__A                                    0x420022
#define HI_RA_RAM_SLV1_FLG_CRC__W                                    1
#define HI_RA_RAM_SLV1_FLG_CRC__M                                    0x1
#define   HI_RA_RAM_SLV1_FLG_CRC_CONTINUE                            0x0
#define   HI_RA_RAM_SLV1_FLG_CRC_RESTART                             0x1

#define HI_RA_RAM_SLV1_FLG_ACC__A                                    0x420023
#define HI_RA_RAM_SLV1_FLG_ACC__W                                    3
#define HI_RA_RAM_SLV1_FLG_ACC__M                                    0x7
#define   HI_RA_RAM_SLV1_FLG_ACC_RWM__B                              0
#define   HI_RA_RAM_SLV1_FLG_ACC_RWM__W                              2
#define   HI_RA_RAM_SLV1_FLG_ACC_RWM__M                              0x3
#define     HI_RA_RAM_SLV1_FLG_ACC_RWM_NORMAL                        0x0
#define     HI_RA_RAM_SLV1_FLG_ACC_RWM_READ_WRITE                    0x3
#define   HI_RA_RAM_SLV1_FLG_ACC_BRC__B                              2
#define   HI_RA_RAM_SLV1_FLG_ACC_BRC__W                              1
#define   HI_RA_RAM_SLV1_FLG_ACC_BRC__M                              0x4
#define     HI_RA_RAM_SLV1_FLG_ACC_BRC_NORMAL                        0x0
#define     HI_RA_RAM_SLV1_FLG_ACC_BRC_BROADCAST                     0x4

#define HI_RA_RAM_SLV1_STATE__A                                      0x420024
#define HI_RA_RAM_SLV1_STATE__W                                      1
#define HI_RA_RAM_SLV1_STATE__M                                      0x1
#define   HI_RA_RAM_SLV1_STATE_ADDRESS                               0x0
#define   HI_RA_RAM_SLV1_STATE_DATA                                  0x1

#define HI_RA_RAM_SLV1_BLK_BNK__A                                    0x420025
#define HI_RA_RAM_SLV1_BLK_BNK__W                                    12
#define HI_RA_RAM_SLV1_BLK_BNK__M                                    0xFFF
#define   HI_RA_RAM_SLV1_BLK_BNK_BNK__B                              0
#define   HI_RA_RAM_SLV1_BLK_BNK_BNK__W                              6
#define   HI_RA_RAM_SLV1_BLK_BNK_BNK__M                              0x3F
#define   HI_RA_RAM_SLV1_BLK_BNK_BLK__B                              6
#define   HI_RA_RAM_SLV1_BLK_BNK_BLK__W                              6
#define   HI_RA_RAM_SLV1_BLK_BNK_BLK__M                              0xFC0

#define HI_RA_RAM_SLV1_ADDR__A                                       0x420026
#define HI_RA_RAM_SLV1_ADDR__W                                       16
#define HI_RA_RAM_SLV1_ADDR__M                                       0xFFFF

#define HI_RA_RAM_SLV1_CRC__A                                        0x420027
#define HI_RA_RAM_SLV1_CRC__W                                        16
#define HI_RA_RAM_SLV1_CRC__M                                        0xFFFF

#define HI_RA_RAM_SLV1_READBACK__A                                   0x420028
#define HI_RA_RAM_SLV1_READBACK__W                                   16
#define HI_RA_RAM_SLV1_READBACK__M                                   0xFFFF

#define HI_RA_RAM_SRV_SEM__A                                         0x420030
#define HI_RA_RAM_SRV_SEM__W                                         1
#define HI_RA_RAM_SRV_SEM__M                                         0x1
#define   HI_RA_RAM_SRV_SEM_FREE                                     0x0
#define   HI_RA_RAM_SRV_SEM_CLAIMED                                  0x1

#define HI_RA_RAM_SRV_RES__A                                         0x420031
#define HI_RA_RAM_SRV_RES__W                                         3
#define HI_RA_RAM_SRV_RES__M                                         0x7
#define   HI_RA_RAM_SRV_RES_OK                                       0x0
#define   HI_RA_RAM_SRV_RES_START_FOUND_OR_ERROR                     0x1
#define   HI_RA_RAM_SRV_RES_STOP_FOUND                               0x2
#define   HI_RA_RAM_SRV_RES_ARBITRATION_FAILED                       0x3
#define   HI_RA_RAM_SRV_RES_INTERNAL_ERROR                           0x4

#define HI_RA_RAM_SRV_CMD__A                                         0x420032
#define HI_RA_RAM_SRV_CMD__W                                         3
#define HI_RA_RAM_SRV_CMD__M                                         0x7
#define   HI_RA_RAM_SRV_CMD_NULL                                     0x0
#define   HI_RA_RAM_SRV_CMD_UIO                                      0x1
#define   HI_RA_RAM_SRV_CMD_RESET                                    0x2
#define   HI_RA_RAM_SRV_CMD_CONFIG                                   0x3
#define   HI_RA_RAM_SRV_CMD_COPY                                     0x4
#define   HI_RA_RAM_SRV_CMD_TRANSMIT                                 0x5
#define   HI_RA_RAM_SRV_CMD_EXECUTE                                  0x6

#define HI_RA_RAM_SRV_PAR__AX                                        0x420033
#define HI_RA_RAM_SRV_PAR__XSZ                                       5
#define HI_RA_RAM_SRV_PAR__W                                         16
#define HI_RA_RAM_SRV_PAR__M                                         0xFFFF

#define HI_RA_RAM_SRV_NOP_RES__A                                     0x420031
#define HI_RA_RAM_SRV_NOP_RES__W                                     3
#define HI_RA_RAM_SRV_NOP_RES__M                                     0x7
#define   HI_RA_RAM_SRV_NOP_RES_OK                                   0x0
#define   HI_RA_RAM_SRV_NOP_RES_INTERNAL_ERROR                       0x4

#define HI_RA_RAM_SRV_UIO_RES__A                                     0x420031
#define HI_RA_RAM_SRV_UIO_RES__W                                     3
#define HI_RA_RAM_SRV_UIO_RES__M                                     0x7
#define   HI_RA_RAM_SRV_UIO_RES_LO                                   0x0
#define   HI_RA_RAM_SRV_UIO_RES_HI                                   0x1

#define HI_RA_RAM_SRV_UIO_KEY__A                                     0x420033
#define HI_RA_RAM_SRV_UIO_KEY__W                                     16
#define HI_RA_RAM_SRV_UIO_KEY__M                                     0xFFFF
#define   HI_RA_RAM_SRV_UIO_KEY_ACT                                  0x3973

#define HI_RA_RAM_SRV_UIO_SEL__A                                     0x420034
#define HI_RA_RAM_SRV_UIO_SEL__W                                     2
#define HI_RA_RAM_SRV_UIO_SEL__M                                     0x3
#define   HI_RA_RAM_SRV_UIO_SEL_ASEL                                 0x0
#define   HI_RA_RAM_SRV_UIO_SEL_UIO                                  0x1

#define HI_RA_RAM_SRV_UIO_SET__A                                     0x420035
#define HI_RA_RAM_SRV_UIO_SET__W                                     2
#define HI_RA_RAM_SRV_UIO_SET__M                                     0x3
#define   HI_RA_RAM_SRV_UIO_SET_OUT__B                               0
#define   HI_RA_RAM_SRV_UIO_SET_OUT__W                               1
#define   HI_RA_RAM_SRV_UIO_SET_OUT__M                               0x1
#define     HI_RA_RAM_SRV_UIO_SET_OUT_LO                             0x0
#define     HI_RA_RAM_SRV_UIO_SET_OUT_HI                             0x1
#define   HI_RA_RAM_SRV_UIO_SET_DIR__B                               1
#define   HI_RA_RAM_SRV_UIO_SET_DIR__W                               1
#define   HI_RA_RAM_SRV_UIO_SET_DIR__M                               0x2
#define     HI_RA_RAM_SRV_UIO_SET_DIR_OUT                            0x0
#define     HI_RA_RAM_SRV_UIO_SET_DIR_IN                             0x2

#define HI_RA_RAM_SRV_RST_RES__A                                     0x420031
#define HI_RA_RAM_SRV_RST_RES__W                                     1
#define HI_RA_RAM_SRV_RST_RES__M                                     0x1
#define   HI_RA_RAM_SRV_RST_RES_OK                                   0x0
#define   HI_RA_RAM_SRV_RST_RES_ERROR                                0x1

#define HI_RA_RAM_SRV_RST_KEY__A                                     0x420033
#define HI_RA_RAM_SRV_RST_KEY__W                                     16
#define HI_RA_RAM_SRV_RST_KEY__M                                     0xFFFF
#define   HI_RA_RAM_SRV_RST_KEY_ACT                                  0x3973

#define HI_RA_RAM_SRV_CFG_RES__A                                     0x420031
#define HI_RA_RAM_SRV_CFG_RES__W                                     1
#define HI_RA_RAM_SRV_CFG_RES__M                                     0x1
#define   HI_RA_RAM_SRV_CFG_RES_OK                                   0x0
#define   HI_RA_RAM_SRV_CFG_RES_ERROR                                0x1

#define HI_RA_RAM_SRV_CFG_KEY__A                                     0x420033
#define HI_RA_RAM_SRV_CFG_KEY__W                                     16
#define HI_RA_RAM_SRV_CFG_KEY__M                                     0xFFFF
#define   HI_RA_RAM_SRV_CFG_KEY_ACT                                  0x3973

#define HI_RA_RAM_SRV_CFG_DIV__A                                     0x420034
#define HI_RA_RAM_SRV_CFG_DIV__W                                     5
#define HI_RA_RAM_SRV_CFG_DIV__M                                     0x1F

#define HI_RA_RAM_SRV_CFG_BDL__A                                     0x420035
#define HI_RA_RAM_SRV_CFG_BDL__W                                     6
#define HI_RA_RAM_SRV_CFG_BDL__M                                     0x3F

#define HI_RA_RAM_SRV_CFG_WUP__A                                     0x420036
#define HI_RA_RAM_SRV_CFG_WUP__W                                     8
#define HI_RA_RAM_SRV_CFG_WUP__M                                     0xFF

#define HI_RA_RAM_SRV_CFG_ACT__A                                     0x420037
#define HI_RA_RAM_SRV_CFG_ACT__W                                     4
#define HI_RA_RAM_SRV_CFG_ACT__M                                     0xF
#define   HI_RA_RAM_SRV_CFG_ACT_SLV0__B                              0
#define   HI_RA_RAM_SRV_CFG_ACT_SLV0__W                              1
#define   HI_RA_RAM_SRV_CFG_ACT_SLV0__M                              0x1
#define     HI_RA_RAM_SRV_CFG_ACT_SLV0_OFF                           0x0
#define     HI_RA_RAM_SRV_CFG_ACT_SLV0_ON                            0x1
#define   HI_RA_RAM_SRV_CFG_ACT_SLV1__B                              1
#define   HI_RA_RAM_SRV_CFG_ACT_SLV1__W                              1
#define   HI_RA_RAM_SRV_CFG_ACT_SLV1__M                              0x2
#define     HI_RA_RAM_SRV_CFG_ACT_SLV1_OFF                           0x0
#define     HI_RA_RAM_SRV_CFG_ACT_SLV1_ON                            0x2
#define   HI_RA_RAM_SRV_CFG_ACT_BRD__B                               2
#define   HI_RA_RAM_SRV_CFG_ACT_BRD__W                               1
#define   HI_RA_RAM_SRV_CFG_ACT_BRD__M                               0x4
#define     HI_RA_RAM_SRV_CFG_ACT_BRD_OFF                            0x0
#define     HI_RA_RAM_SRV_CFG_ACT_BRD_ON                             0x4
#define   HI_RA_RAM_SRV_CFG_ACT_PWD__B                               3
#define   HI_RA_RAM_SRV_CFG_ACT_PWD__W                               1
#define   HI_RA_RAM_SRV_CFG_ACT_PWD__M                               0x8
#define     HI_RA_RAM_SRV_CFG_ACT_PWD_NOP                            0x0
#define     HI_RA_RAM_SRV_CFG_ACT_PWD_EXE                            0x8

#define HI_RA_RAM_SRV_CPY_RES__A                                     0x420031
#define HI_RA_RAM_SRV_CPY_RES__W                                     1
#define HI_RA_RAM_SRV_CPY_RES__M                                     0x1
#define   HI_RA_RAM_SRV_CPY_RES_OK                                   0x0
#define   HI_RA_RAM_SRV_CPY_RES_ERROR                                0x1

#define HI_RA_RAM_SRV_CPY_SBB__A                                     0x420033
#define HI_RA_RAM_SRV_CPY_SBB__W                                     12
#define HI_RA_RAM_SRV_CPY_SBB__M                                     0xFFF
#define   HI_RA_RAM_SRV_CPY_SBB_BNK__B                               0
#define   HI_RA_RAM_SRV_CPY_SBB_BNK__W                               6
#define   HI_RA_RAM_SRV_CPY_SBB_BNK__M                               0x3F
#define   HI_RA_RAM_SRV_CPY_SBB_BLK__B                               6
#define   HI_RA_RAM_SRV_CPY_SBB_BLK__W                               6
#define   HI_RA_RAM_SRV_CPY_SBB_BLK__M                               0xFC0

#define HI_RA_RAM_SRV_CPY_SAD__A                                     0x420034
#define HI_RA_RAM_SRV_CPY_SAD__W                                     16
#define HI_RA_RAM_SRV_CPY_SAD__M                                     0xFFFF

#define HI_RA_RAM_SRV_CPY_LEN__A                                     0x420035
#define HI_RA_RAM_SRV_CPY_LEN__W                                     16
#define HI_RA_RAM_SRV_CPY_LEN__M                                     0xFFFF

#define HI_RA_RAM_SRV_CPY_DBB__A                                     0x420033
#define HI_RA_RAM_SRV_CPY_DBB__W                                     12
#define HI_RA_RAM_SRV_CPY_DBB__M                                     0xFFF
#define   HI_RA_RAM_SRV_CPY_DBB_BNK__B                               0
#define   HI_RA_RAM_SRV_CPY_DBB_BNK__W                               6
#define   HI_RA_RAM_SRV_CPY_DBB_BNK__M                               0x3F
#define   HI_RA_RAM_SRV_CPY_DBB_BLK__B                               6
#define   HI_RA_RAM_SRV_CPY_DBB_BLK__W                               6
#define   HI_RA_RAM_SRV_CPY_DBB_BLK__M                               0xFC0

#define HI_RA_RAM_SRV_CPY_DAD__A                                     0x420034
#define HI_RA_RAM_SRV_CPY_DAD__W                                     16
#define HI_RA_RAM_SRV_CPY_DAD__M                                     0xFFFF

#define HI_RA_RAM_SRV_TRM_RES__A                                     0x420031
#define HI_RA_RAM_SRV_TRM_RES__W                                     2
#define HI_RA_RAM_SRV_TRM_RES__M                                     0x3
#define   HI_RA_RAM_SRV_TRM_RES_OK                                   0x0
#define   HI_RA_RAM_SRV_TRM_RES_ERROR                                0x1
#define   HI_RA_RAM_SRV_TRM_RES_ARBITRATION_FAILED                   0x3

#define HI_RA_RAM_SRV_TRM_MST__A                                     0x420033
#define HI_RA_RAM_SRV_TRM_MST__W                                     12
#define HI_RA_RAM_SRV_TRM_MST__M                                     0xFFF

#define HI_RA_RAM_SRV_TRM_SEQ__A                                     0x420034
#define HI_RA_RAM_SRV_TRM_SEQ__W                                     7
#define HI_RA_RAM_SRV_TRM_SEQ__M                                     0x7F

#define HI_RA_RAM_SRV_TRM_TRM__A                                     0x420035
#define HI_RA_RAM_SRV_TRM_TRM__W                                     15
#define HI_RA_RAM_SRV_TRM_TRM__M                                     0x7FFF
#define   HI_RA_RAM_SRV_TRM_TRM_DAT__B                               0
#define   HI_RA_RAM_SRV_TRM_TRM_DAT__W                               8
#define   HI_RA_RAM_SRV_TRM_TRM_DAT__M                               0xFF

#define HI_RA_RAM_SRV_TRM_DBB__A                                     0x420033
#define HI_RA_RAM_SRV_TRM_DBB__W                                     12
#define HI_RA_RAM_SRV_TRM_DBB__M                                     0xFFF
#define   HI_RA_RAM_SRV_TRM_DBB_BNK__B                               0
#define   HI_RA_RAM_SRV_TRM_DBB_BNK__W                               6
#define   HI_RA_RAM_SRV_TRM_DBB_BNK__M                               0x3F
#define   HI_RA_RAM_SRV_TRM_DBB_BLK__B                               6
#define   HI_RA_RAM_SRV_TRM_DBB_BLK__W                               6
#define   HI_RA_RAM_SRV_TRM_DBB_BLK__M                               0xFC0

#define HI_RA_RAM_SRV_TRM_DAD__A                                     0x420034
#define HI_RA_RAM_SRV_TRM_DAD__W                                     16
#define HI_RA_RAM_SRV_TRM_DAD__M                                     0xFFFF

#define HI_RA_RAM_USR_BEGIN__A                                       0x420040
#define HI_RA_RAM_USR_BEGIN__W                                       16
#define HI_RA_RAM_USR_BEGIN__M                                       0xFFFF

#define HI_RA_RAM_USR_END__A                                         0x42007F
#define HI_RA_RAM_USR_END__W                                         16
#define HI_RA_RAM_USR_END__M                                         0xFFFF

#define HI_IF_RAM_TRP_BPT0__AX                                       0x430000
#define HI_IF_RAM_TRP_BPT0__XSZ                                      2
#define HI_IF_RAM_TRP_BPT0__W                                        12
#define HI_IF_RAM_TRP_BPT0__M                                        0xFFF

#define HI_IF_RAM_TRP_STKU__AX                                       0x430002
#define HI_IF_RAM_TRP_STKU__XSZ                                      2
#define HI_IF_RAM_TRP_STKU__W                                        12
#define HI_IF_RAM_TRP_STKU__M                                        0xFFF

#define HI_IF_RAM_USR_BEGIN__A                                       0x430200
#define HI_IF_RAM_USR_BEGIN__W                                       12
#define HI_IF_RAM_USR_BEGIN__M                                       0xFFF

#define HI_IF_RAM_USR_END__A                                         0x4303FF
#define HI_IF_RAM_USR_END__W                                         12
#define HI_IF_RAM_USR_END__M                                         0xFFF

#define SC_SID                                                       0x11

#define SC_COMM_EXEC__A                                              0x800000
#define SC_COMM_EXEC__W                                              3
#define SC_COMM_EXEC__M                                              0x7
#define   SC_COMM_EXEC_CTL__B                                        0
#define   SC_COMM_EXEC_CTL__W                                        3
#define   SC_COMM_EXEC_CTL__M                                        0x7
#define     SC_COMM_EXEC_CTL_STOP                                    0x0
#define     SC_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     SC_COMM_EXEC_CTL_HOLD                                    0x2
#define     SC_COMM_EXEC_CTL_STEP                                    0x3
#define     SC_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     SC_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define SC_COMM_STATE__A                                             0x800001
#define SC_COMM_STATE__W                                             16
#define SC_COMM_STATE__M                                             0xFFFF
#define SC_COMM_MB__A                                                0x800002
#define SC_COMM_MB__W                                                16
#define SC_COMM_MB__M                                                0xFFFF
#define SC_COMM_SERVICE0__A                                          0x800003
#define SC_COMM_SERVICE0__W                                          16
#define SC_COMM_SERVICE0__M                                          0xFFFF
#define SC_COMM_SERVICE1__A                                          0x800004
#define SC_COMM_SERVICE1__W                                          16
#define SC_COMM_SERVICE1__M                                          0xFFFF
#define SC_COMM_INT_STA__A                                           0x800007
#define SC_COMM_INT_STA__W                                           16
#define SC_COMM_INT_STA__M                                           0xFFFF
#define SC_COMM_INT_MSK__A                                           0x800008
#define SC_COMM_INT_MSK__W                                           16
#define SC_COMM_INT_MSK__M                                           0xFFFF

#define SC_CT_REG_COMM_EXEC__A                                       0x810000
#define SC_CT_REG_COMM_EXEC__W                                       3
#define SC_CT_REG_COMM_EXEC__M                                       0x7
#define   SC_CT_REG_COMM_EXEC_CTL__B                                 0
#define   SC_CT_REG_COMM_EXEC_CTL__W                                 3
#define   SC_CT_REG_COMM_EXEC_CTL__M                                 0x7
#define     SC_CT_REG_COMM_EXEC_CTL_STOP                             0x0
#define     SC_CT_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     SC_CT_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     SC_CT_REG_COMM_EXEC_CTL_STEP                             0x3

#define SC_CT_REG_COMM_STATE__A                                      0x810001
#define SC_CT_REG_COMM_STATE__W                                      10
#define SC_CT_REG_COMM_STATE__M                                      0x3FF
#define SC_CT_REG_COMM_SERVICE0__A                                   0x810003
#define SC_CT_REG_COMM_SERVICE0__W                                   16
#define SC_CT_REG_COMM_SERVICE0__M                                   0xFFFF
#define SC_CT_REG_COMM_SERVICE1__A                                   0x810004
#define SC_CT_REG_COMM_SERVICE1__W                                   16
#define SC_CT_REG_COMM_SERVICE1__M                                   0xFFFF
#define   SC_CT_REG_COMM_SERVICE1_SC__B                              1
#define   SC_CT_REG_COMM_SERVICE1_SC__W                              1
#define   SC_CT_REG_COMM_SERVICE1_SC__M                              0x2

#define SC_CT_REG_COMM_INT_STA__A                                    0x810007
#define SC_CT_REG_COMM_INT_STA__W                                    1
#define SC_CT_REG_COMM_INT_STA__M                                    0x1
#define   SC_CT_REG_COMM_INT_STA_REQUEST__B                          0
#define   SC_CT_REG_COMM_INT_STA_REQUEST__W                          1
#define   SC_CT_REG_COMM_INT_STA_REQUEST__M                          0x1

#define SC_CT_REG_COMM_INT_MSK__A                                    0x810008
#define SC_CT_REG_COMM_INT_MSK__W                                    1
#define SC_CT_REG_COMM_INT_MSK__M                                    0x1
#define   SC_CT_REG_COMM_INT_MSK_REQUEST__B                          0
#define   SC_CT_REG_COMM_INT_MSK_REQUEST__W                          1
#define   SC_CT_REG_COMM_INT_MSK_REQUEST__M                          0x1

#define SC_CT_REG_CTL_STK__AX                                        0x810010
#define SC_CT_REG_CTL_STK__XSZ                                       4
#define SC_CT_REG_CTL_STK__W                                         10
#define SC_CT_REG_CTL_STK__M                                         0x3FF

#define SC_CT_REG_CTL_BPT_IDX__A                                     0x81001F
#define SC_CT_REG_CTL_BPT_IDX__W                                     1
#define SC_CT_REG_CTL_BPT_IDX__M                                     0x1

#define SC_CT_REG_CTL_BPT__A                                         0x810020
#define SC_CT_REG_CTL_BPT__W                                         10
#define SC_CT_REG_CTL_BPT__M                                         0x3FF

#define SC_RA_RAM_PARAM0__A                                          0x820040
#define SC_RA_RAM_PARAM0__W                                          16
#define SC_RA_RAM_PARAM0__M                                          0xFFFF
#define SC_RA_RAM_PARAM1__A                                          0x820041
#define SC_RA_RAM_PARAM1__W                                          16
#define SC_RA_RAM_PARAM1__M                                          0xFFFF
#define SC_RA_RAM_CMD_ADDR__A                                        0x820042
#define SC_RA_RAM_CMD_ADDR__W                                        16
#define SC_RA_RAM_CMD_ADDR__M                                        0xFFFF
#define SC_RA_RAM_CMD__A                                             0x820043
#define SC_RA_RAM_CMD__W                                             16
#define SC_RA_RAM_CMD__M                                             0xFFFF
#define   SC_RA_RAM_CMD_NULL                                         0x0
#define   SC_RA_RAM_CMD_PROC_START                                   0x1
#define   SC_RA_RAM_CMD_PROC_TRIGGER                                 0x2
#define   SC_RA_RAM_CMD_SET_PREF_PARAM                               0x3
#define   SC_RA_RAM_CMD_PROGRAM_PARAM                                0x4
#define   SC_RA_RAM_CMD_GET_OP_PARAM                                 0x5
#define   SC_RA_RAM_CMD_USER_IO                                      0x6
#define   SC_RA_RAM_CMD_SET_TIMER                                    0x7
#define   SC_RA_RAM_CMD_SET_ECHO_TIMING                              0x8
#define   SC_RA_RAM_CMD_MAX                                          0x8
#define   SC_RA_RAM_CMDBLOCK__C                                      0x4

#define SC_RA_RAM_PROC_ACTIVATE__A                                   0x820044
#define SC_RA_RAM_PROC_ACTIVATE__W                                   16
#define SC_RA_RAM_PROC_ACTIVATE__M                                   0xFFFF
#define SC_RA_RAM_PROC_ACTIVATE__PRE                                 0xFFFF
#define SC_RA_RAM_PROC_TERMINATED__A                                 0x820045
#define SC_RA_RAM_PROC_TERMINATED__W                                 16
#define SC_RA_RAM_PROC_TERMINATED__M                                 0xFFFF
#define SC_RA_RAM_SW_EVENT__A                                        0x820046
#define SC_RA_RAM_SW_EVENT__W                                        14
#define SC_RA_RAM_SW_EVENT__M                                        0x3FFF
#define   SC_RA_RAM_SW_EVENT_RUN_NMASK__B                            0
#define   SC_RA_RAM_SW_EVENT_RUN_NMASK__W                            1
#define   SC_RA_RAM_SW_EVENT_RUN_NMASK__M                            0x1
#define   SC_RA_RAM_SW_EVENT_RUN__B                                  1
#define   SC_RA_RAM_SW_EVENT_RUN__W                                  1
#define   SC_RA_RAM_SW_EVENT_RUN__M                                  0x2
#define   SC_RA_RAM_SW_EVENT_TERMINATE__B                            2
#define   SC_RA_RAM_SW_EVENT_TERMINATE__W                            1
#define   SC_RA_RAM_SW_EVENT_TERMINATE__M                            0x4
#define   SC_RA_RAM_SW_EVENT_FT_START__B                             3
#define   SC_RA_RAM_SW_EVENT_FT_START__W                             1
#define   SC_RA_RAM_SW_EVENT_FT_START__M                             0x8
#define   SC_RA_RAM_SW_EVENT_FI_START__B                             4
#define   SC_RA_RAM_SW_EVENT_FI_START__W                             1
#define   SC_RA_RAM_SW_EVENT_FI_START__M                             0x10
#define   SC_RA_RAM_SW_EVENT_EQ_TPS__B                               5
#define   SC_RA_RAM_SW_EVENT_EQ_TPS__W                               1
#define   SC_RA_RAM_SW_EVENT_EQ_TPS__M                               0x20
#define   SC_RA_RAM_SW_EVENT_EQ_ERR__B                               6
#define   SC_RA_RAM_SW_EVENT_EQ_ERR__W                               1
#define   SC_RA_RAM_SW_EVENT_EQ_ERR__M                               0x40
#define   SC_RA_RAM_SW_EVENT_CE_IR__B                                7
#define   SC_RA_RAM_SW_EVENT_CE_IR__W                                1
#define   SC_RA_RAM_SW_EVENT_CE_IR__M                                0x80
#define   SC_RA_RAM_SW_EVENT_FE_FD__B                                8
#define   SC_RA_RAM_SW_EVENT_FE_FD__W                                1
#define   SC_RA_RAM_SW_EVENT_FE_FD__M                                0x100
#define   SC_RA_RAM_SW_EVENT_FE_CF__B                                9
#define   SC_RA_RAM_SW_EVENT_FE_CF__W                                1
#define   SC_RA_RAM_SW_EVENT_FE_CF__M                                0x200
#define   SC_RA_RAM_SW_EVENT_DEMOD_LOCK_FOUND__B                     10
#define   SC_RA_RAM_SW_EVENT_DEMOD_LOCK_FOUND__W                     1
#define   SC_RA_RAM_SW_EVENT_DEMOD_LOCK_FOUND__M                     0x400
#define   SC_RA_RAM_SW_EVENT_DEMOD_LOCK_LOST__B                      11
#define   SC_RA_RAM_SW_EVENT_DEMOD_LOCK_LOST__W                      1
#define   SC_RA_RAM_SW_EVENT_DEMOD_LOCK_LOST__M                      0x800

#define SC_RA_RAM_LOCKTRACK__A                                       0x820047
#define SC_RA_RAM_LOCKTRACK__W                                       16
#define SC_RA_RAM_LOCKTRACK__M                                       0xFFFF
#define   SC_RA_RAM_LOCKTRACK_NULL                                   0x0
#define   SC_RA_RAM_LOCKTRACK_MIN                                    0x1
#define   SC_RA_RAM_LOCKTRACK_RESET                                  0x1
#define   SC_RA_RAM_LOCKTRACK_MG_DETECT                              0x2
#define   SC_RA_RAM_LOCKTRACK_P_DETECT                               0x3
#define   SC_RA_RAM_LOCKTRACK_P_DETECT_SEARCH                        0x4
#define   SC_RA_RAM_LOCKTRACK_P_DETECT_MIRROR                        0x5
#define   SC_RA_RAM_LOCKTRACK_LC                                     0x6
#define   SC_RA_RAM_LOCKTRACK_P_ECHO                                 0x7
#define   SC_RA_RAM_LOCKTRACK_NE_INIT                                0x8
#define   SC_RA_RAM_LOCKTRACK_TRACK_INIT                             0x9
#define   SC_RA_RAM_LOCKTRACK_TRACK                                  0xA
#define   SC_RA_RAM_LOCKTRACK_TRACK_ERROR                            0xB
#define   SC_RA_RAM_LOCKTRACK_SR_SCANNING                            0xC
#define   SC_RA_RAM_LOCKTRACK_MAX                                    0xD

#define SC_RA_RAM_OP_PARAM__A                                        0x820048
#define SC_RA_RAM_OP_PARAM__W                                        13
#define SC_RA_RAM_OP_PARAM__M                                        0x1FFF
#define   SC_RA_RAM_OP_PARAM_MODE__B                                 0
#define   SC_RA_RAM_OP_PARAM_MODE__W                                 2
#define   SC_RA_RAM_OP_PARAM_MODE__M                                 0x3
#define     SC_RA_RAM_OP_PARAM_MODE_2K                               0x0
#define     SC_RA_RAM_OP_PARAM_MODE_8K                               0x1
#define   SC_RA_RAM_OP_PARAM_GUARD__B                                2
#define   SC_RA_RAM_OP_PARAM_GUARD__W                                2
#define   SC_RA_RAM_OP_PARAM_GUARD__M                                0xC
#define     SC_RA_RAM_OP_PARAM_GUARD_32                              0x0
#define     SC_RA_RAM_OP_PARAM_GUARD_16                              0x4
#define     SC_RA_RAM_OP_PARAM_GUARD_8                               0x8
#define     SC_RA_RAM_OP_PARAM_GUARD_4                               0xC
#define   SC_RA_RAM_OP_PARAM_CONST__B                                4
#define   SC_RA_RAM_OP_PARAM_CONST__W                                2
#define   SC_RA_RAM_OP_PARAM_CONST__M                                0x30
#define     SC_RA_RAM_OP_PARAM_CONST_QPSK                            0x0
#define     SC_RA_RAM_OP_PARAM_CONST_QAM16                           0x10
#define     SC_RA_RAM_OP_PARAM_CONST_QAM64                           0x20
#define   SC_RA_RAM_OP_PARAM_HIER__B                                 6
#define   SC_RA_RAM_OP_PARAM_HIER__W                                 3
#define   SC_RA_RAM_OP_PARAM_HIER__M                                 0x1C0
#define     SC_RA_RAM_OP_PARAM_HIER_NO                               0x0
#define     SC_RA_RAM_OP_PARAM_HIER_A1                               0x40
#define     SC_RA_RAM_OP_PARAM_HIER_A2                               0x80
#define     SC_RA_RAM_OP_PARAM_HIER_A4                               0xC0
#define   SC_RA_RAM_OP_PARAM_RATE__B                                 9
#define   SC_RA_RAM_OP_PARAM_RATE__W                                 3
#define   SC_RA_RAM_OP_PARAM_RATE__M                                 0xE00
#define     SC_RA_RAM_OP_PARAM_RATE_1_2                              0x0
#define     SC_RA_RAM_OP_PARAM_RATE_2_3                              0x200
#define     SC_RA_RAM_OP_PARAM_RATE_3_4                              0x400
#define     SC_RA_RAM_OP_PARAM_RATE_5_6                              0x600
#define     SC_RA_RAM_OP_PARAM_RATE_7_8                              0x800
#define   SC_RA_RAM_OP_PARAM_PRIO__B                                 12
#define   SC_RA_RAM_OP_PARAM_PRIO__W                                 1
#define   SC_RA_RAM_OP_PARAM_PRIO__M                                 0x1000
#define     SC_RA_RAM_OP_PARAM_PRIO_HI                               0x0
#define     SC_RA_RAM_OP_PARAM_PRIO_LO                               0x1000

#define SC_RA_RAM_OP_AUTO__A                                         0x820049
#define SC_RA_RAM_OP_AUTO__W                                         6
#define SC_RA_RAM_OP_AUTO__M                                         0x3F
#define SC_RA_RAM_OP_AUTO__PRE                                       0x1F
#define   SC_RA_RAM_OP_AUTO_MODE__B                                  0
#define   SC_RA_RAM_OP_AUTO_MODE__W                                  1
#define   SC_RA_RAM_OP_AUTO_MODE__M                                  0x1
#define   SC_RA_RAM_OP_AUTO_GUARD__B                                 1
#define   SC_RA_RAM_OP_AUTO_GUARD__W                                 1
#define   SC_RA_RAM_OP_AUTO_GUARD__M                                 0x2
#define   SC_RA_RAM_OP_AUTO_CONST__B                                 2
#define   SC_RA_RAM_OP_AUTO_CONST__W                                 1
#define   SC_RA_RAM_OP_AUTO_CONST__M                                 0x4
#define   SC_RA_RAM_OP_AUTO_HIER__B                                  3
#define   SC_RA_RAM_OP_AUTO_HIER__W                                  1
#define   SC_RA_RAM_OP_AUTO_HIER__M                                  0x8
#define   SC_RA_RAM_OP_AUTO_RATE__B                                  4
#define   SC_RA_RAM_OP_AUTO_RATE__W                                  1
#define   SC_RA_RAM_OP_AUTO_RATE__M                                  0x10
#define   SC_RA_RAM_OP_AUTO_PRIO__B                                  5
#define   SC_RA_RAM_OP_AUTO_PRIO__W                                  1
#define   SC_RA_RAM_OP_AUTO_PRIO__M                                  0x20

#define SC_RA_RAM_PILOT_STATUS__A                                    0x82004A
#define SC_RA_RAM_PILOT_STATUS__W                                    16
#define SC_RA_RAM_PILOT_STATUS__M                                    0xFFFF
#define   SC_RA_RAM_PILOT_STATUS_OK                                  0x0
#define   SC_RA_RAM_PILOT_STATUS_SPD_ERROR                           0x1
#define   SC_RA_RAM_PILOT_STATUS_CPD_ERROR                           0x2

#define SC_RA_RAM_LOCK__A                                            0x82004B
#define SC_RA_RAM_LOCK__W                                            4
#define SC_RA_RAM_LOCK__M                                            0xF
#define   SC_RA_RAM_LOCK_DEMOD__B                                    0
#define   SC_RA_RAM_LOCK_DEMOD__W                                    1
#define   SC_RA_RAM_LOCK_DEMOD__M                                    0x1
#define   SC_RA_RAM_LOCK_FEC__B                                      1
#define   SC_RA_RAM_LOCK_FEC__W                                      1
#define   SC_RA_RAM_LOCK_FEC__M                                      0x2
#define   SC_RA_RAM_LOCK_MPEG__B                                     2
#define   SC_RA_RAM_LOCK_MPEG__W                                     1
#define   SC_RA_RAM_LOCK_MPEG__M                                     0x4
#define   SC_RA_RAM_LOCK_NODVBT__B                                   3
#define   SC_RA_RAM_LOCK_NODVBT__W                                   1
#define   SC_RA_RAM_LOCK_NODVBT__M                                   0x8

#define SC_RA_RAM_BE_OPT_ENA__A                                      0x82004C
#define SC_RA_RAM_BE_OPT_ENA__W                                      5
#define SC_RA_RAM_BE_OPT_ENA__M                                      0x1F
#define SC_RA_RAM_BE_OPT_ENA__PRE                                    0x14
#define   SC_RA_RAM_BE_OPT_ENA_MOTION                                0x0
#define   SC_RA_RAM_BE_OPT_ENA_CP_OPT                                0x1
#define   SC_RA_RAM_BE_OPT_ENA_COCHANNEL                             0x2
#define   SC_RA_RAM_BE_OPT_ENA_FR_WATCH                              0x4
#define   SC_RA_RAM_BE_OPT_ENA_MAX                                   0x5

#define SC_RA_RAM_BE_OPT_DELAY__A                                    0x82004D
#define SC_RA_RAM_BE_OPT_DELAY__W                                    16
#define SC_RA_RAM_BE_OPT_DELAY__M                                    0xFFFF
#define SC_RA_RAM_BE_OPT_DELAY__PRE                                  0x200
#define SC_RA_RAM_BE_OPT_INIT_DELAY__A                               0x82004E
#define SC_RA_RAM_BE_OPT_INIT_DELAY__W                               16
#define SC_RA_RAM_BE_OPT_INIT_DELAY__M                               0xFFFF
#define SC_RA_RAM_BE_OPT_INIT_DELAY__PRE                             0x400
#define SC_RA_RAM_ECHO_THRES__A                                      0x82004F
#define SC_RA_RAM_ECHO_THRES__W                                      16
#define SC_RA_RAM_ECHO_THRES__M                                      0xFFFF
#define SC_RA_RAM_ECHO_THRES__PRE                                    0x2A
#define SC_RA_RAM_CONFIG__A                                          0x820050
#define SC_RA_RAM_CONFIG__W                                          16
#define SC_RA_RAM_CONFIG__M                                          0xFFFF
#define SC_RA_RAM_CONFIG__PRE                                        0x54
#define   SC_RA_RAM_CONFIG_ID__B                                     0
#define   SC_RA_RAM_CONFIG_ID__W                                     1
#define   SC_RA_RAM_CONFIG_ID__M                                     0x1
#define     SC_RA_RAM_CONFIG_ID_PRO                                  0x0
#define     SC_RA_RAM_CONFIG_ID_CONSUMER                             0x1
#define   SC_RA_RAM_CONFIG_GLITCHLESS_ENABLE__B                      1
#define   SC_RA_RAM_CONFIG_GLITCHLESS_ENABLE__W                      1
#define   SC_RA_RAM_CONFIG_GLITCHLESS_ENABLE__M                      0x2
#define   SC_RA_RAM_CONFIG_FR_ENABLE__B                              2
#define   SC_RA_RAM_CONFIG_FR_ENABLE__W                              1
#define   SC_RA_RAM_CONFIG_FR_ENABLE__M                              0x4
#define   SC_RA_RAM_CONFIG_MIXMODE__B                                3
#define   SC_RA_RAM_CONFIG_MIXMODE__W                                1
#define   SC_RA_RAM_CONFIG_MIXMODE__M                                0x8
#define   SC_RA_RAM_CONFIG_FREQSCAN__B                               4
#define   SC_RA_RAM_CONFIG_FREQSCAN__W                               1
#define   SC_RA_RAM_CONFIG_FREQSCAN__M                               0x10
#define   SC_RA_RAM_CONFIG_SLAVE__B                                  5
#define   SC_RA_RAM_CONFIG_SLAVE__W                                  1
#define   SC_RA_RAM_CONFIG_SLAVE__M                                  0x20
#define   SC_RA_RAM_CONFIG_FAR_OFF__B                                6
#define   SC_RA_RAM_CONFIG_FAR_OFF__W                                1
#define   SC_RA_RAM_CONFIG_FAR_OFF__M                                0x40
#define   SC_RA_RAM_CONFIG_FEC_CHECK_ON__B                           7
#define   SC_RA_RAM_CONFIG_FEC_CHECK_ON__W                           1
#define   SC_RA_RAM_CONFIG_FEC_CHECK_ON__M                           0x80
#define   SC_RA_RAM_CONFIG_ECHO_UPDATED__B                           8
#define   SC_RA_RAM_CONFIG_ECHO_UPDATED__W                           1
#define   SC_RA_RAM_CONFIG_ECHO_UPDATED__M                           0x100
#define   SC_RA_RAM_CONFIG_ADJUST_OFF__B                             15
#define   SC_RA_RAM_CONFIG_ADJUST_OFF__W                             1
#define   SC_RA_RAM_CONFIG_ADJUST_OFF__M                             0x8000

#define SC_RA_RAM_PILOT_THRES_SPD__A                                 0x820051
#define SC_RA_RAM_PILOT_THRES_SPD__W                                 16
#define SC_RA_RAM_PILOT_THRES_SPD__M                                 0xFFFF
#define SC_RA_RAM_PILOT_THRES_SPD__PRE                               0x4
#define SC_RA_RAM_PILOT_THRES_CPD__A                                 0x820052
#define SC_RA_RAM_PILOT_THRES_CPD__W                                 16
#define SC_RA_RAM_PILOT_THRES_CPD__M                                 0xFFFF
#define SC_RA_RAM_PILOT_THRES_CPD__PRE                               0x4
#define SC_RA_RAM_PILOT_THRES_FREQSCAN__A                            0x820053
#define SC_RA_RAM_PILOT_THRES_FREQSCAN__W                            16
#define SC_RA_RAM_PILOT_THRES_FREQSCAN__M                            0xFFFF
#define SC_RA_RAM_PILOT_THRES_FREQSCAN__PRE                          0x406

#define SC_RA_RAM_CO_THRES_8K__A                                     0x820055
#define SC_RA_RAM_CO_THRES_8K__W                                     16
#define SC_RA_RAM_CO_THRES_8K__M                                     0xFFFF
#define SC_RA_RAM_CO_THRES_8K__PRE                                   0x10E
#define SC_RA_RAM_CO_THRES_2K__A                                     0x820056
#define SC_RA_RAM_CO_THRES_2K__W                                     16
#define SC_RA_RAM_CO_THRES_2K__M                                     0xFFFF
#define SC_RA_RAM_CO_THRES_2K__PRE                                   0x208
#define SC_RA_RAM_CO_LEVEL__A                                        0x820057
#define SC_RA_RAM_CO_LEVEL__W                                        16
#define SC_RA_RAM_CO_LEVEL__M                                        0xFFFF
#define SC_RA_RAM_CO_DETECT__A                                       0x820058
#define SC_RA_RAM_CO_DETECT__W                                       16
#define SC_RA_RAM_CO_DETECT__M                                       0xFFFF
#define SC_RA_RAM_CO_CAL_OFF_Q4_8K__A                                0x820059
#define SC_RA_RAM_CO_CAL_OFF_Q4_8K__W                                16
#define SC_RA_RAM_CO_CAL_OFF_Q4_8K__M                                0xFFFF
#define SC_RA_RAM_CO_CAL_OFF_Q4_8K__PRE                              0xFFDB
#define SC_RA_RAM_CO_CAL_OFF_Q16_8K__A                               0x82005A
#define SC_RA_RAM_CO_CAL_OFF_Q16_8K__W                               16
#define SC_RA_RAM_CO_CAL_OFF_Q16_8K__M                               0xFFFF
#define SC_RA_RAM_CO_CAL_OFF_Q16_8K__PRE                             0xFFEB
#define SC_RA_RAM_CO_CAL_OFF_Q64_8K__A                               0x82005B
#define SC_RA_RAM_CO_CAL_OFF_Q64_8K__W                               16
#define SC_RA_RAM_CO_CAL_OFF_Q64_8K__M                               0xFFFF
#define SC_RA_RAM_CO_CAL_OFF_Q64_8K__PRE                             0xFFFB
#define SC_RA_RAM_CO_CAL_OFF_Q4_2K__A                                0x82005C
#define SC_RA_RAM_CO_CAL_OFF_Q4_2K__W                                16
#define SC_RA_RAM_CO_CAL_OFF_Q4_2K__M                                0xFFFF
#define SC_RA_RAM_CO_CAL_OFF_Q4_2K__PRE                              0xFFDD
#define SC_RA_RAM_CO_CAL_OFF_Q16_2K__A                               0x82005D
#define SC_RA_RAM_CO_CAL_OFF_Q16_2K__W                               16
#define SC_RA_RAM_CO_CAL_OFF_Q16_2K__M                               0xFFFF
#define SC_RA_RAM_CO_CAL_OFF_Q16_2K__PRE                             0xFFED
#define SC_RA_RAM_CO_CAL_OFF_Q64_2K__A                               0x82005E
#define SC_RA_RAM_CO_CAL_OFF_Q64_2K__W                               16
#define SC_RA_RAM_CO_CAL_OFF_Q64_2K__M                               0xFFFF
#define SC_RA_RAM_CO_CAL_OFF_Q64_2K__PRE                             0xFFFD
#define SC_RA_RAM_MOTION_OFFSET__A                                   0x82005F
#define SC_RA_RAM_MOTION_OFFSET__W                                   16
#define SC_RA_RAM_MOTION_OFFSET__M                                   0xFFFF
#define SC_RA_RAM_MOTION_OFFSET__PRE                                 0x2
#define SC_RA_RAM_STATE_PROC_STOP__AX                                0x820060
#define SC_RA_RAM_STATE_PROC_STOP__XSZ                               12
#define SC_RA_RAM_STATE_PROC_STOP__W                                 16
#define SC_RA_RAM_STATE_PROC_STOP__M                                 0xFFFF
#define SC_RA_RAM_STATE_PROC_STOP_1__PRE                             0xFFFE
#define SC_RA_RAM_STATE_PROC_STOP_2__PRE                             0x0
#define SC_RA_RAM_STATE_PROC_STOP_3__PRE                             0x4
#define SC_RA_RAM_STATE_PROC_STOP_4__PRE                             0x0
#define SC_RA_RAM_STATE_PROC_STOP_5__PRE                             0x0
#define SC_RA_RAM_STATE_PROC_STOP_6__PRE                             0x0
#define SC_RA_RAM_STATE_PROC_STOP_7__PRE                             0x0
#define SC_RA_RAM_STATE_PROC_STOP_8__PRE                             0x0
#define SC_RA_RAM_STATE_PROC_STOP_9__PRE                             0x0
#define SC_RA_RAM_STATE_PROC_STOP_10__PRE                            0x0
#define SC_RA_RAM_STATE_PROC_STOP_11__PRE                            0xFFFE
#define SC_RA_RAM_STATE_PROC_STOP_12__PRE                            0xFFFE
#define SC_RA_RAM_STATE_PROC_START__AX                               0x820070
#define SC_RA_RAM_STATE_PROC_START__XSZ                              12
#define SC_RA_RAM_STATE_PROC_START__W                                16
#define SC_RA_RAM_STATE_PROC_START__M                                0xFFFF
#define SC_RA_RAM_STATE_PROC_START_1__PRE                            0x80
#define SC_RA_RAM_STATE_PROC_START_2__PRE                            0x2
#define SC_RA_RAM_STATE_PROC_START_3__PRE                            0x4
#define SC_RA_RAM_STATE_PROC_START_4__PRE                            0x4
#define SC_RA_RAM_STATE_PROC_START_5__PRE                            0x4
#define SC_RA_RAM_STATE_PROC_START_6__PRE                            0x0
#define SC_RA_RAM_STATE_PROC_START_7__PRE                            0x10
#define SC_RA_RAM_STATE_PROC_START_8__PRE                            0x0
#define SC_RA_RAM_STATE_PROC_START_9__PRE                            0x0
#define SC_RA_RAM_STATE_PROC_START_10__PRE                           0x30
#define SC_RA_RAM_STATE_PROC_START_11__PRE                           0x0
#define SC_RA_RAM_STATE_PROC_START_12__PRE                           0x0
#define SC_RA_RAM_IF_SAVE__AX                                        0x82008E
#define SC_RA_RAM_IF_SAVE__XSZ                                       2
#define SC_RA_RAM_IF_SAVE__W                                         16
#define SC_RA_RAM_IF_SAVE__M                                         0xFFFF
#define SC_RA_RAM_FR_THRES__A                                        0x82007D
#define SC_RA_RAM_FR_THRES__W                                        16
#define SC_RA_RAM_FR_THRES__M                                        0xFFFF
#define SC_RA_RAM_FR_THRES__PRE                                      0x1A2C
#define SC_RA_RAM_STATUS__A                                          0x82007E
#define SC_RA_RAM_STATUS__W                                          16
#define SC_RA_RAM_STATUS__M                                          0xFFFF
#define SC_RA_RAM_NF_BORDER_INIT__A                                  0x82007F
#define SC_RA_RAM_NF_BORDER_INIT__W                                  16
#define SC_RA_RAM_NF_BORDER_INIT__M                                  0xFFFF
#define SC_RA_RAM_NF_BORDER_INIT__PRE                                0x500
#define SC_RA_RAM_TIMER__A                                           0x820080
#define SC_RA_RAM_TIMER__W                                           16
#define SC_RA_RAM_TIMER__M                                           0xFFFF
#define SC_RA_RAM_FI_OFFSET__A                                       0x820081
#define SC_RA_RAM_FI_OFFSET__W                                       16
#define SC_RA_RAM_FI_OFFSET__M                                       0xFFFF
#define SC_RA_RAM_FI_OFFSET__PRE                                     0x382
#define SC_RA_RAM_ECHO_GUARD__A                                      0x820082
#define SC_RA_RAM_ECHO_GUARD__W                                      16
#define SC_RA_RAM_ECHO_GUARD__M                                      0xFFFF
#define SC_RA_RAM_ECHO_GUARD__PRE                                    0x18

#define SC_RA_RAM_IR_FREQ__A                                         0x8200D0
#define SC_RA_RAM_IR_FREQ__W                                         16
#define SC_RA_RAM_IR_FREQ__M                                         0xFFFF
#define SC_RA_RAM_IR_FREQ__PRE                                       0x0

#define SC_RA_RAM_IR_COARSE_2K_LENGTH__A                             0x8200D1
#define SC_RA_RAM_IR_COARSE_2K_LENGTH__W                             16
#define SC_RA_RAM_IR_COARSE_2K_LENGTH__M                             0xFFFF
#define SC_RA_RAM_IR_COARSE_2K_LENGTH__PRE                           0x9
#define SC_RA_RAM_IR_COARSE_2K_FREQINC__A                            0x8200D2
#define SC_RA_RAM_IR_COARSE_2K_FREQINC__W                            16
#define SC_RA_RAM_IR_COARSE_2K_FREQINC__M                            0xFFFF
#define SC_RA_RAM_IR_COARSE_2K_FREQINC__PRE                          0x4
#define SC_RA_RAM_IR_COARSE_2K_KAISINC__A                            0x8200D3
#define SC_RA_RAM_IR_COARSE_2K_KAISINC__W                            16
#define SC_RA_RAM_IR_COARSE_2K_KAISINC__M                            0xFFFF
#define SC_RA_RAM_IR_COARSE_2K_KAISINC__PRE                          0x100

#define SC_RA_RAM_IR_COARSE_8K_LENGTH__A                             0x8200D4
#define SC_RA_RAM_IR_COARSE_8K_LENGTH__W                             16
#define SC_RA_RAM_IR_COARSE_8K_LENGTH__M                             0xFFFF
#define SC_RA_RAM_IR_COARSE_8K_LENGTH__PRE                           0x8
#define SC_RA_RAM_IR_COARSE_8K_FREQINC__A                            0x8200D5
#define SC_RA_RAM_IR_COARSE_8K_FREQINC__W                            16
#define SC_RA_RAM_IR_COARSE_8K_FREQINC__M                            0xFFFF
#define SC_RA_RAM_IR_COARSE_8K_FREQINC__PRE                          0x8
#define SC_RA_RAM_IR_COARSE_8K_KAISINC__A                            0x8200D6
#define SC_RA_RAM_IR_COARSE_8K_KAISINC__W                            16
#define SC_RA_RAM_IR_COARSE_8K_KAISINC__M                            0xFFFF
#define SC_RA_RAM_IR_COARSE_8K_KAISINC__PRE                          0x200

#define SC_RA_RAM_IR_FINE_2K_LENGTH__A                               0x8200D7
#define SC_RA_RAM_IR_FINE_2K_LENGTH__W                               16
#define SC_RA_RAM_IR_FINE_2K_LENGTH__M                               0xFFFF
#define SC_RA_RAM_IR_FINE_2K_LENGTH__PRE                             0x9
#define SC_RA_RAM_IR_FINE_2K_FREQINC__A                              0x8200D8
#define SC_RA_RAM_IR_FINE_2K_FREQINC__W                              16
#define SC_RA_RAM_IR_FINE_2K_FREQINC__M                              0xFFFF
#define SC_RA_RAM_IR_FINE_2K_FREQINC__PRE                            0x4
#define SC_RA_RAM_IR_FINE_2K_KAISINC__A                              0x8200D9
#define SC_RA_RAM_IR_FINE_2K_KAISINC__W                              16
#define SC_RA_RAM_IR_FINE_2K_KAISINC__M                              0xFFFF
#define SC_RA_RAM_IR_FINE_2K_KAISINC__PRE                            0x100

#define SC_RA_RAM_IR_FINE_8K_LENGTH__A                               0x8200DA
#define SC_RA_RAM_IR_FINE_8K_LENGTH__W                               16
#define SC_RA_RAM_IR_FINE_8K_LENGTH__M                               0xFFFF
#define SC_RA_RAM_IR_FINE_8K_LENGTH__PRE                             0xB
#define SC_RA_RAM_IR_FINE_8K_FREQINC__A                              0x8200DB
#define SC_RA_RAM_IR_FINE_8K_FREQINC__W                              16
#define SC_RA_RAM_IR_FINE_8K_FREQINC__M                              0xFFFF
#define SC_RA_RAM_IR_FINE_8K_FREQINC__PRE                            0x1
#define SC_RA_RAM_IR_FINE_8K_KAISINC__A                              0x8200DC
#define SC_RA_RAM_IR_FINE_8K_KAISINC__W                              16
#define SC_RA_RAM_IR_FINE_8K_KAISINC__M                              0xFFFF
#define SC_RA_RAM_IR_FINE_8K_KAISINC__PRE                            0x40

#define SC_RA_RAM_ECHO_SHIFT_LIM__A                                  0x8200DD
#define SC_RA_RAM_ECHO_SHIFT_LIM__W                                  16
#define SC_RA_RAM_ECHO_SHIFT_LIM__M                                  0xFFFF
#define SC_RA_RAM_ECHO_SHIFT_LIM__PRE                                0xFFFF
#define SC_RA_RAM_ECHO_AGE__A                                        0x8200DE
#define SC_RA_RAM_ECHO_AGE__W                                        16
#define SC_RA_RAM_ECHO_AGE__M                                        0xFFFF
#define SC_RA_RAM_ECHO_AGE__PRE                                      0xFFFF
#define SC_RA_RAM_ECHO_FILTER__A                                     0x8200DF
#define SC_RA_RAM_ECHO_FILTER__W                                     16
#define SC_RA_RAM_ECHO_FILTER__M                                     0xFFFF
#define SC_RA_RAM_ECHO_FILTER__PRE                                   0x2

#define SC_RA_RAM_NI_INIT_2K_PER_LEFT__A                             0x8200E0
#define SC_RA_RAM_NI_INIT_2K_PER_LEFT__W                             16
#define SC_RA_RAM_NI_INIT_2K_PER_LEFT__M                             0xFFFF
#define SC_RA_RAM_NI_INIT_2K_PER_LEFT__PRE                           0x7
#define SC_RA_RAM_NI_INIT_2K_PER_RIGHT__A                            0x8200E1
#define SC_RA_RAM_NI_INIT_2K_PER_RIGHT__W                            16
#define SC_RA_RAM_NI_INIT_2K_PER_RIGHT__M                            0xFFFF
#define SC_RA_RAM_NI_INIT_2K_PER_RIGHT__PRE                          0x1
#define SC_RA_RAM_NI_INIT_2K_POS_LR__A                               0x8200E2
#define SC_RA_RAM_NI_INIT_2K_POS_LR__W                               16
#define SC_RA_RAM_NI_INIT_2K_POS_LR__M                               0xFFFF
#define SC_RA_RAM_NI_INIT_2K_POS_LR__PRE                             0xE8

#define SC_RA_RAM_NI_INIT_8K_PER_LEFT__A                             0x8200E3
#define SC_RA_RAM_NI_INIT_8K_PER_LEFT__W                             16
#define SC_RA_RAM_NI_INIT_8K_PER_LEFT__M                             0xFFFF
#define SC_RA_RAM_NI_INIT_8K_PER_LEFT__PRE                           0xE
#define SC_RA_RAM_NI_INIT_8K_PER_RIGHT__A                            0x8200E4
#define SC_RA_RAM_NI_INIT_8K_PER_RIGHT__W                            16
#define SC_RA_RAM_NI_INIT_8K_PER_RIGHT__M                            0xFFFF
#define SC_RA_RAM_NI_INIT_8K_PER_RIGHT__PRE                          0x7
#define SC_RA_RAM_NI_INIT_8K_POS_LR__A                               0x8200E5
#define SC_RA_RAM_NI_INIT_8K_POS_LR__W                               16
#define SC_RA_RAM_NI_INIT_8K_POS_LR__M                               0xFFFF
#define SC_RA_RAM_NI_INIT_8K_POS_LR__PRE                             0xA0

#define SC_RA_RAM_SAMPLE_RATE_COUNT__A                               0x8200E8
#define SC_RA_RAM_SAMPLE_RATE_COUNT__W                               16
#define SC_RA_RAM_SAMPLE_RATE_COUNT__M                               0xFFFF
#define SC_RA_RAM_SAMPLE_RATE_COUNT__PRE                             0x10
#define SC_RA_RAM_SAMPLE_RATE_STEP__A                                0x8200E9
#define SC_RA_RAM_SAMPLE_RATE_STEP__W                                16
#define SC_RA_RAM_SAMPLE_RATE_STEP__M                                0xFFFF
#define SC_RA_RAM_SAMPLE_RATE_STEP__PRE                              0x113

#define SC_RA_RAM_TPS_TIMEOUT_LIM__A                                 0x8200EA
#define SC_RA_RAM_TPS_TIMEOUT_LIM__W                                 16
#define SC_RA_RAM_TPS_TIMEOUT_LIM__M                                 0xFFFF
#define SC_RA_RAM_TPS_TIMEOUT_LIM__PRE                               0xC8
#define SC_RA_RAM_TPS_TIMEOUT__A                                     0x8200EB
#define SC_RA_RAM_TPS_TIMEOUT__W                                     16
#define SC_RA_RAM_TPS_TIMEOUT__M                                     0xFFFF
#define SC_RA_RAM_BAND__A                                            0x8200EC
#define SC_RA_RAM_BAND__W                                            16
#define SC_RA_RAM_BAND__M                                            0xFFFF
#define SC_RA_RAM_BAND__PRE                                          0x0
#define   SC_RA_RAM_BAND_INTERVAL__B                                 0
#define   SC_RA_RAM_BAND_INTERVAL__W                                 4
#define   SC_RA_RAM_BAND_INTERVAL__M                                 0xF
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_32__B                       8
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_32__W                       1
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_32__M                       0x100
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_16__B                       9
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_16__W                       1
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_16__M                       0x200
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_8__B                        10
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_8__W                        1
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_8__M                        0x400
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_4__B                        11
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_4__W                        1
#define   SC_RA_RAM_BAND_INTERVAL_ENABLE_4__M                        0x800
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_32__B                        12
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_32__W                        1
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_32__M                        0x1000
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_16__B                        13
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_16__W                        1
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_16__M                        0x2000
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_8__B                         14
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_8__W                         1
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_8__M                         0x4000
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_4__B                         15
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_4__W                         1
#define   SC_RA_RAM_BAND_HIL_MAR_ENABLE_4__M                         0x8000

#define SC_RA_RAM_EC_OC_CRA_HIP_INIT__A                              0x8200ED
#define SC_RA_RAM_EC_OC_CRA_HIP_INIT__W                              16
#define SC_RA_RAM_EC_OC_CRA_HIP_INIT__M                              0xFFFF
#define SC_RA_RAM_EC_OC_CRA_HIP_INIT__PRE                            0xC0
#define SC_RA_RAM_REG__AX                                            0x8200F0
#define SC_RA_RAM_REG__XSZ                                           2
#define SC_RA_RAM_REG__W                                             16
#define SC_RA_RAM_REG__M                                             0xFFFF
#define SC_RA_RAM_BREAK__A                                           0x8200F2
#define SC_RA_RAM_BREAK__W                                           16
#define SC_RA_RAM_BREAK__M                                           0xFFFF
#define SC_RA_RAM_BOOTCOUNT__A                                       0x8200F3
#define SC_RA_RAM_BOOTCOUNT__W                                       16
#define SC_RA_RAM_BOOTCOUNT__M                                       0xFFFF

#define SC_RA_RAM_LC_ABS_2K__A                                       0x8200F4
#define SC_RA_RAM_LC_ABS_2K__W                                       16
#define SC_RA_RAM_LC_ABS_2K__M                                       0xFFFF
#define SC_RA_RAM_LC_ABS_2K__PRE                                     0x1F
#define SC_RA_RAM_LC_ABS_8K__A                                       0x8200F5
#define SC_RA_RAM_LC_ABS_8K__W                                       16
#define SC_RA_RAM_LC_ABS_8K__M                                       0xFFFF
#define SC_RA_RAM_LC_ABS_8K__PRE                                     0x1F

#define SC_RA_RAM_NE_ERR_SELECT_FR_OFF_2K__A                         0x8200F6
#define SC_RA_RAM_NE_ERR_SELECT_FR_OFF_2K__W                         16
#define SC_RA_RAM_NE_ERR_SELECT_FR_OFF_2K__M                         0xFFFF
#define SC_RA_RAM_NE_ERR_SELECT_FR_OFF_2K__PRE                       0x1
#define SC_RA_RAM_NE_ERR_SELECT_FR_OFF_8K__A                         0x8200F7
#define SC_RA_RAM_NE_ERR_SELECT_FR_OFF_8K__W                         16
#define SC_RA_RAM_NE_ERR_SELECT_FR_OFF_8K__M                         0xFFFF
#define SC_RA_RAM_NE_ERR_SELECT_FR_OFF_8K__PRE                       0x0

#define SC_RA_RAM_NE_ERR_SELECT_FR_ON_2K__A                          0x8200F8
#define SC_RA_RAM_NE_ERR_SELECT_FR_ON_2K__W                          16
#define SC_RA_RAM_NE_ERR_SELECT_FR_ON_2K__M                          0xFFFF
#define SC_RA_RAM_NE_ERR_SELECT_FR_ON_2K__PRE                        0x3
#define SC_RA_RAM_NE_ERR_SELECT_FR_ON_8K__A                          0x8200F9
#define SC_RA_RAM_NE_ERR_SELECT_FR_ON_8K__W                          16
#define SC_RA_RAM_NE_ERR_SELECT_FR_ON_8K__M                          0xFFFF
#define SC_RA_RAM_NE_ERR_SELECT_FR_ON_8K__PRE                        0x2
#define SC_RA_RAM_RELOCK__A                                          0x8200FE
#define SC_RA_RAM_RELOCK__W                                          16
#define SC_RA_RAM_RELOCK__M                                          0xFFFF
#define SC_RA_RAM_STACKUNDERFLOW__A                                  0x8200FF
#define SC_RA_RAM_STACKUNDERFLOW__W                                  16
#define SC_RA_RAM_STACKUNDERFLOW__M                                  0xFFFF

#define SC_RA_RAM_NF_MAXECHOTOKEN__A                                 0x820148
#define SC_RA_RAM_NF_MAXECHOTOKEN__W                                 16
#define SC_RA_RAM_NF_MAXECHOTOKEN__M                                 0xFFFF
#define SC_RA_RAM_NF_PREPOST__A                                      0x820149
#define SC_RA_RAM_NF_PREPOST__W                                      16
#define SC_RA_RAM_NF_PREPOST__M                                      0xFFFF
#define SC_RA_RAM_NF_PREBORDER__A                                    0x82014A
#define SC_RA_RAM_NF_PREBORDER__W                                    16
#define SC_RA_RAM_NF_PREBORDER__M                                    0xFFFF
#define SC_RA_RAM_NF_START__A                                        0x82014B
#define SC_RA_RAM_NF_START__W                                        16
#define SC_RA_RAM_NF_START__M                                        0xFFFF
#define SC_RA_RAM_NF_MINISI__AX                                      0x82014C
#define SC_RA_RAM_NF_MINISI__XSZ                                     2
#define SC_RA_RAM_NF_MINISI__W                                       16
#define SC_RA_RAM_NF_MINISI__M                                       0xFFFF
#define SC_RA_RAM_NF_MAXECHO__A                                      0x82014E
#define SC_RA_RAM_NF_MAXECHO__W                                      16
#define SC_RA_RAM_NF_MAXECHO__M                                      0xFFFF
#define SC_RA_RAM_NF_NRECHOES__A                                     0x82014F
#define SC_RA_RAM_NF_NRECHOES__W                                     16
#define SC_RA_RAM_NF_NRECHOES__M                                     0xFFFF
#define SC_RA_RAM_NF_ECHOTABLE__AX                                   0x820150
#define SC_RA_RAM_NF_ECHOTABLE__XSZ                                  16
#define SC_RA_RAM_NF_ECHOTABLE__W                                    16
#define SC_RA_RAM_NF_ECHOTABLE__M                                    0xFFFF

#define SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__A                          0x8201A0
#define SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__W                          16
#define SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__M                          0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__PRE                        0x1D6
#define SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__A                          0x8201A1
#define SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__W                          16
#define SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__M                          0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__PRE                        0x4

#define SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__A                             0x8201A2
#define SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__W                             16
#define SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__M                             0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__PRE                           0x1BB
#define SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__A                             0x8201A3
#define SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__W                             16
#define SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__M                             0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__PRE                           0x5

#define SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__A                            0x8201A4
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__W                            16
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__M                            0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__PRE                          0x1EF
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__A                            0x8201A5
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__W                            16
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__M                            0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__PRE                          0x5

#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__A                         0x8201A6
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__W                         16
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__M                         0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__PRE                       0x15E
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__A                         0x8201A7
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__W                         16
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__M                         0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__PRE                       0x5

#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__A                         0x8201A8
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__W                         16
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__M                         0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__PRE                       0x11A
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__A                         0x8201A9
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__W                         16
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__M                         0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__PRE                       0x6

#define SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__A                            0x8201AA
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__W                            16
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__M                            0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__PRE                          0x1FB
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__A                            0x8201AB
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__W                            16
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__M                            0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__PRE                          0x5

#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__A                         0x8201AC
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__W                         16
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__M                         0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__PRE                       0x12F
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__A                         0x8201AD
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__W                         16
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__M                         0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__PRE                       0x5

#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__A                         0x8201AE
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__W                         16
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__M                         0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__PRE                       0x197
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__A                         0x8201AF
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__W                         16
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__M                         0xFFFF
#define SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__PRE                       0x5
#define SC_RA_RAM_DRIVER_VERSION__AX                                 0x8201FE
#define SC_RA_RAM_DRIVER_VERSION__XSZ                                2
#define SC_RA_RAM_DRIVER_VERSION__W                                  16
#define SC_RA_RAM_DRIVER_VERSION__M                                  0xFFFF
#define   SC_RA_RAM_EVENT0_MIN                                       0x7
#define   SC_RA_RAM_EVENT0_FE_CU                                     0x7
#define   SC_RA_RAM_EVENT0_CE                                        0xA
#define   SC_RA_RAM_EVENT0_EQ                                        0xE
#define   SC_RA_RAM_EVENT0_MAX                                       0xF
#define   SC_RA_RAM_EVENT1_MIN                                       0x8
#define   SC_RA_RAM_EVENT1_EC_OD                                     0x8
#define   SC_RA_RAM_EVENT1_LC                                        0xC
#define   SC_RA_RAM_EVENT1_MAX                                       0xD
#define   SC_RA_RAM_PROC_LOCKTRACK                                   0x0
#define   SC_RA_RAM_PROC_MODE_GUARD                                  0x1
#define   SC_RA_RAM_PROC_PILOTS                                      0x2
#define   SC_RA_RAM_PROC_FESTART_ADJUST                              0x3
#define   SC_RA_RAM_PROC_ECHO                                        0x4
#define   SC_RA_RAM_PROC_BE_OPT                                      0x5
#define   SC_RA_RAM_PROC_EQ                                          0x7
#define   SC_RA_RAM_PROC_MAX                                         0x8

#define SC_IF_RAM_TRP_RST__AX                                        0x830000
#define SC_IF_RAM_TRP_RST__XSZ                                       2
#define SC_IF_RAM_TRP_RST__W                                         12
#define SC_IF_RAM_TRP_RST__M                                         0xFFF

#define SC_IF_RAM_TRP_BPT0__AX                                       0x830002
#define SC_IF_RAM_TRP_BPT0__XSZ                                      2
#define SC_IF_RAM_TRP_BPT0__W                                        12
#define SC_IF_RAM_TRP_BPT0__M                                        0xFFF

#define SC_IF_RAM_TRP_STKU__AX                                       0x830004
#define SC_IF_RAM_TRP_STKU__XSZ                                      2
#define SC_IF_RAM_TRP_STKU__W                                        12
#define SC_IF_RAM_TRP_STKU__M                                        0xFFF

#define SC_IF_RAM_VERSION_MA_MI__A                                   0x830FFE
#define SC_IF_RAM_VERSION_MA_MI__W                                   12
#define SC_IF_RAM_VERSION_MA_MI__M                                   0xFFF

#define SC_IF_RAM_VERSION_PATCH__A                                   0x830FFF
#define SC_IF_RAM_VERSION_PATCH__W                                   12
#define SC_IF_RAM_VERSION_PATCH__M                                   0xFFF

#define FE_COMM_EXEC__A                                              0xC00000
#define FE_COMM_EXEC__W                                              3
#define FE_COMM_EXEC__M                                              0x7
#define   FE_COMM_EXEC_CTL__B                                        0
#define   FE_COMM_EXEC_CTL__W                                        3
#define   FE_COMM_EXEC_CTL__M                                        0x7
#define     FE_COMM_EXEC_CTL_STOP                                    0x0
#define     FE_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     FE_COMM_EXEC_CTL_HOLD                                    0x2
#define     FE_COMM_EXEC_CTL_STEP                                    0x3
#define     FE_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     FE_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define FE_COMM_STATE__A                                             0xC00001
#define FE_COMM_STATE__W                                             16
#define FE_COMM_STATE__M                                             0xFFFF
#define FE_COMM_MB__A                                                0xC00002
#define FE_COMM_MB__W                                                16
#define FE_COMM_MB__M                                                0xFFFF
#define FE_COMM_SERVICE0__A                                          0xC00003
#define FE_COMM_SERVICE0__W                                          16
#define FE_COMM_SERVICE0__M                                          0xFFFF
#define FE_COMM_SERVICE1__A                                          0xC00004
#define FE_COMM_SERVICE1__W                                          16
#define FE_COMM_SERVICE1__M                                          0xFFFF
#define FE_COMM_INT_STA__A                                           0xC00007
#define FE_COMM_INT_STA__W                                           16
#define FE_COMM_INT_STA__M                                           0xFFFF
#define FE_COMM_INT_MSK__A                                           0xC00008
#define FE_COMM_INT_MSK__W                                           16
#define FE_COMM_INT_MSK__M                                           0xFFFF

#define FE_AD_SID                                                    0x1

#define FE_AD_REG_COMM_EXEC__A                                       0xC10000
#define FE_AD_REG_COMM_EXEC__W                                       3
#define FE_AD_REG_COMM_EXEC__M                                       0x7
#define   FE_AD_REG_COMM_EXEC_CTL__B                                 0
#define   FE_AD_REG_COMM_EXEC_CTL__W                                 3
#define   FE_AD_REG_COMM_EXEC_CTL__M                                 0x7
#define     FE_AD_REG_COMM_EXEC_CTL_STOP                             0x0
#define     FE_AD_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     FE_AD_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     FE_AD_REG_COMM_EXEC_CTL_STEP                             0x3

#define FE_AD_REG_COMM_MB__A                                         0xC10002
#define FE_AD_REG_COMM_MB__W                                         2
#define FE_AD_REG_COMM_MB__M                                         0x3
#define   FE_AD_REG_COMM_MB_CTR__B                                   0
#define   FE_AD_REG_COMM_MB_CTR__W                                   1
#define   FE_AD_REG_COMM_MB_CTR__M                                   0x1
#define     FE_AD_REG_COMM_MB_CTR_OFF                                0x0
#define     FE_AD_REG_COMM_MB_CTR_ON                                 0x1
#define   FE_AD_REG_COMM_MB_OBS__B                                   1
#define   FE_AD_REG_COMM_MB_OBS__W                                   1
#define   FE_AD_REG_COMM_MB_OBS__M                                   0x2
#define     FE_AD_REG_COMM_MB_OBS_OFF                                0x0
#define     FE_AD_REG_COMM_MB_OBS_ON                                 0x2

#define FE_AD_REG_COMM_SERVICE0__A                                   0xC10003
#define FE_AD_REG_COMM_SERVICE0__W                                   10
#define FE_AD_REG_COMM_SERVICE0__M                                   0x3FF
#define   FE_AD_REG_COMM_SERVICE0_FE_AD__B                           0
#define   FE_AD_REG_COMM_SERVICE0_FE_AD__W                           1
#define   FE_AD_REG_COMM_SERVICE0_FE_AD__M                           0x1

#define FE_AD_REG_COMM_SERVICE1__A                                   0xC10004
#define FE_AD_REG_COMM_SERVICE1__W                                   11
#define FE_AD_REG_COMM_SERVICE1__M                                   0x7FF

#define FE_AD_REG_COMM_INT_STA__A                                    0xC10007
#define FE_AD_REG_COMM_INT_STA__W                                    2
#define FE_AD_REG_COMM_INT_STA__M                                    0x3
#define   FE_AD_REG_COMM_INT_STA_ADC_OVERFLOW__B                     0
#define   FE_AD_REG_COMM_INT_STA_ADC_OVERFLOW__W                     1
#define   FE_AD_REG_COMM_INT_STA_ADC_OVERFLOW__M                     0x1

#define FE_AD_REG_COMM_INT_MSK__A                                    0xC10008
#define FE_AD_REG_COMM_INT_MSK__W                                    2
#define FE_AD_REG_COMM_INT_MSK__M                                    0x3
#define   FE_AD_REG_COMM_INT_MSK_ADC_OVERFLOW__B                     0
#define   FE_AD_REG_COMM_INT_MSK_ADC_OVERFLOW__W                     1
#define   FE_AD_REG_COMM_INT_MSK_ADC_OVERFLOW__M                     0x1

#define FE_AD_REG_CUR_SEL__A                                         0xC10010
#define FE_AD_REG_CUR_SEL__W                                         2
#define FE_AD_REG_CUR_SEL__M                                         0x3
#define   FE_AD_REG_CUR_SEL_INIT                                     0x2

#define FE_AD_REG_OVERFLOW__A                                        0xC10011
#define FE_AD_REG_OVERFLOW__W                                        1
#define FE_AD_REG_OVERFLOW__M                                        0x1
#define   FE_AD_REG_OVERFLOW_INIT                                    0x0

#define FE_AD_REG_FDB_IN__A                                          0xC10012
#define FE_AD_REG_FDB_IN__W                                          1
#define FE_AD_REG_FDB_IN__M                                          0x1
#define   FE_AD_REG_FDB_IN_INIT                                      0x0

#define FE_AD_REG_PD__A                                              0xC10013
#define FE_AD_REG_PD__W                                              1
#define FE_AD_REG_PD__M                                              0x1
#define   FE_AD_REG_PD_INIT                                          0x1

#define FE_AD_REG_INVEXT__A                                          0xC10014
#define FE_AD_REG_INVEXT__W                                          1
#define FE_AD_REG_INVEXT__M                                          0x1
#define   FE_AD_REG_INVEXT_INIT                                      0x0

#define FE_AD_REG_CLKNEG__A                                          0xC10015
#define FE_AD_REG_CLKNEG__W                                          1
#define FE_AD_REG_CLKNEG__M                                          0x1
#define   FE_AD_REG_CLKNEG_INIT                                      0x0

#define FE_AD_REG_MON_IN_MUX__A                                      0xC10016
#define FE_AD_REG_MON_IN_MUX__W                                      2
#define FE_AD_REG_MON_IN_MUX__M                                      0x3
#define   FE_AD_REG_MON_IN_MUX_INIT                                  0x0

#define FE_AD_REG_MON_IN5__A                                         0xC10017
#define FE_AD_REG_MON_IN5__W                                         10
#define FE_AD_REG_MON_IN5__M                                         0x3FF
#define   FE_AD_REG_MON_IN5_INIT                                     0x0

#define FE_AD_REG_MON_IN4__A                                         0xC10018
#define FE_AD_REG_MON_IN4__W                                         10
#define FE_AD_REG_MON_IN4__M                                         0x3FF
#define   FE_AD_REG_MON_IN4_INIT                                     0x0

#define FE_AD_REG_MON_IN3__A                                         0xC10019
#define FE_AD_REG_MON_IN3__W                                         10
#define FE_AD_REG_MON_IN3__M                                         0x3FF
#define   FE_AD_REG_MON_IN3_INIT                                     0x0

#define FE_AD_REG_MON_IN2__A                                         0xC1001A
#define FE_AD_REG_MON_IN2__W                                         10
#define FE_AD_REG_MON_IN2__M                                         0x3FF
#define   FE_AD_REG_MON_IN2_INIT                                     0x0

#define FE_AD_REG_MON_IN1__A                                         0xC1001B
#define FE_AD_REG_MON_IN1__W                                         10
#define FE_AD_REG_MON_IN1__M                                         0x3FF
#define   FE_AD_REG_MON_IN1_INIT                                     0x0

#define FE_AD_REG_MON_IN0__A                                         0xC1001C
#define FE_AD_REG_MON_IN0__W                                         10
#define FE_AD_REG_MON_IN0__M                                         0x3FF
#define   FE_AD_REG_MON_IN0_INIT                                     0x0

#define FE_AD_REG_MON_IN_VAL__A                                      0xC1001D
#define FE_AD_REG_MON_IN_VAL__W                                      1
#define FE_AD_REG_MON_IN_VAL__M                                      0x1
#define   FE_AD_REG_MON_IN_VAL_INIT                                  0x0

#define FE_AD_REG_CTR_CLK_O__A                                       0xC1001E
#define FE_AD_REG_CTR_CLK_O__W                                       1
#define FE_AD_REG_CTR_CLK_O__M                                       0x1
#define   FE_AD_REG_CTR_CLK_O_INIT                                   0x0

#define FE_AD_REG_CTR_CLK_E_O__A                                     0xC1001F
#define FE_AD_REG_CTR_CLK_E_O__W                                     1
#define FE_AD_REG_CTR_CLK_E_O__M                                     0x1
#define   FE_AD_REG_CTR_CLK_E_O_INIT                                 0x1

#define FE_AD_REG_CTR_VAL_O__A                                       0xC10020
#define FE_AD_REG_CTR_VAL_O__W                                       1
#define FE_AD_REG_CTR_VAL_O__M                                       0x1
#define   FE_AD_REG_CTR_VAL_O_INIT                                   0x0

#define FE_AD_REG_CTR_VAL_E_O__A                                     0xC10021
#define FE_AD_REG_CTR_VAL_E_O__W                                     1
#define FE_AD_REG_CTR_VAL_E_O__M                                     0x1
#define   FE_AD_REG_CTR_VAL_E_O_INIT                                 0x1

#define FE_AD_REG_CTR_DATA_O__A                                      0xC10022
#define FE_AD_REG_CTR_DATA_O__W                                      10
#define FE_AD_REG_CTR_DATA_O__M                                      0x3FF
#define   FE_AD_REG_CTR_DATA_O_INIT                                  0x0

#define FE_AD_REG_CTR_DATA_E_O__A                                    0xC10023
#define FE_AD_REG_CTR_DATA_E_O__W                                    10
#define FE_AD_REG_CTR_DATA_E_O__M                                    0x3FF
#define   FE_AD_REG_CTR_DATA_E_O_INIT                                0x3FF

#define FE_AG_SID                                                    0x2

#define FE_AG_REG_COMM_EXEC__A                                       0xC20000
#define FE_AG_REG_COMM_EXEC__W                                       3
#define FE_AG_REG_COMM_EXEC__M                                       0x7
#define   FE_AG_REG_COMM_EXEC_CTL__B                                 0
#define   FE_AG_REG_COMM_EXEC_CTL__W                                 3
#define   FE_AG_REG_COMM_EXEC_CTL__M                                 0x7
#define     FE_AG_REG_COMM_EXEC_CTL_STOP                             0x0
#define     FE_AG_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     FE_AG_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     FE_AG_REG_COMM_EXEC_CTL_STEP                             0x3

#define FE_AG_REG_COMM_STATE__A                                      0xC20001
#define FE_AG_REG_COMM_STATE__W                                      4
#define FE_AG_REG_COMM_STATE__M                                      0xF

#define FE_AG_REG_COMM_MB__A                                         0xC20002
#define FE_AG_REG_COMM_MB__W                                         2
#define FE_AG_REG_COMM_MB__M                                         0x3
#define   FE_AG_REG_COMM_MB_CTR__B                                   0
#define   FE_AG_REG_COMM_MB_CTR__W                                   1
#define   FE_AG_REG_COMM_MB_CTR__M                                   0x1
#define     FE_AG_REG_COMM_MB_CTR_OFF                                0x0
#define     FE_AG_REG_COMM_MB_CTR_ON                                 0x1
#define   FE_AG_REG_COMM_MB_OBS__B                                   1
#define   FE_AG_REG_COMM_MB_OBS__W                                   1
#define   FE_AG_REG_COMM_MB_OBS__M                                   0x2
#define     FE_AG_REG_COMM_MB_OBS_OFF                                0x0
#define     FE_AG_REG_COMM_MB_OBS_ON                                 0x2

#define FE_AG_REG_COMM_SERVICE0__A                                   0xC20003
#define FE_AG_REG_COMM_SERVICE0__W                                   10
#define FE_AG_REG_COMM_SERVICE0__M                                   0x3FF

#define FE_AG_REG_COMM_SERVICE1__A                                   0xC20004
#define FE_AG_REG_COMM_SERVICE1__W                                   11
#define FE_AG_REG_COMM_SERVICE1__M                                   0x7FF

#define FE_AG_REG_COMM_INT_STA__A                                    0xC20007
#define FE_AG_REG_COMM_INT_STA__W                                    8
#define FE_AG_REG_COMM_INT_STA__M                                    0xFF
#define   FE_AG_REG_COMM_INT_STA_DCE_AVE_UPD__B                      0
#define   FE_AG_REG_COMM_INT_STA_DCE_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_STA_DCE_AVE_UPD__M                      0x1
#define   FE_AG_REG_COMM_INT_STA_ACE_AVE_UPD__B                      1
#define   FE_AG_REG_COMM_INT_STA_ACE_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_STA_ACE_AVE_UPD__M                      0x2
#define   FE_AG_REG_COMM_INT_STA_CDR_CLP_UPD__B                      2
#define   FE_AG_REG_COMM_INT_STA_CDR_CLP_UPD__W                      1
#define   FE_AG_REG_COMM_INT_STA_CDR_CLP_UPD__M                      0x4
#define   FE_AG_REG_COMM_INT_STA_AEC_AVE_UPD__B                      3
#define   FE_AG_REG_COMM_INT_STA_AEC_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_STA_AEC_AVE_UPD__M                      0x8
#define   FE_AG_REG_COMM_INT_STA_PDA_AVE_UPD__B                      4
#define   FE_AG_REG_COMM_INT_STA_PDA_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_STA_PDA_AVE_UPD__M                      0x10
#define   FE_AG_REG_COMM_INT_STA_TGA_AVE_UPD__B                      5
#define   FE_AG_REG_COMM_INT_STA_TGA_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_STA_TGA_AVE_UPD__M                      0x20
#define   FE_AG_REG_COMM_INT_STA_FGA_AVE_UPD__B                      6
#define   FE_AG_REG_COMM_INT_STA_FGA_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_STA_FGA_AVE_UPD__M                      0x40
#define   FE_AG_REG_COMM_INT_STA_BGC_PGA_UPD__B                      7
#define   FE_AG_REG_COMM_INT_STA_BGC_PGA_UPD__W                      1
#define   FE_AG_REG_COMM_INT_STA_BGC_PGA_UPD__M                      0x80

#define FE_AG_REG_COMM_INT_MSK__A                                    0xC20008
#define FE_AG_REG_COMM_INT_MSK__W                                    8
#define FE_AG_REG_COMM_INT_MSK__M                                    0xFF
#define   FE_AG_REG_COMM_INT_MSK_DCE_AVE_UPD__B                      0
#define   FE_AG_REG_COMM_INT_MSK_DCE_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_MSK_DCE_AVE_UPD__M                      0x1
#define   FE_AG_REG_COMM_INT_MSK_ACE_AVE_UPD__B                      1
#define   FE_AG_REG_COMM_INT_MSK_ACE_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_MSK_ACE_AVE_UPD__M                      0x2
#define   FE_AG_REG_COMM_INT_MSK_CDR_CLP_UPD__B                      2
#define   FE_AG_REG_COMM_INT_MSK_CDR_CLP_UPD__W                      1
#define   FE_AG_REG_COMM_INT_MSK_CDR_CLP_UPD__M                      0x4
#define   FE_AG_REG_COMM_INT_MSK_AEC_AVE_UPD__B                      3
#define   FE_AG_REG_COMM_INT_MSK_AEC_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_MSK_AEC_AVE_UPD__M                      0x8
#define   FE_AG_REG_COMM_INT_MSK_PDA_AVE_UPD__B                      4
#define   FE_AG_REG_COMM_INT_MSK_PDA_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_MSK_PDA_AVE_UPD__M                      0x10
#define   FE_AG_REG_COMM_INT_MSK_TGA_AVE_UPD__B                      5
#define   FE_AG_REG_COMM_INT_MSK_TGA_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_MSK_TGA_AVE_UPD__M                      0x20
#define   FE_AG_REG_COMM_INT_MSK_FGA_AVE_UPD__B                      6
#define   FE_AG_REG_COMM_INT_MSK_FGA_AVE_UPD__W                      1
#define   FE_AG_REG_COMM_INT_MSK_FGA_AVE_UPD__M                      0x40
#define   FE_AG_REG_COMM_INT_MSK_BGC_PGA_UPD__B                      7
#define   FE_AG_REG_COMM_INT_MSK_BGC_PGA_UPD__W                      1
#define   FE_AG_REG_COMM_INT_MSK_BGC_PGA_UPD__M                      0x80

#define FE_AG_REG_AG_MODE_LOP__A                                     0xC20010
#define FE_AG_REG_AG_MODE_LOP__W                                     16
#define FE_AG_REG_AG_MODE_LOP__M                                     0xFFFF
#define   FE_AG_REG_AG_MODE_LOP_INIT                                 0x0

#define   FE_AG_REG_AG_MODE_LOP_MODE_0__B                            0
#define   FE_AG_REG_AG_MODE_LOP_MODE_0__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_0__M                            0x1
#define     FE_AG_REG_AG_MODE_LOP_MODE_0_ENABLE                      0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_0_DISABLE                     0x1

#define   FE_AG_REG_AG_MODE_LOP_MODE_1__B                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_1__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_1__M                            0x2
#define     FE_AG_REG_AG_MODE_LOP_MODE_1_STATIC                      0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_1_DYNAMIC                     0x2

#define   FE_AG_REG_AG_MODE_LOP_MODE_2__B                            2
#define   FE_AG_REG_AG_MODE_LOP_MODE_2__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_2__M                            0x4
#define     FE_AG_REG_AG_MODE_LOP_MODE_2_AVE_B                       0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_2_AVE_CB                      0x4

#define   FE_AG_REG_AG_MODE_LOP_MODE_3__B                            3
#define   FE_AG_REG_AG_MODE_LOP_MODE_3__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_3__M                            0x8
#define     FE_AG_REG_AG_MODE_LOP_MODE_3_AVE_B                       0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_3_AVE_CB                      0x8

#define   FE_AG_REG_AG_MODE_LOP_MODE_4__B                            4
#define   FE_AG_REG_AG_MODE_LOP_MODE_4__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_4__M                            0x10
#define     FE_AG_REG_AG_MODE_LOP_MODE_4_STATIC                      0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_4_DYNAMIC                     0x10

#define   FE_AG_REG_AG_MODE_LOP_MODE_5__B                            5
#define   FE_AG_REG_AG_MODE_LOP_MODE_5__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_5__M                            0x20
#define     FE_AG_REG_AG_MODE_LOP_MODE_5_STATIC                      0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_5_DYNAMIC                     0x20

#define   FE_AG_REG_AG_MODE_LOP_MODE_6__B                            6
#define   FE_AG_REG_AG_MODE_LOP_MODE_6__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_6__M                            0x40
#define     FE_AG_REG_AG_MODE_LOP_MODE_6_AVE_B                       0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_6_AVE_CB                      0x40

#define   FE_AG_REG_AG_MODE_LOP_MODE_7__B                            7
#define   FE_AG_REG_AG_MODE_LOP_MODE_7__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_7__M                            0x80
#define     FE_AG_REG_AG_MODE_LOP_MODE_7_DYNAMIC                     0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_7_STATIC                      0x80

#define   FE_AG_REG_AG_MODE_LOP_MODE_8__B                            8
#define   FE_AG_REG_AG_MODE_LOP_MODE_8__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_8__M                            0x100
#define     FE_AG_REG_AG_MODE_LOP_MODE_8_AVE_B                       0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_8_AVE_CB                      0x100

#define   FE_AG_REG_AG_MODE_LOP_MODE_9__B                            9
#define   FE_AG_REG_AG_MODE_LOP_MODE_9__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_9__M                            0x200
#define     FE_AG_REG_AG_MODE_LOP_MODE_9_STATIC                      0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_9_DYNAMIC                     0x200

#define   FE_AG_REG_AG_MODE_LOP_MODE_A__B                            10
#define   FE_AG_REG_AG_MODE_LOP_MODE_A__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_A__M                            0x400
#define     FE_AG_REG_AG_MODE_LOP_MODE_A_AVE_B                       0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_A_AVE_CB                      0x400

#define   FE_AG_REG_AG_MODE_LOP_MODE_B__B                            11
#define   FE_AG_REG_AG_MODE_LOP_MODE_B__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_B__M                            0x800
#define     FE_AG_REG_AG_MODE_LOP_MODE_B_START                       0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_B_ALWAYS                      0x800

#define   FE_AG_REG_AG_MODE_LOP_MODE_C__B                            12
#define   FE_AG_REG_AG_MODE_LOP_MODE_C__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_C__M                            0x1000
#define     FE_AG_REG_AG_MODE_LOP_MODE_C_STATIC                      0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_C_DYNAMIC                     0x1000

#define   FE_AG_REG_AG_MODE_LOP_MODE_D__B                            13
#define   FE_AG_REG_AG_MODE_LOP_MODE_D__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_D__M                            0x2000
#define     FE_AG_REG_AG_MODE_LOP_MODE_D_START                       0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_D_ALWAYS                      0x2000

#define   FE_AG_REG_AG_MODE_LOP_MODE_E__B                            14
#define   FE_AG_REG_AG_MODE_LOP_MODE_E__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_E__M                            0x4000
#define     FE_AG_REG_AG_MODE_LOP_MODE_E_STATIC                      0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_E_DYNAMIC                     0x4000

#define   FE_AG_REG_AG_MODE_LOP_MODE_F__B                            15
#define   FE_AG_REG_AG_MODE_LOP_MODE_F__W                            1
#define   FE_AG_REG_AG_MODE_LOP_MODE_F__M                            0x8000
#define     FE_AG_REG_AG_MODE_LOP_MODE_F_DISABLE                     0x0
#define     FE_AG_REG_AG_MODE_LOP_MODE_F_ENABLE                      0x8000

#define FE_AG_REG_AG_MODE_HIP__A                                     0xC20011
#define FE_AG_REG_AG_MODE_HIP__W                                     2
#define FE_AG_REG_AG_MODE_HIP__M                                     0x3
#define   FE_AG_REG_AG_MODE_HIP_INIT                                 0x0

#define   FE_AG_REG_AG_MODE_HIP_MODE_G__B                            0
#define   FE_AG_REG_AG_MODE_HIP_MODE_G__W                            1
#define   FE_AG_REG_AG_MODE_HIP_MODE_G__M                            0x1
#define     FE_AG_REG_AG_MODE_HIP_MODE_G_OUTPUT                      0x0
#define     FE_AG_REG_AG_MODE_HIP_MODE_G_ENABLE                      0x1

#define   FE_AG_REG_AG_MODE_HIP_MODE_H__B                            1
#define   FE_AG_REG_AG_MODE_HIP_MODE_H__W                            1
#define   FE_AG_REG_AG_MODE_HIP_MODE_H__M                            0x2
#define     FE_AG_REG_AG_MODE_HIP_MODE_H_OUTPUT                      0x0
#define     FE_AG_REG_AG_MODE_HIP_MODE_H_ENABLE                      0x2

#define FE_AG_REG_AG_PGA_MODE__A                                     0xC20012
#define FE_AG_REG_AG_PGA_MODE__W                                     3
#define FE_AG_REG_AG_PGA_MODE__M                                     0x7
#define   FE_AG_REG_AG_PGA_MODE_INIT                                 0x0
#define   FE_AG_REG_AG_PGA_MODE_PFY_PCY_AFY_REN                      0x0
#define   FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFY_REN                      0x1
#define   FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFN_REN                      0x2
#define   FE_AG_REG_AG_PGA_MODE_PFN_PCY_AFY_REN                      0x3
#define   FE_AG_REG_AG_PGA_MODE_PFY_PCY_AFY_REY                      0x4
#define   FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFY_REY                      0x5
#define   FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFN_REY                      0x6
#define   FE_AG_REG_AG_PGA_MODE_PFN_PCY_AFY_REY                      0x7

#define FE_AG_REG_AG_AGC_SIO__A                                      0xC20013
#define FE_AG_REG_AG_AGC_SIO__W                                      2
#define FE_AG_REG_AG_AGC_SIO__M                                      0x3
#define   FE_AG_REG_AG_AGC_SIO_INIT                                  0x3

#define   FE_AG_REG_AG_AGC_SIO_AGC_SIO_1__B                          0
#define   FE_AG_REG_AG_AGC_SIO_AGC_SIO_1__W                          1
#define   FE_AG_REG_AG_AGC_SIO_AGC_SIO_1__M                          0x1
#define     FE_AG_REG_AG_AGC_SIO_AGC_SIO_1_OUTPUT                    0x0
#define     FE_AG_REG_AG_AGC_SIO_AGC_SIO_1_INPUT                     0x1

#define   FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__B                          1
#define   FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__W                          1
#define   FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__M                          0x2
#define     FE_AG_REG_AG_AGC_SIO_AGC_SIO_2_OUTPUT                    0x0
#define     FE_AG_REG_AG_AGC_SIO_AGC_SIO_2_INPUT                     0x2

#define FE_AG_REG_AG_AGC_USR_DAT__A                                  0xC20014
#define FE_AG_REG_AG_AGC_USR_DAT__W                                  2
#define FE_AG_REG_AG_AGC_USR_DAT__M                                  0x3
#define   FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_1__B                      0
#define   FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_1__W                      1
#define   FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_1__M                      0x1
#define   FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_2__B                      1
#define   FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_2__W                      1
#define   FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_2__M                      0x2

#define FE_AG_REG_AG_PWD__A                                          0xC20015
#define FE_AG_REG_AG_PWD__W                                          5
#define FE_AG_REG_AG_PWD__M                                          0x1F
#define   FE_AG_REG_AG_PWD_INIT                                      0x1F

#define   FE_AG_REG_AG_PWD_PWD_PD1__B                                0
#define   FE_AG_REG_AG_PWD_PWD_PD1__W                                1
#define   FE_AG_REG_AG_PWD_PWD_PD1__M                                0x1
#define     FE_AG_REG_AG_PWD_PWD_PD1_DISABLE                         0x0
#define     FE_AG_REG_AG_PWD_PWD_PD1_ENABLE                          0x1

#define   FE_AG_REG_AG_PWD_PWD_PD2__B                                1
#define   FE_AG_REG_AG_PWD_PWD_PD2__W                                1
#define   FE_AG_REG_AG_PWD_PWD_PD2__M                                0x2
#define     FE_AG_REG_AG_PWD_PWD_PD2_DISABLE                         0x0
#define     FE_AG_REG_AG_PWD_PWD_PD2_ENABLE                          0x2

#define   FE_AG_REG_AG_PWD_PWD_PGA_F__B                              2
#define   FE_AG_REG_AG_PWD_PWD_PGA_F__W                              1
#define   FE_AG_REG_AG_PWD_PWD_PGA_F__M                              0x4
#define     FE_AG_REG_AG_PWD_PWD_PGA_F_DISABLE                       0x0
#define     FE_AG_REG_AG_PWD_PWD_PGA_F_ENABLE                        0x4

#define   FE_AG_REG_AG_PWD_PWD_PGA_C__B                              3
#define   FE_AG_REG_AG_PWD_PWD_PGA_C__W                              1
#define   FE_AG_REG_AG_PWD_PWD_PGA_C__M                              0x8
#define     FE_AG_REG_AG_PWD_PWD_PGA_C_DISABLE                       0x0
#define     FE_AG_REG_AG_PWD_PWD_PGA_C_ENABLE                        0x8

#define   FE_AG_REG_AG_PWD_PWD_AAF__B                                4
#define   FE_AG_REG_AG_PWD_PWD_AAF__W                                1
#define   FE_AG_REG_AG_PWD_PWD_AAF__M                                0x10
#define     FE_AG_REG_AG_PWD_PWD_AAF_DISABLE                         0x0
#define     FE_AG_REG_AG_PWD_PWD_AAF_ENABLE                          0x10

#define FE_AG_REG_DCE_AUR_CNT__A                                     0xC20016
#define FE_AG_REG_DCE_AUR_CNT__W                                     5
#define FE_AG_REG_DCE_AUR_CNT__M                                     0x1F
#define   FE_AG_REG_DCE_AUR_CNT_INIT                                 0x0

#define FE_AG_REG_DCE_RUR_CNT__A                                     0xC20017
#define FE_AG_REG_DCE_RUR_CNT__W                                     5
#define FE_AG_REG_DCE_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_DCE_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_DCE_AVE_DAT__A                                     0xC20018
#define FE_AG_REG_DCE_AVE_DAT__W                                     10
#define FE_AG_REG_DCE_AVE_DAT__M                                     0x3FF

#define FE_AG_REG_DEC_AVE_WRI__A                                     0xC20019
#define FE_AG_REG_DEC_AVE_WRI__W                                     10
#define FE_AG_REG_DEC_AVE_WRI__M                                     0x3FF
#define   FE_AG_REG_DEC_AVE_WRI_INIT                                 0x0

#define FE_AG_REG_ACE_AUR_CNT__A                                     0xC2001A
#define FE_AG_REG_ACE_AUR_CNT__W                                     5
#define FE_AG_REG_ACE_AUR_CNT__M                                     0x1F
#define   FE_AG_REG_ACE_AUR_CNT_INIT                                 0x0

#define FE_AG_REG_ACE_RUR_CNT__A                                     0xC2001B
#define FE_AG_REG_ACE_RUR_CNT__W                                     5
#define FE_AG_REG_ACE_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_ACE_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_ACE_AVE_DAT__A                                     0xC2001C
#define FE_AG_REG_ACE_AVE_DAT__W                                     10
#define FE_AG_REG_ACE_AVE_DAT__M                                     0x3FF

#define FE_AG_REG_AEC_AVE_INC__A                                     0xC2001D
#define FE_AG_REG_AEC_AVE_INC__W                                     10
#define FE_AG_REG_AEC_AVE_INC__M                                     0x3FF
#define   FE_AG_REG_AEC_AVE_INC_INIT                                 0x0

#define FE_AG_REG_AEC_AVE_DAT__A                                     0xC2001E
#define FE_AG_REG_AEC_AVE_DAT__W                                     10
#define FE_AG_REG_AEC_AVE_DAT__M                                     0x3FF

#define FE_AG_REG_AEC_CLP_LVL__A                                     0xC2001F
#define FE_AG_REG_AEC_CLP_LVL__W                                     16
#define FE_AG_REG_AEC_CLP_LVL__M                                     0xFFFF
#define   FE_AG_REG_AEC_CLP_LVL_INIT                                 0x0

#define FE_AG_REG_CDR_RUR_CNT__A                                     0xC20020
#define FE_AG_REG_CDR_RUR_CNT__W                                     5
#define FE_AG_REG_CDR_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_CDR_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_CDR_CLP_DAT__A                                     0xC20021
#define FE_AG_REG_CDR_CLP_DAT__W                                     16
#define FE_AG_REG_CDR_CLP_DAT__M                                     0xFFFF

#define FE_AG_REG_CDR_CLP_POS__A                                     0xC20022
#define FE_AG_REG_CDR_CLP_POS__W                                     10
#define FE_AG_REG_CDR_CLP_POS__M                                     0x3FF
#define   FE_AG_REG_CDR_CLP_POS_INIT                                 0x0

#define FE_AG_REG_CDR_CLP_NEG__A                                     0xC20023
#define FE_AG_REG_CDR_CLP_NEG__W                                     10
#define FE_AG_REG_CDR_CLP_NEG__M                                     0x3FF
#define   FE_AG_REG_CDR_CLP_NEG_INIT                                 0x0

#define FE_AG_REG_EGC_RUR_CNT__A                                     0xC20024
#define FE_AG_REG_EGC_RUR_CNT__W                                     5
#define FE_AG_REG_EGC_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_EGC_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_EGC_SET_LVL__A                                     0xC20025
#define FE_AG_REG_EGC_SET_LVL__W                                     9
#define FE_AG_REG_EGC_SET_LVL__M                                     0x1FF
#define   FE_AG_REG_EGC_SET_LVL_INIT                                 0x0

#define FE_AG_REG_EGC_FLA_RGN__A                                     0xC20026
#define FE_AG_REG_EGC_FLA_RGN__W                                     9
#define FE_AG_REG_EGC_FLA_RGN__M                                     0x1FF
#define   FE_AG_REG_EGC_FLA_RGN_INIT                                 0x0

#define FE_AG_REG_EGC_SLO_RGN__A                                     0xC20027
#define FE_AG_REG_EGC_SLO_RGN__W                                     9
#define FE_AG_REG_EGC_SLO_RGN__M                                     0x1FF
#define   FE_AG_REG_EGC_SLO_RGN_INIT                                 0x0

#define FE_AG_REG_EGC_JMP_PSN__A                                     0xC20028
#define FE_AG_REG_EGC_JMP_PSN__W                                     4
#define FE_AG_REG_EGC_JMP_PSN__M                                     0xF
#define   FE_AG_REG_EGC_JMP_PSN_INIT                                 0x0

#define FE_AG_REG_EGC_FLA_INC__A                                     0xC20029
#define FE_AG_REG_EGC_FLA_INC__W                                     16
#define FE_AG_REG_EGC_FLA_INC__M                                     0xFFFF
#define   FE_AG_REG_EGC_FLA_INC_INIT                                 0x0

#define FE_AG_REG_EGC_FLA_DEC__A                                     0xC2002A
#define FE_AG_REG_EGC_FLA_DEC__W                                     16
#define FE_AG_REG_EGC_FLA_DEC__M                                     0xFFFF
#define   FE_AG_REG_EGC_FLA_DEC_INIT                                 0x0

#define FE_AG_REG_EGC_SLO_INC__A                                     0xC2002B
#define FE_AG_REG_EGC_SLO_INC__W                                     16
#define FE_AG_REG_EGC_SLO_INC__M                                     0xFFFF
#define   FE_AG_REG_EGC_SLO_INC_INIT                                 0x0

#define FE_AG_REG_EGC_SLO_DEC__A                                     0xC2002C
#define FE_AG_REG_EGC_SLO_DEC__W                                     16
#define FE_AG_REG_EGC_SLO_DEC__M                                     0xFFFF
#define   FE_AG_REG_EGC_SLO_DEC_INIT                                 0x0

#define FE_AG_REG_EGC_FAS_INC__A                                     0xC2002D
#define FE_AG_REG_EGC_FAS_INC__W                                     16
#define FE_AG_REG_EGC_FAS_INC__M                                     0xFFFF
#define   FE_AG_REG_EGC_FAS_INC_INIT                                 0x0

#define FE_AG_REG_EGC_FAS_DEC__A                                     0xC2002E
#define FE_AG_REG_EGC_FAS_DEC__W                                     16
#define FE_AG_REG_EGC_FAS_DEC__M                                     0xFFFF
#define   FE_AG_REG_EGC_FAS_DEC_INIT                                 0x0

#define FE_AG_REG_EGC_MAP_DAT__A                                     0xC2002F
#define FE_AG_REG_EGC_MAP_DAT__W                                     16
#define FE_AG_REG_EGC_MAP_DAT__M                                     0xFFFF

#define FE_AG_REG_PM1_AGC_WRI__A                                     0xC20030
#define FE_AG_REG_PM1_AGC_WRI__W                                     11
#define FE_AG_REG_PM1_AGC_WRI__M                                     0x7FF
#define   FE_AG_REG_PM1_AGC_WRI_INIT                                 0x0

#define FE_AG_REG_GC1_AGC_RIC__A                                     0xC20031
#define FE_AG_REG_GC1_AGC_RIC__W                                     16
#define FE_AG_REG_GC1_AGC_RIC__M                                     0xFFFF
#define   FE_AG_REG_GC1_AGC_RIC_INIT                                 0x0

#define FE_AG_REG_GC1_AGC_OFF__A                                     0xC20032
#define FE_AG_REG_GC1_AGC_OFF__W                                     16
#define FE_AG_REG_GC1_AGC_OFF__M                                     0xFFFF
#define   FE_AG_REG_GC1_AGC_OFF_INIT                                 0x0

#define FE_AG_REG_GC1_AGC_MAX__A                                     0xC20033
#define FE_AG_REG_GC1_AGC_MAX__W                                     10
#define FE_AG_REG_GC1_AGC_MAX__M                                     0x3FF
#define   FE_AG_REG_GC1_AGC_MAX_INIT                                 0x0

#define FE_AG_REG_GC1_AGC_MIN__A                                     0xC20034
#define FE_AG_REG_GC1_AGC_MIN__W                                     10
#define FE_AG_REG_GC1_AGC_MIN__M                                     0x3FF
#define   FE_AG_REG_GC1_AGC_MIN_INIT                                 0x0

#define FE_AG_REG_GC1_AGC_DAT__A                                     0xC20035
#define FE_AG_REG_GC1_AGC_DAT__W                                     10
#define FE_AG_REG_GC1_AGC_DAT__M                                     0x3FF

#define FE_AG_REG_PM2_AGC_WRI__A                                     0xC20036
#define FE_AG_REG_PM2_AGC_WRI__W                                     11
#define FE_AG_REG_PM2_AGC_WRI__M                                     0x7FF
#define   FE_AG_REG_PM2_AGC_WRI_INIT                                 0x0

#define FE_AG_REG_GC2_AGC_RIC__A                                     0xC20037
#define FE_AG_REG_GC2_AGC_RIC__W                                     16
#define FE_AG_REG_GC2_AGC_RIC__M                                     0xFFFF
#define   FE_AG_REG_GC2_AGC_RIC_INIT                                 0x0

#define FE_AG_REG_GC2_AGC_OFF__A                                     0xC20038
#define FE_AG_REG_GC2_AGC_OFF__W                                     16
#define FE_AG_REG_GC2_AGC_OFF__M                                     0xFFFF
#define   FE_AG_REG_GC2_AGC_OFF_INIT                                 0x0

#define FE_AG_REG_GC2_AGC_MAX__A                                     0xC20039
#define FE_AG_REG_GC2_AGC_MAX__W                                     10
#define FE_AG_REG_GC2_AGC_MAX__M                                     0x3FF
#define   FE_AG_REG_GC2_AGC_MAX_INIT                                 0x0

#define FE_AG_REG_GC2_AGC_MIN__A                                     0xC2003A
#define FE_AG_REG_GC2_AGC_MIN__W                                     10
#define FE_AG_REG_GC2_AGC_MIN__M                                     0x3FF
#define   FE_AG_REG_GC2_AGC_MIN_INIT                                 0x0

#define FE_AG_REG_GC2_AGC_DAT__A                                     0xC2003B
#define FE_AG_REG_GC2_AGC_DAT__W                                     10
#define FE_AG_REG_GC2_AGC_DAT__M                                     0x3FF

#define FE_AG_REG_IND_WIN__A                                         0xC2003C
#define FE_AG_REG_IND_WIN__W                                         5
#define FE_AG_REG_IND_WIN__M                                         0x1F
#define   FE_AG_REG_IND_WIN_INIT                                     0x0

#define FE_AG_REG_IND_THD_LOL__A                                     0xC2003D
#define FE_AG_REG_IND_THD_LOL__W                                     6
#define FE_AG_REG_IND_THD_LOL__M                                     0x3F
#define   FE_AG_REG_IND_THD_LOL_INIT                                 0x0

#define FE_AG_REG_IND_THD_HIL__A                                     0xC2003E
#define FE_AG_REG_IND_THD_HIL__W                                     6
#define FE_AG_REG_IND_THD_HIL__M                                     0x3F
#define   FE_AG_REG_IND_THD_HIL_INIT                                 0x0

#define FE_AG_REG_IND_DEL__A                                         0xC2003F
#define FE_AG_REG_IND_DEL__W                                         7
#define FE_AG_REG_IND_DEL__M                                         0x7F
#define   FE_AG_REG_IND_DEL_INIT                                     0x0

#define FE_AG_REG_IND_PD1_WRI__A                                     0xC20040
#define FE_AG_REG_IND_PD1_WRI__W                                     6
#define FE_AG_REG_IND_PD1_WRI__M                                     0x3F
#define   FE_AG_REG_IND_PD1_WRI_INIT                                 0x1F

#define FE_AG_REG_PDA_AUR_CNT__A                                     0xC20041
#define FE_AG_REG_PDA_AUR_CNT__W                                     5
#define FE_AG_REG_PDA_AUR_CNT__M                                     0x1F
#define   FE_AG_REG_PDA_AUR_CNT_INIT                                 0x0

#define FE_AG_REG_PDA_RUR_CNT__A                                     0xC20042
#define FE_AG_REG_PDA_RUR_CNT__W                                     5
#define FE_AG_REG_PDA_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_PDA_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_PDA_AVE_DAT__A                                     0xC20043
#define FE_AG_REG_PDA_AVE_DAT__W                                     6
#define FE_AG_REG_PDA_AVE_DAT__M                                     0x3F

#define FE_AG_REG_PDC_RUR_CNT__A                                     0xC20044
#define FE_AG_REG_PDC_RUR_CNT__W                                     5
#define FE_AG_REG_PDC_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_PDC_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_PDC_SET_LVL__A                                     0xC20045
#define FE_AG_REG_PDC_SET_LVL__W                                     6
#define FE_AG_REG_PDC_SET_LVL__M                                     0x3F
#define   FE_AG_REG_PDC_SET_LVL_INIT                                 0x10

#define FE_AG_REG_PDC_FLA_RGN__A                                     0xC20046
#define FE_AG_REG_PDC_FLA_RGN__W                                     6
#define FE_AG_REG_PDC_FLA_RGN__M                                     0x3F
#define   FE_AG_REG_PDC_FLA_RGN_INIT                                 0x0

#define FE_AG_REG_PDC_JMP_PSN__A                                     0xC20047
#define FE_AG_REG_PDC_JMP_PSN__W                                     3
#define FE_AG_REG_PDC_JMP_PSN__M                                     0x7
#define   FE_AG_REG_PDC_JMP_PSN_INIT                                 0x0

#define FE_AG_REG_PDC_FLA_STP__A                                     0xC20048
#define FE_AG_REG_PDC_FLA_STP__W                                     16
#define FE_AG_REG_PDC_FLA_STP__M                                     0xFFFF
#define   FE_AG_REG_PDC_FLA_STP_INIT                                 0x0

#define FE_AG_REG_PDC_SLO_STP__A                                     0xC20049
#define FE_AG_REG_PDC_SLO_STP__W                                     16
#define FE_AG_REG_PDC_SLO_STP__M                                     0xFFFF
#define   FE_AG_REG_PDC_SLO_STP_INIT                                 0x0

#define FE_AG_REG_PDC_PD2_WRI__A                                     0xC2004A
#define FE_AG_REG_PDC_PD2_WRI__W                                     6
#define FE_AG_REG_PDC_PD2_WRI__M                                     0x3F
#define   FE_AG_REG_PDC_PD2_WRI_INIT                                 0x0

#define FE_AG_REG_PDC_MAP_DAT__A                                     0xC2004B
#define FE_AG_REG_PDC_MAP_DAT__W                                     6
#define FE_AG_REG_PDC_MAP_DAT__M                                     0x3F

#define FE_AG_REG_PDC_MAX__A                                         0xC2004C
#define FE_AG_REG_PDC_MAX__W                                         6
#define FE_AG_REG_PDC_MAX__M                                         0x3F
#define   FE_AG_REG_PDC_MAX_INIT                                     0x2

#define FE_AG_REG_TGA_AUR_CNT__A                                     0xC2004D
#define FE_AG_REG_TGA_AUR_CNT__W                                     5
#define FE_AG_REG_TGA_AUR_CNT__M                                     0x1F
#define   FE_AG_REG_TGA_AUR_CNT_INIT                                 0x0

#define FE_AG_REG_TGA_RUR_CNT__A                                     0xC2004E
#define FE_AG_REG_TGA_RUR_CNT__W                                     5
#define FE_AG_REG_TGA_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_TGA_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_TGA_AVE_DAT__A                                     0xC2004F
#define FE_AG_REG_TGA_AVE_DAT__W                                     6
#define FE_AG_REG_TGA_AVE_DAT__M                                     0x3F

#define FE_AG_REG_TGC_RUR_CNT__A                                     0xC20050
#define FE_AG_REG_TGC_RUR_CNT__W                                     5
#define FE_AG_REG_TGC_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_TGC_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_TGC_SET_LVL__A                                     0xC20051
#define FE_AG_REG_TGC_SET_LVL__W                                     6
#define FE_AG_REG_TGC_SET_LVL__M                                     0x3F
#define   FE_AG_REG_TGC_SET_LVL_INIT                                 0x0

#define FE_AG_REG_TGC_FLA_RGN__A                                     0xC20052
#define FE_AG_REG_TGC_FLA_RGN__W                                     6
#define FE_AG_REG_TGC_FLA_RGN__M                                     0x3F
#define   FE_AG_REG_TGC_FLA_RGN_INIT                                 0x0

#define FE_AG_REG_TGC_JMP_PSN__A                                     0xC20053
#define FE_AG_REG_TGC_JMP_PSN__W                                     4
#define FE_AG_REG_TGC_JMP_PSN__M                                     0xF
#define   FE_AG_REG_TGC_JMP_PSN_INIT                                 0x0

#define FE_AG_REG_TGC_FLA_STP__A                                     0xC20054
#define FE_AG_REG_TGC_FLA_STP__W                                     16
#define FE_AG_REG_TGC_FLA_STP__M                                     0xFFFF
#define   FE_AG_REG_TGC_FLA_STP_INIT                                 0x0

#define FE_AG_REG_TGC_SLO_STP__A                                     0xC20055
#define FE_AG_REG_TGC_SLO_STP__W                                     16
#define FE_AG_REG_TGC_SLO_STP__M                                     0xFFFF
#define   FE_AG_REG_TGC_SLO_STP_INIT                                 0x0

#define FE_AG_REG_TGC_MAP_DAT__A                                     0xC20056
#define FE_AG_REG_TGC_MAP_DAT__W                                     10
#define FE_AG_REG_TGC_MAP_DAT__M                                     0x3FF

#define FE_AG_REG_FGA_AUR_CNT__A                                     0xC20057
#define FE_AG_REG_FGA_AUR_CNT__W                                     5
#define FE_AG_REG_FGA_AUR_CNT__M                                     0x1F
#define   FE_AG_REG_FGA_AUR_CNT_INIT                                 0x0

#define FE_AG_REG_FGA_RUR_CNT__A                                     0xC20058
#define FE_AG_REG_FGA_RUR_CNT__W                                     5
#define FE_AG_REG_FGA_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_FGA_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_FGA_AVE_DAT__A                                     0xC20059
#define FE_AG_REG_FGA_AVE_DAT__W                                     10
#define FE_AG_REG_FGA_AVE_DAT__M                                     0x3FF

#define FE_AG_REG_FGC_RUR_CNT__A                                     0xC2005A
#define FE_AG_REG_FGC_RUR_CNT__W                                     5
#define FE_AG_REG_FGC_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_FGC_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_FGC_SET_LVL__A                                     0xC2005B
#define FE_AG_REG_FGC_SET_LVL__W                                     9
#define FE_AG_REG_FGC_SET_LVL__M                                     0x1FF
#define   FE_AG_REG_FGC_SET_LVL_INIT                                 0x0

#define FE_AG_REG_FGC_FLA_RGN__A                                     0xC2005C
#define FE_AG_REG_FGC_FLA_RGN__W                                     9
#define FE_AG_REG_FGC_FLA_RGN__M                                     0x1FF
#define   FE_AG_REG_FGC_FLA_RGN_INIT                                 0x0

#define FE_AG_REG_FGC_JMP_PSN__A                                     0xC2005D
#define FE_AG_REG_FGC_JMP_PSN__W                                     4
#define FE_AG_REG_FGC_JMP_PSN__M                                     0xF
#define   FE_AG_REG_FGC_JMP_PSN_INIT                                 0x0

#define FE_AG_REG_FGC_FLA_STP__A                                     0xC2005E
#define FE_AG_REG_FGC_FLA_STP__W                                     16
#define FE_AG_REG_FGC_FLA_STP__M                                     0xFFFF
#define   FE_AG_REG_FGC_FLA_STP_INIT                                 0x0

#define FE_AG_REG_FGC_SLO_STP__A                                     0xC2005F
#define FE_AG_REG_FGC_SLO_STP__W                                     16
#define FE_AG_REG_FGC_SLO_STP__M                                     0xFFFF
#define   FE_AG_REG_FGC_SLO_STP_INIT                                 0x0

#define FE_AG_REG_FGC_MAP_DAT__A                                     0xC20060
#define FE_AG_REG_FGC_MAP_DAT__W                                     10
#define FE_AG_REG_FGC_MAP_DAT__M                                     0x3FF

#define FE_AG_REG_FGM_WRI__A                                         0xC20061
#define FE_AG_REG_FGM_WRI__W                                         10
#define FE_AG_REG_FGM_WRI__M                                         0x3FF
#define   FE_AG_REG_FGM_WRI_INIT                                     0x20

#define FE_AG_REG_BGC_RUR_CNT__A                                     0xC20062
#define FE_AG_REG_BGC_RUR_CNT__W                                     5
#define FE_AG_REG_BGC_RUR_CNT__M                                     0x1F
#define   FE_AG_REG_BGC_RUR_CNT_INIT                                 0x0

#define FE_AG_REG_BGC_SET_LVL__A                                     0xC20063
#define FE_AG_REG_BGC_SET_LVL__W                                     9
#define FE_AG_REG_BGC_SET_LVL__M                                     0x1FF
#define   FE_AG_REG_BGC_SET_LVL_INIT                                 0x0

#define FE_AG_REG_BGC_FLA_RGN__A                                     0xC20064
#define FE_AG_REG_BGC_FLA_RGN__W                                     9
#define FE_AG_REG_BGC_FLA_RGN__M                                     0x1FF
#define   FE_AG_REG_BGC_FLA_RGN_INIT                                 0x0

#define FE_AG_REG_BGC_JMP_PSN__A                                     0xC20065
#define FE_AG_REG_BGC_JMP_PSN__W                                     4
#define FE_AG_REG_BGC_JMP_PSN__M                                     0xF
#define   FE_AG_REG_BGC_JMP_PSN_INIT                                 0x0

#define FE_AG_REG_BGC_FLA_STP__A                                     0xC20066
#define FE_AG_REG_BGC_FLA_STP__W                                     16
#define FE_AG_REG_BGC_FLA_STP__M                                     0xFFFF
#define   FE_AG_REG_BGC_FLA_STP_INIT                                 0x0

#define FE_AG_REG_BGC_SLO_STP__A                                     0xC20067
#define FE_AG_REG_BGC_SLO_STP__W                                     16
#define FE_AG_REG_BGC_SLO_STP__M                                     0xFFFF
#define   FE_AG_REG_BGC_SLO_STP_INIT                                 0x0

#define FE_AG_REG_BGC_FGC_WRI__A                                     0xC20068
#define FE_AG_REG_BGC_FGC_WRI__W                                     4
#define FE_AG_REG_BGC_FGC_WRI__M                                     0xF
#define   FE_AG_REG_BGC_FGC_WRI_INIT                                 0x7

#define FE_AG_REG_BGC_CGC_WRI__A                                     0xC20069
#define FE_AG_REG_BGC_CGC_WRI__W                                     2
#define FE_AG_REG_BGC_CGC_WRI__M                                     0x3
#define   FE_AG_REG_BGC_CGC_WRI_INIT                                 0x1

#define FE_AG_REG_BGC_FGC_DAT__A                                     0xC2006A
#define FE_AG_REG_BGC_FGC_DAT__W                                     4
#define FE_AG_REG_BGC_FGC_DAT__M                                     0xF

#define FE_FS_SID                                                    0x3

#define FE_FS_REG_COMM_EXEC__A                                       0xC30000
#define FE_FS_REG_COMM_EXEC__W                                       3
#define FE_FS_REG_COMM_EXEC__M                                       0x7
#define   FE_FS_REG_COMM_EXEC_CTL__B                                 0
#define   FE_FS_REG_COMM_EXEC_CTL__W                                 3
#define   FE_FS_REG_COMM_EXEC_CTL__M                                 0x7
#define     FE_FS_REG_COMM_EXEC_CTL_STOP                             0x0
#define     FE_FS_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     FE_FS_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     FE_FS_REG_COMM_EXEC_CTL_STEP                             0x3

#define FE_FS_REG_COMM_STATE__A                                      0xC30001
#define FE_FS_REG_COMM_STATE__W                                      4
#define FE_FS_REG_COMM_STATE__M                                      0xF

#define FE_FS_REG_COMM_MB__A                                         0xC30002
#define FE_FS_REG_COMM_MB__W                                         3
#define FE_FS_REG_COMM_MB__M                                         0x7
#define   FE_FS_REG_COMM_MB_CTR__B                                   0
#define   FE_FS_REG_COMM_MB_CTR__W                                   1
#define   FE_FS_REG_COMM_MB_CTR__M                                   0x1
#define     FE_FS_REG_COMM_MB_CTR_OFF                                0x0
#define     FE_FS_REG_COMM_MB_CTR_ON                                 0x1
#define   FE_FS_REG_COMM_MB_OBS__B                                   1
#define   FE_FS_REG_COMM_MB_OBS__W                                   1
#define   FE_FS_REG_COMM_MB_OBS__M                                   0x2
#define     FE_FS_REG_COMM_MB_OBS_OFF                                0x0
#define     FE_FS_REG_COMM_MB_OBS_ON                                 0x2
#define   FE_FS_REG_COMM_MB_MUX__B                                   2
#define   FE_FS_REG_COMM_MB_MUX__W                                   1
#define   FE_FS_REG_COMM_MB_MUX__M                                   0x4
#define     FE_FS_REG_COMM_MB_MUX_REAL                               0x0
#define     FE_FS_REG_COMM_MB_MUX_IMAG                               0x4

#define FE_FS_REG_COMM_SERVICE0__A                                   0xC30003
#define FE_FS_REG_COMM_SERVICE0__W                                   10
#define FE_FS_REG_COMM_SERVICE0__M                                   0x3FF

#define FE_FS_REG_COMM_SERVICE1__A                                   0xC30004
#define FE_FS_REG_COMM_SERVICE1__W                                   11
#define FE_FS_REG_COMM_SERVICE1__M                                   0x7FF

#define FE_FS_REG_COMM_ACT__A                                        0xC30005
#define FE_FS_REG_COMM_ACT__W                                        2
#define FE_FS_REG_COMM_ACT__M                                        0x3

#define FE_FS_REG_COMM_CNT__A                                        0xC30006
#define FE_FS_REG_COMM_CNT__W                                        16
#define FE_FS_REG_COMM_CNT__M                                        0xFFFF

#define FE_FS_REG_ADD_INC_LOP__A                                     0xC30010
#define FE_FS_REG_ADD_INC_LOP__W                                     16
#define FE_FS_REG_ADD_INC_LOP__M                                     0xFFFF
#define   FE_FS_REG_ADD_INC_LOP_INIT                                 0x0

#define FE_FS_REG_ADD_INC_HIP__A                                     0xC30011
#define FE_FS_REG_ADD_INC_HIP__W                                     12
#define FE_FS_REG_ADD_INC_HIP__M                                     0xFFF
#define   FE_FS_REG_ADD_INC_HIP_INIT                                 0x0

#define FE_FS_REG_ADD_OFF__A                                         0xC30012
#define FE_FS_REG_ADD_OFF__W                                         12
#define FE_FS_REG_ADD_OFF__M                                         0xFFF
#define   FE_FS_REG_ADD_OFF_INIT                                     0x0

#define FE_FS_REG_ADD_OFF_VAL__A                                     0xC30013
#define FE_FS_REG_ADD_OFF_VAL__W                                     1
#define FE_FS_REG_ADD_OFF_VAL__M                                     0x1
#define   FE_FS_REG_ADD_OFF_VAL_INIT                                 0x0

#define FE_FD_SID                                                    0x4

#define FE_FD_REG_COMM_EXEC__A                                       0xC40000
#define FE_FD_REG_COMM_EXEC__W                                       3
#define FE_FD_REG_COMM_EXEC__M                                       0x7
#define   FE_FD_REG_COMM_EXEC_CTL__B                                 0
#define   FE_FD_REG_COMM_EXEC_CTL__W                                 3
#define   FE_FD_REG_COMM_EXEC_CTL__M                                 0x7
#define     FE_FD_REG_COMM_EXEC_CTL_STOP                             0x0
#define     FE_FD_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     FE_FD_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     FE_FD_REG_COMM_EXEC_CTL_STEP                             0x3

#define FE_FD_REG_COMM_MB__A                                         0xC40002
#define FE_FD_REG_COMM_MB__W                                         3
#define FE_FD_REG_COMM_MB__M                                         0x7
#define   FE_FD_REG_COMM_MB_CTR__B                                   0
#define   FE_FD_REG_COMM_MB_CTR__W                                   1
#define   FE_FD_REG_COMM_MB_CTR__M                                   0x1
#define     FE_FD_REG_COMM_MB_CTR_OFF                                0x0
#define     FE_FD_REG_COMM_MB_CTR_ON                                 0x1
#define   FE_FD_REG_COMM_MB_OBS__B                                   1
#define   FE_FD_REG_COMM_MB_OBS__W                                   1
#define   FE_FD_REG_COMM_MB_OBS__M                                   0x2
#define     FE_FD_REG_COMM_MB_OBS_OFF                                0x0
#define     FE_FD_REG_COMM_MB_OBS_ON                                 0x2

#define FE_FD_REG_COMM_SERVICE0__A                                   0xC40003
#define FE_FD_REG_COMM_SERVICE0__W                                   10
#define FE_FD_REG_COMM_SERVICE0__M                                   0x3FF
#define FE_FD_REG_COMM_SERVICE1__A                                   0xC40004
#define FE_FD_REG_COMM_SERVICE1__W                                   11
#define FE_FD_REG_COMM_SERVICE1__M                                   0x7FF

#define FE_FD_REG_COMM_INT_STA__A                                    0xC40007
#define FE_FD_REG_COMM_INT_STA__W                                    1
#define FE_FD_REG_COMM_INT_STA__M                                    0x1
#define   FE_FD_REG_COMM_INT_STA_NEW_MEAS__B                         0
#define   FE_FD_REG_COMM_INT_STA_NEW_MEAS__W                         1
#define   FE_FD_REG_COMM_INT_STA_NEW_MEAS__M                         0x1

#define FE_FD_REG_COMM_INT_MSK__A                                    0xC40008
#define FE_FD_REG_COMM_INT_MSK__W                                    1
#define FE_FD_REG_COMM_INT_MSK__M                                    0x1
#define   FE_FD_REG_COMM_INT_MSK_NEW_MEAS__B                         0
#define   FE_FD_REG_COMM_INT_MSK_NEW_MEAS__W                         1
#define   FE_FD_REG_COMM_INT_MSK_NEW_MEAS__M                         0x1

#define FE_FD_REG_SCL__A                                             0xC40010
#define FE_FD_REG_SCL__W                                             6
#define FE_FD_REG_SCL__M                                             0x3F

#define FE_FD_REG_MAX_LEV__A                                         0xC40011
#define FE_FD_REG_MAX_LEV__W                                         3
#define FE_FD_REG_MAX_LEV__M                                         0x7

#define FE_FD_REG_NR__A                                              0xC40012
#define FE_FD_REG_NR__W                                              5
#define FE_FD_REG_NR__M                                              0x1F

#define FE_FD_REG_MEAS_SEL__A                                        0xC40013
#define FE_FD_REG_MEAS_SEL__W                                        1
#define FE_FD_REG_MEAS_SEL__M                                        0x1

#define FE_FD_REG_MEAS_VAL__A                                        0xC40014
#define FE_FD_REG_MEAS_VAL__W                                        1
#define FE_FD_REG_MEAS_VAL__M                                        0x1

#define FE_FD_REG_MAX__A                                             0xC40015
#define FE_FD_REG_MAX__W                                             16
#define FE_FD_REG_MAX__M                                             0xFFFF

#define FE_FD_REG_POWER__A                                           0xC40016
#define FE_FD_REG_POWER__W                                           10
#define FE_FD_REG_POWER__M                                           0x3FF

#define FE_IF_SID                                                    0x5

#define FE_IF_REG_COMM_EXEC__A                                       0xC50000
#define FE_IF_REG_COMM_EXEC__W                                       3
#define FE_IF_REG_COMM_EXEC__M                                       0x7
#define   FE_IF_REG_COMM_EXEC_CTL__B                                 0
#define   FE_IF_REG_COMM_EXEC_CTL__W                                 3
#define   FE_IF_REG_COMM_EXEC_CTL__M                                 0x7
#define     FE_IF_REG_COMM_EXEC_CTL_STOP                             0x0
#define     FE_IF_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     FE_IF_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     FE_IF_REG_COMM_EXEC_CTL_STEP                             0x3

#define FE_IF_REG_COMM_MB__A                                         0xC50002
#define FE_IF_REG_COMM_MB__W                                         3
#define FE_IF_REG_COMM_MB__M                                         0x7
#define   FE_IF_REG_COMM_MB_CTR__B                                   0
#define   FE_IF_REG_COMM_MB_CTR__W                                   1
#define   FE_IF_REG_COMM_MB_CTR__M                                   0x1
#define     FE_IF_REG_COMM_MB_CTR_OFF                                0x0
#define     FE_IF_REG_COMM_MB_CTR_ON                                 0x1
#define   FE_IF_REG_COMM_MB_OBS__B                                   1
#define   FE_IF_REG_COMM_MB_OBS__W                                   1
#define   FE_IF_REG_COMM_MB_OBS__M                                   0x2
#define     FE_IF_REG_COMM_MB_OBS_OFF                                0x0
#define     FE_IF_REG_COMM_MB_OBS_ON                                 0x2

#define FE_IF_REG_INCR0__A                                           0xC50010
#define FE_IF_REG_INCR0__W                                           16
#define FE_IF_REG_INCR0__M                                           0xFFFF
#define   FE_IF_REG_INCR0_INIT                                       0x0

#define FE_IF_REG_INCR1__A                                           0xC50011
#define FE_IF_REG_INCR1__W                                           8
#define FE_IF_REG_INCR1__M                                           0xFF
#define   FE_IF_REG_INCR1_INIT                                       0x28

#define FE_CF_SID                                                    0x6

#define FE_CF_REG_COMM_EXEC__A                                       0xC60000
#define FE_CF_REG_COMM_EXEC__W                                       3
#define FE_CF_REG_COMM_EXEC__M                                       0x7
#define   FE_CF_REG_COMM_EXEC_CTL__B                                 0
#define   FE_CF_REG_COMM_EXEC_CTL__W                                 3
#define   FE_CF_REG_COMM_EXEC_CTL__M                                 0x7
#define     FE_CF_REG_COMM_EXEC_CTL_STOP                             0x0
#define     FE_CF_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     FE_CF_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     FE_CF_REG_COMM_EXEC_CTL_STEP                             0x3

#define FE_CF_REG_COMM_MB__A                                         0xC60002
#define FE_CF_REG_COMM_MB__W                                         3
#define FE_CF_REG_COMM_MB__M                                         0x7
#define   FE_CF_REG_COMM_MB_CTR__B                                   0
#define   FE_CF_REG_COMM_MB_CTR__W                                   1
#define   FE_CF_REG_COMM_MB_CTR__M                                   0x1
#define     FE_CF_REG_COMM_MB_CTR_OFF                                0x0
#define     FE_CF_REG_COMM_MB_CTR_ON                                 0x1
#define   FE_CF_REG_COMM_MB_OBS__B                                   1
#define   FE_CF_REG_COMM_MB_OBS__W                                   1
#define   FE_CF_REG_COMM_MB_OBS__M                                   0x2
#define     FE_CF_REG_COMM_MB_OBS_OFF                                0x0
#define     FE_CF_REG_COMM_MB_OBS_ON                                 0x2

#define FE_CF_REG_COMM_SERVICE0__A                                   0xC60003
#define FE_CF_REG_COMM_SERVICE0__W                                   10
#define FE_CF_REG_COMM_SERVICE0__M                                   0x3FF
#define FE_CF_REG_COMM_SERVICE1__A                                   0xC60004
#define FE_CF_REG_COMM_SERVICE1__W                                   11
#define FE_CF_REG_COMM_SERVICE1__M                                   0x7FF

#define FE_CF_REG_COMM_INT_STA__A                                    0xC60007
#define FE_CF_REG_COMM_INT_STA__W                                    2
#define FE_CF_REG_COMM_INT_STA__M                                    0x3
#define   FE_CF_REG_COMM_INT_STA_NEW_MEAS__B                         0
#define   FE_CF_REG_COMM_INT_STA_NEW_MEAS__W                         1
#define   FE_CF_REG_COMM_INT_STA_NEW_MEAS__M                         0x1

#define FE_CF_REG_COMM_INT_MSK__A                                    0xC60008
#define FE_CF_REG_COMM_INT_MSK__W                                    2
#define FE_CF_REG_COMM_INT_MSK__M                                    0x3
#define   FE_CF_REG_COMM_INT_MSK_NEW_MEAS__B                         0
#define   FE_CF_REG_COMM_INT_MSK_NEW_MEAS__W                         1
#define   FE_CF_REG_COMM_INT_MSK_NEW_MEAS__M                         0x1

#define FE_CF_REG_SCL__A                                             0xC60010
#define FE_CF_REG_SCL__W                                             9
#define FE_CF_REG_SCL__M                                             0x1FF

#define FE_CF_REG_MAX_LEV__A                                         0xC60011
#define FE_CF_REG_MAX_LEV__W                                         3
#define FE_CF_REG_MAX_LEV__M                                         0x7

#define FE_CF_REG_NR__A                                              0xC60012
#define FE_CF_REG_NR__W                                              5
#define FE_CF_REG_NR__M                                              0x1F

#define FE_CF_REG_IMP_VAL__A                                         0xC60013
#define FE_CF_REG_IMP_VAL__W                                         1
#define FE_CF_REG_IMP_VAL__M                                         0x1

#define FE_CF_REG_MEAS_VAL__A                                        0xC60014
#define FE_CF_REG_MEAS_VAL__W                                        1
#define FE_CF_REG_MEAS_VAL__M                                        0x1

#define FE_CF_REG_MAX__A                                             0xC60015
#define FE_CF_REG_MAX__W                                             16
#define FE_CF_REG_MAX__M                                             0xFFFF

#define FE_CF_REG_POWER__A                                           0xC60016
#define FE_CF_REG_POWER__W                                           10
#define FE_CF_REG_POWER__M                                           0x3FF

#define FE_CU_SID                                                    0x7

#define FE_CU_REG_COMM_EXEC__A                                       0xC70000
#define FE_CU_REG_COMM_EXEC__W                                       3
#define FE_CU_REG_COMM_EXEC__M                                       0x7
#define   FE_CU_REG_COMM_EXEC_CTL__B                                 0
#define   FE_CU_REG_COMM_EXEC_CTL__W                                 3
#define   FE_CU_REG_COMM_EXEC_CTL__M                                 0x7
#define     FE_CU_REG_COMM_EXEC_CTL_STOP                             0x0
#define     FE_CU_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     FE_CU_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     FE_CU_REG_COMM_EXEC_CTL_STEP                             0x3

#define FE_CU_REG_COMM_STATE__A                                      0xC70001
#define FE_CU_REG_COMM_STATE__W                                      4
#define FE_CU_REG_COMM_STATE__M                                      0xF

#define FE_CU_REG_COMM_MB__A                                         0xC70002
#define FE_CU_REG_COMM_MB__W                                         3
#define FE_CU_REG_COMM_MB__M                                         0x7
#define   FE_CU_REG_COMM_MB_CTR__B                                   0
#define   FE_CU_REG_COMM_MB_CTR__W                                   1
#define   FE_CU_REG_COMM_MB_CTR__M                                   0x1
#define     FE_CU_REG_COMM_MB_CTR_OFF                                0x0
#define     FE_CU_REG_COMM_MB_CTR_ON                                 0x1
#define   FE_CU_REG_COMM_MB_OBS__B                                   1
#define   FE_CU_REG_COMM_MB_OBS__W                                   1
#define   FE_CU_REG_COMM_MB_OBS__M                                   0x2
#define     FE_CU_REG_COMM_MB_OBS_OFF                                0x0
#define     FE_CU_REG_COMM_MB_OBS_ON                                 0x2
#define   FE_CU_REG_COMM_MB_MUX__B                                   2
#define   FE_CU_REG_COMM_MB_MUX__W                                   1
#define   FE_CU_REG_COMM_MB_MUX__M                                   0x4
#define     FE_CU_REG_COMM_MB_MUX_REAL                               0x0
#define     FE_CU_REG_COMM_MB_MUX_IMAG                               0x4

#define FE_CU_REG_COMM_SERVICE0__A                                   0xC70003
#define FE_CU_REG_COMM_SERVICE0__W                                   10
#define FE_CU_REG_COMM_SERVICE0__M                                   0x3FF

#define FE_CU_REG_COMM_SERVICE1__A                                   0xC70004
#define FE_CU_REG_COMM_SERVICE1__W                                   11
#define FE_CU_REG_COMM_SERVICE1__M                                   0x7FF

#define FE_CU_REG_COMM_ACT__A                                        0xC70005
#define FE_CU_REG_COMM_ACT__W                                        2
#define FE_CU_REG_COMM_ACT__M                                        0x3

#define FE_CU_REG_COMM_CNT__A                                        0xC70006
#define FE_CU_REG_COMM_CNT__W                                        16
#define FE_CU_REG_COMM_CNT__M                                        0xFFFF

#define FE_CU_REG_COMM_INT_STA__A                                    0xC70007
#define FE_CU_REG_COMM_INT_STA__W                                    2
#define FE_CU_REG_COMM_INT_STA__M                                    0x3
#define   FE_CU_REG_COMM_INT_STA_FE_START__B                         0
#define   FE_CU_REG_COMM_INT_STA_FE_START__W                         1
#define   FE_CU_REG_COMM_INT_STA_FE_START__M                         0x1
#define   FE_CU_REG_COMM_INT_STA_FT_START__B                         1
#define   FE_CU_REG_COMM_INT_STA_FT_START__W                         1
#define   FE_CU_REG_COMM_INT_STA_FT_START__M                         0x2

#define FE_CU_REG_COMM_INT_MSK__A                                    0xC70008
#define FE_CU_REG_COMM_INT_MSK__W                                    2
#define FE_CU_REG_COMM_INT_MSK__M                                    0x3
#define   FE_CU_REG_COMM_INT_MSK_FE_START__B                         0
#define   FE_CU_REG_COMM_INT_MSK_FE_START__W                         1
#define   FE_CU_REG_COMM_INT_MSK_FE_START__M                         0x1
#define   FE_CU_REG_COMM_INT_MSK_FT_START__B                         1
#define   FE_CU_REG_COMM_INT_MSK_FT_START__W                         1
#define   FE_CU_REG_COMM_INT_MSK_FT_START__M                         0x2

#define FE_CU_REG_MODE__A                                            0xC70010
#define FE_CU_REG_MODE__W                                            3
#define FE_CU_REG_MODE__M                                            0x7
#define   FE_CU_REG_MODE_INIT                                        0x0

#define   FE_CU_REG_MODE_FFT__B                                      0
#define   FE_CU_REG_MODE_FFT__W                                      1
#define   FE_CU_REG_MODE_FFT__M                                      0x1
#define     FE_CU_REG_MODE_FFT_M8K                                   0x0
#define     FE_CU_REG_MODE_FFT_M2K                                   0x1

#define   FE_CU_REG_MODE_COR__B                                      1
#define   FE_CU_REG_MODE_COR__W                                      1
#define   FE_CU_REG_MODE_COR__M                                      0x2
#define     FE_CU_REG_MODE_COR_OFF                                   0x0
#define     FE_CU_REG_MODE_COR_ON                                    0x2

#define   FE_CU_REG_MODE_IFD__B                                      2
#define   FE_CU_REG_MODE_IFD__W                                      1
#define   FE_CU_REG_MODE_IFD__M                                      0x4
#define     FE_CU_REG_MODE_IFD_ENABLE                                0x0
#define     FE_CU_REG_MODE_IFD_DISABLE                               0x4

#define FE_CU_REG_FRM_CNT_RST__A                                     0xC70011
#define FE_CU_REG_FRM_CNT_RST__W                                     15
#define FE_CU_REG_FRM_CNT_RST__M                                     0x7FFF
#define   FE_CU_REG_FRM_CNT_RST_INIT                                 0x0

#define FE_CU_REG_FRM_CNT_STR__A                                     0xC70012
#define FE_CU_REG_FRM_CNT_STR__W                                     15
#define FE_CU_REG_FRM_CNT_STR__M                                     0x7FFF
#define   FE_CU_REG_FRM_CNT_STR_INIT                                 0x0

#define FE_CU_REG_FRM_SMP_CNT__A                                     0xC70013
#define FE_CU_REG_FRM_SMP_CNT__W                                     15
#define FE_CU_REG_FRM_SMP_CNT__M                                     0x7FFF

#define FE_CU_REG_FRM_SMB_CNT__A                                     0xC70014
#define FE_CU_REG_FRM_SMB_CNT__W                                     16
#define FE_CU_REG_FRM_SMB_CNT__M                                     0xFFFF

#define FE_CU_REG_CMP_MAX_DAT__A                                     0xC70015
#define FE_CU_REG_CMP_MAX_DAT__W                                     12
#define FE_CU_REG_CMP_MAX_DAT__M                                     0xFFF

#define FE_CU_REG_CMP_MAX_ADR__A                                     0xC70016
#define FE_CU_REG_CMP_MAX_ADR__W                                     10
#define FE_CU_REG_CMP_MAX_ADR__M                                     0x3FF

#define FE_CU_REG_CTR_NF1_WLO__A                                     0xC70017
#define FE_CU_REG_CTR_NF1_WLO__W                                     15
#define FE_CU_REG_CTR_NF1_WLO__M                                     0x7FFF
#define   FE_CU_REG_CTR_NF1_WLO_INIT                                 0x0

#define FE_CU_REG_CTR_NF1_WHI__A                                     0xC70018
#define FE_CU_REG_CTR_NF1_WHI__W                                     15
#define FE_CU_REG_CTR_NF1_WHI__M                                     0x7FFF
#define   FE_CU_REG_CTR_NF1_WHI_INIT                                 0x0

#define FE_CU_REG_CTR_NF2_WLO__A                                     0xC70019
#define FE_CU_REG_CTR_NF2_WLO__W                                     15
#define FE_CU_REG_CTR_NF2_WLO__M                                     0x7FFF
#define   FE_CU_REG_CTR_NF2_WLO_INIT                                 0x0

#define FE_CU_REG_CTR_NF2_WHI__A                                     0xC7001A
#define FE_CU_REG_CTR_NF2_WHI__W                                     15
#define FE_CU_REG_CTR_NF2_WHI__M                                     0x7FFF
#define   FE_CU_REG_CTR_NF2_WHI_INIT                                 0x0

#define FE_CU_REG_DIV_NF1_REA__A                                     0xC7001B
#define FE_CU_REG_DIV_NF1_REA__W                                     12
#define FE_CU_REG_DIV_NF1_REA__M                                     0xFFF

#define FE_CU_REG_DIV_NF1_IMA__A                                     0xC7001C
#define FE_CU_REG_DIV_NF1_IMA__W                                     12
#define FE_CU_REG_DIV_NF1_IMA__M                                     0xFFF

#define FE_CU_REG_DIV_NF2_REA__A                                     0xC7001D
#define FE_CU_REG_DIV_NF2_REA__W                                     12
#define FE_CU_REG_DIV_NF2_REA__M                                     0xFFF

#define FE_CU_REG_DIV_NF2_IMA__A                                     0xC7001E
#define FE_CU_REG_DIV_NF2_IMA__W                                     12
#define FE_CU_REG_DIV_NF2_IMA__M                                     0xFFF

#define FE_CU_BUF_RAM__A                                             0xC80000

#define FE_CU_CMP_RAM__A                                             0xC90000

#define FT_SID                                                       0x8

#define FT_COMM_EXEC__A                                              0x1000000
#define FT_COMM_EXEC__W                                              3
#define FT_COMM_EXEC__M                                              0x7
#define   FT_COMM_EXEC_CTL__B                                        0
#define   FT_COMM_EXEC_CTL__W                                        3
#define   FT_COMM_EXEC_CTL__M                                        0x7
#define     FT_COMM_EXEC_CTL_STOP                                    0x0
#define     FT_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     FT_COMM_EXEC_CTL_HOLD                                    0x2
#define     FT_COMM_EXEC_CTL_STEP                                    0x3
#define     FT_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     FT_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define FT_COMM_STATE__A                                             0x1000001
#define FT_COMM_STATE__W                                             16
#define FT_COMM_STATE__M                                             0xFFFF
#define FT_COMM_MB__A                                                0x1000002
#define FT_COMM_MB__W                                                16
#define FT_COMM_MB__M                                                0xFFFF
#define FT_COMM_SERVICE0__A                                          0x1000003
#define FT_COMM_SERVICE0__W                                          16
#define FT_COMM_SERVICE0__M                                          0xFFFF
#define FT_COMM_SERVICE1__A                                          0x1000004
#define FT_COMM_SERVICE1__W                                          16
#define FT_COMM_SERVICE1__M                                          0xFFFF
#define FT_COMM_INT_STA__A                                           0x1000007
#define FT_COMM_INT_STA__W                                           16
#define FT_COMM_INT_STA__M                                           0xFFFF
#define FT_COMM_INT_MSK__A                                           0x1000008
#define FT_COMM_INT_MSK__W                                           16
#define FT_COMM_INT_MSK__M                                           0xFFFF

#define FT_REG_COMM_EXEC__A                                          0x1010000
#define FT_REG_COMM_EXEC__W                                          3
#define FT_REG_COMM_EXEC__M                                          0x7
#define   FT_REG_COMM_EXEC_CTL__B                                    0
#define   FT_REG_COMM_EXEC_CTL__W                                    3
#define   FT_REG_COMM_EXEC_CTL__M                                    0x7
#define     FT_REG_COMM_EXEC_CTL_STOP                                0x0
#define     FT_REG_COMM_EXEC_CTL_ACTIVE                              0x1
#define     FT_REG_COMM_EXEC_CTL_HOLD                                0x2
#define     FT_REG_COMM_EXEC_CTL_STEP                                0x3

#define FT_REG_COMM_MB__A                                            0x1010002
#define FT_REG_COMM_MB__W                                            3
#define FT_REG_COMM_MB__M                                            0x7
#define   FT_REG_COMM_MB_CTR__B                                      0
#define   FT_REG_COMM_MB_CTR__W                                      1
#define   FT_REG_COMM_MB_CTR__M                                      0x1
#define     FT_REG_COMM_MB_CTR_OFF                                   0x0
#define     FT_REG_COMM_MB_CTR_ON                                    0x1
#define   FT_REG_COMM_MB_OBS__B                                      1
#define   FT_REG_COMM_MB_OBS__W                                      1
#define   FT_REG_COMM_MB_OBS__M                                      0x2
#define     FT_REG_COMM_MB_OBS_OFF                                   0x0
#define     FT_REG_COMM_MB_OBS_ON                                    0x2

#define FT_REG_COMM_SERVICE0__A                                      0x1010003
#define FT_REG_COMM_SERVICE0__W                                      10
#define FT_REG_COMM_SERVICE0__M                                      0x3FF
#define   FT_REG_COMM_SERVICE0_FT__B                                 8
#define   FT_REG_COMM_SERVICE0_FT__W                                 1
#define   FT_REG_COMM_SERVICE0_FT__M                                 0x100

#define FT_REG_COMM_SERVICE1__A                                      0x1010004
#define FT_REG_COMM_SERVICE1__W                                      11
#define FT_REG_COMM_SERVICE1__M                                      0x7FF

#define FT_REG_COMM_INT_STA__A                                       0x1010007
#define FT_REG_COMM_INT_STA__W                                       2
#define FT_REG_COMM_INT_STA__M                                       0x3
#define   FT_REG_COMM_INT_STA_NEW_MEAS__B                            0
#define   FT_REG_COMM_INT_STA_NEW_MEAS__W                            1
#define   FT_REG_COMM_INT_STA_NEW_MEAS__M                            0x1

#define FT_REG_COMM_INT_MSK__A                                       0x1010008
#define FT_REG_COMM_INT_MSK__W                                       2
#define FT_REG_COMM_INT_MSK__M                                       0x3
#define   FT_REG_COMM_INT_MSK_NEW_MEAS__B                            0
#define   FT_REG_COMM_INT_MSK_NEW_MEAS__W                            1
#define   FT_REG_COMM_INT_MSK_NEW_MEAS__M                            0x1

#define FT_REG_MODE_2K__A                                            0x1010010
#define FT_REG_MODE_2K__W                                            1
#define FT_REG_MODE_2K__M                                            0x1
#define   FT_REG_MODE_2K_MODE_8K                                     0x0
#define   FT_REG_MODE_2K_MODE_2K                                     0x1
#define   FT_REG_MODE_2K_INIT                                        0x0

#define FT_REG_BUS_MOD__A                                            0x1010011
#define FT_REG_BUS_MOD__W                                            1
#define FT_REG_BUS_MOD__M                                            0x1
#define   FT_REG_BUS_MOD_INPUT                                       0x0
#define   FT_REG_BUS_MOD_PILOT                                       0x1
#define   FT_REG_BUS_MOD_INIT                                        0x0

#define FT_REG_BUS_REAL__A                                           0x1010012
#define FT_REG_BUS_REAL__W                                           10
#define FT_REG_BUS_REAL__M                                           0x3FF
#define   FT_REG_BUS_REAL_INIT                                       0x0

#define FT_REG_BUS_IMAG__A                                           0x1010013
#define FT_REG_BUS_IMAG__W                                           10
#define FT_REG_BUS_IMAG__M                                           0x3FF
#define   FT_REG_BUS_IMAG_INIT                                       0x0

#define FT_REG_BUS_VAL__A                                            0x1010014
#define FT_REG_BUS_VAL__W                                            1
#define FT_REG_BUS_VAL__M                                            0x1
#define   FT_REG_BUS_VAL_INIT                                        0x0

#define FT_REG_PEAK__A                                               0x1010015
#define FT_REG_PEAK__W                                               11
#define FT_REG_PEAK__M                                               0x7FF
#define   FT_REG_PEAK_INIT                                           0x0

#define FT_REG_NORM_OFF__A                                           0x1010016
#define FT_REG_NORM_OFF__W                                           4
#define FT_REG_NORM_OFF__M                                           0xF
#define   FT_REG_NORM_OFF_INIT                                       0x2

#define FT_ST1_RAM__A                                                0x1020000

#define FT_ST2_RAM__A                                                0x1030000

#define FT_ST3_RAM__A                                                0x1040000

#define FT_ST5_RAM__A                                                0x1050000

#define FT_ST6_RAM__A                                                0x1060000

#define FT_ST8_RAM__A                                                0x1070000

#define FT_ST9_RAM__A                                                0x1080000

#define CP_SID                                                       0x9

#define CP_COMM_EXEC__A                                              0x1400000
#define CP_COMM_EXEC__W                                              3
#define CP_COMM_EXEC__M                                              0x7
#define   CP_COMM_EXEC_CTL__B                                        0
#define   CP_COMM_EXEC_CTL__W                                        3
#define   CP_COMM_EXEC_CTL__M                                        0x7
#define     CP_COMM_EXEC_CTL_STOP                                    0x0
#define     CP_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     CP_COMM_EXEC_CTL_HOLD                                    0x2
#define     CP_COMM_EXEC_CTL_STEP                                    0x3
#define     CP_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     CP_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define CP_COMM_STATE__A                                             0x1400001
#define CP_COMM_STATE__W                                             16
#define CP_COMM_STATE__M                                             0xFFFF
#define CP_COMM_MB__A                                                0x1400002
#define CP_COMM_MB__W                                                16
#define CP_COMM_MB__M                                                0xFFFF
#define CP_COMM_SERVICE0__A                                          0x1400003
#define CP_COMM_SERVICE0__W                                          16
#define CP_COMM_SERVICE0__M                                          0xFFFF
#define CP_COMM_SERVICE1__A                                          0x1400004
#define CP_COMM_SERVICE1__W                                          16
#define CP_COMM_SERVICE1__M                                          0xFFFF
#define CP_COMM_INT_STA__A                                           0x1400007
#define CP_COMM_INT_STA__W                                           16
#define CP_COMM_INT_STA__M                                           0xFFFF
#define CP_COMM_INT_MSK__A                                           0x1400008
#define CP_COMM_INT_MSK__W                                           16
#define CP_COMM_INT_MSK__M                                           0xFFFF

#define CP_REG_COMM_EXEC__A                                          0x1410000
#define CP_REG_COMM_EXEC__W                                          3
#define CP_REG_COMM_EXEC__M                                          0x7
#define   CP_REG_COMM_EXEC_CTL__B                                    0
#define   CP_REG_COMM_EXEC_CTL__W                                    3
#define   CP_REG_COMM_EXEC_CTL__M                                    0x7
#define     CP_REG_COMM_EXEC_CTL_STOP                                0x0
#define     CP_REG_COMM_EXEC_CTL_ACTIVE                              0x1
#define     CP_REG_COMM_EXEC_CTL_HOLD                                0x2
#define     CP_REG_COMM_EXEC_CTL_STEP                                0x3

#define CP_REG_COMM_MB__A                                            0x1410002
#define CP_REG_COMM_MB__W                                            3
#define CP_REG_COMM_MB__M                                            0x7
#define   CP_REG_COMM_MB_CTR__B                                      0
#define   CP_REG_COMM_MB_CTR__W                                      1
#define   CP_REG_COMM_MB_CTR__M                                      0x1
#define     CP_REG_COMM_MB_CTR_OFF                                   0x0
#define     CP_REG_COMM_MB_CTR_ON                                    0x1
#define   CP_REG_COMM_MB_OBS__B                                      1
#define   CP_REG_COMM_MB_OBS__W                                      1
#define   CP_REG_COMM_MB_OBS__M                                      0x2
#define     CP_REG_COMM_MB_OBS_OFF                                   0x0
#define     CP_REG_COMM_MB_OBS_ON                                    0x2

#define CP_REG_COMM_SERVICE0__A                                      0x1410003
#define CP_REG_COMM_SERVICE0__W                                      10
#define CP_REG_COMM_SERVICE0__M                                      0x3FF
#define   CP_REG_COMM_SERVICE0_CP__B                                 9
#define   CP_REG_COMM_SERVICE0_CP__W                                 1
#define   CP_REG_COMM_SERVICE0_CP__M                                 0x200

#define CP_REG_COMM_SERVICE1__A                                      0x1410004
#define CP_REG_COMM_SERVICE1__W                                      11
#define CP_REG_COMM_SERVICE1__M                                      0x7FF

#define CP_REG_COMM_INT_STA__A                                       0x1410007
#define CP_REG_COMM_INT_STA__W                                       2
#define CP_REG_COMM_INT_STA__M                                       0x3
#define   CP_REG_COMM_INT_STA_NEW_MEAS__B                            0
#define   CP_REG_COMM_INT_STA_NEW_MEAS__W                            1
#define   CP_REG_COMM_INT_STA_NEW_MEAS__M                            0x1

#define CP_REG_COMM_INT_MSK__A                                       0x1410008
#define CP_REG_COMM_INT_MSK__W                                       2
#define CP_REG_COMM_INT_MSK__M                                       0x3
#define   CP_REG_COMM_INT_MSK_NEW_MEAS__B                            0
#define   CP_REG_COMM_INT_MSK_NEW_MEAS__W                            1
#define   CP_REG_COMM_INT_MSK_NEW_MEAS__M                            0x1

#define CP_REG_MODE_2K__A                                            0x1410010
#define CP_REG_MODE_2K__W                                            1
#define CP_REG_MODE_2K__M                                            0x1
#define   CP_REG_MODE_2K_INIT                                        0x0

#define CP_REG_INTERVAL__A                                           0x1410011
#define CP_REG_INTERVAL__W                                           4
#define CP_REG_INTERVAL__M                                           0xF
#define   CP_REG_INTERVAL_INIT                                       0x5

#define CP_REG_SKIP_START0__A                                        0x1410012
#define CP_REG_SKIP_START0__W                                        13
#define CP_REG_SKIP_START0__M                                        0x1FFF
#define   CP_REG_SKIP_START0_INIT                                    0x0

#define CP_REG_SKIP_STOP0__A                                         0x1410013
#define CP_REG_SKIP_STOP0__W                                         13
#define CP_REG_SKIP_STOP0__M                                         0x1FFF
#define   CP_REG_SKIP_STOP0_INIT                                     0x0

#define CP_REG_SKIP_START1__A                                        0x1410014
#define CP_REG_SKIP_START1__W                                        13
#define CP_REG_SKIP_START1__M                                        0x1FFF
#define   CP_REG_SKIP_START1_INIT                                    0x0

#define CP_REG_SKIP_STOP1__A                                         0x1410015
#define CP_REG_SKIP_STOP1__W                                         13
#define CP_REG_SKIP_STOP1__M                                         0x1FFF
#define   CP_REG_SKIP_STOP1_INIT                                     0x0

#define CP_REG_SKIP_START2__A                                        0x1410016
#define CP_REG_SKIP_START2__W                                        13
#define CP_REG_SKIP_START2__M                                        0x1FFF
#define   CP_REG_SKIP_START2_INIT                                    0x0

#define CP_REG_SKIP_STOP2__A                                         0x1410017
#define CP_REG_SKIP_STOP2__W                                         13
#define CP_REG_SKIP_STOP2__M                                         0x1FFF
#define   CP_REG_SKIP_STOP2_INIT                                     0x0

#define CP_REG_SKIP_ENA__A                                           0x1410018
#define CP_REG_SKIP_ENA__W                                           3
#define CP_REG_SKIP_ENA__M                                           0x7

#define   CP_REG_SKIP_ENA_CPL__B                                     0
#define   CP_REG_SKIP_ENA_CPL__W                                     1
#define   CP_REG_SKIP_ENA_CPL__M                                     0x1

#define   CP_REG_SKIP_ENA_SPD__B                                     1
#define   CP_REG_SKIP_ENA_SPD__W                                     1
#define   CP_REG_SKIP_ENA_SPD__M                                     0x2

#define   CP_REG_SKIP_ENA_CPD__B                                     2
#define   CP_REG_SKIP_ENA_CPD__W                                     1
#define   CP_REG_SKIP_ENA_CPD__M                                     0x4
#define     CP_REG_SKIP_ENA_INIT                                     0x0

#define CP_REG_BR_MODE_MIX__A                                        0x1410020
#define CP_REG_BR_MODE_MIX__W                                        1
#define CP_REG_BR_MODE_MIX__M                                        0x1
#define   CP_REG_BR_MODE_MIX_INIT                                    0x0

#define CP_REG_BR_SMB_NR__A                                          0x1410021
#define CP_REG_BR_SMB_NR__W                                          3
#define CP_REG_BR_SMB_NR__M                                          0x7

#define   CP_REG_BR_SMB_NR_SMB__B                                    0
#define   CP_REG_BR_SMB_NR_SMB__W                                    2
#define   CP_REG_BR_SMB_NR_SMB__M                                    0x3

#define   CP_REG_BR_SMB_NR_VAL__B                                    2
#define   CP_REG_BR_SMB_NR_VAL__W                                    1
#define   CP_REG_BR_SMB_NR_VAL__M                                    0x4
#define     CP_REG_BR_SMB_NR_INIT                                    0x0

#define CP_REG_BR_CP_SMB_NR__A                                       0x1410022
#define CP_REG_BR_CP_SMB_NR__W                                       2
#define CP_REG_BR_CP_SMB_NR__M                                       0x3
#define   CP_REG_BR_CP_SMB_NR_INIT                                   0x0

#define CP_REG_BR_SPL_OFFSET__A                                      0x1410023
#define CP_REG_BR_SPL_OFFSET__W                                      3
#define CP_REG_BR_SPL_OFFSET__M                                      0x7
#define   CP_REG_BR_SPL_OFFSET_INIT                                  0x0

#define CP_REG_BR_STR_DEL__A                                         0x1410024
#define CP_REG_BR_STR_DEL__W                                         10
#define CP_REG_BR_STR_DEL__M                                         0x3FF
#define   CP_REG_BR_STR_DEL_INIT                                     0xA

#define CP_REG_RT_ANG_INC0__A                                        0x1410030
#define CP_REG_RT_ANG_INC0__W                                        16
#define CP_REG_RT_ANG_INC0__M                                        0xFFFF
#define   CP_REG_RT_ANG_INC0_INIT                                    0x0

#define CP_REG_RT_ANG_INC1__A                                        0x1410031
#define CP_REG_RT_ANG_INC1__W                                        8
#define CP_REG_RT_ANG_INC1__M                                        0xFF
#define   CP_REG_RT_ANG_INC1_INIT                                    0x0

#define CP_REG_RT_DETECT_ENA__A                                      0x1410032
#define CP_REG_RT_DETECT_ENA__W                                      2
#define CP_REG_RT_DETECT_ENA__M                                      0x3

#define   CP_REG_RT_DETECT_ENA_SCATTERED__B                          0
#define   CP_REG_RT_DETECT_ENA_SCATTERED__W                          1
#define   CP_REG_RT_DETECT_ENA_SCATTERED__M                          0x1

#define   CP_REG_RT_DETECT_ENA_CONTINUOUS__B                         1
#define   CP_REG_RT_DETECT_ENA_CONTINUOUS__W                         1
#define   CP_REG_RT_DETECT_ENA_CONTINUOUS__M                         0x2
#define     CP_REG_RT_DETECT_ENA_INIT                                0x0

#define CP_REG_RT_DETECT_TRH__A                                      0x1410033
#define CP_REG_RT_DETECT_TRH__W                                      2
#define CP_REG_RT_DETECT_TRH__M                                      0x3
#define   CP_REG_RT_DETECT_TRH_INIT                                  0x3

#define CP_REG_RT_SPD_RELIABLE__A                                    0x1410034
#define CP_REG_RT_SPD_RELIABLE__W                                    3
#define CP_REG_RT_SPD_RELIABLE__M                                    0x7
#define   CP_REG_RT_SPD_RELIABLE_INIT                                0x0

#define CP_REG_RT_SPD_DIRECTION__A                                   0x1410035
#define CP_REG_RT_SPD_DIRECTION__W                                   1
#define CP_REG_RT_SPD_DIRECTION__M                                   0x1
#define   CP_REG_RT_SPD_DIRECTION_INIT                               0x0

#define CP_REG_RT_SPD_MOD__A                                         0x1410036
#define CP_REG_RT_SPD_MOD__W                                         2
#define CP_REG_RT_SPD_MOD__M                                         0x3
#define   CP_REG_RT_SPD_MOD_INIT                                     0x0

#define CP_REG_RT_SPD_SMB__A                                         0x1410037
#define CP_REG_RT_SPD_SMB__W                                         2
#define CP_REG_RT_SPD_SMB__M                                         0x3
#define   CP_REG_RT_SPD_SMB_INIT                                     0x0

#define CP_REG_RT_CPD_MODE__A                                        0x1410038
#define CP_REG_RT_CPD_MODE__W                                        3
#define CP_REG_RT_CPD_MODE__M                                        0x7

#define   CP_REG_RT_CPD_MODE_MOD3__B                                 0
#define   CP_REG_RT_CPD_MODE_MOD3__W                                 2
#define   CP_REG_RT_CPD_MODE_MOD3__M                                 0x3

#define   CP_REG_RT_CPD_MODE_ADD__B                                  2
#define   CP_REG_RT_CPD_MODE_ADD__W                                  1
#define   CP_REG_RT_CPD_MODE_ADD__M                                  0x4
#define     CP_REG_RT_CPD_MODE_INIT                                  0x0

#define CP_REG_RT_CPD_RELIABLE__A                                    0x1410039
#define CP_REG_RT_CPD_RELIABLE__W                                    3
#define CP_REG_RT_CPD_RELIABLE__M                                    0x7
#define   CP_REG_RT_CPD_RELIABLE_INIT                                0x0

#define CP_REG_RT_CPD_BIN__A                                         0x141003A
#define CP_REG_RT_CPD_BIN__W                                         5
#define CP_REG_RT_CPD_BIN__M                                         0x1F
#define   CP_REG_RT_CPD_BIN_INIT                                     0x0

#define CP_REG_RT_CPD_MAX__A                                         0x141003B
#define CP_REG_RT_CPD_MAX__W                                         4
#define CP_REG_RT_CPD_MAX__M                                         0xF
#define   CP_REG_RT_CPD_MAX_INIT                                     0x0

#define CP_REG_RT_SUPR_VAL__A                                        0x141003C
#define CP_REG_RT_SUPR_VAL__W                                        2
#define CP_REG_RT_SUPR_VAL__M                                        0x3

#define   CP_REG_RT_SUPR_VAL_CE__B                                   0
#define   CP_REG_RT_SUPR_VAL_CE__W                                   1
#define   CP_REG_RT_SUPR_VAL_CE__M                                   0x1

#define   CP_REG_RT_SUPR_VAL_DL__B                                   1
#define   CP_REG_RT_SUPR_VAL_DL__W                                   1
#define   CP_REG_RT_SUPR_VAL_DL__M                                   0x2
#define     CP_REG_RT_SUPR_VAL_INIT                                  0x0

#define CP_REG_RT_EXP_AVE__A                                         0x141003D
#define CP_REG_RT_EXP_AVE__W                                         5
#define CP_REG_RT_EXP_AVE__M                                         0x1F
#define   CP_REG_RT_EXP_AVE_INIT                                     0x0

#define CP_REG_RT_EXP_MARG__A                                        0x141003E
#define CP_REG_RT_EXP_MARG__W                                        5
#define CP_REG_RT_EXP_MARG__M                                        0x1F
#define   CP_REG_RT_EXP_MARG_INIT                                    0x0

#define CP_REG_AC_NEXP_OFFS__A                                       0x1410040
#define CP_REG_AC_NEXP_OFFS__W                                       8
#define CP_REG_AC_NEXP_OFFS__M                                       0xFF
#define   CP_REG_AC_NEXP_OFFS_INIT                                   0x0

#define CP_REG_AC_AVER_POW__A                                        0x1410041
#define CP_REG_AC_AVER_POW__W                                        8
#define CP_REG_AC_AVER_POW__M                                        0xFF
#define   CP_REG_AC_AVER_POW_INIT                                    0x5F

#define CP_REG_AC_MAX_POW__A                                         0x1410042
#define CP_REG_AC_MAX_POW__W                                         8
#define CP_REG_AC_MAX_POW__M                                         0xFF
#define   CP_REG_AC_MAX_POW_INIT                                     0x7A

#define CP_REG_AC_WEIGHT_MAN__A                                      0x1410043
#define CP_REG_AC_WEIGHT_MAN__W                                      6
#define CP_REG_AC_WEIGHT_MAN__M                                      0x3F
#define   CP_REG_AC_WEIGHT_MAN_INIT                                  0x31

#define CP_REG_AC_WEIGHT_EXP__A                                      0x1410044
#define CP_REG_AC_WEIGHT_EXP__W                                      5
#define CP_REG_AC_WEIGHT_EXP__M                                      0x1F
#define   CP_REG_AC_WEIGHT_EXP_INIT                                  0x10

#define CP_REG_AC_GAIN_MAN__A                                        0x1410045
#define CP_REG_AC_GAIN_MAN__W                                        16
#define CP_REG_AC_GAIN_MAN__M                                        0xFFFF
#define   CP_REG_AC_GAIN_MAN_INIT                                    0x0

#define CP_REG_AC_GAIN_EXP__A                                        0x1410046
#define CP_REG_AC_GAIN_EXP__W                                        5
#define CP_REG_AC_GAIN_EXP__M                                        0x1F
#define   CP_REG_AC_GAIN_EXP_INIT                                    0x0

#define CP_REG_AC_AMP_MODE__A                                        0x1410047
#define CP_REG_AC_AMP_MODE__W                                        2
#define CP_REG_AC_AMP_MODE__M                                        0x3
#define   CP_REG_AC_AMP_MODE_NEW                                     0x0
#define   CP_REG_AC_AMP_MODE_OLD                                     0x1
#define   CP_REG_AC_AMP_MODE_FIXED                                   0x2
#define   CP_REG_AC_AMP_MODE_INIT                                    0x2

#define CP_REG_AC_AMP_FIX__A                                         0x1410048
#define CP_REG_AC_AMP_FIX__W                                         14
#define CP_REG_AC_AMP_FIX__M                                         0x3FFF
#define   CP_REG_AC_AMP_FIX_INIT                                     0x1FF

#define CP_REG_AC_AMP_READ__A                                        0x1410049
#define CP_REG_AC_AMP_READ__W                                        14
#define CP_REG_AC_AMP_READ__M                                        0x3FFF
#define   CP_REG_AC_AMP_READ_INIT                                    0x0

#define CP_REG_AC_ANG_MODE__A                                        0x141004A
#define CP_REG_AC_ANG_MODE__W                                        2
#define CP_REG_AC_ANG_MODE__M                                        0x3
#define   CP_REG_AC_ANG_MODE_NEW                                     0x0
#define   CP_REG_AC_ANG_MODE_OLD                                     0x1
#define   CP_REG_AC_ANG_MODE_NO_INT                                  0x2
#define   CP_REG_AC_ANG_MODE_OFFSET                                  0x3
#define   CP_REG_AC_ANG_MODE_INIT                                    0x3

#define CP_REG_AC_ANG_OFFS__A                                        0x141004B
#define CP_REG_AC_ANG_OFFS__W                                        14
#define CP_REG_AC_ANG_OFFS__M                                        0x3FFF
#define   CP_REG_AC_ANG_OFFS_INIT                                    0x0

#define CP_REG_AC_ANG_READ__A                                        0x141004C
#define CP_REG_AC_ANG_READ__W                                        16
#define CP_REG_AC_ANG_READ__M                                        0xFFFF
#define   CP_REG_AC_ANG_READ_INIT                                    0x0

#define CP_REG_DL_MB_WR_ADDR__A                                      0x1410050
#define CP_REG_DL_MB_WR_ADDR__W                                      15
#define CP_REG_DL_MB_WR_ADDR__M                                      0x7FFF
#define   CP_REG_DL_MB_WR_ADDR_INIT                                  0x0

#define CP_REG_DL_MB_WR_CTR__A                                       0x1410051
#define CP_REG_DL_MB_WR_CTR__W                                       5
#define CP_REG_DL_MB_WR_CTR__M                                       0x1F

#define   CP_REG_DL_MB_WR_CTR_WORD__B                                2
#define   CP_REG_DL_MB_WR_CTR_WORD__W                                3
#define   CP_REG_DL_MB_WR_CTR_WORD__M                                0x1C

#define   CP_REG_DL_MB_WR_CTR_OBS__B                                 1
#define   CP_REG_DL_MB_WR_CTR_OBS__W                                 1
#define   CP_REG_DL_MB_WR_CTR_OBS__M                                 0x2

#define   CP_REG_DL_MB_WR_CTR_CTR__B                                 0
#define   CP_REG_DL_MB_WR_CTR_CTR__W                                 1
#define   CP_REG_DL_MB_WR_CTR_CTR__M                                 0x1
#define     CP_REG_DL_MB_WR_CTR_INIT                                 0x0

#define CP_REG_DL_MB_RD_ADDR__A                                      0x1410052
#define CP_REG_DL_MB_RD_ADDR__W                                      15
#define CP_REG_DL_MB_RD_ADDR__M                                      0x7FFF
#define   CP_REG_DL_MB_RD_ADDR_INIT                                  0x0

#define CP_REG_DL_MB_RD_CTR__A                                       0x1410053
#define CP_REG_DL_MB_RD_CTR__W                                       11
#define CP_REG_DL_MB_RD_CTR__M                                       0x7FF

#define   CP_REG_DL_MB_RD_CTR_TEST__B                                10
#define   CP_REG_DL_MB_RD_CTR_TEST__W                                1
#define   CP_REG_DL_MB_RD_CTR_TEST__M                                0x400

#define   CP_REG_DL_MB_RD_CTR_OFFSET__B                              8
#define   CP_REG_DL_MB_RD_CTR_OFFSET__W                              2
#define   CP_REG_DL_MB_RD_CTR_OFFSET__M                              0x300

#define   CP_REG_DL_MB_RD_CTR_VALID__B                               5
#define   CP_REG_DL_MB_RD_CTR_VALID__W                               3
#define   CP_REG_DL_MB_RD_CTR_VALID__M                               0xE0

#define   CP_REG_DL_MB_RD_CTR_WORD__B                                2
#define   CP_REG_DL_MB_RD_CTR_WORD__W                                3
#define   CP_REG_DL_MB_RD_CTR_WORD__M                                0x1C

#define   CP_REG_DL_MB_RD_CTR_OBS__B                                 1
#define   CP_REG_DL_MB_RD_CTR_OBS__W                                 1
#define   CP_REG_DL_MB_RD_CTR_OBS__M                                 0x2

#define   CP_REG_DL_MB_RD_CTR_CTR__B                                 0
#define   CP_REG_DL_MB_RD_CTR_CTR__W                                 1
#define   CP_REG_DL_MB_RD_CTR_CTR__M                                 0x1
#define     CP_REG_DL_MB_RD_CTR_INIT                                 0x0

#define CP_BR_BUF_RAM__A                                             0x1420000

#define CP_BR_CPL_RAM__A                                             0x1430000

#define CP_PB_DL0_RAM__A                                             0x1440000

#define CP_PB_DL1_RAM__A                                             0x1450000

#define CP_PB_DL2_RAM__A                                             0x1460000

#define CE_SID                                                       0xA

#define CE_COMM_EXEC__A                                              0x1800000
#define CE_COMM_EXEC__W                                              3
#define CE_COMM_EXEC__M                                              0x7
#define   CE_COMM_EXEC_CTL__B                                        0
#define   CE_COMM_EXEC_CTL__W                                        3
#define   CE_COMM_EXEC_CTL__M                                        0x7
#define     CE_COMM_EXEC_CTL_STOP                                    0x0
#define     CE_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     CE_COMM_EXEC_CTL_HOLD                                    0x2
#define     CE_COMM_EXEC_CTL_STEP                                    0x3
#define     CE_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     CE_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define CE_COMM_STATE__A                                             0x1800001
#define CE_COMM_STATE__W                                             16
#define CE_COMM_STATE__M                                             0xFFFF
#define CE_COMM_MB__A                                                0x1800002
#define CE_COMM_MB__W                                                16
#define CE_COMM_MB__M                                                0xFFFF
#define CE_COMM_SERVICE0__A                                          0x1800003
#define CE_COMM_SERVICE0__W                                          16
#define CE_COMM_SERVICE0__M                                          0xFFFF
#define CE_COMM_SERVICE1__A                                          0x1800004
#define CE_COMM_SERVICE1__W                                          16
#define CE_COMM_SERVICE1__M                                          0xFFFF
#define CE_COMM_INT_STA__A                                           0x1800007
#define CE_COMM_INT_STA__W                                           16
#define CE_COMM_INT_STA__M                                           0xFFFF
#define CE_COMM_INT_MSK__A                                           0x1800008
#define CE_COMM_INT_MSK__W                                           16
#define CE_COMM_INT_MSK__M                                           0xFFFF

#define CE_REG_COMM_EXEC__A                                          0x1810000
#define CE_REG_COMM_EXEC__W                                          3
#define CE_REG_COMM_EXEC__M                                          0x7
#define   CE_REG_COMM_EXEC_CTL__B                                    0
#define   CE_REG_COMM_EXEC_CTL__W                                    3
#define   CE_REG_COMM_EXEC_CTL__M                                    0x7
#define     CE_REG_COMM_EXEC_CTL_STOP                                0x0
#define     CE_REG_COMM_EXEC_CTL_ACTIVE                              0x1
#define     CE_REG_COMM_EXEC_CTL_HOLD                                0x2
#define     CE_REG_COMM_EXEC_CTL_STEP                                0x3

#define CE_REG_COMM_MB__A                                            0x1810002
#define CE_REG_COMM_MB__W                                            4
#define CE_REG_COMM_MB__M                                            0xF
#define   CE_REG_COMM_MB_CTR__B                                      0
#define   CE_REG_COMM_MB_CTR__W                                      1
#define   CE_REG_COMM_MB_CTR__M                                      0x1
#define     CE_REG_COMM_MB_CTR_OFF                                   0x0
#define     CE_REG_COMM_MB_CTR_ON                                    0x1
#define   CE_REG_COMM_MB_OBS__B                                      1
#define   CE_REG_COMM_MB_OBS__W                                      1
#define   CE_REG_COMM_MB_OBS__M                                      0x2
#define     CE_REG_COMM_MB_OBS_OFF                                   0x0
#define     CE_REG_COMM_MB_OBS_ON                                    0x2
#define   CE_REG_COMM_MB_OBS_SEL__B                                  2
#define   CE_REG_COMM_MB_OBS_SEL__W                                  2
#define   CE_REG_COMM_MB_OBS_SEL__M                                  0xC
#define     CE_REG_COMM_MB_OBS_SEL_FI                                0x0
#define     CE_REG_COMM_MB_OBS_SEL_TP                                0x4
#define     CE_REG_COMM_MB_OBS_SEL_TI                                0x8
#define     CE_REG_COMM_MB_OBS_SEL_FR                                0x8

#define CE_REG_COMM_SERVICE0__A                                      0x1810003
#define CE_REG_COMM_SERVICE0__W                                      10
#define CE_REG_COMM_SERVICE0__M                                      0x3FF
#define   CE_REG_COMM_SERVICE0_FT__B                                 8
#define   CE_REG_COMM_SERVICE0_FT__W                                 1
#define   CE_REG_COMM_SERVICE0_FT__M                                 0x100

#define CE_REG_COMM_SERVICE1__A                                      0x1810004
#define CE_REG_COMM_SERVICE1__W                                      11
#define CE_REG_COMM_SERVICE1__M                                      0x7FF

#define CE_REG_COMM_INT_STA__A                                       0x1810007
#define CE_REG_COMM_INT_STA__W                                       3
#define CE_REG_COMM_INT_STA__M                                       0x7
#define   CE_REG_COMM_INT_STA_CE_PE__B                               0
#define   CE_REG_COMM_INT_STA_CE_PE__W                               1
#define   CE_REG_COMM_INT_STA_CE_PE__M                               0x1
#define   CE_REG_COMM_INT_STA_CE_IR__B                               1
#define   CE_REG_COMM_INT_STA_CE_IR__W                               1
#define   CE_REG_COMM_INT_STA_CE_IR__M                               0x2
#define   CE_REG_COMM_INT_STA_CE_FI__B                               2
#define   CE_REG_COMM_INT_STA_CE_FI__W                               1
#define   CE_REG_COMM_INT_STA_CE_FI__M                               0x4

#define CE_REG_COMM_INT_MSK__A                                       0x1810008
#define CE_REG_COMM_INT_MSK__W                                       3
#define CE_REG_COMM_INT_MSK__M                                       0x7
#define   CE_REG_COMM_INT_MSK_CE_PE__B                               0
#define   CE_REG_COMM_INT_MSK_CE_PE__W                               1
#define   CE_REG_COMM_INT_MSK_CE_PE__M                               0x1
#define   CE_REG_COMM_INT_MSK_CE_IR__B                               1
#define   CE_REG_COMM_INT_MSK_CE_IR__W                               1
#define   CE_REG_COMM_INT_MSK_CE_IR__M                               0x2
#define   CE_REG_COMM_INT_MSK_CE_FI__B                               2
#define   CE_REG_COMM_INT_MSK_CE_FI__W                               1
#define   CE_REG_COMM_INT_MSK_CE_FI__M                               0x4

#define CE_REG_2K__A                                                 0x1810010
#define CE_REG_2K__W                                                 1
#define CE_REG_2K__M                                                 0x1
#define   CE_REG_2K_INIT                                             0x0

#define CE_REG_TAPSET__A                                             0x1810011
#define CE_REG_TAPSET__W                                             2
#define CE_REG_TAPSET__M                                             0x3

#define CE_REG_TAPSET_MOTION_INIT                                    0x0

#define CE_REG_TAPSET_MOTION_NO                                      0x0

#define CE_REG_TAPSET_MOTION_LOW                                     0x1

#define CE_REG_TAPSET_MOTION_HIGH                                    0x2

#define CE_REG_TAPSET_MOTION_UNDEFINED                               0x3

#define CE_REG_AVG_POW__A                                            0x1810012
#define CE_REG_AVG_POW__W                                            8
#define CE_REG_AVG_POW__M                                            0xFF
#define   CE_REG_AVG_POW_INIT                                        0x0

#define CE_REG_MAX_POW__A                                            0x1810013
#define CE_REG_MAX_POW__W                                            8
#define CE_REG_MAX_POW__M                                            0xFF
#define   CE_REG_MAX_POW_INIT                                        0x0

#define CE_REG_ATT__A                                                0x1810014
#define CE_REG_ATT__W                                                8
#define CE_REG_ATT__M                                                0xFF
#define   CE_REG_ATT_INIT                                            0x0

#define CE_REG_NRED__A                                               0x1810015
#define CE_REG_NRED__W                                               6
#define CE_REG_NRED__M                                               0x3F
#define   CE_REG_NRED_INIT                                           0x0

#define CE_REG_PU_SIGN__A                                            0x1810020
#define CE_REG_PU_SIGN__W                                            1
#define CE_REG_PU_SIGN__M                                            0x1
#define   CE_REG_PU_SIGN_INIT                                        0x0

#define CE_REG_PU_MIX__A                                             0x1810021
#define CE_REG_PU_MIX__W                                             7
#define CE_REG_PU_MIX__M                                             0x7F
#define   CE_REG_PU_MIX_INIT                                         0x0

#define CE_REG_PB_PILOT_REQ__A                                       0x1810030
#define CE_REG_PB_PILOT_REQ__W                                       15
#define CE_REG_PB_PILOT_REQ__M                                       0x7FFF
#define   CE_REG_PB_PILOT_REQ_INIT                                   0x0
#define   CE_REG_PB_PILOT_REQ_BUFFER_INDEX__B                        12
#define   CE_REG_PB_PILOT_REQ_BUFFER_INDEX__W                        3
#define   CE_REG_PB_PILOT_REQ_BUFFER_INDEX__M                        0x7000
#define   CE_REG_PB_PILOT_REQ_PILOT_ADR__B                           0
#define   CE_REG_PB_PILOT_REQ_PILOT_ADR__W                           12
#define   CE_REG_PB_PILOT_REQ_PILOT_ADR__M                           0xFFF

#define CE_REG_PB_PILOT_REQ_VALID__A                                 0x1810031
#define CE_REG_PB_PILOT_REQ_VALID__W                                 1
#define CE_REG_PB_PILOT_REQ_VALID__M                                 0x1
#define   CE_REG_PB_PILOT_REQ_VALID_INIT                             0x0

#define CE_REG_PB_FREEZE__A                                          0x1810032
#define CE_REG_PB_FREEZE__W                                          1
#define CE_REG_PB_FREEZE__M                                          0x1
#define   CE_REG_PB_FREEZE_INIT                                      0x0

#define CE_REG_PB_PILOT_EXP__A                                       0x1810038
#define CE_REG_PB_PILOT_EXP__W                                       4
#define CE_REG_PB_PILOT_EXP__M                                       0xF
#define   CE_REG_PB_PILOT_EXP_INIT                                   0x0

#define CE_REG_PB_PILOT_REAL__A                                      0x1810039
#define CE_REG_PB_PILOT_REAL__W                                      10
#define CE_REG_PB_PILOT_REAL__M                                      0x3FF
#define   CE_REG_PB_PILOT_REAL_INIT                                  0x0

#define CE_REG_PB_PILOT_IMAG__A                                      0x181003A
#define CE_REG_PB_PILOT_IMAG__W                                      10
#define CE_REG_PB_PILOT_IMAG__M                                      0x3FF
#define   CE_REG_PB_PILOT_IMAG_INIT                                  0x0

#define CE_REG_PB_SMBNR__A                                           0x181003B
#define CE_REG_PB_SMBNR__W                                           5
#define CE_REG_PB_SMBNR__M                                           0x1F
#define   CE_REG_PB_SMBNR_INIT                                       0x0

#define CE_REG_NE_PILOT_REQ__A                                       0x1810040
#define CE_REG_NE_PILOT_REQ__W                                       12
#define CE_REG_NE_PILOT_REQ__M                                       0xFFF
#define   CE_REG_NE_PILOT_REQ_INIT                                   0x0

#define CE_REG_NE_PILOT_REQ_VALID__A                                 0x1810041
#define CE_REG_NE_PILOT_REQ_VALID__W                                 2
#define CE_REG_NE_PILOT_REQ_VALID__M                                 0x3
#define   CE_REG_NE_PILOT_REQ_VALID_INIT                             0x0
#define   CE_REG_NE_PILOT_REQ_VALID_WRITE_VALID__B                   1
#define   CE_REG_NE_PILOT_REQ_VALID_WRITE_VALID__W                   1
#define   CE_REG_NE_PILOT_REQ_VALID_WRITE_VALID__M                   0x2
#define   CE_REG_NE_PILOT_REQ_VALID_READ_VALID__B                    0
#define   CE_REG_NE_PILOT_REQ_VALID_READ_VALID__W                    1
#define   CE_REG_NE_PILOT_REQ_VALID_READ_VALID__M                    0x1

#define CE_REG_NE_PILOT_DATA__A                                      0x1810042
#define CE_REG_NE_PILOT_DATA__W                                      10
#define CE_REG_NE_PILOT_DATA__M                                      0x3FF
#define   CE_REG_NE_PILOT_DATA_INIT                                  0x0

#define CE_REG_NE_ERR_SELECT__A                                      0x1810043
#define CE_REG_NE_ERR_SELECT__W                                      3
#define CE_REG_NE_ERR_SELECT__M                                      0x7
#define   CE_REG_NE_ERR_SELECT_INIT                                  0x0

#define   CE_REG_NE_ERR_SELECT_RESET_RAM__B                          2
#define   CE_REG_NE_ERR_SELECT_RESET_RAM__W                          1
#define   CE_REG_NE_ERR_SELECT_RESET_RAM__M                          0x4

#define   CE_REG_NE_ERR_SELECT_FD_ENABLE__B                          1
#define   CE_REG_NE_ERR_SELECT_FD_ENABLE__W                          1
#define   CE_REG_NE_ERR_SELECT_FD_ENABLE__M                          0x2

#define   CE_REG_NE_ERR_SELECT_TD_ENABLE__B                          0
#define   CE_REG_NE_ERR_SELECT_TD_ENABLE__W                          1
#define   CE_REG_NE_ERR_SELECT_TD_ENABLE__M                          0x1

#define CE_REG_NE_TD_CAL__A                                          0x1810044
#define CE_REG_NE_TD_CAL__W                                          9
#define CE_REG_NE_TD_CAL__M                                          0x1FF
#define   CE_REG_NE_TD_CAL_INIT                                      0x0

#define CE_REG_NE_FD_CAL__A                                          0x1810045
#define CE_REG_NE_FD_CAL__W                                          9
#define CE_REG_NE_FD_CAL__M                                          0x1FF
#define   CE_REG_NE_FD_CAL_INIT                                      0x0

#define CE_REG_NE_MIXAVG__A                                          0x1810046
#define CE_REG_NE_MIXAVG__W                                          3
#define CE_REG_NE_MIXAVG__M                                          0x7
#define   CE_REG_NE_MIXAVG_INIT                                      0x0

#define CE_REG_NE_NUPD_OFS__A                                        0x1810047
#define CE_REG_NE_NUPD_OFS__W                                        7
#define CE_REG_NE_NUPD_OFS__M                                        0x7F
#define   CE_REG_NE_NUPD_OFS_INIT                                    0x0

#define CE_REG_NE_TD_POW__A                                          0x1810048
#define CE_REG_NE_TD_POW__W                                          15
#define CE_REG_NE_TD_POW__M                                          0x7FFF
#define   CE_REG_NE_TD_POW_INIT                                      0x0

#define   CE_REG_NE_TD_POW_EXPONENT__B                               10
#define   CE_REG_NE_TD_POW_EXPONENT__W                               5
#define   CE_REG_NE_TD_POW_EXPONENT__M                               0x7C00

#define   CE_REG_NE_TD_POW_MANTISSA__B                               0
#define   CE_REG_NE_TD_POW_MANTISSA__W                               10
#define   CE_REG_NE_TD_POW_MANTISSA__M                               0x3FF

#define CE_REG_NE_FD_POW__A                                          0x1810049
#define CE_REG_NE_FD_POW__W                                          15
#define CE_REG_NE_FD_POW__M                                          0x7FFF
#define   CE_REG_NE_FD_POW_INIT                                      0x0

#define   CE_REG_NE_FD_POW_EXPONENT__B                               10
#define   CE_REG_NE_FD_POW_EXPONENT__W                               5
#define   CE_REG_NE_FD_POW_EXPONENT__M                               0x7C00

#define   CE_REG_NE_FD_POW_MANTISSA__B                               0
#define   CE_REG_NE_FD_POW_MANTISSA__W                               10
#define   CE_REG_NE_FD_POW_MANTISSA__M                               0x3FF

#define CE_REG_NE_NEXP_AVG__A                                        0x181004A
#define CE_REG_NE_NEXP_AVG__W                                        8
#define CE_REG_NE_NEXP_AVG__M                                        0xFF
#define   CE_REG_NE_NEXP_AVG_INIT                                    0x0

#define CE_REG_NE_OFFSET__A                                          0x181004B
#define CE_REG_NE_OFFSET__W                                          9
#define CE_REG_NE_OFFSET__M                                          0x1FF
#define   CE_REG_NE_OFFSET_INIT                                      0x0

#define CE_REG_PE_NEXP_OFFS__A                                       0x1810050
#define CE_REG_PE_NEXP_OFFS__W                                       8
#define CE_REG_PE_NEXP_OFFS__M                                       0xFF
#define   CE_REG_PE_NEXP_OFFS_INIT                                   0x0

#define CE_REG_PE_TIMESHIFT__A                                       0x1810051
#define CE_REG_PE_TIMESHIFT__W                                       14
#define CE_REG_PE_TIMESHIFT__M                                       0x3FFF
#define   CE_REG_PE_TIMESHIFT_INIT                                   0x0

#define CE_REG_PE_DIF_REAL_L__A                                      0x1810052
#define CE_REG_PE_DIF_REAL_L__W                                      16
#define CE_REG_PE_DIF_REAL_L__M                                      0xFFFF
#define   CE_REG_PE_DIF_REAL_L_INIT                                  0x0

#define CE_REG_PE_DIF_IMAG_L__A                                      0x1810053
#define CE_REG_PE_DIF_IMAG_L__W                                      16
#define CE_REG_PE_DIF_IMAG_L__M                                      0xFFFF
#define   CE_REG_PE_DIF_IMAG_L_INIT                                  0x0

#define CE_REG_PE_DIF_REAL_R__A                                      0x1810054
#define CE_REG_PE_DIF_REAL_R__W                                      16
#define CE_REG_PE_DIF_REAL_R__M                                      0xFFFF
#define   CE_REG_PE_DIF_REAL_R_INIT                                  0x0

#define CE_REG_PE_DIF_IMAG_R__A                                      0x1810055
#define CE_REG_PE_DIF_IMAG_R__W                                      16
#define CE_REG_PE_DIF_IMAG_R__M                                      0xFFFF
#define   CE_REG_PE_DIF_IMAG_R_INIT                                  0x0

#define CE_REG_PE_ABS_REAL_L__A                                      0x1810056
#define CE_REG_PE_ABS_REAL_L__W                                      16
#define CE_REG_PE_ABS_REAL_L__M                                      0xFFFF
#define   CE_REG_PE_ABS_REAL_L_INIT                                  0x0

#define CE_REG_PE_ABS_IMAG_L__A                                      0x1810057
#define CE_REG_PE_ABS_IMAG_L__W                                      16
#define CE_REG_PE_ABS_IMAG_L__M                                      0xFFFF
#define   CE_REG_PE_ABS_IMAG_L_INIT                                  0x0

#define CE_REG_PE_ABS_REAL_R__A                                      0x1810058
#define CE_REG_PE_ABS_REAL_R__W                                      16
#define CE_REG_PE_ABS_REAL_R__M                                      0xFFFF
#define   CE_REG_PE_ABS_REAL_R_INIT                                  0x0

#define CE_REG_PE_ABS_IMAG_R__A                                      0x1810059
#define CE_REG_PE_ABS_IMAG_R__W                                      16
#define CE_REG_PE_ABS_IMAG_R__M                                      0xFFFF
#define   CE_REG_PE_ABS_IMAG_R_INIT                                  0x0

#define CE_REG_PE_ABS_EXP_L__A                                       0x181005A
#define CE_REG_PE_ABS_EXP_L__W                                       5
#define CE_REG_PE_ABS_EXP_L__M                                       0x1F
#define   CE_REG_PE_ABS_EXP_L_INIT                                   0x0

#define CE_REG_PE_ABS_EXP_R__A                                       0x181005B
#define CE_REG_PE_ABS_EXP_R__W                                       5
#define CE_REG_PE_ABS_EXP_R__M                                       0x1F
#define   CE_REG_PE_ABS_EXP_R_INIT                                   0x0

#define CE_REG_TP_UPDATE_MODE__A                                     0x1810060
#define CE_REG_TP_UPDATE_MODE__W                                     1
#define CE_REG_TP_UPDATE_MODE__M                                     0x1
#define   CE_REG_TP_UPDATE_MODE_INIT                                 0x0

#define CE_REG_TP_LMS_TAP_ON__A                                      0x1810061
#define CE_REG_TP_LMS_TAP_ON__W                                      1
#define CE_REG_TP_LMS_TAP_ON__M                                      0x1

#define CE_REG_TP_A0_TAP_NEW__A                                      0x1810064
#define CE_REG_TP_A0_TAP_NEW__W                                      10
#define CE_REG_TP_A0_TAP_NEW__M                                      0x3FF

#define CE_REG_TP_A0_TAP_NEW_VALID__A                                0x1810065
#define CE_REG_TP_A0_TAP_NEW_VALID__W                                1
#define CE_REG_TP_A0_TAP_NEW_VALID__M                                0x1

#define CE_REG_TP_A0_MU_LMS_STEP__A                                  0x1810066
#define CE_REG_TP_A0_MU_LMS_STEP__W                                  5
#define CE_REG_TP_A0_MU_LMS_STEP__M                                  0x1F

#define CE_REG_TP_A0_TAP_CURR__A                                     0x1810067
#define CE_REG_TP_A0_TAP_CURR__W                                     10
#define CE_REG_TP_A0_TAP_CURR__M                                     0x3FF

#define CE_REG_TP_A1_TAP_NEW__A                                      0x1810068
#define CE_REG_TP_A1_TAP_NEW__W                                      10
#define CE_REG_TP_A1_TAP_NEW__M                                      0x3FF

#define CE_REG_TP_A1_TAP_NEW_VALID__A                                0x1810069
#define CE_REG_TP_A1_TAP_NEW_VALID__W                                1
#define CE_REG_TP_A1_TAP_NEW_VALID__M                                0x1

#define CE_REG_TP_A1_MU_LMS_STEP__A                                  0x181006A
#define CE_REG_TP_A1_MU_LMS_STEP__W                                  5
#define CE_REG_TP_A1_MU_LMS_STEP__M                                  0x1F

#define CE_REG_TP_A1_TAP_CURR__A                                     0x181006B
#define CE_REG_TP_A1_TAP_CURR__W                                     10
#define CE_REG_TP_A1_TAP_CURR__M                                     0x3FF

#define CE_REG_TP_DOPP_ENERGY__A                                     0x181006C
#define CE_REG_TP_DOPP_ENERGY__W                                     15
#define CE_REG_TP_DOPP_ENERGY__M                                     0x7FFF
#define   CE_REG_TP_DOPP_ENERGY_INIT                                 0x0

#define   CE_REG_TP_DOPP_ENERGY_EXPONENT__B                          10
#define   CE_REG_TP_DOPP_ENERGY_EXPONENT__W                          5
#define   CE_REG_TP_DOPP_ENERGY_EXPONENT__M                          0x7C00

#define   CE_REG_TP_DOPP_ENERGY_MANTISSA__B                          0
#define   CE_REG_TP_DOPP_ENERGY_MANTISSA__W                          10
#define   CE_REG_TP_DOPP_ENERGY_MANTISSA__M                          0x3FF

#define CE_REG_TP_DOPP_DIFF_ENERGY__A                                0x181006D
#define CE_REG_TP_DOPP_DIFF_ENERGY__W                                15
#define CE_REG_TP_DOPP_DIFF_ENERGY__M                                0x7FFF
#define   CE_REG_TP_DOPP_DIFF_ENERGY_INIT                            0x0

#define   CE_REG_TP_DOPP_DIFF_ENERGY_EXPONENT__B                     10
#define   CE_REG_TP_DOPP_DIFF_ENERGY_EXPONENT__W                     5
#define   CE_REG_TP_DOPP_DIFF_ENERGY_EXPONENT__M                     0x7C00

#define   CE_REG_TP_DOPP_DIFF_ENERGY_MANTISSA__B                     0
#define   CE_REG_TP_DOPP_DIFF_ENERGY_MANTISSA__W                     10
#define   CE_REG_TP_DOPP_DIFF_ENERGY_MANTISSA__M                     0x3FF

#define CE_REG_TP_A0_TAP_ENERGY__A                                   0x181006E
#define CE_REG_TP_A0_TAP_ENERGY__W                                   15
#define CE_REG_TP_A0_TAP_ENERGY__M                                   0x7FFF
#define   CE_REG_TP_A0_TAP_ENERGY_INIT                               0x0

#define   CE_REG_TP_A0_TAP_ENERGY_EXPONENT__B                        10
#define   CE_REG_TP_A0_TAP_ENERGY_EXPONENT__W                        5
#define   CE_REG_TP_A0_TAP_ENERGY_EXPONENT__M                        0x7C00

#define   CE_REG_TP_A0_TAP_ENERGY_MANTISSA__B                        0
#define   CE_REG_TP_A0_TAP_ENERGY_MANTISSA__W                        10
#define   CE_REG_TP_A0_TAP_ENERGY_MANTISSA__M                        0x3FF

#define CE_REG_TP_A1_TAP_ENERGY__A                                   0x181006F
#define CE_REG_TP_A1_TAP_ENERGY__W                                   15
#define CE_REG_TP_A1_TAP_ENERGY__M                                   0x7FFF
#define   CE_REG_TP_A1_TAP_ENERGY_INIT                               0x0

#define   CE_REG_TP_A1_TAP_ENERGY_EXPONENT__B                        10
#define   CE_REG_TP_A1_TAP_ENERGY_EXPONENT__W                        5
#define   CE_REG_TP_A1_TAP_ENERGY_EXPONENT__M                        0x7C00

#define   CE_REG_TP_A1_TAP_ENERGY_MANTISSA__B                        0
#define   CE_REG_TP_A1_TAP_ENERGY_MANTISSA__W                        10
#define   CE_REG_TP_A1_TAP_ENERGY_MANTISSA__M                        0x3FF

#define CE_REG_TI_NEXP_OFFS__A                                       0x1810070
#define CE_REG_TI_NEXP_OFFS__W                                       8
#define CE_REG_TI_NEXP_OFFS__M                                       0xFF
#define   CE_REG_TI_NEXP_OFFS_INIT                                   0x0

#define CE_REG_TI_PEAK__A                                            0x1810071
#define CE_REG_TI_PEAK__W                                            8
#define CE_REG_TI_PEAK__M                                            0xFF
#define   CE_REG_TI_PEAK_INIT                                        0x0

#define CE_REG_FI_SHT_INCR__A                                        0x1810090
#define CE_REG_FI_SHT_INCR__W                                        7
#define CE_REG_FI_SHT_INCR__M                                        0x7F
#define   CE_REG_FI_SHT_INCR_INIT                                    0x9

#define CE_REG_FI_EXP_NORM__A                                        0x1810091
#define CE_REG_FI_EXP_NORM__W                                        4
#define CE_REG_FI_EXP_NORM__M                                        0xF
#define   CE_REG_FI_EXP_NORM_INIT                                    0x4

#define CE_REG_FI_SUPR_VAL__A                                        0x1810092
#define CE_REG_FI_SUPR_VAL__W                                        1
#define CE_REG_FI_SUPR_VAL__M                                        0x1
#define   CE_REG_FI_SUPR_VAL_INIT                                    0x1

#define CE_REG_IR_INPUTSEL__A                                        0x18100A0
#define CE_REG_IR_INPUTSEL__W                                        1
#define CE_REG_IR_INPUTSEL__M                                        0x1
#define   CE_REG_IR_INPUTSEL_INIT                                    0x0

#define CE_REG_IR_STARTPOS__A                                        0x18100A1
#define CE_REG_IR_STARTPOS__W                                        8
#define CE_REG_IR_STARTPOS__M                                        0xFF
#define   CE_REG_IR_STARTPOS_INIT                                    0x0

#define CE_REG_IR_NEXP_THRES__A                                      0x18100A2
#define CE_REG_IR_NEXP_THRES__W                                      8
#define CE_REG_IR_NEXP_THRES__M                                      0xFF
#define   CE_REG_IR_NEXP_THRES_INIT                                  0x0

#define CE_REG_IR_LENGTH__A                                          0x18100A3
#define CE_REG_IR_LENGTH__W                                          4
#define CE_REG_IR_LENGTH__M                                          0xF
#define   CE_REG_IR_LENGTH_INIT                                      0x0

#define CE_REG_IR_FREQ__A                                            0x18100A4
#define CE_REG_IR_FREQ__W                                            11
#define CE_REG_IR_FREQ__M                                            0x7FF
#define   CE_REG_IR_FREQ_INIT                                        0x0

#define CE_REG_IR_FREQINC__A                                         0x18100A5
#define CE_REG_IR_FREQINC__W                                         11
#define CE_REG_IR_FREQINC__M                                         0x7FF
#define   CE_REG_IR_FREQINC_INIT                                     0x0

#define CE_REG_IR_KAISINC__A                                         0x18100A6
#define CE_REG_IR_KAISINC__W                                         15
#define CE_REG_IR_KAISINC__M                                         0x7FFF
#define   CE_REG_IR_KAISINC_INIT                                     0x0

#define CE_REG_IR_CTL__A                                             0x18100A7
#define CE_REG_IR_CTL__W                                             3
#define CE_REG_IR_CTL__M                                             0x7
#define   CE_REG_IR_CTL_INIT                                         0x0

#define CE_REG_IR_REAL__A                                            0x18100A8
#define CE_REG_IR_REAL__W                                            16
#define CE_REG_IR_REAL__M                                            0xFFFF
#define   CE_REG_IR_REAL_INIT                                        0x0

#define CE_REG_IR_IMAG__A                                            0x18100A9
#define CE_REG_IR_IMAG__W                                            16
#define CE_REG_IR_IMAG__M                                            0xFFFF
#define   CE_REG_IR_IMAG_INIT                                        0x0

#define CE_REG_IR_INDEX__A                                           0x18100AA
#define CE_REG_IR_INDEX__W                                           12
#define CE_REG_IR_INDEX__M                                           0xFFF
#define   CE_REG_IR_INDEX_INIT                                       0x0

#define CE_REG_FR_TREAL00__A                                         0x1820010
#define CE_REG_FR_TREAL00__W                                         11
#define CE_REG_FR_TREAL00__M                                         0x7FF
#define   CE_REG_FR_TREAL00_INIT                                     0x52

#define CE_REG_FR_TIMAG00__A                                         0x1820011
#define CE_REG_FR_TIMAG00__W                                         11
#define CE_REG_FR_TIMAG00__M                                         0x7FF
#define   CE_REG_FR_TIMAG00_INIT                                     0x0

#define CE_REG_FR_TREAL01__A                                         0x1820012
#define CE_REG_FR_TREAL01__W                                         11
#define CE_REG_FR_TREAL01__M                                         0x7FF
#define   CE_REG_FR_TREAL01_INIT                                     0x52

#define CE_REG_FR_TIMAG01__A                                         0x1820013
#define CE_REG_FR_TIMAG01__W                                         11
#define CE_REG_FR_TIMAG01__M                                         0x7FF
#define   CE_REG_FR_TIMAG01_INIT                                     0x0

#define CE_REG_FR_TREAL02__A                                         0x1820014
#define CE_REG_FR_TREAL02__W                                         11
#define CE_REG_FR_TREAL02__M                                         0x7FF
#define   CE_REG_FR_TREAL02_INIT                                     0x52

#define CE_REG_FR_TIMAG02__A                                         0x1820015
#define CE_REG_FR_TIMAG02__W                                         11
#define CE_REG_FR_TIMAG02__M                                         0x7FF
#define   CE_REG_FR_TIMAG02_INIT                                     0x0

#define CE_REG_FR_TREAL03__A                                         0x1820016
#define CE_REG_FR_TREAL03__W                                         11
#define CE_REG_FR_TREAL03__M                                         0x7FF
#define   CE_REG_FR_TREAL03_INIT                                     0x52

#define CE_REG_FR_TIMAG03__A                                         0x1820017
#define CE_REG_FR_TIMAG03__W                                         11
#define CE_REG_FR_TIMAG03__M                                         0x7FF
#define   CE_REG_FR_TIMAG03_INIT                                     0x0

#define CE_REG_FR_TREAL04__A                                         0x1820018
#define CE_REG_FR_TREAL04__W                                         11
#define CE_REG_FR_TREAL04__M                                         0x7FF
#define   CE_REG_FR_TREAL04_INIT                                     0x52

#define CE_REG_FR_TIMAG04__A                                         0x1820019
#define CE_REG_FR_TIMAG04__W                                         11
#define CE_REG_FR_TIMAG04__M                                         0x7FF
#define   CE_REG_FR_TIMAG04_INIT                                     0x0

#define CE_REG_FR_TREAL05__A                                         0x182001A
#define CE_REG_FR_TREAL05__W                                         11
#define CE_REG_FR_TREAL05__M                                         0x7FF
#define   CE_REG_FR_TREAL05_INIT                                     0x52

#define CE_REG_FR_TIMAG05__A                                         0x182001B
#define CE_REG_FR_TIMAG05__W                                         11
#define CE_REG_FR_TIMAG05__M                                         0x7FF
#define   CE_REG_FR_TIMAG05_INIT                                     0x0

#define CE_REG_FR_TREAL06__A                                         0x182001C
#define CE_REG_FR_TREAL06__W                                         11
#define CE_REG_FR_TREAL06__M                                         0x7FF
#define   CE_REG_FR_TREAL06_INIT                                     0x52

#define CE_REG_FR_TIMAG06__A                                         0x182001D
#define CE_REG_FR_TIMAG06__W                                         11
#define CE_REG_FR_TIMAG06__M                                         0x7FF
#define   CE_REG_FR_TIMAG06_INIT                                     0x0

#define CE_REG_FR_TREAL07__A                                         0x182001E
#define CE_REG_FR_TREAL07__W                                         11
#define CE_REG_FR_TREAL07__M                                         0x7FF
#define   CE_REG_FR_TREAL07_INIT                                     0x52

#define CE_REG_FR_TIMAG07__A                                         0x182001F
#define CE_REG_FR_TIMAG07__W                                         11
#define CE_REG_FR_TIMAG07__M                                         0x7FF
#define   CE_REG_FR_TIMAG07_INIT                                     0x0

#define CE_REG_FR_TREAL08__A                                         0x1820020
#define CE_REG_FR_TREAL08__W                                         11
#define CE_REG_FR_TREAL08__M                                         0x7FF
#define   CE_REG_FR_TREAL08_INIT                                     0x52

#define CE_REG_FR_TIMAG08__A                                         0x1820021
#define CE_REG_FR_TIMAG08__W                                         11
#define CE_REG_FR_TIMAG08__M                                         0x7FF
#define   CE_REG_FR_TIMAG08_INIT                                     0x0

#define CE_REG_FR_TREAL09__A                                         0x1820022
#define CE_REG_FR_TREAL09__W                                         11
#define CE_REG_FR_TREAL09__M                                         0x7FF
#define   CE_REG_FR_TREAL09_INIT                                     0x52

#define CE_REG_FR_TIMAG09__A                                         0x1820023
#define CE_REG_FR_TIMAG09__W                                         11
#define CE_REG_FR_TIMAG09__M                                         0x7FF
#define   CE_REG_FR_TIMAG09_INIT                                     0x0

#define CE_REG_FR_TREAL10__A                                         0x1820024
#define CE_REG_FR_TREAL10__W                                         11
#define CE_REG_FR_TREAL10__M                                         0x7FF
#define   CE_REG_FR_TREAL10_INIT                                     0x52

#define CE_REG_FR_TIMAG10__A                                         0x1820025
#define CE_REG_FR_TIMAG10__W                                         11
#define CE_REG_FR_TIMAG10__M                                         0x7FF
#define   CE_REG_FR_TIMAG10_INIT                                     0x0

#define CE_REG_FR_TREAL11__A                                         0x1820026
#define CE_REG_FR_TREAL11__W                                         11
#define CE_REG_FR_TREAL11__M                                         0x7FF
#define   CE_REG_FR_TREAL11_INIT                                     0x52

#define CE_REG_FR_TIMAG11__A                                         0x1820027
#define CE_REG_FR_TIMAG11__W                                         11
#define CE_REG_FR_TIMAG11__M                                         0x7FF
#define   CE_REG_FR_TIMAG11_INIT                                     0x0

#define CE_REG_FR_MID_TAP__A                                         0x1820028
#define CE_REG_FR_MID_TAP__W                                         11
#define CE_REG_FR_MID_TAP__M                                         0x7FF
#define   CE_REG_FR_MID_TAP_INIT                                     0x51

#define CE_REG_FR_SQS_G00__A                                         0x1820029
#define CE_REG_FR_SQS_G00__W                                         8
#define CE_REG_FR_SQS_G00__M                                         0xFF
#define   CE_REG_FR_SQS_G00_INIT                                     0xB

#define CE_REG_FR_SQS_G01__A                                         0x182002A
#define CE_REG_FR_SQS_G01__W                                         8
#define CE_REG_FR_SQS_G01__M                                         0xFF
#define   CE_REG_FR_SQS_G01_INIT                                     0xB

#define CE_REG_FR_SQS_G02__A                                         0x182002B
#define CE_REG_FR_SQS_G02__W                                         8
#define CE_REG_FR_SQS_G02__M                                         0xFF
#define   CE_REG_FR_SQS_G02_INIT                                     0xB

#define CE_REG_FR_SQS_G03__A                                         0x182002C
#define CE_REG_FR_SQS_G03__W                                         8
#define CE_REG_FR_SQS_G03__M                                         0xFF
#define   CE_REG_FR_SQS_G03_INIT                                     0xB

#define CE_REG_FR_SQS_G04__A                                         0x182002D
#define CE_REG_FR_SQS_G04__W                                         8
#define CE_REG_FR_SQS_G04__M                                         0xFF
#define   CE_REG_FR_SQS_G04_INIT                                     0xB

#define CE_REG_FR_SQS_G05__A                                         0x182002E
#define CE_REG_FR_SQS_G05__W                                         8
#define CE_REG_FR_SQS_G05__M                                         0xFF
#define   CE_REG_FR_SQS_G05_INIT                                     0xB

#define CE_REG_FR_SQS_G06__A                                         0x182002F
#define CE_REG_FR_SQS_G06__W                                         8
#define CE_REG_FR_SQS_G06__M                                         0xFF
#define   CE_REG_FR_SQS_G06_INIT                                     0xB

#define CE_REG_FR_SQS_G07__A                                         0x1820030
#define CE_REG_FR_SQS_G07__W                                         8
#define CE_REG_FR_SQS_G07__M                                         0xFF
#define   CE_REG_FR_SQS_G07_INIT                                     0xB

#define CE_REG_FR_SQS_G08__A                                         0x1820031
#define CE_REG_FR_SQS_G08__W                                         8
#define CE_REG_FR_SQS_G08__M                                         0xFF
#define   CE_REG_FR_SQS_G08_INIT                                     0xB

#define CE_REG_FR_SQS_G09__A                                         0x1820032
#define CE_REG_FR_SQS_G09__W                                         8
#define CE_REG_FR_SQS_G09__M                                         0xFF
#define   CE_REG_FR_SQS_G09_INIT                                     0xB

#define CE_REG_FR_SQS_G10__A                                         0x1820033
#define CE_REG_FR_SQS_G10__W                                         8
#define CE_REG_FR_SQS_G10__M                                         0xFF
#define   CE_REG_FR_SQS_G10_INIT                                     0xB

#define CE_REG_FR_SQS_G11__A                                         0x1820034
#define CE_REG_FR_SQS_G11__W                                         8
#define CE_REG_FR_SQS_G11__M                                         0xFF
#define   CE_REG_FR_SQS_G11_INIT                                     0xB

#define CE_REG_FR_SQS_G12__A                                         0x1820035
#define CE_REG_FR_SQS_G12__W                                         8
#define CE_REG_FR_SQS_G12__M                                         0xFF
#define   CE_REG_FR_SQS_G12_INIT                                     0x5

#define CE_REG_FR_RIO_G00__A                                         0x1820036
#define CE_REG_FR_RIO_G00__W                                         9
#define CE_REG_FR_RIO_G00__M                                         0x1FF
#define   CE_REG_FR_RIO_G00_INIT                                     0x1FF

#define CE_REG_FR_RIO_G01__A                                         0x1820037
#define CE_REG_FR_RIO_G01__W                                         9
#define CE_REG_FR_RIO_G01__M                                         0x1FF
#define   CE_REG_FR_RIO_G01_INIT                                     0x190

#define CE_REG_FR_RIO_G02__A                                         0x1820038
#define CE_REG_FR_RIO_G02__W                                         9
#define CE_REG_FR_RIO_G02__M                                         0x1FF
#define   CE_REG_FR_RIO_G02_INIT                                     0x10B

#define CE_REG_FR_RIO_G03__A                                         0x1820039
#define CE_REG_FR_RIO_G03__W                                         9
#define CE_REG_FR_RIO_G03__M                                         0x1FF
#define   CE_REG_FR_RIO_G03_INIT                                     0xC8

#define CE_REG_FR_RIO_G04__A                                         0x182003A
#define CE_REG_FR_RIO_G04__W                                         9
#define CE_REG_FR_RIO_G04__M                                         0x1FF
#define   CE_REG_FR_RIO_G04_INIT                                     0xA0

#define CE_REG_FR_RIO_G05__A                                         0x182003B
#define CE_REG_FR_RIO_G05__W                                         9
#define CE_REG_FR_RIO_G05__M                                         0x1FF
#define   CE_REG_FR_RIO_G05_INIT                                     0x85

#define CE_REG_FR_RIO_G06__A                                         0x182003C
#define CE_REG_FR_RIO_G06__W                                         9
#define CE_REG_FR_RIO_G06__M                                         0x1FF
#define   CE_REG_FR_RIO_G06_INIT                                     0x72

#define CE_REG_FR_RIO_G07__A                                         0x182003D
#define CE_REG_FR_RIO_G07__W                                         9
#define CE_REG_FR_RIO_G07__M                                         0x1FF
#define   CE_REG_FR_RIO_G07_INIT                                     0x64

#define CE_REG_FR_RIO_G08__A                                         0x182003E
#define CE_REG_FR_RIO_G08__W                                         9
#define CE_REG_FR_RIO_G08__M                                         0x1FF
#define   CE_REG_FR_RIO_G08_INIT                                     0x59

#define CE_REG_FR_RIO_G09__A                                         0x182003F
#define CE_REG_FR_RIO_G09__W                                         9
#define CE_REG_FR_RIO_G09__M                                         0x1FF
#define   CE_REG_FR_RIO_G09_INIT                                     0x50

#define CE_REG_FR_RIO_G10__A                                         0x1820040
#define CE_REG_FR_RIO_G10__W                                         9
#define CE_REG_FR_RIO_G10__M                                         0x1FF
#define   CE_REG_FR_RIO_G10_INIT                                     0x49

#define CE_REG_FR_MODE__A                                            0x1820041
#define CE_REG_FR_MODE__W                                            6
#define CE_REG_FR_MODE__M                                            0x3F

#define   CE_REG_FR_MODE_UPDATE_ENABLE__B                            0
#define   CE_REG_FR_MODE_UPDATE_ENABLE__W                            1
#define   CE_REG_FR_MODE_UPDATE_ENABLE__M                            0x1

#define   CE_REG_FR_MODE_ERROR_SHIFT__B                              1
#define   CE_REG_FR_MODE_ERROR_SHIFT__W                              1
#define   CE_REG_FR_MODE_ERROR_SHIFT__M                              0x2

#define   CE_REG_FR_MODE_NEXP_UPDATE__B                              2
#define   CE_REG_FR_MODE_NEXP_UPDATE__W                              1
#define   CE_REG_FR_MODE_NEXP_UPDATE__M                              0x4

#define   CE_REG_FR_MODE_MANUAL_SHIFT__B                             3
#define   CE_REG_FR_MODE_MANUAL_SHIFT__W                             1
#define   CE_REG_FR_MODE_MANUAL_SHIFT__M                             0x8

#define   CE_REG_FR_MODE_SQUASH_MODE__B                              4
#define   CE_REG_FR_MODE_SQUASH_MODE__W                              1
#define   CE_REG_FR_MODE_SQUASH_MODE__M                              0x10

#define   CE_REG_FR_MODE_UPDATE_MODE__B                              5
#define   CE_REG_FR_MODE_UPDATE_MODE__W                              1
#define   CE_REG_FR_MODE_UPDATE_MODE__M                              0x20
#define     CE_REG_FR_MODE_INIT                                      0x3E

#define CE_REG_FR_SQS_TRH__A                                         0x1820042
#define CE_REG_FR_SQS_TRH__W                                         8
#define CE_REG_FR_SQS_TRH__M                                         0xFF
#define   CE_REG_FR_SQS_TRH_INIT                                     0x80

#define CE_REG_FR_RIO_GAIN__A                                        0x1820043
#define CE_REG_FR_RIO_GAIN__W                                        3
#define CE_REG_FR_RIO_GAIN__M                                        0x7
#define   CE_REG_FR_RIO_GAIN_INIT                                    0x2

#define CE_REG_FR_BYPASS__A                                          0x1820044
#define CE_REG_FR_BYPASS__W                                          10
#define CE_REG_FR_BYPASS__M                                          0x3FF

#define   CE_REG_FR_BYPASS_RUN_IN__B                                 0
#define   CE_REG_FR_BYPASS_RUN_IN__W                                 4
#define   CE_REG_FR_BYPASS_RUN_IN__M                                 0xF

#define   CE_REG_FR_BYPASS_RUN_SEMI_IN__B                            4
#define   CE_REG_FR_BYPASS_RUN_SEMI_IN__W                            5
#define   CE_REG_FR_BYPASS_RUN_SEMI_IN__M                            0x1F0

#define   CE_REG_FR_BYPASS_TOTAL__B                                  9
#define   CE_REG_FR_BYPASS_TOTAL__W                                  1
#define   CE_REG_FR_BYPASS_TOTAL__M                                  0x200
#define     CE_REG_FR_BYPASS_INIT                                    0x13B

#define CE_REG_FR_PM_SET__A                                          0x1820045
#define CE_REG_FR_PM_SET__W                                          4
#define CE_REG_FR_PM_SET__M                                          0xF
#define   CE_REG_FR_PM_SET_INIT                                      0x4

#define CE_REG_FR_ERR_SH__A                                          0x1820046
#define CE_REG_FR_ERR_SH__W                                          4
#define CE_REG_FR_ERR_SH__M                                          0xF
#define   CE_REG_FR_ERR_SH_INIT                                      0x4

#define CE_REG_FR_MAN_SH__A                                          0x1820047
#define CE_REG_FR_MAN_SH__W                                          4
#define CE_REG_FR_MAN_SH__M                                          0xF
#define   CE_REG_FR_MAN_SH_INIT                                      0x7

#define CE_REG_FR_TAP_SH__A                                          0x1820048
#define CE_REG_FR_TAP_SH__W                                          3
#define CE_REG_FR_TAP_SH__M                                          0x7
#define   CE_REG_FR_TAP_SH_INIT                                      0x3

#define CE_REG_FR_CLIP__A                                            0x1820049
#define CE_REG_FR_CLIP__W                                            9
#define CE_REG_FR_CLIP__M                                            0x1FF
#define   CE_REG_FR_CLIP_INIT                                        0x49

#define CE_PB_RAM__A                                                 0x1830000

#define CE_NE_RAM__A                                                 0x1840000

#define EQ_SID                                                       0xE

#define EQ_COMM_EXEC__A                                              0x1C00000
#define EQ_COMM_EXEC__W                                              3
#define EQ_COMM_EXEC__M                                              0x7
#define   EQ_COMM_EXEC_CTL__B                                        0
#define   EQ_COMM_EXEC_CTL__W                                        3
#define   EQ_COMM_EXEC_CTL__M                                        0x7
#define     EQ_COMM_EXEC_CTL_STOP                                    0x0
#define     EQ_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     EQ_COMM_EXEC_CTL_HOLD                                    0x2
#define     EQ_COMM_EXEC_CTL_STEP                                    0x3
#define     EQ_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     EQ_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define EQ_COMM_STATE__A                                             0x1C00001
#define EQ_COMM_STATE__W                                             16
#define EQ_COMM_STATE__M                                             0xFFFF
#define EQ_COMM_MB__A                                                0x1C00002
#define EQ_COMM_MB__W                                                16
#define EQ_COMM_MB__M                                                0xFFFF
#define EQ_COMM_SERVICE0__A                                          0x1C00003
#define EQ_COMM_SERVICE0__W                                          16
#define EQ_COMM_SERVICE0__M                                          0xFFFF
#define EQ_COMM_SERVICE1__A                                          0x1C00004
#define EQ_COMM_SERVICE1__W                                          16
#define EQ_COMM_SERVICE1__M                                          0xFFFF
#define EQ_COMM_INT_STA__A                                           0x1C00007
#define EQ_COMM_INT_STA__W                                           16
#define EQ_COMM_INT_STA__M                                           0xFFFF
#define EQ_COMM_INT_MSK__A                                           0x1C00008
#define EQ_COMM_INT_MSK__W                                           16
#define EQ_COMM_INT_MSK__M                                           0xFFFF

#define EQ_REG_COMM_EXEC__A                                          0x1C10000
#define EQ_REG_COMM_EXEC__W                                          3
#define EQ_REG_COMM_EXEC__M                                          0x7
#define   EQ_REG_COMM_EXEC_CTL__B                                    0
#define   EQ_REG_COMM_EXEC_CTL__W                                    3
#define   EQ_REG_COMM_EXEC_CTL__M                                    0x7
#define     EQ_REG_COMM_EXEC_CTL_STOP                                0x0
#define     EQ_REG_COMM_EXEC_CTL_ACTIVE                              0x1
#define     EQ_REG_COMM_EXEC_CTL_HOLD                                0x2
#define     EQ_REG_COMM_EXEC_CTL_STEP                                0x3

#define EQ_REG_COMM_STATE__A                                         0x1C10001
#define EQ_REG_COMM_STATE__W                                         4
#define EQ_REG_COMM_STATE__M                                         0xF

#define EQ_REG_COMM_MB__A                                            0x1C10002
#define EQ_REG_COMM_MB__W                                            6
#define EQ_REG_COMM_MB__M                                            0x3F
#define   EQ_REG_COMM_MB_CTR__B                                      0
#define   EQ_REG_COMM_MB_CTR__W                                      1
#define   EQ_REG_COMM_MB_CTR__M                                      0x1
#define     EQ_REG_COMM_MB_CTR_OFF                                   0x0
#define     EQ_REG_COMM_MB_CTR_ON                                    0x1
#define   EQ_REG_COMM_MB_OBS__B                                      1
#define   EQ_REG_COMM_MB_OBS__W                                      1
#define   EQ_REG_COMM_MB_OBS__M                                      0x2
#define     EQ_REG_COMM_MB_OBS_OFF                                   0x0
#define     EQ_REG_COMM_MB_OBS_ON                                    0x2
#define   EQ_REG_COMM_MB_CTR_MUX__B                                  2
#define   EQ_REG_COMM_MB_CTR_MUX__W                                  2
#define   EQ_REG_COMM_MB_CTR_MUX__M                                  0xC
#define     EQ_REG_COMM_MB_CTR_MUX_EQ_OT                             0x0
#define     EQ_REG_COMM_MB_CTR_MUX_EQ_RC                             0x4
#define     EQ_REG_COMM_MB_CTR_MUX_EQ_IS                             0x8
#define   EQ_REG_COMM_MB_OBS_MUX__B                                  4
#define   EQ_REG_COMM_MB_OBS_MUX__W                                  2
#define   EQ_REG_COMM_MB_OBS_MUX__M                                  0x30
#define     EQ_REG_COMM_MB_OBS_MUX_EQ_OT                             0x0
#define     EQ_REG_COMM_MB_OBS_MUX_EQ_RC                             0x10
#define     EQ_REG_COMM_MB_OBS_MUX_EQ_IS                             0x20
#define     EQ_REG_COMM_MB_OBS_MUX_EQ_SN                             0x30

#define EQ_REG_COMM_SERVICE0__A                                      0x1C10003
#define EQ_REG_COMM_SERVICE0__W                                      10
#define EQ_REG_COMM_SERVICE0__M                                      0x3FF

#define EQ_REG_COMM_SERVICE1__A                                      0x1C10004
#define EQ_REG_COMM_SERVICE1__W                                      11
#define EQ_REG_COMM_SERVICE1__M                                      0x7FF

#define EQ_REG_COMM_INT_STA__A                                       0x1C10007
#define EQ_REG_COMM_INT_STA__W                                       2
#define EQ_REG_COMM_INT_STA__M                                       0x3
#define   EQ_REG_COMM_INT_STA_TPS_RDY__B                             0
#define   EQ_REG_COMM_INT_STA_TPS_RDY__W                             1
#define   EQ_REG_COMM_INT_STA_TPS_RDY__M                             0x1
#define   EQ_REG_COMM_INT_STA_ERR_RDY__B                             1
#define   EQ_REG_COMM_INT_STA_ERR_RDY__W                             1
#define   EQ_REG_COMM_INT_STA_ERR_RDY__M                             0x2

#define EQ_REG_COMM_INT_MSK__A                                       0x1C10008
#define EQ_REG_COMM_INT_MSK__W                                       2
#define EQ_REG_COMM_INT_MSK__M                                       0x3
#define   EQ_REG_COMM_INT_MSK_TPS_RDY__B                             0
#define   EQ_REG_COMM_INT_MSK_TPS_RDY__W                             1
#define   EQ_REG_COMM_INT_MSK_TPS_RDY__M                             0x1
#define   EQ_REG_COMM_INT_MSK_MER_RDY__B                             1
#define   EQ_REG_COMM_INT_MSK_MER_RDY__W                             1
#define   EQ_REG_COMM_INT_MSK_MER_RDY__M                             0x2

#define EQ_REG_IS_MODE__A                                            0x1C10014
#define EQ_REG_IS_MODE__W                                            4
#define EQ_REG_IS_MODE__M                                            0xF
#define   EQ_REG_IS_MODE_INIT                                        0x0

#define   EQ_REG_IS_MODE_LIM_EXP_SEL__B                              0
#define   EQ_REG_IS_MODE_LIM_EXP_SEL__W                              1
#define   EQ_REG_IS_MODE_LIM_EXP_SEL__M                              0x1
#define     EQ_REG_IS_MODE_LIM_EXP_SEL_EXP_SEL_MAX                   0x0
#define     EQ_REG_IS_MODE_LIM_EXP_SEL_EXP_SEL_ZER                   0x1

#define   EQ_REG_IS_MODE_LIM_CLP_SEL__B                              1
#define   EQ_REG_IS_MODE_LIM_CLP_SEL__W                              1
#define   EQ_REG_IS_MODE_LIM_CLP_SEL__M                              0x2
#define     EQ_REG_IS_MODE_LIM_CLP_SEL_CLP_SEL_ONE                   0x0
#define     EQ_REG_IS_MODE_LIM_CLP_SEL_CLP_SEL_TWO                   0x2

#define EQ_REG_IS_GAIN_MAN__A                                        0x1C10015
#define EQ_REG_IS_GAIN_MAN__W                                        10
#define EQ_REG_IS_GAIN_MAN__M                                        0x3FF
#define   EQ_REG_IS_GAIN_MAN_INIT                                    0x0

#define EQ_REG_IS_GAIN_EXP__A                                        0x1C10016
#define EQ_REG_IS_GAIN_EXP__W                                        5
#define EQ_REG_IS_GAIN_EXP__M                                        0x1F
#define   EQ_REG_IS_GAIN_EXP_INIT                                    0x0

#define EQ_REG_IS_CLIP_EXP__A                                        0x1C10017
#define EQ_REG_IS_CLIP_EXP__W                                        5
#define EQ_REG_IS_CLIP_EXP__M                                        0x1F
#define   EQ_REG_IS_CLIP_EXP_INIT                                    0x0

#define EQ_REG_DV_MODE__A                                            0x1C1001E
#define EQ_REG_DV_MODE__W                                            4
#define EQ_REG_DV_MODE__M                                            0xF
#define   EQ_REG_DV_MODE_INIT                                        0x0

#define   EQ_REG_DV_MODE_CLP_CNT_EVR__B                              0
#define   EQ_REG_DV_MODE_CLP_CNT_EVR__W                              1
#define   EQ_REG_DV_MODE_CLP_CNT_EVR__M                              0x1
#define     EQ_REG_DV_MODE_CLP_CNT_EVR_CLP_REA_DIS                   0x0
#define     EQ_REG_DV_MODE_CLP_CNT_EVR_CLP_REA_ENA                   0x1

#define   EQ_REG_DV_MODE_CLP_CNT_EVI__B                              1
#define   EQ_REG_DV_MODE_CLP_CNT_EVI__W                              1
#define   EQ_REG_DV_MODE_CLP_CNT_EVI__M                              0x2
#define     EQ_REG_DV_MODE_CLP_CNT_EVI_CLP_IMA_DIS                   0x0
#define     EQ_REG_DV_MODE_CLP_CNT_EVI_CLP_IMA_ENA                   0x2

#define   EQ_REG_DV_MODE_CLP_REA_ENA__B                              2
#define   EQ_REG_DV_MODE_CLP_REA_ENA__W                              1
#define   EQ_REG_DV_MODE_CLP_REA_ENA__M                              0x4
#define     EQ_REG_DV_MODE_CLP_REA_ENA_CLP_REA_DIS                   0x0
#define     EQ_REG_DV_MODE_CLP_REA_ENA_CLP_REA_ENA                   0x4

#define   EQ_REG_DV_MODE_CLP_IMA_ENA__B                              3
#define   EQ_REG_DV_MODE_CLP_IMA_ENA__W                              1
#define   EQ_REG_DV_MODE_CLP_IMA_ENA__M                              0x8
#define     EQ_REG_DV_MODE_CLP_IMA_ENA_CLP_IMA_DIS                   0x0
#define     EQ_REG_DV_MODE_CLP_IMA_ENA_CLP_IMA_ENA                   0x8

#define EQ_REG_DV_POS_CLIP_DAT__A                                    0x1C1001F
#define EQ_REG_DV_POS_CLIP_DAT__W                                    16
#define EQ_REG_DV_POS_CLIP_DAT__M                                    0xFFFF

#define EQ_REG_SN_MODE__A                                            0x1C10028
#define EQ_REG_SN_MODE__W                                            8
#define EQ_REG_SN_MODE__M                                            0xFF
#define   EQ_REG_SN_MODE_INIT                                        0x0

#define   EQ_REG_SN_MODE_MODE_0__B                                   0
#define   EQ_REG_SN_MODE_MODE_0__W                                   1
#define   EQ_REG_SN_MODE_MODE_0__M                                   0x1
#define     EQ_REG_SN_MODE_MODE_0_DISABLE                            0x0
#define     EQ_REG_SN_MODE_MODE_0_ENABLE                             0x1

#define   EQ_REG_SN_MODE_MODE_1__B                                   1
#define   EQ_REG_SN_MODE_MODE_1__W                                   1
#define   EQ_REG_SN_MODE_MODE_1__M                                   0x2
#define     EQ_REG_SN_MODE_MODE_1_DISABLE                            0x0
#define     EQ_REG_SN_MODE_MODE_1_ENABLE                             0x2

#define   EQ_REG_SN_MODE_MODE_2__B                                   2
#define   EQ_REG_SN_MODE_MODE_2__W                                   1
#define   EQ_REG_SN_MODE_MODE_2__M                                   0x4
#define     EQ_REG_SN_MODE_MODE_2_DISABLE                            0x0
#define     EQ_REG_SN_MODE_MODE_2_ENABLE                             0x4

#define   EQ_REG_SN_MODE_MODE_3__B                                   3
#define   EQ_REG_SN_MODE_MODE_3__W                                   1
#define   EQ_REG_SN_MODE_MODE_3__M                                   0x8
#define     EQ_REG_SN_MODE_MODE_3_DISABLE                            0x0
#define     EQ_REG_SN_MODE_MODE_3_ENABLE                             0x8

#define   EQ_REG_SN_MODE_MODE_4__B                                   4
#define   EQ_REG_SN_MODE_MODE_4__W                                   1
#define   EQ_REG_SN_MODE_MODE_4__M                                   0x10
#define     EQ_REG_SN_MODE_MODE_4_DISABLE                            0x0
#define     EQ_REG_SN_MODE_MODE_4_ENABLE                             0x10

#define   EQ_REG_SN_MODE_MODE_5__B                                   5
#define   EQ_REG_SN_MODE_MODE_5__W                                   1
#define   EQ_REG_SN_MODE_MODE_5__M                                   0x20
#define     EQ_REG_SN_MODE_MODE_5_DISABLE                            0x0
#define     EQ_REG_SN_MODE_MODE_5_ENABLE                             0x20

#define   EQ_REG_SN_MODE_MODE_6__B                                   6
#define   EQ_REG_SN_MODE_MODE_6__W                                   1
#define   EQ_REG_SN_MODE_MODE_6__M                                   0x40
#define     EQ_REG_SN_MODE_MODE_6_DYNAMIC                            0x0
#define     EQ_REG_SN_MODE_MODE_6_STATIC                             0x40

#define   EQ_REG_SN_MODE_MODE_7__B                                   7
#define   EQ_REG_SN_MODE_MODE_7__W                                   1
#define   EQ_REG_SN_MODE_MODE_7__M                                   0x80
#define     EQ_REG_SN_MODE_MODE_7_DYNAMIC                            0x0
#define     EQ_REG_SN_MODE_MODE_7_STATIC                             0x80

#define EQ_REG_SN_PFIX__A                                            0x1C10029
#define EQ_REG_SN_PFIX__W                                            8
#define EQ_REG_SN_PFIX__M                                            0xFF
#define   EQ_REG_SN_PFIX_INIT                                        0x0

#define EQ_REG_SN_CEGAIN__A                                          0x1C1002A
#define EQ_REG_SN_CEGAIN__W                                          8
#define EQ_REG_SN_CEGAIN__M                                          0xFF
#define   EQ_REG_SN_CEGAIN_INIT                                      0x0

#define EQ_REG_SN_OFFSET__A                                          0x1C1002B
#define EQ_REG_SN_OFFSET__W                                          6
#define EQ_REG_SN_OFFSET__M                                          0x3F
#define   EQ_REG_SN_OFFSET_INIT                                      0x0

#define EQ_REG_SN_NULLIFY__A                                         0x1C1002C
#define EQ_REG_SN_NULLIFY__W                                         6
#define EQ_REG_SN_NULLIFY__M                                         0x3F
#define   EQ_REG_SN_NULLIFY_INIT                                     0x0

#define EQ_REG_SN_SQUASH__A                                          0x1C1002D
#define EQ_REG_SN_SQUASH__W                                          10
#define EQ_REG_SN_SQUASH__M                                          0x3FF
#define   EQ_REG_SN_SQUASH_INIT                                      0x0

#define   EQ_REG_SN_SQUASH_MAN__B                                    0
#define   EQ_REG_SN_SQUASH_MAN__W                                    6
#define   EQ_REG_SN_SQUASH_MAN__M                                    0x3F

#define   EQ_REG_SN_SQUASH_EXP__B                                    6
#define   EQ_REG_SN_SQUASH_EXP__W                                    4
#define   EQ_REG_SN_SQUASH_EXP__M                                    0x3C0

#define EQ_REG_RC_SEL_CAR__A                                         0x1C10032
#define EQ_REG_RC_SEL_CAR__W                                         6
#define EQ_REG_RC_SEL_CAR__M                                         0x3F
#define   EQ_REG_RC_SEL_CAR_INIT                                     0x0
#define   EQ_REG_RC_SEL_CAR_DIV__B                                   0
#define   EQ_REG_RC_SEL_CAR_DIV__W                                   1
#define   EQ_REG_RC_SEL_CAR_DIV__M                                   0x1
#define     EQ_REG_RC_SEL_CAR_DIV_OFF                                0x0
#define     EQ_REG_RC_SEL_CAR_DIV_ON                                 0x1

#define   EQ_REG_RC_SEL_CAR_PASS__B                                  1
#define   EQ_REG_RC_SEL_CAR_PASS__W                                  2
#define   EQ_REG_RC_SEL_CAR_PASS__M                                  0x6
#define     EQ_REG_RC_SEL_CAR_PASS_A_CC                              0x0
#define     EQ_REG_RC_SEL_CAR_PASS_B_CE                              0x2
#define     EQ_REG_RC_SEL_CAR_PASS_C_DRI                             0x4
#define     EQ_REG_RC_SEL_CAR_PASS_D_CC                              0x6

#define   EQ_REG_RC_SEL_CAR_LOCAL__B                                 3
#define   EQ_REG_RC_SEL_CAR_LOCAL__W                                 2
#define   EQ_REG_RC_SEL_CAR_LOCAL__M                                 0x18
#define     EQ_REG_RC_SEL_CAR_LOCAL_A_CC                             0x0
#define     EQ_REG_RC_SEL_CAR_LOCAL_B_CE                             0x8
#define     EQ_REG_RC_SEL_CAR_LOCAL_C_DRI                            0x10
#define     EQ_REG_RC_SEL_CAR_LOCAL_D_CC                             0x18

#define   EQ_REG_RC_SEL_CAR_MEAS__B                                  5
#define   EQ_REG_RC_SEL_CAR_MEAS__W                                  1
#define   EQ_REG_RC_SEL_CAR_MEAS__M                                  0x20
#define     EQ_REG_RC_SEL_CAR_MEAS_A_CC                              0x0
#define     EQ_REG_RC_SEL_CAR_MEAS_B_CE                              0x20

#define EQ_REG_RC_STS__A                                             0x1C10033
#define EQ_REG_RC_STS__W                                             12
#define EQ_REG_RC_STS__M                                             0xFFF

#define   EQ_REG_RC_STS_DIFF__B                                      0
#define   EQ_REG_RC_STS_DIFF__W                                      9
#define   EQ_REG_RC_STS_DIFF__M                                      0x1FF

#define   EQ_REG_RC_STS_FIRST__B                                     9
#define   EQ_REG_RC_STS_FIRST__W                                     1
#define   EQ_REG_RC_STS_FIRST__M                                     0x200
#define     EQ_REG_RC_STS_FIRST_A_CE                                 0x0
#define     EQ_REG_RC_STS_FIRST_B_DRI                                0x200

#define   EQ_REG_RC_STS_SELEC__B                                     10
#define   EQ_REG_RC_STS_SELEC__W                                     1
#define   EQ_REG_RC_STS_SELEC__M                                     0x400
#define     EQ_REG_RC_STS_SELEC_A_CE                                 0x0
#define     EQ_REG_RC_STS_SELEC_B_DRI                                0x400

#define   EQ_REG_RC_STS_OVERFLOW__B                                  11
#define   EQ_REG_RC_STS_OVERFLOW__W                                  1
#define   EQ_REG_RC_STS_OVERFLOW__M                                  0x800
#define     EQ_REG_RC_STS_OVERFLOW_NO                                0x0
#define     EQ_REG_RC_STS_OVERFLOW_YES                               0x800

#define EQ_REG_OT_CONST__A                                           0x1C10046
#define EQ_REG_OT_CONST__W                                           2
#define EQ_REG_OT_CONST__M                                           0x3
#define   EQ_REG_OT_CONST_INIT                                       0x0

#define EQ_REG_OT_ALPHA__A                                           0x1C10047
#define EQ_REG_OT_ALPHA__W                                           2
#define EQ_REG_OT_ALPHA__M                                           0x3
#define   EQ_REG_OT_ALPHA_INIT                                       0x0

#define EQ_REG_OT_QNT_THRES0__A                                      0x1C10048
#define EQ_REG_OT_QNT_THRES0__W                                      5
#define EQ_REG_OT_QNT_THRES0__M                                      0x1F
#define   EQ_REG_OT_QNT_THRES0_INIT                                  0x0

#define EQ_REG_OT_QNT_THRES1__A                                      0x1C10049
#define EQ_REG_OT_QNT_THRES1__W                                      5
#define EQ_REG_OT_QNT_THRES1__M                                      0x1F
#define   EQ_REG_OT_QNT_THRES1_INIT                                  0x0

#define EQ_REG_OT_CSI_STEP__A                                        0x1C1004A
#define EQ_REG_OT_CSI_STEP__W                                        4
#define EQ_REG_OT_CSI_STEP__M                                        0xF
#define   EQ_REG_OT_CSI_STEP_INIT                                    0x0

#define EQ_REG_OT_CSI_OFFSET__A                                      0x1C1004B
#define EQ_REG_OT_CSI_OFFSET__W                                      7
#define EQ_REG_OT_CSI_OFFSET__M                                      0x7F
#define   EQ_REG_OT_CSI_OFFSET_INIT                                  0x0

#define EQ_REG_TD_TPS_INIT__A                                        0x1C10050
#define EQ_REG_TD_TPS_INIT__W                                        1
#define EQ_REG_TD_TPS_INIT__M                                        0x1
#define   EQ_REG_TD_TPS_INIT_INIT                                    0x0
#define   EQ_REG_TD_TPS_INIT_POS                                     0x0
#define   EQ_REG_TD_TPS_INIT_NEG                                     0x1

#define EQ_REG_TD_TPS_SYNC__A                                        0x1C10051
#define EQ_REG_TD_TPS_SYNC__W                                        16
#define EQ_REG_TD_TPS_SYNC__M                                        0xFFFF
#define   EQ_REG_TD_TPS_SYNC_INIT                                    0x0
#define   EQ_REG_TD_TPS_SYNC_ODD                                     0x35EE
#define   EQ_REG_TD_TPS_SYNC_EVEN                                    0xCA11

#define EQ_REG_TD_TPS_LEN__A                                         0x1C10052
#define EQ_REG_TD_TPS_LEN__W                                         6
#define EQ_REG_TD_TPS_LEN__M                                         0x3F
#define   EQ_REG_TD_TPS_LEN_INIT                                     0x0
#define   EQ_REG_TD_TPS_LEN_DEF                                      0x17
#define   EQ_REG_TD_TPS_LEN_ID_SUP                                   0x1F

#define EQ_REG_TD_TPS_FRM_NMB__A                                     0x1C10053
#define EQ_REG_TD_TPS_FRM_NMB__W                                     2
#define EQ_REG_TD_TPS_FRM_NMB__M                                     0x3
#define   EQ_REG_TD_TPS_FRM_NMB_INIT                                 0x0
#define   EQ_REG_TD_TPS_FRM_NMB_1                                    0x0
#define   EQ_REG_TD_TPS_FRM_NMB_2                                    0x1
#define   EQ_REG_TD_TPS_FRM_NMB_3                                    0x2
#define   EQ_REG_TD_TPS_FRM_NMB_4                                    0x3

#define EQ_REG_TD_TPS_CONST__A                                       0x1C10054
#define EQ_REG_TD_TPS_CONST__W                                       2
#define EQ_REG_TD_TPS_CONST__M                                       0x3
#define   EQ_REG_TD_TPS_CONST_INIT                                   0x0
#define   EQ_REG_TD_TPS_CONST_QPSK                                   0x0
#define   EQ_REG_TD_TPS_CONST_16QAM                                  0x1
#define   EQ_REG_TD_TPS_CONST_64QAM                                  0x2

#define EQ_REG_TD_TPS_HINFO__A                                       0x1C10055
#define EQ_REG_TD_TPS_HINFO__W                                       3
#define EQ_REG_TD_TPS_HINFO__M                                       0x7
#define   EQ_REG_TD_TPS_HINFO_INIT                                   0x0
#define   EQ_REG_TD_TPS_HINFO_NH                                     0x0
#define   EQ_REG_TD_TPS_HINFO_H1                                     0x1
#define   EQ_REG_TD_TPS_HINFO_H2                                     0x2
#define   EQ_REG_TD_TPS_HINFO_H4                                     0x3

#define EQ_REG_TD_TPS_CODE_HP__A                                     0x1C10056
#define EQ_REG_TD_TPS_CODE_HP__W                                     3
#define EQ_REG_TD_TPS_CODE_HP__M                                     0x7
#define   EQ_REG_TD_TPS_CODE_HP_INIT                                 0x0
#define   EQ_REG_TD_TPS_CODE_HP_1_2                                  0x0
#define   EQ_REG_TD_TPS_CODE_HP_2_3                                  0x1
#define   EQ_REG_TD_TPS_CODE_HP_3_4                                  0x2
#define   EQ_REG_TD_TPS_CODE_HP_5_6                                  0x3
#define   EQ_REG_TD_TPS_CODE_HP_7_8                                  0x4

#define EQ_REG_TD_TPS_CODE_LP__A                                     0x1C10057
#define EQ_REG_TD_TPS_CODE_LP__W                                     3
#define EQ_REG_TD_TPS_CODE_LP__M                                     0x7
#define   EQ_REG_TD_TPS_CODE_LP_INIT                                 0x0
#define   EQ_REG_TD_TPS_CODE_LP_1_2                                  0x0
#define   EQ_REG_TD_TPS_CODE_LP_2_3                                  0x1
#define   EQ_REG_TD_TPS_CODE_LP_3_4                                  0x2
#define   EQ_REG_TD_TPS_CODE_LP_5_6                                  0x3
#define   EQ_REG_TD_TPS_CODE_LP_7_8                                  0x4

#define EQ_REG_TD_TPS_GUARD__A                                       0x1C10058
#define EQ_REG_TD_TPS_GUARD__W                                       2
#define EQ_REG_TD_TPS_GUARD__M                                       0x3
#define   EQ_REG_TD_TPS_GUARD_INIT                                   0x0
#define   EQ_REG_TD_TPS_GUARD_32                                     0x0
#define   EQ_REG_TD_TPS_GUARD_16                                     0x1
#define   EQ_REG_TD_TPS_GUARD_08                                     0x2
#define   EQ_REG_TD_TPS_GUARD_04                                     0x3

#define EQ_REG_TD_TPS_TR_MODE__A                                     0x1C10059
#define EQ_REG_TD_TPS_TR_MODE__W                                     2
#define EQ_REG_TD_TPS_TR_MODE__M                                     0x3
#define   EQ_REG_TD_TPS_TR_MODE_INIT                                 0x0
#define   EQ_REG_TD_TPS_TR_MODE_2K                                   0x0
#define   EQ_REG_TD_TPS_TR_MODE_8K                                   0x1

#define EQ_REG_TD_TPS_CELL_ID_HI__A                                  0x1C1005A
#define EQ_REG_TD_TPS_CELL_ID_HI__W                                  8
#define EQ_REG_TD_TPS_CELL_ID_HI__M                                  0xFF
#define   EQ_REG_TD_TPS_CELL_ID_HI_INIT                              0x0

#define EQ_REG_TD_TPS_CELL_ID_LO__A                                  0x1C1005B
#define EQ_REG_TD_TPS_CELL_ID_LO__W                                  8
#define EQ_REG_TD_TPS_CELL_ID_LO__M                                  0xFF
#define   EQ_REG_TD_TPS_CELL_ID_LO_INIT                              0x0

#define EQ_REG_TD_TPS_RSV__A                                         0x1C1005C
#define EQ_REG_TD_TPS_RSV__W                                         6
#define EQ_REG_TD_TPS_RSV__M                                         0x3F
#define   EQ_REG_TD_TPS_RSV_INIT                                     0x0

#define EQ_REG_TD_TPS_BCH__A                                         0x1C1005D
#define EQ_REG_TD_TPS_BCH__W                                         14
#define EQ_REG_TD_TPS_BCH__M                                         0x3FFF
#define   EQ_REG_TD_TPS_BCH_INIT                                     0x0

#define EQ_REG_TD_SQR_ERR_I__A                                       0x1C1005E
#define EQ_REG_TD_SQR_ERR_I__W                                       16
#define EQ_REG_TD_SQR_ERR_I__M                                       0xFFFF
#define   EQ_REG_TD_SQR_ERR_I_INIT                                   0x0

#define EQ_REG_TD_SQR_ERR_Q__A                                       0x1C1005F
#define EQ_REG_TD_SQR_ERR_Q__W                                       16
#define EQ_REG_TD_SQR_ERR_Q__M                                       0xFFFF
#define   EQ_REG_TD_SQR_ERR_Q_INIT                                   0x0

#define EQ_REG_TD_SQR_ERR_EXP__A                                     0x1C10060
#define EQ_REG_TD_SQR_ERR_EXP__W                                     4
#define EQ_REG_TD_SQR_ERR_EXP__M                                     0xF
#define   EQ_REG_TD_SQR_ERR_EXP_INIT                                 0x0

#define EQ_REG_TD_REQ_SMB_CNT__A                                     0x1C10061
#define EQ_REG_TD_REQ_SMB_CNT__W                                     16
#define EQ_REG_TD_REQ_SMB_CNT__M                                     0xFFFF
#define   EQ_REG_TD_REQ_SMB_CNT_INIT                                 0x0

#define EQ_REG_TD_TPS_PWR_OFS__A                                     0x1C10062
#define EQ_REG_TD_TPS_PWR_OFS__W                                     16
#define EQ_REG_TD_TPS_PWR_OFS__M                                     0xFFFF
#define   EQ_REG_TD_TPS_PWR_OFS_INIT                                 0x0

#define EC_COMM_EXEC__A                                              0x2000000
#define EC_COMM_EXEC__W                                              3
#define EC_COMM_EXEC__M                                              0x7
#define   EC_COMM_EXEC_CTL__B                                        0
#define   EC_COMM_EXEC_CTL__W                                        3
#define   EC_COMM_EXEC_CTL__M                                        0x7
#define     EC_COMM_EXEC_CTL_STOP                                    0x0
#define     EC_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     EC_COMM_EXEC_CTL_HOLD                                    0x2
#define     EC_COMM_EXEC_CTL_STEP                                    0x3
#define     EC_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     EC_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define EC_COMM_STATE__A                                             0x2000001
#define EC_COMM_STATE__W                                             16
#define EC_COMM_STATE__M                                             0xFFFF
#define EC_COMM_MB__A                                                0x2000002
#define EC_COMM_MB__W                                                16
#define EC_COMM_MB__M                                                0xFFFF
#define EC_COMM_SERVICE0__A                                          0x2000003
#define EC_COMM_SERVICE0__W                                          16
#define EC_COMM_SERVICE0__M                                          0xFFFF
#define EC_COMM_SERVICE1__A                                          0x2000004
#define EC_COMM_SERVICE1__W                                          16
#define EC_COMM_SERVICE1__M                                          0xFFFF
#define EC_COMM_INT_STA__A                                           0x2000007
#define EC_COMM_INT_STA__W                                           16
#define EC_COMM_INT_STA__M                                           0xFFFF
#define EC_COMM_INT_MSK__A                                           0x2000008
#define EC_COMM_INT_MSK__W                                           16
#define EC_COMM_INT_MSK__M                                           0xFFFF

#define EC_SB_SID                                                    0x16

#define EC_SB_REG_COMM_EXEC__A                                       0x2010000
#define EC_SB_REG_COMM_EXEC__W                                       3
#define EC_SB_REG_COMM_EXEC__M                                       0x7
#define   EC_SB_REG_COMM_EXEC_CTL__B                                 0
#define   EC_SB_REG_COMM_EXEC_CTL__W                                 3
#define   EC_SB_REG_COMM_EXEC_CTL__M                                 0x7
#define     EC_SB_REG_COMM_EXEC_CTL_STOP                             0x0
#define     EC_SB_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     EC_SB_REG_COMM_EXEC_CTL_HOLD                             0x2

#define EC_SB_REG_COMM_STATE__A                                      0x2010001
#define EC_SB_REG_COMM_STATE__W                                      4
#define EC_SB_REG_COMM_STATE__M                                      0xF
#define EC_SB_REG_COMM_MB__A                                         0x2010002
#define EC_SB_REG_COMM_MB__W                                         2
#define EC_SB_REG_COMM_MB__M                                         0x3
#define   EC_SB_REG_COMM_MB_CTR__B                                   0
#define   EC_SB_REG_COMM_MB_CTR__W                                   1
#define   EC_SB_REG_COMM_MB_CTR__M                                   0x1
#define     EC_SB_REG_COMM_MB_CTR_OFF                                0x0
#define     EC_SB_REG_COMM_MB_CTR_ON                                 0x1
#define   EC_SB_REG_COMM_MB_OBS__B                                   1
#define   EC_SB_REG_COMM_MB_OBS__W                                   1
#define   EC_SB_REG_COMM_MB_OBS__M                                   0x2
#define     EC_SB_REG_COMM_MB_OBS_OFF                                0x0
#define     EC_SB_REG_COMM_MB_OBS_ON                                 0x2

#define EC_SB_REG_TR_MODE__A                                         0x2010010
#define EC_SB_REG_TR_MODE__W                                         1
#define EC_SB_REG_TR_MODE__M                                         0x1
#define   EC_SB_REG_TR_MODE_INIT                                     0x0
#define   EC_SB_REG_TR_MODE_8K                                       0x0
#define   EC_SB_REG_TR_MODE_2K                                       0x1

#define EC_SB_REG_CONST__A                                           0x2010011
#define EC_SB_REG_CONST__W                                           2
#define EC_SB_REG_CONST__M                                           0x3
#define   EC_SB_REG_CONST_INIT                                       0x2
#define   EC_SB_REG_CONST_QPSK                                       0x0
#define   EC_SB_REG_CONST_16QAM                                      0x1
#define   EC_SB_REG_CONST_64QAM                                      0x2

#define EC_SB_REG_ALPHA__A                                           0x2010012
#define EC_SB_REG_ALPHA__W                                           3
#define EC_SB_REG_ALPHA__M                                           0x7

#define   EC_SB_REG_ALPHA_INIT                                       0x0

#define   EC_SB_REG_ALPHA_NH                                         0x0

#define   EC_SB_REG_ALPHA_H1                                         0x1

#define   EC_SB_REG_ALPHA_H2                                         0x2

#define   EC_SB_REG_ALPHA_H4                                         0x3

#define EC_SB_REG_PRIOR__A                                           0x2010013
#define EC_SB_REG_PRIOR__W                                           1
#define EC_SB_REG_PRIOR__M                                           0x1
#define   EC_SB_REG_PRIOR_INIT                                       0x0
#define   EC_SB_REG_PRIOR_HI                                         0x0
#define   EC_SB_REG_PRIOR_LO                                         0x1

#define EC_SB_REG_CSI_HI__A                                          0x2010014
#define EC_SB_REG_CSI_HI__W                                          5
#define EC_SB_REG_CSI_HI__M                                          0x1F
#define   EC_SB_REG_CSI_HI_INIT                                      0x1F
#define   EC_SB_REG_CSI_HI_MAX                                       0x1F
#define   EC_SB_REG_CSI_HI_MIN                                       0x0
#define   EC_SB_REG_CSI_HI_TAG                                       0x0

#define EC_SB_REG_CSI_LO__A                                          0x2010015
#define EC_SB_REG_CSI_LO__W                                          5
#define EC_SB_REG_CSI_LO__M                                          0x1F
#define   EC_SB_REG_CSI_LO_INIT                                      0x1F
#define   EC_SB_REG_CSI_LO_MAX                                       0x1F
#define   EC_SB_REG_CSI_LO_MIN                                       0x0
#define   EC_SB_REG_CSI_LO_TAG                                       0x0

#define EC_SB_REG_SMB_TGL__A                                         0x2010016
#define EC_SB_REG_SMB_TGL__W                                         1
#define EC_SB_REG_SMB_TGL__M                                         0x1
#define   EC_SB_REG_SMB_TGL_OFF                                      0x0
#define   EC_SB_REG_SMB_TGL_ON                                       0x1

#define EC_SB_REG_SNR_HI__A                                          0x2010017
#define EC_SB_REG_SNR_HI__W                                          8
#define EC_SB_REG_SNR_HI__M                                          0xFF
#define   EC_SB_REG_SNR_HI_INIT                                      0xFF
#define   EC_SB_REG_SNR_HI_MAX                                       0xFF
#define   EC_SB_REG_SNR_HI_MIN                                       0x0
#define   EC_SB_REG_SNR_HI_TAG                                       0x0

#define EC_SB_REG_SNR_MID__A                                         0x2010018
#define EC_SB_REG_SNR_MID__W                                         8
#define EC_SB_REG_SNR_MID__M                                         0xFF
#define   EC_SB_REG_SNR_MID_INIT                                     0xFF
#define   EC_SB_REG_SNR_MID_MAX                                      0xFF
#define   EC_SB_REG_SNR_MID_MIN                                      0x0
#define   EC_SB_REG_SNR_MID_TAG                                      0x0

#define EC_SB_REG_SNR_LO__A                                          0x2010019
#define EC_SB_REG_SNR_LO__W                                          8
#define EC_SB_REG_SNR_LO__M                                          0xFF
#define   EC_SB_REG_SNR_LO_INIT                                      0xFF
#define   EC_SB_REG_SNR_LO_MAX                                       0xFF
#define   EC_SB_REG_SNR_LO_MIN                                       0x0
#define   EC_SB_REG_SNR_LO_TAG                                       0x0

#define EC_SB_REG_SCALE_MSB__A                                       0x201001A
#define EC_SB_REG_SCALE_MSB__W                                       6
#define EC_SB_REG_SCALE_MSB__M                                       0x3F
#define   EC_SB_REG_SCALE_MSB_INIT                                   0x30
#define   EC_SB_REG_SCALE_MSB_MAX                                    0x3F

#define EC_SB_REG_SCALE_BIT2__A                                      0x201001B
#define EC_SB_REG_SCALE_BIT2__W                                      6
#define EC_SB_REG_SCALE_BIT2__M                                      0x3F
#define   EC_SB_REG_SCALE_BIT2_INIT                                  0x20
#define   EC_SB_REG_SCALE_BIT2_MAX                                   0x3F

#define EC_SB_REG_SCALE_LSB__A                                       0x201001C
#define EC_SB_REG_SCALE_LSB__W                                       6
#define EC_SB_REG_SCALE_LSB__M                                       0x3F
#define   EC_SB_REG_SCALE_LSB_INIT                                   0x10
#define   EC_SB_REG_SCALE_LSB_MAX                                    0x3F

#define EC_SB_REG_CSI_OFS__A                                         0x201001D
#define EC_SB_REG_CSI_OFS__W                                         4
#define EC_SB_REG_CSI_OFS__M                                         0xF
#define   EC_SB_REG_CSI_OFS_INIT                                     0x1
#define   EC_SB_REG_CSI_OFS_ADD__B                                   0
#define   EC_SB_REG_CSI_OFS_ADD__W                                   3
#define   EC_SB_REG_CSI_OFS_ADD__M                                   0x7
#define   EC_SB_REG_CSI_OFS_DIS__B                                   3
#define   EC_SB_REG_CSI_OFS_DIS__W                                   1
#define   EC_SB_REG_CSI_OFS_DIS__M                                   0x8
#define     EC_SB_REG_CSI_OFS_DIS_ENA                                0x0
#define     EC_SB_REG_CSI_OFS_DIS_DIS                                0x8

#define EC_SB_SD_RAM__A                                              0x2020000

#define EC_SB_BD0_RAM__A                                             0x2030000

#define EC_SB_BD1_RAM__A                                             0x2040000

#define EC_VD_SID                                                    0x17

#define EC_VD_REG_COMM_EXEC__A                                       0x2090000
#define EC_VD_REG_COMM_EXEC__W                                       3
#define EC_VD_REG_COMM_EXEC__M                                       0x7
#define   EC_VD_REG_COMM_EXEC_CTL__B                                 0
#define   EC_VD_REG_COMM_EXEC_CTL__W                                 3
#define   EC_VD_REG_COMM_EXEC_CTL__M                                 0x7
#define     EC_VD_REG_COMM_EXEC_CTL_STOP                             0x0
#define     EC_VD_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     EC_VD_REG_COMM_EXEC_CTL_HOLD                             0x2

#define EC_VD_REG_COMM_STATE__A                                      0x2090001
#define EC_VD_REG_COMM_STATE__W                                      4
#define EC_VD_REG_COMM_STATE__M                                      0xF
#define EC_VD_REG_COMM_MB__A                                         0x2090002
#define EC_VD_REG_COMM_MB__W                                         2
#define EC_VD_REG_COMM_MB__M                                         0x3
#define   EC_VD_REG_COMM_MB_CTR__B                                   0
#define   EC_VD_REG_COMM_MB_CTR__W                                   1
#define   EC_VD_REG_COMM_MB_CTR__M                                   0x1
#define     EC_VD_REG_COMM_MB_CTR_OFF                                0x0
#define     EC_VD_REG_COMM_MB_CTR_ON                                 0x1
#define   EC_VD_REG_COMM_MB_OBS__B                                   1
#define   EC_VD_REG_COMM_MB_OBS__W                                   1
#define   EC_VD_REG_COMM_MB_OBS__M                                   0x2
#define     EC_VD_REG_COMM_MB_OBS_OFF                                0x0
#define     EC_VD_REG_COMM_MB_OBS_ON                                 0x2

#define EC_VD_REG_COMM_SERVICE0__A                                   0x2090003
#define EC_VD_REG_COMM_SERVICE0__W                                   16
#define EC_VD_REG_COMM_SERVICE0__M                                   0xFFFF
#define EC_VD_REG_COMM_SERVICE1__A                                   0x2090004
#define EC_VD_REG_COMM_SERVICE1__W                                   16
#define EC_VD_REG_COMM_SERVICE1__M                                   0xFFFF
#define EC_VD_REG_COMM_INT_STA__A                                    0x2090007
#define EC_VD_REG_COMM_INT_STA__W                                    1
#define EC_VD_REG_COMM_INT_STA__M                                    0x1
#define   EC_VD_REG_COMM_INT_STA_BER_RDY__B                          0
#define   EC_VD_REG_COMM_INT_STA_BER_RDY__W                          1
#define   EC_VD_REG_COMM_INT_STA_BER_RDY__M                          0x1

#define EC_VD_REG_COMM_INT_MSK__A                                    0x2090008
#define EC_VD_REG_COMM_INT_MSK__W                                    1
#define EC_VD_REG_COMM_INT_MSK__M                                    0x1
#define   EC_VD_REG_COMM_INT_MSK_BER_RDY__B                          0
#define   EC_VD_REG_COMM_INT_MSK_BER_RDY__W                          1
#define   EC_VD_REG_COMM_INT_MSK_BER_RDY__M                          0x1

#define EC_VD_REG_FORCE__A                                           0x2090010
#define EC_VD_REG_FORCE__W                                           2
#define EC_VD_REG_FORCE__M                                           0x3
#define   EC_VD_REG_FORCE_INIT                                       0x0
#define   EC_VD_REG_FORCE_FREE                                       0x0
#define   EC_VD_REG_FORCE_PROP                                       0x1
#define   EC_VD_REG_FORCE_FORCED                                     0x2
#define   EC_VD_REG_FORCE_FIXED                                      0x3

#define EC_VD_REG_SET_CODERATE__A                                    0x2090011
#define EC_VD_REG_SET_CODERATE__W                                    3
#define EC_VD_REG_SET_CODERATE__M                                    0x7
#define   EC_VD_REG_SET_CODERATE_INIT                                0x0
#define   EC_VD_REG_SET_CODERATE_C1_2                                0x0
#define   EC_VD_REG_SET_CODERATE_C2_3                                0x1
#define   EC_VD_REG_SET_CODERATE_C3_4                                0x2
#define   EC_VD_REG_SET_CODERATE_C5_6                                0x3
#define   EC_VD_REG_SET_CODERATE_C7_8                                0x4

#define EC_VD_REG_REQ_SMB_CNT__A                                     0x2090012
#define EC_VD_REG_REQ_SMB_CNT__W                                     16
#define EC_VD_REG_REQ_SMB_CNT__M                                     0xFFFF
#define   EC_VD_REG_REQ_SMB_CNT_INIT                                 0x0

#define EC_VD_REG_REQ_BIT_CNT__A                                     0x2090013
#define EC_VD_REG_REQ_BIT_CNT__W                                     16
#define EC_VD_REG_REQ_BIT_CNT__M                                     0xFFFF
#define   EC_VD_REG_REQ_BIT_CNT_INIT                                 0xFFF

#define EC_VD_REG_RLK_ENA__A                                         0x2090014
#define EC_VD_REG_RLK_ENA__W                                         1
#define EC_VD_REG_RLK_ENA__M                                         0x1
#define   EC_VD_REG_RLK_ENA_INIT                                     0x0
#define   EC_VD_REG_RLK_ENA_OFF                                      0x0
#define   EC_VD_REG_RLK_ENA_ON                                       0x1

#define EC_VD_REG_VAL__A                                             0x2090015
#define EC_VD_REG_VAL__W                                             2
#define EC_VD_REG_VAL__M                                             0x3
#define   EC_VD_REG_VAL_INIT                                         0x0
#define   EC_VD_REG_VAL_CODE                                         0x1
#define   EC_VD_REG_VAL_CNT                                          0x2

#define EC_VD_REG_GET_CODERATE__A                                    0x2090016
#define EC_VD_REG_GET_CODERATE__W                                    3
#define EC_VD_REG_GET_CODERATE__M                                    0x7
#define   EC_VD_REG_GET_CODERATE_INIT                                0x0
#define   EC_VD_REG_GET_CODERATE_C1_2                                0x0
#define   EC_VD_REG_GET_CODERATE_C2_3                                0x1
#define   EC_VD_REG_GET_CODERATE_C3_4                                0x2
#define   EC_VD_REG_GET_CODERATE_C5_6                                0x3
#define   EC_VD_REG_GET_CODERATE_C7_8                                0x4

#define EC_VD_REG_ERR_BIT_CNT__A                                     0x2090017
#define EC_VD_REG_ERR_BIT_CNT__W                                     16
#define EC_VD_REG_ERR_BIT_CNT__M                                     0xFFFF
#define   EC_VD_REG_ERR_BIT_CNT_INIT                                 0xFFFF

#define EC_VD_REG_IN_BIT_CNT__A                                      0x2090018
#define EC_VD_REG_IN_BIT_CNT__W                                      16
#define EC_VD_REG_IN_BIT_CNT__M                                      0xFFFF
#define   EC_VD_REG_IN_BIT_CNT_INIT                                  0x0

#define EC_VD_REG_STS__A                                             0x2090019
#define EC_VD_REG_STS__W                                             1
#define EC_VD_REG_STS__M                                             0x1
#define   EC_VD_REG_STS_INIT                                         0x0
#define   EC_VD_REG_STS_NO_LOCK                                      0x0
#define   EC_VD_REG_STS_IN_LOCK                                      0x1

#define EC_VD_REG_RLK_CNT__A                                         0x209001A
#define EC_VD_REG_RLK_CNT__W                                         16
#define EC_VD_REG_RLK_CNT__M                                         0xFFFF
#define   EC_VD_REG_RLK_CNT_INIT                                     0x0

#define EC_VD_TB0_RAM__A                                             0x20A0000

#define EC_VD_TB1_RAM__A                                             0x20B0000

#define EC_VD_TB2_RAM__A                                             0x20C0000

#define EC_VD_TB3_RAM__A                                             0x20D0000

#define EC_VD_RE_RAM__A                                              0x2100000

#define EC_OD_SID                                                    0x18

#define EC_OD_REG_COMM_EXEC__A                                       0x2110000
#define EC_OD_REG_COMM_EXEC__W                                       3
#define EC_OD_REG_COMM_EXEC__M                                       0x7
#define   EC_OD_REG_COMM_EXEC_CTL__B                                 0
#define   EC_OD_REG_COMM_EXEC_CTL__W                                 3
#define   EC_OD_REG_COMM_EXEC_CTL__M                                 0x7
#define     EC_OD_REG_COMM_EXEC_CTL_STOP                             0x0
#define     EC_OD_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     EC_OD_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     EC_OD_REG_COMM_EXEC_CTL_STEP                             0x3

#define EC_OD_REG_COMM_MB__A                                         0x2110002
#define EC_OD_REG_COMM_MB__W                                         3
#define EC_OD_REG_COMM_MB__M                                         0x7
#define   EC_OD_REG_COMM_MB_CTR__B                                   0
#define   EC_OD_REG_COMM_MB_CTR__W                                   1
#define   EC_OD_REG_COMM_MB_CTR__M                                   0x1
#define     EC_OD_REG_COMM_MB_CTR_OFF                                0x0
#define     EC_OD_REG_COMM_MB_CTR_ON                                 0x1
#define   EC_OD_REG_COMM_MB_OBS__B                                   1
#define   EC_OD_REG_COMM_MB_OBS__W                                   1
#define   EC_OD_REG_COMM_MB_OBS__M                                   0x2
#define     EC_OD_REG_COMM_MB_OBS_OFF                                0x0
#define     EC_OD_REG_COMM_MB_OBS_ON                                 0x2

#define EC_OD_REG_COMM_SERVICE0__A                                   0x2110003
#define EC_OD_REG_COMM_SERVICE0__W                                   10
#define EC_OD_REG_COMM_SERVICE0__M                                   0x3FF
#define EC_OD_REG_COMM_SERVICE1__A                                   0x2110004
#define EC_OD_REG_COMM_SERVICE1__W                                   11
#define EC_OD_REG_COMM_SERVICE1__M                                   0x7FF

#define EC_OD_REG_COMM_ACTIVATE__A                                   0x2110005
#define EC_OD_REG_COMM_ACTIVATE__W                                   2
#define EC_OD_REG_COMM_ACTIVATE__M                                   0x3

#define EC_OD_REG_COMM_COUNT__A                                      0x2110006
#define EC_OD_REG_COMM_COUNT__W                                      16
#define EC_OD_REG_COMM_COUNT__M                                      0xFFFF

#define EC_OD_REG_COMM_INT_STA__A                                    0x2110007
#define EC_OD_REG_COMM_INT_STA__W                                    2
#define EC_OD_REG_COMM_INT_STA__M                                    0x3
#define   EC_OD_REG_COMM_INT_STA_IN_SYNC__B                          0
#define   EC_OD_REG_COMM_INT_STA_IN_SYNC__W                          1
#define   EC_OD_REG_COMM_INT_STA_IN_SYNC__M                          0x1
#define   EC_OD_REG_COMM_INT_STA_LOST_SYNC__B                        1
#define   EC_OD_REG_COMM_INT_STA_LOST_SYNC__W                        1
#define   EC_OD_REG_COMM_INT_STA_LOST_SYNC__M                        0x2

#define EC_OD_REG_COMM_INT_MSK__A                                    0x2110008
#define EC_OD_REG_COMM_INT_MSK__W                                    2
#define EC_OD_REG_COMM_INT_MSK__M                                    0x3
#define   EC_OD_REG_COMM_INT_MSK_IN_SYNC__B                          0
#define   EC_OD_REG_COMM_INT_MSK_IN_SYNC__W                          1
#define   EC_OD_REG_COMM_INT_MSK_IN_SYNC__M                          0x1
#define   EC_OD_REG_COMM_INT_MSK_LOST_SYNC__B                        1
#define   EC_OD_REG_COMM_INT_MSK_LOST_SYNC__W                        1
#define   EC_OD_REG_COMM_INT_MSK_LOST_SYNC__M                        0x2

#define EC_OD_REG_SYNC__A                                            0x2110010
#define EC_OD_REG_SYNC__W                                            12
#define EC_OD_REG_SYNC__M                                            0xFFF
#define   EC_OD_REG_SYNC_NR_SYNC__B                                  0
#define   EC_OD_REG_SYNC_NR_SYNC__W                                  5
#define   EC_OD_REG_SYNC_NR_SYNC__M                                  0x1F
#define   EC_OD_REG_SYNC_IN_SYNC__B                                  5
#define   EC_OD_REG_SYNC_IN_SYNC__W                                  4
#define   EC_OD_REG_SYNC_IN_SYNC__M                                  0x1E0
#define   EC_OD_REG_SYNC_OUT_SYNC__B                                 9
#define   EC_OD_REG_SYNC_OUT_SYNC__W                                 3
#define   EC_OD_REG_SYNC_OUT_SYNC__M                                 0xE00

#define EC_OD_REG_NOSYNC__A                                          0x2110011
#define EC_OD_REG_NOSYNC__W                                          8
#define EC_OD_REG_NOSYNC__M                                          0xFF

#define EC_OD_DEINT_RAM__A                                           0x2120000

#define EC_RS_SID                                                    0x19

#define EC_RS_REG_COMM_EXEC__A                                       0x2130000
#define EC_RS_REG_COMM_EXEC__W                                       3
#define EC_RS_REG_COMM_EXEC__M                                       0x7
#define   EC_RS_REG_COMM_EXEC_CTL__B                                 0
#define   EC_RS_REG_COMM_EXEC_CTL__W                                 3
#define   EC_RS_REG_COMM_EXEC_CTL__M                                 0x7
#define     EC_RS_REG_COMM_EXEC_CTL_STOP                             0x0
#define     EC_RS_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     EC_RS_REG_COMM_EXEC_CTL_HOLD                             0x2

#define EC_RS_REG_COMM_STATE__A                                      0x2130001
#define EC_RS_REG_COMM_STATE__W                                      4
#define EC_RS_REG_COMM_STATE__M                                      0xF
#define EC_RS_REG_COMM_MB__A                                         0x2130002
#define EC_RS_REG_COMM_MB__W                                         2
#define EC_RS_REG_COMM_MB__M                                         0x3
#define   EC_RS_REG_COMM_MB_CTR__B                                   0
#define   EC_RS_REG_COMM_MB_CTR__W                                   1
#define   EC_RS_REG_COMM_MB_CTR__M                                   0x1
#define     EC_RS_REG_COMM_MB_CTR_OFF                                0x0
#define     EC_RS_REG_COMM_MB_CTR_ON                                 0x1
#define   EC_RS_REG_COMM_MB_OBS__B                                   1
#define   EC_RS_REG_COMM_MB_OBS__W                                   1
#define   EC_RS_REG_COMM_MB_OBS__M                                   0x2
#define     EC_RS_REG_COMM_MB_OBS_OFF                                0x0
#define     EC_RS_REG_COMM_MB_OBS_ON                                 0x2

#define EC_RS_REG_COMM_SERVICE0__A                                   0x2130003
#define EC_RS_REG_COMM_SERVICE0__W                                   16
#define EC_RS_REG_COMM_SERVICE0__M                                   0xFFFF
#define EC_RS_REG_COMM_SERVICE1__A                                   0x2130004
#define EC_RS_REG_COMM_SERVICE1__W                                   16
#define EC_RS_REG_COMM_SERVICE1__M                                   0xFFFF
#define EC_RS_REG_COMM_INT_STA__A                                    0x2130007
#define EC_RS_REG_COMM_INT_STA__W                                    1
#define EC_RS_REG_COMM_INT_STA__M                                    0x1
#define   EC_RS_REG_COMM_INT_STA_BER_RDY__B                          0
#define   EC_RS_REG_COMM_INT_STA_BER_RDY__W                          1
#define   EC_RS_REG_COMM_INT_STA_BER_RDY__M                          0x1

#define EC_RS_REG_COMM_INT_MSK__A                                    0x2130008
#define EC_RS_REG_COMM_INT_MSK__W                                    1
#define EC_RS_REG_COMM_INT_MSK__M                                    0x1
#define   EC_RS_REG_COMM_INT_MSK_BER_RDY__B                          0
#define   EC_RS_REG_COMM_INT_MSK_BER_RDY__W                          1
#define   EC_RS_REG_COMM_INT_MSK_BER_RDY__M                          0x1

#define EC_RS_REG_REQ_PCK_CNT__A                                     0x2130010
#define EC_RS_REG_REQ_PCK_CNT__W                                     16
#define EC_RS_REG_REQ_PCK_CNT__M                                     0xFFFF
#define   EC_RS_REG_REQ_PCK_CNT_INIT                                 0xFF

#define EC_RS_REG_VAL__A                                             0x2130011
#define EC_RS_REG_VAL__W                                             1
#define EC_RS_REG_VAL__M                                             0x1
#define   EC_RS_REG_VAL_INIT                                         0x0
#define   EC_RS_REG_VAL_PCK                                          0x1

#define EC_RS_REG_ERR_PCK_CNT__A                                     0x2130012
#define EC_RS_REG_ERR_PCK_CNT__W                                     16
#define EC_RS_REG_ERR_PCK_CNT__M                                     0xFFFF
#define   EC_RS_REG_ERR_PCK_CNT_INIT                                 0xFFFF

#define EC_RS_REG_ERR_SMB_CNT__A                                     0x2130013
#define EC_RS_REG_ERR_SMB_CNT__W                                     16
#define EC_RS_REG_ERR_SMB_CNT__M                                     0xFFFF
#define   EC_RS_REG_ERR_SMB_CNT_INIT                                 0xFFFF

#define EC_RS_REG_ERR_BIT_CNT__A                                     0x2130014
#define EC_RS_REG_ERR_BIT_CNT__W                                     16
#define EC_RS_REG_ERR_BIT_CNT__M                                     0xFFFF
#define   EC_RS_REG_ERR_BIT_CNT_INIT                                 0xFFFF

#define EC_RS_REG_IN_PCK_CNT__A                                      0x2130015
#define EC_RS_REG_IN_PCK_CNT__W                                      16
#define EC_RS_REG_IN_PCK_CNT__M                                      0xFFFF
#define   EC_RS_REG_IN_PCK_CNT_INIT                                  0x0

#define EC_RS_EC_RAM__A                                              0x2140000

#define EC_OC_SID                                                    0x1A

#define EC_OC_REG_COMM_EXEC__A                                       0x2150000
#define EC_OC_REG_COMM_EXEC__W                                       3
#define EC_OC_REG_COMM_EXEC__M                                       0x7
#define   EC_OC_REG_COMM_EXEC_CTL__B                                 0
#define   EC_OC_REG_COMM_EXEC_CTL__W                                 3
#define   EC_OC_REG_COMM_EXEC_CTL__M                                 0x7
#define     EC_OC_REG_COMM_EXEC_CTL_STOP                             0x0
#define     EC_OC_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     EC_OC_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     EC_OC_REG_COMM_EXEC_CTL_STEP                             0x3

#define EC_OC_REG_COMM_STATE__A                                      0x2150001
#define EC_OC_REG_COMM_STATE__W                                      4
#define EC_OC_REG_COMM_STATE__M                                      0xF

#define EC_OC_REG_COMM_MB__A                                         0x2150002
#define EC_OC_REG_COMM_MB__W                                         2
#define EC_OC_REG_COMM_MB__M                                         0x3
#define   EC_OC_REG_COMM_MB_CTR__B                                   0
#define   EC_OC_REG_COMM_MB_CTR__W                                   1
#define   EC_OC_REG_COMM_MB_CTR__M                                   0x1
#define     EC_OC_REG_COMM_MB_CTR_OFF                                0x0
#define     EC_OC_REG_COMM_MB_CTR_ON                                 0x1
#define   EC_OC_REG_COMM_MB_OBS__B                                   1
#define   EC_OC_REG_COMM_MB_OBS__W                                   1
#define   EC_OC_REG_COMM_MB_OBS__M                                   0x2
#define     EC_OC_REG_COMM_MB_OBS_OFF                                0x0
#define     EC_OC_REG_COMM_MB_OBS_ON                                 0x2

#define EC_OC_REG_COMM_SERVICE0__A                                   0x2150003
#define EC_OC_REG_COMM_SERVICE0__W                                   10
#define EC_OC_REG_COMM_SERVICE0__M                                   0x3FF

#define EC_OC_REG_COMM_SERVICE1__A                                   0x2150004
#define EC_OC_REG_COMM_SERVICE1__W                                   11
#define EC_OC_REG_COMM_SERVICE1__M                                   0x7FF

#define EC_OC_REG_COMM_INT_STA__A                                    0x2150007
#define EC_OC_REG_COMM_INT_STA__W                                    6
#define EC_OC_REG_COMM_INT_STA__M                                    0x3F
#define   EC_OC_REG_COMM_INT_STA_MEM_FUL_STS__B                      0
#define   EC_OC_REG_COMM_INT_STA_MEM_FUL_STS__W                      1
#define   EC_OC_REG_COMM_INT_STA_MEM_FUL_STS__M                      0x1
#define   EC_OC_REG_COMM_INT_STA_MEM_EMP_STS__B                      1
#define   EC_OC_REG_COMM_INT_STA_MEM_EMP_STS__W                      1
#define   EC_OC_REG_COMM_INT_STA_MEM_EMP_STS__M                      0x2
#define   EC_OC_REG_COMM_INT_STA_SNC_ISS_STS__B                      2
#define   EC_OC_REG_COMM_INT_STA_SNC_ISS_STS__W                      1
#define   EC_OC_REG_COMM_INT_STA_SNC_ISS_STS__M                      0x4
#define   EC_OC_REG_COMM_INT_STA_SNC_OSS_STS__B                      3
#define   EC_OC_REG_COMM_INT_STA_SNC_OSS_STS__W                      1
#define   EC_OC_REG_COMM_INT_STA_SNC_OSS_STS__M                      0x8
#define   EC_OC_REG_COMM_INT_STA_SNC_NSS_STS__B                      4
#define   EC_OC_REG_COMM_INT_STA_SNC_NSS_STS__W                      1
#define   EC_OC_REG_COMM_INT_STA_SNC_NSS_STS__M                      0x10
#define   EC_OC_REG_COMM_INT_STA_PCK_ERR_UPD__B                      5
#define   EC_OC_REG_COMM_INT_STA_PCK_ERR_UPD__W                      1
#define   EC_OC_REG_COMM_INT_STA_PCK_ERR_UPD__M                      0x20

#define EC_OC_REG_COMM_INT_MSK__A                                    0x2150008
#define EC_OC_REG_COMM_INT_MSK__W                                    6
#define EC_OC_REG_COMM_INT_MSK__M                                    0x3F
#define   EC_OC_REG_COMM_INT_MSK_MEM_FUL_STS__B                      0
#define   EC_OC_REG_COMM_INT_MSK_MEM_FUL_STS__W                      1
#define   EC_OC_REG_COMM_INT_MSK_MEM_FUL_STS__M                      0x1
#define   EC_OC_REG_COMM_INT_MSK_MEM_EMP_STS__B                      1
#define   EC_OC_REG_COMM_INT_MSK_MEM_EMP_STS__W                      1
#define   EC_OC_REG_COMM_INT_MSK_MEM_EMP_STS__M                      0x2
#define   EC_OC_REG_COMM_INT_MSK_SNC_ISS_STS__B                      2
#define   EC_OC_REG_COMM_INT_MSK_SNC_ISS_STS__W                      1
#define   EC_OC_REG_COMM_INT_MSK_SNC_ISS_STS__M                      0x4
#define   EC_OC_REG_COMM_INT_MSK_SNC_OSS_STS__B                      3
#define   EC_OC_REG_COMM_INT_MSK_SNC_OSS_STS__W                      1
#define   EC_OC_REG_COMM_INT_MSK_SNC_OSS_STS__M                      0x8
#define   EC_OC_REG_COMM_INT_MSK_SNC_NSS_STS__B                      4
#define   EC_OC_REG_COMM_INT_MSK_SNC_NSS_STS__W                      1
#define   EC_OC_REG_COMM_INT_MSK_SNC_NSS_STS__M                      0x10
#define   EC_OC_REG_COMM_INT_MSK_PCK_ERR_UPD__B                      5
#define   EC_OC_REG_COMM_INT_MSK_PCK_ERR_UPD__W                      1
#define   EC_OC_REG_COMM_INT_MSK_PCK_ERR_UPD__M                      0x20

#define EC_OC_REG_OC_MODE_LOP__A                                     0x2150010
#define EC_OC_REG_OC_MODE_LOP__W                                     16
#define EC_OC_REG_OC_MODE_LOP__M                                     0xFFFF
#define   EC_OC_REG_OC_MODE_LOP_INIT                                 0x0

#define   EC_OC_REG_OC_MODE_LOP_PAR_ENA__B                           0
#define   EC_OC_REG_OC_MODE_LOP_PAR_ENA__W                           1
#define   EC_OC_REG_OC_MODE_LOP_PAR_ENA__M                           0x1
#define     EC_OC_REG_OC_MODE_LOP_PAR_ENA_ENABLE                     0x0
#define     EC_OC_REG_OC_MODE_LOP_PAR_ENA_DISABLE                    0x1

#define   EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC__B                       2
#define   EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC__W                       1
#define   EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC__M                       0x4
#define     EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC_STATIC                 0x0
#define     EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC_DYNAMIC                0x4

#define   EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA__B                       4
#define   EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA__W                       1
#define   EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA__M                       0x10
#define     EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA_ENABLE                 0x10

#define   EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE__B                       5
#define   EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE__W                       1
#define   EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE__M                       0x20
#define     EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE_ENABLE                 0x20

#define   EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV__B                       6
#define   EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV__M                       0x40
#define     EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV_ENABLE                 0x40

#define   EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE__B                       7
#define   EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE__M                       0x80
#define     EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE_PARALLEL               0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE_SERIAL                 0x80

#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE__B                       8
#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE__M                       0x100
#define     EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE_ENABLE                 0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE_DISABLE                0x100

#define   EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK__B                       9
#define   EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK__M                       0x200
#define     EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK_STRETCH                0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK_GATE                   0x200

#define   EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR__B                       10
#define   EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR__M                       0x400
#define     EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR_CONTINOUS              0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR_BURST                  0x400

#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC__B                       11
#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC__M                       0x800
#define     EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC_ENABLE                 0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC_DISABLE                0x800

#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO__B                       12
#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO__M                       0x1000
#define     EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO_ENABLE                 0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO_DISABLE                0x1000

#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT__B                       13
#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT__M                       0x2000
#define     EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT_ENABLE                 0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT_DISABLE                0x2000

#define   EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS__B                       14
#define   EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS__W                       1
#define   EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS__M                       0x4000
#define     EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS_ENABLE                 0x0
#define     EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS_DISABLE                0x4000

#define   EC_OC_REG_OC_MODE_LOP_DER_ENA__B                           15
#define   EC_OC_REG_OC_MODE_LOP_DER_ENA__W                           1
#define   EC_OC_REG_OC_MODE_LOP_DER_ENA__M                           0x8000
#define     EC_OC_REG_OC_MODE_LOP_DER_ENA_ENABLE                     0x0
#define     EC_OC_REG_OC_MODE_LOP_DER_ENA_DISABLE                    0x8000

#define EC_OC_REG_OC_MODE_HIP__A                                     0x2150011
#define EC_OC_REG_OC_MODE_HIP__W                                     14
#define EC_OC_REG_OC_MODE_HIP__M                                     0x3FFF
#define   EC_OC_REG_OC_MODE_HIP_INIT                                 0x0

#define   EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS__B                       0
#define   EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS__M                       0x1
#define     EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS_OBSERVE                0x0
#define     EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS_CONTROL                0x1

#define   EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC__B                       1
#define   EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC__M                       0x2
#define     EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC_MPEG_SYNC              0x0
#define     EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC_MPEG                   0x2

#define   EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE__B                       2
#define   EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE__M                       0x4
#define     EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE_OBSERVE                0x0
#define     EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE_CONTROL                0x4

#define   EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC__B                       3
#define   EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC__M                       0x8
#define     EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC_MONITOR                0x0
#define     EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC_MPEG                   0x8

#define   EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC__B                       4
#define   EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC__M                       0x10
#define     EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC_MPEG                   0x0
#define     EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC_MONITOR                0x10

#define   EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE__B                       5
#define   EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE__M                       0x20
#define     EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE_ENABLE                 0x20

#define   EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE__B                       6
#define   EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE__W                       1
#define   EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE__M                       0x40
#define     EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE_ENABLE                 0x0
#define     EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE_DISABLE                0x40

#define   EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP__B                       7
#define   EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP__M                       0x80
#define     EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP_ENABLE                 0x80

#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK__B                       8
#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK__M                       0x100
#define     EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK_ENABLE                 0x100

#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL__B                       9
#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL__M                       0x200
#define     EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL_ENABLE                 0x200

#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR__B                       10
#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR__M                       0x400
#define     EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR_ENABLE                 0x400

#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT__B                       11
#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT__W                       1
#define   EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT__M                       0x800
#define     EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT_DISABLE                0x0
#define     EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT_ENABLE                 0x800

#define   EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON__B                       12
#define   EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON__W                       1
#define   EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON__M                       0x1000
#define     EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON_SEL_ZER                0x0
#define     EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON_SEL_MON                0x1000

#define   EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG__B                       13
#define   EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG__W                       1
#define   EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG__M                       0x2000
#define     EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG_SEL_ZER                0x0
#define     EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG_SEL_MPG                0x2000

#define EC_OC_REG_OC_MPG_SIO__A                                      0x2150012
#define EC_OC_REG_OC_MPG_SIO__W                                      12
#define EC_OC_REG_OC_MPG_SIO__M                                      0xFFF
#define   EC_OC_REG_OC_MPG_SIO_INIT                                  0xFFF

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_0__B                          0
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_0__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_0__M                          0x1
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_0_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_0_INPUT                     0x1

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_1__B                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_1__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_1__M                          0x2
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_1_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_1_INPUT                     0x2

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_2__B                          2
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_2__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_2__M                          0x4
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_2_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_2_INPUT                     0x4

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_3__B                          3
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_3__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_3__M                          0x8
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_3_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_3_INPUT                     0x8

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_4__B                          4
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_4__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_4__M                          0x10
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_4_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_4_INPUT                     0x10

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_5__B                          5
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_5__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_5__M                          0x20
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_5_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_5_INPUT                     0x20

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_6__B                          6
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_6__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_6__M                          0x40
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_6_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_6_INPUT                     0x40

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_7__B                          7
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_7__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_7__M                          0x80
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_7_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_7_INPUT                     0x80

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_8__B                          8
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_8__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_8__M                          0x100
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_8_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_8_INPUT                     0x100

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_9__B                          9
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_9__W                          1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_9__M                          0x200
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_9_OUTPUT                    0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_9_INPUT                     0x200

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_10__B                         10
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_10__W                         1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_10__M                         0x400
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_10_OUTPUT                   0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_10_INPUT                    0x400

#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_11__B                         11
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_11__W                         1
#define   EC_OC_REG_OC_MPG_SIO_MPG_SIO_11__M                         0x800
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_11_OUTPUT                   0x0
#define     EC_OC_REG_OC_MPG_SIO_MPG_SIO_11_INPUT                    0x800

#define EC_OC_REG_OC_MON_SIO__A                                      0x2150013
#define EC_OC_REG_OC_MON_SIO__W                                      12
#define EC_OC_REG_OC_MON_SIO__M                                      0xFFF
#define   EC_OC_REG_OC_MON_SIO_INIT                                  0xFFF

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_0__B                          0
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_0__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_0__M                          0x1
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_0_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_0_INPUT                     0x1

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_1__B                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_1__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_1__M                          0x2
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_1_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_1_INPUT                     0x2

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_2__B                          2
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_2__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_2__M                          0x4
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_2_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_2_INPUT                     0x4

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_3__B                          3
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_3__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_3__M                          0x8
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_3_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_3_INPUT                     0x8

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_4__B                          4
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_4__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_4__M                          0x10
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_4_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_4_INPUT                     0x10

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_5__B                          5
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_5__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_5__M                          0x20
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_5_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_5_INPUT                     0x20

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_6__B                          6
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_6__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_6__M                          0x40
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_6_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_6_INPUT                     0x40

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_7__B                          7
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_7__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_7__M                          0x80
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_7_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_7_INPUT                     0x80

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_8__B                          8
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_8__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_8__M                          0x100
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_8_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_8_INPUT                     0x100

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_9__B                          9
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_9__W                          1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_9__M                          0x200
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_9_OUTPUT                    0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_9_INPUT                     0x200

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_10__B                         10
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_10__W                         1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_10__M                         0x400
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_10_OUTPUT                   0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_10_INPUT                    0x400

#define   EC_OC_REG_OC_MON_SIO_MON_SIO_11__B                         11
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_11__W                         1
#define   EC_OC_REG_OC_MON_SIO_MON_SIO_11__M                         0x800
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_11_OUTPUT                   0x0
#define     EC_OC_REG_OC_MON_SIO_MON_SIO_11_INPUT                    0x800

#define EC_OC_REG_DTO_INC_LOP__A                                     0x2150014
#define EC_OC_REG_DTO_INC_LOP__W                                     16
#define EC_OC_REG_DTO_INC_LOP__M                                     0xFFFF
#define   EC_OC_REG_DTO_INC_LOP_INIT                                 0x0

#define EC_OC_REG_DTO_INC_HIP__A                                     0x2150015
#define EC_OC_REG_DTO_INC_HIP__W                                     8
#define EC_OC_REG_DTO_INC_HIP__M                                     0xFF
#define   EC_OC_REG_DTO_INC_HIP_INIT                                 0x0

#define EC_OC_REG_SNC_ISC_LVL__A                                     0x2150016
#define EC_OC_REG_SNC_ISC_LVL__W                                     12
#define EC_OC_REG_SNC_ISC_LVL__M                                     0xFFF
#define   EC_OC_REG_SNC_ISC_LVL_INIT                                 0x0

#define   EC_OC_REG_SNC_ISC_LVL_ISC__B                               0
#define   EC_OC_REG_SNC_ISC_LVL_ISC__W                               4
#define   EC_OC_REG_SNC_ISC_LVL_ISC__M                               0xF

#define   EC_OC_REG_SNC_ISC_LVL_OSC__B                               4
#define   EC_OC_REG_SNC_ISC_LVL_OSC__W                               4
#define   EC_OC_REG_SNC_ISC_LVL_OSC__M                               0xF0

#define   EC_OC_REG_SNC_ISC_LVL_NSC__B                               8
#define   EC_OC_REG_SNC_ISC_LVL_NSC__W                               4
#define   EC_OC_REG_SNC_ISC_LVL_NSC__M                               0xF00

#define EC_OC_REG_SNC_NSC_LVL__A                                     0x2150017
#define EC_OC_REG_SNC_NSC_LVL__W                                     8
#define EC_OC_REG_SNC_NSC_LVL__M                                     0xFF
#define   EC_OC_REG_SNC_NSC_LVL_INIT                                 0x0

#define EC_OC_REG_SNC_SNC_MODE__A                                    0x2150019
#define EC_OC_REG_SNC_SNC_MODE__W                                    2
#define EC_OC_REG_SNC_SNC_MODE__M                                    0x3
#define   EC_OC_REG_SNC_SNC_MODE_SEARCH                              0x0
#define   EC_OC_REG_SNC_SNC_MODE_TRACK                               0x1
#define   EC_OC_REG_SNC_SNC_MODE_LOCK                                0x2

#define EC_OC_REG_SNC_PCK_NMB__A                                     0x215001A
#define EC_OC_REG_SNC_PCK_NMB__W                                     16
#define EC_OC_REG_SNC_PCK_NMB__M                                     0xFFFF

#define EC_OC_REG_SNC_PCK_CNT__A                                     0x215001B
#define EC_OC_REG_SNC_PCK_CNT__W                                     16
#define EC_OC_REG_SNC_PCK_CNT__M                                     0xFFFF

#define EC_OC_REG_SNC_PCK_ERR__A                                     0x215001C
#define EC_OC_REG_SNC_PCK_ERR__W                                     16
#define EC_OC_REG_SNC_PCK_ERR__M                                     0xFFFF

#define EC_OC_REG_TMD_TOP_MODE__A                                    0x215001D
#define EC_OC_REG_TMD_TOP_MODE__W                                    2
#define EC_OC_REG_TMD_TOP_MODE__M                                    0x3
#define   EC_OC_REG_TMD_TOP_MODE_INIT                                0x0
#define   EC_OC_REG_TMD_TOP_MODE_SELECT_ACT_ACT                      0x0
#define   EC_OC_REG_TMD_TOP_MODE_SELECT_TOP_TOP                      0x1
#define   EC_OC_REG_TMD_TOP_MODE_SELECT_BOT_BOT                      0x2
#define   EC_OC_REG_TMD_TOP_MODE_SELECT_TOP_BOT                      0x3

#define EC_OC_REG_TMD_TOP_CNT__A                                     0x215001E
#define EC_OC_REG_TMD_TOP_CNT__W                                     10
#define EC_OC_REG_TMD_TOP_CNT__M                                     0x3FF
#define   EC_OC_REG_TMD_TOP_CNT_INIT                                 0x0

#define EC_OC_REG_TMD_HIL_MAR__A                                     0x215001F
#define EC_OC_REG_TMD_HIL_MAR__W                                     10
#define EC_OC_REG_TMD_HIL_MAR__M                                     0x3FF
#define   EC_OC_REG_TMD_HIL_MAR_INIT                                 0x0

#define EC_OC_REG_TMD_LOL_MAR__A                                     0x2150020
#define EC_OC_REG_TMD_LOL_MAR__W                                     10
#define EC_OC_REG_TMD_LOL_MAR__M                                     0x3FF
#define   EC_OC_REG_TMD_LOL_MAR_INIT                                 0x0

#define EC_OC_REG_TMD_CUR_CNT__A                                     0x2150021
#define EC_OC_REG_TMD_CUR_CNT__W                                     4
#define EC_OC_REG_TMD_CUR_CNT__M                                     0xF
#define   EC_OC_REG_TMD_CUR_CNT_INIT                                 0x0

#define EC_OC_REG_TMD_IUR_CNT__A                                     0x2150022
#define EC_OC_REG_TMD_IUR_CNT__W                                     4
#define EC_OC_REG_TMD_IUR_CNT__M                                     0xF
#define   EC_OC_REG_TMD_IUR_CNT_INIT                                 0x0

#define EC_OC_REG_AVR_ASH_CNT__A                                     0x2150023
#define EC_OC_REG_AVR_ASH_CNT__W                                     4
#define EC_OC_REG_AVR_ASH_CNT__M                                     0xF
#define   EC_OC_REG_AVR_ASH_CNT_INIT                                 0x0

#define EC_OC_REG_AVR_BSH_CNT__A                                     0x2150024
#define EC_OC_REG_AVR_BSH_CNT__W                                     4
#define EC_OC_REG_AVR_BSH_CNT__M                                     0xF
#define   EC_OC_REG_AVR_BSH_CNT_INIT                                 0x0

#define EC_OC_REG_AVR_AVE_LOP__A                                     0x2150025
#define EC_OC_REG_AVR_AVE_LOP__W                                     16
#define EC_OC_REG_AVR_AVE_LOP__M                                     0xFFFF

#define EC_OC_REG_AVR_AVE_HIP__A                                     0x2150026
#define EC_OC_REG_AVR_AVE_HIP__W                                     5
#define EC_OC_REG_AVR_AVE_HIP__M                                     0x1F

#define EC_OC_REG_RCN_MODE__A                                        0x2150027
#define EC_OC_REG_RCN_MODE__W                                        3
#define EC_OC_REG_RCN_MODE__M                                        0x7
#define   EC_OC_REG_RCN_MODE_INIT                                    0x0

#define   EC_OC_REG_RCN_MODE_MODE_0__B                               0
#define   EC_OC_REG_RCN_MODE_MODE_0__W                               1
#define   EC_OC_REG_RCN_MODE_MODE_0__M                               0x1
#define     EC_OC_REG_RCN_MODE_MODE_0_ENABLE                         0x0
#define     EC_OC_REG_RCN_MODE_MODE_0_DISABLE                        0x1

#define   EC_OC_REG_RCN_MODE_MODE_1__B                               1
#define   EC_OC_REG_RCN_MODE_MODE_1__W                               1
#define   EC_OC_REG_RCN_MODE_MODE_1__M                               0x2
#define     EC_OC_REG_RCN_MODE_MODE_1_ENABLE                         0x0
#define     EC_OC_REG_RCN_MODE_MODE_1_DISABLE                        0x2

#define   EC_OC_REG_RCN_MODE_MODE_2__B                               2
#define   EC_OC_REG_RCN_MODE_MODE_2__W                               1
#define   EC_OC_REG_RCN_MODE_MODE_2__M                               0x4
#define     EC_OC_REG_RCN_MODE_MODE_2_ENABLE                         0x4
#define     EC_OC_REG_RCN_MODE_MODE_2_DISABLE                        0x0

#define EC_OC_REG_RCN_CRA_LOP__A                                     0x2150028
#define EC_OC_REG_RCN_CRA_LOP__W                                     16
#define EC_OC_REG_RCN_CRA_LOP__M                                     0xFFFF
#define   EC_OC_REG_RCN_CRA_LOP_INIT                                 0x0

#define EC_OC_REG_RCN_CRA_HIP__A                                     0x2150029
#define EC_OC_REG_RCN_CRA_HIP__W                                     8
#define EC_OC_REG_RCN_CRA_HIP__M                                     0xFF
#define   EC_OC_REG_RCN_CRA_HIP_INIT                                 0x0

#define EC_OC_REG_RCN_CST_LOP__A                                     0x215002A
#define EC_OC_REG_RCN_CST_LOP__W                                     16
#define EC_OC_REG_RCN_CST_LOP__M                                     0xFFFF
#define   EC_OC_REG_RCN_CST_LOP_INIT                                 0x0

#define EC_OC_REG_RCN_CST_HIP__A                                     0x215002B
#define EC_OC_REG_RCN_CST_HIP__W                                     8
#define EC_OC_REG_RCN_CST_HIP__M                                     0xFF
#define   EC_OC_REG_RCN_CST_HIP_INIT                                 0x0

#define EC_OC_REG_RCN_SET_LVL__A                                     0x215002C
#define EC_OC_REG_RCN_SET_LVL__W                                     9
#define EC_OC_REG_RCN_SET_LVL__M                                     0x1FF
#define   EC_OC_REG_RCN_SET_LVL_INIT                                 0x0

#define EC_OC_REG_RCN_GAI_LVL__A                                     0x215002D
#define EC_OC_REG_RCN_GAI_LVL__W                                     4
#define EC_OC_REG_RCN_GAI_LVL__M                                     0xF
#define   EC_OC_REG_RCN_GAI_LVL_INIT                                 0x0

#define EC_OC_REG_RCN_DRA_LOP__A                                     0x215002E
#define EC_OC_REG_RCN_DRA_LOP__W                                     16
#define EC_OC_REG_RCN_DRA_LOP__M                                     0xFFFF

#define EC_OC_REG_RCN_DRA_HIP__A                                     0x215002F
#define EC_OC_REG_RCN_DRA_HIP__W                                     8
#define EC_OC_REG_RCN_DRA_HIP__M                                     0xFF

#define EC_OC_REG_RCN_DOF_LOP__A                                     0x2150030
#define EC_OC_REG_RCN_DOF_LOP__W                                     16
#define EC_OC_REG_RCN_DOF_LOP__M                                     0xFFFF

#define EC_OC_REG_RCN_DOF_HIP__A                                     0x2150031
#define EC_OC_REG_RCN_DOF_HIP__W                                     8
#define EC_OC_REG_RCN_DOF_HIP__M                                     0xFF

#define EC_OC_REG_RCN_CLP_LOP__A                                     0x2150032
#define EC_OC_REG_RCN_CLP_LOP__W                                     16
#define EC_OC_REG_RCN_CLP_LOP__M                                     0xFFFF
#define   EC_OC_REG_RCN_CLP_LOP_INIT                                 0xFFFF

#define EC_OC_REG_RCN_CLP_HIP__A                                     0x2150033
#define EC_OC_REG_RCN_CLP_HIP__W                                     8
#define EC_OC_REG_RCN_CLP_HIP__M                                     0xFF
#define   EC_OC_REG_RCN_CLP_HIP_INIT                                 0xFF

#define EC_OC_REG_RCN_MAP_LOP__A                                     0x2150034
#define EC_OC_REG_RCN_MAP_LOP__W                                     16
#define EC_OC_REG_RCN_MAP_LOP__M                                     0xFFFF

#define EC_OC_REG_RCN_MAP_HIP__A                                     0x2150035
#define EC_OC_REG_RCN_MAP_HIP__W                                     8
#define EC_OC_REG_RCN_MAP_HIP__M                                     0xFF

#define EC_OC_REG_OCR_MPG_UOS__A                                     0x2150036
#define EC_OC_REG_OCR_MPG_UOS__W                                     12
#define EC_OC_REG_OCR_MPG_UOS__M                                     0xFFF
#define   EC_OC_REG_OCR_MPG_UOS_INIT                                 0x0

#define   EC_OC_REG_OCR_MPG_UOS_DAT_0__B                             0
#define   EC_OC_REG_OCR_MPG_UOS_DAT_0__W                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_0__M                             0x1
#define     EC_OC_REG_OCR_MPG_UOS_DAT_0_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_UOS_DAT_0_ENABLE                       0x1

#define   EC_OC_REG_OCR_MPG_UOS_DAT_1__B                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_1__W                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_1__M                             0x2
#define     EC_OC_REG_OCR_MPG_UOS_DAT_1_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_UOS_DAT_1_ENABLE                       0x2

#define   EC_OC_REG_OCR_MPG_UOS_DAT_2__B                             2
#define   EC_OC_REG_OCR_MPG_UOS_DAT_2__W                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_2__M                             0x4
#define     EC_OC_REG_OCR_MPG_UOS_DAT_2_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_UOS_DAT_2_ENABLE                       0x4

#define   EC_OC_REG_OCR_MPG_UOS_DAT_3__B                             3
#define   EC_OC_REG_OCR_MPG_UOS_DAT_3__W                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_3__M                             0x8
#define     EC_OC_REG_OCR_MPG_UOS_DAT_3_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_UOS_DAT_3_ENABLE                       0x8

#define   EC_OC_REG_OCR_MPG_UOS_DAT_4__B                             4
#define   EC_OC_REG_OCR_MPG_UOS_DAT_4__W                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_4__M                             0x10
#define     EC_OC_REG_OCR_MPG_UOS_DAT_4_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_UOS_DAT_4_ENABLE                       0x10

#define   EC_OC_REG_OCR_MPG_UOS_DAT_5__B                             5
#define   EC_OC_REG_OCR_MPG_UOS_DAT_5__W                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_5__M                             0x20
#define     EC_OC_REG_OCR_MPG_UOS_DAT_5_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_UOS_DAT_5_ENABLE                       0x20

#define   EC_OC_REG_OCR_MPG_UOS_DAT_6__B                             6
#define   EC_OC_REG_OCR_MPG_UOS_DAT_6__W                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_6__M                             0x40
#define     EC_OC_REG_OCR_MPG_UOS_DAT_6_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_UOS_DAT_6_ENABLE                       0x40

#define   EC_OC_REG_OCR_MPG_UOS_DAT_7__B                             7
#define   EC_OC_REG_OCR_MPG_UOS_DAT_7__W                             1
#define   EC_OC_REG_OCR_MPG_UOS_DAT_7__M                             0x80
#define     EC_OC_REG_OCR_MPG_UOS_DAT_7_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_UOS_DAT_7_ENABLE                       0x80

#define   EC_OC_REG_OCR_MPG_UOS_ERR__B                               8
#define   EC_OC_REG_OCR_MPG_UOS_ERR__W                               1
#define   EC_OC_REG_OCR_MPG_UOS_ERR__M                               0x100
#define     EC_OC_REG_OCR_MPG_UOS_ERR_DISABLE                        0x0
#define     EC_OC_REG_OCR_MPG_UOS_ERR_ENABLE                         0x100

#define   EC_OC_REG_OCR_MPG_UOS_STR__B                               9
#define   EC_OC_REG_OCR_MPG_UOS_STR__W                               1
#define   EC_OC_REG_OCR_MPG_UOS_STR__M                               0x200
#define     EC_OC_REG_OCR_MPG_UOS_STR_DISABLE                        0x0
#define     EC_OC_REG_OCR_MPG_UOS_STR_ENABLE                         0x200

#define   EC_OC_REG_OCR_MPG_UOS_VAL__B                               10
#define   EC_OC_REG_OCR_MPG_UOS_VAL__W                               1
#define   EC_OC_REG_OCR_MPG_UOS_VAL__M                               0x400
#define     EC_OC_REG_OCR_MPG_UOS_VAL_DISABLE                        0x0
#define     EC_OC_REG_OCR_MPG_UOS_VAL_ENABLE                         0x400

#define   EC_OC_REG_OCR_MPG_UOS_CLK__B                               11
#define   EC_OC_REG_OCR_MPG_UOS_CLK__W                               1
#define   EC_OC_REG_OCR_MPG_UOS_CLK__M                               0x800
#define     EC_OC_REG_OCR_MPG_UOS_CLK_DISABLE                        0x0
#define     EC_OC_REG_OCR_MPG_UOS_CLK_ENABLE                         0x800

#define EC_OC_REG_OCR_MPG_WRI__A                                     0x2150037
#define EC_OC_REG_OCR_MPG_WRI__W                                     12
#define EC_OC_REG_OCR_MPG_WRI__M                                     0xFFF
#define   EC_OC_REG_OCR_MPG_WRI_INIT                                 0x0
#define   EC_OC_REG_OCR_MPG_WRI_DAT_0__B                             0
#define   EC_OC_REG_OCR_MPG_WRI_DAT_0__W                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_0__M                             0x1
#define     EC_OC_REG_OCR_MPG_WRI_DAT_0_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_WRI_DAT_0_ENABLE                       0x1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_1__B                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_1__W                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_1__M                             0x2
#define     EC_OC_REG_OCR_MPG_WRI_DAT_1_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_WRI_DAT_1_ENABLE                       0x2
#define   EC_OC_REG_OCR_MPG_WRI_DAT_2__B                             2
#define   EC_OC_REG_OCR_MPG_WRI_DAT_2__W                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_2__M                             0x4
#define     EC_OC_REG_OCR_MPG_WRI_DAT_2_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_WRI_DAT_2_ENABLE                       0x4
#define   EC_OC_REG_OCR_MPG_WRI_DAT_3__B                             3
#define   EC_OC_REG_OCR_MPG_WRI_DAT_3__W                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_3__M                             0x8
#define     EC_OC_REG_OCR_MPG_WRI_DAT_3_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_WRI_DAT_3_ENABLE                       0x8
#define   EC_OC_REG_OCR_MPG_WRI_DAT_4__B                             4
#define   EC_OC_REG_OCR_MPG_WRI_DAT_4__W                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_4__M                             0x10
#define     EC_OC_REG_OCR_MPG_WRI_DAT_4_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_WRI_DAT_4_ENABLE                       0x10
#define   EC_OC_REG_OCR_MPG_WRI_DAT_5__B                             5
#define   EC_OC_REG_OCR_MPG_WRI_DAT_5__W                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_5__M                             0x20
#define     EC_OC_REG_OCR_MPG_WRI_DAT_5_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_WRI_DAT_5_ENABLE                       0x20
#define   EC_OC_REG_OCR_MPG_WRI_DAT_6__B                             6
#define   EC_OC_REG_OCR_MPG_WRI_DAT_6__W                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_6__M                             0x40
#define     EC_OC_REG_OCR_MPG_WRI_DAT_6_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_WRI_DAT_6_ENABLE                       0x40
#define   EC_OC_REG_OCR_MPG_WRI_DAT_7__B                             7
#define   EC_OC_REG_OCR_MPG_WRI_DAT_7__W                             1
#define   EC_OC_REG_OCR_MPG_WRI_DAT_7__M                             0x80
#define     EC_OC_REG_OCR_MPG_WRI_DAT_7_DISABLE                      0x0
#define     EC_OC_REG_OCR_MPG_WRI_DAT_7_ENABLE                       0x80
#define   EC_OC_REG_OCR_MPG_WRI_ERR__B                               8
#define   EC_OC_REG_OCR_MPG_WRI_ERR__W                               1
#define   EC_OC_REG_OCR_MPG_WRI_ERR__M                               0x100
#define     EC_OC_REG_OCR_MPG_WRI_ERR_DISABLE                        0x0
#define     EC_OC_REG_OCR_MPG_WRI_ERR_ENABLE                         0x100
#define   EC_OC_REG_OCR_MPG_WRI_STR__B                               9
#define   EC_OC_REG_OCR_MPG_WRI_STR__W                               1
#define   EC_OC_REG_OCR_MPG_WRI_STR__M                               0x200
#define     EC_OC_REG_OCR_MPG_WRI_STR_DISABLE                        0x0
#define     EC_OC_REG_OCR_MPG_WRI_STR_ENABLE                         0x200
#define   EC_OC_REG_OCR_MPG_WRI_VAL__B                               10
#define   EC_OC_REG_OCR_MPG_WRI_VAL__W                               1
#define   EC_OC_REG_OCR_MPG_WRI_VAL__M                               0x400
#define     EC_OC_REG_OCR_MPG_WRI_VAL_DISABLE                        0x0
#define     EC_OC_REG_OCR_MPG_WRI_VAL_ENABLE                         0x400
#define   EC_OC_REG_OCR_MPG_WRI_CLK__B                               11
#define   EC_OC_REG_OCR_MPG_WRI_CLK__W                               1
#define   EC_OC_REG_OCR_MPG_WRI_CLK__M                               0x800
#define     EC_OC_REG_OCR_MPG_WRI_CLK_DISABLE                        0x0
#define     EC_OC_REG_OCR_MPG_WRI_CLK_ENABLE                         0x800

#define EC_OC_REG_OCR_MPG_USR_DAT__A                                 0x2150038
#define EC_OC_REG_OCR_MPG_USR_DAT__W                                 12
#define EC_OC_REG_OCR_MPG_USR_DAT__M                                 0xFFF

#define EC_OC_REG_OCR_MON_UOS__A                                     0x2150039
#define EC_OC_REG_OCR_MON_UOS__W                                     12
#define EC_OC_REG_OCR_MON_UOS__M                                     0xFFF
#define   EC_OC_REG_OCR_MON_UOS_INIT                                 0x0

#define   EC_OC_REG_OCR_MON_UOS_DAT_0__B                             0
#define   EC_OC_REG_OCR_MON_UOS_DAT_0__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_0__M                             0x1
#define     EC_OC_REG_OCR_MON_UOS_DAT_0_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_0_ENABLE                       0x1

#define   EC_OC_REG_OCR_MON_UOS_DAT_1__B                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_1__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_1__M                             0x2
#define     EC_OC_REG_OCR_MON_UOS_DAT_1_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_1_ENABLE                       0x2

#define   EC_OC_REG_OCR_MON_UOS_DAT_2__B                             2
#define   EC_OC_REG_OCR_MON_UOS_DAT_2__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_2__M                             0x4
#define     EC_OC_REG_OCR_MON_UOS_DAT_2_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_2_ENABLE                       0x4

#define   EC_OC_REG_OCR_MON_UOS_DAT_3__B                             3
#define   EC_OC_REG_OCR_MON_UOS_DAT_3__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_3__M                             0x8
#define     EC_OC_REG_OCR_MON_UOS_DAT_3_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_3_ENABLE                       0x8

#define   EC_OC_REG_OCR_MON_UOS_DAT_4__B                             4
#define   EC_OC_REG_OCR_MON_UOS_DAT_4__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_4__M                             0x10
#define     EC_OC_REG_OCR_MON_UOS_DAT_4_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_4_ENABLE                       0x10

#define   EC_OC_REG_OCR_MON_UOS_DAT_5__B                             5
#define   EC_OC_REG_OCR_MON_UOS_DAT_5__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_5__M                             0x20
#define     EC_OC_REG_OCR_MON_UOS_DAT_5_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_5_ENABLE                       0x20

#define   EC_OC_REG_OCR_MON_UOS_DAT_6__B                             6
#define   EC_OC_REG_OCR_MON_UOS_DAT_6__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_6__M                             0x40
#define     EC_OC_REG_OCR_MON_UOS_DAT_6_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_6_ENABLE                       0x40

#define   EC_OC_REG_OCR_MON_UOS_DAT_7__B                             7
#define   EC_OC_REG_OCR_MON_UOS_DAT_7__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_7__M                             0x80
#define     EC_OC_REG_OCR_MON_UOS_DAT_7_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_7_ENABLE                       0x80

#define   EC_OC_REG_OCR_MON_UOS_DAT_8__B                             8
#define   EC_OC_REG_OCR_MON_UOS_DAT_8__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_8__M                             0x100
#define     EC_OC_REG_OCR_MON_UOS_DAT_8_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_8_ENABLE                       0x100

#define   EC_OC_REG_OCR_MON_UOS_DAT_9__B                             9
#define   EC_OC_REG_OCR_MON_UOS_DAT_9__W                             1
#define   EC_OC_REG_OCR_MON_UOS_DAT_9__M                             0x200
#define     EC_OC_REG_OCR_MON_UOS_DAT_9_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_UOS_DAT_9_ENABLE                       0x200

#define   EC_OC_REG_OCR_MON_UOS_VAL__B                               10
#define   EC_OC_REG_OCR_MON_UOS_VAL__W                               1
#define   EC_OC_REG_OCR_MON_UOS_VAL__M                               0x400
#define     EC_OC_REG_OCR_MON_UOS_VAL_DISABLE                        0x0
#define     EC_OC_REG_OCR_MON_UOS_VAL_ENABLE                         0x400

#define   EC_OC_REG_OCR_MON_UOS_CLK__B                               11
#define   EC_OC_REG_OCR_MON_UOS_CLK__W                               1
#define   EC_OC_REG_OCR_MON_UOS_CLK__M                               0x800
#define     EC_OC_REG_OCR_MON_UOS_CLK_DISABLE                        0x0
#define     EC_OC_REG_OCR_MON_UOS_CLK_ENABLE                         0x800

#define EC_OC_REG_OCR_MON_WRI__A                                     0x215003A
#define EC_OC_REG_OCR_MON_WRI__W                                     12
#define EC_OC_REG_OCR_MON_WRI__M                                     0xFFF
#define   EC_OC_REG_OCR_MON_WRI_INIT                                 0x0
#define   EC_OC_REG_OCR_MON_WRI_DAT_0__B                             0
#define   EC_OC_REG_OCR_MON_WRI_DAT_0__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_0__M                             0x1
#define     EC_OC_REG_OCR_MON_WRI_DAT_0_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_0_ENABLE                       0x1
#define   EC_OC_REG_OCR_MON_WRI_DAT_1__B                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_1__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_1__M                             0x2
#define     EC_OC_REG_OCR_MON_WRI_DAT_1_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_1_ENABLE                       0x2
#define   EC_OC_REG_OCR_MON_WRI_DAT_2__B                             2
#define   EC_OC_REG_OCR_MON_WRI_DAT_2__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_2__M                             0x4
#define     EC_OC_REG_OCR_MON_WRI_DAT_2_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_2_ENABLE                       0x4
#define   EC_OC_REG_OCR_MON_WRI_DAT_3__B                             3
#define   EC_OC_REG_OCR_MON_WRI_DAT_3__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_3__M                             0x8
#define     EC_OC_REG_OCR_MON_WRI_DAT_3_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_3_ENABLE                       0x8
#define   EC_OC_REG_OCR_MON_WRI_DAT_4__B                             4
#define   EC_OC_REG_OCR_MON_WRI_DAT_4__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_4__M                             0x10
#define     EC_OC_REG_OCR_MON_WRI_DAT_4_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_4_ENABLE                       0x10
#define   EC_OC_REG_OCR_MON_WRI_DAT_5__B                             5
#define   EC_OC_REG_OCR_MON_WRI_DAT_5__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_5__M                             0x20
#define     EC_OC_REG_OCR_MON_WRI_DAT_5_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_5_ENABLE                       0x20
#define   EC_OC_REG_OCR_MON_WRI_DAT_6__B                             6
#define   EC_OC_REG_OCR_MON_WRI_DAT_6__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_6__M                             0x40
#define     EC_OC_REG_OCR_MON_WRI_DAT_6_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_6_ENABLE                       0x40
#define   EC_OC_REG_OCR_MON_WRI_DAT_7__B                             7
#define   EC_OC_REG_OCR_MON_WRI_DAT_7__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_7__M                             0x80
#define     EC_OC_REG_OCR_MON_WRI_DAT_7_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_7_ENABLE                       0x80
#define   EC_OC_REG_OCR_MON_WRI_DAT_8__B                             8
#define   EC_OC_REG_OCR_MON_WRI_DAT_8__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_8__M                             0x100
#define     EC_OC_REG_OCR_MON_WRI_DAT_8_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_8_ENABLE                       0x100
#define   EC_OC_REG_OCR_MON_WRI_DAT_9__B                             9
#define   EC_OC_REG_OCR_MON_WRI_DAT_9__W                             1
#define   EC_OC_REG_OCR_MON_WRI_DAT_9__M                             0x200
#define     EC_OC_REG_OCR_MON_WRI_DAT_9_DISABLE                      0x0
#define     EC_OC_REG_OCR_MON_WRI_DAT_9_ENABLE                       0x200
#define   EC_OC_REG_OCR_MON_WRI_VAL__B                               10
#define   EC_OC_REG_OCR_MON_WRI_VAL__W                               1
#define   EC_OC_REG_OCR_MON_WRI_VAL__M                               0x400
#define     EC_OC_REG_OCR_MON_WRI_VAL_DISABLE                        0x0
#define     EC_OC_REG_OCR_MON_WRI_VAL_ENABLE                         0x400
#define   EC_OC_REG_OCR_MON_WRI_CLK__B                               11
#define   EC_OC_REG_OCR_MON_WRI_CLK__W                               1
#define   EC_OC_REG_OCR_MON_WRI_CLK__M                               0x800
#define     EC_OC_REG_OCR_MON_WRI_CLK_DISABLE                        0x0
#define     EC_OC_REG_OCR_MON_WRI_CLK_ENABLE                         0x800

#define EC_OC_REG_OCR_MON_USR_DAT__A                                 0x215003B
#define EC_OC_REG_OCR_MON_USR_DAT__W                                 12
#define EC_OC_REG_OCR_MON_USR_DAT__M                                 0xFFF

#define EC_OC_REG_OCR_MON_CNT__A                                     0x215003C
#define EC_OC_REG_OCR_MON_CNT__W                                     14
#define EC_OC_REG_OCR_MON_CNT__M                                     0x3FFF
#define   EC_OC_REG_OCR_MON_CNT_INIT                                 0x0

#define EC_OC_REG_OCR_MON_RDX__A                                     0x215003D
#define EC_OC_REG_OCR_MON_RDX__W                                     1
#define EC_OC_REG_OCR_MON_RDX__M                                     0x1
#define   EC_OC_REG_OCR_MON_RDX_INIT                                 0x0

#define EC_OC_REG_OCR_MON_RD0__A                                     0x215003E
#define EC_OC_REG_OCR_MON_RD0__W                                     10
#define EC_OC_REG_OCR_MON_RD0__M                                     0x3FF

#define EC_OC_REG_OCR_MON_RD1__A                                     0x215003F
#define EC_OC_REG_OCR_MON_RD1__W                                     10
#define EC_OC_REG_OCR_MON_RD1__M                                     0x3FF

#define EC_OC_REG_OCR_MON_RD2__A                                     0x2150040
#define EC_OC_REG_OCR_MON_RD2__W                                     10
#define EC_OC_REG_OCR_MON_RD2__M                                     0x3FF

#define EC_OC_REG_OCR_MON_RD3__A                                     0x2150041
#define EC_OC_REG_OCR_MON_RD3__W                                     10
#define EC_OC_REG_OCR_MON_RD3__M                                     0x3FF

#define EC_OC_REG_OCR_MON_RD4__A                                     0x2150042
#define EC_OC_REG_OCR_MON_RD4__W                                     10
#define EC_OC_REG_OCR_MON_RD4__M                                     0x3FF

#define EC_OC_REG_OCR_MON_RD5__A                                     0x2150043
#define EC_OC_REG_OCR_MON_RD5__W                                     10
#define EC_OC_REG_OCR_MON_RD5__M                                     0x3FF

#define EC_OC_REG_OCR_INV_MON__A                                     0x2150044
#define EC_OC_REG_OCR_INV_MON__W                                     12
#define EC_OC_REG_OCR_INV_MON__M                                     0xFFF
#define   EC_OC_REG_OCR_INV_MON_INIT                                 0x0

#define EC_OC_REG_IPR_INV_MPG__A                                     0x2150045
#define EC_OC_REG_IPR_INV_MPG__W                                     12
#define EC_OC_REG_IPR_INV_MPG__M                                     0xFFF
#define   EC_OC_REG_IPR_INV_MPG_INIT                                 0x0

#define EC_OC_REG_IPR_MSR_SNC__A                                     0x2150046
#define EC_OC_REG_IPR_MSR_SNC__W                                     6
#define EC_OC_REG_IPR_MSR_SNC__M                                     0x3F
#define   EC_OC_REG_IPR_MSR_SNC_INIT                                 0x0

#define EC_OC_RAM__A                                                 0x2160000

#define CC_SID                                                       0x1B

#define CC_COMM_EXEC__A                                              0x2400000
#define CC_COMM_EXEC__W                                              3
#define CC_COMM_EXEC__M                                              0x7
#define   CC_COMM_EXEC_CTL__B                                        0
#define   CC_COMM_EXEC_CTL__W                                        3
#define   CC_COMM_EXEC_CTL__M                                        0x7
#define     CC_COMM_EXEC_CTL_STOP                                    0x0
#define     CC_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     CC_COMM_EXEC_CTL_HOLD                                    0x2
#define     CC_COMM_EXEC_CTL_STEP                                    0x3
#define     CC_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     CC_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define CC_COMM_STATE__A                                             0x2400001
#define CC_COMM_STATE__W                                             16
#define CC_COMM_STATE__M                                             0xFFFF
#define CC_COMM_MB__A                                                0x2400002
#define CC_COMM_MB__W                                                16
#define CC_COMM_MB__M                                                0xFFFF
#define CC_COMM_SERVICE0__A                                          0x2400003
#define CC_COMM_SERVICE0__W                                          16
#define CC_COMM_SERVICE0__M                                          0xFFFF
#define CC_COMM_SERVICE1__A                                          0x2400004
#define CC_COMM_SERVICE1__W                                          16
#define CC_COMM_SERVICE1__M                                          0xFFFF
#define CC_COMM_INT_STA__A                                           0x2400007
#define CC_COMM_INT_STA__W                                           16
#define CC_COMM_INT_STA__M                                           0xFFFF
#define CC_COMM_INT_MSK__A                                           0x2400008
#define CC_COMM_INT_MSK__W                                           16
#define CC_COMM_INT_MSK__M                                           0xFFFF

#define CC_REG_COMM_EXEC__A                                          0x2410000
#define CC_REG_COMM_EXEC__W                                          3
#define CC_REG_COMM_EXEC__M                                          0x7
#define   CC_REG_COMM_EXEC_CTL__B                                    0
#define   CC_REG_COMM_EXEC_CTL__W                                    3
#define   CC_REG_COMM_EXEC_CTL__M                                    0x7
#define     CC_REG_COMM_EXEC_CTL_STOP                                0x0
#define     CC_REG_COMM_EXEC_CTL_ACTIVE                              0x1
#define     CC_REG_COMM_EXEC_CTL_HOLD                                0x2
#define     CC_REG_COMM_EXEC_CTL_STEP                                0x3
#define     CC_REG_COMM_EXEC_CTL_BYPASS_STOP                         0x4
#define     CC_REG_COMM_EXEC_CTL_BYPASS_HOLD                         0x6

#define CC_REG_COMM_STATE__A                                         0x2410001
#define CC_REG_COMM_STATE__W                                         16
#define CC_REG_COMM_STATE__M                                         0xFFFF
#define CC_REG_COMM_MB__A                                            0x2410002
#define CC_REG_COMM_MB__W                                            16
#define CC_REG_COMM_MB__M                                            0xFFFF
#define CC_REG_COMM_SERVICE0__A                                      0x2410003
#define CC_REG_COMM_SERVICE0__W                                      16
#define CC_REG_COMM_SERVICE0__M                                      0xFFFF
#define CC_REG_COMM_SERVICE1__A                                      0x2410004
#define CC_REG_COMM_SERVICE1__W                                      16
#define CC_REG_COMM_SERVICE1__M                                      0xFFFF
#define CC_REG_COMM_INT_STA__A                                       0x2410007
#define CC_REG_COMM_INT_STA__W                                       16
#define CC_REG_COMM_INT_STA__M                                       0xFFFF
#define CC_REG_COMM_INT_MSK__A                                       0x2410008
#define CC_REG_COMM_INT_MSK__W                                       16
#define CC_REG_COMM_INT_MSK__M                                       0xFFFF

#define CC_REG_OSC_MODE__A                                           0x2410010
#define CC_REG_OSC_MODE__W                                           2
#define CC_REG_OSC_MODE__M                                           0x3
#define   CC_REG_OSC_MODE_OHW                                        0x0
#define   CC_REG_OSC_MODE_M20                                        0x1
#define   CC_REG_OSC_MODE_M48                                        0x2

#define CC_REG_PLL_MODE__A                                           0x2410011
#define CC_REG_PLL_MODE__W                                           6
#define CC_REG_PLL_MODE__M                                           0x3F
#define   CC_REG_PLL_MODE_INIT                                       0xC
#define   CC_REG_PLL_MODE_BYPASS__B                                  0
#define   CC_REG_PLL_MODE_BYPASS__W                                  2
#define   CC_REG_PLL_MODE_BYPASS__M                                  0x3
#define     CC_REG_PLL_MODE_BYPASS_OHW                               0x0
#define     CC_REG_PLL_MODE_BYPASS_PLL                               0x1
#define     CC_REG_PLL_MODE_BYPASS_BYPASS                            0x2
#define   CC_REG_PLL_MODE_PUMP__B                                    2
#define   CC_REG_PLL_MODE_PUMP__W                                    3
#define   CC_REG_PLL_MODE_PUMP__M                                    0x1C
#define     CC_REG_PLL_MODE_PUMP_OFF                                 0x0
#define     CC_REG_PLL_MODE_PUMP_CUR_08                              0x4
#define     CC_REG_PLL_MODE_PUMP_CUR_09                              0x8
#define     CC_REG_PLL_MODE_PUMP_CUR_10                              0xC
#define     CC_REG_PLL_MODE_PUMP_CUR_11                              0x10
#define     CC_REG_PLL_MODE_PUMP_CUR_12                              0x14
#define   CC_REG_PLL_MODE_OUT_EN__B                                  5
#define   CC_REG_PLL_MODE_OUT_EN__W                                  1
#define   CC_REG_PLL_MODE_OUT_EN__M                                  0x20
#define     CC_REG_PLL_MODE_OUT_EN_OFF                               0x0
#define     CC_REG_PLL_MODE_OUT_EN_ON                                0x20

#define CC_REG_REF_DIVIDE__A                                         0x2410012
#define CC_REG_REF_DIVIDE__W                                         4
#define CC_REG_REF_DIVIDE__M                                         0xF
#define   CC_REG_REF_DIVIDE_INIT                                     0xA
#define   CC_REG_REF_DIVIDE_OHW                                      0x0
#define   CC_REG_REF_DIVIDE_D01                                      0x1
#define   CC_REG_REF_DIVIDE_D02                                      0x2
#define   CC_REG_REF_DIVIDE_D03                                      0x3
#define   CC_REG_REF_DIVIDE_D04                                      0x4
#define   CC_REG_REF_DIVIDE_D05                                      0x5
#define   CC_REG_REF_DIVIDE_D06                                      0x6
#define   CC_REG_REF_DIVIDE_D07                                      0x7
#define   CC_REG_REF_DIVIDE_D08                                      0x8
#define   CC_REG_REF_DIVIDE_D09                                      0x9
#define   CC_REG_REF_DIVIDE_D10                                      0xA

#define CC_REG_REF_DELAY__A                                          0x2410013
#define CC_REG_REF_DELAY__W                                          3
#define CC_REG_REF_DELAY__M                                          0x7
#define   CC_REG_REF_DELAY_EDGE__B                                   0
#define   CC_REG_REF_DELAY_EDGE__W                                   1
#define   CC_REG_REF_DELAY_EDGE__M                                   0x1
#define     CC_REG_REF_DELAY_EDGE_POS                                0x0
#define     CC_REG_REF_DELAY_EDGE_NEG                                0x1
#define   CC_REG_REF_DELAY_DELAY__B                                  1
#define   CC_REG_REF_DELAY_DELAY__W                                  2
#define   CC_REG_REF_DELAY_DELAY__M                                  0x6
#define     CC_REG_REF_DELAY_DELAY_DEL_0                             0x0
#define     CC_REG_REF_DELAY_DELAY_DEL_3                             0x2
#define     CC_REG_REF_DELAY_DELAY_DEL_6                             0x4
#define     CC_REG_REF_DELAY_DELAY_DEL_9                             0x6

#define CC_REG_CLK_DELAY__A                                          0x2410014
#define CC_REG_CLK_DELAY__W                                          4
#define CC_REG_CLK_DELAY__M                                          0xF
#define   CC_REG_CLK_DELAY_OFF                                       0x0

#define CC_REG_PWD_MODE__A                                           0x2410015
#define CC_REG_PWD_MODE__W                                           2
#define CC_REG_PWD_MODE__M                                           0x3
#define   CC_REG_PWD_MODE_UP                                         0x0
#define   CC_REG_PWD_MODE_DOWN_CLK                                   0x1
#define   CC_REG_PWD_MODE_DOWN_PLL                                   0x2
#define   CC_REG_PWD_MODE_DOWN_OSC                                   0x3

#define CC_REG_SOFT_RST__A                                           0x2410016
#define CC_REG_SOFT_RST__W                                           2
#define CC_REG_SOFT_RST__M                                           0x3
#define   CC_REG_SOFT_RST_SYS__B                                     0
#define   CC_REG_SOFT_RST_SYS__W                                     1
#define   CC_REG_SOFT_RST_SYS__M                                     0x1
#define   CC_REG_SOFT_RST_OSC__B                                     1
#define   CC_REG_SOFT_RST_OSC__W                                     1
#define   CC_REG_SOFT_RST_OSC__M                                     0x2

#define CC_REG_UPDATE__A                                             0x2410017
#define CC_REG_UPDATE__W                                             16
#define CC_REG_UPDATE__M                                             0xFFFF
#define   CC_REG_UPDATE_KEY                                          0x3973

#define CC_REG_PLL_LOCK__A                                           0x2410018
#define CC_REG_PLL_LOCK__W                                           1
#define CC_REG_PLL_LOCK__M                                           0x1
#define   CC_REG_PLL_LOCK_LOCK                                       0x1

#define CC_REG_JTAGID_L__A                                           0x2410019
#define CC_REG_JTAGID_L__W                                           16
#define CC_REG_JTAGID_L__M                                           0xFFFF
#define   CC_REG_JTAGID_L_INIT                                       0x0

#define CC_REG_JTAGID_H__A                                           0x241001A
#define CC_REG_JTAGID_H__W                                           16
#define CC_REG_JTAGID_H__M                                           0xFFFF
#define   CC_REG_JTAGID_H_INIT                                       0x0

#define LC_SID                                                       0x1C

#define LC_COMM_EXEC__A                                              0x2800000
#define LC_COMM_EXEC__W                                              3
#define LC_COMM_EXEC__M                                              0x7
#define   LC_COMM_EXEC_CTL__B                                        0
#define   LC_COMM_EXEC_CTL__W                                        3
#define   LC_COMM_EXEC_CTL__M                                        0x7
#define     LC_COMM_EXEC_CTL_STOP                                    0x0
#define     LC_COMM_EXEC_CTL_ACTIVE                                  0x1
#define     LC_COMM_EXEC_CTL_HOLD                                    0x2
#define     LC_COMM_EXEC_CTL_STEP                                    0x3
#define     LC_COMM_EXEC_CTL_BYPASS_STOP                             0x4
#define     LC_COMM_EXEC_CTL_BYPASS_HOLD                             0x6

#define LC_COMM_STATE__A                                             0x2800001
#define LC_COMM_STATE__W                                             16
#define LC_COMM_STATE__M                                             0xFFFF
#define LC_COMM_MB__A                                                0x2800002
#define LC_COMM_MB__W                                                16
#define LC_COMM_MB__M                                                0xFFFF
#define LC_COMM_SERVICE0__A                                          0x2800003
#define LC_COMM_SERVICE0__W                                          16
#define LC_COMM_SERVICE0__M                                          0xFFFF
#define LC_COMM_SERVICE1__A                                          0x2800004
#define LC_COMM_SERVICE1__W                                          16
#define LC_COMM_SERVICE1__M                                          0xFFFF
#define LC_COMM_INT_STA__A                                           0x2800007
#define LC_COMM_INT_STA__W                                           16
#define LC_COMM_INT_STA__M                                           0xFFFF
#define LC_COMM_INT_MSK__A                                           0x2800008
#define LC_COMM_INT_MSK__W                                           16
#define LC_COMM_INT_MSK__M                                           0xFFFF

#define LC_CT_REG_COMM_EXEC__A                                       0x2810000
#define LC_CT_REG_COMM_EXEC__W                                       3
#define LC_CT_REG_COMM_EXEC__M                                       0x7
#define   LC_CT_REG_COMM_EXEC_CTL__B                                 0
#define   LC_CT_REG_COMM_EXEC_CTL__W                                 3
#define   LC_CT_REG_COMM_EXEC_CTL__M                                 0x7
#define     LC_CT_REG_COMM_EXEC_CTL_STOP                             0x0
#define     LC_CT_REG_COMM_EXEC_CTL_ACTIVE                           0x1
#define     LC_CT_REG_COMM_EXEC_CTL_HOLD                             0x2
#define     LC_CT_REG_COMM_EXEC_CTL_STEP                             0x3

#define LC_CT_REG_COMM_STATE__A                                      0x2810001
#define LC_CT_REG_COMM_STATE__W                                      10
#define LC_CT_REG_COMM_STATE__M                                      0x3FF
#define LC_CT_REG_COMM_SERVICE0__A                                   0x2810003
#define LC_CT_REG_COMM_SERVICE0__W                                   16
#define LC_CT_REG_COMM_SERVICE0__M                                   0xFFFF
#define LC_CT_REG_COMM_SERVICE1__A                                   0x2810004
#define LC_CT_REG_COMM_SERVICE1__W                                   16
#define LC_CT_REG_COMM_SERVICE1__M                                   0xFFFF
#define   LC_CT_REG_COMM_SERVICE1_LC__B                              12
#define   LC_CT_REG_COMM_SERVICE1_LC__W                              1
#define   LC_CT_REG_COMM_SERVICE1_LC__M                              0x1000

#define LC_CT_REG_COMM_INT_STA__A                                    0x2810007
#define LC_CT_REG_COMM_INT_STA__W                                    1
#define LC_CT_REG_COMM_INT_STA__M                                    0x1
#define   LC_CT_REG_COMM_INT_STA_REQUEST__B                          0
#define   LC_CT_REG_COMM_INT_STA_REQUEST__W                          1
#define   LC_CT_REG_COMM_INT_STA_REQUEST__M                          0x1

#define LC_CT_REG_COMM_INT_MSK__A                                    0x2810008
#define LC_CT_REG_COMM_INT_MSK__W                                    1
#define LC_CT_REG_COMM_INT_MSK__M                                    0x1
#define   LC_CT_REG_COMM_INT_MSK_REQUEST__B                          0
#define   LC_CT_REG_COMM_INT_MSK_REQUEST__W                          1
#define   LC_CT_REG_COMM_INT_MSK_REQUEST__M                          0x1

#define LC_CT_REG_CTL_STK__AX                                        0x2810010
#define LC_CT_REG_CTL_STK__XSZ                                       4
#define LC_CT_REG_CTL_STK__W                                         10
#define LC_CT_REG_CTL_STK__M                                         0x3FF

#define LC_CT_REG_CTL_BPT_IDX__A                                     0x281001F
#define LC_CT_REG_CTL_BPT_IDX__W                                     1
#define LC_CT_REG_CTL_BPT_IDX__M                                     0x1

#define LC_CT_REG_CTL_BPT__A                                         0x2810020
#define LC_CT_REG_CTL_BPT__W                                         10
#define LC_CT_REG_CTL_BPT__M                                         0x3FF

#define LC_RA_RAM_PROC_DELAY_IF__A                                   0x2820006
#define LC_RA_RAM_PROC_DELAY_IF__W                                   16
#define LC_RA_RAM_PROC_DELAY_IF__M                                   0xFFFF
#define LC_RA_RAM_PROC_DELAY_IF__PRE                                 0xFFE6
#define LC_RA_RAM_PROC_DELAY_FS__A                                   0x2820007
#define LC_RA_RAM_PROC_DELAY_FS__W                                   16
#define LC_RA_RAM_PROC_DELAY_FS__M                                   0xFFFF
#define LC_RA_RAM_PROC_DELAY_FS__PRE                                 0xFFE3
#define LC_RA_RAM_LOCK_TH_CRMM__A                                    0x2820008
#define LC_RA_RAM_LOCK_TH_CRMM__W                                    16
#define LC_RA_RAM_LOCK_TH_CRMM__M                                    0xFFFF
#define LC_RA_RAM_LOCK_TH_CRMM__PRE                                  0xC8
#define LC_RA_RAM_LOCK_TH_SRMM__A                                    0x2820009
#define LC_RA_RAM_LOCK_TH_SRMM__W                                    16
#define LC_RA_RAM_LOCK_TH_SRMM__M                                    0xFFFF
#define LC_RA_RAM_LOCK_TH_SRMM__PRE                                  0x46
#define LC_RA_RAM_LOCK_COUNT__A                                      0x282000A
#define LC_RA_RAM_LOCK_COUNT__W                                      16
#define LC_RA_RAM_LOCK_COUNT__M                                      0xFFFF
#define LC_RA_RAM_CPRTOFS_NOM__A                                     0x282000B
#define LC_RA_RAM_CPRTOFS_NOM__W                                     16
#define LC_RA_RAM_CPRTOFS_NOM__M                                     0xFFFF
#define LC_RA_RAM_IFINCR_NOM_L__A                                    0x282000C
#define LC_RA_RAM_IFINCR_NOM_L__W                                    16
#define LC_RA_RAM_IFINCR_NOM_L__M                                    0xFFFF
#define LC_RA_RAM_IFINCR_NOM_H__A                                    0x282000D
#define LC_RA_RAM_IFINCR_NOM_H__W                                    16
#define LC_RA_RAM_IFINCR_NOM_H__M                                    0xFFFF
#define LC_RA_RAM_FSINCR_NOM_L__A                                    0x282000E
#define LC_RA_RAM_FSINCR_NOM_L__W                                    16
#define LC_RA_RAM_FSINCR_NOM_L__M                                    0xFFFF
#define LC_RA_RAM_FSINCR_NOM_H__A                                    0x282000F
#define LC_RA_RAM_FSINCR_NOM_H__W                                    16
#define LC_RA_RAM_FSINCR_NOM_H__M                                    0xFFFF
#define LC_RA_RAM_MODE_2K__A                                         0x2820010
#define LC_RA_RAM_MODE_2K__W                                         16
#define LC_RA_RAM_MODE_2K__M                                         0xFFFF
#define LC_RA_RAM_MODE_GUARD__A                                      0x2820011
#define LC_RA_RAM_MODE_GUARD__W                                      16
#define LC_RA_RAM_MODE_GUARD__M                                      0xFFFF
#define   LC_RA_RAM_MODE_GUARD_32                                    0x0
#define   LC_RA_RAM_MODE_GUARD_16                                    0x1
#define   LC_RA_RAM_MODE_GUARD_8                                     0x2
#define   LC_RA_RAM_MODE_GUARD_4                                     0x3

#define LC_RA_RAM_MODE_ADJUST__A                                     0x2820012
#define LC_RA_RAM_MODE_ADJUST__W                                     16
#define LC_RA_RAM_MODE_ADJUST__M                                     0xFFFF
#define   LC_RA_RAM_MODE_ADJUST_CP_CRMM__B                           0
#define   LC_RA_RAM_MODE_ADJUST_CP_CRMM__W                           1
#define   LC_RA_RAM_MODE_ADJUST_CP_CRMM__M                           0x1
#define   LC_RA_RAM_MODE_ADJUST_CE_CRMM__B                           1
#define   LC_RA_RAM_MODE_ADJUST_CE_CRMM__W                           1
#define   LC_RA_RAM_MODE_ADJUST_CE_CRMM__M                           0x2
#define   LC_RA_RAM_MODE_ADJUST_SRMM__B                              2
#define   LC_RA_RAM_MODE_ADJUST_SRMM__W                              1
#define   LC_RA_RAM_MODE_ADJUST_SRMM__M                              0x4
#define   LC_RA_RAM_MODE_ADJUST_PHASE__B                             3
#define   LC_RA_RAM_MODE_ADJUST_PHASE__W                             1
#define   LC_RA_RAM_MODE_ADJUST_PHASE__M                             0x8
#define   LC_RA_RAM_MODE_ADJUST_DELAY__B                             4
#define   LC_RA_RAM_MODE_ADJUST_DELAY__W                             1
#define   LC_RA_RAM_MODE_ADJUST_DELAY__M                             0x10
#define   LC_RA_RAM_MODE_ADJUST_OPENLOOP__B                          5
#define   LC_RA_RAM_MODE_ADJUST_OPENLOOP__W                          1
#define   LC_RA_RAM_MODE_ADJUST_OPENLOOP__M                          0x20
#define   LC_RA_RAM_MODE_ADJUST_NO_CP__B                             6
#define   LC_RA_RAM_MODE_ADJUST_NO_CP__W                             1
#define   LC_RA_RAM_MODE_ADJUST_NO_CP__M                             0x40
#define   LC_RA_RAM_MODE_ADJUST_NO_FS__B                             7
#define   LC_RA_RAM_MODE_ADJUST_NO_FS__W                             1
#define   LC_RA_RAM_MODE_ADJUST_NO_FS__M                             0x80
#define   LC_RA_RAM_MODE_ADJUST_NO_IF__B                             8
#define   LC_RA_RAM_MODE_ADJUST_NO_IF__W                             1
#define   LC_RA_RAM_MODE_ADJUST_NO_IF__M                             0x100
#define   LC_RA_RAM_MODE_ADJUST_NO_PH_PIPE__B                        9
#define   LC_RA_RAM_MODE_ADJUST_NO_PH_PIPE__W                        1
#define   LC_RA_RAM_MODE_ADJUST_NO_PH_PIPE__M                        0x200

#define LC_RA_RAM_FILTER_SYM_SET__A                                  0x282001A
#define LC_RA_RAM_FILTER_SYM_SET__W                                  16
#define LC_RA_RAM_FILTER_SYM_SET__M                                  0xFFFF
#define LC_RA_RAM_FILTER_SYM_SET__PRE                                0x3E8
#define LC_RA_RAM_FILTER_SYM_CUR__A                                  0x282001B
#define LC_RA_RAM_FILTER_SYM_CUR__W                                  16
#define LC_RA_RAM_FILTER_SYM_CUR__M                                  0xFFFF
#define LC_RA_RAM_FILTER_SYM_CUR__PRE                                0x0
#define LC_RA_RAM_MAX_ABS_EXP__A                                     0x282001D
#define LC_RA_RAM_MAX_ABS_EXP__W                                     16
#define LC_RA_RAM_MAX_ABS_EXP__M                                     0xFFFF
#define LC_RA_RAM_MAX_ABS_EXP__PRE                                   0x10
#define LC_RA_RAM_ACTUAL_CP_CRMM__A                                  0x282001F
#define LC_RA_RAM_ACTUAL_CP_CRMM__W                                  16
#define LC_RA_RAM_ACTUAL_CP_CRMM__M                                  0xFFFF
#define LC_RA_RAM_ACTUAL_CE_CRMM__A                                  0x2820020
#define LC_RA_RAM_ACTUAL_CE_CRMM__W                                  16
#define LC_RA_RAM_ACTUAL_CE_CRMM__M                                  0xFFFF
#define LC_RA_RAM_ACTUAL_CE_SRMM__A                                  0x2820021
#define LC_RA_RAM_ACTUAL_CE_SRMM__W                                  16
#define LC_RA_RAM_ACTUAL_CE_SRMM__M                                  0xFFFF
#define LC_RA_RAM_ACTUAL_PHASE__A                                    0x2820022
#define LC_RA_RAM_ACTUAL_PHASE__W                                    16
#define LC_RA_RAM_ACTUAL_PHASE__M                                    0xFFFF
#define LC_RA_RAM_ACTUAL_DELAY__A                                    0x2820023
#define LC_RA_RAM_ACTUAL_DELAY__W                                    16
#define LC_RA_RAM_ACTUAL_DELAY__M                                    0xFFFF
#define LC_RA_RAM_ADJUST_CRMM__A                                     0x2820024
#define LC_RA_RAM_ADJUST_CRMM__W                                     16
#define LC_RA_RAM_ADJUST_CRMM__M                                     0xFFFF
#define LC_RA_RAM_ADJUST_SRMM__A                                     0x2820025
#define LC_RA_RAM_ADJUST_SRMM__W                                     16
#define LC_RA_RAM_ADJUST_SRMM__M                                     0xFFFF
#define LC_RA_RAM_ADJUST_PHASE__A                                    0x2820026
#define LC_RA_RAM_ADJUST_PHASE__W                                    16
#define LC_RA_RAM_ADJUST_PHASE__M                                    0xFFFF
#define LC_RA_RAM_ADJUST_DELAY__A                                    0x2820027
#define LC_RA_RAM_ADJUST_DELAY__W                                    16
#define LC_RA_RAM_ADJUST_DELAY__M                                    0xFFFF

#define LC_RA_RAM_PIPE_CP_PHASE_0__A                                 0x2820028
#define LC_RA_RAM_PIPE_CP_PHASE_0__W                                 16
#define LC_RA_RAM_PIPE_CP_PHASE_0__M                                 0xFFFF
#define LC_RA_RAM_PIPE_CP_PHASE_1__A                                 0x2820029
#define LC_RA_RAM_PIPE_CP_PHASE_1__W                                 16
#define LC_RA_RAM_PIPE_CP_PHASE_1__M                                 0xFFFF
#define LC_RA_RAM_PIPE_CP_PHASE_CON__A                               0x282002A
#define LC_RA_RAM_PIPE_CP_PHASE_CON__W                               16
#define LC_RA_RAM_PIPE_CP_PHASE_CON__M                               0xFFFF
#define LC_RA_RAM_PIPE_CP_PHASE_DIF__A                               0x282002B
#define LC_RA_RAM_PIPE_CP_PHASE_DIF__W                               16
#define LC_RA_RAM_PIPE_CP_PHASE_DIF__M                               0xFFFF
#define LC_RA_RAM_PIPE_CP_PHASE_RES__A                               0x282002C
#define LC_RA_RAM_PIPE_CP_PHASE_RES__W                               16
#define LC_RA_RAM_PIPE_CP_PHASE_RES__M                               0xFFFF
#define LC_RA_RAM_PIPE_CP_PHASE_RZ__A                                0x282002D
#define LC_RA_RAM_PIPE_CP_PHASE_RZ__W                                16
#define LC_RA_RAM_PIPE_CP_PHASE_RZ__M                                0xFFFF

#define LC_RA_RAM_PIPE_CP_CRMM_0__A                                  0x2820030
#define LC_RA_RAM_PIPE_CP_CRMM_0__W                                  16
#define LC_RA_RAM_PIPE_CP_CRMM_0__M                                  0xFFFF
#define LC_RA_RAM_PIPE_CP_CRMM_1__A                                  0x2820031
#define LC_RA_RAM_PIPE_CP_CRMM_1__W                                  16
#define LC_RA_RAM_PIPE_CP_CRMM_1__M                                  0xFFFF
#define LC_RA_RAM_PIPE_CP_CRMM_CON__A                                0x2820032
#define LC_RA_RAM_PIPE_CP_CRMM_CON__W                                16
#define LC_RA_RAM_PIPE_CP_CRMM_CON__M                                0xFFFF
#define LC_RA_RAM_PIPE_CP_CRMM_DIF__A                                0x2820033
#define LC_RA_RAM_PIPE_CP_CRMM_DIF__W                                16
#define LC_RA_RAM_PIPE_CP_CRMM_DIF__M                                0xFFFF
#define LC_RA_RAM_PIPE_CP_CRMM_RES__A                                0x2820034
#define LC_RA_RAM_PIPE_CP_CRMM_RES__W                                16
#define LC_RA_RAM_PIPE_CP_CRMM_RES__M                                0xFFFF
#define LC_RA_RAM_PIPE_CP_CRMM_RZ__A                                 0x2820035
#define LC_RA_RAM_PIPE_CP_CRMM_RZ__W                                 16
#define LC_RA_RAM_PIPE_CP_CRMM_RZ__M                                 0xFFFF

#define LC_RA_RAM_PIPE_CP_SRMM_0__A                                  0x2820038
#define LC_RA_RAM_PIPE_CP_SRMM_0__W                                  16
#define LC_RA_RAM_PIPE_CP_SRMM_0__M                                  0xFFFF
#define LC_RA_RAM_PIPE_CP_SRMM_1__A                                  0x2820039
#define LC_RA_RAM_PIPE_CP_SRMM_1__W                                  16
#define LC_RA_RAM_PIPE_CP_SRMM_1__M                                  0xFFFF
#define LC_RA_RAM_PIPE_CP_SRMM_CON__A                                0x282003A
#define LC_RA_RAM_PIPE_CP_SRMM_CON__W                                16
#define LC_RA_RAM_PIPE_CP_SRMM_CON__M                                0xFFFF
#define LC_RA_RAM_PIPE_CP_SRMM_DIF__A                                0x282003B
#define LC_RA_RAM_PIPE_CP_SRMM_DIF__W                                16
#define LC_RA_RAM_PIPE_CP_SRMM_DIF__M                                0xFFFF
#define LC_RA_RAM_PIPE_CP_SRMM_RES__A                                0x282003C
#define LC_RA_RAM_PIPE_CP_SRMM_RES__W                                16
#define LC_RA_RAM_PIPE_CP_SRMM_RES__M                                0xFFFF
#define LC_RA_RAM_PIPE_CP_SRMM_RZ__A                                 0x282003D
#define LC_RA_RAM_PIPE_CP_SRMM_RZ__W                                 16
#define LC_RA_RAM_PIPE_CP_SRMM_RZ__M                                 0xFFFF

#define LC_RA_RAM_FILTER_CRMM_A__A                                   0x2820060
#define LC_RA_RAM_FILTER_CRMM_A__W                                   16
#define LC_RA_RAM_FILTER_CRMM_A__M                                   0xFFFF
#define LC_RA_RAM_FILTER_CRMM_A__PRE                                 0x4
#define LC_RA_RAM_FILTER_CRMM_B__A                                   0x2820061
#define LC_RA_RAM_FILTER_CRMM_B__W                                   16
#define LC_RA_RAM_FILTER_CRMM_B__M                                   0xFFFF
#define LC_RA_RAM_FILTER_CRMM_B__PRE                                 0x1
#define LC_RA_RAM_FILTER_CRMM_Z1__AX                                 0x2820062
#define LC_RA_RAM_FILTER_CRMM_Z1__XSZ                                2
#define LC_RA_RAM_FILTER_CRMM_Z1__W                                  16
#define LC_RA_RAM_FILTER_CRMM_Z1__M                                  0xFFFF
#define LC_RA_RAM_FILTER_CRMM_Z2__AX                                 0x2820064
#define LC_RA_RAM_FILTER_CRMM_Z2__XSZ                                2
#define LC_RA_RAM_FILTER_CRMM_Z2__W                                  16
#define LC_RA_RAM_FILTER_CRMM_Z2__M                                  0xFFFF
#define LC_RA_RAM_FILTER_CRMM_TMP__AX                                0x2820066
#define LC_RA_RAM_FILTER_CRMM_TMP__XSZ                               2
#define LC_RA_RAM_FILTER_CRMM_TMP__W                                 16
#define LC_RA_RAM_FILTER_CRMM_TMP__M                                 0xFFFF

#define LC_RA_RAM_FILTER_SRMM_A__A                                   0x2820068
#define LC_RA_RAM_FILTER_SRMM_A__W                                   16
#define LC_RA_RAM_FILTER_SRMM_A__M                                   0xFFFF
#define LC_RA_RAM_FILTER_SRMM_A__PRE                                 0x4
#define LC_RA_RAM_FILTER_SRMM_B__A                                   0x2820069
#define LC_RA_RAM_FILTER_SRMM_B__W                                   16
#define LC_RA_RAM_FILTER_SRMM_B__M                                   0xFFFF
#define LC_RA_RAM_FILTER_SRMM_B__PRE                                 0x1
#define LC_RA_RAM_FILTER_SRMM_Z1__AX                                 0x282006A
#define LC_RA_RAM_FILTER_SRMM_Z1__XSZ                                2
#define LC_RA_RAM_FILTER_SRMM_Z1__W                                  16
#define LC_RA_RAM_FILTER_SRMM_Z1__M                                  0xFFFF
#define LC_RA_RAM_FILTER_SRMM_Z2__AX                                 0x282006C
#define LC_RA_RAM_FILTER_SRMM_Z2__XSZ                                2
#define LC_RA_RAM_FILTER_SRMM_Z2__W                                  16
#define LC_RA_RAM_FILTER_SRMM_Z2__M                                  0xFFFF
#define LC_RA_RAM_FILTER_SRMM_TMP__AX                                0x282006E
#define LC_RA_RAM_FILTER_SRMM_TMP__XSZ                               2
#define LC_RA_RAM_FILTER_SRMM_TMP__W                                 16
#define LC_RA_RAM_FILTER_SRMM_TMP__M                                 0xFFFF

#define LC_RA_RAM_FILTER_PHASE_A__A                                  0x2820070
#define LC_RA_RAM_FILTER_PHASE_A__W                                  16
#define LC_RA_RAM_FILTER_PHASE_A__M                                  0xFFFF
#define LC_RA_RAM_FILTER_PHASE_A__PRE                                0x4
#define LC_RA_RAM_FILTER_PHASE_B__A                                  0x2820071
#define LC_RA_RAM_FILTER_PHASE_B__W                                  16
#define LC_RA_RAM_FILTER_PHASE_B__M                                  0xFFFF
#define LC_RA_RAM_FILTER_PHASE_B__PRE                                0x1
#define LC_RA_RAM_FILTER_PHASE_Z1__AX                                0x2820072
#define LC_RA_RAM_FILTER_PHASE_Z1__XSZ                               2
#define LC_RA_RAM_FILTER_PHASE_Z1__W                                 16
#define LC_RA_RAM_FILTER_PHASE_Z1__M                                 0xFFFF
#define LC_RA_RAM_FILTER_PHASE_Z2__AX                                0x2820074
#define LC_RA_RAM_FILTER_PHASE_Z2__XSZ                               2
#define LC_RA_RAM_FILTER_PHASE_Z2__W                                 16
#define LC_RA_RAM_FILTER_PHASE_Z2__M                                 0xFFFF
#define LC_RA_RAM_FILTER_PHASE_TMP__AX                               0x2820076
#define LC_RA_RAM_FILTER_PHASE_TMP__XSZ                              2
#define LC_RA_RAM_FILTER_PHASE_TMP__W                                16
#define LC_RA_RAM_FILTER_PHASE_TMP__M                                0xFFFF

#define LC_RA_RAM_FILTER_DELAY_A__A                                  0x2820078
#define LC_RA_RAM_FILTER_DELAY_A__W                                  16
#define LC_RA_RAM_FILTER_DELAY_A__M                                  0xFFFF
#define LC_RA_RAM_FILTER_DELAY_A__PRE                                0x4
#define LC_RA_RAM_FILTER_DELAY_B__A                                  0x2820079
#define LC_RA_RAM_FILTER_DELAY_B__W                                  16
#define LC_RA_RAM_FILTER_DELAY_B__M                                  0xFFFF
#define LC_RA_RAM_FILTER_DELAY_B__PRE                                0x1
#define LC_RA_RAM_FILTER_DELAY_Z1__AX                                0x282007A
#define LC_RA_RAM_FILTER_DELAY_Z1__XSZ                               2
#define LC_RA_RAM_FILTER_DELAY_Z1__W                                 16
#define LC_RA_RAM_FILTER_DELAY_Z1__M                                 0xFFFF
#define LC_RA_RAM_FILTER_DELAY_Z2__AX                                0x282007C
#define LC_RA_RAM_FILTER_DELAY_Z2__XSZ                               2
#define LC_RA_RAM_FILTER_DELAY_Z2__W                                 16
#define LC_RA_RAM_FILTER_DELAY_Z2__M                                 0xFFFF
#define LC_RA_RAM_FILTER_DELAY_TMP__AX                               0x282007E
#define LC_RA_RAM_FILTER_DELAY_TMP__XSZ                              2
#define LC_RA_RAM_FILTER_DELAY_TMP__W                                16
#define LC_RA_RAM_FILTER_DELAY_TMP__M                                0xFFFF

#define LC_IF_RAM_TRP_BPT0__AX                                       0x2830000
#define LC_IF_RAM_TRP_BPT0__XSZ                                      2
#define LC_IF_RAM_TRP_BPT0__W                                        12
#define LC_IF_RAM_TRP_BPT0__M                                        0xFFF

#define LC_IF_RAM_TRP_STKU__AX                                       0x2830002
#define LC_IF_RAM_TRP_STKU__XSZ                                      2
#define LC_IF_RAM_TRP_STKU__W                                        12
#define LC_IF_RAM_TRP_STKU__M                                        0xFFF

#define LC_IF_RAM_TRP_WARM__AX                                       0x2830006
#define LC_IF_RAM_TRP_WARM__XSZ                                      2
#define LC_IF_RAM_TRP_WARM__W                                        12
#define LC_IF_RAM_TRP_WARM__M                                        0xFFF

#define B_HI_SID                                                     0x10

#define B_HI_COMM_EXEC__A                                            0x400000
#define B_HI_COMM_EXEC__W                                            3
#define B_HI_COMM_EXEC__M                                            0x7
#define   B_HI_COMM_EXEC_CTL__B                                      0
#define   B_HI_COMM_EXEC_CTL__W                                      3
#define   B_HI_COMM_EXEC_CTL__M                                      0x7
#define     B_HI_COMM_EXEC_CTL_STOP                                  0x0
#define     B_HI_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_HI_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_HI_COMM_EXEC_CTL_STEP                                  0x3
#define     B_HI_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_HI_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_HI_COMM_STATE__A                                           0x400001
#define B_HI_COMM_STATE__W                                           16
#define B_HI_COMM_STATE__M                                           0xFFFF
#define B_HI_COMM_MB__A                                              0x400002
#define B_HI_COMM_MB__W                                              16
#define B_HI_COMM_MB__M                                              0xFFFF
#define B_HI_COMM_SERVICE0__A                                        0x400003
#define B_HI_COMM_SERVICE0__W                                        16
#define B_HI_COMM_SERVICE0__M                                        0xFFFF
#define B_HI_COMM_SERVICE1__A                                        0x400004
#define B_HI_COMM_SERVICE1__W                                        16
#define B_HI_COMM_SERVICE1__M                                        0xFFFF
#define B_HI_COMM_INT_STA__A                                         0x400007
#define B_HI_COMM_INT_STA__W                                         16
#define B_HI_COMM_INT_STA__M                                         0xFFFF
#define B_HI_COMM_INT_MSK__A                                         0x400008
#define B_HI_COMM_INT_MSK__W                                         16
#define B_HI_COMM_INT_MSK__M                                         0xFFFF

#define B_HI_CT_REG_COMM_EXEC__A                                     0x410000
#define B_HI_CT_REG_COMM_EXEC__W                                     3
#define B_HI_CT_REG_COMM_EXEC__M                                     0x7
#define   B_HI_CT_REG_COMM_EXEC_CTL__B                               0
#define   B_HI_CT_REG_COMM_EXEC_CTL__W                               3
#define   B_HI_CT_REG_COMM_EXEC_CTL__M                               0x7
#define     B_HI_CT_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_HI_CT_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_HI_CT_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_HI_CT_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_HI_CT_REG_COMM_STATE__A                                    0x410001
#define B_HI_CT_REG_COMM_STATE__W                                    10
#define B_HI_CT_REG_COMM_STATE__M                                    0x3FF
#define B_HI_CT_REG_COMM_SERVICE0__A                                 0x410003
#define B_HI_CT_REG_COMM_SERVICE0__W                                 16
#define B_HI_CT_REG_COMM_SERVICE0__M                                 0xFFFF
#define B_HI_CT_REG_COMM_SERVICE1__A                                 0x410004
#define B_HI_CT_REG_COMM_SERVICE1__W                                 16
#define B_HI_CT_REG_COMM_SERVICE1__M                                 0xFFFF
#define   B_HI_CT_REG_COMM_SERVICE1_HI__B                            0
#define   B_HI_CT_REG_COMM_SERVICE1_HI__W                            1
#define   B_HI_CT_REG_COMM_SERVICE1_HI__M                            0x1

#define B_HI_CT_REG_COMM_INT_STA__A                                  0x410007
#define B_HI_CT_REG_COMM_INT_STA__W                                  1
#define B_HI_CT_REG_COMM_INT_STA__M                                  0x1
#define   B_HI_CT_REG_COMM_INT_STA_REQUEST__B                        0
#define   B_HI_CT_REG_COMM_INT_STA_REQUEST__W                        1
#define   B_HI_CT_REG_COMM_INT_STA_REQUEST__M                        0x1

#define B_HI_CT_REG_COMM_INT_MSK__A                                  0x410008
#define B_HI_CT_REG_COMM_INT_MSK__W                                  1
#define B_HI_CT_REG_COMM_INT_MSK__M                                  0x1
#define   B_HI_CT_REG_COMM_INT_MSK_REQUEST__B                        0
#define   B_HI_CT_REG_COMM_INT_MSK_REQUEST__W                        1
#define   B_HI_CT_REG_COMM_INT_MSK_REQUEST__M                        0x1

#define B_HI_CT_REG_CTL_STK__AX                                      0x410010
#define B_HI_CT_REG_CTL_STK__XSZ                                     4
#define B_HI_CT_REG_CTL_STK__W                                       10
#define B_HI_CT_REG_CTL_STK__M                                       0x3FF

#define B_HI_CT_REG_CTL_BPT_IDX__A                                   0x41001F
#define B_HI_CT_REG_CTL_BPT_IDX__W                                   1
#define B_HI_CT_REG_CTL_BPT_IDX__M                                   0x1

#define B_HI_CT_REG_CTL_BPT__A                                       0x410020
#define B_HI_CT_REG_CTL_BPT__W                                       10
#define B_HI_CT_REG_CTL_BPT__M                                       0x3FF

#define B_HI_RA_RAM_SLV0_FLG_SMM__A                                  0x420010
#define B_HI_RA_RAM_SLV0_FLG_SMM__W                                  1
#define B_HI_RA_RAM_SLV0_FLG_SMM__M                                  0x1
#define   B_HI_RA_RAM_SLV0_FLG_SMM_MULTI                             0x0
#define   B_HI_RA_RAM_SLV0_FLG_SMM_SINGLE                            0x1

#define B_HI_RA_RAM_SLV0_DEV_ID__A                                   0x420011
#define B_HI_RA_RAM_SLV0_DEV_ID__W                                   7
#define B_HI_RA_RAM_SLV0_DEV_ID__M                                   0x7F

#define B_HI_RA_RAM_SLV0_FLG_CRC__A                                  0x420012
#define B_HI_RA_RAM_SLV0_FLG_CRC__W                                  1
#define B_HI_RA_RAM_SLV0_FLG_CRC__M                                  0x1
#define   B_HI_RA_RAM_SLV0_FLG_CRC_CONTINUE                          0x0
#define   B_HI_RA_RAM_SLV0_FLG_CRC_RESTART                           0x1

#define B_HI_RA_RAM_SLV0_FLG_ACC__A                                  0x420013
#define B_HI_RA_RAM_SLV0_FLG_ACC__W                                  3
#define B_HI_RA_RAM_SLV0_FLG_ACC__M                                  0x7
#define   B_HI_RA_RAM_SLV0_FLG_ACC_RWM__B                            0
#define   B_HI_RA_RAM_SLV0_FLG_ACC_RWM__W                            2
#define   B_HI_RA_RAM_SLV0_FLG_ACC_RWM__M                            0x3
#define     B_HI_RA_RAM_SLV0_FLG_ACC_RWM_NORMAL                      0x0
#define     B_HI_RA_RAM_SLV0_FLG_ACC_RWM_READ_WRITE                  0x3
#define   B_HI_RA_RAM_SLV0_FLG_ACC_BRC__B                            2
#define   B_HI_RA_RAM_SLV0_FLG_ACC_BRC__W                            1
#define   B_HI_RA_RAM_SLV0_FLG_ACC_BRC__M                            0x4
#define     B_HI_RA_RAM_SLV0_FLG_ACC_BRC_NORMAL                      0x0
#define     B_HI_RA_RAM_SLV0_FLG_ACC_BRC_BROADCAST                   0x4

#define B_HI_RA_RAM_SLV0_STATE__A                                    0x420014
#define B_HI_RA_RAM_SLV0_STATE__W                                    1
#define B_HI_RA_RAM_SLV0_STATE__M                                    0x1
#define   B_HI_RA_RAM_SLV0_STATE_ADDRESS                             0x0
#define   B_HI_RA_RAM_SLV0_STATE_DATA                                0x1

#define B_HI_RA_RAM_SLV0_BLK_BNK__A                                  0x420015
#define B_HI_RA_RAM_SLV0_BLK_BNK__W                                  12
#define B_HI_RA_RAM_SLV0_BLK_BNK__M                                  0xFFF
#define   B_HI_RA_RAM_SLV0_BLK_BNK_BNK__B                            0
#define   B_HI_RA_RAM_SLV0_BLK_BNK_BNK__W                            6
#define   B_HI_RA_RAM_SLV0_BLK_BNK_BNK__M                            0x3F
#define   B_HI_RA_RAM_SLV0_BLK_BNK_BLK__B                            6
#define   B_HI_RA_RAM_SLV0_BLK_BNK_BLK__W                            6
#define   B_HI_RA_RAM_SLV0_BLK_BNK_BLK__M                            0xFC0

#define B_HI_RA_RAM_SLV0_ADDR__A                                     0x420016
#define B_HI_RA_RAM_SLV0_ADDR__W                                     16
#define B_HI_RA_RAM_SLV0_ADDR__M                                     0xFFFF

#define B_HI_RA_RAM_SLV0_CRC__A                                      0x420017
#define B_HI_RA_RAM_SLV0_CRC__W                                      16
#define B_HI_RA_RAM_SLV0_CRC__M                                      0xFFFF

#define B_HI_RA_RAM_SLV0_READBACK__A                                 0x420018
#define B_HI_RA_RAM_SLV0_READBACK__W                                 16
#define B_HI_RA_RAM_SLV0_READBACK__M                                 0xFFFF

#define B_HI_RA_RAM_SLV1_FLG_SMM__A                                  0x420020
#define B_HI_RA_RAM_SLV1_FLG_SMM__W                                  1
#define B_HI_RA_RAM_SLV1_FLG_SMM__M                                  0x1
#define   B_HI_RA_RAM_SLV1_FLG_SMM_MULTI                             0x0
#define   B_HI_RA_RAM_SLV1_FLG_SMM_SINGLE                            0x1

#define B_HI_RA_RAM_SLV1_DEV_ID__A                                   0x420021
#define B_HI_RA_RAM_SLV1_DEV_ID__W                                   7
#define B_HI_RA_RAM_SLV1_DEV_ID__M                                   0x7F

#define B_HI_RA_RAM_SLV1_FLG_CRC__A                                  0x420022
#define B_HI_RA_RAM_SLV1_FLG_CRC__W                                  1
#define B_HI_RA_RAM_SLV1_FLG_CRC__M                                  0x1
#define   B_HI_RA_RAM_SLV1_FLG_CRC_CONTINUE                          0x0
#define   B_HI_RA_RAM_SLV1_FLG_CRC_RESTART                           0x1

#define B_HI_RA_RAM_SLV1_FLG_ACC__A                                  0x420023
#define B_HI_RA_RAM_SLV1_FLG_ACC__W                                  3
#define B_HI_RA_RAM_SLV1_FLG_ACC__M                                  0x7
#define   B_HI_RA_RAM_SLV1_FLG_ACC_RWM__B                            0
#define   B_HI_RA_RAM_SLV1_FLG_ACC_RWM__W                            2
#define   B_HI_RA_RAM_SLV1_FLG_ACC_RWM__M                            0x3
#define     B_HI_RA_RAM_SLV1_FLG_ACC_RWM_NORMAL                      0x0
#define     B_HI_RA_RAM_SLV1_FLG_ACC_RWM_READ_WRITE                  0x3
#define   B_HI_RA_RAM_SLV1_FLG_ACC_BRC__B                            2
#define   B_HI_RA_RAM_SLV1_FLG_ACC_BRC__W                            1
#define   B_HI_RA_RAM_SLV1_FLG_ACC_BRC__M                            0x4
#define     B_HI_RA_RAM_SLV1_FLG_ACC_BRC_NORMAL                      0x0
#define     B_HI_RA_RAM_SLV1_FLG_ACC_BRC_BROADCAST                   0x4

#define B_HI_RA_RAM_SLV1_STATE__A                                    0x420024
#define B_HI_RA_RAM_SLV1_STATE__W                                    1
#define B_HI_RA_RAM_SLV1_STATE__M                                    0x1
#define   B_HI_RA_RAM_SLV1_STATE_ADDRESS                             0x0
#define   B_HI_RA_RAM_SLV1_STATE_DATA                                0x1

#define B_HI_RA_RAM_SLV1_BLK_BNK__A                                  0x420025
#define B_HI_RA_RAM_SLV1_BLK_BNK__W                                  12
#define B_HI_RA_RAM_SLV1_BLK_BNK__M                                  0xFFF
#define   B_HI_RA_RAM_SLV1_BLK_BNK_BNK__B                            0
#define   B_HI_RA_RAM_SLV1_BLK_BNK_BNK__W                            6
#define   B_HI_RA_RAM_SLV1_BLK_BNK_BNK__M                            0x3F
#define   B_HI_RA_RAM_SLV1_BLK_BNK_BLK__B                            6
#define   B_HI_RA_RAM_SLV1_BLK_BNK_BLK__W                            6
#define   B_HI_RA_RAM_SLV1_BLK_BNK_BLK__M                            0xFC0

#define B_HI_RA_RAM_SLV1_ADDR__A                                     0x420026
#define B_HI_RA_RAM_SLV1_ADDR__W                                     16
#define B_HI_RA_RAM_SLV1_ADDR__M                                     0xFFFF

#define B_HI_RA_RAM_SLV1_CRC__A                                      0x420027
#define B_HI_RA_RAM_SLV1_CRC__W                                      16
#define B_HI_RA_RAM_SLV1_CRC__M                                      0xFFFF

#define B_HI_RA_RAM_SLV1_READBACK__A                                 0x420028
#define B_HI_RA_RAM_SLV1_READBACK__W                                 16
#define B_HI_RA_RAM_SLV1_READBACK__M                                 0xFFFF

#define B_HI_RA_RAM_SRV_SEM__A                                       0x420030
#define B_HI_RA_RAM_SRV_SEM__W                                       1
#define B_HI_RA_RAM_SRV_SEM__M                                       0x1
#define   B_HI_RA_RAM_SRV_SEM_FREE                                   0x0
#define   B_HI_RA_RAM_SRV_SEM_CLAIMED                                0x1

#define B_HI_RA_RAM_SRV_RES__A                                       0x420031
#define B_HI_RA_RAM_SRV_RES__W                                       3
#define B_HI_RA_RAM_SRV_RES__M                                       0x7
#define   B_HI_RA_RAM_SRV_RES_OK                                     0x0
#define   B_HI_RA_RAM_SRV_RES_START_FOUND_OR_ERROR                   0x1
#define   B_HI_RA_RAM_SRV_RES_STOP_FOUND                             0x2
#define   B_HI_RA_RAM_SRV_RES_ARBITRATION_FAILED                     0x3
#define   B_HI_RA_RAM_SRV_RES_INTERNAL_ERROR                         0x4

#define B_HI_RA_RAM_SRV_CMD__A                                       0x420032
#define B_HI_RA_RAM_SRV_CMD__W                                       3
#define B_HI_RA_RAM_SRV_CMD__M                                       0x7
#define   B_HI_RA_RAM_SRV_CMD_NULL                                   0x0
#define   B_HI_RA_RAM_SRV_CMD_UIO                                    0x1
#define   B_HI_RA_RAM_SRV_CMD_RESET                                  0x2
#define   B_HI_RA_RAM_SRV_CMD_CONFIG                                 0x3
#define   B_HI_RA_RAM_SRV_CMD_COPY                                   0x4
#define   B_HI_RA_RAM_SRV_CMD_TRANSMIT                               0x5
#define   B_HI_RA_RAM_SRV_CMD_EXECUTE                                0x6

#define B_HI_RA_RAM_SRV_PAR__AX                                      0x420033
#define B_HI_RA_RAM_SRV_PAR__XSZ                                     5
#define B_HI_RA_RAM_SRV_PAR__W                                       16
#define B_HI_RA_RAM_SRV_PAR__M                                       0xFFFF

#define B_HI_RA_RAM_SRV_NOP_RES__A                                   0x420031
#define B_HI_RA_RAM_SRV_NOP_RES__W                                   3
#define B_HI_RA_RAM_SRV_NOP_RES__M                                   0x7
#define   B_HI_RA_RAM_SRV_NOP_RES_OK                                 0x0
#define   B_HI_RA_RAM_SRV_NOP_RES_INTERNAL_ERROR                     0x4

#define B_HI_RA_RAM_SRV_UIO_RES__A                                   0x420031
#define B_HI_RA_RAM_SRV_UIO_RES__W                                   3
#define B_HI_RA_RAM_SRV_UIO_RES__M                                   0x7
#define   B_HI_RA_RAM_SRV_UIO_RES_LO                                 0x0
#define   B_HI_RA_RAM_SRV_UIO_RES_HI                                 0x1

#define B_HI_RA_RAM_SRV_UIO_KEY__A                                   0x420033
#define B_HI_RA_RAM_SRV_UIO_KEY__W                                   16
#define B_HI_RA_RAM_SRV_UIO_KEY__M                                   0xFFFF
#define   B_HI_RA_RAM_SRV_UIO_KEY_ACT                                0x3973

#define B_HI_RA_RAM_SRV_UIO_SEL__A                                   0x420034
#define B_HI_RA_RAM_SRV_UIO_SEL__W                                   2
#define B_HI_RA_RAM_SRV_UIO_SEL__M                                   0x3
#define   B_HI_RA_RAM_SRV_UIO_SEL_ASEL                               0x0
#define   B_HI_RA_RAM_SRV_UIO_SEL_UIO                                0x1

#define B_HI_RA_RAM_SRV_UIO_SET__A                                   0x420035
#define B_HI_RA_RAM_SRV_UIO_SET__W                                   2
#define B_HI_RA_RAM_SRV_UIO_SET__M                                   0x3
#define   B_HI_RA_RAM_SRV_UIO_SET_OUT__B                             0
#define   B_HI_RA_RAM_SRV_UIO_SET_OUT__W                             1
#define   B_HI_RA_RAM_SRV_UIO_SET_OUT__M                             0x1
#define     B_HI_RA_RAM_SRV_UIO_SET_OUT_LO                           0x0
#define     B_HI_RA_RAM_SRV_UIO_SET_OUT_HI                           0x1
#define   B_HI_RA_RAM_SRV_UIO_SET_DIR__B                             1
#define   B_HI_RA_RAM_SRV_UIO_SET_DIR__W                             1
#define   B_HI_RA_RAM_SRV_UIO_SET_DIR__M                             0x2
#define     B_HI_RA_RAM_SRV_UIO_SET_DIR_OUT                          0x0
#define     B_HI_RA_RAM_SRV_UIO_SET_DIR_IN                           0x2

#define B_HI_RA_RAM_SRV_RST_RES__A                                   0x420031
#define B_HI_RA_RAM_SRV_RST_RES__W                                   1
#define B_HI_RA_RAM_SRV_RST_RES__M                                   0x1
#define   B_HI_RA_RAM_SRV_RST_RES_OK                                 0x0
#define   B_HI_RA_RAM_SRV_RST_RES_ERROR                              0x1

#define B_HI_RA_RAM_SRV_RST_KEY__A                                   0x420033
#define B_HI_RA_RAM_SRV_RST_KEY__W                                   16
#define B_HI_RA_RAM_SRV_RST_KEY__M                                   0xFFFF
#define   B_HI_RA_RAM_SRV_RST_KEY_ACT                                0x3973

#define B_HI_RA_RAM_SRV_CFG_RES__A                                   0x420031
#define B_HI_RA_RAM_SRV_CFG_RES__W                                   1
#define B_HI_RA_RAM_SRV_CFG_RES__M                                   0x1
#define   B_HI_RA_RAM_SRV_CFG_RES_OK                                 0x0
#define   B_HI_RA_RAM_SRV_CFG_RES_ERROR                              0x1

#define B_HI_RA_RAM_SRV_CFG_KEY__A                                   0x420033
#define B_HI_RA_RAM_SRV_CFG_KEY__W                                   16
#define B_HI_RA_RAM_SRV_CFG_KEY__M                                   0xFFFF
#define   B_HI_RA_RAM_SRV_CFG_KEY_ACT                                0x3973

#define B_HI_RA_RAM_SRV_CFG_DIV__A                                   0x420034
#define B_HI_RA_RAM_SRV_CFG_DIV__W                                   5
#define B_HI_RA_RAM_SRV_CFG_DIV__M                                   0x1F

#define B_HI_RA_RAM_SRV_CFG_BDL__A                                   0x420035
#define B_HI_RA_RAM_SRV_CFG_BDL__W                                   6
#define B_HI_RA_RAM_SRV_CFG_BDL__M                                   0x3F

#define B_HI_RA_RAM_SRV_CFG_WUP__A                                   0x420036
#define B_HI_RA_RAM_SRV_CFG_WUP__W                                   8
#define B_HI_RA_RAM_SRV_CFG_WUP__M                                   0xFF

#define B_HI_RA_RAM_SRV_CFG_ACT__A                                   0x420037
#define B_HI_RA_RAM_SRV_CFG_ACT__W                                   4
#define B_HI_RA_RAM_SRV_CFG_ACT__M                                   0xF
#define   B_HI_RA_RAM_SRV_CFG_ACT_SLV0__B                            0
#define   B_HI_RA_RAM_SRV_CFG_ACT_SLV0__W                            1
#define   B_HI_RA_RAM_SRV_CFG_ACT_SLV0__M                            0x1
#define     B_HI_RA_RAM_SRV_CFG_ACT_SLV0_OFF                         0x0
#define     B_HI_RA_RAM_SRV_CFG_ACT_SLV0_ON                          0x1
#define   B_HI_RA_RAM_SRV_CFG_ACT_SLV1__B                            1
#define   B_HI_RA_RAM_SRV_CFG_ACT_SLV1__W                            1
#define   B_HI_RA_RAM_SRV_CFG_ACT_SLV1__M                            0x2
#define     B_HI_RA_RAM_SRV_CFG_ACT_SLV1_OFF                         0x0
#define     B_HI_RA_RAM_SRV_CFG_ACT_SLV1_ON                          0x2
#define   B_HI_RA_RAM_SRV_CFG_ACT_BRD__B                             2
#define   B_HI_RA_RAM_SRV_CFG_ACT_BRD__W                             1
#define   B_HI_RA_RAM_SRV_CFG_ACT_BRD__M                             0x4
#define     B_HI_RA_RAM_SRV_CFG_ACT_BRD_OFF                          0x0
#define     B_HI_RA_RAM_SRV_CFG_ACT_BRD_ON                           0x4
#define   B_HI_RA_RAM_SRV_CFG_ACT_PWD__B                             3
#define   B_HI_RA_RAM_SRV_CFG_ACT_PWD__W                             1
#define   B_HI_RA_RAM_SRV_CFG_ACT_PWD__M                             0x8
#define     B_HI_RA_RAM_SRV_CFG_ACT_PWD_NOP                          0x0
#define     B_HI_RA_RAM_SRV_CFG_ACT_PWD_EXE                          0x8

#define B_HI_RA_RAM_SRV_CPY_RES__A                                   0x420031
#define B_HI_RA_RAM_SRV_CPY_RES__W                                   1
#define B_HI_RA_RAM_SRV_CPY_RES__M                                   0x1
#define   B_HI_RA_RAM_SRV_CPY_RES_OK                                 0x0
#define   B_HI_RA_RAM_SRV_CPY_RES_ERROR                              0x1

#define B_HI_RA_RAM_SRV_CPY_SBB__A                                   0x420033
#define B_HI_RA_RAM_SRV_CPY_SBB__W                                   12
#define B_HI_RA_RAM_SRV_CPY_SBB__M                                   0xFFF
#define   B_HI_RA_RAM_SRV_CPY_SBB_BNK__B                             0
#define   B_HI_RA_RAM_SRV_CPY_SBB_BNK__W                             6
#define   B_HI_RA_RAM_SRV_CPY_SBB_BNK__M                             0x3F
#define   B_HI_RA_RAM_SRV_CPY_SBB_BLK__B                             6
#define   B_HI_RA_RAM_SRV_CPY_SBB_BLK__W                             6
#define   B_HI_RA_RAM_SRV_CPY_SBB_BLK__M                             0xFC0

#define B_HI_RA_RAM_SRV_CPY_SAD__A                                   0x420034
#define B_HI_RA_RAM_SRV_CPY_SAD__W                                   16
#define B_HI_RA_RAM_SRV_CPY_SAD__M                                   0xFFFF

#define B_HI_RA_RAM_SRV_CPY_LEN__A                                   0x420035
#define B_HI_RA_RAM_SRV_CPY_LEN__W                                   16
#define B_HI_RA_RAM_SRV_CPY_LEN__M                                   0xFFFF

#define B_HI_RA_RAM_SRV_CPY_DBB__A                                   0x420033
#define B_HI_RA_RAM_SRV_CPY_DBB__W                                   12
#define B_HI_RA_RAM_SRV_CPY_DBB__M                                   0xFFF
#define   B_HI_RA_RAM_SRV_CPY_DBB_BNK__B                             0
#define   B_HI_RA_RAM_SRV_CPY_DBB_BNK__W                             6
#define   B_HI_RA_RAM_SRV_CPY_DBB_BNK__M                             0x3F
#define   B_HI_RA_RAM_SRV_CPY_DBB_BLK__B                             6
#define   B_HI_RA_RAM_SRV_CPY_DBB_BLK__W                             6
#define   B_HI_RA_RAM_SRV_CPY_DBB_BLK__M                             0xFC0

#define B_HI_RA_RAM_SRV_CPY_DAD__A                                   0x420034
#define B_HI_RA_RAM_SRV_CPY_DAD__W                                   16
#define B_HI_RA_RAM_SRV_CPY_DAD__M                                   0xFFFF

#define B_HI_RA_RAM_SRV_TRM_RES__A                                   0x420031
#define B_HI_RA_RAM_SRV_TRM_RES__W                                   2
#define B_HI_RA_RAM_SRV_TRM_RES__M                                   0x3
#define   B_HI_RA_RAM_SRV_TRM_RES_OK                                 0x0
#define   B_HI_RA_RAM_SRV_TRM_RES_ERROR                              0x1
#define   B_HI_RA_RAM_SRV_TRM_RES_ARBITRATION_FAILED                 0x3

#define B_HI_RA_RAM_SRV_TRM_MST__A                                   0x420033
#define B_HI_RA_RAM_SRV_TRM_MST__W                                   12
#define B_HI_RA_RAM_SRV_TRM_MST__M                                   0xFFF

#define B_HI_RA_RAM_SRV_TRM_SEQ__A                                   0x420034
#define B_HI_RA_RAM_SRV_TRM_SEQ__W                                   7
#define B_HI_RA_RAM_SRV_TRM_SEQ__M                                   0x7F

#define B_HI_RA_RAM_SRV_TRM_TRM__A                                   0x420035
#define B_HI_RA_RAM_SRV_TRM_TRM__W                                   15
#define B_HI_RA_RAM_SRV_TRM_TRM__M                                   0x7FFF
#define   B_HI_RA_RAM_SRV_TRM_TRM_DAT__B                             0
#define   B_HI_RA_RAM_SRV_TRM_TRM_DAT__W                             8
#define   B_HI_RA_RAM_SRV_TRM_TRM_DAT__M                             0xFF

#define B_HI_RA_RAM_SRV_TRM_DBB__A                                   0x420033
#define B_HI_RA_RAM_SRV_TRM_DBB__W                                   12
#define B_HI_RA_RAM_SRV_TRM_DBB__M                                   0xFFF
#define   B_HI_RA_RAM_SRV_TRM_DBB_BNK__B                             0
#define   B_HI_RA_RAM_SRV_TRM_DBB_BNK__W                             6
#define   B_HI_RA_RAM_SRV_TRM_DBB_BNK__M                             0x3F
#define   B_HI_RA_RAM_SRV_TRM_DBB_BLK__B                             6
#define   B_HI_RA_RAM_SRV_TRM_DBB_BLK__W                             6
#define   B_HI_RA_RAM_SRV_TRM_DBB_BLK__M                             0xFC0

#define B_HI_RA_RAM_SRV_TRM_DAD__A                                   0x420034
#define B_HI_RA_RAM_SRV_TRM_DAD__W                                   16
#define B_HI_RA_RAM_SRV_TRM_DAD__M                                   0xFFFF

#define B_HI_RA_RAM_USR_BEGIN__A                                     0x420040
#define B_HI_RA_RAM_USR_BEGIN__W                                     16
#define B_HI_RA_RAM_USR_BEGIN__M                                     0xFFFF

#define B_HI_RA_RAM_USR_END__A                                       0x42007F
#define B_HI_RA_RAM_USR_END__W                                       16
#define B_HI_RA_RAM_USR_END__M                                       0xFFFF

#define B_HI_IF_RAM_TRP_BPT0__AX                                     0x430000
#define B_HI_IF_RAM_TRP_BPT0__XSZ                                    2
#define B_HI_IF_RAM_TRP_BPT0__W                                      12
#define B_HI_IF_RAM_TRP_BPT0__M                                      0xFFF

#define B_HI_IF_RAM_TRP_STKU__AX                                     0x430002
#define B_HI_IF_RAM_TRP_STKU__XSZ                                    2
#define B_HI_IF_RAM_TRP_STKU__W                                      12
#define B_HI_IF_RAM_TRP_STKU__M                                      0xFFF

#define B_HI_IF_RAM_USR_BEGIN__A                                     0x430200
#define B_HI_IF_RAM_USR_BEGIN__W                                     12
#define B_HI_IF_RAM_USR_BEGIN__M                                     0xFFF

#define B_HI_IF_RAM_USR_END__A                                       0x4303FF
#define B_HI_IF_RAM_USR_END__W                                       12
#define B_HI_IF_RAM_USR_END__M                                       0xFFF

#define B_SC_SID                                                     0x11

#define B_SC_COMM_EXEC__A                                            0x800000
#define B_SC_COMM_EXEC__W                                            3
#define B_SC_COMM_EXEC__M                                            0x7
#define   B_SC_COMM_EXEC_CTL__B                                      0
#define   B_SC_COMM_EXEC_CTL__W                                      3
#define   B_SC_COMM_EXEC_CTL__M                                      0x7
#define     B_SC_COMM_EXEC_CTL_STOP                                  0x0
#define     B_SC_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_SC_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_SC_COMM_EXEC_CTL_STEP                                  0x3
#define     B_SC_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_SC_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_SC_COMM_STATE__A                                           0x800001
#define B_SC_COMM_STATE__W                                           16
#define B_SC_COMM_STATE__M                                           0xFFFF
#define B_SC_COMM_MB__A                                              0x800002
#define B_SC_COMM_MB__W                                              16
#define B_SC_COMM_MB__M                                              0xFFFF
#define B_SC_COMM_SERVICE0__A                                        0x800003
#define B_SC_COMM_SERVICE0__W                                        16
#define B_SC_COMM_SERVICE0__M                                        0xFFFF
#define B_SC_COMM_SERVICE1__A                                        0x800004
#define B_SC_COMM_SERVICE1__W                                        16
#define B_SC_COMM_SERVICE1__M                                        0xFFFF
#define B_SC_COMM_INT_STA__A                                         0x800007
#define B_SC_COMM_INT_STA__W                                         16
#define B_SC_COMM_INT_STA__M                                         0xFFFF
#define B_SC_COMM_INT_MSK__A                                         0x800008
#define B_SC_COMM_INT_MSK__W                                         16
#define B_SC_COMM_INT_MSK__M                                         0xFFFF

#define B_SC_CT_REG_COMM_EXEC__A                                     0x810000
#define B_SC_CT_REG_COMM_EXEC__W                                     3
#define B_SC_CT_REG_COMM_EXEC__M                                     0x7
#define   B_SC_CT_REG_COMM_EXEC_CTL__B                               0
#define   B_SC_CT_REG_COMM_EXEC_CTL__W                               3
#define   B_SC_CT_REG_COMM_EXEC_CTL__M                               0x7
#define     B_SC_CT_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_SC_CT_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_SC_CT_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_SC_CT_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_SC_CT_REG_COMM_STATE__A                                    0x810001
#define B_SC_CT_REG_COMM_STATE__W                                    10
#define B_SC_CT_REG_COMM_STATE__M                                    0x3FF
#define B_SC_CT_REG_COMM_SERVICE0__A                                 0x810003
#define B_SC_CT_REG_COMM_SERVICE0__W                                 16
#define B_SC_CT_REG_COMM_SERVICE0__M                                 0xFFFF
#define B_SC_CT_REG_COMM_SERVICE1__A                                 0x810004
#define B_SC_CT_REG_COMM_SERVICE1__W                                 16
#define B_SC_CT_REG_COMM_SERVICE1__M                                 0xFFFF
#define   B_SC_CT_REG_COMM_SERVICE1_SC__B                            1
#define   B_SC_CT_REG_COMM_SERVICE1_SC__W                            1
#define   B_SC_CT_REG_COMM_SERVICE1_SC__M                            0x2

#define B_SC_CT_REG_COMM_INT_STA__A                                  0x810007
#define B_SC_CT_REG_COMM_INT_STA__W                                  1
#define B_SC_CT_REG_COMM_INT_STA__M                                  0x1
#define   B_SC_CT_REG_COMM_INT_STA_REQUEST__B                        0
#define   B_SC_CT_REG_COMM_INT_STA_REQUEST__W                        1
#define   B_SC_CT_REG_COMM_INT_STA_REQUEST__M                        0x1

#define B_SC_CT_REG_COMM_INT_MSK__A                                  0x810008
#define B_SC_CT_REG_COMM_INT_MSK__W                                  1
#define B_SC_CT_REG_COMM_INT_MSK__M                                  0x1
#define   B_SC_CT_REG_COMM_INT_MSK_REQUEST__B                        0
#define   B_SC_CT_REG_COMM_INT_MSK_REQUEST__W                        1
#define   B_SC_CT_REG_COMM_INT_MSK_REQUEST__M                        0x1

#define B_SC_CT_REG_CTL_STK__AX                                      0x810010
#define B_SC_CT_REG_CTL_STK__XSZ                                     4
#define B_SC_CT_REG_CTL_STK__W                                       10
#define B_SC_CT_REG_CTL_STK__M                                       0x3FF

#define B_SC_CT_REG_CTL_BPT_IDX__A                                   0x81001F
#define B_SC_CT_REG_CTL_BPT_IDX__W                                   1
#define B_SC_CT_REG_CTL_BPT_IDX__M                                   0x1

#define B_SC_CT_REG_CTL_BPT__A                                       0x810020
#define B_SC_CT_REG_CTL_BPT__W                                       10
#define B_SC_CT_REG_CTL_BPT__M                                       0x3FF

#define B_SC_RA_RAM_PARAM0__A                                        0x820040
#define B_SC_RA_RAM_PARAM0__W                                        16
#define B_SC_RA_RAM_PARAM0__M                                        0xFFFF
#define B_SC_RA_RAM_PARAM1__A                                        0x820041
#define B_SC_RA_RAM_PARAM1__W                                        16
#define B_SC_RA_RAM_PARAM1__M                                        0xFFFF
#define B_SC_RA_RAM_CMD_ADDR__A                                      0x820042
#define B_SC_RA_RAM_CMD_ADDR__W                                      16
#define B_SC_RA_RAM_CMD_ADDR__M                                      0xFFFF
#define B_SC_RA_RAM_CMD__A                                           0x820043
#define B_SC_RA_RAM_CMD__W                                           16
#define B_SC_RA_RAM_CMD__M                                           0xFFFF
#define   B_SC_RA_RAM_CMD_NULL                                       0x0
#define   B_SC_RA_RAM_CMD_PROC_START                                 0x1
#define   B_SC_RA_RAM_CMD_PROC_TRIGGER                               0x2
#define   B_SC_RA_RAM_CMD_SET_PREF_PARAM                             0x3
#define   B_SC_RA_RAM_CMD_PROGRAM_PARAM                              0x4
#define   B_SC_RA_RAM_CMD_GET_OP_PARAM                               0x5
#define   B_SC_RA_RAM_CMD_USER_IO                                    0x6
#define   B_SC_RA_RAM_CMD_SET_TIMER                                  0x7
#define   B_SC_RA_RAM_CMD_SET_ECHO_TIMING                            0x8
#define   B_SC_RA_RAM_CMD_MAX                                        0x9
#define   B_SC_RA_RAM_CMDBLOCK__C                                    0x4

#define B_SC_RA_RAM_PROC_ACTIVATE__A                                 0x820044
#define B_SC_RA_RAM_PROC_ACTIVATE__W                                 16
#define B_SC_RA_RAM_PROC_ACTIVATE__M                                 0xFFFF
#define B_SC_RA_RAM_PROC_ACTIVATE__PRE                               0xFFFF
#define B_SC_RA_RAM_PROC_TERMINATED__A                               0x820045
#define B_SC_RA_RAM_PROC_TERMINATED__W                               16
#define B_SC_RA_RAM_PROC_TERMINATED__M                               0xFFFF
#define B_SC_RA_RAM_SW_EVENT__A                                      0x820046
#define B_SC_RA_RAM_SW_EVENT__W                                      14
#define B_SC_RA_RAM_SW_EVENT__M                                      0x3FFF
#define   B_SC_RA_RAM_SW_EVENT_RUN_NMASK__B                          0
#define   B_SC_RA_RAM_SW_EVENT_RUN_NMASK__W                          1
#define   B_SC_RA_RAM_SW_EVENT_RUN_NMASK__M                          0x1
#define   B_SC_RA_RAM_SW_EVENT_RUN__B                                1
#define   B_SC_RA_RAM_SW_EVENT_RUN__W                                1
#define   B_SC_RA_RAM_SW_EVENT_RUN__M                                0x2
#define   B_SC_RA_RAM_SW_EVENT_TERMINATE__B                          2
#define   B_SC_RA_RAM_SW_EVENT_TERMINATE__W                          1
#define   B_SC_RA_RAM_SW_EVENT_TERMINATE__M                          0x4
#define   B_SC_RA_RAM_SW_EVENT_FT_START__B                           3
#define   B_SC_RA_RAM_SW_EVENT_FT_START__W                           1
#define   B_SC_RA_RAM_SW_EVENT_FT_START__M                           0x8
#define   B_SC_RA_RAM_SW_EVENT_FI_START__B                           4
#define   B_SC_RA_RAM_SW_EVENT_FI_START__W                           1
#define   B_SC_RA_RAM_SW_EVENT_FI_START__M                           0x10
#define   B_SC_RA_RAM_SW_EVENT_EQ_TPS__B                             5
#define   B_SC_RA_RAM_SW_EVENT_EQ_TPS__W                             1
#define   B_SC_RA_RAM_SW_EVENT_EQ_TPS__M                             0x20
#define   B_SC_RA_RAM_SW_EVENT_EQ_ERR__B                             6
#define   B_SC_RA_RAM_SW_EVENT_EQ_ERR__W                             1
#define   B_SC_RA_RAM_SW_EVENT_EQ_ERR__M                             0x40
#define   B_SC_RA_RAM_SW_EVENT_CE_IR__B                              7
#define   B_SC_RA_RAM_SW_EVENT_CE_IR__W                              1
#define   B_SC_RA_RAM_SW_EVENT_CE_IR__M                              0x80
#define   B_SC_RA_RAM_SW_EVENT_FE_FD__B                              8
#define   B_SC_RA_RAM_SW_EVENT_FE_FD__W                              1
#define   B_SC_RA_RAM_SW_EVENT_FE_FD__M                              0x100
#define   B_SC_RA_RAM_SW_EVENT_FE_CF__B                              9
#define   B_SC_RA_RAM_SW_EVENT_FE_CF__W                              1
#define   B_SC_RA_RAM_SW_EVENT_FE_CF__M                              0x200
#define   B_SC_RA_RAM_SW_EVENT_NF_READY__B                           12
#define   B_SC_RA_RAM_SW_EVENT_NF_READY__W                           1
#define   B_SC_RA_RAM_SW_EVENT_NF_READY__M                           0x1000

#define B_SC_RA_RAM_LOCKTRACK__A                                     0x820047
#define B_SC_RA_RAM_LOCKTRACK__W                                     16
#define B_SC_RA_RAM_LOCKTRACK__M                                     0xFFFF
#define   B_SC_RA_RAM_LOCKTRACK_NULL                                 0x0
#define   B_SC_RA_RAM_LOCKTRACK_MIN                                  0x1
#define   B_SC_RA_RAM_LOCKTRACK_RESET                                0x1
#define   B_SC_RA_RAM_LOCKTRACK_MG_DETECT                            0x2
#define   B_SC_RA_RAM_LOCKTRACK_P_DETECT                             0x3
#define   B_SC_RA_RAM_LOCKTRACK_P_DETECT_SEARCH                      0x4
#define   B_SC_RA_RAM_LOCKTRACK_LC                                   0x5
#define   B_SC_RA_RAM_LOCKTRACK_P_ECHO                               0x6
#define   B_SC_RA_RAM_LOCKTRACK_NE_INIT                              0x7
#define   B_SC_RA_RAM_LOCKTRACK_TRACK_INIT                           0x8
#define   B_SC_RA_RAM_LOCKTRACK_TRACK                                0x9
#define   B_SC_RA_RAM_LOCKTRACK_TRACK_ERROR                          0xA
#define   B_SC_RA_RAM_LOCKTRACK_MAX                                  0xB

#define B_SC_RA_RAM_OP_PARAM__A                                      0x820048
#define B_SC_RA_RAM_OP_PARAM__W                                      13
#define B_SC_RA_RAM_OP_PARAM__M                                      0x1FFF
#define   B_SC_RA_RAM_OP_PARAM_MODE__B                               0
#define   B_SC_RA_RAM_OP_PARAM_MODE__W                               2
#define   B_SC_RA_RAM_OP_PARAM_MODE__M                               0x3
#define     B_SC_RA_RAM_OP_PARAM_MODE_2K                             0x0
#define     B_SC_RA_RAM_OP_PARAM_MODE_8K                             0x1
#define   B_SC_RA_RAM_OP_PARAM_GUARD__B                              2
#define   B_SC_RA_RAM_OP_PARAM_GUARD__W                              2
#define   B_SC_RA_RAM_OP_PARAM_GUARD__M                              0xC
#define     B_SC_RA_RAM_OP_PARAM_GUARD_32                            0x0
#define     B_SC_RA_RAM_OP_PARAM_GUARD_16                            0x4
#define     B_SC_RA_RAM_OP_PARAM_GUARD_8                             0x8
#define     B_SC_RA_RAM_OP_PARAM_GUARD_4                             0xC
#define   B_SC_RA_RAM_OP_PARAM_CONST__B                              4
#define   B_SC_RA_RAM_OP_PARAM_CONST__W                              2
#define   B_SC_RA_RAM_OP_PARAM_CONST__M                              0x30
#define     B_SC_RA_RAM_OP_PARAM_CONST_QPSK                          0x0
#define     B_SC_RA_RAM_OP_PARAM_CONST_QAM16                         0x10
#define     B_SC_RA_RAM_OP_PARAM_CONST_QAM64                         0x20
#define   B_SC_RA_RAM_OP_PARAM_HIER__B                               6
#define   B_SC_RA_RAM_OP_PARAM_HIER__W                               3
#define   B_SC_RA_RAM_OP_PARAM_HIER__M                               0x1C0
#define     B_SC_RA_RAM_OP_PARAM_HIER_NO                             0x0
#define     B_SC_RA_RAM_OP_PARAM_HIER_A1                             0x40
#define     B_SC_RA_RAM_OP_PARAM_HIER_A2                             0x80
#define     B_SC_RA_RAM_OP_PARAM_HIER_A4                             0xC0
#define   B_SC_RA_RAM_OP_PARAM_RATE__B                               9
#define   B_SC_RA_RAM_OP_PARAM_RATE__W                               3
#define   B_SC_RA_RAM_OP_PARAM_RATE__M                               0xE00
#define     B_SC_RA_RAM_OP_PARAM_RATE_1_2                            0x0
#define     B_SC_RA_RAM_OP_PARAM_RATE_2_3                            0x200
#define     B_SC_RA_RAM_OP_PARAM_RATE_3_4                            0x400
#define     B_SC_RA_RAM_OP_PARAM_RATE_5_6                            0x600
#define     B_SC_RA_RAM_OP_PARAM_RATE_7_8                            0x800
#define   B_SC_RA_RAM_OP_PARAM_PRIO__B                               12
#define   B_SC_RA_RAM_OP_PARAM_PRIO__W                               1
#define   B_SC_RA_RAM_OP_PARAM_PRIO__M                               0x1000
#define     B_SC_RA_RAM_OP_PARAM_PRIO_HI                             0x0
#define     B_SC_RA_RAM_OP_PARAM_PRIO_LO                             0x1000

#define B_SC_RA_RAM_OP_AUTO__A                                       0x820049
#define B_SC_RA_RAM_OP_AUTO__W                                       6
#define B_SC_RA_RAM_OP_AUTO__M                                       0x3F
#define B_SC_RA_RAM_OP_AUTO__PRE                                     0x1F
#define   B_SC_RA_RAM_OP_AUTO_MODE__B                                0
#define   B_SC_RA_RAM_OP_AUTO_MODE__W                                1
#define   B_SC_RA_RAM_OP_AUTO_MODE__M                                0x1
#define   B_SC_RA_RAM_OP_AUTO_GUARD__B                               1
#define   B_SC_RA_RAM_OP_AUTO_GUARD__W                               1
#define   B_SC_RA_RAM_OP_AUTO_GUARD__M                               0x2
#define   B_SC_RA_RAM_OP_AUTO_CONST__B                               2
#define   B_SC_RA_RAM_OP_AUTO_CONST__W                               1
#define   B_SC_RA_RAM_OP_AUTO_CONST__M                               0x4
#define   B_SC_RA_RAM_OP_AUTO_HIER__B                                3
#define   B_SC_RA_RAM_OP_AUTO_HIER__W                                1
#define   B_SC_RA_RAM_OP_AUTO_HIER__M                                0x8
#define   B_SC_RA_RAM_OP_AUTO_RATE__B                                4
#define   B_SC_RA_RAM_OP_AUTO_RATE__W                                1
#define   B_SC_RA_RAM_OP_AUTO_RATE__M                                0x10
#define   B_SC_RA_RAM_OP_AUTO_PRIO__B                                5
#define   B_SC_RA_RAM_OP_AUTO_PRIO__W                                1
#define   B_SC_RA_RAM_OP_AUTO_PRIO__M                                0x20

#define B_SC_RA_RAM_PILOT_STATUS__A                                  0x82004A
#define B_SC_RA_RAM_PILOT_STATUS__W                                  16
#define B_SC_RA_RAM_PILOT_STATUS__M                                  0xFFFF
#define   B_SC_RA_RAM_PILOT_STATUS_OK                                0x0
#define   B_SC_RA_RAM_PILOT_STATUS_SPD_ERROR                         0x1
#define   B_SC_RA_RAM_PILOT_STATUS_CPD_ERROR                         0x2
#define   B_SC_RA_RAM_PILOT_STATUS_SYM_ERROR                         0x3

#define B_SC_RA_RAM_LOCK__A                                          0x82004B
#define B_SC_RA_RAM_LOCK__W                                          4
#define B_SC_RA_RAM_LOCK__M                                          0xF
#define   B_SC_RA_RAM_LOCK_DEMOD__B                                  0
#define   B_SC_RA_RAM_LOCK_DEMOD__W                                  1
#define   B_SC_RA_RAM_LOCK_DEMOD__M                                  0x1
#define   B_SC_RA_RAM_LOCK_FEC__B                                    1
#define   B_SC_RA_RAM_LOCK_FEC__W                                    1
#define   B_SC_RA_RAM_LOCK_FEC__M                                    0x2
#define   B_SC_RA_RAM_LOCK_MPEG__B                                   2
#define   B_SC_RA_RAM_LOCK_MPEG__W                                   1
#define   B_SC_RA_RAM_LOCK_MPEG__M                                   0x4
#define   B_SC_RA_RAM_LOCK_NODVBT__B                                 3
#define   B_SC_RA_RAM_LOCK_NODVBT__W                                 1
#define   B_SC_RA_RAM_LOCK_NODVBT__M                                 0x8

#define B_SC_RA_RAM_BE_OPT_ENA__A                                    0x82004C
#define B_SC_RA_RAM_BE_OPT_ENA__W                                    5
#define B_SC_RA_RAM_BE_OPT_ENA__M                                    0x1F
#define B_SC_RA_RAM_BE_OPT_ENA__PRE                                  0x1E
#define   B_SC_RA_RAM_BE_OPT_ENA_MOTION                              0x0
#define   B_SC_RA_RAM_BE_OPT_ENA_CP_OPT                              0x1
#define   B_SC_RA_RAM_BE_OPT_ENA_CSI_OPT                             0x2
#define   B_SC_RA_RAM_BE_OPT_ENA_CAL_OPT                             0x3
#define   B_SC_RA_RAM_BE_OPT_ENA_FR_WATCH                            0x4
#define   B_SC_RA_RAM_BE_OPT_ENA_MAX                                 0x5

#define B_SC_RA_RAM_BE_OPT_DELAY__A                                  0x82004D
#define B_SC_RA_RAM_BE_OPT_DELAY__W                                  16
#define B_SC_RA_RAM_BE_OPT_DELAY__M                                  0xFFFF
#define B_SC_RA_RAM_BE_OPT_DELAY__PRE                                0x200
#define B_SC_RA_RAM_BE_OPT_INIT_DELAY__A                             0x82004E
#define B_SC_RA_RAM_BE_OPT_INIT_DELAY__W                             16
#define B_SC_RA_RAM_BE_OPT_INIT_DELAY__M                             0xFFFF
#define B_SC_RA_RAM_BE_OPT_INIT_DELAY__PRE                           0x400
#define B_SC_RA_RAM_ECHO_THRES__A                                    0x82004F
#define B_SC_RA_RAM_ECHO_THRES__W                                    16
#define B_SC_RA_RAM_ECHO_THRES__M                                    0xFFFF
#define B_SC_RA_RAM_ECHO_THRES__PRE                                  0x2A
#define B_SC_RA_RAM_CONFIG__A                                        0x820050
#define B_SC_RA_RAM_CONFIG__W                                        16
#define B_SC_RA_RAM_CONFIG__M                                        0xFFFF
#define B_SC_RA_RAM_CONFIG__PRE                                      0x14
#define   B_SC_RA_RAM_CONFIG_ID__B                                   0
#define   B_SC_RA_RAM_CONFIG_ID__W                                   1
#define   B_SC_RA_RAM_CONFIG_ID__M                                   0x1
#define     B_SC_RA_RAM_CONFIG_ID_PRO                                0x0
#define     B_SC_RA_RAM_CONFIG_ID_CONSUMER                           0x1
#define   B_SC_RA_RAM_CONFIG_GLITCHLESS_ENABLE__B                    1
#define   B_SC_RA_RAM_CONFIG_GLITCHLESS_ENABLE__W                    1
#define   B_SC_RA_RAM_CONFIG_GLITCHLESS_ENABLE__M                    0x2
#define   B_SC_RA_RAM_CONFIG_FR_ENABLE__B                            2
#define   B_SC_RA_RAM_CONFIG_FR_ENABLE__W                            1
#define   B_SC_RA_RAM_CONFIG_FR_ENABLE__M                            0x4
#define   B_SC_RA_RAM_CONFIG_MIXMODE__B                              3
#define   B_SC_RA_RAM_CONFIG_MIXMODE__W                              1
#define   B_SC_RA_RAM_CONFIG_MIXMODE__M                              0x8
#define   B_SC_RA_RAM_CONFIG_FREQSCAN__B                             4
#define   B_SC_RA_RAM_CONFIG_FREQSCAN__W                             1
#define   B_SC_RA_RAM_CONFIG_FREQSCAN__M                             0x10
#define   B_SC_RA_RAM_CONFIG_SLAVE__B                                5
#define   B_SC_RA_RAM_CONFIG_SLAVE__W                                1
#define   B_SC_RA_RAM_CONFIG_SLAVE__M                                0x20
#define   B_SC_RA_RAM_CONFIG_FAR_OFF__B                              6
#define   B_SC_RA_RAM_CONFIG_FAR_OFF__W                              1
#define   B_SC_RA_RAM_CONFIG_FAR_OFF__M                              0x40
#define   B_SC_RA_RAM_CONFIG_FEC_CHECK_ON__B                         7
#define   B_SC_RA_RAM_CONFIG_FEC_CHECK_ON__W                         1
#define   B_SC_RA_RAM_CONFIG_FEC_CHECK_ON__M                         0x80
#define   B_SC_RA_RAM_CONFIG_ECHO_UPDATED__B                         8
#define   B_SC_RA_RAM_CONFIG_ECHO_UPDATED__W                         1
#define   B_SC_RA_RAM_CONFIG_ECHO_UPDATED__M                         0x100
#define   B_SC_RA_RAM_CONFIG_DIV_BLANK_ENABLE__B                     9
#define   B_SC_RA_RAM_CONFIG_DIV_BLANK_ENABLE__W                     1
#define   B_SC_RA_RAM_CONFIG_DIV_BLANK_ENABLE__M                     0x200
#define   B_SC_RA_RAM_CONFIG_DIV_ECHO_ENABLE__B                      10
#define   B_SC_RA_RAM_CONFIG_DIV_ECHO_ENABLE__W                      1
#define   B_SC_RA_RAM_CONFIG_DIV_ECHO_ENABLE__M                      0x400
#define   B_SC_RA_RAM_CONFIG_ADJUST_OFF__B                           15
#define   B_SC_RA_RAM_CONFIG_ADJUST_OFF__W                           1
#define   B_SC_RA_RAM_CONFIG_ADJUST_OFF__M                           0x8000

#define B_SC_RA_RAM_CE_REG_NE_FD_OFF__A                              0x820054
#define B_SC_RA_RAM_CE_REG_NE_FD_OFF__W                              16
#define B_SC_RA_RAM_CE_REG_NE_FD_OFF__M                              0xFFFF
#define B_SC_RA_RAM_CE_REG_NE_FD_OFF__PRE                            0xA0

#define B_SC_RA_RAM_FR_2K_MAN_SH__A                                  0x820055
#define B_SC_RA_RAM_FR_2K_MAN_SH__W                                  16
#define B_SC_RA_RAM_FR_2K_MAN_SH__M                                  0xFFFF
#define B_SC_RA_RAM_FR_2K_MAN_SH__PRE                                0x7
#define B_SC_RA_RAM_FR_2K_TAP_SH__A                                  0x820056
#define B_SC_RA_RAM_FR_2K_TAP_SH__W                                  16
#define B_SC_RA_RAM_FR_2K_TAP_SH__M                                  0xFFFF
#define B_SC_RA_RAM_FR_2K_TAP_SH__PRE                                0x3
#define B_SC_RA_RAM_FR_2K_LEAK_UPD__A                                0x820057
#define B_SC_RA_RAM_FR_2K_LEAK_UPD__W                                16
#define B_SC_RA_RAM_FR_2K_LEAK_UPD__M                                0xFFFF
#define B_SC_RA_RAM_FR_2K_LEAK_UPD__PRE                              0x2
#define B_SC_RA_RAM_FR_2K_LEAK_SH__A                                 0x820058
#define B_SC_RA_RAM_FR_2K_LEAK_SH__W                                 16
#define B_SC_RA_RAM_FR_2K_LEAK_SH__M                                 0xFFFF
#define B_SC_RA_RAM_FR_2K_LEAK_SH__PRE                               0x2

#define B_SC_RA_RAM_FR_8K_MAN_SH__A                                  0x820059
#define B_SC_RA_RAM_FR_8K_MAN_SH__W                                  16
#define B_SC_RA_RAM_FR_8K_MAN_SH__M                                  0xFFFF
#define B_SC_RA_RAM_FR_8K_MAN_SH__PRE                                0x7
#define B_SC_RA_RAM_FR_8K_TAP_SH__A                                  0x82005A
#define B_SC_RA_RAM_FR_8K_TAP_SH__W                                  16
#define B_SC_RA_RAM_FR_8K_TAP_SH__M                                  0xFFFF
#define B_SC_RA_RAM_FR_8K_TAP_SH__PRE                                0x4
#define B_SC_RA_RAM_FR_8K_LEAK_UPD__A                                0x82005B
#define B_SC_RA_RAM_FR_8K_LEAK_UPD__W                                16
#define B_SC_RA_RAM_FR_8K_LEAK_UPD__M                                0xFFFF
#define B_SC_RA_RAM_FR_8K_LEAK_UPD__PRE                              0x2
#define B_SC_RA_RAM_FR_8K_LEAK_SH__A                                 0x82005C
#define B_SC_RA_RAM_FR_8K_LEAK_SH__W                                 16
#define B_SC_RA_RAM_FR_8K_LEAK_SH__M                                 0xFFFF
#define B_SC_RA_RAM_FR_8K_LEAK_SH__PRE                               0x2

#define B_SC_RA_RAM_CO_TD_CAL_2K__A                                  0x82005D
#define B_SC_RA_RAM_CO_TD_CAL_2K__W                                  16
#define B_SC_RA_RAM_CO_TD_CAL_2K__M                                  0xFFFF
#define B_SC_RA_RAM_CO_TD_CAL_2K__PRE                                0xFFEB
#define B_SC_RA_RAM_CO_TD_CAL_8K__A                                  0x82005E
#define B_SC_RA_RAM_CO_TD_CAL_8K__W                                  16
#define B_SC_RA_RAM_CO_TD_CAL_8K__M                                  0xFFFF
#define B_SC_RA_RAM_CO_TD_CAL_8K__PRE                                0xFFE8
#define B_SC_RA_RAM_MOTION_OFFSET__A                                 0x82005F
#define B_SC_RA_RAM_MOTION_OFFSET__W                                 16
#define B_SC_RA_RAM_MOTION_OFFSET__M                                 0xFFFF
#define B_SC_RA_RAM_MOTION_OFFSET__PRE                               0x2
#define B_SC_RA_RAM_STATE_PROC_STOP__AX                              0x820060
#define B_SC_RA_RAM_STATE_PROC_STOP__XSZ                             10
#define B_SC_RA_RAM_STATE_PROC_STOP__W                               16
#define B_SC_RA_RAM_STATE_PROC_STOP__M                               0xFFFF
#define B_SC_RA_RAM_STATE_PROC_STOP_1__PRE                           0xFFFE
#define B_SC_RA_RAM_STATE_PROC_STOP_2__PRE                           0x0
#define B_SC_RA_RAM_STATE_PROC_STOP_3__PRE                           0x4
#define B_SC_RA_RAM_STATE_PROC_STOP_4__PRE                           0x0
#define B_SC_RA_RAM_STATE_PROC_STOP_5__PRE                           0x0
#define B_SC_RA_RAM_STATE_PROC_STOP_6__PRE                           0x0
#define B_SC_RA_RAM_STATE_PROC_STOP_7__PRE                           0x0
#define B_SC_RA_RAM_STATE_PROC_STOP_8__PRE                           0x0
#define B_SC_RA_RAM_STATE_PROC_STOP_9__PRE                           0x0
#define B_SC_RA_RAM_STATE_PROC_STOP_10__PRE                          0xFFFE
#define B_SC_RA_RAM_STATE_PROC_START__AX                             0x820070
#define B_SC_RA_RAM_STATE_PROC_START__XSZ                            10
#define B_SC_RA_RAM_STATE_PROC_START__W                              16
#define B_SC_RA_RAM_STATE_PROC_START__M                              0xFFFF
#define B_SC_RA_RAM_STATE_PROC_START_1__PRE                          0x80
#define B_SC_RA_RAM_STATE_PROC_START_2__PRE                          0x2
#define B_SC_RA_RAM_STATE_PROC_START_3__PRE                          0x4
#define B_SC_RA_RAM_STATE_PROC_START_4__PRE                          0x4
#define B_SC_RA_RAM_STATE_PROC_START_5__PRE                          0x100
#define B_SC_RA_RAM_STATE_PROC_START_6__PRE                          0x0
#define B_SC_RA_RAM_STATE_PROC_START_7__PRE                          0x40
#define B_SC_RA_RAM_STATE_PROC_START_8__PRE                          0x10
#define B_SC_RA_RAM_STATE_PROC_START_9__PRE                          0x30
#define B_SC_RA_RAM_STATE_PROC_START_10__PRE                         0x0
#define B_SC_RA_RAM_IF_SAVE__AX                                      0x82008E
#define B_SC_RA_RAM_IF_SAVE__XSZ                                     2
#define B_SC_RA_RAM_IF_SAVE__W                                       16
#define B_SC_RA_RAM_IF_SAVE__M                                       0xFFFF
#define B_SC_RA_RAM_FR_THRES__A                                      0x82007D
#define B_SC_RA_RAM_FR_THRES__W                                      16
#define B_SC_RA_RAM_FR_THRES__M                                      0xFFFF
#define B_SC_RA_RAM_FR_THRES__PRE                                    0x1A2C
#define B_SC_RA_RAM_STATUS__A                                        0x82007E
#define B_SC_RA_RAM_STATUS__W                                        16
#define B_SC_RA_RAM_STATUS__M                                        0xFFFF
#define B_SC_RA_RAM_NF_BORDER_INIT__A                                0x82007F
#define B_SC_RA_RAM_NF_BORDER_INIT__W                                16
#define B_SC_RA_RAM_NF_BORDER_INIT__M                                0xFFFF
#define B_SC_RA_RAM_NF_BORDER_INIT__PRE                              0x708
#define B_SC_RA_RAM_TIMER__A                                         0x820080
#define B_SC_RA_RAM_TIMER__W                                         16
#define B_SC_RA_RAM_TIMER__M                                         0xFFFF
#define B_SC_RA_RAM_FI_OFFSET__A                                     0x820081
#define B_SC_RA_RAM_FI_OFFSET__W                                     16
#define B_SC_RA_RAM_FI_OFFSET__M                                     0xFFFF
#define B_SC_RA_RAM_FI_OFFSET__PRE                                   0x382
#define B_SC_RA_RAM_ECHO_GUARD__A                                    0x820082
#define B_SC_RA_RAM_ECHO_GUARD__W                                    16
#define B_SC_RA_RAM_ECHO_GUARD__M                                    0xFFFF
#define B_SC_RA_RAM_ECHO_GUARD__PRE                                  0x18
#define B_SC_RA_RAM_PILOT_CPD_EXP_MARG_CO__A                         0x8200BA
#define B_SC_RA_RAM_PILOT_CPD_EXP_MARG_CO__W                         16
#define B_SC_RA_RAM_PILOT_CPD_EXP_MARG_CO__M                         0xFFFF
#define B_SC_RA_RAM_PILOT_CPD_EXP_MARG_CO__PRE                       0x3
#define B_SC_RA_RAM_PILOT_CPD_EXP_MARG_TILT__A                       0x8200BB
#define B_SC_RA_RAM_PILOT_CPD_EXP_MARG_TILT__W                       16
#define B_SC_RA_RAM_PILOT_CPD_EXP_MARG_TILT__M                       0xFFFF
#define B_SC_RA_RAM_PILOT_CPD_EXP_MARG_TILT__PRE                     0x0

#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_32__A                         0x820098
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_32__W                         16
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_32__M                         0xFFFF
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_32__PRE                       0x258
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_16__A                         0x820099
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_16__W                         16
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_16__M                         0xFFFF
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_16__PRE                       0x258
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_8__A                          0x82009A
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_8__W                          16
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_8__M                          0xFFFF
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_8__PRE                        0x258
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_4__A                          0x82009B
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_4__W                          16
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_4__M                          0xFFFF
#define B_SC_RA_RAM_DIVERSITY_DELAY_2K_4__PRE                        0x258

#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_32__A                         0x82009C
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_32__W                         16
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_32__M                         0xFFFF
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_32__PRE                       0xDAC
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_16__A                         0x82009D
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_16__W                         16
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_16__M                         0xFFFF
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_16__PRE                       0xDAC
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_8__A                          0x82009E
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_8__W                          16
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_8__M                          0xFFFF
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_8__PRE                        0xDAC
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_4__A                          0x82009F
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_4__W                          16
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_4__M                          0xFFFF
#define B_SC_RA_RAM_DIVERSITY_DELAY_8K_4__PRE                        0xDAC

#define B_SC_RA_RAM_IR_FREQ__A                                       0x8200D0
#define B_SC_RA_RAM_IR_FREQ__W                                       16
#define B_SC_RA_RAM_IR_FREQ__M                                       0xFFFF
#define B_SC_RA_RAM_IR_FREQ__PRE                                     0x0

#define B_SC_RA_RAM_IR_COARSE_2K_LENGTH__A                           0x8200D1
#define B_SC_RA_RAM_IR_COARSE_2K_LENGTH__W                           16
#define B_SC_RA_RAM_IR_COARSE_2K_LENGTH__M                           0xFFFF
#define B_SC_RA_RAM_IR_COARSE_2K_LENGTH__PRE                         0x9
#define B_SC_RA_RAM_IR_COARSE_2K_FREQINC__A                          0x8200D2
#define B_SC_RA_RAM_IR_COARSE_2K_FREQINC__W                          16
#define B_SC_RA_RAM_IR_COARSE_2K_FREQINC__M                          0xFFFF
#define B_SC_RA_RAM_IR_COARSE_2K_FREQINC__PRE                        0x4
#define B_SC_RA_RAM_IR_COARSE_2K_KAISINC__A                          0x8200D3
#define B_SC_RA_RAM_IR_COARSE_2K_KAISINC__W                          16
#define B_SC_RA_RAM_IR_COARSE_2K_KAISINC__M                          0xFFFF
#define B_SC_RA_RAM_IR_COARSE_2K_KAISINC__PRE                        0x100

#define B_SC_RA_RAM_IR_COARSE_8K_LENGTH__A                           0x8200D4
#define B_SC_RA_RAM_IR_COARSE_8K_LENGTH__W                           16
#define B_SC_RA_RAM_IR_COARSE_8K_LENGTH__M                           0xFFFF
#define B_SC_RA_RAM_IR_COARSE_8K_LENGTH__PRE                         0x8
#define B_SC_RA_RAM_IR_COARSE_8K_FREQINC__A                          0x8200D5
#define B_SC_RA_RAM_IR_COARSE_8K_FREQINC__W                          16
#define B_SC_RA_RAM_IR_COARSE_8K_FREQINC__M                          0xFFFF
#define B_SC_RA_RAM_IR_COARSE_8K_FREQINC__PRE                        0x8
#define B_SC_RA_RAM_IR_COARSE_8K_KAISINC__A                          0x8200D6
#define B_SC_RA_RAM_IR_COARSE_8K_KAISINC__W                          16
#define B_SC_RA_RAM_IR_COARSE_8K_KAISINC__M                          0xFFFF
#define B_SC_RA_RAM_IR_COARSE_8K_KAISINC__PRE                        0x200

#define B_SC_RA_RAM_IR_FINE_2K_LENGTH__A                             0x8200D7
#define B_SC_RA_RAM_IR_FINE_2K_LENGTH__W                             16
#define B_SC_RA_RAM_IR_FINE_2K_LENGTH__M                             0xFFFF
#define B_SC_RA_RAM_IR_FINE_2K_LENGTH__PRE                           0x9
#define B_SC_RA_RAM_IR_FINE_2K_FREQINC__A                            0x8200D8
#define B_SC_RA_RAM_IR_FINE_2K_FREQINC__W                            16
#define B_SC_RA_RAM_IR_FINE_2K_FREQINC__M                            0xFFFF
#define B_SC_RA_RAM_IR_FINE_2K_FREQINC__PRE                          0x4
#define B_SC_RA_RAM_IR_FINE_2K_KAISINC__A                            0x8200D9
#define B_SC_RA_RAM_IR_FINE_2K_KAISINC__W                            16
#define B_SC_RA_RAM_IR_FINE_2K_KAISINC__M                            0xFFFF
#define B_SC_RA_RAM_IR_FINE_2K_KAISINC__PRE                          0x100

#define B_SC_RA_RAM_IR_FINE_8K_LENGTH__A                             0x8200DA
#define B_SC_RA_RAM_IR_FINE_8K_LENGTH__W                             16
#define B_SC_RA_RAM_IR_FINE_8K_LENGTH__M                             0xFFFF
#define B_SC_RA_RAM_IR_FINE_8K_LENGTH__PRE                           0xB
#define B_SC_RA_RAM_IR_FINE_8K_FREQINC__A                            0x8200DB
#define B_SC_RA_RAM_IR_FINE_8K_FREQINC__W                            16
#define B_SC_RA_RAM_IR_FINE_8K_FREQINC__M                            0xFFFF
#define B_SC_RA_RAM_IR_FINE_8K_FREQINC__PRE                          0x1
#define B_SC_RA_RAM_IR_FINE_8K_KAISINC__A                            0x8200DC
#define B_SC_RA_RAM_IR_FINE_8K_KAISINC__W                            16
#define B_SC_RA_RAM_IR_FINE_8K_KAISINC__M                            0xFFFF
#define B_SC_RA_RAM_IR_FINE_8K_KAISINC__PRE                          0x40

#define B_SC_RA_RAM_ECHO_SHIFT_LIM__A                                0x8200DD
#define B_SC_RA_RAM_ECHO_SHIFT_LIM__W                                16
#define B_SC_RA_RAM_ECHO_SHIFT_LIM__M                                0xFFFF
#define B_SC_RA_RAM_ECHO_SHIFT_LIM__PRE                              0x18
#define B_SC_RA_RAM_ECHO_SHT_LIM__A                                  0x8200DE
#define B_SC_RA_RAM_ECHO_SHT_LIM__W                                  16
#define B_SC_RA_RAM_ECHO_SHT_LIM__M                                  0xFFFF
#define B_SC_RA_RAM_ECHO_SHT_LIM__PRE                                0x1
#define B_SC_RA_RAM_ECHO_SHIFT_TERM__A                               0x8200DF
#define B_SC_RA_RAM_ECHO_SHIFT_TERM__W                               16
#define B_SC_RA_RAM_ECHO_SHIFT_TERM__M                               0xFFFF
#define B_SC_RA_RAM_ECHO_SHIFT_TERM__PRE                             0xCC0
#define   B_SC_RA_RAM_ECHO_SHIFT_TERM_THRES__B                       0
#define   B_SC_RA_RAM_ECHO_SHIFT_TERM_THRES__W                       10
#define   B_SC_RA_RAM_ECHO_SHIFT_TERM_THRES__M                       0x3FF
#define   B_SC_RA_RAM_ECHO_SHIFT_TERM_TIMEOUT__B                     10
#define   B_SC_RA_RAM_ECHO_SHIFT_TERM_TIMEOUT__W                     6
#define   B_SC_RA_RAM_ECHO_SHIFT_TERM_TIMEOUT__M                     0xFC00

#define B_SC_RA_RAM_NI_INIT_2K_PER_LEFT__A                           0x8200E0
#define B_SC_RA_RAM_NI_INIT_2K_PER_LEFT__W                           16
#define B_SC_RA_RAM_NI_INIT_2K_PER_LEFT__M                           0xFFFF
#define B_SC_RA_RAM_NI_INIT_2K_PER_LEFT__PRE                         0x7
#define B_SC_RA_RAM_NI_INIT_2K_PER_RIGHT__A                          0x8200E1
#define B_SC_RA_RAM_NI_INIT_2K_PER_RIGHT__W                          16
#define B_SC_RA_RAM_NI_INIT_2K_PER_RIGHT__M                          0xFFFF
#define B_SC_RA_RAM_NI_INIT_2K_PER_RIGHT__PRE                        0x1
#define B_SC_RA_RAM_NI_INIT_2K_POS_LR__A                             0x8200E2
#define B_SC_RA_RAM_NI_INIT_2K_POS_LR__W                             16
#define B_SC_RA_RAM_NI_INIT_2K_POS_LR__M                             0xFFFF
#define B_SC_RA_RAM_NI_INIT_2K_POS_LR__PRE                           0xE8

#define B_SC_RA_RAM_NI_INIT_8K_PER_LEFT__A                           0x8200E3
#define B_SC_RA_RAM_NI_INIT_8K_PER_LEFT__W                           16
#define B_SC_RA_RAM_NI_INIT_8K_PER_LEFT__M                           0xFFFF
#define B_SC_RA_RAM_NI_INIT_8K_PER_LEFT__PRE                         0xE
#define B_SC_RA_RAM_NI_INIT_8K_PER_RIGHT__A                          0x8200E4
#define B_SC_RA_RAM_NI_INIT_8K_PER_RIGHT__W                          16
#define B_SC_RA_RAM_NI_INIT_8K_PER_RIGHT__M                          0xFFFF
#define B_SC_RA_RAM_NI_INIT_8K_PER_RIGHT__PRE                        0x7
#define B_SC_RA_RAM_NI_INIT_8K_POS_LR__A                             0x8200E5
#define B_SC_RA_RAM_NI_INIT_8K_POS_LR__W                             16
#define B_SC_RA_RAM_NI_INIT_8K_POS_LR__M                             0xFFFF
#define B_SC_RA_RAM_NI_INIT_8K_POS_LR__PRE                           0xA0

#define B_SC_RA_RAM_SAMPLE_RATE_COUNT__A                             0x8200E8
#define B_SC_RA_RAM_SAMPLE_RATE_COUNT__W                             16
#define B_SC_RA_RAM_SAMPLE_RATE_COUNT__M                             0xFFFF
#define B_SC_RA_RAM_SAMPLE_RATE_COUNT__PRE                           0x2
#define B_SC_RA_RAM_SAMPLE_RATE_STEP__A                              0x8200E9
#define B_SC_RA_RAM_SAMPLE_RATE_STEP__W                              16
#define B_SC_RA_RAM_SAMPLE_RATE_STEP__M                              0xFFFF
#define B_SC_RA_RAM_SAMPLE_RATE_STEP__PRE                            0x44C

#define B_SC_RA_RAM_TPS_TIMEOUT_LIM__A                               0x8200EA
#define B_SC_RA_RAM_TPS_TIMEOUT_LIM__W                               16
#define B_SC_RA_RAM_TPS_TIMEOUT_LIM__M                               0xFFFF
#define B_SC_RA_RAM_TPS_TIMEOUT_LIM__PRE                             0xC8
#define B_SC_RA_RAM_TPS_TIMEOUT__A                                   0x8200EB
#define B_SC_RA_RAM_TPS_TIMEOUT__W                                   16
#define B_SC_RA_RAM_TPS_TIMEOUT__M                                   0xFFFF
#define B_SC_RA_RAM_BAND__A                                          0x8200EC
#define B_SC_RA_RAM_BAND__W                                          16
#define B_SC_RA_RAM_BAND__M                                          0xFFFF
#define B_SC_RA_RAM_BAND__PRE                                        0x0
#define   B_SC_RA_RAM_BAND_INTERVAL__B                               0
#define   B_SC_RA_RAM_BAND_INTERVAL__W                               4
#define   B_SC_RA_RAM_BAND_INTERVAL__M                               0xF
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_32__B                     8
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_32__W                     1
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_32__M                     0x100
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_16__B                     9
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_16__W                     1
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_16__M                     0x200
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_8__B                      10
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_8__W                      1
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_8__M                      0x400
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_4__B                      11
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_4__W                      1
#define   B_SC_RA_RAM_BAND_INTERVAL_ENABLE_4__M                      0x800
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_32__B                      12
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_32__W                      1
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_32__M                      0x1000
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_16__B                      13
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_16__W                      1
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_16__M                      0x2000
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_8__B                       14
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_8__W                       1
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_8__M                       0x4000
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_4__B                       15
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_4__W                       1
#define   B_SC_RA_RAM_BAND_HIL_MAR_ENABLE_4__M                       0x8000

#define B_SC_RA_RAM_EC_OC_CRA_HIP_INIT__A                            0x8200ED
#define B_SC_RA_RAM_EC_OC_CRA_HIP_INIT__W                            16
#define B_SC_RA_RAM_EC_OC_CRA_HIP_INIT__M                            0xFFFF
#define B_SC_RA_RAM_EC_OC_CRA_HIP_INIT__PRE                          0xC0
#define B_SC_RA_RAM_REG__AX                                          0x8200F0
#define B_SC_RA_RAM_REG__XSZ                                         2
#define B_SC_RA_RAM_REG__W                                           16
#define B_SC_RA_RAM_REG__M                                           0xFFFF
#define B_SC_RA_RAM_BREAK__A                                         0x8200F2
#define B_SC_RA_RAM_BREAK__W                                         16
#define B_SC_RA_RAM_BREAK__M                                         0xFFFF
#define B_SC_RA_RAM_BOOTCOUNT__A                                     0x8200F3
#define B_SC_RA_RAM_BOOTCOUNT__W                                     16
#define B_SC_RA_RAM_BOOTCOUNT__M                                     0xFFFF

#define B_SC_RA_RAM_LC_ABS_2K__A                                     0x8200F4
#define B_SC_RA_RAM_LC_ABS_2K__W                                     16
#define B_SC_RA_RAM_LC_ABS_2K__M                                     0xFFFF
#define B_SC_RA_RAM_LC_ABS_2K__PRE                                   0x1F
#define B_SC_RA_RAM_LC_ABS_8K__A                                     0x8200F5
#define B_SC_RA_RAM_LC_ABS_8K__W                                     16
#define B_SC_RA_RAM_LC_ABS_8K__M                                     0xFFFF
#define B_SC_RA_RAM_LC_ABS_8K__PRE                                   0x1F
#define B_SC_RA_RAM_NE_ERR_SELECT__A                                 0x8200F6
#define B_SC_RA_RAM_NE_ERR_SELECT__W                                 16
#define B_SC_RA_RAM_NE_ERR_SELECT__M                                 0xFFFF
#define B_SC_RA_RAM_NE_ERR_SELECT__PRE                               0x19
#define B_SC_RA_RAM_CP_GAIN_PEXP_SUB__A                              0x8200F7
#define B_SC_RA_RAM_CP_GAIN_PEXP_SUB__W                              16
#define B_SC_RA_RAM_CP_GAIN_PEXP_SUB__M                              0xFFFF
#define B_SC_RA_RAM_CP_GAIN_PEXP_SUB__PRE                            0x14
#define B_SC_RA_RAM_RELOCK__A                                        0x8200FE
#define B_SC_RA_RAM_RELOCK__W                                        16
#define B_SC_RA_RAM_RELOCK__M                                        0xFFFF
#define B_SC_RA_RAM_STACKUNDERFLOW__A                                0x8200FF
#define B_SC_RA_RAM_STACKUNDERFLOW__W                                16
#define B_SC_RA_RAM_STACKUNDERFLOW__M                                0xFFFF

#define B_SC_RA_RAM_NF_MAXECHOTOKEN__A                               0x820148
#define B_SC_RA_RAM_NF_MAXECHOTOKEN__W                               16
#define B_SC_RA_RAM_NF_MAXECHOTOKEN__M                               0xFFFF
#define B_SC_RA_RAM_NF_PREPOST__A                                    0x820149
#define B_SC_RA_RAM_NF_PREPOST__W                                    16
#define B_SC_RA_RAM_NF_PREPOST__M                                    0xFFFF
#define B_SC_RA_RAM_NF_PREBORDER__A                                  0x82014A
#define B_SC_RA_RAM_NF_PREBORDER__W                                  16
#define B_SC_RA_RAM_NF_PREBORDER__M                                  0xFFFF
#define B_SC_RA_RAM_NF_START__A                                      0x82014B
#define B_SC_RA_RAM_NF_START__W                                      16
#define B_SC_RA_RAM_NF_START__M                                      0xFFFF
#define B_SC_RA_RAM_NF_MINISI__AX                                    0x82014C
#define B_SC_RA_RAM_NF_MINISI__XSZ                                   2
#define B_SC_RA_RAM_NF_MINISI__W                                     16
#define B_SC_RA_RAM_NF_MINISI__M                                     0xFFFF
#define B_SC_RA_RAM_NF_MAXECHO__A                                    0x82014E
#define B_SC_RA_RAM_NF_MAXECHO__W                                    16
#define B_SC_RA_RAM_NF_MAXECHO__M                                    0xFFFF
#define B_SC_RA_RAM_NF_NRECHOES__A                                   0x82014F
#define B_SC_RA_RAM_NF_NRECHOES__W                                   16
#define B_SC_RA_RAM_NF_NRECHOES__M                                   0xFFFF
#define B_SC_RA_RAM_NF_ECHOTABLE__AX                                 0x820150
#define B_SC_RA_RAM_NF_ECHOTABLE__XSZ                                16
#define B_SC_RA_RAM_NF_ECHOTABLE__W                                  16
#define B_SC_RA_RAM_NF_ECHOTABLE__M                                  0xFFFF

#define B_SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__A                        0x8201A0
#define B_SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__W                        16
#define B_SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__M                        0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__PRE                      0x100
#define B_SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__A                        0x8201A1
#define B_SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__W                        16
#define B_SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__M                        0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__PRE                      0x4

#define B_SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__A                           0x8201A2
#define B_SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__W                           16
#define B_SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__M                           0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__PRE                         0x1E2
#define B_SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__A                           0x8201A3
#define B_SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__W                           16
#define B_SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__M                           0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__PRE                         0x4

#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__A                          0x8201A4
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__W                          16
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__M                          0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__PRE                        0x10D
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__A                          0x8201A5
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__W                          16
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__M                          0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__PRE                        0x5

#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__A                       0x8201A6
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__W                       16
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__M                       0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__PRE                     0x17D
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__A                       0x8201A7
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__W                       16
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__M                       0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__PRE                     0x4

#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__A                       0x8201A8
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__W                       16
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__M                       0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__PRE                     0x133
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__A                       0x8201A9
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__W                       16
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__M                       0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__PRE                     0x5

#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__A                          0x8201AA
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__W                          16
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__M                          0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__PRE                        0x114
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__A                          0x8201AB
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__W                          16
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__M                          0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__PRE                        0x5

#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__A                       0x8201AC
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__W                       16
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__M                       0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__PRE                     0x14A
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__A                       0x8201AD
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__W                       16
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__M                       0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__PRE                     0x4

#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__A                       0x8201AE
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__W                       16
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__M                       0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__PRE                     0x1BB
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__A                       0x8201AF
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__W                       16
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__M                       0xFFFF
#define B_SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__PRE                     0x4
#define B_SC_RA_RAM_DRIVER_VERSION__AX                               0x8201FE
#define B_SC_RA_RAM_DRIVER_VERSION__XSZ                              2
#define B_SC_RA_RAM_DRIVER_VERSION__W                                16
#define B_SC_RA_RAM_DRIVER_VERSION__M                                0xFFFF
#define   B_SC_RA_RAM_EVENT0_MIN                                     0x7
#define   B_SC_RA_RAM_EVENT0_FE_CU                                   0x7
#define   B_SC_RA_RAM_EVENT0_CE                                      0xA
#define   B_SC_RA_RAM_EVENT0_EQ                                      0xE
#define   B_SC_RA_RAM_EVENT0_MAX                                     0xF
#define   B_SC_RA_RAM_PROC_LOCKTRACK                                 0x0
#define   B_SC_RA_RAM_PROC_MODE_GUARD                                0x1
#define   B_SC_RA_RAM_PROC_PILOTS                                    0x2
#define   B_SC_RA_RAM_PROC_FESTART_ADJUST                            0x3
#define   B_SC_RA_RAM_PROC_ECHO                                      0x4
#define   B_SC_RA_RAM_PROC_BE_OPT                                    0x5
#define   B_SC_RA_RAM_PROC_LOCK_MON                                  0x6
#define   B_SC_RA_RAM_PROC_EQ                                        0x7
#define   B_SC_RA_RAM_PROC_ECHO_DIVERSITY                            0x8
#define   B_SC_RA_RAM_PROC_MAX                                       0x9

#define B_SC_IF_RAM_TRP_RST__AX                                      0x830000
#define B_SC_IF_RAM_TRP_RST__XSZ                                     2
#define B_SC_IF_RAM_TRP_RST__W                                       12
#define B_SC_IF_RAM_TRP_RST__M                                       0xFFF

#define B_SC_IF_RAM_TRP_BPT0__AX                                     0x830002
#define B_SC_IF_RAM_TRP_BPT0__XSZ                                    2
#define B_SC_IF_RAM_TRP_BPT0__W                                      12
#define B_SC_IF_RAM_TRP_BPT0__M                                      0xFFF

#define B_SC_IF_RAM_TRP_STKU__AX                                     0x830004
#define B_SC_IF_RAM_TRP_STKU__XSZ                                    2
#define B_SC_IF_RAM_TRP_STKU__W                                      12
#define B_SC_IF_RAM_TRP_STKU__M                                      0xFFF

#define B_SC_IF_RAM_VERSION_MA_MI__A                                 0x830FFE
#define B_SC_IF_RAM_VERSION_MA_MI__W                                 12
#define B_SC_IF_RAM_VERSION_MA_MI__M                                 0xFFF

#define B_SC_IF_RAM_VERSION_PATCH__A                                 0x830FFF
#define B_SC_IF_RAM_VERSION_PATCH__W                                 12
#define B_SC_IF_RAM_VERSION_PATCH__M                                 0xFFF

#define B_FE_COMM_EXEC__A                                            0xC00000
#define B_FE_COMM_EXEC__W                                            3
#define B_FE_COMM_EXEC__M                                            0x7
#define   B_FE_COMM_EXEC_CTL__B                                      0
#define   B_FE_COMM_EXEC_CTL__W                                      3
#define   B_FE_COMM_EXEC_CTL__M                                      0x7
#define     B_FE_COMM_EXEC_CTL_STOP                                  0x0
#define     B_FE_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_FE_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_FE_COMM_EXEC_CTL_STEP                                  0x3
#define     B_FE_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_FE_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_FE_COMM_STATE__A                                           0xC00001
#define B_FE_COMM_STATE__W                                           16
#define B_FE_COMM_STATE__M                                           0xFFFF
#define B_FE_COMM_MB__A                                              0xC00002
#define B_FE_COMM_MB__W                                              16
#define B_FE_COMM_MB__M                                              0xFFFF
#define B_FE_COMM_SERVICE0__A                                        0xC00003
#define B_FE_COMM_SERVICE0__W                                        16
#define B_FE_COMM_SERVICE0__M                                        0xFFFF
#define B_FE_COMM_SERVICE1__A                                        0xC00004
#define B_FE_COMM_SERVICE1__W                                        16
#define B_FE_COMM_SERVICE1__M                                        0xFFFF
#define B_FE_COMM_INT_STA__A                                         0xC00007
#define B_FE_COMM_INT_STA__W                                         16
#define B_FE_COMM_INT_STA__M                                         0xFFFF
#define B_FE_COMM_INT_MSK__A                                         0xC00008
#define B_FE_COMM_INT_MSK__W                                         16
#define B_FE_COMM_INT_MSK__M                                         0xFFFF

#define B_FE_AD_SID                                                  0x1

#define B_FE_AD_REG_COMM_EXEC__A                                     0xC10000
#define B_FE_AD_REG_COMM_EXEC__W                                     3
#define B_FE_AD_REG_COMM_EXEC__M                                     0x7
#define   B_FE_AD_REG_COMM_EXEC_CTL__B                               0
#define   B_FE_AD_REG_COMM_EXEC_CTL__W                               3
#define   B_FE_AD_REG_COMM_EXEC_CTL__M                               0x7
#define     B_FE_AD_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_FE_AD_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_FE_AD_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_FE_AD_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_FE_AD_REG_COMM_MB__A                                       0xC10002
#define B_FE_AD_REG_COMM_MB__W                                       2
#define B_FE_AD_REG_COMM_MB__M                                       0x3
#define   B_FE_AD_REG_COMM_MB_CTR__B                                 0
#define   B_FE_AD_REG_COMM_MB_CTR__W                                 1
#define   B_FE_AD_REG_COMM_MB_CTR__M                                 0x1
#define     B_FE_AD_REG_COMM_MB_CTR_OFF                              0x0
#define     B_FE_AD_REG_COMM_MB_CTR_ON                               0x1
#define   B_FE_AD_REG_COMM_MB_OBS__B                                 1
#define   B_FE_AD_REG_COMM_MB_OBS__W                                 1
#define   B_FE_AD_REG_COMM_MB_OBS__M                                 0x2
#define     B_FE_AD_REG_COMM_MB_OBS_OFF                              0x0
#define     B_FE_AD_REG_COMM_MB_OBS_ON                               0x2

#define B_FE_AD_REG_COMM_SERVICE0__A                                 0xC10003
#define B_FE_AD_REG_COMM_SERVICE0__W                                 10
#define B_FE_AD_REG_COMM_SERVICE0__M                                 0x3FF
#define   B_FE_AD_REG_COMM_SERVICE0_FE_AD__B                         0
#define   B_FE_AD_REG_COMM_SERVICE0_FE_AD__W                         1
#define   B_FE_AD_REG_COMM_SERVICE0_FE_AD__M                         0x1

#define B_FE_AD_REG_COMM_SERVICE1__A                                 0xC10004
#define B_FE_AD_REG_COMM_SERVICE1__W                                 11
#define B_FE_AD_REG_COMM_SERVICE1__M                                 0x7FF

#define B_FE_AD_REG_COMM_INT_STA__A                                  0xC10007
#define B_FE_AD_REG_COMM_INT_STA__W                                  2
#define B_FE_AD_REG_COMM_INT_STA__M                                  0x3
#define   B_FE_AD_REG_COMM_INT_STA_ADC_OVERFLOW__B                   0
#define   B_FE_AD_REG_COMM_INT_STA_ADC_OVERFLOW__W                   1
#define   B_FE_AD_REG_COMM_INT_STA_ADC_OVERFLOW__M                   0x1

#define B_FE_AD_REG_COMM_INT_MSK__A                                  0xC10008
#define B_FE_AD_REG_COMM_INT_MSK__W                                  2
#define B_FE_AD_REG_COMM_INT_MSK__M                                  0x3
#define   B_FE_AD_REG_COMM_INT_MSK_ADC_OVERFLOW__B                   0
#define   B_FE_AD_REG_COMM_INT_MSK_ADC_OVERFLOW__W                   1
#define   B_FE_AD_REG_COMM_INT_MSK_ADC_OVERFLOW__M                   0x1

#define B_FE_AD_REG_CUR_SEL__A                                       0xC10010
#define B_FE_AD_REG_CUR_SEL__W                                       2
#define B_FE_AD_REG_CUR_SEL__M                                       0x3
#define   B_FE_AD_REG_CUR_SEL_INIT                                   0x2

#define B_FE_AD_REG_OVERFLOW__A                                      0xC10011
#define B_FE_AD_REG_OVERFLOW__W                                      1
#define B_FE_AD_REG_OVERFLOW__M                                      0x1
#define   B_FE_AD_REG_OVERFLOW_INIT                                  0x0

#define B_FE_AD_REG_FDB_IN__A                                        0xC10012
#define B_FE_AD_REG_FDB_IN__W                                        1
#define B_FE_AD_REG_FDB_IN__M                                        0x1
#define   B_FE_AD_REG_FDB_IN_INIT                                    0x0

#define B_FE_AD_REG_PD__A                                            0xC10013
#define B_FE_AD_REG_PD__W                                            1
#define B_FE_AD_REG_PD__M                                            0x1
#define   B_FE_AD_REG_PD_INIT                                        0x1

#define B_FE_AD_REG_INVEXT__A                                        0xC10014
#define B_FE_AD_REG_INVEXT__W                                        1
#define B_FE_AD_REG_INVEXT__M                                        0x1
#define   B_FE_AD_REG_INVEXT_INIT                                    0x0

#define B_FE_AD_REG_CLKNEG__A                                        0xC10015
#define B_FE_AD_REG_CLKNEG__W                                        1
#define B_FE_AD_REG_CLKNEG__M                                        0x1
#define   B_FE_AD_REG_CLKNEG_INIT                                    0x0

#define B_FE_AD_REG_MON_IN_MUX__A                                    0xC10016
#define B_FE_AD_REG_MON_IN_MUX__W                                    2
#define B_FE_AD_REG_MON_IN_MUX__M                                    0x3
#define   B_FE_AD_REG_MON_IN_MUX_INIT                                0x0

#define B_FE_AD_REG_MON_IN5__A                                       0xC10017
#define B_FE_AD_REG_MON_IN5__W                                       10
#define B_FE_AD_REG_MON_IN5__M                                       0x3FF
#define   B_FE_AD_REG_MON_IN5_INIT                                   0x0

#define B_FE_AD_REG_MON_IN4__A                                       0xC10018
#define B_FE_AD_REG_MON_IN4__W                                       10
#define B_FE_AD_REG_MON_IN4__M                                       0x3FF
#define   B_FE_AD_REG_MON_IN4_INIT                                   0x0

#define B_FE_AD_REG_MON_IN3__A                                       0xC10019
#define B_FE_AD_REG_MON_IN3__W                                       10
#define B_FE_AD_REG_MON_IN3__M                                       0x3FF
#define   B_FE_AD_REG_MON_IN3_INIT                                   0x0

#define B_FE_AD_REG_MON_IN2__A                                       0xC1001A
#define B_FE_AD_REG_MON_IN2__W                                       10
#define B_FE_AD_REG_MON_IN2__M                                       0x3FF
#define   B_FE_AD_REG_MON_IN2_INIT                                   0x0

#define B_FE_AD_REG_MON_IN1__A                                       0xC1001B
#define B_FE_AD_REG_MON_IN1__W                                       10
#define B_FE_AD_REG_MON_IN1__M                                       0x3FF
#define   B_FE_AD_REG_MON_IN1_INIT                                   0x0

#define B_FE_AD_REG_MON_IN0__A                                       0xC1001C
#define B_FE_AD_REG_MON_IN0__W                                       10
#define B_FE_AD_REG_MON_IN0__M                                       0x3FF
#define   B_FE_AD_REG_MON_IN0_INIT                                   0x0

#define B_FE_AD_REG_MON_IN_VAL__A                                    0xC1001D
#define B_FE_AD_REG_MON_IN_VAL__W                                    1
#define B_FE_AD_REG_MON_IN_VAL__M                                    0x1
#define   B_FE_AD_REG_MON_IN_VAL_INIT                                0x0

#define B_FE_AD_REG_CTR_CLK_O__A                                     0xC1001E
#define B_FE_AD_REG_CTR_CLK_O__W                                     1
#define B_FE_AD_REG_CTR_CLK_O__M                                     0x1
#define   B_FE_AD_REG_CTR_CLK_O_INIT                                 0x0

#define B_FE_AD_REG_CTR_CLK_E_O__A                                   0xC1001F
#define B_FE_AD_REG_CTR_CLK_E_O__W                                   1
#define B_FE_AD_REG_CTR_CLK_E_O__M                                   0x1
#define   B_FE_AD_REG_CTR_CLK_E_O_INIT                               0x1

#define B_FE_AD_REG_CTR_VAL_O__A                                     0xC10020
#define B_FE_AD_REG_CTR_VAL_O__W                                     1
#define B_FE_AD_REG_CTR_VAL_O__M                                     0x1
#define   B_FE_AD_REG_CTR_VAL_O_INIT                                 0x0

#define B_FE_AD_REG_CTR_VAL_E_O__A                                   0xC10021
#define B_FE_AD_REG_CTR_VAL_E_O__W                                   1
#define B_FE_AD_REG_CTR_VAL_E_O__M                                   0x1
#define   B_FE_AD_REG_CTR_VAL_E_O_INIT                               0x1

#define B_FE_AD_REG_CTR_DATA_O__A                                    0xC10022
#define B_FE_AD_REG_CTR_DATA_O__W                                    10
#define B_FE_AD_REG_CTR_DATA_O__M                                    0x3FF
#define   B_FE_AD_REG_CTR_DATA_O_INIT                                0x0

#define B_FE_AD_REG_CTR_DATA_E_O__A                                  0xC10023
#define B_FE_AD_REG_CTR_DATA_E_O__W                                  10
#define B_FE_AD_REG_CTR_DATA_E_O__M                                  0x3FF
#define   B_FE_AD_REG_CTR_DATA_E_O_INIT                              0x3FF

#define B_FE_AG_SID                                                  0x2

#define B_FE_AG_REG_COMM_EXEC__A                                     0xC20000
#define B_FE_AG_REG_COMM_EXEC__W                                     3
#define B_FE_AG_REG_COMM_EXEC__M                                     0x7
#define   B_FE_AG_REG_COMM_EXEC_CTL__B                               0
#define   B_FE_AG_REG_COMM_EXEC_CTL__W                               3
#define   B_FE_AG_REG_COMM_EXEC_CTL__M                               0x7
#define     B_FE_AG_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_FE_AG_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_FE_AG_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_FE_AG_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_FE_AG_REG_COMM_STATE__A                                    0xC20001
#define B_FE_AG_REG_COMM_STATE__W                                    4
#define B_FE_AG_REG_COMM_STATE__M                                    0xF

#define B_FE_AG_REG_COMM_MB__A                                       0xC20002
#define B_FE_AG_REG_COMM_MB__W                                       4
#define B_FE_AG_REG_COMM_MB__M                                       0xF
#define   B_FE_AG_REG_COMM_MB_OBS__B                                 1
#define   B_FE_AG_REG_COMM_MB_OBS__W                                 1
#define   B_FE_AG_REG_COMM_MB_OBS__M                                 0x2
#define     B_FE_AG_REG_COMM_MB_OBS_OFF                              0x0
#define     B_FE_AG_REG_COMM_MB_OBS_ON                               0x2
#define   B_FE_AG_REG_COMM_MB_MUX__B                                 2
#define   B_FE_AG_REG_COMM_MB_MUX__W                                 2
#define   B_FE_AG_REG_COMM_MB_MUX__M                                 0xC
#define     B_FE_AG_REG_COMM_MB_MUX_DAT                              0x0
#define     B_FE_AG_REG_COMM_MB_MUX_DAT_PD2                          0x4
#define     B_FE_AG_REG_COMM_MB_MUX_DAT_PD1                          0x8
#define     B_FE_AG_REG_COMM_MB_MUX_DAT_IND_PD1                      0xC

#define B_FE_AG_REG_COMM_SERVICE0__A                                 0xC20003
#define B_FE_AG_REG_COMM_SERVICE0__W                                 10
#define B_FE_AG_REG_COMM_SERVICE0__M                                 0x3FF

#define B_FE_AG_REG_COMM_SERVICE1__A                                 0xC20004
#define B_FE_AG_REG_COMM_SERVICE1__W                                 11
#define B_FE_AG_REG_COMM_SERVICE1__M                                 0x7FF

#define B_FE_AG_REG_COMM_INT_STA__A                                  0xC20007
#define B_FE_AG_REG_COMM_INT_STA__W                                  8
#define B_FE_AG_REG_COMM_INT_STA__M                                  0xFF
#define   B_FE_AG_REG_COMM_INT_STA_DCE_AVE_UPD__B                    0
#define   B_FE_AG_REG_COMM_INT_STA_DCE_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_STA_DCE_AVE_UPD__M                    0x1
#define   B_FE_AG_REG_COMM_INT_STA_ACE_AVE_UPD__B                    1
#define   B_FE_AG_REG_COMM_INT_STA_ACE_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_STA_ACE_AVE_UPD__M                    0x2
#define   B_FE_AG_REG_COMM_INT_STA_CDR_CLP_UPD__B                    2
#define   B_FE_AG_REG_COMM_INT_STA_CDR_CLP_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_STA_CDR_CLP_UPD__M                    0x4
#define   B_FE_AG_REG_COMM_INT_STA_AEC_AVE_UPD__B                    3
#define   B_FE_AG_REG_COMM_INT_STA_AEC_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_STA_AEC_AVE_UPD__M                    0x8
#define   B_FE_AG_REG_COMM_INT_STA_PDA_AVE_UPD__B                    4
#define   B_FE_AG_REG_COMM_INT_STA_PDA_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_STA_PDA_AVE_UPD__M                    0x10
#define   B_FE_AG_REG_COMM_INT_STA_TGA_AVE_UPD__B                    5
#define   B_FE_AG_REG_COMM_INT_STA_TGA_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_STA_TGA_AVE_UPD__M                    0x20
#define   B_FE_AG_REG_COMM_INT_STA_BGC_PGA_UPD__B                    7
#define   B_FE_AG_REG_COMM_INT_STA_BGC_PGA_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_STA_BGC_PGA_UPD__M                    0x80

#define B_FE_AG_REG_COMM_INT_MSK__A                                  0xC20008
#define B_FE_AG_REG_COMM_INT_MSK__W                                  8
#define B_FE_AG_REG_COMM_INT_MSK__M                                  0xFF
#define   B_FE_AG_REG_COMM_INT_MSK_DCE_AVE_UPD__B                    0
#define   B_FE_AG_REG_COMM_INT_MSK_DCE_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_MSK_DCE_AVE_UPD__M                    0x1
#define   B_FE_AG_REG_COMM_INT_MSK_ACE_AVE_UPD__B                    1
#define   B_FE_AG_REG_COMM_INT_MSK_ACE_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_MSK_ACE_AVE_UPD__M                    0x2
#define   B_FE_AG_REG_COMM_INT_MSK_CDR_CLP_UPD__B                    2
#define   B_FE_AG_REG_COMM_INT_MSK_CDR_CLP_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_MSK_CDR_CLP_UPD__M                    0x4
#define   B_FE_AG_REG_COMM_INT_MSK_AEC_AVE_UPD__B                    3
#define   B_FE_AG_REG_COMM_INT_MSK_AEC_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_MSK_AEC_AVE_UPD__M                    0x8
#define   B_FE_AG_REG_COMM_INT_MSK_PDA_AVE_UPD__B                    4
#define   B_FE_AG_REG_COMM_INT_MSK_PDA_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_MSK_PDA_AVE_UPD__M                    0x10
#define   B_FE_AG_REG_COMM_INT_MSK_TGA_AVE_UPD__B                    5
#define   B_FE_AG_REG_COMM_INT_MSK_TGA_AVE_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_MSK_TGA_AVE_UPD__M                    0x20
#define   B_FE_AG_REG_COMM_INT_MSK_BGC_PGA_UPD__B                    7
#define   B_FE_AG_REG_COMM_INT_MSK_BGC_PGA_UPD__W                    1
#define   B_FE_AG_REG_COMM_INT_MSK_BGC_PGA_UPD__M                    0x80

#define B_FE_AG_REG_AG_MODE_LOP__A                                   0xC20010
#define B_FE_AG_REG_AG_MODE_LOP__W                                   15
#define B_FE_AG_REG_AG_MODE_LOP__M                                   0x7FFF
#define   B_FE_AG_REG_AG_MODE_LOP_INIT                               0x81E

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_0__B                          0
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_0__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_0__M                          0x1
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_0_ENABLE                    0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_0_DISABLE                   0x1

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_1__B                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_1__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_1__M                          0x2
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_1_STATIC                    0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_1_DYNAMIC                   0x2

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_2__B                          2
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_2__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_2__M                          0x4
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_2_AVE_B                     0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_2_AVE_CB                    0x4

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_3__B                          3
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_3__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_3__M                          0x8
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_3_AVE_B                     0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_3_AVE_CB                    0x8

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_4__B                          4
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_4__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_4__M                          0x10
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_4_STATIC                    0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_4_DYNAMIC                   0x10

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_5__B                          5
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_5__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_5__M                          0x20
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_5_STATIC                    0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_5_DYNAMIC                   0x20

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_6__B                          6
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_6__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_6__M                          0x40
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_6_AVE_B                     0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_6_AVE_CB                    0x40

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_7__B                          7
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_7__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_7__M                          0x80
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_7_DYNAMIC                   0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_7_STATIC                    0x80

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_8__B                          8
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_8__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_8__M                          0x100
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_8_AVE_B                     0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_8_AVE_CB                    0x100

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_B__B                          11
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_B__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_B__M                          0x800
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_B_START                     0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_B_ALWAYS                    0x800

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_9__B                          9
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_9__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_9__M                          0x200
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_9_STATIC                    0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_9_DYNAMIC                   0x200

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_C__B                          12
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_C__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_C__M                          0x1000
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_C_STATIC                    0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_C_DYNAMIC                   0x1000

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_D__B                          13
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_D__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_D__M                          0x2000
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_D_START                     0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_D_ALWAYS                    0x2000

#define   B_FE_AG_REG_AG_MODE_LOP_MODE_E__B                          14
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_E__W                          1
#define   B_FE_AG_REG_AG_MODE_LOP_MODE_E__M                          0x4000
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_E_STATIC                    0x0
#define     B_FE_AG_REG_AG_MODE_LOP_MODE_E_DYNAMIC                   0x4000

#define B_FE_AG_REG_AG_MODE_HIP__A                                   0xC20011
#define B_FE_AG_REG_AG_MODE_HIP__W                                   5
#define B_FE_AG_REG_AG_MODE_HIP__M                                   0x1F
#define   B_FE_AG_REG_AG_MODE_HIP_INIT                               0x0

#define   B_FE_AG_REG_AG_MODE_HIP_MODE_G__B                          0
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_G__W                          1
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_G__M                          0x1
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_G_OUTPUT                    0x0
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_G_ENABLE                    0x1

#define   B_FE_AG_REG_AG_MODE_HIP_MODE_H__B                          1
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_H__W                          1
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_H__M                          0x2
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_H_OUTPUT                    0x0
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_H_ENABLE                    0x2

#define   B_FE_AG_REG_AG_MODE_HIP_MODE_I__B                          2
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_I__W                          1
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_I__M                          0x4
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_I_GRAPH1                    0x0
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_I_GRAPH2                    0x4

#define   B_FE_AG_REG_AG_MODE_HIP_MODE_J__B                          3
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_J__W                          1
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_J__M                          0x8
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_J_STATIC                    0x0
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_J_DYNAMIC                   0x8

#define   B_FE_AG_REG_AG_MODE_HIP_MODE_K__B                          4
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_K__W                          1
#define   B_FE_AG_REG_AG_MODE_HIP_MODE_K__M                          0x10
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_K_GRAPH1                    0x0
#define     B_FE_AG_REG_AG_MODE_HIP_MODE_K_GRAPH2                    0x10

#define B_FE_AG_REG_AG_PGA_MODE__A                                   0xC20012
#define B_FE_AG_REG_AG_PGA_MODE__W                                   3
#define B_FE_AG_REG_AG_PGA_MODE__M                                   0x7
#define   B_FE_AG_REG_AG_PGA_MODE_INIT                               0x3
#define   B_FE_AG_REG_AG_PGA_MODE_PFY_PCY_AFY_REN                    0x0
#define   B_FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFY_REN                    0x1
#define   B_FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFN_REN                    0x2
#define   B_FE_AG_REG_AG_PGA_MODE_PFN_PCY_AFY_REN                    0x3
#define   B_FE_AG_REG_AG_PGA_MODE_PFY_PCY_AFY_REY                    0x4
#define   B_FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFY_REY                    0x5
#define   B_FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFN_REY                    0x6
#define   B_FE_AG_REG_AG_PGA_MODE_PFN_PCY_AFY_REY                    0x7

#define B_FE_AG_REG_AG_AGC_SIO__A                                    0xC20013
#define B_FE_AG_REG_AG_AGC_SIO__W                                    2
#define B_FE_AG_REG_AG_AGC_SIO__M                                    0x3
#define   B_FE_AG_REG_AG_AGC_SIO_INIT                                0x3

#define   B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_1__B                        0
#define   B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_1__W                        1
#define   B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_1__M                        0x1
#define     B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_1_OUTPUT                  0x0
#define     B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_1_INPUT                   0x1

#define   B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__B                        1
#define   B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__W                        1
#define   B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__M                        0x2
#define     B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_2_OUTPUT                  0x0
#define     B_FE_AG_REG_AG_AGC_SIO_AGC_SIO_2_INPUT                   0x2

#define B_FE_AG_REG_AG_AGC_USR_DAT__A                                0xC20014
#define B_FE_AG_REG_AG_AGC_USR_DAT__W                                2
#define B_FE_AG_REG_AG_AGC_USR_DAT__M                                0x3
#define   B_FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_1__B                    0
#define   B_FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_1__W                    1
#define   B_FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_1__M                    0x1
#define   B_FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_2__B                    1
#define   B_FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_2__W                    1
#define   B_FE_AG_REG_AG_AGC_USR_DAT_USR_DAT_2__M                    0x2

#define B_FE_AG_REG_AG_PWD__A                                        0xC20015
#define B_FE_AG_REG_AG_PWD__W                                        5
#define B_FE_AG_REG_AG_PWD__M                                        0x1F
#define   B_FE_AG_REG_AG_PWD_INIT                                    0x6

#define   B_FE_AG_REG_AG_PWD_PWD_PD1__B                              0
#define   B_FE_AG_REG_AG_PWD_PWD_PD1__W                              1
#define   B_FE_AG_REG_AG_PWD_PWD_PD1__M                              0x1
#define     B_FE_AG_REG_AG_PWD_PWD_PD1_DISABLE                       0x0
#define     B_FE_AG_REG_AG_PWD_PWD_PD1_ENABLE                        0x1

#define   B_FE_AG_REG_AG_PWD_PWD_PD2__B                              1
#define   B_FE_AG_REG_AG_PWD_PWD_PD2__W                              1
#define   B_FE_AG_REG_AG_PWD_PWD_PD2__M                              0x2
#define     B_FE_AG_REG_AG_PWD_PWD_PD2_DISABLE                       0x0
#define     B_FE_AG_REG_AG_PWD_PWD_PD2_ENABLE                        0x2

#define   B_FE_AG_REG_AG_PWD_PWD_PGA_F__B                            2
#define   B_FE_AG_REG_AG_PWD_PWD_PGA_F__W                            1
#define   B_FE_AG_REG_AG_PWD_PWD_PGA_F__M                            0x4
#define     B_FE_AG_REG_AG_PWD_PWD_PGA_F_DISABLE                     0x0
#define     B_FE_AG_REG_AG_PWD_PWD_PGA_F_ENABLE                      0x4

#define   B_FE_AG_REG_AG_PWD_PWD_PGA_C__B                            3
#define   B_FE_AG_REG_AG_PWD_PWD_PGA_C__W                            1
#define   B_FE_AG_REG_AG_PWD_PWD_PGA_C__M                            0x8
#define     B_FE_AG_REG_AG_PWD_PWD_PGA_C_DISABLE                     0x0
#define     B_FE_AG_REG_AG_PWD_PWD_PGA_C_ENABLE                      0x8

#define   B_FE_AG_REG_AG_PWD_PWD_AAF__B                              4
#define   B_FE_AG_REG_AG_PWD_PWD_AAF__W                              1
#define   B_FE_AG_REG_AG_PWD_PWD_AAF__M                              0x10
#define     B_FE_AG_REG_AG_PWD_PWD_AAF_DISABLE                       0x0
#define     B_FE_AG_REG_AG_PWD_PWD_AAF_ENABLE                        0x10

#define B_FE_AG_REG_DCE_AUR_CNT__A                                   0xC20016
#define B_FE_AG_REG_DCE_AUR_CNT__W                                   5
#define B_FE_AG_REG_DCE_AUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_DCE_AUR_CNT_INIT                               0x10

#define B_FE_AG_REG_DCE_RUR_CNT__A                                   0xC20017
#define B_FE_AG_REG_DCE_RUR_CNT__W                                   5
#define B_FE_AG_REG_DCE_RUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_DCE_RUR_CNT_INIT                               0x0

#define B_FE_AG_REG_DCE_AVE_DAT__A                                   0xC20018
#define B_FE_AG_REG_DCE_AVE_DAT__W                                   10
#define B_FE_AG_REG_DCE_AVE_DAT__M                                   0x3FF

#define B_FE_AG_REG_DEC_AVE_WRI__A                                   0xC20019
#define B_FE_AG_REG_DEC_AVE_WRI__W                                   10
#define B_FE_AG_REG_DEC_AVE_WRI__M                                   0x3FF
#define   B_FE_AG_REG_DEC_AVE_WRI_INIT                               0x0

#define B_FE_AG_REG_ACE_AUR_CNT__A                                   0xC2001A
#define B_FE_AG_REG_ACE_AUR_CNT__W                                   5
#define B_FE_AG_REG_ACE_AUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_ACE_AUR_CNT_INIT                               0xE

#define B_FE_AG_REG_ACE_RUR_CNT__A                                   0xC2001B
#define B_FE_AG_REG_ACE_RUR_CNT__W                                   5
#define B_FE_AG_REG_ACE_RUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_ACE_RUR_CNT_INIT                               0x0

#define B_FE_AG_REG_ACE_AVE_DAT__A                                   0xC2001C
#define B_FE_AG_REG_ACE_AVE_DAT__W                                   10
#define B_FE_AG_REG_ACE_AVE_DAT__M                                   0x3FF

#define B_FE_AG_REG_AEC_AVE_INC__A                                   0xC2001D
#define B_FE_AG_REG_AEC_AVE_INC__W                                   10
#define B_FE_AG_REG_AEC_AVE_INC__M                                   0x3FF
#define   B_FE_AG_REG_AEC_AVE_INC_INIT                               0x0

#define B_FE_AG_REG_AEC_AVE_DAT__A                                   0xC2001E
#define B_FE_AG_REG_AEC_AVE_DAT__W                                   10
#define B_FE_AG_REG_AEC_AVE_DAT__M                                   0x3FF

#define B_FE_AG_REG_AEC_CLP_LVL__A                                   0xC2001F
#define B_FE_AG_REG_AEC_CLP_LVL__W                                   16
#define B_FE_AG_REG_AEC_CLP_LVL__M                                   0xFFFF
#define   B_FE_AG_REG_AEC_CLP_LVL_INIT                               0x0

#define B_FE_AG_REG_CDR_RUR_CNT__A                                   0xC20020
#define B_FE_AG_REG_CDR_RUR_CNT__W                                   5
#define B_FE_AG_REG_CDR_RUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_CDR_RUR_CNT_INIT                               0x10

#define B_FE_AG_REG_CDR_CLP_DAT__A                                   0xC20021
#define B_FE_AG_REG_CDR_CLP_DAT__W                                   16
#define B_FE_AG_REG_CDR_CLP_DAT__M                                   0xFFFF

#define B_FE_AG_REG_CDR_CLP_POS__A                                   0xC20022
#define B_FE_AG_REG_CDR_CLP_POS__W                                   10
#define B_FE_AG_REG_CDR_CLP_POS__M                                   0x3FF
#define   B_FE_AG_REG_CDR_CLP_POS_INIT                               0x16A

#define B_FE_AG_REG_CDR_CLP_NEG__A                                   0xC20023
#define B_FE_AG_REG_CDR_CLP_NEG__W                                   10
#define B_FE_AG_REG_CDR_CLP_NEG__M                                   0x3FF
#define   B_FE_AG_REG_CDR_CLP_NEG_INIT                               0x296

#define B_FE_AG_REG_EGC_RUR_CNT__A                                   0xC20024
#define B_FE_AG_REG_EGC_RUR_CNT__W                                   5
#define B_FE_AG_REG_EGC_RUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_EGC_RUR_CNT_INIT                               0x0

#define B_FE_AG_REG_EGC_SET_LVL__A                                   0xC20025
#define B_FE_AG_REG_EGC_SET_LVL__W                                   9
#define B_FE_AG_REG_EGC_SET_LVL__M                                   0x1FF
#define   B_FE_AG_REG_EGC_SET_LVL_INIT                               0x46

#define B_FE_AG_REG_EGC_FLA_RGN__A                                   0xC20026
#define B_FE_AG_REG_EGC_FLA_RGN__W                                   9
#define B_FE_AG_REG_EGC_FLA_RGN__M                                   0x1FF
#define   B_FE_AG_REG_EGC_FLA_RGN_INIT                               0x4

#define B_FE_AG_REG_EGC_SLO_RGN__A                                   0xC20027
#define B_FE_AG_REG_EGC_SLO_RGN__W                                   9
#define B_FE_AG_REG_EGC_SLO_RGN__M                                   0x1FF
#define   B_FE_AG_REG_EGC_SLO_RGN_INIT                               0x1F

#define B_FE_AG_REG_EGC_JMP_PSN__A                                   0xC20028
#define B_FE_AG_REG_EGC_JMP_PSN__W                                   4
#define B_FE_AG_REG_EGC_JMP_PSN__M                                   0xF
#define   B_FE_AG_REG_EGC_JMP_PSN_INIT                               0x0

#define B_FE_AG_REG_EGC_FLA_INC__A                                   0xC20029
#define B_FE_AG_REG_EGC_FLA_INC__W                                   16
#define B_FE_AG_REG_EGC_FLA_INC__M                                   0xFFFF
#define   B_FE_AG_REG_EGC_FLA_INC_INIT                               0x0

#define B_FE_AG_REG_EGC_FLA_DEC__A                                   0xC2002A
#define B_FE_AG_REG_EGC_FLA_DEC__W                                   16
#define B_FE_AG_REG_EGC_FLA_DEC__M                                   0xFFFF
#define   B_FE_AG_REG_EGC_FLA_DEC_INIT                               0x0

#define B_FE_AG_REG_EGC_SLO_INC__A                                   0xC2002B
#define B_FE_AG_REG_EGC_SLO_INC__W                                   16
#define B_FE_AG_REG_EGC_SLO_INC__M                                   0xFFFF
#define   B_FE_AG_REG_EGC_SLO_INC_INIT                               0x3

#define B_FE_AG_REG_EGC_SLO_DEC__A                                   0xC2002C
#define B_FE_AG_REG_EGC_SLO_DEC__W                                   16
#define B_FE_AG_REG_EGC_SLO_DEC__M                                   0xFFFF
#define   B_FE_AG_REG_EGC_SLO_DEC_INIT                               0x3

#define B_FE_AG_REG_EGC_FAS_INC__A                                   0xC2002D
#define B_FE_AG_REG_EGC_FAS_INC__W                                   16
#define B_FE_AG_REG_EGC_FAS_INC__M                                   0xFFFF
#define   B_FE_AG_REG_EGC_FAS_INC_INIT                               0xE

#define B_FE_AG_REG_EGC_FAS_DEC__A                                   0xC2002E
#define B_FE_AG_REG_EGC_FAS_DEC__W                                   16
#define B_FE_AG_REG_EGC_FAS_DEC__M                                   0xFFFF
#define   B_FE_AG_REG_EGC_FAS_DEC_INIT                               0xE

#define B_FE_AG_REG_EGC_MAP_DAT__A                                   0xC2002F
#define B_FE_AG_REG_EGC_MAP_DAT__W                                   16
#define B_FE_AG_REG_EGC_MAP_DAT__M                                   0xFFFF

#define B_FE_AG_REG_PM1_AGC_WRI__A                                   0xC20030
#define B_FE_AG_REG_PM1_AGC_WRI__W                                   11
#define B_FE_AG_REG_PM1_AGC_WRI__M                                   0x7FF
#define   B_FE_AG_REG_PM1_AGC_WRI_INIT                               0x0

#define B_FE_AG_REG_GC1_AGC_RIC__A                                   0xC20031
#define B_FE_AG_REG_GC1_AGC_RIC__W                                   16
#define B_FE_AG_REG_GC1_AGC_RIC__M                                   0xFFFF
#define   B_FE_AG_REG_GC1_AGC_RIC_INIT                               0x64

#define B_FE_AG_REG_GC1_AGC_OFF__A                                   0xC20032
#define B_FE_AG_REG_GC1_AGC_OFF__W                                   16
#define B_FE_AG_REG_GC1_AGC_OFF__M                                   0xFFFF
#define   B_FE_AG_REG_GC1_AGC_OFF_INIT                               0xFEC8

#define B_FE_AG_REG_GC1_AGC_MAX__A                                   0xC20033
#define B_FE_AG_REG_GC1_AGC_MAX__W                                   10
#define B_FE_AG_REG_GC1_AGC_MAX__M                                   0x3FF
#define   B_FE_AG_REG_GC1_AGC_MAX_INIT                               0x1FF

#define B_FE_AG_REG_GC1_AGC_MIN__A                                   0xC20034
#define B_FE_AG_REG_GC1_AGC_MIN__W                                   10
#define B_FE_AG_REG_GC1_AGC_MIN__M                                   0x3FF
#define   B_FE_AG_REG_GC1_AGC_MIN_INIT                               0x200

#define B_FE_AG_REG_GC1_AGC_DAT__A                                   0xC20035
#define B_FE_AG_REG_GC1_AGC_DAT__W                                   10
#define B_FE_AG_REG_GC1_AGC_DAT__M                                   0x3FF

#define B_FE_AG_REG_PM2_AGC_WRI__A                                   0xC20036
#define B_FE_AG_REG_PM2_AGC_WRI__W                                   11
#define B_FE_AG_REG_PM2_AGC_WRI__M                                   0x7FF
#define   B_FE_AG_REG_PM2_AGC_WRI_INIT                               0x0

#define B_FE_AG_REG_GC2_AGC_RIC__A                                   0xC20037
#define B_FE_AG_REG_GC2_AGC_RIC__W                                   16
#define B_FE_AG_REG_GC2_AGC_RIC__M                                   0xFFFF
#define   B_FE_AG_REG_GC2_AGC_RIC_INIT                               0x64

#define B_FE_AG_REG_GC2_AGC_OFF__A                                   0xC20038
#define B_FE_AG_REG_GC2_AGC_OFF__W                                   16
#define B_FE_AG_REG_GC2_AGC_OFF__M                                   0xFFFF
#define   B_FE_AG_REG_GC2_AGC_OFF_INIT                               0xFEC8

#define B_FE_AG_REG_GC2_AGC_MAX__A                                   0xC20039
#define B_FE_AG_REG_GC2_AGC_MAX__W                                   10
#define B_FE_AG_REG_GC2_AGC_MAX__M                                   0x3FF
#define   B_FE_AG_REG_GC2_AGC_MAX_INIT                               0x1FF

#define B_FE_AG_REG_GC2_AGC_MIN__A                                   0xC2003A
#define B_FE_AG_REG_GC2_AGC_MIN__W                                   10
#define B_FE_AG_REG_GC2_AGC_MIN__M                                   0x3FF
#define   B_FE_AG_REG_GC2_AGC_MIN_INIT                               0x200

#define B_FE_AG_REG_GC2_AGC_DAT__A                                   0xC2003B
#define B_FE_AG_REG_GC2_AGC_DAT__W                                   10
#define B_FE_AG_REG_GC2_AGC_DAT__M                                   0x3FF

#define B_FE_AG_REG_IND_WIN__A                                       0xC2003C
#define B_FE_AG_REG_IND_WIN__W                                       5
#define B_FE_AG_REG_IND_WIN__M                                       0x1F
#define   B_FE_AG_REG_IND_WIN_INIT                                   0x0

#define B_FE_AG_REG_IND_THD_LOL__A                                   0xC2003D
#define B_FE_AG_REG_IND_THD_LOL__W                                   6
#define B_FE_AG_REG_IND_THD_LOL__M                                   0x3F
#define   B_FE_AG_REG_IND_THD_LOL_INIT                               0x5

#define B_FE_AG_REG_IND_THD_HIL__A                                   0xC2003E
#define B_FE_AG_REG_IND_THD_HIL__W                                   6
#define B_FE_AG_REG_IND_THD_HIL__M                                   0x3F
#define   B_FE_AG_REG_IND_THD_HIL_INIT                               0xF

#define B_FE_AG_REG_IND_DEL__A                                       0xC2003F
#define B_FE_AG_REG_IND_DEL__W                                       7
#define B_FE_AG_REG_IND_DEL__M                                       0x7F
#define   B_FE_AG_REG_IND_DEL_INIT                                   0x32

#define B_FE_AG_REG_IND_PD1_WRI__A                                   0xC20040
#define B_FE_AG_REG_IND_PD1_WRI__W                                   6
#define B_FE_AG_REG_IND_PD1_WRI__M                                   0x3F
#define   B_FE_AG_REG_IND_PD1_WRI_INIT                               0x1E

#define B_FE_AG_REG_PDA_AUR_CNT__A                                   0xC20041
#define B_FE_AG_REG_PDA_AUR_CNT__W                                   5
#define B_FE_AG_REG_PDA_AUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_PDA_AUR_CNT_INIT                               0x10

#define B_FE_AG_REG_PDA_RUR_CNT__A                                   0xC20042
#define B_FE_AG_REG_PDA_RUR_CNT__W                                   5
#define B_FE_AG_REG_PDA_RUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_PDA_RUR_CNT_INIT                               0x0

#define B_FE_AG_REG_PDA_AVE_DAT__A                                   0xC20043
#define B_FE_AG_REG_PDA_AVE_DAT__W                                   6
#define B_FE_AG_REG_PDA_AVE_DAT__M                                   0x3F

#define B_FE_AG_REG_PDC_RUR_CNT__A                                   0xC20044
#define B_FE_AG_REG_PDC_RUR_CNT__W                                   5
#define B_FE_AG_REG_PDC_RUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_PDC_RUR_CNT_INIT                               0x0

#define B_FE_AG_REG_PDC_SET_LVL__A                                   0xC20045
#define B_FE_AG_REG_PDC_SET_LVL__W                                   6
#define B_FE_AG_REG_PDC_SET_LVL__M                                   0x3F
#define   B_FE_AG_REG_PDC_SET_LVL_INIT                               0x10

#define B_FE_AG_REG_PDC_FLA_RGN__A                                   0xC20046
#define B_FE_AG_REG_PDC_FLA_RGN__W                                   6
#define B_FE_AG_REG_PDC_FLA_RGN__M                                   0x3F
#define   B_FE_AG_REG_PDC_FLA_RGN_INIT                               0x0

#define B_FE_AG_REG_PDC_JMP_PSN__A                                   0xC20047
#define B_FE_AG_REG_PDC_JMP_PSN__W                                   3
#define B_FE_AG_REG_PDC_JMP_PSN__M                                   0x7
#define   B_FE_AG_REG_PDC_JMP_PSN_INIT                               0x0

#define B_FE_AG_REG_PDC_FLA_STP__A                                   0xC20048
#define B_FE_AG_REG_PDC_FLA_STP__W                                   16
#define B_FE_AG_REG_PDC_FLA_STP__M                                   0xFFFF
#define   B_FE_AG_REG_PDC_FLA_STP_INIT                               0x0

#define B_FE_AG_REG_PDC_SLO_STP__A                                   0xC20049
#define B_FE_AG_REG_PDC_SLO_STP__W                                   16
#define B_FE_AG_REG_PDC_SLO_STP__M                                   0xFFFF
#define   B_FE_AG_REG_PDC_SLO_STP_INIT                               0x1

#define B_FE_AG_REG_PDC_PD2_WRI__A                                   0xC2004A
#define B_FE_AG_REG_PDC_PD2_WRI__W                                   6
#define B_FE_AG_REG_PDC_PD2_WRI__M                                   0x3F
#define   B_FE_AG_REG_PDC_PD2_WRI_INIT                               0x1F

#define B_FE_AG_REG_PDC_MAP_DAT__A                                   0xC2004B
#define B_FE_AG_REG_PDC_MAP_DAT__W                                   6
#define B_FE_AG_REG_PDC_MAP_DAT__M                                   0x3F

#define B_FE_AG_REG_PDC_MAX__A                                       0xC2004C
#define B_FE_AG_REG_PDC_MAX__W                                       6
#define B_FE_AG_REG_PDC_MAX__M                                       0x3F
#define   B_FE_AG_REG_PDC_MAX_INIT                                   0x2

#define B_FE_AG_REG_TGA_AUR_CNT__A                                   0xC2004D
#define B_FE_AG_REG_TGA_AUR_CNT__W                                   5
#define B_FE_AG_REG_TGA_AUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_TGA_AUR_CNT_INIT                               0x10

#define B_FE_AG_REG_TGA_RUR_CNT__A                                   0xC2004E
#define B_FE_AG_REG_TGA_RUR_CNT__W                                   5
#define B_FE_AG_REG_TGA_RUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_TGA_RUR_CNT_INIT                               0x0

#define B_FE_AG_REG_TGA_AVE_DAT__A                                   0xC2004F
#define B_FE_AG_REG_TGA_AVE_DAT__W                                   6
#define B_FE_AG_REG_TGA_AVE_DAT__M                                   0x3F

#define B_FE_AG_REG_TGC_RUR_CNT__A                                   0xC20050
#define B_FE_AG_REG_TGC_RUR_CNT__W                                   5
#define B_FE_AG_REG_TGC_RUR_CNT__M                                   0x1F
#define   B_FE_AG_REG_TGC_RUR_CNT_INIT                               0x0

#define B_FE_AG_REG_TGC_SET_LVL__A                                   0xC20051
#define B_FE_AG_REG_TGC_SET_LVL__W                                   6
#define B_FE_AG_REG_TGC_SET_LVL__M                                   0x3F
#define   B_FE_AG_REG_TGC_SET_LVL_INIT                               0x18

#define B_FE_AG_REG_TGC_FLA_RGN__A                                   0xC20052
#define B_FE_AG_REG_TGC_FLA_RGN__W                                   6
#define B_FE_AG_REG_TGC_FLA_RGN__M                                   0x3F
#define   B_FE_AG_REG_TGC_FLA_RGN_INIT                               0x0

#define B_FE_AG_REG_TGC_JMP_PSN__A                                   0xC20053
#define B_FE_AG_REG_TGC_JMP_PSN__W                                   4
#define B_FE_AG_REG_TGC_JMP_PSN__M                                   0xF
#define   B_FE_AG_REG_TGC_JMP_PSN_INIT                               0x0

#define B_FE_AG_REG_TGC_FLA_STP__A                                   0xC20054
#define B_FE_AG_REG_TGC_FLA_STP__W                                   16
#define B_FE_AG_REG_TGC_FLA_STP__M                                   0xFFFF
#define   B_FE_AG_REG_TGC_FLA_STP_INIT                               0x0

#define B_FE_AG_REG_TGC_SLO_STP__A                                   0xC20055
#define B_FE_AG_REG_TGC_SLO_STP__W                                   16
#define B_FE_AG_REG_TGC_SLO_STP__M                                   0xFFFF
#define   B_FE_AG_REG_TGC_SLO_STP_INIT                               0x1

#define B_FE_AG_REG_TGC_MAP_DAT__A                                   0xC20056
#define B_FE_AG_REG_TGC_MAP_DAT__W                                   10
#define B_FE_AG_REG_TGC_MAP_DAT__M                                   0x3FF

#define B_FE_AG_REG_FGM_WRI__A                                       0xC20061
#define B_FE_AG_REG_FGM_WRI__W                                       10
#define B_FE_AG_REG_FGM_WRI__M                                       0x3FF
#define   B_FE_AG_REG_FGM_WRI_INIT                                   0x80

#define B_FE_AG_REG_BGC_FGC_WRI__A                                   0xC20068
#define B_FE_AG_REG_BGC_FGC_WRI__W                                   4
#define B_FE_AG_REG_BGC_FGC_WRI__M                                   0xF
#define   B_FE_AG_REG_BGC_FGC_WRI_INIT                               0x0

#define B_FE_AG_REG_BGC_CGC_WRI__A                                   0xC20069
#define B_FE_AG_REG_BGC_CGC_WRI__W                                   2
#define B_FE_AG_REG_BGC_CGC_WRI__M                                   0x3
#define   B_FE_AG_REG_BGC_CGC_WRI_INIT                               0x0

#define B_FE_AG_REG_BGC_THD_LVL__A                                   0xC2006B
#define B_FE_AG_REG_BGC_THD_LVL__W                                   4
#define B_FE_AG_REG_BGC_THD_LVL__M                                   0xF
#define   B_FE_AG_REG_BGC_THD_LVL_INIT                               0xF

#define B_FE_AG_REG_BGC_THD_INC__A                                   0xC2006C
#define B_FE_AG_REG_BGC_THD_INC__W                                   4
#define B_FE_AG_REG_BGC_THD_INC__M                                   0xF
#define   B_FE_AG_REG_BGC_THD_INC_INIT                               0x8

#define B_FE_AG_REG_BGC_DAT__A                                       0xC2006D
#define B_FE_AG_REG_BGC_DAT__W                                       4
#define B_FE_AG_REG_BGC_DAT__M                                       0xF

#define B_FE_AG_REG_IND_PD1_COM__A                                   0xC2006E
#define B_FE_AG_REG_IND_PD1_COM__W                                   6
#define B_FE_AG_REG_IND_PD1_COM__M                                   0x3F
#define   B_FE_AG_REG_IND_PD1_COM_INIT                               0x7

#define B_FE_AG_REG_AG_AGC_BUF__A                                    0xC2006F
#define B_FE_AG_REG_AG_AGC_BUF__W                                    2
#define B_FE_AG_REG_AG_AGC_BUF__M                                    0x3
#define   B_FE_AG_REG_AG_AGC_BUF_INIT                                0x3

#define   B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_1__B                        0
#define   B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_1__W                        1
#define   B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_1__M                        0x1
#define     B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_1_SLOW                    0x0
#define     B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_1_FAST                    0x1

#define   B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_2__B                        1
#define   B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_2__W                        1
#define   B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_2__M                        0x2
#define     B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_2_SLOW                    0x0
#define     B_FE_AG_REG_AG_AGC_BUF_AGC_BUF_2_FAST                    0x2

#define B_FE_AG_REG_PMX_SPE__A                                       0xC20070
#define B_FE_AG_REG_PMX_SPE__W                                       3
#define B_FE_AG_REG_PMX_SPE__M                                       0x7
#define   B_FE_AG_REG_PMX_SPE_INIT                                   0x1
#define   B_FE_AG_REG_PMX_SPE_48MHZ_DIVIDE_BY_1                      0x0
#define   B_FE_AG_REG_PMX_SPE_48MHZ_DIVIDE_BY_2                      0x1
#define   B_FE_AG_REG_PMX_SPE_48MHZ_DIVIDE_BY_3                      0x2
#define   B_FE_AG_REG_PMX_SPE_48MHZ_DIVIDE_BY_4                      0x3
#define   B_FE_AG_REG_PMX_SPE_48MHZ_DIVIDE_BY_5                      0x4
#define   B_FE_AG_REG_PMX_SPE_48MHZ_DIVIDE_BY_6                      0x5
#define   B_FE_AG_REG_PMX_SPE_48MHZ_DIVIDE_BY_7                      0x6
#define   B_FE_AG_REG_PMX_SPE_48MHZ_DIVIDE_BY_8                      0x7

#define B_FE_FS_SID                                                  0x3

#define B_FE_FS_REG_COMM_EXEC__A                                     0xC30000
#define B_FE_FS_REG_COMM_EXEC__W                                     3
#define B_FE_FS_REG_COMM_EXEC__M                                     0x7
#define   B_FE_FS_REG_COMM_EXEC_CTL__B                               0
#define   B_FE_FS_REG_COMM_EXEC_CTL__W                               3
#define   B_FE_FS_REG_COMM_EXEC_CTL__M                               0x7
#define     B_FE_FS_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_FE_FS_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_FE_FS_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_FE_FS_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_FE_FS_REG_COMM_STATE__A                                    0xC30001
#define B_FE_FS_REG_COMM_STATE__W                                    4
#define B_FE_FS_REG_COMM_STATE__M                                    0xF

#define B_FE_FS_REG_COMM_MB__A                                       0xC30002
#define B_FE_FS_REG_COMM_MB__W                                       3
#define B_FE_FS_REG_COMM_MB__M                                       0x7
#define   B_FE_FS_REG_COMM_MB_CTR__B                                 0
#define   B_FE_FS_REG_COMM_MB_CTR__W                                 1
#define   B_FE_FS_REG_COMM_MB_CTR__M                                 0x1
#define     B_FE_FS_REG_COMM_MB_CTR_OFF                              0x0
#define     B_FE_FS_REG_COMM_MB_CTR_ON                               0x1
#define   B_FE_FS_REG_COMM_MB_OBS__B                                 1
#define   B_FE_FS_REG_COMM_MB_OBS__W                                 1
#define   B_FE_FS_REG_COMM_MB_OBS__M                                 0x2
#define     B_FE_FS_REG_COMM_MB_OBS_OFF                              0x0
#define     B_FE_FS_REG_COMM_MB_OBS_ON                               0x2
#define   B_FE_FS_REG_COMM_MB_MUX__B                                 2
#define   B_FE_FS_REG_COMM_MB_MUX__W                                 1
#define   B_FE_FS_REG_COMM_MB_MUX__M                                 0x4
#define     B_FE_FS_REG_COMM_MB_MUX_REAL                             0x0
#define     B_FE_FS_REG_COMM_MB_MUX_IMAG                             0x4

#define B_FE_FS_REG_COMM_SERVICE0__A                                 0xC30003
#define B_FE_FS_REG_COMM_SERVICE0__W                                 10
#define B_FE_FS_REG_COMM_SERVICE0__M                                 0x3FF

#define B_FE_FS_REG_COMM_SERVICE1__A                                 0xC30004
#define B_FE_FS_REG_COMM_SERVICE1__W                                 11
#define B_FE_FS_REG_COMM_SERVICE1__M                                 0x7FF

#define B_FE_FS_REG_COMM_ACT__A                                      0xC30005
#define B_FE_FS_REG_COMM_ACT__W                                      2
#define B_FE_FS_REG_COMM_ACT__M                                      0x3

#define B_FE_FS_REG_COMM_CNT__A                                      0xC30006
#define B_FE_FS_REG_COMM_CNT__W                                      16
#define B_FE_FS_REG_COMM_CNT__M                                      0xFFFF

#define B_FE_FS_REG_ADD_INC_LOP__A                                   0xC30010
#define B_FE_FS_REG_ADD_INC_LOP__W                                   16
#define B_FE_FS_REG_ADD_INC_LOP__M                                   0xFFFF
#define   B_FE_FS_REG_ADD_INC_LOP_INIT                               0x0

#define B_FE_FS_REG_ADD_INC_HIP__A                                   0xC30011
#define B_FE_FS_REG_ADD_INC_HIP__W                                   12
#define B_FE_FS_REG_ADD_INC_HIP__M                                   0xFFF
#define   B_FE_FS_REG_ADD_INC_HIP_INIT                               0xC00

#define B_FE_FS_REG_ADD_OFF__A                                       0xC30012
#define B_FE_FS_REG_ADD_OFF__W                                       12
#define B_FE_FS_REG_ADD_OFF__M                                       0xFFF
#define   B_FE_FS_REG_ADD_OFF_INIT                                   0x0

#define B_FE_FS_REG_ADD_OFF_VAL__A                                   0xC30013
#define B_FE_FS_REG_ADD_OFF_VAL__W                                   1
#define B_FE_FS_REG_ADD_OFF_VAL__M                                   0x1
#define   B_FE_FS_REG_ADD_OFF_VAL_INIT                               0x0

#define B_FE_FD_SID                                                  0x4

#define B_FE_FD_REG_COMM_EXEC__A                                     0xC40000
#define B_FE_FD_REG_COMM_EXEC__W                                     3
#define B_FE_FD_REG_COMM_EXEC__M                                     0x7
#define   B_FE_FD_REG_COMM_EXEC_CTL__B                               0
#define   B_FE_FD_REG_COMM_EXEC_CTL__W                               3
#define   B_FE_FD_REG_COMM_EXEC_CTL__M                               0x7
#define     B_FE_FD_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_FE_FD_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_FE_FD_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_FE_FD_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_FE_FD_REG_COMM_MB__A                                       0xC40002
#define B_FE_FD_REG_COMM_MB__W                                       3
#define B_FE_FD_REG_COMM_MB__M                                       0x7
#define   B_FE_FD_REG_COMM_MB_CTR__B                                 0
#define   B_FE_FD_REG_COMM_MB_CTR__W                                 1
#define   B_FE_FD_REG_COMM_MB_CTR__M                                 0x1
#define     B_FE_FD_REG_COMM_MB_CTR_OFF                              0x0
#define     B_FE_FD_REG_COMM_MB_CTR_ON                               0x1
#define   B_FE_FD_REG_COMM_MB_OBS__B                                 1
#define   B_FE_FD_REG_COMM_MB_OBS__W                                 1
#define   B_FE_FD_REG_COMM_MB_OBS__M                                 0x2
#define     B_FE_FD_REG_COMM_MB_OBS_OFF                              0x0
#define     B_FE_FD_REG_COMM_MB_OBS_ON                               0x2

#define B_FE_FD_REG_COMM_SERVICE0__A                                 0xC40003
#define B_FE_FD_REG_COMM_SERVICE0__W                                 10
#define B_FE_FD_REG_COMM_SERVICE0__M                                 0x3FF
#define B_FE_FD_REG_COMM_SERVICE1__A                                 0xC40004
#define B_FE_FD_REG_COMM_SERVICE1__W                                 11
#define B_FE_FD_REG_COMM_SERVICE1__M                                 0x7FF

#define B_FE_FD_REG_COMM_INT_STA__A                                  0xC40007
#define B_FE_FD_REG_COMM_INT_STA__W                                  1
#define B_FE_FD_REG_COMM_INT_STA__M                                  0x1
#define   B_FE_FD_REG_COMM_INT_STA_NEW_MEAS__B                       0
#define   B_FE_FD_REG_COMM_INT_STA_NEW_MEAS__W                       1
#define   B_FE_FD_REG_COMM_INT_STA_NEW_MEAS__M                       0x1

#define B_FE_FD_REG_COMM_INT_MSK__A                                  0xC40008
#define B_FE_FD_REG_COMM_INT_MSK__W                                  1
#define B_FE_FD_REG_COMM_INT_MSK__M                                  0x1
#define   B_FE_FD_REG_COMM_INT_MSK_NEW_MEAS__B                       0
#define   B_FE_FD_REG_COMM_INT_MSK_NEW_MEAS__W                       1
#define   B_FE_FD_REG_COMM_INT_MSK_NEW_MEAS__M                       0x1

#define B_FE_FD_REG_SCL__A                                           0xC40010
#define B_FE_FD_REG_SCL__W                                           6
#define B_FE_FD_REG_SCL__M                                           0x3F

#define B_FE_FD_REG_MAX_LEV__A                                       0xC40011
#define B_FE_FD_REG_MAX_LEV__W                                       3
#define B_FE_FD_REG_MAX_LEV__M                                       0x7

#define B_FE_FD_REG_NR__A                                            0xC40012
#define B_FE_FD_REG_NR__W                                            5
#define B_FE_FD_REG_NR__M                                            0x1F

#define B_FE_FD_REG_MEAS_SEL__A                                      0xC40013
#define B_FE_FD_REG_MEAS_SEL__W                                      1
#define B_FE_FD_REG_MEAS_SEL__M                                      0x1

#define B_FE_FD_REG_MEAS_VAL__A                                      0xC40014
#define B_FE_FD_REG_MEAS_VAL__W                                      1
#define B_FE_FD_REG_MEAS_VAL__M                                      0x1

#define B_FE_FD_REG_MAX__A                                           0xC40015
#define B_FE_FD_REG_MAX__W                                           16
#define B_FE_FD_REG_MAX__M                                           0xFFFF

#define B_FE_IF_SID                                                  0x5

#define B_FE_IF_REG_COMM_EXEC__A                                     0xC50000
#define B_FE_IF_REG_COMM_EXEC__W                                     3
#define B_FE_IF_REG_COMM_EXEC__M                                     0x7
#define   B_FE_IF_REG_COMM_EXEC_CTL__B                               0
#define   B_FE_IF_REG_COMM_EXEC_CTL__W                               3
#define   B_FE_IF_REG_COMM_EXEC_CTL__M                               0x7
#define     B_FE_IF_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_FE_IF_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_FE_IF_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_FE_IF_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_FE_IF_REG_COMM_MB__A                                       0xC50002
#define B_FE_IF_REG_COMM_MB__W                                       3
#define B_FE_IF_REG_COMM_MB__M                                       0x7
#define   B_FE_IF_REG_COMM_MB_CTR__B                                 0
#define   B_FE_IF_REG_COMM_MB_CTR__W                                 1
#define   B_FE_IF_REG_COMM_MB_CTR__M                                 0x1
#define     B_FE_IF_REG_COMM_MB_CTR_OFF                              0x0
#define     B_FE_IF_REG_COMM_MB_CTR_ON                               0x1
#define   B_FE_IF_REG_COMM_MB_OBS__B                                 1
#define   B_FE_IF_REG_COMM_MB_OBS__W                                 1
#define   B_FE_IF_REG_COMM_MB_OBS__M                                 0x2
#define     B_FE_IF_REG_COMM_MB_OBS_OFF                              0x0
#define     B_FE_IF_REG_COMM_MB_OBS_ON                               0x2

#define B_FE_IF_REG_INCR0__A                                         0xC50010
#define B_FE_IF_REG_INCR0__W                                         16
#define B_FE_IF_REG_INCR0__M                                         0xFFFF
#define   B_FE_IF_REG_INCR0_INIT                                     0x0

#define B_FE_IF_REG_INCR1__A                                         0xC50011
#define B_FE_IF_REG_INCR1__W                                         8
#define B_FE_IF_REG_INCR1__M                                         0xFF
#define   B_FE_IF_REG_INCR1_INIT                                     0x28

#define B_FE_CF_SID                                                  0x6

#define B_FE_CF_REG_COMM_EXEC__A                                     0xC60000
#define B_FE_CF_REG_COMM_EXEC__W                                     3
#define B_FE_CF_REG_COMM_EXEC__M                                     0x7
#define   B_FE_CF_REG_COMM_EXEC_CTL__B                               0
#define   B_FE_CF_REG_COMM_EXEC_CTL__W                               3
#define   B_FE_CF_REG_COMM_EXEC_CTL__M                               0x7
#define     B_FE_CF_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_FE_CF_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_FE_CF_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_FE_CF_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_FE_CF_REG_COMM_MB__A                                       0xC60002
#define B_FE_CF_REG_COMM_MB__W                                       3
#define B_FE_CF_REG_COMM_MB__M                                       0x7
#define   B_FE_CF_REG_COMM_MB_CTR__B                                 0
#define   B_FE_CF_REG_COMM_MB_CTR__W                                 1
#define   B_FE_CF_REG_COMM_MB_CTR__M                                 0x1
#define     B_FE_CF_REG_COMM_MB_CTR_OFF                              0x0
#define     B_FE_CF_REG_COMM_MB_CTR_ON                               0x1
#define   B_FE_CF_REG_COMM_MB_OBS__B                                 1
#define   B_FE_CF_REG_COMM_MB_OBS__W                                 1
#define   B_FE_CF_REG_COMM_MB_OBS__M                                 0x2
#define     B_FE_CF_REG_COMM_MB_OBS_OFF                              0x0
#define     B_FE_CF_REG_COMM_MB_OBS_ON                               0x2

#define B_FE_CF_REG_COMM_SERVICE0__A                                 0xC60003
#define B_FE_CF_REG_COMM_SERVICE0__W                                 10
#define B_FE_CF_REG_COMM_SERVICE0__M                                 0x3FF
#define B_FE_CF_REG_COMM_SERVICE1__A                                 0xC60004
#define B_FE_CF_REG_COMM_SERVICE1__W                                 11
#define B_FE_CF_REG_COMM_SERVICE1__M                                 0x7FF

#define B_FE_CF_REG_COMM_INT_STA__A                                  0xC60007
#define B_FE_CF_REG_COMM_INT_STA__W                                  2
#define B_FE_CF_REG_COMM_INT_STA__M                                  0x3
#define   B_FE_CF_REG_COMM_INT_STA_NEW_MEAS__B                       0
#define   B_FE_CF_REG_COMM_INT_STA_NEW_MEAS__W                       1
#define   B_FE_CF_REG_COMM_INT_STA_NEW_MEAS__M                       0x1

#define B_FE_CF_REG_COMM_INT_MSK__A                                  0xC60008
#define B_FE_CF_REG_COMM_INT_MSK__W                                  2
#define B_FE_CF_REG_COMM_INT_MSK__M                                  0x3
#define   B_FE_CF_REG_COMM_INT_MSK_NEW_MEAS__B                       0
#define   B_FE_CF_REG_COMM_INT_MSK_NEW_MEAS__W                       1
#define   B_FE_CF_REG_COMM_INT_MSK_NEW_MEAS__M                       0x1

#define B_FE_CF_REG_SCL__A                                           0xC60010
#define B_FE_CF_REG_SCL__W                                           9
#define B_FE_CF_REG_SCL__M                                           0x1FF

#define B_FE_CF_REG_MAX_LEV__A                                       0xC60011
#define B_FE_CF_REG_MAX_LEV__W                                       3
#define B_FE_CF_REG_MAX_LEV__M                                       0x7

#define B_FE_CF_REG_NR__A                                            0xC60012
#define B_FE_CF_REG_NR__W                                            5
#define B_FE_CF_REG_NR__M                                            0x1F

#define B_FE_CF_REG_IMP_VAL__A                                       0xC60013
#define B_FE_CF_REG_IMP_VAL__W                                       1
#define B_FE_CF_REG_IMP_VAL__M                                       0x1

#define B_FE_CF_REG_MEAS_VAL__A                                      0xC60014
#define B_FE_CF_REG_MEAS_VAL__W                                      1
#define B_FE_CF_REG_MEAS_VAL__M                                      0x1

#define B_FE_CF_REG_MAX__A                                           0xC60015
#define B_FE_CF_REG_MAX__W                                           16
#define B_FE_CF_REG_MAX__M                                           0xFFFF

#define B_FE_CU_SID                                                  0x7

#define B_FE_CU_REG_COMM_EXEC__A                                     0xC70000
#define B_FE_CU_REG_COMM_EXEC__W                                     3
#define B_FE_CU_REG_COMM_EXEC__M                                     0x7
#define   B_FE_CU_REG_COMM_EXEC_CTL__B                               0
#define   B_FE_CU_REG_COMM_EXEC_CTL__W                               3
#define   B_FE_CU_REG_COMM_EXEC_CTL__M                               0x7
#define     B_FE_CU_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_FE_CU_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_FE_CU_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_FE_CU_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_FE_CU_REG_COMM_STATE__A                                    0xC70001
#define B_FE_CU_REG_COMM_STATE__W                                    4
#define B_FE_CU_REG_COMM_STATE__M                                    0xF

#define B_FE_CU_REG_COMM_MB__A                                       0xC70002
#define B_FE_CU_REG_COMM_MB__W                                       3
#define B_FE_CU_REG_COMM_MB__M                                       0x7
#define   B_FE_CU_REG_COMM_MB_CTR__B                                 0
#define   B_FE_CU_REG_COMM_MB_CTR__W                                 1
#define   B_FE_CU_REG_COMM_MB_CTR__M                                 0x1
#define     B_FE_CU_REG_COMM_MB_CTR_OFF                              0x0
#define     B_FE_CU_REG_COMM_MB_CTR_ON                               0x1
#define   B_FE_CU_REG_COMM_MB_OBS__B                                 1
#define   B_FE_CU_REG_COMM_MB_OBS__W                                 1
#define   B_FE_CU_REG_COMM_MB_OBS__M                                 0x2
#define     B_FE_CU_REG_COMM_MB_OBS_OFF                              0x0
#define     B_FE_CU_REG_COMM_MB_OBS_ON                               0x2
#define   B_FE_CU_REG_COMM_MB_MUX__B                                 2
#define   B_FE_CU_REG_COMM_MB_MUX__W                                 1
#define   B_FE_CU_REG_COMM_MB_MUX__M                                 0x4
#define     B_FE_CU_REG_COMM_MB_MUX_REAL                             0x0
#define     B_FE_CU_REG_COMM_MB_MUX_IMAG                             0x4

#define B_FE_CU_REG_COMM_SERVICE0__A                                 0xC70003
#define B_FE_CU_REG_COMM_SERVICE0__W                                 10
#define B_FE_CU_REG_COMM_SERVICE0__M                                 0x3FF

#define B_FE_CU_REG_COMM_SERVICE1__A                                 0xC70004
#define B_FE_CU_REG_COMM_SERVICE1__W                                 11
#define B_FE_CU_REG_COMM_SERVICE1__M                                 0x7FF

#define B_FE_CU_REG_COMM_ACT__A                                      0xC70005
#define B_FE_CU_REG_COMM_ACT__W                                      2
#define B_FE_CU_REG_COMM_ACT__M                                      0x3

#define B_FE_CU_REG_COMM_CNT__A                                      0xC70006
#define B_FE_CU_REG_COMM_CNT__W                                      16
#define B_FE_CU_REG_COMM_CNT__M                                      0xFFFF

#define B_FE_CU_REG_COMM_INT_STA__A                                  0xC70007
#define B_FE_CU_REG_COMM_INT_STA__W                                  4
#define B_FE_CU_REG_COMM_INT_STA__M                                  0xF
#define   B_FE_CU_REG_COMM_INT_STA_FE_START__B                       0
#define   B_FE_CU_REG_COMM_INT_STA_FE_START__W                       1
#define   B_FE_CU_REG_COMM_INT_STA_FE_START__M                       0x1
#define   B_FE_CU_REG_COMM_INT_STA_FT_START__B                       1
#define   B_FE_CU_REG_COMM_INT_STA_FT_START__W                       1
#define   B_FE_CU_REG_COMM_INT_STA_FT_START__M                       0x2
#define   B_FE_CU_REG_COMM_INT_STA_SB_START__B                       2
#define   B_FE_CU_REG_COMM_INT_STA_SB_START__W                       1
#define   B_FE_CU_REG_COMM_INT_STA_SB_START__M                       0x4
#define   B_FE_CU_REG_COMM_INT_STA_NF_READY__B                       3
#define   B_FE_CU_REG_COMM_INT_STA_NF_READY__W                       1
#define   B_FE_CU_REG_COMM_INT_STA_NF_READY__M                       0x8

#define B_FE_CU_REG_COMM_INT_MSK__A                                  0xC70008
#define B_FE_CU_REG_COMM_INT_MSK__W                                  4
#define B_FE_CU_REG_COMM_INT_MSK__M                                  0xF
#define   B_FE_CU_REG_COMM_INT_MSK_FE_START__B                       0
#define   B_FE_CU_REG_COMM_INT_MSK_FE_START__W                       1
#define   B_FE_CU_REG_COMM_INT_MSK_FE_START__M                       0x1
#define   B_FE_CU_REG_COMM_INT_MSK_FT_START__B                       1
#define   B_FE_CU_REG_COMM_INT_MSK_FT_START__W                       1
#define   B_FE_CU_REG_COMM_INT_MSK_FT_START__M                       0x2
#define   B_FE_CU_REG_COMM_INT_MSK_SB_START__B                       2
#define   B_FE_CU_REG_COMM_INT_MSK_SB_START__W                       1
#define   B_FE_CU_REG_COMM_INT_MSK_SB_START__M                       0x4
#define   B_FE_CU_REG_COMM_INT_MSK_NF_READY__B                       3
#define   B_FE_CU_REG_COMM_INT_MSK_NF_READY__W                       1
#define   B_FE_CU_REG_COMM_INT_MSK_NF_READY__M                       0x8

#define B_FE_CU_REG_MODE__A                                          0xC70010
#define B_FE_CU_REG_MODE__W                                          5
#define B_FE_CU_REG_MODE__M                                          0x1F
#define   B_FE_CU_REG_MODE_INIT                                      0x0

#define   B_FE_CU_REG_MODE_FFT__B                                    0
#define   B_FE_CU_REG_MODE_FFT__W                                    1
#define   B_FE_CU_REG_MODE_FFT__M                                    0x1
#define     B_FE_CU_REG_MODE_FFT_M8K                                 0x0
#define     B_FE_CU_REG_MODE_FFT_M2K                                 0x1

#define   B_FE_CU_REG_MODE_COR__B                                    1
#define   B_FE_CU_REG_MODE_COR__W                                    1
#define   B_FE_CU_REG_MODE_COR__M                                    0x2
#define     B_FE_CU_REG_MODE_COR_OFF                                 0x0
#define     B_FE_CU_REG_MODE_COR_ON                                  0x2

#define   B_FE_CU_REG_MODE_IFD__B                                    2
#define   B_FE_CU_REG_MODE_IFD__W                                    1
#define   B_FE_CU_REG_MODE_IFD__M                                    0x4
#define     B_FE_CU_REG_MODE_IFD_ENABLE                              0x0
#define     B_FE_CU_REG_MODE_IFD_DISABLE                             0x4

#define   B_FE_CU_REG_MODE_SEL__B                                    3
#define   B_FE_CU_REG_MODE_SEL__W                                    1
#define   B_FE_CU_REG_MODE_SEL__M                                    0x8
#define     B_FE_CU_REG_MODE_SEL_COR                                 0x0
#define     B_FE_CU_REG_MODE_SEL_COR_NFC                             0x8

#define   B_FE_CU_REG_MODE_FES__B                                    4
#define   B_FE_CU_REG_MODE_FES__W                                    1
#define   B_FE_CU_REG_MODE_FES__M                                    0x10
#define     B_FE_CU_REG_MODE_FES_SEL_RST                             0x0
#define     B_FE_CU_REG_MODE_FES_SEL_UPD                             0x10

#define B_FE_CU_REG_FRM_CNT_RST__A                                   0xC70011
#define B_FE_CU_REG_FRM_CNT_RST__W                                   15
#define B_FE_CU_REG_FRM_CNT_RST__M                                   0x7FFF
#define   B_FE_CU_REG_FRM_CNT_RST_INIT                               0x20FF

#define B_FE_CU_REG_FRM_CNT_STR__A                                   0xC70012
#define B_FE_CU_REG_FRM_CNT_STR__W                                   15
#define B_FE_CU_REG_FRM_CNT_STR__M                                   0x7FFF
#define   B_FE_CU_REG_FRM_CNT_STR_INIT                               0x1E

#define B_FE_CU_REG_FRM_SMP_CNT__A                                   0xC70013
#define B_FE_CU_REG_FRM_SMP_CNT__W                                   15
#define B_FE_CU_REG_FRM_SMP_CNT__M                                   0x7FFF

#define B_FE_CU_REG_FRM_SMB_CNT__A                                   0xC70014
#define B_FE_CU_REG_FRM_SMB_CNT__W                                   16
#define B_FE_CU_REG_FRM_SMB_CNT__M                                   0xFFFF

#define B_FE_CU_REG_CMP_MAX_DAT__A                                   0xC70015
#define B_FE_CU_REG_CMP_MAX_DAT__W                                   12
#define B_FE_CU_REG_CMP_MAX_DAT__M                                   0xFFF

#define B_FE_CU_REG_CMP_MAX_ADR__A                                   0xC70016
#define B_FE_CU_REG_CMP_MAX_ADR__W                                   10
#define B_FE_CU_REG_CMP_MAX_ADR__M                                   0x3FF

#define B_FE_CU_REG_BUF_NFC_DEL__A                                   0xC7001F
#define B_FE_CU_REG_BUF_NFC_DEL__W                                   14
#define B_FE_CU_REG_BUF_NFC_DEL__M                                   0x3FFF
#define   B_FE_CU_REG_BUF_NFC_DEL_INIT                               0x0

#define B_FE_CU_REG_CTR_NFC_ICR__A                                   0xC70020
#define B_FE_CU_REG_CTR_NFC_ICR__W                                   5
#define B_FE_CU_REG_CTR_NFC_ICR__M                                   0x1F
#define   B_FE_CU_REG_CTR_NFC_ICR_INIT                               0x0

#define B_FE_CU_REG_CTR_NFC_OCR__A                                   0xC70021
#define B_FE_CU_REG_CTR_NFC_OCR__W                                   15
#define B_FE_CU_REG_CTR_NFC_OCR__M                                   0x7FFF
#define   B_FE_CU_REG_CTR_NFC_OCR_INIT                               0x61A8

#define B_FE_CU_REG_CTR_NFC_CNT__A                                   0xC70022
#define B_FE_CU_REG_CTR_NFC_CNT__W                                   15
#define B_FE_CU_REG_CTR_NFC_CNT__M                                   0x7FFF

#define B_FE_CU_REG_CTR_NFC_STS__A                                   0xC70023
#define B_FE_CU_REG_CTR_NFC_STS__W                                   3
#define B_FE_CU_REG_CTR_NFC_STS__M                                   0x7
#define   B_FE_CU_REG_CTR_NFC_STS_RUN                                0x0
#define   B_FE_CU_REG_CTR_NFC_STS_ACC_MAX_IMA                        0x1
#define   B_FE_CU_REG_CTR_NFC_STS_ACC_MAX_REA                        0x2
#define   B_FE_CU_REG_CTR_NFC_STS_CNT_MAX                            0x4

#define B_FE_CU_REG_DIV_NFC_REA__A                                   0xC70024
#define B_FE_CU_REG_DIV_NFC_REA__W                                   14
#define B_FE_CU_REG_DIV_NFC_REA__M                                   0x3FFF

#define B_FE_CU_REG_DIV_NFC_IMA__A                                   0xC70025
#define B_FE_CU_REG_DIV_NFC_IMA__W                                   14
#define B_FE_CU_REG_DIV_NFC_IMA__M                                   0x3FFF

#define B_FE_CU_REG_FRM_CNT_UPD__A                                   0xC70026
#define B_FE_CU_REG_FRM_CNT_UPD__W                                   15
#define B_FE_CU_REG_FRM_CNT_UPD__M                                   0x7FFF
#define   B_FE_CU_REG_FRM_CNT_UPD_INIT                               0x20FF

#define B_FE_CU_REG_DIV_NFC_CLP__A                                   0xC70027
#define B_FE_CU_REG_DIV_NFC_CLP__W                                   2
#define B_FE_CU_REG_DIV_NFC_CLP__M                                   0x3
#define   B_FE_CU_REG_DIV_NFC_CLP_INIT                               0x1
#define   B_FE_CU_REG_DIV_NFC_CLP_CLIP_S11                           0x0
#define   B_FE_CU_REG_DIV_NFC_CLP_CLIP_S12                           0x1
#define   B_FE_CU_REG_DIV_NFC_CLP_CLIP_S13                           0x2
#define   B_FE_CU_REG_DIV_NFC_CLP_CLIP_S14                           0x3

#define B_FE_CU_BUF_RAM__A                                           0xC80000

#define B_FE_CU_CMP_RAM__A                                           0xC90000

#define B_FT_SID                                                     0x8

#define B_FT_COMM_EXEC__A                                            0x1000000
#define B_FT_COMM_EXEC__W                                            3
#define B_FT_COMM_EXEC__M                                            0x7
#define   B_FT_COMM_EXEC_CTL__B                                      0
#define   B_FT_COMM_EXEC_CTL__W                                      3
#define   B_FT_COMM_EXEC_CTL__M                                      0x7
#define     B_FT_COMM_EXEC_CTL_STOP                                  0x0
#define     B_FT_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_FT_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_FT_COMM_EXEC_CTL_STEP                                  0x3
#define     B_FT_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_FT_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_FT_COMM_STATE__A                                           0x1000001
#define B_FT_COMM_STATE__W                                           16
#define B_FT_COMM_STATE__M                                           0xFFFF
#define B_FT_COMM_MB__A                                              0x1000002
#define B_FT_COMM_MB__W                                              16
#define B_FT_COMM_MB__M                                              0xFFFF
#define B_FT_COMM_SERVICE0__A                                        0x1000003
#define B_FT_COMM_SERVICE0__W                                        16
#define B_FT_COMM_SERVICE0__M                                        0xFFFF
#define B_FT_COMM_SERVICE1__A                                        0x1000004
#define B_FT_COMM_SERVICE1__W                                        16
#define B_FT_COMM_SERVICE1__M                                        0xFFFF
#define B_FT_COMM_INT_STA__A                                         0x1000007
#define B_FT_COMM_INT_STA__W                                         16
#define B_FT_COMM_INT_STA__M                                         0xFFFF
#define B_FT_COMM_INT_MSK__A                                         0x1000008
#define B_FT_COMM_INT_MSK__W                                         16
#define B_FT_COMM_INT_MSK__M                                         0xFFFF

#define B_FT_REG_COMM_EXEC__A                                        0x1010000
#define B_FT_REG_COMM_EXEC__W                                        3
#define B_FT_REG_COMM_EXEC__M                                        0x7
#define   B_FT_REG_COMM_EXEC_CTL__B                                  0
#define   B_FT_REG_COMM_EXEC_CTL__W                                  3
#define   B_FT_REG_COMM_EXEC_CTL__M                                  0x7
#define     B_FT_REG_COMM_EXEC_CTL_STOP                              0x0
#define     B_FT_REG_COMM_EXEC_CTL_ACTIVE                            0x1
#define     B_FT_REG_COMM_EXEC_CTL_HOLD                              0x2
#define     B_FT_REG_COMM_EXEC_CTL_STEP                              0x3

#define B_FT_REG_COMM_MB__A                                          0x1010002
#define B_FT_REG_COMM_MB__W                                          3
#define B_FT_REG_COMM_MB__M                                          0x7
#define   B_FT_REG_COMM_MB_CTR__B                                    0
#define   B_FT_REG_COMM_MB_CTR__W                                    1
#define   B_FT_REG_COMM_MB_CTR__M                                    0x1
#define     B_FT_REG_COMM_MB_CTR_OFF                                 0x0
#define     B_FT_REG_COMM_MB_CTR_ON                                  0x1
#define   B_FT_REG_COMM_MB_OBS__B                                    1
#define   B_FT_REG_COMM_MB_OBS__W                                    1
#define   B_FT_REG_COMM_MB_OBS__M                                    0x2
#define     B_FT_REG_COMM_MB_OBS_OFF                                 0x0
#define     B_FT_REG_COMM_MB_OBS_ON                                  0x2

#define B_FT_REG_MODE_2K__A                                          0x1010010
#define B_FT_REG_MODE_2K__W                                          1
#define B_FT_REG_MODE_2K__M                                          0x1
#define   B_FT_REG_MODE_2K_MODE_8K                                   0x0
#define   B_FT_REG_MODE_2K_MODE_2K                                   0x1
#define   B_FT_REG_MODE_2K_INIT                                      0x0

#define B_FT_REG_NORM_OFF__A                                         0x1010016
#define B_FT_REG_NORM_OFF__W                                         4
#define B_FT_REG_NORM_OFF__M                                         0xF
#define   B_FT_REG_NORM_OFF_INIT                                     0x2

#define B_FT_ST1_RAM__A                                              0x1020000

#define B_FT_ST2_RAM__A                                              0x1030000

#define B_FT_ST3_RAM__A                                              0x1040000

#define B_FT_ST5_RAM__A                                              0x1050000

#define B_FT_ST6_RAM__A                                              0x1060000

#define B_FT_ST8_RAM__A                                              0x1070000

#define B_FT_ST9_RAM__A                                              0x1080000

#define B_CP_SID                                                     0x9

#define B_CP_COMM_EXEC__A                                            0x1400000
#define B_CP_COMM_EXEC__W                                            3
#define B_CP_COMM_EXEC__M                                            0x7
#define   B_CP_COMM_EXEC_CTL__B                                      0
#define   B_CP_COMM_EXEC_CTL__W                                      3
#define   B_CP_COMM_EXEC_CTL__M                                      0x7
#define     B_CP_COMM_EXEC_CTL_STOP                                  0x0
#define     B_CP_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_CP_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_CP_COMM_EXEC_CTL_STEP                                  0x3
#define     B_CP_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_CP_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_CP_COMM_STATE__A                                           0x1400001
#define B_CP_COMM_STATE__W                                           16
#define B_CP_COMM_STATE__M                                           0xFFFF
#define B_CP_COMM_MB__A                                              0x1400002
#define B_CP_COMM_MB__W                                              16
#define B_CP_COMM_MB__M                                              0xFFFF
#define B_CP_COMM_SERVICE0__A                                        0x1400003
#define B_CP_COMM_SERVICE0__W                                        16
#define B_CP_COMM_SERVICE0__M                                        0xFFFF
#define B_CP_COMM_SERVICE1__A                                        0x1400004
#define B_CP_COMM_SERVICE1__W                                        16
#define B_CP_COMM_SERVICE1__M                                        0xFFFF
#define B_CP_COMM_INT_STA__A                                         0x1400007
#define B_CP_COMM_INT_STA__W                                         16
#define B_CP_COMM_INT_STA__M                                         0xFFFF
#define B_CP_COMM_INT_MSK__A                                         0x1400008
#define B_CP_COMM_INT_MSK__W                                         16
#define B_CP_COMM_INT_MSK__M                                         0xFFFF

#define B_CP_REG_COMM_EXEC__A                                        0x1410000
#define B_CP_REG_COMM_EXEC__W                                        3
#define B_CP_REG_COMM_EXEC__M                                        0x7
#define   B_CP_REG_COMM_EXEC_CTL__B                                  0
#define   B_CP_REG_COMM_EXEC_CTL__W                                  3
#define   B_CP_REG_COMM_EXEC_CTL__M                                  0x7
#define     B_CP_REG_COMM_EXEC_CTL_STOP                              0x0
#define     B_CP_REG_COMM_EXEC_CTL_ACTIVE                            0x1
#define     B_CP_REG_COMM_EXEC_CTL_HOLD                              0x2
#define     B_CP_REG_COMM_EXEC_CTL_STEP                              0x3

#define B_CP_REG_COMM_MB__A                                          0x1410002
#define B_CP_REG_COMM_MB__W                                          3
#define B_CP_REG_COMM_MB__M                                          0x7
#define   B_CP_REG_COMM_MB_CTR__B                                    0
#define   B_CP_REG_COMM_MB_CTR__W                                    1
#define   B_CP_REG_COMM_MB_CTR__M                                    0x1
#define     B_CP_REG_COMM_MB_CTR_OFF                                 0x0
#define     B_CP_REG_COMM_MB_CTR_ON                                  0x1
#define   B_CP_REG_COMM_MB_OBS__B                                    1
#define   B_CP_REG_COMM_MB_OBS__W                                    1
#define   B_CP_REG_COMM_MB_OBS__M                                    0x2
#define     B_CP_REG_COMM_MB_OBS_OFF                                 0x0
#define     B_CP_REG_COMM_MB_OBS_ON                                  0x2

#define B_CP_REG_COMM_SERVICE0__A                                    0x1410003
#define B_CP_REG_COMM_SERVICE0__W                                    10
#define B_CP_REG_COMM_SERVICE0__M                                    0x3FF
#define   B_CP_REG_COMM_SERVICE0_CP__B                               9
#define   B_CP_REG_COMM_SERVICE0_CP__W                               1
#define   B_CP_REG_COMM_SERVICE0_CP__M                               0x200

#define B_CP_REG_COMM_SERVICE1__A                                    0x1410004
#define B_CP_REG_COMM_SERVICE1__W                                    11
#define B_CP_REG_COMM_SERVICE1__M                                    0x7FF

#define B_CP_REG_COMM_INT_STA__A                                     0x1410007
#define B_CP_REG_COMM_INT_STA__W                                     2
#define B_CP_REG_COMM_INT_STA__M                                     0x3
#define   B_CP_REG_COMM_INT_STA_NEW_MEAS__B                          0
#define   B_CP_REG_COMM_INT_STA_NEW_MEAS__W                          1
#define   B_CP_REG_COMM_INT_STA_NEW_MEAS__M                          0x1

#define B_CP_REG_COMM_INT_MSK__A                                     0x1410008
#define B_CP_REG_COMM_INT_MSK__W                                     2
#define B_CP_REG_COMM_INT_MSK__M                                     0x3
#define   B_CP_REG_COMM_INT_MSK_NEW_MEAS__B                          0
#define   B_CP_REG_COMM_INT_MSK_NEW_MEAS__W                          1
#define   B_CP_REG_COMM_INT_MSK_NEW_MEAS__M                          0x1

#define B_CP_REG_MODE_2K__A                                          0x1410010
#define B_CP_REG_MODE_2K__W                                          1
#define B_CP_REG_MODE_2K__M                                          0x1
#define   B_CP_REG_MODE_2K_INIT                                      0x0

#define B_CP_REG_INTERVAL__A                                         0x1410011
#define B_CP_REG_INTERVAL__W                                         4
#define B_CP_REG_INTERVAL__M                                         0xF
#define   B_CP_REG_INTERVAL_INIT                                     0x5

#define B_CP_REG_DETECT_ENA__A                                       0x1410012
#define B_CP_REG_DETECT_ENA__W                                       2
#define B_CP_REG_DETECT_ENA__M                                       0x3

#define   B_CP_REG_DETECT_ENA_SCATTERED__B                           0
#define   B_CP_REG_DETECT_ENA_SCATTERED__W                           1
#define   B_CP_REG_DETECT_ENA_SCATTERED__M                           0x1

#define   B_CP_REG_DETECT_ENA_CONTINUOUS__B                          1
#define   B_CP_REG_DETECT_ENA_CONTINUOUS__W                          1
#define   B_CP_REG_DETECT_ENA_CONTINUOUS__M                          0x2
#define     B_CP_REG_DETECT_ENA_INIT                                 0x0

#define B_CP_REG_BR_SMB_NR__A                                        0x1410021
#define B_CP_REG_BR_SMB_NR__W                                        4
#define B_CP_REG_BR_SMB_NR__M                                        0xF

#define   B_CP_REG_BR_SMB_NR_SMB__B                                  0
#define   B_CP_REG_BR_SMB_NR_SMB__W                                  2
#define   B_CP_REG_BR_SMB_NR_SMB__M                                  0x3

#define   B_CP_REG_BR_SMB_NR_VAL__B                                  2
#define   B_CP_REG_BR_SMB_NR_VAL__W                                  1
#define   B_CP_REG_BR_SMB_NR_VAL__M                                  0x4

#define   B_CP_REG_BR_SMB_NR_OFFSET__B                               3
#define   B_CP_REG_BR_SMB_NR_OFFSET__W                               1
#define   B_CP_REG_BR_SMB_NR_OFFSET__M                               0x8
#define     B_CP_REG_BR_SMB_NR_INIT                                  0x0

#define B_CP_REG_BR_CP_SMB_NR__A                                     0x1410022
#define B_CP_REG_BR_CP_SMB_NR__W                                     2
#define B_CP_REG_BR_CP_SMB_NR__M                                     0x3
#define   B_CP_REG_BR_CP_SMB_NR_INIT                                 0x0

#define B_CP_REG_BR_SPL_OFFSET__A                                    0x1410023
#define B_CP_REG_BR_SPL_OFFSET__W                                    3
#define B_CP_REG_BR_SPL_OFFSET__M                                    0x7
#define   B_CP_REG_BR_SPL_OFFSET_INIT                                0x0

#define B_CP_REG_BR_STR_DEL__A                                       0x1410024
#define B_CP_REG_BR_STR_DEL__W                                       10
#define B_CP_REG_BR_STR_DEL__M                                       0x3FF
#define   B_CP_REG_BR_STR_DEL_INIT                                   0xA

#define B_CP_REG_BR_EXP_ADJ__A                                       0x1410025
#define B_CP_REG_BR_EXP_ADJ__W                                       5
#define B_CP_REG_BR_EXP_ADJ__M                                       0x1F
#define   B_CP_REG_BR_EXP_ADJ_INIT                                   0x10

#define B_CP_REG_RT_ANG_INC0__A                                      0x1410030
#define B_CP_REG_RT_ANG_INC0__W                                      16
#define B_CP_REG_RT_ANG_INC0__M                                      0xFFFF
#define   B_CP_REG_RT_ANG_INC0_INIT                                  0x0

#define B_CP_REG_RT_ANG_INC1__A                                      0x1410031
#define B_CP_REG_RT_ANG_INC1__W                                      8
#define B_CP_REG_RT_ANG_INC1__M                                      0xFF
#define   B_CP_REG_RT_ANG_INC1_INIT                                  0x0

#define B_CP_REG_RT_SPD_EXP_MARG__A                                  0x1410032
#define B_CP_REG_RT_SPD_EXP_MARG__W                                  5
#define B_CP_REG_RT_SPD_EXP_MARG__M                                  0x1F
#define   B_CP_REG_RT_SPD_EXP_MARG_INIT                              0x5

#define B_CP_REG_RT_DETECT_TRH__A                                    0x1410033
#define B_CP_REG_RT_DETECT_TRH__W                                    2
#define B_CP_REG_RT_DETECT_TRH__M                                    0x3
#define   B_CP_REG_RT_DETECT_TRH_INIT                                0x3

#define B_CP_REG_RT_SPD_RELIABLE__A                                  0x1410034
#define B_CP_REG_RT_SPD_RELIABLE__W                                  3
#define B_CP_REG_RT_SPD_RELIABLE__M                                  0x7
#define   B_CP_REG_RT_SPD_RELIABLE_INIT                              0x0

#define B_CP_REG_RT_SPD_DIRECTION__A                                 0x1410035
#define B_CP_REG_RT_SPD_DIRECTION__W                                 1
#define B_CP_REG_RT_SPD_DIRECTION__M                                 0x1
#define   B_CP_REG_RT_SPD_DIRECTION_INIT                             0x0

#define B_CP_REG_RT_SPD_MOD__A                                       0x1410036
#define B_CP_REG_RT_SPD_MOD__W                                       2
#define B_CP_REG_RT_SPD_MOD__M                                       0x3
#define   B_CP_REG_RT_SPD_MOD_INIT                                   0x0

#define B_CP_REG_RT_SPD_SMB__A                                       0x1410037
#define B_CP_REG_RT_SPD_SMB__W                                       2
#define B_CP_REG_RT_SPD_SMB__M                                       0x3
#define   B_CP_REG_RT_SPD_SMB_INIT                                   0x0

#define B_CP_REG_RT_CPD_MODE__A                                      0x1410038
#define B_CP_REG_RT_CPD_MODE__W                                      3
#define B_CP_REG_RT_CPD_MODE__M                                      0x7

#define   B_CP_REG_RT_CPD_MODE_MOD3__B                               0
#define   B_CP_REG_RT_CPD_MODE_MOD3__W                               2
#define   B_CP_REG_RT_CPD_MODE_MOD3__M                               0x3

#define   B_CP_REG_RT_CPD_MODE_ADD__B                                2
#define   B_CP_REG_RT_CPD_MODE_ADD__W                                1
#define   B_CP_REG_RT_CPD_MODE_ADD__M                                0x4
#define     B_CP_REG_RT_CPD_MODE_INIT                                0x0

#define B_CP_REG_RT_CPD_RELIABLE__A                                  0x1410039
#define B_CP_REG_RT_CPD_RELIABLE__W                                  3
#define B_CP_REG_RT_CPD_RELIABLE__M                                  0x7
#define   B_CP_REG_RT_CPD_RELIABLE_INIT                              0x0

#define B_CP_REG_RT_CPD_BIN__A                                       0x141003A
#define B_CP_REG_RT_CPD_BIN__W                                       5
#define B_CP_REG_RT_CPD_BIN__M                                       0x1F
#define   B_CP_REG_RT_CPD_BIN_INIT                                   0x0

#define B_CP_REG_RT_CPD_MAX__A                                       0x141003B
#define B_CP_REG_RT_CPD_MAX__W                                       4
#define B_CP_REG_RT_CPD_MAX__M                                       0xF
#define   B_CP_REG_RT_CPD_MAX_INIT                                   0x0

#define B_CP_REG_RT_SUPR_VAL__A                                      0x141003C
#define B_CP_REG_RT_SUPR_VAL__W                                      2
#define B_CP_REG_RT_SUPR_VAL__M                                      0x3

#define   B_CP_REG_RT_SUPR_VAL_CE__B                                 0
#define   B_CP_REG_RT_SUPR_VAL_CE__W                                 1
#define   B_CP_REG_RT_SUPR_VAL_CE__M                                 0x1

#define   B_CP_REG_RT_SUPR_VAL_DL__B                                 1
#define   B_CP_REG_RT_SUPR_VAL_DL__W                                 1
#define   B_CP_REG_RT_SUPR_VAL_DL__M                                 0x2
#define     B_CP_REG_RT_SUPR_VAL_INIT                                0x0

#define B_CP_REG_RT_EXP_AVE__A                                       0x141003D
#define B_CP_REG_RT_EXP_AVE__W                                       5
#define B_CP_REG_RT_EXP_AVE__M                                       0x1F
#define   B_CP_REG_RT_EXP_AVE_INIT                                   0x0

#define B_CP_REG_RT_CPD_EXP_MARG__A                                  0x141003E
#define B_CP_REG_RT_CPD_EXP_MARG__W                                  5
#define B_CP_REG_RT_CPD_EXP_MARG__M                                  0x1F
#define   B_CP_REG_RT_CPD_EXP_MARG_INIT                              0x3

#define B_CP_REG_AC_NEXP_OFFS__A                                     0x1410040
#define B_CP_REG_AC_NEXP_OFFS__W                                     8
#define B_CP_REG_AC_NEXP_OFFS__M                                     0xFF
#define   B_CP_REG_AC_NEXP_OFFS_INIT                                 0x0

#define B_CP_REG_AC_AVER_POW__A                                      0x1410041
#define B_CP_REG_AC_AVER_POW__W                                      8
#define B_CP_REG_AC_AVER_POW__M                                      0xFF
#define   B_CP_REG_AC_AVER_POW_INIT                                  0x5F

#define B_CP_REG_AC_MAX_POW__A                                       0x1410042
#define B_CP_REG_AC_MAX_POW__W                                       8
#define B_CP_REG_AC_MAX_POW__M                                       0xFF
#define   B_CP_REG_AC_MAX_POW_INIT                                   0x7A

#define B_CP_REG_AC_WEIGHT_MAN__A                                    0x1410043
#define B_CP_REG_AC_WEIGHT_MAN__W                                    6
#define B_CP_REG_AC_WEIGHT_MAN__M                                    0x3F
#define   B_CP_REG_AC_WEIGHT_MAN_INIT                                0x31

#define B_CP_REG_AC_WEIGHT_EXP__A                                    0x1410044
#define B_CP_REG_AC_WEIGHT_EXP__W                                    5
#define B_CP_REG_AC_WEIGHT_EXP__M                                    0x1F
#define   B_CP_REG_AC_WEIGHT_EXP_INIT                                0x10

#define B_CP_REG_AC_GAIN_MAN__A                                      0x1410045
#define B_CP_REG_AC_GAIN_MAN__W                                      16
#define B_CP_REG_AC_GAIN_MAN__M                                      0xFFFF
#define   B_CP_REG_AC_GAIN_MAN_INIT                                  0x0

#define B_CP_REG_AC_GAIN_EXP__A                                      0x1410046
#define B_CP_REG_AC_GAIN_EXP__W                                      5
#define B_CP_REG_AC_GAIN_EXP__M                                      0x1F
#define   B_CP_REG_AC_GAIN_EXP_INIT                                  0x0

#define B_CP_REG_AC_AMP_MODE__A                                      0x1410047
#define B_CP_REG_AC_AMP_MODE__W                                      2
#define B_CP_REG_AC_AMP_MODE__M                                      0x3
#define   B_CP_REG_AC_AMP_MODE_NEW                                   0x0
#define   B_CP_REG_AC_AMP_MODE_OLD                                   0x1
#define   B_CP_REG_AC_AMP_MODE_FIXED                                 0x2
#define   B_CP_REG_AC_AMP_MODE_INIT                                  0x2

#define B_CP_REG_AC_AMP_FIX__A                                       0x1410048
#define B_CP_REG_AC_AMP_FIX__W                                       14
#define B_CP_REG_AC_AMP_FIX__M                                       0x3FFF
#define   B_CP_REG_AC_AMP_FIX_INIT                                   0x1FF

#define B_CP_REG_AC_AMP_READ__A                                      0x1410049
#define B_CP_REG_AC_AMP_READ__W                                      14
#define B_CP_REG_AC_AMP_READ__M                                      0x3FFF
#define   B_CP_REG_AC_AMP_READ_INIT                                  0x0

#define B_CP_REG_AC_ANG_MODE__A                                      0x141004A
#define B_CP_REG_AC_ANG_MODE__W                                      2
#define B_CP_REG_AC_ANG_MODE__M                                      0x3
#define   B_CP_REG_AC_ANG_MODE_NEW                                   0x0
#define   B_CP_REG_AC_ANG_MODE_OLD                                   0x1
#define   B_CP_REG_AC_ANG_MODE_NO_INT                                0x2
#define   B_CP_REG_AC_ANG_MODE_OFFSET                                0x3
#define   B_CP_REG_AC_ANG_MODE_INIT                                  0x3

#define B_CP_REG_AC_ANG_OFFS__A                                      0x141004B
#define B_CP_REG_AC_ANG_OFFS__W                                      14
#define B_CP_REG_AC_ANG_OFFS__M                                      0x3FFF
#define   B_CP_REG_AC_ANG_OFFS_INIT                                  0x0

#define B_CP_REG_AC_ANG_READ__A                                      0x141004C
#define B_CP_REG_AC_ANG_READ__W                                      16
#define B_CP_REG_AC_ANG_READ__M                                      0xFFFF
#define   B_CP_REG_AC_ANG_READ_INIT                                  0x0

#define B_CP_REG_AC_ACCU_REAL0__A                                    0x1410060
#define B_CP_REG_AC_ACCU_REAL0__W                                    8
#define B_CP_REG_AC_ACCU_REAL0__M                                    0xFF
#define   B_CP_REG_AC_ACCU_REAL0_INIT                                0x0

#define B_CP_REG_AC_ACCU_IMAG0__A                                    0x1410061
#define B_CP_REG_AC_ACCU_IMAG0__W                                    8
#define B_CP_REG_AC_ACCU_IMAG0__M                                    0xFF
#define   B_CP_REG_AC_ACCU_IMAG0_INIT                                0x0

#define B_CP_REG_AC_ACCU_REAL1__A                                    0x1410062
#define B_CP_REG_AC_ACCU_REAL1__W                                    8
#define B_CP_REG_AC_ACCU_REAL1__M                                    0xFF
#define   B_CP_REG_AC_ACCU_REAL1_INIT                                0x0

#define B_CP_REG_AC_ACCU_IMAG1__A                                    0x1410063
#define B_CP_REG_AC_ACCU_IMAG1__W                                    8
#define B_CP_REG_AC_ACCU_IMAG1__M                                    0xFF
#define   B_CP_REG_AC_ACCU_IMAG1_INIT                                0x0

#define B_CP_REG_DL_MB_WR_ADDR__A                                    0x1410050
#define B_CP_REG_DL_MB_WR_ADDR__W                                    15
#define B_CP_REG_DL_MB_WR_ADDR__M                                    0x7FFF
#define   B_CP_REG_DL_MB_WR_ADDR_INIT                                0x0

#define B_CP_REG_DL_MB_WR_CTR__A                                     0x1410051
#define B_CP_REG_DL_MB_WR_CTR__W                                     5
#define B_CP_REG_DL_MB_WR_CTR__M                                     0x1F

#define   B_CP_REG_DL_MB_WR_CTR_WORD__B                              2
#define   B_CP_REG_DL_MB_WR_CTR_WORD__W                              3
#define   B_CP_REG_DL_MB_WR_CTR_WORD__M                              0x1C

#define   B_CP_REG_DL_MB_WR_CTR_OBS__B                               1
#define   B_CP_REG_DL_MB_WR_CTR_OBS__W                               1
#define   B_CP_REG_DL_MB_WR_CTR_OBS__M                               0x2

#define   B_CP_REG_DL_MB_WR_CTR_CTR__B                               0
#define   B_CP_REG_DL_MB_WR_CTR_CTR__W                               1
#define   B_CP_REG_DL_MB_WR_CTR_CTR__M                               0x1
#define     B_CP_REG_DL_MB_WR_CTR_INIT                               0x0

#define B_CP_REG_DL_MB_RD_ADDR__A                                    0x1410052
#define B_CP_REG_DL_MB_RD_ADDR__W                                    15
#define B_CP_REG_DL_MB_RD_ADDR__M                                    0x7FFF
#define   B_CP_REG_DL_MB_RD_ADDR_INIT                                0x0

#define B_CP_REG_DL_MB_RD_CTR__A                                     0x1410053
#define B_CP_REG_DL_MB_RD_CTR__W                                     11
#define B_CP_REG_DL_MB_RD_CTR__M                                     0x7FF

#define   B_CP_REG_DL_MB_RD_CTR_TEST__B                              10
#define   B_CP_REG_DL_MB_RD_CTR_TEST__W                              1
#define   B_CP_REG_DL_MB_RD_CTR_TEST__M                              0x400

#define   B_CP_REG_DL_MB_RD_CTR_OFFSET__B                            8
#define   B_CP_REG_DL_MB_RD_CTR_OFFSET__W                            2
#define   B_CP_REG_DL_MB_RD_CTR_OFFSET__M                            0x300

#define   B_CP_REG_DL_MB_RD_CTR_VALID__B                             5
#define   B_CP_REG_DL_MB_RD_CTR_VALID__W                             3
#define   B_CP_REG_DL_MB_RD_CTR_VALID__M                             0xE0

#define   B_CP_REG_DL_MB_RD_CTR_WORD__B                              2
#define   B_CP_REG_DL_MB_RD_CTR_WORD__W                              3
#define   B_CP_REG_DL_MB_RD_CTR_WORD__M                              0x1C

#define   B_CP_REG_DL_MB_RD_CTR_OBS__B                               1
#define   B_CP_REG_DL_MB_RD_CTR_OBS__W                               1
#define   B_CP_REG_DL_MB_RD_CTR_OBS__M                               0x2

#define   B_CP_REG_DL_MB_RD_CTR_CTR__B                               0
#define   B_CP_REG_DL_MB_RD_CTR_CTR__W                               1
#define   B_CP_REG_DL_MB_RD_CTR_CTR__M                               0x1
#define     B_CP_REG_DL_MB_RD_CTR_INIT                               0x0

#define B_CP_BR_BUF_RAM__A                                           0x1420000

#define B_CP_BR_CPL_RAM__A                                           0x1430000

#define B_CP_PB_DL0_RAM__A                                           0x1440000

#define B_CP_PB_DL1_RAM__A                                           0x1450000

#define B_CP_PB_DL2_RAM__A                                           0x1460000

#define B_CE_SID                                                     0xA

#define B_CE_COMM_EXEC__A                                            0x1800000
#define B_CE_COMM_EXEC__W                                            3
#define B_CE_COMM_EXEC__M                                            0x7
#define   B_CE_COMM_EXEC_CTL__B                                      0
#define   B_CE_COMM_EXEC_CTL__W                                      3
#define   B_CE_COMM_EXEC_CTL__M                                      0x7
#define     B_CE_COMM_EXEC_CTL_STOP                                  0x0
#define     B_CE_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_CE_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_CE_COMM_EXEC_CTL_STEP                                  0x3
#define     B_CE_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_CE_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_CE_COMM_STATE__A                                           0x1800001
#define B_CE_COMM_STATE__W                                           16
#define B_CE_COMM_STATE__M                                           0xFFFF
#define B_CE_COMM_MB__A                                              0x1800002
#define B_CE_COMM_MB__W                                              16
#define B_CE_COMM_MB__M                                              0xFFFF
#define B_CE_COMM_SERVICE0__A                                        0x1800003
#define B_CE_COMM_SERVICE0__W                                        16
#define B_CE_COMM_SERVICE0__M                                        0xFFFF
#define B_CE_COMM_SERVICE1__A                                        0x1800004
#define B_CE_COMM_SERVICE1__W                                        16
#define B_CE_COMM_SERVICE1__M                                        0xFFFF
#define B_CE_COMM_INT_STA__A                                         0x1800007
#define B_CE_COMM_INT_STA__W                                         16
#define B_CE_COMM_INT_STA__M                                         0xFFFF
#define B_CE_COMM_INT_MSK__A                                         0x1800008
#define B_CE_COMM_INT_MSK__W                                         16
#define B_CE_COMM_INT_MSK__M                                         0xFFFF

#define B_CE_REG_COMM_EXEC__A                                        0x1810000
#define B_CE_REG_COMM_EXEC__W                                        3
#define B_CE_REG_COMM_EXEC__M                                        0x7
#define   B_CE_REG_COMM_EXEC_CTL__B                                  0
#define   B_CE_REG_COMM_EXEC_CTL__W                                  3
#define   B_CE_REG_COMM_EXEC_CTL__M                                  0x7
#define     B_CE_REG_COMM_EXEC_CTL_STOP                              0x0
#define     B_CE_REG_COMM_EXEC_CTL_ACTIVE                            0x1
#define     B_CE_REG_COMM_EXEC_CTL_HOLD                              0x2
#define     B_CE_REG_COMM_EXEC_CTL_STEP                              0x3

#define B_CE_REG_COMM_MB__A                                          0x1810002
#define B_CE_REG_COMM_MB__W                                          4
#define B_CE_REG_COMM_MB__M                                          0xF
#define   B_CE_REG_COMM_MB_CTR__B                                    0
#define   B_CE_REG_COMM_MB_CTR__W                                    1
#define   B_CE_REG_COMM_MB_CTR__M                                    0x1
#define     B_CE_REG_COMM_MB_CTR_OFF                                 0x0
#define     B_CE_REG_COMM_MB_CTR_ON                                  0x1
#define   B_CE_REG_COMM_MB_OBS__B                                    1
#define   B_CE_REG_COMM_MB_OBS__W                                    1
#define   B_CE_REG_COMM_MB_OBS__M                                    0x2
#define     B_CE_REG_COMM_MB_OBS_OFF                                 0x0
#define     B_CE_REG_COMM_MB_OBS_ON                                  0x2
#define   B_CE_REG_COMM_MB_OBS_SEL__B                                2
#define   B_CE_REG_COMM_MB_OBS_SEL__W                                2
#define   B_CE_REG_COMM_MB_OBS_SEL__M                                0xC
#define     B_CE_REG_COMM_MB_OBS_SEL_FI                              0x0
#define     B_CE_REG_COMM_MB_OBS_SEL_TP                              0x4
#define     B_CE_REG_COMM_MB_OBS_SEL_TI                              0x8
#define     B_CE_REG_COMM_MB_OBS_SEL_FR                              0x8

#define B_CE_REG_COMM_SERVICE0__A                                    0x1810003
#define B_CE_REG_COMM_SERVICE0__W                                    10
#define B_CE_REG_COMM_SERVICE0__M                                    0x3FF
#define   B_CE_REG_COMM_SERVICE0_FT__B                               8
#define   B_CE_REG_COMM_SERVICE0_FT__W                               1
#define   B_CE_REG_COMM_SERVICE0_FT__M                               0x100

#define B_CE_REG_COMM_SERVICE1__A                                    0x1810004
#define B_CE_REG_COMM_SERVICE1__W                                    11
#define B_CE_REG_COMM_SERVICE1__M                                    0x7FF

#define B_CE_REG_COMM_INT_STA__A                                     0x1810007
#define B_CE_REG_COMM_INT_STA__W                                     3
#define B_CE_REG_COMM_INT_STA__M                                     0x7
#define   B_CE_REG_COMM_INT_STA_CE_PE__B                             0
#define   B_CE_REG_COMM_INT_STA_CE_PE__W                             1
#define   B_CE_REG_COMM_INT_STA_CE_PE__M                             0x1
#define   B_CE_REG_COMM_INT_STA_CE_IR__B                             1
#define   B_CE_REG_COMM_INT_STA_CE_IR__W                             1
#define   B_CE_REG_COMM_INT_STA_CE_IR__M                             0x2
#define   B_CE_REG_COMM_INT_STA_CE_FI__B                             2
#define   B_CE_REG_COMM_INT_STA_CE_FI__W                             1
#define   B_CE_REG_COMM_INT_STA_CE_FI__M                             0x4

#define B_CE_REG_COMM_INT_MSK__A                                     0x1810008
#define B_CE_REG_COMM_INT_MSK__W                                     3
#define B_CE_REG_COMM_INT_MSK__M                                     0x7
#define   B_CE_REG_COMM_INT_MSK_CE_PE__B                             0
#define   B_CE_REG_COMM_INT_MSK_CE_PE__W                             1
#define   B_CE_REG_COMM_INT_MSK_CE_PE__M                             0x1
#define   B_CE_REG_COMM_INT_MSK_CE_IR__B                             1
#define   B_CE_REG_COMM_INT_MSK_CE_IR__W                             1
#define   B_CE_REG_COMM_INT_MSK_CE_IR__M                             0x2
#define   B_CE_REG_COMM_INT_MSK_CE_FI__B                             2
#define   B_CE_REG_COMM_INT_MSK_CE_FI__W                             1
#define   B_CE_REG_COMM_INT_MSK_CE_FI__M                             0x4

#define B_CE_REG_2K__A                                               0x1810010
#define B_CE_REG_2K__W                                               1
#define B_CE_REG_2K__M                                               0x1
#define   B_CE_REG_2K_INIT                                           0x0

#define B_CE_REG_TAPSET__A                                           0x1810011
#define B_CE_REG_TAPSET__W                                           4
#define B_CE_REG_TAPSET__M                                           0xF

#define B_CE_REG_TAPSET_MOTION_INIT                                  0x0

#define B_CE_REG_TAPSET_MOTION_NO                                    0x0

#define B_CE_REG_TAPSET_MOTION_LOW                                   0x1

#define B_CE_REG_TAPSET_MOTION_HIGH                                  0x2

#define B_CE_REG_TAPSET_MOTION_HIGH2                                 0x4

#define B_CE_REG_TAPSET_MOTION_UNDEFINED                             0x8

#define B_CE_REG_AVG_POW__A                                          0x1810012
#define B_CE_REG_AVG_POW__W                                          8
#define B_CE_REG_AVG_POW__M                                          0xFF
#define   B_CE_REG_AVG_POW_INIT                                      0x0

#define B_CE_REG_MAX_POW__A                                          0x1810013
#define B_CE_REG_MAX_POW__W                                          8
#define B_CE_REG_MAX_POW__M                                          0xFF
#define   B_CE_REG_MAX_POW_INIT                                      0x0

#define B_CE_REG_ATT__A                                              0x1810014
#define B_CE_REG_ATT__W                                              8
#define B_CE_REG_ATT__M                                              0xFF
#define   B_CE_REG_ATT_INIT                                          0x0

#define B_CE_REG_NRED__A                                             0x1810015
#define B_CE_REG_NRED__W                                             6
#define B_CE_REG_NRED__M                                             0x3F
#define   B_CE_REG_NRED_INIT                                         0x0

#define B_CE_REG_PU_SIGN__A                                          0x1810020
#define B_CE_REG_PU_SIGN__W                                          1
#define B_CE_REG_PU_SIGN__M                                          0x1
#define   B_CE_REG_PU_SIGN_INIT                                      0x0

#define B_CE_REG_PU_MIX__A                                           0x1810021
#define B_CE_REG_PU_MIX__W                                           1
#define B_CE_REG_PU_MIX__M                                           0x1
#define   B_CE_REG_PU_MIX_INIT                                       0x0

#define B_CE_REG_PB_PILOT_REQ__A                                     0x1810030
#define B_CE_REG_PB_PILOT_REQ__W                                     15
#define B_CE_REG_PB_PILOT_REQ__M                                     0x7FFF
#define   B_CE_REG_PB_PILOT_REQ_INIT                                 0x0
#define   B_CE_REG_PB_PILOT_REQ_BUFFER_INDEX__B                      12
#define   B_CE_REG_PB_PILOT_REQ_BUFFER_INDEX__W                      3
#define   B_CE_REG_PB_PILOT_REQ_BUFFER_INDEX__M                      0x7000
#define   B_CE_REG_PB_PILOT_REQ_PILOT_ADR__B                         0
#define   B_CE_REG_PB_PILOT_REQ_PILOT_ADR__W                         12
#define   B_CE_REG_PB_PILOT_REQ_PILOT_ADR__M                         0xFFF

#define B_CE_REG_PB_PILOT_REQ_VALID__A                               0x1810031
#define B_CE_REG_PB_PILOT_REQ_VALID__W                               1
#define B_CE_REG_PB_PILOT_REQ_VALID__M                               0x1
#define   B_CE_REG_PB_PILOT_REQ_VALID_INIT                           0x0

#define B_CE_REG_PB_FREEZE__A                                        0x1810032
#define B_CE_REG_PB_FREEZE__W                                        1
#define B_CE_REG_PB_FREEZE__M                                        0x1
#define   B_CE_REG_PB_FREEZE_INIT                                    0x0

#define B_CE_REG_PB_PILOT_EXP__A                                     0x1810038
#define B_CE_REG_PB_PILOT_EXP__W                                     4
#define B_CE_REG_PB_PILOT_EXP__M                                     0xF
#define   B_CE_REG_PB_PILOT_EXP_INIT                                 0x0

#define B_CE_REG_PB_PILOT_REAL__A                                    0x1810039
#define B_CE_REG_PB_PILOT_REAL__W                                    10
#define B_CE_REG_PB_PILOT_REAL__M                                    0x3FF
#define   B_CE_REG_PB_PILOT_REAL_INIT                                0x0

#define B_CE_REG_PB_PILOT_IMAG__A                                    0x181003A
#define B_CE_REG_PB_PILOT_IMAG__W                                    10
#define B_CE_REG_PB_PILOT_IMAG__M                                    0x3FF
#define   B_CE_REG_PB_PILOT_IMAG_INIT                                0x0

#define B_CE_REG_PB_SMBNR__A                                         0x181003B
#define B_CE_REG_PB_SMBNR__W                                         5
#define B_CE_REG_PB_SMBNR__M                                         0x1F
#define   B_CE_REG_PB_SMBNR_INIT                                     0x0

#define B_CE_REG_NE_PILOT_REQ__A                                     0x1810040
#define B_CE_REG_NE_PILOT_REQ__W                                     12
#define B_CE_REG_NE_PILOT_REQ__M                                     0xFFF
#define   B_CE_REG_NE_PILOT_REQ_INIT                                 0x0

#define B_CE_REG_NE_PILOT_REQ_VALID__A                               0x1810041
#define B_CE_REG_NE_PILOT_REQ_VALID__W                               2
#define B_CE_REG_NE_PILOT_REQ_VALID__M                               0x3
#define   B_CE_REG_NE_PILOT_REQ_VALID_INIT                           0x0
#define   B_CE_REG_NE_PILOT_REQ_VALID_WRITE_VALID__B                 1
#define   B_CE_REG_NE_PILOT_REQ_VALID_WRITE_VALID__W                 1
#define   B_CE_REG_NE_PILOT_REQ_VALID_WRITE_VALID__M                 0x2
#define   B_CE_REG_NE_PILOT_REQ_VALID_READ_VALID__B                  0
#define   B_CE_REG_NE_PILOT_REQ_VALID_READ_VALID__W                  1
#define   B_CE_REG_NE_PILOT_REQ_VALID_READ_VALID__M                  0x1

#define B_CE_REG_NE_PILOT_DATA__A                                    0x1810042
#define B_CE_REG_NE_PILOT_DATA__W                                    10
#define B_CE_REG_NE_PILOT_DATA__M                                    0x3FF
#define   B_CE_REG_NE_PILOT_DATA_INIT                                0x0

#define B_CE_REG_NE_ERR_SELECT__A                                    0x1810043
#define B_CE_REG_NE_ERR_SELECT__W                                    5
#define B_CE_REG_NE_ERR_SELECT__M                                    0x1F
#define   B_CE_REG_NE_ERR_SELECT_INIT                                0x7

#define   B_CE_REG_NE_ERR_SELECT_MAX_UPD__B                          4
#define   B_CE_REG_NE_ERR_SELECT_MAX_UPD__W                          1
#define   B_CE_REG_NE_ERR_SELECT_MAX_UPD__M                          0x10

#define   B_CE_REG_NE_ERR_SELECT_MED_MATCH__B                        3
#define   B_CE_REG_NE_ERR_SELECT_MED_MATCH__W                        1
#define   B_CE_REG_NE_ERR_SELECT_MED_MATCH__M                        0x8

#define   B_CE_REG_NE_ERR_SELECT_RESET_RAM__B                        2
#define   B_CE_REG_NE_ERR_SELECT_RESET_RAM__W                        1
#define   B_CE_REG_NE_ERR_SELECT_RESET_RAM__M                        0x4

#define   B_CE_REG_NE_ERR_SELECT_FD_ENABLE__B                        1
#define   B_CE_REG_NE_ERR_SELECT_FD_ENABLE__W                        1
#define   B_CE_REG_NE_ERR_SELECT_FD_ENABLE__M                        0x2

#define   B_CE_REG_NE_ERR_SELECT_TD_ENABLE__B                        0
#define   B_CE_REG_NE_ERR_SELECT_TD_ENABLE__W                        1
#define   B_CE_REG_NE_ERR_SELECT_TD_ENABLE__M                        0x1

#define B_CE_REG_NE_TD_CAL__A                                        0x1810044
#define B_CE_REG_NE_TD_CAL__W                                        9
#define B_CE_REG_NE_TD_CAL__M                                        0x1FF
#define   B_CE_REG_NE_TD_CAL_INIT                                    0x1E8

#define B_CE_REG_NE_FD_CAL__A                                        0x1810045
#define B_CE_REG_NE_FD_CAL__W                                        9
#define B_CE_REG_NE_FD_CAL__M                                        0x1FF
#define   B_CE_REG_NE_FD_CAL_INIT                                    0x1D9

#define B_CE_REG_NE_MIXAVG__A                                        0x1810046
#define B_CE_REG_NE_MIXAVG__W                                        3
#define B_CE_REG_NE_MIXAVG__M                                        0x7
#define   B_CE_REG_NE_MIXAVG_INIT                                    0x6

#define B_CE_REG_NE_NUPD_OFS__A                                      0x1810047
#define B_CE_REG_NE_NUPD_OFS__W                                      4
#define B_CE_REG_NE_NUPD_OFS__M                                      0xF
#define   B_CE_REG_NE_NUPD_OFS_INIT                                  0x4

#define B_CE_REG_NE_TD_POW__A                                        0x1810048
#define B_CE_REG_NE_TD_POW__W                                        15
#define B_CE_REG_NE_TD_POW__M                                        0x7FFF
#define   B_CE_REG_NE_TD_POW_INIT                                    0x0

#define   B_CE_REG_NE_TD_POW_EXPONENT__B                             10
#define   B_CE_REG_NE_TD_POW_EXPONENT__W                             5
#define   B_CE_REG_NE_TD_POW_EXPONENT__M                             0x7C00

#define   B_CE_REG_NE_TD_POW_MANTISSA__B                             0
#define   B_CE_REG_NE_TD_POW_MANTISSA__W                             10
#define   B_CE_REG_NE_TD_POW_MANTISSA__M                             0x3FF

#define B_CE_REG_NE_FD_POW__A                                        0x1810049
#define B_CE_REG_NE_FD_POW__W                                        15
#define B_CE_REG_NE_FD_POW__M                                        0x7FFF
#define   B_CE_REG_NE_FD_POW_INIT                                    0x0

#define   B_CE_REG_NE_FD_POW_EXPONENT__B                             10
#define   B_CE_REG_NE_FD_POW_EXPONENT__W                             5
#define   B_CE_REG_NE_FD_POW_EXPONENT__M                             0x7C00

#define   B_CE_REG_NE_FD_POW_MANTISSA__B                             0
#define   B_CE_REG_NE_FD_POW_MANTISSA__W                             10
#define   B_CE_REG_NE_FD_POW_MANTISSA__M                             0x3FF

#define B_CE_REG_NE_NEXP_AVG__A                                      0x181004A
#define B_CE_REG_NE_NEXP_AVG__W                                      8
#define B_CE_REG_NE_NEXP_AVG__M                                      0xFF
#define   B_CE_REG_NE_NEXP_AVG_INIT                                  0x0

#define B_CE_REG_NE_OFFSET__A                                        0x181004B
#define B_CE_REG_NE_OFFSET__W                                        9
#define B_CE_REG_NE_OFFSET__M                                        0x1FF
#define   B_CE_REG_NE_OFFSET_INIT                                    0x0

#define B_CE_REG_NE_NUPD_TRH__A                                      0x181004C
#define B_CE_REG_NE_NUPD_TRH__W                                      5
#define B_CE_REG_NE_NUPD_TRH__M                                      0x1F
#define   B_CE_REG_NE_NUPD_TRH_INIT                                  0x14

#define B_CE_REG_PE_NEXP_OFFS__A                                     0x1810050
#define B_CE_REG_PE_NEXP_OFFS__W                                     8
#define B_CE_REG_PE_NEXP_OFFS__M                                     0xFF
#define   B_CE_REG_PE_NEXP_OFFS_INIT                                 0x0

#define B_CE_REG_PE_TIMESHIFT__A                                     0x1810051
#define B_CE_REG_PE_TIMESHIFT__W                                     14
#define B_CE_REG_PE_TIMESHIFT__M                                     0x3FFF
#define   B_CE_REG_PE_TIMESHIFT_INIT                                 0x0

#define B_CE_REG_PE_DIF_REAL_L__A                                    0x1810052
#define B_CE_REG_PE_DIF_REAL_L__W                                    16
#define B_CE_REG_PE_DIF_REAL_L__M                                    0xFFFF
#define   B_CE_REG_PE_DIF_REAL_L_INIT                                0x0

#define B_CE_REG_PE_DIF_IMAG_L__A                                    0x1810053
#define B_CE_REG_PE_DIF_IMAG_L__W                                    16
#define B_CE_REG_PE_DIF_IMAG_L__M                                    0xFFFF
#define   B_CE_REG_PE_DIF_IMAG_L_INIT                                0x0

#define B_CE_REG_PE_DIF_REAL_R__A                                    0x1810054
#define B_CE_REG_PE_DIF_REAL_R__W                                    16
#define B_CE_REG_PE_DIF_REAL_R__M                                    0xFFFF
#define   B_CE_REG_PE_DIF_REAL_R_INIT                                0x0

#define B_CE_REG_PE_DIF_IMAG_R__A                                    0x1810055
#define B_CE_REG_PE_DIF_IMAG_R__W                                    16
#define B_CE_REG_PE_DIF_IMAG_R__M                                    0xFFFF
#define   B_CE_REG_PE_DIF_IMAG_R_INIT                                0x0

#define B_CE_REG_PE_ABS_REAL_L__A                                    0x1810056
#define B_CE_REG_PE_ABS_REAL_L__W                                    16
#define B_CE_REG_PE_ABS_REAL_L__M                                    0xFFFF
#define   B_CE_REG_PE_ABS_REAL_L_INIT                                0x0

#define B_CE_REG_PE_ABS_IMAG_L__A                                    0x1810057
#define B_CE_REG_PE_ABS_IMAG_L__W                                    16
#define B_CE_REG_PE_ABS_IMAG_L__M                                    0xFFFF
#define   B_CE_REG_PE_ABS_IMAG_L_INIT                                0x0

#define B_CE_REG_PE_ABS_REAL_R__A                                    0x1810058
#define B_CE_REG_PE_ABS_REAL_R__W                                    16
#define B_CE_REG_PE_ABS_REAL_R__M                                    0xFFFF
#define   B_CE_REG_PE_ABS_REAL_R_INIT                                0x0

#define B_CE_REG_PE_ABS_IMAG_R__A                                    0x1810059
#define B_CE_REG_PE_ABS_IMAG_R__W                                    16
#define B_CE_REG_PE_ABS_IMAG_R__M                                    0xFFFF
#define   B_CE_REG_PE_ABS_IMAG_R_INIT                                0x0

#define B_CE_REG_PE_ABS_EXP_L__A                                     0x181005A
#define B_CE_REG_PE_ABS_EXP_L__W                                     5
#define B_CE_REG_PE_ABS_EXP_L__M                                     0x1F
#define   B_CE_REG_PE_ABS_EXP_L_INIT                                 0x0

#define B_CE_REG_PE_ABS_EXP_R__A                                     0x181005B
#define B_CE_REG_PE_ABS_EXP_R__W                                     5
#define B_CE_REG_PE_ABS_EXP_R__M                                     0x1F
#define   B_CE_REG_PE_ABS_EXP_R_INIT                                 0x0

#define B_CE_REG_TP_UPDATE_MODE__A                                   0x1810060
#define B_CE_REG_TP_UPDATE_MODE__W                                   1
#define B_CE_REG_TP_UPDATE_MODE__M                                   0x1
#define   B_CE_REG_TP_UPDATE_MODE_INIT                               0x0

#define B_CE_REG_TP_LMS_TAP_ON__A                                    0x1810061
#define B_CE_REG_TP_LMS_TAP_ON__W                                    1
#define B_CE_REG_TP_LMS_TAP_ON__M                                    0x1

#define B_CE_REG_TP_A0_TAP_NEW__A                                    0x1810064
#define B_CE_REG_TP_A0_TAP_NEW__W                                    10
#define B_CE_REG_TP_A0_TAP_NEW__M                                    0x3FF

#define B_CE_REG_TP_A0_TAP_NEW_VALID__A                              0x1810065
#define B_CE_REG_TP_A0_TAP_NEW_VALID__W                              1
#define B_CE_REG_TP_A0_TAP_NEW_VALID__M                              0x1

#define B_CE_REG_TP_A0_MU_LMS_STEP__A                                0x1810066
#define B_CE_REG_TP_A0_MU_LMS_STEP__W                                5
#define B_CE_REG_TP_A0_MU_LMS_STEP__M                                0x1F

#define B_CE_REG_TP_A0_TAP_CURR__A                                   0x1810067
#define B_CE_REG_TP_A0_TAP_CURR__W                                   10
#define B_CE_REG_TP_A0_TAP_CURR__M                                   0x3FF

#define B_CE_REG_TP_A1_TAP_NEW__A                                    0x1810068
#define B_CE_REG_TP_A1_TAP_NEW__W                                    10
#define B_CE_REG_TP_A1_TAP_NEW__M                                    0x3FF

#define B_CE_REG_TP_A1_TAP_NEW_VALID__A                              0x1810069
#define B_CE_REG_TP_A1_TAP_NEW_VALID__W                              1
#define B_CE_REG_TP_A1_TAP_NEW_VALID__M                              0x1

#define B_CE_REG_TP_A1_MU_LMS_STEP__A                                0x181006A
#define B_CE_REG_TP_A1_MU_LMS_STEP__W                                5
#define B_CE_REG_TP_A1_MU_LMS_STEP__M                                0x1F

#define B_CE_REG_TP_A1_TAP_CURR__A                                   0x181006B
#define B_CE_REG_TP_A1_TAP_CURR__W                                   10
#define B_CE_REG_TP_A1_TAP_CURR__M                                   0x3FF

#define B_CE_REG_TP_DOPP_ENERGY__A                                   0x181006C
#define B_CE_REG_TP_DOPP_ENERGY__W                                   15
#define B_CE_REG_TP_DOPP_ENERGY__M                                   0x7FFF
#define   B_CE_REG_TP_DOPP_ENERGY_INIT                               0x0

#define   B_CE_REG_TP_DOPP_ENERGY_EXPONENT__B                        10
#define   B_CE_REG_TP_DOPP_ENERGY_EXPONENT__W                        5
#define   B_CE_REG_TP_DOPP_ENERGY_EXPONENT__M                        0x7C00

#define   B_CE_REG_TP_DOPP_ENERGY_MANTISSA__B                        0
#define   B_CE_REG_TP_DOPP_ENERGY_MANTISSA__W                        10
#define   B_CE_REG_TP_DOPP_ENERGY_MANTISSA__M                        0x3FF

#define B_CE_REG_TP_DOPP_DIFF_ENERGY__A                              0x181006D
#define B_CE_REG_TP_DOPP_DIFF_ENERGY__W                              15
#define B_CE_REG_TP_DOPP_DIFF_ENERGY__M                              0x7FFF
#define   B_CE_REG_TP_DOPP_DIFF_ENERGY_INIT                          0x0

#define   B_CE_REG_TP_DOPP_DIFF_ENERGY_EXPONENT__B                   10
#define   B_CE_REG_TP_DOPP_DIFF_ENERGY_EXPONENT__W                   5
#define   B_CE_REG_TP_DOPP_DIFF_ENERGY_EXPONENT__M                   0x7C00

#define   B_CE_REG_TP_DOPP_DIFF_ENERGY_MANTISSA__B                   0
#define   B_CE_REG_TP_DOPP_DIFF_ENERGY_MANTISSA__W                   10
#define   B_CE_REG_TP_DOPP_DIFF_ENERGY_MANTISSA__M                   0x3FF

#define B_CE_REG_TP_A0_TAP_ENERGY__A                                 0x181006E
#define B_CE_REG_TP_A0_TAP_ENERGY__W                                 15
#define B_CE_REG_TP_A0_TAP_ENERGY__M                                 0x7FFF
#define   B_CE_REG_TP_A0_TAP_ENERGY_INIT                             0x0

#define   B_CE_REG_TP_A0_TAP_ENERGY_EXPONENT__B                      10
#define   B_CE_REG_TP_A0_TAP_ENERGY_EXPONENT__W                      5
#define   B_CE_REG_TP_A0_TAP_ENERGY_EXPONENT__M                      0x7C00

#define   B_CE_REG_TP_A0_TAP_ENERGY_MANTISSA__B                      0
#define   B_CE_REG_TP_A0_TAP_ENERGY_MANTISSA__W                      10
#define   B_CE_REG_TP_A0_TAP_ENERGY_MANTISSA__M                      0x3FF

#define B_CE_REG_TP_A1_TAP_ENERGY__A                                 0x181006F
#define B_CE_REG_TP_A1_TAP_ENERGY__W                                 15
#define B_CE_REG_TP_A1_TAP_ENERGY__M                                 0x7FFF
#define   B_CE_REG_TP_A1_TAP_ENERGY_INIT                             0x0

#define   B_CE_REG_TP_A1_TAP_ENERGY_EXPONENT__B                      10
#define   B_CE_REG_TP_A1_TAP_ENERGY_EXPONENT__W                      5
#define   B_CE_REG_TP_A1_TAP_ENERGY_EXPONENT__M                      0x7C00

#define   B_CE_REG_TP_A1_TAP_ENERGY_MANTISSA__B                      0
#define   B_CE_REG_TP_A1_TAP_ENERGY_MANTISSA__W                      10
#define   B_CE_REG_TP_A1_TAP_ENERGY_MANTISSA__M                      0x3FF

#define B_CE_REG_TI_SYM_CNT__A                                       0x1810072
#define B_CE_REG_TI_SYM_CNT__W                                       6
#define B_CE_REG_TI_SYM_CNT__M                                       0x3F
#define   B_CE_REG_TI_SYM_CNT_INIT                                   0x0

#define B_CE_REG_TI_PHN_ENABLE__A                                    0x1810073
#define B_CE_REG_TI_PHN_ENABLE__W                                    1
#define B_CE_REG_TI_PHN_ENABLE__M                                    0x1
#define   B_CE_REG_TI_PHN_ENABLE_INIT                                0x0

#define B_CE_REG_TI_SHIFT__A                                         0x1810074
#define B_CE_REG_TI_SHIFT__W                                         2
#define B_CE_REG_TI_SHIFT__M                                         0x3
#define   B_CE_REG_TI_SHIFT_INIT                                     0x0

#define B_CE_REG_TI_SLOW__A                                          0x1810075
#define B_CE_REG_TI_SLOW__W                                          1
#define B_CE_REG_TI_SLOW__M                                          0x1
#define   B_CE_REG_TI_SLOW_INIT                                      0x0

#define B_CE_REG_TI_MGAIN__A                                         0x1810076
#define B_CE_REG_TI_MGAIN__W                                         8
#define B_CE_REG_TI_MGAIN__M                                         0xFF
#define   B_CE_REG_TI_MGAIN_INIT                                     0x0

#define B_CE_REG_TI_ACCU1__A                                         0x1810077
#define B_CE_REG_TI_ACCU1__W                                         8
#define B_CE_REG_TI_ACCU1__M                                         0xFF
#define   B_CE_REG_TI_ACCU1_INIT                                     0x0

#define B_CE_REG_NI_PER_LEFT__A                                      0x18100B0
#define B_CE_REG_NI_PER_LEFT__W                                      5
#define B_CE_REG_NI_PER_LEFT__M                                      0x1F
#define   B_CE_REG_NI_PER_LEFT_INIT                                  0xE

#define B_CE_REG_NI_PER_RIGHT__A                                     0x18100B1
#define B_CE_REG_NI_PER_RIGHT__W                                     5
#define B_CE_REG_NI_PER_RIGHT__M                                     0x1F
#define   B_CE_REG_NI_PER_RIGHT_INIT                                 0x7

#define B_CE_REG_NI_POS_LR__A                                        0x18100B2
#define B_CE_REG_NI_POS_LR__W                                        9
#define B_CE_REG_NI_POS_LR__M                                        0x1FF
#define   B_CE_REG_NI_POS_LR_INIT                                    0xA0

#define B_CE_REG_FI_SHT_INCR__A                                      0x1810090
#define B_CE_REG_FI_SHT_INCR__W                                      7
#define B_CE_REG_FI_SHT_INCR__M                                      0x7F
#define   B_CE_REG_FI_SHT_INCR_INIT                                  0x9

#define B_CE_REG_FI_EXP_NORM__A                                      0x1810091
#define B_CE_REG_FI_EXP_NORM__W                                      4
#define B_CE_REG_FI_EXP_NORM__M                                      0xF
#define   B_CE_REG_FI_EXP_NORM_INIT                                  0x4

#define B_CE_REG_FI_SUPR_VAL__A                                      0x1810092
#define B_CE_REG_FI_SUPR_VAL__W                                      1
#define B_CE_REG_FI_SUPR_VAL__M                                      0x1
#define   B_CE_REG_FI_SUPR_VAL_INIT                                  0x1

#define B_CE_REG_IR_INPUTSEL__A                                      0x18100A0
#define B_CE_REG_IR_INPUTSEL__W                                      1
#define B_CE_REG_IR_INPUTSEL__M                                      0x1
#define   B_CE_REG_IR_INPUTSEL_INIT                                  0x0

#define B_CE_REG_IR_STARTPOS__A                                      0x18100A1
#define B_CE_REG_IR_STARTPOS__W                                      8
#define B_CE_REG_IR_STARTPOS__M                                      0xFF
#define   B_CE_REG_IR_STARTPOS_INIT                                  0x0

#define B_CE_REG_IR_NEXP_THRES__A                                    0x18100A2
#define B_CE_REG_IR_NEXP_THRES__W                                    8
#define B_CE_REG_IR_NEXP_THRES__M                                    0xFF
#define   B_CE_REG_IR_NEXP_THRES_INIT                                0x0

#define B_CE_REG_IR_LENGTH__A                                        0x18100A3
#define B_CE_REG_IR_LENGTH__W                                        4
#define B_CE_REG_IR_LENGTH__M                                        0xF
#define   B_CE_REG_IR_LENGTH_INIT                                    0x0

#define B_CE_REG_IR_FREQ__A                                          0x18100A4
#define B_CE_REG_IR_FREQ__W                                          11
#define B_CE_REG_IR_FREQ__M                                          0x7FF
#define   B_CE_REG_IR_FREQ_INIT                                      0x0

#define B_CE_REG_IR_FREQINC__A                                       0x18100A5
#define B_CE_REG_IR_FREQINC__W                                       11
#define B_CE_REG_IR_FREQINC__M                                       0x7FF
#define   B_CE_REG_IR_FREQINC_INIT                                   0x0

#define B_CE_REG_IR_KAISINC__A                                       0x18100A6
#define B_CE_REG_IR_KAISINC__W                                       15
#define B_CE_REG_IR_KAISINC__M                                       0x7FFF
#define   B_CE_REG_IR_KAISINC_INIT                                   0x0

#define B_CE_REG_IR_CTL__A                                           0x18100A7
#define B_CE_REG_IR_CTL__W                                           3
#define B_CE_REG_IR_CTL__M                                           0x7
#define   B_CE_REG_IR_CTL_INIT                                       0x0

#define B_CE_REG_IR_REAL__A                                          0x18100A8
#define B_CE_REG_IR_REAL__W                                          16
#define B_CE_REG_IR_REAL__M                                          0xFFFF
#define   B_CE_REG_IR_REAL_INIT                                      0x0

#define B_CE_REG_IR_IMAG__A                                          0x18100A9
#define B_CE_REG_IR_IMAG__W                                          16
#define B_CE_REG_IR_IMAG__M                                          0xFFFF
#define   B_CE_REG_IR_IMAG_INIT                                      0x0

#define B_CE_REG_IR_INDEX__A                                         0x18100AA
#define B_CE_REG_IR_INDEX__W                                         12
#define B_CE_REG_IR_INDEX__M                                         0xFFF
#define   B_CE_REG_IR_INDEX_INIT                                     0x0

#define B_CE_REG_FR_COMM_EXEC__A                                     0x1820000
#define B_CE_REG_FR_COMM_EXEC__W                                     1
#define B_CE_REG_FR_COMM_EXEC__M                                     0x1

#define B_CE_REG_FR_TREAL00__A                                       0x1820010
#define B_CE_REG_FR_TREAL00__W                                       11
#define B_CE_REG_FR_TREAL00__M                                       0x7FF
#define   B_CE_REG_FR_TREAL00_INIT                                   0x52

#define B_CE_REG_FR_TIMAG00__A                                       0x1820011
#define B_CE_REG_FR_TIMAG00__W                                       11
#define B_CE_REG_FR_TIMAG00__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG00_INIT                                   0x0

#define B_CE_REG_FR_TREAL01__A                                       0x1820012
#define B_CE_REG_FR_TREAL01__W                                       11
#define B_CE_REG_FR_TREAL01__M                                       0x7FF
#define   B_CE_REG_FR_TREAL01_INIT                                   0x52

#define B_CE_REG_FR_TIMAG01__A                                       0x1820013
#define B_CE_REG_FR_TIMAG01__W                                       11
#define B_CE_REG_FR_TIMAG01__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG01_INIT                                   0x0

#define B_CE_REG_FR_TREAL02__A                                       0x1820014
#define B_CE_REG_FR_TREAL02__W                                       11
#define B_CE_REG_FR_TREAL02__M                                       0x7FF
#define   B_CE_REG_FR_TREAL02_INIT                                   0x52

#define B_CE_REG_FR_TIMAG02__A                                       0x1820015
#define B_CE_REG_FR_TIMAG02__W                                       11
#define B_CE_REG_FR_TIMAG02__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG02_INIT                                   0x0

#define B_CE_REG_FR_TREAL03__A                                       0x1820016
#define B_CE_REG_FR_TREAL03__W                                       11
#define B_CE_REG_FR_TREAL03__M                                       0x7FF
#define   B_CE_REG_FR_TREAL03_INIT                                   0x52

#define B_CE_REG_FR_TIMAG03__A                                       0x1820017
#define B_CE_REG_FR_TIMAG03__W                                       11
#define B_CE_REG_FR_TIMAG03__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG03_INIT                                   0x0

#define B_CE_REG_FR_TREAL04__A                                       0x1820018
#define B_CE_REG_FR_TREAL04__W                                       11
#define B_CE_REG_FR_TREAL04__M                                       0x7FF
#define   B_CE_REG_FR_TREAL04_INIT                                   0x52

#define B_CE_REG_FR_TIMAG04__A                                       0x1820019
#define B_CE_REG_FR_TIMAG04__W                                       11
#define B_CE_REG_FR_TIMAG04__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG04_INIT                                   0x0

#define B_CE_REG_FR_TREAL05__A                                       0x182001A
#define B_CE_REG_FR_TREAL05__W                                       11
#define B_CE_REG_FR_TREAL05__M                                       0x7FF
#define   B_CE_REG_FR_TREAL05_INIT                                   0x52

#define B_CE_REG_FR_TIMAG05__A                                       0x182001B
#define B_CE_REG_FR_TIMAG05__W                                       11
#define B_CE_REG_FR_TIMAG05__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG05_INIT                                   0x0

#define B_CE_REG_FR_TREAL06__A                                       0x182001C
#define B_CE_REG_FR_TREAL06__W                                       11
#define B_CE_REG_FR_TREAL06__M                                       0x7FF
#define   B_CE_REG_FR_TREAL06_INIT                                   0x52

#define B_CE_REG_FR_TIMAG06__A                                       0x182001D
#define B_CE_REG_FR_TIMAG06__W                                       11
#define B_CE_REG_FR_TIMAG06__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG06_INIT                                   0x0

#define B_CE_REG_FR_TREAL07__A                                       0x182001E
#define B_CE_REG_FR_TREAL07__W                                       11
#define B_CE_REG_FR_TREAL07__M                                       0x7FF
#define   B_CE_REG_FR_TREAL07_INIT                                   0x52

#define B_CE_REG_FR_TIMAG07__A                                       0x182001F
#define B_CE_REG_FR_TIMAG07__W                                       11
#define B_CE_REG_FR_TIMAG07__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG07_INIT                                   0x0

#define B_CE_REG_FR_TREAL08__A                                       0x1820020
#define B_CE_REG_FR_TREAL08__W                                       11
#define B_CE_REG_FR_TREAL08__M                                       0x7FF
#define   B_CE_REG_FR_TREAL08_INIT                                   0x52

#define B_CE_REG_FR_TIMAG08__A                                       0x1820021
#define B_CE_REG_FR_TIMAG08__W                                       11
#define B_CE_REG_FR_TIMAG08__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG08_INIT                                   0x0

#define B_CE_REG_FR_TREAL09__A                                       0x1820022
#define B_CE_REG_FR_TREAL09__W                                       11
#define B_CE_REG_FR_TREAL09__M                                       0x7FF
#define   B_CE_REG_FR_TREAL09_INIT                                   0x52

#define B_CE_REG_FR_TIMAG09__A                                       0x1820023
#define B_CE_REG_FR_TIMAG09__W                                       11
#define B_CE_REG_FR_TIMAG09__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG09_INIT                                   0x0

#define B_CE_REG_FR_TREAL10__A                                       0x1820024
#define B_CE_REG_FR_TREAL10__W                                       11
#define B_CE_REG_FR_TREAL10__M                                       0x7FF
#define   B_CE_REG_FR_TREAL10_INIT                                   0x52

#define B_CE_REG_FR_TIMAG10__A                                       0x1820025
#define B_CE_REG_FR_TIMAG10__W                                       11
#define B_CE_REG_FR_TIMAG10__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG10_INIT                                   0x0

#define B_CE_REG_FR_TREAL11__A                                       0x1820026
#define B_CE_REG_FR_TREAL11__W                                       11
#define B_CE_REG_FR_TREAL11__M                                       0x7FF
#define   B_CE_REG_FR_TREAL11_INIT                                   0x52

#define B_CE_REG_FR_TIMAG11__A                                       0x1820027
#define B_CE_REG_FR_TIMAG11__W                                       11
#define B_CE_REG_FR_TIMAG11__M                                       0x7FF
#define   B_CE_REG_FR_TIMAG11_INIT                                   0x0

#define B_CE_REG_FR_MID_TAP__A                                       0x1820028
#define B_CE_REG_FR_MID_TAP__W                                       11
#define B_CE_REG_FR_MID_TAP__M                                       0x7FF
#define   B_CE_REG_FR_MID_TAP_INIT                                   0x51

#define B_CE_REG_FR_SQS_G00__A                                       0x1820029
#define B_CE_REG_FR_SQS_G00__W                                       8
#define B_CE_REG_FR_SQS_G00__M                                       0xFF
#define   B_CE_REG_FR_SQS_G00_INIT                                   0xB

#define B_CE_REG_FR_SQS_G01__A                                       0x182002A
#define B_CE_REG_FR_SQS_G01__W                                       8
#define B_CE_REG_FR_SQS_G01__M                                       0xFF
#define   B_CE_REG_FR_SQS_G01_INIT                                   0xB

#define B_CE_REG_FR_SQS_G02__A                                       0x182002B
#define B_CE_REG_FR_SQS_G02__W                                       8
#define B_CE_REG_FR_SQS_G02__M                                       0xFF
#define   B_CE_REG_FR_SQS_G02_INIT                                   0xB

#define B_CE_REG_FR_SQS_G03__A                                       0x182002C
#define B_CE_REG_FR_SQS_G03__W                                       8
#define B_CE_REG_FR_SQS_G03__M                                       0xFF
#define   B_CE_REG_FR_SQS_G03_INIT                                   0xB

#define B_CE_REG_FR_SQS_G04__A                                       0x182002D
#define B_CE_REG_FR_SQS_G04__W                                       8
#define B_CE_REG_FR_SQS_G04__M                                       0xFF
#define   B_CE_REG_FR_SQS_G04_INIT                                   0xB

#define B_CE_REG_FR_SQS_G05__A                                       0x182002E
#define B_CE_REG_FR_SQS_G05__W                                       8
#define B_CE_REG_FR_SQS_G05__M                                       0xFF
#define   B_CE_REG_FR_SQS_G05_INIT                                   0xB

#define B_CE_REG_FR_SQS_G06__A                                       0x182002F
#define B_CE_REG_FR_SQS_G06__W                                       8
#define B_CE_REG_FR_SQS_G06__M                                       0xFF
#define   B_CE_REG_FR_SQS_G06_INIT                                   0xB

#define B_CE_REG_FR_SQS_G07__A                                       0x1820030
#define B_CE_REG_FR_SQS_G07__W                                       8
#define B_CE_REG_FR_SQS_G07__M                                       0xFF
#define   B_CE_REG_FR_SQS_G07_INIT                                   0xB

#define B_CE_REG_FR_SQS_G08__A                                       0x1820031
#define B_CE_REG_FR_SQS_G08__W                                       8
#define B_CE_REG_FR_SQS_G08__M                                       0xFF
#define   B_CE_REG_FR_SQS_G08_INIT                                   0xB

#define B_CE_REG_FR_SQS_G09__A                                       0x1820032
#define B_CE_REG_FR_SQS_G09__W                                       8
#define B_CE_REG_FR_SQS_G09__M                                       0xFF
#define   B_CE_REG_FR_SQS_G09_INIT                                   0xB

#define B_CE_REG_FR_SQS_G10__A                                       0x1820033
#define B_CE_REG_FR_SQS_G10__W                                       8
#define B_CE_REG_FR_SQS_G10__M                                       0xFF
#define   B_CE_REG_FR_SQS_G10_INIT                                   0xB

#define B_CE_REG_FR_SQS_G11__A                                       0x1820034
#define B_CE_REG_FR_SQS_G11__W                                       8
#define B_CE_REG_FR_SQS_G11__M                                       0xFF
#define   B_CE_REG_FR_SQS_G11_INIT                                   0xB

#define B_CE_REG_FR_SQS_G12__A                                       0x1820035
#define B_CE_REG_FR_SQS_G12__W                                       8
#define B_CE_REG_FR_SQS_G12__M                                       0xFF
#define   B_CE_REG_FR_SQS_G12_INIT                                   0x5

#define B_CE_REG_FR_RIO_G00__A                                       0x1820036
#define B_CE_REG_FR_RIO_G00__W                                       9
#define B_CE_REG_FR_RIO_G00__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G00_INIT                                   0x1FF

#define B_CE_REG_FR_RIO_G01__A                                       0x1820037
#define B_CE_REG_FR_RIO_G01__W                                       9
#define B_CE_REG_FR_RIO_G01__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G01_INIT                                   0x190

#define B_CE_REG_FR_RIO_G02__A                                       0x1820038
#define B_CE_REG_FR_RIO_G02__W                                       9
#define B_CE_REG_FR_RIO_G02__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G02_INIT                                   0x10B

#define B_CE_REG_FR_RIO_G03__A                                       0x1820039
#define B_CE_REG_FR_RIO_G03__W                                       9
#define B_CE_REG_FR_RIO_G03__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G03_INIT                                   0xC8

#define B_CE_REG_FR_RIO_G04__A                                       0x182003A
#define B_CE_REG_FR_RIO_G04__W                                       9
#define B_CE_REG_FR_RIO_G04__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G04_INIT                                   0xA0

#define B_CE_REG_FR_RIO_G05__A                                       0x182003B
#define B_CE_REG_FR_RIO_G05__W                                       9
#define B_CE_REG_FR_RIO_G05__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G05_INIT                                   0x85

#define B_CE_REG_FR_RIO_G06__A                                       0x182003C
#define B_CE_REG_FR_RIO_G06__W                                       9
#define B_CE_REG_FR_RIO_G06__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G06_INIT                                   0x72

#define B_CE_REG_FR_RIO_G07__A                                       0x182003D
#define B_CE_REG_FR_RIO_G07__W                                       9
#define B_CE_REG_FR_RIO_G07__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G07_INIT                                   0x64

#define B_CE_REG_FR_RIO_G08__A                                       0x182003E
#define B_CE_REG_FR_RIO_G08__W                                       9
#define B_CE_REG_FR_RIO_G08__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G08_INIT                                   0x59

#define B_CE_REG_FR_RIO_G09__A                                       0x182003F
#define B_CE_REG_FR_RIO_G09__W                                       9
#define B_CE_REG_FR_RIO_G09__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G09_INIT                                   0x50

#define B_CE_REG_FR_RIO_G10__A                                       0x1820040
#define B_CE_REG_FR_RIO_G10__W                                       9
#define B_CE_REG_FR_RIO_G10__M                                       0x1FF
#define   B_CE_REG_FR_RIO_G10_INIT                                   0x49

#define B_CE_REG_FR_MODE__A                                          0x1820041
#define B_CE_REG_FR_MODE__W                                          9
#define B_CE_REG_FR_MODE__M                                          0x1FF

#define   B_CE_REG_FR_MODE_UPDATE_ENABLE__B                          0
#define   B_CE_REG_FR_MODE_UPDATE_ENABLE__W                          1
#define   B_CE_REG_FR_MODE_UPDATE_ENABLE__M                          0x1

#define   B_CE_REG_FR_MODE_ERROR_SHIFT__B                            1
#define   B_CE_REG_FR_MODE_ERROR_SHIFT__W                            1
#define   B_CE_REG_FR_MODE_ERROR_SHIFT__M                            0x2

#define   B_CE_REG_FR_MODE_NEXP_UPDATE__B                            2
#define   B_CE_REG_FR_MODE_NEXP_UPDATE__W                            1
#define   B_CE_REG_FR_MODE_NEXP_UPDATE__M                            0x4

#define   B_CE_REG_FR_MODE_MANUAL_SHIFT__B                           3
#define   B_CE_REG_FR_MODE_MANUAL_SHIFT__W                           1
#define   B_CE_REG_FR_MODE_MANUAL_SHIFT__M                           0x8

#define   B_CE_REG_FR_MODE_SQUASH_MODE__B                            4
#define   B_CE_REG_FR_MODE_SQUASH_MODE__W                            1
#define   B_CE_REG_FR_MODE_SQUASH_MODE__M                            0x10

#define   B_CE_REG_FR_MODE_UPDATE_MODE__B                            5
#define   B_CE_REG_FR_MODE_UPDATE_MODE__W                            1
#define   B_CE_REG_FR_MODE_UPDATE_MODE__M                            0x20

#define   B_CE_REG_FR_MODE_MID_MODE__B                               6
#define   B_CE_REG_FR_MODE_MID_MODE__W                               1
#define   B_CE_REG_FR_MODE_MID_MODE__M                               0x40

#define   B_CE_REG_FR_MODE_NOISE_MODE__B                             7
#define   B_CE_REG_FR_MODE_NOISE_MODE__W                             1
#define   B_CE_REG_FR_MODE_NOISE_MODE__M                             0x80

#define   B_CE_REG_FR_MODE_NOTCH_MODE__B                             8
#define   B_CE_REG_FR_MODE_NOTCH_MODE__W                             1
#define   B_CE_REG_FR_MODE_NOTCH_MODE__M                             0x100
#define     B_CE_REG_FR_MODE_INIT                                    0xDE

#define B_CE_REG_FR_SQS_TRH__A                                       0x1820042
#define B_CE_REG_FR_SQS_TRH__W                                       8
#define B_CE_REG_FR_SQS_TRH__M                                       0xFF
#define   B_CE_REG_FR_SQS_TRH_INIT                                   0x80

#define B_CE_REG_FR_RIO_GAIN__A                                      0x1820043
#define B_CE_REG_FR_RIO_GAIN__W                                      3
#define B_CE_REG_FR_RIO_GAIN__M                                      0x7
#define   B_CE_REG_FR_RIO_GAIN_INIT                                  0x2

#define B_CE_REG_FR_BYPASS__A                                        0x1820044
#define B_CE_REG_FR_BYPASS__W                                        10
#define B_CE_REG_FR_BYPASS__M                                        0x3FF

#define   B_CE_REG_FR_BYPASS_RUN_IN__B                               0
#define   B_CE_REG_FR_BYPASS_RUN_IN__W                               4
#define   B_CE_REG_FR_BYPASS_RUN_IN__M                               0xF

#define   B_CE_REG_FR_BYPASS_RUN_SEMI_IN__B                          4
#define   B_CE_REG_FR_BYPASS_RUN_SEMI_IN__W                          5
#define   B_CE_REG_FR_BYPASS_RUN_SEMI_IN__M                          0x1F0

#define   B_CE_REG_FR_BYPASS_TOTAL__B                                9
#define   B_CE_REG_FR_BYPASS_TOTAL__W                                1
#define   B_CE_REG_FR_BYPASS_TOTAL__M                                0x200
#define     B_CE_REG_FR_BYPASS_INIT                                  0x13B

#define B_CE_REG_FR_PM_SET__A                                        0x1820045
#define B_CE_REG_FR_PM_SET__W                                        4
#define B_CE_REG_FR_PM_SET__M                                        0xF
#define   B_CE_REG_FR_PM_SET_INIT                                    0x4

#define B_CE_REG_FR_ERR_SH__A                                        0x1820046
#define B_CE_REG_FR_ERR_SH__W                                        4
#define B_CE_REG_FR_ERR_SH__M                                        0xF
#define   B_CE_REG_FR_ERR_SH_INIT                                    0x4

#define B_CE_REG_FR_MAN_SH__A                                        0x1820047
#define B_CE_REG_FR_MAN_SH__W                                        4
#define B_CE_REG_FR_MAN_SH__M                                        0xF
#define   B_CE_REG_FR_MAN_SH_INIT                                    0x7

#define B_CE_REG_FR_TAP_SH__A                                        0x1820048
#define B_CE_REG_FR_TAP_SH__W                                        3
#define B_CE_REG_FR_TAP_SH__M                                        0x7
#define   B_CE_REG_FR_TAP_SH_INIT                                    0x3

#define B_CE_REG_FR_CLIP__A                                          0x1820049
#define B_CE_REG_FR_CLIP__W                                          9
#define B_CE_REG_FR_CLIP__M                                          0x1FF
#define   B_CE_REG_FR_CLIP_INIT                                      0x49

#define B_CE_REG_FR_LEAK_UPD__A                                      0x182004A
#define B_CE_REG_FR_LEAK_UPD__W                                      3
#define B_CE_REG_FR_LEAK_UPD__M                                      0x7
#define   B_CE_REG_FR_LEAK_UPD_INIT                                  0x1

#define B_CE_REG_FR_LEAK_SH__A                                       0x182004B
#define B_CE_REG_FR_LEAK_SH__W                                       3
#define B_CE_REG_FR_LEAK_SH__M                                       0x7
#define   B_CE_REG_FR_LEAK_SH_INIT                                   0x1

#define B_CE_PB_RAM__A                                               0x1830000

#define B_CE_NE_RAM__A                                               0x1840000

#define B_EQ_SID                                                     0xE

#define B_EQ_COMM_EXEC__A                                            0x1C00000
#define B_EQ_COMM_EXEC__W                                            3
#define B_EQ_COMM_EXEC__M                                            0x7
#define   B_EQ_COMM_EXEC_CTL__B                                      0
#define   B_EQ_COMM_EXEC_CTL__W                                      3
#define   B_EQ_COMM_EXEC_CTL__M                                      0x7
#define     B_EQ_COMM_EXEC_CTL_STOP                                  0x0
#define     B_EQ_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_EQ_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_EQ_COMM_EXEC_CTL_STEP                                  0x3
#define     B_EQ_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_EQ_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_EQ_COMM_STATE__A                                           0x1C00001
#define B_EQ_COMM_STATE__W                                           16
#define B_EQ_COMM_STATE__M                                           0xFFFF
#define B_EQ_COMM_MB__A                                              0x1C00002
#define B_EQ_COMM_MB__W                                              16
#define B_EQ_COMM_MB__M                                              0xFFFF
#define B_EQ_COMM_SERVICE0__A                                        0x1C00003
#define B_EQ_COMM_SERVICE0__W                                        16
#define B_EQ_COMM_SERVICE0__M                                        0xFFFF
#define B_EQ_COMM_SERVICE1__A                                        0x1C00004
#define B_EQ_COMM_SERVICE1__W                                        16
#define B_EQ_COMM_SERVICE1__M                                        0xFFFF
#define B_EQ_COMM_INT_STA__A                                         0x1C00007
#define B_EQ_COMM_INT_STA__W                                         16
#define B_EQ_COMM_INT_STA__M                                         0xFFFF
#define B_EQ_COMM_INT_MSK__A                                         0x1C00008
#define B_EQ_COMM_INT_MSK__W                                         16
#define B_EQ_COMM_INT_MSK__M                                         0xFFFF

#define B_EQ_REG_COMM_EXEC__A                                        0x1C10000
#define B_EQ_REG_COMM_EXEC__W                                        3
#define B_EQ_REG_COMM_EXEC__M                                        0x7
#define   B_EQ_REG_COMM_EXEC_CTL__B                                  0
#define   B_EQ_REG_COMM_EXEC_CTL__W                                  3
#define   B_EQ_REG_COMM_EXEC_CTL__M                                  0x7
#define     B_EQ_REG_COMM_EXEC_CTL_STOP                              0x0
#define     B_EQ_REG_COMM_EXEC_CTL_ACTIVE                            0x1
#define     B_EQ_REG_COMM_EXEC_CTL_HOLD                              0x2
#define     B_EQ_REG_COMM_EXEC_CTL_STEP                              0x3

#define B_EQ_REG_COMM_STATE__A                                       0x1C10001
#define B_EQ_REG_COMM_STATE__W                                       4
#define B_EQ_REG_COMM_STATE__M                                       0xF

#define B_EQ_REG_COMM_MB__A                                          0x1C10002
#define B_EQ_REG_COMM_MB__W                                          6
#define B_EQ_REG_COMM_MB__M                                          0x3F
#define   B_EQ_REG_COMM_MB_CTR__B                                    0
#define   B_EQ_REG_COMM_MB_CTR__W                                    1
#define   B_EQ_REG_COMM_MB_CTR__M                                    0x1
#define     B_EQ_REG_COMM_MB_CTR_OFF                                 0x0
#define     B_EQ_REG_COMM_MB_CTR_ON                                  0x1
#define   B_EQ_REG_COMM_MB_OBS__B                                    1
#define   B_EQ_REG_COMM_MB_OBS__W                                    1
#define   B_EQ_REG_COMM_MB_OBS__M                                    0x2
#define     B_EQ_REG_COMM_MB_OBS_OFF                                 0x0
#define     B_EQ_REG_COMM_MB_OBS_ON                                  0x2
#define   B_EQ_REG_COMM_MB_CTR_MUX__B                                2
#define   B_EQ_REG_COMM_MB_CTR_MUX__W                                2
#define   B_EQ_REG_COMM_MB_CTR_MUX__M                                0xC
#define     B_EQ_REG_COMM_MB_CTR_MUX_EQ_OT                           0x0
#define     B_EQ_REG_COMM_MB_CTR_MUX_EQ_RC                           0x4
#define     B_EQ_REG_COMM_MB_CTR_MUX_EQ_IS                           0x8
#define   B_EQ_REG_COMM_MB_OBS_MUX__B                                4
#define   B_EQ_REG_COMM_MB_OBS_MUX__W                                2
#define   B_EQ_REG_COMM_MB_OBS_MUX__M                                0x30
#define     B_EQ_REG_COMM_MB_OBS_MUX_EQ_OT                           0x0
#define     B_EQ_REG_COMM_MB_OBS_MUX_EQ_RC                           0x10
#define     B_EQ_REG_COMM_MB_OBS_MUX_EQ_IS                           0x20
#define     B_EQ_REG_COMM_MB_OBS_MUX_EQ_SN                           0x30

#define B_EQ_REG_COMM_SERVICE0__A                                    0x1C10003
#define B_EQ_REG_COMM_SERVICE0__W                                    10
#define B_EQ_REG_COMM_SERVICE0__M                                    0x3FF

#define B_EQ_REG_COMM_SERVICE1__A                                    0x1C10004
#define B_EQ_REG_COMM_SERVICE1__W                                    11
#define B_EQ_REG_COMM_SERVICE1__M                                    0x7FF

#define B_EQ_REG_COMM_INT_STA__A                                     0x1C10007
#define B_EQ_REG_COMM_INT_STA__W                                     2
#define B_EQ_REG_COMM_INT_STA__M                                     0x3
#define   B_EQ_REG_COMM_INT_STA_TPS_RDY__B                           0
#define   B_EQ_REG_COMM_INT_STA_TPS_RDY__W                           1
#define   B_EQ_REG_COMM_INT_STA_TPS_RDY__M                           0x1
#define   B_EQ_REG_COMM_INT_STA_ERR_RDY__B                           1
#define   B_EQ_REG_COMM_INT_STA_ERR_RDY__W                           1
#define   B_EQ_REG_COMM_INT_STA_ERR_RDY__M                           0x2

#define B_EQ_REG_COMM_INT_MSK__A                                     0x1C10008
#define B_EQ_REG_COMM_INT_MSK__W                                     2
#define B_EQ_REG_COMM_INT_MSK__M                                     0x3
#define   B_EQ_REG_COMM_INT_MSK_TPS_RDY__B                           0
#define   B_EQ_REG_COMM_INT_MSK_TPS_RDY__W                           1
#define   B_EQ_REG_COMM_INT_MSK_TPS_RDY__M                           0x1
#define   B_EQ_REG_COMM_INT_MSK_MER_RDY__B                           1
#define   B_EQ_REG_COMM_INT_MSK_MER_RDY__W                           1
#define   B_EQ_REG_COMM_INT_MSK_MER_RDY__M                           0x2

#define B_EQ_REG_IS_MODE__A                                          0x1C10014
#define B_EQ_REG_IS_MODE__W                                          4
#define B_EQ_REG_IS_MODE__M                                          0xF
#define   B_EQ_REG_IS_MODE_INIT                                      0x0

#define   B_EQ_REG_IS_MODE_LIM_EXP_SEL__B                            0
#define   B_EQ_REG_IS_MODE_LIM_EXP_SEL__W                            1
#define   B_EQ_REG_IS_MODE_LIM_EXP_SEL__M                            0x1
#define     B_EQ_REG_IS_MODE_LIM_EXP_SEL_EXP_SEL_MAX                 0x0
#define     B_EQ_REG_IS_MODE_LIM_EXP_SEL_EXP_SEL_ZER                 0x1

#define   B_EQ_REG_IS_MODE_LIM_CLP_SEL__B                            1
#define   B_EQ_REG_IS_MODE_LIM_CLP_SEL__W                            1
#define   B_EQ_REG_IS_MODE_LIM_CLP_SEL__M                            0x2
#define     B_EQ_REG_IS_MODE_LIM_CLP_SEL_CLP_SEL_ONE                 0x0
#define     B_EQ_REG_IS_MODE_LIM_CLP_SEL_CLP_SEL_TWO                 0x2

#define B_EQ_REG_IS_GAIN_MAN__A                                      0x1C10015
#define B_EQ_REG_IS_GAIN_MAN__W                                      10
#define B_EQ_REG_IS_GAIN_MAN__M                                      0x3FF
#define   B_EQ_REG_IS_GAIN_MAN_INIT                                  0x114

#define B_EQ_REG_IS_GAIN_EXP__A                                      0x1C10016
#define B_EQ_REG_IS_GAIN_EXP__W                                      5
#define B_EQ_REG_IS_GAIN_EXP__M                                      0x1F
#define   B_EQ_REG_IS_GAIN_EXP_INIT                                  0x5

#define B_EQ_REG_IS_CLIP_EXP__A                                      0x1C10017
#define B_EQ_REG_IS_CLIP_EXP__W                                      5
#define B_EQ_REG_IS_CLIP_EXP__M                                      0x1F
#define   B_EQ_REG_IS_CLIP_EXP_INIT                                  0x10

#define B_EQ_REG_DV_MODE__A                                          0x1C1001E
#define B_EQ_REG_DV_MODE__W                                          4
#define B_EQ_REG_DV_MODE__M                                          0xF
#define   B_EQ_REG_DV_MODE_INIT                                      0xF

#define   B_EQ_REG_DV_MODE_CLP_CNT_EVR__B                            0
#define   B_EQ_REG_DV_MODE_CLP_CNT_EVR__W                            1
#define   B_EQ_REG_DV_MODE_CLP_CNT_EVR__M                            0x1
#define     B_EQ_REG_DV_MODE_CLP_CNT_EVR_CLP_REA_DIS                 0x0
#define     B_EQ_REG_DV_MODE_CLP_CNT_EVR_CLP_REA_ENA                 0x1

#define   B_EQ_REG_DV_MODE_CLP_CNT_EVI__B                            1
#define   B_EQ_REG_DV_MODE_CLP_CNT_EVI__W                            1
#define   B_EQ_REG_DV_MODE_CLP_CNT_EVI__M                            0x2
#define     B_EQ_REG_DV_MODE_CLP_CNT_EVI_CLP_IMA_DIS                 0x0
#define     B_EQ_REG_DV_MODE_CLP_CNT_EVI_CLP_IMA_ENA                 0x2

#define   B_EQ_REG_DV_MODE_CLP_REA_ENA__B                            2
#define   B_EQ_REG_DV_MODE_CLP_REA_ENA__W                            1
#define   B_EQ_REG_DV_MODE_CLP_REA_ENA__M                            0x4
#define     B_EQ_REG_DV_MODE_CLP_REA_ENA_CLP_REA_DIS                 0x0
#define     B_EQ_REG_DV_MODE_CLP_REA_ENA_CLP_REA_ENA                 0x4

#define   B_EQ_REG_DV_MODE_CLP_IMA_ENA__B                            3
#define   B_EQ_REG_DV_MODE_CLP_IMA_ENA__W                            1
#define   B_EQ_REG_DV_MODE_CLP_IMA_ENA__M                            0x8
#define     B_EQ_REG_DV_MODE_CLP_IMA_ENA_CLP_IMA_DIS                 0x0
#define     B_EQ_REG_DV_MODE_CLP_IMA_ENA_CLP_IMA_ENA                 0x8

#define B_EQ_REG_DV_POS_CLIP_DAT__A                                  0x1C1001F
#define B_EQ_REG_DV_POS_CLIP_DAT__W                                  16
#define B_EQ_REG_DV_POS_CLIP_DAT__M                                  0xFFFF

#define B_EQ_REG_SN_MODE__A                                          0x1C10028
#define B_EQ_REG_SN_MODE__W                                          8
#define B_EQ_REG_SN_MODE__M                                          0xFF
#define   B_EQ_REG_SN_MODE_INIT                                      0x18

#define   B_EQ_REG_SN_MODE_MODE_0__B                                 0
#define   B_EQ_REG_SN_MODE_MODE_0__W                                 1
#define   B_EQ_REG_SN_MODE_MODE_0__M                                 0x1
#define     B_EQ_REG_SN_MODE_MODE_0_DISABLE                          0x0
#define     B_EQ_REG_SN_MODE_MODE_0_ENABLE                           0x1

#define   B_EQ_REG_SN_MODE_MODE_1__B                                 1
#define   B_EQ_REG_SN_MODE_MODE_1__W                                 1
#define   B_EQ_REG_SN_MODE_MODE_1__M                                 0x2
#define     B_EQ_REG_SN_MODE_MODE_1_DISABLE                          0x0
#define     B_EQ_REG_SN_MODE_MODE_1_ENABLE                           0x2

#define   B_EQ_REG_SN_MODE_MODE_2__B                                 2
#define   B_EQ_REG_SN_MODE_MODE_2__W                                 1
#define   B_EQ_REG_SN_MODE_MODE_2__M                                 0x4
#define     B_EQ_REG_SN_MODE_MODE_2_DISABLE                          0x0
#define     B_EQ_REG_SN_MODE_MODE_2_ENABLE                           0x4

#define   B_EQ_REG_SN_MODE_MODE_3__B                                 3
#define   B_EQ_REG_SN_MODE_MODE_3__W                                 1
#define   B_EQ_REG_SN_MODE_MODE_3__M                                 0x8
#define     B_EQ_REG_SN_MODE_MODE_3_DISABLE                          0x0
#define     B_EQ_REG_SN_MODE_MODE_3_ENABLE                           0x8

#define   B_EQ_REG_SN_MODE_MODE_4__B                                 4
#define   B_EQ_REG_SN_MODE_MODE_4__W                                 1
#define   B_EQ_REG_SN_MODE_MODE_4__M                                 0x10
#define     B_EQ_REG_SN_MODE_MODE_4_DISABLE                          0x0
#define     B_EQ_REG_SN_MODE_MODE_4_ENABLE                           0x10

#define   B_EQ_REG_SN_MODE_MODE_5__B                                 5
#define   B_EQ_REG_SN_MODE_MODE_5__W                                 1
#define   B_EQ_REG_SN_MODE_MODE_5__M                                 0x20
#define     B_EQ_REG_SN_MODE_MODE_5_DISABLE                          0x0
#define     B_EQ_REG_SN_MODE_MODE_5_ENABLE                           0x20

#define   B_EQ_REG_SN_MODE_MODE_6__B                                 6
#define   B_EQ_REG_SN_MODE_MODE_6__W                                 1
#define   B_EQ_REG_SN_MODE_MODE_6__M                                 0x40
#define     B_EQ_REG_SN_MODE_MODE_6_DYNAMIC                          0x0
#define     B_EQ_REG_SN_MODE_MODE_6_STATIC                           0x40

#define   B_EQ_REG_SN_MODE_MODE_7__B                                 7
#define   B_EQ_REG_SN_MODE_MODE_7__W                                 1
#define   B_EQ_REG_SN_MODE_MODE_7__M                                 0x80
#define     B_EQ_REG_SN_MODE_MODE_7_DYNAMIC                          0x0
#define     B_EQ_REG_SN_MODE_MODE_7_STATIC                           0x80

#define B_EQ_REG_SN_PFIX__A                                          0x1C10029
#define B_EQ_REG_SN_PFIX__W                                          8
#define B_EQ_REG_SN_PFIX__M                                          0xFF
#define   B_EQ_REG_SN_PFIX_INIT                                      0x0

#define B_EQ_REG_SN_CEGAIN__A                                        0x1C1002A
#define B_EQ_REG_SN_CEGAIN__W                                        8
#define B_EQ_REG_SN_CEGAIN__M                                        0xFF
#define   B_EQ_REG_SN_CEGAIN_INIT                                    0x30

#define B_EQ_REG_SN_OFFSET__A                                        0x1C1002B
#define B_EQ_REG_SN_OFFSET__W                                        6
#define B_EQ_REG_SN_OFFSET__M                                        0x3F
#define   B_EQ_REG_SN_OFFSET_INIT                                    0x39

#define B_EQ_REG_SN_NULLIFY__A                                       0x1C1002C
#define B_EQ_REG_SN_NULLIFY__W                                       6
#define B_EQ_REG_SN_NULLIFY__M                                       0x3F
#define   B_EQ_REG_SN_NULLIFY_INIT                                   0x0

#define B_EQ_REG_SN_SQUASH__A                                        0x1C1002D
#define B_EQ_REG_SN_SQUASH__W                                        10
#define B_EQ_REG_SN_SQUASH__M                                        0x3FF
#define   B_EQ_REG_SN_SQUASH_INIT                                    0x7

#define   B_EQ_REG_SN_SQUASH_MAN__B                                  0
#define   B_EQ_REG_SN_SQUASH_MAN__W                                  6
#define   B_EQ_REG_SN_SQUASH_MAN__M                                  0x3F

#define   B_EQ_REG_SN_SQUASH_EXP__B                                  6
#define   B_EQ_REG_SN_SQUASH_EXP__W                                  4
#define   B_EQ_REG_SN_SQUASH_EXP__M                                  0x3C0

#define B_EQ_REG_RC_SEL_CAR__A                                       0x1C10032
#define B_EQ_REG_RC_SEL_CAR__W                                       8
#define B_EQ_REG_RC_SEL_CAR__M                                       0xFF
#define   B_EQ_REG_RC_SEL_CAR_INIT                                   0x2
#define   B_EQ_REG_RC_SEL_CAR_DIV__B                                 0
#define   B_EQ_REG_RC_SEL_CAR_DIV__W                                 1
#define   B_EQ_REG_RC_SEL_CAR_DIV__M                                 0x1
#define     B_EQ_REG_RC_SEL_CAR_DIV_OFF                              0x0
#define     B_EQ_REG_RC_SEL_CAR_DIV_ON                               0x1

#define   B_EQ_REG_RC_SEL_CAR_PASS__B                                1
#define   B_EQ_REG_RC_SEL_CAR_PASS__W                                2
#define   B_EQ_REG_RC_SEL_CAR_PASS__M                                0x6
#define     B_EQ_REG_RC_SEL_CAR_PASS_A_CC                            0x0
#define     B_EQ_REG_RC_SEL_CAR_PASS_B_CE                            0x2
#define     B_EQ_REG_RC_SEL_CAR_PASS_C_DRI                           0x4
#define     B_EQ_REG_RC_SEL_CAR_PASS_D_CC                            0x6

#define   B_EQ_REG_RC_SEL_CAR_LOCAL__B                               3
#define   B_EQ_REG_RC_SEL_CAR_LOCAL__W                               2
#define   B_EQ_REG_RC_SEL_CAR_LOCAL__M                               0x18
#define     B_EQ_REG_RC_SEL_CAR_LOCAL_A_CC                           0x0
#define     B_EQ_REG_RC_SEL_CAR_LOCAL_B_CE                           0x8
#define     B_EQ_REG_RC_SEL_CAR_LOCAL_C_DRI                          0x10
#define     B_EQ_REG_RC_SEL_CAR_LOCAL_D_CC                           0x18

#define   B_EQ_REG_RC_SEL_CAR_MEAS__B                                5
#define   B_EQ_REG_RC_SEL_CAR_MEAS__W                                2
#define   B_EQ_REG_RC_SEL_CAR_MEAS__M                                0x60
#define     B_EQ_REG_RC_SEL_CAR_MEAS_A_CC                            0x0
#define     B_EQ_REG_RC_SEL_CAR_MEAS_B_CE                            0x20
#define     B_EQ_REG_RC_SEL_CAR_MEAS_C_DRI                           0x40
#define     B_EQ_REG_RC_SEL_CAR_MEAS_D_CC                            0x60

#define   B_EQ_REG_RC_SEL_CAR_FFTMODE__B                             7
#define   B_EQ_REG_RC_SEL_CAR_FFTMODE__W                             1
#define   B_EQ_REG_RC_SEL_CAR_FFTMODE__M                             0x80
#define     B_EQ_REG_RC_SEL_CAR_FFTMODE_2K                           0x0
#define     B_EQ_REG_RC_SEL_CAR_FFTMODE_8K                           0x80

#define B_EQ_REG_RC_STS__A                                           0x1C10033
#define B_EQ_REG_RC_STS__W                                           14
#define B_EQ_REG_RC_STS__M                                           0x3FFF

#define   B_EQ_REG_RC_STS_DIFF__B                                    0
#define   B_EQ_REG_RC_STS_DIFF__W                                    9
#define   B_EQ_REG_RC_STS_DIFF__M                                    0x1FF

#define   B_EQ_REG_RC_STS_FIRST__B                                   9
#define   B_EQ_REG_RC_STS_FIRST__W                                   1
#define   B_EQ_REG_RC_STS_FIRST__M                                   0x200
#define     B_EQ_REG_RC_STS_FIRST_A_CE                               0x0
#define     B_EQ_REG_RC_STS_FIRST_B_DRI                              0x200

#define   B_EQ_REG_RC_STS_SELEC__B                                   10
#define   B_EQ_REG_RC_STS_SELEC__W                                   1
#define   B_EQ_REG_RC_STS_SELEC__M                                   0x400
#define     B_EQ_REG_RC_STS_SELEC_A_CE                               0x0
#define     B_EQ_REG_RC_STS_SELEC_B_DRI                              0x400

#define   B_EQ_REG_RC_STS_OVERFLOW__B                                11
#define   B_EQ_REG_RC_STS_OVERFLOW__W                                1
#define   B_EQ_REG_RC_STS_OVERFLOW__M                                0x800
#define     B_EQ_REG_RC_STS_OVERFLOW_NO                              0x0
#define     B_EQ_REG_RC_STS_OVERFLOW_YES                             0x800

#define   B_EQ_REG_RC_STS_LOC_PRS__B                                 12
#define   B_EQ_REG_RC_STS_LOC_PRS__W                                 1
#define   B_EQ_REG_RC_STS_LOC_PRS__M                                 0x1000
#define     B_EQ_REG_RC_STS_LOC_PRS_NO                               0x0
#define     B_EQ_REG_RC_STS_LOC_PRS_YES                              0x1000

#define   B_EQ_REG_RC_STS_DRI_PRS__B                                 13
#define   B_EQ_REG_RC_STS_DRI_PRS__W                                 1
#define   B_EQ_REG_RC_STS_DRI_PRS__M                                 0x2000
#define     B_EQ_REG_RC_STS_DRI_PRS_NO                               0x0
#define     B_EQ_REG_RC_STS_DRI_PRS_YES                              0x2000

#define B_EQ_REG_OT_CONST__A                                         0x1C10046
#define B_EQ_REG_OT_CONST__W                                         2
#define B_EQ_REG_OT_CONST__M                                         0x3
#define   B_EQ_REG_OT_CONST_INIT                                     0x2

#define B_EQ_REG_OT_ALPHA__A                                         0x1C10047
#define B_EQ_REG_OT_ALPHA__W                                         2
#define B_EQ_REG_OT_ALPHA__M                                         0x3
#define   B_EQ_REG_OT_ALPHA_INIT                                     0x0

#define B_EQ_REG_OT_QNT_THRES0__A                                    0x1C10048
#define B_EQ_REG_OT_QNT_THRES0__W                                    5
#define B_EQ_REG_OT_QNT_THRES0__M                                    0x1F
#define   B_EQ_REG_OT_QNT_THRES0_INIT                                0x1E

#define B_EQ_REG_OT_QNT_THRES1__A                                    0x1C10049
#define B_EQ_REG_OT_QNT_THRES1__W                                    5
#define B_EQ_REG_OT_QNT_THRES1__M                                    0x1F
#define   B_EQ_REG_OT_QNT_THRES1_INIT                                0x1F

#define B_EQ_REG_OT_CSI_STEP__A                                      0x1C1004A
#define B_EQ_REG_OT_CSI_STEP__W                                      4
#define B_EQ_REG_OT_CSI_STEP__M                                      0xF
#define   B_EQ_REG_OT_CSI_STEP_INIT                                  0x5

#define B_EQ_REG_OT_CSI_OFFSET__A                                    0x1C1004B
#define B_EQ_REG_OT_CSI_OFFSET__W                                    7
#define B_EQ_REG_OT_CSI_OFFSET__M                                    0x7F
#define   B_EQ_REG_OT_CSI_OFFSET_INIT                                0x5

#define B_EQ_REG_OT_CSI_GAIN__A                                      0x1C1004C
#define B_EQ_REG_OT_CSI_GAIN__W                                      8
#define B_EQ_REG_OT_CSI_GAIN__M                                      0xFF
#define   B_EQ_REG_OT_CSI_GAIN_INIT                                  0x2B

#define B_EQ_REG_OT_CSI_MEAN__A                                      0x1C1004D
#define B_EQ_REG_OT_CSI_MEAN__W                                      7
#define B_EQ_REG_OT_CSI_MEAN__M                                      0x7F

#define B_EQ_REG_OT_CSI_VARIANCE__A                                  0x1C1004E
#define B_EQ_REG_OT_CSI_VARIANCE__W                                  7
#define B_EQ_REG_OT_CSI_VARIANCE__M                                  0x7F

#define B_EQ_REG_TD_TPS_INIT__A                                      0x1C10050
#define B_EQ_REG_TD_TPS_INIT__W                                      1
#define B_EQ_REG_TD_TPS_INIT__M                                      0x1
#define   B_EQ_REG_TD_TPS_INIT_INIT                                  0x0
#define   B_EQ_REG_TD_TPS_INIT_POS                                   0x0
#define   B_EQ_REG_TD_TPS_INIT_NEG                                   0x1

#define B_EQ_REG_TD_TPS_SYNC__A                                      0x1C10051
#define B_EQ_REG_TD_TPS_SYNC__W                                      16
#define B_EQ_REG_TD_TPS_SYNC__M                                      0xFFFF
#define   B_EQ_REG_TD_TPS_SYNC_INIT                                  0x0
#define   B_EQ_REG_TD_TPS_SYNC_ODD                                   0x35EE
#define   B_EQ_REG_TD_TPS_SYNC_EVEN                                  0xCA11

#define B_EQ_REG_TD_TPS_LEN__A                                       0x1C10052
#define B_EQ_REG_TD_TPS_LEN__W                                       6
#define B_EQ_REG_TD_TPS_LEN__M                                       0x3F
#define   B_EQ_REG_TD_TPS_LEN_INIT                                   0x0
#define   B_EQ_REG_TD_TPS_LEN_DEF                                    0x17
#define   B_EQ_REG_TD_TPS_LEN_ID_SUP                                 0x1F

#define B_EQ_REG_TD_TPS_FRM_NMB__A                                   0x1C10053
#define B_EQ_REG_TD_TPS_FRM_NMB__W                                   2
#define B_EQ_REG_TD_TPS_FRM_NMB__M                                   0x3
#define   B_EQ_REG_TD_TPS_FRM_NMB_INIT                               0x0
#define   B_EQ_REG_TD_TPS_FRM_NMB_1                                  0x0
#define   B_EQ_REG_TD_TPS_FRM_NMB_2                                  0x1
#define   B_EQ_REG_TD_TPS_FRM_NMB_3                                  0x2
#define   B_EQ_REG_TD_TPS_FRM_NMB_4                                  0x3

#define B_EQ_REG_TD_TPS_CONST__A                                     0x1C10054
#define B_EQ_REG_TD_TPS_CONST__W                                     2
#define B_EQ_REG_TD_TPS_CONST__M                                     0x3
#define   B_EQ_REG_TD_TPS_CONST_INIT                                 0x0
#define   B_EQ_REG_TD_TPS_CONST_QPSK                                 0x0
#define   B_EQ_REG_TD_TPS_CONST_16QAM                                0x1
#define   B_EQ_REG_TD_TPS_CONST_64QAM                                0x2

#define B_EQ_REG_TD_TPS_HINFO__A                                     0x1C10055
#define B_EQ_REG_TD_TPS_HINFO__W                                     3
#define B_EQ_REG_TD_TPS_HINFO__M                                     0x7
#define   B_EQ_REG_TD_TPS_HINFO_INIT                                 0x0
#define   B_EQ_REG_TD_TPS_HINFO_NH                                   0x0
#define   B_EQ_REG_TD_TPS_HINFO_H1                                   0x1
#define   B_EQ_REG_TD_TPS_HINFO_H2                                   0x2
#define   B_EQ_REG_TD_TPS_HINFO_H4                                   0x3

#define B_EQ_REG_TD_TPS_CODE_HP__A                                   0x1C10056
#define B_EQ_REG_TD_TPS_CODE_HP__W                                   3
#define B_EQ_REG_TD_TPS_CODE_HP__M                                   0x7
#define   B_EQ_REG_TD_TPS_CODE_HP_INIT                               0x0
#define   B_EQ_REG_TD_TPS_CODE_HP_1_2                                0x0
#define   B_EQ_REG_TD_TPS_CODE_HP_2_3                                0x1
#define   B_EQ_REG_TD_TPS_CODE_HP_3_4                                0x2
#define   B_EQ_REG_TD_TPS_CODE_HP_5_6                                0x3
#define   B_EQ_REG_TD_TPS_CODE_HP_7_8                                0x4

#define B_EQ_REG_TD_TPS_CODE_LP__A                                   0x1C10057
#define B_EQ_REG_TD_TPS_CODE_LP__W                                   3
#define B_EQ_REG_TD_TPS_CODE_LP__M                                   0x7
#define   B_EQ_REG_TD_TPS_CODE_LP_INIT                               0x0
#define   B_EQ_REG_TD_TPS_CODE_LP_1_2                                0x0
#define   B_EQ_REG_TD_TPS_CODE_LP_2_3                                0x1
#define   B_EQ_REG_TD_TPS_CODE_LP_3_4                                0x2
#define   B_EQ_REG_TD_TPS_CODE_LP_5_6                                0x3
#define   B_EQ_REG_TD_TPS_CODE_LP_7_8                                0x4

#define B_EQ_REG_TD_TPS_GUARD__A                                     0x1C10058
#define B_EQ_REG_TD_TPS_GUARD__W                                     2
#define B_EQ_REG_TD_TPS_GUARD__M                                     0x3
#define   B_EQ_REG_TD_TPS_GUARD_INIT                                 0x0
#define   B_EQ_REG_TD_TPS_GUARD_32                                   0x0
#define   B_EQ_REG_TD_TPS_GUARD_16                                   0x1
#define   B_EQ_REG_TD_TPS_GUARD_08                                   0x2
#define   B_EQ_REG_TD_TPS_GUARD_04                                   0x3

#define B_EQ_REG_TD_TPS_TR_MODE__A                                   0x1C10059
#define B_EQ_REG_TD_TPS_TR_MODE__W                                   2
#define B_EQ_REG_TD_TPS_TR_MODE__M                                   0x3
#define   B_EQ_REG_TD_TPS_TR_MODE_INIT                               0x0
#define   B_EQ_REG_TD_TPS_TR_MODE_2K                                 0x0
#define   B_EQ_REG_TD_TPS_TR_MODE_8K                                 0x1

#define B_EQ_REG_TD_TPS_CELL_ID_HI__A                                0x1C1005A
#define B_EQ_REG_TD_TPS_CELL_ID_HI__W                                8
#define B_EQ_REG_TD_TPS_CELL_ID_HI__M                                0xFF
#define   B_EQ_REG_TD_TPS_CELL_ID_HI_INIT                            0x0

#define B_EQ_REG_TD_TPS_CELL_ID_LO__A                                0x1C1005B
#define B_EQ_REG_TD_TPS_CELL_ID_LO__W                                8
#define B_EQ_REG_TD_TPS_CELL_ID_LO__M                                0xFF
#define   B_EQ_REG_TD_TPS_CELL_ID_LO_INIT                            0x0

#define B_EQ_REG_TD_TPS_RSV__A                                       0x1C1005C
#define B_EQ_REG_TD_TPS_RSV__W                                       6
#define B_EQ_REG_TD_TPS_RSV__M                                       0x3F
#define   B_EQ_REG_TD_TPS_RSV_INIT                                   0x0

#define B_EQ_REG_TD_TPS_BCH__A                                       0x1C1005D
#define B_EQ_REG_TD_TPS_BCH__W                                       14
#define B_EQ_REG_TD_TPS_BCH__M                                       0x3FFF
#define   B_EQ_REG_TD_TPS_BCH_INIT                                   0x0

#define B_EQ_REG_TD_SQR_ERR_I__A                                     0x1C1005E
#define B_EQ_REG_TD_SQR_ERR_I__W                                     16
#define B_EQ_REG_TD_SQR_ERR_I__M                                     0xFFFF
#define   B_EQ_REG_TD_SQR_ERR_I_INIT                                 0x0

#define B_EQ_REG_TD_SQR_ERR_Q__A                                     0x1C1005F
#define B_EQ_REG_TD_SQR_ERR_Q__W                                     16
#define B_EQ_REG_TD_SQR_ERR_Q__M                                     0xFFFF
#define   B_EQ_REG_TD_SQR_ERR_Q_INIT                                 0x0

#define B_EQ_REG_TD_SQR_ERR_EXP__A                                   0x1C10060
#define B_EQ_REG_TD_SQR_ERR_EXP__W                                   4
#define B_EQ_REG_TD_SQR_ERR_EXP__M                                   0xF
#define   B_EQ_REG_TD_SQR_ERR_EXP_INIT                               0x0

#define B_EQ_REG_TD_REQ_SMB_CNT__A                                   0x1C10061
#define B_EQ_REG_TD_REQ_SMB_CNT__W                                   16
#define B_EQ_REG_TD_REQ_SMB_CNT__M                                   0xFFFF
#define   B_EQ_REG_TD_REQ_SMB_CNT_INIT                               0x200

#define B_EQ_REG_TD_TPS_PWR_OFS__A                                   0x1C10062
#define B_EQ_REG_TD_TPS_PWR_OFS__W                                   16
#define B_EQ_REG_TD_TPS_PWR_OFS__M                                   0xFFFF
#define   B_EQ_REG_TD_TPS_PWR_OFS_INIT                               0x19F

#define B_EC_COMM_EXEC__A                                            0x2000000
#define B_EC_COMM_EXEC__W                                            3
#define B_EC_COMM_EXEC__M                                            0x7
#define   B_EC_COMM_EXEC_CTL__B                                      0
#define   B_EC_COMM_EXEC_CTL__W                                      3
#define   B_EC_COMM_EXEC_CTL__M                                      0x7
#define     B_EC_COMM_EXEC_CTL_STOP                                  0x0
#define     B_EC_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_EC_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_EC_COMM_EXEC_CTL_STEP                                  0x3
#define     B_EC_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_EC_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_EC_COMM_STATE__A                                           0x2000001
#define B_EC_COMM_STATE__W                                           16
#define B_EC_COMM_STATE__M                                           0xFFFF
#define B_EC_COMM_MB__A                                              0x2000002
#define B_EC_COMM_MB__W                                              16
#define B_EC_COMM_MB__M                                              0xFFFF
#define B_EC_COMM_SERVICE0__A                                        0x2000003
#define B_EC_COMM_SERVICE0__W                                        16
#define B_EC_COMM_SERVICE0__M                                        0xFFFF
#define B_EC_COMM_SERVICE1__A                                        0x2000004
#define B_EC_COMM_SERVICE1__W                                        16
#define B_EC_COMM_SERVICE1__M                                        0xFFFF
#define B_EC_COMM_INT_STA__A                                         0x2000007
#define B_EC_COMM_INT_STA__W                                         16
#define B_EC_COMM_INT_STA__M                                         0xFFFF
#define B_EC_COMM_INT_MSK__A                                         0x2000008
#define B_EC_COMM_INT_MSK__W                                         16
#define B_EC_COMM_INT_MSK__M                                         0xFFFF

#define B_EC_SB_SID                                                  0x16

#define B_EC_SB_REG_COMM_EXEC__A                                     0x2010000
#define B_EC_SB_REG_COMM_EXEC__W                                     3
#define B_EC_SB_REG_COMM_EXEC__M                                     0x7
#define   B_EC_SB_REG_COMM_EXEC_CTL__B                               0
#define   B_EC_SB_REG_COMM_EXEC_CTL__W                               3
#define   B_EC_SB_REG_COMM_EXEC_CTL__M                               0x7
#define     B_EC_SB_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_EC_SB_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_EC_SB_REG_COMM_EXEC_CTL_HOLD                           0x2

#define B_EC_SB_REG_COMM_STATE__A                                    0x2010001
#define B_EC_SB_REG_COMM_STATE__W                                    4
#define B_EC_SB_REG_COMM_STATE__M                                    0xF
#define B_EC_SB_REG_COMM_MB__A                                       0x2010002
#define B_EC_SB_REG_COMM_MB__W                                       2
#define B_EC_SB_REG_COMM_MB__M                                       0x3
#define   B_EC_SB_REG_COMM_MB_CTR__B                                 0
#define   B_EC_SB_REG_COMM_MB_CTR__W                                 1
#define   B_EC_SB_REG_COMM_MB_CTR__M                                 0x1
#define     B_EC_SB_REG_COMM_MB_CTR_OFF                              0x0
#define     B_EC_SB_REG_COMM_MB_CTR_ON                               0x1
#define   B_EC_SB_REG_COMM_MB_OBS__B                                 1
#define   B_EC_SB_REG_COMM_MB_OBS__W                                 1
#define   B_EC_SB_REG_COMM_MB_OBS__M                                 0x2
#define     B_EC_SB_REG_COMM_MB_OBS_OFF                              0x0
#define     B_EC_SB_REG_COMM_MB_OBS_ON                               0x2

#define B_EC_SB_REG_TR_MODE__A                                       0x2010010
#define B_EC_SB_REG_TR_MODE__W                                       1
#define B_EC_SB_REG_TR_MODE__M                                       0x1
#define   B_EC_SB_REG_TR_MODE_INIT                                   0x0
#define   B_EC_SB_REG_TR_MODE_8K                                     0x0
#define   B_EC_SB_REG_TR_MODE_2K                                     0x1

#define B_EC_SB_REG_CONST__A                                         0x2010011
#define B_EC_SB_REG_CONST__W                                         2
#define B_EC_SB_REG_CONST__M                                         0x3
#define   B_EC_SB_REG_CONST_INIT                                     0x2
#define   B_EC_SB_REG_CONST_QPSK                                     0x0
#define   B_EC_SB_REG_CONST_16QAM                                    0x1
#define   B_EC_SB_REG_CONST_64QAM                                    0x2

#define B_EC_SB_REG_ALPHA__A                                         0x2010012
#define B_EC_SB_REG_ALPHA__W                                         3
#define B_EC_SB_REG_ALPHA__M                                         0x7

#define   B_EC_SB_REG_ALPHA_INIT                                     0x0

#define   B_EC_SB_REG_ALPHA_NH                                       0x0

#define   B_EC_SB_REG_ALPHA_H1                                       0x1

#define   B_EC_SB_REG_ALPHA_H2                                       0x2

#define   B_EC_SB_REG_ALPHA_H4                                       0x3

#define B_EC_SB_REG_PRIOR__A                                         0x2010013
#define B_EC_SB_REG_PRIOR__W                                         1
#define B_EC_SB_REG_PRIOR__M                                         0x1
#define   B_EC_SB_REG_PRIOR_INIT                                     0x0
#define   B_EC_SB_REG_PRIOR_HI                                       0x0
#define   B_EC_SB_REG_PRIOR_LO                                       0x1

#define B_EC_SB_REG_CSI_HI__A                                        0x2010014
#define B_EC_SB_REG_CSI_HI__W                                        5
#define B_EC_SB_REG_CSI_HI__M                                        0x1F
#define   B_EC_SB_REG_CSI_HI_INIT                                    0x1F
#define   B_EC_SB_REG_CSI_HI_MAX                                     0x1F
#define   B_EC_SB_REG_CSI_HI_MIN                                     0x0
#define   B_EC_SB_REG_CSI_HI_TAG                                     0x0

#define B_EC_SB_REG_CSI_LO__A                                        0x2010015
#define B_EC_SB_REG_CSI_LO__W                                        5
#define B_EC_SB_REG_CSI_LO__M                                        0x1F
#define   B_EC_SB_REG_CSI_LO_INIT                                    0x1E
#define   B_EC_SB_REG_CSI_LO_MAX                                     0x1F
#define   B_EC_SB_REG_CSI_LO_MIN                                     0x0
#define   B_EC_SB_REG_CSI_LO_TAG                                     0x0

#define B_EC_SB_REG_SMB_TGL__A                                       0x2010016
#define B_EC_SB_REG_SMB_TGL__W                                       1
#define B_EC_SB_REG_SMB_TGL__M                                       0x1
#define   B_EC_SB_REG_SMB_TGL_OFF                                    0x0
#define   B_EC_SB_REG_SMB_TGL_ON                                     0x1
#define   B_EC_SB_REG_SMB_TGL_INIT                                   0x1

#define B_EC_SB_REG_SNR_HI__A                                        0x2010017
#define B_EC_SB_REG_SNR_HI__W                                        8
#define B_EC_SB_REG_SNR_HI__M                                        0xFF
#define   B_EC_SB_REG_SNR_HI_INIT                                    0x6E
#define   B_EC_SB_REG_SNR_HI_MAX                                     0xFF
#define   B_EC_SB_REG_SNR_HI_MIN                                     0x0
#define   B_EC_SB_REG_SNR_HI_TAG                                     0x0

#define B_EC_SB_REG_SNR_MID__A                                       0x2010018
#define B_EC_SB_REG_SNR_MID__W                                       8
#define B_EC_SB_REG_SNR_MID__M                                       0xFF
#define   B_EC_SB_REG_SNR_MID_INIT                                   0x6C
#define   B_EC_SB_REG_SNR_MID_MAX                                    0xFF
#define   B_EC_SB_REG_SNR_MID_MIN                                    0x0
#define   B_EC_SB_REG_SNR_MID_TAG                                    0x0

#define B_EC_SB_REG_SNR_LO__A                                        0x2010019
#define B_EC_SB_REG_SNR_LO__W                                        8
#define B_EC_SB_REG_SNR_LO__M                                        0xFF
#define   B_EC_SB_REG_SNR_LO_INIT                                    0x68
#define   B_EC_SB_REG_SNR_LO_MAX                                     0xFF
#define   B_EC_SB_REG_SNR_LO_MIN                                     0x0
#define   B_EC_SB_REG_SNR_LO_TAG                                     0x0

#define B_EC_SB_REG_SCALE_MSB__A                                     0x201001A
#define B_EC_SB_REG_SCALE_MSB__W                                     6
#define B_EC_SB_REG_SCALE_MSB__M                                     0x3F
#define   B_EC_SB_REG_SCALE_MSB_INIT                                 0x30
#define   B_EC_SB_REG_SCALE_MSB_MAX                                  0x3F

#define B_EC_SB_REG_SCALE_BIT2__A                                    0x201001B
#define B_EC_SB_REG_SCALE_BIT2__W                                    6
#define B_EC_SB_REG_SCALE_BIT2__M                                    0x3F
#define   B_EC_SB_REG_SCALE_BIT2_INIT                                0xC
#define   B_EC_SB_REG_SCALE_BIT2_MAX                                 0x3F

#define B_EC_SB_REG_SCALE_LSB__A                                     0x201001C
#define B_EC_SB_REG_SCALE_LSB__W                                     6
#define B_EC_SB_REG_SCALE_LSB__M                                     0x3F
#define   B_EC_SB_REG_SCALE_LSB_INIT                                 0x3
#define   B_EC_SB_REG_SCALE_LSB_MAX                                  0x3F

#define B_EC_SB_REG_CSI_OFS0__A                                      0x201001D
#define B_EC_SB_REG_CSI_OFS0__W                                      4
#define B_EC_SB_REG_CSI_OFS0__M                                      0xF
#define   B_EC_SB_REG_CSI_OFS0_INIT                                  0x4

#define B_EC_SB_REG_CSI_OFS1__A                                      0x201001E
#define B_EC_SB_REG_CSI_OFS1__W                                      4
#define B_EC_SB_REG_CSI_OFS1__M                                      0xF
#define   B_EC_SB_REG_CSI_OFS1_INIT                                  0x1

#define B_EC_SB_REG_CSI_OFS2__A                                      0x201001F
#define B_EC_SB_REG_CSI_OFS2__W                                      4
#define B_EC_SB_REG_CSI_OFS2__M                                      0xF
#define   B_EC_SB_REG_CSI_OFS2_INIT                                  0x2

#define B_EC_SB_REG_MAX0__A                                          0x2010020
#define B_EC_SB_REG_MAX0__W                                          6
#define B_EC_SB_REG_MAX0__M                                          0x3F
#define   B_EC_SB_REG_MAX0_INIT                                      0x3F

#define B_EC_SB_REG_MAX1__A                                          0x2010021
#define B_EC_SB_REG_MAX1__W                                          6
#define B_EC_SB_REG_MAX1__M                                          0x3F
#define   B_EC_SB_REG_MAX1_INIT                                      0x3F

#define B_EC_SB_REG_MAX2__A                                          0x2010022
#define B_EC_SB_REG_MAX2__W                                          6
#define B_EC_SB_REG_MAX2__M                                          0x3F
#define   B_EC_SB_REG_MAX2_INIT                                      0x3F

#define B_EC_SB_REG_CSI_DIS__A                                       0x2010023
#define B_EC_SB_REG_CSI_DIS__W                                       1
#define B_EC_SB_REG_CSI_DIS__M                                       0x1
#define   B_EC_SB_REG_CSI_DIS_INIT                                   0x0

#define B_EC_SB_SD_RAM__A                                            0x2020000

#define B_EC_SB_BD0_RAM__A                                           0x2030000

#define B_EC_SB_BD1_RAM__A                                           0x2040000

#define B_EC_VD_SID                                                  0x17

#define B_EC_VD_REG_COMM_EXEC__A                                     0x2090000
#define B_EC_VD_REG_COMM_EXEC__W                                     3
#define B_EC_VD_REG_COMM_EXEC__M                                     0x7
#define   B_EC_VD_REG_COMM_EXEC_CTL__B                               0
#define   B_EC_VD_REG_COMM_EXEC_CTL__W                               3
#define   B_EC_VD_REG_COMM_EXEC_CTL__M                               0x7
#define     B_EC_VD_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_EC_VD_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_EC_VD_REG_COMM_EXEC_CTL_HOLD                           0x2

#define B_EC_VD_REG_COMM_STATE__A                                    0x2090001
#define B_EC_VD_REG_COMM_STATE__W                                    4
#define B_EC_VD_REG_COMM_STATE__M                                    0xF
#define B_EC_VD_REG_COMM_MB__A                                       0x2090002
#define B_EC_VD_REG_COMM_MB__W                                       2
#define B_EC_VD_REG_COMM_MB__M                                       0x3
#define   B_EC_VD_REG_COMM_MB_CTR__B                                 0
#define   B_EC_VD_REG_COMM_MB_CTR__W                                 1
#define   B_EC_VD_REG_COMM_MB_CTR__M                                 0x1
#define     B_EC_VD_REG_COMM_MB_CTR_OFF                              0x0
#define     B_EC_VD_REG_COMM_MB_CTR_ON                               0x1
#define   B_EC_VD_REG_COMM_MB_OBS__B                                 1
#define   B_EC_VD_REG_COMM_MB_OBS__W                                 1
#define   B_EC_VD_REG_COMM_MB_OBS__M                                 0x2
#define     B_EC_VD_REG_COMM_MB_OBS_OFF                              0x0
#define     B_EC_VD_REG_COMM_MB_OBS_ON                               0x2

#define B_EC_VD_REG_COMM_SERVICE0__A                                 0x2090003
#define B_EC_VD_REG_COMM_SERVICE0__W                                 16
#define B_EC_VD_REG_COMM_SERVICE0__M                                 0xFFFF
#define B_EC_VD_REG_COMM_SERVICE1__A                                 0x2090004
#define B_EC_VD_REG_COMM_SERVICE1__W                                 16
#define B_EC_VD_REG_COMM_SERVICE1__M                                 0xFFFF
#define B_EC_VD_REG_COMM_INT_STA__A                                  0x2090007
#define B_EC_VD_REG_COMM_INT_STA__W                                  1
#define B_EC_VD_REG_COMM_INT_STA__M                                  0x1
#define   B_EC_VD_REG_COMM_INT_STA_BER_RDY__B                        0
#define   B_EC_VD_REG_COMM_INT_STA_BER_RDY__W                        1
#define   B_EC_VD_REG_COMM_INT_STA_BER_RDY__M                        0x1

#define B_EC_VD_REG_COMM_INT_MSK__A                                  0x2090008
#define B_EC_VD_REG_COMM_INT_MSK__W                                  1
#define B_EC_VD_REG_COMM_INT_MSK__M                                  0x1
#define   B_EC_VD_REG_COMM_INT_MSK_BER_RDY__B                        0
#define   B_EC_VD_REG_COMM_INT_MSK_BER_RDY__W                        1
#define   B_EC_VD_REG_COMM_INT_MSK_BER_RDY__M                        0x1

#define B_EC_VD_REG_FORCE__A                                         0x2090010
#define B_EC_VD_REG_FORCE__W                                         2
#define B_EC_VD_REG_FORCE__M                                         0x3
#define   B_EC_VD_REG_FORCE_INIT                                     0x2
#define   B_EC_VD_REG_FORCE_FREE                                     0x0
#define   B_EC_VD_REG_FORCE_PROP                                     0x1
#define   B_EC_VD_REG_FORCE_FORCED                                   0x2
#define   B_EC_VD_REG_FORCE_FIXED                                    0x3

#define B_EC_VD_REG_SET_CODERATE__A                                  0x2090011
#define B_EC_VD_REG_SET_CODERATE__W                                  3
#define B_EC_VD_REG_SET_CODERATE__M                                  0x7
#define   B_EC_VD_REG_SET_CODERATE_INIT                              0x1
#define   B_EC_VD_REG_SET_CODERATE_C1_2                              0x0
#define   B_EC_VD_REG_SET_CODERATE_C2_3                              0x1
#define   B_EC_VD_REG_SET_CODERATE_C3_4                              0x2
#define   B_EC_VD_REG_SET_CODERATE_C5_6                              0x3
#define   B_EC_VD_REG_SET_CODERATE_C7_8                              0x4

#define B_EC_VD_REG_REQ_SMB_CNT__A                                   0x2090012
#define B_EC_VD_REG_REQ_SMB_CNT__W                                   16
#define B_EC_VD_REG_REQ_SMB_CNT__M                                   0xFFFF
#define   B_EC_VD_REG_REQ_SMB_CNT_INIT                               0x1

#define B_EC_VD_REG_REQ_BIT_CNT__A                                   0x2090013
#define B_EC_VD_REG_REQ_BIT_CNT__W                                   16
#define B_EC_VD_REG_REQ_BIT_CNT__M                                   0xFFFF
#define   B_EC_VD_REG_REQ_BIT_CNT_INIT                               0xFFF

#define B_EC_VD_REG_RLK_ENA__A                                       0x2090014
#define B_EC_VD_REG_RLK_ENA__W                                       1
#define B_EC_VD_REG_RLK_ENA__M                                       0x1
#define   B_EC_VD_REG_RLK_ENA_INIT                                   0x1
#define   B_EC_VD_REG_RLK_ENA_OFF                                    0x0
#define   B_EC_VD_REG_RLK_ENA_ON                                     0x1

#define B_EC_VD_REG_VAL__A                                           0x2090015
#define B_EC_VD_REG_VAL__W                                           2
#define B_EC_VD_REG_VAL__M                                           0x3
#define   B_EC_VD_REG_VAL_INIT                                       0x0
#define   B_EC_VD_REG_VAL_CODE                                       0x1
#define   B_EC_VD_REG_VAL_CNT                                        0x2

#define B_EC_VD_REG_GET_CODERATE__A                                  0x2090016
#define B_EC_VD_REG_GET_CODERATE__W                                  3
#define B_EC_VD_REG_GET_CODERATE__M                                  0x7
#define   B_EC_VD_REG_GET_CODERATE_INIT                              0x0
#define   B_EC_VD_REG_GET_CODERATE_C1_2                              0x0
#define   B_EC_VD_REG_GET_CODERATE_C2_3                              0x1
#define   B_EC_VD_REG_GET_CODERATE_C3_4                              0x2
#define   B_EC_VD_REG_GET_CODERATE_C5_6                              0x3
#define   B_EC_VD_REG_GET_CODERATE_C7_8                              0x4

#define B_EC_VD_REG_ERR_BIT_CNT__A                                   0x2090017
#define B_EC_VD_REG_ERR_BIT_CNT__W                                   16
#define B_EC_VD_REG_ERR_BIT_CNT__M                                   0xFFFF
#define   B_EC_VD_REG_ERR_BIT_CNT_INIT                               0xFFFF

#define B_EC_VD_REG_IN_BIT_CNT__A                                    0x2090018
#define B_EC_VD_REG_IN_BIT_CNT__W                                    16
#define B_EC_VD_REG_IN_BIT_CNT__M                                    0xFFFF
#define   B_EC_VD_REG_IN_BIT_CNT_INIT                                0x0

#define B_EC_VD_REG_STS__A                                           0x2090019
#define B_EC_VD_REG_STS__W                                           1
#define B_EC_VD_REG_STS__M                                           0x1
#define   B_EC_VD_REG_STS_INIT                                       0x0
#define   B_EC_VD_REG_STS_NO_LOCK                                    0x0
#define   B_EC_VD_REG_STS_IN_LOCK                                    0x1

#define B_EC_VD_REG_RLK_CNT__A                                       0x209001A
#define B_EC_VD_REG_RLK_CNT__W                                       16
#define B_EC_VD_REG_RLK_CNT__M                                       0xFFFF
#define   B_EC_VD_REG_RLK_CNT_INIT                                   0x0

#define B_EC_VD_TB0_RAM__A                                           0x20A0000

#define B_EC_VD_TB1_RAM__A                                           0x20B0000

#define B_EC_VD_TB2_RAM__A                                           0x20C0000

#define B_EC_VD_TB3_RAM__A                                           0x20D0000

#define B_EC_VD_RE_RAM__A                                            0x2100000

#define B_EC_OD_SID                                                  0x18

#define B_EC_OD_REG_COMM_EXEC__A                                     0x2110000
#define B_EC_OD_REG_COMM_EXEC__W                                     3
#define B_EC_OD_REG_COMM_EXEC__M                                     0x7
#define   B_EC_OD_REG_COMM_EXEC_CTL__B                               0
#define   B_EC_OD_REG_COMM_EXEC_CTL__W                               3
#define   B_EC_OD_REG_COMM_EXEC_CTL__M                               0x7
#define     B_EC_OD_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_EC_OD_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_EC_OD_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_EC_OD_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_EC_OD_REG_COMM_STATE__A                                    0x2110001
#define B_EC_OD_REG_COMM_STATE__W                                    1
#define B_EC_OD_REG_COMM_STATE__M                                    0x1
#define   B_EC_OD_REG_COMM_STATE_DI_LOCKED__B                        0
#define   B_EC_OD_REG_COMM_STATE_DI_LOCKED__W                        1
#define   B_EC_OD_REG_COMM_STATE_DI_LOCKED__M                        0x1

#define B_EC_OD_REG_COMM_MB__A                                       0x2110002
#define B_EC_OD_REG_COMM_MB__W                                       3
#define B_EC_OD_REG_COMM_MB__M                                       0x7
#define   B_EC_OD_REG_COMM_MB_CTR__B                                 0
#define   B_EC_OD_REG_COMM_MB_CTR__W                                 1
#define   B_EC_OD_REG_COMM_MB_CTR__M                                 0x1
#define     B_EC_OD_REG_COMM_MB_CTR_OFF                              0x0
#define     B_EC_OD_REG_COMM_MB_CTR_ON                               0x1
#define   B_EC_OD_REG_COMM_MB_OBS__B                                 1
#define   B_EC_OD_REG_COMM_MB_OBS__W                                 1
#define   B_EC_OD_REG_COMM_MB_OBS__M                                 0x2
#define     B_EC_OD_REG_COMM_MB_OBS_OFF                              0x0
#define     B_EC_OD_REG_COMM_MB_OBS_ON                               0x2

#define B_EC_OD_REG_COMM_SERVICE0__A                                 0x2110003
#define B_EC_OD_REG_COMM_SERVICE0__W                                 10
#define B_EC_OD_REG_COMM_SERVICE0__M                                 0x3FF
#define B_EC_OD_REG_COMM_SERVICE1__A                                 0x2110004
#define B_EC_OD_REG_COMM_SERVICE1__W                                 11
#define B_EC_OD_REG_COMM_SERVICE1__M                                 0x7FF

#define B_EC_OD_REG_COMM_ACTIVATE__A                                 0x2110005
#define B_EC_OD_REG_COMM_ACTIVATE__W                                 2
#define B_EC_OD_REG_COMM_ACTIVATE__M                                 0x3

#define B_EC_OD_REG_COMM_COUNT__A                                    0x2110006
#define B_EC_OD_REG_COMM_COUNT__W                                    16
#define B_EC_OD_REG_COMM_COUNT__M                                    0xFFFF

#define B_EC_OD_REG_COMM_INT_STA__A                                  0x2110007
#define B_EC_OD_REG_COMM_INT_STA__W                                  2
#define B_EC_OD_REG_COMM_INT_STA__M                                  0x3
#define   B_EC_OD_REG_COMM_INT_STA_IN_SYNC__B                        0
#define   B_EC_OD_REG_COMM_INT_STA_IN_SYNC__W                        1
#define   B_EC_OD_REG_COMM_INT_STA_IN_SYNC__M                        0x1
#define   B_EC_OD_REG_COMM_INT_STA_LOST_SYNC__B                      1
#define   B_EC_OD_REG_COMM_INT_STA_LOST_SYNC__W                      1
#define   B_EC_OD_REG_COMM_INT_STA_LOST_SYNC__M                      0x2

#define B_EC_OD_REG_COMM_INT_MSK__A                                  0x2110008
#define B_EC_OD_REG_COMM_INT_MSK__W                                  2
#define B_EC_OD_REG_COMM_INT_MSK__M                                  0x3
#define   B_EC_OD_REG_COMM_INT_MSK_IN_SYNC__B                        0
#define   B_EC_OD_REG_COMM_INT_MSK_IN_SYNC__W                        1
#define   B_EC_OD_REG_COMM_INT_MSK_IN_SYNC__M                        0x1
#define   B_EC_OD_REG_COMM_INT_MSK_LOST_SYNC__B                      1
#define   B_EC_OD_REG_COMM_INT_MSK_LOST_SYNC__W                      1
#define   B_EC_OD_REG_COMM_INT_MSK_LOST_SYNC__M                      0x2

#define B_EC_OD_REG_SYNC__A                                          0x2110664
#define B_EC_OD_REG_SYNC__W                                          12
#define B_EC_OD_REG_SYNC__M                                          0xFFF
#define   B_EC_OD_REG_SYNC_NR_SYNC__B                                0
#define   B_EC_OD_REG_SYNC_NR_SYNC__W                                5
#define   B_EC_OD_REG_SYNC_NR_SYNC__M                                0x1F
#define   B_EC_OD_REG_SYNC_IN_SYNC__B                                5
#define   B_EC_OD_REG_SYNC_IN_SYNC__W                                4
#define   B_EC_OD_REG_SYNC_IN_SYNC__M                                0x1E0
#define   B_EC_OD_REG_SYNC_OUT_SYNC__B                               9
#define   B_EC_OD_REG_SYNC_OUT_SYNC__W                               3
#define   B_EC_OD_REG_SYNC_OUT_SYNC__M                               0xE00

#define B_EC_OD_REG_NOSYNC__A                                        0x2110004
#define B_EC_OD_REG_NOSYNC__W                                        8
#define B_EC_OD_REG_NOSYNC__M                                        0xFF

#define B_EC_OD_DEINT_RAM__A                                         0x2120000

#define B_EC_RS_SID                                                  0x19

#define B_EC_RS_REG_COMM_EXEC__A                                     0x2130000
#define B_EC_RS_REG_COMM_EXEC__W                                     3
#define B_EC_RS_REG_COMM_EXEC__M                                     0x7
#define   B_EC_RS_REG_COMM_EXEC_CTL__B                               0
#define   B_EC_RS_REG_COMM_EXEC_CTL__W                               3
#define   B_EC_RS_REG_COMM_EXEC_CTL__M                               0x7
#define     B_EC_RS_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_EC_RS_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_EC_RS_REG_COMM_EXEC_CTL_HOLD                           0x2

#define B_EC_RS_REG_COMM_STATE__A                                    0x2130001
#define B_EC_RS_REG_COMM_STATE__W                                    4
#define B_EC_RS_REG_COMM_STATE__M                                    0xF
#define B_EC_RS_REG_COMM_MB__A                                       0x2130002
#define B_EC_RS_REG_COMM_MB__W                                       2
#define B_EC_RS_REG_COMM_MB__M                                       0x3
#define   B_EC_RS_REG_COMM_MB_CTR__B                                 0
#define   B_EC_RS_REG_COMM_MB_CTR__W                                 1
#define   B_EC_RS_REG_COMM_MB_CTR__M                                 0x1
#define     B_EC_RS_REG_COMM_MB_CTR_OFF                              0x0
#define     B_EC_RS_REG_COMM_MB_CTR_ON                               0x1
#define   B_EC_RS_REG_COMM_MB_OBS__B                                 1
#define   B_EC_RS_REG_COMM_MB_OBS__W                                 1
#define   B_EC_RS_REG_COMM_MB_OBS__M                                 0x2
#define     B_EC_RS_REG_COMM_MB_OBS_OFF                              0x0
#define     B_EC_RS_REG_COMM_MB_OBS_ON                               0x2

#define B_EC_RS_REG_COMM_SERVICE0__A                                 0x2130003
#define B_EC_RS_REG_COMM_SERVICE0__W                                 16
#define B_EC_RS_REG_COMM_SERVICE0__M                                 0xFFFF
#define B_EC_RS_REG_COMM_SERVICE1__A                                 0x2130004
#define B_EC_RS_REG_COMM_SERVICE1__W                                 16
#define B_EC_RS_REG_COMM_SERVICE1__M                                 0xFFFF
#define B_EC_RS_REG_COMM_INT_STA__A                                  0x2130007
#define B_EC_RS_REG_COMM_INT_STA__W                                  1
#define B_EC_RS_REG_COMM_INT_STA__M                                  0x1
#define   B_EC_RS_REG_COMM_INT_STA_BER_RDY__B                        0
#define   B_EC_RS_REG_COMM_INT_STA_BER_RDY__W                        1
#define   B_EC_RS_REG_COMM_INT_STA_BER_RDY__M                        0x1

#define B_EC_RS_REG_COMM_INT_MSK__A                                  0x2130008
#define B_EC_RS_REG_COMM_INT_MSK__W                                  1
#define B_EC_RS_REG_COMM_INT_MSK__M                                  0x1
#define   B_EC_RS_REG_COMM_INT_MSK_BER_RDY__B                        0
#define   B_EC_RS_REG_COMM_INT_MSK_BER_RDY__W                        1
#define   B_EC_RS_REG_COMM_INT_MSK_BER_RDY__M                        0x1

#define B_EC_RS_REG_REQ_PCK_CNT__A                                   0x2130010
#define B_EC_RS_REG_REQ_PCK_CNT__W                                   16
#define B_EC_RS_REG_REQ_PCK_CNT__M                                   0xFFFF
#define   B_EC_RS_REG_REQ_PCK_CNT_INIT                               0x200

#define B_EC_RS_REG_VAL__A                                           0x2130011
#define B_EC_RS_REG_VAL__W                                           1
#define B_EC_RS_REG_VAL__M                                           0x1
#define   B_EC_RS_REG_VAL_INIT                                       0x0
#define   B_EC_RS_REG_VAL_PCK                                        0x1

#define B_EC_RS_REG_ERR_PCK_CNT__A                                   0x2130012
#define B_EC_RS_REG_ERR_PCK_CNT__W                                   16
#define B_EC_RS_REG_ERR_PCK_CNT__M                                   0xFFFF
#define   B_EC_RS_REG_ERR_PCK_CNT_INIT                               0xFFFF

#define B_EC_RS_REG_ERR_SMB_CNT__A                                   0x2130013
#define B_EC_RS_REG_ERR_SMB_CNT__W                                   16
#define B_EC_RS_REG_ERR_SMB_CNT__M                                   0xFFFF
#define   B_EC_RS_REG_ERR_SMB_CNT_INIT                               0xFFFF

#define B_EC_RS_REG_ERR_BIT_CNT__A                                   0x2130014
#define B_EC_RS_REG_ERR_BIT_CNT__W                                   16
#define B_EC_RS_REG_ERR_BIT_CNT__M                                   0xFFFF
#define   B_EC_RS_REG_ERR_BIT_CNT_INIT                               0xFFFF

#define B_EC_RS_REG_IN_PCK_CNT__A                                    0x2130015
#define B_EC_RS_REG_IN_PCK_CNT__W                                    16
#define B_EC_RS_REG_IN_PCK_CNT__M                                    0xFFFF
#define   B_EC_RS_REG_IN_PCK_CNT_INIT                                0x0

#define B_EC_RS_EC_RAM__A                                            0x2140000

#define B_EC_OC_SID                                                  0x1A

#define B_EC_OC_REG_COMM_EXEC__A                                     0x2150000
#define B_EC_OC_REG_COMM_EXEC__W                                     3
#define B_EC_OC_REG_COMM_EXEC__M                                     0x7
#define   B_EC_OC_REG_COMM_EXEC_CTL__B                               0
#define   B_EC_OC_REG_COMM_EXEC_CTL__W                               3
#define   B_EC_OC_REG_COMM_EXEC_CTL__M                               0x7
#define     B_EC_OC_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_EC_OC_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_EC_OC_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_EC_OC_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_EC_OC_REG_COMM_STATE__A                                    0x2150001
#define B_EC_OC_REG_COMM_STATE__W                                    4
#define B_EC_OC_REG_COMM_STATE__M                                    0xF

#define B_EC_OC_REG_COMM_MB__A                                       0x2150002
#define B_EC_OC_REG_COMM_MB__W                                       2
#define B_EC_OC_REG_COMM_MB__M                                       0x3
#define   B_EC_OC_REG_COMM_MB_CTR__B                                 0
#define   B_EC_OC_REG_COMM_MB_CTR__W                                 1
#define   B_EC_OC_REG_COMM_MB_CTR__M                                 0x1
#define     B_EC_OC_REG_COMM_MB_CTR_OFF                              0x0
#define     B_EC_OC_REG_COMM_MB_CTR_ON                               0x1
#define   B_EC_OC_REG_COMM_MB_OBS__B                                 1
#define   B_EC_OC_REG_COMM_MB_OBS__W                                 1
#define   B_EC_OC_REG_COMM_MB_OBS__M                                 0x2
#define     B_EC_OC_REG_COMM_MB_OBS_OFF                              0x0
#define     B_EC_OC_REG_COMM_MB_OBS_ON                               0x2

#define B_EC_OC_REG_COMM_SERVICE0__A                                 0x2150003
#define B_EC_OC_REG_COMM_SERVICE0__W                                 10
#define B_EC_OC_REG_COMM_SERVICE0__M                                 0x3FF

#define B_EC_OC_REG_COMM_SERVICE1__A                                 0x2150004
#define B_EC_OC_REG_COMM_SERVICE1__W                                 11
#define B_EC_OC_REG_COMM_SERVICE1__M                                 0x7FF

#define B_EC_OC_REG_COMM_INT_STA__A                                  0x2150007
#define B_EC_OC_REG_COMM_INT_STA__W                                  6
#define B_EC_OC_REG_COMM_INT_STA__M                                  0x3F
#define   B_EC_OC_REG_COMM_INT_STA_MEM_FUL_STS__B                    0
#define   B_EC_OC_REG_COMM_INT_STA_MEM_FUL_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_STA_MEM_FUL_STS__M                    0x1
#define   B_EC_OC_REG_COMM_INT_STA_MEM_EMP_STS__B                    1
#define   B_EC_OC_REG_COMM_INT_STA_MEM_EMP_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_STA_MEM_EMP_STS__M                    0x2
#define   B_EC_OC_REG_COMM_INT_STA_SNC_ISS_STS__B                    2
#define   B_EC_OC_REG_COMM_INT_STA_SNC_ISS_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_STA_SNC_ISS_STS__M                    0x4
#define   B_EC_OC_REG_COMM_INT_STA_SNC_OSS_STS__B                    3
#define   B_EC_OC_REG_COMM_INT_STA_SNC_OSS_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_STA_SNC_OSS_STS__M                    0x8
#define   B_EC_OC_REG_COMM_INT_STA_SNC_NSS_STS__B                    4
#define   B_EC_OC_REG_COMM_INT_STA_SNC_NSS_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_STA_SNC_NSS_STS__M                    0x10
#define   B_EC_OC_REG_COMM_INT_STA_PCK_ERR_UPD__B                    5
#define   B_EC_OC_REG_COMM_INT_STA_PCK_ERR_UPD__W                    1
#define   B_EC_OC_REG_COMM_INT_STA_PCK_ERR_UPD__M                    0x20

#define B_EC_OC_REG_COMM_INT_MSK__A                                  0x2150008
#define B_EC_OC_REG_COMM_INT_MSK__W                                  6
#define B_EC_OC_REG_COMM_INT_MSK__M                                  0x3F
#define   B_EC_OC_REG_COMM_INT_MSK_MEM_FUL_STS__B                    0
#define   B_EC_OC_REG_COMM_INT_MSK_MEM_FUL_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_MSK_MEM_FUL_STS__M                    0x1
#define   B_EC_OC_REG_COMM_INT_MSK_MEM_EMP_STS__B                    1
#define   B_EC_OC_REG_COMM_INT_MSK_MEM_EMP_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_MSK_MEM_EMP_STS__M                    0x2
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_ISS_STS__B                    2
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_ISS_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_ISS_STS__M                    0x4
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_OSS_STS__B                    3
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_OSS_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_OSS_STS__M                    0x8
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_NSS_STS__B                    4
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_NSS_STS__W                    1
#define   B_EC_OC_REG_COMM_INT_MSK_SNC_NSS_STS__M                    0x10
#define   B_EC_OC_REG_COMM_INT_MSK_PCK_ERR_UPD__B                    5
#define   B_EC_OC_REG_COMM_INT_MSK_PCK_ERR_UPD__W                    1
#define   B_EC_OC_REG_COMM_INT_MSK_PCK_ERR_UPD__M                    0x20

#define B_EC_OC_REG_OC_MODE_LOP__A                                   0x2150010
#define B_EC_OC_REG_OC_MODE_LOP__W                                   16
#define B_EC_OC_REG_OC_MODE_LOP__M                                   0xFFFF
#define   B_EC_OC_REG_OC_MODE_LOP_INIT                               0x0

#define   B_EC_OC_REG_OC_MODE_LOP_PAR_ENA__B                         0
#define   B_EC_OC_REG_OC_MODE_LOP_PAR_ENA__W                         1
#define   B_EC_OC_REG_OC_MODE_LOP_PAR_ENA__M                         0x1
#define     B_EC_OC_REG_OC_MODE_LOP_PAR_ENA_ENABLE                   0x0
#define     B_EC_OC_REG_OC_MODE_LOP_PAR_ENA_DISABLE                  0x1

#define   B_EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC__B                     2
#define   B_EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC__M                     0x4
#define     B_EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC_STATIC               0x0
#define     B_EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC_DYNAMIC              0x4

#define   B_EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA__B                     4
#define   B_EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA__M                     0x10
#define     B_EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_LOP_DAT_PRP_ENA_ENABLE               0x10

#define   B_EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE__B                     5
#define   B_EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE__M                     0x20
#define     B_EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_LOP_SNC_LCK_MDE_ENABLE               0x20

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV__B                     6
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV__M                     0x40
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_BIT_REV_ENABLE               0x40

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE__B                     7
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE__M                     0x80
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE_PARALLEL             0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE_SERIAL               0x80

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE__B                     8
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE__M                     0x100
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE_ENABLE               0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_MDE_DISABLE              0x100

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK__B                     9
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK__M                     0x200
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK_STRETCH              0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_SER_CLK_GATE                 0x200

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR__B                     10
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR__M                     0x400
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR_CONTINOUS            0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_SER_BUR_BURST                0x400

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC__B                     11
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC__M                     0x800
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC_ENABLE               0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_SNC_DISABLE              0x800

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO__B                     12
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO__M                     0x1000
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO_ENABLE               0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_RSO_DISABLE              0x1000

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT__B                     13
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT__M                     0x2000
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT_ENABLE               0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_ERR_BIT_DISABLE              0x2000

#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS__B                     14
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS__W                     1
#define   B_EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS__M                     0x4000
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS_ENABLE               0x0
#define     B_EC_OC_REG_OC_MODE_LOP_MPG_SNC_INS_DISABLE              0x4000

#define   B_EC_OC_REG_OC_MODE_LOP_DER_ENA__B                         15
#define   B_EC_OC_REG_OC_MODE_LOP_DER_ENA__W                         1
#define   B_EC_OC_REG_OC_MODE_LOP_DER_ENA__M                         0x8000
#define     B_EC_OC_REG_OC_MODE_LOP_DER_ENA_ENABLE                   0x0
#define     B_EC_OC_REG_OC_MODE_LOP_DER_ENA_DISABLE                  0x8000

#define B_EC_OC_REG_OC_MODE_HIP__A                                   0x2150011
#define B_EC_OC_REG_OC_MODE_HIP__W                                   15
#define B_EC_OC_REG_OC_MODE_HIP__M                                   0x7FFF
#define   B_EC_OC_REG_OC_MODE_HIP_INIT                               0x5

#define   B_EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS__B                     0
#define   B_EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS__M                     0x1
#define     B_EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS_OBSERVE              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MON_BUS_RDS_CONTROL              0x1

#define   B_EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC__B                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC__M                     0x2
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC_MPEG_SYNC            0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_SER_SNC_MPEG                 0x2

#define   B_EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE__B                     2
#define   B_EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE__M                     0x4
#define     B_EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE_OBSERVE              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MON_OBS_MDE_CONTROL              0x4

#define   B_EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC__B                     3
#define   B_EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC__M                     0x8
#define     B_EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC_MONITOR              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MON_BUS_SRC_MPEG                 0x8

#define   B_EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC__B                     4
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC__M                     0x10
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC_MPEG                 0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC_MONITOR              0x10

#define   B_EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE__B                     5
#define   B_EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE__M                     0x20
#define     B_EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MON_RDC_MDE_ENABLE               0x20

#define   B_EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE__B                     6
#define   B_EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE__M                     0x40
#define     B_EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE_ENABLE               0x0
#define     B_EC_OC_REG_OC_MODE_HIP_DER_SNC_MDE_DISABLE              0x40

#define   B_EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP__B                     7
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP__M                     0x80
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_CLK_SUP_ENABLE               0x80

#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK__B                     8
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK__M                     0x100
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_CLK_ENABLE               0x100

#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL__B                     9
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL__M                     0x200
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL_ENABLE               0x200

#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR__B                     10
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR__M                     0x400
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_ERR_ENABLE               0x400

#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT__B                     11
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT__M                     0x800
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT_DISABLE              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_MPG_PAR_DAT_ENABLE               0x800

#define   B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON__B                     12
#define   B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON__M                     0x1000
#define     B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON_SEL_ZER              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MON_SEL_MON              0x1000

#define   B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG__B                     13
#define   B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG__W                     1
#define   B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG__M                     0x2000
#define     B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG_SEL_ZER              0x0
#define     B_EC_OC_REG_OC_MODE_HIP_FDB_SEL_MPG_SEL_MPG              0x2000

#define   B_EC_OC_REG_OC_MODE_HIP_SNC_OFF__B                         14
#define   B_EC_OC_REG_OC_MODE_HIP_SNC_OFF__W                         1
#define   B_EC_OC_REG_OC_MODE_HIP_SNC_OFF__M                         0x4000
#define     B_EC_OC_REG_OC_MODE_HIP_SNC_OFF_SEL_ZER                  0x0
#define     B_EC_OC_REG_OC_MODE_HIP_SNC_OFF_SEL_CLC                  0x4000

#define B_EC_OC_REG_OC_MPG_SIO__A                                    0x2150012
#define B_EC_OC_REG_OC_MPG_SIO__W                                    12
#define B_EC_OC_REG_OC_MPG_SIO__M                                    0xFFF
#define   B_EC_OC_REG_OC_MPG_SIO_INIT                                0xFFF

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_0__B                        0
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_0__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_0__M                        0x1
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_0_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_0_INPUT                   0x1

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_1__B                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_1__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_1__M                        0x2
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_1_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_1_INPUT                   0x2

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_2__B                        2
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_2__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_2__M                        0x4
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_2_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_2_INPUT                   0x4

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_3__B                        3
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_3__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_3__M                        0x8
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_3_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_3_INPUT                   0x8

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_4__B                        4
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_4__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_4__M                        0x10
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_4_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_4_INPUT                   0x10

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_5__B                        5
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_5__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_5__M                        0x20
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_5_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_5_INPUT                   0x20

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_6__B                        6
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_6__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_6__M                        0x40
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_6_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_6_INPUT                   0x40

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_7__B                        7
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_7__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_7__M                        0x80
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_7_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_7_INPUT                   0x80

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_8__B                        8
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_8__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_8__M                        0x100
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_8_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_8_INPUT                   0x100

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_9__B                        9
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_9__W                        1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_9__M                        0x200
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_9_OUTPUT                  0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_9_INPUT                   0x200

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_10__B                       10
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_10__W                       1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_10__M                       0x400
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_10_OUTPUT                 0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_10_INPUT                  0x400

#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_11__B                       11
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_11__W                       1
#define   B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_11__M                       0x800
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_11_OUTPUT                 0x0
#define     B_EC_OC_REG_OC_MPG_SIO_MPG_SIO_11_INPUT                  0x800

#define B_EC_OC_REG_DTO_INC_LOP__A                                   0x2150014
#define B_EC_OC_REG_DTO_INC_LOP__W                                   16
#define B_EC_OC_REG_DTO_INC_LOP__M                                   0xFFFF
#define   B_EC_OC_REG_DTO_INC_LOP_INIT                               0x0

#define B_EC_OC_REG_DTO_INC_HIP__A                                   0x2150015
#define B_EC_OC_REG_DTO_INC_HIP__W                                   8
#define B_EC_OC_REG_DTO_INC_HIP__M                                   0xFF
#define   B_EC_OC_REG_DTO_INC_HIP_INIT                               0xC0

#define B_EC_OC_REG_SNC_ISC_LVL__A                                   0x2150016
#define B_EC_OC_REG_SNC_ISC_LVL__W                                   12
#define B_EC_OC_REG_SNC_ISC_LVL__M                                   0xFFF
#define   B_EC_OC_REG_SNC_ISC_LVL_INIT                               0x422

#define   B_EC_OC_REG_SNC_ISC_LVL_ISC__B                             0
#define   B_EC_OC_REG_SNC_ISC_LVL_ISC__W                             4
#define   B_EC_OC_REG_SNC_ISC_LVL_ISC__M                             0xF

#define   B_EC_OC_REG_SNC_ISC_LVL_OSC__B                             4
#define   B_EC_OC_REG_SNC_ISC_LVL_OSC__W                             4
#define   B_EC_OC_REG_SNC_ISC_LVL_OSC__M                             0xF0

#define   B_EC_OC_REG_SNC_ISC_LVL_NSC__B                             8
#define   B_EC_OC_REG_SNC_ISC_LVL_NSC__W                             4
#define   B_EC_OC_REG_SNC_ISC_LVL_NSC__M                             0xF00

#define B_EC_OC_REG_SNC_NSC_LVL__A                                   0x2150017
#define B_EC_OC_REG_SNC_NSC_LVL__W                                   8
#define B_EC_OC_REG_SNC_NSC_LVL__M                                   0xFF
#define   B_EC_OC_REG_SNC_NSC_LVL_INIT                               0x0

#define B_EC_OC_REG_SNC_SNC_MODE__A                                  0x2150019
#define B_EC_OC_REG_SNC_SNC_MODE__W                                  2
#define B_EC_OC_REG_SNC_SNC_MODE__M                                  0x3
#define   B_EC_OC_REG_SNC_SNC_MODE_SEARCH                            0x0
#define   B_EC_OC_REG_SNC_SNC_MODE_TRACK                             0x1
#define   B_EC_OC_REG_SNC_SNC_MODE_LOCK                              0x2

#define B_EC_OC_REG_SNC_PCK_NMB__A                                   0x215001A
#define B_EC_OC_REG_SNC_PCK_NMB__W                                   16
#define B_EC_OC_REG_SNC_PCK_NMB__M                                   0xFFFF

#define B_EC_OC_REG_SNC_PCK_CNT__A                                   0x215001B
#define B_EC_OC_REG_SNC_PCK_CNT__W                                   16
#define B_EC_OC_REG_SNC_PCK_CNT__M                                   0xFFFF

#define B_EC_OC_REG_SNC_PCK_ERR__A                                   0x215001C
#define B_EC_OC_REG_SNC_PCK_ERR__W                                   16
#define B_EC_OC_REG_SNC_PCK_ERR__M                                   0xFFFF

#define B_EC_OC_REG_TMD_TOP_MODE__A                                  0x215001D
#define B_EC_OC_REG_TMD_TOP_MODE__W                                  2
#define B_EC_OC_REG_TMD_TOP_MODE__M                                  0x3
#define   B_EC_OC_REG_TMD_TOP_MODE_INIT                              0x3
#define   B_EC_OC_REG_TMD_TOP_MODE_SELECT_ACT_ACT                    0x0
#define   B_EC_OC_REG_TMD_TOP_MODE_SELECT_TOP_TOP                    0x1
#define   B_EC_OC_REG_TMD_TOP_MODE_SELECT_BOT_BOT                    0x2
#define   B_EC_OC_REG_TMD_TOP_MODE_SELECT_TOP_BOT                    0x3

#define B_EC_OC_REG_TMD_TOP_CNT__A                                   0x215001E
#define B_EC_OC_REG_TMD_TOP_CNT__W                                   10
#define B_EC_OC_REG_TMD_TOP_CNT__M                                   0x3FF
#define   B_EC_OC_REG_TMD_TOP_CNT_INIT                               0x1F4

#define B_EC_OC_REG_TMD_HIL_MAR__A                                   0x215001F
#define B_EC_OC_REG_TMD_HIL_MAR__W                                   10
#define B_EC_OC_REG_TMD_HIL_MAR__M                                   0x3FF
#define   B_EC_OC_REG_TMD_HIL_MAR_INIT                               0x3C0

#define B_EC_OC_REG_TMD_LOL_MAR__A                                   0x2150020
#define B_EC_OC_REG_TMD_LOL_MAR__W                                   10
#define B_EC_OC_REG_TMD_LOL_MAR__M                                   0x3FF
#define   B_EC_OC_REG_TMD_LOL_MAR_INIT                               0x40

#define B_EC_OC_REG_TMD_CUR_CNT__A                                   0x2150021
#define B_EC_OC_REG_TMD_CUR_CNT__W                                   4
#define B_EC_OC_REG_TMD_CUR_CNT__M                                   0xF
#define   B_EC_OC_REG_TMD_CUR_CNT_INIT                               0x3

#define B_EC_OC_REG_TMD_IUR_CNT__A                                   0x2150022
#define B_EC_OC_REG_TMD_IUR_CNT__W                                   4
#define B_EC_OC_REG_TMD_IUR_CNT__M                                   0xF
#define   B_EC_OC_REG_TMD_IUR_CNT_INIT                               0x0

#define B_EC_OC_REG_AVR_ASH_CNT__A                                   0x2150023
#define B_EC_OC_REG_AVR_ASH_CNT__W                                   4
#define B_EC_OC_REG_AVR_ASH_CNT__M                                   0xF
#define   B_EC_OC_REG_AVR_ASH_CNT_INIT                               0x6

#define B_EC_OC_REG_AVR_BSH_CNT__A                                   0x2150024
#define B_EC_OC_REG_AVR_BSH_CNT__W                                   4
#define B_EC_OC_REG_AVR_BSH_CNT__M                                   0xF
#define   B_EC_OC_REG_AVR_BSH_CNT_INIT                               0x2

#define B_EC_OC_REG_AVR_AVE_LOP__A                                   0x2150025
#define B_EC_OC_REG_AVR_AVE_LOP__W                                   16
#define B_EC_OC_REG_AVR_AVE_LOP__M                                   0xFFFF

#define B_EC_OC_REG_AVR_AVE_HIP__A                                   0x2150026
#define B_EC_OC_REG_AVR_AVE_HIP__W                                   5
#define B_EC_OC_REG_AVR_AVE_HIP__M                                   0x1F

#define B_EC_OC_REG_RCN_MODE__A                                      0x2150027
#define B_EC_OC_REG_RCN_MODE__W                                      3
#define B_EC_OC_REG_RCN_MODE__M                                      0x7
#define   B_EC_OC_REG_RCN_MODE_INIT                                  0x7

#define   B_EC_OC_REG_RCN_MODE_MODE_0__B                             0
#define   B_EC_OC_REG_RCN_MODE_MODE_0__W                             1
#define   B_EC_OC_REG_RCN_MODE_MODE_0__M                             0x1
#define     B_EC_OC_REG_RCN_MODE_MODE_0_ENABLE                       0x0
#define     B_EC_OC_REG_RCN_MODE_MODE_0_DISABLE                      0x1

#define   B_EC_OC_REG_RCN_MODE_MODE_1__B                             1
#define   B_EC_OC_REG_RCN_MODE_MODE_1__W                             1
#define   B_EC_OC_REG_RCN_MODE_MODE_1__M                             0x2
#define     B_EC_OC_REG_RCN_MODE_MODE_1_ENABLE                       0x0
#define     B_EC_OC_REG_RCN_MODE_MODE_1_DISABLE                      0x2

#define   B_EC_OC_REG_RCN_MODE_MODE_2__B                             2
#define   B_EC_OC_REG_RCN_MODE_MODE_2__W                             1
#define   B_EC_OC_REG_RCN_MODE_MODE_2__M                             0x4
#define     B_EC_OC_REG_RCN_MODE_MODE_2_ENABLE                       0x4
#define     B_EC_OC_REG_RCN_MODE_MODE_2_DISABLE                      0x0

#define B_EC_OC_REG_RCN_CRA_LOP__A                                   0x2150028
#define B_EC_OC_REG_RCN_CRA_LOP__W                                   16
#define B_EC_OC_REG_RCN_CRA_LOP__M                                   0xFFFF
#define   B_EC_OC_REG_RCN_CRA_LOP_INIT                               0x0

#define B_EC_OC_REG_RCN_CRA_HIP__A                                   0x2150029
#define B_EC_OC_REG_RCN_CRA_HIP__W                                   8
#define B_EC_OC_REG_RCN_CRA_HIP__M                                   0xFF
#define   B_EC_OC_REG_RCN_CRA_HIP_INIT                               0xC0

#define B_EC_OC_REG_RCN_CST_LOP__A                                   0x215002A
#define B_EC_OC_REG_RCN_CST_LOP__W                                   16
#define B_EC_OC_REG_RCN_CST_LOP__M                                   0xFFFF
#define   B_EC_OC_REG_RCN_CST_LOP_INIT                               0x1000

#define B_EC_OC_REG_RCN_CST_HIP__A                                   0x215002B
#define B_EC_OC_REG_RCN_CST_HIP__W                                   8
#define B_EC_OC_REG_RCN_CST_HIP__M                                   0xFF
#define   B_EC_OC_REG_RCN_CST_HIP_INIT                               0x0

#define B_EC_OC_REG_RCN_SET_LVL__A                                   0x215002C
#define B_EC_OC_REG_RCN_SET_LVL__W                                   9
#define B_EC_OC_REG_RCN_SET_LVL__M                                   0x1FF
#define   B_EC_OC_REG_RCN_SET_LVL_INIT                               0x1FF

#define B_EC_OC_REG_RCN_GAI_LVL__A                                   0x215002D
#define B_EC_OC_REG_RCN_GAI_LVL__W                                   4
#define B_EC_OC_REG_RCN_GAI_LVL__M                                   0xF
#define   B_EC_OC_REG_RCN_GAI_LVL_INIT                               0xA

#define B_EC_OC_REG_RCN_DRA_LOP__A                                   0x215002E
#define B_EC_OC_REG_RCN_DRA_LOP__W                                   16
#define B_EC_OC_REG_RCN_DRA_LOP__M                                   0xFFFF

#define B_EC_OC_REG_RCN_DRA_HIP__A                                   0x215002F
#define B_EC_OC_REG_RCN_DRA_HIP__W                                   8
#define B_EC_OC_REG_RCN_DRA_HIP__M                                   0xFF

#define B_EC_OC_REG_RCN_DOF_LOP__A                                   0x2150030
#define B_EC_OC_REG_RCN_DOF_LOP__W                                   16
#define B_EC_OC_REG_RCN_DOF_LOP__M                                   0xFFFF

#define B_EC_OC_REG_RCN_DOF_HIP__A                                   0x2150031
#define B_EC_OC_REG_RCN_DOF_HIP__W                                   8
#define B_EC_OC_REG_RCN_DOF_HIP__M                                   0xFF

#define B_EC_OC_REG_RCN_CLP_LOP__A                                   0x2150032
#define B_EC_OC_REG_RCN_CLP_LOP__W                                   16
#define B_EC_OC_REG_RCN_CLP_LOP__M                                   0xFFFF
#define   B_EC_OC_REG_RCN_CLP_LOP_INIT                               0x0

#define B_EC_OC_REG_RCN_CLP_HIP__A                                   0x2150033
#define B_EC_OC_REG_RCN_CLP_HIP__W                                   8
#define B_EC_OC_REG_RCN_CLP_HIP__M                                   0xFF
#define   B_EC_OC_REG_RCN_CLP_HIP_INIT                               0xC0

#define B_EC_OC_REG_RCN_MAP_LOP__A                                   0x2150034
#define B_EC_OC_REG_RCN_MAP_LOP__W                                   16
#define B_EC_OC_REG_RCN_MAP_LOP__M                                   0xFFFF

#define B_EC_OC_REG_RCN_MAP_HIP__A                                   0x2150035
#define B_EC_OC_REG_RCN_MAP_HIP__W                                   8
#define B_EC_OC_REG_RCN_MAP_HIP__M                                   0xFF

#define B_EC_OC_REG_OCR_MPG_UOS__A                                   0x2150036
#define B_EC_OC_REG_OCR_MPG_UOS__W                                   12
#define B_EC_OC_REG_OCR_MPG_UOS__M                                   0xFFF
#define   B_EC_OC_REG_OCR_MPG_UOS_INIT                               0x0

#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_0__B                           0
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_0__W                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_0__M                           0x1
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_0_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_0_ENABLE                     0x1

#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_1__B                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_1__W                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_1__M                           0x2
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_1_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_1_ENABLE                     0x2

#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_2__B                           2
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_2__W                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_2__M                           0x4
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_2_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_2_ENABLE                     0x4

#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_3__B                           3
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_3__W                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_3__M                           0x8
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_3_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_3_ENABLE                     0x8

#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_4__B                           4
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_4__W                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_4__M                           0x10
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_4_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_4_ENABLE                     0x10

#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_5__B                           5
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_5__W                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_5__M                           0x20
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_5_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_5_ENABLE                     0x20

#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_6__B                           6
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_6__W                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_6__M                           0x40
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_6_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_6_ENABLE                     0x40

#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_7__B                           7
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_7__W                           1
#define   B_EC_OC_REG_OCR_MPG_UOS_DAT_7__M                           0x80
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_7_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_DAT_7_ENABLE                     0x80

#define   B_EC_OC_REG_OCR_MPG_UOS_ERR__B                             8
#define   B_EC_OC_REG_OCR_MPG_UOS_ERR__W                             1
#define   B_EC_OC_REG_OCR_MPG_UOS_ERR__M                             0x100
#define     B_EC_OC_REG_OCR_MPG_UOS_ERR_DISABLE                      0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_ERR_ENABLE                       0x100

#define   B_EC_OC_REG_OCR_MPG_UOS_STR__B                             9
#define   B_EC_OC_REG_OCR_MPG_UOS_STR__W                             1
#define   B_EC_OC_REG_OCR_MPG_UOS_STR__M                             0x200
#define     B_EC_OC_REG_OCR_MPG_UOS_STR_DISABLE                      0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_STR_ENABLE                       0x200

#define   B_EC_OC_REG_OCR_MPG_UOS_VAL__B                             10
#define   B_EC_OC_REG_OCR_MPG_UOS_VAL__W                             1
#define   B_EC_OC_REG_OCR_MPG_UOS_VAL__M                             0x400
#define     B_EC_OC_REG_OCR_MPG_UOS_VAL_DISABLE                      0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_VAL_ENABLE                       0x400

#define   B_EC_OC_REG_OCR_MPG_UOS_CLK__B                             11
#define   B_EC_OC_REG_OCR_MPG_UOS_CLK__W                             1
#define   B_EC_OC_REG_OCR_MPG_UOS_CLK__M                             0x800
#define     B_EC_OC_REG_OCR_MPG_UOS_CLK_DISABLE                      0x0
#define     B_EC_OC_REG_OCR_MPG_UOS_CLK_ENABLE                       0x800

#define B_EC_OC_REG_OCR_MPG_WRI__A                                   0x2150037
#define B_EC_OC_REG_OCR_MPG_WRI__W                                   12
#define B_EC_OC_REG_OCR_MPG_WRI__M                                   0xFFF
#define   B_EC_OC_REG_OCR_MPG_WRI_INIT                               0x0
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_0__B                           0
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_0__W                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_0__M                           0x1
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_0_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_0_ENABLE                     0x1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_1__B                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_1__W                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_1__M                           0x2
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_1_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_1_ENABLE                     0x2
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_2__B                           2
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_2__W                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_2__M                           0x4
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_2_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_2_ENABLE                     0x4
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_3__B                           3
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_3__W                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_3__M                           0x8
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_3_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_3_ENABLE                     0x8
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_4__B                           4
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_4__W                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_4__M                           0x10
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_4_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_4_ENABLE                     0x10
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_5__B                           5
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_5__W                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_5__M                           0x20
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_5_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_5_ENABLE                     0x20
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_6__B                           6
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_6__W                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_6__M                           0x40
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_6_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_6_ENABLE                     0x40
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_7__B                           7
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_7__W                           1
#define   B_EC_OC_REG_OCR_MPG_WRI_DAT_7__M                           0x80
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_7_DISABLE                    0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_DAT_7_ENABLE                     0x80
#define   B_EC_OC_REG_OCR_MPG_WRI_ERR__B                             8
#define   B_EC_OC_REG_OCR_MPG_WRI_ERR__W                             1
#define   B_EC_OC_REG_OCR_MPG_WRI_ERR__M                             0x100
#define     B_EC_OC_REG_OCR_MPG_WRI_ERR_DISABLE                      0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_ERR_ENABLE                       0x100
#define   B_EC_OC_REG_OCR_MPG_WRI_STR__B                             9
#define   B_EC_OC_REG_OCR_MPG_WRI_STR__W                             1
#define   B_EC_OC_REG_OCR_MPG_WRI_STR__M                             0x200
#define     B_EC_OC_REG_OCR_MPG_WRI_STR_DISABLE                      0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_STR_ENABLE                       0x200
#define   B_EC_OC_REG_OCR_MPG_WRI_VAL__B                             10
#define   B_EC_OC_REG_OCR_MPG_WRI_VAL__W                             1
#define   B_EC_OC_REG_OCR_MPG_WRI_VAL__M                             0x400
#define     B_EC_OC_REG_OCR_MPG_WRI_VAL_DISABLE                      0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_VAL_ENABLE                       0x400
#define   B_EC_OC_REG_OCR_MPG_WRI_CLK__B                             11
#define   B_EC_OC_REG_OCR_MPG_WRI_CLK__W                             1
#define   B_EC_OC_REG_OCR_MPG_WRI_CLK__M                             0x800
#define     B_EC_OC_REG_OCR_MPG_WRI_CLK_DISABLE                      0x0
#define     B_EC_OC_REG_OCR_MPG_WRI_CLK_ENABLE                       0x800

#define B_EC_OC_REG_OCR_MPG_USR_DAT__A                               0x2150038
#define B_EC_OC_REG_OCR_MPG_USR_DAT__W                               12
#define B_EC_OC_REG_OCR_MPG_USR_DAT__M                               0xFFF

#define B_EC_OC_REG_OCR_MON_CNT__A                                   0x215003C
#define B_EC_OC_REG_OCR_MON_CNT__W                                   14
#define B_EC_OC_REG_OCR_MON_CNT__M                                   0x3FFF
#define   B_EC_OC_REG_OCR_MON_CNT_INIT                               0x0

#define B_EC_OC_REG_OCR_MON_RDX__A                                   0x215003D
#define B_EC_OC_REG_OCR_MON_RDX__W                                   1
#define B_EC_OC_REG_OCR_MON_RDX__M                                   0x1
#define   B_EC_OC_REG_OCR_MON_RDX_INIT                               0x0

#define B_EC_OC_REG_OCR_MON_RD0__A                                   0x215003E
#define B_EC_OC_REG_OCR_MON_RD0__W                                   10
#define B_EC_OC_REG_OCR_MON_RD0__M                                   0x3FF

#define B_EC_OC_REG_OCR_MON_RD1__A                                   0x215003F
#define B_EC_OC_REG_OCR_MON_RD1__W                                   10
#define B_EC_OC_REG_OCR_MON_RD1__M                                   0x3FF

#define B_EC_OC_REG_OCR_MON_RD2__A                                   0x2150040
#define B_EC_OC_REG_OCR_MON_RD2__W                                   10
#define B_EC_OC_REG_OCR_MON_RD2__M                                   0x3FF

#define B_EC_OC_REG_OCR_MON_RD3__A                                   0x2150041
#define B_EC_OC_REG_OCR_MON_RD3__W                                   10
#define B_EC_OC_REG_OCR_MON_RD3__M                                   0x3FF

#define B_EC_OC_REG_OCR_MON_RD4__A                                   0x2150042
#define B_EC_OC_REG_OCR_MON_RD4__W                                   10
#define B_EC_OC_REG_OCR_MON_RD4__M                                   0x3FF

#define B_EC_OC_REG_OCR_MON_RD5__A                                   0x2150043
#define B_EC_OC_REG_OCR_MON_RD5__W                                   10
#define B_EC_OC_REG_OCR_MON_RD5__M                                   0x3FF

#define B_EC_OC_REG_OCR_INV_MON__A                                   0x2150044
#define B_EC_OC_REG_OCR_INV_MON__W                                   12
#define B_EC_OC_REG_OCR_INV_MON__M                                   0xFFF
#define   B_EC_OC_REG_OCR_INV_MON_INIT                               0x0

#define B_EC_OC_REG_IPR_INV_MPG__A                                   0x2150045
#define B_EC_OC_REG_IPR_INV_MPG__W                                   12
#define B_EC_OC_REG_IPR_INV_MPG__M                                   0xFFF
#define   B_EC_OC_REG_IPR_INV_MPG_INIT                               0x0

#define B_EC_OC_REG_IPR_MSR_SNC__A                                   0x2150046
#define B_EC_OC_REG_IPR_MSR_SNC__W                                   6
#define B_EC_OC_REG_IPR_MSR_SNC__M                                   0x3F
#define   B_EC_OC_REG_IPR_MSR_SNC_INIT                               0x0

#define B_EC_OC_REG_DTO_CLKMODE__A                                   0x2150047
#define B_EC_OC_REG_DTO_CLKMODE__W                                   2
#define B_EC_OC_REG_DTO_CLKMODE__M                                   0x3
#define   B_EC_OC_REG_DTO_CLKMODE_INIT                               0x2

#define   B_EC_OC_REG_DTO_CLKMODE_EVEN_ODD__B                        0
#define   B_EC_OC_REG_DTO_CLKMODE_EVEN_ODD__W                        1
#define   B_EC_OC_REG_DTO_CLKMODE_EVEN_ODD__M                        0x1
#define     B_EC_OC_REG_DTO_CLKMODE_EVEN_ODD_EVEN_ODD                0x0
#define     B_EC_OC_REG_DTO_CLKMODE_EVEN_ODD_ODD_EVEN                0x1

#define   B_EC_OC_REG_DTO_CLKMODE_PAR_SER__B                         1
#define   B_EC_OC_REG_DTO_CLKMODE_PAR_SER__W                         1
#define   B_EC_OC_REG_DTO_CLKMODE_PAR_SER__M                         0x2
#define     B_EC_OC_REG_DTO_CLKMODE_PAR_SER_SERIAL_MODE              0x0
#define     B_EC_OC_REG_DTO_CLKMODE_PAR_SER_PARALLEL_MODE            0x2

#define B_EC_OC_REG_DTO_PER__A                                       0x2150048
#define B_EC_OC_REG_DTO_PER__W                                       8
#define B_EC_OC_REG_DTO_PER__M                                       0xFF
#define   B_EC_OC_REG_DTO_PER_INIT                                   0x6

#define B_EC_OC_REG_DTO_BUR__A                                       0x2150049
#define B_EC_OC_REG_DTO_BUR__W                                       2
#define B_EC_OC_REG_DTO_BUR__M                                       0x3
#define   B_EC_OC_REG_DTO_BUR_INIT                                   0x1
#define   B_EC_OC_REG_DTO_BUR_SELECT_1                               0x0
#define   B_EC_OC_REG_DTO_BUR_SELECT_188                             0x1
#define   B_EC_OC_REG_DTO_BUR_SELECT_204                             0x2
#define   B_EC_OC_REG_DTO_BUR_SELECT_47                              0x3

#define B_EC_OC_REG_RCR_CLKMODE__A                                   0x215004A
#define B_EC_OC_REG_RCR_CLKMODE__W                                   3
#define B_EC_OC_REG_RCR_CLKMODE__M                                   0x7
#define   B_EC_OC_REG_RCR_CLKMODE_INIT                               0x0

#define   B_EC_OC_REG_RCR_CLKMODE_FIFO_SOURCE__B                     0
#define   B_EC_OC_REG_RCR_CLKMODE_FIFO_SOURCE__W                     1
#define   B_EC_OC_REG_RCR_CLKMODE_FIFO_SOURCE__M                     0x1
#define     B_EC_OC_REG_RCR_CLKMODE_FIFO_SOURCE_FIFO_FRACIONAL       0x0
#define     B_EC_OC_REG_RCR_CLKMODE_FIFO_SOURCE_FIFO_RATIONAL        0x1

#define   B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SOURCE__B             1
#define   B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SOURCE__W             1
#define   B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SOURCE__M             0x2
#define     B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SOURCE_FEEDBACKLOOP_FRACTIONAL 0x0
#define     B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SOURCE_FEEDBACKLOOP_RATIONAL   0x2

#define   B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SELECT__B             2
#define   B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SELECT__W             1
#define   B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SELECT__M             0x4
#define     B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SELECT_SELECT_FIFO  0x0
#define     B_EC_OC_REG_RCR_CLKMODE_FEEDBACKLOOP_SELECT_SELECT_FEEDBACKLOOP     0x4

#define B_EC_OC_RAM__A                                               0x2160000

#define B_CC_SID                                                     0x1B

#define B_CC_COMM_EXEC__A                                            0x2400000
#define B_CC_COMM_EXEC__W                                            3
#define B_CC_COMM_EXEC__M                                            0x7
#define   B_CC_COMM_EXEC_CTL__B                                      0
#define   B_CC_COMM_EXEC_CTL__W                                      3
#define   B_CC_COMM_EXEC_CTL__M                                      0x7
#define     B_CC_COMM_EXEC_CTL_STOP                                  0x0
#define     B_CC_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_CC_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_CC_COMM_EXEC_CTL_STEP                                  0x3
#define     B_CC_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_CC_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_CC_COMM_STATE__A                                           0x2400001
#define B_CC_COMM_STATE__W                                           16
#define B_CC_COMM_STATE__M                                           0xFFFF
#define B_CC_COMM_MB__A                                              0x2400002
#define B_CC_COMM_MB__W                                              16
#define B_CC_COMM_MB__M                                              0xFFFF
#define B_CC_COMM_SERVICE0__A                                        0x2400003
#define B_CC_COMM_SERVICE0__W                                        16
#define B_CC_COMM_SERVICE0__M                                        0xFFFF
#define B_CC_COMM_SERVICE1__A                                        0x2400004
#define B_CC_COMM_SERVICE1__W                                        16
#define B_CC_COMM_SERVICE1__M                                        0xFFFF
#define B_CC_COMM_INT_STA__A                                         0x2400007
#define B_CC_COMM_INT_STA__W                                         16
#define B_CC_COMM_INT_STA__M                                         0xFFFF
#define B_CC_COMM_INT_MSK__A                                         0x2400008
#define B_CC_COMM_INT_MSK__W                                         16
#define B_CC_COMM_INT_MSK__M                                         0xFFFF

#define B_CC_REG_COMM_EXEC__A                                        0x2410000
#define B_CC_REG_COMM_EXEC__W                                        3
#define B_CC_REG_COMM_EXEC__M                                        0x7
#define   B_CC_REG_COMM_EXEC_CTL__B                                  0
#define   B_CC_REG_COMM_EXEC_CTL__W                                  3
#define   B_CC_REG_COMM_EXEC_CTL__M                                  0x7
#define     B_CC_REG_COMM_EXEC_CTL_STOP                              0x0
#define     B_CC_REG_COMM_EXEC_CTL_ACTIVE                            0x1
#define     B_CC_REG_COMM_EXEC_CTL_HOLD                              0x2
#define     B_CC_REG_COMM_EXEC_CTL_STEP                              0x3
#define     B_CC_REG_COMM_EXEC_CTL_BYPASS_STOP                       0x4
#define     B_CC_REG_COMM_EXEC_CTL_BYPASS_HOLD                       0x6

#define B_CC_REG_COMM_STATE__A                                       0x2410001
#define B_CC_REG_COMM_STATE__W                                       16
#define B_CC_REG_COMM_STATE__M                                       0xFFFF
#define B_CC_REG_COMM_MB__A                                          0x2410002
#define B_CC_REG_COMM_MB__W                                          16
#define B_CC_REG_COMM_MB__M                                          0xFFFF
#define B_CC_REG_COMM_SERVICE0__A                                    0x2410003
#define B_CC_REG_COMM_SERVICE0__W                                    16
#define B_CC_REG_COMM_SERVICE0__M                                    0xFFFF
#define B_CC_REG_COMM_SERVICE1__A                                    0x2410004
#define B_CC_REG_COMM_SERVICE1__W                                    16
#define B_CC_REG_COMM_SERVICE1__M                                    0xFFFF
#define B_CC_REG_COMM_INT_STA__A                                     0x2410007
#define B_CC_REG_COMM_INT_STA__W                                     16
#define B_CC_REG_COMM_INT_STA__M                                     0xFFFF
#define B_CC_REG_COMM_INT_MSK__A                                     0x2410008
#define B_CC_REG_COMM_INT_MSK__W                                     16
#define B_CC_REG_COMM_INT_MSK__M                                     0xFFFF

#define B_CC_REG_OSC_MODE__A                                         0x2410010
#define B_CC_REG_OSC_MODE__W                                         2
#define B_CC_REG_OSC_MODE__M                                         0x3
#define   B_CC_REG_OSC_MODE_OHW                                      0x0
#define   B_CC_REG_OSC_MODE_M20                                      0x1
#define   B_CC_REG_OSC_MODE_M48                                      0x2

#define B_CC_REG_PLL_MODE__A                                         0x2410011
#define B_CC_REG_PLL_MODE__W                                         6
#define B_CC_REG_PLL_MODE__M                                         0x3F
#define   B_CC_REG_PLL_MODE_INIT                                     0xC
#define   B_CC_REG_PLL_MODE_BYPASS__B                                0
#define   B_CC_REG_PLL_MODE_BYPASS__W                                2
#define   B_CC_REG_PLL_MODE_BYPASS__M                                0x3
#define     B_CC_REG_PLL_MODE_BYPASS_OHW                             0x0
#define     B_CC_REG_PLL_MODE_BYPASS_PLL                             0x1
#define     B_CC_REG_PLL_MODE_BYPASS_BYPASS                          0x2
#define   B_CC_REG_PLL_MODE_PUMP__B                                  2
#define   B_CC_REG_PLL_MODE_PUMP__W                                  3
#define   B_CC_REG_PLL_MODE_PUMP__M                                  0x1C
#define     B_CC_REG_PLL_MODE_PUMP_OFF                               0x0
#define     B_CC_REG_PLL_MODE_PUMP_CUR_08                            0x4
#define     B_CC_REG_PLL_MODE_PUMP_CUR_09                            0x8
#define     B_CC_REG_PLL_MODE_PUMP_CUR_10                            0xC
#define     B_CC_REG_PLL_MODE_PUMP_CUR_11                            0x10
#define     B_CC_REG_PLL_MODE_PUMP_CUR_12                            0x14
#define   B_CC_REG_PLL_MODE_OUT_EN__B                                5
#define   B_CC_REG_PLL_MODE_OUT_EN__W                                1
#define   B_CC_REG_PLL_MODE_OUT_EN__M                                0x20
#define     B_CC_REG_PLL_MODE_OUT_EN_OFF                             0x0
#define     B_CC_REG_PLL_MODE_OUT_EN_ON                              0x20

#define B_CC_REG_REF_DIVIDE__A                                       0x2410012
#define B_CC_REG_REF_DIVIDE__W                                       4
#define B_CC_REG_REF_DIVIDE__M                                       0xF
#define   B_CC_REG_REF_DIVIDE_INIT                                   0xA
#define   B_CC_REG_REF_DIVIDE_OHW                                    0x0
#define   B_CC_REG_REF_DIVIDE_D01                                    0x1
#define   B_CC_REG_REF_DIVIDE_D02                                    0x2
#define   B_CC_REG_REF_DIVIDE_D03                                    0x3
#define   B_CC_REG_REF_DIVIDE_D04                                    0x4
#define   B_CC_REG_REF_DIVIDE_D05                                    0x5
#define   B_CC_REG_REF_DIVIDE_D06                                    0x6
#define   B_CC_REG_REF_DIVIDE_D07                                    0x7
#define   B_CC_REG_REF_DIVIDE_D08                                    0x8
#define   B_CC_REG_REF_DIVIDE_D09                                    0x9
#define   B_CC_REG_REF_DIVIDE_D10                                    0xA

#define B_CC_REG_REF_DELAY__A                                        0x2410013
#define B_CC_REG_REF_DELAY__W                                        3
#define B_CC_REG_REF_DELAY__M                                        0x7
#define   B_CC_REG_REF_DELAY_EDGE__B                                 0
#define   B_CC_REG_REF_DELAY_EDGE__W                                 1
#define   B_CC_REG_REF_DELAY_EDGE__M                                 0x1
#define     B_CC_REG_REF_DELAY_EDGE_POS                              0x0
#define     B_CC_REG_REF_DELAY_EDGE_NEG                              0x1
#define   B_CC_REG_REF_DELAY_DELAY__B                                1
#define   B_CC_REG_REF_DELAY_DELAY__W                                2
#define   B_CC_REG_REF_DELAY_DELAY__M                                0x6
#define     B_CC_REG_REF_DELAY_DELAY_DEL_0                           0x0
#define     B_CC_REG_REF_DELAY_DELAY_DEL_3                           0x2
#define     B_CC_REG_REF_DELAY_DELAY_DEL_6                           0x4
#define     B_CC_REG_REF_DELAY_DELAY_DEL_9                           0x6

#define B_CC_REG_CLK_DELAY__A                                        0x2410014
#define B_CC_REG_CLK_DELAY__W                                        5
#define B_CC_REG_CLK_DELAY__M                                        0x1F
#define   B_CC_REG_CLK_DELAY_DELAY__B                                0
#define   B_CC_REG_CLK_DELAY_DELAY__W                                4
#define   B_CC_REG_CLK_DELAY_DELAY__M                                0xF
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_00                          0x0
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_05                          0x1
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_10                          0x2
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_15                          0x3
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_20                          0x4
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_25                          0x5
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_30                          0x6
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_35                          0x7
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_40                          0x8
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_45                          0x9
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_50                          0xA
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_55                          0xB
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_60                          0xC
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_65                          0xD
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_70                          0xE
#define     B_CC_REG_CLK_DELAY_DELAY_DEL_75                          0xF
#define   B_CC_REG_CLK_DELAY_EDGE__B                                 4
#define   B_CC_REG_CLK_DELAY_EDGE__W                                 1
#define   B_CC_REG_CLK_DELAY_EDGE__M                                 0x10
#define     B_CC_REG_CLK_DELAY_EDGE_POS                              0x0
#define     B_CC_REG_CLK_DELAY_EDGE_NEG                              0x10

#define B_CC_REG_PWD_MODE__A                                         0x2410015
#define B_CC_REG_PWD_MODE__W                                         2
#define B_CC_REG_PWD_MODE__M                                         0x3
#define   B_CC_REG_PWD_MODE_UP                                       0x0
#define   B_CC_REG_PWD_MODE_DOWN_CLK                                 0x1
#define   B_CC_REG_PWD_MODE_DOWN_PLL                                 0x2
#define   B_CC_REG_PWD_MODE_DOWN_OSC                                 0x3

#define B_CC_REG_SOFT_RST__A                                         0x2410016
#define B_CC_REG_SOFT_RST__W                                         2
#define B_CC_REG_SOFT_RST__M                                         0x3
#define   B_CC_REG_SOFT_RST_SYS__B                                   0
#define   B_CC_REG_SOFT_RST_SYS__W                                   1
#define   B_CC_REG_SOFT_RST_SYS__M                                   0x1
#define   B_CC_REG_SOFT_RST_OSC__B                                   1
#define   B_CC_REG_SOFT_RST_OSC__W                                   1
#define   B_CC_REG_SOFT_RST_OSC__M                                   0x2

#define B_CC_REG_UPDATE__A                                           0x2410017
#define B_CC_REG_UPDATE__W                                           16
#define B_CC_REG_UPDATE__M                                           0xFFFF
#define   B_CC_REG_UPDATE_KEY                                        0x3973

#define B_CC_REG_PLL_LOCK__A                                         0x2410018
#define B_CC_REG_PLL_LOCK__W                                         1
#define B_CC_REG_PLL_LOCK__M                                         0x1
#define   B_CC_REG_PLL_LOCK_LOCK                                     0x1

#define B_CC_REG_JTAGID_L__A                                         0x2410019
#define B_CC_REG_JTAGID_L__W                                         16
#define B_CC_REG_JTAGID_L__M                                         0xFFFF
#define   B_CC_REG_JTAGID_L_INIT                                     0x0

#define B_CC_REG_JTAGID_H__A                                         0x241001A
#define B_CC_REG_JTAGID_H__W                                         16
#define B_CC_REG_JTAGID_H__M                                         0xFFFF
#define   B_CC_REG_JTAGID_H_INIT                                     0x0

#define B_CC_REG_DIVERSITY__A                                        0x241001B
#define B_CC_REG_DIVERSITY__W                                        1
#define B_CC_REG_DIVERSITY__M                                        0x1
#define   B_CC_REG_DIVERSITY_INIT                                    0x0

#define B_CC_REG_BACKUP3V__A                                         0x241001C
#define B_CC_REG_BACKUP3V__W                                         1
#define B_CC_REG_BACKUP3V__M                                         0x1
#define   B_CC_REG_BACKUP3V_INIT                                     0x0

#define B_CC_REG_DRV_IO__A                                           0x241001D
#define B_CC_REG_DRV_IO__W                                           3
#define B_CC_REG_DRV_IO__M                                           0x7
#define   B_CC_REG_DRV_IO_INIT                                       0x2

#define B_CC_REG_DRV_MPG__A                                          0x241001E
#define B_CC_REG_DRV_MPG__W                                          3
#define B_CC_REG_DRV_MPG__M                                          0x7
#define   B_CC_REG_DRV_MPG_INIT                                      0x2

#define B_CC_REG_DRV_I2C1__A                                         0x241001F
#define B_CC_REG_DRV_I2C1__W                                         3
#define B_CC_REG_DRV_I2C1__M                                         0x7
#define   B_CC_REG_DRV_I2C1_INIT                                     0x2

#define B_CC_REG_DRV_I2C2__A                                         0x2410020
#define B_CC_REG_DRV_I2C2__W                                         1
#define B_CC_REG_DRV_I2C2__M                                         0x1
#define   B_CC_REG_DRV_I2C2_INIT                                     0x0

#define B_LC_SID                                                     0x1C

#define B_LC_COMM_EXEC__A                                            0x2800000
#define B_LC_COMM_EXEC__W                                            3
#define B_LC_COMM_EXEC__M                                            0x7
#define   B_LC_COMM_EXEC_CTL__B                                      0
#define   B_LC_COMM_EXEC_CTL__W                                      3
#define   B_LC_COMM_EXEC_CTL__M                                      0x7
#define     B_LC_COMM_EXEC_CTL_STOP                                  0x0
#define     B_LC_COMM_EXEC_CTL_ACTIVE                                0x1
#define     B_LC_COMM_EXEC_CTL_HOLD                                  0x2
#define     B_LC_COMM_EXEC_CTL_STEP                                  0x3
#define     B_LC_COMM_EXEC_CTL_BYPASS_STOP                           0x4
#define     B_LC_COMM_EXEC_CTL_BYPASS_HOLD                           0x6

#define B_LC_COMM_STATE__A                                           0x2800001
#define B_LC_COMM_STATE__W                                           16
#define B_LC_COMM_STATE__M                                           0xFFFF
#define B_LC_COMM_MB__A                                              0x2800002
#define B_LC_COMM_MB__W                                              16
#define B_LC_COMM_MB__M                                              0xFFFF
#define B_LC_COMM_SERVICE0__A                                        0x2800003
#define B_LC_COMM_SERVICE0__W                                        16
#define B_LC_COMM_SERVICE0__M                                        0xFFFF
#define B_LC_COMM_SERVICE1__A                                        0x2800004
#define B_LC_COMM_SERVICE1__W                                        16
#define B_LC_COMM_SERVICE1__M                                        0xFFFF
#define B_LC_COMM_INT_STA__A                                         0x2800007
#define B_LC_COMM_INT_STA__W                                         16
#define B_LC_COMM_INT_STA__M                                         0xFFFF
#define B_LC_COMM_INT_MSK__A                                         0x2800008
#define B_LC_COMM_INT_MSK__W                                         16
#define B_LC_COMM_INT_MSK__M                                         0xFFFF

#define B_LC_CT_REG_COMM_EXEC__A                                     0x2810000
#define B_LC_CT_REG_COMM_EXEC__W                                     3
#define B_LC_CT_REG_COMM_EXEC__M                                     0x7
#define   B_LC_CT_REG_COMM_EXEC_CTL__B                               0
#define   B_LC_CT_REG_COMM_EXEC_CTL__W                               3
#define   B_LC_CT_REG_COMM_EXEC_CTL__M                               0x7
#define     B_LC_CT_REG_COMM_EXEC_CTL_STOP                           0x0
#define     B_LC_CT_REG_COMM_EXEC_CTL_ACTIVE                         0x1
#define     B_LC_CT_REG_COMM_EXEC_CTL_HOLD                           0x2
#define     B_LC_CT_REG_COMM_EXEC_CTL_STEP                           0x3

#define B_LC_CT_REG_COMM_STATE__A                                    0x2810001
#define B_LC_CT_REG_COMM_STATE__W                                    10
#define B_LC_CT_REG_COMM_STATE__M                                    0x3FF
#define B_LC_CT_REG_COMM_SERVICE0__A                                 0x2810003
#define B_LC_CT_REG_COMM_SERVICE0__W                                 16
#define B_LC_CT_REG_COMM_SERVICE0__M                                 0xFFFF
#define B_LC_CT_REG_COMM_SERVICE1__A                                 0x2810004
#define B_LC_CT_REG_COMM_SERVICE1__W                                 16
#define B_LC_CT_REG_COMM_SERVICE1__M                                 0xFFFF
#define   B_LC_CT_REG_COMM_SERVICE1_LC__B                            12
#define   B_LC_CT_REG_COMM_SERVICE1_LC__W                            1
#define   B_LC_CT_REG_COMM_SERVICE1_LC__M                            0x1000

#define B_LC_CT_REG_COMM_INT_STA__A                                  0x2810007
#define B_LC_CT_REG_COMM_INT_STA__W                                  1
#define B_LC_CT_REG_COMM_INT_STA__M                                  0x1
#define   B_LC_CT_REG_COMM_INT_STA_REQUEST__B                        0
#define   B_LC_CT_REG_COMM_INT_STA_REQUEST__W                        1
#define   B_LC_CT_REG_COMM_INT_STA_REQUEST__M                        0x1

#define B_LC_CT_REG_COMM_INT_MSK__A                                  0x2810008
#define B_LC_CT_REG_COMM_INT_MSK__W                                  1
#define B_LC_CT_REG_COMM_INT_MSK__M                                  0x1
#define   B_LC_CT_REG_COMM_INT_MSK_REQUEST__B                        0
#define   B_LC_CT_REG_COMM_INT_MSK_REQUEST__W                        1
#define   B_LC_CT_REG_COMM_INT_MSK_REQUEST__M                        0x1

#define B_LC_CT_REG_CTL_STK__AX                                      0x2810010
#define B_LC_CT_REG_CTL_STK__XSZ                                     4
#define B_LC_CT_REG_CTL_STK__W                                       10
#define B_LC_CT_REG_CTL_STK__M                                       0x3FF

#define B_LC_CT_REG_CTL_BPT_IDX__A                                   0x281001F
#define B_LC_CT_REG_CTL_BPT_IDX__W                                   1
#define B_LC_CT_REG_CTL_BPT_IDX__M                                   0x1

#define B_LC_CT_REG_CTL_BPT__A                                       0x2810020
#define B_LC_CT_REG_CTL_BPT__W                                       10
#define B_LC_CT_REG_CTL_BPT__M                                       0x3FF

#define B_LC_RA_RAM_PROC_DELAY_IF__A                                 0x2820006
#define B_LC_RA_RAM_PROC_DELAY_IF__W                                 16
#define B_LC_RA_RAM_PROC_DELAY_IF__M                                 0xFFFF
#define B_LC_RA_RAM_PROC_DELAY_IF__PRE                               0xFFE6
#define B_LC_RA_RAM_PROC_DELAY_FS__A                                 0x2820007
#define B_LC_RA_RAM_PROC_DELAY_FS__W                                 16
#define B_LC_RA_RAM_PROC_DELAY_FS__M                                 0xFFFF
#define B_LC_RA_RAM_PROC_DELAY_FS__PRE                               0xFFE3
#define B_LC_RA_RAM_LOCK_TH_CRMM__A                                  0x2820008
#define B_LC_RA_RAM_LOCK_TH_CRMM__W                                  16
#define B_LC_RA_RAM_LOCK_TH_CRMM__M                                  0xFFFF
#define B_LC_RA_RAM_LOCK_TH_CRMM__PRE                                0xC8
#define B_LC_RA_RAM_LOCK_TH_SRMM__A                                  0x2820009
#define B_LC_RA_RAM_LOCK_TH_SRMM__W                                  16
#define B_LC_RA_RAM_LOCK_TH_SRMM__M                                  0xFFFF
#define B_LC_RA_RAM_LOCK_TH_SRMM__PRE                                0x46
#define B_LC_RA_RAM_LOCK_COUNT__A                                    0x282000A
#define B_LC_RA_RAM_LOCK_COUNT__W                                    16
#define B_LC_RA_RAM_LOCK_COUNT__M                                    0xFFFF
#define B_LC_RA_RAM_CPRTOFS_NOM__A                                   0x282000B
#define B_LC_RA_RAM_CPRTOFS_NOM__W                                   16
#define B_LC_RA_RAM_CPRTOFS_NOM__M                                   0xFFFF
#define B_LC_RA_RAM_IFINCR_NOM_L__A                                  0x282000C
#define B_LC_RA_RAM_IFINCR_NOM_L__W                                  16
#define B_LC_RA_RAM_IFINCR_NOM_L__M                                  0xFFFF
#define B_LC_RA_RAM_IFINCR_NOM_H__A                                  0x282000D
#define B_LC_RA_RAM_IFINCR_NOM_H__W                                  16
#define B_LC_RA_RAM_IFINCR_NOM_H__M                                  0xFFFF
#define B_LC_RA_RAM_FSINCR_NOM_L__A                                  0x282000E
#define B_LC_RA_RAM_FSINCR_NOM_L__W                                  16
#define B_LC_RA_RAM_FSINCR_NOM_L__M                                  0xFFFF
#define B_LC_RA_RAM_FSINCR_NOM_H__A                                  0x282000F
#define B_LC_RA_RAM_FSINCR_NOM_H__W                                  16
#define B_LC_RA_RAM_FSINCR_NOM_H__M                                  0xFFFF
#define B_LC_RA_RAM_MODE_2K__A                                       0x2820010
#define B_LC_RA_RAM_MODE_2K__W                                       16
#define B_LC_RA_RAM_MODE_2K__M                                       0xFFFF
#define B_LC_RA_RAM_MODE_GUARD__A                                    0x2820011
#define B_LC_RA_RAM_MODE_GUARD__W                                    16
#define B_LC_RA_RAM_MODE_GUARD__M                                    0xFFFF
#define   B_LC_RA_RAM_MODE_GUARD_32                                  0x0
#define   B_LC_RA_RAM_MODE_GUARD_16                                  0x1
#define   B_LC_RA_RAM_MODE_GUARD_8                                   0x2
#define   B_LC_RA_RAM_MODE_GUARD_4                                   0x3

#define B_LC_RA_RAM_MODE_ADJUST__A                                   0x2820012
#define B_LC_RA_RAM_MODE_ADJUST__W                                   16
#define B_LC_RA_RAM_MODE_ADJUST__M                                   0xFFFF
#define   B_LC_RA_RAM_MODE_ADJUST_CP_CRMM__B                         0
#define   B_LC_RA_RAM_MODE_ADJUST_CP_CRMM__W                         1
#define   B_LC_RA_RAM_MODE_ADJUST_CP_CRMM__M                         0x1
#define   B_LC_RA_RAM_MODE_ADJUST_CE_CRMM__B                         1
#define   B_LC_RA_RAM_MODE_ADJUST_CE_CRMM__W                         1
#define   B_LC_RA_RAM_MODE_ADJUST_CE_CRMM__M                         0x2
#define   B_LC_RA_RAM_MODE_ADJUST_SRMM__B                            2
#define   B_LC_RA_RAM_MODE_ADJUST_SRMM__W                            1
#define   B_LC_RA_RAM_MODE_ADJUST_SRMM__M                            0x4
#define   B_LC_RA_RAM_MODE_ADJUST_PHASE__B                           3
#define   B_LC_RA_RAM_MODE_ADJUST_PHASE__W                           1
#define   B_LC_RA_RAM_MODE_ADJUST_PHASE__M                           0x8
#define   B_LC_RA_RAM_MODE_ADJUST_DELAY__B                           4
#define   B_LC_RA_RAM_MODE_ADJUST_DELAY__W                           1
#define   B_LC_RA_RAM_MODE_ADJUST_DELAY__M                           0x10
#define   B_LC_RA_RAM_MODE_ADJUST_OPENLOOP__B                        5
#define   B_LC_RA_RAM_MODE_ADJUST_OPENLOOP__W                        1
#define   B_LC_RA_RAM_MODE_ADJUST_OPENLOOP__M                        0x20
#define   B_LC_RA_RAM_MODE_ADJUST_NO_CP__B                           6
#define   B_LC_RA_RAM_MODE_ADJUST_NO_CP__W                           1
#define   B_LC_RA_RAM_MODE_ADJUST_NO_CP__M                           0x40
#define   B_LC_RA_RAM_MODE_ADJUST_NO_FS__B                           7
#define   B_LC_RA_RAM_MODE_ADJUST_NO_FS__W                           1
#define   B_LC_RA_RAM_MODE_ADJUST_NO_FS__M                           0x80
#define   B_LC_RA_RAM_MODE_ADJUST_NO_IF__B                           8
#define   B_LC_RA_RAM_MODE_ADJUST_NO_IF__W                           1
#define   B_LC_RA_RAM_MODE_ADJUST_NO_IF__M                           0x100
#define   B_LC_RA_RAM_MODE_ADJUST_NO_PH_PIPE__B                      9
#define   B_LC_RA_RAM_MODE_ADJUST_NO_PH_PIPE__W                      1
#define   B_LC_RA_RAM_MODE_ADJUST_NO_PH_PIPE__M                      0x200
#define   B_LC_RA_RAM_MODE_ADJUST_CP_DIF_CRMM__B                     10
#define   B_LC_RA_RAM_MODE_ADJUST_CP_DIF_CRMM__W                     1
#define   B_LC_RA_RAM_MODE_ADJUST_CP_DIF_CRMM__M                     0x400
#define   B_LC_RA_RAM_MODE_ADJUST_CP_DIF_SRMM__B                     11
#define   B_LC_RA_RAM_MODE_ADJUST_CP_DIF_SRMM__W                     1
#define   B_LC_RA_RAM_MODE_ADJUST_CP_DIF_SRMM__M                     0x800

#define B_LC_RA_RAM_RC_STS__A                                        0x2820014
#define B_LC_RA_RAM_RC_STS__W                                        16
#define B_LC_RA_RAM_RC_STS__M                                        0xFFFF
#define B_LC_RA_RAM_ACTUAL_CP_DIF_CRMM__A                            0x2820018
#define B_LC_RA_RAM_ACTUAL_CP_DIF_CRMM__W                            16
#define B_LC_RA_RAM_ACTUAL_CP_DIF_CRMM__M                            0xFFFF
#define B_LC_RA_RAM_ACTUAL_CP_DIF_SRMM__A                            0x2820019
#define B_LC_RA_RAM_ACTUAL_CP_DIF_SRMM__W                            16
#define B_LC_RA_RAM_ACTUAL_CP_DIF_SRMM__M                            0xFFFF
#define B_LC_RA_RAM_FILTER_SYM_SET__A                                0x282001A
#define B_LC_RA_RAM_FILTER_SYM_SET__W                                16
#define B_LC_RA_RAM_FILTER_SYM_SET__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_SYM_SET__PRE                              0x3E8
#define B_LC_RA_RAM_FILTER_SYM_CUR__A                                0x282001B
#define B_LC_RA_RAM_FILTER_SYM_CUR__W                                16
#define B_LC_RA_RAM_FILTER_SYM_CUR__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_SYM_CUR__PRE                              0x0
#define B_LC_RA_RAM_DIVERSITY_DELAY__A                               0x282001C
#define B_LC_RA_RAM_DIVERSITY_DELAY__W                               16
#define B_LC_RA_RAM_DIVERSITY_DELAY__M                               0xFFFF
#define B_LC_RA_RAM_DIVERSITY_DELAY__PRE                             0x3E8
#define B_LC_RA_RAM_MAX_ABS_EXP__A                                   0x282001D
#define B_LC_RA_RAM_MAX_ABS_EXP__W                                   16
#define B_LC_RA_RAM_MAX_ABS_EXP__M                                   0xFFFF
#define B_LC_RA_RAM_MAX_ABS_EXP__PRE                                 0x10
#define B_LC_RA_RAM_ACTUAL_CP_CRMM__A                                0x282001F
#define B_LC_RA_RAM_ACTUAL_CP_CRMM__W                                16
#define B_LC_RA_RAM_ACTUAL_CP_CRMM__M                                0xFFFF
#define B_LC_RA_RAM_ACTUAL_CE_CRMM__A                                0x2820020
#define B_LC_RA_RAM_ACTUAL_CE_CRMM__W                                16
#define B_LC_RA_RAM_ACTUAL_CE_CRMM__M                                0xFFFF
#define B_LC_RA_RAM_ACTUAL_CE_SRMM__A                                0x2820021
#define B_LC_RA_RAM_ACTUAL_CE_SRMM__W                                16
#define B_LC_RA_RAM_ACTUAL_CE_SRMM__M                                0xFFFF
#define B_LC_RA_RAM_ACTUAL_PHASE__A                                  0x2820022
#define B_LC_RA_RAM_ACTUAL_PHASE__W                                  16
#define B_LC_RA_RAM_ACTUAL_PHASE__M                                  0xFFFF
#define B_LC_RA_RAM_ACTUAL_DELAY__A                                  0x2820023
#define B_LC_RA_RAM_ACTUAL_DELAY__W                                  16
#define B_LC_RA_RAM_ACTUAL_DELAY__M                                  0xFFFF
#define B_LC_RA_RAM_ADJUST_CRMM__A                                   0x2820024
#define B_LC_RA_RAM_ADJUST_CRMM__W                                   16
#define B_LC_RA_RAM_ADJUST_CRMM__M                                   0xFFFF
#define B_LC_RA_RAM_ADJUST_SRMM__A                                   0x2820025
#define B_LC_RA_RAM_ADJUST_SRMM__W                                   16
#define B_LC_RA_RAM_ADJUST_SRMM__M                                   0xFFFF
#define B_LC_RA_RAM_ADJUST_PHASE__A                                  0x2820026
#define B_LC_RA_RAM_ADJUST_PHASE__W                                  16
#define B_LC_RA_RAM_ADJUST_PHASE__M                                  0xFFFF
#define B_LC_RA_RAM_ADJUST_DELAY__A                                  0x2820027
#define B_LC_RA_RAM_ADJUST_DELAY__W                                  16
#define B_LC_RA_RAM_ADJUST_DELAY__M                                  0xFFFF

#define B_LC_RA_RAM_PIPE_CP_PHASE_0__A                               0x2820028
#define B_LC_RA_RAM_PIPE_CP_PHASE_0__W                               16
#define B_LC_RA_RAM_PIPE_CP_PHASE_0__M                               0xFFFF
#define B_LC_RA_RAM_PIPE_CP_PHASE_1__A                               0x2820029
#define B_LC_RA_RAM_PIPE_CP_PHASE_1__W                               16
#define B_LC_RA_RAM_PIPE_CP_PHASE_1__M                               0xFFFF
#define B_LC_RA_RAM_PIPE_CP_PHASE_CON__A                             0x282002A
#define B_LC_RA_RAM_PIPE_CP_PHASE_CON__W                             16
#define B_LC_RA_RAM_PIPE_CP_PHASE_CON__M                             0xFFFF
#define B_LC_RA_RAM_PIPE_CP_PHASE_DIF__A                             0x282002B
#define B_LC_RA_RAM_PIPE_CP_PHASE_DIF__W                             16
#define B_LC_RA_RAM_PIPE_CP_PHASE_DIF__M                             0xFFFF
#define B_LC_RA_RAM_PIPE_CP_PHASE_RES__A                             0x282002C
#define B_LC_RA_RAM_PIPE_CP_PHASE_RES__W                             16
#define B_LC_RA_RAM_PIPE_CP_PHASE_RES__M                             0xFFFF
#define B_LC_RA_RAM_PIPE_CP_PHASE_RZ__A                              0x282002D
#define B_LC_RA_RAM_PIPE_CP_PHASE_RZ__W                              16
#define B_LC_RA_RAM_PIPE_CP_PHASE_RZ__M                              0xFFFF

#define B_LC_RA_RAM_PIPE_CP_CRMM_0__A                                0x2820030
#define B_LC_RA_RAM_PIPE_CP_CRMM_0__W                                16
#define B_LC_RA_RAM_PIPE_CP_CRMM_0__M                                0xFFFF
#define B_LC_RA_RAM_PIPE_CP_CRMM_1__A                                0x2820031
#define B_LC_RA_RAM_PIPE_CP_CRMM_1__W                                16
#define B_LC_RA_RAM_PIPE_CP_CRMM_1__M                                0xFFFF
#define B_LC_RA_RAM_PIPE_CP_CRMM_CON__A                              0x2820032
#define B_LC_RA_RAM_PIPE_CP_CRMM_CON__W                              16
#define B_LC_RA_RAM_PIPE_CP_CRMM_CON__M                              0xFFFF
#define B_LC_RA_RAM_PIPE_CP_CRMM_DIF__A                              0x2820033
#define B_LC_RA_RAM_PIPE_CP_CRMM_DIF__W                              16
#define B_LC_RA_RAM_PIPE_CP_CRMM_DIF__M                              0xFFFF
#define B_LC_RA_RAM_PIPE_CP_CRMM_RES__A                              0x2820034
#define B_LC_RA_RAM_PIPE_CP_CRMM_RES__W                              16
#define B_LC_RA_RAM_PIPE_CP_CRMM_RES__M                              0xFFFF
#define B_LC_RA_RAM_PIPE_CP_CRMM_RZ__A                               0x2820035
#define B_LC_RA_RAM_PIPE_CP_CRMM_RZ__W                               16
#define B_LC_RA_RAM_PIPE_CP_CRMM_RZ__M                               0xFFFF

#define B_LC_RA_RAM_PIPE_CP_SRMM_0__A                                0x2820038
#define B_LC_RA_RAM_PIPE_CP_SRMM_0__W                                16
#define B_LC_RA_RAM_PIPE_CP_SRMM_0__M                                0xFFFF
#define B_LC_RA_RAM_PIPE_CP_SRMM_1__A                                0x2820039
#define B_LC_RA_RAM_PIPE_CP_SRMM_1__W                                16
#define B_LC_RA_RAM_PIPE_CP_SRMM_1__M                                0xFFFF
#define B_LC_RA_RAM_PIPE_CP_SRMM_CON__A                              0x282003A
#define B_LC_RA_RAM_PIPE_CP_SRMM_CON__W                              16
#define B_LC_RA_RAM_PIPE_CP_SRMM_CON__M                              0xFFFF
#define B_LC_RA_RAM_PIPE_CP_SRMM_DIF__A                              0x282003B
#define B_LC_RA_RAM_PIPE_CP_SRMM_DIF__W                              16
#define B_LC_RA_RAM_PIPE_CP_SRMM_DIF__M                              0xFFFF
#define B_LC_RA_RAM_PIPE_CP_SRMM_RES__A                              0x282003C
#define B_LC_RA_RAM_PIPE_CP_SRMM_RES__W                              16
#define B_LC_RA_RAM_PIPE_CP_SRMM_RES__M                              0xFFFF
#define B_LC_RA_RAM_PIPE_CP_SRMM_RZ__A                               0x282003D
#define B_LC_RA_RAM_PIPE_CP_SRMM_RZ__W                               16
#define B_LC_RA_RAM_PIPE_CP_SRMM_RZ__M                               0xFFFF

#define B_LC_RA_RAM_FILTER_CRMM_A__A                                 0x2820060
#define B_LC_RA_RAM_FILTER_CRMM_A__W                                 16
#define B_LC_RA_RAM_FILTER_CRMM_A__M                                 0xFFFF
#define B_LC_RA_RAM_FILTER_CRMM_A__PRE                               0x4
#define B_LC_RA_RAM_FILTER_CRMM_B__A                                 0x2820061
#define B_LC_RA_RAM_FILTER_CRMM_B__W                                 16
#define B_LC_RA_RAM_FILTER_CRMM_B__M                                 0xFFFF
#define B_LC_RA_RAM_FILTER_CRMM_B__PRE                               0x1
#define B_LC_RA_RAM_FILTER_CRMM_Z1__AX                               0x2820062
#define B_LC_RA_RAM_FILTER_CRMM_Z1__XSZ                              2
#define B_LC_RA_RAM_FILTER_CRMM_Z1__W                                16
#define B_LC_RA_RAM_FILTER_CRMM_Z1__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_CRMM_Z2__AX                               0x2820064
#define B_LC_RA_RAM_FILTER_CRMM_Z2__XSZ                              2
#define B_LC_RA_RAM_FILTER_CRMM_Z2__W                                16
#define B_LC_RA_RAM_FILTER_CRMM_Z2__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_CRMM_TMP__AX                              0x2820066
#define B_LC_RA_RAM_FILTER_CRMM_TMP__XSZ                             2
#define B_LC_RA_RAM_FILTER_CRMM_TMP__W                               16
#define B_LC_RA_RAM_FILTER_CRMM_TMP__M                               0xFFFF

#define B_LC_RA_RAM_FILTER_SRMM_A__A                                 0x2820068
#define B_LC_RA_RAM_FILTER_SRMM_A__W                                 16
#define B_LC_RA_RAM_FILTER_SRMM_A__M                                 0xFFFF
#define B_LC_RA_RAM_FILTER_SRMM_A__PRE                               0x4
#define B_LC_RA_RAM_FILTER_SRMM_B__A                                 0x2820069
#define B_LC_RA_RAM_FILTER_SRMM_B__W                                 16
#define B_LC_RA_RAM_FILTER_SRMM_B__M                                 0xFFFF
#define B_LC_RA_RAM_FILTER_SRMM_B__PRE                               0x1
#define B_LC_RA_RAM_FILTER_SRMM_Z1__AX                               0x282006A
#define B_LC_RA_RAM_FILTER_SRMM_Z1__XSZ                              2
#define B_LC_RA_RAM_FILTER_SRMM_Z1__W                                16
#define B_LC_RA_RAM_FILTER_SRMM_Z1__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_SRMM_Z2__AX                               0x282006C
#define B_LC_RA_RAM_FILTER_SRMM_Z2__XSZ                              2
#define B_LC_RA_RAM_FILTER_SRMM_Z2__W                                16
#define B_LC_RA_RAM_FILTER_SRMM_Z2__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_SRMM_TMP__AX                              0x282006E
#define B_LC_RA_RAM_FILTER_SRMM_TMP__XSZ                             2
#define B_LC_RA_RAM_FILTER_SRMM_TMP__W                               16
#define B_LC_RA_RAM_FILTER_SRMM_TMP__M                               0xFFFF

#define B_LC_RA_RAM_FILTER_PHASE_A__A                                0x2820070
#define B_LC_RA_RAM_FILTER_PHASE_A__W                                16
#define B_LC_RA_RAM_FILTER_PHASE_A__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_PHASE_A__PRE                              0x4
#define B_LC_RA_RAM_FILTER_PHASE_B__A                                0x2820071
#define B_LC_RA_RAM_FILTER_PHASE_B__W                                16
#define B_LC_RA_RAM_FILTER_PHASE_B__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_PHASE_B__PRE                              0x1
#define B_LC_RA_RAM_FILTER_PHASE_Z1__AX                              0x2820072
#define B_LC_RA_RAM_FILTER_PHASE_Z1__XSZ                             2
#define B_LC_RA_RAM_FILTER_PHASE_Z1__W                               16
#define B_LC_RA_RAM_FILTER_PHASE_Z1__M                               0xFFFF
#define B_LC_RA_RAM_FILTER_PHASE_Z2__AX                              0x2820074
#define B_LC_RA_RAM_FILTER_PHASE_Z2__XSZ                             2
#define B_LC_RA_RAM_FILTER_PHASE_Z2__W                               16
#define B_LC_RA_RAM_FILTER_PHASE_Z2__M                               0xFFFF
#define B_LC_RA_RAM_FILTER_PHASE_TMP__AX                             0x2820076
#define B_LC_RA_RAM_FILTER_PHASE_TMP__XSZ                            2
#define B_LC_RA_RAM_FILTER_PHASE_TMP__W                              16
#define B_LC_RA_RAM_FILTER_PHASE_TMP__M                              0xFFFF

#define B_LC_RA_RAM_FILTER_DELAY_A__A                                0x2820078
#define B_LC_RA_RAM_FILTER_DELAY_A__W                                16
#define B_LC_RA_RAM_FILTER_DELAY_A__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_DELAY_A__PRE                              0x4
#define B_LC_RA_RAM_FILTER_DELAY_B__A                                0x2820079
#define B_LC_RA_RAM_FILTER_DELAY_B__W                                16
#define B_LC_RA_RAM_FILTER_DELAY_B__M                                0xFFFF
#define B_LC_RA_RAM_FILTER_DELAY_B__PRE                              0x1
#define B_LC_RA_RAM_FILTER_DELAY_Z1__AX                              0x282007A
#define B_LC_RA_RAM_FILTER_DELAY_Z1__XSZ                             2
#define B_LC_RA_RAM_FILTER_DELAY_Z1__W                               16
#define B_LC_RA_RAM_FILTER_DELAY_Z1__M                               0xFFFF
#define B_LC_RA_RAM_FILTER_DELAY_Z2__AX                              0x282007C
#define B_LC_RA_RAM_FILTER_DELAY_Z2__XSZ                             2
#define B_LC_RA_RAM_FILTER_DELAY_Z2__W                               16
#define B_LC_RA_RAM_FILTER_DELAY_Z2__M                               0xFFFF
#define B_LC_RA_RAM_FILTER_DELAY_TMP__AX                             0x282007E
#define B_LC_RA_RAM_FILTER_DELAY_TMP__XSZ                            2
#define B_LC_RA_RAM_FILTER_DELAY_TMP__W                              16
#define B_LC_RA_RAM_FILTER_DELAY_TMP__M                              0xFFFF

#define B_LC_IF_RAM_TRP_BPT0__AX                                     0x2830000
#define B_LC_IF_RAM_TRP_BPT0__XSZ                                    2
#define B_LC_IF_RAM_TRP_BPT0__W                                      12
#define B_LC_IF_RAM_TRP_BPT0__M                                      0xFFF

#define B_LC_IF_RAM_TRP_STKU__AX                                     0x2830002
#define B_LC_IF_RAM_TRP_STKU__XSZ                                    2
#define B_LC_IF_RAM_TRP_STKU__W                                      12
#define B_LC_IF_RAM_TRP_STKU__M                                      0xFFF

#define B_LC_IF_RAM_TRP_WARM__AX                                     0x2830006
#define B_LC_IF_RAM_TRP_WARM__XSZ                                    2
#define B_LC_IF_RAM_TRP_WARM__W                                      12
#define B_LC_IF_RAM_TRP_WARM__M                                      0xFFF

#endif
