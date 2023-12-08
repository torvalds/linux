/* SPDX-License-Identifier: GPL-2.0-only */
/*****************************************************************************
 *                                                                           *
 * File: suni1x10gexp_regs.h                                                 *
 * $Revision: 1.9 $                                                          *
 * $Date: 2005/06/22 00:17:04 $                                              *
 * Description:                                                              *
 *  PMC/SIERRA (pm3393) MAC-PHY functionality.                               *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: PMC/SIERRA                                                       *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef _CXGB_SUNI1x10GEXP_REGS_H_
#define _CXGB_SUNI1x10GEXP_REGS_H_

/*
** Space allocated for each Exact Match Filter
**     There are 8 filter configurations
*/
#define SUNI1x10GEXP_REG_SIZEOF_MAC_FILTER 0x0003

#define mSUNI1x10GEXP_MAC_FILTER_OFFSET(filterId)       ( (filterId) * SUNI1x10GEXP_REG_SIZEOF_MAC_FILTER )

/*
** Space allocated for VLAN-Id Filter
**      There are 8 filter configurations
*/
#define SUNI1x10GEXP_REG_SIZEOF_MAC_VID_FILTER 0x0001

#define mSUNI1x10GEXP_MAC_VID_FILTER_OFFSET(filterId)   ( (filterId) * SUNI1x10GEXP_REG_SIZEOF_MAC_VID_FILTER )

/*
** Space allocated for each MSTAT Counter
*/
#define SUNI1x10GEXP_REG_SIZEOF_MSTAT_COUNT 0x0004

#define mSUNI1x10GEXP_MSTAT_COUNT_OFFSET(countId)       ( (countId) * SUNI1x10GEXP_REG_SIZEOF_MSTAT_COUNT )


/******************************************************************************/
/** S/UNI-1x10GE-XP REGISTER ADDRESS MAP                                     **/
/******************************************************************************/
/* Refer to the Register Bit Masks bellow for the naming of each register and */
/* to the S/UNI-1x10GE-XP Data Sheet for the signification of each bit        */
/******************************************************************************/


#define SUNI1x10GEXP_REG_IDENTIFICATION                                  0x0000
#define SUNI1x10GEXP_REG_PRODUCT_REVISION                                0x0001
#define SUNI1x10GEXP_REG_CONFIG_AND_RESET_CONTROL                        0x0002
#define SUNI1x10GEXP_REG_LOOPBACK_MISC_CTRL                              0x0003
#define SUNI1x10GEXP_REG_DEVICE_STATUS                                   0x0004
#define SUNI1x10GEXP_REG_GLOBAL_PERFORMANCE_MONITOR_UPDATE               0x0005

#define SUNI1x10GEXP_REG_MDIO_COMMAND                                    0x0006
#define SUNI1x10GEXP_REG_MDIO_INTERRUPT_ENABLE                           0x0007
#define SUNI1x10GEXP_REG_MDIO_INTERRUPT_STATUS                           0x0008
#define SUNI1x10GEXP_REG_MMD_PHY_ADDRESS                                 0x0009
#define SUNI1x10GEXP_REG_MMD_CONTROL_ADDRESS_DATA                        0x000A
#define SUNI1x10GEXP_REG_MDIO_READ_STATUS_DATA                           0x000B

#define SUNI1x10GEXP_REG_OAM_INTF_CTRL                                   0x000C
#define SUNI1x10GEXP_REG_MASTER_INTERRUPT_STATUS                         0x000D
#define SUNI1x10GEXP_REG_GLOBAL_INTERRUPT_ENABLE                         0x000E
#define SUNI1x10GEXP_REG_FREE                                            0x000F

#define SUNI1x10GEXP_REG_XTEF_MISC_CTRL                                  0x0010
#define SUNI1x10GEXP_REG_XRF_MISC_CTRL                                   0x0011

#define SUNI1x10GEXP_REG_SERDES_3125_CONFIG_1                            0x0100
#define SUNI1x10GEXP_REG_SERDES_3125_CONFIG_2                            0x0101
#define SUNI1x10GEXP_REG_SERDES_3125_INTERRUPT_ENABLE                    0x0102
#define SUNI1x10GEXP_REG_SERDES_3125_INTERRUPT_VISIBLE                   0x0103
#define SUNI1x10GEXP_REG_SERDES_3125_INTERRUPT_STATUS                    0x0104
#define SUNI1x10GEXP_REG_SERDES_3125_TEST_CONFIG                         0x0107

#define SUNI1x10GEXP_REG_RXXG_CONFIG_1                                   0x2040
#define SUNI1x10GEXP_REG_RXXG_CONFIG_2                                   0x2041
#define SUNI1x10GEXP_REG_RXXG_CONFIG_3                                   0x2042
#define SUNI1x10GEXP_REG_RXXG_INTERRUPT                                  0x2043
#define SUNI1x10GEXP_REG_RXXG_MAX_FRAME_LENGTH                           0x2045
#define SUNI1x10GEXP_REG_RXXG_SA_15_0                                    0x2046
#define SUNI1x10GEXP_REG_RXXG_SA_31_16                                   0x2047
#define SUNI1x10GEXP_REG_RXXG_SA_47_32                                   0x2048
#define SUNI1x10GEXP_REG_RXXG_RECEIVE_FIFO_THRESHOLD                     0x2049
#define mSUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_LOW(filterId) (0x204A + mSUNI1x10GEXP_MAC_FILTER_OFFSET(filterId))
#define mSUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_MID(filterId) (0x204B + mSUNI1x10GEXP_MAC_FILTER_OFFSET(filterId))
#define mSUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_HIGH(filterId)(0x204C + mSUNI1x10GEXP_MAC_FILTER_OFFSET(filterId))
#define mSUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID(filterId)      (0x2062 + mSUNI1x10GEXP_MAC_VID_FILTER_OFFSET(filterId))
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_0_LOW                     0x204A
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_0_MID                     0x204B
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_0_HIGH                    0x204C
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_1_LOW                     0x204D
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_1_MID                     0x204E
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_1_HIGH                    0x204F
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_2_LOW                     0x2050
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_2_MID                     0x2051
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_2_HIGH                    0x2052
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_3_LOW                     0x2053
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_3_MID                     0x2054
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_3_HIGH                    0x2055
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_4_LOW                     0x2056
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_4_MID                     0x2057
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_4_HIGH                    0x2058
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_5_LOW                     0x2059
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_5_MID                     0x205A
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_5_HIGH                    0x205B
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_6_LOW                     0x205C
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_6_MID                     0x205D
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_6_HIGH                    0x205E
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_7_LOW                     0x205F
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_7_MID                     0x2060
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_7_HIGH                    0x2061
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID_0                          0x2062
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID_1                          0x2063
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID_2                          0x2064
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID_3                          0x2065
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID_4                          0x2066
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID_5                          0x2067
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID_6                          0x2068
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_VID_7                          0x2069
#define SUNI1x10GEXP_REG_RXXG_MULTICAST_HASH_LOW                         0x206A
#define SUNI1x10GEXP_REG_RXXG_MULTICAST_HASH_MIDLOW                      0x206B
#define SUNI1x10GEXP_REG_RXXG_MULTICAST_HASH_MIDHIGH                     0x206C
#define SUNI1x10GEXP_REG_RXXG_MULTICAST_HASH_HIGH                        0x206D
#define SUNI1x10GEXP_REG_RXXG_ADDRESS_FILTER_CONTROL_0                   0x206E
#define SUNI1x10GEXP_REG_RXXG_ADDRESS_FILTER_CONTROL_1                   0x206F
#define SUNI1x10GEXP_REG_RXXG_ADDRESS_FILTER_CONTROL_2                   0x2070

#define SUNI1x10GEXP_REG_XRF_PATTERN_GEN_CTRL                            0x2081
#define SUNI1x10GEXP_REG_XRF_8BTB_ERR_COUNT_LANE_0                       0x2084
#define SUNI1x10GEXP_REG_XRF_8BTB_ERR_COUNT_LANE_1                       0x2085
#define SUNI1x10GEXP_REG_XRF_8BTB_ERR_COUNT_LANE_2                       0x2086
#define SUNI1x10GEXP_REG_XRF_8BTB_ERR_COUNT_LANE_3                       0x2087
#define SUNI1x10GEXP_REG_XRF_INTERRUPT_ENABLE                            0x2088
#define SUNI1x10GEXP_REG_XRF_INTERRUPT_STATUS                            0x2089
#define SUNI1x10GEXP_REG_XRF_ERR_STATUS                                  0x208A
#define SUNI1x10GEXP_REG_XRF_DIAG_INTERRUPT_ENABLE                       0x208B
#define SUNI1x10GEXP_REG_XRF_DIAG_INTERRUPT_STATUS                       0x208C
#define SUNI1x10GEXP_REG_XRF_CODE_ERR_THRES                              0x2092

#define SUNI1x10GEXP_REG_RXOAM_CONFIG                                    0x20C0
#define SUNI1x10GEXP_REG_RXOAM_FILTER_1_CONFIG                           0x20C1
#define SUNI1x10GEXP_REG_RXOAM_FILTER_2_CONFIG                           0x20C2
#define SUNI1x10GEXP_REG_RXOAM_CONFIG_2                                  0x20C3
#define SUNI1x10GEXP_REG_RXOAM_HEC_CONFIG                                0x20C4
#define SUNI1x10GEXP_REG_RXOAM_HEC_ERR_THRES                             0x20C5
#define SUNI1x10GEXP_REG_RXOAM_INTERRUPT_ENABLE                          0x20C7
#define SUNI1x10GEXP_REG_RXOAM_INTERRUPT_STATUS                          0x20C8
#define SUNI1x10GEXP_REG_RXOAM_STATUS                                    0x20C9
#define SUNI1x10GEXP_REG_RXOAM_HEC_ERR_COUNT                             0x20CA
#define SUNI1x10GEXP_REG_RXOAM_FIFO_OVERFLOW_COUNT                       0x20CB
#define SUNI1x10GEXP_REG_RXOAM_FILTER_MISMATCH_COUNT_LSB                 0x20CC
#define SUNI1x10GEXP_REG_RXOAM_FILTER_MISMATCH_COUNT_MSB                 0x20CD
#define SUNI1x10GEXP_REG_RXOAM_FILTER_1_MISMATCH_COUNT_LSB               0x20CE
#define SUNI1x10GEXP_REG_RXOAM_FILTER_1_MISMATCH_COUNT_MSB               0x20CF
#define SUNI1x10GEXP_REG_RXOAM_FILTER_2_MISMATCH_COUNT_LSB               0x20D0
#define SUNI1x10GEXP_REG_RXOAM_FILTER_2_MISMATCH_COUNT_MSB               0x20D1
#define SUNI1x10GEXP_REG_RXOAM_OAM_EXTRACT_COUNT_LSB                     0x20D2
#define SUNI1x10GEXP_REG_RXOAM_OAM_EXTRACT_COUNT_MSB                     0x20D3
#define SUNI1x10GEXP_REG_RXOAM_MINI_PACKET_COUNT_LSB                     0x20D4
#define SUNI1x10GEXP_REG_RXOAM_MINI_PACKET_COUNT_MSB                     0x20D5
#define SUNI1x10GEXP_REG_RXOAM_FILTER_MISMATCH_THRES_LSB                 0x20D6
#define SUNI1x10GEXP_REG_RXOAM_FILTER_MISMATCH_THRES_MSB                 0x20D7

