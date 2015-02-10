/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* #define MAC_SPLIT_MODE */

#define MAC_SPACING                 0x400
#define XGMAC_SPACING               0x400

/* PE-MCXMAC register and bit field definitions */
#define R_MAC_CONFIG_1                                              0x00
#define   O_MAC_CONFIG_1__srst                                      31
#define   O_MAC_CONFIG_1__simr                                      30
#define   O_MAC_CONFIG_1__hrrmc                                     18
#define   W_MAC_CONFIG_1__hrtmc                                      2
#define   O_MAC_CONFIG_1__hrrfn                                     16
#define   W_MAC_CONFIG_1__hrtfn                                      2
#define   O_MAC_CONFIG_1__intlb                                      8
#define   O_MAC_CONFIG_1__rxfc                                       5
#define   O_MAC_CONFIG_1__txfc                                       4
#define   O_MAC_CONFIG_1__srxen                                      3
#define   O_MAC_CONFIG_1__rxen                                       2
#define   O_MAC_CONFIG_1__stxen                                      1
#define   O_MAC_CONFIG_1__txen                                       0
#define R_MAC_CONFIG_2                                              0x01
#define   O_MAC_CONFIG_2__prlen                                     12
#define   W_MAC_CONFIG_2__prlen                                      4
#define   O_MAC_CONFIG_2__speed                                      8
#define   W_MAC_CONFIG_2__speed                                      2
#define   O_MAC_CONFIG_2__hugen                                      5
#define   O_MAC_CONFIG_2__flchk                                      4
#define   O_MAC_CONFIG_2__crce                                       1
#define   O_MAC_CONFIG_2__fulld                                      0
#define R_IPG_IFG                                                   0x02
#define   O_IPG_IFG__ipgr1                                          24
#define   W_IPG_IFG__ipgr1                                           7
#define   O_IPG_IFG__ipgr2                                          16
#define   W_IPG_IFG__ipgr2                                           7
#define   O_IPG_IFG__mifg                                            8
#define   W_IPG_IFG__mifg                                            8
#define   O_IPG_IFG__ipgt                                            0
#define   W_IPG_IFG__ipgt                                            7
#define R_HALF_DUPLEX                                               0x03
#define   O_HALF_DUPLEX__abebt                                      24
#define   W_HALF_DUPLEX__abebt                                       4
#define   O_HALF_DUPLEX__abebe                                      19
#define   O_HALF_DUPLEX__bpnb                                       18
#define   O_HALF_DUPLEX__nobo                                       17
#define   O_HALF_DUPLEX__edxsdfr                                    16
#define   O_HALF_DUPLEX__retry                                      12
#define   W_HALF_DUPLEX__retry                                       4
#define   O_HALF_DUPLEX__lcol                                        0
#define   W_HALF_DUPLEX__lcol                                       10
#define R_MAXIMUM_FRAME_LENGTH                                      0x04
#define   O_MAXIMUM_FRAME_LENGTH__maxf                               0
#define   W_MAXIMUM_FRAME_LENGTH__maxf                              16
#define R_TEST                                                      0x07
#define   O_TEST__mbof                                               3
#define   O_TEST__rthdf                                              2
#define   O_TEST__tpause                                             1
#define   O_TEST__sstct                                              0
#define R_MII_MGMT_CONFIG                                           0x08
#define   O_MII_MGMT_CONFIG__scinc                                   5
#define   O_MII_MGMT_CONFIG__spre                                    4
#define   O_MII_MGMT_CONFIG__clks                                    3
#define   W_MII_MGMT_CONFIG__clks                                    3
#define R_MII_MGMT_COMMAND                                          0x09
#define   O_MII_MGMT_COMMAND__scan                                   1
#define   O_MII_MGMT_COMMAND__rstat                                  0
#define R_MII_MGMT_ADDRESS                                          0x0A
#define   O_MII_MGMT_ADDRESS__fiad                                   8
#define   W_MII_MGMT_ADDRESS__fiad                                   5
#define   O_MII_MGMT_ADDRESS__fgad                                   5
#define   W_MII_MGMT_ADDRESS__fgad                                   0
#define R_MII_MGMT_WRITE_DATA                                       0x0B
#define   O_MII_MGMT_WRITE_DATA__ctld                                0
#define   W_MII_MGMT_WRITE_DATA__ctld                               16
#define R_MII_MGMT_STATUS                                           0x0C
#define R_MII_MGMT_INDICATORS                                       0x0D
#define   O_MII_MGMT_INDICATORS__nvalid                              2
#define   O_MII_MGMT_INDICATORS__scan                                1
#define   O_MII_MGMT_INDICATORS__busy                                0
#define R_INTERFACE_CONTROL                                         0x0E
#define   O_INTERFACE_CONTROL__hrstint                              31
#define   O_INTERFACE_CONTROL__tbimode                              27
#define   O_INTERFACE_CONTROL__ghdmode                              26
#define   O_INTERFACE_CONTROL__lhdmode                              25
#define   O_INTERFACE_CONTROL__phymod                               24
#define   O_INTERFACE_CONTROL__hrrmi                                23
#define   O_INTERFACE_CONTROL__rspd                                 16
#define   O_INTERFACE_CONTROL__hr100                                15
#define   O_INTERFACE_CONTROL__frcq                                 10
#define   O_INTERFACE_CONTROL__nocfr                                 9
#define   O_INTERFACE_CONTROL__dlfct                                 8
#define   O_INTERFACE_CONTROL__enjab                                 0
#define R_INTERFACE_STATUS                                         0x0F
#define   O_INTERFACE_STATUS__xsdfr                                  9
#define   O_INTERFACE_STATUS__ssrr                                   8
#define   W_INTERFACE_STATUS__ssrr                                   5
#define   O_INTERFACE_STATUS__miilf                                  3
#define   O_INTERFACE_STATUS__locar                                  2
#define   O_INTERFACE_STATUS__sqerr                                  1
#define   O_INTERFACE_STATUS__jabber                                 0
#define R_STATION_ADDRESS_LS                                       0x10
#define R_STATION_ADDRESS_MS                                       0x11

