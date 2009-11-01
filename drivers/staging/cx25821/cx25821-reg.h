/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __CX25821_REGISTERS__
#define __CX25821_REGISTERS__

/* Risc Instructions */
#define RISC_CNT_INC	 0x00010000
#define RISC_CNT_RESET	 0x00030000
#define RISC_IRQ1		 0x01000000
#define RISC_IRQ2		 0x02000000
#define RISC_EOL		 0x04000000
#define RISC_SOL		 0x08000000
#define RISC_WRITE		 0x10000000
#define RISC_SKIP		 0x20000000
#define RISC_JUMP		 0x70000000
#define RISC_SYNC		 0x80000000
#define RISC_RESYNC		 0x80008000
#define RISC_READ		 0x90000000
#define RISC_WRITERM	 0xB0000000
#define RISC_WRITECM	 0xC0000000
#define RISC_WRITECR	 0xD0000000
#define RISC_WRITEC		 0x50000000
#define RISC_READC		 0xA0000000

#define RISC_SYNC_ODD		 0x00000000
#define RISC_SYNC_EVEN		 0x00000200
#define RISC_SYNC_ODD_VBI	 0x00000006
#define RISC_SYNC_EVEN_VBI	 0x00000207
#define RISC_NOOP			 0xF0000000

//*****************************************************************************
// ASB SRAM
//*****************************************************************************
#define  TX_SRAM                   0x000000	// Transmit SRAM

//*****************************************************************************
#define  RX_RAM                    0x010000	// Receive SRAM

//*****************************************************************************
// Application Layer (AL)
//*****************************************************************************
#define  DEV_CNTRL2                0x040000	// Device control
#define  FLD_RUN_RISC              0x00000020

//*****************************************************************************
#define  PCI_INT_MSK               0x040010	// PCI interrupt mask
#define  PCI_INT_STAT              0x040014	// PCI interrupt status
#define  PCI_INT_MSTAT             0x040018	// PCI interrupt masked status
#define  FLD_HAMMERHEAD_INT        (1 << 27)
#define  FLD_UART_INT              (1 << 26)
#define  FLD_IRQN_INT              (1 << 25)
#define  FLD_TM_INT                (1 << 28)
#define  FLD_I2C_3_RACK            (1 << 27)
#define  FLD_I2C_3_INT             (1 << 26)
#define  FLD_I2C_2_RACK            (1 << 25)
#define  FLD_I2C_2_INT             (1 << 24)
#define  FLD_I2C_1_RACK            (1 << 23)
#define  FLD_I2C_1_INT             (1 << 22)

#define  FLD_APB_DMA_BERR_INT      (1 << 21)
#define  FLD_AL_WR_BERR_INT        (1 << 20)
#define  FLD_AL_RD_BERR_INT        (1 << 19)
#define  FLD_RISC_WR_BERR_INT      (1 << 18)
#define  FLD_RISC_RD_BERR_INT      (1 << 17)

#define  FLD_VID_I_INT             (1 << 8)
#define  FLD_VID_H_INT             (1 << 7)
#define  FLD_VID_G_INT             (1 << 6)
#define  FLD_VID_F_INT             (1 << 5)
#define  FLD_VID_E_INT             (1 << 4)
#define  FLD_VID_D_INT             (1 << 3)
#define  FLD_VID_C_INT             (1 << 2)
#define  FLD_VID_B_INT             (1 << 1)
#define  FLD_VID_A_INT             (1 << 0)

//*****************************************************************************
#define  VID_A_INT_MSK             0x040020	// Video A interrupt mask
#define  VID_A_INT_STAT            0x040024	// Video A interrupt status
#define  VID_A_INT_MSTAT           0x040028	// Video A interrupt masked status
#define  VID_A_INT_SSTAT           0x04002C	// Video A interrupt set status

//*****************************************************************************
#define  VID_B_INT_MSK             0x040030	// Video B interrupt mask
#define  VID_B_INT_STAT            0x040034	// Video B interrupt status
#define  VID_B_INT_MSTAT           0x040038	// Video B interrupt masked status
#define  VID_B_INT_SSTAT           0x04003C	// Video B interrupt set status

//*****************************************************************************
#define  VID_C_INT_MSK             0x040040	// Video C interrupt mask
#define  VID_C_INT_STAT            0x040044	// Video C interrupt status
#define  VID_C_INT_MSTAT           0x040048	// Video C interrupt masked status
#define  VID_C_INT_SSTAT           0x04004C	// Video C interrupt set status

//*****************************************************************************
#define  VID_D_INT_MSK             0x040050	// Video D interrupt mask
#define  VID_D_INT_STAT            0x040054	// Video D interrupt status
#define  VID_D_INT_MSTAT           0x040058	// Video D interrupt masked status
#define  VID_D_INT_SSTAT           0x04005C	// Video D interrupt set status

//*****************************************************************************
#define  VID_E_INT_MSK             0x040060	// Video E interrupt mask
#define  VID_E_INT_STAT            0x040064	// Video E interrupt status
#define  VID_E_INT_MSTAT           0x040068	// Video E interrupt masked status
#define  VID_E_INT_SSTAT           0x04006C	// Video E interrupt set status

//*****************************************************************************
#define  VID_F_INT_MSK             0x040070	// Video F interrupt mask
#define  VID_F_INT_STAT            0x040074	// Video F interrupt status
#define  VID_F_INT_MSTAT           0x040078	// Video F interrupt masked status
#define  VID_F_INT_SSTAT           0x04007C	// Video F interrupt set status

//*****************************************************************************
#define  VID_G_INT_MSK             0x040080	// Video G interrupt mask
#define  VID_G_INT_STAT            0x040084	// Video G interrupt status
#define  VID_G_INT_MSTAT           0x040088	// Video G interrupt masked status
#define  VID_G_INT_SSTAT           0x04008C	// Video G interrupt set status

//*****************************************************************************
#define  VID_H_INT_MSK             0x040090	// Video H interrupt mask
#define  VID_H_INT_STAT            0x040094	// Video H interrupt status
#define  VID_H_INT_MSTAT           0x040098	// Video H interrupt masked status
#define  VID_H_INT_SSTAT           0x04009C	// Video H interrupt set status

//*****************************************************************************
#define  VID_I_INT_MSK             0x0400A0	// Video I interrupt mask
#define  VID_I_INT_STAT            0x0400A4	// Video I interrupt status
#define  VID_I_INT_MSTAT           0x0400A8	// Video I interrupt masked status
#define  VID_I_INT_SSTAT           0x0400AC	// Video I interrupt set status

//*****************************************************************************
#define  VID_J_INT_MSK             0x0400B0	// Video J interrupt mask
#define  VID_J_INT_STAT            0x0400B4	// Video J interrupt status
#define  VID_J_INT_MSTAT           0x0400B8	// Video J interrupt masked status
#define  VID_J_INT_SSTAT           0x0400BC	// Video J interrupt set status

#define  FLD_VID_SRC_OPC_ERR       0x00020000
#define  FLD_VID_DST_OPC_ERR       0x00010000
#define  FLD_VID_SRC_SYNC          0x00002000
#define  FLD_VID_DST_SYNC          0x00001000
#define  FLD_VID_SRC_UF            0x00000200
#define  FLD_VID_DST_OF            0x00000100
#define  FLD_VID_SRC_RISC2         0x00000020
#define  FLD_VID_DST_RISC2         0x00000010
#define  FLD_VID_SRC_RISC1         0x00000002
#define  FLD_VID_DST_RISC1         0x00000001
#define  FLD_VID_SRC_ERRORS		FLD_VID_SRC_OPC_ERR | FLD_VID_SRC_SYNC | FLD_VID_SRC_UF
#define  FLD_VID_DST_ERRORS		FLD_VID_DST_OPC_ERR | FLD_VID_DST_SYNC | FLD_VID_DST_OF

//*****************************************************************************
#define  AUD_A_INT_MSK             0x0400C0	// Audio Int interrupt mask
#define  AUD_A_INT_STAT            0x0400C4	// Audio Int interrupt status
#define  AUD_A_INT_MSTAT           0x0400C8	// Audio Int interrupt masked status
#define  AUD_A_INT_SSTAT           0x0400CC	// Audio Int interrupt set status

//*****************************************************************************
#define  AUD_B_INT_MSK             0x0400D0	// Audio Int interrupt mask
#define  AUD_B_INT_STAT            0x0400D4	// Audio Int interrupt status
#define  AUD_B_INT_MSTAT           0x0400D8	// Audio Int interrupt masked status
#define  AUD_B_INT_SSTAT           0x0400DC	// Audio Int interrupt set status

