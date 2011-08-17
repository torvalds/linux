//------------------------------------------------------------------------------
// <copyright file="testcmd.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef  TESTCMD_H_
#define  TESTCMD_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef AR6002_REV2
#define TCMD_MAX_RATES 12
#else
#define TCMD_MAX_RATES 28
#endif

typedef enum {
    ZEROES_PATTERN = 0,
    ONES_PATTERN,
    REPEATING_10,
    PN7_PATTERN,
    PN9_PATTERN,
    PN15_PATTERN
}TX_DATA_PATTERN;

/* Continuous tx
   mode : TCMD_CONT_TX_OFF - Disabling continuous tx
          TCMD_CONT_TX_SINE - Enable continuous unmodulated tx
          TCMD_CONT_TX_FRAME- Enable continuous modulated tx
   freq : Channel freq in Mhz. (e.g 2412 for channel 1 in 11 g)
dataRate: 0 - 1 Mbps
          1 - 2 Mbps
          2 - 5.5 Mbps
          3 - 11 Mbps
          4 - 6 Mbps
          5 - 9 Mbps
          6 - 12 Mbps
          7 - 18 Mbps
          8 - 24 Mbps
          9 - 36 Mbps
         10 - 28 Mbps
         11 - 54 Mbps
  txPwr: Tx power in dBm[5 -11] for unmod Tx, [5-14] for mod Tx
antenna:  1 - one antenna
          2 - two antenna
Note : Enable/disable continuous tx test cmd works only when target is awake.
*/

typedef enum {
    TCMD_CONT_TX_OFF = 0,
    TCMD_CONT_TX_SINE,
    TCMD_CONT_TX_FRAME,
    TCMD_CONT_TX_TX99,
    TCMD_CONT_TX_TX100
} TCMD_CONT_TX_MODE;

typedef enum {
    TCMD_WLAN_MODE_NOHT = 0,
    TCMD_WLAN_MODE_HT20 = 1,
    TCMD_WLAN_MODE_HT40PLUS = 2,
    TCMD_WLAN_MODE_HT40MINUS = 3,
} TCMD_WLAN_MODE;

typedef PREPACK struct {
    u32 testCmdId;
    u32 mode;
    u32 freq;
    u32 dataRate;
    s32 txPwr;
    u32 antenna;
    u32 enANI;
    u32 scramblerOff;
    u32 aifsn;
    u16 pktSz;
    u16 txPattern;
    u32 shortGuard;
    u32 numPackets;
    u32 wlanMode;
} POSTPACK TCMD_CONT_TX;

#define TCMD_TXPATTERN_ZERONE                 0x1
#define TCMD_TXPATTERN_ZERONE_DIS_SCRAMBLE    0x2

/* Continuous Rx
 act: TCMD_CONT_RX_PROMIS - promiscuous mode (accept all incoming frames)
      TCMD_CONT_RX_FILTER - filter mode (accept only frames with dest
                                             address equal specified
                                             mac address (set via act =3)
      TCMD_CONT_RX_REPORT  off mode  (disable cont rx mode and get the
                                          report from the last cont
                                          Rx test)

     TCMD_CONT_RX_SETMAC - set MacAddr mode (sets the MAC address for the
                                                 target. This Overrides
                                                 the default MAC address.)

*/
typedef enum {
    TCMD_CONT_RX_PROMIS =0,
    TCMD_CONT_RX_FILTER,
    TCMD_CONT_RX_REPORT,
    TCMD_CONT_RX_SETMAC,
    TCMD_CONT_RX_SET_ANT_SWITCH_TABLE
} TCMD_CONT_RX_ACT;

typedef PREPACK struct {
    u32 testCmdId;
    u32 act;
    u32 enANI;
    PREPACK union {
        struct PREPACK TCMD_CONT_RX_PARA {
            u32 freq;
            u32 antenna;
            u32 wlanMode;
        } POSTPACK para;
        struct PREPACK TCMD_CONT_RX_REPORT {
            u32 totalPkt;
            s32 rssiInDBm;
            u32 crcErrPkt;
            u32 secErrPkt;
            u16 rateCnt[TCMD_MAX_RATES];
            u16 rateCntShortGuard[TCMD_MAX_RATES];
        } POSTPACK report;
        struct PREPACK TCMD_CONT_RX_MAC {
            u8    addr[ATH_MAC_LEN];
        } POSTPACK mac;
        struct PREPACK TCMD_CONT_RX_ANT_SWITCH_TABLE {
            u32 antswitch1;
            u32 antswitch2;
        }POSTPACK antswitchtable;
    } POSTPACK u;
} POSTPACK TCMD_CONT_RX;

/* Force sleep/wake  test cmd
 mode: TCMD_PM_WAKEUP - Wakeup the target
       TCMD_PM_SLEEP - Force the target to sleep.
 */
typedef enum {
    TCMD_PM_WAKEUP = 1, /* be consistent with target */
    TCMD_PM_SLEEP,
    TCMD_PM_DEEPSLEEP
} TCMD_PM_MODE;

typedef PREPACK struct {
    u32 testCmdId;
    u32 mode;
} POSTPACK TCMD_PM;

typedef enum {
    TCMD_CONT_TX_ID,
    TCMD_CONT_RX_ID,
    TCMD_PM_ID
} TCMD_ID;

typedef PREPACK union {
          TCMD_CONT_TX contTx;
          TCMD_CONT_RX contRx;
          TCMD_PM pm;
} POSTPACK TEST_CMD;

#ifdef __cplusplus
}
#endif

#endif /* TESTCMD_H_ */