#define SUNI1x10GEXP_REG_MSTAT_CONTROL                                   0x2100
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_ROLLOVER_0                        0x2101
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_ROLLOVER_1                        0x2102
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_ROLLOVER_2                        0x2103
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_ROLLOVER_3                        0x2104
#define SUNI1x10GEXP_REG_MSTAT_INTERRUPT_MASK_0                          0x2105
#define SUNI1x10GEXP_REG_MSTAT_INTERRUPT_MASK_1                          0x2106
#define SUNI1x10GEXP_REG_MSTAT_INTERRUPT_MASK_2                          0x2107
#define SUNI1x10GEXP_REG_MSTAT_INTERRUPT_MASK_3                          0x2108
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_WRITE_ADDRESS                     0x2109
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_WRITE_DATA_LOW                    0x210A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_WRITE_DATA_MIDDLE                 0x210B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_WRITE_DATA_HIGH                   0x210C
#define mSUNI1x10GEXP_REG_MSTAT_COUNTER_LOW(countId)   (0x2110 + mSUNI1x10GEXP_MSTAT_COUNT_OFFSET(countId))
#define mSUNI1x10GEXP_REG_MSTAT_COUNTER_MID(countId)   (0x2111 + mSUNI1x10GEXP_MSTAT_COUNT_OFFSET(countId))
#define mSUNI1x10GEXP_REG_MSTAT_COUNTER_HIGH(countId)  (0x2112 + mSUNI1x10GEXP_MSTAT_COUNT_OFFSET(countId))
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_0_LOW                             0x2110
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_0_MID                             0x2111
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_0_HIGH                            0x2112
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_0_RESVD                           0x2113
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_1_LOW                             0x2114
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_1_MID                             0x2115
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_1_HIGH                            0x2116
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_1_RESVD                           0x2117
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_2_LOW                             0x2118
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_2_MID                             0x2119
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_2_HIGH                            0x211A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_2_RESVD                           0x211B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_3_LOW                             0x211C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_3_MID                             0x211D
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_3_HIGH                            0x211E
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_3_RESVD                           0x211F
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_4_LOW                             0x2120
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_4_MID                             0x2121
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_4_HIGH                            0x2122
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_4_RESVD                           0x2123
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_5_LOW                             0x2124
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_5_MID                             0x2125
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_5_HIGH                            0x2126
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_5_RESVD                           0x2127
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_6_LOW                             0x2128
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_6_MID                             0x2129
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_6_HIGH                            0x212A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_6_RESVD                           0x212B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_7_LOW                             0x212C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_7_MID                             0x212D
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_7_HIGH                            0x212E
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_7_RESVD                           0x212F
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_8_LOW                             0x2130
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_8_MID                             0x2131
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_8_HIGH                            0x2132
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_8_RESVD                           0x2133
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_9_LOW                             0x2134
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_9_MID                             0x2135
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_9_HIGH                            0x2136
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_9_RESVD                           0x2137
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_10_LOW                            0x2138
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_10_MID                            0x2139
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_10_HIGH                           0x213A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_10_RESVD                          0x213B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_11_LOW                            0x213C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_11_MID                            0x213D
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_11_HIGH                           0x213E
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_11_RESVD                          0x213F
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_12_LOW                            0x2140
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_12_MID                            0x2141
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_12_HIGH                           0x2142
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_12_RESVD                          0x2143
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_13_LOW                            0x2144
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_13_MID                            0x2145
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_13_HIGH                           0x2146
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_13_RESVD                          0x2147
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_14_LOW                            0x2148
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_14_MID                            0x2149
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_14_HIGH                           0x214A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_14_RESVD                          0x214B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_15_LOW                            0x214C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_15_MID                            0x214D
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_15_HIGH                           0x214E
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_15_RESVD                          0x214F
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_16_LOW                            0x2150
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_16_MID                            0x2151
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_16_HIGH                           0x2152
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_16_RESVD                          0x2153
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_17_LOW                            0x2154
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_17_MID                            0x2155
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_17_HIGH                           0x2156
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_17_RESVD                          0x2157
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_18_LOW                            0x2158
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_18_MID                            0x2159
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_18_HIGH                           0x215A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_18_RESVD                          0x215B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_19_LOW                            0x215C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_19_MID                            0x215D
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_19_HIGH                           0x215E
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_19_RESVD                          0x215F
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_20_LOW                            0x2160
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_20_MID                            0x2161
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_20_HIGH                           0x2162
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_20_RESVD                          0x2163
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_21_LOW                            0x2164
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_21_MID                            0x2165
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_21_HIGH                           0x2166
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_21_RESVD                          0x2167
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_22_LOW                            0x2168
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_22_MID                            0x2169
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_22_HIGH                           0x216A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_22_RESVD                          0x216B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_23_LOW                            0x216C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_23_MID                            0x216D
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_23_HIGH                           0x216E
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_23_RESVD                          0x216F
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_24_LOW                            0x2170
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_24_MID                            0x2171
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_24_HIGH                           0x2172
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_24_RESVD                          0x2173
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_25_LOW                            0x2174
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_25_MID                            0x2175
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_25_HIGH                           0x2176
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_25_RESVD                          0x2177
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_26_LOW                            0x2178
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_26_MID                            0x2179
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_26_HIGH                           0x217a
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_26_RESVD                          0x217b
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_27_LOW                            0x217c
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_27_MID                            0x217d
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_27_HIGH                           0x217e
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_27_RESVD                          0x217f
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_28_LOW                            0x2180
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_28_MID                            0x2181
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_28_HIGH                           0x2182
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_28_RESVD                          0x2183
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_29_LOW                            0x2184
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_29_MID                            0x2185
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_29_HIGH                           0x2186
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_29_RESVD                          0x2187
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_30_LOW                            0x2188
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_30_MID                            0x2189
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_30_HIGH                           0x218A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_30_RESVD                          0x218B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_31_LOW                            0x218C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_31_MID                            0x218D
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_31_HIGH                           0x218E
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_31_RESVD                          0x218F
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_32_LOW                            0x2190
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_32_MID                            0x2191
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_32_HIGH                           0x2192
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_32_RESVD                          0x2193
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_33_LOW                            0x2194
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_33_MID                            0x2195
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_33_HIGH                           0x2196
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_33_RESVD                          0x2197
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_34_LOW                            0x2198
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_34_MID                            0x2199
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_34_HIGH                           0x219A
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_34_RESVD                          0x219B
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_35_LOW                            0x219C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_35_MID                            0x219D
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_35_HIGH                           0x219E
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_35_RESVD                          0x219F
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_36_LOW                            0x21A0
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_36_MID                            0x21A1
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_36_HIGH                           0x21A2
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_36_RESVD                          0x21A3
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_37_LOW                            0x21A4
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_37_MID                            0x21A5
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_37_HIGH                           0x21A6
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_37_RESVD                          0x21A7
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_38_LOW                            0x21A8
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_38_MID                            0x21A9
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_38_HIGH                           0x21AA
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_38_RESVD                          0x21AB
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_39_LOW                            0x21AC
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_39_MID                            0x21AD
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_39_HIGH                           0x21AE
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_39_RESVD                          0x21AF
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_40_LOW                            0x21B0
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_40_MID                            0x21B1
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_40_HIGH                           0x21B2
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_40_RESVD                          0x21B3
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_41_LOW                            0x21B4
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_41_MID                            0x21B5
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_41_HIGH                           0x21B6
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_41_RESVD                          0x21B7
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_42_LOW                            0x21B8
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_42_MID                            0x21B9
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_42_HIGH                           0x21BA
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_42_RESVD                          0x21BB
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_43_LOW                            0x21BC
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_43_MID                            0x21BD
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_43_HIGH                           0x21BE
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_43_RESVD                          0x21BF
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_44_LOW                            0x21C0
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_44_MID                            0x21C1
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_44_HIGH                           0x21C2
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_44_RESVD                          0x21C3
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_45_LOW                            0x21C4
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_45_MID                            0x21C5
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_45_HIGH                           0x21C6
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_45_RESVD                          0x21C7
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_46_LOW                            0x21C8
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_46_MID                            0x21C9
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_46_HIGH                           0x21CA
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_46_RESVD                          0x21CB
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_47_LOW                            0x21CC
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_47_MID                            0x21CD
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_47_HIGH                           0x21CE
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_47_RESVD                          0x21CF
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_48_LOW                            0x21D0
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_48_MID                            0x21D1
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_48_HIGH                           0x21D2
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_48_RESVD                          0x21D3
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_49_LOW                            0x21D4
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_49_MID                            0x21D5
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_49_HIGH                           0x21D6
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_49_RESVD                          0x21D7
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_50_LOW                            0x21D8
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_50_MID                            0x21D9
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_50_HIGH                           0x21DA
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_50_RESVD                          0x21DB
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_51_LOW                            0x21DC
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_51_MID                            0x21DD
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_51_HIGH                           0x21DE
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_51_RESVD                          0x21DF
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_52_LOW                            0x21E0
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_52_MID                            0x21E1
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_52_HIGH                           0x21E2
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_52_RESVD                          0x21E3
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_53_LOW                            0x21E4
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_53_MID                            0x21E5
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_53_HIGH                           0x21E6
#define SUNI1x10GEXP_CNTR_MAC_ETHERNET_NUM                               51

#define SUNI1x10GEXP_REG_IFLX_GLOBAL_CONFIG                              0x2200
#define SUNI1x10GEXP_REG_IFLX_CHANNEL_PROVISION                          0x2201
#define SUNI1x10GEXP_REG_IFLX_FIFO_OVERFLOW_ENABLE                       0x2209
#define SUNI1x10GEXP_REG_IFLX_FIFO_OVERFLOW_INTERRUPT                    0x220A
#define SUNI1x10GEXP_REG_IFLX_INDIR_CHANNEL_ADDRESS                      0x220D
#define SUNI1x10GEXP_REG_IFLX_INDIR_LOGICAL_FIFO_LOW_LIMIT_PROVISION     0x220E
#define SUNI1x10GEXP_REG_IFLX_INDIR_LOGICAL_FIFO_HIGH_LIMIT              0x220F
#define SUNI1x10GEXP_REG_IFLX_INDIR_FULL_ALMOST_FULL_STATUS_LIMIT        0x2210
#define SUNI1x10GEXP_REG_IFLX_INDIR_EMPTY_ALMOST_EMPTY_STATUS_LIMIT      0x2211

#define SUNI1x10GEXP_REG_PL4MOS_CONFIG                                   0x2240
#define SUNI1x10GEXP_REG_PL4MOS_MASK                                     0x2241
#define SUNI1x10GEXP_REG_PL4MOS_FAIRNESS_MASKING                         0x2242
#define SUNI1x10GEXP_REG_PL4MOS_MAXBURST1                                0x2243
#define SUNI1x10GEXP_REG_PL4MOS_MAXBURST2                                0x2244
#define SUNI1x10GEXP_REG_PL4MOS_TRANSFER_SIZE                            0x2245