//*****************************************************************************
#define  AUD_C_INT_MSK             0x0400E0	// Audio Int interrupt mask
#define  AUD_C_INT_STAT            0x0400E4	// Audio Int interrupt status
#define  AUD_C_INT_MSTAT           0x0400E8	// Audio Int interrupt masked status
#define  AUD_C_INT_SSTAT           0x0400EC	// Audio Int interrupt set status

//*****************************************************************************
#define  AUD_D_INT_MSK             0x0400F0	// Audio Int interrupt mask
#define  AUD_D_INT_STAT            0x0400F4	// Audio Int interrupt status
#define  AUD_D_INT_MSTAT           0x0400F8	// Audio Int interrupt masked status
#define  AUD_D_INT_SSTAT           0x0400FC	// Audio Int interrupt set status

//*****************************************************************************
#define  AUD_E_INT_MSK             0x040100	// Audio Int interrupt mask
#define  AUD_E_INT_STAT            0x040104	// Audio Int interrupt status
#define  AUD_E_INT_MSTAT           0x040108	// Audio Int interrupt masked status
#define  AUD_E_INT_SSTAT           0x04010C	// Audio Int interrupt set status

#define  FLD_AUD_SRC_OPC_ERR       0x00020000
#define  FLD_AUD_DST_OPC_ERR       0x00010000
#define  FLD_AUD_SRC_SYNC          0x00002000
#define  FLD_AUD_DST_SYNC          0x00001000
#define  FLD_AUD_SRC_OF            0x00000200
#define  FLD_AUD_DST_OF            0x00000100
#define  FLD_AUD_SRC_RISCI2        0x00000020
#define  FLD_AUD_DST_RISCI2        0x00000010
#define  FLD_AUD_SRC_RISCI1        0x00000002
#define  FLD_AUD_DST_RISCI1        0x00000001

//*****************************************************************************
#define  MBIF_A_INT_MSK             0x040110	// MBIF Int interrupt mask
#define  MBIF_A_INT_STAT            0x040114	// MBIF Int interrupt status
#define  MBIF_A_INT_MSTAT           0x040118	// MBIF Int interrupt masked status
#define  MBIF_A_INT_SSTAT           0x04011C	// MBIF Int interrupt set status

//*****************************************************************************
#define  MBIF_B_INT_MSK             0x040120	// MBIF Int interrupt mask
#define  MBIF_B_INT_STAT            0x040124	// MBIF Int interrupt status
#define  MBIF_B_INT_MSTAT           0x040128	// MBIF Int interrupt masked status
#define  MBIF_B_INT_SSTAT           0x04012C	// MBIF Int interrupt set status

#define  FLD_MBIF_DST_OPC_ERR       0x00010000
#define  FLD_MBIF_DST_SYNC          0x00001000
#define  FLD_MBIF_DST_OF            0x00000100
#define  FLD_MBIF_DST_RISCI2        0x00000010
#define  FLD_MBIF_DST_RISCI1        0x00000001

//*****************************************************************************
#define  AUD_EXT_INT_MSK           0x040060	// Audio Ext interrupt mask
#define  AUD_EXT_INT_STAT          0x040064	// Audio Ext interrupt status
#define  AUD_EXT_INT_MSTAT         0x040068	// Audio Ext interrupt masked status
#define  AUD_EXT_INT_SSTAT         0x04006C	// Audio Ext interrupt set status
#define  FLD_AUD_EXT_OPC_ERR       0x00010000
#define  FLD_AUD_EXT_SYNC          0x00001000
#define  FLD_AUD_EXT_OF            0x00000100
#define  FLD_AUD_EXT_RISCI2        0x00000010
#define  FLD_AUD_EXT_RISCI1        0x00000001

//*****************************************************************************
#define  GPIO_LO                   0x110010	// Lower  of GPIO pins [31:0]
#define  GPIO_HI                   0x110014	// Upper WORD  of GPIO pins [47:31]

#define  GPIO_LO_OE                0x110018	// Lower  of GPIO output enable [31:0]
#define  GPIO_HI_OE                0x11001C	// Upper word  of GPIO output enable [47:32]

#define  GPIO_LO_INT_MSK           0x11003C	// GPIO interrupt mask
#define  GPIO_LO_INT_STAT          0x110044	// GPIO interrupt status
#define  GPIO_LO_INT_MSTAT         0x11004C	// GPIO interrupt masked status
#define  GPIO_LO_ISM_SNS           0x110054	// GPIO interrupt sensitivity
#define  GPIO_LO_ISM_POL           0x11005C	// GPIO interrupt polarity

#define  GPIO_HI_INT_MSK           0x110040	// GPIO interrupt mask
#define  GPIO_HI_INT_STAT          0x110048	// GPIO interrupt status
#define  GPIO_HI_INT_MSTAT         0x110050	// GPIO interrupt masked status
#define  GPIO_HI_ISM_SNS           0x110058	// GPIO interrupt sensitivity
#define  GPIO_HI_ISM_POL           0x110060	// GPIO interrupt polarity

#define  FLD_GPIO43_INT            (1 << 11)
#define  FLD_GPIO42_INT            (1 << 10)
#define  FLD_GPIO41_INT            (1 << 9)
#define  FLD_GPIO40_INT            (1 << 8)

#define  FLD_GPIO9_INT             (1 << 9)
#define  FLD_GPIO8_INT             (1 << 8)
#define  FLD_GPIO7_INT             (1 << 7)
#define  FLD_GPIO6_INT             (1 << 6)
#define  FLD_GPIO5_INT             (1 << 5)
#define  FLD_GPIO4_INT             (1 << 4)
#define  FLD_GPIO3_INT             (1 << 3)
#define  FLD_GPIO2_INT             (1 << 2)
#define  FLD_GPIO1_INT             (1 << 1)
#define  FLD_GPIO0_INT             (1 << 0)

//*****************************************************************************
#define  TC_REQ                    0x040090	// Rider PCI Express traFFic class request

//*****************************************************************************
#define  TC_REQ_SET                0x040094	// Rider PCI Express traFFic class request set

//*****************************************************************************
// Rider
//*****************************************************************************

// PCI Compatible Header
//*****************************************************************************
#define  RDR_CFG0                  0x050000
#define  RDR_VENDOR_DEVICE_ID_CFG  0x050000

//*****************************************************************************
#define  RDR_CFG1                  0x050004

//*****************************************************************************
#define  RDR_CFG2                  0x050008

//*****************************************************************************
#define  RDR_CFG3                  0x05000C

//*****************************************************************************
#define  RDR_CFG4                  0x050010

//*****************************************************************************
#define  RDR_CFG5                  0x050014

//*****************************************************************************
#define  RDR_CFG6                  0x050018

//*****************************************************************************
#define  RDR_CFG7                  0x05001C

//*****************************************************************************
#define  RDR_CFG8                  0x050020

//*****************************************************************************
#define  RDR_CFG9                  0x050024

//*****************************************************************************
#define  RDR_CFGA                  0x050028

//*****************************************************************************
#define  RDR_CFGB                  0x05002C
#define  RDR_SUSSYSTEM_ID_CFG      0x05002C

//*****************************************************************************
#define  RDR_CFGC                  0x050030

//*****************************************************************************
#define  RDR_CFGD                  0x050034

//*****************************************************************************
#define  RDR_CFGE                  0x050038

//*****************************************************************************
#define  RDR_CFGF                  0x05003C

//*****************************************************************************
// PCI-Express Capabilities
//*****************************************************************************
#define  RDR_PECAP                 0x050040

//*****************************************************************************
#define  RDR_PEDEVCAP              0x050044

//*****************************************************************************
#define  RDR_PEDEVSC               0x050048

//*****************************************************************************
#define  RDR_PELINKCAP             0x05004C

//*****************************************************************************
#define  RDR_PELINKSC              0x050050

//*****************************************************************************
#define  RDR_PMICAP                0x050080

//*****************************************************************************
#define  RDR_PMCSR                 0x050084

//*****************************************************************************
#define  RDR_VPDCAP                0x050090

//*****************************************************************************
#define  RDR_VPDDATA               0x050094

//*****************************************************************************
#define  RDR_MSICAP                0x0500A0

//*****************************************************************************
#define  RDR_MSIARL                0x0500A4

//*****************************************************************************
#define  RDR_MSIARU                0x0500A8

//*****************************************************************************
#define  RDR_MSIDATA               0x0500AC

//*****************************************************************************
// PCI Express Extended Capabilities
//*****************************************************************************
#define  RDR_AERXCAP               0x050100

