/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  interface for ethernetdriver
                "fast ethernet controller" (FEC)
                freescale coldfire MCF528x and compatible FEC

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

                $RCSfile: EdrvFec5282.h,v $

                $Author: D.Krueger $

                $Revision: 1.3 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                Dev C++ and GNU-Compiler for m68k

  -------------------------------------------------------------------------

  Revision History:

  2005/08/01 m.b.:   start of implementation

****************************************************************************/

#ifndef _EDRVFEC_H_
#define _EDRVFEC_H_

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------
// base addresses
#define FEC0_ADDR 0x0000
#define FEC1_ADDR 0x0000	//tbd

// control / status registers
#define FEC_EIR                 0x1004	// interrupt event register
#define FEC_EIMR                0x1008	// interrupt mask register
#define FEC_RDAR                0x1010	// receive descriptor active register
#define FEC_TDAR                0x1014	// transmit descriptor active register
#define FEC_ECR                 0x1024	// ethernet control register
#define FEC_MMFR                0x1040	// MII data register
#define FEC_MSCR                0x1044	// MII speed register
#define FEC_MIBC                0x1064	// MIB control/status register
#define FEC_RCR                 0x1084	// receive control register
#define FEC_TCR                 0x10C4	// transmit control register
#define FEC_PALR                0x10E4	// physical address low register
#define FEC_PAUR                0x10E8	// physical address high + type register
#define FEC_OPD                 0x10EC	// opcode + pause register
#define FEC_IAUR                0x1118	// upper 32 bit of individual hash table
#define FEC_IALR                0x111C	// lower 32 bit of individual hash table
#define FEC_GAUR                0x1120	// upper 32 bit of group hash table
#define FEC_GALR                0x1124	// lower 32 bit of group hash table
#define FEC_TFWR                0x1144	// transmit FIFO watermark
#define FEC_FRBR                0x114C	// FIFO receive bound register
#define FEC_FRSR                0x1150	// FIFO receive FIFO start register
#define FEC_ERDSR               0x1180	// pointer to receive descriptor ring
#define FEC_ETDSR               0x1184	// pointer to transmit descriptor ring
#define FEC_EMRBR               0x1188	// maximum receive buffer size