/* A-XGMAC register and bit field definitions */
#define R_XGMAC_CONFIG_0    0x00
#define   O_XGMAC_CONFIG_0__hstmacrst               31
#define   O_XGMAC_CONFIG_0__hstrstrctl              23
#define   O_XGMAC_CONFIG_0__hstrstrfn               22
#define   O_XGMAC_CONFIG_0__hstrsttctl              18
#define   O_XGMAC_CONFIG_0__hstrsttfn               17
#define   O_XGMAC_CONFIG_0__hstrstmiim              16
#define   O_XGMAC_CONFIG_0__hstloopback             8
#define R_XGMAC_CONFIG_1    0x01
#define   O_XGMAC_CONFIG_1__hsttctlen               31
#define   O_XGMAC_CONFIG_1__hsttfen                 30
#define   O_XGMAC_CONFIG_1__hstrctlen               29
#define   O_XGMAC_CONFIG_1__hstrfen                 28
#define   O_XGMAC_CONFIG_1__tfen                    26
#define   O_XGMAC_CONFIG_1__rfen                    24
#define   O_XGMAC_CONFIG_1__hstrctlshrtp            12
#define   O_XGMAC_CONFIG_1__hstdlyfcstx             10
#define   W_XGMAC_CONFIG_1__hstdlyfcstx              2
#define   O_XGMAC_CONFIG_1__hstdlyfcsrx              8
#define   W_XGMAC_CONFIG_1__hstdlyfcsrx              2
#define   O_XGMAC_CONFIG_1__hstppen                  7
#define   O_XGMAC_CONFIG_1__hstbytswp                6
#define   O_XGMAC_CONFIG_1__hstdrplt64               5
#define   O_XGMAC_CONFIG_1__hstprmscrx               4
#define   O_XGMAC_CONFIG_1__hstlenchk                3
#define   O_XGMAC_CONFIG_1__hstgenfcs                2
#define   O_XGMAC_CONFIG_1__hstpadmode               0
#define   W_XGMAC_CONFIG_1__hstpadmode               2
#define R_XGMAC_CONFIG_2    0x02
#define   O_XGMAC_CONFIG_2__hsttctlfrcp             31
#define   O_XGMAC_CONFIG_2__hstmlnkflth             27
#define   O_XGMAC_CONFIG_2__hstalnkflth             26
#define   O_XGMAC_CONFIG_2__rflnkflt                24
#define   W_XGMAC_CONFIG_2__rflnkflt                 2
#define   O_XGMAC_CONFIG_2__hstipgextmod            16
#define   W_XGMAC_CONFIG_2__hstipgextmod             5
#define   O_XGMAC_CONFIG_2__hstrctlfrcp             15
#define   O_XGMAC_CONFIG_2__hstipgexten              5
#define   O_XGMAC_CONFIG_2__hstmipgext               0
#define   W_XGMAC_CONFIG_2__hstmipgext               5
#define R_XGMAC_CONFIG_3    0x03
#define   O_XGMAC_CONFIG_3__hstfltrfrm              31
#define   W_XGMAC_CONFIG_3__hstfltrfrm              16
#define   O_XGMAC_CONFIG_3__hstfltrfrmdc            15
#define   W_XGMAC_CONFIG_3__hstfltrfrmdc            16
#define R_XGMAC_STATION_ADDRESS_LS      0x04
#define   O_XGMAC_STATION_ADDRESS_LS__hstmacadr0    0
#define   W_XGMAC_STATION_ADDRESS_LS__hstmacadr0    32
#define R_XGMAC_STATION_ADDRESS_MS      0x05
#define R_XGMAC_MAX_FRAME_LEN           0x08
#define   O_XGMAC_MAX_FRAME_LEN__hstmxfrmwctx       16
#define   W_XGMAC_MAX_FRAME_LEN__hstmxfrmwctx       14
#define   O_XGMAC_MAX_FRAME_LEN__hstmxfrmbcrx        0
#define   W_XGMAC_MAX_FRAME_LEN__hstmxfrmbcrx       16
#define R_XGMAC_REV_LEVEL               0x0B
#define   O_XGMAC_REV_LEVEL__revlvl                  0
#define   W_XGMAC_REV_LEVEL__revlvl                 15
#define R_XGMAC_MIIM_COMMAND            0x10
#define   O_XGMAC_MIIM_COMMAND__hstldcmd             3
#define   O_XGMAC_MIIM_COMMAND__hstmiimcmd           0
#define   W_XGMAC_MIIM_COMMAND__hstmiimcmd           3
#define R_XGMAC_MIIM_FILED              0x11
#define   O_XGMAC_MIIM_FILED__hststfield            30
#define   W_XGMAC_MIIM_FILED__hststfield             2
#define   O_XGMAC_MIIM_FILED__hstopfield            28
#define   W_XGMAC_MIIM_FILED__hstopfield             2
#define   O_XGMAC_MIIM_FILED__hstphyadx             23
#define   W_XGMAC_MIIM_FILED__hstphyadx              5
#define   O_XGMAC_MIIM_FILED__hstregadx             18
#define   W_XGMAC_MIIM_FILED__hstregadx              5
#define   O_XGMAC_MIIM_FILED__hsttafield            16
#define   W_XGMAC_MIIM_FILED__hsttafield             2
#define   O_XGMAC_MIIM_FILED__miimrddat              0
#define   W_XGMAC_MIIM_FILED__miimrddat             16
#define R_XGMAC_MIIM_CONFIG             0x12
#define   O_XGMAC_MIIM_CONFIG__hstnopram             7
#define   O_XGMAC_MIIM_CONFIG__hstclkdiv             0
#define   W_XGMAC_MIIM_CONFIG__hstclkdiv             7
#define R_XGMAC_MIIM_LINK_FAIL_VECTOR   0x13
#define   O_XGMAC_MIIM_LINK_FAIL_VECTOR__miimlfvec   0
#define   W_XGMAC_MIIM_LINK_FAIL_VECTOR__miimlfvec  32
#define R_XGMAC_MIIM_INDICATOR          0x14
#define   O_XGMAC_MIIM_INDICATOR__miimphylf          4
#define   O_XGMAC_MIIM_INDICATOR__miimmoncplt        3
#define   O_XGMAC_MIIM_INDICATOR__miimmonvld         2
#define   O_XGMAC_MIIM_INDICATOR__miimmon            1
#define   O_XGMAC_MIIM_INDICATOR__miimbusy           0

/* GMAC stats registers */
#define R_RBYT							    0x27
#define R_RPKT							    0x28
#define R_RFCS							    0x29
#define R_RMCA							    0x2A
#define R_RBCA							    0x2B
#define R_RXCF							    0x2C
#define R_RXPF							    0x2D
#define R_RXUO							    0x2E
#define R_RALN							    0x2F
#define R_RFLR							    0x30
#define R_RCDE							    0x31
#define R_RCSE							    0x32
#define R_RUND							    0x33
#define R_ROVR							    0x34
#define R_TBYT							    0x38
#define R_TPKT							    0x39
#define R_TMCA							    0x3A
#define R_TBCA							    0x3B
#define R_TXPF							    0x3C
#define R_TDFR							    0x3D
#define R_TEDF							    0x3E
#define R_TSCL							    0x3F
#define R_TMCL							    0x40
#define R_TLCL							    0x41
#define R_TXCL							    0x42
#define R_TNCL							    0x43
#define R_TJBR							    0x46
#define R_TFCS							    0x47
#define R_TXCF							    0x48
#define R_TOVR							    0x49
#define R_TUND							    0x4A
#define R_TFRG							    0x4B