//*****************************************************************************
#define  RDR_AERUESTA              0x050104

//*****************************************************************************
#define  RDR_AERUEMSK              0x050108

//*****************************************************************************
#define  RDR_AERUESEV              0x05010C

//*****************************************************************************
#define  RDR_AERCESTA              0x050110

//*****************************************************************************
#define  RDR_AERCEMSK              0x050114

//*****************************************************************************
#define  RDR_AERCC                 0x050118

//*****************************************************************************
#define  RDR_AERHL0                0x05011C

//*****************************************************************************
#define  RDR_AERHL1                0x050120

//*****************************************************************************
#define  RDR_AERHL2                0x050124

//*****************************************************************************
#define  RDR_AERHL3                0x050128

//*****************************************************************************
#define  RDR_VCXCAP                0x050200

//*****************************************************************************
#define  RDR_VCCAP1                0x050204

//*****************************************************************************
#define  RDR_VCCAP2                0x050208

//*****************************************************************************
#define  RDR_VCSC                  0x05020C

//*****************************************************************************
#define  RDR_VCR0_CAP              0x050210

//*****************************************************************************
#define  RDR_VCR0_CTRL             0x050214

//*****************************************************************************
#define  RDR_VCR0_STAT             0x050218

//*****************************************************************************
#define  RDR_VCR1_CAP              0x05021C

//*****************************************************************************
#define  RDR_VCR1_CTRL             0x050220

//*****************************************************************************
#define  RDR_VCR1_STAT             0x050224

//*****************************************************************************
#define  RDR_VCR2_CAP              0x050228

//*****************************************************************************
#define  RDR_VCR2_CTRL             0x05022C

//*****************************************************************************
#define  RDR_VCR2_STAT             0x050230

//*****************************************************************************
#define  RDR_VCR3_CAP              0x050234

//*****************************************************************************
#define  RDR_VCR3_CTRL             0x050238

//*****************************************************************************
#define  RDR_VCR3_STAT             0x05023C

//*****************************************************************************
#define  RDR_VCARB0                0x050240

//*****************************************************************************
#define  RDR_VCARB1                0x050244

//*****************************************************************************
#define  RDR_VCARB2                0x050248

//*****************************************************************************
#define  RDR_VCARB3                0x05024C

//*****************************************************************************
#define  RDR_VCARB4                0x050250

//*****************************************************************************
#define  RDR_VCARB5                0x050254

//*****************************************************************************
#define  RDR_VCARB6                0x050258

//*****************************************************************************
#define  RDR_VCARB7                0x05025C

//*****************************************************************************
#define  RDR_RDRSTAT0              0x050300

//*****************************************************************************
#define  RDR_RDRSTAT1              0x050304

//*****************************************************************************
#define  RDR_RDRCTL0               0x050308

//*****************************************************************************
#define  RDR_RDRCTL1               0x05030C

//*****************************************************************************
// Transaction Layer Registers
//*****************************************************************************
#define  RDR_TLSTAT0               0x050310

//*****************************************************************************
#define  RDR_TLSTAT1               0x050314

//*****************************************************************************
#define  RDR_TLCTL0                0x050318
#define  FLD_CFG_UR_CPL_MODE       0x00000040
#define  FLD_CFG_CORR_ERR_QUITE    0x00000020
#define  FLD_CFG_RCB_CK_EN         0x00000010
#define  FLD_CFG_BNDRY_CK_EN       0x00000008
#define  FLD_CFG_BYTE_EN_CK_EN     0x00000004
#define  FLD_CFG_RELAX_ORDER_MSK   0x00000002
#define  FLD_CFG_TAG_ORDER_EN      0x00000001

//*****************************************************************************
#define  RDR_TLCTL1                0x05031C

//*****************************************************************************
#define  RDR_REQRCAL               0x050320

//*****************************************************************************
#define  RDR_REQRCAU               0x050324

//*****************************************************************************
#define  RDR_REQEPA                0x050328

//*****************************************************************************
#define  RDR_REQCTRL               0x05032C

//*****************************************************************************
#define  RDR_REQSTAT               0x050330

//*****************************************************************************
#define  RDR_TL_TEST               0x050334

//*****************************************************************************
#define  RDR_VCR01_CTL             0x050348

//*****************************************************************************
#define  RDR_VCR23_CTL             0x05034C

//*****************************************************************************
#define  RDR_RX_VCR0_FC            0x050350

//*****************************************************************************
#define  RDR_RX_VCR1_FC            0x050354

//*****************************************************************************
#define  RDR_RX_VCR2_FC            0x050358

//*****************************************************************************
#define  RDR_RX_VCR3_FC            0x05035C

//*****************************************************************************
// Data Link Layer Registers
//*****************************************************************************
#define  RDR_DLLSTAT               0x050360

//*****************************************************************************
#define  RDR_DLLCTRL               0x050364

//*****************************************************************************
#define  RDR_REPLAYTO              0x050368

//*****************************************************************************
#define  RDR_ACKLATTO              0x05036C

//*****************************************************************************
// MAC Layer Registers
//*****************************************************************************
#define  RDR_MACSTAT0              0x050380

//*****************************************************************************
#define  RDR_MACSTAT1              0x050384

//*****************************************************************************
#define  RDR_MACCTRL0              0x050388

//*****************************************************************************
#define  RDR_MACCTRL1              0x05038C

//*****************************************************************************
#define  RDR_MACCTRL2              0x050390

//*****************************************************************************
#define  RDR_MAC_LB_DATA           0x050394

//*****************************************************************************
#define  RDR_L0S_EXIT_LAT          0x050398

//*****************************************************************************
// DMAC
//*****************************************************************************
#define  DMA1_PTR1                 0x100000	// DMA Current Ptr : Ch#1

//*****************************************************************************
#define  DMA2_PTR1                 0x100004	// DMA Current Ptr : Ch#2

//*****************************************************************************
#define  DMA3_PTR1                 0x100008	// DMA Current Ptr : Ch#3

//*****************************************************************************
#define  DMA4_PTR1                 0x10000C	// DMA Current Ptr : Ch#4

//*****************************************************************************
#define  DMA5_PTR1                 0x100010	// DMA Current Ptr : Ch#5

//*****************************************************************************
#define  DMA6_PTR1                 0x100014	// DMA Current Ptr : Ch#6

//*****************************************************************************
#define  DMA7_PTR1                 0x100018	// DMA Current Ptr : Ch#7

//*****************************************************************************
#define  DMA8_PTR1                 0x10001C	// DMA Current Ptr : Ch#8

//*****************************************************************************
#define  DMA9_PTR1                 0x100020	// DMA Current Ptr : Ch#9

//*****************************************************************************
#define  DMA10_PTR1                0x100024	// DMA Current Ptr : Ch#10

//*****************************************************************************
#define  DMA11_PTR1                0x100028	// DMA Current Ptr : Ch#11

//*****************************************************************************
#define  DMA12_PTR1                0x10002C	// DMA Current Ptr : Ch#12

//*****************************************************************************
#define  DMA13_PTR1                0x100030	// DMA Current Ptr : Ch#13

//*****************************************************************************
#define  DMA14_PTR1                0x100034	// DMA Current Ptr : Ch#14

//*****************************************************************************
#define  DMA15_PTR1                0x100038	// DMA Current Ptr : Ch#15

//*****************************************************************************
#define  DMA16_PTR1                0x10003C	// DMA Current Ptr : Ch#16

//*****************************************************************************
#define  DMA17_PTR1                0x100040	// DMA Current Ptr : Ch#17

//*****************************************************************************
#define  DMA18_PTR1                0x100044	// DMA Current Ptr : Ch#18

//*****************************************************************************
#define  DMA19_PTR1                0x100048	// DMA Current Ptr : Ch#19

//*****************************************************************************
#define  DMA20_PTR1                0x10004C	// DMA Current Ptr : Ch#20

//*****************************************************************************
#define  DMA21_PTR1                0x100050	// DMA Current Ptr : Ch#21

//*****************************************************************************
#define  DMA22_PTR1                0x100054	// DMA Current Ptr : Ch#22

//*****************************************************************************
#define  DMA23_PTR1                0x100058	// DMA Current Ptr : Ch#23

//*****************************************************************************
#define  DMA24_PTR1                0x10005C	// DMA Current Ptr : Ch#24

//*****************************************************************************
#define  DMA25_PTR1                0x100060	// DMA Current Ptr : Ch#25

//*****************************************************************************
#define  DMA26_PTR1                0x100064	// DMA Current Ptr : Ch#26