// mib block counters memory map
#define FEC_RMON_T_DROP         0x1200	// count of frames not counted correctly
#define FEC_RMON_T_PACKETS      0x1204	// RMON tx packet count
#define FEC_RMON_T_BC_PKT       0x1208	// RMON tx broadcast packets
#define FEC_RMON_T_MC_PKT       0x120C	// RMON tx multicast packets
#define FEC_RMON_T_CRC_ALIGN    0x1210	// RMON tx packets w CRC/align error
#define FEC_RMON_T_UNDERSIZE    0x1214	// RMON tx packets < 64 bytes, good CRC
#define FEC_RMON_T_OVERSIZE     0x1218	// RMON tx packets > MAX_FL bytes, good CRC
#define FEC_RMON_T_FRAG         0x121C	// RMON tx packets < 64 bytes, bad CRC
#define FEC_RMON_T_JAB          0x1220	// RMON tx packets > MAX_FL bytes, bad CRC
#define FEC_RMON_T_COL          0x1224	// RMON tx collision count
#define FEC_RMON_T_P64          0x1228	// RMON tx           64 byte packets
#define FEC_RMON_T_P65TO127     0x122C	// RMON tx   65 to  127 byte packets
#define FEC_RMON_T_P128TO255    0x1230	// RMON tx  128 to  255 byte packets
#define FEC_RMON_T_P256TO511    0x1234	// RMON tx  256 to  511 byte packets
#define FEC_RMON_T_P512TO1023   0x1238	// RMON tx  512 to 1023 byte packets
#define FEC_RMON_T_P1024TO2047  0x123C	// RMON tx 1024 to 2047 byte packets
#define FEC_RMON_T_P_GTE2048    0x1240	// RMON tx w > 2048 bytes
#define FEC_RMON_T_OCTETS       0x1244	// RMON tx octets
#define FEC_IEEE_T_DROP         0x1248	// count of frames not counted correctly
#define FEC_IEEE_T_FRAME_OK     0x124C	// frames transmitted OK
#define FEC_IEEE_T_1COL         0x1250	// frames transmitted with single collision
#define FEC_IEEE_T_MCOL         0x1254	// frames transmitted with multiple collisions
#define FEC_IEEE_T_DEF          0x1258	// frames transmitted after deferral delay
#define FEC_IEEE_T_LCOL         0x125C	// frames transmitted with late collisions
#define FEC_IEEE_T_EXCOL        0x1260	// frames transmitted with excessive collisions
#define FEC_IEEE_T_MACERR       0x1264	// frames transmitted with tx-FIFO underrun
#define FEC_IEEE_T_CSERR        0x1268	// frames transmitted with carrier sense error
#define FEC_IEEE_T_SQE          0x126C	// frames transmitted with SQE error
#define FEC_IEEE_T_FDXFC        0x1270	// flow control pause frames transmitted
#define FEC_IEEE_T_OCTETS_OK    0x1274	// octet count for frames transmitted w/o error
#define FEC_RMON_R_PACKETS      0x1284	// RMON rx packet count
#define FEC_RMON_R_BC_PKT       0x1288	// RMON rx broadcast packets
#define FEC_RMON_R_MC_PKT       0x128C	// RMON rx multicast packets
#define FEC_RMON_R_CRC_ALIGN    0x1290	// RMON rx packets w CRC/align error
#define FEC_RMON_R_UNDERSIZE    0x1294	// RMON rx packets < 64 bytes, good CRC
#define FEC_RMON_R_OVERSIZE     0x1298	// RMON rx packets > MAX_FL bytes, good CRC
#define FEC_RMON_R_FRAG         0x129C	// RMON rx packets < 64 bytes, bad CRC
#define FEC_RMON_R_JAB          0x12A0	// RMON rx packets > MAX_FL bytes, bad CRC
#define FEC_RMON_R_RESVD_0      0x12A4	//
#define FEC_RMON_R_P64          0x12A8	// RMON rx           64 byte packets
#define FEC_RMON_R_P65T0127     0x12AC	// RMON rx   65 to  127 byte packets
#define FEC_RMON_R_P128TO255    0x12B0	// RMON rx  128 to  255 byte packets
#define FEC_RMON_R_P256TO511    0x12B4	// RMON rx  256 to  511 byte packets
#define FEC_RMON_R_P512TO1023   0x12B8	// RMON rx  512 to 1023 byte packets
#define FEC_RMON_R_P1024TO2047  0x12BC	// RMON rx 1024 to 2047 byte packets
#define FEC_RMON_R_GTE2048      0x12C0	// RMON rx w > 2048 bytes
#define FEC_RMON_R_OCTETS       0x12C4	// RMON rx octets
#define FEC_IEEE_R_DROP         0x12C8	// count of frames not counted correctly
#define FEC_IEEE_R_FRAME_OK     0x12CC	// frames received OK
#define FEC_IEEE_R_CRC          0x12D0	// frames received with CRC error
#define FEC_IEEE_R_ALIGN        0x12D4	// frames received with alignment error
#define FEC_IEEE_R_MACERR       0x12D8	// receive FIFO overflow count
#define FEC_IEEE_R_FDXFC        0x12DC	// flow control pause frames received
#define FEC_IEEE_R_OCTETS_OK    0x12E0	// octet count for frames rcvd w/o error

// register bit definitions and macros
#define FEC_EIR_UN              (0x00080000)
#define FEC_EIR_RL              (0x00100000)
#define FEC_EIR_LC              (0x00200000)
#define FEC_EIR_EBERR           (0x00400000)
#define FEC_EIR_MII             (0x00800000)
#define FEC_EIR_RXB             (0x01000000)
#define FEC_EIR_RXF             (0x02000000)
#define FEC_EIR_TXB             (0x04000000)
#define FEC_EIR_TXF             (0x08000000)
#define FEC_EIR_GRA             (0x10000000)
#define FEC_EIR_BABT            (0x20000000)
#define FEC_EIR_BABR            (0x40000000)
#define FEC_EIR_HBERR           (0x80000000)

