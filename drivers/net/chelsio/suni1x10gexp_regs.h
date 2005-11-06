/*****************************************************************************
 *                                                                           *
 * File: suni1x10gexp_regs.h                                                 *
 * $Revision: 1.9 $                                                          *
 * $Date: 2005/06/22 00:17:04 $                                              *
 * Description:                                                              *
 *  PMC/SIERRA (pm3393) MAC-PHY functionality.                               *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
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

/******************************************************************************/
/** S/UNI-1x10GE-XP REGISTER ADDRESS MAP                                     **/
/******************************************************************************/
/* Refer to the Register Bit Masks bellow for the naming of each register and */
/* to the S/UNI-1x10GE-XP Data Sheet for the signification of each bit        */
/******************************************************************************/

#define SUNI1x10GEXP_REG_DEVICE_STATUS                                   0x0004
#define SUNI1x10GEXP_REG_MASTER_INTERRUPT_STATUS                         0x000D
#define SUNI1x10GEXP_REG_GLOBAL_INTERRUPT_ENABLE                         0x000E
#define SUNI1x10GEXP_REG_SERDES_3125_INTERRUPT_ENABLE                    0x0102
#define SUNI1x10GEXP_REG_SERDES_3125_INTERRUPT_STATUS                    0x0104
#define SUNI1x10GEXP_REG_RXXG_CONFIG_1                                   0x2040
#define SUNI1x10GEXP_REG_RXXG_CONFIG_3                                   0x2042
#define SUNI1x10GEXP_REG_RXXG_INTERRUPT                                  0x2043
#define SUNI1x10GEXP_REG_RXXG_MAX_FRAME_LENGTH                           0x2045
#define SUNI1x10GEXP_REG_RXXG_SA_15_0                                    0x2046
#define SUNI1x10GEXP_REG_RXXG_SA_31_16                                   0x2047
#define SUNI1x10GEXP_REG_RXXG_SA_47_32                                   0x2048
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_1_LOW                     0x204D
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_1_MID                     0x204E
#define SUNI1x10GEXP_REG_RXXG_EXACT_MATCH_ADDR_1_HIGH                    0x204F
#define SUNI1x10GEXP_REG_RXXG_MULTICAST_HASH_LOW                         0x206A
#define SUNI1x10GEXP_REG_RXXG_MULTICAST_HASH_MIDLOW                      0x206B
#define SUNI1x10GEXP_REG_RXXG_MULTICAST_HASH_MIDHIGH                     0x206C
#define SUNI1x10GEXP_REG_RXXG_MULTICAST_HASH_HIGH                        0x206D
#define SUNI1x10GEXP_REG_RXXG_ADDRESS_FILTER_CONTROL_0                   0x206E
#define SUNI1x10GEXP_REG_RXXG_ADDRESS_FILTER_CONTROL_2                   0x2070
#define SUNI1x10GEXP_REG_XRF_INTERRUPT_ENABLE                            0x2088
#define SUNI1x10GEXP_REG_XRF_INTERRUPT_STATUS                            0x2089
#define SUNI1x10GEXP_REG_XRF_DIAG_INTERRUPT_ENABLE                       0x208B
#define SUNI1x10GEXP_REG_XRF_DIAG_INTERRUPT_STATUS                       0x208C
#define SUNI1x10GEXP_REG_RXOAM_INTERRUPT_ENABLE                          0x20C7
#define SUNI1x10GEXP_REG_RXOAM_INTERRUPT_STATUS                          0x20C8
#define SUNI1x10GEXP_REG_MSTAT_CONTROL                                   0x2100
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_ROLLOVER_0                        0x2101
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_ROLLOVER_1                        0x2102
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_ROLLOVER_2                        0x2103
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_ROLLOVER_3                        0x2104
#define SUNI1x10GEXP_REG_MSTAT_INTERRUPT_MASK_0                          0x2105
#define SUNI1x10GEXP_REG_MSTAT_INTERRUPT_MASK_1                          0x2106
#define SUNI1x10GEXP_REG_MSTAT_INTERRUPT_MASK_2                          0x2107
#define SUNI1x10GEXP_REG_MSTAT_INTERRUPT_MASK_3                          0x2108
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_0_LOW                             0x2110
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_1_LOW                             0x2114
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_4_LOW                             0x2120
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_5_LOW                             0x2124
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_6_LOW                             0x2128
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_8_LOW                             0x2130
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_10_LOW                            0x2138
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_11_LOW                            0x213C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_12_LOW                            0x2140
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_13_LOW                            0x2144
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_15_LOW                            0x214C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_16_LOW                            0x2150
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_17_LOW                            0x2154
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_18_LOW                            0x2158
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_33_LOW                            0x2194
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_35_LOW                            0x219C
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_36_LOW                            0x21A0
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_38_LOW                            0x21A8
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_40_LOW                            0x21B0
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_42_LOW                            0x21B8
#define SUNI1x10GEXP_REG_MSTAT_COUNTER_43_LOW                            0x21BC
#define SUNI1x10GEXP_REG_IFLX_FIFO_OVERFLOW_ENABLE                       0x2209
#define SUNI1x10GEXP_REG_IFLX_FIFO_OVERFLOW_INTERRUPT                    0x220A
#define SUNI1x10GEXP_REG_PL4ODP_INTERRUPT_MASK                           0x2282
#define SUNI1x10GEXP_REG_PL4ODP_INTERRUPT                                0x2283
#define SUNI1x10GEXP_REG_PL4IO_LOCK_DETECT_STATUS                        0x2300
#define SUNI1x10GEXP_REG_PL4IO_LOCK_DETECT_CHANGE                        0x2301
#define SUNI1x10GEXP_REG_PL4IO_LOCK_DETECT_MASK                          0x2302
#define SUNI1x10GEXP_REG_TXXG_CONFIG_1                                   0x3040
#define SUNI1x10GEXP_REG_TXXG_CONFIG_3                                   0x3042
#define SUNI1x10GEXP_REG_TXXG_INTERRUPT                                  0x3043
#define SUNI1x10GEXP_REG_TXXG_MAX_FRAME_SIZE                             0x3045
#define SUNI1x10GEXP_REG_TXXG_SA_15_0                                    0x3047
#define SUNI1x10GEXP_REG_TXXG_SA_31_16                                   0x3048
#define SUNI1x10GEXP_REG_TXXG_SA_47_32                                   0x3049
#define SUNI1x10GEXP_REG_XTEF_INTERRUPT_STATUS                           0x3084
#define SUNI1x10GEXP_REG_XTEF_INTERRUPT_ENABLE                           0x3085
#define SUNI1x10GEXP_REG_TXOAM_INTERRUPT_ENABLE                          0x30C6
#define SUNI1x10GEXP_REG_TXOAM_INTERRUPT_STATUS                          0x30C7
#define SUNI1x10GEXP_REG_EFLX_FIFO_OVERFLOW_ERROR_ENABLE                 0x320C
#define SUNI1x10GEXP_REG_EFLX_FIFO_OVERFLOW_ERROR_INDICATION             0x320D
#define SUNI1x10GEXP_REG_PL4IDU_INTERRUPT_MASK                           0x3282
#define SUNI1x10GEXP_REG_PL4IDU_INTERRUPT                                0x3283