//*****************************************************************************
#define  DMA1_PTR2                 0x100080	// DMA Tab Ptr : Ch#1

//*****************************************************************************
#define  DMA2_PTR2                 0x100084	// DMA Tab Ptr : Ch#2

//*****************************************************************************
#define  DMA3_PTR2                 0x100088	// DMA Tab Ptr : Ch#3

//*****************************************************************************
#define  DMA4_PTR2                 0x10008C	// DMA Tab Ptr : Ch#4

//*****************************************************************************
#define  DMA5_PTR2                 0x100090	// DMA Tab Ptr : Ch#5

//*****************************************************************************
#define  DMA6_PTR2                 0x100094	// DMA Tab Ptr : Ch#6

//*****************************************************************************
#define  DMA7_PTR2                 0x100098	// DMA Tab Ptr : Ch#7

//*****************************************************************************
#define  DMA8_PTR2                 0x10009C	// DMA Tab Ptr : Ch#8

//*****************************************************************************
#define  DMA9_PTR2                 0x1000A0	// DMA Tab Ptr : Ch#9

//*****************************************************************************
#define  DMA10_PTR2                0x1000A4	// DMA Tab Ptr : Ch#10

//*****************************************************************************
#define  DMA11_PTR2                0x1000A8	// DMA Tab Ptr : Ch#11

//*****************************************************************************
#define  DMA12_PTR2                0x1000AC	// DMA Tab Ptr : Ch#12

//*****************************************************************************
#define  DMA13_PTR2                0x1000B0	// DMA Tab Ptr : Ch#13

//*****************************************************************************
#define  DMA14_PTR2                0x1000B4	// DMA Tab Ptr : Ch#14

//*****************************************************************************
#define  DMA15_PTR2                0x1000B8	// DMA Tab Ptr : Ch#15

//*****************************************************************************
#define  DMA16_PTR2                0x1000BC	// DMA Tab Ptr : Ch#16

//*****************************************************************************
#define  DMA17_PTR2                0x1000C0	// DMA Tab Ptr : Ch#17

//*****************************************************************************
#define  DMA18_PTR2                0x1000C4	// DMA Tab Ptr : Ch#18

//*****************************************************************************
#define  DMA19_PTR2                0x1000C8	// DMA Tab Ptr : Ch#19

//*****************************************************************************
#define  DMA20_PTR2                0x1000CC	// DMA Tab Ptr : Ch#20

//*****************************************************************************
#define  DMA21_PTR2                0x1000D0	// DMA Tab Ptr : Ch#21

//*****************************************************************************
#define  DMA22_PTR2                0x1000D4	// DMA Tab Ptr : Ch#22

//*****************************************************************************
#define  DMA23_PTR2                0x1000D8	// DMA Tab Ptr : Ch#23

//*****************************************************************************
#define  DMA24_PTR2                0x1000DC	// DMA Tab Ptr : Ch#24

//*****************************************************************************
#define  DMA25_PTR2                0x1000E0	// DMA Tab Ptr : Ch#25

//*****************************************************************************
#define  DMA26_PTR2                0x1000E4	// DMA Tab Ptr : Ch#26

//*****************************************************************************
#define  DMA1_CNT1                 0x100100	// DMA BuFFer Size : Ch#1

//*****************************************************************************
#define  DMA2_CNT1                 0x100104	// DMA BuFFer Size : Ch#2

//*****************************************************************************
#define  DMA3_CNT1                 0x100108	// DMA BuFFer Size : Ch#3

//*****************************************************************************
#define  DMA4_CNT1                 0x10010C	// DMA BuFFer Size : Ch#4

//*****************************************************************************
#define  DMA5_CNT1                 0x100110	// DMA BuFFer Size : Ch#5

//*****************************************************************************
#define  DMA6_CNT1                 0x100114	// DMA BuFFer Size : Ch#6

//*****************************************************************************
#define  DMA7_CNT1                 0x100118	// DMA BuFFer Size : Ch#7

//*****************************************************************************
#define  DMA8_CNT1                 0x10011C	// DMA BuFFer Size : Ch#8

//*****************************************************************************
#define  DMA9_CNT1                 0x100120	// DMA BuFFer Size : Ch#9

//*****************************************************************************
#define  DMA10_CNT1                0x100124	// DMA BuFFer Size : Ch#10

//*****************************************************************************
#define  DMA11_CNT1                0x100128	// DMA BuFFer Size : Ch#11

//*****************************************************************************
#define  DMA12_CNT1                0x10012C	// DMA BuFFer Size : Ch#12

//*****************************************************************************
#define  DMA13_CNT1                0x100130	// DMA BuFFer Size : Ch#13

//*****************************************************************************
#define  DMA14_CNT1                0x100134	// DMA BuFFer Size : Ch#14

//*****************************************************************************
#define  DMA15_CNT1                0x100138	// DMA BuFFer Size : Ch#15

//*****************************************************************************
#define  DMA16_CNT1                0x10013C	// DMA BuFFer Size : Ch#16

//*****************************************************************************
#define  DMA17_CNT1                0x100140	// DMA BuFFer Size : Ch#17

//*****************************************************************************
#define  DMA18_CNT1                0x100144	// DMA BuFFer Size : Ch#18

//*****************************************************************************
#define  DMA19_CNT1                0x100148	// DMA BuFFer Size : Ch#19

//*****************************************************************************
#define  DMA20_CNT1                0x10014C	// DMA BuFFer Size : Ch#20

//*****************************************************************************
#define  DMA21_CNT1                0x100150	// DMA BuFFer Size : Ch#21

//*****************************************************************************
#define  DMA22_CNT1                0x100154	// DMA BuFFer Size : Ch#22

//*****************************************************************************
#define  DMA23_CNT1                0x100158	// DMA BuFFer Size : Ch#23

//*****************************************************************************
#define  DMA24_CNT1                0x10015C	// DMA BuFFer Size : Ch#24

//*****************************************************************************
#define  DMA25_CNT1                0x100160	// DMA BuFFer Size : Ch#25

//*****************************************************************************
#define  DMA26_CNT1                0x100164	// DMA BuFFer Size : Ch#26

//*****************************************************************************
#define  DMA1_CNT2                 0x100180	// DMA Table Size : Ch#1

//*****************************************************************************
#define  DMA2_CNT2                 0x100184	// DMA Table Size : Ch#2

//*****************************************************************************
#define  DMA3_CNT2                 0x100188	// DMA Table Size : Ch#3

//*****************************************************************************
#define  DMA4_CNT2                 0x10018C	// DMA Table Size : Ch#4

//*****************************************************************************
#define  DMA5_CNT2                 0x100190	// DMA Table Size : Ch#5

//*****************************************************************************
#define  DMA6_CNT2                 0x100194	// DMA Table Size : Ch#6

//*****************************************************************************
#define  DMA7_CNT2                 0x100198	// DMA Table Size : Ch#7

//*****************************************************************************
#define  DMA8_CNT2                 0x10019C	// DMA Table Size : Ch#8

//*****************************************************************************
#define  DMA9_CNT2                 0x1001A0	// DMA Table Size : Ch#9

//*****************************************************************************
#define  DMA10_CNT2                0x1001A4	// DMA Table Size : Ch#10

//*****************************************************************************
#define  DMA11_CNT2                0x1001A8	// DMA Table Size : Ch#11

//*****************************************************************************
#define  DMA12_CNT2                0x1001AC	// DMA Table Size : Ch#12

//*****************************************************************************
#define  DMA13_CNT2                0x1001B0	// DMA Table Size : Ch#13

//*****************************************************************************
#define  DMA14_CNT2                0x1001B4	// DMA Table Size : Ch#14

//*****************************************************************************
#define  DMA15_CNT2                0x1001B8	// DMA Table Size : Ch#15

//*****************************************************************************
#define  DMA16_CNT2                0x1001BC	// DMA Table Size : Ch#16

//*****************************************************************************
#define  DMA17_CNT2                0x1001C0	// DMA Table Size : Ch#17

//*****************************************************************************
#define  DMA18_CNT2                0x1001C4	// DMA Table Size : Ch#18

//*****************************************************************************
#define  DMA19_CNT2                0x1001C8	// DMA Table Size : Ch#19

//*****************************************************************************
#define  DMA20_CNT2                0x1001CC	// DMA Table Size : Ch#20

//*****************************************************************************
#define  DMA21_CNT2                0x1001D0	// DMA Table Size : Ch#21

//*****************************************************************************
#define  DMA22_CNT2                0x1001D4	// DMA Table Size : Ch#22