/* Glue logic register and bit field definitions */
#define R_MAC_ADDR0                                                 0x50
#define R_MAC_ADDR1                                                 0x52
#define R_MAC_ADDR2                                                 0x54
#define R_MAC_ADDR3                                                 0x56
#define R_MAC_ADDR_MASK2                                            0x58
#define R_MAC_ADDR_MASK3                                            0x5A
#define R_MAC_FILTER_CONFIG                                         0x5C
#define   O_MAC_FILTER_CONFIG__BROADCAST_EN                         10
#define   O_MAC_FILTER_CONFIG__PAUSE_FRAME_EN                       9
#define   O_MAC_FILTER_CONFIG__ALL_MCAST_EN                         8
#define   O_MAC_FILTER_CONFIG__ALL_UCAST_EN                         7
#define   O_MAC_FILTER_CONFIG__HASH_MCAST_EN                        6
#define   O_MAC_FILTER_CONFIG__HASH_UCAST_EN                        5
#define   O_MAC_FILTER_CONFIG__ADDR_MATCH_DISC                      4
#define   O_MAC_FILTER_CONFIG__MAC_ADDR3_VALID                      3
#define   O_MAC_FILTER_CONFIG__MAC_ADDR2_VALID                      2
#define   O_MAC_FILTER_CONFIG__MAC_ADDR1_VALID                      1
#define   O_MAC_FILTER_CONFIG__MAC_ADDR0_VALID                      0
#define R_HASH_TABLE_VECTOR                                         0x30
#define R_TX_CONTROL                                                 0x0A0
#define   O_TX_CONTROL__Tx15Halt                                     31
#define   O_TX_CONTROL__Tx14Halt                                     30
#define   O_TX_CONTROL__Tx13Halt                                     29
#define   O_TX_CONTROL__Tx12Halt                                     28
#define   O_TX_CONTROL__Tx11Halt                                     27
#define   O_TX_CONTROL__Tx10Halt                                     26
#define   O_TX_CONTROL__Tx9Halt                                      25
#define   O_TX_CONTROL__Tx8Halt                                      24
#define   O_TX_CONTROL__Tx7Halt                                      23
#define   O_TX_CONTROL__Tx6Halt                                      22
#define   O_TX_CONTROL__Tx5Halt                                      21
#define   O_TX_CONTROL__Tx4Halt                                      20
#define   O_TX_CONTROL__Tx3Halt                                      19
#define   O_TX_CONTROL__Tx2Halt                                      18
#define   O_TX_CONTROL__Tx1Halt                                      17
#define   O_TX_CONTROL__Tx0Halt                                      16
#define   O_TX_CONTROL__TxIdle                                       15
#define   O_TX_CONTROL__TxEnable                                     14
#define   O_TX_CONTROL__TxThreshold                                  0
#define   W_TX_CONTROL__TxThreshold                                  14
#define R_RX_CONTROL                                                 0x0A1
#define   O_RX_CONTROL__RGMII                                        10
#define   O_RX_CONTROL__SoftReset			             2
#define   O_RX_CONTROL__RxHalt                                       1
#define   O_RX_CONTROL__RxEnable                                     0
#define R_DESC_PACK_CTRL                                            0x0A2
#define   O_DESC_PACK_CTRL__ByteOffset                              17
#define   W_DESC_PACK_CTRL__ByteOffset                              3
#define   O_DESC_PACK_CTRL__PrePadEnable                            16
#define   O_DESC_PACK_CTRL__MaxEntry                                14
#define   W_DESC_PACK_CTRL__MaxEntry                                2
#define   O_DESC_PACK_CTRL__RegularSize                             0
#define   W_DESC_PACK_CTRL__RegularSize                             14
#define R_STATCTRL                                                  0x0A3
#define   O_STATCTRL__OverFlowEn                                    4
#define   O_STATCTRL__GIG                                           3
#define   O_STATCTRL__Sten                                          2
#define   O_STATCTRL__ClrCnt                                        1
#define   O_STATCTRL__AutoZ                                         0
#define R_L2ALLOCCTRL                                               0x0A4
#define   O_L2ALLOCCTRL__TxL2Allocate                               9
#define   W_L2ALLOCCTRL__TxL2Allocate                               9
#define   O_L2ALLOCCTRL__RxL2Allocate                               0
#define   W_L2ALLOCCTRL__RxL2Allocate                               9
#define R_INTMASK                                                   0x0A5
#define   O_INTMASK__Spi4TxError                                     28
#define   O_INTMASK__Spi4RxError                                     27
#define   O_INTMASK__RGMIIHalfDupCollision                           27
#define   O_INTMASK__Abort                                           26
#define   O_INTMASK__Underrun                                        25
#define   O_INTMASK__DiscardPacket                                   24
#define   O_INTMASK__AsyncFifoFull                                   23
#define   O_INTMASK__TagFull                                         22
#define   O_INTMASK__Class3Full                                      21
#define   O_INTMASK__C3EarlyFull                                     20
#define   O_INTMASK__Class2Full                                      19
#define   O_INTMASK__C2EarlyFull                                     18
#define   O_INTMASK__Class1Full                                      17
#define   O_INTMASK__C1EarlyFull                                     16
#define   O_INTMASK__Class0Full                                      15
#define   O_INTMASK__C0EarlyFull                                     14
#define   O_INTMASK__RxDataFull                                      13
#define   O_INTMASK__RxEarlyFull                                     12
#define   O_INTMASK__RFreeEmpty                                      9
#define   O_INTMASK__RFEarlyEmpty                                    8
#define   O_INTMASK__P2PSpillEcc                                     7
#define   O_INTMASK__FreeDescFull                                    5
#define   O_INTMASK__FreeEarlyFull                                   4
#define   O_INTMASK__TxFetchError                                    3
#define   O_INTMASK__StatCarry                                       2
#define   O_INTMASK__MDInt                                           1
#define   O_INTMASK__TxIllegal                                       0
#define R_INTREG                                                    0x0A6
#define   O_INTREG__Spi4TxError                                     28
#define   O_INTREG__Spi4RxError                                     27
#define   O_INTREG__RGMIIHalfDupCollision                           27
#define   O_INTREG__Abort                                           26
#define   O_INTREG__Underrun                                        25
#define   O_INTREG__DiscardPacket                                   24
#define   O_INTREG__AsyncFifoFull                                   23
#define   O_INTREG__TagFull                                         22
#define   O_INTREG__Class3Full                                      21
#define   O_INTREG__C3EarlyFull                                     20
#define   O_INTREG__Class2Full                                      19
#define   O_INTREG__C2EarlyFull                                     18
#define   O_INTREG__Class1Full                                      17
#define   O_INTREG__C1EarlyFull                                     16
#define   O_INTREG__Class0Full                                      15
#define   O_INTREG__C0EarlyFull                                     14
#define   O_INTREG__RxDataFull                                      13
#define   O_INTREG__RxEarlyFull                                     12
#define   O_INTREG__RFreeEmpty                                      9
#define   O_INTREG__RFEarlyEmpty                                    8
#define   O_INTREG__P2PSpillEcc                                     7
#define   O_INTREG__FreeDescFull                                    5
#define   O_INTREG__FreeEarlyFull                                   4
#define   O_INTREG__TxFetchError                                    3
#define   O_INTREG__StatCarry                                       2
#define   O_INTREG__MDInt                                           1
#define   O_INTREG__TxIllegal                                       0
#define R_TXRETRY                                                   0x0A7
#define   O_TXRETRY__CollisionRetry                                 6
#define   O_TXRETRY__BusErrorRetry                                  5
#define   O_TXRETRY__UnderRunRetry                                  4
#define   O_TXRETRY__Retries                                        0
#define   W_TXRETRY__Retries                                        4
#define R_CORECONTROL                                               0x0A8
#define   O_CORECONTROL__ErrorThread                                4
#define   W_CORECONTROL__ErrorThread                                7
#define   O_CORECONTROL__Shutdown                                   2
#define   O_CORECONTROL__Speed                                      0
#define   W_CORECONTROL__Speed                                      2
#define R_BYTEOFFSET0                                               0x0A9
#define R_BYTEOFFSET1                                               0x0AA
#define R_L2TYPE_0                                                  0x0F0
#define   O_L2TYPE__ExtraHdrProtoSize                               26
#define   W_L2TYPE__ExtraHdrProtoSize                               5
#define   O_L2TYPE__ExtraHdrProtoOffset                             20
#define   W_L2TYPE__ExtraHdrProtoOffset                             6
#define   O_L2TYPE__ExtraHeaderSize                                 14
#define   W_L2TYPE__ExtraHeaderSize                                 6
#define   O_L2TYPE__ProtoOffset                                     8
#define   W_L2TYPE__ProtoOffset                                     6
#define   O_L2TYPE__L2HdrOffset                                     2
#define   W_L2TYPE__L2HdrOffset                                     6
#define   O_L2TYPE__L2Proto                                         0
#define   W_L2TYPE__L2Proto                                         2
#define R_L2TYPE_1                                                  0xF0
#define R_L2TYPE_2                                                  0xF0
#define R_L2TYPE_3                                                  0xF0
#define R_PARSERCONFIGREG                                           0x100
#define   O_PARSERCONFIGREG__CRCHashPoly                            8
#define   W_PARSERCONFIGREG__CRCHashPoly                            7
#define   O_PARSERCONFIGREG__PrePadOffset                           4
#define   W_PARSERCONFIGREG__PrePadOffset                           4
#define   O_PARSERCONFIGREG__UseCAM                                 2
#define   O_PARSERCONFIGREG__UseHASH                                1
#define   O_PARSERCONFIGREG__UseProto                               0
#define R_L3CTABLE                                                  0x140
#define   O_L3CTABLE__Offset0                                       25
#define   W_L3CTABLE__Offset0                                       7
#define   O_L3CTABLE__Len0                                          21
#define   W_L3CTABLE__Len0                                          4
#define   O_L3CTABLE__Offset1                                       14
#define   W_L3CTABLE__Offset1                                       7
#define   O_L3CTABLE__Len1                                          10
#define   W_L3CTABLE__Len1                                          4
#define   O_L3CTABLE__Offset2                                       4
#define   W_L3CTABLE__Offset2                                       6
#define   O_L3CTABLE__Len2                                          0
#define   W_L3CTABLE__Len2                                          4
#define   O_L3CTABLE__L3HdrOffset                                   26
#define   W_L3CTABLE__L3HdrOffset                                   6
#define   O_L3CTABLE__L4ProtoOffset                                 20
#define   W_L3CTABLE__L4ProtoOffset                                 6
#define   O_L3CTABLE__IPChksumCompute                               19
#define   O_L3CTABLE__L4Classify                                    18
#define   O_L3CTABLE__L2Proto                                       16
#define   W_L3CTABLE__L2Proto                                       2
#define   O_L3CTABLE__L3ProtoKey                                    0
#define   W_L3CTABLE__L3ProtoKey                                    16
#define R_L4CTABLE                                                  0x160
#define   O_L4CTABLE__Offset0                                       21
#define   W_L4CTABLE__Offset0                                       6
#define   O_L4CTABLE__Len0                                          17
#define   W_L4CTABLE__Len0                                          4
#define   O_L4CTABLE__Offset1                                       11
#define   W_L4CTABLE__Offset1                                       6
#define   O_L4CTABLE__Len1                                          7
#define   W_L4CTABLE__Len1                                          4
#define   O_L4CTABLE__TCPChksumEnable                               0
#define R_CAM4X128TABLE                                             0x172
#define   O_CAM4X128TABLE__ClassId                                  7
#define   W_CAM4X128TABLE__ClassId                                  2
#define   O_CAM4X128TABLE__BucketId                                 1
#define   W_CAM4X128TABLE__BucketId                                 6
#define   O_CAM4X128TABLE__UseBucket                                0
#define R_CAM4X128KEY                                               0x180
#define R_TRANSLATETABLE                                            0x1A0
#define R_DMACR0                                                    0x200
#define   O_DMACR0__Data0WrMaxCr                                    27
#define   W_DMACR0__Data0WrMaxCr                                    3
#define   O_DMACR0__Data0RdMaxCr                                    24
#define   W_DMACR0__Data0RdMaxCr                                    3
#define   O_DMACR0__Data1WrMaxCr                                    21
#define   W_DMACR0__Data1WrMaxCr                                    3
#define   O_DMACR0__Data1RdMaxCr                                    18
#define   W_DMACR0__Data1RdMaxCr                                    3
#define   O_DMACR0__Data2WrMaxCr                                    15
#define   W_DMACR0__Data2WrMaxCr                                    3
#define   O_DMACR0__Data2RdMaxCr                                    12
#define   W_DMACR0__Data2RdMaxCr                                    3
#define   O_DMACR0__Data3WrMaxCr                                    9
#define   W_DMACR0__Data3WrMaxCr                                    3
#define   O_DMACR0__Data3RdMaxCr                                    6
#define   W_DMACR0__Data3RdMaxCr                                    3
#define   O_DMACR0__Data4WrMaxCr                                    3
#define   W_DMACR0__Data4WrMaxCr                                    3
#define   O_DMACR0__Data4RdMaxCr                                    0
#define   W_DMACR0__Data4RdMaxCr                                    3
#define R_DMACR1                                                    0x201
#define   O_DMACR1__Data5WrMaxCr                                    27
#define   W_DMACR1__Data5WrMaxCr                                    3
#define   O_DMACR1__Data5RdMaxCr                                    24
#define   W_DMACR1__Data5RdMaxCr                                    3
#define   O_DMACR1__Data6WrMaxCr                                    21
#define   W_DMACR1__Data6WrMaxCr                                    3
#define   O_DMACR1__Data6RdMaxCr                                    18
#define   W_DMACR1__Data6RdMaxCr                                    3
#define   O_DMACR1__Data7WrMaxCr                                    15
#define   W_DMACR1__Data7WrMaxCr                                    3
#define   O_DMACR1__Data7RdMaxCr                                    12
#define   W_DMACR1__Data7RdMaxCr                                    3
#define   O_DMACR1__Data8WrMaxCr                                    9
#define   W_DMACR1__Data8WrMaxCr                                    3
#define   O_DMACR1__Data8RdMaxCr                                    6
#define   W_DMACR1__Data8RdMaxCr                                    3
#define   O_DMACR1__Data9WrMaxCr                                    3
#define   W_DMACR1__Data9WrMaxCr                                    3
#define   O_DMACR1__Data9RdMaxCr                                    0
#define   W_DMACR1__Data9RdMaxCr                                    3
#define R_DMACR2                                                    0x202
#define   O_DMACR2__Data10WrMaxCr                                   27
#define   W_DMACR2__Data10WrMaxCr                                   3
#define   O_DMACR2__Data10RdMaxCr                                   24
#define   W_DMACR2__Data10RdMaxCr                                   3
#define   O_DMACR2__Data11WrMaxCr                                   21
#define   W_DMACR2__Data11WrMaxCr                                   3
#define   O_DMACR2__Data11RdMaxCr                                   18
#define   W_DMACR2__Data11RdMaxCr                                   3
#define   O_DMACR2__Data12WrMaxCr                                   15
#define   W_DMACR2__Data12WrMaxCr                                   3
#define   O_DMACR2__Data12RdMaxCr                                   12
#define   W_DMACR2__Data12RdMaxCr                                   3
#define   O_DMACR2__Data13WrMaxCr                                   9
#define   W_DMACR2__Data13WrMaxCr                                   3
#define   O_DMACR2__Data13RdMaxCr                                   6
#define   W_DMACR2__Data13RdMaxCr                                   3
#define   O_DMACR2__Data14WrMaxCr                                   3
#define   W_DMACR2__Data14WrMaxCr                                   3
#define   O_DMACR2__Data14RdMaxCr                                   0
#define   W_DMACR2__Data14RdMaxCr                                   3
#define R_DMACR3                                                    0x203
#define   O_DMACR3__Data15WrMaxCr                                   27
#define   W_DMACR3__Data15WrMaxCr                                   3
#define   O_DMACR3__Data15RdMaxCr                                   24
#define   W_DMACR3__Data15RdMaxCr                                   3
#define   O_DMACR3__SpClassWrMaxCr                                  21
#define   W_DMACR3__SpClassWrMaxCr                                  3
#define   O_DMACR3__SpClassRdMaxCr                                  18
#define   W_DMACR3__SpClassRdMaxCr                                  3
#define   O_DMACR3__JumFrInWrMaxCr                                  15
#define   W_DMACR3__JumFrInWrMaxCr                                  3
#define   O_DMACR3__JumFrInRdMaxCr                                  12
#define   W_DMACR3__JumFrInRdMaxCr                                  3
#define   O_DMACR3__RegFrInWrMaxCr                                  9
#define   W_DMACR3__RegFrInWrMaxCr                                  3
#define   O_DMACR3__RegFrInRdMaxCr                                  6
#define   W_DMACR3__RegFrInRdMaxCr                                  3
#define   O_DMACR3__FrOutWrMaxCr                                    3
#define   W_DMACR3__FrOutWrMaxCr                                    3
#define   O_DMACR3__FrOutRdMaxCr                                    0
#define   W_DMACR3__FrOutRdMaxCr                                    3
#define R_REG_FRIN_SPILL_MEM_START_0                                0x204
#define   O_REG_FRIN_SPILL_MEM_START_0__RegFrInSpillMemStart0        0
#define   W_REG_FRIN_SPILL_MEM_START_0__RegFrInSpillMemStart0       32
#define R_REG_FRIN_SPILL_MEM_START_1                                0x205
#define   O_REG_FRIN_SPILL_MEM_START_1__RegFrInSpillMemStart1        0
#define   W_REG_FRIN_SPILL_MEM_START_1__RegFrInSpillMemStart1        3
#define R_REG_FRIN_SPILL_MEM_SIZE                                   0x206
#define   O_REG_FRIN_SPILL_MEM_SIZE__RegFrInSpillMemSize             0
#define   W_REG_FRIN_SPILL_MEM_SIZE__RegFrInSpillMemSize            32
#define R_FROUT_SPILL_MEM_START_0                                   0x207
#define   O_FROUT_SPILL_MEM_START_0__FrOutSpillMemStart0             0
#define   W_FROUT_SPILL_MEM_START_0__FrOutSpillMemStart0            32
#define R_FROUT_SPILL_MEM_START_1                                   0x208
#define   O_FROUT_SPILL_MEM_START_1__FrOutSpillMemStart1             0
#define   W_FROUT_SPILL_MEM_START_1__FrOutSpillMemStart1             3
#define R_FROUT_SPILL_MEM_SIZE                                      0x209
#define   O_FROUT_SPILL_MEM_SIZE__FrOutSpillMemSize                  0
#define   W_FROUT_SPILL_MEM_SIZE__FrOutSpillMemSize                 32
#define R_CLASS0_SPILL_MEM_START_0                                  0x20A
#define   O_CLASS0_SPILL_MEM_START_0__Class0SpillMemStart0           0
#define   W_CLASS0_SPILL_MEM_START_0__Class0SpillMemStart0          32
#define R_CLASS0_SPILL_MEM_START_1                                  0x20B
#define   O_CLASS0_SPILL_MEM_START_1__Class0SpillMemStart1           0
#define   W_CLASS0_SPILL_MEM_START_1__Class0SpillMemStart1           3
#define R_CLASS0_SPILL_MEM_SIZE                                     0x20C
#define   O_CLASS0_SPILL_MEM_SIZE__Class0SpillMemSize                0
#define   W_CLASS0_SPILL_MEM_SIZE__Class0SpillMemSize               32
#define R_JUMFRIN_SPILL_MEM_START_0                                 0x20D
#define   O_JUMFRIN_SPILL_MEM_START_0__JumFrInSpillMemStar0          0
#define   W_JUMFRIN_SPILL_MEM_START_0__JumFrInSpillMemStar0         32
#define R_JUMFRIN_SPILL_MEM_START_1                                 0x20E
#define   O_JUMFRIN_SPILL_MEM_START_1__JumFrInSpillMemStart1         0
#define   W_JUMFRIN_SPILL_MEM_START_1__JumFrInSpillMemStart1         3
#define R_JUMFRIN_SPILL_MEM_SIZE                                    0x20F
#define   O_JUMFRIN_SPILL_MEM_SIZE__JumFrInSpillMemSize              0
#define   W_JUMFRIN_SPILL_MEM_SIZE__JumFrInSpillMemSize             32
#define R_CLASS1_SPILL_MEM_START_0                                  0x210
#define   O_CLASS1_SPILL_MEM_START_0__Class1SpillMemStart0           0
#define   W_CLASS1_SPILL_MEM_START_0__Class1SpillMemStart0          32
#define R_CLASS1_SPILL_MEM_START_1                                  0x211
#define   O_CLASS1_SPILL_MEM_START_1__Class1SpillMemStart1           0
#define   W_CLASS1_SPILL_MEM_START_1__Class1SpillMemStart1           3
#define R_CLASS1_SPILL_MEM_SIZE                                     0x212
#define   O_CLASS1_SPILL_MEM_SIZE__Class1SpillMemSize                0
#define   W_CLASS1_SPILL_MEM_SIZE__Class1SpillMemSize               32
#define R_CLASS2_SPILL_MEM_START_0                                  0x213
#define   O_CLASS2_SPILL_MEM_START_0__Class2SpillMemStart0           0
#define   W_CLASS2_SPILL_MEM_START_0__Class2SpillMemStart0          32
#define R_CLASS2_SPILL_MEM_START_1                                  0x214
#define   O_CLASS2_SPILL_MEM_START_1__Class2SpillMemStart1           0
#define   W_CLASS2_SPILL_MEM_START_1__Class2SpillMemStart1           3
#define R_CLASS2_SPILL_MEM_SIZE                                     0x215
#define   O_CLASS2_SPILL_MEM_SIZE__Class2SpillMemSize                0
#define   W_CLASS2_SPILL_MEM_SIZE__Class2SpillMemSize               32
#define R_CLASS3_SPILL_MEM_START_0                                  0x216
#define   O_CLASS3_SPILL_MEM_START_0__Class3SpillMemStart0           0
#define   W_CLASS3_SPILL_MEM_START_0__Class3SpillMemStart0          32
#define R_CLASS3_SPILL_MEM_START_1                                  0x217
#define   O_CLASS3_SPILL_MEM_START_1__Class3SpillMemStart1           0
#define   W_CLASS3_SPILL_MEM_START_1__Class3SpillMemStart1           3
#define R_CLASS3_SPILL_MEM_SIZE                                     0x218
#define   O_CLASS3_SPILL_MEM_SIZE__Class3SpillMemSize                0
#define   W_CLASS3_SPILL_MEM_SIZE__Class3SpillMemSize               32
#define R_REG_FRIN1_SPILL_MEM_START_0                               0x219
#define R_REG_FRIN1_SPILL_MEM_START_1                               0x21a
#define R_REG_FRIN1_SPILL_MEM_SIZE                                  0x21b
#define R_SPIHNGY0                                                  0x219
#define   O_SPIHNGY0__EG_HNGY_THRESH_0                              24
#define   W_SPIHNGY0__EG_HNGY_THRESH_0                              7
#define   O_SPIHNGY0__EG_HNGY_THRESH_1                              16
#define   W_SPIHNGY0__EG_HNGY_THRESH_1                              7
#define   O_SPIHNGY0__EG_HNGY_THRESH_2                              8
#define   W_SPIHNGY0__EG_HNGY_THRESH_2                              7
#define   O_SPIHNGY0__EG_HNGY_THRESH_3                              0
#define   W_SPIHNGY0__EG_HNGY_THRESH_3                              7
#define R_SPIHNGY1                                                  0x21A
#define   O_SPIHNGY1__EG_HNGY_THRESH_4                              24
#define   W_SPIHNGY1__EG_HNGY_THRESH_4                              7
#define   O_SPIHNGY1__EG_HNGY_THRESH_5                              16
#define   W_SPIHNGY1__EG_HNGY_THRESH_5                              7
#define   O_SPIHNGY1__EG_HNGY_THRESH_6                              8
#define   W_SPIHNGY1__EG_HNGY_THRESH_6                              7
#define   O_SPIHNGY1__EG_HNGY_THRESH_7                              0
#define   W_SPIHNGY1__EG_HNGY_THRESH_7                              7
#define R_SPIHNGY2                                                  0x21B
#define   O_SPIHNGY2__EG_HNGY_THRESH_8                              24
#define   W_SPIHNGY2__EG_HNGY_THRESH_8                              7
#define   O_SPIHNGY2__EG_HNGY_THRESH_9                              16
#define   W_SPIHNGY2__EG_HNGY_THRESH_9                              7
#define   O_SPIHNGY2__EG_HNGY_THRESH_10                             8
#define   W_SPIHNGY2__EG_HNGY_THRESH_10                             7
#define   O_SPIHNGY2__EG_HNGY_THRESH_11                             0
#define   W_SPIHNGY2__EG_HNGY_THRESH_11                             7
#define R_SPIHNGY3                                                  0x21C
#define   O_SPIHNGY3__EG_HNGY_THRESH_12                             24
#define   W_SPIHNGY3__EG_HNGY_THRESH_12                             7
#define   O_SPIHNGY3__EG_HNGY_THRESH_13                             16
#define   W_SPIHNGY3__EG_HNGY_THRESH_13                             7
#define   O_SPIHNGY3__EG_HNGY_THRESH_14                             8
#define   W_SPIHNGY3__EG_HNGY_THRESH_14                             7
#define   O_SPIHNGY3__EG_HNGY_THRESH_15                             0
#define   W_SPIHNGY3__EG_HNGY_THRESH_15                             7
#define R_SPISTRV0                                                  0x21D
#define   O_SPISTRV0__EG_STRV_THRESH_0                              24
#define   W_SPISTRV0__EG_STRV_THRESH_0                              7
#define   O_SPISTRV0__EG_STRV_THRESH_1                              16
#define   W_SPISTRV0__EG_STRV_THRESH_1                              7
#define   O_SPISTRV0__EG_STRV_THRESH_2                              8
#define   W_SPISTRV0__EG_STRV_THRESH_2                              7
#define   O_SPISTRV0__EG_STRV_THRESH_3                              0
#define   W_SPISTRV0__EG_STRV_THRESH_3                              7
#define R_SPISTRV1                                                  0x21E
#define   O_SPISTRV1__EG_STRV_THRESH_4                              24
#define   W_SPISTRV1__EG_STRV_THRESH_4                              7
#define   O_SPISTRV1__EG_STRV_THRESH_5                              16
#define   W_SPISTRV1__EG_STRV_THRESH_5                              7
#define   O_SPISTRV1__EG_STRV_THRESH_6                              8
#define   W_SPISTRV1__EG_STRV_THRESH_6                              7
#define   O_SPISTRV1__EG_STRV_THRESH_7                              0
#define   W_SPISTRV1__EG_STRV_THRESH_7                              7
#define R_SPISTRV2                                                  0x21F
#define   O_SPISTRV2__EG_STRV_THRESH_8                              24
#define   W_SPISTRV2__EG_STRV_THRESH_8                              7
#define   O_SPISTRV2__EG_STRV_THRESH_9                              16
#define   W_SPISTRV2__EG_STRV_THRESH_9                              7
#define   O_SPISTRV2__EG_STRV_THRESH_10                             8
#define   W_SPISTRV2__EG_STRV_THRESH_10                             7
#define   O_SPISTRV2__EG_STRV_THRESH_11                             0
#define   W_SPISTRV2__EG_STRV_THRESH_11                             7
#define R_SPISTRV3                                                  0x220
#define   O_SPISTRV3__EG_STRV_THRESH_12                             24
#define   W_SPISTRV3__EG_STRV_THRESH_12                             7
#define   O_SPISTRV3__EG_STRV_THRESH_13                             16
#define   W_SPISTRV3__EG_STRV_THRESH_13                             7
#define   O_SPISTRV3__EG_STRV_THRESH_14                             8
#define   W_SPISTRV3__EG_STRV_THRESH_14                             7
#define   O_SPISTRV3__EG_STRV_THRESH_15                             0
#define   W_SPISTRV3__EG_STRV_THRESH_15                             7
#define R_TXDATAFIFO0                                               0x221
#define   O_TXDATAFIFO0__Tx0DataFifoStart                           24
#define   W_TXDATAFIFO0__Tx0DataFifoStart                           7
#define   O_TXDATAFIFO0__Tx0DataFifoSize                            16
#define   W_TXDATAFIFO0__Tx0DataFifoSize                            7
#define   O_TXDATAFIFO0__Tx1DataFifoStart                           8
#define   W_TXDATAFIFO0__Tx1DataFifoStart                           7
#define   O_TXDATAFIFO0__Tx1DataFifoSize                            0
#define   W_TXDATAFIFO0__Tx1DataFifoSize                            7
#define R_TXDATAFIFO1                                               0x222
#define   O_TXDATAFIFO1__Tx2DataFifoStart                           24
#define   W_TXDATAFIFO1__Tx2DataFifoStart                           7
#define   O_TXDATAFIFO1__Tx2DataFifoSize                            16
#define   W_TXDATAFIFO1__Tx2DataFifoSize                            7
#define   O_TXDATAFIFO1__Tx3DataFifoStart                           8
#define   W_TXDATAFIFO1__Tx3DataFifoStart                           7
#define   O_TXDATAFIFO1__Tx3DataFifoSize                            0
#define   W_TXDATAFIFO1__Tx3DataFifoSize                            7
#define R_TXDATAFIFO2                                               0x223
#define   O_TXDATAFIFO2__Tx4DataFifoStart                           24
#define   W_TXDATAFIFO2__Tx4DataFifoStart                           7
#define   O_TXDATAFIFO2__Tx4DataFifoSize                            16
#define   W_TXDATAFIFO2__Tx4DataFifoSize                            7
#define   O_TXDATAFIFO2__Tx5DataFifoStart                           8
#define   W_TXDATAFIFO2__Tx5DataFifoStart                           7
#define   O_TXDATAFIFO2__Tx5DataFifoSize                            0
#define   W_TXDATAFIFO2__Tx5DataFifoSize                            7
#define R_TXDATAFIFO3                                               0x224
#define   O_TXDATAFIFO3__Tx6DataFifoStart                           24
#define   W_TXDATAFIFO3__Tx6DataFifoStart                           7
#define   O_TXDATAFIFO3__Tx6DataFifoSize                            16
#define   W_TXDATAFIFO3__Tx6DataFifoSize                            7
#define   O_TXDATAFIFO3__Tx7DataFifoStart                           8
#define   W_TXDATAFIFO3__Tx7DataFifoStart                           7
#define   O_TXDATAFIFO3__Tx7DataFifoSize                            0
#define   W_TXDATAFIFO3__Tx7DataFifoSize                            7
#define R_TXDATAFIFO4                                               0x225
#define   O_TXDATAFIFO4__Tx8DataFifoStart                           24
#define   W_TXDATAFIFO4__Tx8DataFifoStart                           7
#define   O_TXDATAFIFO4__Tx8DataFifoSize                            16
#define   W_TXDATAFIFO4__Tx8DataFifoSize                            7
#define   O_TXDATAFIFO4__Tx9DataFifoStart                           8
#define   W_TXDATAFIFO4__Tx9DataFifoStart                           7
#define   O_TXDATAFIFO4__Tx9DataFifoSize                            0
#define   W_TXDATAFIFO4__Tx9DataFifoSize                            7
#define R_TXDATAFIFO5                                               0x226
#define   O_TXDATAFIFO5__Tx10DataFifoStart                          24
#define   W_TXDATAFIFO5__Tx10DataFifoStart                          7
#define   O_TXDATAFIFO5__Tx10DataFifoSize                           16
#define   W_TXDATAFIFO5__Tx10DataFifoSize                           7
#define   O_TXDATAFIFO5__Tx11DataFifoStart                          8
#define   W_TXDATAFIFO5__Tx11DataFifoStart                          7
#define   O_TXDATAFIFO5__Tx11DataFifoSize                           0
#define   W_TXDATAFIFO5__Tx11DataFifoSize                           7
#define R_TXDATAFIFO6                                               0x227
#define   O_TXDATAFIFO6__Tx12DataFifoStart                          24
#define   W_TXDATAFIFO6__Tx12DataFifoStart                          7
#define   O_TXDATAFIFO6__Tx12DataFifoSize                           16
#define   W_TXDATAFIFO6__Tx12DataFifoSize                           7
#define   O_TXDATAFIFO6__Tx13DataFifoStart                          8
#define   W_TXDATAFIFO6__Tx13DataFifoStart                          7
#define   O_TXDATAFIFO6__Tx13DataFifoSize                           0
#define   W_TXDATAFIFO6__Tx13DataFifoSize                           7
#define R_TXDATAFIFO7                                               0x228
#define   O_TXDATAFIFO7__Tx14DataFifoStart                          24
#define   W_TXDATAFIFO7__Tx14DataFifoStart                          7
#define   O_TXDATAFIFO7__Tx14DataFifoSize                           16
#define   W_TXDATAFIFO7__Tx14DataFifoSize                           7
#define   O_TXDATAFIFO7__Tx15DataFifoStart                          8
#define   W_TXDATAFIFO7__Tx15DataFifoStart                          7
#define   O_TXDATAFIFO7__Tx15DataFifoSize                           0
#define   W_TXDATAFIFO7__Tx15DataFifoSize                           7
#define R_RXDATAFIFO0                                               0x229
#define   O_RXDATAFIFO0__Rx0DataFifoStart                           24
#define   W_RXDATAFIFO0__Rx0DataFifoStart                           7
#define   O_RXDATAFIFO0__Rx0DataFifoSize                            16
#define   W_RXDATAFIFO0__Rx0DataFifoSize                            7
#define   O_RXDATAFIFO0__Rx1DataFifoStart                           8
#define   W_RXDATAFIFO0__Rx1DataFifoStart                           7
#define   O_RXDATAFIFO0__Rx1DataFifoSize                            0
#define   W_RXDATAFIFO0__Rx1DataFifoSize                            7
#define R_RXDATAFIFO1                                               0x22A
#define   O_RXDATAFIFO1__Rx2DataFifoStart                           24
#define   W_RXDATAFIFO1__Rx2DataFifoStart                           7
#define   O_RXDATAFIFO1__Rx2DataFifoSize                            16
#define   W_RXDATAFIFO1__Rx2DataFifoSize                            7
#define   O_RXDATAFIFO1__Rx3DataFifoStart                           8
#define   W_RXDATAFIFO1__Rx3DataFifoStart                           7
#define   O_RXDATAFIFO1__Rx3DataFifoSize                            0
#define   W_RXDATAFIFO1__Rx3DataFifoSize                            7
#define R_RXDATAFIFO2                                               0x22B
#define   O_RXDATAFIFO2__Rx4DataFifoStart                           24
#define   W_RXDATAFIFO2__Rx4DataFifoStart                           7
#define   O_RXDATAFIFO2__Rx4DataFifoSize                            16
#define   W_RXDATAFIFO2__Rx4DataFifoSize                            7
#define   O_RXDATAFIFO2__Rx5DataFifoStart                           8
#define   W_RXDATAFIFO2__Rx5DataFifoStart                           7
#define   O_RXDATAFIFO2__Rx5DataFifoSize                            0
#define   W_RXDATAFIFO2__Rx5DataFifoSize                            7
#define R_RXDATAFIFO3                                               0x22C
#define   O_RXDATAFIFO3__Rx6DataFifoStart                           24
#define   W_RXDATAFIFO3__Rx6DataFifoStart                           7
#define   O_RXDATAFIFO3__Rx6DataFifoSize                            16
#define   W_RXDATAFIFO3__Rx6DataFifoSize                            7
#define   O_RXDATAFIFO3__Rx7DataFifoStart                           8
#define   W_RXDATAFIFO3__Rx7DataFifoStart                           7
#define   O_RXDATAFIFO3__Rx7DataFifoSize                            0
#define   W_RXDATAFIFO3__Rx7DataFifoSize                            7
#define R_RXDATAFIFO4                                               0x22D
#define   O_RXDATAFIFO4__Rx8DataFifoStart                           24
#define   W_RXDATAFIFO4__Rx8DataFifoStart                           7
#define   O_RXDATAFIFO4__Rx8DataFifoSize                            16
#define   W_RXDATAFIFO4__Rx8DataFifoSize                            7
#define   O_RXDATAFIFO4__Rx9DataFifoStart                           8
#define   W_RXDATAFIFO4__Rx9DataFifoStart                           7
#define   O_RXDATAFIFO4__Rx9DataFifoSize                            0
#define   W_RXDATAFIFO4__Rx9DataFifoSize                            7
#define R_RXDATAFIFO5                                               0x22E
#define   O_RXDATAFIFO5__Rx10DataFifoStart                          24
#define   W_RXDATAFIFO5__Rx10DataFifoStart                          7
#define   O_RXDATAFIFO5__Rx10DataFifoSize                           16
#define   W_RXDATAFIFO5__Rx10DataFifoSize                           7
#define   O_RXDATAFIFO5__Rx11DataFifoStart                          8
#define   W_RXDATAFIFO5__Rx11DataFifoStart                          7
#define   O_RXDATAFIFO5__Rx11DataFifoSize                           0
#define   W_RXDATAFIFO5__Rx11DataFifoSize                           7
#define R_RXDATAFIFO6                                               0x22F
#define   O_RXDATAFIFO6__Rx12DataFifoStart                          24
#define   W_RXDATAFIFO6__Rx12DataFifoStart                          7
#define   O_RXDATAFIFO6__Rx12DataFifoSize                           16
#define   W_RXDATAFIFO6__Rx12DataFifoSize                           7
#define   O_RXDATAFIFO6__Rx13DataFifoStart                          8
#define   W_RXDATAFIFO6__Rx13DataFifoStart                          7
#define   O_RXDATAFIFO6__Rx13DataFifoSize                           0
#define   W_RXDATAFIFO6__Rx13DataFifoSize                           7
#define R_RXDATAFIFO7                                               0x230
#define   O_RXDATAFIFO7__Rx14DataFifoStart                          24
#define   W_RXDATAFIFO7__Rx14DataFifoStart                          7
#define   O_RXDATAFIFO7__Rx14DataFifoSize                           16
#define   W_RXDATAFIFO7__Rx14DataFifoSize                           7
#define   O_RXDATAFIFO7__Rx15DataFifoStart                          8
#define   W_RXDATAFIFO7__Rx15DataFifoStart                          7
#define   O_RXDATAFIFO7__Rx15DataFifoSize                           0
#define   W_RXDATAFIFO7__Rx15DataFifoSize                           7
#define R_XGMACPADCALIBRATION                                       0x231
#define R_FREEQCARVE                                                0x233
#define R_SPI4STATICDELAY0                                          0x240
#define   O_SPI4STATICDELAY0__DataLine7                             28
#define   W_SPI4STATICDELAY0__DataLine7                             4
#define   O_SPI4STATICDELAY0__DataLine6                             24
#define   W_SPI4STATICDELAY0__DataLine6                             4
#define   O_SPI4STATICDELAY0__DataLine5                             20
#define   W_SPI4STATICDELAY0__DataLine5                             4
#define   O_SPI4STATICDELAY0__DataLine4                             16
#define   W_SPI4STATICDELAY0__DataLine4                             4
#define   O_SPI4STATICDELAY0__DataLine3                             12
#define   W_SPI4STATICDELAY0__DataLine3                             4
#define   O_SPI4STATICDELAY0__DataLine2                             8
#define   W_SPI4STATICDELAY0__DataLine2                             4
#define   O_SPI4STATICDELAY0__DataLine1                             4
#define   W_SPI4STATICDELAY0__DataLine1                             4
#define   O_SPI4STATICDELAY0__DataLine0                             0
#define   W_SPI4STATICDELAY0__DataLine0                             4
#define R_SPI4STATICDELAY1                                          0x241
#define   O_SPI4STATICDELAY1__DataLine15                            28
#define   W_SPI4STATICDELAY1__DataLine15                            4
#define   O_SPI4STATICDELAY1__DataLine14                            24
#define   W_SPI4STATICDELAY1__DataLine14                            4
#define   O_SPI4STATICDELAY1__DataLine13                            20
#define   W_SPI4STATICDELAY1__DataLine13                            4
#define   O_SPI4STATICDELAY1__DataLine12                            16
#define   W_SPI4STATICDELAY1__DataLine12                            4
#define   O_SPI4STATICDELAY1__DataLine11                            12
#define   W_SPI4STATICDELAY1__DataLine11                            4
#define   O_SPI4STATICDELAY1__DataLine10                            8
#define   W_SPI4STATICDELAY1__DataLine10                            4
#define   O_SPI4STATICDELAY1__DataLine9                             4
#define   W_SPI4STATICDELAY1__DataLine9                             4
#define   O_SPI4STATICDELAY1__DataLine8                             0
#define   W_SPI4STATICDELAY1__DataLine8                             4
#define R_SPI4STATICDELAY2                                          0x242
#define   O_SPI4STATICDELAY0__TxStat1                               8
#define   W_SPI4STATICDELAY0__TxStat1                               4
#define   O_SPI4STATICDELAY0__TxStat0                               4
#define   W_SPI4STATICDELAY0__TxStat0                               4
#define   O_SPI4STATICDELAY0__RxControl                             0
#define   W_SPI4STATICDELAY0__RxControl                             4
#define R_SPI4CONTROL                                               0x243
#define   O_SPI4CONTROL__StaticDelay                                2
#define   O_SPI4CONTROL__LVDS_LVTTL                                 1
#define   O_SPI4CONTROL__SPI4Enable                                 0
#define R_CLASSWATERMARKS                                           0x244
#define   O_CLASSWATERMARKS__Class0Watermark                        24
#define   W_CLASSWATERMARKS__Class0Watermark                        5
#define   O_CLASSWATERMARKS__Class1Watermark                        16
#define   W_CLASSWATERMARKS__Class1Watermark                        5
#define   O_CLASSWATERMARKS__Class3Watermark                        0
#define   W_CLASSWATERMARKS__Class3Watermark                        5
#define R_RXWATERMARKS1                                              0x245
#define   O_RXWATERMARKS__Rx0DataWatermark                          24
#define   W_RXWATERMARKS__Rx0DataWatermark                          7
#define   O_RXWATERMARKS__Rx1DataWatermark                          16
#define   W_RXWATERMARKS__Rx1DataWatermark                          7
#define   O_RXWATERMARKS__Rx3DataWatermark                          0
#define   W_RXWATERMARKS__Rx3DataWatermark                          7
#define R_RXWATERMARKS2                                              0x246
#define   O_RXWATERMARKS__Rx4DataWatermark                          24
#define   W_RXWATERMARKS__Rx4DataWatermark                          7
#define   O_RXWATERMARKS__Rx5DataWatermark                          16
#define   W_RXWATERMARKS__Rx5DataWatermark                          7
#define   O_RXWATERMARKS__Rx6DataWatermark                          8
#define   W_RXWATERMARKS__Rx6DataWatermark                          7
#define   O_RXWATERMARKS__Rx7DataWatermark                          0
#define   W_RXWATERMARKS__Rx7DataWatermark                          7
#define R_RXWATERMARKS3                                              0x247
#define   O_RXWATERMARKS__Rx8DataWatermark                          24
#define   W_RXWATERMARKS__Rx8DataWatermark                          7
#define   O_RXWATERMARKS__Rx9DataWatermark                          16
#define   W_RXWATERMARKS__Rx9DataWatermark                          7
#define   O_RXWATERMARKS__Rx10DataWatermark                         8
#define   W_RXWATERMARKS__Rx10DataWatermark                         7
#define   O_RXWATERMARKS__Rx11DataWatermark                         0
#define   W_RXWATERMARKS__Rx11DataWatermark                         7
#define R_RXWATERMARKS4                                              0x248
#define   O_RXWATERMARKS__Rx12DataWatermark                         24
#define   W_RXWATERMARKS__Rx12DataWatermark                         7
#define   O_RXWATERMARKS__Rx13DataWatermark                         16
#define   W_RXWATERMARKS__Rx13DataWatermark                         7
#define   O_RXWATERMARKS__Rx14DataWatermark                         8
#define   W_RXWATERMARKS__Rx14DataWatermark                         7
#define   O_RXWATERMARKS__Rx15DataWatermark                         0
#define   W_RXWATERMARKS__Rx15DataWatermark                         7
#define R_FREEWATERMARKS                                            0x249
#define   O_FREEWATERMARKS__FreeOutWatermark                        16
#define   W_FREEWATERMARKS__FreeOutWatermark                        16
#define   O_FREEWATERMARKS__JumFrWatermark                          8
#define   W_FREEWATERMARKS__JumFrWatermark                          7
#define   O_FREEWATERMARKS__RegFrWatermark                          0
#define   W_FREEWATERMARKS__RegFrWatermark                          7
#define R_EGRESSFIFOCARVINGSLOTS                                    0x24a