/******************************************************************************/
/*                 -- End register offset definitions --                      */
/******************************************************************************/

/******************************************************************************/
/** SUNI-1x10GE-XP REGISTER BIT MASKS                                        **/
/******************************************************************************/

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
#define SUNI1x10GEXP_BITMSK_TOP_EXPIRED       0x0040
#define SUNI1x10GEXP_BITMSK_TOP_PL4_ID_DOOL   0x0010
#define SUNI1x10GEXP_BITMSK_TOP_PL4_IS_DOOL   0x0008
#define SUNI1x10GEXP_BITMSK_TOP_PL4_ID_ROOL   0x0004
#define SUNI1x10GEXP_BITMSK_TOP_PL4_IS_ROOL   0x0002
#define SUNI1x10GEXP_BITMSK_TOP_PL4_OUT_ROOL  0x0001

/*----------------------------------------------------------------------------
 * Register 0x000E:PM3393 Global interrupt enable
 *    Bit 15 TOP_INTE
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_TOP_INTE  0x8000

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
#define SUNI1x10GEXP_BITMSK_RXXG_PUREP      0x0400
#define SUNI1x10GEXP_BITMSK_RXXG_FLCHK      0x0080
#define SUNI1x10GEXP_BITMSK_RXXG_CRC_STRIP  0x0008

/*----------------------------------------------------------------------------
 * Register 0x2070: RXXG Address Filter Control 2
 *    Bit 1 RXXG_PMODE
 *    Bit 0 RXXG_MHASH_EN
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_RXXG_PMODE     0x0002
#define SUNI1x10GEXP_BITMSK_RXXG_MHASH_EN  0x0001

/*----------------------------------------------------------------------------
 * Register 0x2100: MSTAT Control
 *    Bit 2 MSTAT_WRITE
 *    Bit 1 MSTAT_CLEAR
 *    Bit 0 MSTAT_SNAP
 *----------------------------------------------------------------------------*/
#define SUNI1x10GEXP_BITMSK_MSTAT_CLEAR  0x0002
#define SUNI1x10GEXP_BITMSK_MSTAT_SNAP   0x0001

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
#define SUNI1x10GEXP_BITOFF_TXXG_IPGT         7
#define SUNI1x10GEXP_BITMSK_TXXG_32BIT_ALIGN  0x0020
#define SUNI1x10GEXP_BITMSK_TXXG_CRCEN        0x0010
#define SUNI1x10GEXP_BITMSK_TXXG_FCTX         0x0008
#define SUNI1x10GEXP_BITMSK_TXXG_FCRX         0x0004
#define SUNI1x10GEXP_BITMSK_TXXG_PADEN        0x0002

#endif /* _CXGB_SUNI1x10GEXP_REGS_H_ */