//*****************************************************************************
#define  DMA23_CNT2                0x1001D8	// DMA Table Size : Ch#23

//*****************************************************************************
#define  DMA24_CNT2                0x1001DC	// DMA Table Size : Ch#24

//*****************************************************************************
#define  DMA25_CNT2                0x1001E0	// DMA Table Size : Ch#25

//*****************************************************************************
#define  DMA26_CNT2                0x1001E4	// DMA Table Size : Ch#26

//*****************************************************************************
 // ITG
//*****************************************************************************
#define  TM_CNT_LDW                0x110000	// Timer : Counter low

//*****************************************************************************
#define  TM_CNT_UW                 0x110004	// Timer : Counter high word

//*****************************************************************************
#define  TM_LMT_LDW                0x110008	// Timer : Limit low

//*****************************************************************************
#define  TM_LMT_UW                 0x11000C	// Timer : Limit high word

//*****************************************************************************
#define  GP0_IO                    0x110010	// GPIO output enables data I/O
#define  FLD_GP_OE                 0x00FF0000	// GPIO: GP_OE output enable
#define  FLD_GP_IN                 0x0000FF00	// GPIO: GP_IN status
#define  FLD_GP_OUT                0x000000FF	// GPIO: GP_OUT control

//*****************************************************************************
#define  GPIO_ISM                  0x110014	// GPIO interrupt sensitivity mode
#define  FLD_GP_ISM_SNS            0x00000070
#define  FLD_GP_ISM_POL            0x00000007

//*****************************************************************************
#define  SOFT_RESET                0x11001C	// Output system reset reg
#define  FLD_PECOS_SOFT_RESET      0x00000001

//*****************************************************************************
#define  MC416_RWD                 0x110020	// MC416 GPIO[18:3] pin
#define  MC416_OEN                 0x110024	// Output enable of GPIO[18:3]
#define  MC416_CTL                 0x110028

//*****************************************************************************
#define  ALT_PIN_OUT_SEL           0x11002C	// Alternate GPIO output select

#define  FLD_ALT_GPIO_OUT_SEL      0xF0000000
// 0          Disabled <-- default
// 1          GPIO[0]
// 2          GPIO[10]
// 3          VIP_656_DATA_VAL
// 4          VIP_656_DATA[0]
// 5          VIP_656_CLK
// 6          VIP_656_DATA_EXT[1]
// 7          VIP_656_DATA_EXT[0]
// 8          ATT_IF

#define  FLD_AUX_PLL_CLK_ALT_SEL   0x0F000000
// 0          AUX_PLL_CLK<-- default
// 1          GPIO[2]
// 2          GPIO[10]
// 3          VIP_656_DATA_VAL
// 4          VIP_656_DATA[0]
// 5          VIP_656_CLK
// 6          VIP_656_DATA_EXT[1]
// 7          VIP_656_DATA_EXT[0]

#define  FLD_IR_TX_ALT_SEL         0x00F00000
// 0          IR_TX <-- default
// 1          GPIO[1]
// 2          GPIO[10]
// 3          VIP_656_DATA_VAL
// 4          VIP_656_DATA[0]
// 5          VIP_656_CLK
// 6          VIP_656_DATA_EXT[1]
// 7          VIP_656_DATA_EXT[0]

#define  FLD_IR_RX_ALT_SEL         0x000F0000
// 0          IR_RX <-- default
// 1          GPIO[0]
// 2          GPIO[10]
// 3          VIP_656_DATA_VAL
// 4          VIP_656_DATA[0]
// 5          VIP_656_CLK
// 6          VIP_656_DATA_EXT[1]
// 7          VIP_656_DATA_EXT[0]

#define  FLD_GPIO10_ALT_SEL        0x0000F000
// 0          GPIO[10] <-- default
// 1          GPIO[0]
// 2          GPIO[10]
// 3          VIP_656_DATA_VAL
// 4          VIP_656_DATA[0]
// 5          VIP_656_CLK
// 6          VIP_656_DATA_EXT[1]
// 7          VIP_656_DATA_EXT[0]

#define  FLD_GPIO2_ALT_SEL         0x00000F00
// 0          GPIO[2] <-- default
// 1          GPIO[1]
// 2          GPIO[10]
// 3          VIP_656_DATA_VAL
// 4          VIP_656_DATA[0]
// 5          VIP_656_CLK
// 6          VIP_656_DATA_EXT[1]
// 7          VIP_656_DATA_EXT[0]

#define  FLD_GPIO1_ALT_SEL         0x000000F0
// 0          GPIO[1] <-- default
// 1          GPIO[0]
// 2          GPIO[10]
// 3          VIP_656_DATA_VAL
// 4          VIP_656_DATA[0]
// 5          VIP_656_CLK
// 6          VIP_656_DATA_EXT[1]
// 7          VIP_656_DATA_EXT[0]

#define  FLD_GPIO0_ALT_SEL         0x0000000F
// 0          GPIO[0] <-- default
// 1          GPIO[1]
// 2          GPIO[10]
// 3          VIP_656_DATA_VAL
// 4          VIP_656_DATA[0]
// 5          VIP_656_CLK
// 6          VIP_656_DATA_EXT[1]
// 7          VIP_656_DATA_EXT[0]

#define  ALT_PIN_IN_SEL            0x110030	// Alternate GPIO input select

#define  FLD_GPIO10_ALT_IN_SEL     0x0000F000
// 0          GPIO[10] <-- default
// 1          IR_RX
// 2          IR_TX
// 3          AUX_PLL_CLK
// 4          IF_ATT_SEL
// 5          GPIO[0]
// 6          GPIO[1]
// 7          GPIO[2]

#define  FLD_GPIO2_ALT_IN_SEL      0x00000F00
// 0          GPIO[2] <-- default
// 1          IR_RX
// 2          IR_TX
// 3          AUX_PLL_CLK
// 4          IF_ATT_SEL

#define  FLD_GPIO1_ALT_IN_SEL      0x000000F0
// 0          GPIO[1] <-- default
// 1          IR_RX
// 2          IR_TX
// 3          AUX_PLL_CLK
// 4          IF_ATT_SEL

#define  FLD_GPIO0_ALT_IN_SEL      0x0000000F
// 0          GPIO[0] <-- default
// 1          IR_RX
// 2          IR_TX
// 3          AUX_PLL_CLK
// 4          IF_ATT_SEL

//*****************************************************************************
#define  TEST_BUS_CTL1             0x110040	// Test bus control register #1

//*****************************************************************************
#define  TEST_BUS_CTL2             0x110044	// Test bus control register #2

//*****************************************************************************
#define  CLK_DELAY                 0x110048	// Clock delay
#define  FLD_MOE_CLK_DIS           0x80000000	// Disable MoE clock

//*****************************************************************************
#define  PAD_CTRL                  0x110068	// Pad drive strength control

//*****************************************************************************
#define  MBIST_CTRL                0x110050	// SRAM memory built-in self test control

//*****************************************************************************
#define  MBIST_STAT                0x110054	// SRAM memory built-in self test status

//*****************************************************************************
// PLL registers
//*****************************************************************************
#define  PLL_A_INT_FRAC            0x110088
#define  PLL_A_POST_STAT_BIST      0x11008C
#define  PLL_B_INT_FRAC            0x110090
#define  PLL_B_POST_STAT_BIST      0x110094
#define  PLL_C_INT_FRAC            0x110098
#define  PLL_C_POST_STAT_BIST      0x11009C
#define  PLL_D_INT_FRAC            0x1100A0
#define  PLL_D_POST_STAT_BIST      0x1100A4

#define  CLK_RST                   0x11002C
#define  FLD_VID_I_CLK_NOE         0x00001000
#define  FLD_VID_J_CLK_NOE         0x00002000
#define  FLD_USE_ALT_PLL_REF       0x00004000

#define  VID_CH_MODE_SEL           0x110078
#define  VID_CH_CLK_SEL            0x11007C

//*****************************************************************************
#define  VBI_A_DMA                 0x130008	// VBI A DMA data port

//*****************************************************************************
#define  VID_A_VIP_CTL             0x130080	// Video A VIP format control
#define  FLD_VIP_MODE              0x00000001

//*****************************************************************************
#define  VID_A_PIXEL_FRMT          0x130084	// Video A pixel format
#define  FLD_VID_A_GAMMA_DIS       0x00000008
#define  FLD_VID_A_FORMAT          0x00000007
#define  FLD_VID_A_GAMMA_FACTOR    0x00000010

//*****************************************************************************
#define  VID_A_VBI_CTL             0x130088	// Video A VBI miscellaneous control
#define  FLD_VID_A_VIP_EXT         0x00000003

