/*
 *
 * IPACX specific defines
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

/* All Registers original Siemens Spec  */

#ifndef INCLUDE_IPACX_H
#define INCLUDE_IPACX_H

/* D-channel registers   */
#define IPACX_RFIFOD        0x00    /* RD       */
#define IPACX_XFIFOD        0x00    /* WR       */
#define IPACX_ISTAD         0x20    /* RD       */
#define IPACX_MASKD         0x20    /* WR       */
#define IPACX_STARD         0x21    /* RD       */
#define IPACX_CMDRD         0x21    /* WR       */
#define IPACX_MODED         0x22    /* RD/WR    */
#define IPACX_EXMD1         0x23    /* RD/WR    */
#define IPACX_TIMR1         0x24    /* RD/WR    */
#define IPACX_SAP1          0x25    /* WR       */
#define IPACX_SAP2          0x26    /* WR       */
#define IPACX_RBCLD         0x26    /* RD       */
#define IPACX_RBCHD         0x27    /* RD       */
#define IPACX_TEI1          0x27    /* WR       */
#define IPACX_TEI2          0x28    /* WR       */
#define IPACX_RSTAD         0x28    /* RD       */
#define IPACX_TMD           0x29    /* RD/WR    */
#define IPACX_CIR0          0x2E    /* RD       */
#define IPACX_CIX0          0x2E    /* WR       */
#define IPACX_CIR1          0x2F    /* RD       */
#define IPACX_CIX1          0x2F    /* WR       */

/* Transceiver registers    */
#define IPACX_TR_CONF0      0x30    /* RD/WR    */
#define IPACX_TR_CONF1      0x31    /* RD/WR    */
#define IPACX_TR_CONF2      0x32    /* RD/WR    */
#define IPACX_TR_STA        0x33    /* RD       */
#define IPACX_TR_CMD        0x34    /* RD/WR    */
#define IPACX_SQRR1         0x35    /* RD       */
#define IPACX_SQXR1         0x35    /* WR       */
#define IPACX_SQRR2         0x36    /* RD       */
#define IPACX_SQXR2         0x36    /* WR       */
#define IPACX_SQRR3         0x37    /* RD       */
#define IPACX_SQXR3         0x37    /* WR       */
#define IPACX_ISTATR        0x38    /* RD       */
#define IPACX_MASKTR        0x39    /* RD/WR    */
#define IPACX_TR_MODE       0x3A    /* RD/WR    */
#define IPACX_ACFG1         0x3C    /* RD/WR    */
#define IPACX_ACFG2         0x3D    /* RD/WR    */
#define IPACX_AOE           0x3E    /* RD/WR    */
#define IPACX_ARX           0x3F    /* RD       */
#define IPACX_ATX           0x3F    /* WR       */

/* IOM: Timeslot, DPS, CDA  */
#define IPACX_CDA10         0x40    /* RD/WR    */
#define IPACX_CDA11         0x41    /* RD/WR    */
#define IPACX_CDA20         0x42    /* RD/WR    */
#define IPACX_CDA21         0x43    /* RD/WR    */
#define IPACX_CDA_TSDP10    0x44    /* RD/WR    */
#define IPACX_CDA_TSDP11    0x45    /* RD/WR    */
#define IPACX_CDA_TSDP20    0x46    /* RD/WR    */
#define IPACX_CDA_TSDP21    0x47    /* RD/WR    */
#define IPACX_BCHA_TSDP_BC1 0x48    /* RD/WR    */
#define IPACX_BCHA_TSDP_BC2 0x49    /* RD/WR    */
#define IPACX_BCHB_TSDP_BC1 0x4A    /* RD/WR    */
#define IPACX_BCHB_TSDP_BC2 0x4B    /* RD/WR    */
#define IPACX_TR_TSDP_BC1   0x4C    /* RD/WR    */
#define IPACX_TR_TSDP_BC2   0x4D    /* RD/WR    */
#define IPACX_CDA1_CR       0x4E    /* RD/WR    */
#define IPACX_CDA2_CR       0x4F    /* RD/WR    */