#define CTRL_RES0           0
#define CTRL_RES1           1
#define CTRL_REG_FREE       2
#define CTRL_JUMBO_FREE     3
#define CTRL_CONT           4
#define CTRL_EOP            5
#define CTRL_START          6
#define CTRL_SNGL           7

#define CTRL_B0_NOT_EOP     0
#define CTRL_B0_EOP         1

#define R_ROUND_ROBIN_TABLE                 0
#define R_PDE_CLASS_0                       0x300
#define R_PDE_CLASS_1                       0x302
#define R_PDE_CLASS_2                       0x304
#define R_PDE_CLASS_3                       0x306

#define R_MSG_TX_THRESHOLD                  0x308

#define R_GMAC_JFR0_BUCKET_SIZE              0x320
#define R_GMAC_RFR0_BUCKET_SIZE              0x321
#define R_GMAC_TX0_BUCKET_SIZE              0x322
#define R_GMAC_TX1_BUCKET_SIZE              0x323
#define R_GMAC_TX2_BUCKET_SIZE              0x324
#define R_GMAC_TX3_BUCKET_SIZE              0x325
#define R_GMAC_JFR1_BUCKET_SIZE              0x326
#define R_GMAC_RFR1_BUCKET_SIZE              0x327

#define R_XGS_TX0_BUCKET_SIZE               0x320
#define R_XGS_TX1_BUCKET_SIZE               0x321
#define R_XGS_TX2_BUCKET_SIZE               0x322
#define R_XGS_TX3_BUCKET_SIZE               0x323
#define R_XGS_TX4_BUCKET_SIZE               0x324
#define R_XGS_TX5_BUCKET_SIZE               0x325
#define R_XGS_TX6_BUCKET_SIZE               0x326
#define R_XGS_TX7_BUCKET_SIZE               0x327
#define R_XGS_TX8_BUCKET_SIZE               0x328
#define R_XGS_TX9_BUCKET_SIZE               0x329
#define R_XGS_TX10_BUCKET_SIZE              0x32A
#define R_XGS_TX11_BUCKET_SIZE              0x32B
#define R_XGS_TX12_BUCKET_SIZE              0x32C
#define R_XGS_TX13_BUCKET_SIZE              0x32D
#define R_XGS_TX14_BUCKET_SIZE              0x32E
#define R_XGS_TX15_BUCKET_SIZE              0x32F
#define R_XGS_JFR_BUCKET_SIZE               0x330
#define R_XGS_RFR_BUCKET_SIZE               0x331