//*****************************************************************************
#define  VID_B_DMA                 0x130100	// Video B DMA data port

//*****************************************************************************
#define  VBI_B_DMA                 0x130108	// VBI B DMA data port

//*****************************************************************************
#define  VID_B_SRC_SEL             0x130144	// Video B source select
#define  FLD_VID_B_SRC_SEL         0x00000000

//*****************************************************************************
#define  VID_B_LNGTH               0x130150	// Video B line length
#define  FLD_VID_B_LN_LNGTH        0x00000FFF

//*****************************************************************************
#define  VID_B_VIP_CTL             0x130180	// Video B VIP format control

//*****************************************************************************
#define  VID_B_PIXEL_FRMT          0x130184	// Video B pixel format
#define  FLD_VID_B_GAMMA_DIS       0x00000008
#define  FLD_VID_B_FORMAT          0x00000007
#define  FLD_VID_B_GAMMA_FACTOR    0x00000010

//*****************************************************************************
#define  VID_C_DMA                 0x130200	// Video C DMA data port

//*****************************************************************************
#define  VID_C_LNGTH               0x130250	// Video C line length
#define  FLD_VID_C_LN_LNGTH        0x00000FFF

//*****************************************************************************
// Video Destination Channels
//*****************************************************************************

#define  VID_DST_A_GPCNT           0x130020	// Video A general purpose counter
#define  VID_DST_B_GPCNT           0x130120	// Video B general purpose counter
#define  VID_DST_C_GPCNT           0x130220	// Video C general purpose counter
#define  VID_DST_D_GPCNT           0x130320	// Video D general purpose counter
#define  VID_DST_E_GPCNT           0x130420	// Video E general purpose counter
#define  VID_DST_F_GPCNT           0x130520	// Video F general purpose counter
#define  VID_DST_G_GPCNT           0x130620	// Video G general purpose counter
#define  VID_DST_H_GPCNT           0x130720	// Video H general purpose counter

//*****************************************************************************

#define  VID_DST_A_GPCNT_CTL       0x130030	// Video A general purpose control
#define  VID_DST_B_GPCNT_CTL       0x130130	// Video B general purpose control
#define  VID_DST_C_GPCNT_CTL       0x130230	// Video C general purpose control
#define  VID_DST_D_GPCNT_CTL       0x130330	// Video D general purpose control
#define  VID_DST_E_GPCNT_CTL       0x130430	// Video E general purpose control
#define  VID_DST_F_GPCNT_CTL       0x130530	// Video F general purpose control
#define  VID_DST_G_GPCNT_CTL       0x130630	// Video G general purpose control
#define  VID_DST_H_GPCNT_CTL       0x130730	// Video H general purpose control

//*****************************************************************************

#define  VID_DST_A_DMA_CTL         0x130040	// Video A DMA control
#define  VID_DST_B_DMA_CTL         0x130140	// Video B DMA control
#define  VID_DST_C_DMA_CTL         0x130240	// Video C DMA control
#define  VID_DST_D_DMA_CTL         0x130340	// Video D DMA control
#define  VID_DST_E_DMA_CTL         0x130440	// Video E DMA control
#define  VID_DST_F_DMA_CTL         0x130540	// Video F DMA control
#define  VID_DST_G_DMA_CTL         0x130640	// Video G DMA control
#define  VID_DST_H_DMA_CTL         0x130740	// Video H DMA control

#define  FLD_VID_RISC_EN           0x00000010
#define  FLD_VID_FIFO_EN           0x00000001

//*****************************************************************************

#define  VID_DST_A_VIP_CTL         0x130080	// Video A VIP control
#define  VID_DST_B_VIP_CTL         0x130180	// Video B VIP control
#define  VID_DST_C_VIP_CTL         0x130280	// Video C VIP control
#define  VID_DST_D_VIP_CTL         0x130380	// Video D VIP control
#define  VID_DST_E_VIP_CTL         0x130480	// Video E VIP control
#define  VID_DST_F_VIP_CTL         0x130580	// Video F VIP control
#define  VID_DST_G_VIP_CTL         0x130680	// Video G VIP control
#define  VID_DST_H_VIP_CTL         0x130780	// Video H VIP control

//*****************************************************************************

#define  VID_DST_A_PIX_FRMT        0x130084	// Video A Pixel format
#define  VID_DST_B_PIX_FRMT        0x130184	// Video B Pixel format
#define  VID_DST_C_PIX_FRMT        0x130284	// Video C Pixel format
#define  VID_DST_D_PIX_FRMT        0x130384	// Video D Pixel format
#define  VID_DST_E_PIX_FRMT        0x130484	// Video E Pixel format
#define  VID_DST_F_PIX_FRMT        0x130584	// Video F Pixel format
#define  VID_DST_G_PIX_FRMT        0x130684	// Video G Pixel format
#define  VID_DST_H_PIX_FRMT        0x130784	// Video H Pixel format

//*****************************************************************************
// Video Source Channels
//*****************************************************************************

#define  VID_SRC_A_GPCNT_CTL       0x130804	// Video A general purpose control
#define  VID_SRC_B_GPCNT_CTL       0x130904	// Video B general purpose control
#define  VID_SRC_C_GPCNT_CTL       0x130A04	// Video C general purpose control
#define  VID_SRC_D_GPCNT_CTL       0x130B04	// Video D general purpose control
#define  VID_SRC_E_GPCNT_CTL       0x130C04	// Video E general purpose control
#define  VID_SRC_F_GPCNT_CTL       0x130D04	// Video F general purpose control
#define  VID_SRC_I_GPCNT_CTL       0x130E04	// Video I general purpose control
#define  VID_SRC_J_GPCNT_CTL       0x130F04	// Video J general purpose control

//*****************************************************************************

#define  VID_SRC_A_GPCNT           0x130808	// Video A general purpose counter
#define  VID_SRC_B_GPCNT           0x130908	// Video B general purpose counter
#define  VID_SRC_C_GPCNT           0x130A08	// Video C general purpose counter
#define  VID_SRC_D_GPCNT           0x130B08	// Video D general purpose counter
#define  VID_SRC_E_GPCNT           0x130C08	// Video E general purpose counter
#define  VID_SRC_F_GPCNT           0x130D08	// Video F general purpose counter
#define  VID_SRC_I_GPCNT           0x130E08	// Video I general purpose counter
#define  VID_SRC_J_GPCNT           0x130F08	// Video J general purpose counter

//*****************************************************************************

#define  VID_SRC_A_DMA_CTL         0x13080C	// Video A DMA control
#define  VID_SRC_B_DMA_CTL         0x13090C	// Video B DMA control
#define  VID_SRC_C_DMA_CTL         0x130A0C	// Video C DMA control
#define  VID_SRC_D_DMA_CTL         0x130B0C	// Video D DMA control
#define  VID_SRC_E_DMA_CTL         0x130C0C	// Video E DMA control
#define  VID_SRC_F_DMA_CTL         0x130D0C	// Video F DMA control
#define  VID_SRC_I_DMA_CTL         0x130E0C	// Video I DMA control
#define  VID_SRC_J_DMA_CTL         0x130F0C	// Video J DMA control

#define  FLD_APB_RISC_EN           0x00000010
#define  FLD_APB_FIFO_EN           0x00000001

//*****************************************************************************

#define  VID_SRC_A_FMT_CTL         0x130810	// Video A format control
#define  VID_SRC_B_FMT_CTL         0x130910	// Video B format control
#define  VID_SRC_C_FMT_CTL         0x130A10	// Video C format control
#define  VID_SRC_D_FMT_CTL         0x130B10	// Video D format control
#define  VID_SRC_E_FMT_CTL         0x130C10	// Video E format control
#define  VID_SRC_F_FMT_CTL         0x130D10	// Video F format control
#define  VID_SRC_I_FMT_CTL         0x130E10	// Video I format control
#define  VID_SRC_J_FMT_CTL         0x130F10	// Video J format control

//*****************************************************************************

#define  VID_SRC_A_ACTIVE_CTL1     0x130814	// Video A active control      1
#define  VID_SRC_B_ACTIVE_CTL1     0x130914	// Video B active control      1
#define  VID_SRC_C_ACTIVE_CTL1     0x130A14	// Video C active control      1
#define  VID_SRC_D_ACTIVE_CTL1     0x130B14	// Video D active control      1
#define  VID_SRC_E_ACTIVE_CTL1     0x130C14	// Video E active control      1
#define  VID_SRC_F_ACTIVE_CTL1     0x130D14	// Video F active control      1
#define  VID_SRC_I_ACTIVE_CTL1     0x130E14	// Video I active control      1
#define  VID_SRC_J_ACTIVE_CTL1     0x130F14	// Video J active control      1