#define FEC_EIMR_UN             (0x00080000)
#define FEC_EIMR_RL             (0x00100000)
#define FEC_EIMR_LC             (0x00200000)
#define FEC_EIMR_EBERR          (0x00400000)
#define FEC_EIMR_MII            (0x00800000)
#define FEC_EIMR_RXB            (0x01000000)
#define FEC_EIMR_RXF            (0x02000000)
#define FEC_EIMR_TXB            (0x04000000)
#define FEC_EIMR_TXF            (0x08000000)
#define FEC_EIMR_GRA            (0x10000000)
#define FEC_EIMR_BABT           (0x20000000)
#define FEC_EIMR_BABR           (0x40000000)
#define FEC_EIMR_HBERR          (0x80000000)

#define FEC_RDAR_R_DES_ACTIVE   (0x01000000)

#define FEC_TDAR_X_DES_ACTIVE   (0x01000000)

#define FEC_ECR_RESET           (0x00000001)
#define FEC_ECR_ETHER_EN        (0x00000002)

#define FEC_MMFR_DATA(x)        (((x) & 0xFFFF))
#define FEC_MMFR_TA             (0x00020000)
#define FEC_MMFR_RA(x)          (((x) & 0x1F) << 18)
#define FEC_MMFR_PA(x)          (((x) & 0x1F) << 23)
#define FEC_MMFR_OP_WR          (0x10000000)
#define FEC_MMFR_OP_RD          (0x20000000)
#define FEC_MMFR_ST             (0x40000000)

#define FEC_MSCR_MII_SPEED(x)   (((x) & 0x1F) << 1)
#define FEC_MSCR_DIS_PREAMBLE   (0x00000008)

#define FEC_MIBC_MIB_IDLE       (0x40000000)
#define FEC_MIBC_MIB_DISABLE    (0x80000000)

#define FEC_RCR_LOOP            (0x00000001)
#define FEC_RCR_DRT             (0x00000002)
#define FEC_RCR_MII_MODE        (0x00000004)
#define FEC_RCR_PROM            (0x00000008)
#define FEC_RCR_BC_REJ          (0x00000010)
#define FEC_RCR_FCE             (0x00000020)
#define FEC_RCR_MAX_FL(x)       (((x) & 0x07FF) << 16)

#define FEC_TCR_GTS             (0x00000001)
#define FEC_TCR_HBC             (0x00000002)
#define FEC_TCR_FDEN            (0x00000004)
#define FEC_TCR_TFC_PAUSE       (0x00000008)
#define FEC_TCR_RFC_PAUSE       (0x00000010)

#define FEC_PALR_BYTE3(x)       (((x) & 0xFF) <<  0)
#define FEC_PALR_BYTE2(x)       (((x) & 0xFF) <<  8)
#define FEC_PALR_BYTE1(x)       (((x) & 0xFF) << 16)
#define FEC_PALR_BYTE0(x)       (((x) & 0xFF) << 24)

//#define FEC_PAUR_TYPE(x)        (((x) & 0xFFFF) <<  0)
#define FEC_PAUR_BYTE5(x)       (((x) &   0xFF) << 16)
#define FEC_PAUR_BYTE4(x)       (((x) &   0xFF) << 24)

#define FEC_OPD_PAUSE_DUR(x)    (((x) & 0xFFFF))
//#define FEC_OPD_OPCODE(x)       (((x) & 0xFFFF) << 16)

//m.b.
#define FEC_IAUR_BYTE7(x)       (((x) & 0xFF) <<  0)
#define FEC_IAUR_BYTE6(x)       (((x) & 0xFF) <<  8)
#define FEC_IAUR_BYTE5(x)       (((x) & 0xFF) << 16)
#define FEC_IAUR_BYTE4(x)       (((x) & 0xFF) << 24)

#define FEC_IALR_BYTE3(x)       (((x) & 0xFF) <<  0)
#define FEC_IALR_BYTE2(x)       (((x) & 0xFF) <<  8)
#define FEC_IALR_BYTE1(x)       (((x) & 0xFF) << 16)
#define FEC_IALR_BYTE0(x)       (((x) & 0xFF) << 24)