#define R_CC_CPU0_0                         0x380
#define R_CC_CPU1_0                         0x388
#define R_CC_CPU2_0                         0x390
#define R_CC_CPU3_0                         0x398
#define R_CC_CPU4_0                         0x3a0
#define R_CC_CPU5_0                         0x3a8
#define R_CC_CPU6_0                         0x3b0
#define R_CC_CPU7_0                         0x3b8

#define XLR_GMAC_BLK_SZ		            (XLR_IO_GMAC_1_OFFSET - \
		XLR_IO_GMAC_0_OFFSET)

/* Constants used for configuring the devices */

#define XLR_FB_STN			6 /* Bucket used for Tx freeback */

#define MAC_B2B_IPG                     88

#define	XLR_NET_PREPAD_LEN		32

/* frame sizes need to be cacheline aligned */
#define MAX_FRAME_SIZE                  (1536 + XLR_NET_PREPAD_LEN)
#define MAX_FRAME_SIZE_JUMBO            9216

#define MAC_SKB_BACK_PTR_SIZE           SMP_CACHE_BYTES
#define MAC_PREPAD                      0
#define BYTE_OFFSET                     2
#define XLR_RX_BUF_SIZE                 (MAX_FRAME_SIZE + BYTE_OFFSET + \
		MAC_PREPAD + MAC_SKB_BACK_PTR_SIZE + SMP_CACHE_BYTES)