//*****************************************************************************

#define  VID_SRC_A_ACTIVE_CTL2     0x130818	// Video A active control      2
#define  VID_SRC_B_ACTIVE_CTL2     0x130918	// Video B active control      2
#define  VID_SRC_C_ACTIVE_CTL2     0x130A18	// Video C active control      2
#define  VID_SRC_D_ACTIVE_CTL2     0x130B18	// Video D active control      2
#define  VID_SRC_E_ACTIVE_CTL2     0x130C18	// Video E active control      2
#define  VID_SRC_F_ACTIVE_CTL2     0x130D18	// Video F active control      2
#define  VID_SRC_I_ACTIVE_CTL2     0x130E18	// Video I active control      2
#define  VID_SRC_J_ACTIVE_CTL2     0x130F18	// Video J active control      2

//*****************************************************************************

#define  VID_SRC_A_CDT_SZ          0x13081C	// Video A CDT size
#define  VID_SRC_B_CDT_SZ          0x13091C	// Video B CDT size
#define  VID_SRC_C_CDT_SZ          0x130A1C	// Video C CDT size
#define  VID_SRC_D_CDT_SZ          0x130B1C	// Video D CDT size
#define  VID_SRC_E_CDT_SZ          0x130C1C	// Video E CDT size
#define  VID_SRC_F_CDT_SZ          0x130D1C	// Video F CDT size
#define  VID_SRC_I_CDT_SZ          0x130E1C	// Video I CDT size
#define  VID_SRC_J_CDT_SZ          0x130F1C	// Video J CDT size

//*****************************************************************************
// Audio I/F
//*****************************************************************************
#define  AUD_DST_A_DMA             0x140000	// Audio Int A DMA data port
#define  AUD_SRC_A_DMA             0x140008	// Audio Int A DMA data port

#define  AUD_A_GPCNT               0x140010	// Audio Int A gp counter
#define  FLD_AUD_A_GP_CNT          0x0000FFFF

#define  AUD_A_GPCNT_CTL           0x140014	// Audio Int A gp control

#define  AUD_A_LNGTH               0x140018	// Audio Int A line length

#define  AUD_A_CFG                 0x14001C	// Audio Int A configuration

//*****************************************************************************
#define  AUD_DST_B_DMA             0x140100	// Audio Int B DMA data port
#define  AUD_SRC_B_DMA             0x140108	// Audio Int B DMA data port

#define  AUD_B_GPCNT               0x140110	// Audio Int B gp counter
#define  FLD_AUD_B_GP_CNT          0x0000FFFF

#define  AUD_B_GPCNT_CTL           0x140114	// Audio Int B gp control

#define  AUD_B_LNGTH               0x140118	// Audio Int B line length

#define  AUD_B_CFG                 0x14011C	// Audio Int B configuration

//*****************************************************************************
#define  AUD_DST_C_DMA             0x140200	// Audio Int C DMA data port
#define  AUD_SRC_C_DMA             0x140208	// Audio Int C DMA data port

#define  AUD_C_GPCNT               0x140210	// Audio Int C gp counter
#define  FLD_AUD_C_GP_CNT          0x0000FFFF

#define  AUD_C_GPCNT_CTL           0x140214	// Audio Int C gp control

#define  AUD_C_LNGTH               0x140218	// Audio Int C line length

#define  AUD_C_CFG                 0x14021C	// Audio Int C configuration

//*****************************************************************************
#define  AUD_DST_D_DMA             0x140300	// Audio Int D DMA data port
#define  AUD_SRC_D_DMA             0x140308	// Audio Int D DMA data port

#define  AUD_D_GPCNT               0x140310	// Audio Int D gp counter
#define  FLD_AUD_D_GP_CNT          0x0000FFFF

#define  AUD_D_GPCNT_CTL           0x140314	// Audio Int D gp control

#define  AUD_D_LNGTH               0x140318	// Audio Int D line length

#define  AUD_D_CFG                 0x14031C	// Audio Int D configuration

//*****************************************************************************
#define  AUD_SRC_E_DMA             0x140400	// Audio Int E DMA data port

#define  AUD_E_GPCNT               0x140410	// Audio Int E gp counter
#define  FLD_AUD_E_GP_CNT          0x0000FFFF

#define  AUD_E_GPCNT_CTL           0x140414	// Audio Int E gp control

#define  AUD_E_CFG                 0x14041C	// Audio Int E configuration

//*****************************************************************************

#define  FLD_AUD_DST_LN_LNGTH      0x00000FFF

#define  FLD_AUD_DST_PK_MODE       0x00004000

#define  FLD_AUD_CLK_ENABLE        0x00000200

#define  FLD_AUD_MASTER_MODE       0x00000002

#define  FLD_AUD_SONY_MODE         0x00000001

#define  FLD_AUD_CLK_SELECT_PLL_D  0x00001800

#define  FLD_AUD_DST_ENABLE        0x00020000

#define  FLD_AUD_SRC_ENABLE        0x00010000

//*****************************************************************************
#define  AUD_INT_DMA_CTL           0x140500	// Audio Int DMA control

#define  FLD_AUD_SRC_E_RISC_EN     0x00008000
#define  FLD_AUD_SRC_C_RISC_EN     0x00004000
#define  FLD_AUD_SRC_B_RISC_EN     0x00002000
#define  FLD_AUD_SRC_A_RISC_EN     0x00001000

#define  FLD_AUD_DST_D_RISC_EN     0x00000800
#define  FLD_AUD_DST_C_RISC_EN     0x00000400
#define  FLD_AUD_DST_B_RISC_EN     0x00000200
#define  FLD_AUD_DST_A_RISC_EN     0x00000100

#define  FLD_AUD_SRC_E_FIFO_EN     0x00000080
#define  FLD_AUD_SRC_C_FIFO_EN     0x00000040
#define  FLD_AUD_SRC_B_FIFO_EN     0x00000020
#define  FLD_AUD_SRC_A_FIFO_EN     0x00000010

#define  FLD_AUD_DST_D_FIFO_EN     0x00000008
#define  FLD_AUD_DST_C_FIFO_EN     0x00000004
#define  FLD_AUD_DST_B_FIFO_EN     0x00000002
#define  FLD_AUD_DST_A_FIFO_EN     0x00000001

//*****************************************************************************
//
//                   Mobilygen Interface Registers
//
//*****************************************************************************
// Mobilygen Interface A
//*****************************************************************************
#define  MB_IF_A_DMA               0x150000	// MBIF A DMA data port
#define  MB_IF_A_GPCN              0x150008	// MBIF A GP counter
#define  MB_IF_A_GPCN_CTRL         0x15000C
#define  MB_IF_A_DMA_CTRL          0x150010
#define  MB_IF_A_LENGTH            0x150014
#define  MB_IF_A_HDMA_XFER_SZ      0x150018
#define  MB_IF_A_HCMD              0x15001C
#define  MB_IF_A_HCONFIG           0x150020
#define  MB_IF_A_DATA_STRUCT_0     0x150024
#define  MB_IF_A_DATA_STRUCT_1     0x150028
#define  MB_IF_A_DATA_STRUCT_2     0x15002C
#define  MB_IF_A_DATA_STRUCT_3     0x150030
#define  MB_IF_A_DATA_STRUCT_4     0x150034
#define  MB_IF_A_DATA_STRUCT_5     0x150038
#define  MB_IF_A_DATA_STRUCT_6     0x15003C
#define  MB_IF_A_DATA_STRUCT_7     0x150040
#define  MB_IF_A_DATA_STRUCT_8     0x150044
#define  MB_IF_A_DATA_STRUCT_9     0x150048
#define  MB_IF_A_DATA_STRUCT_A     0x15004C
#define  MB_IF_A_DATA_STRUCT_B     0x150050
#define  MB_IF_A_DATA_STRUCT_C     0x150054
#define  MB_IF_A_DATA_STRUCT_D     0x150058
#define  MB_IF_A_DATA_STRUCT_E     0x15005C
#define  MB_IF_A_DATA_STRUCT_F     0x150060
//*****************************************************************************
// Mobilygen Interface B
//*****************************************************************************
#define  MB_IF_B_DMA               0x160000	// MBIF A DMA data port
#define  MB_IF_B_GPCN              0x160008	// MBIF A GP counter
#define  MB_IF_B_GPCN_CTRL         0x16000C
#define  MB_IF_B_DMA_CTRL          0x160010
#define  MB_IF_B_LENGTH            0x160014
#define  MB_IF_B_HDMA_XFER_SZ      0x160018
#define  MB_IF_B_HCMD              0x16001C
#define  MB_IF_B_HCONFIG           0x160020
#define  MB_IF_B_DATA_STRUCT_0     0x160024
#define  MB_IF_B_DATA_STRUCT_1     0x160028
#define  MB_IF_B_DATA_STRUCT_2     0x16002C
#define  MB_IF_B_DATA_STRUCT_3     0x160030
#define  MB_IF_B_DATA_STRUCT_4     0x160034
#define  MB_IF_B_DATA_STRUCT_5     0x160038
#define  MB_IF_B_DATA_STRUCT_6     0x16003C
#define  MB_IF_B_DATA_STRUCT_7     0x160040
#define  MB_IF_B_DATA_STRUCT_8     0x160044
#define  MB_IF_B_DATA_STRUCT_9     0x160048
#define  MB_IF_B_DATA_STRUCT_A     0x16004C
#define  MB_IF_B_DATA_STRUCT_B     0x160050
#define  MB_IF_B_DATA_STRUCT_C     0x160054
#define  MB_IF_B_DATA_STRUCT_D     0x160058
#define  MB_IF_B_DATA_STRUCT_E     0x16005C
#define  MB_IF_B_DATA_STRUCT_F     0x160060