/* IOM: Contol, Sync transfer, Monitor    */
#define IPACX_TR_CR         0x50    /* RD/WR    */
#define IPACX_TRC_CR        0x50    /* RD/WR    */
#define IPACX_BCHA_CR       0x51    /* RD/WR    */
#define IPACX_BCHB_CR       0x52    /* RD/WR    */
#define IPACX_DCI_CR        0x53    /* RD/WR    */
#define IPACX_DCIC_CR       0x53    /* RD/WR    */
#define IPACX_MON_CR        0x54    /* RD/WR    */
#define IPACX_SDS1_CR       0x55    /* RD/WR    */
#define IPACX_SDS2_CR       0x56    /* RD/WR    */
#define IPACX_IOM_CR        0x57    /* RD/WR    */
#define IPACX_STI           0x58    /* RD       */
#define IPACX_ASTI          0x58    /* WR       */
#define IPACX_MSTI          0x59    /* RD/WR    */
#define IPACX_SDS_CONF      0x5A    /* RD/WR    */
#define IPACX_MCDA          0x5B    /* RD       */
#define IPACX_MOR           0x5C    /* RD       */
#define IPACX_MOX           0x5C    /* WR       */
#define IPACX_MOSR          0x5D    /* RD       */
#define IPACX_MOCR          0x5E    /* RD/WR    */
#define IPACX_MSTA          0x5F    /* RD       */
#define IPACX_MCONF         0x5F    /* WR       */

/* Interrupt and general registers */
#define IPACX_ISTA          0x60    /* RD       */
#define IPACX_MASK          0x60    /* WR       */
#define IPACX_AUXI          0x61    /* RD       */
#define IPACX_AUXM          0x61    /* WR       */
#define IPACX_MODE1         0x62    /* RD/WR    */
#define IPACX_MODE2         0x63    /* RD/WR    */
#define IPACX_ID            0x64    /* RD       */
#define IPACX_SRES          0x64    /* WR       */
#define IPACX_TIMR2         0x65    /* RD/WR    */

/* B-channel registers */
#define IPACX_OFF_B1        0x70
#define IPACX_OFF_B2        0x80

#define IPACX_ISTAB         0x00    /* RD       */
#define IPACX_MASKB         0x00    /* WR       */
#define IPACX_STARB         0x01    /* RD       */
#define IPACX_CMDRB         0x01    /* WR       */
#define IPACX_MODEB         0x02    /* RD/WR    */
#define IPACX_EXMB          0x03    /* RD/WR    */
#define IPACX_RAH1          0x05    /* WR       */
#define IPACX_RAH2          0x06    /* WR       */
#define IPACX_RBCLB         0x06    /* RD       */
#define IPACX_RBCHB         0x07    /* RD       */
#define IPACX_RAL1          0x07    /* WR       */
#define IPACX_RAL2          0x08    /* WR       */
#define IPACX_RSTAB         0x08    /* RD       */
#define IPACX_TMB           0x09    /* RD/WR    */
#define IPACX_RFIFOB        0x0A    /*- RD      */
#define IPACX_XFIFOB        0x0A    /*- WR      */

/* Layer 1 Commands */
#define IPACX_CMD_TIM    0x0
#define IPACX_CMD_RES    0x1
#define IPACX_CMD_SSP    0x2
#define IPACX_CMD_SCP    0x3
#define IPACX_CMD_AR8    0x8
#define IPACX_CMD_AR10   0x9
#define IPACX_CMD_ARL    0xa
#define IPACX_CMD_DI     0xf

/* Layer 1 Indications */
#define IPACX_IND_DR     0x0
#define IPACX_IND_RES    0x1
#define IPACX_IND_TMA    0x2
#define IPACX_IND_SLD    0x3
#define IPACX_IND_RSY    0x4
#define IPACX_IND_DR6    0x5
#define IPACX_IND_PU     0x7
#define IPACX_IND_AR     0x8
#define IPACX_IND_ARL    0xa
#define IPACX_IND_CVR    0xb
#define IPACX_IND_AI8    0xc
#define IPACX_IND_AI10   0xd
#define IPACX_IND_AIL    0xe
#define IPACX_IND_DC     0xf

extern void init_ipacx(struct IsdnCardState *, int);
extern void interrupt_ipacx(struct IsdnCardState *);
extern void setup_isac(struct IsdnCardState *);

#endif