#define MAC_CRC_LEN                     4
#define MAX_NUM_MSGRNG_STN_CC           128
#define MAX_MSG_SND_ATTEMPTS		100	/* 13 stns x 4 entry msg/stn +
						   headroom */

#define MAC_FRIN_TO_BE_SENT_THRESHOLD   16

#define MAX_NUM_DESC_SPILL		1024
#define MAX_FRIN_SPILL                  (MAX_NUM_DESC_SPILL << 2)
#define MAX_FROUT_SPILL                 (MAX_NUM_DESC_SPILL << 2)
#define MAX_CLASS_0_SPILL               (MAX_NUM_DESC_SPILL << 2)
#define MAX_CLASS_1_SPILL               (MAX_NUM_DESC_SPILL << 2)
#define MAX_CLASS_2_SPILL               (MAX_NUM_DESC_SPILL << 2)
#define MAX_CLASS_3_SPILL               (MAX_NUM_DESC_SPILL << 2)

enum {
	SGMII_SPEED_10 = 0x00000000,
	SGMII_SPEED_100 = 0x02000000,
	SGMII_SPEED_1000 = 0x04000000,
};

enum tsv_rsv_reg {
	TX_RX_64_BYTE_FRAME = 0x20,
	TX_RX_64_127_BYTE_FRAME,
	TX_RX_128_255_BYTE_FRAME,
	TX_RX_256_511_BYTE_FRAME,
	TX_RX_512_1023_BYTE_FRAME,
	TX_RX_1024_1518_BYTE_FRAME,
	TX_RX_1519_1522_VLAN_BYTE_FRAME,