// MB_DMA_CTRL
#define  FLD_MB_IF_RISC_EN         0x00000010
#define  FLD_MB_IF_FIFO_EN         0x00000001

// MB_LENGTH
#define  FLD_MB_IF_LN_LNGTH        0x00000FFF

// MB_HCMD register
#define  FLD_MB_HCMD_H_GO          0x80000000
#define  FLD_MB_HCMD_H_BUSY        0x40000000
#define  FLD_MB_HCMD_H_DMA_HOLD    0x10000000
#define  FLD_MB_HCMD_H_DMA_BUSY    0x08000000
#define  FLD_MB_HCMD_H_DMA_TYPE    0x04000000
#define  FLD_MB_HCMD_H_DMA_XACT    0x02000000
#define  FLD_MB_HCMD_H_RW_N        0x01000000
#define  FLD_MB_HCMD_H_ADDR        0x00FF0000
#define  FLD_MB_HCMD_H_DATA        0x0000FFFF

//*****************************************************************************
// I2C #1
//*****************************************************************************
#define  I2C1_ADDR                 0x180000	// I2C #1 address
#define  FLD_I2C_DADDR             0xfe000000	// RW [31:25] I2C Device Address
						 // RO [24] reserved
//*****************************************************************************
#define  FLD_I2C_SADDR             0x00FFFFFF	// RW [23:0]  I2C Sub-address

//*****************************************************************************
#define  I2C1_WDATA                0x180004	// I2C #1 write data
#define  FLD_I2C_WDATA             0xFFFFFFFF	// RW [31:0]

//*****************************************************************************
#define  I2C1_CTRL                 0x180008	// I2C #1 control
#define  FLD_I2C_PERIOD            0xFF000000	// RW [31:24]
#define  FLD_I2C_SCL_IN            0x00200000	// RW [21]
#define  FLD_I2C_SDA_IN            0x00100000	// RW [20]
						 // RO [19:18] reserved
#define  FLD_I2C_SCL_OUT           0x00020000	// RW [17]
#define  FLD_I2C_SDA_OUT           0x00010000	// RW [16]
						 // RO [15] reserved
#define  FLD_I2C_DATA_LEN          0x00007000	// RW [14:12]
#define  FLD_I2C_SADDR_INC         0x00000800	// RW [11]
						 // RO [10:9] reserved
#define  FLD_I2C_SADDR_LEN         0x00000300	// RW [9:8]
						 // RO [7:6] reserved
#define  FLD_I2C_SOFT              0x00000020	// RW [5]
#define  FLD_I2C_NOSTOP            0x00000010	// RW [4]
#define  FLD_I2C_EXTEND            0x00000008	// RW [3]
#define  FLD_I2C_SYNC              0x00000004	// RW [2]
#define  FLD_I2C_READ_SA           0x00000002	// RW [1]
#define  FLD_I2C_READ_WRN          0x00000001	// RW [0]

//*****************************************************************************
#define  I2C1_RDATA                0x18000C	// I2C #1 read data
#define  FLD_I2C_RDATA             0xFFFFFFFF	// RO [31:0]

//*****************************************************************************
#define  I2C1_STAT                 0x180010	// I2C #1 status
#define  FLD_I2C_XFER_IN_PROG      0x00000002	// RO [1]
#define  FLD_I2C_RACK              0x00000001	// RO [0]

//*****************************************************************************
// I2C #2
//*****************************************************************************
#define  I2C2_ADDR                 0x190000	// I2C #2 address

//*****************************************************************************
#define  I2C2_WDATA                0x190004	// I2C #2 write data

//*****************************************************************************
#define  I2C2_CTRL                 0x190008	// I2C #2 control

//*****************************************************************************
#define  I2C2_RDATA                0x19000C	// I2C #2 read data

//*****************************************************************************
#define  I2C2_STAT                 0x190010	// I2C #2 status

//*****************************************************************************
// I2C #3
//*****************************************************************************
#define  I2C3_ADDR                 0x1A0000	// I2C #3 address

//*****************************************************************************
#define  I2C3_WDATA                0x1A0004	// I2C #3 write data

//*****************************************************************************
#define  I2C3_CTRL                 0x1A0008	// I2C #3 control

//*****************************************************************************
#define  I2C3_RDATA                0x1A000C	// I2C #3 read data

//*****************************************************************************
#define  I2C3_STAT                 0x1A0010	// I2C #3 status

//*****************************************************************************
// UART
//*****************************************************************************
#define  UART_CTL                  0x1B0000	// UART Control Register
#define  FLD_LOOP_BACK_EN          (1 << 7)	// RW field - default 0
#define  FLD_RX_TRG_SZ             (3 << 2)	// RW field - default 0
#define  FLD_RX_EN                 (1 << 1)	// RW field - default 0
#define  FLD_TX_EN                 (1 << 0)	// RW field - default 0

//*****************************************************************************
#define  UART_BRD                  0x1B0004	// UART Baud Rate Divisor
#define  FLD_BRD                   0x0000FFFF	// RW field - default 0x197

//*****************************************************************************
#define  UART_DBUF                 0x1B0008	// UART Tx/Rx Data BuFFer
#define  FLD_DB                    0xFFFFFFFF	// RW field - default 0

//*****************************************************************************
#define  UART_ISR                  0x1B000C	// UART Interrupt Status
#define  FLD_RXD_TIMEOUT_EN        (1 << 7)	// RW field - default 0
#define  FLD_FRM_ERR_EN            (1 << 6)	// RW field - default 0
#define  FLD_RXD_RDY_EN            (1 << 5)	// RW field - default 0
#define  FLD_TXD_EMPTY_EN          (1 << 4)	// RW field - default 0
#define  FLD_RXD_OVERFLOW          (1 << 3)	// RW field - default 0
#define  FLD_FRM_ERR               (1 << 2)	// RW field - default 0
#define  FLD_RXD_RDY               (1 << 1)	// RW field - default 0
#define  FLD_TXD_EMPTY             (1 << 0)	// RW field - default 0

//*****************************************************************************
#define  UART_CNT                  0x1B0010	// UART Tx/Rx FIFO Byte Count
#define  FLD_TXD_CNT               (0x1F << 8)	// RW field - default 0
#define  FLD_RXD_CNT               (0x1F << 0)	// RW field - default 0

//*****************************************************************************
// Motion Detection
#define  MD_CH0_GRID_BLOCK_YCNT    0x170014
#define  MD_CH1_GRID_BLOCK_YCNT    0x170094
#define  MD_CH2_GRID_BLOCK_YCNT    0x170114
#define  MD_CH3_GRID_BLOCK_YCNT    0x170194
#define  MD_CH4_GRID_BLOCK_YCNT    0x170214
#define  MD_CH5_GRID_BLOCK_YCNT    0x170294
#define  MD_CH6_GRID_BLOCK_YCNT    0x170314
#define  MD_CH7_GRID_BLOCK_YCNT    0x170394

#define PIXEL_FRMT_422    4
#define PIXEL_FRMT_411    5
#define PIXEL_FRMT_Y8     6

#define PIXEL_ENGINE_VIP1 0
#define PIXEL_ENGINE_VIP2 1

#endif //Athena_REGISTERS
