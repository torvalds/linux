/* SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause) */
/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
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
#define   O_TX_CONTROL__TX15HALT                                     31
#define   O_TX_CONTROL__TX14HALT                                     30
#define   O_TX_CONTROL__TX13HALT                                     29
#define   O_TX_CONTROL__TX12HALT                                     28
#define   O_TX_CONTROL__TX11HALT                                     27
#define   O_TX_CONTROL__TX10HALT                                     26
#define   O_TX_CONTROL__TX9HALT                                      25
#define   O_TX_CONTROL__TX8HALT                                      24
#define   O_TX_CONTROL__TX7HALT                                      23
#define   O_TX_CONTROL__TX6HALT                                      22
#define   O_TX_CONTROL__TX5HALT                                      21
#define   O_TX_CONTROL__TX4HALT                                      20
#define   O_TX_CONTROL__TX3HALT                                      19
#define   O_TX_CONTROL__TX2HALT                                      18
#define   O_TX_CONTROL__TX1HALT                                      17
#define   O_TX_CONTROL__TX0HALT                                      16
#define   O_TX_CONTROL__TXIDLE                                       15
#define   O_TX_CONTROL__TXENABLE                                     14
#define   O_TX_CONTROL__TXTHRESHOLD                                  0
#define   W_TX_CONTROL__TXTHRESHOLD                                  14
#define R_RX_CONTROL                                                 0x0A1
#define   O_RX_CONTROL__RGMII                                        10
#define   O_RX_CONTROL__SOFTRESET			             2
#define   O_RX_CONTROL__RXHALT                                       1
#define   O_RX_CONTROL__RXENABLE                                     0
#define R_DESC_PACK_CTRL                                            0x0A2
#define   O_DESC_PACK_CTRL__BYTEOFFSET                              17
#define   W_DESC_PACK_CTRL__BYTEOFFSET                              3
#define   O_DESC_PACK_CTRL__PREPADENABLE                            16
#define   O_DESC_PACK_CTRL__MAXENTRY                                14
#define   W_DESC_PACK_CTRL__MAXENTRY                                2
#define   O_DESC_PACK_CTRL__REGULARSIZE                             0
#define   W_DESC_PACK_CTRL__REGULARSIZE                             14
#define R_STATCTRL                                                  0x0A3
#define   O_STATCTRL__OVERFLOWEN                                    4
#define   O_STATCTRL__GIG                                           3
#define   O_STATCTRL__STEN                                          2
#define   O_STATCTRL__CLRCNT                                        1
#define   O_STATCTRL__AUTOZ                                         0
#define R_L2ALLOCCTRL                                               0x0A4
#define   O_L2ALLOCCTRL__TXL2ALLOCATE                               9
#define   W_L2ALLOCCTRL__TXL2ALLOCATE                               9
#define   O_L2ALLOCCTRL__RXL2ALLOCATE                               0
#define   W_L2ALLOCCTRL__RXL2ALLOCATE                               9
#define R_INTMASK                                                   0x0A5
#define   O_INTMASK__SPI4TXERROR                                     28
#define   O_INTMASK__SPI4RXERROR                                     27
#define   O_INTMASK__RGMIIHALFDUPCOLLISION                           27
#define   O_INTMASK__ABORT                                           26
#define   O_INTMASK__UNDERRUN                                        25
#define   O_INTMASK__DISCARDPACKET                                   24
#define   O_INTMASK__ASYNCFIFOFULL                                   23
#define   O_INTMASK__TAGFULL                                         22
#define   O_INTMASK__CLASS3FULL                                      21
#define   O_INTMASK__C3EARLYFULL                                     20
#define   O_INTMASK__CLASS2FULL                                      19
#define   O_INTMASK__C2EARLYFULL                                     18
#define   O_INTMASK__CLASS1FULL                                      17
#define   O_INTMASK__C1EARLYFULL                                     16
#define   O_INTMASK__CLASS0FULL                                      15
#define   O_INTMASK__C0EARLYFULL                                     14
#define   O_INTMASK__RXDATAFULL                                      13
#define   O_INTMASK__RXEARLYFULL                                     12
#define   O_INTMASK__RFREEEMPTY                                      9
#define   O_INTMASK__RFEARLYEMPTY                                    8
#define   O_INTMASK__P2PSPILLECC                                     7
#define   O_INTMASK__FREEDESCFULL                                    5
#define   O_INTMASK__FREEEARLYFULL                                   4
#define   O_INTMASK__TXFETCHERROR                                    3
#define   O_INTMASK__STATCARRY                                       2
#define   O_INTMASK__MDINT                                           1
#define   O_INTMASK__TXILLEGAL                                       0
#define R_INTREG                                                    0x0A6
#define   O_INTREG__SPI4TXERROR                                     28
#define   O_INTREG__SPI4RXERROR                                     27
#define   O_INTREG__RGMIIHALFDUPCOLLISION                           27
#define   O_INTREG__ABORT                                           26
#define   O_INTREG__UNDERRUN                                        25
#define   O_INTREG__DISCARDPACKET                                   24
#define   O_INTREG__ASYNCFIFOFULL                                   23
#define   O_INTREG__TAGFULL                                         22
#define   O_INTREG__CLASS3FULL                                      21
#define   O_INTREG__C3EARLYFULL                                     20
#define   O_INTREG__CLASS2FULL                                      19
#define   O_INTREG__C2EARLYFULL                                     18
#define   O_INTREG__CLASS1FULL                                      17
#define   O_INTREG__C1EARLYFULL                                     16
#define   O_INTREG__CLASS0FULL                                      15
#define   O_INTREG__C0EARLYFULL                                     14
#define   O_INTREG__RXDATAFULL                                      13
#define   O_INTREG__RXEARLYFULL                                     12
#define   O_INTREG__RFREEEMPTY                                      9
#define   O_INTREG__RFEARLYEMPTY                                    8
#define   O_INTREG__P2PSPILLECC                                     7
#define   O_INTREG__FREEDESCFULL                                    5
#define   O_INTREG__FREEEARLYFULL                                   4
#define   O_INTREG__TXFETCHERROR                                    3
#define   O_INTREG__STATCARRY                                       2
#define   O_INTREG__MDINT                                           1
#define   O_INTREG__TXILLEGAL                                       0
#define R_TXRETRY                                                   0x0A7
#define   O_TXRETRY__COLLISIONRETRY                                 6
#define   O_TXRETRY__BUSERRORRETRY                                  5
#define   O_TXRETRY__UNDERRUNRETRY                                  4
#define   O_TXRETRY__RETRIES                                        0
#define   W_TXRETRY__RETRIES                                        4
#define R_CORECONTROL                                               0x0A8
#define   O_CORECONTROL__ERRORTHREAD                                4
#define   W_CORECONTROL__ERRORTHREAD                                7
#define   O_CORECONTROL__SHUTDOWN                                   2
#define   O_CORECONTROL__SPEED                                      0
#define   W_CORECONTROL__SPEED                                      2
#define R_BYTEOFFSET0                                               0x0A9
#define R_BYTEOFFSET1                                               0x0AA
#define R_L2TYPE_0                                                  0x0F0
#define   O_L2TYPE__EXTRAHDRPROTOSIZE                               26
#define   W_L2TYPE__EXTRAHDRPROTOSIZE                               5
#define   O_L2TYPE__EXTRAHDRPROTOOFFSET                             20
#define   W_L2TYPE__EXTRAHDRPROTOOFFSET                             6
#define   O_L2TYPE__EXTRAHEADERSIZE                                 14
#define   W_L2TYPE__EXTRAHEADERSIZE                                 6
#define   O_L2TYPE__PROTOOFFSET                                     8
#define   W_L2TYPE__PROTOOFFSET                                     6
#define   O_L2TYPE__L2HDROFFSET                                     2
#define   W_L2TYPE__L2HDROFFSET                                     6
#define   O_L2TYPE__L2PROTO                                         0
#define   W_L2TYPE__L2PROTO                                         2
#define R_L2TYPE_1                                                  0xF0
#define R_L2TYPE_2                                                  0xF0
#define R_L2TYPE_3                                                  0xF0
#define R_PARSERCONFIGREG                                           0x100
#define   O_PARSERCONFIGREG__CRCHASHPOLY                            8
#define   W_PARSERCONFIGREG__CRCHASHPOLY                            7
#define   O_PARSERCONFIGREG__PREPADOFFSET                           4
#define   W_PARSERCONFIGREG__PREPADOFFSET                           4
#define   O_PARSERCONFIGREG__USECAM                                 2
#define   O_PARSERCONFIGREG__USEHASH                                1
#define   O_PARSERCONFIGREG__USEPROTO                               0
#define R_L3CTABLE                                                  0x140
#define   O_L3CTABLE__OFFSET0                                       25
#define   W_L3CTABLE__OFFSET0                                       7
#define   O_L3CTABLE__LEN0                                          21
#define   W_L3CTABLE__LEN0                                          4
#define   O_L3CTABLE__OFFSET1                                       14
#define   W_L3CTABLE__OFFSET1                                       7
#define   O_L3CTABLE__LEN1                                          10
#define   W_L3CTABLE__LEN1                                          4
#define   O_L3CTABLE__OFFSET2                                       4
#define   W_L3CTABLE__OFFSET2                                       6
#define   O_L3CTABLE__LEN2                                          0
#define   W_L3CTABLE__LEN2                                          4
#define   O_L3CTABLE__L3HDROFFSET                                   26
#define   W_L3CTABLE__L3HDROFFSET                                   6
#define   O_L3CTABLE__L4PROTOOFFSET                                 20
#define   W_L3CTABLE__L4PROTOOFFSET                                 6
#define   O_L3CTABLE__IPCHKSUMCOMPUTE                               19
#define   O_L3CTABLE__L4CLASSIFY                                    18
#define   O_L3CTABLE__L2PROTO                                       16
#define   W_L3CTABLE__L2PROTO                                       2
#define   O_L3CTABLE__L3PROTOKEY                                    0
#define   W_L3CTABLE__L3PROTOKEY                                    16
#define R_L4CTABLE                                                  0x160
#define   O_L4CTABLE__OFFSET0                                       21
#define   W_L4CTABLE__OFFSET0                                       6
#define   O_L4CTABLE__LEN0                                          17
#define   W_L4CTABLE__LEN0                                          4
#define   O_L4CTABLE__OFFSET1                                       11
#define   W_L4CTABLE__OFFSET1                                       6
#define   O_L4CTABLE__LEN1                                          7
#define   W_L4CTABLE__LEN1                                          4
#define   O_L4CTABLE__TCPCHKSUMENABLE                               0
#define R_CAM4X128TABLE                                             0x172
#define   O_CAM4X128TABLE__CLASSID                                  7
#define   W_CAM4X128TABLE__CLASSID                                  2
#define   O_CAM4X128TABLE__BUCKETID                                 1
#define   W_CAM4X128TABLE__BUCKETID                                 6
#define   O_CAM4X128TABLE__USEBUCKET                                0
#define R_CAM4X128KEY                                               0x180
#define R_TRANSLATETABLE                                            0x1A0
#define R_DMACR0                                                    0x200
#define   O_DMACR0__DATA0WRMAXCR                                    27
#define   W_DMACR0__DATA0WRMAXCR                                    3
#define   O_DMACR0__DATA0RDMAXCR                                    24
#define   W_DMACR0__DATA0RDMAXCR                                    3
#define   O_DMACR0__DATA1WRMAXCR                                    21
#define   W_DMACR0__DATA1WRMAXCR                                    3
#define   O_DMACR0__DATA1RDMAXCR                                    18
#define   W_DMACR0__DATA1RDMAXCR                                    3
#define   O_DMACR0__DATA2WRMAXCR                                    15
#define   W_DMACR0__DATA2WRMAXCR                                    3
#define   O_DMACR0__DATA2RDMAXCR                                    12
#define   W_DMACR0__DATA2RDMAXCR                                    3
#define   O_DMACR0__DATA3WRMAXCR                                    9
#define   W_DMACR0__DATA3WRMAXCR                                    3
#define   O_DMACR0__DATA3RDMAXCR                                    6
#define   W_DMACR0__DATA3RDMAXCR                                    3
#define   O_DMACR0__DATA4WRMAXCR                                    3
#define   W_DMACR0__DATA4WRMAXCR                                    3
#define   O_DMACR0__DATA4RDMAXCR                                    0
#define   W_DMACR0__DATA4RDMAXCR                                    3
#define R_DMACR1                                                    0x201
#define   O_DMACR1__DATA5WRMAXCR                                    27
#define   W_DMACR1__DATA5WRMAXCR                                    3
#define   O_DMACR1__DATA5RDMAXCR                                    24
#define   W_DMACR1__DATA5RDMAXCR                                    3
#define   O_DMACR1__DATA6WRMAXCR                                    21
#define   W_DMACR1__DATA6WRMAXCR                                    3
#define   O_DMACR1__DATA6RDMAXCR                                    18
#define   W_DMACR1__DATA6RDMAXCR                                    3
#define   O_DMACR1__DATA7WRMAXCR                                    15
#define   W_DMACR1__DATA7WRMAXCR                                    3
#define   O_DMACR1__DATA7RDMAXCR                                    12
#define   W_DMACR1__DATA7RDMAXCR                                    3
#define   O_DMACR1__DATA8WRMAXCR                                    9
#define   W_DMACR1__DATA8WRMAXCR                                    3
#define   O_DMACR1__DATA8RDMAXCR                                    6
#define   W_DMACR1__DATA8RDMAXCR                                    3
#define   O_DMACR1__DATA9WRMAXCR                                    3
#define   W_DMACR1__DATA9WRMAXCR                                    3
#define   O_DMACR1__DATA9RDMAXCR                                    0
#define   W_DMACR1__DATA9RDMAXCR                                    3
#define R_DMACR2                                                    0x202
#define   O_DMACR2__DATA10WRMAXCR                                   27
#define   W_DMACR2__DATA10WRMAXCR                                   3
#define   O_DMACR2__DATA10RDMAXCR                                   24
#define   W_DMACR2__DATA10RDMAXCR                                   3
#define   O_DMACR2__DATA11WRMAXCR                                   21
#define   W_DMACR2__DATA11WRMAXCR                                   3
#define   O_DMACR2__DATA11RDMAXCR                                   18
#define   W_DMACR2__DATA11RDMAXCR                                   3
#define   O_DMACR2__DATA12WRMAXCR                                   15
#define   W_DMACR2__DATA12WRMAXCR                                   3
#define   O_DMACR2__DATA12RDMAXCR                                   12
#define   W_DMACR2__DATA12RDMAXCR                                   3
#define   O_DMACR2__DATA13WRMAXCR                                   9
#define   W_DMACR2__DATA13WRMAXCR                                   3
#define   O_DMACR2__DATA13RDMAXCR                                   6
#define   W_DMACR2__DATA13RDMAXCR                                   3
#define   O_DMACR2__DATA14WRMAXCR                                   3
#define   W_DMACR2__DATA14WRMAXCR                                   3
#define   O_DMACR2__DATA14RDMAXCR                                   0
#define   W_DMACR2__DATA14RDMAXCR                                   3
#define R_DMACR3                                                    0x203
#define   O_DMACR3__DATA15WRMAXCR                                   27
#define   W_DMACR3__DATA15WRMAXCR                                   3
#define   O_DMACR3__DATA15RDMAXCR                                   24
#define   W_DMACR3__DATA15RDMAXCR                                   3
#define   O_DMACR3__SPCLASSWRMAXCR                                  21
#define   W_DMACR3__SPCLASSWRMAXCR                                  3
#define   O_DMACR3__SPCLASSRDMAXCR                                  18
#define   W_DMACR3__SPCLASSRDMAXCR                                  3
#define   O_DMACR3__JUMFRINWRMAXCR                                  15
#define   W_DMACR3__JUMFRINWRMAXCR                                  3
#define   O_DMACR3__JUMFRINRDMAXCR                                  12
#define   W_DMACR3__JUMFRINRDMAXCR                                  3
#define   O_DMACR3__REGFRINWRMAXCR                                  9
#define   W_DMACR3__REGFRINWRMAXCR                                  3
#define   O_DMACR3__REGFRINRDMAXCR                                  6
#define   W_DMACR3__REGFRINRDMAXCR                                  3
#define   O_DMACR3__FROUTWRMAXCR                                    3
#define   W_DMACR3__FROUTWRMAXCR                                    3
#define   O_DMACR3__FROUTRDMAXCR                                    0
#define   W_DMACR3__FROUTRDMAXCR                                    3
#define R_REG_FRIN_SPILL_MEM_START_0                                0x204
#define   O_REG_FRIN_SPILL_MEM_START_0__REGFRINSPILLMEMSTART0        0
#define   W_REG_FRIN_SPILL_MEM_START_0__REGFRINSPILLMEMSTART0       32
#define R_REG_FRIN_SPILL_MEM_START_1                                0x205
#define   O_REG_FRIN_SPILL_MEM_START_1__REGFRINSPILLMEMSTART1        0
#define   W_REG_FRIN_SPILL_MEM_START_1__REGFRINSPILLMEMSTART1        3
#define R_REG_FRIN_SPILL_MEM_SIZE                                   0x206
#define   O_REG_FRIN_SPILL_MEM_SIZE__REGFRINSPILLMEMSIZE             0
#define   W_REG_FRIN_SPILL_MEM_SIZE__REGFRINSPILLMEMSIZE            32
#define R_FROUT_SPILL_MEM_START_0                                   0x207
#define   O_FROUT_SPILL_MEM_START_0__FROUTSPILLMEMSTART0             0
#define   W_FROUT_SPILL_MEM_START_0__FROUTSPILLMEMSTART0            32
#define R_FROUT_SPILL_MEM_START_1                                   0x208
#define   O_FROUT_SPILL_MEM_START_1__FROUTSPILLMEMSTART1             0
#define   W_FROUT_SPILL_MEM_START_1__FROUTSPILLMEMSTART1             3
#define R_FROUT_SPILL_MEM_SIZE                                      0x209
#define   O_FROUT_SPILL_MEM_SIZE__FROUTSPILLMEMSIZE                  0
#define   W_FROUT_SPILL_MEM_SIZE__FROUTSPILLMEMSIZE                 32
#define R_CLASS0_SPILL_MEM_START_0                                  0x20A
#define   O_CLASS0_SPILL_MEM_START_0__CLASS0SPILLMEMSTART0           0
#define   W_CLASS0_SPILL_MEM_START_0__CLASS0SPILLMEMSTART0          32
#define R_CLASS0_SPILL_MEM_START_1                                  0x20B
#define   O_CLASS0_SPILL_MEM_START_1__CLASS0SPILLMEMSTART1           0
#define   W_CLASS0_SPILL_MEM_START_1__CLASS0SPILLMEMSTART1           3
#define R_CLASS0_SPILL_MEM_SIZE                                     0x20C
#define   O_CLASS0_SPILL_MEM_SIZE__CLASS0SPILLMEMSIZE                0
#define   W_CLASS0_SPILL_MEM_SIZE__CLASS0SPILLMEMSIZE               32
#define R_JUMFRIN_SPILL_MEM_START_0                                 0x20D
#define   O_JUMFRIN_SPILL_MEM_START_0__JUMFRINSPILLMEMSTART0          0
#define   W_JUMFRIN_SPILL_MEM_START_0__JUMFRINSPILLMEMSTART0         32
#define R_JUMFRIN_SPILL_MEM_START_1                                 0x20E
#define   O_JUMFRIN_SPILL_MEM_START_1__JUMFRINSPILLMEMSTART1         0
#define   W_JUMFRIN_SPILL_MEM_START_1__JUMFRINSPILLMEMSTART1         3
#define R_JUMFRIN_SPILL_MEM_SIZE                                    0x20F
#define   O_JUMFRIN_SPILL_MEM_SIZE__JUMFRINSPILLMEMSIZE              0
#define   W_JUMFRIN_SPILL_MEM_SIZE__JUMFRINSPILLMEMSIZE             32
#define R_CLASS1_SPILL_MEM_START_0                                  0x210
#define   O_CLASS1_SPILL_MEM_START_0__CLASS1SPILLMEMSTART0           0
#define   W_CLASS1_SPILL_MEM_START_0__CLASS1SPILLMEMSTART0          32
#define R_CLASS1_SPILL_MEM_START_1                                  0x211
#define   O_CLASS1_SPILL_MEM_START_1__CLASS1SPILLMEMSTART1           0
#define   W_CLASS1_SPILL_MEM_START_1__CLASS1SPILLMEMSTART1           3
#define R_CLASS1_SPILL_MEM_SIZE                                     0x212
#define   O_CLASS1_SPILL_MEM_SIZE__CLASS1SPILLMEMSIZE                0
#define   W_CLASS1_SPILL_MEM_SIZE__CLASS1SPILLMEMSIZE               32
#define R_CLASS2_SPILL_MEM_START_0                                  0x213
#define   O_CLASS2_SPILL_MEM_START_0__CLASS2SPILLMEMSTART0           0
#define   W_CLASS2_SPILL_MEM_START_0__CLASS2SPILLMEMSTART0          32
#define R_CLASS2_SPILL_MEM_START_1                                  0x214
#define   O_CLASS2_SPILL_MEM_START_1__CLASS2SPILLMEMSTART1           0
#define   W_CLASS2_SPILL_MEM_START_1__CLASS2SPILLMEMSTART1           3
#define R_CLASS2_SPILL_MEM_SIZE                                     0x215
#define   O_CLASS2_SPILL_MEM_SIZE__CLASS2SPILLMEMSIZE                0
#define   W_CLASS2_SPILL_MEM_SIZE__CLASS2SPILLMEMSIZE               32
#define R_CLASS3_SPILL_MEM_START_0                                  0x216
#define   O_CLASS3_SPILL_MEM_START_0__CLASS3SPILLMEMSTART0           0
#define   W_CLASS3_SPILL_MEM_START_0__CLASS3SPILLMEMSTART0          32
#define R_CLASS3_SPILL_MEM_START_1                                  0x217
#define   O_CLASS3_SPILL_MEM_START_1__CLASS3SPILLMEMSTART1           0
#define   W_CLASS3_SPILL_MEM_START_1__CLASS3SPILLMEMSTART1           3
#define R_CLASS3_SPILL_MEM_SIZE                                     0x218
#define   O_CLASS3_SPILL_MEM_SIZE__CLASS3SPILLMEMSIZE                0
#define   W_CLASS3_SPILL_MEM_SIZE__CLASS3SPILLMEMSIZE               32
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
#define   O_TXDATAFIFO0__TX0DATAFIFOSTART                           24
#define   W_TXDATAFIFO0__TX0DATAFIFOSTART                           7
#define   O_TXDATAFIFO0__TX0DATAFIFOSIZE                            16
#define   W_TXDATAFIFO0__TX0DATAFIFOSIZE                            7
#define   O_TXDATAFIFO0__TX1DATAFIFOSTART                           8
#define   W_TXDATAFIFO0__TX1DATAFIFOSTART                           7
#define   O_TXDATAFIFO0__TX1DATAFIFOSIZE                            0
#define   W_TXDATAFIFO0__TX1DATAFIFOSIZE                            7
#define R_TXDATAFIFO1                                               0x222
#define   O_TXDATAFIFO1__TX2DATAFIFOSTART                           24
#define   W_TXDATAFIFO1__TX2DATAFIFOSTART                           7
#define   O_TXDATAFIFO1__TX2DATAFIFOSIZE                            16
#define   W_TXDATAFIFO1__TX2DATAFIFOSIZE                            7
#define   O_TXDATAFIFO1__TX3DATAFIFOSTART                           8
#define   W_TXDATAFIFO1__TX3DATAFIFOSTART                           7
#define   O_TXDATAFIFO1__TX3DATAFIFOSIZE                            0
#define   W_TXDATAFIFO1__TX3DATAFIFOSIZE                            7
#define R_TXDATAFIFO2                                               0x223
#define   O_TXDATAFIFO2__TX4DATAFIFOSTART                           24
#define   W_TXDATAFIFO2__TX4DATAFIFOSTART                           7
#define   O_TXDATAFIFO2__TX4DATAFIFOSIZE                            16
#define   W_TXDATAFIFO2__TX4DATAFIFOSIZE                            7
#define   O_TXDATAFIFO2__TX5DATAFIFOSTART                           8
#define   W_TXDATAFIFO2__TX5DATAFIFOSTART                           7
#define   O_TXDATAFIFO2__TX5DATAFIFOSIZE                            0
#define   W_TXDATAFIFO2__TX5DATAFIFOSIZE                            7
#define R_TXDATAFIFO3                                               0x224
#define   O_TXDATAFIFO3__TX6DATAFIFOSTART                           24
#define   W_TXDATAFIFO3__TX6DATAFIFOSTART                           7
#define   O_TXDATAFIFO3__TX6DATAFIFOSIZE                            16
#define   W_TXDATAFIFO3__TX6DATAFIFOSIZE                            7
#define   O_TXDATAFIFO3__TX7DATAFIFOSTART                           8
#define   W_TXDATAFIFO3__TX7DATAFIFOSTART                           7
#define   O_TXDATAFIFO3__TX7DATAFIFOSIZE                            0
#define   W_TXDATAFIFO3__TX7DATAFIFOSIZE                            7
#define R_TXDATAFIFO4                                               0x225
#define   O_TXDATAFIFO4__TX8DATAFIFOSTART                           24
#define   W_TXDATAFIFO4__TX8DATAFIFOSTART                           7
#define   O_TXDATAFIFO4__TX8DATAFIFOSIZE                            16
#define   W_TXDATAFIFO4__TX8DATAFIFOSIZE                            7
#define   O_TXDATAFIFO4__TX9DATAFIFOSTART                           8
#define   W_TXDATAFIFO4__TX9DATAFIFOSTART                           7
#define   O_TXDATAFIFO4__TX9DATAFIFOSIZE                            0
#define   W_TXDATAFIFO4__TX9DATAFIFOSIZE                            7
#define R_TXDATAFIFO5                                               0x226
#define   O_TXDATAFIFO5__TX10DATAFIFOSTART                          24
#define   W_TXDATAFIFO5__TX10DATAFIFOSTART                          7
#define   O_TXDATAFIFO5__TX10DATAFIFOSIZE                           16
#define   W_TXDATAFIFO5__TX10DATAFIFOSIZE                           7
#define   O_TXDATAFIFO5__TX11DATAFIFOSTART                          8
#define   W_TXDATAFIFO5__TX11DATAFIFOSTART                          7
#define   O_TXDATAFIFO5__TX11DATAFIFOSIZE                           0
#define   W_TXDATAFIFO5__TX11DATAFIFOSIZE                           7
#define R_TXDATAFIFO6                                               0x227
#define   O_TXDATAFIFO6__TX12DATAFIFOSTART                          24
#define   W_TXDATAFIFO6__TX12DATAFIFOSTART                          7
#define   O_TXDATAFIFO6__TX12DATAFIFOSIZE                           16
#define   W_TXDATAFIFO6__TX12DATAFIFOSIZE                           7
#define   O_TXDATAFIFO6__TX13DATAFIFOSTART                          8
#define   W_TXDATAFIFO6__TX13DATAFIFOSTART                          7
#define   O_TXDATAFIFO6__TX13DATAFIFOSIZE                           0
#define   W_TXDATAFIFO6__TX13DATAFIFOSIZE                           7
#define R_TXDATAFIFO7                                               0x228
#define   O_TXDATAFIFO7__TX14DATAFIFOSTART                          24
#define   W_TXDATAFIFO7__TX14DATAFIFOSTART                          7
#define   O_TXDATAFIFO7__TX14DATAFIFOSIZE                           16
#define   W_TXDATAFIFO7__TX14DATAFIFOSIZE                           7
#define   O_TXDATAFIFO7__TX15DATAFIFOSTART                          8
#define   W_TXDATAFIFO7__TX15DATAFIFOSTART                          7
#define   O_TXDATAFIFO7__TX15DATAFIFOSIZE                           0
#define   W_TXDATAFIFO7__TX15DATAFIFOSIZE                           7
#define R_RXDATAFIFO0                                               0x229
#define   O_RXDATAFIFO0__RX0DATAFIFOSTART                           24
#define   W_RXDATAFIFO0__RX0DATAFIFOSTART                           7
#define   O_RXDATAFIFO0__RX0DATAFIFOSIZE                            16
#define   W_RXDATAFIFO0__RX0DATAFIFOSIZE                            7
#define   O_RXDATAFIFO0__RX1DATAFIFOSTART                           8
#define   W_RXDATAFIFO0__RX1DATAFIFOSTART                           7
#define   O_RXDATAFIFO0__RX1DATAFIFOSIZE                            0
#define   W_RXDATAFIFO0__RX1DATAFIFOSIZE                            7
#define R_RXDATAFIFO1                                               0x22A
#define   O_RXDATAFIFO1__RX2DATAFIFOSTART                           24
#define   W_RXDATAFIFO1__RX2DATAFIFOSTART                           7
#define   O_RXDATAFIFO1__RX2DATAFIFOSIZE                            16
#define   W_RXDATAFIFO1__RX2DATAFIFOSIZE                            7
#define   O_RXDATAFIFO1__RX3DATAFIFOSTART                           8
#define   W_RXDATAFIFO1__RX3DATAFIFOSTART                           7
#define   O_RXDATAFIFO1__RX3DATAFIFOSIZE                            0
#define   W_RXDATAFIFO1__RX3DATAFIFOSIZE                            7
#define R_RXDATAFIFO2                                               0x22B
#define   O_RXDATAFIFO2__RX4DATAFIFOSTART                           24
#define   W_RXDATAFIFO2__RX4DATAFIFOSTART                           7
#define   O_RXDATAFIFO2__RX4DATAFIFOSIZE                            16
#define   W_RXDATAFIFO2__RX4DATAFIFOSIZE                            7
#define   O_RXDATAFIFO2__RX5DATAFIFOSTART                           8
#define   W_RXDATAFIFO2__RX5DATAFIFOSTART                           7
#define   O_RXDATAFIFO2__RX5DATAFIFOSIZE                            0
#define   W_RXDATAFIFO2__RX5DATAFIFOSIZE                            7
#define R_RXDATAFIFO3                                               0x22C
#define   O_RXDATAFIFO3__RX6DATAFIFOSTART                           24
#define   W_RXDATAFIFO3__RX6DATAFIFOSTART                           7
#define   O_RXDATAFIFO3__RX6DATAFIFOSIZE                            16
#define   W_RXDATAFIFO3__RX6DATAFIFOSIZE                            7
#define   O_RXDATAFIFO3__RX7DATAFIFOSTART                           8
#define   W_RXDATAFIFO3__RX7DATAFIFOSTART                           7
#define   O_RXDATAFIFO3__RX7DATAFIFOSIZE                            0
#define   W_RXDATAFIFO3__RX7DATAFIFOSIZE                            7
#define R_RXDATAFIFO4                                               0x22D
#define   O_RXDATAFIFO4__RX8DATAFIFOSTART                           24
#define   W_RXDATAFIFO4__RX8DATAFIFOSTART                           7
#define   O_RXDATAFIFO4__RX8DATAFIFOSIZE                            16
#define   W_RXDATAFIFO4__RX8DATAFIFOSIZE                            7
#define   O_RXDATAFIFO4__RX9DATAFIFOSTART                           8
#define   W_RXDATAFIFO4__RX9DATAFIFOSTART                           7
#define   O_RXDATAFIFO4__RX9DATAFIFOSIZE                            0
#define   W_RXDATAFIFO4__RX9DATAFIFOSIZE                            7
#define R_RXDATAFIFO5                                               0x22E
#define   O_RXDATAFIFO5__RX10DATAFIFOSTART                          24
#define   W_RXDATAFIFO5__RX10DATAFIFOSTART                          7
#define   O_RXDATAFIFO5__RX10DATAFIFOSIZE                           16
#define   W_RXDATAFIFO5__RX10DATAFIFOSIZE                           7
#define   O_RXDATAFIFO5__RX11DATAFIFOSTART                          8
#define   W_RXDATAFIFO5__RX11DATAFIFOSTART                          7
#define   O_RXDATAFIFO5__RX11DATAFIFOSIZE                           0
#define   W_RXDATAFIFO5__RX11DATAFIFOSIZE                           7
#define R_RXDATAFIFO6                                               0x22F
#define   O_RXDATAFIFO6__RX12DATAFIFOSTART                          24
#define   W_RXDATAFIFO6__RX12DATAFIFOSTART                          7
#define   O_RXDATAFIFO6__RX12DATAFIFOSIZE                           16
#define   W_RXDATAFIFO6__RX12DATAFIFOSIZE                           7
#define   O_RXDATAFIFO6__RX13DATAFIFOSTART                          8
#define   W_RXDATAFIFO6__RX13DATAFIFOSTART                          7
#define   O_RXDATAFIFO6__RX13DATAFIFOSIZE                           0
#define   W_RXDATAFIFO6__RX13DATAFIFOSIZE                           7
#define R_RXDATAFIFO7                                               0x230
#define   O_RXDATAFIFO7__RX14DATAFIFOSTART                          24
#define   W_RXDATAFIFO7__RX14DATAFIFOSTART                          7
#define   O_RXDATAFIFO7__RX14DATAFIFOSIZE                           16
#define   W_RXDATAFIFO7__RX14DATAFIFOSIZE                           7
#define   O_RXDATAFIFO7__RX15DATAFIFOSTART                          8
#define   W_RXDATAFIFO7__RX15DATAFIFOSTART                          7
#define   O_RXDATAFIFO7__RX15DATAFIFOSIZE                           0
#define   W_RXDATAFIFO7__RX15DATAFIFOSIZE                           7
#define R_XGMACPADCALIBRATION                                       0x231
#define R_FREEQCARVE                                                0x233
#define R_SPI4STATICDELAY0                                          0x240
#define   O_SPI4STATICDELAY0__DATALINE7                             28
#define   W_SPI4STATICDELAY0__DATALINE7                             4
#define   O_SPI4STATICDELAY0__DATALINE6                             24
#define   W_SPI4STATICDELAY0__DATALINE6                             4
#define   O_SPI4STATICDELAY0__DATALINE5                             20
#define   W_SPI4STATICDELAY0__DATALINE5                             4
#define   O_SPI4STATICDELAY0__DATALINE4                             16
#define   W_SPI4STATICDELAY0__DATALINE4                             4
#define   O_SPI4STATICDELAY0__DATALINE3                             12
#define   W_SPI4STATICDELAY0__DATALINE3                             4
#define   O_SPI4STATICDELAY0__DATALINE2                             8
#define   W_SPI4STATICDELAY0__DATALINE2                             4
#define   O_SPI4STATICDELAY0__DATALINE1                             4
#define   W_SPI4STATICDELAY0__DATALINE1                             4
#define   O_SPI4STATICDELAY0__DATALINE0                             0
#define   W_SPI4STATICDELAY0__DATALINE0                             4
#define R_SPI4STATICDELAY1                                          0x241
#define   O_SPI4STATICDELAY1__DATALINE15                            28
#define   W_SPI4STATICDELAY1__DATALINE15                            4
#define   O_SPI4STATICDELAY1__DATALINE14                            24
#define   W_SPI4STATICDELAY1__DATALINE14                            4
#define   O_SPI4STATICDELAY1__DATALINE13                            20
#define   W_SPI4STATICDELAY1__DATALINE13                            4
#define   O_SPI4STATICDELAY1__DATALINE12                            16
#define   W_SPI4STATICDELAY1__DATALINE12                            4
#define   O_SPI4STATICDELAY1__DATALINE11                            12
#define   W_SPI4STATICDELAY1__DATALINE11                            4
#define   O_SPI4STATICDELAY1__DATALINE10                            8
#define   W_SPI4STATICDELAY1__DATALINE10                            4
#define   O_SPI4STATICDELAY1__DATALINE9                             4
#define   W_SPI4STATICDELAY1__DATALINE9                             4
#define   O_SPI4STATICDELAY1__DATALINE8                             0
#define   W_SPI4STATICDELAY1__DATALINE8                             4
#define R_SPI4STATICDELAY2                                          0x242
#define   O_SPI4STATICDELAY0__TXSTAT1                               8
#define   W_SPI4STATICDELAY0__TXSTAT1                               4
#define   O_SPI4STATICDELAY0__TXSTAT0                               4
#define   W_SPI4STATICDELAY0__TXSTAT0                               4
#define   O_SPI4STATICDELAY0__RXCONTROL                             0
#define   W_SPI4STATICDELAY0__RXCONTROL                             4
#define R_SPI4CONTROL                                               0x243
#define   O_SPI4CONTROL__STATICDELAY                                2
#define   O_SPI4CONTROL__LVDS_LVTTL                                 1
#define   O_SPI4CONTROL__SPI4ENABLE                                 0
#define R_CLASSWATERMARKS                                           0x244
#define   O_CLASSWATERMARKS__CLASS0WATERMARK                        24
#define   W_CLASSWATERMARKS__CLASS0WATERMARK                        5
#define   O_CLASSWATERMARKS__CLASS1WATERMARK                        16
#define   W_CLASSWATERMARKS__CLASS1WATERMARK                        5
#define   O_CLASSWATERMARKS__CLASS3WATERMARK                        0
#define   W_CLASSWATERMARKS__CLASS3WATERMARK                        5
#define R_RXWATERMARKS1                                              0x245
#define   O_RXWATERMARKS__RX0DATAWATERMARK                          24
#define   W_RXWATERMARKS__RX0DATAWATERMARK                          7
#define   O_RXWATERMARKS__RX1DATAWATERMARK                          16
#define   W_RXWATERMARKS__RX1DATAWATERMARK                          7
#define   O_RXWATERMARKS__RX3DATAWATERMARK                          0
#define   W_RXWATERMARKS__RX3DATAWATERMARK                          7
#define R_RXWATERMARKS2                                              0x246
#define   O_RXWATERMARKS__RX4DATAWATERMARK                          24
#define   W_RXWATERMARKS__RX4DATAWATERMARK                          7
#define   O_RXWATERMARKS__RX5DATAWATERMARK                          16
#define   W_RXWATERMARKS__RX5DATAWATERMARK                          7
#define   O_RXWATERMARKS__RX6DATAWATERMARK                          8
#define   W_RXWATERMARKS__RX6DATAWATERMARK                          7
#define   O_RXWATERMARKS__RX7DATAWATERMARK                          0
#define   W_RXWATERMARKS__RX7DATAWATERMARK                          7
#define R_RXWATERMARKS3                                              0x247
#define   O_RXWATERMARKS__RX8DATAWATERMARK                          24
#define   W_RXWATERMARKS__RX8DATAWATERMARK                          7
#define   O_RXWATERMARKS__RX9DATAWATERMARK                          16
#define   W_RXWATERMARKS__RX9DATAWATERMARK                          7
#define   O_RXWATERMARKS__RX10DATAWATERMARK                         8
#define   W_RXWATERMARKS__RX10DATAWATERMARK                         7
#define   O_RXWATERMARKS__RX11DATAWATERMARK                         0
#define   W_RXWATERMARKS__RX11DATAWATERMARK                         7
#define R_RXWATERMARKS4                                              0x248
#define   O_RXWATERMARKS__RX12DATAWATERMARK                         24
#define   W_RXWATERMARKS__RX12DATAWATERMARK                         7
#define   O_RXWATERMARKS__RX13DATAWATERMARK                         16
#define   W_RXWATERMARKS__RX13DATAWATERMARK                         7
#define   O_RXWATERMARKS__RX14DATAWATERMARK                         8
#define   W_RXWATERMARKS__RX14DATAWATERMARK                         7
#define   O_RXWATERMARKS__RX15DATAWATERMARK                         0
#define   W_RXWATERMARKS__RX15DATAWATERMARK                         7
#define R_FREEWATERMARKS                                            0x249
#define   O_FREEWATERMARKS__FREEOUTWATERMARK                        16
#define   W_FREEWATERMARKS__FREEOUTWATERMARK                        16
#define   O_FREEWATERMARKS__JUMFRWATERMARK                          8
#define   W_FREEWATERMARKS__JUMFRWATERMARK                          7
#define   O_FREEWATERMARKS__REGFRWATERMARK                          0
#define   W_FREEWATERMARKS__REGFRWATERMARK                          7
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
						 * headroom
						 */

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

void xlr_set_gmac_speed(struct xlr_net_priv *priv);