	RX_BYTE_COUNTER = 0x27,
	RX_PACKET_COUNTER,
	RX_FCS_ERROR_COUNTER,
	RX_MULTICAST_PACKET_COUNTER,
	RX_BROADCAST_PACKET_COUNTER,
	RX_CONTROL_FRAME_PACKET_COUNTER,
	RX_PAUSE_FRAME_PACKET_COUNTER,
	RX_UNKNOWN_OP_CODE_COUNTER,
	RX_ALIGNMENT_ERROR_COUNTER,
	RX_FRAME_LENGTH_ERROR_COUNTER,
	RX_CODE_ERROR_COUNTER,
	RX_CARRIER_SENSE_ERROR_COUNTER,
	RX_UNDERSIZE_PACKET_COUNTER,
	RX_OVERSIZE_PACKET_COUNTER,
	RX_FRAGMENTS_COUNTER,
	RX_JABBER_COUNTER,
	RX_DROP_PACKET_COUNTER,

	TX_BYTE_COUNTER   = 0x38,
	TX_PACKET_COUNTER,
	TX_MULTICAST_PACKET_COUNTER,
	TX_BROADCAST_PACKET_COUNTER,
	TX_PAUSE_CONTROL_FRAME_COUNTER,
	TX_DEFERRAL_PACKET_COUNTER,
	TX_EXCESSIVE_DEFERRAL_PACKET_COUNTER,
	TX_SINGLE_COLLISION_PACKET_COUNTER,
	TX_MULTI_COLLISION_PACKET_COUNTER,
	TX_LATE_COLLISION_PACKET_COUNTER,
	TX_EXCESSIVE_COLLISION_PACKET_COUNTER,
	TX_TOTAL_COLLISION_COUNTER,
	TX_PAUSE_FRAME_HONERED_COUNTER,
	TX_DROP_FRAME_COUNTER,
	TX_JABBER_FRAME_COUNTER,
	TX_FCS_ERROR_COUNTER,
	TX_CONTROL_FRAME_COUNTER,
	TX_OVERSIZE_FRAME_COUNTER,
	TX_UNDERSIZE_FRAME_COUNTER,
	TX_FRAGMENT_FRAME_COUNTER,

	CARRY_REG_1 = 0x4c,
	CARRY_REG_2 = 0x4d,
};

struct xlr_adapter {
	struct net_device *netdev[4];
};

struct xlr_net_priv {
	u32 __iomem *base_addr;
	struct net_device *ndev;
	struct xlr_adapter *adapter;
	struct mii_bus *mii_bus;
	int num_rx_desc;
	int phy_addr;	/* PHY addr on MDIO bus */
	int pcs_id;	/* PCS id on MDIO bus */
	int port_id;	/* Port(gmac/xgmac) number, i.e 0-7 */
	int tx_stnid;
	u32 __iomem *mii_addr;
	u32 __iomem *serdes_addr;
	u32 __iomem *pcs_addr;
	u32 __iomem *gpio_addr;
	int phy_speed;
	int port_type;
	struct timer_list queue_timer;
	int wakeup_q;
	struct platform_device *pdev;
	struct xlr_net_data *nd;

	u64 *frin_spill;
	u64 *frout_spill;
	u64 *class_0_spill;
	u64 *class_1_spill;
	u64 *class_2_spill;
	u64 *class_3_spill;
};

extern void xlr_set_gmac_speed(struct xlr_net_priv *priv);