#define SUNI1x10GEXP_REG_PL4ODP_CONFIG                                   0x2280
#define SUNI1x10GEXP_REG_PL4ODP_INTERRUPT_MASK                           0x2282
#define SUNI1x10GEXP_REG_PL4ODP_INTERRUPT                                0x2283
#define SUNI1x10GEXP_REG_PL4ODP_CONFIG_MAX_T                             0x2284

#define SUNI1x10GEXP_REG_PL4IO_LOCK_DETECT_STATUS                        0x2300
#define SUNI1x10GEXP_REG_PL4IO_LOCK_DETECT_CHANGE                        0x2301
#define SUNI1x10GEXP_REG_PL4IO_LOCK_DETECT_MASK                          0x2302
#define SUNI1x10GEXP_REG_PL4IO_LOCK_DETECT_LIMITS                        0x2303
#define SUNI1x10GEXP_REG_PL4IO_CALENDAR_REPETITIONS                      0x2304
#define SUNI1x10GEXP_REG_PL4IO_CONFIG                                    0x2305

#define SUNI1x10GEXP_REG_TXXG_CONFIG_1                                   0x3040
#define SUNI1x10GEXP_REG_TXXG_CONFIG_2                                   0x3041
#define SUNI1x10GEXP_REG_TXXG_CONFIG_3                                   0x3042
#define SUNI1x10GEXP_REG_TXXG_INTERRUPT                                  0x3043
#define SUNI1x10GEXP_REG_TXXG_STATUS                                     0x3044
#define SUNI1x10GEXP_REG_TXXG_MAX_FRAME_SIZE                             0x3045
#define SUNI1x10GEXP_REG_TXXG_MIN_FRAME_SIZE                             0x3046
#define SUNI1x10GEXP_REG_TXXG_SA_15_0                                    0x3047
#define SUNI1x10GEXP_REG_TXXG_SA_31_16                                   0x3048
#define SUNI1x10GEXP_REG_TXXG_SA_47_32                                   0x3049
#define SUNI1x10GEXP_REG_TXXG_PAUSE_TIMER                                0x304D
#define SUNI1x10GEXP_REG_TXXG_PAUSE_TIMER_INTERVAL                       0x304E
#define SUNI1x10GEXP_REG_TXXG_FILTER_ERROR_COUNTER                       0x3051
#define SUNI1x10GEXP_REG_TXXG_PAUSE_QUANTUM_CONFIG                       0x3052

#define SUNI1x10GEXP_REG_XTEF_CTRL                                       0x3080
#define SUNI1x10GEXP_REG_XTEF_INTERRUPT_STATUS                           0x3084
#define SUNI1x10GEXP_REG_XTEF_INTERRUPT_ENABLE                           0x3085
#define SUNI1x10GEXP_REG_XTEF_VISIBILITY                                 0x3086

#define SUNI1x10GEXP_REG_TXOAM_OAM_CONFIG                                0x30C0
#define SUNI1x10GEXP_REG_TXOAM_MINI_RATE_CONFIG                          0x30C1
#define SUNI1x10GEXP_REG_TXOAM_MINI_GAP_FIFO_CONFIG                      0x30C2
#define SUNI1x10GEXP_REG_TXOAM_P1P2_STATIC_VALUES                        0x30C3
#define SUNI1x10GEXP_REG_TXOAM_P3P4_STATIC_VALUES                        0x30C4
#define SUNI1x10GEXP_REG_TXOAM_P5P6_STATIC_VALUES                        0x30C5
#define SUNI1x10GEXP_REG_TXOAM_INTERRUPT_ENABLE                          0x30C6
#define SUNI1x10GEXP_REG_TXOAM_INTERRUPT_STATUS                          0x30C7
#define SUNI1x10GEXP_REG_TXOAM_INSERT_COUNT_LSB                          0x30C8
#define SUNI1x10GEXP_REG_TXOAM_INSERT_COUNT_MSB                          0x30C9
#define SUNI1x10GEXP_REG_TXOAM_OAM_MINI_COUNT_LSB                        0x30CA
#define SUNI1x10GEXP_REG_TXOAM_OAM_MINI_COUNT_MSB                        0x30CB
#define SUNI1x10GEXP_REG_TXOAM_P1P2_MINI_MASK                            0x30CC
#define SUNI1x10GEXP_REG_TXOAM_P3P4_MINI_MASK                            0x30CD
#define SUNI1x10GEXP_REG_TXOAM_P5P6_MINI_MASK                            0x30CE
#define SUNI1x10GEXP_REG_TXOAM_COSET                                     0x30CF
#define SUNI1x10GEXP_REG_TXOAM_EMPTY_FIFO_INS_OP_CNT_LSB                 0x30D0
#define SUNI1x10GEXP_REG_TXOAM_EMPTY_FIFO_INS_OP_CNT_MSB                 0x30D1
#define SUNI1x10GEXP_REG_TXOAM_STATIC_VALUE_MINI_COUNT_LSB               0x30D2
#define SUNI1x10GEXP_REG_TXOAM_STATIC_VALUE_MINI_COUNT_MSB               0x30D3


#define SUNI1x10GEXP_REG_EFLX_GLOBAL_CONFIG                              0x3200
#define SUNI1x10GEXP_REG_EFLX_ERCU_GLOBAL_STATUS                         0x3201
#define SUNI1x10GEXP_REG_EFLX_INDIR_CHANNEL_ADDRESS                      0x3202
#define SUNI1x10GEXP_REG_EFLX_INDIR_FIFO_LOW_LIMIT                       0x3203
#define SUNI1x10GEXP_REG_EFLX_INDIR_FIFO_HIGH_LIMIT                      0x3204
#define SUNI1x10GEXP_REG_EFLX_INDIR_FULL_ALMOST_FULL_STATUS_AND_LIMIT    0x3205
#define SUNI1x10GEXP_REG_EFLX_INDIR_EMPTY_ALMOST_EMPTY_STATUS_AND_LIMIT  0x3206
#define SUNI1x10GEXP_REG_EFLX_INDIR_FIFO_CUT_THROUGH_THRESHOLD           0x3207
#define SUNI1x10GEXP_REG_EFLX_FIFO_OVERFLOW_ERROR_ENABLE                 0x320C
#define SUNI1x10GEXP_REG_EFLX_FIFO_OVERFLOW_ERROR_INDICATION             0x320D
#define SUNI1x10GEXP_REG_EFLX_CHANNEL_PROVISION                          0x3210

#define SUNI1x10GEXP_REG_PL4IDU_CONFIG                                   0x3280
#define SUNI1x10GEXP_REG_PL4IDU_INTERRUPT_MASK                           0x3282
#define SUNI1x10GEXP_REG_PL4IDU_INTERRUPT                                0x3283


/*----------------------------------------*/
#define SUNI1x10GEXP_REG_MAX_OFFSET                                      0x3480

/******************************************************************************/
/*                 -- End register offset definitions --                      */
/******************************************************************************/

/******************************************************************************/
/** SUNI-1x10GE-XP REGISTER BIT MASKS                                        **/
/******************************************************************************/

#define SUNI1x10GEXP_BITMSK_BITS_1   0x00001
#define SUNI1x10GEXP_BITMSK_BITS_2   0x00003
#define SUNI1x10GEXP_BITMSK_BITS_3   0x00007
#define SUNI1x10GEXP_BITMSK_BITS_4   0x0000f
#define SUNI1x10GEXP_BITMSK_BITS_5   0x0001f
#define SUNI1x10GEXP_BITMSK_BITS_6   0x0003f
#define SUNI1x10GEXP_BITMSK_BITS_7   0x0007f
#define SUNI1x10GEXP_BITMSK_BITS_8   0x000ff
#define SUNI1x10GEXP_BITMSK_BITS_9   0x001ff
#define SUNI1x10GEXP_BITMSK_BITS_10  0x003ff
#define SUNI1x10GEXP_BITMSK_BITS_11  0x007ff
#define SUNI1x10GEXP_BITMSK_BITS_12  0x00fff
#define SUNI1x10GEXP_BITMSK_BITS_13  0x01fff
#define SUNI1x10GEXP_BITMSK_BITS_14  0x03fff
#define SUNI1x10GEXP_BITMSK_BITS_15  0x07fff
#define SUNI1x10GEXP_BITMSK_BITS_16  0x0ffff

#define mSUNI1x10GEXP_CLR_MSBITS_1(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_15)
#define mSUNI1x10GEXP_CLR_MSBITS_2(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_14)
#define mSUNI1x10GEXP_CLR_MSBITS_3(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_13)
#define mSUNI1x10GEXP_CLR_MSBITS_4(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_12)
#define mSUNI1x10GEXP_CLR_MSBITS_5(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_11)
#define mSUNI1x10GEXP_CLR_MSBITS_6(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_10)
#define mSUNI1x10GEXP_CLR_MSBITS_7(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_9)
#define mSUNI1x10GEXP_CLR_MSBITS_8(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_8)
#define mSUNI1x10GEXP_CLR_MSBITS_9(v)  ((v) & SUNI1x10GEXP_BITMSK_BITS_7)
#define mSUNI1x10GEXP_CLR_MSBITS_10(v) ((v) & SUNI1x10GEXP_BITMSK_BITS_6)
#define mSUNI1x10GEXP_CLR_MSBITS_11(v) ((v) & SUNI1x10GEXP_BITMSK_BITS_5)
#define mSUNI1x10GEXP_CLR_MSBITS_12(v) ((v) & SUNI1x10GEXP_BITMSK_BITS_4)
#define mSUNI1x10GEXP_CLR_MSBITS_13(v) ((v) & SUNI1x10GEXP_BITMSK_BITS_3)
#define mSUNI1x10GEXP_CLR_MSBITS_14(v) ((v) & SUNI1x10GEXP_BITMSK_BITS_2)
#define mSUNI1x10GEXP_CLR_MSBITS_15(v) ((v) & SUNI1x10GEXP_BITMSK_BITS_1)

#define mSUNI1x10GEXP_GET_BIT(val, bitMsk) (((val)&(bitMsk)) ? 1:0)