#define FEC_GAUR_BYTE7(x)       (((x) & 0xFF) <<  0)
#define FEC_GAUR_BYTE6(x)       (((x) & 0xFF) <<  8)
#define FEC_GAUR_BYTE5(x)       (((x) & 0xFF) << 16)
#define FEC_GAUR_BYTE4(x)       (((x) & 0xFF) << 24)

#define FEC_GALR_BYTE3(x)       (((x) & 0xFF) <<  0)
#define FEC_GALR_BYTE2(x)       (((x) & 0xFF) <<  8)
#define FEC_GALR_BYTE1(x)       (((x) & 0xFF) << 16)
#define FEC_GALR_BYTE0(x)       (((x) & 0xFF) << 24)
// ^^^^

#define FEC_TFWR_X_WMRK_64      (0x00000001)
#define FEC_TFWR_X_WMRK_128     (0x00000002)
#define FEC_TFWR_X_WMRK_192     (0x00000003)

//m.b.
#define FEC_FRBR_R_BOUND(x)     (((x) & 0xFF) << 2)

//m.b.
#define FEC_FRSR_R_FSTART(x)    (((x) & 0xFF) << 2)

//m.b.
#define FEC_ERDSR_R_DES_START(x)  (((x) & 0x3FFFFFFF) << 2)

//m.b.
#define FEC_ETSDR_X_DES_START(x)  (((x) & 0x3FFFFFFF) << 2)

#define FEC_EMRBR_R_BUF_SIZE(x) (((x) & 0x7F) <<  4)

#define FEC_RxBD_TR             0x0001
#define FEC_RxBD_OV             0x0002
#define FEC_RxBD_CR             0x0004
#define FEC_RxBD_NO             0x0010
#define FEC_RxBD_LG             0x0020
#define FEC_RxBD_MC             0x0040
#define FEC_RxBD_BC             0x0080
#define FEC_RxBD_M              0x0100
#define FEC_RxBD_L              0x0800
#define FEC_RxBD_R02            0x1000
#define FEC_RxBD_W              0x2000
#define FEC_RxBD_R01            0x4000
#define FEC_RxBD_INUSE          0x4000
#define FEC_RxBD_E              0x8000

//m.b.
//#define FEC_TxBD_CSL            0x0001
//#define FEC_TxBD_UN             0x0002
//#define FEC_TxBD_RL             0x0040
//#define FEC_TxBD_LC             0x0080
//#define FEC_TxBD_HB             0x0100
//#define FEC_TxBD_DEF            0x0200
#define FEC_TxBD_ABC            0x0200
// ^^^^
#define FEC_TxBD_TC             0x0400
#define FEC_TxBD_L              0x0800
#define FEC_TxBD_TO2            0x1000
#define FEC_TxBD_W              0x2000
#define FEC_TxBD_TO1            0x4000
#define FEC_TxBD_INUSE          0x4000
#define FEC_TxBD_R              0x8000

//---------------------------------------------------------------------------
// types
//---------------------------------------------------------------------------

// Rx and Tx buffer descriptor format
typedef struct {
	WORD m_wStatus;		// control / status  ---  used by edrv, do not change in application
	WORD m_wLength;		// transfer length
	BYTE *m_pbData;		// buffer address
} tBufferDescr;

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------

#if (NO_OF_INSTANCES > 1)
#define ECI_WRITE_DW_REG(off,val)       (*(DWORD *)(void *)(&__IPSBAR[off]) = val)
#define ECI_READ_DW_REG(off)            (*(DWORD *)(void *)(&__IPSBAR[off]))
#else
#if (EDRV_USED_ETH_CTRL == 0)
#define ECI_WRITE_DW_REG(off,val)       (*(DWORD *)(void *)(&__IPSBAR[FEC0_ADDR+off]) = val)
#define ECI_READ_DW_REG(off)            (*(DWORD *)(void *)(&__IPSBAR[FEC0_ADDR+off]))
#else
#define ECI_WRITE_DW_REG(off,val)       (*(DWORD *)(void *)(&__IPSBAR[FEC1_ADDR+off]) = val)
#define ECI_READ_DW_REG(off)            (*(DWORD *)(void *)(&__IPSBAR[FEC1_ADDR+off]))
#endif
#endif

#endif // #ifndef _EDRV_FEC_H_