/*----------------------------------------------------------------------------
 * Register 0x0001: S/UNI-1x10GE-XP Product Revision
 *    Bit 3-0  REVISION
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_REVISION  0x000F

/*----------------------------------------------------------------------------
 * Register 0x0002: S/UNI-1x10GE-XP Configuration and Reset Control
 *    Bit 2  XAUI_ARESETB
 *    Bit 1  PL4_ARESETB
 *    Bit 0  DRESETB
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_XAUI_ARESET  0x0004
#define SUNI1x10GEXP_BITMSK_PL4_ARESET   0x0002
#define SUNI1x10GEXP_BITMSK_DRESETB      0x0001

/*----------------------------------------------------------------------------
 * Register 0x0003: S/UNI-1x10GE-XP Loop Back and Miscellaneous Control
 *    Bit 11  PL4IO_OUTCLKSEL
 *    Bit 9   SYSPCSLB
 *    Bit 8   LINEPCSLB
 *    Bit 7   MSTAT_BYPASS
 *    Bit 6   RXXG_BYPASS
 *    Bit 5   TXXG_BYPASS
 *    Bit 4   SOP_PAD_EN
 *    Bit 1   LOS_INV
 *    Bit 0   OVERRIDE_LOS
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IO_OUTCLKSEL  0x0800
#define SUNI1x10GEXP_BITMSK_SYSPCSLB         0x0200
#define SUNI1x10GEXP_BITMSK_LINEPCSLB        0x0100
#define SUNI1x10GEXP_BITMSK_MSTAT_BYPASS     0x0080
#define SUNI1x10GEXP_BITMSK_RXXG_BYPASS      0x0040
#define SUNI1x10GEXP_BITMSK_TXXG_BYPASS      0x0020
#define SUNI1x10GEXP_BITMSK_SOP_PAD_EN       0x0010
#define SUNI1x10GEXP_BITMSK_LOS_INV          0x0002
#define SUNI1x10GEXP_BITMSK_OVERRIDE_LOS     0x0001

/*----------------------------------------------------------------------------
 * Register 0x0004: S/UNI-1x10GE-XP Device Status
 *    Bit 9 TOP_SXRA_EXPIRED
 *    Bit 8 TOP_MDIO_BUSY
 *    Bit 7 TOP_DTRB
 *    Bit 6 TOP_EXPIRED
 *    Bit 5 TOP_PAUSED
 *    Bit 4 TOP_PL4_ID_DOOL
 *    Bit 3 TOP_PL4_IS_DOOL
 *    Bit 2 TOP_PL4_ID_ROOL
 *    Bit 1 TOP_PL4_IS_ROOL
 *    Bit 0 TOP_PL4_OUT_ROOL
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TOP_SXRA_EXPIRED  0x0200
#define SUNI1x10GEXP_BITMSK_TOP_MDIO_BUSY     0x0100
#define SUNI1x10GEXP_BITMSK_TOP_DTRB          0x0080
#define SUNI1x10GEXP_BITMSK_TOP_EXPIRED       0x0040
#define SUNI1x10GEXP_BITMSK_TOP_PAUSED        0x0020
#define SUNI1x10GEXP_BITMSK_TOP_PL4_ID_DOOL   0x0010
#define SUNI1x10GEXP_BITMSK_TOP_PL4_IS_DOOL   0x0008
#define SUNI1x10GEXP_BITMSK_TOP_PL4_ID_ROOL   0x0004
#define SUNI1x10GEXP_BITMSK_TOP_PL4_IS_ROOL   0x0002
#define SUNI1x10GEXP_BITMSK_TOP_PL4_OUT_ROOL  0x0001

/*----------------------------------------------------------------------------
 * Register 0x0005: Global Performance Update and Clock Monitors
 *    Bit 15 TIP
 *    Bit 8  XAUI_REF_CLKA
 *    Bit 7  RXLANE3CLKA
 *    Bit 6  RXLANE2CLKA
 *    Bit 5  RXLANE1CLKA
 *    Bit 4  RXLANE0CLKA
 *    Bit 3  CSUCLKA
 *    Bit 2  TDCLKA
 *    Bit 1  RSCLKA
 *    Bit 0  RDCLKA
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TIP            0x8000
#define SUNI1x10GEXP_BITMSK_XAUI_REF_CLKA  0x0100
#define SUNI1x10GEXP_BITMSK_RXLANE3CLKA    0x0080
#define SUNI1x10GEXP_BITMSK_RXLANE2CLKA    0x0040
#define SUNI1x10GEXP_BITMSK_RXLANE1CLKA    0x0020
#define SUNI1x10GEXP_BITMSK_RXLANE0CLKA    0x0010
#define SUNI1x10GEXP_BITMSK_CSUCLKA        0x0008
#define SUNI1x10GEXP_BITMSK_TDCLKA         0x0004
#define SUNI1x10GEXP_BITMSK_RSCLKA         0x0002
#define SUNI1x10GEXP_BITMSK_RDCLKA         0x0001

/*----------------------------------------------------------------------------
 * Register 0x0006: MDIO Command
 *    Bit 4 MDIO_RDINC
 *    Bit 3 MDIO_RSTAT
 *    Bit 2 MDIO_LCTLD
 *    Bit 1 MDIO_LCTLA
 *    Bit 0 MDIO_SPRE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_MDIO_RDINC  0x0010
#define SUNI1x10GEXP_BITMSK_MDIO_RSTAT  0x0008
#define SUNI1x10GEXP_BITMSK_MDIO_LCTLD  0x0004
#define SUNI1x10GEXP_BITMSK_MDIO_LCTLA  0x0002
#define SUNI1x10GEXP_BITMSK_MDIO_SPRE   0x0001

/*----------------------------------------------------------------------------
 * Register 0x0007: MDIO Interrupt Enable
 *    Bit 0 MDIO_BUSY_EN
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_MDIO_BUSY_EN  0x0001

/*----------------------------------------------------------------------------
 * Register 0x0008: MDIO Interrupt Status
 *    Bit 0 MDIO_BUSYI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_MDIO_BUSYI  0x0001

/*----------------------------------------------------------------------------
 * Register 0x0009: MMD PHY Address
 *    Bit 12-8 MDIO_DEVADR
 *    Bit 4-0 MDIO_PRTADR
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_MDIO_DEVADR  0x1F00
#define SUNI1x10GEXP_BITOFF_MDIO_DEVADR  8
#define SUNI1x10GEXP_BITMSK_MDIO_PRTADR  0x001F
#define SUNI1x10GEXP_BITOFF_MDIO_PRTADR  0

/*----------------------------------------------------------------------------
 * Register 0x000C: OAM Interface Control
 *    Bit 6 MDO_OD_ENB
 *    Bit 5 MDI_INV
 *    Bit 4 MDI_SEL
 *    Bit 3 RXOAMEN
 *    Bit 2 RXOAMCLKEN
 *    Bit 1 TXOAMEN
 *    Bit 0 TXOAMCLKEN
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_MDO_OD_ENB  0x0040
#define SUNI1x10GEXP_BITMSK_MDI_INV     0x0020
#define SUNI1x10GEXP_BITMSK_MDI_SEL     0x0010
#define SUNI1x10GEXP_BITMSK_RXOAMEN     0x0008
#define SUNI1x10GEXP_BITMSK_RXOAMCLKEN  0x0004
#define SUNI1x10GEXP_BITMSK_TXOAMEN     0x0002
#define SUNI1x10GEXP_BITMSK_TXOAMCLKEN  0x0001

/*----------------------------------------------------------------------------
 * Register 0x000D: S/UNI-1x10GE-XP Master Interrupt Status
 *    Bit 15 TOP_PL4IO_INT
 *    Bit 14 TOP_IRAM_INT
 *    Bit 13 TOP_ERAM_INT
 *    Bit 12 TOP_XAUI_INT
 *    Bit 11 TOP_MSTAT_INT
 *    Bit 10 TOP_RXXG_INT
 *    Bit 9 TOP_TXXG_INT
 *    Bit 8 TOP_XRF_INT
 *    Bit 7 TOP_XTEF_INT
 *    Bit 6 TOP_MDIO_BUSY_INT
 *    Bit 5 TOP_RXOAM_INT
 *    Bit 4 TOP_TXOAM_INT
 *    Bit 3 TOP_IFLX_INT
 *    Bit 2 TOP_EFLX_INT
 *    Bit 1 TOP_PL4ODP_INT
 *    Bit 0 TOP_PL4IDU_INT
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TOP_PL4IO_INT      0x8000
#define SUNI1x10GEXP_BITMSK_TOP_IRAM_INT       0x4000
#define SUNI1x10GEXP_BITMSK_TOP_ERAM_INT       0x2000
#define SUNI1x10GEXP_BITMSK_TOP_XAUI_INT       0x1000
#define SUNI1x10GEXP_BITMSK_TOP_MSTAT_INT      0x0800
#define SUNI1x10GEXP_BITMSK_TOP_RXXG_INT       0x0400
#define SUNI1x10GEXP_BITMSK_TOP_TXXG_INT       0x0200
#define SUNI1x10GEXP_BITMSK_TOP_XRF_INT        0x0100
#define SUNI1x10GEXP_BITMSK_TOP_XTEF_INT       0x0080
#define SUNI1x10GEXP_BITMSK_TOP_MDIO_BUSY_INT  0x0040
#define SUNI1x10GEXP_BITMSK_TOP_RXOAM_INT      0x0020
#define SUNI1x10GEXP_BITMSK_TOP_TXOAM_INT      0x0010
#define SUNI1x10GEXP_BITMSK_TOP_IFLX_INT       0x0008
#define SUNI1x10GEXP_BITMSK_TOP_EFLX_INT       0x0004
#define SUNI1x10GEXP_BITMSK_TOP_PL4ODP_INT     0x0002
#define SUNI1x10GEXP_BITMSK_TOP_PL4IDU_INT     0x0001

/*----------------------------------------------------------------------------
 * Register 0x000E:PM3393 Global interrupt enable
 *    Bit 15 TOP_INTE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TOP_INTE  0x8000

/*----------------------------------------------------------------------------
 * Register 0x0010: XTEF Miscellaneous Control
 *    Bit 7 RF_VAL
 *    Bit 6 RF_OVERRIDE
 *    Bit 5 LF_VAL
 *    Bit 4 LF_OVERRIDE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RF_VAL             0x0080
#define SUNI1x10GEXP_BITMSK_RF_OVERRIDE        0x0040
#define SUNI1x10GEXP_BITMSK_LF_VAL             0x0020
#define SUNI1x10GEXP_BITMSK_LF_OVERRIDE        0x0010
#define SUNI1x10GEXP_BITMSK_LFRF_OVERRIDE_VAL  0x00F0

/*----------------------------------------------------------------------------
 * Register 0x0011: XRF Miscellaneous Control
 *    Bit 6-4 EN_IDLE_REP
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EN_IDLE_REP  0x0070

/*----------------------------------------------------------------------------
 * Register 0x0100: SERDES 3125 Configuration Register 1
 *    Bit 10 RXEQB_3
 *    Bit 8  RXEQB_2
 *    Bit 6  RXEQB_1
 *    Bit 4  RXEQB_0
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXEQB    0x0FF0
#define SUNI1x10GEXP_BITOFF_RXEQB_3  10
#define SUNI1x10GEXP_BITOFF_RXEQB_2  8
#define SUNI1x10GEXP_BITOFF_RXEQB_1  6
#define SUNI1x10GEXP_BITOFF_RXEQB_0  4

/*----------------------------------------------------------------------------
 * Register 0x0101: SERDES 3125 Configuration Register 2
 *    Bit 12 YSEL
 *    Bit  7 PRE_EMPH_3
 *    Bit  6 PRE_EMPH_2
 *    Bit  5 PRE_EMPH_1
 *    Bit  4 PRE_EMPH_0
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_YSEL        0x1000
#define SUNI1x10GEXP_BITMSK_PRE_EMPH    0x00F0
#define SUNI1x10GEXP_BITMSK_PRE_EMPH_3  0x0080
#define SUNI1x10GEXP_BITMSK_PRE_EMPH_2  0x0040
#define SUNI1x10GEXP_BITMSK_PRE_EMPH_1  0x0020
#define SUNI1x10GEXP_BITMSK_PRE_EMPH_0  0x0010

/*----------------------------------------------------------------------------
 * Register 0x0102: SERDES 3125 Interrupt Enable Register
 *    Bit 3 LASIE
 *    Bit 2 SPLL_RAE
 *    Bit 1 MPLL_RAE
 *    Bit 0 PLL_LOCKE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_LASIE      0x0008
#define SUNI1x10GEXP_BITMSK_SPLL_RAE   0x0004
#define SUNI1x10GEXP_BITMSK_MPLL_RAE   0x0002
#define SUNI1x10GEXP_BITMSK_PLL_LOCKE  0x0001

/*----------------------------------------------------------------------------
 * Register 0x0103: SERDES 3125 Interrupt Visibility Register
 *    Bit 3 LASIV
 *    Bit 2 SPLL_RAV
 *    Bit 1 MPLL_RAV
 *    Bit 0 PLL_LOCKV
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_LASIV      0x0008
#define SUNI1x10GEXP_BITMSK_SPLL_RAV   0x0004
#define SUNI1x10GEXP_BITMSK_MPLL_RAV   0x0002
#define SUNI1x10GEXP_BITMSK_PLL_LOCKV  0x0001

/*----------------------------------------------------------------------------
 * Register 0x0104: SERDES 3125 Interrupt Status Register
 *    Bit 3 LASII
 *    Bit 2 SPLL_RAI
 *    Bit 1 MPLL_RAI
 *    Bit 0 PLL_LOCKI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_LASII      0x0008
#define SUNI1x10GEXP_BITMSK_SPLL_RAI   0x0004
#define SUNI1x10GEXP_BITMSK_MPLL_RAI   0x0002
#define SUNI1x10GEXP_BITMSK_PLL_LOCKI  0x0001

/*----------------------------------------------------------------------------
 * Register 0x0107: SERDES 3125 Test Configuration
 *    Bit 12 DUALTX
 *    Bit 10 HC_1
 *    Bit  9 HC_0
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_DUALTX  0x1000
#define SUNI1x10GEXP_BITMSK_HC      0x0600
#define SUNI1x10GEXP_BITOFF_HC_0    9

/*----------------------------------------------------------------------------
 * Register 0x2040: RXXG Configuration 1
 *    Bit 15  RXXG_RXEN
 *    Bit 14  RXXG_ROCF
 *    Bit 13  RXXG_PAD_STRIP
 *    Bit 10  RXXG_PUREP
 *    Bit 9   RXXG_LONGP
 *    Bit 8   RXXG_PARF
 *    Bit 7   RXXG_FLCHK
 *    Bit 5   RXXG_PASS_CTRL
 *    Bit 3   RXXG_CRC_STRIP
 *    Bit 2-0 RXXG_MIFG
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_RXEN       0x8000
#define SUNI1x10GEXP_BITMSK_RXXG_ROCF       0x4000
#define SUNI1x10GEXP_BITMSK_RXXG_PAD_STRIP  0x2000
#define SUNI1x10GEXP_BITMSK_RXXG_PUREP      0x0400
#define SUNI1x10GEXP_BITMSK_RXXG_LONGP      0x0200
#define SUNI1x10GEXP_BITMSK_RXXG_PARF       0x0100
#define SUNI1x10GEXP_BITMSK_RXXG_FLCHK      0x0080
#define SUNI1x10GEXP_BITMSK_RXXG_PASS_CTRL  0x0020
#define SUNI1x10GEXP_BITMSK_RXXG_CRC_STRIP  0x0008

/*----------------------------------------------------------------------------
 * Register 0x02041: RXXG Configuration 2
 *    Bit 7-0 RXXG_HDRSIZE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_HDRSIZE  0x00FF

/*----------------------------------------------------------------------------
 * Register 0x2042: RXXG Configuration 3
 *    Bit 15 RXXG_MIN_LERRE
 *    Bit 14 RXXG_MAX_LERRE
 *    Bit 12 RXXG_LINE_ERRE
 *    Bit 10 RXXG_RX_OVRE
 *    Bit 9  RXXG_ADR_FILTERE
 *    Bit 8  RXXG_ERR_FILTERE
 *    Bit 5  RXXG_PRMB_ERRE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_MIN_LERRE     0x8000
#define SUNI1x10GEXP_BITMSK_RXXG_MAX_LERRE     0x4000
#define SUNI1x10GEXP_BITMSK_RXXG_LINE_ERRE     0x1000
#define SUNI1x10GEXP_BITMSK_RXXG_RX_OVRE       0x0400
#define SUNI1x10GEXP_BITMSK_RXXG_ADR_FILTERE   0x0200
#define SUNI1x10GEXP_BITMSK_RXXG_ERR_FILTERRE  0x0100
#define SUNI1x10GEXP_BITMSK_RXXG_PRMB_ERRE     0x0020

/*----------------------------------------------------------------------------
 * Register 0x2043: RXXG Interrupt
 *    Bit 15 RXXG_MIN_LERRI
 *    Bit 14 RXXG_MAX_LERRI
 *    Bit 12 RXXG_LINE_ERRI
 *    Bit 10 RXXG_RX_OVRI
 *    Bit 9  RXXG_ADR_FILTERI
 *    Bit 8  RXXG_ERR_FILTERI
 *    Bit 5  RXXG_PRMB_ERRE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_MIN_LERRI    0x8000
#define SUNI1x10GEXP_BITMSK_RXXG_MAX_LERRI    0x4000
#define SUNI1x10GEXP_BITMSK_RXXG_LINE_ERRI    0x1000
#define SUNI1x10GEXP_BITMSK_RXXG_RX_OVRI      0x0400
#define SUNI1x10GEXP_BITMSK_RXXG_ADR_FILTERI  0x0200
#define SUNI1x10GEXP_BITMSK_RXXG_ERR_FILTERI  0x0100
#define SUNI1x10GEXP_BITMSK_RXXG_PRMB_ERRE    0x0020

/*----------------------------------------------------------------------------
 * Register 0x2049: RXXG Receive FIFO Threshold
 *    Bit 2-0 RXXG_CUT_THRU
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_CUT_THRU  0x0007
#define SUNI1x10GEXP_BITOFF_RXXG_CUT_THRU  0

/*----------------------------------------------------------------------------
 * Register 0x2062H - 0x2069: RXXG Exact Match VID
 *    Bit 11-0 RXXG_VID_MATCH
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_VID_MATCH  0x0FFF
#define SUNI1x10GEXP_BITOFF_RXXG_VID_MATCH  0

/*----------------------------------------------------------------------------
 * Register 0x206EH - 0x206F: RXXG Address Filter Control
 *    Bit 3 RXXG_FORWARD_ENABLE
 *    Bit 2 RXXG_VLAN_ENABLE
 *    Bit 1 RXXG_SRC_ADDR
 *    Bit 0 RXXG_MATCH_ENABLE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_FORWARD_ENABLE  0x0008
#define SUNI1x10GEXP_BITMSK_RXXG_VLAN_ENABLE     0x0004
#define SUNI1x10GEXP_BITMSK_RXXG_SRC_ADDR        0x0002
#define SUNI1x10GEXP_BITMSK_RXXG_MATCH_ENABLE    0x0001

/*----------------------------------------------------------------------------
 * Register 0x2070: RXXG Address Filter Control 2
 *    Bit 1 RXXG_PMODE
 *    Bit 0 RXXG_MHASH_EN
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_PMODE     0x0002
#define SUNI1x10GEXP_BITMSK_RXXG_MHASH_EN  0x0001

/*----------------------------------------------------------------------------
 * Register 0x2081: XRF Control Register 2
 *    Bit 6   EN_PKT_GEN
 *    Bit 4-2 PATT
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EN_PKT_GEN  0x0040
#define SUNI1x10GEXP_BITMSK_PATT        0x001C
#define SUNI1x10GEXP_BITOFF_PATT        2

/*----------------------------------------------------------------------------
 * Register 0x2088: XRF Interrupt Enable
 *    Bit 12-9 LANE_HICERE
 *    Bit 8-5  HS_SD_LANEE
 *    Bit 4    ALIGN_STATUS_ERRE
 *    Bit 3-0  LANE_SYNC_STAT_ERRE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_LANE_HICERE          0x1E00
#define SUNI1x10GEXP_BITOFF_LANE_HICERE          9
#define SUNI1x10GEXP_BITMSK_HS_SD_LANEE          0x01E0
#define SUNI1x10GEXP_BITOFF_HS_SD_LANEE          5
#define SUNI1x10GEXP_BITMSK_ALIGN_STATUS_ERRE    0x0010
#define SUNI1x10GEXP_BITMSK_LANE_SYNC_STAT_ERRE  0x000F
#define SUNI1x10GEXP_BITOFF_LANE_SYNC_STAT_ERRE  0

/*----------------------------------------------------------------------------
 * Register 0x2089: XRF Interrupt Status
 *    Bit 12-9 LANE_HICERI
 *    Bit 8-5  HS_SD_LANEI
 *    Bit 4    ALIGN_STATUS_ERRI
 *    Bit 3-0  LANE_SYNC_STAT_ERRI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_LANE_HICERI          0x1E00
#define SUNI1x10GEXP_BITOFF_LANE_HICERI          9
#define SUNI1x10GEXP_BITMSK_HS_SD_LANEI          0x01E0
#define SUNI1x10GEXP_BITOFF_HS_SD_LANEI          5
#define SUNI1x10GEXP_BITMSK_ALIGN_STATUS_ERRI    0x0010
#define SUNI1x10GEXP_BITMSK_LANE_SYNC_STAT_ERRI  0x000F
#define SUNI1x10GEXP_BITOFF_LANE_SYNC_STAT_ERRI  0

/*----------------------------------------------------------------------------
 * Register 0x208A: XRF Error Status
 *    Bit 8-5  HS_SD_LANE
 *    Bit 4    ALIGN_STATUS_ERR
 *    Bit 3-0  LANE_SYNC_STAT_ERR
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_HS_SD_LANE3          0x0100
#define SUNI1x10GEXP_BITMSK_HS_SD_LANE2          0x0080
#define SUNI1x10GEXP_BITMSK_HS_SD_LANE1          0x0040
#define SUNI1x10GEXP_BITMSK_HS_SD_LANE0          0x0020
#define SUNI1x10GEXP_BITMSK_ALIGN_STATUS_ERR     0x0010
#define SUNI1x10GEXP_BITMSK_LANE3_SYNC_STAT_ERR  0x0008
#define SUNI1x10GEXP_BITMSK_LANE2_SYNC_STAT_ERR  0x0004
#define SUNI1x10GEXP_BITMSK_LANE1_SYNC_STAT_ERR  0x0002
#define SUNI1x10GEXP_BITMSK_LANE0_SYNC_STAT_ERR  0x0001

/*----------------------------------------------------------------------------
 * Register 0x208B: XRF Diagnostic Interrupt Enable
 *    Bit 7-4 LANE_OVERRUNE
 *    Bit 3-0 LANE_UNDERRUNE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_LANE_OVERRUNE   0x00F0
#define SUNI1x10GEXP_BITOFF_LANE_OVERRUNE   4
#define SUNI1x10GEXP_BITMSK_LANE_UNDERRUNE  0x000F
#define SUNI1x10GEXP_BITOFF_LANE_UNDERRUNE  0

/*----------------------------------------------------------------------------
 * Register 0x208C: XRF Diagnostic Interrupt Status
 *    Bit 7-4 LANE_OVERRUNI
 *    Bit 3-0 LANE_UNDERRUNI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_LANE_OVERRUNI   0x00F0
#define SUNI1x10GEXP_BITOFF_LANE_OVERRUNI   4
#define SUNI1x10GEXP_BITMSK_LANE_UNDERRUNI  0x000F
#define SUNI1x10GEXP_BITOFF_LANE_UNDERRUNI  0

/*----------------------------------------------------------------------------
 * Register 0x20C0: RXOAM Configuration
 *    Bit 15    RXOAM_BUSY
 *    Bit 14-12 RXOAM_F2_SEL
 *    Bit 10-8  RXOAM_F1_SEL
 *    Bit 7-6   RXOAM_FILTER_CTRL
 *    Bit 5-0   RXOAM_PX_EN
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXOAM_BUSY         0x8000
#define SUNI1x10GEXP_BITMSK_RXOAM_F2_SEL       0x7000
#define SUNI1x10GEXP_BITOFF_RXOAM_F2_SEL       12
#define SUNI1x10GEXP_BITMSK_RXOAM_F1_SEL       0x0700
#define SUNI1x10GEXP_BITOFF_RXOAM_F1_SEL       8
#define SUNI1x10GEXP_BITMSK_RXOAM_FILTER_CTRL  0x00C0
#define SUNI1x10GEXP_BITOFF_RXOAM_FILTER_CTRL  6
#define SUNI1x10GEXP_BITMSK_RXOAM_PX_EN        0x003F
#define SUNI1x10GEXP_BITOFF_RXOAM_PX_EN        0

/*----------------------------------------------------------------------------
 * Register 0x20C1,0x20C2: RXOAM Filter Configuration
 *    Bit 15-8 RXOAM_FX_MASK
 *    Bit 7-0  RXOAM_FX_VAL
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXOAM_FX_MASK  0xFF00
#define SUNI1x10GEXP_BITOFF_RXOAM_FX_MASK  8
#define SUNI1x10GEXP_BITMSK_RXOAM_FX_VAL   0x00FF
#define SUNI1x10GEXP_BITOFF_RXOAM_FX_VAl   0

/*----------------------------------------------------------------------------
 * Register 0x20C3: RXOAM Configuration Register 2
 *    Bit 13    RXOAM_REC_BYTE_VAL
 *    Bit 11-10 RXOAM_BYPASS_MODE
 *    Bit 5-0   RXOAM_PX_CLEAR
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXOAM_REC_BYTE_VAL  0x2000
#define SUNI1x10GEXP_BITMSK_RXOAM_BYPASS_MODE   0x0C00
#define SUNI1x10GEXP_BITOFF_RXOAM_BYPASS_MODE   10
#define SUNI1x10GEXP_BITMSK_RXOAM_PX_CLEAR      0x003F
#define SUNI1x10GEXP_BITOFF_RXOAM_PX_CLEAR      0

/*----------------------------------------------------------------------------
 * Register 0x20C4: RXOAM HEC Configuration
 *    Bit 15-8 RXOAM_COSET
 *    Bit 2    RXOAM_HEC_ERR_PKT
 *    Bit 0    RXOAM_HEC_EN
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXOAM_COSET        0xFF00
#define SUNI1x10GEXP_BITOFF_RXOAM_COSET        8
#define SUNI1x10GEXP_BITMSK_RXOAM_HEC_ERR_PKT  0x0004
#define SUNI1x10GEXP_BITMSK_RXOAM_HEC_EN       0x0001

/*----------------------------------------------------------------------------
 * Register 0x20C7: RXOAM Interrupt Enable
 *    Bit 10 RXOAM_FILTER_THRSHE
 *    Bit 9  RXOAM_OAM_ERRE
 *    Bit 8  RXOAM_HECE_THRSHE
 *    Bit 7  RXOAM_SOPE
 *    Bit 6  RXOAM_RFE
 *    Bit 5  RXOAM_LFE
 *    Bit 4  RXOAM_DV_ERRE
 *    Bit 3  RXOAM_DATA_INVALIDE
 *    Bit 2  RXOAM_FILTER_DROPE
 *    Bit 1  RXOAM_HECE
 *    Bit 0  RXOAM_OFLE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXOAM_FILTER_THRSHE  0x0400
#define SUNI1x10GEXP_BITMSK_RXOAM_OAM_ERRE       0x0200
#define SUNI1x10GEXP_BITMSK_RXOAM_HECE_THRSHE    0x0100
#define SUNI1x10GEXP_BITMSK_RXOAM_SOPE           0x0080
#define SUNI1x10GEXP_BITMSK_RXOAM_RFE            0x0040
#define SUNI1x10GEXP_BITMSK_RXOAM_LFE            0x0020
#define SUNI1x10GEXP_BITMSK_RXOAM_DV_ERRE        0x0010
#define SUNI1x10GEXP_BITMSK_RXOAM_DATA_INVALIDE  0x0008
#define SUNI1x10GEXP_BITMSK_RXOAM_FILTER_DROPE   0x0004
#define SUNI1x10GEXP_BITMSK_RXOAM_HECE           0x0002
#define SUNI1x10GEXP_BITMSK_RXOAM_OFLE           0x0001

/*----------------------------------------------------------------------------
 * Register 0x20C8: RXOAM Interrupt Status
 *    Bit 10 RXOAM_FILTER_THRSHI
 *    Bit 9  RXOAM_OAM_ERRI
 *    Bit 8  RXOAM_HECE_THRSHI
 *    Bit 7  RXOAM_SOPI
 *    Bit 6  RXOAM_RFI
 *    Bit 5  RXOAM_LFI
 *    Bit 4  RXOAM_DV_ERRI
 *    Bit 3  RXOAM_DATA_INVALIDI
 *    Bit 2  RXOAM_FILTER_DROPI
 *    Bit 1  RXOAM_HECI
 *    Bit 0  RXOAM_OFLI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXOAM_FILTER_THRSHI  0x0400
#define SUNI1x10GEXP_BITMSK_RXOAM_OAM_ERRI       0x0200
#define SUNI1x10GEXP_BITMSK_RXOAM_HECE_THRSHI    0x0100
#define SUNI1x10GEXP_BITMSK_RXOAM_SOPI           0x0080
#define SUNI1x10GEXP_BITMSK_RXOAM_RFI            0x0040
#define SUNI1x10GEXP_BITMSK_RXOAM_LFI            0x0020
#define SUNI1x10GEXP_BITMSK_RXOAM_DV_ERRI        0x0010
#define SUNI1x10GEXP_BITMSK_RXOAM_DATA_INVALIDI  0x0008
#define SUNI1x10GEXP_BITMSK_RXOAM_FILTER_DROPI   0x0004
#define SUNI1x10GEXP_BITMSK_RXOAM_HECI           0x0002
#define SUNI1x10GEXP_BITMSK_RXOAM_OFLI           0x0001

/*----------------------------------------------------------------------------
 * Register 0x20C9: RXOAM Status
 *    Bit 10 RXOAM_FILTER_THRSHV
 *    Bit 8  RXOAM_HECE_THRSHV
 *    Bit 6  RXOAM_RFV
 *    Bit 5  RXOAM_LFV
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXOAM_FILTER_THRSHV  0x0400
#define SUNI1x10GEXP_BITMSK_RXOAM_HECE_THRSHV    0x0100
#define SUNI1x10GEXP_BITMSK_RXOAM_RFV            0x0040
#define SUNI1x10GEXP_BITMSK_RXOAM_LFV            0x0020

/*----------------------------------------------------------------------------
 * Register 0x2100: MSTAT Control
 *    Bit 2 MSTAT_WRITE
 *    Bit 1 MSTAT_CLEAR
 *    Bit 0 MSTAT_SNAP
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_MSTAT_WRITE  0x0004
#define SUNI1x10GEXP_BITMSK_MSTAT_CLEAR  0x0002
#define SUNI1x10GEXP_BITMSK_MSTAT_SNAP   0x0001

/*----------------------------------------------------------------------------
 * Register 0x2109: MSTAT Counter Write Address
 *    Bit 5-0 MSTAT_WRITE_ADDRESS
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_MSTAT_WRITE_ADDRESS 0x003F
#define SUNI1x10GEXP_BITOFF_MSTAT_WRITE_ADDRESS 0

/*----------------------------------------------------------------------------
 * Register 0x2200: IFLX Global Configuration Register
 *    Bit 15   IFLX_IRCU_ENABLE
 *    Bit 14   IFLX_IDSWT_ENABLE
 *    Bit 13-0 IFLX_IFD_CNT
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_IFLX_IRCU_ENABLE   0x8000
#define SUNI1x10GEXP_BITMSK_IFLX_IDSWT_ENABLE  0x4000
#define SUNI1x10GEXP_BITMSK_IFLX_IFD_CNT       0x3FFF
#define SUNI1x10GEXP_BITOFF_IFLX_IFD_CNT       0

/*----------------------------------------------------------------------------
 * Register 0x2209: IFLX FIFO Overflow Enable
 *    Bit 0 IFLX_OVFE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_IFLX_OVFE 0x0001

/*----------------------------------------------------------------------------
 * Register 0x220A: IFLX FIFO Overflow Interrupt
 *    Bit 0 IFLX_OVFI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_IFLX_OVFI 0x0001

/*----------------------------------------------------------------------------
 * Register 0x220D: IFLX Indirect Channel Address
 *    Bit 15 IFLX_BUSY
 *    Bit 14 IFLX_RWB
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_IFLX_BUSY  0x8000
#define SUNI1x10GEXP_BITMSK_IFLX_RWB   0x4000

/*----------------------------------------------------------------------------
 * Register 0x220E: IFLX Indirect Logical FIFO Low Limit & Provision
 *    Bit 9-0 IFLX_LOLIM
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_IFLX_LOLIM  0x03FF
#define SUNI1x10GEXP_BITOFF_IFLX_LOLIM  0

/*----------------------------------------------------------------------------
 * Register 0x220F: IFLX Indirect Logical FIFO High Limit
 *    Bit 9-0 IFLX_HILIM
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_IFLX_HILIM  0x03FF
#define SUNI1x10GEXP_BITOFF_IFLX_HILIM  0

/*----------------------------------------------------------------------------
 * Register 0x2210: IFLX Indirect Full/Almost Full Status & Limit
 *    Bit 15   IFLX_FULL
 *    Bit 14   IFLX_AFULL
 *    Bit 13-0 IFLX_AFTH
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_IFLX_FULL   0x8000
#define SUNI1x10GEXP_BITMSK_IFLX_AFULL  0x4000
#define SUNI1x10GEXP_BITMSK_IFLX_AFTH   0x3FFF
#define SUNI1x10GEXP_BITOFF_IFLX_AFTH   0

/*----------------------------------------------------------------------------
 * Register 0x2211: IFLX Indirect Empty/Almost Empty Status & Limit
 *    Bit 15   IFLX_EMPTY
 *    Bit 14   IFLX_AEMPTY
 *    Bit 13-0 IFLX_AETH
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_IFLX_EMPTY   0x8000
#define SUNI1x10GEXP_BITMSK_IFLX_AEMPTY  0x4000
#define SUNI1x10GEXP_BITMSK_IFLX_AETH    0x3FFF
#define SUNI1x10GEXP_BITOFF_IFLX_AETH    0

/*----------------------------------------------------------------------------
 * Register 0x2240: PL4MOS Configuration Register
 *    Bit 3 PL4MOS_RE_INIT
 *    Bit 2 PL4MOS_EN
 *    Bit 1 PL4MOS_NO_STATUS
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4MOS_RE_INIT          0x0008
#define SUNI1x10GEXP_BITMSK_PL4MOS_EN               0x0004
#define SUNI1x10GEXP_BITMSK_PL4MOS_NO_STATUS        0x0002

/*----------------------------------------------------------------------------
 * Register 0x2243: PL4MOS MaxBurst1 Register
 *    Bit 11-0 PL4MOS_MAX_BURST1
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4MOS_MAX_BURST1  0x0FFF
#define SUNI1x10GEXP_BITOFF_PL4MOS_MAX_BURST1  0

/*----------------------------------------------------------------------------
 * Register 0x2244: PL4MOS MaxBurst2 Register
 *    Bit 11-0 PL4MOS_MAX_BURST2
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4MOS_MAX_BURST2  0x0FFF
#define SUNI1x10GEXP_BITOFF_PL4MOS_MAX_BURST2  0

/*----------------------------------------------------------------------------
 * Register 0x2245: PL4MOS Transfer Size Register
 *    Bit 7-0 PL4MOS_MAX_TRANSFER
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4MOS_MAX_TRANSFER  0x00FF
#define SUNI1x10GEXP_BITOFF_PL4MOS_MAX_TRANSFER  0

/*----------------------------------------------------------------------------
 * Register 0x2280: PL4ODP Configuration
 *    Bit 15-12 PL4ODP_REPEAT_T
 *    Bit 8     PL4ODP_SOP_RULE
 *    Bit 1     PL4ODP_EN_PORTS
 *    Bit 0     PL4ODP_EN_DFWD
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4ODP_REPEAT_T   0xF000
#define SUNI1x10GEXP_BITOFF_PL4ODP_REPEAT_T   12
#define SUNI1x10GEXP_BITMSK_PL4ODP_SOP_RULE   0x0100
#define SUNI1x10GEXP_BITMSK_PL4ODP_EN_PORTS   0x0002
#define SUNI1x10GEXP_BITMSK_PL4ODP_EN_DFWD    0x0001

/*----------------------------------------------------------------------------
 * Register 0x2282: PL4ODP Interrupt Mask
 *    Bit 0 PL4ODP_OUT_DISE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4ODP_OUT_DISE     0x0001



#define SUNI1x10GEXP_BITMSK_PL4ODP_PPE_EOPEOBE  0x0080
#define SUNI1x10GEXP_BITMSK_PL4ODP_PPE_ERREOPE  0x0040
#define SUNI1x10GEXP_BITMSK_PL4ODP_PPE_MEOPE    0x0008
#define SUNI1x10GEXP_BITMSK_PL4ODP_PPE_MSOPE    0x0004
#define SUNI1x10GEXP_BITMSK_PL4ODP_ES_OVRE      0x0002


/*----------------------------------------------------------------------------
 * Register 0x2283: PL4ODP Interrupt
 *    Bit 0 PL4ODP_OUT_DISI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4ODP_OUT_DISI     0x0001



#define SUNI1x10GEXP_BITMSK_PL4ODP_PPE_EOPEOBI  0x0080
#define SUNI1x10GEXP_BITMSK_PL4ODP_PPE_ERREOPI  0x0040
#define SUNI1x10GEXP_BITMSK_PL4ODP_PPE_MEOPI    0x0008
#define SUNI1x10GEXP_BITMSK_PL4ODP_PPE_MSOPI    0x0004
#define SUNI1x10GEXP_BITMSK_PL4ODP_ES_OVRI      0x0002

/*----------------------------------------------------------------------------
 * Register 0x2300:  PL4IO Lock Detect Status
 *    Bit 15 PL4IO_OUT_ROOLV
 *    Bit 12 PL4IO_IS_ROOLV
 *    Bit 11 PL4IO_DIP2_ERRV
 *    Bit 8  PL4IO_ID_ROOLV
 *    Bit 4  PL4IO_IS_DOOLV
 *    Bit 0  PL4IO_ID_DOOLV
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IO_OUT_ROOLV  0x8000
#define SUNI1x10GEXP_BITMSK_PL4IO_IS_ROOLV   0x1000
#define SUNI1x10GEXP_BITMSK_PL4IO_DIP2_ERRV  0x0800
#define SUNI1x10GEXP_BITMSK_PL4IO_ID_ROOLV   0x0100
#define SUNI1x10GEXP_BITMSK_PL4IO_IS_DOOLV   0x0010
#define SUNI1x10GEXP_BITMSK_PL4IO_ID_DOOLV   0x0001

/*----------------------------------------------------------------------------
 * Register 0x2301:  PL4IO Lock Detect Change
 *    Bit 15 PL4IO_OUT_ROOLI
 *    Bit 12 PL4IO_IS_ROOLI
 *    Bit 11 PL4IO_DIP2_ERRI
 *    Bit 8  PL4IO_ID_ROOLI
 *    Bit 4  PL4IO_IS_DOOLI
 *    Bit 0  PL4IO_ID_DOOLI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IO_OUT_ROOLI  0x8000
#define SUNI1x10GEXP_BITMSK_PL4IO_IS_ROOLI   0x1000
#define SUNI1x10GEXP_BITMSK_PL4IO_DIP2_ERRI  0x0800
#define SUNI1x10GEXP_BITMSK_PL4IO_ID_ROOLI   0x0100
#define SUNI1x10GEXP_BITMSK_PL4IO_IS_DOOLI   0x0010
#define SUNI1x10GEXP_BITMSK_PL4IO_ID_DOOLI   0x0001

/*----------------------------------------------------------------------------
 * Register 0x2302:  PL4IO Lock Detect Mask
 *    Bit 15 PL4IO_OUT_ROOLE
 *    Bit 12 PL4IO_IS_ROOLE
 *    Bit 11 PL4IO_DIP2_ERRE
 *    Bit 8  PL4IO_ID_ROOLE
 *    Bit 4  PL4IO_IS_DOOLE
 *    Bit 0  PL4IO_ID_DOOLE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IO_OUT_ROOLE  0x8000
#define SUNI1x10GEXP_BITMSK_PL4IO_IS_ROOLE   0x1000
#define SUNI1x10GEXP_BITMSK_PL4IO_DIP2_ERRE  0x0800
#define SUNI1x10GEXP_BITMSK_PL4IO_ID_ROOLE   0x0100
#define SUNI1x10GEXP_BITMSK_PL4IO_IS_DOOLE   0x0010
#define SUNI1x10GEXP_BITMSK_PL4IO_ID_DOOLE   0x0001

/*----------------------------------------------------------------------------
 * Register 0x2303:  PL4IO Lock Detect Limits
 *    Bit 15-8 PL4IO_REF_LIMIT
 *    Bit 7-0  PL4IO_TRAN_LIMIT
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IO_REF_LIMIT   0xFF00
#define SUNI1x10GEXP_BITOFF_PL4IO_REF_LIMIT   8
#define SUNI1x10GEXP_BITMSK_PL4IO_TRAN_LIMIT  0x00FF
#define SUNI1x10GEXP_BITOFF_PL4IO_TRAN_LIMIT  0

/*----------------------------------------------------------------------------
 * Register 0x2304:  PL4IO Calendar Repetitions
 *    Bit 15-8 PL4IO_IN_MUL
 *    Bit 7-0  PL4IO_OUT_MUL
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IO_IN_MUL   0xFF00
#define SUNI1x10GEXP_BITOFF_PL4IO_IN_MUL   8
#define SUNI1x10GEXP_BITMSK_PL4IO_OUT_MUL  0x00FF
#define SUNI1x10GEXP_BITOFF_PL4IO_OUT_MUL  0

/*----------------------------------------------------------------------------
 * Register 0x2305:  PL4IO Configuration
 *    Bit 15  PL4IO_DIP2_ERR_CHK
 *    Bit 11  PL4IO_ODAT_DIS
 *    Bit 10  PL4IO_TRAIN_DIS
 *    Bit 9   PL4IO_OSTAT_DIS
 *    Bit 8   PL4IO_ISTAT_DIS
 *    Bit 7   PL4IO_NO_ISTAT
 *    Bit 6   PL4IO_STAT_OUTSEL
 *    Bit 5   PL4IO_INSEL
 *    Bit 4   PL4IO_DLSEL
 *    Bit 1-0 PL4IO_OUTSEL
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IO_DIP2_ERR_CHK  0x8000
#define SUNI1x10GEXP_BITMSK_PL4IO_ODAT_DIS      0x0800
#define SUNI1x10GEXP_BITMSK_PL4IO_TRAIN_DIS     0x0400
#define SUNI1x10GEXP_BITMSK_PL4IO_OSTAT_DIS     0x0200
#define SUNI1x10GEXP_BITMSK_PL4IO_ISTAT_DIS     0x0100
#define SUNI1x10GEXP_BITMSK_PL4IO_NO_ISTAT      0x0080
#define SUNI1x10GEXP_BITMSK_PL4IO_STAT_OUTSEL   0x0040
#define SUNI1x10GEXP_BITMSK_PL4IO_INSEL         0x0020
#define SUNI1x10GEXP_BITMSK_PL4IO_DLSEL         0x0010
#define SUNI1x10GEXP_BITMSK_PL4IO_OUTSEL        0x0003
#define SUNI1x10GEXP_BITOFF_PL4IO_OUTSEL        0

/*----------------------------------------------------------------------------
 * Register 0x3040: TXXG Configuration Register 1
 *    Bit 15   TXXG_TXEN0
 *    Bit 13   TXXG_HOSTPAUSE
 *    Bit 12-7 TXXG_IPGT
 *    Bit 5    TXXG_32BIT_ALIGN
 *    Bit 4    TXXG_CRCEN
 *    Bit 3    TXXG_FCTX
 *    Bit 2    TXXG_FCRX
 *    Bit 1    TXXG_PADEN
 *    Bit 0    TXXG_SPRE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXXG_TXEN0        0x8000
#define SUNI1x10GEXP_BITMSK_TXXG_HOSTPAUSE    0x2000
#define SUNI1x10GEXP_BITMSK_TXXG_IPGT         0x1F80
#define SUNI1x10GEXP_BITOFF_TXXG_IPGT         7
#define SUNI1x10GEXP_BITMSK_TXXG_32BIT_ALIGN  0x0020
#define SUNI1x10GEXP_BITMSK_TXXG_CRCEN        0x0010
#define SUNI1x10GEXP_BITMSK_TXXG_FCTX         0x0008
#define SUNI1x10GEXP_BITMSK_TXXG_FCRX         0x0004
#define SUNI1x10GEXP_BITMSK_TXXG_PADEN        0x0002
#define SUNI1x10GEXP_BITMSK_TXXG_SPRE         0x0001

/*----------------------------------------------------------------------------
 * Register 0x3041: TXXG Configuration Register 2
 *    Bit 7-0   TXXG_HDRSIZE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXXG_HDRSIZE  0x00FF

/*----------------------------------------------------------------------------
 * Register 0x3042: TXXG Configuration Register 3
 *    Bit 15 TXXG_FIFO_ERRE
 *    Bit 14 TXXG_FIFO_UDRE
 *    Bit 13 TXXG_MAX_LERRE
 *    Bit 12 TXXG_MIN_LERRE
 *    Bit 11 TXXG_XFERE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXXG_FIFO_ERRE  0x8000
#define SUNI1x10GEXP_BITMSK_TXXG_FIFO_UDRE  0x4000
#define SUNI1x10GEXP_BITMSK_TXXG_MAX_LERRE  0x2000
#define SUNI1x10GEXP_BITMSK_TXXG_MIN_LERRE  0x1000
#define SUNI1x10GEXP_BITMSK_TXXG_XFERE      0x0800

/*----------------------------------------------------------------------------
 * Register 0x3043: TXXG Interrupt
 *    Bit 15 TXXG_FIFO_ERRI
 *    Bit 14 TXXG_FIFO_UDRI
 *    Bit 13 TXXG_MAX_LERRI
 *    Bit 12 TXXG_MIN_LERRI
 *    Bit 11 TXXG_XFERI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXXG_FIFO_ERRI  0x8000
#define SUNI1x10GEXP_BITMSK_TXXG_FIFO_UDRI  0x4000
#define SUNI1x10GEXP_BITMSK_TXXG_MAX_LERRI  0x2000
#define SUNI1x10GEXP_BITMSK_TXXG_MIN_LERRI  0x1000
#define SUNI1x10GEXP_BITMSK_TXXG_XFERI      0x0800

/*----------------------------------------------------------------------------
 * Register 0x3044: TXXG Status Register
 *    Bit 1 TXXG_TXACTIVE
 *    Bit 0 TXXG_PAUSED
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXXG_TXACTIVE  0x0002
#define SUNI1x10GEXP_BITMSK_TXXG_PAUSED    0x0001

/*----------------------------------------------------------------------------
 * Register 0x3046: TXXG TX_MINFR -  Transmit Min Frame Size Register
 *    Bit 7-0 TXXG_TX_MINFR
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXXG_TX_MINFR  0x00FF
#define SUNI1x10GEXP_BITOFF_TXXG_TX_MINFR  0

/*----------------------------------------------------------------------------
 * Register 0x3052: TXXG Pause Quantum Value Configuration Register
 *    Bit 7-0 TXXG_FC_PAUSE_QNTM
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXXG_FC_PAUSE_QNTM  0x00FF
#define SUNI1x10GEXP_BITOFF_TXXG_FC_PAUSE_QNTM  0

/*----------------------------------------------------------------------------
 * Register 0x3080: XTEF Control
 *    Bit 3-0 XTEF_FORCE_PARITY_ERR
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_XTEF_FORCE_PARITY_ERR  0x000F
#define SUNI1x10GEXP_BITOFF_XTEF_FORCE_PARITY_ERR  0

/*----------------------------------------------------------------------------
 * Register 0x3084: XTEF Interrupt Event Register
 *    Bit 0 XTEF_LOST_SYNCI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_XTEF_LOST_SYNCI  0x0001

/*----------------------------------------------------------------------------
 * Register 0x3085: XTEF Interrupt Enable Register
 *    Bit 0 XTEF_LOST_SYNCE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_XTEF_LOST_SYNCE  0x0001

/*----------------------------------------------------------------------------
 * Register 0x3086: XTEF Visibility Register
 *    Bit 0 XTEF_LOST_SYNCV
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_XTEF_LOST_SYNCV  0x0001

/*----------------------------------------------------------------------------
 * Register 0x30C0: TXOAM OAM Configuration
 *    Bit 15   TXOAM_HEC_EN
 *    Bit 14   TXOAM_EMPTYCODE_EN
 *    Bit 13   TXOAM_FORCE_IDLE
 *    Bit 12   TXOAM_IGNORE_IDLE
 *    Bit 11-6 TXOAM_PX_OVERWRITE
 *    Bit 5-0  TXOAM_PX_SEL
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXOAM_HEC_EN        0x8000
#define SUNI1x10GEXP_BITMSK_TXOAM_EMPTYCODE_EN  0x4000
#define SUNI1x10GEXP_BITMSK_TXOAM_FORCE_IDLE    0x2000
#define SUNI1x10GEXP_BITMSK_TXOAM_IGNORE_IDLE   0x1000
#define SUNI1x10GEXP_BITMSK_TXOAM_PX_OVERWRITE  0x0FC0
#define SUNI1x10GEXP_BITOFF_TXOAM_PX_OVERWRITE  6
#define SUNI1x10GEXP_BITMSK_TXOAM_PX_SEL        0x003F
#define SUNI1x10GEXP_BITOFF_TXOAM_PX_SEL        0

/*----------------------------------------------------------------------------
 * Register 0x30C1: TXOAM Mini-Packet Rate Configuration
 *    Bit 15   TXOAM_MINIDIS
 *    Bit 14   TXOAM_BUSY
 *    Bit 13   TXOAM_TRANS_EN
 *    Bit 10-0 TXOAM_MINIRATE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXOAM_MINIDIS   0x8000
#define SUNI1x10GEXP_BITMSK_TXOAM_BUSY      0x4000
#define SUNI1x10GEXP_BITMSK_TXOAM_TRANS_EN  0x2000
#define SUNI1x10GEXP_BITMSK_TXOAM_MINIRATE  0x07FF

/*----------------------------------------------------------------------------
 * Register 0x30C2: TXOAM Mini-Packet Gap and FIFO Configuration
 *    Bit 13-10 TXOAM_FTHRESH
 *    Bit 9-6   TXOAM_MINIPOST
 *    Bit 5-0   TXOAM_MINIPRE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXOAM_FTHRESH   0x3C00
#define SUNI1x10GEXP_BITOFF_TXOAM_FTHRESH   10
#define SUNI1x10GEXP_BITMSK_TXOAM_MINIPOST  0x03C0
#define SUNI1x10GEXP_BITOFF_TXOAM_MINIPOST  6
#define SUNI1x10GEXP_BITMSK_TXOAM_MINIPRE   0x003F

/*----------------------------------------------------------------------------
 * Register 0x30C6: TXOAM Interrupt Enable
 *    Bit 2 TXOAM_SOP_ERRE
 *    Bit 1 TXOAM_OFLE
 *    Bit 0 TXOAM_ERRE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXOAM_SOP_ERRE    0x0004
#define SUNI1x10GEXP_BITMSK_TXOAM_OFLE        0x0002
#define SUNI1x10GEXP_BITMSK_TXOAM_ERRE        0x0001

/*----------------------------------------------------------------------------
 * Register 0x30C7: TXOAM Interrupt Status
 *    Bit 2 TXOAM_SOP_ERRI
 *    Bit 1 TXOAM_OFLI
 *    Bit 0 TXOAM_ERRI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXOAM_SOP_ERRI    0x0004
#define SUNI1x10GEXP_BITMSK_TXOAM_OFLI        0x0002
#define SUNI1x10GEXP_BITMSK_TXOAM_ERRI        0x0001

/*----------------------------------------------------------------------------
 * Register 0x30CF: TXOAM Coset
 *    Bit 7-0 TXOAM_COSET
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TXOAM_COSET  0x00FF

/*----------------------------------------------------------------------------
 * Register 0x3200: EFLX Global Configuration
 *    Bit 15 EFLX_ERCU_EN
 *    Bit 7  EFLX_EN_EDSWT
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_ERCU_EN   0x8000
#define SUNI1x10GEXP_BITMSK_EFLX_EN_EDSWT  0x0080

/*----------------------------------------------------------------------------
 * Register 0x3201: EFLX ERCU Global Status
 *    Bit 13 EFLX_OVF_ERR
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_OVF_ERR  0x2000

/*----------------------------------------------------------------------------
 * Register 0x3202: EFLX Indirect Channel Address
 *    Bit 15 EFLX_BUSY
 *    Bit 14 EFLX_RDWRB
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_BUSY   0x8000
#define SUNI1x10GEXP_BITMSK_EFLX_RDWRB  0x4000

/*----------------------------------------------------------------------------
 * Register 0x3203: EFLX Indirect Logical FIFO Low Limit
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_LOLIM                    0x03FF
#define SUNI1x10GEXP_BITOFF_EFLX_LOLIM                    0

/*----------------------------------------------------------------------------
 * Register 0x3204: EFLX Indirect Logical FIFO High Limit
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_HILIM                    0x03FF
#define SUNI1x10GEXP_BITOFF_EFLX_HILIM                    0

/*----------------------------------------------------------------------------
 * Register 0x3205: EFLX Indirect Full/Almost-Full Status and Limit
 *    Bit 15   EFLX_FULL
 *    Bit 14   EFLX_AFULL
 *    Bit 13-0 EFLX_AFTH
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_FULL   0x8000
#define SUNI1x10GEXP_BITMSK_EFLX_AFULL  0x4000
#define SUNI1x10GEXP_BITMSK_EFLX_AFTH   0x3FFF
#define SUNI1x10GEXP_BITOFF_EFLX_AFTH   0

/*----------------------------------------------------------------------------
 * Register 0x3206: EFLX Indirect Empty/Almost-Empty Status and Limit
 *    Bit 15   EFLX_EMPTY
 *    Bit 14   EFLX_AEMPTY
 *    Bit 13-0 EFLX_AETH
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_EMPTY   0x8000
#define SUNI1x10GEXP_BITMSK_EFLX_AEMPTY  0x4000
#define SUNI1x10GEXP_BITMSK_EFLX_AETH    0x3FFF
#define SUNI1x10GEXP_BITOFF_EFLX_AETH    0

/*----------------------------------------------------------------------------
 * Register 0x3207: EFLX Indirect FIFO Cut-Through Threshold
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_CUT_THRU                 0x3FFF
#define SUNI1x10GEXP_BITOFF_EFLX_CUT_THRU                 0

/*----------------------------------------------------------------------------
 * Register 0x320C: EFLX FIFO Overflow Error Enable
 *    Bit 0 EFLX_OVFE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_OVFE  0x0001

/*----------------------------------------------------------------------------
 * Register 0x320D: EFLX FIFO Overflow Error Indication
 *    Bit 0 EFLX_OVFI
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_OVFI  0x0001

/*----------------------------------------------------------------------------
 * Register 0x3210: EFLX Channel Provision
 *    Bit 0 EFLX_PROV
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_EFLX_PROV  0x0001

/*----------------------------------------------------------------------------
 * Register 0x3280: PL4IDU Configuration
 *    Bit 2 PL4IDU_SYNCH_ON_TRAIN
 *    Bit 1 PL4IDU_EN_PORTS
 *    Bit 0 PL4IDU_EN_DFWD
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IDU_SYNCH_ON_TRAIN  0x0004
#define SUNI1x10GEXP_BITMSK_PL4IDU_EN_PORTS        0x0002
#define SUNI1x10GEXP_BITMSK_PL4IDU_EN_DFWD         0x0001

/*----------------------------------------------------------------------------
 * Register 0x3282: PL4IDU Interrupt Mask
 *    Bit 1 PL4IDU_DIP4E
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IDU_DIP4E       0x0002

/*----------------------------------------------------------------------------
 * Register 0x3283: PL4IDU Interrupt
 *    Bit 1 PL4IDU_DIP4I
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_PL4IDU_DIP4I       0x0002

#endif /* _CXGB_SUNI1x10GEXP_REGS_H_ */
